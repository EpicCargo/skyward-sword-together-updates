#!/usr/bin/env python3
"""Test Windows PE networking under an isolated Wine prefix against Linux clients/hosts."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/windows-network"


def main():
    env = dict(os.environ, WINEPREFIX=str(ROOT / "build/wine-network-test"), WINEDEBUG="-all",
               WINEDLLOVERRIDES="mscoree,mshtml=", DISPLAY="", WAYLAND_DISPLAY="", LIBGL_ALWAYS_SOFTWARE="1")
    summary = []

    def run(label, command, overrides=None, expected=0, cwd=ROOT):
        with (OUT / (label + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=cwd, env=env | (overrides or {}), stdout=log,
                                    stderr=subprocess.STDOUT, timeout=100)
        if result.returncode != expected:
            raise RuntimeError(f"{label}: exit {result.returncode}, expected {expected}; see {OUT / (label + '.log')}")
        summary.append(label)
        print(label, "PASS", flush=True)

    try:
        for name in ["defeat_test", "stream_test", "spawn_test", "animation_test", "world_test", "motion_test", "chest_test", "udp_queue_test", "scene_history_test"]:
            run("windows-" + name, ["wine", str(OUT / (name + ".exe"))])
        run("windows-queue-recovery", [sys.executable, str(ROOT / "client/tests/queue_e2e.py")], {
            "SST_QUEUE_CLIENT_COMMAND": json.dumps(["wine", str(OUT / "queue_fixture.exe")])})
        run("windows-snapshot-recovery", [sys.executable, str(ROOT / "client/tests/refresh_e2e.py")], {
            "SST_QUEUE_CLIENT_COMMAND": json.dumps(["wine", str(OUT / "queue_fixture.exe")])})
        run("windows-snapshot-host", [sys.executable, str(ROOT / "client/tests/refresh_host_test.py")], {
            "SST_TEST_HOST_COMMAND": json.dumps(["wine", str(OUT / "host_fixture.exe")])})
        run("windows-default-room-aliases", [sys.executable, str(ROOT / "client/tests/room_alias_test.py")], {
            "SST_ROOM_HOST_COMMAND": json.dumps(["wine", str(OUT / "host_fixture.exe")])})
        with tempfile.TemporaryDirectory(prefix="backup-",dir=OUT) as work:
            run("windows-backup",["wine",str(OUT / "backup_portable_test.exe")],cwd=work)
        clients = [[str(ROOT / "client/build/four_player")], ["wine", str(OUT / "four_player.exe")]] * 2
        for name, host in [("windows", ["wine", str(OUT / "host_fixture.exe")]),
                           ("linux", [str(ROOT / "client/build/host_fixture")])]:
            run(name + "-host-mixed", [sys.executable, str(ROOT / "client/tests/host_e2e.py")], {
                "SST_TEST_HOST_COMMAND": json.dumps(host), "SST_TEST_CLIENT_COMMANDS": json.dumps(clients)})
        udp_clients = [[str(ROOT / "client/build/udp_fixture")], ["wine", str(OUT / "udp_fixture.exe")]] * 2
        for name, host in [("windows", ["wine", str(OUT / "host_fixture.exe")]),
                           ("linux", [str(ROOT / "client/build/host_fixture")])]:
            run(name + "-udp-mixed", [sys.executable, str(ROOT / "client/tests/udp_e2e.py")], {
                "SST_TEST_HOST_COMMAND": json.dumps(host), "SST_UDP_CLIENT_COMMANDS": json.dumps(udp_clients),
                "SST_UDP_RESULTS": str(OUT / (name + "-udp-results"))})
        run("windows-udp-wire", [sys.executable, str(ROOT / "client/tests/udp_wire_test.py")], {
            "SST_TEST_HOST_COMMAND": json.dumps(["wine", str(OUT / "host_fixture.exe")])})
        for test in ["client", "fallback"]:
            run("windows-udp-" + test, [sys.executable, str(ROOT / f"client/tests/udp_{test}_test.py")], {
                "SST_TEST_HOST_COMMAND": json.dumps(["wine", str(OUT / "host_fixture.exe")]),
                "SST_UDP_CLIENT_COMMAND": json.dumps(["wine", str(OUT / "udp_fixture.exe")])})
        run("windows-udp-checker", [sys.executable, str(ROOT / "client/tests/udp_checker_test.py")], {
            "SST_TEST_HOST_COMMAND": json.dumps(["wine", str(OUT / "host_fixture.exe")]),
            "SST_CHECKER_COMMAND": json.dumps(["wine", str(OUT / "sst-network-check.exe")])})
        code = "const {server}=require(" + json.dumps(str(ROOT / "server/relay.js")) + ").createRelay();server.listen(0,'127.0.0.1',()=>console.log(server.address().port))"
        node = subprocess.Popen(["node", "-e", code], stdout=subprocess.PIPE, text=True)
        peers = []
        try:
            port = int(node.stdout.readline())
            for name, peer, prefix in [("Linux", "Windows", [str(ROOT / "client/build/net_e2e")]),
                                       ("Windows", "Linux", ["wine", str(OUT / "net_e2e.exe")])]:
                log = (OUT / ("relay-client-" + name + ".log")).open("w")
                process = subprocess.Popen(prefix + [str(port), name, peer], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
                peers.append((process, log))
            for process, _ in peers:
                if process.wait(timeout=30):
                    raise RuntimeError("mixed Linux/Windows relay clients failed")
            summary.append("relay-mixed-clients")
            run("windows-checker-success", ["wine", str(OUT / "sst-network-check.exe"), f"127.0.0.1:{port}"])
            run("linux-checker-success", [str(ROOT / "client/build/network_check"), f"127.0.0.1:{port}"])
        finally:
            for process, log in peers:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
                log.close()
            node.terminate()
            node.wait(timeout=5)
        run("windows-checker-refused", ["wine", str(OUT / "sst-network-check.exe"), f"127.0.0.1:{port}"], expected=1)
        (OUT / "summary.json").write_text(json.dumps(dict(passed=summary, platform="Windows PE under Wine; not a Windows game/PC test"), indent=2))
    finally:
        # This prefix belongs only to these tests. Other Wine applications use their own server.
        subprocess.run(["wineserver", "-k"], env=env, timeout=10, check=False)


if __name__ == "__main__":
    main()
