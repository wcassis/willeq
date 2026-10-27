#!/usr/bin/env python3
"""B1172: read 32-bit floats / dwords / C strings at virtual addresses of a PE image
(stdlib only: section table walk).
usage: pe_data.py <pe> f|d|s <va_hex> [<va_hex> ...]"""
import struct, sys


def load(path):
    b = open(path, 'rb').read()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    optsz = struct.unpack_from('<H', b, pe + 20)[0]
    base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
    secs = []
    o = pe + 24 + optsz
    for i in range(nsec):
        name = b[o:o + 8].rstrip(b'\0').decode()
        vsz, va, rsz, raw = struct.unpack_from('<IIII', b, o + 8)
        secs.append((name, base + va, max(vsz, rsz), raw, rsz))
        o += 40
    return b, secs


def read(b, secs, va, n):
    for name, sva, sz, raw, rsz in secs:
        if sva <= va < sva + sz:
            off = va - sva
            if off + n > rsz:
                return b'\0' * n  # uninitialised (.bss): zero at load
            return b[raw + off: raw + off + n]
    raise SystemExit(f'{va:#x} not in any section')


if __name__ == '__main__':
    b, secs = load(sys.argv[1])
    kind = sys.argv[2]
    for a in sys.argv[3:]:
        va = int(a, 16)
        if kind == 'f':
            raw = read(b, secs, va, 4)
            print(f'{va:08x} float {struct.unpack("<f", raw)[0]!r} (0x{struct.unpack("<I", raw)[0]:08x})')
        elif kind == 'd':
            raw = read(b, secs, va, 8)
            print(f'{va:08x} double {struct.unpack("<d", raw)[0]!r}')
        else:
            raw = read(b, secs, va, 128)
            print(f'{va:08x} str {raw.split(bytes(1))[0].decode("latin-1")!r}')
