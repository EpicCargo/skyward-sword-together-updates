"""Versioned ELF link interfaces for the frozen public Linux dependencies.

These objects describe symbols to the linker. Their function bodies trap and
must never be executed or packaged as runtime libraries. Gameplay uses the real
libraries identified by the captured SHA256 values and SONAMEs.
"""
import json
import os
from pathlib import Path
import re
import struct
import subprocess

SONAMES = ('libSDL3.so.0', 'libavformat.so.63', 'libavcodec.so.63',
           'libavutil.so.61', 'libswscale.so.10', 'libshaderc_shared.so.1')
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z_0-9]*\Z')
VERSION = re.compile(r'[A-Za-z_][A-Za-z_0-9.]*\Z')

def validate_library(library):
    if library.get('soname') not in SONAMES:
        raise ValueError('Unknown Linux dependency SONAME')
    if not re.fullmatch('[0-9a-f]{64}', library.get('real_library_sha256', '')):
        raise ValueError('Missing real dependency identity')
    symbols = library.get('symbols')
    if not isinstance(symbols, list) or not 1 <= len(symbols) <= 2048:
        raise ValueError('Invalid interface symbol count')
    seen, versions = set(), set()
    for symbol in symbols:
        name, version = symbol.get('name', ''), symbol.get('version')
        if not isinstance(name, str) or not IDENTIFIER.fullmatch(name) or name in seen:
            raise ValueError('Invalid or duplicate interface symbol')
        seen.add(name)
        if symbol.get('kind') not in ('FUNC', 'OBJECT') or symbol.get('binding') not in ('GLOBAL', 'WEAK'):
            raise ValueError('Unsupported interface symbol type')
        size = symbol.get('size')
        if type(size) is not int or not 0 <= size <= 1024 * 1024:
            raise ValueError('Invalid interface symbol size')
        if version is not None and (not isinstance(version, str) or not VERSION.fullmatch(version)):
            raise ValueError('Invalid interface symbol version')
        versions.add(version)
    if len(versions) != 1:
        raise ValueError('Mixed/non-default symbol versions need explicit support')
    return library

def load_interfaces(path):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError('Invalid link metadata file')
    config = json.loads(path.read_text())
    if config.get('format') != 1 or config.get('architecture') != 'x86_64-linux-gnu':
        raise ValueError('Unsupported interface format/architecture')
    libraries = config.get('libraries', [])
    if len(libraries) != len(SONAMES) or {r.get('soname') for r in libraries} != set(SONAMES):
        raise ValueError('Incomplete/duplicate link interfaces')
    for library in libraries:
        validate_library(library)
    return config

def interface_sources(library):
    validate_library(library)
    lines = ['// LINK ONLY: trap bodies are never runtime implementations.']
    for symbol in library['symbols']:
        name = symbol['name']
        lines.append(('.weak ' if symbol['binding'] == 'WEAK' else '.globl ') + name)
        if symbol['kind'] == 'FUNC':
            lines.extend(['.text', '.type ' + name + ', @function', name + ':',
                          '  .byte 0xcc', '.size ' + name + ', .-' + name])
        else:
            lines.extend(['.bss', '.balign 64', '.type ' + name + ', @object', name + ':',
                          '  .zero ' + str(max(1, symbol['size'])), '.size ' + name + ', ' + str(symbol['size'])])
    lines.append('.section .note.GNU-stack,"",@progbits')
    version = library['symbols'][0]['version']
    mapping = (version + ' { global:\n' + ''.join('  ' + s['name'] + ';\n' for s in library['symbols']) +
               ' local: *;\n};\n') if version else None
    return '\n'.join(lines) + '\n', mapping

def build_interfaces(config, destination, log, compiler='clang'):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    for library in config['libraries']:
        assembly, mapping = interface_sources(library)
        name = library['soname']
        source = destination / (name + '.S')
        source.write_text(assembly)
        flags = []
        if mapping:
            version_file = destination / (name + '.map')
            version_file.write_text(mapping)
            flags.append('-Wl,--version-script=' + str(version_file))
        subprocess.run([compiler, '-shared', '-nostdlib', '-fuse-ld=lld',
            '-Wl,-soname,' + name, *flags, str(source), '-o', str(destination / name)],
            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        linker_name = name.split('.so')[0] + '.so'
        (destination / linker_name).symlink_to(name)
    return destination

def inspect_elf(path):
    path = Path(path)
    with path.open('rb') as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:6] != b'\x7fELF\x02\x01' or
            struct.unpack_from('<H', header, 18)[0] != 62 or struct.unpack_from('<H', header, 16)[0] not in (2, 3)):
        raise ValueError('Expected an x86-64 ELF executable/shared object')
    env = dict(os.environ, LC_ALL='C')
    dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True, env=env, timeout=30)
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(path)], text=True, env=env, timeout=30)
    needed = re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic)
    imports = []
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[6] == 'UND':
            imports.append(fields[7])
    return dict(machine='AMD64', needed=needed, imports=sorted(imports),
                has_runtime_search_path=bool(re.search(r'\((?:RPATH|RUNPATH)\)', dynamic)))

def validate_runtime(path, config):
    elf = inspect_elf(path)
    if not set(SONAMES) <= set(elf['needed']) or elf['has_runtime_search_path']:
        raise ValueError('Runtime must reference the real dependency SONAMEs without a link-interface search path')
    exports = {s['name']:s['version'] for r in config['libraries'] for s in r['symbols']}
    for symbol in elf['imports']:
        name, _, version = symbol.partition('@')
        if name.startswith(('SDL_', 'shaderc_', 'av_', 'avcodec_', 'avformat_', 'avio_', 'avpriv_', 'sws_')):
            if name not in exports or (exports[name] or '') != version:
                raise ValueError('Runtime import not covered by the frozen ABI: ' + symbol)
    return elf
