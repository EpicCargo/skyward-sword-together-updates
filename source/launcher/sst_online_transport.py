"""Bounded stdlib WebSocket transport for the opaque SST room relay.

Native protocol bytes remain opaque. Host backends and guest listeners are always
loopback; room tokens are kept in memory and never included in status/errors.
"""
import base64
import hashlib
import ipaddress
import json
import re
import secrets
import select
import socket
import ssl
import struct
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

MAX_DATA = 32768
MAX_FRAME = 65536
MAX_BUFFER = 256 * 1024
MAX_GUESTS = 3
USER_AGENT = "SkywardSwordTogether/1"
PING_INTERVAL = 15.0
HEARTBEAT_TIMEOUT = 30.0
OPEN, DATA, CLOSE = 1, 2, 3
_CODE = re.compile(r"[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}\Z")
_TOKEN = re.compile(r"[0-9a-f]{64}\Z")
_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class TransportError(Exception):
    """Public errors deliberately contain no URLs, room tokens or game bytes."""
    def __init__(self, message, close_code=1002):
        super().__init__(message)
        self.close_code = close_code


def _loopback(host):
    if host == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _url(value, websocket=False):
    if not isinstance(value, str) or any(c in value for c in "\r\n\x00"):
        raise TransportError("Invalid relay address")
    try:
        p = urllib.parse.urlsplit(value)
        allowed = ("wss", "ws") if websocket else ("https", "http")
        if p.scheme not in allowed or not p.hostname or p.username or p.password or p.fragment:
            raise ValueError()
        port = p.port or (443 if p.scheme in ("https", "wss") else 80)
        if not 1 <= port <= 65535 or (p.scheme in ("http", "ws") and not _loopback(p.hostname)):
            raise ValueError()
        return p, port
    except (ValueError, AttributeError):
        raise TransportError("Use a secure relay address; development HTTP is loopback only") from None


def _endpoint(base, path, websocket=False):
    p, _ = _url(base)
    if p.query:
        raise TransportError("Relay base address cannot contain a query")
    scheme = ("wss" if p.scheme == "https" else "ws") if websocket else p.scheme
    return urllib.parse.urlunsplit((scheme, p.netloc, p.path.rstrip("/") + path, "", ""))


def _context(ca):
    try:
        return ssl.create_default_context(cafile=str(ca) if ca is not None else None)
    except (OSError, ssl.SSLError):
        raise TransportError("Certificate trust could not be loaded") from None


def _code(value):
    if not isinstance(value, str) or not _CODE.fullmatch(value):
        raise TransportError("Invalid room code")
    return value


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def create_room(relay_url, ca=None, timeout=10):
    """POST /api/rooms. Caller owns room code/token; neither is ever logged here."""
    endpoint = _endpoint(relay_url, "/api/rooms")
    opener = urllib.request.build_opener(_NoRedirect(), urllib.request.HTTPSHandler(context=_context(ca)))
    req = urllib.request.Request(endpoint, data=b"", method="POST",
                                 headers={"Accept": "application/json", "User-Agent": USER_AGENT})
    try:
        with opener.open(req, timeout=timeout) as response:
            if response.status != 201 and response.status != 200:
                raise TransportError("Room creation was refused")
            raw = response.read(4097)
        if len(raw) > 4096:
            raise ValueError()
        result = json.loads(raw)
        _code(result.get("code"))
        if result.get("version") != 1 or not isinstance(result.get("host_token"), str) or not _TOKEN.fullmatch(result["host_token"]):
            raise ValueError()
        if result.get("max_guests", MAX_GUESTS) != MAX_GUESTS or result.get("max_data_bytes", MAX_DATA) != MAX_DATA:
            raise ValueError()
        if result.get("credit_ack") is not True or result.get("envelope_bytes") != 8:
            raise ValueError()
        # Returned socket URLs are informational; tunnels use the configured origin.
        return result
    except TransportError:
        raise
    except (OSError, ValueError, TypeError, AttributeError, urllib.error.URLError):
        raise TransportError("Could not create a secure relay room") from None


def _shutdown(sock):
    if sock is None:
        return
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    try:
        sock.close()
    except OSError:
        pass


def _bound_socket(sock):
    # Linux may double these requested kernel buffers, still below256KiB each.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
    sock.settimeout(1)


def _close_payload(code, reason=""):
    if code not in (1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014) and not 3000 <= code <= 4999:
        raise TransportError("Invalid relay close code")
    try:
        raw = reason.encode("utf-8")
    except (AttributeError, UnicodeError):
        raise TransportError("Invalid relay close reason") from None
    if len(raw) > 96:
        raise TransportError("Invalid relay close reason")
    return struct.pack("!H", code) + raw


class _TLSIO:
    """Serialize TLS record calls; wait for network readiness outside the lock."""
    def __init__(self, sock):
        self.sock = sock
        self._mu = threading.Lock()
        self._closed = False
        sock.setblocking(False)

    def _wait(self, readable, deadline):
        if self._closed:
            raise OSError("TLS stream closed")
        left = deadline - time.monotonic()
        if left <= 0:
            raise socket.timeout("TLS operation timed out")
        try:
            # Retry periodically: another serialized TLS call may consume a
            # control record and make plaintext available without a new TCP byte.
            select.select([self.sock] if readable else [], [] if readable else [self.sock],
                          [self.sock], min(.05, left))
        except ValueError:
            raise OSError("TLS stream closed") from None

    def recv(self, size):
        deadline = time.monotonic() + 1
        while True:
            try:
                with self._mu:
                    if self._closed:
                        raise OSError("TLS stream closed")
                    return self.sock.recv(size)
            except ssl.SSLWantReadError:
                self._wait(True, deadline)
            except ssl.SSLWantWriteError:
                self._wait(False, deadline)

    def sendall(self, data):
        deadline = time.monotonic() + 1
        remaining = memoryview(data)
        while remaining:
            if time.monotonic() >= deadline:
                raise socket.timeout("TLS operation timed out")
            try:
                with self._mu:
                    if self._closed:
                        raise OSError("TLS stream closed")
                    count = self.sock.send(remaining)
                if not count:
                    raise OSError("TLS stream closed")
                remaining = remaining[count:]
            except ssl.SSLWantReadError:
                self._wait(True, deadline)
            except ssl.SSLWantWriteError:
                self._wait(False, deadline)

    def shutdown(self, how):
        with self._mu:
            self._closed = True
            self.sock.shutdown(how)

    def close(self):
        with self._mu:
            self._closed = True
            self.sock.close()


class WebSocket:
    def __init__(self, sock, buffered=b"", max_message=MAX_DATA + 5, credit_ack=False):
        if not 1 <= max_message <= MAX_FRAME:
            raise ValueError("Invalid message limit")
        self.sock = sock
        self.max_message = max_message
        self.credit_ack = credit_ack
        self._credit = None
        self._buffer = bytearray(buffered)
        self._closed = threading.Event()
        self._send_mu = threading.Lock()
        self._last_pong = time.monotonic()
        self._ping_nonce = None
        _bound_socket(sock)
        if isinstance(sock, ssl.SSLSocket):
            self.sock = _TLSIO(sock)

    @classmethod
    def connect(cls, url, token=None, ca=None, timeout=10, max_message=MAX_DATA + 5,
                _addresses=None, _cancel=None, credit_ack=False):
        p, port = _url(url, websocket=True)
        if token is not None and (not isinstance(token, str) or not _TOKEN.fullmatch(token)):
            raise TransportError("Invalid room credentials")
        sock = None
        try:
            addresses = _addresses or socket.getaddrinfo(p.hostname, port, type=socket.SOCK_STREAM)
            deadline = time.monotonic() + timeout
            for family, kind, protocol, _, address in addresses[:4]:
                if _cancel is not None and _cancel.is_set():
                    raise TransportError("Relay connection cancelled")
                sock = socket.socket(family, kind, protocol)
                sock.settimeout(max(.05, deadline - time.monotonic()))
                try:
                    sock.connect(address)
                    break
                except OSError:
                    _shutdown(sock); sock = None
            if sock is None or time.monotonic() >= deadline:
                raise TransportError("Could not connect to the relay")
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            sock.settimeout(max(.05, deadline - time.monotonic()))
            if p.scheme == "wss":
                sock = _context(ca).wrap_socket(sock, server_hostname=p.hostname)
            key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
            target = p.path or "/"
            if p.query:
                target += "?" + p.query
            request = [f"GET {target} HTTP/1.1", f"Host: {p.netloc}", "Upgrade: websocket",
                       "Connection: Upgrade", f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13",
                       "User-Agent: " + USER_AGENT]
            if token is not None:
                request.append("Authorization: Bearer " + token)
            sock.sendall(("\r\n".join(request) + "\r\n\r\n").encode("ascii"))
            header = bytearray()
            while b"\r\n\r\n" not in header:
                if _cancel is not None and _cancel.is_set():
                    raise TransportError("Relay connection cancelled")
                sock.settimeout(max(.05, deadline - time.monotonic()))
                chunk = sock.recv(4096)
                if not chunk or len(header) + len(chunk) > 16384 or time.monotonic() >= deadline:
                    raise TransportError("Invalid relay handshake")
                header.extend(chunk)
            head, buffered = bytes(header).split(b"\r\n\r\n", 1)
            lines = head.decode("iso-8859-1").split("\r\n")
            status = lines.pop(0).split(" ", 2)
            if status[:2] != ["HTTP/1.1", "101"]:
                raise TransportError("Room unavailable, full, expired, or credentials refused")
            fields = {}
            for line in lines:
                if ":" not in line or line[0].isspace():
                    raise TransportError("Invalid relay handshake")
                name, value = line.split(":", 1); name = name.lower().strip()
                if name in fields and name in ("upgrade", "connection", "sec-websocket-accept"):
                    raise TransportError("Invalid relay handshake")
                fields[name] = value.strip()
            accept = base64.b64encode(hashlib.sha1((key + _GUID).encode("ascii")).digest()).decode("ascii")
            if fields.get("upgrade", "").lower() != "websocket" or "upgrade" not in [v.strip().lower() for v in fields.get("connection", "").split(",")] or fields.get("sec-websocket-accept") != accept or "sec-websocket-extensions" in fields or "sec-websocket-protocol" in fields:
                raise TransportError("Invalid relay handshake")
            if _cancel is not None and _cancel.is_set():
                raise TransportError("Relay connection cancelled")
            return cls(sock, buffered, max_message, credit_ack)
        except TransportError:
            _shutdown(sock); raise
        except (OSError, ValueError, UnicodeError):
            _shutdown(sock)
            raise TransportError("Secure relay connection failed") from None

    def _abort(self):
        self._closed.set(); _shutdown(self.sock)

    def _read(self, count):
        self._heartbeat()
        while len(self._buffer) < count:
            if self._closed.is_set():
                raise EOFError()
            try:
                part = self.sock.recv(min(65536, count - len(self._buffer)))
            except socket.timeout:
                self._heartbeat()
                continue
            except OSError:
                raise EOFError() from None
            if not part:
                raise EOFError()
            self._buffer.extend(part)
            self._heartbeat()
        result = bytes(self._buffer[:count]); del self._buffer[:count]
        return result

    def _send(self, opcode, data):
        if not isinstance(data, bytes):
            raise TypeError("WebSocket payload must be bytes")
        if len(data) > (125 if opcode >= 8 else 64 if opcode == 1 else self.max_message):
            raise TransportError("Relay message exceeds its limit")
        with self._send_mu:
            if self._closed.is_set():
                raise TransportError("Relay is closed")
            size = len(data); head = bytes((0x80 | opcode, 0x80 | (size if size < 126 else 126 if size < 65536 else 127)))
            if 126 <= size < 65536:
                head += struct.pack("!H", size)
            elif size >= 65536:
                head += struct.pack("!Q", size)
            mask = secrets.token_bytes(4)
            body = bytes(value ^ mask[i % 4] for i, value in enumerate(data))
            try:
                self.sock.sendall(head + mask + body)
            except OSError:
                self._abort()
                raise TransportError("Relay connection stopped") from None

    def send(self, data):
        self._send(2, data)

    def ack(self, count):
        if not self.credit_ack:
            return
        if not isinstance(count, int) or self._credit is None or count != self._credit[1]:
            raise TransportError("Invalid relay acknowledgement")
        nonce, _ = self._credit
        self._send(1, ("{\"ack\":\"%s\"}" % nonce.hex()).encode("ascii"))
        self._credit = None

    def _deliver(self, data):
        if self.credit_ack:
            if len(data) < 8 or self._credit is not None:
                raise TransportError("Invalid relay credit envelope")
            self._credit = (data[:8], len(data) - 8)
            return data[8:]
        return data

    def ping(self, data=b""):
        self._send(9, data)

    def _heartbeat(self):
        # Only the receiver owns these fields. No extra worker or application
        # message: Cloudflare responds to protocol Pings without waking the DO.
        if self._closed.is_set():
            return
        age = time.monotonic() - self._last_pong
        if age >= HEARTBEAT_TIMEOUT:
            raise TransportError("Relay heartbeat timed out", 1001)
        if self._ping_nonce is None and age >= PING_INTERVAL:
            self._ping_nonce = secrets.token_bytes(8)
            self.ping(self._ping_nonce)

    def recv(self):
        fragments = None
        try:
            while not self._closed.is_set():
                self._heartbeat()
                a, b = self._read(2)
                final, opcode = bool(a & 128), a & 15
                if a & 112 or b & 128 or opcode not in (0, 1, 2, 8, 9, 10):
                    raise TransportError("Invalid relay frame")
                size = b & 127
                if size == 126:
                    size = struct.unpack("!H", self._read(2))[0]
                    if size < 126:
                        raise TransportError("Invalid relay frame length")
                elif size == 127:
                    size = struct.unpack("!Q", self._read(8))[0]
                    if size < 65536 or size >> 63:
                        raise TransportError("Invalid relay frame length")
                if size > MAX_FRAME:
                    raise TransportError("Relay frame exceeds its limit", 1009)
                if opcode >= 8 and (not final or size > 125):
                    raise TransportError("Invalid relay control frame")
                if opcode < 8 and size + (len(fragments) if fragments is not None else 0) > self.max_message + (8 if self.credit_ack else 0):
                    raise TransportError("Relay message exceeds its limit", 1009)
                data = self._read(size)
                if opcode == 8:
                    if len(data) == 1:
                        raise TransportError("Invalid relay close frame")
                    if data:
                        code = struct.unpack("!H", data[:2])[0]
                        _close_payload(code)
                        try:
                            data[2:].decode("utf-8")
                        except UnicodeError:
                            raise TransportError("Invalid relay close reason", 1007) from None
                    self._send(8, data); self._abort(); return None
                if opcode == 9:
                    self._send(10, data); continue
                if opcode == 10:
                    if self._ping_nonce is not None and secrets.compare_digest(data, self._ping_nonce):
                        self._ping_nonce = None
                        self._last_pong = time.monotonic()
                    continue
                if opcode == 1 or (opcode == 0 and fragments is None) or (opcode == 2 and fragments is not None):
                    raise TransportError("Unexpected relay message type", 1003 if opcode == 1 else 1002)
                if opcode == 2:
                    if final:
                        return self._deliver(data)
                    fragments = bytearray(data)
                else:
                    fragments.extend(data)
                    if final:
                        return self._deliver(bytes(fragments))
        except EOFError:
            self._abort(); return None
        except TransportError as error:
            self.close(error.close_code); raise
        return None

    def close(self, code=1000, reason=""):
        if not self._closed.is_set():
            try:
                self._send(8, _close_payload(code, reason))
            except (OSError, TransportError):
                pass
            self._abort()


class _Tunnel:
    def __init__(self, relay_url, code, ca, acknowledge=True):
        self.relay_url = relay_url; self.code = _code(code); self.ca = ca
        self.acknowledge = acknowledge
        _endpoint(relay_url, "/")
        self._stop = threading.Event(); self._mu = threading.Lock()
        self._threads = []; self._state = "new"; self._message = "Not started"
        self._sent = self._received = 0; self._started = False

    def _begin_start(self):
        with self._mu:
            if self._started or self._stop.is_set():
                raise TransportError("Tunnel already started")
            self._started = True

    def _thread(self, target, name, *args):
        thread = threading.Thread(target=target, args=args, name=name, daemon=True)
        with self._mu:
            if self._stop.is_set():
                return False
            self._threads = [t for t in self._threads if t.is_alive()]
            if len(self._threads) >= 6:
                raise TransportError("Local bridge worker limit reached")
            self._threads.append(thread)
            # Publish only a started thread: close() can safely join its snapshot.
            try:
                thread.start()
            except Exception:
                self._threads.remove(thread)
                raise
            return True

    def _set(self, state, message):
        with self._mu:
            if not self._stop.is_set():
                self._state = state; self._message = message

    def _fail(self, message):
        with self._mu:
            if not self._stop.is_set():
                # Completions cannot publish an idle state between this error
                # and its stop flag, or leave a dead bridge looking available.
                self._state = "error"; self._message = message
                self._stop.set()

    def _count(self, sent=0, received=0):
        with self._mu:
            self._sent += sent; self._received += received

    def _join(self):
        deadline = time.monotonic() + 3
        with self._mu:
            threads = list(self._threads)
        for thread in threads:
            if thread is not threading.current_thread():
                thread.join(max(0, deadline - time.monotonic()))
        with self._mu:
            self._state = "closed"; self._message = "Closed"

    def status(self):
        with self._mu:
            return {"state": self._state, "message": self._message,
                    "connections": self._connections(), "bytes_sent": self._sent,
                    "bytes_received": self._received}


class HostTunnel(_Tunnel):
    def __init__(self, relay_url, code, token, backend_port, ca=None, acknowledge=True):
        super().__init__(relay_url, code, ca, acknowledge)
        if not isinstance(token, str) or not _TOKEN.fullmatch(token):
            raise TransportError("Invalid room credentials")
        if not isinstance(backend_port, int) or not 1 <= backend_port <= 65535:
            raise TransportError("Invalid local game port")
        self._token = token; self.backend_port = backend_port
        self._ws = None; self._channels = {}; self._last_channel = 0

    def address(self):
        return ("127.0.0.1", self.backend_port)

    def _connections(self):
        return len(self._channels)

    def start(self):
        self._begin_start()
        ws = None
        started = False
        try:
            ws = WebSocket.connect(_endpoint(self.relay_url, "/r/" + self.code + "/host", True),
                                   token=self._token, ca=self.ca, timeout=5, credit_ack=self.acknowledge,
                                   _cancel=self._stop)
            with self._mu:
                if self._stop.is_set():
                    raise TransportError("Relay connection cancelled")
                self._ws = ws
            self._set("running", "Room relay connected")
            if not self._thread(self._receive, "sst-room-host"):
                raise TransportError("Relay connection cancelled")
            started = True
            return self
        finally:
            if not started:
                try:
                    if ws:
                        ws.close()
                finally:
                    self.close()

    def _drop(self, channel, notify=False, expected=None, close_code=1000):
        with self._mu:
            current = self._channels.get(channel)
            removed = current is not None and (expected is None or current is expected)
            backend = self._channels.pop(channel) if removed else expected
        # A reader owns its captured backend, not a later connection occupying
        # the same dictionary key. Late completion must never close that socket.
        _shutdown(backend)
        if removed and notify and not self._stop.is_set():
            try:
                self._ws.send(bytes((CLOSE,)) + struct.pack("<IH", channel, close_code))
            except TransportError:
                pass

    def _read_backend(self, channel, backend):
        try:
            while not self._stop.is_set():
                try:
                    data = backend.recv(MAX_DATA)
                except socket.timeout:
                    continue
                if not data:
                    break
                self._ws.send(bytes((DATA,)) + struct.pack("<I", channel) + data)
                self._count(sent=len(data))
        except (OSError, TransportError):
            pass
        finally:
            self._drop(channel, notify=True, expected=backend)

    def _receive(self):
        try:
            while not self._stop.is_set():
                message = self._ws.recv()
                if message is None:
                    break
                if len(message) < 5:
                    raise TransportError("Invalid room channel message")
                opcode = message[0]; channel = struct.unpack("<I", message[1:5])[0]
                if not channel:
                    raise TransportError("Invalid room channel")
                with self._mu:
                    backend = self._channels.get(channel); count = len(self._channels)
                if opcode == OPEN:
                    if len(message) != 5 or backend is not None or count >= MAX_GUESTS or channel <= self._last_channel:
                        raise TransportError("Invalid room channel opening")
                    # The relay contract never reuses IDs, including an OPEN
                    # whose local backend connection failed. Gaps are valid.
                    self._last_channel = channel
                    try:
                        backend = socket.create_connection(("127.0.0.1", self.backend_port), timeout=2)
                        _bound_socket(backend)
                    except OSError:
                        _shutdown(backend)
                        self._ws.send(bytes((CLOSE,)) + struct.pack("<IH", channel, 1011))
                    else:
                        with self._mu:
                            publish = not self._stop.is_set()
                            if publish:
                                self._channels[channel] = backend
                        if not publish:
                            _shutdown(backend); break
                        try:
                            worker_started = self._thread(self._read_backend, "sst-room-backend", channel, backend)
                        except (RuntimeError, TransportError):
                            # A transient local worker shortage affects this guest,
                            # not the established channels of the whole room.
                            self._drop(channel, notify=True, expected=backend, close_code=1013)
                        else:
                            if not worker_started:
                                self._drop(channel, expected=backend)
                elif opcode == DATA:
                    if len(message) - 5 > MAX_DATA:
                        raise TransportError("Room data exceeds its limit")
                    if backend is not None:
                        try:
                            backend.sendall(message[5:])
                            self._count(received=len(message) - 5)
                        except OSError:
                            self._drop(channel, notify=True, expected=backend)
                elif opcode == CLOSE:
                    if not 7 <= len(message) <= 103:
                        raise TransportError("Invalid room channel close")
                    _close_payload(struct.unpack("<H", message[5:7])[0])
                    message[7:].decode("utf-8")
                    self._drop(channel)
                else:
                    raise TransportError("Invalid room channel operation")
                if self.acknowledge:
                    self._ws.ack(len(message))
        except (OSError, UnicodeError, TransportError, RuntimeError):
            self._set("error", "Room relay disconnected")
        finally:
            if not self._stop.is_set():
                self._set("error", "Room relay disconnected")
            self._stop.set(); self._ws.close()
            with self._mu:
                channels = list(self._channels)
            for channel in channels:
                self._drop(channel)

    def close(self):
        self._stop.set()
        if self._ws:
            self._ws.close()
        with self._mu:
            channels = list(self._channels)
        for channel in channels:
            self._drop(channel)
        self._join()


class GuestTunnel(_Tunnel):
    def __init__(self, relay_url, code, ca=None, acknowledge=True):
        super().__init__(relay_url, code, ca, acknowledge)
        self._listener = None; self._local = None; self._ws = None
        self._addresses = None; self._bound = None
        self._session_left = 0

    def address(self):
        return self._bound

    def _connections(self):
        return int(self._local is not None)

    def start(self):
        self._begin_start()
        url = _endpoint(self.relay_url, "/r/" + self.code + "/join", True)
        p, port = _url(url, websocket=True)
        # Resolve before owning worker threads: a blocked system DNS resolver cannot
        # leave an uninterruptible resolver inside tunnel shutdown.
        listener = None
        started = False
        try:
            addresses = socket.getaddrinfo(p.hostname, port, type=socket.SOCK_STREAM)
            if self._stop.is_set():
                raise TransportError("Relay connection cancelled")
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(.25)
            bound = listener.getsockname()
            with self._mu:
                if self._stop.is_set():
                    raise TransportError("Relay connection cancelled")
                self._addresses = addresses; self._listener = listener; self._bound = bound
            self._set("listening", "Local game bridge ready")
            if not self._thread(self._accept, "sst-room-listener", url):
                raise TransportError("Relay connection cancelled")
            started = True
            return self
        except OSError:
            raise TransportError("Could not open the local game bridge") from None
        finally:
            if not started:
                _shutdown(listener)
                self.close()

    def _accept(self, url):
        while not self._stop.is_set():
            try:
                local, _ = self._listener.accept()
            except socket.timeout:
                continue
            except OSError:
                self._fail("Local game bridge stopped")
                _shutdown(self._listener)
                break
            with self._mu:
                busy = self._local is not None
            if busy:
                _shutdown(local); continue
            ws = None; published = False; started = 0
            local_ended = threading.Event()
            try:
                ws = WebSocket.connect(url, ca=self.ca, timeout=2, max_message=MAX_DATA,
                                       _addresses=self._addresses, _cancel=self._stop, credit_ack=self.acknowledge)
                _bound_socket(local)
                with self._mu:
                    if self._stop.is_set():
                        break
                    self._local = local; self._ws = ws; self._session_left = 2; published = True
                self._set("running", "Room relay connected")
                started += int(self._thread(self._read_local, "sst-room-game", local, ws, local_ended))
                started += int(self._thread(self._receive, "sst-room-guest", local, ws, local_ended))
            except (OSError, TransportError, RuntimeError):
                if published:
                    self._fail("Could not join the room relay")
                    _shutdown(self._listener)
                else:
                    self._set("error", "Could not join the room relay")
            finally:
                if not published:
                    _shutdown(local)
                    if ws:
                        ws.close()
                elif started < 2:
                    # Each published session reserves two worker completions.
                    # Account for workers that failed or were suppressed by close().
                    local_ended.set(); _shutdown(local); ws.close()
                    for _ in range(2 - started):
                        self._finish(local, ws)

    def _finish(self, local, ws):
        _shutdown(local); ws.close()
        with self._mu:
            if self._local is local:
                self._session_left -= 1
                if not self._session_left:
                    self._local = None; self._ws = None
                    # Publish the idle state with the empty slot. A new accept
                    # may run immediately after unlocking and set its own status.
                    if not self._stop.is_set():
                        self._state = "listening"; self._message = "Local game bridge ready"

    def _read_local(self, local, ws, local_ended):
        try:
            while not self._stop.is_set():
                try:
                    data = local.recv(MAX_DATA)
                except socket.timeout:
                    continue
                except OSError:
                    local_ended.set()
                    break
                if not data:
                    local_ended.set()
                    break
                ws.send(data); self._count(sent=len(data))
        except (OSError, TransportError):
            pass
        finally:
            self._finish(local, ws)

    def _receive(self, local, ws, local_ended):
        remote_ended = False
        try:
            while not self._stop.is_set():
                try:
                    data = ws.recv()
                except (OSError, TransportError):
                    remote_ended = True
                    break
                if data is None:
                    remote_ended = True
                    break
                try:
                    local.sendall(data)
                except OSError:
                    local_ended.set()
                    break
                self._count(received=len(data))
                if self.acknowledge:
                    ws.ack(len(data))
        except (OSError, TransportError):
            remote_ended = True
        finally:
            # Remote closure ends this room. Local game EOF alone may reopen the
            # bridge for that game's reconnect without losing the invitation.
            if remote_ended and not local_ended.is_set() and not self._stop.is_set():
                self._fail("Online room disconnected")
                _shutdown(self._listener)
            self._finish(local, ws)

    def close(self):
        self._stop.set(); _shutdown(self._listener)
        with self._mu:
            local, ws = self._local, self._ws
        _shutdown(local)
        if ws:
            ws.close()
        self._join()
