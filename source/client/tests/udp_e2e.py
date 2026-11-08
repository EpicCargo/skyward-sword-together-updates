"""Real C hybrid transport through Co-op Anything's MIT UDP chaos relay. No game required."""
import errno
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import select
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "third_party/coop-anything-chaos"
for name, info in json.loads((VENDOR / "UPSTREAM.json").read_text())["files"].items():
    assert hashlib.sha256((VENDOR / name).read_bytes()).hexdigest() == info["sha256"], name
sys.path.insert(0, str(VENDOR))
from relay import Relay  # noqa: E402

PROTO = int(re.search(r"^#define SST_PROTO (\d+)$", (ROOT / "client/src/sst_net.h").read_text(), re.M)[1])


class Process:
    def __init__(self, command):
        self.p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1)
        self.lines = queue.Queue()
        self.errors = []
        def output():
            for line in self.p.stdout:
                self.lines.put(line.strip())
            self.lines.put(None)
        def errors():
            self.errors.extend(self.p.stderr)
        self.reader = threading.Thread(target=output, daemon=True)
        self.reader.start()
        self.error_reader = threading.Thread(target=errors, daemon=True)
        self.error_reader.start()

    def line(self):
        line = self.lines.get(timeout=10)
        assert line is not None, (self.p.poll(), self.errors)
        return line

    def command(self, command):
        self.p.stdin.write(command + "\n")
        self.p.stdin.flush()
        return self.line()

    def stats(self):
        return json.loads(self.command("stats"))

    def close(self):
        if self.p.poll() is None:
            self.p.stdin.close()
            try:
                self.p.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
                raise AssertionError("network fixture did not stop")
        self.reader.join(timeout=2)
        self.error_reader.join(timeout=2)
        assert self.p.returncode == 0, (self.p.returncode, self.errors)
        assert not self.errors, self.errors


class TcpAdapter:
    """TCP passthrough sharing the chaos relay's UDP port. Does not impair reliable traffic."""
    def __init__(self, port, target):
        self.listener = socket.socket()
        try:
            self.listener.bind(("127.0.0.1", port))
            self.listener.listen(8)
        except OSError:
            self.listener.close()
            raise
        self.listener.settimeout(.1)
        self.target = target
        self.stop = threading.Event()
        self.sockets = []
        self.workers = []
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stop.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            upstream = socket.create_connection(self.target, 3)
            client.setblocking(False)
            upstream.setblocking(False)
            self.sockets += [client, upstream]
            worker = threading.Thread(target=self.pump, args=(client, upstream), daemon=True)
            self.workers.append(worker)
            worker.start()

    def pump(self, a, b):
        pending = {a: bytearray(), b: bytearray()}
        try:
            while not self.stop.is_set():
                readable, writable, _ = select.select([a, b], [s for s in pending if pending[s]], [], .1)
                for source in readable:
                    data = source.recv(65536)
                    if not data:
                        return
                    pending[b if source is a else a].extend(data)
                for dest in writable:
                    sent = dest.send(pending[dest])
                    del pending[dest][:sent]
        except (OSError, ValueError):
            pass
        finally:
            a.close()
            b.close()

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        for worker in self.workers:
            worker.join(timeout=2)
        self.listener.close()

    def drop_connections(self):
        for sock in list(self.sockets):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def start_hybrid_adapter(target, **options):
    """Keep UDP bound while claiming TCP; retry ports occupied by another TCP service."""
    for _ in range(32):
        relay = Relay(target, **options).start()
        try:
            return relay, TcpAdapter(relay.port, target)
        except OSError as error:
            relay.close()
            if error.errno != errno.EADDRINUSE:
                raise
    raise RuntimeError("Could not reserve a shared TCP/UDP test port")


def wait_for(peers, predicate, timeout=7):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        samples = [p.stats() for p in peers]
        if predicate(samples):
            return samples
        time.sleep(.05)
    raise AssertionError(samples)


def main():
    out = Path(os.environ.get("SST_UDP_RESULTS", ROOT / "build/udp-network"))
    out.mkdir(parents=True, exist_ok=True)
    host = Process(json.loads(os.environ.get("SST_TEST_HOST_COMMAND", "null")) or
                   [str(ROOT / "client/build/host_fixture")])
    port = int(host.line())
    commands = json.loads(os.environ.get("SST_UDP_CLIENT_COMMANDS", "null")) or [
        [str(ROOT / "client/build/udp_fixture")]] * 4
    peers = []
    relay = adapter = None
    results = {}
    try:
        relay, adapter = start_hybrid_adapter(("127.0.0.1", port), profile="lan", log_path=out / "packets.jsonl")
        # The upstream log records prefixes. Our header contains a bearer token: omit it.
        record = relay.log.packet
        def redacted(**entry):
            entry["head"] = entry.get("head", "")[:8]
            record(**entry)
        relay.log.packet = redacted
        for i, command in enumerate(commands):
            # Three UDP peers plus one explicitly TCP-only player.
            p = Process(command + [str(relay.port), str(int(i < 3)), f"UDP Link {i+1}"])
            peers.append(p)
            assert p.line() == "ready"
        results["four_players"] = wait_for(peers, lambda s: all(x["online"] and x["players"] == 3 for x in s)
            and all(x["udp"] for x in s[:3]) and not s[3]["udp"] and all(x["tick"] > 5 for x in s))
        owner = min(results["four_players"], key=lambda s: s["id"])["id"]
        results["enemy_motion"] = wait_for(peers, lambda s: all(x["motions"] > 0 for x in s if x["id"] != owner))
        # Retain loss/reorder/duplicate deterministically, increase rates to exercise rejection.
        relay.set_profile("bad_wifi", seed=2401)
        relay.set_params({"loss": .12, "reorder": .3, "duplicate": .15, "corrupt": .03})
        old = [p.stats()["tick"] for p in peers]
        time.sleep(2)
        results["loss_reorder_corruption"] = wait_for(peers, lambda s: all(x["tick"] > old[i]+10 for i, x in enumerate(s)))
        stats = relay.get_stats()
        assert sum(x["dropped"] for x in stats.values()) > 0 and sum(x["duplicated"] for x in stats.values()) > 0, stats
        results["chaos_stats"] = stats
        # Round trip exceeds a probe interval; reordered movement must not invalidate ACKs.
        relay.set_profile("lan")
        relay.set_params({"latency_ms": 300, "jitter_ms": 15})
        time.sleep(2)
        results["slow_path"] = wait_for(peers, lambda s: all(x["udp"] for x in s[:3]))
        relay.set_profile("lan")
        time.sleep(.7)  # let the previous profile's scheduled packets drain
        # Test both one-way outages: UDP probes fail, movement falls back without reconnecting.
        relay.set_profile("lan")
        for direction in ["s2c", "c2s"]:
            relay.partition(direction, True)
            before = [p.stats() for p in peers]
            fallback = wait_for(peers, lambda s: all(not x["udp"] for x in s)
                and all(x["tick"] > before[i]["tick"]+5 for i, x in enumerate(s)), timeout=6)
            assert all(x["id"] == before[i]["id"] and x["online"] for i, x in enumerate(fallback)), fallback
            peers[0].command("flag " + ("1" if direction == "s2c" else "2"))
            peers[0].command("reward")
            peers[0].command("damage")
            wanted = 1 if direction == "s2c" else 3
            hits = 1 if direction == "s2c" else 2
            results[direction + "_fallback"] = wait_for(peers, lambda s: all(x["flag"] == wanted and x["rewards"] == 1 and x["damage_events"] == hits for x in s[1:]))
            relay.partition(direction, False)
            results[direction + "_recovery"] = wait_for(peers, lambda s: all(x["udp"] for x in s[:3]))
        # Reliable scene changes disable old UDP samples, including after returning to the same room.
        peers[0].command("scene 1")
        time.sleep(.4)
        isolated = peers[0].stats()["tick"]
        time.sleep(.5)
        assert peers[0].stats()["tick"] == isolated
        peers[0].command("scene 0")
        results["area_return"] = wait_for(peers, lambda s: s[0]["tick"] > isolated and all(x["udp"] for x in s[:3]))
        assert all(x["fallbacks"] >= 2 for x in results["area_return"][:3])
        owner_index = min(range(4), key=lambda i: results["area_return"][i]["id"])
        peers[owner_index].command("actors 256")
        results["crowded_room"] = wait_for(peers, lambda s: all(x["motion_unique"] == 256 for i, x in enumerate(s) if i != owner_index), timeout=20)
        before = [p.stats() for p in peers]
        adapter.drop_connections()
        results["tcp_reconnect"] = wait_for(peers, lambda s: all(x["online"] and x["players"] == 3
            and x["id"] == before[i]["id"] and x["generation"] > before[i]["generation"]
            and x["tick"] > before[i]["tick"] for i, x in enumerate(s)) and all(x["udp"] for x in s[:3]), timeout=10)
        results["full_game_instances"] = 0
        results["upstream_commit"] = "5b86e5bb10ce864ab9872f2589651f73ffcae5db"
        (out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
        print("UDP PASS: four peers, elected enemy movement, loss/reorder/duplicates/corruption, one-way TCP fallback, reliable rewards/progress/damage, recovery, area return, 256 actors, TCP reconnect")
    finally:
        errors = []
        for peer in reversed(peers):
            try:
                peer.close()
            except Exception as error:
                errors.append(error)
        if adapter is not None: adapter.close()
        if relay is not None: relay.close()
        host.close()
        assert not errors, errors


if __name__ == "__main__":
    main()
