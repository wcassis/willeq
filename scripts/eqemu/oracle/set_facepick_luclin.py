#!/usr/bin/env python3
"""Enable the 26 in-era per-race Luclin models for an oracle capture."""
from pathlib import Path
import re

path = Path('/home/user/eq-oracle/client/eqclient.ini')
races = ('Human', 'Barbarian', 'Erudite', 'WoodElf', 'HighElf',
         'DarkElf', 'HalfElf', 'Dwarf', 'Troll', 'Ogre',
         'Halfling', 'Gnome', 'Iksar')
keys = {f'UseLuclin{race}{gender}' for race in races for gender in ('Male', 'Female')}
raw = path.read_bytes()
lines = raw.splitlines(keepends=True)
found = set()
for i, line in enumerate(lines):
    m = re.match(rb'(UseLuclin\w+)=(TRUE|FALSE)', line)
    if not m:
        continue
    key = m.group(1).decode('ascii')
    if key in keys:
        lines[i] = line.replace(b'=FALSE', b'=TRUE', 1)
        found.add(key)
if found != keys:
    raise ValueError(f'found {len(found)} in-era toggles, expected {len(keys)}')
path.write_bytes(b''.join(lines))
print(f'Enabled {len(found)} in-era Luclin model toggles for supplemental oracle capture')
