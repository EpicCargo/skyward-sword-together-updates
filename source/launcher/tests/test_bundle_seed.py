"""Portable seeding must verify a complete runtime before activating it."""
import hashlib
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_bundle
import sst_launcher


class BundleSeedTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='sst-seed-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.app = self.root / 'app'
        self.app.mkdir()
        self.archive = self.root / 'runtime.zip'
        self.payload = {'skyrecomp.exe': b'public runtime fixture', 'online/helper.py': b'public helper'}
        self.prefix = 'Skyward Sword Together/'
        self.manifest = dict(schema=1, app=sst_launcher.APP_ID, release_id='seed-test',
                             platform='windows-x86_64', entrypoint='skyrecomp.exe',
                             files={name: dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())
                                    for name, data in self.payload.items()})
        self.feed = dict(release=dict(id='seed-test', sequence=1, protocol=45,
                                     platforms={'windows-x86_64': dict(prefix=self.prefix)}))

    def write(self, entries=None):
        entries = entries if entries is not None else [(self.prefix + n, d) for n, d in self.payload.items()]
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(self.archive, 'w') as archive:
                for name, data in entries:
                    archive.writestr(name, data)

    def seed(self):
        build_bundle.seed_version(self.app, self.feed, self.manifest, self.archive)

    def assert_not_activated(self):
        self.assertFalse((self.app / 'launcher-state.json').exists())
        self.assertFalse((self.app / 'versions/seed-test').exists())
        versions = self.app / 'versions'
        self.assertEqual(list(versions.iterdir()) if versions.exists() else [], [])

    def test_missing_runtime_member_never_activates_or_leaves_a_version(self):
        self.write([(self.prefix + 'online/helper.py', self.payload['online/helper.py'])])
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()

    def test_empty_archive_never_activates(self):
        self.write([])
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()

    def test_duplicate_member_is_rejected_even_when_bytes_match(self):
        entries = [(self.prefix + n, d) for n, d in self.payload.items()]
        self.write(entries + [entries[0]])
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()

    def test_unexpected_root_is_rejected_before_any_file_is_installed(self):
        self.write([('Wrong root/' + n, d) for n, d in self.payload.items()])
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()

    def test_bad_bytes_leave_no_partial_version_and_allow_a_clean_retry(self):
        entries = [(self.prefix + n, d) for n, d in self.payload.items()]
        entries[-1] = (entries[-1][0], b'x' * len(entries[-1][1]))
        self.write(entries)
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()
        self.write()
        self.seed()
        self.assertEqual((self.app / 'versions/seed-test/skyrecomp.exe').read_bytes(), self.payload['skyrecomp.exe'])

    def test_existing_version_and_launcher_state_are_preserved(self):
        target = self.app / 'versions/seed-test'
        target.mkdir(parents=True)
        (target / 'keep').write_bytes(b'previous version')
        state = self.app / 'launcher-state.json'
        state.write_bytes(b'previous state')
        self.write()
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assertEqual((target / 'keep').read_bytes(), b'previous version')
        self.assertEqual(state.read_bytes(), b'previous state')

    def test_mismatched_release_id_and_size_are_rejected(self):
        self.write()
        self.manifest['release_id'] = 'different-release'
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()
        self.manifest['release_id'] = 'seed-test'
        self.manifest['files']['skyrecomp.exe']['size'] += 1
        with self.assertRaises(sst_launcher.UpdateError):
            self.seed()
        self.assert_not_activated()

    def test_complete_runtime_is_verified_and_activated(self):
        self.write()
        self.seed()
        folder = self.app / 'versions/seed-test'
        for name, data in self.payload.items():
            self.assertEqual((folder / name).read_bytes(), data)
        state = json.loads((self.app / 'launcher-state.json').read_text())
        self.assertEqual(state['active']['id'], 'seed-test')
        self.assertEqual(json.loads((folder / 'installed-manifest.json').read_text()), self.manifest)

    @unittest.skipIf(os.name == 'nt', 'POSIX runtime modes')
    def test_linux_entrypoint_and_helper_keep_executable_permissions(self):
        self.payload = {'skyrecomp': b'runtime', 'sst-session-host': b'host', 'online/helper.py': b'helper'}
        self.prefix = ''
        self.manifest.update(platform='linux-x86_64', entrypoint='skyrecomp', files={
            n: dict(size=len(d), sha256=hashlib.sha256(d).hexdigest()) for n, d in self.payload.items()})
        self.feed['release']['platforms'] = {'linux-x86_64': dict(prefix='')}
        self.write()
        self.seed()
        folder = self.app / 'versions/seed-test'
        self.assertEqual((folder / 'skyrecomp').stat().st_mode & 0o777, 0o755)
        self.assertEqual((folder / 'sst-session-host').stat().st_mode & 0o777, 0o755)
        self.assertEqual((folder / 'online/helper.py').stat().st_mode & 0o777, 0o644)


class PortableOutputTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='sst-portable-output-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.stage = self.root / 'stage'
        self.stage.mkdir()
        (self.stage / 'README.txt').write_bytes(b'public portable fixture')
        self.target = self.root / 'portable.zip'

    def test_existing_portable_is_preserved(self):
        self.target.write_bytes(b'previous portable')
        with self.assertRaises(sst_launcher.UpdateError):
            build_bundle.zip_stage(self.stage, self.target)
        self.assertEqual(self.target.read_bytes(), b'previous portable')

    def test_dangling_output_link_is_preserved_without_creating_its_target(self):
        other = self.root / 'missing-target'
        self.target.symlink_to(other)
        with self.assertRaises(sst_launcher.UpdateError):
            build_bundle.zip_stage(self.stage, self.target)
        self.assertTrue(self.target.is_symlink())
        self.assertFalse(other.exists())

    def test_optimized_python_also_preserves_an_existing_portable(self):
        self.target.write_bytes(b'previous portable')
        code = (
            "import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);"
            "import build_bundle,sst_launcher\n"
            "try:build_bundle.zip_stage(Path(sys.argv[2]),Path(sys.argv[3]))\n"
            "except sst_launcher.UpdateError:sys.exit(0)\n"
            "sys.exit(3)\n"
        )
        result = subprocess.run([sys.executable, '-O', '-c', code,
                                 str(Path(build_bundle.__file__).parent), str(self.stage), str(self.target)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.target.read_bytes(), b'previous portable')

    def test_fresh_portable_retains_its_expected_layout(self):
        build_bundle.zip_stage(self.stage, self.target)
        with zipfile.ZipFile(self.target) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.namelist(), ['SST/README.txt'])
            self.assertEqual(archive.read('SST/README.txt'), b'public portable fixture')


if __name__ == '__main__':
    unittest.main()
