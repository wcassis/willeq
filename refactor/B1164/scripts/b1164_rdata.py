#!/usr/bin/env python3
"""B1164 (#483): print the 32-bit floats at virtual addresses of a PE image
(eqgame.exe .rdata constants used by the actor-scale rule).
usage: python3 -I b1164_rdata.py <eqgame.exe> <va_hex> [<va_hex> ...]"""
import struct
import sys


def sections(b):
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec, = struct.unpack_from('<H', b, pe + 6)
    optsz, = struct.unpack_from('<H', b, pe + 20)
    base, = struct.unpack_from('<I', b, pe + 24 + 28)
    off = pe + 24 + optsz
    for _ in range(nsec):
        name = b[off:off + 8].rstrip(b'\0').decode()
        vsz, va, rsz, raw = struct.unpack_from('<IIII', b, off + 8)
        yield name, base + va, vsz, rsz, raw
        off += 40


def main():
    b = open(sys.argv[1], 'rb').read()
    secs = list(sections(b))
    for arg in sys.argv[2:]:
        va = int(arg, 16)
        for name, sva, vsz, rsz, raw in secs:
            if sva <= va < sva + max(vsz, rsz):
                o = va - sva
                if o + 4 > rsz:
                    print(f'{va:#010x} {name} uninitialised (0 at load)')
                else:
                    w, = struct.unpack_from('<I', b, raw + o)
                    f, = struct.unpack_from('<f', b, raw + o)
                    print(f'{va:#010x} {name} 0x{w:08x} float {f!r}')
                break
        else:
            print(f'{va:#010x} not in any section')


if __name__ == '__main__':
    main()
