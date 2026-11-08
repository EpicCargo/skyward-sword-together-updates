import json
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import importlib.util

spec = importlib.util.spec_from_file_location("avatar_peers", ROOT / "scripts/avatar-peers.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class AvatarPeerTests(unittest.TestCase):
    def test_animation_controls(self):
        pose = fixture.POSE.unpack(fixture.POSE.pack(1, 0,0,0, 0,0,0, 27,12.5,3, 24,24,0,0))
        self.assertEqual(fixture.animation(pose, None, 10), (27,12.5,3))
        self.assertEqual(fixture.animation(pose, dict(id=26,frame=3,rate=30,length=45), 2), (26,18,0))
        self.assertEqual(fixture.animation(pose, dict(id=0,frame=2), 100), (0,2,0))
        for override in [dict(id=443),dict(id=26,frame=float('nan')),dict(id=26,rate=1000),dict(id=26,length=-1)]:
            with self.assertRaises(ValueError): fixture.animation(pose, override, 1)

    def exercise(self, command, udp=False, udp_expected=False):
        with tempfile.TemporaryDirectory(prefix="sst-avatar-peers-") as temp:
            state = Path(temp) / "state.json"
            host = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            bots = source = None
            try:
                self.assertTrue(select.select([host.stdout], [], [], 5)[0], "host startup timed out")
                port = int(host.stdout.readline())
                source = socket.create_connection(("127.0.0.1", port), timeout=3)
                source.sendall(fixture.frame(1, struct.pack("<H", fixture.PROTOCOL) + fixture.string("Source test") + fixture.string("coop") + fixture.string("team") + b"s" * 16))
                scene = fixture.string("F001r") + bytes([1, 1, 5])
                source.sendall(fixture.frame(0x10, scene))
                bots = subprocess.Popen([
                    sys.executable, str(ROOT / "scripts/avatar-peers.py"), "--port", str(port),
                    "--follow-name", "Source test", "--room", "coop", "--team", "team", "--seconds", "3", "--state", str(state),
                ] + (["--udp"] if udp else []), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                source.setblocking(False)
                poses, scenes, departed = {}, {}, set()
                buffer = b""
                start = time.monotonic()
                changed = False
                tick = 0
                while time.monotonic() - start < 5:
                    if not changed and time.monotonic() - start > 1:
                        changed = True
                        scene = fixture.string("F001r") + bytes([2, 1, 5])
                        source.sendall(fixture.frame(0x10, scene))
                    tick += 1
                    source.sendall(fixture.frame(0x11, fixture.POSE.pack(tick, 100, 20, 300, 0, 0, 0, 27, 12.5, 3, 24, 24, 0, 0)))
                    if select.select([source], [], [], .033)[0]:
                        buffer += source.recv(65536)
                    while len(buffer) >= 4:
                        length = struct.unpack_from("<I", buffer)[0]
                        if len(buffer) < length + 4:
                            break
                        kind, body = buffer[4], buffer[5:length + 4]
                        buffer = buffer[length + 4:]
                        if kind == 0x10:
                            scenes[struct.unpack_from("<H", body)[0]] = body[2:]
                        elif kind == 0x11:
                            poses[struct.unpack_from("<H", body)[0]] = fixture.POSE.unpack(body[2:])
                        elif kind == 4:
                            departed.add(struct.unpack_from("<H", body)[0])
                    if bots.poll() is not None and len(departed) == 3:
                        break
                _, errors = bots.communicate(timeout=3)
                self.assertEqual(bots.returncode, 0, errors.decode())
                self.assertEqual(len(poses), 3)
                self.assertEqual({tuple(p[1:4]) for p in poses.values()}, {(-25., 20., 470.), (100., 20., 470.), (225., 20., 470.)})
                self.assertTrue(all(scenes[who] == scene for who in poses), "peers did not follow the changed room")
                self.assertTrue(all(p[7:10] == (27,12.5,3) for p in poses.values()))
                self.assertEqual(set(poses), departed, "simulated peers did not leave on time")
                self.assertGreater(json.loads(state.read_text())["poses"], 10)
                if udp:
                    channels=json.loads(state.read_text())["udp"]
                    self.assertEqual([c["active"] for c in channels],[udp_expected]*3)
                    if udp_expected:
                        self.assertTrue(all(c["sent"]>10 and c["received"]>0 for c in channels))
            finally:
                if bots is not None and bots.poll() is None:
                    bots.terminate()
                    bots.communicate(timeout=5)
                if source is not None:
                    source.close()
                if host.poll() is None:
                    host.terminate()
                host.communicate(timeout=5)

    @unittest.skipUnless((ROOT / "client/build/host_fixture").exists(), "build client/build/host_fixture first")
    def test_embedded_host_join_handshake(self):
        self.exercise([str(ROOT / "client/build/host_fixture")])

    def test_relay_welcome_handshake(self):
        self.exercise(["node", "-e", "const {server}=require('./server/relay').createRelay();server.listen(0,'127.0.0.1',()=>console.log(server.address().port))"])

    @unittest.skipUnless((ROOT / "client/build/host_fixture").exists(), "build client/build/host_fixture first")
    def test_embedded_host_udp(self):
        self.exercise([str(ROOT / "client/build/host_fixture")],udp=True,udp_expected=True)

    def test_relay_udp_fallback(self):
        self.exercise(["node", "-e", "const {server}=require('./server/relay').createRelay();server.listen(0,'127.0.0.1',()=>console.log(server.address().port))"],udp=True)


if __name__ == "__main__":
    unittest.main()
