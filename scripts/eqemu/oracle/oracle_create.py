"""Validate the Titanium OP_CharacterCreate identity against a plan row."""
import struct


RACES = dict(zip(
    ('Human', 'Barbarian', 'Erudite', 'WoodElf', 'HighElf', 'DarkElf',
     'HalfElf', 'Dwarf', 'Troll', 'Ogre', 'Halfling', 'Gnome', 'Iksar'),
    (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128)))
CLASSES = dict(zip(
    ('Warrior', 'Cleric', 'Paladin', 'Ranger', 'ShadowKnight', 'Druid',
     'Monk', 'Bard', 'Rogue', 'Shaman', 'Necromancer', 'Wizard',
     'Magician', 'Enchanter'), range(1, 15)))
GENDERS = {'Male': 0, 'Female': 1}


def validate_create(packets, race, klass, gender):
    """Require exactly one C->S 80-byte packet with the requested identity."""
    expected = (CLASSES[klass], GENDERS[gender], RACES[race])
    creates = [(direction, payload) for _, direction, opcode, _, payload in packets
               if opcode == 'OP_CharacterCreate']
    if len(creates) != 1:
        raise RuntimeError(f'expected one OP_CharacterCreate, got {len(creates)}')
    direction, payload = creates[0]
    if direction != 'C->S' or len(payload) != 80:
        raise RuntimeError(f'bad create direction/size: {direction}, {len(payload)}')
    actual = struct.unpack_from('<I', payload, 0)[0], \
        struct.unpack_from('<I', payload, 16)[0], \
        struct.unpack_from('<I', payload, 20)[0]
    if actual != expected:
        raise RuntimeError(f'create class/gender/race {actual} != requested {expected}')
    return actual
