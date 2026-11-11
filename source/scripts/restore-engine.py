#!/usr/bin/env python3
"""Reconstruct the exact tested public engine tree in a NEW directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def git(folder, *args):
    return subprocess.check_output(['git', '-C', str(folder), '-c', 'core.hooksPath=/dev/null',
        '-c', 'commit.gpgsign=false', '-c', 'user.name=SST source restoration',
        '-c', 'user.email=sst-source@localhost', *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', default='https://github.com/Apolinario-coder/SkyRecomp.git')
    args = parser.parse_args()
    subprocess.run(['python3', str(ROOT / 'scripts/verify-source.py')], check=True)
    output = ROOT / 'engine'
    if output.exists() or output.is_symlink():
        raise SystemExit('Preserve the existing engine; choose a fresh source checkout.')
    state = json.loads((ROOT / 'validation/native-source.json').read_text())
    patches = sorted((ROOT / 'engine-patches').glob('*.patch'))
    assert len(patches) == state['engine_patches']
    assert (ROOT / 'engine-patches/base.txt').read_text().strip() == state['base_commit']
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='engine-restore-', dir=build) as work:
        engine = Path(work) / 'engine'
        subprocess.run(['git', 'clone', '--quiet', '--no-hardlinks', '--no-checkout', args.repository, str(engine)], check=True)
        git(engine, '-c', 'advice.detachedHead=false', 'checkout', '--quiet', '--detach', state['base_commit'])
        git(engine, '-c', 'am.keepcr=true', 'am', *map(str, patches))
        assert git(engine, 'rev-parse', 'HEAD^{tree}') == state['engine_tree'], 'Imported engine tree mismatch'
        for name, expected in state['native_file_sha256'].items():
            path = engine / name[len('engine/'):] if name.startswith('engine/') else ROOT / name
            with path.open('rb') as stream:
                assert hashlib.file_digest(stream, 'sha256').hexdigest() == expected, name
        engine.rename(output)
    assert (output / 'runtime/sst').resolve() == (ROOT / 'client/src').resolve()
    print('Restored exact tested engine and client source; no compiler or game launched.')


if __name__ == '__main__':
    main()
