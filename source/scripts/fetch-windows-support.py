#!/usr/bin/env python3
"""Fetch allowlisted setup tools and DLLs using exact ZIP byte ranges.

The active patchset may select a separate cache and upstream release. Completed
legacy caches remain reusable without probing a changed remote release archive.
"""
import email.utils
import hashlib
import io
import json
import math
import os
import re
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib
from pathlib import Path, PurePosixPath, PureWindowsPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_patchset import active_patchset, windows_build_paths

ROOT = Path(__file__).resolve().parents[1]
URL = ("https://github.com/Apolinario-coder/SkyRecomp/releases/download/"
       "v1.0.6/SkyRecomp-v1.0.6-Windows.zip")
SIZE = 2075313323
MAX_DIRECTORY = 16 * 1024 * 1024
MAX_MEMBER = 256 * 1024 * 1024

NOTICES = {
    "SDL3-LICENSE.txt": "https://raw.githubusercontent.com/libsdl-org/SDL/release-3.4.18/LICENSE.txt",
    "FFmpeg-LGPL-2.1.txt": "https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/COPYING.LGPLv2.1",
    "shaderc-LICENSE.txt": "https://raw.githubusercontent.com/google/shaderc/v2026.4/LICENSE",
    "glslang-LICENSE.txt": "https://raw.githubusercontent.com/KhronosGroup/glslang/16.0.0/LICENSE.txt",
    "SPIRV-Tools-LICENSE.txt": "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Tools/v2026.4/LICENSE",
    "Python-LICENSE.txt": "https://raw.githubusercontent.com/python/cpython/3.13/LICENSE",
    "PyInstaller-COPYING.txt": "https://raw.githubusercontent.com/pyinstaller/pyinstaller/v6.22.3/COPYING.txt",
    "capstone-LICENSE.txt": "https://raw.githubusercontent.com/capstone-engine/capstone/5.0.9/LICENSE.TXT",
    "unicorn-COPYING.txt": "https://raw.githubusercontent.com/unicorn-engine/unicorn/2.1.4/COPYING",
    "GCC-RUNTIME-LICENSE.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/COPYING.RUNTIME",
    "GPL-3.0.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/COPYING3",
}


def project_path(root, value, label):
    if (not isinstance(value, str) or not value.strip() or "\x00" in value or
            Path(value).is_absolute() or PureWindowsPath(value).drive or "\\" in value or ":" in value):
        raise ValueError(f"{label} must be a project-relative POSIX path")
    path = (root / value).resolve()
    if path == root or not path.is_relative_to(root):
        raise ValueError(f"{label} must resolve beneath the project")
    return path


def load_config(root=ROOT):
    root = Path(root).resolve()
    path = active_patchset(root) / "windows-support.json"
    values = {}
    if path.exists() or path.is_symlink():
        if not path.resolve().is_relative_to(root):
            raise ValueError("Windows support configuration escapes the project")
        values = json.loads(path.read_text())
        if not isinstance(values, dict):
            raise ValueError("Windows support configuration must be an object")
    support, prefix = windows_build_paths(root)
    cache = project_path(root, values.get("cache", ".deps/windows-upstream"), "Windows cache")
    support = project_path(root, str(support.relative_to(root)), "Windows support")
    prefix = project_path(root, str(prefix.relative_to(root)), "Windows prefix")
    url = values.get("url", URL)
    parsed = urllib.parse.urlsplit(url) if isinstance(url, str) else None
    if (not parsed or parsed.scheme != "https" or parsed.netloc != "github.com" or
            not parsed.path.startswith("/Apolinario-coder/SkyRecomp/releases/download/") or
            not parsed.path.endswith(".zip") or parsed.query or parsed.fragment):
        raise ValueError("Windows support URL must name an official GitHub release ZIP")
    size = values.get("archive_size", SIZE)
    if isinstance(size, bool) or not isinstance(size, int) or not 22 <= size < 0xffffffff:
        raise ValueError("Windows archive_size must be a positive ZIP32 size")
    digest = values.get("asset_digest")
    if digest is not None and (not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest)):
        raise ValueError("Windows asset_digest must be sha256 followed by 64 lowercase hex digits")
    return dict(root=root, cache=cache, support=support, prefix=prefix,
                url=url, archive_size=size, asset_digest=digest)


class RateLimitedError(RuntimeError):
    def __init__(self, retry_after):
        self.retry_after = retry_after
        try:
            delay = float(retry_after)
        except (TypeError, ValueError):
            try:
                delay = email.utils.parsedate_to_datetime(retry_after).timestamp() - time.time()
            except (TypeError, ValueError, OverflowError):
                delay = 60
        if not math.isfinite(delay):
            delay = 60
        self.not_before = time.time() + max(0, math.ceil(delay))
        super().__init__(f"GitHub rate limited the support download; Retry-After={retry_after!r}. "
                         "Verified files are retained. Retry after the indicated cooldown.")


def request(start, end, config):
    size = config["archive_size"]
    if (isinstance(start, bool) or isinstance(end, bool) or not isinstance(start, int) or
            not isinstance(end, int) or not 0 <= start <= end < size):
        raise ValueError("Invalid ZIP byte range")
    length = end - start + 1
    if length > MAX_MEMBER:
        raise ValueError("Support byte range is too large")
    req = urllib.request.Request(config["url"] + f"?sst-range={start}-{end}",
                                 headers={"Range": f"bytes={start}-{end}"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=90) as response:
                if response.status != 206 or response.headers.get("Content-Range") != f"bytes {start}-{end}/{size}":
                    raise RuntimeError("Server did not return the exact requested ZIP range")
                declared = response.headers.get("Content-Length")
                if declared is not None and declared != str(length):
                    raise RuntimeError("ZIP range has an incorrect Content-Length")
                data = response.read(length + 1)
                if len(data) != length:
                    raise RuntimeError("ZIP range is truncated or oversized")
                return data
        except urllib.error.HTTPError as error:
            if error.code == 429:
                # Do not loop on 429 or shorten Retry-After. main records the cooldown
                # so a later invocation also waits before making another request.
                raise RateLimitedError(error.headers.get("Retry-After")) from error
            if error.code not in (500, 502, 503, 504) or attempt == 4:
                raise
            time.sleep(min(30, 3 * 2 ** attempt))


def safe_name(name):
    if (not isinstance(name, str) or not name or "\x00" in name or "\\" in name or
            ":" in name or PurePosixPath(name).is_absolute() or PureWindowsPath(name).drive or
            any(part in ("..", ".") for part in name.split("/")) or "//" in name):
        raise ValueError("Unsafe ZIP member path")
    return name


def wanted(name):
    safe_name(name)
    parts = PurePosixPath(name).parts
    lowered = [part.lower() for part in parts]
    if (name.endswith("/") or any(part.startswith(("skygame", "lasgame")) for part in lowered) or
            any(part in {"keys", "save", "saves", "romfs", "exefs", "extracted", "textures",
                         "texturepack", "texturepacks", "reshade-shaders"} for part in lowered) or
            PurePosixPath(name).suffix.lower() in {".keys", ".nca", ".nsp", ".xci", ".nso", ".dds",
                                                    ".bntx", ".bfres", ".szs", ".webm", ".mp4"}):
        return False
    return ((len(parts) == 1 and (name.lower().endswith(".dll") or name.startswith("LICENSE") or
                                 name in ("Baloo2-SemiBold.ttf", "Baloo2-OFL.txt", "icon.png"))) or
            parts[0] in ("lasgen", "skygen"))


def parse_directory(directory, count, offset):
    records = []
    cursor = 0
    while cursor < len(directory):
        if len(directory) - cursor < 46 or directory[cursor:cursor + 4] != b"PK\x01\x02":
            raise RuntimeError("Invalid ZIP central directory")
        f = struct.unpack_from("<4s6H3L5H2L", directory, cursor)
        nl, el, cl = f[10:13]
        end = cursor + 46 + nl + el + cl
        if end > len(directory) or f[13] or f[3] & 1:
            raise RuntimeError("Invalid, split or encrypted ZIP member")
        name = directory[cursor + 46:cursor + 46 + nl].decode("utf-8" if f[3] & 0x800 else "cp437")
        safe_name(name)
        records.append(dict(archive_name=name, method=f[4], crc=f[7], compressed=f[8], size=f[9], offset=f[16]))
        cursor = end
    if len(records) != count:
        raise RuntimeError("ZIP entry count mismatch")
    # Older official releases wrap every entry in a SkyRecomp-* folder; newer
    # archives put DLLs at the root. Never strip an extracted/ or asset directory.
    first = {entry["archive_name"].split("/", 1)[0] for entry in records}
    wrapper = next(iter(first)) + "/" if len(first) == 1 and next(iter(first)).lower().startswith("skyrecomp") else ""
    records.sort(key=lambda entry: entry["offset"])
    selected = []
    names = set()
    for index, entry in enumerate(records):
        next_offset = records[index + 1]["offset"] if index + 1 < len(records) else offset
        if not 0 <= entry["offset"] < next_offset <= offset:
            raise RuntimeError("Overlapping or out-of-bounds ZIP members")
        relative = entry["archive_name"].removeprefix(wrapper)
        if not relative:
            continue
        if relative in names:
            raise RuntimeError("Duplicate ZIP member path")
        names.add(relative)
        if not wanted(relative):
            continue
        if (entry["method"] not in (0, 8) or entry["size"] > MAX_MEMBER or
                entry["offset"] + 30 + len(entry["archive_name"].encode("utf-8")) + entry["compressed"] > next_offset):
            raise RuntimeError("Unsupported or out-of-bounds support member")
        selected.append(dict(entry, name=relative, end=next_offset - 1))
    if not selected:
        raise RuntimeError("ZIP contains no supported setup files")
    return selected


def members(config):
    size = config["archive_size"]
    # Official releases have no archive comment. Read only the footer and its
    # exact central-directory span, avoiding a padded tail containing game data.
    footer = request(size - 22, size - 1, config)
    f = struct.unpack("<4s4H2LH", footer)
    if f[0] != b"PK\x05\x06" or f[1] or f[2] or f[3] != f[4] or f[7] or f[4] == 0xffff:
        raise RuntimeError("Unsupported ZIP footer (comments, split archive or ZIP64)")
    count, length, offset = f[4:7]
    if not 0 < length <= MAX_DIRECTORY or offset + length != size - 22:
        raise RuntimeError("Invalid ZIP directory bounds")
    return parse_directory(request(offset, offset + length - 1, config), count, offset)


def target_path(entry, config):
    name = entry["name"]
    if not wanted(name):
        raise ValueError("Member is outside the support allowlist")
    out = config["support"]
    target = out / name
    if not target.resolve().is_relative_to(out.resolve()):
        raise ValueError("Support member escapes output directory")
    for path in (target, *target.parents):
        if path == out.parent:
            break
        if path.is_symlink():
            raise ValueError("Symlink in support output path")
    return target


def valid(entry, config):
    target = target_path(entry, config)
    if not target.is_file() or target.stat().st_size != entry["size"]:
        return False
    crc = 0
    digest = hashlib.sha256()
    with target.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            crc = zlib.crc32(block, crc)
            digest.update(block)
    return crc == entry["crc"] and ("sha256" not in entry or digest.hexdigest() == entry["sha256"])


def manifest_entries(config, filename, complete=False):
    path = config["cache"] / filename
    if not path.exists():
        return None
    if path.is_symlink():
        raise ValueError("Symlink support manifest is not supported")
    try:
        if path.stat().st_size > MAX_DIRECTORY:
            return None
        saved = json.loads(path.read_text())
        if not isinstance(saved, dict):
            return None
        if (saved.get("url") != config["url"] or saved.get("archive_size") != config["archive_size"] or
                config["asset_digest"] and saved.get("asset_digest", saved.get("official_archive_digest")) != config["asset_digest"]):
            return None
        raw_entries = saved["entries"]
        if not raw_entries or not isinstance(raw_entries, list):
            return None
        # The isolated upgrade audit records relative/crc32; accept its verified
        # manifest as well as the repository's original name/crc schema.
        entries = []
        for original in raw_entries:
            entry = dict(original)
            if "relative" in entry:
                entry["archive_name"] = entry.get("archive_name", entry["name"])
                entry["name"] = entry["relative"]
            if "crc" not in entry and "crc32" in entry:
                entry["crc"] = int(entry["crc32"], 16)
            entries.append(entry)
        if len({entry["name"] for entry in entries}) != len(entries):
            return None
        for entry in entries:
            target_path(entry, config)
            if any(isinstance(entry[key], bool) or not isinstance(entry[key], int) or entry[key] < 0
                   for key in ("offset", "compressed", "size", "crc", "method")):
                return None
            if entry["size"] > MAX_MEMBER or entry["crc"] > 0xffffffff or entry["method"] not in (0, 8):
                return None
            if complete:
                if not re.fullmatch(r"[0-9a-f]{64}", entry.get("sha256", "")) or not valid(entry, config):
                    return None
            else:
                safe_name(entry["archive_name"])
                if not entry["offset"] < entry["end"] < config["archive_size"]:
                    return None
        return entries
    except (KeyError, TypeError, ValueError, OSError):
        return None


def groups(entries):
    result = []
    for entry in sorted(entries, key=lambda entry: entry["offset"]):
        if entry["end"] - entry["offset"] + 1 > MAX_MEMBER:
            raise RuntimeError("Support member range exceeds the request limit")
        if (result and entry["offset"] == result[-1][1] + 1 and
                entry["end"] - result[-1][0] + 1 <= MAX_MEMBER):
            result[-1][1] = entry["end"]
            result[-1][2].append(entry)
        else:
            result.append([entry["offset"], entry["end"], [entry]])
    return result


def fetch(entry, archive, archive_start, config):
    target = target_path(entry, config)
    if valid(entry, config):
        return
    if target.exists():
        raise FileExistsError(f"Existing support file was preserved: {target}; use a fresh cache to replace it")
    archive.seek(entry["offset"] - archive_start)
    header = archive.read(30)
    if len(header) != 30:
        raise RuntimeError("Truncated ZIP local header")
    h = struct.unpack("<4s5H3L2H", header)
    if h[0] != b"PK\x03\x04" or h[3] != entry["method"] or h[2] & 1 or h[3] not in (0, 8):
        raise RuntimeError("Unsupported ZIP local member")
    if not h[2] & 8 and (h[6], h[7], h[8]) != (entry["crc"], entry["compressed"], entry["size"]):
        raise RuntimeError("ZIP local size/CRC disagrees with directory")
    name = archive.read(h[9]).decode("utf-8" if h[2] & 0x800 else "cp437")
    if name != entry["archive_name"]:
        raise RuntimeError("ZIP local member name disagrees with directory")
    start = entry["offset"] + 30 + h[9] + h[10]
    if start + entry["compressed"] - 1 > entry["end"]:
        raise RuntimeError("ZIP payload overlaps another member")
    archive.seek(start - archive_start)
    decoder = zlib.decompressobj(-15) if h[3] == 8 else None
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    length = crc = 0
    try:
        with tempfile.NamedTemporaryFile(prefix=target.name + ".", suffix=".part", dir=target.parent, delete=False) as output:
            temporary = Path(output.name)
            remaining = entry["compressed"]
            while remaining:
                block = archive.read(min(65536, remaining))
                if not block:
                    raise RuntimeError("Truncated ZIP range")
                remaining -= len(block)
                decoded = decoder.decompress(block, entry["size"] - length + 1) if decoder else block
                length += len(decoded)
                if length > entry["size"] or decoder and decoder.unconsumed_tail:
                    raise RuntimeError("ZIP member exceeds expected size")
                crc = zlib.crc32(decoded, crc)
                output.write(decoded)
            if decoder and (not decoder.eof or decoder.unused_data):
                raise RuntimeError("Invalid ZIP compressed stream")
            if length != entry["size"] or crc != entry["crc"]:
                raise RuntimeError("ZIP member checksum mismatch")
        # Publish only verified files and never overwrite a racing delivery.
        os.link(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def write_manifest(config, entries, filename):
    payload = dict(url=config["url"], archive_size=config["archive_size"], asset_digest=config["asset_digest"],
                   validation="Exact206 ranges; ZIP length/CRC32 and local SHA256. Entire archive digest not computed.",
                   entries=entries)
    target = config["cache"] / filename
    with tempfile.NamedTemporaryFile(mode="w", prefix=filename, suffix=".part", dir=config["cache"], delete=False) as output:
        temporary = Path(output.name)
        json.dump(payload, output, indent=2)
        output.write("\n")
    try:
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def fetch_notices(root):
    notices = root / ".deps/windows-licenses"
    if not notices.resolve().is_relative_to(root):
        raise ValueError("Third-party notices directory escapes the project")
    notices.mkdir(parents=True, exist_ok=True)
    for name, url in NOTICES.items():
        destination = notices / name
        if destination.is_symlink():
            raise ValueError("Symlink third-party notice is not supported")
        if not destination.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(8 * 1024 * 1024 + 1)
            if len(data) > 8 * 1024 * 1024:
                raise RuntimeError("Third-party notice exceeds size limit")
            destination.write_bytes(data)
    for name, data in {
        "libiconv-NOTICE.txt": "GNU libiconv, Copyright (C) Free Software Foundation, Inc.\nLGPL-2.1-or-later; see FFmpeg-LGPL-2.1.txt.\nCorresponding sources: https://ftp.gnu.org/pub/gnu/libiconv/\n",
        "sources.json": json.dumps(NOTICES, indent=2) + "\n",
    }.items():
        destination = notices / name
        if destination.is_symlink():
            raise ValueError("Symlink third-party notice is not supported")
        if not destination.exists():
            destination.write_text(data)


def main(root=ROOT):
    config = load_config(root)
    config["cache"].mkdir(parents=True, exist_ok=True)
    entries = manifest_entries(config, "support-manifest.json", complete=True)
    if entries is not None:
        print(f"Verified complete cached support ({len(entries)} files); no release directory request.")
        fetch_notices(config["root"])
        return
    cooldown = config["cache"] / "rate-limit.json"
    if cooldown.is_symlink():
        raise ValueError("Symlink rate-limit record is not supported")
    if cooldown.exists():
        saved = json.loads(cooldown.read_text())
        if saved.get("url") == config["url"] and saved.get("not_before", 0) > time.time():
            raise RuntimeError("GitHub Retry-After cooldown is still active; verified support files were retained")
    try:
        entries = manifest_entries(config, "directory-manifest.json")
        if entries is None:
            entries = members(config)
            write_manifest(config, entries, "directory-manifest.json")
        for entry in entries:
            target = target_path(entry, config)
            if target.exists() and not valid(entry, config):
                raise FileExistsError(f"Existing mismatched support file was preserved: {target}; select a fresh cache")
        needed = [entry for entry in entries if not valid(entry, config)]
        for index, (start, end, group) in enumerate(groups(needed), 1):
            print(f"Downloading support range {index} ({end-start+1} bytes)", flush=True)
            data = request(start, end, config)
            archive = io.BytesIO(data)
            for entry in group:
                fetch(entry, archive, start, config)
        manifest = []
        for entry in entries:
            if not valid(entry, config):
                raise RuntimeError("Missing or corrupt support file: " + entry["name"])
            digest = hashlib.sha256(target_path(entry, config).read_bytes()).hexdigest()
            manifest.append(dict(entry, sha256=digest))
        write_manifest(config, manifest, "support-manifest.json")
    except RateLimitedError as error:
        cooldown.write_text(json.dumps(dict(url=config["url"], retry_after=error.retry_after,
                                           not_before=error.not_before), indent=2) + "\n")
        raise
    fetch_notices(config["root"])


if __name__ == "__main__":
    main()
