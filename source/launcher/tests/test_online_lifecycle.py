"""Connection ownership races over bounded in-memory streams; no OS sockets."""
from pathlib import Path
import queue
import socket
import struct
import sys
import threading
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sst_online_transport as m
from memory_stream import memory_pair


class SocketMessages:
    def __init__(self, messages=()):
        self.incoming = queue.Queue()
        for message in messages:
            self.incoming.put(message)
        self.closed = False
        self.sent = []
        self.acks = []

    def recv(self):
        return self.incoming.get(timeout=2)

    def send(self, data):
        self.sent.append(data)

    def ack(self, count):
        self.acks.append(count)

    def close(self, *_args):
        self.closed = True
        self.incoming.put(None)


class ConnectionOwnership(unittest.TestCase):
    def guest(self):
        local, peer = memory_pair()
        self.addCleanup(peer.close)
        self.addCleanup(local.close)
        tunnel = m.GuestTunnel('http://127.0.0.1', 'ABC234')
        listener = mock.Mock()
        listener.accept.side_effect = [(local, None), OSError('test listener ended')]
        tunnel._listener = listener
        self.addCleanup(tunnel.close)
        return tunnel, local, SocketMessages()

    def test_guest_close_during_socket_setup_cannot_publish_an_unowned_connection(self):
        tunnel, local, ws = self.guest()
        with mock.patch.object(m.WebSocket, 'connect', return_value=ws), \
                mock.patch.object(m, '_bound_socket', side_effect=lambda _: tunnel.close()):
            tunnel._accept('ws://127.0.0.1/unused')
        self.assertTrue(ws.closed)
        self.assertTrue(local._closed)
        self.assertEqual(tunnel.status()['connections'], 0)
        self.assertEqual(tunnel.status()['state'], 'closed')

    def test_guest_socket_setup_failure_closes_its_unpublished_websocket(self):
        tunnel, local, ws = self.guest()
        with mock.patch.object(m.WebSocket, 'connect', return_value=ws), \
                mock.patch.object(m, '_bound_socket', side_effect=OSError('injected socket setup failure')):
            tunnel._accept('ws://127.0.0.1/unused')
        self.assertTrue(ws.closed)
        self.assertTrue(local._closed)
        self.assertEqual(tunnel.status()['connections'], 0)

    def test_guest_second_worker_start_failure_cleans_up_first_worker_and_connection(self):
        tunnel, local, ws = self.guest()
        original = tunnel._thread
        calls = []

        def start(target, name, *args):
            calls.append(name)
            if len(calls) == 2:
                raise RuntimeError('injected second worker failure')
            return original(target, name, *args)

        with mock.patch.object(m.WebSocket, 'connect', return_value=ws), \
                mock.patch.object(m, '_bound_socket', side_effect=lambda sock: sock.settimeout(.2)), \
                mock.patch.object(tunnel, '_thread', side_effect=start):
            tunnel._accept('ws://127.0.0.1/unused')
        self.assertEqual(len(calls), 2)
        tunnel.close()
        self.assertTrue(ws.closed)
        self.assertTrue(local._closed)
        self.assertEqual(tunnel.status()['connections'], 0)
        self.assertTrue(all(not thread.is_alive() for thread in tunnel._threads))

    def test_host_socket_setup_failure_closes_unpublished_backend(self):
        ws = SocketMessages([bytes((m.OPEN,)) + struct.pack('<I', 1), None])
        backend = mock.Mock()
        tunnel = m.HostTunnel('http://127.0.0.1', 'ABC234', 'a' * 64, 12345)
        tunnel._ws = ws
        self.addCleanup(tunnel.close)
        with mock.patch.object(m.socket, 'create_connection', return_value=backend), \
                mock.patch.object(m, '_bound_socket', side_effect=OSError('injected backend setup failure')):
            tunnel._receive()
        backend.close.assert_called_once()
        self.assertEqual(tunnel.status()['connections'], 0)
        self.assertEqual(ws.acks, [5])
        self.assertEqual(ws.sent, [bytes((m.CLOSE,)) + struct.pack('<IH', 1, 1011)])

    def test_old_guest_completion_cannot_overwrite_a_new_connections_status(self):
        tunnel, old, old_ws = self.guest()
        new, peer = memory_pair();new_ws = SocketMessages()
        self.addCleanup(new.close);self.addCleanup(peer.close)
        tunnel._local=old;tunnel._ws=old_ws;tunnel._session_left=1
        lock=threading.Lock()

        class PublicationBoundary:
            def __enter__(self):
                lock.acquire()

            def __exit__(self,*_args):
                lock.release()
                # A new accept can acquire the mutex immediately after the old
                # completion publishes an empty slot. Force that valid ordering.
                if tunnel._local is None and not tunnel._stop.is_set():
                    tunnel._local=new;tunnel._ws=new_ws;tunnel._session_left=2
                    tunnel._set('running','Room relay connected')

        tunnel._mu=PublicationBoundary()
        tunnel._finish(old,old_ws)
        self.assertIs(tunnel._local,new)
        self.assertEqual(tunnel.status()['state'],'running')
        self.assertEqual(tunnel.status()['connections'],1)
        self.assertFalse(new_ws.closed)

    def host_receive(self,messages,backends):
        tunnel=m.HostTunnel('http://127.0.0.1','ABC234','a'*64,12345)
        tunnel._ws=SocketMessages(messages)
        self.addCleanup(tunnel.close)
        with mock.patch.object(m.socket,'create_connection',side_effect=backends) as connect, \
                mock.patch.object(tunnel,'_thread',return_value=True):
            tunnel._receive()
        return tunnel,connect

    def test_host_rejects_reuse_of_a_closed_channel_before_opening_another_backend(self):
        opened=bytes((m.OPEN,))+struct.pack('<I',1)
        closed=bytes((m.CLOSE,))+struct.pack('<IH',1,1000)
        tunnel,connect=self.host_receive([opened,closed,opened,None],[mock.Mock(),mock.Mock()])
        self.assertEqual(connect.call_count,1)
        self.assertEqual(tunnel._ws.acks,[5,7])

    def test_failed_backend_open_also_consumes_its_channel_identity(self):
        opened=bytes((m.OPEN,))+struct.pack('<I',7)
        tunnel,connect=self.host_receive([opened,opened,None],[OSError('refused'),mock.Mock()])
        self.assertEqual(connect.call_count,1)
        self.assertEqual(tunnel._ws.acks,[5])

    def test_closing_an_absent_channel_does_not_emit_an_extra_close(self):
        tunnel=m.HostTunnel('http://127.0.0.1','ABC234','a'*64,12345)
        tunnel._ws=SocketMessages();self.addCleanup(tunnel.close)
        tunnel._drop(1,notify=True)
        self.assertEqual(tunnel._ws.sent,[])

    def test_stale_backend_reader_cannot_close_a_different_connection(self):
        tunnel=m.HostTunnel('http://127.0.0.1','ABC234','a'*64,12345)
        tunnel._ws=SocketMessages();self.addCleanup(tunnel.close)
        obsolete,replacement=mock.Mock(),mock.Mock();obsolete.recv.return_value=b''
        tunnel._channels[1]=replacement
        tunnel._read_backend(1,obsolete)
        self.assertIs(tunnel._channels.get(1),replacement)
        replacement.close.assert_not_called();obsolete.close.assert_called_once()
        self.assertEqual(tunnel._ws.sent,[])

    def test_forward_channel_gap_after_failed_backend_open_is_valid(self):
        one=bytes((m.OPEN,))+struct.pack('<I',1)
        four=bytes((m.OPEN,))+struct.pack('<I',4)
        tunnel,connect=self.host_receive([one,four,None],[OSError('refused'),mock.Mock()])
        self.assertEqual(connect.call_count,2)
        self.assertEqual(tunnel._ws.acks,[5,5])

    def test_current_backend_completion_notifies_once_and_preserves_other_channels(self):
        tunnel=m.HostTunnel('http://127.0.0.1','ABC234','a'*64,12345)
        tunnel._ws=SocketMessages();self.addCleanup(tunnel.close)
        current,other=mock.Mock(),mock.Mock();current.recv.return_value=b''
        tunnel._channels={1:current,2:other}
        tunnel._read_backend(1,current)
        self.assertIs(tunnel._channels.get(2),other)
        self.assertEqual(tunnel._ws.sent,[bytes((m.CLOSE,))+struct.pack('<IH',1,1000)])
        tunnel._drop(1,notify=True)
        self.assertEqual(len(tunnel._ws.sent),1)

    def test_listener_failure_cannot_leave_a_stopped_tunnel_looking_live(self):
        tunnel,old,ws=self.guest()
        tunnel._local=old;tunnel._ws=ws;tunnel._session_left=1
        tunnel._listener.accept.side_effect=OSError('injected listener failure')
        lock=threading.Lock();armed=True

        class TerminalBoundary:
            def __enter__(self):lock.acquire()
            def __exit__(self,*_args):
                nonlocal armed
                lock.release()
                if armed and tunnel._state=='error':
                    armed=False
                    # Complete the local EOF as soon as terminal state is visible.
                    tunnel._finish(old,ws)

        tunnel._mu=TerminalBoundary()
        tunnel._accept('ws://127.0.0.1/unused')
        self.assertTrue(tunnel._stop.is_set())
        self.assertEqual(tunnel.status()['state'],'error')
        self.assertEqual(tunnel.status()['connections'],0)


if __name__ == '__main__':
    unittest.main()
