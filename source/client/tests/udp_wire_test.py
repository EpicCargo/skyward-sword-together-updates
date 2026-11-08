"""Adversarial datagrams against the real embedded host, including identity/scene reuse."""
import json
import os
from pathlib import Path
import socket
import struct
import time
import zlib

from udp_e2e import Process, PROTO, ROOT


def string(text):
    b = text.encode()
    return bytes([len(b)]) + b


def datagram(token, sequence, kind, body=b""):
    head = b"SSU1" + token + struct.pack("<IB", sequence, kind)
    return head + struct.pack("<I", zlib.crc32(head + body)) + body


def pose(tick):
    return struct.pack("<I3f3hHfHHHBB", tick, 100, 200, 300, 0, 0, 0, 20, 1, 0, 24, 24, 0x20, 0)


class Peer:
    def __init__(self, port, session=None):
        self.tcp = socket.create_connection(("127.0.0.1", port), 3)
        self.tcp.settimeout(3)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.connect(("127.0.0.1", port))
        self.udp.settimeout(1)
        self.session = session or os.urandom(16)
        self.cookie = self.sequence = 0
        self.send(1, struct.pack("<H", PROTO) + string("Wire") + string("coop") + string("team") + self.session)
        self.id = struct.unpack_from("<H", self.until(2))[0]
        self.token = self.until(8)
        assert len(self.token) == 16

    def send(self, kind, body=b""):
        self.tcp.sendall(struct.pack("<IB", len(body)+1, kind) + body)

    def exact(self, count):
        out = bytearray()
        while len(out) < count:
            part = self.tcp.recv(count-len(out))
            assert part, "unexpected disconnect"
            out.extend(part)
        return out

    def receive(self):
        n, = struct.unpack("<I", self.exact(4))
        assert 1 <= n <= 512
        body = self.exact(n)
        return body[0], bytes(body[1:])

    def until(self, kind, predicate=lambda b: True):
        end = time.monotonic()+3
        while time.monotonic() < end:
            k, b = self.receive()
            assert k != 5, b
            if k == kind and predicate(b):
                return b
        raise AssertionError(kind)

    def barrier(self):
        self.send(6, b"sync")
        self.until(7, lambda b: b == b"sync")

    def scene(self, room=0):
        self.cookie += 1
        self.send(0x10, string("D000") + bytes([room, 0, 0]))
        self.send(10, struct.pack("<I", self.cookie))
        context = self.until(10, lambda b: struct.unpack_from("<HI", b) == (self.id, self.cookie))
        self.epoch = struct.unpack_from("<I", context, 6)[0]

    def probe(self):
        self.sequence += 1
        nonce = self.sequence
        self.udp.send(datagram(self.token, self.sequence, 0, struct.pack("<I", nonce)))
        while True:
            b = self.udp.recv(193)
            if b[24] == 0:
                assert b[29:] == struct.pack("<I", nonce)
                break
        self.send(9, struct.pack("<I", nonce))
        self.barrier()

    def live(self, tick, epoch=None, sequence=None):
        self.sequence += 1
        wire = datagram(self.token, self.sequence if sequence is None else sequence, 0x11,
                        struct.pack("<I", self.epoch if epoch is None else epoch) + pose(tick))
        self.udp.send(wire)
        return wire

    def movement(self, tick):
        end = time.monotonic()+2
        while time.monotonic() < end:
            b = self.udp.recv(193)
            if b[24] == 0x11:
                assert struct.unpack_from("<I", b, 39)[0] == tick, b.hex()
                return b
        raise AssertionError("no movement")

    def quiet(self):
        self.udp.settimeout(.12)
        try:
            b = self.udp.recv(193)
            raise AssertionError(("unexpected datagram", b.hex()))
        except socket.timeout:
            pass
        finally:
            self.udp.settimeout(1)

    def close(self):
        self.tcp.close()
        self.udp.close()


def main():
    host = Process(json.loads(os.environ.get("SST_TEST_HOST_COMMAND", "null")) or
                   [str(ROOT / "client/build/host_fixture")])
    port = int(host.line())
    peers = []
    try:
        a, b = Peer(port), Peer(port)
        peers += [a, b]
        for peer in peers:
            peer.scene()
            peer.probe()
        a.live(10)
        live = b.movement(10)
        assert struct.unpack_from("<HII", live, 29) == (a.id, a.epoch, b.epoch)
        # Duplicate, old sequence, wrong credentials, corrupted checksum and wrong scene.
        duplicate = a.live(11)
        b.movement(11)
        a.udp.send(duplicate)
        a.live(900, sequence=1)
        a.udp.send(datagram(b"x"*16, 999, 0x11, struct.pack("<I", a.epoch)+pose(901)))
        corrupt = bytearray(datagram(a.token, 999, 0x11, struct.pack("<I", a.epoch)+pose(902)))
        corrupt[-1] ^= 1
        a.udp.send(corrupt)
        invalid_pose = bytearray(pose(999))
        struct.pack_into("<f", invalid_pose, 4, float("nan"))
        a.sequence += 1
        a.udp.send(datagram(a.token, a.sequence, 0x11, struct.pack("<I", a.epoch)+invalid_pose))
        a.live(903, epoch=a.epoch+100)
        # Reliable events and malformed data cannot enter through UDP.
        for kind, body in [(0x20, struct.pack("<BHB", 1, 321, 1)), (0x23, struct.pack("<HH", 831, 176)),
                           (0x11, b"short"), (0x31, b"\xff"*80), (0x30, b"enemy")]:
            a.sequence += 1
            a.udp.send(datagram(a.token, a.sequence, kind, struct.pack("<I", a.epoch)+body))
        a.udp.send(b"short")
        a.udp.send(b"x"*1000)
        b.quiet()
        a.barrier()
        a.live(12)
        b.movement(12)
        # Sender scene incarnation protects a leave-and-return to the identical area.
        old_epoch = a.epoch
        a.scene(1)
        a.live(13)
        b.quiet()
        a.scene(0)
        assert a.epoch != old_epoch
        a.live(904, epoch=old_epoch)
        b.quiet()
        a.live(14)
        b.movement(14)
        # Receiver's scene incarnation is serialized too, so a delayed old packet is detectable.
        old_target = b.epoch
        b.scene(1)
        b.scene(0)
        a.live(15)
        assert struct.unpack_from("<I", b.movement(15), 35)[0] != old_target
        # Replacing a live connection preserves identity, rotates UDP credentials and scenes.
        replacement = Peer(port, session=a.session)
        peers.append(replacement)
        assert replacement.id == a.id and replacement.token != a.token
        replacement.scene()
        replacement.probe()
        a.live(905)
        b.quiet()
        replacement.live(1)
        b.movement(1)
        replacement.barrier()
        print("UDP wire PASS: replay, stale scenes, reconnect credentials, malformed/oversize/corrupt/spoofed packets, reliable-only message rejection")
    finally:
        for peer in peers:
            peer.close()
        host.close()


if __name__ == "__main__":
    main()
