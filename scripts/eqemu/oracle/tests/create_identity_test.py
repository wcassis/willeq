#!/usr/bin/env python3
"""The accepted create packet must describe the selected plan identity."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from oracle_create import validate_create


def packet(cls):
    data = bytearray(80)
    for offset, value in ((0, cls), (16, 0), (20, 2)):
        struct.pack_into('<I', data, offset, value)
    return [(0, 'C->S', 'OP_CharacterCreate', 0x10b2, data)]


assert validate_create(packet(1), 'Barbarian', 'Warrior', 'Male') == (1, 0, 2)
try:
    validate_create(packet(16), 'Barbarian', 'Warrior', 'Male')
except RuntimeError as exc:
    assert 'class/gender/race' in str(exc)
else:
    raise AssertionError('Berserker packet accepted as Warrior')
print('PASS: Barbarian Warrior packet accepted; Berserker packet rejected')
