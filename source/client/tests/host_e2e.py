"""Four independent TCP peers against the embedded C host, under ASan/UBSan.

Covers fragmented frames, full sessions, scene/pose forwarding, late join, flag catch-up,
identity after a real disconnect, scene isolation, wrong rooms/teams and malformed input.
"""
import os
import json
import re
import socket
import struct
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO = int(re.search(r"^#define SST_PROTO (\d+)$", (ROOT / "src/sst_net.h").read_text(), re.M)[1])


def string(s):
    b = s.encode()
    return bytes([len(b)]) + b


class Peer:
    def __init__(self, port, name, room="coop", team="team", session=None, fragmented=False):
        self.sock = socket.create_connection(("127.0.0.1", port), 2)
        self.sock.settimeout(2)
        self.session = session or os.urandom(16)
        hello = struct.pack("<H", PROTO) + string(name) + string(room) + string(team) + self.session
        frame = struct.pack("<IB", len(hello) + 1, 1) + hello
        if fragmented:
            for b in frame:
                self.sock.sendall(bytes([b]))
        else:
            self.sock.sendall(frame)
        kind, body = self.recv()
        self.error = body[1:].decode() if kind == 5 else None
        self.id = struct.unpack_from("<H", body)[0] if kind == 2 else 0
        assert kind in (2, 5), (kind, body)

    def send(self, kind, b=b""):
        self.sock.sendall(struct.pack("<IB", len(b) + 1, kind) + b)

    def exact(self, n):
        chunks = bytearray()
        while len(chunks) < n:
            b = self.sock.recv(n - len(chunks))
            assert b, "unexpected disconnect"
            chunks.extend(b)
        return bytes(chunks)

    def recv(self):
        n, = struct.unpack("<I", self.exact(4))
        assert 1 <= n <= 512
        b = self.exact(n)
        return b[0], b[1:]

    def until(self, kind, check=lambda b: True):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            k, b = self.recv()
            if k == kind and check(b):
                return b
        raise AssertionError(f"no message {kind}")

    def scene(self, stage="F001r", room=1, layer=1, entrance=5):
        self.send(0x10, string(stage) + bytes([room, layer, entrance]))

    def pose(self, tick=1, x=300, flags=1):
        b = struct.pack("<I3f3hHfHHHBB", tick, x, 1.5, -2700, 0, 123, 0, 7, 3.5, 3, 24, 24, flags, 0)
        assert len(b) == 36
        self.send(0x11, b)
        return b

    def close(self):
        self.sock.close()


host_command = json.loads(os.environ.get("SST_TEST_HOST_COMMAND", "null")) or [str(ROOT / "build/host_fixture")]
client_commands = json.loads(os.environ.get("SST_TEST_CLIENT_COMMANDS", "null")) or [[str(ROOT / "build/four_player")]] * 4
assert len(client_commands) == 4 and all(isinstance(cmd,list) and cmd for cmd in client_commands)
host = subprocess.Popen(host_command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
peers = []
try:
    port = int(host.stdout.readline())
    for i in range(3):
        p = Peer(port, f"Link {i+1}", fragmented=(i == 0))
        peers.append(p)
        p.scene()
        p.pose(x=300 + i * 100)
    ids = {p.id for p in peers}
    assert len(ids) == 3
    # Chest rewards are immutable transactions, independent of flag OR merging.
    chest = struct.pack("<HH", 831, 176)
    peers[0].send(0x23, chest)
    peers[1].until(0x23, lambda b: b == struct.pack("<H", peers[0].id) + chest)
    peers[0].send(0x23, chest)
    peers[0].send(0x23, struct.pack("<HH", 831, 175))  # conflict cannot replace or OR an item ID
    peers[0].send(6, b"done")
    peers[0].until(7)
    skyview_key = struct.pack("<HH", 355, 1)
    cistern_key = struct.pack("<HH", 387, 1)
    peers[0].send(0x23, skyview_key)
    peers[1].until(0x23, lambda b: b[2:] == skyview_key)
    peers[1].send(0x23, cistern_key)
    peers[0].until(0x23, lambda b: b[2:] == cistern_key)
    heart_piece = struct.pack("<HH", 500, 94)
    peers[0].send(0x23, heart_piece)
    peers[1].until(0x23, lambda b: b[2:] == heart_piece)
    scene_heart = struct.pack("<HH", 865, 94)
    receipt_flag = struct.pack("<BHB", 2, 33, 1)
    peers[0].send(0x20, receipt_flag)
    peers[1].until(0x20, lambda b: b[2:] == receipt_flag)
    peers[0].send(0x23, scene_heart)  # retires an accidentally cached ordinary switch
    peers[1].until(0x23, lambda b: b[2:] == scene_heart)
    peers[0].send(0x20, receipt_flag)  # cannot return to the switch cache
    dungeon_map = struct.pack("<HH", 362, 50)
    boss_key = struct.pack("<HH", 372, 29)
    peers[0].send(0x23, dungeon_map); peers[0].send(0x23, boss_key)
    peers[1].until(0x23, lambda b: b[2:] == dungeon_map)
    peers[1].until(0x23, lambda b: b[2:] == boss_key)
    key_pieces = {struct.pack("<HH", index, 28) for index in [1352, 1353, 1354, 1356, 1408]}
    for reward in sorted(key_pieces):
        # Even a switch that arrives before its reward must never hide a missing piece.
        peers[0].send(0x20, struct.pack("<BHB", 2, struct.unpack_from("<H", reward)[0] - 832, 1))
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[0].send(0x23, reward)  # repeated source/resync
    placed_hearts = {struct.pack("<HH", index, 94) for index in [993, 1028, 1446, 1576, 2343]}
    for reward in sorted(placed_hearts):
        peers[0].send(0x20, struct.pack("<BHB", 2, struct.unpack_from("<H", reward)[0] - 832, 1))
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[0].send(0x23, reward)
    tools = {struct.pack("<HH", index, item) for index, item in [(228,75), (1550,105), (1115,71), (1116,76), (1117,77), (1118,90), (1119,91), (1123,140), (165,92), (449,92), (1114,92), (484,99), (1106,56), (1107,68), (1108,138), (1109,99), (162,20), (1103,20), (353,53), (389,137), (548,49), (1100,53), (584,19), (164,52), (1101,19), (1102,52)]}
    for reward in sorted(tools):
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[0].send(0x23, reward)
    peers[0].send(0x20, struct.pack("<BHB",1,153,1))
    bottle = struct.pack("<HH", 826, 153)
    peers[0].send(0x23, bottle)
    peers[1].until(0x23, lambda b: b[2:] == bottle)
    peers[1].send(0x23, bottle)
    filled_bottle = struct.pack("<HH", 823, 126)
    peers[0].send(0x20, struct.pack("<BHB",1,126,1))
    peers[0].send(0x23, filled_bottle)
    peers[1].until(0x23, lambda b: b[2:] == filled_bottle)
    peers[1].send(0x23, filled_bottle)
    bottle_gift = struct.pack("<HH", 4195, 153)
    peers[0].send(0x20, struct.pack("<BHB", 0, 35, 1))
    peers[0].send(0x23, bottle_gift)
    peers[1].until(0x23, lambda b: b[2:] == bottle_gift)
    peers[1].send(0x23, bottle_gift)
    batreaux = {struct.pack("<HH", i, item) for i,item in [(902,108),(903,94),(904,109),(905,33),(906,110),(907,33),(4520,111)]}
    peers[0].send(0x20,struct.pack("<BHB",0,360,1))
    for item in range(108,112):peers[0].send(0x20,struct.pack("<BHB",1,item,1))
    for bit in range(70,76):peers[0].send(0x20,struct.pack("<BHB",2,bit,1))
    for reward in sorted(batreaux):
        peers[0].send(0x23,reward);peers[1].until(0x23,lambda b:b[2:]==reward)
        peers[1].send(0x23,reward)
    crystal_indices = [934,935,936,938,940,941,942,943,944,945,946,947,3620,3636,3639]
    crystals = {struct.pack("<HH", i, 48) for i in crystal_indices}
    for reward in sorted(crystals):
        index = struct.unpack_from("<H", reward)[0]
        peers[0].send(0x20, struct.pack("<BHB", 2, index - 832, 1))
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[1].send(0x23, reward)
    for story_id in [35,382,395,547,548,549,550,551,552,554,555,556,557,558,559,560,561,663,664,771]:
        peers[0].send(0x20, struct.pack("<BHB", 0, story_id, 1))
    quest_indices = [4486,4533,4543,4544,4547,4554,4558,4632,4637,4642,4713,4721,4735,4796,4799]
    quests = {struct.pack("<HH", i, 35) for i in quest_indices}
    for reward in sorted(quests):
        peers[0].send(0x20, struct.pack("<BHB", 0, struct.unpack_from("<H", reward)[0]-4160, 1))
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[1].send(0x23, reward)
    peers[0].send(0x20,struct.pack("<BHB",1,180,1)) # ownership-only Stone must never cache
    trials = {struct.pack("<HH", index, item) for index, item in [(4366,68),(4367,138),(4368,20),(4369,180)]}
    for reward in sorted(trials):
        peers[0].send(0x20, struct.pack("<BHB", 0, struct.unpack_from("<H",reward)[0]-4160, 1))
        peers[0].send(0x23, reward)
        peers[1].until(0x23, lambda b: b[2:] == reward)
        peers[1].send(0x23, reward)
    # Shields need personal inventory transactions; raw acquired flags never bypass them.
    shields = {struct.pack("<HH", 5369+i, 116+i) for i in range(10)}
    shields |= {struct.pack("<HH", 740+i, 116+i) for i in range(10)}
    for item in range(116,126):peers[0].send(0x20,struct.pack("<BHB",1,item,1))
    for reward in sorted(shields):
        peers[0].send(0x23,reward)
        peers[1].until(0x23,lambda b:b[2:]==reward)
        peers[1].send(0x23,reward)
    ammo_first = {struct.pack("<HH",5379+i,128+i) for i in range(9)}
    ammo_first |= {struct.pack("<HH",710+i,128+i) for i in range(9)}
    for item in range(128,137):peers[0].send(0x20,struct.pack("<BHB",1,item,1))
    for reward in sorted(ammo_first):
        peers[0].send(0x23,reward)
        peers[1].until(0x23,lambda b:b[2:]==reward)
        peers[1].send(0x23,reward)
    swords = {struct.pack("<HH",2,10)} | {struct.pack("<HH",700+i,item) for i,item in enumerate([10,11,12,9,13,14])}
    for reward in sorted(swords):
        peers[0].send(0x23,reward)
        peers[1].until(0x23,lambda b:b[2:]==reward)
        peers[1].send(0x23,reward)
    # Cache progress before the fourth peer joins.
    peers[0].send(0x20, struct.pack("<BHB", 1, 321, 1))
    peers[1].until(0x20, lambda b: b[2:] == struct.pack("<BHB", 1, 321, 1))
    time.sleep(1.05)
    # A heartbeat must not make an old pose look fresh to a late joiner.
    peers[0].send(6, b"ping")
    peers[0].until(7)
    late = Peer(port, "Link 4")
    peers.append(late)
    assert late.id not in ids
    # Existing peers' full scene (including layer) arrives without waiting for a transition.
    b = late.until(0x10, lambda b: struct.unpack_from("<H", b)[0] == peers[0].id)
    assert b[2:] == string("F001r") + bytes([1, 1, 5])
    late.send(6, b"join")
    while True:
        kind, body = late.recv()
        assert kind != 0x11, "stale cached pose was replayed"
        if kind == 7:
            break
    late.scene()
    late.send(0x21)
    expected_rewards = {filled_bottle, bottle_gift, bottle, chest, skyview_key, cistern_key, heart_piece, scene_heart, dungeon_map, boss_key} | key_pieces | placed_hearts | tools | crystals | quests | trials | batreaux | shields | ammo_first | swords
    received_rewards = set(); progress_received = False
    while received_rewards != expected_rewards or not progress_received:
        kind, body = late.recv()
        if kind == 0x23 and body[:2] == struct.pack("<H", 0):
            assert body[2:] in expected_rewards
            received_rewards.add(body[2:])
        if kind == 0x20 and body == struct.pack("<HBHB", 0, 1, 321, 1):
            progress_received = True
    late.send(0x21); late.send(6, b"rcpt")
    while True:
        kind, body = late.recv()
        assert kind != 0x20 or body[2:] != receipt_flag, "chest receipt leaked into ordinary switch catch-up"
        assert kind != 0x20 or body[2] != 2 or struct.unpack_from("<H", body, 3)[0] not in [70,71,72,73,74,75,520, 521, 522, 524, 576, 161, 196, 614, 744, 1511] + [i - 832 for i in crystal_indices], "pickup receipt leaked into switch catch-up"
        assert kind != 0x20 or body[2:] != struct.pack("<BHB",1,180,1), "unpaired Stone ownership leaked into catch-up"
        assert kind != 0x20 or body[2] != 1 or struct.unpack_from("<H",body,3)[0] not in [108,109,110,111], "unpaired wallet ownership leaked"
        assert kind != 0x20 or body[2:] != struct.pack("<BHB",1,126,1), "filled bottle bypassed its pouch transaction"
        assert kind != 0x20 or body[2:] != struct.pack("<BHB",1,153,1), "bottle ownership bypassed its pouch transaction"
        assert kind != 0x20 or body[2] != 0 or struct.unpack_from("<H", body, 3)[0] not in [206,207,208,209,210,35,326,360,361,373,382,383,384,386,387,394,395,398,472,473,477,478,481,482,547,548,549,550,551,552,553,554,555,556,557,558,559,560,561,575,635,636,639,663,664,771], "quest receipt leaked into story catch-up"
        assert kind != 0x20 or body[2] != 1 or not 116 <= struct.unpack_from("<H",body,3)[0] <= 125, "raw shield ownership bypassed personal delivery"
        assert kind != 0x20 or body[2] != 1 or not 128 <= struct.unpack_from("<H",body,3)[0] <= 136, "raw spare ammo bypassed personal delivery"
        if kind == 7: break
    # Every player sees a fresh pose from each of the other three.
    for i, p in enumerate(peers):
        expected = p.pose(tick=100+i, x=400+i*100)
        for q in peers:
            if q is not p:
                body = q.until(0x11, lambda b: b[:2] == struct.pack("<H", p.id) and b[2:6] == struct.pack("<I", 100+i))
                assert body[2:] == expected
    fifth = Peer(port, "Link 5")
    assert fifth.error == "Session full (4 players)"
    fifth.close()
    # A real closed connection frees a slot and preserves its identity on return.
    old = peers.pop()
    old_id, old_session = old.id, old.session
    old.close()
    peers[0].until(4, lambda b: b == struct.pack("<H", old_id))
    rejoined = Peer(port, "Link 4", session=old_session)
    peers.append(rejoined)
    assert rejoined.id == old_id
    rejoined.scene(room=2)
    peers[0].until(0x10, lambda b: b[:2] == struct.pack("<H", old_id) and b[-3] == 2)
    # A recovery request obtains a fresh safety sample across areas, without subscribing to their
    # movement stream. This is needed before catch-up can choose a different room's entrance.
    sample = peers[0].pose(tick=998)
    rejoined.send(0x21)
    rejoined.until(0x11, lambda b: b == struct.pack("<H", peers[0].id) + sample)
    # Recovery asks all teammates for a fresh announcement, including across rooms.
    rejoined.send(0x22)
    peers[0].until(0x22, lambda b: b == struct.pack("<H", rejoined.id))
    enemy = string("F001r") + bytes([1, 1]) + struct.pack("<QIHHHB", 0x123456, 1, 0x17e, 3, 7, 0)
    peers[0].send(0x30, enemy)
    peers[1].until(0x30, lambda b: b == struct.pack("<H", peers[0].id) + enemy)
    # Regular-enemy movement has one owner per area; other peers cannot fight its updates.
    for i, p in enumerate(peers[:3]):
        p.pose(tick=1000+i, flags=0x20)
    key = (123 << 32) | (0x16d << 16) | 15
    def movement(sequence, x=10):
        return string("F001r") + bytes([1, 1]) + struct.pack("<QIH3f3h3f", key, sequence, 0x16d, x, 250, -1300, 0, 100, 0, 2, 0, 0)
    move = movement(1)
    peers[0].send(0x31, move)
    for p in peers[1:3]:
        p.until(0x31, lambda b: b == struct.pack("<H", peers[0].id) + move)
    peers[1].send(0x31, movement(900))  # not the owner
    peers[2].send(6, b"role")
    while True:
        kind, body = peers[2].recv()
        assert kind != 0x31, "non-owner movement was forwarded"
        if kind == 7:break
    peers[0].scene(room=3)
    peers[1].until(0x10, lambda b: b[:2] == struct.pack("<H", peers[0].id) and b[-3] == 3)
    move = movement(2)
    peers[1].send(0x31, move)
    peers[2].until(0x31, lambda b: b == struct.pack("<H", peers[1].id) + move)
    # An event/cutscene also relinquishes ownership while preserving area membership.
    peers[1].pose(tick=2000, flags=0x60)
    peers[1].send(6, b"evnt")
    peers[1].until(7, lambda b: b == b"evnt")
    peers[2].pose(tick=2001, flags=0x20)
    peers[2].send(0x31, movement(3))
    peers[1].until(0x31, lambda b: b == struct.pack("<H", peers[2].id) + movement(3))
    # In another room the rejoined player gets no movement, but PONG confirms the link is live.
    peers[0].pose(tick=999)
    rejoined.send(6, struct.pack("<I", 999))
    while True:
        kind, body = rejoined.recv()
        assert kind != 0x30, "cross-room enemy damage leaked"
        assert kind != 0x31, "cross-room enemy movement leaked"
        assert not (kind == 0x11 and body[2:6] == struct.pack("<I", 999)), "cross-room pose leaked"
        if kind == 7:
            break
    wrong = Peer(port, "Stranger", room="wrong")
    assert wrong.error == "Room does not match the host"
    wrong.close()
    wrong = Peer(port, "Stranger", team="wrong")
    assert wrong.error == "Team must match the host"
    wrong.close()
    malformed = socket.create_connection(("127.0.0.1", port), 2)
    malformed.settimeout(2)
    malformed.sendall(struct.pack("<I", 0xffffffff))
    # An invalid frame receives an error and is closed, without affecting the session.
    assert malformed.recv(512)[4] == 5
    malformed.close()
    peers[0].send(6, b"live")
    assert peers[0].until(7) == b"live"
    for p in peers:
        p.close()
    peers.clear()
    time.sleep(.1)
    for reward in [struct.pack("<HH",700,17),struct.pack("<HH",4160,10),struct.pack("<HH",5379,129),struct.pack("<HH",5387,135),struct.pack("<HH",5388,136),struct.pack("<HH",5369,117),struct.pack("<HH",5378,124),struct.pack("<HH",5379,125),struct.pack("<HH",902,33),struct.pack("<HH",903,108),struct.pack("<HH",907,3),struct.pack("<HH",4520,108),struct.pack("<HH", 4718, 35), struct.pack("<HH", 4713, 94), struct.pack("<HH", 4366, 20), struct.pack("<HH",4368,35), struct.pack("<HH",4370,180), struct.pack("<HH", 5362, 35), struct.pack("<HH", 4486, 94), struct.pack("<HH", 4545, 35), struct.pack("<HH", 4160, 3), struct.pack("<HH", 1, 154), struct.pack("<HH", 1, 93), struct.pack("<HH", 14 * 32, 1), struct.pack("<HH", 0, 1), struct.pack("<HH", 864, 1), struct.pack("<HH", 7 * 32, 50), struct.pack("<HH", 11 * 32, 25), struct.pack("<HH", 11 * 32, 51), struct.pack("<HH", 938, 35), struct.pack("<HH", 937, 48), struct.pack("<HH", 102, 48), struct.pack("<HH", 1351, 28), struct.pack("<HH", 4 * 32 + 8, 28), b"bad"]:
        bad = Peer(port, "Bad chest")
        bad.send(0x23, reward)
        bad.until(5)
        bad.close()
        time.sleep(.05)
    clients = []
    try:
        for i in range(4):
            clients.append(subprocess.Popen(client_commands[i] + [str(port), str(i)]))
            time.sleep(.15)
        for process in clients:
            assert process.wait(timeout=20) == 0
    finally:
        for process in clients:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    print("embedded host: four peers, fifth rejected, late join, reconnect, world requests, chest rewards, enemies, isolation and malformed packets PASS")
finally:
    host.stdin.close()
    assert host.wait(timeout=5) == 0
    for p in peers:
        p.close()
