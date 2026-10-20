#!/usr/bin/env python3
"""Temporarily edit one character- INI key, preserving the client's INI bytes.

argv: client_dir out_dir character directive

directive is 'Section.Key' (remove the line, B1058) or 'Section.Key=VALUE'
(B1002: set/override the key, e.g. Defaults.HideChatWin=TRUE so a plan
captures a chat-free backdrop; for eqclient.ini the character arg is empty
and eqclient.ini itself is the target). The original bytes are saved to
out_dir/character-original.ini before any change (both shapes).
"""
from pathlib import Path
import sys

client, out = map(Path, sys.argv[1:3])
character, directive = sys.argv[3:5]
if '=' in directive:
    section_name, rest = directive.split('.', 1)
    key, value = rest.split('=', 1)
    set_value = value
else:
    section_name, key = directive.split('.', 1)
    set_value = None
if character:
    files = list(client.glob(f'{character}_*.ini'))
else:
    files = [client / 'eqclient.ini']
if len(files) != 1 or not section_name or not key or (set_value is not None and not set_value):
    raise SystemExit('expected exactly one target INI and a Section.Key[=VALUE] directive')
source = files[0]
original = source.read_bytes()
if character:
    # character-INI edits keep the existing restore bookkeeping
    (out / 'character-original.ini').write_bytes(original)
    (out / 'character-file.txt').write_text(str(source))
else:
    # eqclient.ini edits have their own backup (oracle_remote.sh's
    # eqclient-iniset-original.ini); do NOT touch the character bookkeeping
    # or the character-restore path would write eqclient.ini back.
    pass
section = None
seen_key = False
seeded = []
for line in original.splitlines(keepends=True):
    stripped = line.strip()
    if stripped.startswith(b'[') and stripped.endswith(b']'):
        section = stripped[1:-1].decode('ascii', errors='replace')
    if section == section_name and stripped.startswith(key.encode('ascii') + b'='):
        seen_key = True
        if set_value is not None:
            # keep the line ending (CRLF on these INIs)
            ending = line[len(line.rstrip(b'\r\n')):]
            seeded.append(key.encode('ascii') + b'=' + set_value.encode('ascii') + ending)
        continue
    seeded.append(line)
if set_value is not None and not seen_key:
    # insert into the section (or create it, like eqclient's own writer does)
    inserted = False
    for i in range(len(seeded)):
        s = seeded[i].strip()
        if s.startswith(b'[') and s.endswith(b']') and s[1:-1].decode('ascii', errors='replace') == section_name:
            nl = b'\r\n' if b'\r\n' in original else b'\n'
            seeded.insert(i + 1, key.encode('ascii') + b'=' + set_value.encode('ascii') + nl)
            inserted = True
            break
    if not inserted:
        nl = b'\r\n' if b'\r\n' in original else b'\n'
        seeded.append(b'[' + section_name.encode('ascii') + b']' + nl)
        seeded.append(key.encode('ascii') + b'=' + set_value.encode('ascii') + nl)
source.write_bytes(b''.join(seeded))
