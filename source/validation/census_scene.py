"""Bind asynchronous census records to observed native-source scene buckets.

The travel counter is a queueing counter, not proof of the current scene.
Classification deliberately does not inspect target counts or actor identities.
"""


def classify(samples, observations, expected_scene):
    buckets = {}
    for row in observations:
        frame = row['frame']
        if type(frame) is not int or frame < 0:
            raise ValueError('Invalid observation frame')
        scene = row['peers'].get('scene')
        if scene is not None and not isinstance(scene, str):
            raise ValueError('Invalid source scene')
        buckets.setdefault(frame, set()).add(scene)
    frames = sorted(buckets)
    records, stable, transitional = [], [], []
    for original in samples:
        record = dict(original)
        frame = record['last_logged_present_frame']
        if type(frame) is not int or frame < 0:
            raise ValueError('Invalid census frame')
        scenes = buckets.get(frame)
        if scenes is None:
            before = [f for f in frames if f < frame]
            after = [f for f in frames if f > frame]
            scenes = buckets[before[-1]] | buckets[after[0]] if before and after else set()
        record['source_scenes_in_frame_bucket'] = sorted(scenes, key=lambda s: s or '')
        record['source_scene'] = next(iter(scenes)) if len(scenes) == 1 else None
        records.append(record)
        if record.get('complete') and record.get('loads') == 1:
            (stable if record['source_scene'] == expected_scene else transitional).append(record)
    return records, stable, transitional
