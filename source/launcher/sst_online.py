"""Launcher-owned online rooms. Native protocol stays in the current SST host."""
import json
import os
from pathlib import Path
import queue
import stat
import subprocess
import threading

if __package__:
    from .sst_online_transport import HostTunnel, GuestTunnel, create_room, TransportError, _code
else:
    from sst_online_transport import HostTunnel, GuestTunnel, create_room, TransportError, _code


class OnlineSession:
    def __init__(self, service, *, helper=None, ca=None, lease=None):
        self.service = service
        self.helper = Path(helper) if helper is not None else None
        self.ca = ca
        self.role = self.code = ""
        self.tunnel = None
        self.process = None
        self.lock = threading.RLock()
        self._stop = threading.Event()
        self._monitor = None
        self._error = ""
        self.lease = Path(lease) if lease is not None else None
        self._lease_id = None

    def _open_lease(self):
        if self.lease:
            self.lease.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(self.lease, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            try:
                info = os.fstat(fd)
                self._lease_id = (info.st_dev, info.st_ino)
            finally:
                os.close(fd)

    def _owns_lease(self):
        if self.lease is None or self._lease_id is None:
            return False
        info = self.lease.lstat()
        return stat.S_ISREG(info.st_mode) and (info.st_dev, info.st_ino) == self._lease_id

    def _watch(self):
        while not self._stop.wait(.2):
            with self.lock:
                if self._stop.is_set():
                    return
                tunnel, process = self.tunnel, self.process
                failed = tunnel and tunnel.status().get("state") in {"error", "closed", "failed"}
                backend_dead = process is not None and process.poll() is not None
                if not failed and not backend_dead:
                    if self.lease:
                        try:
                            if not self._owns_lease():
                                raise OSError("Online lease ownership changed")
                            os.utime(self.lease, None)
                        except OSError:
                            self._error = "The online session lease is unavailable"
                            failed = True
                    if not failed:
                        continue
                if not self._error:
                    self._error = "Online room disconnected" if failed else "The private session host closed"
            # Internet loss must also close the host's private TCP backend. Otherwise
            # the creator's game would falsely remain online in an isolated room.
            self.close()
            return

    def _start_monitor(self):
        self._monitor = threading.Thread(target=self._watch, daemon=True, name="sst-room-lifecycle")
        self._monitor.start()

    def _backend(self, code):
        if self.helper is None or not self.helper.is_file():
            raise TransportError("Install an online-enabled SST runtime before hosting")
        ready = queue.Queue(maxsize=1)
        self.process = subprocess.Popen(
            [str(self.helper), "--room", code, "--port", "0", "--bind", "127.0.0.1"],
            cwd=self.helper.parent, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        process = self.process

        def read_ready():
            try:
                line = process.stdout.readline(4097)
                data = json.loads(line) if len(line) <= 4096 else None
            except (OSError, ValueError):
                data = None
            try:
                ready.put_nowait(data)
            except queue.Full:
                pass

        thread = threading.Thread(target=read_ready, daemon=True, name="sst-host-ready")
        thread.start()
        try:
            data = ready.get(timeout=5)
        except queue.Empty:
            raise TransportError("The private session host did not start") from None
        if not isinstance(data, dict) or data.get("protocol") != 45 or type(data.get("port")) is not int or not 1 <= data["port"] <= 65535:
            raise TransportError("The private session host returned invalid startup data")
        return data["port"]

    def host(self):
        with self.lock:
            if self._stop.is_set():
                raise TransportError("Create a new online session")
            if self.tunnel:
                raise TransportError("Leave the current online room first")
            if self.helper is None or not self.helper.is_file():
                raise TransportError("Install an online-enabled SST runtime before hosting")
            try:
                room = create_room(self.service, self.ca)
                self.code = room["code"]
                port = self._backend(self.code)
                self.tunnel = HostTunnel(self.service, self.code, room["host_token"], port, ca=self.ca)
                self.tunnel.start()
                self.role = "host"
                self._open_lease()
                self._start_monitor()
                return self.snapshot()
            except Exception:
                self.close()
                raise

    def join(self, code):
        code = _code(code.strip().upper()) if isinstance(code, str) else _code(code)
        with self.lock:
            if self._stop.is_set():
                raise TransportError("Create a new online session")
            if self.tunnel:
                raise TransportError("Leave the current online room first")
            try:
                self.tunnel = GuestTunnel(self.service, code, ca=self.ca)
                self.tunnel.start()
                self.code, self.role = code, "guest"
                self._open_lease()
                self._start_monitor()
                return self.snapshot()
            except Exception:
                self.close()
                raise

    def environment(self):
        with self.lock:
            if self._stop.is_set() or not self.tunnel or not self.code or \
                    self.tunnel.status().get("state") in {"error", "closed", "failed"}:
                return {}
            host, port = self.tunnel.address()
            if host != "127.0.0.1" or not 1 <= port <= 65535:
                raise TransportError("The private online bridge is unavailable")
            env = {"SST_AUTOCONNECT": "1", "SST_AUTOHOST": "0",
                    "SST_SESSION_SERVER": f"127.0.0.1:{port}",
                    "SST_SESSION_ROOM": self.code, "SST_SESSION_ROLE": self.role}
            if self.lease:
                env["SST_SESSION_LEASE_FILE"] = str(self.lease)
            return env

    def snapshot(self):
        with self.lock:
            state = self.tunnel.status() if self.tunnel else {"state": "error" if self._error else "offline", "message": self._error or "No online room"}
            # Transport status is bounded and sanitized; never expose host_token.
            return {**state, "code": self.code, "role": self.role}

    def close(self):
        self._stop.set()
        with self.lock:
            try:
                if self._owns_lease():
                    self.lease.unlink(missing_ok=True)
            except OSError:
                # Its heartbeat will expire if deletion is unavailable. Never
                # remove a pre-existing lease or a replacement owned elsewhere.
                pass
            finally:
                self._lease_id = None
            tunnel, process = self.tunnel, self.process
            self.tunnel = self.process = None
            self.code = self.role = ""
            try:
                if tunnel:
                    tunnel.close()
            finally:
                if process:
                    try:
                        if process.stdin:
                            process.stdin.close()
                        process.wait(timeout=3)
                    except (OSError, subprocess.TimeoutExpired):
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=2)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait(timeout=2)
                    finally:
                        if process.stdout:
                            process.stdout.close()
