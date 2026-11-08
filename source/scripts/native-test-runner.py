#!/usr/bin/env python3
"""Limit native tests to one game, a resident-memory budget, and a bounded lifetime."""
import argparse
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from resource_runner import group_rss, stop_group, is_game_command, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--rss-mib", type=int, default=12288,
                        help="aggregate process-group RSS ceiling in MiB (default: 12288)")
    parser.add_argument("--available-mib", type=int, default=6144)
    parser.add_argument("--game-test", action="store_true", help="guard a wrapper whose arguments hide its game executable")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if args.seconds <= 0 or not args.seconds < float("inf") or args.rss_mib <= 0 or args.available_mib < 6144 or not command:
        parser.error("a finite positive duration, positive RSS budget, at least6144 MiB memory floor and a command are required")

    args.lock.parent.mkdir(parents=True, exist_ok=True)
    with args.lock.open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Another native SST test is running; finish it before starting this one.", file=sys.stderr)
            return 75

        return run(command, rss_mib=args.rss_mib, available_mib=args.available_mib,
                   seconds=args.seconds, nice=0, label="Native test",
                   guard_game=args.game_test or is_game_command(command))


if __name__ == "__main__":
    sys.exit(main())
