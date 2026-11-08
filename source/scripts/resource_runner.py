#!/usr/bin/env python3
"""Run an owned process group within RSS and system-memory budgets."""
import argparse
import fcntl
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

MIB = 1024 * 1024
SIGNALS = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)


def group_rss(group, proc_root=Path("/proc")):
    """Sample aggregate RSS of this process group, never unrelated applications."""
    pages = 0
    for path in proc_root.iterdir():
        if not path.name.isdigit():
            continue
        try:
            fields = (path / "stat").read_text().rsplit(")", 1)[1].split()
            if int(fields[2]) == group:
                pages += int((path / "statm").read_text().split()[1])
        except (OSError, ValueError, IndexError):
            continue
    return pages * os.sysconf("SC_PAGE_SIZE")


def available_memory(meminfo=Path("/proc/meminfo")):
    for line in meminfo.read_text().splitlines():
        fields = line.split()
        if fields[:1] == ["MemAvailable:"] and len(fields) == 3 and fields[2] == "kB":
            value = int(fields[1])
            if value >= 0:
                return value * 1024
    raise ValueError("MemAvailable is missing or invalid")


def game_name(value):
    name = str(value).replace("\\", "/").rsplit("/", 1)[-1].lower()
    if name.endswith(" (deleted)"):
        name = name[:-10]
    return name in ("skyrecomp", "skyrecomp.exe")


def is_game_command(command):
    return any(game_name(arg) for arg in command)


def running_games(proc_root=Path("/proc")):
    """Return (pid, group) for native or Wine SkyRecomp, including installed versions."""
    found = []
    for path in proc_root.iterdir():
        if not path.name.isdigit():
            continue
        try:
            fields = (path / "stat").read_text().rsplit(")", 1)[1].split()
            if fields[0] == "Z":
                continue
            group = int(fields[2])
            names = [(path / "comm").read_text().strip()]
            try:
                names.append(os.readlink(path / "exe"))
            except OSError:
                pass
            # A linker, compositor or wrapper merely mentioning the output filename
            # is not a full game. Only Wine loaders need argv inspection here.
            loaders = {"wine", "wine64", "wine-preloader", "wine64-preloader"}
            if any(str(name).replace("\\", "/").rsplit("/", 1)[-1].lower() in loaders for name in names):
                names += [arg.decode("utf-8", "replace") for arg in (path / "cmdline").read_bytes().split(b"\0") if arg]
            if any(game_name(name) for name in names):
                found.append((int(path.name), group))
        except (OSError, ValueError, IndexError):
            continue
    return found


def stop_group(process):
    """Terminate only the new session's group, including descendants after leader exit."""
    def send(sig):
        try:
            os.killpg(process.pid, sig)
            return True
        except ProcessLookupError:
            return False
    if send(signal.SIGTERM):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            process.poll()
            if not send(0):
                break
            time.sleep(.05)
        send(signal.SIGKILL)
    process.wait()


def run(command, *, rss_mib=4096, available_mib=6144, seconds=None, nice=10,
        env=None, cwd=None, label="Background build", guard_game=False, lock=None):
    """Monitor one fresh group. Returns125 for memory pressure,75 for another game,124 for timeout."""
    if not command or rss_mib <= 0 or available_mib < 6144 or not 0 <= nice <= 19 or \
            (seconds is not None and (seconds <= 0 or not math.isfinite(seconds))):
        raise ValueError("positive RSS budget, at least6144 MiB available-memory floor, niceness0..19 and finite positive duration required")
    if lock is not None:
        lock = Path(lock)
        lock.parent.mkdir(parents=True, exist_ok=True)
        with lock.open("a+") as stream:
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                print("Another background developer build is running.", file=sys.stderr)
                return 75
            return run(command, rss_mib=rss_mib, available_mib=available_mib, seconds=seconds,
                       nice=nice, env=env, cwd=cwd, label=label, guard_game=guard_game)
    if guard_game and running_games():
        print("A SkyRecomp game is already running; finish it before starting this native test.", file=sys.stderr, flush=True)
        return 75
    try:
        if available_mib and available_memory() < available_mib * MIB:
            print(f"{label}: MemAvailable is below {available_mib} MiB; command was not started.", file=sys.stderr, flush=True)
            return 125
    except (OSError, ValueError) as error:
        print(f"{label}: cannot verify available memory: {error}; command was not started.", file=sys.stderr, flush=True)
        return 125

    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    previous = {sig: signal.getsignal(sig) for sig in SIGNALS}
    process = None
    peak = 0
    try:
        for sig in SIGNALS:
            signal.signal(sig, interrupted)
        child_env = dict(os.environ if env is None else env)
        child_env["SST_RESOURCE_RUNNER_PID"] = str(os.getpid())
        process = subprocess.Popen(command, start_new_session=True, env=child_env, cwd=cwd)
        if nice:
            try:
                os.setpriority(os.PRIO_PROCESS, process.pid, nice)
            except OSError as error:
                print(f"{label}: could not apply optional niceness: {error}", file=sys.stderr, flush=True)
        deadline = time.monotonic() + seconds if seconds is not None else None
        while True:
            try:
                resident = group_rss(process.pid)
                available = available_memory() if available_mib else None
                games = running_games() if guard_game else []
            except (OSError, ValueError) as error:
                print(f"{label}: memory/process monitoring failed: {error}; stopping owned processes.", file=sys.stderr, flush=True)
                return 125
            peak = max(peak, resident)
            if resident > rss_mib * MIB:
                print(f"{label} exceeded {rss_mib} MiB group RSS; stopping its processes.", file=sys.stderr, flush=True)
                return 125
            if available is not None and available < available_mib * MIB:
                print(f"{label}: MemAvailable fell below {available_mib} MiB; stopping only its processes.", file=sys.stderr, flush=True)
                return 125
            if guard_game and (any(group != process.pid for _, group in games) or len(games) > 1):
                print("Another SkyRecomp game is running; stopping only this native test's processes.", file=sys.stderr, flush=True)
                return 75
            result = process.poll()
            if result is not None:
                return result if result >= 0 else 128 - result
            if deadline is not None and time.monotonic() >= deadline:
                return 124
            time.sleep(.2 if deadline is None else min(.2, max(0, deadline - time.monotonic())))
    finally:
        try:
            if process is not None:
                for sig in SIGNALS:
                    signal.signal(sig, signal.SIG_IGN)
                stop_group(process)
                print(f"{label} peak sampled group RSS: {peak / MIB:.1f} MiB", file=sys.stderr, flush=True)
        finally:
            for sig, handler in previous.items():
                signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rss-mib", type=int, default=4096)
    parser.add_argument("--available-mib", type=int, default=6144)
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--nice", type=int, default=10)
    parser.add_argument("--lock", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    def execute():
        try:
            return run(command, rss_mib=args.rss_mib, available_mib=args.available_mib,
                       seconds=args.seconds, nice=args.nice)
        except ValueError as error:
            parser.error(str(error))
    if args.lock:
        args.lock.parent.mkdir(parents=True, exist_ok=True)
        with args.lock.open("a+") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                print("Another background developer build is running.", file=sys.stderr)
                return 75
            return execute()
    return execute()


if __name__ == "__main__":
    sys.exit(main())
