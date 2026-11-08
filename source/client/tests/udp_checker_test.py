"""Check optional UDP diagnostics and ordinary TCP success without game files."""
import json
import os
import socket
import subprocess

from udp_e2e import Process, ROOT


def main():
    host_command = json.loads(os.environ.get("SST_TEST_HOST_COMMAND", "null")) or [str(ROOT / "client/build/host_fixture")]
    checker = json.loads(os.environ.get("SST_CHECKER_COMMAND", "null")) or [str(ROOT / "client/build/network_check")]
    for blocked in [False, True]:
        # A free UDP port may still be occupied by a TCP connection or TIME_WAIT.
        for attempt in range(100):
            blocker = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            blocker.bind(("0.0.0.0", 0))
            port = blocker.getsockname()[1]
            with socket.socket() as tcp:
                try:
                    tcp.bind(("0.0.0.0", port))
                except OSError:
                    blocker.close()
                    continue
            break
        else:
            raise AssertionError("No free TCP/UDP test port")
        if not blocked:
            blocker.close()
        host = Process(host_command+[str(port)])
        try:
            assert int(host.line()) == port
            result = subprocess.run(checker+[f"127.0.0.1:{port}","coop","team","--udp"],
                                    capture_output=True,text=True,timeout=15)
            assert result.returncode == 0 and "PASS: host accepted" in result.stdout, result
            assert ("UDP unavailable" if blocked else "UDP PASS") in result.stdout, result
        finally:
            host.close()
            blocker.close()
    print("UDP checker PASS: bidirectional UDP and TCP-success fallback reported correctly")


if __name__ == "__main__":
    main()
