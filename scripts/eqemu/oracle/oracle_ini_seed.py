#!/usr/bin/env python3
"""Temporarily remove one hotbutton key, preserving the client's INI bytes."""
from pathlib import Path
import sys

client, out = map(Path, sys.argv[1:3])
character, directive = sys.argv[3:5]
section_name, key = directive.split('.', 1)
files = list(client.glob(f'{character}_*.ini'))
if len(files) != 1 or not section_name or not key:
    raise SystemExit('expected exactly one character INI and a Section.Key directive')
source = files[0]
original = source.read_bytes()
(out / 'character-original.ini').write_bytes(original)
(out / 'character-file.txt').write_text(str(source))
section = None
seeded = []
for line in original.splitlines(keepends=True):
    stripped = line.strip()
    if stripped.startswith(b'[') and stripped.endswith(b']'):
        section = stripped[1:-1].decode('ascii', errors='replace')
    if section == section_name and stripped.startswith(key.encode('ascii') + b'='):
        continue
    seeded.append(line)
source.write_bytes(b''.join(seeded))
