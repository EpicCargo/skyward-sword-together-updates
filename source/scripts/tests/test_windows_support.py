"""Offline safety and preservation checks for selected-member support fetching."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import urllib.error
import zipfile
import zlib


spec = importlib.util.spec_from_file_location(
    "sst_windows_support", Path(__file__).resolve().parents[1] / "fetch-windows-support.py")
fetcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetcher)


def configuration(root, size=1000):
    return dict(root=root, cache=root / ".deps/new-cache", support=root / ".deps/new-cache/support",
                prefix=root / ".deps/new-prefix", url=fetcher.URL, archive_size=size, asset_digest=None)


def fixture_archive():
    stream = io.BytesIO()
    contents = {
        "SDL3.dll": b"public SDL fixture",
        "avutil-61.dll": b"public AV fixture",
        "extracted/skygame.dll": b"excluded placeholder",
        "skygen/skygen.exe": b"public helper fixture",
        "textures/example.dds": b"excluded texture placeholder",
        "lasgen/_internal/python.dll": b"public Python fixture",
    }
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in contents.items():
            archive.writestr(name, data)
    return stream.getvalue(), contents


def directory(blob, config):
    with mock.patch.object(fetcher, "request", side_effect=lambda start, end, cfg: blob[start:end + 1]):
        return fetcher.members(config)


class ConfigSelectionTests(unittest.TestCase):
    def test_defaults_and_selected_release_keep_old_cache_separate(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            patches = root / "engine-patches"
            patches.mkdir()
            legacy = fetcher.load_config(root)
            self.assertEqual(legacy["cache"], root / ".deps/windows-upstream")
            self.assertEqual(legacy["archive_size"], 2075313323)
            selected = patches / "upstream202"
            selected.mkdir()
            (patches / "current").symlink_to("upstream202", target_is_directory=True)
            values = dict(cache=".deps/windows-upstream-v202", support=".deps/windows-upstream-v202/support",
                          prefix=".deps/windows-runtime-v202",
                          url=fetcher.URL.replace("v1.0.6", "v2.0.2"), archive_size=1881203900,
                          asset_digest="sha256:" + "a" * 64)
            (selected / "windows-support.json").write_text(json.dumps(values))
            active = fetcher.load_config(root)
            self.assertEqual(active["support"], root / values["support"])
            self.assertEqual(active["prefix"], root / values["prefix"])
            self.assertEqual(active["cache"], root / values["cache"])
            self.assertEqual(active["archive_size"], 1881203900)
            self.assertNotEqual(active["cache"], legacy["cache"])

    def test_configuration_cannot_escape_project_or_use_unrelated_downloads(self):
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
            root = Path(folder)
            patches = root / "engine-patches"
            patches.mkdir()
            path = patches / "windows-support.json"
            (root / "escape").symlink_to(outside, target_is_directory=True)
            for values in ({"cache": "../escape"}, {"cache": "/tmp/escape"},
                           {"cache": "C:escape"}, {"cache": "C:\\escape"},
                           {"cache": "escape/cache"}, {"support": "../escape"},
                           {"prefix": "../escape"}, {"url": "https://example.com/release.zip"},
                           {"archive_size": True}, {"asset_digest": "sha256:wrong"}):
                with self.subTest(values=values):
                    path.write_text(json.dumps(values))
                    with self.assertRaises(ValueError):
                        fetcher.load_config(root)

    def test_configuration_symlink_cannot_read_an_outside_file(self):
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
            root = Path(folder)
            (root / "engine-patches").mkdir()
            external = Path(outside) / "config.json"
            external.write_text("{}")
            (root / "engine-patches/windows-support.json").symlink_to(external)
            with self.assertRaises(ValueError):
                fetcher.load_config(root)


class ArchiveSafetyTests(unittest.TestCase):
    def test_adjacent_support_members_split_before_range_limit(self):
        stream = io.BytesIO()
        contents = {f"library{i}.dll": bytes([i]) * 24 for i in range(4)}
        with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as archive:
            for name, data in contents.items():
                archive.writestr(name, data)
        blob = stream.getvalue()
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder), len(blob))
            entries = directory(blob, config)
            # Real request() enforces its range cap. Mock only the HTTP boundary;
            # splitting must still deliver complete files with verified CRC/length.
            def response(request, timeout):
                span = request.get_header('Range').removeprefix('bytes=')
                start, end = map(int, span.split('-'))
                result = mock.MagicMock()
                result.status = 206
                result.headers = {'Content-Range': f'bytes {start}-{end}/{len(blob)}',
                                  'Content-Length': str(end-start+1)}
                result.read.side_effect = io.BytesIO(blob[start:end+1]).read
                result.__enter__.return_value = result
                return result
            with mock.patch.object(fetcher, 'MAX_MEMBER', 128), \
                    mock.patch.object(fetcher.urllib.request, 'urlopen', side_effect=response) as network:
                for start, end, group in fetcher.groups(entries):
                    body = fetcher.request(start, end, config)
                    for entry in group:
                        fetcher.fetch(entry, io.BytesIO(body), start, config)
                self.assertGreater(network.call_count, 1)
            self.assertEqual({name: (config['support']/name).read_bytes() for name in contents}, contents)

    def test_new_and_legacy_helpers_are_allowed_but_game_material_is_excluded(self):
        for name in ("SDL3.dll", "LICENSE", "icon.png", "skygen/skygen.exe", "lasgen/_internal/python.dll"):
            self.assertTrue(fetcher.wanted(name), name)
        for name in ("skygame.dll", "skygame.so.id", "extracted/skygame.dll", "prod.keys",
                     "textures/test.dds", "skygen/textures/test.dds", "skygen/prod.keys",
                     "skygen/skygame.dll", "skygen/romfs/asset", "reshade-shaders/texture.dds"):
            self.assertFalse(fetcher.wanted(name), name)
        for name in ("../SDL3.dll", "/SDL3.dll", "C:/SDL3.dll", "skygen/../../file",
                     "skygen\\python.dll", "./SDL3.dll"):
            with self.assertRaises(ValueError, msg=name):
                fetcher.wanted(name)

    def test_exact_ranges_never_cross_an_excluded_member(self):
        blob, contents = fixture_archive()
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder), len(blob))
            requests = []
            def local_range(start, end, cfg):
                requests.append((start, end))
                return blob[start:end + 1]
            with mock.patch.object(fetcher, "request", side_effect=local_range):
                entries = fetcher.members(config)
            footer = struct.unpack("<4s4H2LH", blob[-22:])
            self.assertEqual(requests, [(len(blob) - 22, len(blob) - 1),
                                       (footer[6], footer[6] + footer[5] - 1)])
            self.assertEqual({e["name"] for e in entries},
                             {"SDL3.dll", "avutil-61.dll", "skygen/skygen.exe", "lasgen/_internal/python.dll"})
            runs = fetcher.groups(entries)
            self.assertEqual(len(runs), 3)  # adjacent DLLs share a range; assets split the runs
            with zipfile.ZipFile(io.BytesIO(blob)) as archive:
                infos = archive.infolist()
                for index, info in enumerate(infos):
                    if info.filename in {e["name"] for e in entries}:
                        continue
                    excluded_end = infos[index + 1].header_offset - 1 if index + 1 < len(infos) else footer[6] - 1
                    for start, end, _ in runs:
                        self.assertTrue(end < info.header_offset or start > excluded_end)
            for start, end, group in runs:
                stream = io.BytesIO(blob[start:end + 1])
                for entry in group:
                    fetcher.fetch(entry, stream, start, config)
                    self.assertEqual((config["support"] / entry["name"]).read_bytes(), contents[entry["name"]])
            self.assertFalse((config["support"] / "extracted").exists())
            self.assertFalse((config["support"] / "textures").exists())

    def test_legacy_wrapper_does_not_hide_excluded_game_library(self):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            archive.writestr("SkyRecomp-v1.0.6/SDL3.dll", b"public")
            archive.writestr("SkyRecomp-v1.0.6/extracted/skygame.dll", b"excluded")
        blob = stream.getvalue()
        with tempfile.TemporaryDirectory() as folder:
            entries = directory(blob, configuration(Path(folder), len(blob)))
            self.assertEqual([entry["name"] for entry in entries], ["SDL3.dll"])
            self.assertEqual(entries[0]["archive_name"], "SkyRecomp-v1.0.6/SDL3.dll")

    def test_corrupt_download_is_not_published_and_existing_file_is_preserved(self):
        blob, _ = fixture_archive()
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder), len(blob))
            entry = directory(blob, config)[0]
            config["support"].mkdir(parents=True)
            preserved = config["support"] / "another.dll"
            preserved.write_bytes(b"existing delivery")
            damaged = bytearray(blob)
            header = struct.unpack_from("<4s5H3L2H", blob, entry["offset"])
            payload = entry["offset"] + 30 + header[9] + header[10]
            damaged[payload] ^= 1
            with self.assertRaisesRegex(RuntimeError, "checksum"):
                fetcher.fetch(entry, io.BytesIO(damaged), 0, config)
            self.assertFalse((config["support"] / entry["name"]).exists())
            self.assertFalse(list(config["support"].rglob("*.part")))
            self.assertEqual(preserved.read_bytes(), b"existing delivery")
            target = config["support"] / entry["name"]
            target.write_bytes(b"prior delivery")
            with self.assertRaises(FileExistsError):
                fetcher.fetch(entry, io.BytesIO(blob), 0, config)
            self.assertEqual(target.read_bytes(), b"prior delivery")

    def test_symlink_in_member_output_is_rejected(self):
        blob, _ = fixture_archive()
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
            config = configuration(Path(folder), len(blob))
            entry = next(e for e in directory(blob, config) if e["name"].startswith("skygen/"))
            config["support"].mkdir(parents=True)
            (config["support"] / "skygen").symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                fetcher.fetch(entry, io.BytesIO(blob), 0, config)
            self.assertFalse(list(Path(outside).iterdir()))

    def test_deflated_support_and_truncated_stream_have_distinct_outcomes(self):
        stream = io.BytesIO()
        contents = bytes(range(256)) * 4096
        with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("skygen/_internal/python.dll", contents)
        blob = stream.getvalue()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            complete = configuration(root / "complete", len(blob))
            entry = directory(blob, complete)[0]
            fetcher.fetch(entry, io.BytesIO(blob), 0, complete)
            self.assertEqual((complete["support"] / entry["name"]).read_bytes(), contents)
            truncated = configuration(root / "truncated", len(blob))
            with self.assertRaises(RuntimeError):
                fetcher.fetch(entry, io.BytesIO(blob[:entry["end"] - 4]), 0, truncated)
            self.assertFalse((truncated["support"] / entry["name"]).exists())
            self.assertFalse(list(truncated["support"].rglob("*.part")))


class NetworkAndCacheTests(unittest.TestCase):
    def test_oversized_or_unbounded_range_response_is_rejected(self):
        config = configuration(Path("/unused"), 100)
        for body, status, content_range in ((b"12345", 206, "bytes 0-3/100"),
                                            (b"1234", 200, "bytes 0-3/100"),
                                            (b"1234", 206, "bytes 0-4/100")):
            response = io.BytesIO(body)
            response.status = status
            response.headers = {"Content-Range": content_range}
            with mock.patch.object(fetcher.urllib.request, "urlopen", return_value=response):
                with self.assertRaises(RuntimeError):
                    fetcher.request(0, 3, config)
        with mock.patch.object(fetcher.urllib.request, "urlopen") as open_url:
            for start, end in ((-1, 3), (5, 4), (0, 100)):
                with self.assertRaises(ValueError):
                    fetcher.request(start, end, config)
            open_url.assert_not_called()

    def test_429_cooldown_is_preserved_without_an_automatic_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder))
            error = urllib.error.HTTPError(config["url"], 429, "limited", {"Retry-After": "120"}, None)
            with mock.patch.object(fetcher, "load_config", return_value=config), \
                    mock.patch.object(fetcher, "fetch_notices"), \
                    mock.patch.object(fetcher.time, "time", return_value=100), \
                    mock.patch.object(fetcher.time, "sleep") as sleep, \
                    mock.patch.object(fetcher.urllib.request, "urlopen", side_effect=error) as open_url:
                with self.assertRaises(fetcher.RateLimitedError):
                    fetcher.main()
                self.assertEqual(open_url.call_count, 1)
                sleep.assert_not_called()
                with self.assertRaisesRegex(RuntimeError, "cooldown"):
                    fetcher.main()
                self.assertEqual(open_url.call_count, 1)
            saved = json.loads((config["cache"] / "rate-limit.json").read_text())
            self.assertEqual(saved["not_before"], 220)

    def test_complete_legacy_cache_uses_sha_checks_without_remote_directory_probe(self):
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder))
            config["support"].mkdir(parents=True)
            content = b"preserved legacy support"
            (config["support"] / "SDL3.dll").write_bytes(content)
            entry = dict(name="SDL3.dll", method=8, crc=zlib.crc32(content), compressed=10,
                         size=len(content), offset=0, sha256=hashlib.sha256(content).hexdigest())
            saved = dict(url=config["url"], archive_size=config["archive_size"], entries=[entry])
            manifest = config["cache"] / "support-manifest.json"
            manifest.write_text(json.dumps(saved))
            before = manifest.read_bytes()
            with mock.patch.object(fetcher, "load_config", return_value=config), \
                    mock.patch.object(fetcher, "fetch_notices"), \
                    mock.patch.object(fetcher, "request") as request:
                fetcher.main()
                request.assert_not_called()
            self.assertEqual(manifest.read_bytes(), before)
            self.assertEqual((config["support"] / "SDL3.dll").read_bytes(), content)
            entry["sha256"] = "0" * 64
            manifest.write_text(json.dumps(dict(saved, entries=[entry])))
            self.assertIsNone(fetcher.manifest_entries(config, "support-manifest.json", complete=True))

    def test_partial_cached_directory_resumes_only_missing_support_ranges(self):
        blob, contents = fixture_archive()
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder), len(blob))
            entries = directory(blob, config)
            config["support"].mkdir(parents=True)
            existing = config["support"] / "SDL3.dll"
            existing.write_bytes(contents["SDL3.dll"])
            original_mtime = existing.stat().st_mtime_ns
            fetcher.write_manifest(config, entries, "directory-manifest.json")
            requests = []
            def local_range(start, end, cfg):
                requests.append((start, end))
                return blob[start:end + 1]
            with mock.patch.object(fetcher, "load_config", return_value=config), \
                    mock.patch.object(fetcher, "fetch_notices"), \
                    mock.patch.object(fetcher, "request", side_effect=local_range):
                fetcher.main()
            self.assertTrue(requests)
            self.assertTrue(all(start > entries[0]["end"] for start, end in requests))
            self.assertEqual(existing.stat().st_mtime_ns, original_mtime)
            self.assertIsNotNone(fetcher.manifest_entries(config, "support-manifest.json", complete=True))

    def test_audited_manifest_can_seed_new_cache_without_another_download(self):
        with tempfile.TemporaryDirectory() as folder:
            config = configuration(Path(folder))
            config["asset_digest"] = "sha256:" + "b" * 64
            config["support"].mkdir(parents=True)
            content = b"verified audited support"
            (config["support"] / "SDL3.dll").write_bytes(content)
            entry = dict(name="SDL3.dll", relative="SDL3.dll", method=8,
                         crc32=f"{zlib.crc32(content):08x}", compressed=10,
                         size=len(content), offset=0, sha256=hashlib.sha256(content).hexdigest())
            saved = dict(url=config["url"], archive_size=config["archive_size"],
                         official_archive_digest=config["asset_digest"], entries=[entry])
            (config["cache"] / "support-manifest.json").write_text(json.dumps(saved))
            with mock.patch.object(fetcher, "load_config", return_value=config), \
                    mock.patch.object(fetcher, "fetch_notices"), \
                    mock.patch.object(fetcher, "request") as request:
                fetcher.main()
                request.assert_not_called()


if __name__ == "__main__":
    unittest.main()
