import errno
from pathlib import Path
import socket
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'client/tests'))
import udp_e2e as fixture


class HybridAdapterTests(unittest.TestCase):
    def test_tcp_collision_retries_without_stopping_existing_service(self):
        created = []
        real_relay = fixture.Relay
        with socket.socket() as occupied:
            occupied.bind(('127.0.0.1', 0))
            occupied.listen(1)
            collision = occupied.getsockname()[1]
            def construct(target, **options):
                if not created:
                    options['listen_port'] = collision
                relay = real_relay(target, **options)
                created.append(relay)
                return relay
            relay = adapter = None
            try:
                with patch.object(fixture, 'Relay', side_effect=construct):
                    relay, adapter = fixture.start_hybrid_adapter(('127.0.0.1', 9))
                self.assertGreaterEqual(len(created), 2)
                for failed in created[:-1]:
                    self.assertEqual(failed.sock.fileno(), -1)
                    self.assertTrue(all(not t.is_alive() for t in failed.threads))
                self.assertNotEqual(relay.port, collision)
                self.assertEqual(adapter.listener.getsockname()[1], relay.port)
                # The conflicting service still owns its listener.
                with socket.create_connection(('127.0.0.1', collision), timeout=1) as client:
                    server, _ = occupied.accept()
                    with server:
                        client.sendall(b'alive')
                        self.assertEqual(server.recv(5), b'alive')
            finally:
                if adapter is not None: adapter.close()
                if relay is not None: relay.close()

    def test_non_collision_failure_closes_started_relay_and_preserves_error(self):
        created = []
        real_relay = fixture.Relay
        def construct(target, **options):
            relay = real_relay(target, **options)
            created.append(relay)
            return relay
        with patch.object(fixture, 'Relay', side_effect=construct), patch.object(
            fixture, 'TcpAdapter', side_effect=OSError(errno.EACCES, 'test bind denial')):
            with self.assertRaises(OSError) as raised:
                fixture.start_hybrid_adapter(('127.0.0.1', 9))
        self.assertEqual(raised.exception.errno, errno.EACCES)
        self.assertEqual(len(created), 1)
        self.assertEqual(created[0].sock.fileno(), -1)
        self.assertTrue(all(not t.is_alive() for t in created[0].threads))
