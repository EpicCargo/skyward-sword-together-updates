#!/usr/bin/env python3
"""Import the engine patch series in a temporary local checkout, without changing engine/."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from engine_patchset import active_patchset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "engine", help="local SkyRecomp repository (no network access)")
    parser.add_argument("--check-working-tree", action="store_true", help="also compare the imported result with engine's tracked working files")
    args = parser.parse_args()
    engine = args.engine.resolve()
    try:
        patchset = active_patchset(ROOT)
        base = (patchset / "base.txt").read_text().strip()
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if not re.fullmatch(r"[0-9a-f]{40}", base):
        parser.error("engine-patches/base.txt must contain one full commit ID")
    patches = sorted(patchset.glob("*.patch"))
    if not patches:
        parser.error("no engine patches found")
    try:
        with tempfile.TemporaryDirectory(prefix="sst-engine-patches-") as work:
            checkout = Path(work) / "engine"
            def run(*command):
                return subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
            def git(*command):
                return run("git", "-C", str(checkout), "-c", "core.hooksPath=/dev/null",
                           "-c", "commit.gpgsign=false", "-c", "am.keepcr=true", *command)
            run("git", "clone", "--shared", "--quiet", "--no-checkout", str(engine), str(checkout))
            git("-c", "advice.detachedHead=false", "checkout", "--quiet", "--detach", base)
            git("-c", "user.name=SST verification", "-c", "user.email=sst-verification@localhost",
                "am", *map(str, patches))
            if args.check_working_tree:
                git("--work-tree=" + str(engine), "diff", "--exit-code", "HEAD", "--")
            tree = git("rev-parse", "HEAD^{tree}").strip()
            print(f"{len(patches)} patches import cleanly from {base}; tree {tree}")
            if args.check_working_tree:
                print("Imported engine matches the current engine working files; original checkout/index unchanged.")
    except subprocess.CalledProcessError as error:
        print(error.stdout or str(error))
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
