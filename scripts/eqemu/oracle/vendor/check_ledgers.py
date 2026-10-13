#!/usr/bin/env python3
"""Session-dir adaptation of the UIGAP check_ledgers.py consistency pass."""
import csv
from pathlib import Path
import sys

out = Path(sys.argv[1])
rows = list(csv.DictReader((out / 'steps.tsv').open(), delimiter='\t'))
marks = {(parts[1], parts[2]) for line in (out / 'timeline.tsv').read_text().splitlines()
         if len(parts := line.split('\t')) == 3}
summary = (out / 'summary.md').read_text()
broken = []
for row in rows:
    step = row['step']
    if ('BEGIN', step) not in marks or ('END', step) not in marks:
        broken.append(f'{step}: missing BEGIN/END')
    for field in ('still', 'frame', 'opcodes', 'ini_diff'):
        name = row[field]
        if not (out / name).is_file() or name not in summary:
            broken.append(f'{step}: missing or uncited {name}')
    opcode_file = out / row['opcodes']
    if opcode_file.is_file():
        count = len(opcode_file.read_text().splitlines()) - 1
        if count != int(row['packet_rows']):
            broken.append(f'{step}: packet count differs from {row["opcodes"]}')
    if step not in summary:
        broken.append(f'{step}: not cited in summary')
for name in ('capture.pcap', 'video.mp4', 'contact.png', 'steps.tsv'):
    if not (out / name).is_file() or name not in summary:
        broken.append(f'missing or uncited {name}')
if (out / 'character-after.ini').is_file():
    name = 'character-final.ini.diff'
    if not (out / name).is_file() or name not in summary:
        broken.append(f'missing or uncited {name}')
for item in broken:
    print(item)
print(f'BROKEN REFERENCES: {len(broken)}')
raise SystemExit(1 if broken else 0)
