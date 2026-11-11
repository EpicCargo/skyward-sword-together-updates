"""File-only regressions for the actual queued-travel/old-tree observation."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('census_scene', Path(__file__).resolve().parents[1] / 'validation/census_scene.py')
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
HALL = '054630303172000102'
BEDROOM = '054630303172010105'


def sample(frame, count):
    return dict(last_logged_present_frame=frame, complete=True, loads=1, target_count=count)


def observed(frame, area):
    return dict(frame=frame, peers=dict(scene=area))


class SceneClassificationTests(unittest.TestCase):
    def test_queued_travel_keeps_old_zero_target_tree_as_transition(self):
        old, current = sample(6901, 0), sample(7141, 1)
        records, stable, transition = scene.classify([old, current],
            [observed(6901, BEDROOM), observed(6901, HALL), observed(7141, HALL)], HALL)
        self.assertEqual([r['target_count'] for r in stable], [1])
        self.assertEqual([r['target_count'] for r in transition], [0])
        self.assertEqual(set(records[0]['source_scenes_in_frame_bucket']), {HALL, BEDROOM})
        self.assertNotIn('source_scene', old)

    def test_real_hall_absence_and_duplicates_are_never_filtered(self):
        records, stable, transition = scene.classify([sample(100, 0), sample(100, 2)], [observed(100, HALL)], HALL)
        self.assertEqual([r['target_count'] for r in stable], [0, 2])
        self.assertFalse(transition)

    def test_missing_bucket_requires_matching_scene_on_both_sides(self):
        _, stable, transition = scene.classify([sample(150, 1)], [observed(100, HALL), observed(200, HALL)], HALL)
        self.assertEqual(len(stable), 1)
        for observations in ([observed(100, BEDROOM), observed(200, HALL)], [observed(100, HALL)], []):
            with self.subTest(observations=observations):
                _, stable, transition = scene.classify([sample(150, 1)], observations, HALL)
                self.assertFalse(stable)
                self.assertEqual(len(transition), 1)

    def test_invalid_frames_and_scenes_fail_closed(self):
        for observations in ([observed(True, HALL)], [observed(-1, HALL)], [observed(100, 123)]):
            with self.assertRaises(ValueError): scene.classify([sample(100, 1)], observations, HALL)
        with self.assertRaises(ValueError): scene.classify([sample(-1, 1)], [observed(100, HALL)], HALL)


if __name__ == '__main__':
    unittest.main()
