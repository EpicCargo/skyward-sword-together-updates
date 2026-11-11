#!/usr/bin/env python3
"""Create, verify and restore the default source ZIP; no compiler or game."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=ROOT / 'engine',
                        help='existing local SkyRecomp repository containing the pinned base')
    args = parser.parse_args()
    repository = args.repository.resolve(strict=True)
    subprocess.run([sys.executable, str(ROOT / 'scripts/verify-source.py')], check=True)
    sys.path.insert(0, str(ROOT / 'scripts'))
    spec = importlib.util.spec_from_file_location('sst_source_archive_builder', ROOT / 'scripts/build-windows.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    manifest = json.loads((ROOT / 'SOURCE-SHA256.json').read_text())
    build = ROOT / 'build';build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='source-archive-check-', dir=build) as folder:
        work = Path(folder);archive = work / 'co-op-source.zip'
        builder.write_coop_source(archive)
        with zipfile.ZipFile(archive) as saved:
            names = saved.namelist()
            if len(names) != len(set(names)) or set(names) != set(manifest) | {'SOURCE-SHA256.json'}:
                raise RuntimeError('Source archive inventory differs from the verified snapshot')
            if saved.read('SOURCE-SHA256.json') != (ROOT / 'SOURCE-SHA256.json').read_bytes():
                raise RuntimeError('Source archive manifest changed')
            for name, expected in manifest.items():
                if hashlib.sha256(saved.read(name)).hexdigest() != expected:
                    raise RuntimeError('Archived source checksum mismatch: ' + name)
            # The producer validated canonical relative names and every parent
            # before freezing these bytes; the exact inventory was checked above.
            restored = work / 'source';saved.extractall(restored)
        subprocess.run([sys.executable, str(restored / 'scripts/restore-engine.py'),
                        '--repository', str(repository)], cwd=restored, check=True, timeout=180)
    print('Default source ZIP inventory, checksums and exact engine restoration PASS; no compiler or game.')


if __name__ == '__main__':
    main()
