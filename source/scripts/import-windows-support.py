#!/usr/bin/env python3
"""Import a trusted SST dependency-cache ZIP without downloading or executing files.

The required SHA256 must come from the backup's separately preserved receipt.
Only support files, their manifests and third-party notices are installed. Cached
import libraries and the archived source configuration are deliberately skipped.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sst_support_fetcher', ROOT / 'scripts/fetch-windows-support.py')
fetcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetcher)
MAX_ARCHIVE = 256 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
MAX_FILES = 4096


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def read_json(saved, name):
    if saved.getinfo(name).file_size > fetcher.MAX_DIRECTORY:
        raise ValueError('Oversized cache metadata: ' + name)
    return json.loads(saved.read(name), object_pairs_hook=unique_object)


def digest_file(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def destination(root, name):
    """Reject symlinks in every component before reading or publishing a file."""
    fetcher.safe_name(name)
    target = root
    for part in name.split('/'):
        target = target / part
        if target.is_symlink():
            raise ValueError('Symlink in cache destination: ' + str(target))
        if target != root / name and target.exists() and not target.is_dir():
            raise ValueError('Cache parent is not a directory: ' + str(target))
    if target.exists() and not target.is_file():
        raise ValueError('Cache destination is not a regular file: ' + str(target))
    return target


def import_cache(root, archive, expected_sha256):
    if not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256):
        raise ValueError('Supply the separately recorded 64-character lowercase SHA256')
    root, archive = Path(root).resolve(strict=True), Path(archive)
    if archive.is_symlink() or not archive.is_file() or archive.stat().st_size > MAX_ARCHIVE:
        raise ValueError('Cache archive must be a regular ZIP of at most 256 MiB')
    config = fetcher.load_config(root)
    cache = config['cache'].relative_to(root).as_posix()
    support = config['support'].relative_to(root).as_posix()
    prefix = config['prefix'].relative_to(root).as_posix()
    if not support.startswith(cache + '/'):
        raise ValueError('The configured support directory must be inside its cache')
    notices = '.deps/windows-licenses/'
    required_notices = {notices + name for name in fetcher.NOTICES} | {
        notices + 'libiconv-NOTICE.txt', notices + 'sources.json'}
    manifest_name = cache + '/support-manifest.json'
    directory_name = cache + '/directory-manifest.json'
    config_name = 'engine-patches/windows-support.json'
    # Freeze and hash the very bytes subsequently parsed, including if the input
    # path is replaced while copying. No caller-owned archive is extracted in place.
    with tempfile.TemporaryFile() as frozen:
        outer_hash = hashlib.sha256()
        length = 0
        with archive.open('rb') as stream:
            while block := stream.read(1024 * 1024):
                length += len(block)
                if length > MAX_ARCHIVE:
                    raise ValueError('Cache archive exceeds its size limit')
                outer_hash.update(block)
                frozen.write(block)
        if outer_hash.hexdigest() != expected_sha256:
            raise ValueError('Cache archive SHA256 does not match the trusted receipt')
        frozen.seek(0)
        with zipfile.ZipFile(frozen) as saved:
            infos = saved.infolist()
            if not 1 <= len(infos) <= MAX_FILES or sum(i.file_size for i in infos) > MAX_TOTAL:
                raise ValueError('Cache archive exceeds its inventory or expanded-size limit')
            names = set()
            aliases = set()
            for info in infos:
                fetcher.safe_name(info.filename)
                mode = info.external_attr >> 16
                if (info.is_dir() or stat.S_IFMT(mode) not in (0, stat.S_IFREG) or info.flag_bits & 1 or
                        info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) or
                        info.file_size > fetcher.MAX_MEMBER or info.filename.casefold() in aliases):
                    raise ValueError('Unsupported or duplicate cache ZIP member: ' + info.filename)
                names.add(info.filename)
                aliases.add(info.filename.casefold())
            if 'CACHE-CHECKSUMS.json' not in names:
                raise ValueError('Missing cache checksum inventory')
            inventory = read_json(saved, 'CACHE-CHECKSUMS.json')
            if not isinstance(inventory, dict) or not inventory or names != set(inventory) | {'CACHE-CHECKSUMS.json'}:
                raise ValueError('Cache ZIP and checksum inventory differ')
            selected = set()
            for name, expected in inventory.items():
                if not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected):
                    raise ValueError('Invalid cache checksum: ' + name)
                if name.startswith(support + '/') and fetcher.wanted(name[len(support) + 1:]):
                    selected.add(name)
                elif name in required_notices or name in (manifest_name, directory_name):
                    selected.add(name)
                elif name != config_name and not name.startswith(prefix + '/'):
                    raise ValueError('Member outside the dependency-cache layout: ' + name)
                with saved.open(name) as stream:
                    if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                        raise ValueError('Cache member checksum mismatch: ' + name)
            if not {config_name, manifest_name, *required_notices} <= names:
                raise ValueError('Cache is missing its configuration, support manifest or third-party notices')
            read_json(saved, manifest_name)
            if directory_name in names:
                read_json(saved, directory_name)
            archived_config = read_json(saved, config_name)
            if not isinstance(archived_config, dict) or any(archived_config.get(key) != value for key, value in (
                    ('url', config['url']), ('archive_size', config['archive_size']),
                    ('asset_digest', config['asset_digest']), ('cache', cache), ('support', support), ('prefix', prefix))):
                raise ValueError('Cache belongs to a different Windows support recipe')
            # Stage on the destination filesystem so publishing uses no-clobber
            # hard links. Validate every staged dependency before any cache update.
            deps = root / '.deps'
            if deps.is_symlink() or deps.exists() and not deps.is_dir():
                raise ValueError('The .deps directory must not be a symlink or file')
            deps.mkdir(exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='support-import-', dir=deps) as temporary:
                stage = Path(temporary)
                for name in sorted(selected):
                    target = stage / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with saved.open(name) as source, target.open('xb') as output:
                        while block := source.read(1024 * 1024):
                            output.write(block)
                staged = dict(config, root=stage, cache=stage / cache, support=stage / support)
                entries = fetcher.manifest_entries(staged, 'support-manifest.json', complete=True)
                if not entries or {support + '/' + e['name'] for e in entries} != {
                        n for n in selected if n.startswith(support + '/')}:
                    raise ValueError('Support manifest does not verify the exact staged support inventory')
                if directory_name in selected and fetcher.manifest_entries(staged, 'directory-manifest.json') is None:
                    raise ValueError('Invalid cached release directory manifest')
                for name in selected:
                    target = destination(root, name)
                    if target.exists() and digest_file(target) != inventory[name]:
                        raise FileExistsError('Existing cache file was preserved: ' + str(target))
                imported = reused = 0
                # The complete support manifest becomes visible last. An interrupted
                # import retains only verified files and can be safely rerun.
                for name in sorted(selected, key=lambda n: (n == manifest_name, n)):
                    target = destination(root, name)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    destination(root, name)
                    try:
                        os.link(stage / name, target)
                        imported += 1
                    except FileExistsError:
                        target = destination(root, name)
                        if digest_file(target) != inventory[name]:
                            raise FileExistsError('Racing cache file was preserved: ' + str(target)) from None
                        reused += 1
                if fetcher.manifest_entries(config, 'support-manifest.json', complete=True) is None:
                    raise RuntimeError('Imported support failed its final verification')
    return dict(passed=True,archive_sha256=expected_sha256,verified_archive_members=len(inventory),
                support_files=len(entries),imported_files=imported,reused_files=reused,
                network_used=False,compiler_launched=False,game_launched=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True, help='verified dependency-cache backup ZIP')
    parser.add_argument('--sha256', required=True, help='SHA256 from the separately preserved backup receipt')
    args = parser.parse_args()
    print(json.dumps(import_cache(ROOT, args.archive, args.sha256), indent=2))


if __name__ == '__main__':
    main()
