#!/usr/bin/env python3
"""Cross-compile small Windows protocol tests/checker; this does not build the game executable."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("x86_64-w64-mingw32-clang") or str(
        ROOT / ".deps/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang"))
    args = parser.parse_args()
    if not shutil.which(args.cc):
        parser.error("provide --cc with a Windows MinGW compiler that includes winpthreads")
    out = ROOT / "build/windows-network"
    out.mkdir(parents=True, exist_ok=True)
    libraries = next(line for line in (ROOT / "client/Makefile").read_text().splitlines()
                     if line.startswith("LIB :=")).split(":=", 1)[1].split()
    tests = {name: ROOT / f"client/tests/{name}.c" for name in [
        "defeat_test", "stream_test", "spawn_test", "host_fixture", "net_e2e", "four_player", "animation_test", "world_test", "motion_test", "chest_test", "backup_portable_test", "queue_fixture", "udp_fixture", "udp_queue_test", "scene_history_test"]}
    tests["sst-network-check"] = ROOT / "client/tools/network_check.c"
    with (out / "compile.log").open("w") as log:
        for name, source in tests.items():
            subprocess.run([args.cc, "-O1", "-g", "-std=c11", "-D_GNU_SOURCE", "-D_CRT_RAND_S",
                            "-Wall", "-Wextra", "-static", "-o", str(out / (name + ".exe")), str(source),
                            *[str(ROOT / "client" / path) for path in libraries if name!="udp_queue_test" or path!="src/sst_net.c"],
                            "-lwinpthread", "-lws2_32", "-lm"], check=True, stdout=log, stderr=subprocess.STDOUT)
            print(f"Windows compiled: {name}", flush=True)


if __name__ == "__main__":
    main()
