#!/usr/bin/env python3
"""B1156: cite_check.py <doc.md> [--tree DIR] [--extra DIR ...] [--dis FILE ...]

The worker-side companion of scripts/refactor/check_doc_citations.py (K00/B1020,
which hygiene runs over the live docs and the ANCHORED `file:line (anchor)` cites
in refactor/B*/). This one is run by hand on ONE document before a hand-back and
as the first step of a cold review, and it also checks the unanchored cites the
local workers write, `sed -n` ranges, and disassembly addresses. Wave retro
2026-10-25/26: 0 of 9 FX MRs passed cold review first time and ~70 % of the
findings were this class (right mechanism, wrong line/count/address).

Usage (willeq, from the worktree root):
  python3 -I scripts/refactor/cite_check.py refactor/B1153/README.md --tree . \
      --extra /mnt/projects/fable \
      --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis \
      --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis
Paste the final `checked N cites, P problems, W address warnings` line in the
hand-off; P must be 0. A reviewer treats every `problem` line as a finding.

Checks every file:line cite, every 0x address and every `sed -n 'A,Bp' FILE` in a README:
  file:line  -> the file exists in the tree and the line is inside it; prints the line so the
                author can confirm the named symbol is really there (a bare line number can't be).
  file:line (`symbol`) -> if a backticked identifier sits within 60 chars of the cite, the cited
                line (+-1) must contain it.
  0x1xxxxxxx / 0x4xxxxx -> must be an instruction boundary in at least one --dis file.
Exit 1 on a file/line/symbol problem; address warnings are advisory (data words and range ends are legitimate). No network, no writes."""
import re, sys, os, pathlib
args=sys.argv[1:]; tree='.'; dis=[]; readme=None; extra=[]
i=0
while i<len(args):
    a=args[i]
    if a=='--tree': tree=args[i+1]; i+=2
    elif a=='--dis': dis.append(args[i+1]); i+=2
    elif a=='--extra': extra.append(args[i+1]); i+=2
    else: readme=a; i+=1
text=open(readme).read()
bad=0; warn=0; checked=0
import subprocess
try: tracked=subprocess.run(['git','-C',tree,'ls-files'],capture_output=True,text=True).stdout.split()
except Exception: tracked=[]
by_base={}
for t in tracked: by_base.setdefault(os.path.basename(t),[]).append(t)
def resolve(f):
    if (pathlib.Path(tree)/f).exists(): return f
    for e in extra:
        if (pathlib.Path(e)/f).exists(): return os.path.relpath(pathlib.Path(e)/f, tree)
    c=by_base.get(os.path.basename(f),[]) if '/' not in f else [t for t in tracked if t.endswith('/'+f)]
    return c[0] if len(c)==1 else None
# file:line cites (paths with a slash or a known extension)
for m in re.finditer(r'`?([\w./-]+\.(?:cpp|h|c|py|sh|tsv|md|json|txt)):(\d+)(?:-(\d+))?`?', text):
    f, a, b = m.group(1), int(m.group(2)), int(m.group(3) or m.group(2))
    checked+=1
    r=resolve(f)
    if r is None:
        print(f'MISSING FILE  {f}:{a} (not in the tree, or an ambiguous basename)'); bad+=1; continue
    p=pathlib.Path(tree)/r
    lines=p.read_text(errors='replace').splitlines()
    if b>len(lines):
        print(f'LINE PAST EOF {f}:{a}-{b} (file has {len(lines)})'); bad+=1; continue
    ctx=text[max(0,m.start()-70):m.start()]
    syms=[x for x in re.findall(r'`([A-Za-z_][A-Za-z0-9_:]{5,})`', ctx) if x.lower() not in ('return','static','struct')]
    sym=re.match(r'(.*)', syms[-1]) if syms else None
    if sym and not any(sym.group(1).split('::')[-1] in lines[k-1] for k in range(max(1,a-1), min(len(lines),b+1)+1)):
        print(f'SYMBOL NOT AT {f}:{a}-{b}: `{sym.group(1)}` | line {a}: {lines[a-1].strip()[:90]}'); bad+=1
# sed -n 'A,Bp' FILE
for m in re.finditer(r"sed -n '?(\d+),(\d+)p'? ([\w./-]+)", text):
    a,b,f=int(m.group(1)),int(m.group(2)),m.group(3); r=resolve(f); checked+=1
    if r is None: print(f'MISSING FILE  {f} (sed)'); bad+=1; continue
    p=pathlib.Path(tree)/r
    if False: pass
    elif b>len(p.read_text(errors='replace').splitlines()): print(f'LINE PAST EOF {f}:{a}-{b} (sed)'); bad+=1
# addresses
if dis:
    addrs=set()
    for d in dis:
        for line in open(d, errors='replace'):
            t=line.split(None,1)
            if t and re.fullmatch(r'[0-9a-f]{8}', t[0]): addrs.add(t[0])
    for m in re.finditer(r'0x(1[0-9a-f]{7}|4[0-9a-f]{5})\b', text):
        checked+=1
        if m.group(1).zfill(8) not in addrs: print(f'WARN not an instruction boundary: 0x{m.group(1)} (a data word, vtable or range end is fine; a code cite is not)'); warn+=1
print(f'checked {checked} cites, {bad} problems, {warn} address warnings')
sys.exit(1 if bad else 0)
