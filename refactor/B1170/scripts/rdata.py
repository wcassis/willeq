#!/usr/bin/env python3
"""B1170 (#489): read 4-byte constants (float32 / int32) at virtual addresses
of /mnt/projects/EverQuestP1999/eqgame.exe (PE sections mapped by hand).

usage: python3 -I rdata.py [--exe PATH] 0x6410b4 0x6410b8 ...
"""
import struct
import sys


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    out = []
    off = pe + 24 + optsz
    for i in range(nsec):
        name = data[off:off + 8].rstrip(b'\0').decode('latin-1')
        vsz, va, rsz, rptr = struct.unpack_from('<IIII', data, off + 8)
        out.append((name, base + va, max(vsz, rsz), rptr, rsz))
        off += 40
    return out


def read(data, secs, addr, n=4):
    for name, va, sz, rptr, rsz in secs:
        if va <= addr < va + sz:
            o = addr - va
            if o + n > rsz:
                return None, name  # bss (zero-initialised at load)
            return data[rptr + o:rptr + o + n], name
    raise SystemExit(f'{addr:#x}: not in any section')


def main(argv):
    exe = '/mnt/projects/EverQuestP1999/eqgame.exe'
    if argv[:1] == ['--exe']:
        exe, argv = argv[1], argv[2:]
    data = open(exe, 'rb').read()
    secs = sections(data)
    for a in argv:
        addr = int(a, 16)
        raw, sec = read(data, secs, addr, 8)
        if raw is None:
            print(f'{addr:#010x}\t{sec}\t(uninitialised)')
            continue
        f32 = struct.unpack('<f', raw[:4])[0]
        i32 = struct.unpack('<i', raw[:4])[0]
        f64 = struct.unpack('<d', raw)[0]
        print(f'{addr:#010x}\t{sec}\tf32={f32!r}\ti32={i32}\thex={raw[:4][::-1].hex()}\tf64={f64!r}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
