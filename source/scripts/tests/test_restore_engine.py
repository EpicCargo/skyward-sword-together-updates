"""Real local Git restoration; no network, compiler or game files."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sst_restore_engine_tests', SCRIPTS/'restore-engine.py')
restore = importlib.util.module_from_spec(spec)
spec.loader.exec_module(restore)


class EngineRestorationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='sst-engine-restoration-')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.root = self.work/'snapshot'; self.root.mkdir()
        self.repo = self.work/'preserved-base'; self.repo.mkdir()
        self.git(self.repo, 'init', '--quiet')
        (self.repo/'runtime').mkdir()
        (self.repo/'runtime/test.c').write_text('/* pinned base */\n')
        self.git(self.repo, 'add', '.')
        self.git(self.repo, 'commit', '--quiet', '-m', 'Synthetic public base')
        base = self.git(self.repo, 'rev-parse', 'HEAD')
        (self.repo/'runtime/test.c').write_text('/* patched runtime */\n')
        (self.repo/'runtime/sst').symlink_to('../../client/src')
        self.git(self.repo, 'add', '.')
        self.git(self.repo, 'commit', '--quiet', '-m', 'Synthetic client integration')
        (self.root/'engine-patches').mkdir()
        (self.root/'engine-patches/0001-client.patch').write_text(self.git(self.repo, 'format-patch', '--stdout', '-1')+'\n')
        (self.root/'engine-patches/base.txt').write_text(base+'\n')
        (self.root/'client/src').mkdir(parents=True)
        (self.root/'client/src/client.c').write_text('/* synthetic client */\n')
        (self.root/'scripts').mkdir()
        shutil.copyfile(SCRIPTS/'verify-source.py', self.root/'scripts/verify-source.py')
        (self.root/'validation').mkdir()
        self.state = dict(base_commit=base, engine_tree=self.git(self.repo, 'rev-parse', 'HEAD^{tree}'),
            engine_patches=1, native_file_sha256={
                'engine/runtime/test.c': hashlib.sha256((self.repo/'runtime/test.c').read_bytes()).hexdigest(),
                'client/src/client.c': hashlib.sha256((self.root/'client/src/client.c').read_bytes()).hexdigest()})
        self.freeze()
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(restore, 'ROOT', self.root).start()

    def git(self, folder, *args):
        return subprocess.check_output(['git','-C',str(folder),'-c','core.hooksPath=/dev/null',
            '-c','commit.gpgsign=false','-c','user.name=Synthetic fixture',
            '-c','user.email=fixture@localhost',*args],text=True).strip()

    def freeze(self):
        (self.root/'validation/native-source.json').write_text(json.dumps(self.state))
        inventory = {str(p.relative_to(self.root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in self.root.rglob('*') if p.is_file() and p.name!='SOURCE-SHA256.json'}
        (self.root/'SOURCE-SHA256.json').write_text(json.dumps(inventory))

    def test_unavailable_primary_falls_back_and_restores_exact_tree(self):
        restore.restore([self.work/'unavailable-primary', self.repo])
        self.assertEqual(self.git(self.root/'engine', 'rev-parse', 'HEAD^{tree}'), self.state['engine_tree'])
        self.assertEqual((self.root/'engine/runtime/sst').resolve(), self.root/'client/src')
        self.assertFalse(list((self.root/'build').glob('engine-restore-*')))

    def test_wrong_base_source_does_not_substitute_a_different_commit(self):
        other = self.work/'other'; other.mkdir()
        self.git(other,'init','--quiet')
        self.git(other,'commit','--quiet','--allow-empty','-m','Unrelated base')
        restore.restore([other,self.repo])
        self.assertEqual(self.git(self.root/'engine','rev-parse','HEAD^{tree}'),self.state['engine_tree'])

    def test_available_first_source_never_contacts_an_alternative(self):
        with mock.patch.object(restore.subprocess,'run',wraps=subprocess.run) as run:
            restore.restore([self.repo,self.work/'must-not-contact'])
        clones=[c for c in run.call_args_list if 'clone' in c.args[0]]
        self.assertEqual(len(clones),1)

    def test_all_sources_unavailable_leaves_no_partial_engine(self):
        with self.assertRaisesRegex(RuntimeError,'No configured engine source'):
            restore.restore([self.work/'missing-one',self.work/'missing-two'])
        self.assertFalse((self.root/'engine').exists())
        self.assertFalse(list((self.root/'build').glob('engine-restore-*')))

    def test_existing_engine_is_preserved(self):
        (self.root/'engine').mkdir(); marker=self.root/'engine/preserve.txt';marker.write_text('existing work')
        # Output is not part of the snapshot inventory.
        with self.assertRaisesRegex(SystemExit,'Preserve the existing engine'):
            restore.restore([self.repo])
        self.assertEqual(marker.read_text(),'existing work')

    def test_tree_mismatch_is_rejected_without_trying_another_source(self):
        self.state['engine_tree']='0'*40;self.freeze()
        with mock.patch.object(restore.subprocess,'run',wraps=subprocess.run) as run:
            with self.assertRaisesRegex(RuntimeError,'Imported engine tree mismatch'):
                restore.restore([self.repo,self.repo])
        self.assertEqual(sum('clone' in c.args[0] for c in run.call_args_list),1)
        self.assertFalse((self.root/'engine').exists())

    def test_native_hash_mismatch_is_rejected_before_activating_engine(self):
        self.state['native_file_sha256']['engine/runtime/test.c']='0'*64;self.freeze()
        with self.assertRaisesRegex(RuntimeError,'Native source checksum mismatch'):
            restore.restore([self.repo])
        self.assertFalse((self.root/'engine').exists())


if __name__ == '__main__':
    unittest.main()
