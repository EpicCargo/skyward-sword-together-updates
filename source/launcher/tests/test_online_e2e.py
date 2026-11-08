"""Actual Worker+Node CF shim+Python tunnels+existing native C host, loopback only.

This exercises real HTTP/WebSocket/TLS and native protocol routing. It does NOT
exercise Cloudflare infrastructure, an account, a game, or generated game code.
"""
import json
import os
from pathlib import Path
import queue
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "launcher"))
import sst_online_transport as transport
import sst_online as online

# Keep candidate helper builds isolated from the selected source's build output.
HELPER = Path(os.environ.get("SST_TEST_SESSION_HOST", ROOT / "client/build/sst-session-host")).resolve()


def string(value):
    encoded = value.encode("ascii")
    return bytes((len(encoded),)) + encoded


def exact(sock, size):
    result = bytearray()
    while len(result) < size:
        data = sock.recv(size - len(result))
        if not data:
            raise EOFError("Local test peer closed")
        result.extend(data)
    return bytes(result)


def wait_for(predicate, seconds=3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(.01)
    return False


class Peer:
    def __init__(self, address, room, name):
        self.sock = socket.create_connection(address, timeout=3); self.sock.settimeout(3)
        self.pending = []
        protocol = int(re.search(r"^#define SST_PROTO (\d+)$", (ROOT / "client/src/sst_net.h").read_text(), re.M)[1])
        self.send(1, struct.pack("<H", protocol) + string(name) + string(room) + string("") + os.urandom(16))
        welcome = self.until(2)
        self.id = struct.unpack_from("<H", welcome)[0]

    def send(self, kind, body=b""):
        self.sock.sendall(struct.pack("<IB", len(body) + 1, kind) + body)

    def recv(self):
        length, = struct.unpack("<I", exact(self.sock, 4))
        if not 1 <= length <= 65536:
            raise AssertionError("Invalid native frame length")
        data = exact(self.sock, length)
        return data[0], data[1:]

    def until(self, kind, check=lambda data: True):
        for index, (message, body) in enumerate(self.pending):
            if message == kind and check(body):
                self.pending.pop(index); return body
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            message, body = self.recv()
            if message == 5:
                raise AssertionError("Native host rejected test peer")
            if message == kind and check(body):
                return body
            self.pending.append((message, body))
        raise AssertionError("Expected native test message was not received")

    def scene(self):
        self.send(0x10, string("F001r") + bytes((0, 1, 0)))

    def pose(self):
        body = struct.pack("<I3f3hHfHHHBB", 100, 100., 200., 300., 0, 0, 0, 26, 6., 0, 24, 24, 1, 0)
        assert len(body) == 36
        self.send(0x11, body)

    def close(self):
        self.sock.close()


def prop():
    # Same codec as sst_props.c, including independently chosen big-pot profile.
    scene = string("F001r") + bytes((0, 1, 0))
    key = struct.pack("<HHIBB", 9, 0x1f1, 0, 0, 1)
    state = struct.pack("<IBI3fh", 1, 1, 100, 100., 200., 300., 0)
    matrix = (1., 0., 0., 120., 0., 1., 0., 250., 0., 0., 1., 320.)
    return scene + key + state + struct.pack("<12f3f3h", *matrix, 120., 250., 320., 0, 0, 0)


def pickup():
    return b"\x00" + string("F001r") + bytes((0, 1, 0)) + struct.pack("<HIHIQ", 71, 2 | (255 << 10), 2, 1, 0)


@unittest.skipUnless(shutil.which("node") and HELPER.is_file(),
                     "Requires Node26 and the parent-built asset-free native session host")
class ActualWorkerTests(unittest.TestCase):
    def test_bad_fixture_upgrade_does_not_claim_the_host_slot(self):
        node, ready = self.child([shutil.which("node"), str(ROOT / "scripts/rooms-dev-server.mjs")])
        room = transport.create_room(ready["url"])
        parsed = urllib.parse.urlsplit(ready["url"])
        with socket.create_connection((parsed.hostname, parsed.port), timeout=3) as sock:
            request = (f"GET /r/{room['code']}/host HTTP/1.1\r\nHost: {parsed.netloc}\r\n"
                       "Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                       "Sec-WebSocket-Key: invalid\r\nAuthorization: Bearer " + room["host_token"] + "\r\n\r\n")
            sock.sendall(request.encode("ascii"))
            self.assertFalse(sock.recv(4096).startswith(b"HTTP/1.1 101"))
        ws = transport.WebSocket.connect(transport._endpoint(ready["url"], "/r/" + room["code"] + "/host", True),
                                         token=room["host_token"], credit_ack=True)
        self.addCleanup(ws.close)

    def child(self, command):
        # Only children created here are stopped. No process scans or personal data.
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL, text=True)
        def cleanup():
            if process.poll() is None:
                try:
                    process.stdin.write("quit\n"); process.stdin.flush()
                except (OSError, ValueError):
                    pass
                try:
                    process.wait(timeout=.5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait(timeout=2)
            process.stdin.close(); process.stdout.close()
        self.addCleanup(cleanup)
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True); reader.start()
        try:
            line = lines.get(timeout=5)
        except queue.Empty:
            raise AssertionError("Owned local fixture did not become ready") from None
        reader.join(1)
        self.assertTrue(line, "Owned local fixture exited before readiness")
        return process, json.loads(line)

    def run_session(self, tls=False):
        command = [shutil.which("node"), str(ROOT / "scripts/rooms-dev-server.mjs")]
        ca = None
        if tls:
            if not shutil.which("openssl"):
                self.skipTest("OpenSSL required only to generate an ephemeral loopback TLS fixture")
            temporary = tempfile.TemporaryDirectory(prefix="sst-room-tls-"); self.addCleanup(temporary.cleanup)
            folder = Path(temporary.name); key, ca = folder / "key.pem", folder / "cert.pem"
            subprocess.run([shutil.which("openssl"), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                            "-keyout", str(key), "-out", str(ca)], check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=10)
            command += ["--tls-cert", str(ca), "--tls-key", str(key)]
        node, ready = self.child(command)
        self.assertEqual(ready["fixture"], "actual-worker-node-cloudflare-shim")
        room = transport.create_room(ready["url"], ca=ca)
        self.assertRegex(room["code"], r"^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}$")
        self.assertTrue(room["credit_ack"])
        native, host_ready = self.child([str(HELPER), "--room", room["code"],
                                         "--port", "0", "--bind", "127.0.0.1"])
        host = transport.HostTunnel(ready["url"], room["code"], room["host_token"], host_ready["port"], ca=ca).start()
        self.addCleanup(host.close)
        creator = Peer(host.address(), room["code"], "Creator fixture"); self.addCleanup(creator.close)
        creator.scene(); creator.pose()
        guest = transport.GuestTunnel(ready["url"], room["code"], ca=ca).start(); self.addCleanup(guest.close)
        peer = Peer(guest.address(), room["code"], "Guest fixture"); self.addCleanup(peer.close)
        self.assertNotEqual(creator.id, peer.id)
        peer.scene(); peer.pose()
        creator.until(0x11, lambda body: body[:2] == struct.pack("<H", peer.id))

        # The ACTUAL native C host authenticates/forwards optional0x34. The
        # Worker/Python path must preserve the complete body byte-for-byte.
        body = prop(); peer.send(0x34, body)
        self.assertEqual(creator.until(0x34), struct.pack("<H", peer.id) + body)
        creator.send(0x34, body)
        self.assertEqual(peer.until(0x34), struct.pack("<H", creator.id) + body)

        request = pickup(); peer.send(0x33, request)
        committed = creator.until(0x33, lambda data: data[0] == 1)
        self.assertEqual(committed[1:-6], request[1:])
        epoch, collector = struct.unpack("<IH", committed[-6:])
        self.assertGreater(epoch, 0); self.assertEqual(collector, peer.id)
        self.assertEqual(peer.until(0x33), committed)  # collector ACK unchanged, never inventory rewriting

        late = transport.GuestTunnel(ready["url"], room["code"], ca=ca).start(); self.addCleanup(late.close)
        newcomer = Peer(late.address(), room["code"], "Snapshot fixture"); self.addCleanup(newcomer.close)
        newcomer.scene(); newcomer.pose()
        snapshot = newcomer.until(0x33, lambda data: data[0] == 2)
        self.assertEqual(snapshot, b"\x02" + committed[1:])

        third = transport.GuestTunnel(ready["url"], room["code"], ca=ca).start(); self.addCleanup(third.close)
        fourth_player = Peer(third.address(), room["code"], "Fourth player fixture"); self.addCleanup(fourth_player.close)
        fourth_player.scene(); fourth_player.pose()
        self.assertEqual(fourth_player.until(0x33, lambda data: data[0] == 2), snapshot)
        self.assertEqual(len({creator.id, peer.id, newcomer.id, fourth_player.id}), 4)
        with self.assertRaises(transport.TransportError):
            transport.WebSocket.connect(transport._endpoint(ready["url"], "/r/" + room["code"] + "/join", True),
                                        ca=ca, credit_ack=True)

        # A separate real Worker room code cannot join this native host.
        other = transport.create_room(ready["url"], ca=ca)
        self.assertNotEqual(other["code"], room["code"])
        with self.assertRaises(transport.TransportError):
            transport.WebSocket.connect(transport._endpoint(ready["url"], "/r/" + other["code"] + "/join", True),
                                        ca=ca, credit_ack=True)
        self.assertEqual(host.status()["connections"], 3)
        self.assertNotIn(room["host_token"], repr(host.status()))

        peer_id = peer.id; guest.close()
        self.assertEqual(creator.until(4, lambda data: data == struct.pack("<H", peer_id)), struct.pack("<H", peer_id))
        self.assertTrue(wait_for(lambda: host.status()["connections"] == 2))
        host.close()
        self.assertTrue(wait_for(lambda: late.status()["connections"] == 0 and third.status()["connections"] == 0))
        self.assertTrue(all(not thread.is_alive() for thread in host._threads))
        late.close(); third.close()
        self.assertTrue(all(not thread.is_alive() for thread in guest._threads + late._threads + third._threads))
        # Only test-created processes remain; native sidecar accepts its own quit.
        native.stdin.write("quit\n"); native.stdin.flush(); native.wait(timeout=3)
        self.assertEqual(native.returncode, 0)
        node.terminate(); node.wait(timeout=3); self.assertEqual(node.returncode, 0)

    def test_actual_worker_over_loopback_http_websocket(self):
        self.run_session(False)

    def test_actual_worker_over_verified_loopback_tls_websocket(self):
        self.run_session(True)

    def manager_lifecycle(self, helper_exit):
        node, ready = self.child([shutil.which("node"), str(ROOT / "scripts/rooms-dev-server.mjs")])
        with tempfile.TemporaryDirectory(prefix="sst-managed-room-") as temporary:
            host = online.OnlineSession(ready["url"], helper=HELPER,
                                       lease=Path(temporary) / "host.lease")
            self.addCleanup(host.close); host.host()
            creator = Peer(host.tunnel.address(), host.code, "Managed creator")
            self.addCleanup(creator.close); creator.scene(); creator.pose()
            guest = online.OnlineSession(ready["url"], lease=Path(temporary) / "guest.lease")
            self.addCleanup(guest.close); guest.join(host.code)
            peer = Peer(guest.tunnel.address(), host.code, "Managed guest")
            self.addCleanup(peer.close); peer.scene(); peer.pose()
            creator.until(0x11, lambda body: body[:2] == struct.pack("<H", peer.id))
            owned_helper = host.process
            self.assertTrue(host.lease.is_file() and guest.lease.is_file())
            if helper_exit:
                owned_helper.terminate()
                owned_helper.wait(timeout=2)
            else:
                host.close()
            self.assertTrue(wait_for(lambda: not host.lease.exists() and not guest.lease.exists()))
            self.assertTrue(wait_for(lambda: host.environment() == {} and guest.environment() == {}))
            host._monitor.join(2); guest._monitor.join(2)
            self.assertFalse(host._monitor.is_alive() or guest._monitor.is_alive())
            self.assertIsNotNone(owned_helper.poll())
            self.assertEqual(guest.snapshot()["state"], "error")
            self.assertEqual(guest.snapshot()["code"], "")
            # No reconnect attempt is made by this peer: remote closure itself
            # must notify the manager, revoke its lease and close native TCP.
            while peer.sock.recv(4096):
                pass
        node.terminate(); node.wait(timeout=3)

    def test_manager_leave_revokes_guest_without_native_reconnect(self):
        self.manager_lifecycle(False)

    def test_manager_private_helper_exit_revokes_entire_room(self):
        self.manager_lifecycle(True)

    def test_local_fixture_discards_disconnected_socket_state(self):
        temporary = tempfile.TemporaryDirectory(prefix="sst-fixture-lifetime-")
        self.addCleanup(temporary.cleanup)
        stats_path = Path(temporary.name) / "stats.json"
        node, ready = self.child([shutil.which("node"), str(ROOT / "scripts/rooms-dev-server.mjs"),
                                  "--stats-file", str(stats_path)])
        room = transport.create_room(ready["url"])
        native, backend = self.child([str(HELPER), "--room", room["code"], "--port", "0", "--bind", "127.0.0.1"])
        host = transport.HostTunnel(ready["url"], room["code"], room["host_token"], backend["port"]).start()
        self.addCleanup(host.close)
        creator = Peer(host.address(), room["code"], "Fixture lifetime creator")
        self.addCleanup(creator.close); creator.scene(); creator.pose()
        for _ in range(24):
            guest = transport.GuestTunnel(ready["url"], room["code"]).start()
            try:
                peer = Peer(guest.address(), room["code"], "Fixture replacement")
                try:
                    peer.scene(); peer.pose()
                    creator.until(0x11, lambda body: body[:2] == struct.pack("<H", peer.id))
                    guest.close()
                    creator.until(4, lambda body: body == struct.pack("<H", peer.id))
                    self.assertTrue(wait_for(lambda: host.status()["connections"] == 0))
                finally: peer.close()
            finally: guest.close()
        latest = {}
        def drained():
            try:
                latest.update(json.loads(stats_path.read_text()))
                return latest["active_endpoints"] == 1 and latest["open_network_sockets"] == 1 and not latest["pending_worker_events"]
            except (OSError, ValueError, KeyError):
                return False
        self.assertTrue(wait_for(drained))
        self.assertEqual(latest["worker_event_errors"], 0)
        self.assertEqual(latest["retained_endpoints"], 1)
        self.assertEqual(latest["retained_tags"], 1)


if __name__ == "__main__":
    unittest.main()
