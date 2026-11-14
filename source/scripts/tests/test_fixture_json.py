import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fixture_json import write_json_snapshot

class SnapshotContracts(unittest.TestCase):
    def test_concurrent_readers_never_see_partial_json(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            self.assertTrue(write_json_snapshot(path, dict(sequence=0, body='0' * 512)))
            stop = threading.Event()
            errors, reads = [], []
            def reader():
                count = 0
                try:
                    while not stop.is_set():
                        value = json.loads(path.read_text())
                        if value['body'] != str(value['sequence']) * 512:
                            raise AssertionError('Snapshot contents came from different publications')
                        count += 1
                except BaseException as error:
                    errors.append(error)
                finally:
                    reads.append(count)
            threads = [threading.Thread(target=reader) for _ in range(3)]
            for thread in threads:
                thread.start()
            try:
                for sequence in range(1, 301):
                    self.assertTrue(write_json_snapshot(path, dict(sequence=sequence, body=str(sequence) * 512)))
            finally:
                stop.set()
                for thread in threads:
                    thread.join(timeout=5)
            self.assertTrue(all(not t.is_alive() for t in threads))
            self.assertEqual(errors, [])
            self.assertTrue(all(n > 0 for n in reads))
            self.assertEqual(json.loads(path.read_text())['sequence'], 300)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_reader_lock_retries_without_truncating_old_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'control.json'
            write_json_snapshot(path, dict(active=False))
            import os
            replace = os.replace
            attempts = []
            def locked_twice(source, destination):
                attempts.append(1)
                self.assertEqual(json.loads(path.read_text()), dict(active=False))
                if len(attempts) < 3:
                    raise PermissionError('Reader temporarily holds the destination')
                return replace(source, destination)
            with patch('fixture_json.os.replace', side_effect=locked_twice):
                self.assertTrue(write_json_snapshot(path, dict(active=True)))
            self.assertEqual(len(attempts), 3)
            self.assertEqual(json.loads(path.read_text()), dict(active=True))

    def test_persistent_reader_lock_retains_previous_file_and_cleans_temp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            write_json_snapshot(path, dict(sequence=1))
            with patch('fixture_json.os.replace', side_effect=PermissionError('Busy')):
                self.assertFalse(write_json_snapshot(path, dict(sequence=2)))
            self.assertEqual(json.loads(path.read_text()), dict(sequence=1))
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_invalid_payload_preserves_previous_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            write_json_snapshot(path, dict(sequence=1))
            with self.assertRaises(TypeError):
                write_json_snapshot(path, object())
            self.assertEqual(json.loads(path.read_text()), dict(sequence=1))
            self.assertEqual(list(Path(directory).iterdir()), [path])

if __name__ == '__main__':
    unittest.main()
