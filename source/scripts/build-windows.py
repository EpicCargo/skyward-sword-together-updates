#!/usr/bin/env python3
"""Cross-build the Windows co-op runtime and a game-file-free test package, one job.

Requires LLVM-MinGW and matching SDL3/FFmpeg/shaderc public C headers on the Linux
build machine. Builds the runtime only; first-run setup builds the player's DLL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from engine_patchset import active_patchset, windows_build_paths
from resource_runner import run as run_with_resources

DEFAULT_TC = ROOT / ".deps/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64"
NATIVE_TC = ROOT / ".deps/llvm-mingw-20261020-ucrt-x86_64"
PROTO = int(re.search(r"^#define SST_PROTO (\d+)$", (ROOT / "client/src/sst_net.h").read_text(), re.M)[1])
SUPPORT, PREFIX = windows_build_paths(ROOT)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def prepare(toolchain):
    for name in ("SDL3", "libavcodec", "libavformat", "libavutil", "libswscale", "shaderc"):
        header = Path("/usr/include") / name
        if not header.is_dir():
            raise RuntimeError("Missing public C headers: " + str(header))
        shutil.copytree(header, PREFIX / "include" / name, dirs_exist_ok=True)
    # ReShade's Windows.h spelling works on Windows; MinGW's Linux sysroot is case sensitive.
    (PREFIX / "include/Windows.h").write_text("#include <windows.h>\n")
    (PREFIX / "lib").mkdir(exist_ok=True)
    for name, lib in (("SDL3", "SDL3"), ("avcodec-63", "avcodec"),
                      ("avformat-63", "avformat"), ("avutil-61", "avutil"),
                      ("swscale-10", "swscale"), ("libshaderc_shared", "shaderc_shared")):
        dll = SUPPORT / (name + ".dll")
        result = subprocess.check_output([str(toolchain / "bin/llvm-readobj"), "--coff-exports", str(dll)], text=True)
        names = re.findall(r"^  Name: (.+)$", result, re.M)
        if not names:
            raise RuntimeError("DLL has no exports: " + str(dll))
        make_import(toolchain, dll.name, lib, names)
    xml = ET.parse(ROOT / ".deps/Vulkan-Headers/registry/vk.xml")
    names = [c.findtext("proto/name") or c.get("name") for c in xml.findall("./commands/command")]
    make_import(toolchain, "vulkan-1.dll", "vulkan-1", sorted(set(names)))


def make_import(toolchain, dll, lib, names):
    definition = PREFIX / "lib" / (lib + ".def")
    definition.write_text("LIBRARY " + dll + "\nEXPORTS\n" + "\n".join(names) + "\n")
    subprocess.run([str(toolchain / "bin/llvm-dlltool"), "-m", "i386:x86-64", "-d", str(definition),
                    "-l", str(PREFIX / "lib" / ("lib" + lib + ".dll.a"))], check=True)


def package_target(output=None):
    target = Path(output).absolute() if output else ROOT / "build/dist" / f"SST-Windows-coop-protocol{PROTO}.zip"
    if target.suffix.lower() != ".zip":
        raise ValueError("The package output must end in .zip")
    if target.exists() or target.is_symlink():
        raise FileExistsError(f"Delivery already exists: {target}; choose a new --output path")
    return target


def publish_archive(temporary, target):
    # The temporary archive lives beside the destination. link() publishes it atomically
    # and fails if another build created that name, preserving every existing delivery.
    os.link(temporary, target)
    temporary.unlink()


def snapshot_source_records():
    """Freeze a published snapshot's complete, checksum-verified file inventory."""
    manifest = ROOT / "SOURCE-SHA256.json"
    if not manifest.exists() and not manifest.is_symlink():
        return None
    if manifest.is_symlink() or not manifest.is_file():
        raise ValueError("Source manifest must be a regular file")
    data = manifest.read_bytes()
    def unique(pairs):
        result = {}
        for name, value in pairs:
            if name in result:
                raise ValueError("Duplicate source manifest name: " + name)
            result[name] = value
        return result
    inventory = json.loads(data, object_pairs_hook=unique)
    if not isinstance(inventory, dict) or not inventory:
        raise ValueError("Source manifest must contain a nonempty file inventory")
    records = [("SOURCE-SHA256.json", data)]
    for name, expected in sorted(inventory.items()):
        if not isinstance(name, str) or "\\" in name or ":" in name or "\0" in name or \
                any(part in ("", ".", "..") for part in name.split("/")) or name == "SOURCE-SHA256.json":
            raise ValueError("Invalid source manifest path: " + repr(name))
        if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise ValueError("Invalid source checksum: " + name)
        path = ROOT
        for part in name.split("/"):
            path = path / part
            if path.is_symlink():
                raise ValueError("Source snapshot paths must be regular files: " + name)
        if not path.is_file():
            raise ValueError("Missing source snapshot file: " + name)
        content = path.read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise ValueError("Source checksum mismatch: " + name)
        # Archive these exact verified bytes; do not reread a changing source.
        records.append((name, content))
    return records


def write_coop_source(target):
    """Archive a frozen snapshot exactly, or the active legacy source recipe."""
    records = snapshot_source_records()
    if records is not None:
        with zipfile.ZipFile(target, "x", zipfile.ZIP_DEFLATED) as archive:
            for name, content in records:
                archive.writestr(name, content)
        return
    patchset = active_patchset(ROOT)
    paths = set()
    excluded = {"__pycache__", ".git", ".venv", "node_modules", ".wrangler", "build", "dist", "cache"}

    def source_tree(relative, extensions):
        directory = ROOT / relative
        if not directory.is_dir() or directory.is_symlink():
            return
        for folder, directories, files in os.walk(directory, followlinks=False):
            directories[:] = [name for name in directories if name not in excluded and
                              not (Path(folder) / name).is_symlink()]
            for name in files:
                path = Path(folder) / name
                # JSON belongs only to explicit test fixtures, never runtime settings.
                if path.suffix not in extensions or (path.suffix == ".json" and
                        "fixtures" not in path.relative_to(directory).parts):
                    continue
                if not path.is_symlink() and path.is_file():
                    paths.add(path)

    source_tree("client/src", {".c", ".h", ".inc"})
    source_tree("client/tools", {".c", ".h", ".py"})
    source_tree("client/tests", {".c", ".h", ".py", ".sh", ".json"})
    source_tree("scripts", {".py", ".sh", ".mjs", ".json"})
    source_tree("launcher/tests", {".py", ".json"})
    source_tree("server/rooms/test", {".mjs"})
    paths.update(path for path in (ROOT / "launcher").glob("*") if path.suffix in {".py", ".c"})
    paths.update((ROOT / "third_party/SST-HANDOFF-20261030").glob("*"))
    paths.update((ROOT / "third_party/coop-anything-chaos").glob("*"))
    paths.update(ROOT / name for name in (
        "client/Makefile", "launcher/README.txt", "server/relay.js", "server/reward-catalogue.json", "server/rooms/worker.mjs",
        "server/rooms/package.json", "server/rooms/wrangler.toml", "server/rooms/README.md",
        "README.md", "docs/protocol.md", "docs/world-sync.md", "docs/beedle-purchases.md", "docs/hosting.md",
        "docs/windows-package-readme.txt", "docs/windows-testing.md", "docs/windows-third-party.txt",
        "docs/reward-catalogue.tsv", "docs/story-sync-policy.tsv"))
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(paths):
            if path.is_file() and not path.is_symlink():
                archive.write(path, path.relative_to(ROOT))
        patches = sorted(patchset.glob("*.patch"))
        metadata = [patchset / "base.txt", patchset / "windows-support.json"]
        for path in sorted(patches + metadata):
            if path.is_symlink():
                raise ValueError("Source patch recipe must contain regular files")
            if path.is_file():
                archive.write(path, "engine-patches/" + path.name)


def package(toolchain, output=None, *, runtime_dir=None, network_dir=None,
            game_source_root=None, source_archive=None):
    """Package explicit accepted artifacts, or the conventional build by default.

    The caller verifies candidate hashes before selecting isolated inputs.
    game_source_root supplies matching game ABI headers, not game data.
    """
    runtime_dir = Path(runtime_dir) if runtime_dir is not None else ROOT / "engine/build/windows"
    network_dir = Path(network_dir) if network_dir is not None else ROOT / "build/windows-network"
    game_source_root = Path(game_source_root) if game_source_root is not None else ROOT / "engine"
    dist = ROOT / "build/dist"
    dist.mkdir(parents=True, exist_ok=True)
    target = package_target(output)
    target.parent.mkdir(parents=True, exist_ok=True)
    # A new staging folder prevents saves, logs or game files from entering a subsequent package.
    with tempfile.TemporaryDirectory(prefix="sst-win-package-", dir=ROOT / "build") as work:
        stage = Path(work) / "Skyward Sword Together"
        shutil.copytree(SUPPORT, stage, ignore=shutil.ignore_patterns("zig"))
        compiler = stage / "compiler"
        (compiler / "bin").mkdir(parents=True)
        # Only the x64 C compiler/linker and their dependencies, with public SDK headers.
        binaries = ("clang.exe", "clang-23.exe", "ld.lld.exe", "libLLVM-23.dll", "libclang-cpp.dll",
                    "libc++.dll", "libunwind.dll", "libwinpthread-1.dll")
        for name in binaries:
            shutil.copy2(NATIVE_TC / "bin" / name, compiler / "bin" / name)
        for path in (NATIVE_TC / "bin").glob("*.cfg"):
            shutil.copy2(path, compiler / "bin" / path.name)
        shutil.copytree(NATIVE_TC / "include", compiler / "include")
        shutil.copytree(NATIVE_TC / "x86_64-w64-mingw32", compiler / "x86_64-w64-mingw32")
        shutil.copytree(NATIVE_TC / "lib/clang/23/include", compiler / "lib/clang/23/include")
        runtime = compiler / "lib/clang/23/lib/windows"
        runtime.mkdir(parents=True)
        for path in (NATIVE_TC / "lib/clang/23/lib/windows").glob("*-x86_64.a"):
            shutil.copy2(path, runtime / path.name)
        shutil.copy2(NATIVE_TC / "LICENSE.TXT", compiler / "LICENSE.TXT")
        for name in ("skyrecomp.exe", "skyrecomp.dll.a"):
            shutil.copy2(runtime_dir / name, stage / name)
        for name in ("libc++.dll", "libunwind.dll", "libwinpthread-1.dll"):
            shutil.copy2(toolchain / "x86_64-w64-mingw32/bin" / name, stage / name)
        game = stage / "gamelib-src"
        game.mkdir()
        for name in ("cpu.h", "simd.h", "simd_fp.h", "gamelib.h"):
            shutil.copy2(game_source_root / "runtime" / name, game / name)
        shutil.copy2(game_source_root / "tools/gamelib_entry.c", game / "gamelib_entry.c")
        shutil.copy2(game_source_root / "runtime/hle/hle_symbols.txt", game / "hle_symbols.txt")
        shutil.copy2(network_dir / "sst-network-check.exe", stage / "sst-network-check.exe")
        shutil.copy2(network_dir / "sst-session-host.exe", stage / "sst-session-host.exe")
        online = stage / "online"; online.mkdir()
        for source, name in [("sst_online_init.py", "__init__.py"), ("sst_online.py", "sst_online.py"), ("sst_online_transport.py", "sst_online_transport.py")]:
            shutil.copy2(ROOT / "launcher" / source, online / name)
        shutil.copy2(ROOT / "docs/windows-package-readme.txt", stage / "READ-ME-FIRST.txt")
        shutil.copy2(ROOT / "docs/windows-testing.md", stage / "windows-testing.md")
        shutil.copy2(ROOT / "docs/beedle-purchases.md", stage / "beedle-purchases.md")
        shutil.copy2(ROOT / "docs/windows-third-party.txt", stage / "THIRD-PARTY.txt")
        licenses = stage / "licenses"
        licenses.mkdir()
        shutil.copy2(toolchain / "LICENSE.TXT", licenses / "LLVM-LICENSE.txt")
        for name in ("COPYING", "COPYING.MinGW-w64-runtime.txt", "COPYING.MinGW-w64.txt", "COPYING.winpthreads.txt"):
            shutil.copy2(toolchain / "x86_64-w64-mingw32/share/mingw32" / name, licenses / name)
        extra = ROOT / ".deps/windows-licenses"
        if not extra.is_dir():
            raise RuntimeError("Run scripts/fetch-windows-support.py first to fetch third-party notices")
        shutil.copytree(extra, licenses, dirs_exist_ok=True)
        # Include the co-op sources and patch series needed to reproduce this modified runtime.
        if source_archive is None:
            write_coop_source(stage / "co-op-source.zip")
        else:
            source_archive = Path(source_archive)
            if source_archive.is_symlink() or not source_archive.is_file():
                raise ValueError("Candidate source archive must be a regular file")
            with zipfile.ZipFile(source_archive) as source:
                if source.testzip() is not None:
                    raise ValueError("Candidate source archive failed its CRC check")
            shutil.copy2(source_archive, stage / "co-op-source.zip")
        files = sorted(p for p in stage.rglob("*") if p.is_file())
        (stage / "SHA256.txt").write_text("".join(digest(p) + "  " + p.relative_to(stage).as_posix() + "\n" for p in files))
        with tempfile.NamedTemporaryFile(prefix=target.name + ".", suffix=".tmp", dir=target.parent, delete=False) as f:
            temporary = Path(f.name)
        try:
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        archive.write(path, path.relative_to(Path(work)))
            publish_archive(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    print(str(target), digest(target), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, default=DEFAULT_TC)
    parser.add_argument("--no-fetch", action="store_true", help="use already cached upstream support files")
    parser.add_argument("--package-only", action="store_true", help="package the already compiled runtime")
    parser.add_argument("--output", type=Path, help="new ZIP path for a candidate; existing deliveries are never overwritten")
    parser.add_argument("--build-worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    tc = args.toolchain.resolve()
    if args.build_worker:
        if os.environ.get("SST_RESOURCE_RUNNER_PID") != str(os.getppid()) or os.getpgrp() != os.getpid():
            parser.error("the internal build worker requires its monitored process group")
        build_runtime(tc)
        return
    try:
        output = package_target(args.output)
    except (ValueError, FileExistsError) as error:
        parser.error(str(error))
    if not args.no_fetch and not args.package_only:
        subprocess.run(["python3", str(ROOT / "scripts/fetch-windows-support.py")], check=True)
        subprocess.run(["python3", str(ROOT / "scripts/fetch-windows-compiler.py")], check=True)
    if not (NATIVE_TC / "bin/clang.exe").exists():
        parser.error("Run scripts/fetch-windows-compiler.py to fetch the native Windows setup compiler")
    if not args.package_only:
        result = run_with_resources([sys.executable, str(Path(__file__).resolve()), "--build-worker", "--toolchain", str(tc)],
                                    rss_mib=4096, available_mib=6144, nice=10,
                                    label="Windows background build", lock=ROOT / "build/developer-build.lock")
        if result:
            raise SystemExit(result)
    package(tc, output)


def build_runtime(tc):
    # Called only inside the monitored worker's owned process group, including import-library tools.
    prepare(tc)
    env = os.environ.copy()
    env["MAKEFLAGS"] = env["MFLAGS"] = ""
    inc = ":".join(str(ROOT / ".deps" / name / "include") for name in ("Vulkan-Headers", "VulkanMemoryAllocator"))
    env["C_INCLUDE_PATH"] = inc
    env["CPLUS_INCLUDE_PATH"] = inc
    command = ["make", "-C", str(ROOT / "engine"), "-j1", "PLATFORM=windows", "B=build/windows",
               "DEPS=" + str(PREFIX), "CC=" + str(tc / "bin/x86_64-w64-mingw32-clang"),
               "CXX=" + str(tc / "bin/x86_64-w64-mingw32-clang++"),
               "WINDRES=" + str(tc / "bin/x86_64-w64-mingw32-windres"), "build/windows/skyrecomp.exe"]
    subprocess.run(command, env=env, check=True)
    helper_dir = ROOT / "build/windows-network"
    helper_dir.mkdir(exist_ok=True)
    subprocess.run(["make", "-C", str(ROOT / "client"), "-j1", "B=" + str(helper_dir), "EXEEXT=.exe",
                    "CC=" + str(tc / "bin/x86_64-w64-mingw32-clang"),
                    "CFLAGS=-O2 -g -std=c11 -D_GNU_SOURCE -D_CRT_RAND_S -Wall -Wextra -static",
                    "LDLIBS=-lwinpthread -lws2_32 -lm", str(helper_dir / "sst-session-host.exe"),
                    str(helper_dir / "sst-network-check.exe")], env=env, check=True)


if __name__ == "__main__":
    main()
