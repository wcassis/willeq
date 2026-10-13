#!/usr/bin/env python3
"""Mechanical, cited report for one official-client plan; no inferred observations."""
import csv
import datetime as dt
import difflib
import importlib.util
from pathlib import Path
import subprocess
import sys


out, plan, repo = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
vendor = repo / 'scripts/eqemu/oracle/vendor'
spec = importlib.util.spec_from_file_location('eqpcap', vendor / 'eqpcap.py')
eqpcap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eqpcap)
actions = [r for r in csv.reader(plan.open(), delimiter='\t') if r and not r[0].startswith('#')]
marks = {}
for row in csv.reader((out / 'timeline.tsv').open(), delimiter='\t'):
    if len(row) == 3:
        marks.setdefault(row[2], {})[row[1]] = float(row[0])
t0 = float((out / 'video.mp4.t0').read_text())
decoded = eqpcap.decode(out / 'capture.pcap')
with (out / 'all-opcodes.tsv').open('w') as stream:
    stream.write('utc\tdirection\topcode\tcode\tsize\tfields\n')
    for t, direction, sport, cport, name, code, payload in decoded:
        time = dt.datetime.fromtimestamp(t, dt.timezone.utc).isoformat(timespec='milliseconds')
        stream.write(f'{time}\t{direction}\t{name}\t0x{code:04x}\t{len(payload)}\t{eqpcap.fields(name, payload)}\n')

step_rows = []
summary = ['# Official-client oracle session', '',
           'Every count below is derived from a cited step file, not from visual inference.', '',
           '| Step | Action | Still | Video frame/contact | Decoded packets | INI diff |',
           '| --- | --- | --- | --- | --- | --- |']
stepper = []
previous_ini = out / 'before-character.ini'
for index, row in enumerate(actions, 1):
    verb, target, hold_ms, label = (row + [''] * 4)[:4]
    name = f'{index:02d}_{label or verb}'
    window = marks.get(name, {})
    if 'BEGIN' not in window or 'END' not in window:
        raise RuntimeError(f'uncaptured step {name}: {window}')
    begin, end = window['BEGIN'], window['END']
    packets = [(t, direction, op, code, payload) for t, direction, _, _, op, code, payload in decoded if begin <= t <= end]
    if verb == 'zone' and not packets:
        raise RuntimeError(f'{name}: no decoded zone-entry packets; refusing a false success')
    if verb == 'create' and not any(op == 'OP_CharacterCreate' for _, _, op, _, _ in packets):
        raise RuntimeError(f'{name}: missing OP_CharacterCreate; refusing a false success')
    packet_file = out / f'{name}.opcodes.tsv'
    with packet_file.open('w') as stream:
        stream.write('utc\tdirection\topcode\tcode\tsize\tfields\n')
        for t, direction, op, code, payload in packets:
            time = dt.datetime.fromtimestamp(t, dt.timezone.utc).isoformat(timespec='milliseconds')
            stream.write(f'{time}\t{direction}\t{op}\t0x{code:04x}\t{len(payload)}\t{eqpcap.fields(op, payload)}\n')
    still = out / f'{name}.png'
    if not still.is_file():
        raise RuntimeError(f'missing still: {still}')
    ini = out / f'{name}.ini'
    diff_file = out / f'{name}.ini.diff'
    if ini.is_file() and previous_ini.is_file():
        diff = difflib.unified_diff(previous_ini.read_text(errors='replace').splitlines(True),
                                    ini.read_text(errors='replace').splitlines(True),
                                    fromfile=previous_ini.name, tofile=ini.name)
        diff_file.write_text(''.join(diff))
        previous_ini = ini
    else:
        diff_file.write_text('INI unavailable at this step\n')
    frame = out / f'{name}.frame.png'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', f'{max(0, end-t0):.3f}',
                    '-i', str(out / 'video.mp4'), '-frames:v', '1', str(frame)], check=True)
    step_rows.append((name, f'{begin:.3f}', f'{end:.3f}', verb, target, still.name,
                      frame.name, packet_file.name, diff_file.name, str(len(packets))))
    summary.append(f'| {name} | {verb} {target} | [{still.name}]({still.name}) | '
                   f'[{frame.name}]({frame.name}) | [{packet_file.name}]({packet_file.name}): '
                   f'{len(packets)} data rows | [{diff_file.name}]({diff_file.name}) |')
    if verb not in ('login', 'zone', 'wait') and end - begin < 15:
        stepper.extend([(begin, 'BEGIN', name), (end, 'END', name)])

with (out / 'steps.tsv').open('w') as stream:
    writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
    writer.writerow(('step', 'begin_epoch', 'end_epoch', 'action', 'target', 'still',
                     'frame', 'opcodes', 'ini_diff', 'packet_rows'))
    writer.writerows(step_rows)

# Feed the existing video stepper only short marked actions; avoid decoding
# minutes of login frames into memory. The contact sheet includes every step.
base = out / 'marked'
(out / 'marked.mp4.geom').write_text('0 30 1280 800 (window origin 0,30)\n')
(out / 'marked.timeline.tsv').write_text(''.join(
    f'00:00:00\t{when-t0:.3f}\t640,400\t{kind} {name}\n'
    for when, kind, name in stepper))
marked = out / 'marked.mp4'
if marked.is_symlink():
    if marked.readlink() != Path('video.mp4'):
        raise RuntimeError(f'unexpected marked video link: {marked}')
else:
    marked.symlink_to('video.mp4')
if stepper:
    with (out / 'step_frames.tsv').open('w') as stream:
        subprocess.run([sys.executable, str(vendor / 'step_frames.py'), str(base)],
                       stdout=stream, check=True)
    summary += ['', f'[Video step analysis](step_frames.tsv) uses the existing step_frames.py.']
subprocess.run(['montage', *[str(out / r[6]) for r in step_rows], '-tile', '4x',
                '-geometry', '320x200+2+2', str(out / 'contact.png')], check=True)
summary += ['', '[All-step contact sheet](contact.png)', '',
            f'Raw capture: [capture.pcap](capture.pcap); video: [video.mp4](video.mp4).',
            'Packet totals and per-step timestamps: [steps.tsv](steps.tsv).', '']
final_ini = out / 'character-after.ini'
if final_ini.is_file() and (out / 'before-character.ini').is_file():
    final_diff = out / 'character-final.ini.diff'
    final_diff.write_text(''.join(difflib.unified_diff(
        (out / 'before-character.ini').read_text(errors='replace').splitlines(True),
        final_ini.read_text(errors='replace').splitlines(True),
        fromfile='before-character.ini', tofile=final_ini.name)))
    summary += [f'[Final character INI diff]({final_diff.name}) is the post-session '
                'value; no earlier step is credited with a change absent from its own diff.', '']
(out / 'summary.md').write_text('\n'.join(summary))
with (out / 'ledger_check.txt').open('w') as stream:
    subprocess.run([sys.executable, str(vendor / 'check_ledgers.py'), str(out)],
                   stdout=stream, check=True)
