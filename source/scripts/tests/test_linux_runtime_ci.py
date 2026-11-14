"""File contracts and real, never-executed ELF link fixtures for Linux CI."""
import copy
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
from linux_link_interfaces import build_interfaces, inspect_elf, load_interfaces, validate_library

CONFIG = SCRIPTS.parent / 'third_party/linux-link-interfaces-v202.json'

def fixture():
    return dict(soname='libSDL3.so.0', real_library_sha256='a' * 64, symbols=[
        dict(name='SDL_LinkContract', kind='FUNC', binding='GLOBAL', size=20, version='SDL3_0.0.0'),
        dict(name='SDL_LinkData', kind='OBJECT', binding='GLOBAL', size=16, version='SDL3_0.0.0'),
        dict(name='SDL_LinkWeak', kind='FUNC', binding='WEAK', size=8, version='SDL3_0.0.0')])

class MetadataContracts(unittest.TestCase):
    def test_frozen_public_interfaces_load(self):
        config = load_interfaces(CONFIG)
        self.assertEqual(len(config['libraries']), 6)
        self.assertGreater(sum(len(r['symbols']) for r in config['libraries']), 2000)

    def test_duplicate_symbol_has_no_ambiguous_definition(self):
        library = fixture()
        library['symbols'].append(copy.deepcopy(library['symbols'][0]))
        with self.assertRaises(ValueError):
            validate_library(library)

    def test_mixed_versions_are_not_flattened(self):
        library = fixture()
        library['symbols'][0]['version'] = 'SDL3_1.0.0'
        with self.assertRaises(ValueError):
            validate_library(library)

    def test_object_size_cannot_be_missing(self):
        library = fixture()
        del library['symbols'][1]['size']
        with self.assertRaises(ValueError):
            validate_library(library)

    def test_dependency_identity_is_required(self):
        library = fixture()
        library['real_library_sha256'] = ''
        with self.assertRaises(ValueError):
            validate_library(library)

    def test_incomplete_dependency_set_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'interfaces.json'
            path.write_text(json.dumps(dict(format=1, architecture='x86_64-linux-gnu', libraries=[fixture()])))
            with self.assertRaises(ValueError):
                load_interfaces(path)

    def test_wrong_architecture_rejected_before_tool_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'wrong.elf'
            path.write_bytes(b'\x7fELF\x01\x01' + bytes(58))
            with self.assertRaises(ValueError):
                inspect_elf(path)

@unittest.skipUnless(os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('SST_RESOURCE_RUNNER_PID'),
                     'Real ELF compiler fixture runs inside the guarded Linux CI job')
class RealElfContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        with (cls.root / 'link.log').open('w') as log:
            cls.lib = build_interfaces(dict(libraries=[fixture()]), cls.root / 'link-only', log)
            source = cls.root / 'consumer.c'
            source.write_text('extern void SDL_LinkContract(void);\nextern unsigned char SDL_LinkData[16];\n'
                              'int main(void) { SDL_LinkContract(); return SDL_LinkData[0]; }\n')
            cls.binary = cls.root / 'consumer'
            subprocess.run(['clang', '-fuse-ld=lld', str(source), '-L' + str(cls.lib), '-lSDL3', '-o', str(cls.binary)],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        cls.elf = inspect_elf(cls.binary)
        cls.symbols = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(cls.lib / 'libSDL3.so.0')], text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_consumer_requests_real_soname(self):
        self.assertIn('libSDL3.so.0', self.elf['needed'])
        self.assertFalse(self.elf['has_runtime_search_path'])
        self.assertTrue(all('/' not in name for name in self.elf['needed']))

    def test_consumer_preserves_symbol_version(self):
        self.assertIn('SDL_LinkContract@SDL3_0.0.0', self.elf['imports'])

    def test_weak_binding_and_object_size_survive_link(self):
        weak = next(line.split() for line in self.symbols.splitlines() if 'SDL_LinkWeak@@' in line)
        data = next(line.split() for line in self.symbols.splitlines() if 'SDL_LinkData@@' in line)
        self.assertEqual(weak[4], 'WEAK')
        self.assertEqual((data[2], data[3]), ('16', 'OBJECT'))

    def test_interface_is_explicitly_nonexecuting(self):
        text = subprocess.check_output(['readelf', '-x', '.text', str(self.lib / 'libSDL3.so.0')], text=True)
        self.assertIn('cc', text.lower())
        self.assertIn('trap bodies', (self.lib / 'libSDL3.so.0.S').read_text())

if __name__ == '__main__':
    unittest.main()
