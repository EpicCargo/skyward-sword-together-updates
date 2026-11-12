"""Fresh, request-correlated Windows snapshots from one owned PowerShell process.

Loads the installed .NET assembly; does not compile code or start a game.
EOF closes the sampler. Missing or invalid snapshots fail closed.
"""
import json
import queue
import shutil
import subprocess
import tempfile
import threading

SCRIPT = r"""
$ErrorActionPreference='Stop'
Add-Type -AssemblyName Microsoft.VisualBasic
$memory=New-Object Microsoft.VisualBasic.Devices.ComputerInfo
while (($line=[Console]::ReadLine()) -ne $null) {
  $sequence=[long]$line
  try {
    $games=[System.Diagnostics.Process]::GetProcessesByName('skyrecomp')
    $count=$games.Count
    foreach ($game in $games) { $game.Dispose() }
    $packet=@{ok=$true;sequence=$sequence;free_bytes=[long]$memory.AvailablePhysicalMemory;games=$count}
  } catch {
    $packet=@{ok=$false;sequence=$sequence;error=$_.Exception.GetType().Name}
  }
  [Console]::WriteLine(($packet | ConvertTo-Json -Compress))
}
"""


def valid_snapshot(packet, sequence):
    return (isinstance(packet, dict) and packet.get('ok') is True and
            all(type(packet.get(k)) is int and packet[k] >= 0
                for k in ('sequence', 'free_bytes', 'games')) and packet['sequence'] == sequence)


def validate(packet, sequence, reserve_mib, linux_games):
    if not valid_snapshot(packet, sequence):
        raise RuntimeError('Invalid or stale Windows telemetry; host state is unverified.')
    if packet['free_bytes'] < reserve_mib * 1024 * 1024:
        raise RuntimeError(f'Windows host has less than the required {reserve_mib} MiB free RAM reserve.')
    if packet['games'] + linux_games > 1:
        raise RuntimeError('CPU fixtures allow at most one existing SkyRecomp game across Windows and Linux.')
    return packet


class Sampler:
    def __init__(self):
        executable = shutil.which('powershell.exe')
        if not executable:
            raise RuntimeError('Windows interop unavailable; host state is unverified.')
        self.errors = tempfile.TemporaryFile(mode='w+t')
        self.lines = queue.Queue(maxsize=2)
        self.sequence = 0
        self.minimum_free_bytes = None
        self.last_snapshot = None
        self.last_rejection = None
        self.lock = threading.Lock()
        try:
            self.process = subprocess.Popen(
                [executable, '-NoLogo', '-NoProfile', '-NonInteractive', '-Command', SCRIPT],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.errors,
                text=True, bufsize=1)
        except BaseException:
            self.errors.close()
            raise

        def read():
            for line in self.process.stdout:
                try:
                    self.lines.put(line, timeout=1)
                except queue.Full:
                    return
        self.reader = threading.Thread(target=read, daemon=True, name='owned-windows-telemetry')
        self.reader.start()

    def check(self, guard, reserve_mib=2048, timeout=8):
        with self.lock:
            # No previously sampled or unsolicited value may satisfy a new request.
            if not self.lines.empty() or self.process.poll() is not None:
                raise RuntimeError('Windows telemetry stopped or returned unsolicited data.')
            self.sequence += 1
            try:
                self.process.stdin.write(str(self.sequence) + '\n')
                self.process.stdin.flush()
                line = self.lines.get(timeout=timeout)
                packet = json.loads(line.strip().lstrip('\ufeff'))
            except (OSError, ValueError, queue.Empty) as error:
                raise RuntimeError('Cannot obtain fresh Windows telemetry; host state is unverified.') from error
            linux_games = len(guard.rr.running_games())
            # Keep the fresh pressure-triggering sample as evidence even when
            # validation stops the owned fixture. Invalid/stale data never changes
            # the minimum. Acceptance and resource thresholds are unchanged.
            if valid_snapshot(packet, self.sequence):
                self.last_snapshot = dict(packet)
                self.minimum_free_bytes = min(packet['free_bytes'], self.minimum_free_bytes if self.minimum_free_bytes is not None else packet['free_bytes'])
            try:
                packet = validate(packet, self.sequence, reserve_mib, linux_games)
            except RuntimeError as error:
                self.last_rejection = dict(sequence=self.sequence, reason=str(error), packet=packet)
                raise
            return packet

    def close(self):
        try:
            self.process.stdin.close()
        except (OSError, ValueError):
            pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.reader.join(2)
        self.process.stdout.close()
        self.errors.close()
        if self.reader.is_alive():
            raise RuntimeError('Owned Windows telemetry reader failed to stop.')

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
