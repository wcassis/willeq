#!/usr/bin/env python3
"""B1164 (#483) dev5: decode a model's root bone track from an old-world .s3d (PFS archive ->
<name>.wld), the track EQGraphicsDX9 0x10046830 binds SPELLPOINT_DEFAULT to (the model tag with
no suffix). Prints each <TAG>_TRACKDEF (WLD fragment 0x12) frame: the int16 words
(rot_denominator, rot_x, rot_y, rot_z, shift_x, shift_y, shift_z, shift_denominator; flag 0x8)
and |q| / 16384 -- a unit quaternion contributes no scale to the track's world matrix.
usage: b1164_root_track.py <archive.s3d> <TAG> [TAG ...]   (e.g. global_chr.s3d HUM)"""
import struct, sys, zlib


def pfs_files(path):
    d = open(path, 'rb').read()
    diroff = struct.unpack_from('<I', d, 0)[0]
    n = struct.unpack_from('<I', d, diroff)[0]
    ents = [struct.unpack_from('<III', d, diroff + 4 + 12 * i) for i in range(n)]

    def read(off, size):
        out, p = b'', off
        while len(out) < size:
            dl, il = struct.unpack_from('<II', d, p)
            out += zlib.decompress(d[p + 8:p + 8 + dl])
            p += 8 + dl
        return out
    names, files = [], []
    for crc, off, size in sorted(ents, key=lambda e: e[1]):
        data = read(off, size)
        if crc == 0x61580AC9:  # the filename directory
            c, p = struct.unpack_from('<I', data, 0)[0], 4
            for _ in range(c):
                ln = struct.unpack_from('<I', data, p)[0]
                names.append(data[p + 4:p + 4 + ln].rstrip(b'\0').decode('latin1'))
                p += 4 + ln
        else:
            files.append(data)
    return dict(zip(names, files))


KEY = bytes([0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A])  # the WLD string-hash XOR key
arc, tags = sys.argv[1], sys.argv[2:]
for name, data in pfs_files(arc).items():
    if not name.endswith('.wld'):
        continue
    _, ver, nfrag, _, _, hs, _ = struct.unpack_from('<7I', data, 0)
    sh = bytes(b ^ KEY[i % 8] for i, b in enumerate(data[28:28 + hs]))
    p = 28 + hs
    for _ in range(nfrag):
        size, typ = struct.unpack_from('<II', data, p)
        ref = struct.unpack_from('<i', data, p + 8)[0]
        body = data[p + 8:p + 8 + size]
        p += 8 + size
        if typ != 0x12 or ref >= 0:
            continue
        fname = sh[-ref:sh.index(b'\0', -ref)].decode('latin1')
        if fname not in [t + '_TRACKDEF' for t in tags]:
            continue
        flags, cnt = struct.unpack_from('<II', body, 4)
        print(f'{arc} {name} {fname} flags 0x{flags:x} frames {cnt}')
        q = 12
        for i in range(cnt):
            if flags & 8:
                v = struct.unpack_from('<8h', body, q); q += 16
                mag = sum(x * x for x in v[:4]) ** 0.5 / 16384
                print(f'  frame {i}: {v} |q|/16384 = {mag:.6f}')
            else:
                v = struct.unpack_from('<8f', body, q); q += 32
                print(f'  frame {i}: {v} (float frame)')
