#!/usr/bin/env python3
"""Compile an asset-free Linux runtime on a guarded CI runner.

Frozen ELF interfaces supply link metadata only. The artifact contains no link
interfaces, dependency implementations or game library; execution requires the
real libraries identified in its receipt. No executable is launched by this tool.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from linux_link_interfaces import build_interfaces, load_interfaces, validate_runtime

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'third_party/windows-build-headers-v202'
INTERFACES = ROOT / 'third_party/linux-link-interfaces-v202.json'

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def inputs():
    spec = importlib.util.spec_from_file_location('frozen_runtime_headers', ROOT / 'scripts/build-windows-runtime-ci.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    headers = sdk.verify_sdk()
    config = load_interfaces(INTERFACES)
    return headers, config

def native_identity():
    state = json.loads((ROOT / 'validation/native-source.json').read_text())
    engine = ROOT / 'engine'
    tree = subprocess.check_output(['git', '-C', str(engine), 'rev-parse', 'HEAD^{tree}'], text=True).strip()
    if tree != state['engine_tree']:
        raise RuntimeError('Restored engine tree differs from the native receipt')
    for name, expected in state['native_file_sha256'].items():
        if sha(ROOT / name) != expected:
            raise RuntimeError('Native source changed: ' + name)
    return state

def build():
    if os.environ.get('GITHUB_ACTIONS') != 'true' or not os.environ.get('SST_RESOURCE_RUNNER_PID'):
        raise RuntimeError('Use the guarded GitHub Actions worker; local full builds retain both-OS preflights')
    headers, config = inputs()
    native = native_identity()
    head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    engine = ROOT / 'engine'
    if any((engine / name).exists() for name in ('generated', 'generated-x86_64', 'generated-aarch64', 'extracted')):
        raise RuntimeError('CI runtime checkout must contain no generated game inputs')
    work = ROOT / 'build/linux-runtime-ci'
    work.mkdir(parents=True, exist_ok=False)
    prefix = work / 'link-prefix'
    prefix.mkdir()
    with (work / 'interfaces.log').open('x') as log:
        build_interfaces(config, prefix / 'lib', log)
    includes = [SDK / 'include', SDK / 'vulkan/include', SDK / 'vma/include']
    env = dict(os.environ, C_INCLUDE_PATH=':'.join(map(str, includes)),
               CPLUS_INCLUDE_PATH=':'.join(map(str, includes)), MAKEFLAGS='', MFLAGS='')
    output = work / 'runtime'
    binary = output / 'skyrecomp'
    with (work / 'build.log').open('x') as log:
        subprocess.run(['make', '-C', str(engine), '-j1', 'PLATFORM=linux', 'ARCH=x86_64',
            'B=' + str(output), 'DEPS=' + str(prefix), 'CC=clang', 'CXX=clang++',
            'SHADERC=-lshaderc_shared', str(binary)], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    elf = validate_runtime(binary, config)
    if any(output.rglob('skygame*')):
        raise RuntimeError('Unexpected game library output')
    artifacts = work / 'artifacts'
    artifacts.mkdir()
    shutil.copyfile(binary, artifacts / 'skyrecomp')
    shutil.copytree(SDK / 'licenses', artifacts / 'licenses')
    shutil.copyfile(ROOT / 'ENGINE-LICENSE', artifacts / 'licenses/SkyRecomp-LICENSE.txt')
    for source in (engine / 'third_party').rglob('*'):
        if source.is_file() and source.name.lower().startswith(('license', 'copying', 'notice')):
            data = source.read_bytes()
            if b'\0' in data:
                raise RuntimeError('Unexpected non-text third-party notice')
            target = artifacts / 'licenses/engine-third-party' / source.relative_to(engine / 'third_party')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    report = dict(passed=True, scope='Asset-free CI Linux compile/link and static ABI validation; gameplay remains separate',
        source_commit=head, source_files=len(json.loads((ROOT / 'SOURCE-SHA256.json').read_text())),
        source_inventory_sha256=sha(ROOT / 'SOURCE-SHA256.json'), engine_tree=native['engine_tree'],
        engine_patches=native['engine_patches'], native_source_files=len(native['native_file_sha256']),
        public_header_files=len(headers), interface_metadata_sha256=sha(INTERFACES),
        dependency_libraries={r['soname']:r['real_library_sha256'] for r in config['libraries']},
        compiler=subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
        compiler_jobs=1, machine=elf['machine'], elf=elf,
        gamelib_id=re.search(r'^GAMELIB_ID \?= (\w+)', (engine / 'Makefile').read_text(), re.M)[1],
        binary_sha256=sha(artifacts / 'skyrecomp'), link_interfaces_packaged=False,
        real_dependency_implementations_packaged=False, game_files_used=False,
        game_library_built=False, runtime_executed=False, game_launched=False, installed=False, release_feed_updated=False)
    (artifacts / 'BUILD-RECEIPT.json').write_text(json.dumps(report, indent=2) + '\n')
    (artifacts / 'README-ci.txt').write_text(
        'SST Linux runtime validation artifact.\n'
        'Compiled against frozen public headers and versioned ELF link interfaces.\n'
        'The link interfaces have trap bodies and are deliberately NOT included.\n'
        'Execution requires the real dependency libraries identified in BUILD-RECEIPT.json.\n'
        'Game files, game library and personal saves are absent. No runtime or game was launched in CI.\n')
    inventory = {p.relative_to(artifacts).as_posix():sha(p) for p in sorted(artifacts.rglob('*')) if p.is_file()}
    (artifacts / 'ARTIFACT-SHA256.json').write_text(json.dumps(inventory, indent=2) + '\n')
    native_identity()
    inputs()
    print(json.dumps({k:report[k] for k in ('passed', 'source_commit', 'engine_tree', 'binary_sha256', 'runtime_executed')}), flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validate-inputs', action='store_true')
    args = parser.parse_args()
    if args.validate_inputs:
        headers, config = inputs()
        print(f'Verified {len(headers)} public header files and {len(config["libraries"])} link interfaces; no compiler or game')
    else:
        build()

if __name__ == '__main__':
    main()
