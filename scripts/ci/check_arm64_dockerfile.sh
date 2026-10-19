#!/usr/bin/env bash
# B1030 (#350): docker/Dockerfile.arm64-noble is docker/Dockerfile.arm-noble with the
# architecture swapped -- and NOTHING else. The old arm64 file (52cb54d1) drifted for
# three months (SourceForge wgets after B970, missing probe sources, stale target list)
# because nothing compared them. This script is the comparison: it regenerates the arm64
# file from the armhf one through the substitution table below and fails if the checked-in
# file differs. Run by the `hygiene` ctest label (tests/cmake, DockerfileArm64Derived).
#
#   scripts/ci/check_arm64_dockerfile.sh          check (exit 1 on drift, prints the diff)
#   scripts/ci/check_arm64_dockerfile.sh --write  regenerate docker/Dockerfile.arm64-noble
#
# The header comment block (everything before the first FROM) is the arm64 file's own and
# is kept verbatim from the checked-in file; the table applies to the rest.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/docker/Dockerfile.arm-noble"
DST="$ROOT/docker/Dockerfile.arm64-noble"
MODE="${1:-}"
python3 - "$SRC" "$DST" "$MODE" <<'PY'
import sys, difflib
src, dst, mode = sys.argv[1], sys.argv[2], sys.argv[3]
SUBS = [
    ('gcc-arm-linux-gnueabihf', 'gcc-aarch64-linux-gnu'),
    ('g++-arm-linux-gnueabihf', 'g++-aarch64-linux-gnu'),
    ('--arch=armhf', '--arch=arm64'),
    ('Bootstrap Noble armhf sysroot', 'Bootstrap Noble arm64 sysroot'),
    ('arm-linux-gnueabihf', 'aarch64-linux-gnu'),
    ('-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard', '-march=armv8-a'),
    ('-DCMAKE_SYSTEM_PROCESSOR=arm ', '-DCMAKE_SYSTEM_PROCESSOR=aarch64 '),
    ('NEON intrinsics on Cortex-A7',
     'NEON intrinsics (NEON is baseline on ARMv8; libpng still needs the arm/ objects)'),
]
MARK = 'FROM ubuntu:24.04 AS base'
s = open(src).read()
body = s[s.index(MARK):]
for a, b in SUBS:
    if a not in body:
        sys.exit(f"check_arm64_dockerfile: substitution source {a!r} no longer in {src}; update the table")
    body = body.replace(a, b)
try:
    cur = open(dst).read()
    header = cur[:cur.index(MARK)]
except (FileNotFoundError, ValueError):
    cur, header = '', '# (header missing -- write one)\n\n'
want = header + body
if mode == '--write':
    open(dst, 'w').write(want)
    print(f"wrote {dst}")
    sys.exit(0)
if cur == want:
    print("OK: docker/Dockerfile.arm64-noble is docker/Dockerfile.arm-noble with the arch swapped")
    sys.exit(0)
sys.stdout.writelines(difflib.unified_diff(want.splitlines(True), cur.splitlines(True),
                                           'generated-from-arm-noble', 'docker/Dockerfile.arm64-noble'))
sys.exit("DRIFT: docker/Dockerfile.arm64-noble != arch-swapped docker/Dockerfile.arm-noble "
         "(edit the armhf file, then scripts/ci/check_arm64_dockerfile.sh --write)")
PY
