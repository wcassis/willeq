#!/usr/bin/env python3
"""B1172 fix leg: every S->C OP_ZoneEntry (the player's own Spawn_Struct) in an
EQEmu zone packet log (T311 Leg A's zonelogs), with name @7, size f32 @75,
race u32 @284 (0x11c), gender u8 @334 (0x14e). The log dumps the payload
without its 2-byte opcode, so these are Spawn_Struct offsets.
usage: t311_zoneentry_sizes.py <T311 legA decode dir> <zone_packetlog.log> ..."""
import struct
import sys

sys.path.insert(0, sys.argv[1])
from parse_packetlog import parse  # noqa: E402  (review/T311/legA/decode/parse_packetlog.py)

for path in sys.argv[2:]:
    for r in parse(path):
        if r['op'] != 'OP_ZoneEntry' or r['dir'] != 'S->C':
            continue
        d = r['data']
        name = d[7:71].split(b'\0')[0].decode('latin-1')
        size = struct.unpack_from('<f', d, 75)[0]
        race = struct.unpack_from('<I', d, 0x11c)[0]
        print(f"{path.split('/')[-1]} {r['ts']} S->C OP_ZoneEntry name={name!r} race={race} "
              f"gender={d[0x14e]} size={size:.3f}")
