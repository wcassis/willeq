#!/usr/bin/env python3
"""Copy the VM login helper into a session with this display's known controls.

Credentials stay in the VM's original helper and are never printed or copied
back to the repo. Fail if its expected structure changes instead of running a
partially patched login.
"""
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[1:3])
body = source.read_text()
replacements = {
    'export DISPLAY=:99\n':
        'export DISPLAY=:99\nsource /mnt/projects/eq-oracle/scripts/ui-lib.sh\n',
    'xdotool mousemove 514 497': 'xdotool mousemove 642 514',
    'xdotool mousemove 515 498': 'xdotool mousemove 643 515',
    'key shift+Tab; sleep 1\n    key End; for _ in $(seq 1 25); do key BackSpace; done':
        'jclick 875 376; sleep 1\n    key End; for _ in $(seq 1 60); do key BackSpace; sleep 0.15; done',
    'key Tab; sleep 1\n    key End; for _ in $(seq 1 25); do key BackSpace; done':
        'jclick 875 435; sleep 1\n    key End; for _ in $(seq 1 60); do key BackSpace; sleep 0.15; done',
    'key Return\n\necho "== waiting for character select =="':
        'jclick 415 317; sleep 2; jclick 820 605\n\necho "== waiting for character select =="',
}
for old, new in replacements.items():
    if body.count(old) != 1:
        raise SystemExit(f'login helper changed: expected one occurrence of {old[:32]!r}')
    body = body.replace(old, new)
destination.write_text(body)
destination.chmod(0o700)
