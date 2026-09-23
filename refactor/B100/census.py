#!/usr/bin/env python3
"""B100 census: pack/entry/version census + v3-aware truncation census.

Self-contained evidence for commit 76bc3a14's claim that soldunga's
gnome_stronghold_floor_transition (33 chars) was the ONLY truncated name in
the EQE1 tree.

Parse rules (per include/client/graphics/eqe1_format.h):
  - header: <I H H I I> — magic, 16-BIT version at offset 4, entry count.
  - v1/v2 index entry: 48-byte stride; name in the 32-byte field,
    null-padded, the whole name.
  - v3 index entry: 56-byte stride (48 + 8-byte {nameOffset, nameLength}
    tail). A table-resident entry zeroes its 32-byte inline field and
    carries nameOffset/nameLength; the table sits between the index and
    the data section, contiguous and NUL-free. A name <= 31 chars stays
    inline (tail zeros) even in v3.

Truncation argument: the old builder did strncpy(name, 31) into a
zero-init char[32], so a truncated name is always stored at EXACTLY 31
chars (never 30, never >31). v3 stores long names in full by
construction, so the only truncation candidates anywhere in the tree are
exactly-31-char stored names. Each is cross-checked against the PFS s3d
source archives: a source base (extension stripped) that is a strict
extension of the 31-char name and >31 long => truncated; otherwise the
name is genuine (a true 31-char source name, or a 25-char base + _alpha
companion that fits exactly).

Usage:
  python3 census.py [eqe1-tree] [pfs-source-dir]
Defaults: /mnt/projects/EverQuest_ETC1, /mnt/projects/EverQuestP1999
Exit 0 iff 0 genuinely truncated and no structural violations.
"""
import os
import re
import struct
import sys
import zlib

MAGIC = 0x31455145


def census_pack(path):
    """One pack -> (version, n_entries, table_resident, candidates, violations).

    candidates: exactly-31-char stored names (truncation candidates).
    violations: structural problems (bad stride math, bad table refs,
    v1/v2 non-null-terminated name field, ...).
    """
    d = open(path, 'rb').read()
    magic, ver, n = struct.unpack_from('<IHH', d, 0)
    if magic != MAGIC:
        return ver, n, 0, [], [f'bad magic {magic:#010x}']
    stride = 56 if ver == 3 else 48
    index_end = 16 + n * stride
    violations = []
    if index_end > len(d):
        return ver, n, 0, [], [f'index ({index_end}) exceeds file ({len(d)})']
    # data start = min dataOffset over data-carrying entries
    data_start = len(d)
    table_resident = 0
    candidates = []
    for i in range(n):
        off = 16 + i * stride
        raw = d[off:off + 32]
        w, h = struct.unpack_from('<HH', d, off + 32)
        mips, has_a, etype, pfmt = struct.unpack_from('<BBBB', d, off + 36)
        doff, dsize = struct.unpack_from('<II', d, off + 40)
        if dsize:
            data_start = min(data_start, doff)
            if doff + dsize > len(d):
                violations.append(f'entry {i}: data [{doff},{doff + dsize}) exceeds file')
        nul = raw.find(b'\x00')
        name = raw[:nul if nul >= 0 else 32].decode('latin1')
        if ver == 3:
            noff, nlen = struct.unpack_from('<II', d, off + 48)
            if nlen:  # table-resident
                if raw[0] != 0:
                    violations.append(f'entry {i}: table-ref but inline name field not zero')
                if not (index_end <= noff and noff + nlen <= data_start):
                    violations.append(f'entry {i}: table ref [{noff},{noff + nlen}) outside '
                                      f'[{index_end},{data_start})')
                else:
                    table_resident += 1
                    name = d[noff:noff + nlen].decode('latin1')
            else:  # inline; the v3 builder keeps <=31-char names inline
                if len(raw.rstrip(b'\x00')) > 31:
                    violations.append(f'entry {i}: inline name > 31 chars')
        else:
            if not raw.endswith(b'\x00'):
                # v1/v2: the whole name must fit and be null-terminated; a
                # full 32-byte field is the formal v1 truncation signature
                # (the loader now rejects this — v2 packs in the tree carry
                # null-padded fields, which is what the old builder wrote).
                if len(raw.rstrip(b'\x00')) == 32:
                    violations.append(f'entry {i}: v{ver} name field not null-terminated (32 bytes)')
        if len(name) == 31:
            candidates.append(name)
    return ver, n, table_resident, candidates, violations


def pfs_names(path):
    """File names inside a PFS archive (decompressed file table)."""
    data = open(path, 'rb').read()
    if len(data) < 8 or data[4:8] != b'PFS ':
        return []
    dir_offset = struct.unpack_from('<I', data, 0)[0]
    dir_count = struct.unpack_from('<I', data, dir_offset)[0]
    names = []
    for i in range(dir_count):
        e = dir_offset + 4 + i * 12
        crc, off, size = struct.unpack_from('<iII', data, e)
        if crc == 0x61580ac9:  # file-table CRC
            raw = data[off:off + size]
            blob = b''
            pos = 0
            while pos + 8 <= len(raw):
                dlen, _ilen = struct.unpack_from('<II', raw, pos)
                pos += 8
                if dlen > len(raw) or pos + dlen > len(raw):
                    break
                try:
                    blob += zlib.decompressobj().decompress(raw[pos:pos + dlen])
                except zlib.error:
                    break
                pos += dlen
                if pos >= len(raw):
                    break
            if len(blob) < 4:
                continue
            count = struct.unpack_from('<I', blob, 0)[0]
            pos = 4
            for _ in range(count):
                flen = struct.unpack_from('<I', blob, pos)[0]
                pos += 4
                names.append(blob[pos:pos + flen - 1].decode('latin1').lower())
                pos += flen
    return names


def main():
    tree = sys.argv[1] if len(sys.argv) > 1 else '/mnt/projects/EverQuest_ETC1'
    eq = sys.argv[2] if len(sys.argv) > 2 else '/mnt/projects/EverQuestP1999'

    packs = sorted(f for f in os.listdir(tree) if f.endswith('.eqe1'))
    total_entries = 0
    versions = {}
    v3_packs = []
    table_resident_total = 0
    candidates = []  # (pack, name)
    violations = []
    for f in packs:
        ver, n, tr, cands, v = census_pack(os.path.join(tree, f))
        total_entries += n
        versions[ver] = versions.get(ver, 0) + 1
        if ver == 3:
            v3_packs.append(f)
            table_resident_total += tr
        candidates.extend((f, c) for c in cands)
        violations.extend(v)

    print(f'pack tree: {tree}')
    print(f'PFS source dir: {eq}')
    print()
    print(f'packs: {len(packs)}')
    print(f'entries: {total_entries}')
    vprint = ', '.join(f'v{k}:{versions[k]}' for k in sorted(versions))
    print(f'version census: {{{vprint}}}')
    print(f'v3 packs: {v3_packs or "(none)"}')
    print(f'v3 table-resident names: {table_resident_total}')
    print(f'structural violations: {len(violations)}')
    for v in violations:
        print(f'  VIOLATION: {v}')

    print()
    print(f'=== truncation candidates (stored names of exactly 31 chars): '
          f'{len(candidates)} ===')
    ext = re.compile(r'\.(bmp|dds)$', re.I)
    src_cache = {}
    truncated = []
    for pack, name in candidates:
        zone = pack[:-len('.eqe1')]
        if zone not in src_cache:
            src = []
            missing = []
            for cand in (zone + '.s3d', zone + '_obj.s3d', zone + '_chr.s3d'):
                p = os.path.join(eq, cand)
                if os.path.exists(p):
                    src.extend(pfs_names(p))
                else:
                    missing.append(cand)
            src_cache[zone] = (src, missing)
        src, missing = src_cache[zone]
        base = lambda x: ext.sub('', x.lower())
        # a source whose base extends this name and is longer => truncated
        hits = [s for s in src if base(s).startswith(name) and len(base(s)) > 31]
        if hits:
            verdict = f'TRUNCATED  (source {hits[0]!r}, base len {len(base(hits[0]))})'
            truncated.append((pack, name, hits[0]))
        elif name.endswith('_alpha'):
            basebase = name[:-6]
            srcbase = [s for s in src if base(s) == basebase]
            verdict = (f'GENUINE companion  (base {basebase!r}, len {len(basebase)}'
                       f' + _alpha = 31, fits'
                       + ('; base present in source' if srcbase else '')
                       + ')')
        else:
            verdict = 'GENUINE 31-char source name'
        miss_note = f'  [source archives missing: {", ".join(missing)}]' if missing else ''
        print(f'  {pack:24s} {name!r}  ->  {verdict}{miss_note}')

    print()
    print('=== SUMMARY ===')
    print(f'genuinely truncated: {len(truncated)}')
    for pack, name, src in truncated:
        print(f'  {pack}: {name!r} <- {src!r}')
    ok = not truncated and not violations
    print(f'verdict: {"PASS — 0 genuinely truncated, no structural violations" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
