import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
from unittest import mock


spec = importlib.util.spec_from_file_location("sst_windows_build", Path(__file__).resolve().parents[1] / "build-windows.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class PackagePreservationTests(unittest.TestCase):
    def test_isolated_artifacts_and_matching_headers_enter_fresh_package(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            def put(name, data=b'public fixture'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);return path
            native=root/'native';toolchain=root/'cross';support=root/'support'
            support.mkdir();put('support/SDL3.dll')
            for name in ('clang.exe','clang-23.exe','ld.lld.exe','libLLVM-23.dll','libclang-cpp.dll',
                         'libc++.dll','libunwind.dll','libwinpthread-1.dll'):
                put('native/bin/'+name)
            for name in ('include/stdio.h','x86_64-w64-mingw32/lib/crt2.o','lib/clang/23/include/stddef.h',
                         'lib/clang/23/lib/windows/libclang_rt.builtins-x86_64.a','LICENSE.TXT'):
                put('native/'+name)
            for name in ('libc++.dll','libunwind.dll','libwinpthread-1.dll'):
                put('cross/x86_64-w64-mingw32/bin/'+name)
            put('cross/LICENSE.TXT')
            for name in ('COPYING','COPYING.MinGW-w64-runtime.txt','COPYING.MinGW-w64.txt','COPYING.winpthreads.txt'):
                put('cross/x86_64-w64-mingw32/share/mingw32/'+name)
            put('.deps/windows-licenses/public.txt')
            for name in ('cpu.h','simd.h','simd_fp.h','gamelib.h','hle/hle_symbols.txt'):
                put('selected-engine/runtime/'+name,b'matching ABI header')
                put('engine/runtime/'+name,b'old ABI header')
            put('selected-engine/tools/gamelib_entry.c',b'matching ABI entry')
            for name in ('sst_online_init.py','sst_online.py','sst_online_transport.py'):
                put('launcher/'+name)
            for name in ('windows-package-readme.txt','windows-testing.md','beedle-purchases.md','windows-third-party.txt'):
                put('docs/'+name)
            for name in ('skyrecomp.exe','skyrecomp.dll.a'):
                put('accepted-runtime/'+name,b'accepted runtime')
                put('engine/build/windows/'+name,b'old runtime')
            for name in ('sst-session-host.exe','sst-network-check.exe'):
                put('accepted-helpers/'+name,b'accepted helper')
                put('build/windows-network/'+name,b'old helper')
            source=root/'accepted-source.zip'
            with zipfile.ZipFile(source,'w') as z:z.writestr('candidate/source.c','accepted source')
            target=root/'candidate.zip'
            with mock.patch.object(builder,'ROOT',root),mock.patch.object(builder,'SUPPORT',support), \
                    mock.patch.object(builder,'NATIVE_TC',native):
                builder.package(toolchain,target,runtime_dir=root/'accepted-runtime',network_dir=root/'accepted-helpers',
                                game_source_root=root/'selected-engine',source_archive=source)
            with zipfile.ZipFile(target) as z:
                prefix='Skyward Sword Together/'
                self.assertEqual(z.read(prefix+'skyrecomp.exe'),b'accepted runtime')
                self.assertEqual(z.read(prefix+'sst-session-host.exe'),b'accepted helper')
                self.assertEqual(z.read(prefix+'gamelib-src/cpu.h'),b'matching ABI header')
                self.assertEqual(z.read(prefix+'co-op-source.zip'),source.read_bytes())
                self.assertFalse(any(name.endswith(('skygame.dll','romfs.bin','save.sav')) for name in z.namelist()))

    def test_existing_delivery_stops_before_fetch_or_build(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "release.zip"
            target.write_bytes(b"saved delivery")
            with mock.patch("sys.argv", ["build-windows.py", "--output", str(target)]), \
                    mock.patch.object(builder.subprocess, "run") as run:
                with self.assertRaises(SystemExit) as error:
                    builder.main()
            self.assertEqual(error.exception.code, 2)
            run.assert_not_called()
            self.assertEqual(target.read_bytes(), b"saved delivery")

    def test_publish_does_not_replace_a_racing_delivery(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "candidate.zip"
            temporary = Path(folder) / "candidate.tmp"
            self.assertEqual(builder.package_target(target), target)
            temporary.write_bytes(b"new candidate")
            target.write_bytes(b"concurrent completed delivery")
            with self.assertRaises(FileExistsError):
                builder.publish_archive(temporary, target)
            self.assertEqual(target.read_bytes(), b"concurrent completed delivery")
            self.assertEqual(temporary.read_bytes(), b"new candidate")

    def test_complete_archive_is_published_to_new_name(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "candidate.zip"
            temporary = Path(folder) / "candidate.tmp"
            temporary.write_bytes(b"complete archive")
            builder.publish_archive(temporary, builder.package_target(target))
            self.assertEqual(target.read_bytes(), b"complete archive")
            self.assertFalse(temporary.exists())

    def test_existing_symlink_and_invalid_extension_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "candidate.zip"
            target.symlink_to(Path(folder) / "absent.zip")
            with self.assertRaises(FileExistsError):
                builder.package_target(target)
            with self.assertRaises(ValueError):
                builder.package_target(Path(folder) / "candidate.txt")


if __name__ == "__main__":
    unittest.main()
