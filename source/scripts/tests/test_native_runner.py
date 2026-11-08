import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

RUNNER = Path(__file__).resolve().parents[1] / "native-test-runner.py"


class NativeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sst-runner-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def start(self, code, seconds=20, rss_mib=6144):
        return subprocess.Popen([
            sys.executable, str(RUNNER), "--lock", str(self.root / "test.lock"),
            "--seconds", str(seconds), "--rss-mib", str(rss_mib), "--", sys.executable, "-c", code,
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def wait_file(self, path):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size:
                return int(path.read_text())
            time.sleep(.02)
        self.fail("test child did not start")

    def assert_stopped(self, pid):
        try:
            state = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
            self.assertEqual(state, "Z", f"child {pid} still running")
        except FileNotFoundError:
            pass

    def tree_code(self):
        child = (
            "import os,signal,time; from pathlib import Path; "
            "signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            f"Path({str(self.root / 'child')!r}).write_text(str(os.getpid())); time.sleep(60)"
        )
        return f"import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',{child!r}]); "

    def test_timeout_kills_stubborn_descendant(self):
        process = self.start(self.tree_code() + "time.sleep(60)", seconds=.5)
        child = self.wait_file(self.root / "child")
        process.communicate(timeout=10)
        self.assertEqual(process.returncode, 124)
        self.assert_stopped(child)

    def test_parent_exit_still_cleans_descendant(self):
        process = self.start(self.tree_code() + "time.sleep(.3)")
        child = self.wait_file(self.root / "child")
        process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0)
        self.assert_stopped(child)

    def test_lock_and_interruption(self):
        ready = self.root / "ready"
        code = f"import os,time; from pathlib import Path; Path({str(ready)!r}).write_text(str(os.getpid())); time.sleep(60)"
        first = self.start(code)
        try:
            child = self.wait_file(ready)
            second = self.start("raise SystemExit(99)")
            _, err = second.communicate(timeout=5)
            self.assertEqual(second.returncode, 75)
            self.assertIn(b"Another native SST test", err)
            first.send_signal(signal.SIGTERM)
            first.communicate(timeout=10)
            self.assertEqual(first.returncode, 143)
            self.assert_stopped(child)
        finally:
            if first.poll() is None:
                first.terminate()
                first.communicate(timeout=10)
        after = self.start("raise SystemExit(0)")
        after.communicate(timeout=5)
        self.assertEqual(after.returncode, 0)

    def test_memory_budget_cleans_only_owned_group(self):
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        self.addCleanup(unrelated.wait)
        self.addCleanup(unrelated.terminate)
        ready = self.root / "allocated"
        allocator = (
            "import os,time; from pathlib import Path; memory=bytearray(64*1024*1024); "
            f"Path({str(ready)!r}).write_text(str(os.getpid())); time.sleep(60)"
        )
        code = f"import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',{allocator!r}]); time.sleep(60)"
        process = self.start(code, rss_mib=40)
        child = self.wait_file(ready)
        _, err = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 125, err.decode())
        self.assertIn(b"exceeded 40 MiB group RSS", err)
        self.assertIn(b"peak sampled group RSS", err)
        self.assert_stopped(child)
        self.assertIsNone(unrelated.poll(), "memory guard touched an unrelated process")
        after = self.start("raise SystemExit(0)")
        after.communicate(timeout=5)
        self.assertEqual(after.returncode, 0, "memory stop did not release the native-test lock")

    def test_invalid_memory_budget_does_not_start_command(self):
        process = self.start("raise SystemExit(99)", rss_mib=0)
        _, err = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 2)
        self.assertIn(b"positive RSS budget", err)


if __name__ == "__main__":
    unittest.main()
