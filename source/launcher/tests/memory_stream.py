"""Bounded in-memory duplex streams for transport tests without OS networking.

These exercise application framing and ownership, not kernel socket buffers,
TCP, TLS, DNS or network latency. Each incoming queue is bounded to 256 KiB.
"""
from collections import deque
import socket
import threading
import time


class MemoryStream:
    def __init__(self, capacity=256 * 1024):
        self.capacity = capacity
        self._condition = threading.Condition()
        self._chunks = deque()
        self._size = 0
        self._closed = self._eof = False
        self._timeout = 1.
        self.peer = None

    def settimeout(self, value):
        self._timeout = value

    def setsockopt(self, level, option, value):
        # Validate the production requests; this fixture has no kernel buffers.
        if level != socket.SOL_SOCKET or option not in (socket.SO_SNDBUF, socket.SO_RCVBUF) or value != 65536:
            raise AssertionError('Unexpected socket buffer request')

    def _wait(self, condition, deadline):
        left = None if deadline is None else deadline - time.monotonic()
        if left is not None and left <= 0:
            raise socket.timeout()
        condition.wait(left)

    def sendall(self, data):
        data = bytes(data)
        deadline = None if self._timeout is None else time.monotonic() + self._timeout
        at = 0
        peer = self.peer
        with peer._condition:
            while at < len(data):
                if self._closed or peer._closed:
                    raise OSError('Closed memory stream')
                count = min(len(data) - at, peer.capacity - peer._size)
                if not count:
                    self._wait(peer._condition, deadline)
                    continue
                peer._chunks.append(data[at:at + count])
                peer._size += count
                at += count
                peer._condition.notify_all()

    def recv(self, count):
        deadline = None if self._timeout is None else time.monotonic() + self._timeout
        with self._condition:
            while not self._chunks:
                if self._closed or self._eof:
                    return b''
                self._wait(self._condition, deadline)
            data = self._chunks.popleft()
            if len(data) > count:
                self._chunks.appendleft(data[count:])
                data = data[:count]
            self._size -= len(data)
            self._condition.notify_all()
            return data

    def shutdown(self, *_args):
        with self._condition:
            self._closed = True
            self._condition.notify_all()
        with self.peer._condition:
            self.peer._eof = True
            self.peer._condition.notify_all()

    close = shutdown


def memory_pair():
    left, right = MemoryStream(), MemoryStream()
    left.peer, right.peer = right, left
    return left, right
