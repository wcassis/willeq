#!/usr/bin/env python3
"""B1168: check every bare 8-digit address in a README against a Ghidra .dis.

usage: python3 -I check_addrs.py <README.md> <file.dis> [<file.dis> ...]

- every token `00xxxxxx` (8 hex digits, starting 004/0049..., i.e. an image VA
  written without 0x) must be an instruction start in one of the .dis files;
- every `ADDR  instruction` pair (in a code block or inline backticks, the
  address followed by the instruction text up to `;` or the closing backtick)
  must match the .dis instruction text exactly (whitespace-normalised).
Prints each problem and a summary line; exit 1 on any problem.
"""
import re
import sys


def main():
    readme, dis = sys.argv[1], sys.argv[2:]
    ins = {}
    for d in dis:
        for line in open(d, errors="replace"):
            t = line.rstrip("\n").split(None, 1)
            if t and re.fullmatch(r"[0-9a-f]{8}", t[0]):
                ins[t[0]] = " ".join(t[1].split()) if len(t) > 1 else ""
    text = open(readme).read()
    bad = 0
    bare = set(re.findall(r"(?<![0-9a-fx])(00[45][0-9a-f]{5})(?![0-9a-f])", text))
    for a in sorted(bare):
        if a not in ins:
            print("NOT AN INSTRUCTION START", a)
            bad += 1
    pairs = 0
    for m in re.finditer(r"(?<![0-9a-fx])(00[45][0-9a-f]{5})  ?([a-z][^;`\n]*)", text):
        a, quoted = m.group(1), " ".join(m.group(2).split())
        if a not in ins:
            continue
        if not quoted or quoted.split()[0] not in ins[a].split()[0:1]:
            continue  # prose after a bare address, not a quoted instruction
        pairs += 1
        if quoted != ins[a]:
            print("MISMATCH %s: quoted '%s' | dis '%s'" % (a, quoted, ins[a]))
            bad += 1
    print("checked %d bare addresses, %d quoted instructions, %d problems" % (len(bare), pairs, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
