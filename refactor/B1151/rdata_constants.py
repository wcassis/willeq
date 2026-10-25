#!/usr/bin/env python3
"""Re-verify every float constant cited in refactor/B1151/TRACE.md's address
table against the raw EQGraphicsDX9.dll.

For this DLL ImageBase is 0x10000000 and the .rdata section is read-only
(characteristics 0x40000040) with roff == vaddr (RVA), so file offset == RVA
== VA - 0x10000000: `od -An -tx1 -j <rva> -N 4 <dll>` reads the value
directly. (PE section table: .rdata vaddr=0x1c5000 roff=0x1c5000 rsize=0x36000
chars=0x40000040; ImageBase=0x10000000.)

Usage:  python3 refactor/B1151/rdata_constants.py /path/to/EQGraphicsDX9.dll

One line per constant: VA, raw little-endian bytes, hex bits, decoded float.
The same values are what `od -An -tx1 -j <rva> -N 4 <dll>` prints.
"""
import struct
import sys

# (VA, meaning as cited in TRACE.md's address table)
CONSTANTS = [
    (0x101cc268, "0.15915 (1/2pi)"),
    (0x101c9954, "512.0"),
    (0x101c593c, "near-zero gate upper (+2**-23)"),
    (0x101c5940, "near-zero gate lower (-2**-23)"),
    (0x101c5944, "20000.0 (not referenced in the traced path)"),
    (0x101c5948, "1.0"),
    (0x101c594c, "0.5 (half-extent constant)"),
    (0x101c59d0, "0.25 (atlas cell width)"),
    (0x101cbd40, "0.0 (shape-7/8/9 threshold, sign threshold, N==1 frame)"),
    (0x101cbd44, "-1.0 (negate multiplier)"),
    (0x101c7410, "shuffle index scale (0x38000100 = 2**-15 + 2**-30)"),
    (0x101c6eac, "1000.0 (1000-table shuffle scale)"),
]

def main(path):
    with open(path, "rb") as f:
        data = f.read()
    print("# %s (%d bytes)" % (path, len(data)))
    for va, meaning in CONSTANTS:
        roff = va - 0x10000000  # RVA == file offset for .rdata in this DLL
        raw = data[roff:roff + 4]
        bits = struct.unpack("<I", raw)[0]
        fval = struct.unpack("<f", raw)[0]
        print("0x%08x  %s  bits=0x%08x  %g  ; %s"
              % (va, raw.hex(" "), bits, fval, meaning))

if __name__ == "__main__":
    main(sys.argv[1])
