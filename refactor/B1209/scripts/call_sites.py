# B1209 (#528): every call site of the two P13/P14 addresses (and of the
# dispatcher that owns 0x491cff's only call) in the decompile, with the
# enclosing function. Committed so the README's cite-per-row claim regenerates.
#
# Re-run:
#   python3 refactor/B1209/scripts/call_sites.py
# (reads /mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c; writes
#  refactor/B1209/tables/call_sites.tsv)

import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DECOMPILE = "/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c"
OUT = os.path.join(ROOT, "refactor", "B1209", "tables", "call_sites.tsv")

TARGETS = ("FUN_0044d0d0(", "FUN_00491cff(", "FUN_0049b260(")
FUNC_RE = re.compile(r"^// FUNC ([0-9a-f]+) (\S+)$")
SIG_ARG = r"\(\s*(int|undefined4|char|bool|void|uint|undefined1|float)\b"


def is_call_site(line, tok):
    name = tok[:-1]
    # a signature, one-line: return type at col 0 then the name
    if re.match(r"^(undefined4|bool|char|int|void|uint|undefined1)\s*\**\s*"
                + re.escape(name), line):
        return False
    # a signature, split over two lines: the name at col 0 and a C type as
    # the first parameter
    if re.match(r"^" + re.escape(name) + SIG_ARG, line):
        return False
    return True


def count_calls(tok):
    n = 0
    for line in open(DECOMPILE, errors="replace"):
        if tok in line and is_call_site(line, tok):
            n += 1
    return n


def main():
    rows = []
    cur_addr, cur_name = "-", "-"
    for lineno, line in enumerate(open(DECOMPILE, errors="replace"), 1):
        m = FUNC_RE.match(line)
        if m:
            cur_addr, cur_name = m.group(1), m.group(2)
            continue
        for tok in TARGETS:
            if tok in line and is_call_site(line, tok):
                rows.append((tok[:-1], cur_name, cur_addr, lineno, line.strip()))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("callee\tenclosing\tenclosing_addr\tline\ttext\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    for t in TARGETS:
        print(t, count_calls(t), "call sites")
    print("wrote", OUT, len(rows), "rows")


if __name__ == "__main__":
    main()
