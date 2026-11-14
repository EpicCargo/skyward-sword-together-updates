#!/usr/bin/env python3
"""Show three simulated teammates beside one native game; optional UDP movement via --udp."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import selectors
import socket
import struct
import time
import zlib
from fixture_json import write_json_snapshot

POSE = struct.Struct("<I3f3hHfHHHBB")
ROOT = Path(__file__).resolve().parent.parent
PROTOCOL = int(re.search(r"^#define SST_PROTO (\d+)$", (ROOT / "client/src/sst_net.h").read_text(), re.M)[1])


def string(value):
    encoded = value.encode()
    if len(encoded) >= 64:
        raise ValueError("names, rooms and teams must use fewer than 64 UTF-8 bytes")
    return bytes([len(encoded)]) + encoded


def frame(kind, body=b""):
    return struct.pack("<IB", len(body) + 1, kind) + body


def read_string(data, offset):
    length = data[offset]
    end = offset + 1 + length
    if end > len(data):
        raise ValueError("truncated host string")
    return data[offset + 1:end].decode(), end


class UdpChannel:
    """Small pose-only test adapter. TCP still owns joins, scenes and recovery."""
    def __init__(self, tcp):
        self.sock = socket.socket(tcp.family, socket.SOCK_DGRAM)
        self.sock.connect(tcp.getpeername())
        self.sock.setblocking(False)
        self.token = None
        self.sequence = self.rx_sequence = self.ack_sequence = 0
        self.cookie = self.epoch = self.probe = 0
        self.probes = {}
        self.epochs = {}
        self.last_probe = self.last_ack = 0
        self.sent = self.received = self.fallbacks = 0
        self.was_active = False

    def active(self, now):
        return self.epoch and self.last_ack and now-self.last_ack < 1.5

    def send(self, kind, body):
        self.sequence = (self.sequence+1) & 0xffffffff
        head = b"SSU1"+self.token+struct.pack("<IB", self.sequence, kind)
        wire = head+struct.pack("<I", zlib.crc32(head+body))+body
        try:
            self.sock.send(wire)
            self.sent += 1
        except (BlockingIOError, ConnectionRefusedError, ConnectionResetError):
            pass

    def scene(self):
        self.epoch = 0
        self.cookie += 1
        return frame(10, struct.pack("<I", self.cookie))

    def context(self, body, own_id):
        if len(body) != 10:
            return
        who, cookie, epoch = struct.unpack("<HII", body)
        if who == own_id:
            if cookie == self.cookie:
                self.epoch = epoch
        else:
            self.epochs[who] = epoch

    def poll(self, now):
        outgoing, poses = b"", []
        if not self.token:
            return outgoing, poses
        if now-self.last_probe >= .5:
            self.last_probe = now
            self.probe += 1
            self.probes[self.probe] = now
            self.probes = {k:v for k,v in self.probes.items() if now-v < 1.5}
            self.send(0, struct.pack("<I", self.probe))
        while True:
            try:
                wire = self.sock.recv(193)
            except (BlockingIOError, ConnectionRefusedError, ConnectionResetError):
                break
            if (not 29 <= len(wire) <= 192 or wire[:4] != b"SSU1" or wire[4:20] != self.token or
                    struct.unpack_from("<I",wire,25)[0] != zlib.crc32(wire[:25]+wire[29:])):
                continue
            seq, kind = struct.unpack_from("<IB",wire,20)
            last = self.ack_sequence if kind == 0 else self.rx_sequence
            delta = (seq-last) & 0xffffffff
            if not 0 < delta < 0x80000000:
                continue
            body = wire[29:]
            if kind == 0 and len(body) == 4:
                nonce, = struct.unpack("<I",body)
                if nonce in self.probes and now-self.probes[nonce] < 1.5:
                    self.ack_sequence = seq
                    self.last_ack = now
                    outgoing += frame(9, body)
                    self.received += 1
            elif kind == 0x11 and len(body) == 46:
                who, source, target = struct.unpack_from("<HII",body)
                if self.epoch and target == self.epoch and source and self.epochs.get(who) == source:
                    self.rx_sequence = seq
                    self.received += 1
                    poses.append((who, POSE.unpack(body[10:])))
        active = bool(self.active(now))
        if self.was_active and not active:
            self.fallbacks += 1
            outgoing += frame(9, struct.pack("<I",0))
        self.was_active = active
        return outgoing, poses


def animation(pose, override, elapsed):
    """Mirror the source by default, or independently drive a clip for renderer tests."""
    if override is None:
        return pose[7], pose[8], pose[9]
    clip = override["id"]
    phase = override.get("frame", 0)
    rate = override.get("rate", 0)
    length = override.get("length", 0)
    action = override.get("action", 0)
    if (not isinstance(clip, int) or not (0 <= clip < 443 or clip == 65535) or
            not isinstance(action, int) or not 0 <= action <= 65535 or
            any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in [phase, rate, length]) or
            not 0 <= phase <= 65536 or not 0 <= rate <= 240 or not 0 <= length <= 65536):
        raise ValueError("animation requires a clip ID, finite frame/rate/length and a 16-bit action")
    phase += elapsed * rate
    if length:
        phase %= length
    return clip, min(phase, 65536), action


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7623)
    parser.add_argument("--follow-name", required=True, help="exact name of the native player")
    parser.add_argument("--room", default="")
    parser.add_argument("--team", default="")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--udp", action="store_true", help="use the built-in host's optional UDP movement channel")
    parser.add_argument("--state", type=Path, help="optional JSON status file")
    parser.add_argument("--control", type=Path, help="optional JSON: active, positions (three XYZs), scene, animations (three id/frame/rate/length/action objects)")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0 or not 1 <= args.port <= 65535:
        parser.error("use a finite positive duration and a valid TCP port")
    room, team = string(args.room), string(args.team)
    peers = []
    source = None
    scene = None
    pose = None
    pose_time = 0
    last_sent_scene = None
    tick = 0
    last_tick = last_ping = last_state = 0
    start = time.monotonic()
    with selectors.DefaultSelector() as selector:
        try:
            for index in range(3):
                sock = socket.create_connection((args.host, args.port), timeout=3)
                sock.setblocking(False)
                peer = dict(sock=sock, index=index, id=None, buffer=b"", outgoing=frame(
                    1, struct.pack("<H", PROTOCOL) + string(f"Simulated {index + 1}") + room + team + os.urandom(16)))
                peers.append(peer)
                peer["udp"] = UdpChannel(sock) if args.udp else None
                selector.register(sock, selectors.EVENT_READ, peer)

            while time.monotonic() - start < args.seconds:
                now = time.monotonic()
                for peer in peers:
                    if peer["udp"]:
                        reliable, samples = peer["udp"].poll(now)
                        peer["outgoing"] += reliable
                        for who, sample in samples:
                            if who == source and (pose is None or 0 < ((sample[0]-pose[0]) & 0xffffffff) < 0x80000000):
                                pose, pose_time = sample, now
                if now - last_ping > 1:
                    last_ping = now
                    for peer in peers:
                        peer["outgoing"] += frame(6, struct.pack("<I", tick))
                control = {}
                if args.control and args.control.exists():
                    control = json.loads(args.control.read_text())
                target_scene = scene
                if "scene" in control:
                    area = control["scene"]
                    target_scene = string(area["stage"]) + bytes([area["room"], area["layer"], area["entrance"]])
                if target_scene is not None and target_scene != last_sent_scene:
                    last_sent_scene = target_scene
                    for peer in peers:
                        peer["outgoing"] += frame(0x10, target_scene) + frame(0x21)
                        if peer["udp"]:
                            peer["outgoing"] += peer["udp"].scene()
                if pose is not None and now - pose_time < 1 and now - last_tick >= 1 / 30 and control.get("active", True):
                    last_tick = now
                    tick = (tick + 1) & 0xffffffff
                    x, y, z = pose[1:4]
                    yaw = pose[5]
                    angle = yaw * 2 * math.pi / 65536
                    positions = control.get("positions")
                    animations = control.get("animations")
                    if animations is not None and len(animations) != 3:
                        raise ValueError("animations must contain three entries")
                    for peer in peers:
                        index = peer["index"]
                        offset = (index - 1) * 125
                        position = positions[index] if positions else (
                            x + offset * math.cos(angle) + 170 * math.sin(angle), y,
                            z - offset * math.sin(angle) + 170 * math.cos(angle))
                        if len(position) != 3 or any(not math.isfinite(v) or abs(v) > 1e6 for v in position):
                            raise ValueError("peer positions must be three finite game coordinates")
                        facing = ((yaw + index * 10000 + 32768) % 65536) - 32768
                        clip, phase, action = animation(pose, animations[index] if animations else None, now - start)
                        body = POSE.pack(tick, *position, 0, facing, 0, clip, phase, action, 16 + 4 * index, 24, 0, 0)
                        udp = peer["udp"]
                        if udp and udp.active(now):
                            udp.send(0x11, struct.pack("<I",udp.epoch)+body)
                        else:
                            peer["outgoing"] += frame(0x11, body)
                if args.state and now - last_state >= 1:
                    last_state = now
                    write_json_snapshot(args.state, dict(
                        source=source, scene=target_scene.hex() if target_scene else None,
                        peer_ids=[p["id"] for p in peers], poses=tick,
                        udp=[dict(active=bool(p["udp"].active(now)),sent=p["udp"].sent,
                                  received=p["udp"].received,fallbacks=p["udp"].fallbacks) for p in peers] if args.udp else [],
                        source_pos=list(pose[1:4]) if pose else None, source_yaw=pose[5] if pose else None,
                        source_animation=dict(id=pose[7], frame=pose[8], action=pose[9]) if pose else None))
                for peer in peers:
                    if peer["outgoing"]:
                        try:
                            sent = peer["sock"].send(peer["outgoing"])
                            peer["outgoing"] = peer["outgoing"][sent:]
                        except BlockingIOError:
                            pass
                for key, _event in selector.select(.01):
                    peer = key.data
                    data = peer["sock"].recv(65536)
                    if not data:
                        raise ConnectionError("host disconnected a simulated peer")
                    peer["buffer"] += data
                    while len(peer["buffer"]) >= 4:
                        length = struct.unpack_from("<I", peer["buffer"])[0]
                        if not 1 <= length <= 65536:
                            raise ValueError("invalid host frame length")
                        if len(peer["buffer"]) < length + 4:
                            break
                        kind, body = peer["buffer"][4], peer["buffer"][5:length + 4]
                        peer["buffer"] = peer["buffer"][length + 4:]
                        if kind == 2:
                            peer["id"] = struct.unpack_from("<H", body)[0]
                            # The Node relay lists existing players here; the embedded host uses JOIN.
                            offset = 3
                            for _ in range(body[2]):
                                who = struct.unpack_from("<H", body, offset)[0]
                                name, offset = read_string(body, offset + 2)
                                _team, offset = read_string(body, offset)
                                _stage, offset = read_string(body, offset)
                                offset += 1
                                if name == args.follow_name:
                                    source = who
                        elif kind == 3:
                            who = struct.unpack_from("<H", body)[0]
                            name = body[3:3 + body[2]].decode()
                            if name == args.follow_name:
                                source = who
                            if peer["udp"]:
                                peer["udp"].epochs.pop(who,None)
                        elif kind == 8 and len(body) == 16 and peer["udp"]:
                            peer["udp"].token = body
                        elif kind == 10 and peer["udp"]:
                            peer["udp"].context(body,peer["id"])
                        elif kind == 4 and struct.unpack_from("<H", body)[0] == source:
                            source, scene, pose = None, None, None
                        elif kind == 5:
                            raise ConnectionError(body[1:].decode(errors="replace"))
                        elif kind == 0x10 and struct.unpack_from("<H", body)[0] == source:
                            if peer["udp"]:
                                peer["udp"].epochs.pop(source,None)
                            if scene != body[2:]:
                                scene, pose = body[2:], None
                        elif kind == 0x11 and struct.unpack_from("<H", body)[0] == source:
                            sample = POSE.unpack(body[2:])
                            if pose is None or 0 < ((sample[0]-pose[0]) & 0xffffffff) < 0x80000000:
                                pose, pose_time = sample, now
        finally:
            for peer in peers:
                peer["sock"].close()
                if peer["udp"]:
                    peer["udp"].sock.close()


if __name__ == "__main__":
    main()
