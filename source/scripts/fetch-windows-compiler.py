#!/usr/bin/env python3
"""Fetch the pinned native Windows LLVM-MinGW compiler for first-run game setup."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAME = "llvm-mingw-20261020-ucrt-x86_64"
SHA256 = "fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd"
URL = f"https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/{NAME}.zip"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def main():
    cache = ROOT / ".deps"
    archive = cache / (NAME + ".zip")
    if not archive.exists() or digest(archive) != SHA256:
        partial = archive.with_suffix(".zip.part")
        try:
            with urllib.request.urlopen(URL, timeout=90) as response, partial.open("wb") as output:
                for block in iter(lambda: response.read(1048576), b""):
                    output.write(block)
            if digest(partial) != SHA256:
                raise RuntimeError("Windows compiler SHA256 mismatch")
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    destination = cache / NAME
    stamp = destination / ".sst-verified-sha256"
    if not stamp.exists() or stamp.read_text().strip() != SHA256:
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                path = Path(item.filename)
                if path.is_absolute() or ".." in path.parts or path.parts[0] != NAME:
                    raise RuntimeError("Unsafe compiler archive path")
            source.extractall(cache)
        stamp.write_text(SHA256 + "\n")
    print(destination)


if __name__ == "__main__":
    main()
