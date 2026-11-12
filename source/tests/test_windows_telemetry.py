"""Scalar sampler checks; fake replies launch no Windows process or compiler."""
import importlib.util
import json
from pathlib import Path
import queue
import threading
import unittest
from unittest import mock

spec=importlib.util.spec_from_file_location('windows_telemetry',Path(__file__).resolve().parents[1]/'validation/windows_telemetry.py')
telemetry=importlib.util.module_from_spec(spec)
spec.loader.exec_module(telemetry)
MIB=1024*1024


def sampler(packets):
    value=telemetry.Sampler.__new__(telemetry.Sampler)
    value.lines=queue.Queue(maxsize=2)
    value.sequence=0;value.minimum_free_bytes=None
    value.last_snapshot=None;value.last_rejection=None;value.lock=threading.Lock()
    value.process=mock.Mock()
    value.process.poll.return_value=None
    replies=iter(packets)
    value.process.stdin.flush.side_effect=lambda:value.lines.put(json.dumps(next(replies)))
    return value


def reply(sequence,available=8192,games=0):
    return dict(ok=True,sequence=sequence,free_bytes=available*MIB,games=games)


class TelemetryEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.guard=mock.Mock()
        self.guard.rr.running_games.return_value=[]

    def test_pressure_triggering_sample_updates_minimum_and_still_stops(self):
        first,pressure=reply(1),reply(2,2048)
        value=sampler([first,pressure])
        self.assertEqual(value.check(self.guard,3072),first)
        with self.assertRaisesRegex(RuntimeError,'3072 MiB free RAM reserve'):
            value.check(self.guard,3072)
        self.assertEqual(value.minimum_free_bytes,2048*MIB)
        self.assertEqual(value.last_snapshot,pressure)
        self.assertEqual(value.last_rejection['packet'],pressure)
        self.assertEqual(value.last_rejection['sequence'],2)

    def test_first_reply_below_reserve_is_recorded_and_rejected(self):
        value=sampler([reply(1,3071)])
        with self.assertRaises(RuntimeError):value.check(self.guard,3072)
        self.assertEqual(value.minimum_free_bytes,3071*MIB)

    def test_exact_reserve_is_accepted(self):
        value=sampler([reply(1,3072)])
        self.assertEqual(value.check(self.guard,3072)['free_bytes'],3072*MIB)
        self.assertIsNone(value.last_rejection)

    def test_malformed_stale_or_negative_reply_cannot_lower_minimum(self):
        for bad in (reply(1,1),dict(reply(2,1),sequence=True),dict(reply(2,1),free_bytes=-1),
                    dict(reply(2,1),ok=False),dict(reply(2,1),games='0'),[],None):
            with self.subTest(reply=bad):
                value=sampler([reply(1),bad]);value.check(self.guard,3072)
                with self.assertRaisesRegex(RuntimeError,'Invalid or stale'):value.check(self.guard,3072)
                self.assertEqual(value.minimum_free_bytes,8192*MIB)
                self.assertEqual(value.last_snapshot,reply(1))

    def test_windows_and_linux_game_total_remains_enforced(self):
        value=sampler([reply(1,games=1)])
        self.guard.rr.running_games.return_value=[(7,7)]
        with self.assertRaisesRegex(RuntimeError,'at most one'):value.check(self.guard,3072)
        self.assertEqual(value.last_rejection['packet']['games'],1)

    def test_one_existing_game_remains_allowed(self):
        value=sampler([reply(1,games=1)])
        self.assertEqual(value.check(self.guard,3072)['games'],1)

    def test_unsolicited_reply_is_rejected_without_sending_a_request(self):
        value=sampler([]);value.lines.put(json.dumps(reply(1)))
        with self.assertRaisesRegex(RuntimeError,'unsolicited'):value.check(self.guard,3072)
        value.process.stdin.write.assert_not_called()

    def test_stopped_sampler_is_rejected_without_sending_a_request(self):
        value=sampler([]);value.process.poll.return_value=1
        with self.assertRaisesRegex(RuntimeError,'stopped'):value.check(self.guard,3072)
        value.process.stdin.write.assert_not_called()


if __name__=='__main__':
    unittest.main()
