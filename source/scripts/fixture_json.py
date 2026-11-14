"""Publish complete diagnostic snapshots for concurrently running test tools."""
import json
import os
from pathlib import Path
import tempfile
import time

def write_json_snapshot(path, value):
    """Replace a complete JSON snapshot; retain the old one during reader locks.

    Returning False reports a bounded Windows sharing-lock delay. Other errors
    still propagate. This helper is for fixture status/control, not game saves.
    """
    path = Path(path)
    payload = json.dumps(value) + '\n'
    descriptor, name = tempfile.mkstemp(prefix='.' + path.name + '-', suffix='.tmp', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='\n') as output:
            output.write(payload)
        for attempt in range(4):
            try:
                os.replace(temporary, path)
                return True
            except PermissionError:
                if attempt == 3:
                    return False
                time.sleep(.005)
    finally:
        temporary.unlink(missing_ok=True)
