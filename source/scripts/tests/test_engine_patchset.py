import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPTS = Path(__file__).resolve().parents[1]


def load_script(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


patchsets = load_script("sst_patchset_tests", "engine_patchset.py")
builder = load_script("sst_patchset_builder_tests", "build-windows.py")
checker = load_script("sst_patchset_checker_tests", "check-engine-patches.py")


class EnginePatchsetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sst-patchset-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.patches = self.root / "engine-patches"
        self.patches.mkdir()
        (self.patches / "base.txt").write_text("1" * 40 + "\n")
        (self.patches / "0001-legacy.patch").write_text("preserved legacy patch\n")

    def select(self):
        selected = self.patches / "upstream202"
        selected.mkdir()
        (selected / "base.txt").write_text("2" * 40 + "\n")
        (selected / "0001-current.patch").write_text("current patch\n")
        (self.patches / "current").symlink_to("upstream202", target_is_directory=True)
        return selected

    def configure(self, config, selected=None):
        selected = selected or self.patches
        (selected / "windows-support.json").write_text(json.dumps(config))

    def test_legacy_series_and_windows_defaults_are_preserved(self):
        self.assertEqual(patchsets.active_patchset(self.root), self.patches)
        self.assertEqual(patchsets.windows_build_paths(self.root), (
            self.root / ".deps/windows-upstream/support", self.root / ".deps/windows-runtime"))
        self.assertEqual((self.patches / "0001-legacy.patch").read_text(), "preserved legacy patch\n")
        self.assertEqual((self.patches / "base.txt").read_text(), "1" * 40 + "\n")

    def test_current_symlink_selects_only_the_new_series(self):
        selected = self.select()
        self.assertEqual(patchsets.active_patchset(self.root), selected)
        self.assertEqual([p.name for p in patchsets.active_patchset(self.root).glob("*.patch")],
                         ["0001-current.patch"])
        self.assertTrue((self.patches / "0001-legacy.patch").is_file())

    def test_current_directory_is_supported(self):
        selected = self.patches / "current"
        selected.mkdir()
        self.assertEqual(patchsets.active_patchset(self.root), selected)

    def test_broken_current_never_falls_back_to_legacy(self):
        (self.patches / "current").symlink_to("missing", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "existing directory"):
            patchsets.active_patchset(self.root)

    def test_current_must_be_a_directory(self):
        (self.patches / "current").write_text("not a selector directory")
        with self.assertRaisesRegex(ValueError, "existing directory"):
            patchsets.active_patchset(self.root)

    def test_current_cannot_select_another_project_directory(self):
        elsewhere = self.root / "unrelated"
        elsewhere.mkdir()
        (self.patches / "current").symlink_to("../unrelated", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "inside"):
            patchsets.active_patchset(self.root)

    def test_current_cannot_escape_project_or_follow_an_escaping_chain(self):
        with tempfile.TemporaryDirectory(prefix="sst-outside-patches-") as folder:
            outside = Path(folder)
            (self.patches / "indirect").symlink_to(outside, target_is_directory=True)
            (self.patches / "current").symlink_to("indirect", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "inside"):
                patchsets.active_patchset(self.root)

    def test_current_symlink_loop_is_rejected(self):
        (self.patches / "current").symlink_to("current", target_is_directory=True)
        with self.assertRaises(ValueError):
            patchsets.active_patchset(self.root)

    def test_active_config_ignores_inactive_legacy_config_and_fetch_metadata(self):
        selected = self.select()
        self.configure({"support": "../invalid-inactive", "prefix": "/invalid-inactive"})
        self.configure({"support": ".deps/windows202/support", "prefix": ".deps/windows202-runtime",
                        "cache": ".deps/windows202", "url": "https://example.invalid/tools.zip",
                        "archive_size": 123, "asset_digest": "fake-digest"}, selected)
        self.assertEqual(patchsets.windows_build_paths(self.root), (
            self.root / ".deps/windows202/support", self.root / ".deps/windows202-runtime"))

    def test_missing_optional_field_retains_its_legacy_default(self):
        self.configure({"prefix": ".deps/new-runtime"})
        self.assertEqual(patchsets.windows_build_paths(self.root), (
            self.root / ".deps/windows-upstream/support", self.root / ".deps/new-runtime"))
        self.configure({"support": ".deps/new-support"})
        self.assertEqual(patchsets.windows_build_paths(self.root), (
            self.root / ".deps/new-support", self.root / ".deps/windows-runtime"))

    def test_invalid_paths_are_rejected_for_either_config_field(self):
        invalid = ("../escape", "nested/../../escape", str(self.root / "absolute"),
                   "C:/escape", "C:drive-relative", "\\\\server\\share", "..\\escape",
                   "", "   ", "bad\x00path", None, 123, False, [], {})
        for field in ("support", "prefix"):
            for value in invalid:
                with self.subTest(field=field, value=value):
                    self.configure({field: value})
                    with self.assertRaises(ValueError):
                        patchsets.windows_build_paths(self.root)

    def test_configured_and_default_paths_cannot_escape_through_symlinks(self):
        with tempfile.TemporaryDirectory(prefix="sst-outside-support-") as folder:
            (self.root / "escape").symlink_to(folder, target_is_directory=True)
            for field in ("support", "prefix"):
                self.configure({field: "escape/not-created-yet"})
                with self.subTest(field=field), self.assertRaises(ValueError):
                    patchsets.windows_build_paths(self.root)
            (self.patches / "windows-support.json").unlink()
            (self.root / ".deps").symlink_to(folder, target_is_directory=True)
            with self.assertRaises(ValueError):
                patchsets.windows_build_paths(self.root)

    def test_paths_can_resolve_inside_project_without_existing_yet(self):
        (self.root / "storage").mkdir()
        (self.root / "alias").symlink_to("storage", target_is_directory=True)
        self.configure({"support": "alias/future/support", "prefix": "alias/future/prefix"})
        self.assertEqual(patchsets.windows_build_paths(self.root), (
            self.root / "storage/future/support", self.root / "storage/future/prefix"))

    def test_invalid_config_json_type_and_broken_config_are_rejected(self):
        config_path = self.patches / "windows-support.json"
        for content in ("{broken", "[]", "null", '"string"'):
            with self.subTest(content=content):
                config_path.write_text(content)
                with self.assertRaises(ValueError):
                    patchsets.windows_build_paths(self.root)
        config_path.unlink()
        config_path.symlink_to("missing.json")
        with self.assertRaises(ValueError):
            patchsets.windows_build_paths(self.root)

    def test_config_file_itself_cannot_escape_project(self):
        with tempfile.TemporaryDirectory(prefix="sst-outside-config-") as folder:
            outside = Path(folder) / "config.json"
            outside.write_text("{}")
            (self.patches / "windows-support.json").symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "inside"):
                patchsets.windows_build_paths(self.root)

    def test_source_archive_contains_only_active_normalized_patches_and_helper(self):
        selected = self.select()
        self.configure({"support": ".deps/new-support", "prefix": ".deps/new-prefix"}, selected)
        (selected / "do-not-package.log").write_text("unrelated file")
        (self.root / "scripts").mkdir()
        (self.root / "scripts/engine_patchset.py").write_text("# archive helper fixture\n")
        archive_path = self.root / "source.zip"
        with mock.patch.object(builder, "ROOT", self.root):
            builder.write_coop_source(archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            self.assertEqual(set(archive.namelist()), {
                "scripts/engine_patchset.py", "engine-patches/base.txt",
                "engine-patches/0001-current.patch", "engine-patches/windows-support.json"})
            self.assertEqual(archive.read("engine-patches/base.txt"), ("2" * 40 + "\n").encode())
            self.assertEqual(archive.read("engine-patches/0001-current.patch"), b"current patch\n")
        self.assertEqual((self.patches / "0001-legacy.patch").read_text(), "preserved legacy patch\n")

    def test_legacy_source_archive_retains_flat_patch_names(self):
        archive_path = self.root / "legacy-source.zip"
        with mock.patch.object(builder, "ROOT", self.root):
            builder.write_coop_source(archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            self.assertEqual(set(archive.namelist()), {
                "engine-patches/base.txt", "engine-patches/0001-legacy.patch"})

    def test_source_archive_includes_online_helpers_and_complete_test_inputs_once(self):
        self.select()
        required = [
            "client/Makefile", "client/src/sst_game.c", "client/src/sst_pickup.h",
            "client/tools/session_host.c", "client/tools/network_check.c",
            "client/tests/session_lifecycle_test.c", "client/tests/session_host_test.py",
            "client/tests/avatar_test.c", "client/tests/census_test.c", "client/tests/run_e2e.sh",
            "client/tests/fixtures/queue-backlog.json", "client/tests/sst_test.h",
            "launcher/sst_launcher.py", "launcher/sst_online_init.py", "launcher/sst_online.py",
            "launcher/sst_online_transport.py", "launcher/build_bundle.py", "launcher/publish.py",
            "launcher/release.py", "launcher/bootstrap_windows.c", "launcher/README.txt",
            "launcher/tests/test_online_transport.py", "launcher/tests/test_online_session.py",
            "launcher/tests/test_online_e2e.py", "scripts/rooms-dev-server.mjs",
            "scripts/check-engine-patches.py", "scripts/build-engine.sh", "scripts/resource_runner.py",
            "scripts/build-windows-network.py", "scripts/tests/test_engine_patchset.py",
            "server/relay.js", "server/reward-catalogue.json", "server/rooms/worker.mjs", "server/rooms/package.json",
            "server/rooms/wrangler.toml", "server/rooms/README.md",
            "server/rooms/test/cloudflare-loader.mjs", "server/rooms/test/worker.test.mjs",
            "third_party/SST-HANDOFF-20261030/LICENSE.txt",
            "docs/reward-catalogue.tsv", "docs/story-sync-policy.tsv",
        ]
        for name in required:
            path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("source fixture " + name + "\r\n").encode())
        excluded = ["client/build/sst-session-host", "client/tools/private.log",
                    "client/tests/build/generated.c", "client/tests/__pycache__/private.py",
                    "scripts/node_modules/package/secret.py", "scripts/.git/private.py",
                    "launcher/launcher-config.json", "launcher/launcher-session.json",
                    "launcher/launcher-error.log", "launcher/.env", "launcher/__pycache__/private.py",
                    "server/rooms/.wrangler/state.json", "server/rooms/.dev.vars",
                    "server/rooms/node_modules/private.mjs", "build/personal-save.sav", ".deps/prod.keys"]
        for name in excluded:
            path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"DO_NOT_PACKAGE")
        archive_path = self.root / "complete-source.zip"
        with mock.patch.object(builder, "ROOT", self.root):
            builder.write_coop_source(archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            self.assertEqual(len(names), len(set(names)))
            self.assertEqual(names.count("scripts/resource_runner.py"), 1)
            self.assertTrue(set(required) <= set(names))
            self.assertFalse(set(excluded) & set(names))
            self.assertEqual(archive.read("client/tools/session_host.c"),
                             ("source fixture client/tools/session_host.c\r\n").encode())
            for name in names:
                self.assertNotIn(b"DO_NOT_PACKAGE", archive.read(name))

    def test_source_archive_never_follows_source_or_recipe_symlinks(self):
        selected = self.select()
        private = self.root / "private"; private.mkdir()
        (private / "secret.py").write_text("DO_NOT_PACKAGE")
        launcher = self.root / "launcher"; launcher.mkdir()
        (launcher / "private.py").symlink_to(private / "secret.py")
        tests = self.root / "client/tests"; tests.mkdir(parents=True)
        (tests / "linked").symlink_to(private, target_is_directory=True)
        archive_path = self.root / "symlink-source.zip"
        with mock.patch.object(builder, "ROOT", self.root):
            builder.write_coop_source(archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            self.assertFalse(any("private.py" in name or "secret.py" in name for name in archive.namelist()))
        (selected / "base.txt").unlink()
        (selected / "base.txt").symlink_to(private / "secret.py")
        with mock.patch.object(builder, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "regular files"):
                builder.write_coop_source(self.root / "bad-recipe.zip")

    def test_patch_checker_imports_only_active_base_and_patch_files(self):
        selected = self.select()
        result = mock.Mock(stdout="test-tree\n")
        with mock.patch.object(checker, "ROOT", self.root), \
                mock.patch("sys.argv", ["check-engine-patches.py", "--engine", str(self.root / "engine")]), \
                mock.patch.object(checker.subprocess, "run", return_value=result) as run:
            checker.main()
        commands = [call.args[0] for call in run.call_args_list]
        checkout = next(command for command in commands if "checkout" in command)
        apply = next(command for command in commands if "am" in command)
        self.assertEqual(checkout[-1], "2" * 40)
        self.assertEqual(list(apply[apply.index("am") + 1:]), [str(selected / "0001-current.patch")])


if __name__ == "__main__":
    unittest.main()
