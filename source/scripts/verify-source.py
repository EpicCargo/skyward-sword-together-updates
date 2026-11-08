#!/usr/bin/env python3
"""Verify this source-only snapshot; no compiler, game or network access."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    manifest = json.loads((ROOT / 'SOURCE-SHA256.json').read_text())
    for name, expected in manifest.items():
        path = ROOT / name
        if path.is_symlink() or not path.is_file():
            raise SystemExit('Missing or symlinked source file: ' + name)
        with path.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                raise SystemExit('Source checksum mismatch: ' + name)
    print(f'Verified {len(manifest)} source files; no compiler or game launched.')


if __name__ == '__main__':
    main()
