#!/usr/bin/env python3
"""Hold the actual C client's consumers while TCP delivers snapshot/attack bursts."""
import json
import os
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]

def frame(kind, body=b''):
    return struct.pack('<IB', len(body) + 1, kind) + body

def flag(index, value=1):
    return frame(0x20, struct.pack('<HBHB', 2, 1, index, value))

def enemy(sequence, health, snapshot=False):
    return frame(0x30, struct.pack('<HB', 2, 4) + b'F100' + b'\0\0' +
                 struct.pack('<QIHHHB', (0x1234 << 32) | (0x16d << 16) | 12,
                             sequence, 0x16d, 0 if snapshot else 1, health, snapshot))

class Rig:
    def __init__(self, command=None):
        self.listener = socket.socket(); self.listener.bind(('127.0.0.1', 0)); self.listener.listen(1)
        self.listener.settimeout(8); self.lines = queue.Queue(); self.frames = []; self.condition = threading.Condition()
        self.lock = threading.Lock(); self.error = None; self.mode = ''; self.hello = threading.Event()
        command = command or json.loads(os.environ.get('SST_QUEUE_CLIENT_COMMAND', json.dumps([str(ROOT/'build/queue_fixture')])))
        self.process = subprocess.Popen(command + [str(self.listener.getsockname()[1])], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self.reader = threading.Thread(target=lambda: [self.lines.put(s.strip()) for s in self.process.stdout], daemon=True)
        self.reader.start(); self.sock, _ = self.listener.accept(); self.sock.settimeout(8)
        self.worker = threading.Thread(target=self.receive, daemon=True); self.worker.start()
        assert self.lines.get(timeout=8) == 'ready'; assert self.hello.wait(8)

    def send(self, data):
        with self.lock: self.sock.sendall(data)

    def marker(self, text):
        value = text.encode(); self.send(frame(3, struct.pack('<HB', 2, len(value)) + value + b'\0'))

    def receive(self):
        buf = b''
        try:
            while True:
                data = self.sock.recv(65536)
                if not data: break
                buf += data
                while len(buf) >= 4:
                    n = struct.unpack_from('<I', buf)[0]
                    if len(buf) < 4+n: break
                    kind, body, buf = buf[4], buf[5:4+n], buf[4+n:]
                    with self.condition:
                        self.frames.append((kind, body)); self.condition.notify_all()
                    if kind == 1: self.hello.set()
                    if kind == 6: self.send(frame(7, body))
                    if kind == 0x22 and self.mode:
                        if self.mode == 'flags': self.send(flag(4099, 4))
                        else: self.send(enemy(10000, 400, True))
                        self.marker('healed-' + self.mode)
        except OSError as error:
            if self.process.poll() is None: self.error = str(error)

    def welcome(self):
        self.send(frame(2, struct.pack('<HB', 1, 0)))
        self.wait(lambda: any(k == 0x22 for k, _ in self.frames))

    def wait(self, predicate):
        end = time.monotonic() + 8
        with self.condition:
            while not predicate():
                if time.monotonic() >= end: raise RuntimeError('Queue fixture network timeout: ' + str(self.error))
                self.condition.wait(.02)

    def command(self, command):
        self.process.stdin.write(command + '\n'); self.process.stdin.flush()
        try: value = self.lines.get(timeout=10)
        except queue.Empty:
            raise RuntimeError('Queue fixture command timed out: ' + command) from None
        if value == 'timeout': raise RuntimeError('Marker was not consumed: ' + command)
        return json.loads(value) if value.startswith('{') else value

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write('quit\n'); self.process.stdin.flush()
            try: self.process.wait(timeout=3)
            except subprocess.TimeoutExpired: self.process.terminate(); self.process.wait(timeout=3)
        self.sock.close(); self.listener.close(); self.worker.join(timeout=1)
        error = self.process.stderr.read()
        if self.process.returncode: raise RuntimeError('Queue fixture failed: ' + error[-2000:])

def replay(fixture, command=None):
    """coop-anything flight shrinker oracle; this exercises the real TCP client each time."""
    rig = Rig(command)
    try:
        rig.welcome()
        for step in fixture['steps']:
            if step['op'] == 'flags': rig.send(flag(step['index'], step.get('value', 1)) * step.get('count', 1))
        rig.marker('replay-done'); rig.command('wait replay-done'); result = rig.command('flags')
        missing = result['target'] != 1
        return {'diverged': missing, 'invariant': 'shared_flag_lost' if missing else None,
                'violations': ['queued snapshot lost the final progress flag'] if missing else [], 'observed': result}
    finally: rig.close()

def main():
    rig = Rig()
    try:
        assert rig.command('offline-burst') == 'queued'; rig.welcome()
        rig.wait(lambda: any(k == 0x20 and b == struct.pack('<BHB', 1, 8, 4) for k, b in rig.frames))
        outgoing = [b for k, b in rig.frames if k == 0x20]
        assert outgoing == [struct.pack('<BHB', 1, 7, 3), struct.pack('<BHB', 1, 8, 4)], outgoing
        # Malformed trailing bytes and a short body cannot become shared state.
        rig.send(frame(0x20, struct.pack('<HBHB', 2, 1, 60000, 1) + b'x') + frame(0x20, b'\0'*5))
        rig.send(flag(50, 1)*3000 + flag(50, 2)*3000 + b''.join(flag(i) for i in range(51,4051)))
        rig.marker('coalesced'); rig.command('wait coalesced'); result = rig.command('flags')
        assert result['callbacks'] == result['unique'] == 4001 and result['value50'] == 3 and result['tail4050'] == 1, result
        assert result['recoveries'] == 0, result
        rig.command('reset'); rig.mode = 'flags'
        rig.send(b''.join(flag(i) for i in range(4100))); rig.marker('flag-full'); rig.command('wait flag-full')
        result = rig.command('stats'); assert result['recoveries'] == 0, result
        result = rig.command('flags'); assert result['recoveries'] == 1, result
        rig.command('wait healed-flags'); result = rig.command('flags'); assert result['overflow_tail'] == 4, result
        rig.mode = ''; time.sleep(1.05); rig.command('reset'); rig.mode = 'enemies'
        rig.send(b''.join(enemy(i, 1000-i) for i in range(1,601)))
        rig.marker('enemy-full'); rig.command('wait enemy-full')
        # No premature refresh while the damage queue is still full.
        result = rig.command('flags'); assert result['recoveries'] == 1, result
        result = rig.command('enemies'); assert result['enemy_callbacks'] == 512 and result['health'] == 488 and result['recoveries'] == 2, result
        rig.command('wait healed-enemies'); result = rig.command('enemies'); assert result['health'] == 400, result
        print('Queue recovery: offline/incoming coalescing, strict frames, overflow refresh and enemy health repair PASS')
    finally: rig.close()

if __name__ == '__main__':
    if len(sys.argv) == 2:
        result = replay(json.loads(Path(sys.argv[1]).read_text()))
        print(json.dumps(result)); raise SystemExit(1 if result['diverged'] else 0)
    main()
