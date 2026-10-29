#!/usr/bin/env python3
"""Decode spellsnew.edd colour words for named emitter indices (1-based, as spellsnew.eff uses).
Client rule (B1141/B1146): +0x68 additive; +0x90/0x94 fade in/out; +0xA4 peak;
+0xD4..DC start R,G,B; +0xE0..E8 end R,G,B (per-channel lerp by age/lifetime).
"""
import struct, sys

path = '/mnt/projects/EverQuestP1999/spellsnew.edd'
data = open(path, 'rb').read()
assert data[:8] == b'EDD\x00110\x00', data[:8]
n = (len(data) - 8) // 416
print('entries:', n, 'file bytes:', len(data))

def entry(i1):  # 1-based
    off = 8 + (i1 - 1) * 416
    return data[off:off + 416]

def show(i1):
    e = entry(i1)
    def i32(o): return struct.unpack_from('<i', e, o)[0]
    def f32(o): return struct.unpack_from('<f', e, o)[0]
    name = e[0:32].split(b'\x00')[0].decode(errors='replace')
    tex = e[0x40:0x40+32].split(b'\x00')[0].decode(errors='replace')
    additive = i32(0x68)
    fi, fo, peak = f32(0x90), f32(0x94), f32(0xA4)
    s = [i32(0xD4 + 4*k) for k in range(3)]
    en = [i32(0xE0 + 4*k) for k in range(3)]
    print(f'emitter {i1:4d}  {name:28s} tex={tex:16s} additive={additive} fadeIn={fi:.2f} fadeOut={fo:.2f} peak={peak:.3f}')
    print(f'            start RGB = {s}   end RGB = {en}')

for i in [280, 281, 282, 9]:
    show(i)
