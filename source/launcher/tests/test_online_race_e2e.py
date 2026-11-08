"""Startup cancellation and refused-worker isolation over actual local sockets."""
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
from unittest import mock

import test_online_e2e as fixture

m = fixture.transport


@unittest.skipUnless(shutil.which('node') and fixture.HELPER.is_file(), 'Requires local Node/native C fixture')
class ActualStartupRaces(unittest.TestCase):
    child = fixture.ActualWorkerTests.child

    def relay(self, tls):
        temporary = tempfile.TemporaryDirectory(prefix='sst-start-race-')
        self.addCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        command = [shutil.which('node'), str(fixture.ROOT / 'scripts/rooms-dev-server.mjs'),
                   '--stats-file', str(folder / 'stats.json')]
        ca = None
        if tls:
            if not shutil.which('openssl'):
                self.skipTest('OpenSSL required for local TLS certificate')
            key, ca = folder / 'key.pem', folder / 'ca.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                            '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1',
                            '-keyout', str(key), '-out', str(ca)], check=True, timeout=15,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            command += ['--tls-cert', str(ca), '--tls-key', str(key)]
        _, ready = self.child(command)
        return ready['url'], ca, folder / 'stats.json'

    def test_real_tls_host_close_releases_a_completed_but_unpublished_handshake(self):
        service, ca, stats = self.relay(True)
        room = m.create_room(service, ca=ca)
        host = m.HostTunnel(service, room['code'], room['host_token'], 12345, ca=ca)
        self.addCleanup(host.close)
        connected, release = threading.Event(), threading.Event()
        returned, failures = [], []
        connect = m.WebSocket.connect

        def delayed(*args, **kwargs):
            wire = connect(*args, **kwargs)
            returned.append(wire)
            connected.set()
            if not release.wait(3):
                wire.close()
                raise AssertionError('Fixture did not release the handshake')
            return wire

        def start():
            try:
                host.start()
            except BaseException as error:
                failures.append(error)

        with mock.patch.object(m.WebSocket, 'connect', side_effect=delayed):
            worker = threading.Thread(target=start)
            worker.start()
            try:
                self.assertTrue(connected.wait(3))
                host.close()
            finally:
                release.set()
                worker.join(4)
        self.assertFalse(worker.is_alive())
        self.assertEqual(len(failures), 1)
        self.assertIsInstance(failures[0], m.TransportError)
        self.assertTrue(returned[0]._closed.is_set())
        self.assertTrue(fixture.wait_for(lambda: stats.exists() and not any(
            json.loads(stats.read_text())[n] for n in ['retained_endpoints', 'retained_tags', 'open_network_sockets', 'pending_worker_events'])))

    def test_real_bound_guest_listener_is_closed_when_start_is_cancelled(self):
        host = m.GuestTunnel('http://127.0.0.1', 'ABC234')
        self.addCleanup(host.close)
        bound, release = threading.Event(), threading.Event()
        sockets, failures = [], []
        original = socket.socket

        class PendingListener(original):
            def bind(self, address):
                super().bind(address)
                sockets.append(self)
                bound.set()
                if not release.wait(3):
                    raise AssertionError('Fixture did not release bind')

        def start():
            try:
                host.start()
            except BaseException as error:
                failures.append(error)

        with mock.patch.object(m.socket, 'socket', PendingListener):
            worker = threading.Thread(target=start)
            worker.start()
            try:
                self.assertTrue(bound.wait(3))
                host.close()
            finally:
                release.set()
                worker.join(4)
        self.assertFalse(worker.is_alive())
        self.assertEqual(len(failures), 1)
        self.assertIsInstance(failures[0], m.TransportError)
        self.assertEqual(sockets[0].fileno(), -1)

    def worker_refusal(self, tls):
        service, ca, _ = self.relay(tls)
        room = m.create_room(service, ca=ca)
        _, ready = self.child([str(fixture.HELPER), '--room', room['code'], '--port', '0', '--bind', '127.0.0.1'])
        host = m.HostTunnel(service, room['code'], room['host_token'], ready['port'], ca=ca).start()
        self.addCleanup(host.close)
        stable = m.GuestTunnel(service, room['code'], ca=ca).start()
        self.addCleanup(stable.close)
        peer = fixture.Peer(stable.address(), room['code'], 'Stable fixture')
        self.addCleanup(peer.close)
        refused = m.GuestTunnel(service, room['code'], ca=ca).start()
        self.addCleanup(refused.close)
        with mock.patch.object(host, '_thread', side_effect=RuntimeError('injected worker shortage')):
            with socket.create_connection(refused.address(), timeout=3) as pending:
                pending.settimeout(3)
                self.assertEqual(pending.recv(1), b'')
        self.assertTrue(fixture.wait_for(lambda: host.status()['connections'] == 1))
        self.assertEqual(host.status()['state'], 'running')
        stamp = struct.pack('<I', 12345)
        peer.send(6, stamp)
        self.assertEqual(peer.until(7), stamp)
        retry = m.GuestTunnel(service, room['code'], ca=ca).start()
        self.addCleanup(retry.close)
        newcomer = fixture.Peer(retry.address(), room['code'], 'Retry fixture')
        self.addCleanup(newcomer.close)
        newcomer.send(6, stamp)
        self.assertEqual(newcomer.until(7), stamp)
        self.assertEqual(host.status()['state'], 'running')

    def test_refused_worker_preserves_native_teammate_over_http(self):
        self.worker_refusal(False)

    def test_refused_worker_preserves_native_teammate_over_verified_tls(self):
        self.worker_refusal(True)


if __name__ == '__main__':
    unittest.main()
