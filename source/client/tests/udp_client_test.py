"""Inject stale UDP traffic into the actual C client while its TCP scene context changes."""
import json
import os
import select
import socket
import struct
import threading
import time

from udp_e2e import Process, ROOT, wait_for
from udp_wire_test import datagram, pose, string


class Server:
    def __init__(self):
        self.tcp = socket.socket()
        self.tcp.bind(("127.0.0.1", 0))
        self.port = self.tcp.getsockname()[1]
        self.tcp.listen(1)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", self.port))
        self.token = os.urandom(16)
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.hold = threading.Event()
        self.context_waiting = threading.Event()
        self.block_tcp = threading.Event()
        self.movements = 0
        self.epoch = 100
        self.source_epoch = 500
        self.sequence = 0
        self.errors = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send(self, kind, body):
        with self.lock:
            self.connection.sendall(struct.pack("<IB", len(body)+1, kind)+body)

    def context(self, player, cookie, epoch):
        self.send(10, struct.pack("<HII", player, cookie, epoch))

    def source_scene(self, room, epoch):
        self.source_epoch = epoch
        self.send(0x10, struct.pack("<H", 2)+string("D000")+bytes([room, 0, 0]))
        self.context(2, 1, epoch)

    def inject(self, tick, source=None, target=None):
        with self.lock:
            self.sequence += 1
            body = struct.pack("<HII", 2, self.source_epoch if source is None else source,
                               self.epoch if target is None else target)+pose(tick)
            wire = datagram(self.token, self.sequence, 0x11, body)
            self.udp.sendto(wire, self.address)
            return wire

    def run(self):
        try:
            self.connection, _ = self.tcp.accept()
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            buffer = bytearray()
            while not self.stop.is_set():
                readable, _, _ = select.select([self.udp] if self.block_tcp.is_set() else [self.connection, self.udp], [], [], .05)
                if self.connection in readable:
                    data = self.connection.recv(65536)
                    if not data:
                        break
                    buffer.extend(data)
                    while len(buffer) >= 4:
                        n, = struct.unpack_from("<I", buffer)
                        if len(buffer) < n+4:
                            break
                        kind, body = buffer[4], bytes(buffer[5:n+4])
                        del buffer[:n+4]
                        if kind == 1:
                            self.send(2, struct.pack("<HB", 1, 0))
                            self.send(8, self.token)
                            self.send(3, struct.pack("<H", 2)+string("Other Link")+string("team"))
                            self.source_scene(0, self.source_epoch)
                        elif kind == 6:
                            self.send(7, body)
                        elif kind == 10:
                            self.context_waiting.set()
                            while self.hold.is_set() and not self.stop.wait(.01):
                                pass
                            self.epoch += 1
                            self.context(1, struct.unpack("<I", body)[0], self.epoch)
                if self.udp in readable:
                    data, self.address = self.udp.recvfrom(193)
                    if len(data) >= 33 and data[24] == 0:
                        with self.lock:
                            self.sequence += 1
                            self.udp.sendto(datagram(self.token, self.sequence, 0, data[29:]), self.address)
                    elif len(data) >= 29:
                        self.movements += 1
        except Exception as error:
            self.errors.append(repr(error))

    def close(self):
        self.stop.set()
        self.thread.join(timeout=3)
        self.connection.close()
        self.tcp.close()
        self.udp.close()
        assert not self.thread.is_alive() and not self.errors, self.errors


def main():
    server = Server()
    command = json.loads(os.environ.get("SST_UDP_CLIENT_COMMAND", "null")) or [str(ROOT / "client/build/udp_fixture")]
    client = Process(command+[str(server.port), "1", "Receiver"])
    try:
        assert client.line() == "ready"
        wait_for([client], lambda s: s[0]["udp"] and s[0]["players"] == 1)
        wire = server.inject(10)
        wait_for([client], lambda s: s[0]["tick"] == 10)
        server.udp.sendto(wire, server.address)  # duplicate
        server.inject(901, source=499)
        server.inject(902, target=server.epoch-1)
        body = struct.pack("<HII", 2, server.source_epoch, server.epoch)+pose(907)
        corrupt = bytearray(datagram(server.token, 9999, 0x11, body))
        corrupt[-1] ^= 1
        server.udp.sendto(corrupt, server.address)
        server.udp.sendto(datagram(b"x"*16, 9998, 0x11, body), server.address)
        time.sleep(.1)
        assert client.stats()["tick"] == 10
        server.inject(11)
        wait_for([client], lambda s: s[0]["tick"] == 11)
        # A source changes scene and returns. Its old area's valid-checksum datagrams stay stale.
        server.source_scene(1, 501)
        server.inject(903)
        time.sleep(.1)
        server.source_scene(0, 502)
        time.sleep(.1)
        server.inject(904, source=500)
        time.sleep(.1)
        assert not client.stats()["poses"]
        server.inject(12)
        wait_for([client], lambda s: s[0]["tick"] == 12)
        # Own scene is pending on TCP: even a valid packet with the previous target epoch drops.
        old_target = server.epoch
        server.context_waiting.clear()
        server.hold.set()
        client.command("scene 1")
        assert server.context_waiting.wait(2)
        server.inject(905, target=old_target)
        time.sleep(.1)
        assert client.stats()["tick"] == 12
        server.hold.clear()
        server.source_scene(1, 503)
        time.sleep(.15)
        server.inject(906, target=old_target)
        time.sleep(.1)
        assert not client.stats()["poses"]
        server.inject(13)
        wait_for([client], lambda s: s[0]["tick"] == 13)
        server.block_tcp.set()
        client.command("backlog")
        wait_for([client], lambda s: s[0]["queued"] > 65536)
        before = server.movements
        server.inject(14)
        wait_for([client], lambda s: s[0]["tick"] == 14)
        time.sleep(.3)
        assert server.movements > before+3, (before, server.movements)
        print("UDP client PASS: duplicate/checksum/source-scene/target-scene rejection, pending scene acknowledgement, return to same area")
    finally:
        server.hold.clear()
        client.close()
        server.close()


if __name__ == "__main__":
    main()
