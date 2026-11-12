"""Offline import regressions use synthetic DLL/tool bytes; nothing is executed."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile
import zlib

spec = importlib.util.spec_from_file_location('sst_support_import', Path(__file__).resolve().parents[1] / 'import-windows-support.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


class SupportImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.root = self.folder / 'source'
        patches = self.root / 'engine-patches'
        patches.mkdir(parents=True)
        self.recipe = json.loads((importer.ROOT / 'engine-patches/windows-support.json').read_text())
        self.recipe_bytes = json.dumps(self.recipe).encode()
        (patches / 'windows-support.json').write_bytes(self.recipe_bytes)
        self.cache = self.recipe['cache']
        self.support = self.recipe['support']
        self.prefix = self.recipe['prefix']
        self.contents = {
            self.support + '/SDL3.dll': b'synthetic DLL; never executable',
            self.support + '/skygen/skygen.exe': b'synthetic setup tool; never executable',
            self.prefix + '/lib/generated.dll.a': b'ignored generated import library',
            'engine-patches/windows-support.json': self.recipe_bytes,
        }
        notices = set(importer.fetcher.NOTICES) | {'libiconv-NOTICE.txt', 'sources.json'}
        self.contents.update({'.deps/windows-licenses/' + n: b'fixture third-party notice' for n in notices})
        entries = [dict(name=n, archive_name=n, size=len(data), compressed=len(data), offset=i * 1000,
                        end=i * 1000 + 900, crc=zlib.crc32(data), method=0,
                        sha256=hashlib.sha256(data).hexdigest())
                   for i, (n, data) in enumerate((
                       ('SDL3.dll', self.contents[self.support + '/SDL3.dll']),
                       ('skygen/skygen.exe', self.contents[self.support + '/skygen/skygen.exe'])))]
        self.manifest_name = self.cache + '/support-manifest.json'
        self.manifest = dict(url=self.recipe['url'],archive_size=self.recipe['archive_size'],
                             asset_digest=self.recipe['asset_digest'],entries=entries)
        self.contents[self.manifest_name] = json.dumps(self.manifest).encode()
        self.archive = self.folder / 'cache.zip'
        self.network = mock.patch.object(importer.fetcher.urllib.request, 'urlopen',
                                        side_effect=AssertionError('Offline importer attempted HTTP'))
        self.network.start()
        self.addCleanup(self.network.stop)

    def save(self, *, extra=(), inventory=None):
        inventory = inventory if inventory is not None else {
            n: hashlib.sha256(data).hexdigest() for n, data in self.contents.items()}
        with zipfile.ZipFile(self.archive, 'w', zipfile.ZIP_DEFLATED) as saved:
            for n, data in self.contents.items():
                saved.writestr(n, data)
            saved.writestr('CACHE-CHECKSUMS.json', json.dumps(inventory))
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                for n, data in extra:
                    saved.writestr(n, data)
        return importer.digest_file(self.archive)

    def run_import(self, **kwargs):
        return importer.import_cache(self.root, self.archive, self.save(**kwargs))

    def tree(self):
        return {str(p.relative_to(self.root)):p.read_bytes() for p in self.root.rglob('*') if p.is_file()}

    def expect_rejection(self, error=ValueError, **kwargs):
        before = self.tree()
        with self.assertRaises(error):
            self.run_import(**kwargs)
        self.assertEqual(self.tree(), before)
        self.assertFalse(list(self.root.glob('.deps/support-import-*')))

    def test_matching_cache_imports_offline_and_can_be_reused(self):
        result = self.run_import()
        self.assertTrue(result['passed'])
        self.assertEqual(result['support_files'], 2)
        self.assertEqual(result['imported_files'], 16)
        self.assertEqual(result['reused_files'], 0)
        self.assertFalse(result['network_used'])
        self.assertEqual((self.root / 'engine-patches/windows-support.json').read_bytes(), self.recipe_bytes)
        self.assertFalse((self.root / self.prefix).exists())
        before = self.tree()
        result = self.run_import()
        self.assertEqual(result['imported_files'], 0)
        self.assertEqual(result['reused_files'], 16)
        self.assertEqual(self.tree(), before)
        # This is the actual fetcher's no-download path, including its notices.
        importer.fetcher.main(self.root)

    def test_trusted_outer_hash_is_mandatory_and_must_match(self):
        self.save()
        for value in ('', 'bad', 'A' * 64, '0' * 64, None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                importer.import_cache(self.root, self.archive, value)
        self.assertFalse((self.root / '.deps').exists())

    def test_archive_symlink_is_rejected(self):
        expected = self.save()
        link = self.folder / 'linked.zip'
        link.symlink_to(self.archive)
        with self.assertRaises(ValueError):
            importer.import_cache(self.root, link, expected)

    def test_inner_inventory_checksum_mismatch_is_rejected(self):
        inventory = {n:hashlib.sha256(data).hexdigest() for n,data in self.contents.items()}
        inventory[self.support + '/SDL3.dll'] = '0' * 64
        self.expect_rejection(inventory=inventory)

    def test_extra_zip_member_outside_inventory_is_rejected(self):
        self.expect_rejection(extra=[('extra.dll', b'unlisted')])

    def test_duplicate_zip_name_is_rejected(self):
        self.expect_rejection(extra=[(self.support + '/SDL3.dll', b'duplicate')])

    def test_case_alias_is_rejected(self):
        self.contents[self.support + '/sdl3.DLL'] = b'alias'
        self.expect_rejection()

    def test_zip_symlink_is_rejected(self):
        info = zipfile.ZipInfo(self.support + '/linked.dll')
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.expect_rejection(extra=[(info, b'/outside')])

    def test_traversal_and_foreign_files_are_rejected(self):
        for name in ('../outside.dll', '/outside.dll', 'C:/outside.dll', '.deps/../outside.dll',
                     self.support + '/extracted/skygame.dll', self.cache + '/surprise.txt', 'saves/file.sav'):
            with self.subTest(name=name):
                self.contents[name] = b'excluded fixture'
                self.expect_rejection()
                del self.contents[name]

    def test_wrong_release_is_rejected(self):
        recipe = dict(self.recipe, archive_size=self.recipe['archive_size'] + 1)
        self.contents['engine-patches/windows-support.json'] = json.dumps(recipe).encode()
        self.expect_rejection()

    def test_support_manifest_hash_and_inventory_must_agree(self):
        self.manifest['entries'][0]['sha256'] = '0' * 64
        self.contents[self.manifest_name] = json.dumps(self.manifest).encode()
        self.expect_rejection()

    def test_extra_allowlisted_file_missing_from_manifest_is_rejected(self):
        self.contents[self.support + '/another.dll'] = b'not in support manifest'
        self.expect_rejection()

    def test_missing_support_member_is_rejected(self):
        del self.contents[self.support + '/SDL3.dll']
        self.expect_rejection()

    def test_missing_notice_is_rejected_before_import(self):
        del self.contents['.deps/windows-licenses/SDL3-LICENSE.txt']
        self.expect_rejection()

    def test_duplicate_json_key_is_rejected(self):
        data = self.contents[self.manifest_name].decode()
        self.contents[self.manifest_name] = ('{"url":"incorrect",' + data[1:]).encode()
        self.expect_rejection()

    def test_mismatched_existing_file_is_preserved_before_other_publication(self):
        target = self.root / self.support / 'SDL3.dll'
        target.parent.mkdir(parents=True)
        target.write_bytes(b'user file')
        self.expect_rejection(FileExistsError)

    def test_mismatched_existing_notice_is_preserved(self):
        target = self.root / '.deps/windows-licenses/SDL3-LICENSE.txt'
        target.parent.mkdir(parents=True)
        target.write_bytes(b'user notice')
        self.expect_rejection(FileExistsError)

    def test_destination_directory_symlink_is_rejected_without_writing_outside(self):
        outside = self.folder / 'outside'
        outside.mkdir()
        (self.root / '.deps').symlink_to(outside, target_is_directory=True)
        self.expect_rejection()
        self.assertFalse(list(outside.iterdir()))

    def test_racing_delivery_is_preserved_and_completion_manifest_is_absent(self):
        link = os.link
        target = self.root / self.support / 'SDL3.dll'
        def racing(source, output):
            if output == target:
                output.write_bytes(b'racing user file')
            link(source, output)
        with mock.patch.object(importer.os, 'link', side_effect=racing), self.assertRaises(FileExistsError):
            self.run_import()
        self.assertEqual(target.read_bytes(), b'racing user file')
        self.assertFalse((self.root / self.manifest_name).exists())

    def test_interrupted_import_retains_verified_files_and_retry_completes(self):
        link = os.link
        calls = 0
        def interrupted(source, output):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('simulated write interruption')
            link(source, output)
        with mock.patch.object(importer.os, 'link', side_effect=interrupted), self.assertRaises(OSError):
            self.run_import()
        self.assertFalse((self.root / self.manifest_name).exists())
        self.assertEqual(len(self.tree()), 2)  # Original recipe plus one verified file.
        result = self.run_import()
        self.assertTrue(result['passed'])
        self.assertEqual(result['reused_files'], 1)

    def test_expansion_limit_is_checked_before_import(self):
        with mock.patch.object(importer, 'MAX_TOTAL', 1):
            self.expect_rejection()


if __name__ == '__main__':
    unittest.main()
