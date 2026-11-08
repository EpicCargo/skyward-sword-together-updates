"""Exercise a prebuilt session helper; never compiles, launches a game or reads assets.

Usage: python3 tests/session_host_test.py [path/to/sst-session-host]
SST_SESSION_HOST_COMMAND may instead supply a JSON command array (e.g. Wine).
"""
import json
import os
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

HELPER = sys.argv.pop(1) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None
COMMAND = json.loads(os.environ['SST_SESSION_HOST_COMMAND']) if os.environ.get('SST_SESSION_HOST_COMMAND') else [
    str(Path(HELPER).resolve()) if HELPER else
    str(Path(__file__).resolve().parents[1] / 'build/sst-session-host')]
SECRET = 'session-helper-private-fixture'


def readiness(process):
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=5)
    except queue.Empty:
        process.kill()
        raise AssertionError('Session host did not publish readiness')
    result = json.loads(line)
    assert set(result) == {'protocol', 'port'} and result['protocol'] == 45
    assert type(result['port']) is int and 0 < result['port'] <= 65535
    return result


def exact(sock, count):
    result = b''
    while len(result) < count:
        data = sock.recv(count - len(result))
        if not data:
            raise AssertionError('Session host closed during a protocol frame')
        result += data
    return result


class SessionHostTest(unittest.TestCase):
    def start(self, *args):
        env = dict(os.environ, SST_HOST_BIND='0.0.0.0')  # helper must override inherited wildcard
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        process = subprocess.Popen([*COMMAND, '--room', SECRET, *args], cwd=temporary.name,
                                   env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        def cleanup():
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            for pipe in (process.stdin, process.stdout, process.stderr):
                if pipe and not pipe.closed:
                    pipe.close()
        self.addCleanup(cleanup)
        return process, readiness(process)

    def test_protocol_ready_and_eof_with_a_live_client(self):
        process, ready = self.start('--port', '0')
        with socket.create_connection(('127.0.0.1', ready['port']), timeout=2) as sock:
            sock.settimeout(2)
            string = lambda text: bytes([len(text.encode())]) + text.encode()
            body = struct.pack('<H', 45) + string('Helper fixture') + string(SECRET) + string('team') + bytes(range(16))
            sock.sendall(struct.pack('<I', len(body) + 1) + b'\x01' + body)
            length = struct.unpack('<I', exact(sock, 4))[0]
            response = exact(sock, length)
            self.assertEqual(response[0], 2)  # actual current native HELLO/WELCOME handling
            process.stdin.close()
            self.assertEqual(process.wait(timeout=5), 0)
        remaining = process.stdout.read()
        errors = process.stderr.read()
        self.assertEqual(remaining, '')
        self.assertNotIn(SECRET, errors)

    def test_quit_line_and_equal_arguments(self):
        process, _ = self.start('--port=0', '--bind=127.0.0.1')
        output, errors = process.communicate('ignored\nquit\n', timeout=5)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(output, '')
        self.assertNotIn(SECRET, errors)

    def test_invalid_arguments_never_start_a_listener_or_log_the_secret(self):
        for args in [[], ['--room', ''], ['--room', SECRET, '--port', '65536'],
                     ['--room', SECRET, '--port', '-1'], ['--room', SECRET, '--port', '2x'],
                     ['--room', SECRET, '--bind', '0.0.0.0'], ['--room', SECRET, '--bind', '::'],
                     ['--room', SECRET, '--bind', '192.0.2.1'],
                     ['--room', SECRET, '--port', '0', '--port', '1'],
                     ['--room', SECRET, '--unknown'], ['--room', SECRET, '--port']]:
            with self.subTest(args=args):
                result = subprocess.run([*COMMAND, *args], input='', capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, '')
                self.assertNotIn(SECRET, result.stderr)


if __name__ == '__main__':
    unittest.main()
