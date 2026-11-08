"""Occupied UDP port must leave ordinary TCP hosting fully functional."""
import json
import os
import socket

from udp_e2e import Process, ROOT, wait_for


def main():
    blocker = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    blocker.bind(("0.0.0.0", 0))
    port = blocker.getsockname()[1]
    host_command = json.loads(os.environ.get("SST_TEST_HOST_COMMAND", "null")) or [str(ROOT / "client/build/host_fixture")]
    client_command = json.loads(os.environ.get("SST_UDP_CLIENT_COMMAND", "null")) or [str(ROOT / "client/build/udp_fixture")]
    host = Process(host_command+[str(port)])
    peers = []
    try:
        assert int(host.line()) == port
        for i in range(2):
            peer = Process(client_command+[str(port), "1", f"Fallback {i}"])
            peers.append(peer)
            assert peer.line() == "ready"
        wait_for(peers, lambda s: all(x["online"] and not x["available"] and not x["udp"] and x["tick"] > 5 for x in s))
        peers[0].command("flag 1")
        peers[0].command("reward")
        wait_for(peers, lambda s: s[1]["flag"] == 1 and s[1]["rewards"] == 1 and s[1]["motions"] > 0)
        print("UDP fallback PASS: occupied UDP port, TCP movement/enemies/progress/rewards with UDP enabled at clients")
    finally:
        for peer in reversed(peers):
            peer.close()
        host.close()
        blocker.close()


if __name__ == "__main__":
    main()
