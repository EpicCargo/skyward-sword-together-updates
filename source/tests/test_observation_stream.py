"""Reproduce partial live reads and interleaved scalar diagnostics without a game."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('observation_stream', Path(__file__).resolve().parents[1] / 'validation/observation_stream.py')
stream = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stream)
STATE = '[sst-owned-state] '
PROPS = '[sst-owned-props] '
GOOD = '[vk] present frame 120\n' + STATE + '{"readable": true, "event_state": 0}\n'


class ObservationStreamTests(unittest.TestCase):
    def test_complete_state_has_its_preceding_frame_bucket(self):
        result = stream.latest(GOOD + '[vk] present frame 180\n', STATE)
        self.assertEqual(result['frame'], 180)
        self.assertEqual(result['record']['preceding_renderer_frame'], 120)
        self.assertTrue(result['record']['value']['readable'])

    def test_partial_latest_state_waits_for_completion_instead_of_using_old_state(self):
        partial = GOOD + STATE + '{"readable": true, "event_state":'
        result = stream.latest(partial, STATE)
        self.assertIsNone(result['record'])
        self.assertTrue(result['unfinished_record'])
        self.assertFalse(result['rejected'])
        complete = stream.latest(partial + '2}\n', STATE)
        self.assertEqual(complete['record']['value']['event_state'], 2)

    def test_interleaved_complete_state_invalidates_previous_state_without_repair(self):
        result = stream.latest(GOOD + STATE + '{"readable":true,[renderer] native log}\n', STATE)
        self.assertIsNone(result['record'])
        self.assertEqual(len(result['rejected']), 1)
        self.assertEqual(len(result['rejected'][0]['text_line_sha256']), 64)

    def test_fresh_complete_state_recovers_after_rejected_state(self):
        result = stream.latest(STATE + 'invalid\n' + GOOD, STATE)
        self.assertTrue(result['record']['value']['readable'])
        self.assertEqual(len(result['rejected']), 1)

    def test_non_object_state_is_rejected(self):
        for value in ('null', '[]', 'true', '4', '"string"'):
            with self.subTest(value=value):
                result = stream.latest(GOOD + STATE + value + '\n', STATE)
                self.assertIsNone(result['record'])
                self.assertEqual(len(result['rejected']), 1)

    def test_state_and_props_are_independent(self):
        text = GOOD + PROPS + '{"complete":true,"items":[]}\n' + STATE + 'broken\n'
        self.assertIsNone(stream.latest(text, STATE)['record'])
        self.assertTrue(stream.latest(text, PROPS)['record']['value']['complete'])

    def test_incomplete_unrelated_line_does_not_discard_a_complete_observation(self):
        result = stream.latest(GOOD + '[renderer] other unfinished diagnostic', STATE)
        self.assertIsNotNone(result['record'])
        self.assertFalse(result['unfinished_record'])

    def test_crlf_records_are_complete(self):
        self.assertTrue(stream.latest(GOOD.replace('\n', '\r\n'), STATE)['record']['value']['readable'])

    def test_unknown_marker_is_rejected(self):
        with self.assertRaises(ValueError):
            stream.latest(GOOD, '[unknown] ')


if __name__ == '__main__':
    unittest.main()
