"""One failed guest worker must not disconnect established teammates."""
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sst_online_transport as m


class Messages:
    def __init__(self, items):
        self.items = iter(items)
        self.sent = []
        self.acks = []

    def recv(self):
        return next(self.items)

    def send(self, data):
        self.sent.append(data)

    def ack(self, count):
        self.acks.append(count)

    def close(self):
        pass


class HostAdmission(unittest.TestCase):
    def test_unavailable_guest_worker_does_not_close_stable_channel(self):
        for failure in [RuntimeError('thread unavailable'), m.TransportError('Local bridge worker limit reached')]:
            with self.subTest(failure=type(failure).__name__):
                stable, refused = mock.Mock(), mock.Mock()
                opened = lambda channel: bytes((m.OPEN,)) + struct.pack('<I', channel)
                data = bytes((m.DATA,)) + struct.pack('<I', 1) + b'established teammate'
                ws = Messages([opened(1), opened(2), data, None])
                host = m.HostTunnel('http://127.0.0.1', 'ABC234', 'a' * 64, 12345)
                host._ws = ws
                self.addCleanup(host.close)
                stable.sendall.side_effect = lambda payload: refused.close.assert_called()
                with mock.patch.object(m.socket, 'create_connection', side_effect=[stable, refused]), \
                        mock.patch.object(host, '_thread', side_effect=[True, failure]):
                    host._receive()
                stable.sendall.assert_called_once_with(b'established teammate')
                self.assertEqual(ws.acks, [5, 5, len(data)])
                self.assertEqual(ws.sent, [bytes((m.CLOSE,)) + struct.pack('<IH', 2, 1013)])

    def test_shutdown_suppressed_guest_worker_closes_its_published_backend(self):
        backend = mock.Mock()
        ws = Messages([bytes((m.OPEN,)) + struct.pack('<I', 1), None])
        host = m.HostTunnel('http://127.0.0.1', 'ABC234', 'a' * 64, 12345)
        host._ws = ws
        self.addCleanup(host.close)
        with mock.patch.object(m.socket, 'create_connection', return_value=backend), \
                mock.patch.object(host, '_thread', return_value=False):
            host._receive()
        backend.close.assert_called()
        self.assertEqual(ws.acks, [5])
        self.assertEqual(ws.sent, [])


if __name__ == '__main__':
    unittest.main()
