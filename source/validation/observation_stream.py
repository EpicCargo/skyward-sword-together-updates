"""Read complete scalar records from a concurrently written diagnostic stream.

Frame labels are preceding renderer-log buckets, not atomic game snapshots.
Malformed records invalidate the preceding value; they are never repaired.
"""
import hashlib
import json
import re


def latest(text, marker):
    if marker not in ('[sst-owned-state] ', '[sst-owned-props] '):
        raise ValueError('Unsupported observation marker')
    frame = 0
    record = None
    rejected = []
    unfinished = False
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        if not line.endswith('\n'):
            if marker in line:
                record = None
                unfinished = True
            continue
        match = re.search(r'\[vk\] present frame (\d+)', line)
        if match:
            frame = int(match[1])
        if marker not in line:
            continue
        try:
            value = json.loads(line.split(marker, 1)[1])
            if not isinstance(value, dict):
                raise ValueError('Observation must be a JSON object')
        except ValueError as error:
            record = None
            rejected.append(dict(line=number, preceding_renderer_frame=frame,
                                 text_line_sha256=hashlib.sha256(line.encode('utf-8')).hexdigest(),
                                 error=str(error)))
            continue
        record = dict(preceding_renderer_frame=frame, value=value)
    return dict(frame=frame, record=record, rejected=rejected, unfinished_record=unfinished)
