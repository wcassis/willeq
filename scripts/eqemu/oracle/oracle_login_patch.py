#!/usr/bin/env python3
"""Copy the VM login helper into a session with this display's known controls.

Credentials stay in the VM's original helper and are never printed or copied
back to the repo. Fail if its expected structure changes instead of running a
partially patched login.

usage: oracle_login_patch.py <helper> <session copy> [--display :N] [--credentials FILE]

B1134 (#454): --display retargets the copy at another X display (the caster's
Xvfb of an observer session; default :99). --credentials makes the copy log in
as ANOTHER account (the observer): the helper's two typed values (account,
then password) become "$OBS_USER" / "$OBS_PASS", read at run time by sourcing
FILE (a mode-600 file on the oracle VM, oracle_observer_setup.sh). The copy
holds the file's path, never its contents.
"""
from pathlib import Path
import argparse
import re

ap = argparse.ArgumentParser()
ap.add_argument('source', type=Path)
ap.add_argument('destination', type=Path)
ap.add_argument('--display', default=':99')
ap.add_argument('--credentials', type=Path)
args = ap.parse_args()
if not re.fullmatch(r':[0-9]+', args.display):
    raise SystemExit(f'bad display {args.display!r}')
body = args.source.read_text()
head = f'export DISPLAY={args.display}\nsource /mnt/projects/eq-oracle/scripts/ui-lib.sh\n'
if args.credentials is not None:
    if not re.fullmatch(r'observer-[A-Za-z]+\.cred', args.credentials.name) or not args.credentials.is_absolute():
        raise SystemExit('credentials file must be an absolute .../observer-<Character>.cred')
    if not args.credentials.is_file():
        raise SystemExit(f'no credentials file {args.credentials}')
    head += f'source {args.credentials}\n'
replacements = {
    'export DISPLAY=:99\n': head,
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
if args.credentials is not None:
    typed = re.compile(r'^(\s*xdotool type --delay [0-9]+ ).*$', re.M)
    if len(typed.findall(body)) != 2:
        raise SystemExit('login helper changed: expected two typed credential lines')
    values = iter(('"$OBS_USER"', '"$OBS_PASS"'))
    body = typed.sub(lambda m: m.group(1) + next(values), body)
args.destination.write_text(body)
args.destination.chmod(0o700)
