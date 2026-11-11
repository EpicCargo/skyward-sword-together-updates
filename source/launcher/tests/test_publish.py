"""Pure temporary-file release/provenance tests; no binaries execute or publish."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

LAUNCHER=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(LAUNCHER))
import publish
import sst_launcher
spec=importlib.util.spec_from_file_location('sst_release_provenance_tests',LAUNCHER/'release.py')
release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)


class PublishTests(unittest.TestCase):
 def setUp(self):
  self.temporary=tempfile.TemporaryDirectory(prefix='sst-publish-test-');self.addCleanup(self.temporary.cleanup)
  self.root=Path(self.temporary.name);self.out=self.root/'out'
  for name in ['engine/build/skyrecomp','engine/runtime/cpu.h','engine/runtime/simd.h','engine/runtime/simd_fp.h',
               'engine/runtime/gamelib.h','engine/tools/gamelib_entry.c','engine/runtime/hle/hle_symbols.txt',
               'docs/windows-package-readme.txt',*release.ONLINE_VERIFIED]:
   path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('fixture '+name).encode())
  self.binaries={name:sst_launcher.digest(self.root/name) for name in ['engine/build/skyrecomp',*release.ONLINE_VERIFIED]}
  self.windows=self.root/'windows.zip'
  self.write_windows()

 def write_windows(self,online=True,missing=None,alter=None):
  mapping={'skyrecomp.exe':b'test PE fixture'}
  if online:
   mapping['sst-session-host.exe']=b'asset-free Windows helper fixture'
   mapping.update({name:(self.root/source).read_bytes() for name,source in release.ONLINE_COMPANIONS.items()})
  if missing:mapping.pop(missing,None)
  if alter:mapping[alter]=b'different packaged source'
  with zipfile.ZipFile(self.windows,'w') as archive:
   for name,data in mapping.items():archive.writestr('Skyward Sword Together/'+name,data)

 def test_linux_online_files_are_required_and_manifested(self):
  entry,manifest=publish.linux_release(self.root/'engine/build/skyrecomp',self.out,'test',self.root)
  self.assertEqual(entry['kind'],'full')
  required={'sst-session-host',*release.ONLINE_COMPANIONS}
  self.assertTrue(required<=manifest['files'].keys())
  with zipfile.ZipFile(self.out/entry['url']) as archive:
   self.assertTrue(required<=set(archive.namelist()))
   self.assertEqual(archive.read('sst-session-host'),(self.root/'client/build/sst-session-host').read_bytes())

 def test_linux_missing_or_symlinked_online_input_never_creates_archive(self):
  for index,name in enumerate(release.ONLINE_VERIFIED[:-1]):
   path=self.root/name;data=path.read_bytes();path.unlink()
   with self.assertRaises(sst_launcher.UpdateError):
    publish.linux_release(self.root/'engine/build/skyrecomp',self.out,'missing'+str(index),self.root)
   target=self.root/('private-input-'+str(index));target.write_bytes(data);path.symlink_to(target)
   with self.assertRaises(sst_launcher.UpdateError):
    publish.linux_release(self.root/'engine/build/skyrecomp',self.out,'linked'+str(index),self.root)
   path.unlink();path.write_bytes(data)
  self.assertFalse(self.out.exists())

 def test_existing_published_zip_and_manifest_are_untouched(self):
  self.out.mkdir();archive=self.out/'SST-runtime-linux-x86_64-test.zip';archive.write_bytes(b'published archive')
  with self.assertRaises(sst_launcher.UpdateError):
   publish.linux_release(self.root/'engine/build/skyrecomp',self.out,'test',self.root)
  self.assertEqual(archive.read_bytes(),b'published archive')
  archive.unlink();manifest=self.out/'sst-manifest-linux-x86_64-test.json';manifest.write_bytes(b'published manifest')
  with self.assertRaises(sst_launcher.UpdateError):
   publish.linux_release(self.root/'engine/build/skyrecomp',self.out,'test',self.root)
  self.assertEqual(manifest.read_bytes(),b'published manifest');self.assertFalse(archive.exists())

 def test_complete_verified_online_inputs_pass(self):
  self.assertTrue(release.verify_online_inputs(self.windows,self.binaries,self.root))

 def test_windows_existing_manifest_and_dangling_outputs_are_preserved(self):
  self.out.mkdir()
  manifest=self.out/'sst-manifest-windows-x86_64-test.json'
  manifest.write_bytes(b'previous published manifest')
  with self.assertRaises(sst_launcher.UpdateError):
   publish.windows_release(self.windows,self.out,'test')
  self.assertEqual(manifest.read_bytes(),b'previous published manifest')
  archive=self.out/'SST-runtime-windows-x86_64-test.zip'
  self.assertFalse(archive.exists())
  manifest.unlink()
  for target in [archive,manifest]:
   target.symlink_to(self.root/'missing-output')
   with self.assertRaises(sst_launcher.UpdateError):
    publish.windows_release(self.windows,self.out,'test')
   self.assertTrue(target.is_symlink())
   self.assertFalse((self.root/'missing-output').exists())
   target.unlink()

 def test_windows_duplicate_members_never_produce_a_release(self):
  import warnings
  with warnings.catch_warnings():
   warnings.simplefilter('ignore',UserWarning)
   with zipfile.ZipFile(self.windows,'w') as archive:
    archive.writestr('Skyward Sword Together/skyrecomp.exe',b'first')
    archive.writestr('Skyward Sword Together/skyrecomp.exe',b'second')
  with self.assertRaises(sst_launcher.UpdateError):
   publish.windows_release(self.windows,self.out,'test')
  self.assertFalse((self.out/'SST-runtime-windows-x86_64-test.zip').exists())
  self.assertFalse((self.out/'sst-manifest-windows-x86_64-test.json').exists())

 def test_each_changed_unrecorded_or_symlinked_input_blocks_online_release(self):
  for name in release.ONLINE_VERIFIED:
   with self.subTest(name=name):
    missing=dict(self.binaries);missing.pop(name)
    with self.assertRaises(sst_launcher.UpdateError):release.verify_online_inputs(self.windows,missing,self.root)
    path=self.root/name;data=path.read_bytes();path.write_bytes(data+b' changed')
    with self.assertRaises(sst_launcher.UpdateError):release.verify_online_inputs(self.windows,self.binaries,self.root)
    path.unlink();target=self.root/'private-input';target.write_bytes(data);path.symlink_to(target)
    with self.assertRaises(sst_launcher.UpdateError):release.verify_online_inputs(self.windows,self.binaries,self.root)
    path.unlink();path.write_bytes(data)

 def test_incomplete_or_mismatched_windows_companions_fail(self):
  for missing in ['sst-session-host.exe',*release.ONLINE_COMPANIONS]:
   self.write_windows(missing=missing)
   with self.assertRaises(sst_launcher.UpdateError):release.verify_online_inputs(self.windows,self.binaries,self.root)
  self.write_windows(alter='online/sst_online_transport.py')
  with self.assertRaises(sst_launcher.UpdateError):release.verify_online_inputs(self.windows,self.binaries,self.root)

 def test_legacy_package_does_not_require_new_online_evidence(self):
  self.write_windows(online=False)
  self.assertFalse(release.verify_online_inputs(self.windows,{},self.root))

 def test_provenance_failure_precedes_feed_build_or_publish_calls(self):
  evidence=self.root/'verification';evidence.mkdir()
  (evidence/'finalized.json').write_text(json.dumps({'complete':True,'protocol':45}))
  (evidence/'package.json').write_text(json.dumps({'privacy_checked':True,'path':str(self.windows),'sha256':sst_launcher.digest(self.windows)}))
  bad=dict(self.binaries);bad.pop('launcher/sst_launcher.py')
  (evidence/'tested-binaries.json').write_text(json.dumps(bad))
  argv=['release.py','--verification-dir',str(evidence),'--id','test','--sequence','1','--out',str(self.out),'--publish']
  with mock.patch.object(release,'ROOT',self.root),mock.patch.object(sys,'argv',argv), \
       mock.patch.object(release,'fetch_json') as fetch,mock.patch.object(release.subprocess,'run') as run, \
       mock.patch.object(release,'github_publish') as push:
   with self.assertRaises(sst_launcher.UpdateError):release.main()
   fetch.assert_not_called();run.assert_not_called();push.assert_not_called()

 def test_legacy_main_still_prepares_with_only_legacy_evidence(self):
  self.write_windows(online=False);evidence=self.root/'verification';evidence.mkdir()
  (evidence/'finalized.json').write_text(json.dumps({'complete':True,'protocol':45}))
  (evidence/'package.json').write_text(json.dumps({'privacy_checked':True,'path':str(self.windows),'sha256':sst_launcher.digest(self.windows)}))
  (evidence/'tested-binaries.json').write_text(json.dumps({'engine/build/skyrecomp':self.binaries['engine/build/skyrecomp']}))
  previous=self.root/'previous.json';previous.write_text('{}')
  argv=['release.py','--verification-dir',str(evidence),'--id','test','--sequence','1','--out',str(self.out),'--previous',str(previous)]
  with mock.patch.object(release,'ROOT',self.root),mock.patch.object(sys,'argv',argv), \
       mock.patch.object(release,'fetch_json') as fetch,mock.patch.object(release.subprocess,'run') as run, \
       mock.patch.object(release,'github_publish') as push:
   release.main();run.assert_called_once();fetch.assert_not_called();push.assert_not_called()


if __name__=='__main__':unittest.main()
