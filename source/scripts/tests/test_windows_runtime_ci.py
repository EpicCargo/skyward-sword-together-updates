"""Cloud build input/download/extraction checks; no compiler or Windows process."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
from unittest import mock

spec=importlib.util.spec_from_file_location('windows_runtime_ci',Path(__file__).resolve().parents[1]/'build-windows-runtime-ci.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)


def sdk_fixture(root):
    names=['include/SDL3/SDL.h','include/libavcodec/avcodec.h','include/libavformat/avformat.h',
           'include/libavutil/avutil.h','include/libswscale/swscale.h','include/shaderc/shaderc.h',
           'vulkan/include/vulkan/vulkan.h','vma/include/vk_mem_alloc.h']
    names += ['definitions/'+name+'.def' for name in builder.LIBRARIES]
    inventory={}
    for name in names:
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'public source fixture\n')
        inventory[name]=builder.digest(path)
    (root/'FILES-SHA256.json').write_text(json.dumps(inventory))
    return inventory


def tar_fixture(path,entries):
    with tarfile.open(path,'w:xz',preset=0) as saved:
        for name,data,kind in entries:
            info=tarfile.TarInfo(name);info.mode=0o755
            if kind=='symlink':info.type=tarfile.SYMTYPE;info.linkname=data;saved.addfile(info)
            else:info.size=len(data);saved.addfile(info,io.BytesIO(data))


class CloudRuntimeInputTests(unittest.TestCase):
    def test_real_frozen_sdk_is_complete_text_source(self):
        files=builder.verify_sdk()
        self.assertGreater(len(files),250)
        self.assertTrue(all(Path(name).suffix not in ('.dll','.exe','.a') for name in files))

    def test_sdk_corruption_and_extra_binary_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);sdk_fixture(root);builder.verify_sdk(root)
            (root/'include/SDL3/SDL.h').write_bytes(b'changed')
            with self.assertRaises(ValueError):builder.verify_sdk(root)
            sdk_fixture(root);(root/'unexpected.dll').write_bytes(b'fixture')
            with self.assertRaises(ValueError):builder.verify_sdk(root)

    def test_sdk_traversal_duplicate_keys_and_symlinks_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'sdk';root.mkdir();inventory=sdk_fixture(root)
            manifest=root/'FILES-SHA256.json';manifest.write_text(json.dumps(dict(inventory,**{'../outside.h':'0'*64})))
            with self.assertRaises(ValueError):builder.verify_sdk(root)
            manifest.write_text('{"include/SDL3/SDL.h":"'+inventory['include/SDL3/SDL.h']+'",'+json.dumps(inventory)[1:])
            with self.assertRaises(ValueError):builder.verify_sdk(root)
            manifest.write_text(json.dumps(inventory));path=root/'include/SDL3/SDL.h';path.unlink()
            outside=Path(folder)/'outside.h';outside.write_bytes(b'public source fixture\n');path.symlink_to(outside)
            with self.assertRaises(ValueError):builder.verify_sdk(root)
            self.assertEqual(outside.read_bytes(),b'public source fixture\n')

    def test_download_verifies_hash_size_and_reuses_exact_cache_without_http(self):
        content=b'pinned compiler fixture';checksum=hashlib.sha256(content).hexdigest()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'compiler.tar.xz'
            with mock.patch.object(builder.urllib.request,'urlopen',return_value=io.BytesIO(content)):
                builder.download('https://example.invalid/toolchain',path,checksum,len(content))
            with mock.patch.object(builder.urllib.request,'urlopen',side_effect=AssertionError('Unexpected HTTP')):
                builder.download('https://example.invalid/toolchain',path,checksum,len(content))
            self.assertEqual(path.read_bytes(),content)

    def test_bad_or_oversized_download_is_not_published(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'compiler.tar.xz'
            for limit in (1,3):
                with mock.patch.object(builder.urllib.request,'urlopen',return_value=io.BytesIO(b'bad')):
                    with self.assertRaises(ValueError):builder.download('https://example.invalid',path,'0'*64,limit)
                self.assertFalse(path.exists());self.assertFalse(path.with_name(path.name+'.part').exists())

    def test_existing_download_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'compiler.tar.xz';path.write_bytes(b'user artifact')
            with self.assertRaises(FileExistsError):builder.download('https://example.invalid',path,'0'*64,10)
            self.assertEqual(path.read_bytes(),b'user artifact')

    def test_existing_partial_download_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'compiler.tar.xz';partial=path.with_name(path.name+'.part');partial.write_bytes(b'prior partial')
            with self.assertRaises(FileExistsError):builder.download('https://example.invalid',path,'0'*64,10)
            self.assertEqual(partial.read_bytes(),b'prior partial')

    def test_good_compiler_archive_preserves_internal_link_and_executable_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);archive=root/'toolchain.tar.xz';prefix=builder.TOOLCHAIN
            tar_fixture(archive,[(prefix+'/bin/clang-real',b'fixture tool','file'),(prefix+'/bin/clang','clang-real','symlink')])
            tool=builder.extract_toolchain(archive,root/'tools')
            self.assertEqual((tool/'bin/clang').read_bytes(),b'fixture tool')
            self.assertTrue((tool/'bin/clang-real').stat().st_mode&0o111)

    def test_compiler_archive_rejects_traversal_and_external_links(self):
        for name,data,kind in [('../outside',b'bad','file'),(builder.TOOLCHAIN+'/bin/link','../../../outside','symlink')]:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);archive=root/'toolchain.tar.xz';tar_fixture(archive,[(name,data,kind)])
                with self.assertRaises((ValueError,tarfile.FilterError)):builder.extract_toolchain(archive,root/'tools')
                self.assertFalse((root/'outside').exists())

    def test_compiler_archive_rejects_wrong_root_and_duplicate_members(self):
        for entries in [[('wrong/bin/tool',b'fixture','file')],[(builder.TOOLCHAIN+'/bin/tool',b'a','file'),(builder.TOOLCHAIN+'/bin/tool',b'b','file')]]:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);archive=root/'toolchain.tar.xz';tar_fixture(archive,entries)
                with self.assertRaises(ValueError):builder.extract_toolchain(archive,root/'tools')

    def test_worker_refuses_local_or_unguarded_execution_before_network(self):
        for env in ({},{'GITHUB_ACTIONS':'true'},{'SST_RESOURCE_RUNNER_PID':'1'}):
            with mock.patch.dict(builder.os.environ,env,clear=True),mock.patch.object(builder.urllib.request,'urlopen',side_effect=AssertionError('Unexpected HTTP')):
                with self.assertRaisesRegex(RuntimeError,'CI worker only'):builder.build()

    def test_pe_inspection_rejects_wrong_machine_and_truncated_input(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'fixture.exe';data=bytearray(64+88);data[:2]=b'MZ';struct.pack_into('<I',data,60,64)
            data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664);struct.pack_into('<H',data,88,0x20b)
            struct.pack_into('<Q',data,112,0x140000000);struct.pack_into('<I',data,144,0x100000)
            path.write_bytes(data);self.assertEqual(builder.inspect_pe(path)['image_base'],0x140000000)
            struct.pack_into('<H',data,68,0x14c);path.write_bytes(data)
            with self.assertRaises(ValueError):builder.inspect_pe(path)
            path.write_bytes(b'MZ')
            with self.assertRaises(ValueError):builder.inspect_pe(path)


if __name__=='__main__':unittest.main()
