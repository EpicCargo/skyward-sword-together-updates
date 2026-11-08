"""Deterministic startup/shutdown ordering; no DNS or OS sockets are used."""
from pathlib import Path
import socket
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sst_online_transport as m


class StartupOwnership(unittest.TestCase):
    def host(self):
        tunnel = m.HostTunnel('http://127.0.0.1', 'ABC234', 'a' * 64, 12345)
        self.addCleanup(tunnel.close)
        return tunnel

    def guest(self):
        tunnel = m.GuestTunnel('http://127.0.0.1', 'ABC234')
        self.addCleanup(tunnel.close)
        return tunnel

    def test_host_close_during_handshake_closes_late_socket_and_rejects_start(self):
        tunnel = self.host()
        ws = mock.Mock()

        def connect(*args, **kwargs):
            tunnel.close()
            return ws

        with mock.patch.object(m.WebSocket, 'connect', side_effect=connect), \
                mock.patch.object(tunnel, '_thread') as worker:
            with self.assertRaises(m.TransportError):
                tunnel.start()
        ws.close.assert_called()
        worker.assert_not_called()
        self.assertEqual(tunnel.status()['state'], 'closed')

    def test_host_worker_start_failure_closes_published_socket(self):
        tunnel = self.host()
        ws = mock.Mock()
        with mock.patch.object(m.WebSocket, 'connect', return_value=ws), \
                mock.patch.object(tunnel, '_thread', side_effect=RuntimeError('worker unavailable')):
            with self.assertRaisesRegex(RuntimeError, 'worker unavailable'):
                tunnel.start()
        ws.close.assert_called()
        self.assertTrue(tunnel._stop.is_set())
        self.assertEqual(tunnel.status()['state'], 'closed')

    def test_host_close_after_publication_cannot_return_a_running_start(self):
        tunnel = self.host()
        ws = mock.Mock()

        def suppress(*args):
            tunnel.close()
            return False

        with mock.patch.object(m.WebSocket, 'connect', return_value=ws), \
                mock.patch.object(tunnel, '_thread', side_effect=suppress):
            with self.assertRaises(m.TransportError):
                tunnel.start()
        ws.close.assert_called()

    def test_host_handshake_receives_the_owned_cancellation_event(self):
        tunnel = self.host()
        with mock.patch.object(m.WebSocket, 'connect', side_effect=m.TransportError('cancelled')) as connect:
            with self.assertRaises(m.TransportError):
                tunnel.start()
        self.assertIs(connect.call_args.kwargs.get('_cancel'), tunnel._stop)
        self.assertTrue(tunnel._stop.is_set())

    def test_guest_close_during_resolution_does_not_allocate_a_listener(self):
        tunnel = self.guest()

        def resolve(*args, **kwargs):
            tunnel.close()
            return []

        with mock.patch.object(m.socket, 'getaddrinfo', side_effect=resolve), \
                mock.patch.object(m.socket, 'socket') as create:
            with self.assertRaises(m.TransportError):
                tunnel.start()
        create.assert_not_called()

    def test_guest_close_during_bind_closes_late_listener(self):
        tunnel = self.guest()
        listener = mock.Mock()
        listener.getsockname.return_value = ('127.0.0.1', 34567)
        listener.bind.side_effect = lambda _: tunnel.close()
        with mock.patch.object(m.socket, 'getaddrinfo', return_value=[]), \
                mock.patch.object(m.socket, 'socket', return_value=listener), \
                mock.patch.object(tunnel, '_thread') as worker:
            with self.assertRaises(m.TransportError):
                tunnel.start()
        listener.close.assert_called()
        worker.assert_not_called()
        self.assertEqual(tunnel.status()['state'], 'closed')

    def test_guest_worker_start_failure_closes_published_listener(self):
        tunnel = self.guest()
        listener = mock.Mock()
        listener.getsockname.return_value = ('127.0.0.1', 34567)
        with mock.patch.object(m.socket, 'getaddrinfo', return_value=[]), \
                mock.patch.object(m.socket, 'socket', return_value=listener), \
                mock.patch.object(tunnel, '_thread', side_effect=RuntimeError('worker unavailable')):
            with self.assertRaisesRegex(RuntimeError, 'worker unavailable'):
                tunnel.start()
        listener.close.assert_called()
        self.assertTrue(tunnel._stop.is_set())

    def test_second_start_does_not_close_an_existing_tunnel(self):
        for tunnel in [self.host(), self.guest()]:
            with self.subTest(tunnel=type(tunnel).__name__):
                tunnel._started = True
                with mock.patch.object(tunnel, 'close') as close:
                    with self.assertRaises(m.TransportError):
                        tunnel.start()
                close.assert_not_called()


if __name__ == '__main__':
    unittest.main()
