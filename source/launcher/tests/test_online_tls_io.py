"""TLS record concurrency and progress checks without network timing races."""
from pathlib import Path
import socket
import ssl
import sys
import threading
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sst_online_transport as transport


class TLSRecordSocket:
    def __init__(self):
        self.record = threading.Lock()
        self.entered = threading.Event()
        self.sent = bytearray()
        self.nonblocking = False
        self.closed = False

    def setsockopt(self, *args): pass
    def settimeout(self, *args): pass
    def setblocking(self, value): self.nonblocking = not value

    def recv(self, size):
        if not self.record.acquire(blocking=False):
            raise ssl.SSLError("concurrent TLS record operation")
        try:
            self.entered.set()
            time.sleep(.04)
            return b"x"
        finally:
            self.record.release()

    def send(self, data):
        if not self.record.acquire(blocking=False):
            raise ssl.SSLError("concurrent TLS record operation")
        try:
            count = min(len(data), 7)
            self.sent.extend(data[:count])
            return count
        finally:
            self.record.release()

    def sendall(self, data):
        while data:
            data = data[self.send(data):]

    def shutdown(self, *args): self.closed = True
    def close(self): self.closed = True


class TLSIOTests(unittest.TestCase):
    def test_websocket_read_and_write_do_not_overlap_tls_record_operations(self):
        sock = TLSRecordSocket()
        with mock.patch.object(transport.ssl, "SSLSocket", TLSRecordSocket):
            ws = transport.WebSocket(sock)
        errors = []
        received = []
        def read():
            try: received.append(ws._read(1))
            except Exception as error: errors.append(type(error).__name__)
        worker = threading.Thread(target=read)
        worker.start()
        try:
            self.assertTrue(sock.entered.wait(1))
            try: ws.send(b"opaque fixture bytes")
            except Exception as error: errors.append(type(error).__name__)
        finally:
            worker.join(2)
            ws.close()
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [], "TLS records were accessed concurrently")
        self.assertEqual(received, [b"x"])
        self.assertTrue(sock.sent)

    def test_idle_tls_reader_leaves_outgoing_frames_responsive(self):
        class IdleSocket(TLSRecordSocket):
            def recv(self, size):
                if self.closed: raise OSError("owned fixture closed")
                if not self.sent: raise ssl.SSLWantReadError()
                return b"x"
        sock = IdleSocket(); io = transport._TLSIO(sock)
        waiting = threading.Event(); written = threading.Event(); result = []
        def poll(*args):
            waiting.set(); written.wait(.05)
            return ([sock] if written.is_set() else [], [], [])
        def read():
            try: result.append(io.recv(1))
            except Exception as error: result.append(type(error).__name__)
        with mock.patch.object(transport.select, "select", side_effect=poll):
            worker = threading.Thread(target=read); worker.start()
            try:
                self.assertTrue(waiting.wait(1))
                io.sendall(b"outgoing"); written.set()
            finally:
                written.set(); worker.join(2); io.close()
        self.assertFalse(worker.is_alive())
        self.assertEqual(result, [b"x"])
        self.assertEqual(sock.sent, b"outgoing")

    def test_partial_tls_writes_survive_read_and_write_readiness_retries(self):
        class RetrySocket(TLSRecordSocket):
            attempts = 0
            def send(self, data):
                self.attempts += 1
                if self.attempts == 1: raise ssl.SSLWantReadError()
                if self.attempts == 3: raise ssl.SSLWantWriteError()
                return super().send(data)
        sock = RetrySocket(); io = transport._TLSIO(sock)
        with mock.patch.object(transport.select, "select", return_value=([], [], [])) as poll:
            io.sendall(b"complete opaque payload, once")
        self.assertEqual(sock.sent, b"complete opaque payload, once")
        self.assertEqual(poll.call_count, 2)
        self.assertEqual(poll.call_args_list[0].args[0], [sock])
        self.assertEqual(poll.call_args_list[1].args[1], [sock])
        io.close()

    def test_tls_readiness_wait_has_a_finite_deadline(self):
        class Blackhole(TLSRecordSocket):
            def recv(self, size): raise ssl.SSLWantReadError()
        sock = Blackhole(); io = transport._TLSIO(sock); now = [0.]
        def poll(*args):
            now[0] += args[3]
            return ([], [], [])
        with mock.patch.object(transport.time, "monotonic", side_effect=lambda: now[0]), \
             mock.patch.object(transport.select, "select", side_effect=poll):
            with self.assertRaises(socket.timeout): io.recv(1)
        self.assertLessEqual(now[0], 1.01)
        io.close()

    def test_tls_shutdown_wakes_a_waiting_reader(self):
        class Blackhole(TLSRecordSocket):
            def recv(self, size): raise ssl.SSLWantReadError()
        sock = Blackhole(); io = transport._TLSIO(sock)
        waiting = threading.Event(); result = []
        def poll(*args):
            waiting.set(); time.sleep(.01)
            return ([], [], [])
        def read():
            try: io.recv(1)
            except OSError: result.append("closed")
        with mock.patch.object(transport.select, "select", side_effect=poll):
            worker = threading.Thread(target=read); worker.start()
            try:
                self.assertTrue(waiting.wait(1)); io.shutdown(socket.SHUT_RDWR)
            finally:
                io.close(); worker.join(1)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result, ["closed"])

    def test_zero_length_tls_write_fails_instead_of_spinning(self):
        sock = TLSRecordSocket(); sock.send = lambda data: 0
        io = transport._TLSIO(sock)
        with self.assertRaises(OSError): io.sendall(b"payload")
        io.close()

