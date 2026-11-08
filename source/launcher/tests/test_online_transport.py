import base64
import hashlib
import http.server
import json
import queue
import socket
import ssl
import struct
import sys
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sst_online_transport as m


def wait_for(predicate, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(.01)
    return False


def exact(sock, size):
    result = b""
    while len(result) < size:
        part = sock.recv(size - len(result))
        if not part:
            raise EOFError()
        result += part
    return result


def frame(data, opcode=2, final=True):
    head = bytes(((128 if final else 0) | opcode, len(data) if len(data) < 126 else 126 if len(data) < 65536 else 127))
    if 126 <= len(data) < 65536:
        head += struct.pack("!H", len(data))
    elif len(data) >= 65536:
        head += struct.pack("!Q", len(data))
    return head + data


def client_frame(sock):
    a, b = exact(sock, 2)
    assert b & 128, "Client frame was not masked"
    size = b & 127
    if size == 126:
        size = struct.unpack("!H", exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(sock, 8))[0]
    mask = exact(sock, 4); data = exact(sock, size)
    return a & 15, bytes(value ^ mask[i % 4] for i, value in enumerate(data))


class FakeWS:
    def __init__(self):
        self.incoming = queue.Queue(); self.sent = []; self.acks = []; self.closed = False
    def recv(self):
        return self.incoming.get(timeout=5)
    def send(self, data):
        if self.closed:
            raise m.TransportError("closed")
        self.sent.append(data)
    def ack(self, count):
        self.acks.append(count)
    def close(self, *args):
        if not self.closed:
            self.closed = True; self.incoming.put(None)


class ClockSocket:
    """No network or real wait: time advances only when its reader would time out."""
    def __init__(self, incoming=b"", on_ping=None):
        self.now = 0.0; self.incoming = bytearray(incoming); self.on_ping = on_ping
        self.sent = []; self.closed = False
    def setsockopt(self, *args):
        pass
    def settimeout(self, *args):
        pass
    def recv(self, count):
        if self.closed:
            return b""
        if not self.incoming:
            self.now += 1
            raise socket.timeout()
        result = bytes(self.incoming[:count]); del self.incoming[:count]
        return result
    def sendall(self, wire):
        # These tests send only bounded control frames and tiny credit ACK text.
        assert wire[1] & 128 and wire[1] & 127 < 126
        size = wire[1] & 127; mask = wire[2:6]
        payload = bytes(value ^ mask[i % 4] for i, value in enumerate(wire[6:]))
        assert len(payload) == size
        opcode = wire[0] & 15; self.sent.append((opcode, payload, self.now))
        if opcode == 9 and self.on_ping:
            self.on_ping(self, payload)
    def shutdown(self, *args):
        self.closed = True
    def close(self):
        self.closed = True


class HeartbeatTests(unittest.TestCase):
    def test_idle_blackhole_sends_one_protocol_ping_and_closes_by_deadline(self):
        sock = ClockSocket()
        with mock.patch.object(m.time, "monotonic", side_effect=lambda: sock.now):
            ws = m.WebSocket(sock)
            with self.assertRaises(m.TransportError) as error:
                ws.recv()
        self.assertEqual(str(error.exception), "Relay heartbeat timed out")
        self.assertEqual(error.exception.close_code, 1001)
        self.assertEqual([(op, at) for op, _, at in sock.sent], [(9, 15.0), (8, 30.0)])
        self.assertEqual(len(sock.sent[0][1]), 8)
        self.assertTrue(sock.closed and ws._closed.is_set())

    def test_matching_pong_refreshes_liveness_without_changing_credit_or_game_bytes(self):
        credit, game = b"credit00", b"opaque game bytes"
        def respond(sock, nonce):
            sock.incoming.extend(frame(nonce, opcode=10) + frame(credit + game))
        sock = ClockSocket(on_ping=respond)
        with mock.patch.object(m.time, "monotonic", side_effect=lambda: sock.now):
            ws = m.WebSocket(sock, credit_ack=True)
            self.assertEqual(ws.recv(), game)
            self.assertIsNone(ws._ping_nonce); self.assertEqual(ws._last_pong, 15)
            self.assertEqual(ws._credit, (credit, len(game)))
            ws.ack(len(game))
            self.assertEqual(json.loads(sock.sent[-1][1]), {"ack": credit.hex()})
            sock.now = 30
            self.assertEqual(ws.recv(), game); self.assertEqual(ws._last_pong, 30)
            self.assertEqual([op for op, _, _ in sock.sent], [9, 1, 9])
            ws.close()

    def test_foreign_pong_protocol_ping_and_data_do_not_satisfy_our_probe(self):
        def unrelated(sock, nonce):
            sock.incoming.extend(frame(b"wrong-pong", opcode=10) + frame(b"peer-ping", opcode=9) + frame(b"busy"))
        sock = ClockSocket(on_ping=unrelated)
        with mock.patch.object(m.time, "monotonic", side_effect=lambda: sock.now):
            ws = m.WebSocket(sock)
            self.assertEqual(ws.recv(), b"busy")
            self.assertEqual(ws._last_pong, 0); self.assertIsNotNone(ws._ping_nonce)
            self.assertEqual([op for op, _, _ in sock.sent], [9, 10])
            self.assertEqual(sock.sent[-1][1], b"peer-ping")
            sock.now = 30
            with self.assertRaisesRegex(m.TransportError, "heartbeat timed out"):
                ws.recv()
        self.assertEqual([op for op, _, _ in sock.sent], [9, 10, 8])

    def test_unsolicited_pong_does_not_extend_idle_timer(self):
        sock = ClockSocket(frame(b"unsolicited", opcode=10) + frame(b"data"))
        with mock.patch.object(m.time, "monotonic", side_effect=lambda: sock.now):
            ws = m.WebSocket(sock); sock.now = 10
            self.assertEqual(ws.recv(), b"data"); self.assertEqual(ws._last_pong, 0)
            with self.assertRaisesRegex(m.TransportError, "heartbeat timed out"):
                ws.recv()
        self.assertEqual(sock.sent[0][2], 15)
        self.assertEqual(sock.now, 30)

    def test_incomplete_frame_cannot_hide_a_blackhole_inside_read(self):
        sock = ClockSocket(frame(b"unfinished")[:5])
        with mock.patch.object(m.time, "monotonic", side_effect=lambda: sock.now):
            ws = m.WebSocket(sock)
            with self.assertRaisesRegex(m.TransportError, "heartbeat timed out"):
                ws.recv()
        self.assertEqual(sock.now, 30); self.assertTrue(sock.closed)
        self.assertEqual([op for op, _, _ in sock.sent], [9, 8])


class WebSocketTests(unittest.TestCase):
    def pair(self, **kwargs):
        local, server = socket.socketpair(); server.settimeout(2)
        ws = m.WebSocket(local, **kwargs)
        self.addCleanup(server.close); self.addCleanup(ws.close)
        return ws, server

    def test_client_binary_mask_and_lengths(self):
        ws, server = self.pair()
        for payload in [b"", b"hello", bytes(range(256)) * 128]:
            # Socket buffers are bounded: consume while a full32KiB frame sends.
            result = []
            reader = threading.Thread(target=lambda: result.append(client_frame(server)))
            reader.start(); ws.send(payload); reader.join(2)
            self.assertEqual(result, [(2, payload)])

    def test_fragmentation_and_interleaved_ping(self):
        ws, server = self.pair()
        server.sendall(frame(b"abc", final=False) + frame(b"alive", opcode=9) + frame(b"def", opcode=0))
        self.assertEqual(ws.recv(), b"abcdef")
        self.assertEqual(client_frame(server), (10, b"alive"))

    def test_nonce_credit_envelope_is_not_game_data(self):
        ws, server = self.pair(credit_ack=True)
        nonce = b"12345678"; payload = b"\x02\x01\x00\x00\x00game"
        server.sendall(frame(nonce[:3], final=False) + frame(nonce[3:] + payload, opcode=0))
        self.assertEqual(ws.recv(), payload)
        with self.assertRaises(m.TransportError):
            ws.ack(len(payload) + 1)
        ws.ack(len(payload))
        opcode, ack = client_frame(server)
        self.assertEqual(opcode, 1)
        self.assertEqual(json.loads(ack), {"ack": nonce.hex()})

    def test_credit_bound_counts_payload_plus_envelope(self):
        ws, server = self.pair(max_message=3, credit_ack=True)
        server.sendall(frame(b"12345678abc"))
        self.assertEqual(ws.recv(), b"abc"); ws.ack(3); client_frame(server)
        server.sendall(frame(b"12345678abcd"))
        with self.assertRaises(m.TransportError):
            ws.recv()
        self.assertEqual(struct.unpack("!H", client_frame(server)[1][:2])[0], 1009)

    def test_malformed_frames_fail_closed(self):
        cases = [b"\x82\x80" + b"mask", b"\xc2\x00", frame(b"text", opcode=1),
                 frame(b"x", opcode=0), frame(b"x", opcode=9, final=False),
                 b"\x82\x7e\x00\x01x", b"\x82\x7f" + struct.pack("!Q", 65537),
                 frame(b"x", opcode=8), frame(struct.pack("!H", 1005), opcode=8),
                 frame(struct.pack("!H", 1000) + b"\xff", opcode=8)]
        for bad in cases:
            with self.subTest(bad=bad[:2]):
                ws, server = self.pair(); server.sendall(bad)
                with self.assertRaises(m.TransportError):
                    ws.recv()
                self.assertTrue(ws._closed.is_set())

    def test_fragment_state_and_reassembled_size(self):
        for data in [frame(b"abc", final=False) + frame(b"x", opcode=2),
                     frame(b"abc", final=False) + frame(b"def", opcode=0)]:
            ws, server = self.pair(max_message=5); server.sendall(data)
            with self.assertRaises(m.TransportError):
                ws.recv()

    def test_normal_close_echo_and_eof(self):
        ws, server = self.pair(); close = struct.pack("!H", 1000) + b"done"
        server.sendall(frame(close, opcode=8)); self.assertIsNone(ws.recv())
        self.assertEqual(client_frame(server), (8, close))

    def handshake(self, mutate=None):
        listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen(1)
        self.addCleanup(listener.close); observed = []; failures = []
        def serve():
            conn = None
            try:
                conn, _ = listener.accept(); conn.settimeout(2); request = bytearray()
                while b"\r\n\r\n" not in request:
                    part = conn.recv(1024)
                    if not part:
                        return
                    request.extend(part)
                lines = bytes(request).decode().split("\r\n")
                headers = dict(line.split(": ", 1) for line in lines[1:] if ": " in line)
                observed.append((lines[0], headers))
                accept = base64.b64encode(hashlib.sha1((headers["Sec-WebSocket-Key"] + m._GUID).encode()).digest()).decode()
                response = ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n").encode()
                if mutate:
                    response = mutate(response)
                conn.sendall(response + frame(b"ready")); client_frame(conn)
            except (OSError, EOFError):
                pass
            except Exception as error:
                failures.append(type(error).__name__)
            finally:
                if conn:
                    conn.close()
        thread = threading.Thread(target=serve, daemon=True); thread.start()
        self.addCleanup(thread.join, 2)
        return listener.getsockname()[1], observed, failures

    def test_valid_handshake_and_buffered_frame(self):
        port, observed, _ = self.handshake()
        ws = m.WebSocket.connect(f"ws://127.0.0.1:{port}/r/ABC234/host", token="a" * 64)
        self.assertEqual(ws.recv(), b"ready"); ws.close()
        self.assertEqual(observed[0][0], "GET /r/ABC234/host HTTP/1.1")
        self.assertEqual(observed[0][1]["Authorization"], "Bearer " + "a" * 64)
        self.assertEqual(observed[0][1]["User-Agent"], m.USER_AGENT)

    def test_invalid_accept_duplicate_header_and_status_hide_secrets(self):
        for mutate in [lambda b: b.replace(b"101 Switching", b"401 Refused"),
                       lambda b: b.replace(b"Sec-WebSocket-Accept: ", b"Sec-WebSocket-Accept: bad"),
                       lambda b: b.replace(b"Upgrade: websocket", b"Upgrade: websocket\r\nUpgrade: websocket")]:
            port, _, _ = self.handshake(mutate)
            with self.assertRaises(m.TransportError) as error:
                m.WebSocket.connect(f"ws://127.0.0.1:{port}/private", token="b" * 64)
            self.assertNotIn("b" * 64, str(error.exception)); self.assertNotIn("private", str(error.exception))

    def test_tls_context_validates_certificates_and_sni(self):
        ctx = m._context(None)
        self.assertEqual(ctx.verify_mode, ssl.CERT_REQUIRED); self.assertTrue(ctx.check_hostname)
        port, _, _ = self.handshake()
        context = mock.Mock(); context.wrap_socket.side_effect = lambda sock, server_hostname: sock
        with mock.patch.object(m, "_context", return_value=context):
            ws = m.WebSocket.connect(f"wss://localhost:{port}/", ca="test-ca.pem")
            self.assertEqual(ws.recv(), b"ready"); ws.close()
        self.assertEqual(context.wrap_socket.call_args.kwargs, {"server_hostname": "localhost"})

    def test_tls_certificate_failure_is_never_bypassed(self):
        port, _, _ = self.handshake()
        context = mock.Mock(); context.wrap_socket.side_effect = ssl.SSLCertVerificationError("private endpoint")
        with mock.patch.object(m, "_context", return_value=context):
            with self.assertRaises(m.TransportError) as error:
                m.WebSocket.connect(f"wss://localhost:{port}/", token="d" * 64)
        self.assertNotIn("private endpoint", str(error.exception)); self.assertNotIn("d" * 64, str(error.exception))

    def test_remote_plaintext_and_bad_url_are_rejected(self):
        for url in ["ws://example.com/", "http://example.com/", "https://a:b@example.com/", "https://example.com/#secret", "https://example.com/\r\nX: bad"]:
            with self.assertRaises(m.TransportError):
                m._url(url, websocket=url.startswith("ws"))
        for code in ["ABC123", "ABCDEF1", "abcdef", "AB/O34"]:
            with self.assertRaises(m.TransportError):
                m.GuestTunnel("http://127.0.0.1", code)


class TunnelTests(unittest.TestCase):
    def test_thread_start_failure_removes_unstarted_worker_before_owned_cleanup(self):
        ws = FakeWS()
        tunnel = m.HostTunnel("http://127.0.0.1", "ABC234", "a" * 64, 12345)
        with mock.patch.object(m.WebSocket, "connect", return_value=ws), \
                mock.patch.object(threading.Thread, "start", side_effect=RuntimeError("cannot start worker")):
            with self.assertRaisesRegex(RuntimeError, "cannot start worker"):
                tunnel.start()
        self.assertEqual(tunnel._threads, [])
        tunnel.close()  # Must not join an unstarted thread or lose its owned WebSocket.
        self.assertTrue(ws.closed); self.assertEqual(tunnel.status()["state"], "closed")

    def echo_backend(self):
        listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen(4); listener.settimeout(.2)
        stop = threading.Event(); sockets = []; threads = []
        def echo(conn):
            conn.settimeout(.2)
            try:
                while not stop.is_set():
                    try:
                        data = conn.recv(32768)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    conn.sendall(data)
            except OSError:
                pass
        def accept():
            while not stop.is_set():
                try:
                    conn, _ = listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                sockets.append(conn); thread = threading.Thread(target=echo, args=(conn,), daemon=True)
                threads.append(thread); thread.start()
        owner = threading.Thread(target=accept, daemon=True); owner.start()
        def cleanup():
            stop.set(); m._shutdown(listener)
            for conn in sockets:
                m._shutdown(conn)
            owner.join(1)
            for thread in threads:
                thread.join(1)
        self.addCleanup(cleanup)
        return listener.getsockname()[1]

    def test_host_channels_preserve_opaque_bytes_and_close(self):
        ws = FakeWS(); port = self.echo_backend()
        with mock.patch.object(m.WebSocket, "connect", return_value=ws):
            tunnel = m.HostTunnel("http://127.0.0.1", "ABC234", "a" * 64, port).start()
        self.addCleanup(tunnel.close)
        for channel, payload in [(1, b"native\x00\xff"), (2, b"second")]:
            ws.incoming.put(bytes((m.OPEN,)) + struct.pack("<I", channel))
            ws.incoming.put(bytes((m.DATA,)) + struct.pack("<I", channel) + payload)
        self.assertTrue(wait_for(lambda: len([x for x in ws.sent if x[0] == m.DATA]) == 2))
        self.assertEqual({x[1:5]: x[5:] for x in ws.sent if x[0] == m.DATA}, {struct.pack("<I", 1): b"native\x00\xff", struct.pack("<I", 2): b"second"})
        self.assertEqual(ws.acks[:4], [5, 13, 5, 11])
        ws.incoming.put(bytes((m.CLOSE,)) + struct.pack("<IH", 1, 1000))
        self.assertTrue(wait_for(lambda: tunnel.status()["connections"] == 1))
        self.assertNotIn("a" * 64, repr(tunnel.status()))
        tunnel.close(); self.assertTrue(all(not t.is_alive() for t in tunnel._threads))

    def test_host_rejects_overcapacity_or_malformed_mux(self):
        for frames in [[bytes((m.OPEN,)) + struct.pack("<I", i) for i in range(1, 5)], [b"bad"], [bytes((m.OPEN,)) + struct.pack("<I", 0)]]:
            ws = FakeWS(); port = self.echo_backend()
            with mock.patch.object(m.WebSocket, "connect", return_value=ws):
                tunnel = m.HostTunnel("http://127.0.0.1", "ABC234", "a" * 64, port).start()
            self.addCleanup(tunnel.close)
            for message in frames:
                ws.incoming.put(message)
            self.assertTrue(wait_for(lambda: tunnel.status()["state"] == "error"))
            tunnel.close(); self.assertEqual(tunnel.status()["connections"], 0)

    def test_guest_loopback_bridge_one_connection_and_cleanup(self):
        ws = FakeWS()
        with mock.patch.object(m.WebSocket, "connect", return_value=ws) as connect:
            tunnel = m.GuestTunnel("http://127.0.0.1", "ABC234").start(); self.addCleanup(tunnel.close)
            self.assertEqual(tunnel.address()[0], "127.0.0.1")
            native = socket.create_connection(tunnel.address()); native.settimeout(2); self.addCleanup(native.close)
            native.sendall(b"game bytes")
            self.assertTrue(wait_for(lambda: ws.sent == [b"game bytes"]))
            ws.incoming.put(b"reply\x00")
            self.assertEqual(exact(native, 6), b"reply\x00")
            self.assertTrue(wait_for(lambda: ws.acks == [6]))
            extra = socket.create_connection(tunnel.address()); extra.settimeout(2)
            self.assertEqual(extra.recv(1), b""); extra.close()
            self.assertTrue(connect.call_args.kwargs["credit_ack"])
            tunnel.close(); self.assertTrue(all(not t.is_alive() for t in tunnel._threads))

    def test_closed_tunnel_cannot_start_or_open_new_workers(self):
        tunnel = m.GuestTunnel("http://127.0.0.1", "ABC234"); tunnel.close()
        with self.assertRaises(m.TransportError):
            tunnel.start()
        self.assertFalse(tunnel._threads)

    def test_guest_remote_close_is_terminal_without_native_reconnect(self):
        ws = FakeWS()
        with mock.patch.object(m.WebSocket, "connect", return_value=ws):
            tunnel = m.GuestTunnel("http://127.0.0.1", "ABC234").start()
            self.addCleanup(tunnel.close)
            native = socket.create_connection(tunnel.address()); native.settimeout(2)
            self.addCleanup(native.close)
            self.assertTrue(wait_for(lambda: tunnel.status()["state"] == "running"))
            ws.incoming.put(None)
            self.assertTrue(wait_for(lambda: tunnel.status()["state"] == "error"))
            self.assertEqual(native.recv(1), b"")
            self.assertTrue(wait_for(lambda: tunnel.status()["connections"] == 0))
            self.assertEqual(tunnel.status()["state"], "error")

    def test_guest_local_eof_keeps_bridge_for_same_room_reconnect(self):
        first, second = FakeWS(), FakeWS()
        with mock.patch.object(m.WebSocket, "connect", side_effect=[first, second]):
            tunnel = m.GuestTunnel("http://127.0.0.1", "ABC234").start()
            self.addCleanup(tunnel.close)
            native = socket.create_connection(tunnel.address())
            self.assertTrue(wait_for(lambda: tunnel.status()["state"] == "running"))
            native.close()
            self.assertTrue(wait_for(lambda: tunnel.status()["state"] == "listening"))
            again = socket.create_connection(tunnel.address()); self.addCleanup(again.close)
            again.sendall(b"reconnected")
            self.assertTrue(wait_for(lambda: second.sent == [b"reconnected"]))

    def test_create_room_local_fixture_and_response_bounds(self):
        class Handler(http.server.BaseHTTPRequestHandler):
            payload = {"version": 1, "code": "ABC234", "host_token": "c" * 64,
                       "credit_ack": True, "envelope_bytes": 8, "max_guests": 3, "max_data_bytes": 32768}
            def log_message(self, *args):
                pass
            def do_POST(self):
                self.server.agent = self.headers.get("User-Agent")
                self.server.seen = (self.path, self.headers.get("Authorization"), self.rfile.read(int(self.headers.get("Content-Length", 0))))
                data = json.dumps(self.payload).encode(); self.send_response(201)
                self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        self.addCleanup(thread.join, 2); self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
        room = m.create_room("http://127.0.0.1:%d" % server.server_port)
        self.assertEqual(room["code"], "ABC234"); self.assertEqual(server.seen, ("/api/rooms", None, b""))
        self.assertEqual(server.agent, m.USER_AGENT)
        Handler.payload = {"version": 1, "code": "bad", "host_token": "secret"}
        with self.assertRaises(m.TransportError) as error:
            m.create_room("http://127.0.0.1:%d" % server.server_port)
        self.assertNotIn("secret", str(error.exception))


if __name__ == "__main__":
    unittest.main()
