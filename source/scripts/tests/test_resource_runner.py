"""Mock-only resource policy tests: no game, compiler, child process or real signals."""
import importlib.util
import io
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("sst_resource_test", "resource_runner.py")
native = load("sst_native_policy_test", "native-test-runner.py")
windows = load("sst_windows_resource_test", "build-windows.py")


class ResourcePolicyTests(unittest.TestCase):
    def process(self):
        process = mock.Mock(pid=43210)
        process.poll.return_value = 0
        return process

    def run_mock(self, *, resident=1, available=20000, games=None, **kwargs):
        process = self.process()
        with mock.patch.object(runner.subprocess, "Popen", return_value=process) as popen, \
                mock.patch.object(runner, "group_rss", return_value=resident * runner.MIB), \
                mock.patch.object(runner, "available_memory", return_value=available * runner.MIB), \
                mock.patch.object(runner, "running_games", side_effect=games) as guard, \
                mock.patch.object(runner, "stop_group") as stop, \
                mock.patch.object(runner.os, "setpriority"), mock.patch("sys.stderr", new_callable=io.StringIO):
            result = runner.run(["harmless-fixture"], **kwargs)
        return result, process, popen, guard, stop

    def test_owned_rss_budget_stops_only_owned_group(self):
        result, process, popen, guard, stop = self.run_mock(resident=4097)
        self.assertEqual(result, 125)
        stop.assert_called_once_with(process)
        self.assertTrue(popen.call_args.kwargs["start_new_session"])
        guard.assert_not_called()  # A background build can coexist with a personal game.

    def test_available_floor_cannot_be_disabled_or_lowered(self):
        with mock.patch.object(runner.subprocess, "Popen") as popen:
            for floor in (0, 6143):
                with self.subTest(floor=floor), self.assertRaises(ValueError):
                    runner.run(["fixture"], available_mib=floor)
        popen.assert_not_called()

    def test_memory_floor_prevents_start_without_touching_foreign_processes(self):
        result, _, popen, guard, stop = self.run_mock(available=6143)
        self.assertEqual(result, 125)
        popen.assert_not_called()
        stop.assert_not_called()
        guard.assert_not_called()

    def test_memory_pressure_after_start_stops_only_owned_group(self):
        process = self.process()
        with mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "available_memory", side_effect=[7000 * runner.MIB, 6000 * runner.MIB]), \
                mock.patch.object(runner, "group_rss", return_value=10 * runner.MIB), \
                mock.patch.object(runner, "stop_group") as stop, mock.patch.object(runner.os, "setpriority"), \
                mock.patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(runner.run(["fixture"]), 125)
        stop.assert_called_once_with(process)

    def test_game_guard_prevents_a_second_full_game(self):
        result, _, popen, _, stop = self.run_mock(guard_game=True, games=[[(91, 91)]])
        self.assertEqual(result, 75)
        popen.assert_not_called()
        stop.assert_not_called()

    def test_game_started_later_stops_test_but_never_foreign_game(self):
        result, process, _, _, stop = self.run_mock(guard_game=True, games=[[], [(91, 91)]])
        self.assertEqual(result, 75)
        stop.assert_called_once_with(process)

    def test_one_owned_game_allowed_but_two_owned_games_rejected(self):
        self.assertEqual(self.run_mock(guard_game=True, games=[[], [(43210, 43210)]])[0], 0)
        self.assertEqual(self.run_mock(guard_game=True, games=[[], [(43210, 43210), (43211, 43210)]])[0], 75)

    def test_signal_restores_handlers_and_cleans_owned_group(self):
        previous = signal.getsignal(signal.SIGTERM)
        process = self.process()
        def interrupted():
            signal.getsignal(signal.SIGTERM)(signal.SIGTERM, None)
        process.poll.side_effect = interrupted
        with mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "available_memory", return_value=20000 * runner.MIB), \
                mock.patch.object(runner, "group_rss", return_value=1), \
                mock.patch.object(runner, "stop_group") as stop, mock.patch.object(runner.os, "setpriority"), \
                mock.patch("sys.stderr", new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as error:
                runner.run(["fixture"])
        self.assertEqual(error.exception.code, 143)
        self.assertEqual(signal.getsignal(signal.SIGTERM), previous)
        stop.assert_called_once_with(process)

    def test_cleanup_signals_exact_owned_pgid_even_after_leader_exit(self):
        process = self.process()
        with mock.patch.object(runner.os, "killpg") as kill, \
                mock.patch.object(runner.time, "monotonic", side_effect=[0, 6]):
            runner.stop_group(process)
        self.assertEqual(kill.call_args_list, [mock.call(43210, signal.SIGTERM), mock.call(43210, signal.SIGKILL)])
        process.wait.assert_called_once_with()

    def test_native_defaults_and_fake_command_do_not_activate_game_guard(self):
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch("sys.argv", ["native-test-runner.py", "--lock", str(Path(folder) / "lock"),
                                        "--seconds", "5", "--", "python3", "fixture.py"]), \
                mock.patch.object(native, "run", return_value=0) as run:
            self.assertEqual(native.main(), 0)
        self.assertEqual(run.call_args.kwargs["rss_mib"], 12288)
        self.assertFalse(run.call_args.kwargs["guard_game"])
        self.assertTrue(runner.is_game_command(["gamescope", "--", "/installed/version/skyrecomp"]))
        self.assertTrue(runner.is_game_command(["wine", r"Z:\installed\skyrecomp.exe"]))

    def test_proc_scan_counts_native_installed_and_wine_but_not_build_target(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fixtures = [(11, "skyrecomp", "/installed/items59/skyrecomp", b"skyrecomp\0"),
                        (12, "wine64-preloader", "/usr/lib/wine/wine64-preloader", b"wine\0Z:\\SST\\skyrecomp.exe\0"),
                        (13, "ld.lld", "/compiler/ld.lld", b"ld.lld\0-o\0skyrecomp.exe\0"),
                        (14, "python3", "/usr/bin/python3", b"python3\0fixture.py\0")]
            for pid, comm, executable, argv in fixtures:
                path = root / str(pid); path.mkdir()
                (path / "stat").write_text(f"{pid} ({comm}) S 1 {pid} 1")
                (path / "comm").write_text(comm)
                (path / "cmdline").write_bytes(argv)
                (path / "exe").symlink_to(executable)
                (path / "statm").write_text("99 2 0")
            self.assertCountEqual(runner.running_games(root), [(11, 11), (12, 12)])
            self.assertEqual(runner.group_rss(11, root), 2 * os.sysconf("SC_PAGE_SIZE"))
            meminfo = root / "meminfo"; meminfo.write_text("MemTotal: 32768000 kB\nMemAvailable: 7340032 kB\n")
            self.assertEqual(runner.available_memory(meminfo), 7168 * runner.MIB)

    def test_package_only_never_starts_compiler_or_resource_monitor(self):
        with tempfile.TemporaryDirectory() as folder:
            toolchain = Path(folder); (toolchain / "bin").mkdir(); (toolchain / "bin/clang.exe").touch()
            with mock.patch("sys.argv", ["build-windows.py", "--package-only", "--output", str(toolchain / "candidate.zip")]), \
                    mock.patch.object(windows, "NATIVE_TC", toolchain), \
                    mock.patch.object(windows, "package") as package, \
                    mock.patch.object(windows, "run_with_resources") as resources, \
                    mock.patch.object(windows.subprocess, "run") as subprocess_run:
                windows.main()
            resources.assert_not_called(); subprocess_run.assert_not_called(); package.assert_called_once()

    def test_windows_build_uses_shared_lock_and_monitored_single_job_worker(self):
        with tempfile.TemporaryDirectory() as folder:
            toolchain = Path(folder); (toolchain / "bin").mkdir(); (toolchain / "bin/clang.exe").touch()
            with mock.patch("sys.argv", ["build-windows.py", "--no-fetch", "--output", str(toolchain / "candidate.zip")]), \
                    mock.patch.object(windows, "NATIVE_TC", toolchain), \
                    mock.patch.object(windows, "package") as package, \
                    mock.patch.object(windows, "run_with_resources", return_value=0) as resources:
                windows.main()
            self.assertIn("--build-worker", resources.call_args.args[0])
            self.assertEqual(resources.call_args.kwargs["rss_mib"], 4096)
            self.assertEqual(resources.call_args.kwargs["available_mib"], 6144)
            self.assertEqual(resources.call_args.kwargs["lock"], windows.ROOT / "build/developer-build.lock")
            package.assert_called_once()
            with mock.patch.object(windows, "prepare"), mock.patch.object(windows.subprocess, "run") as run:
                windows.build_runtime(toolchain)
            self.assertIn("-j1", run.call_args.args[0])
            self.assertEqual(run.call_args.kwargs["env"]["MAKEFLAGS"], "")
            self.assertEqual(run.call_args.kwargs["env"]["MFLAGS"], "")

    def test_private_windows_worker_cannot_bypass_monitor_by_cli(self):
        with mock.patch("sys.argv", ["build-windows.py", "--build-worker"]), \
                mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(windows, "build_runtime") as build, \
                mock.patch("sys.stderr", new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as error:
                windows.main()
        self.assertEqual(error.exception.code, 2)
        build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
