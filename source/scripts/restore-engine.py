#!/usr/bin/env python3
"""Reconstruct the exact tested public engine tree in a NEW directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPOSITORIES = (
    'https://github.com/Apolinario-coder/SkyRecomp.git',
    'https://github.com/printfFriendo/SkyRecomp-Android.git',
)


def git(folder, *args):
    return subprocess.check_output(['git', '-C', str(folder), '-c', 'core.hooksPath=/dev/null',
        '-c', 'commit.gpgsign=false', '-c', 'user.name=SST source restoration',
        '-c', 'user.email=sst-source@localhost', *args], text=True).strip()


def clone_base(repositories, engine, base):
    """Accept an available source only when it contains the exact pinned commit."""
    for index, repository in enumerate(repositories, 1):
        cloned = subprocess.run(['git', '-c', 'core.hooksPath=/dev/null', 'clone',
            '--quiet', '--no-hardlinks', '--no-checkout', str(repository), str(engine)],
            env=dict(os.environ, GIT_TERMINAL_PROMPT='0', GCM_INTERACTIVE='Never')).returncode == 0
        if cloned:
            try:
                git(engine, '-c', 'advice.detachedHead=false', 'checkout', '--quiet', '--detach', base)
                print(f'Engine source {index} supplied pinned base {base}.', flush=True)
                return
            except subprocess.CalledProcessError:
                pass
        # This directory was created by our clone inside a fresh temporary
        # workspace. Never reuse a partial clone or touch the caller's source.
        if engine.exists():
            shutil.rmtree(engine)
        print(f'Engine source {index} could not supply the pinned base.', file=sys.stderr)
    raise RuntimeError('No configured engine source supplied the exact pinned base commit.')


def restore(repositories):
    subprocess.run([sys.executable, str(ROOT / 'scripts/verify-source.py')], check=True)
    output = ROOT / 'engine'
    if output.exists() or output.is_symlink():
        raise SystemExit('Preserve the existing engine; choose a fresh source checkout.')
    state = json.loads((ROOT / 'validation/native-source.json').read_text())
    patches = sorted((ROOT / 'engine-patches').glob('*.patch'))
    if len(patches) != state['engine_patches']:
        raise RuntimeError('Engine patch count does not match the frozen source record.')
    if (ROOT / 'engine-patches/base.txt').read_text().strip() != state['base_commit']:
        raise RuntimeError('Engine base does not match the frozen source record.')
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='engine-restore-', dir=build) as work:
        engine = Path(work) / 'engine'
        clone_base(repositories, engine, state['base_commit'])
        git(engine, '-c', 'am.keepcr=true', 'am', *map(str, patches))
        if git(engine, 'rev-parse', 'HEAD^{tree}') != state['engine_tree']:
            raise RuntimeError('Imported engine tree mismatch')
        for name, expected in state['native_file_sha256'].items():
            path = engine / name[len('engine/'):] if name.startswith('engine/') else ROOT / name
            with path.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                    raise RuntimeError('Native source checksum mismatch: ' + name)
        link = engine / 'runtime/sst'
        if not link.is_symlink() or (output / 'runtime' / link.readlink()).resolve() != (ROOT / 'client/src').resolve():
            raise RuntimeError('Restored engine client link does not target this source checkout.')
        engine.rename(output)
    print('Restored exact tested engine and client source; no compiler or game launched.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', action='append', metavar='PATH_OR_URL',
        help='ordered source override; repeat for explicit alternatives (disables defaults)')
    args = parser.parse_args()
    restore(args.repository if args.repository is not None else DEFAULT_REPOSITORIES)


if __name__ == '__main__':
    main()
