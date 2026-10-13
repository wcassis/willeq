#!/usr/bin/env python3
"""eqpcap.py — decode a Titanium EQStream pcap into application packets.

Usage: eqpcap.py <capture.pcap> [--all] [--marks timeline.tsv] [--hex]

Prints one line per APPLICATION packet:
  <utc hh:mm:ss.mmm>  <C->S|S->C>  <port>  <OP_Name>  <0xOPCODE>  <len>  <decoded fields>

Opcode names come from the eqemu Titanium patch table
(/mnt/projects/akk-stack/code/utils/patches/patch_Titanium.conf) — i.e. the
names are mechanical, not guessed. Slot numbers in decoded fields are the
CLIENT's (Titanium) numbering as they appear on the wire, not eqemu's internal
server numbering.

Protocol handled (EQStream, as used by Titanium / eqemu):
  session ops 0x01/0x02 (session request/response, read the CRC/compress
  parameters), 0x03 combined, 0x09 sequenced packet, 0x0d fragment,
  0x19 app-combined, 0x15/0x11 acks (ignored), 0x06 keepalive (ignored);
  zlib compression flag 0x5a / 0xa5; 2-byte CRC trailer.
Capture on ONE interface (lo) so each datagram is seen once.
"""
import struct, sys, zlib, datetime, re
from pathlib import Path

CONF = Path(__file__).with_name('patch_Titanium.conf')
OPN = {}
for line in open(CONF, errors='replace'):
    m = re.match(r'\s*(OP_\w+)\s*=\s*0x([0-9a-fA-F]+)', line)
    if m:
        v = int(m.group(2), 16)
        if v:
            OPN.setdefault(v, m.group(1))

SERVER_PORTS = set([5998, 5999, 9000, 9001] + list(range(7000, 7031)))
NOISE = {'OP_ClientUpdate', 'OP_HPUpdate', 'OP_ManaChange', 'OP_MobHealth',
         'OP_TimeOfDay', 'OP_Weather', 'OP_SpawnAppearance', 'OP_Animation',
         'OP_ExpUpdate', 'OP_Stamina', 'OP_ManaUpdate', 'OP_EnduranceUpdate',
         'OP_SendExpZonein', 'OP_WearChange', 'OP_MobUpdate', 'OP_NpcMoveUpdate',
         'OP_FloatListThing', 'OP_Emote', 'OP_FormattedMessage__noise'}


def pcap_records(path):
    f = open(path, 'rb')
    gh = f.read(24)
    magic = struct.unpack('<I', gh[:4])[0]
    endian = '<' if magic in (0xa1b2c3d4, 0xa1b23c4d) else '>'
    nano = magic in (0xa1b23c4d, 0x4d3cb2a1)
    linktype = struct.unpack(endian + 'I', gh[20:24])[0]
    while True:
        h = f.read(16)
        if len(h) < 16:
            break
        ts, tf, incl, orig = struct.unpack(endian + 'IIII', h)
        data = f.read(incl)
        t = ts + tf / (1e9 if nano else 1e6)
        yield t, linktype, data


def ip_udp(linktype, d):
    if linktype == 1:      # ethernet
        off = 14
    elif linktype == 113:  # linux SLL
        off = 16
    elif linktype == 276:  # linux SLL2
        off = 20
    elif linktype == 0:    # null/loopback
        off = 4
    else:
        off = 14
    ip = d[off:]
    if len(ip) < 20 or ip[0] >> 4 != 4 or ip[9] != 17:
        return None
    ihl = (ip[0] & 15) * 4
    src = '.'.join(map(str, ip[12:16])); dst = '.'.join(map(str, ip[16:20]))
    sp, dp, ln = struct.unpack('>HHH', ip[ihl:ihl + 6])
    return src, sp, dst, dp, ip[ihl + 8: ihl + ln]


class Stream:
    def __init__(self):
        self.crc = 2
        self.compressed = True
        self.frag = None
        self.frag_total = 0


def strip(st, pkt):
    """remove CRC and decompress; returns protocol packet (2-byte op + body)"""
    if len(pkt) < 2:
        return pkt
    if pkt[0] == 0 and pkt[1] in (1, 2, 5):      # session req/resp/disconnect: no crc/compress (resp handled)
        return pkt
    body = pkt[:-st.crc] if st.crc else pkt
    if pkt[0] == 0 and st.compressed and len(body) > 2:
        flag = body[2]
        if flag == 0x5a:
            try:
                body = body[:2] + zlib.decompress(body[3:])
            except Exception:
                return None
        elif flag == 0xa5:
            body = body[:2] + body[3:]
    elif pkt[0] != 0 and st.compressed and len(body) > 1:
        # raw (unsequenced) app packet: the compression flag sits after the
        # FIRST byte (measured: "cb a5 14 ..." = OP_ClientUpdate 0x14cb)
        flag = body[1]
        if flag == 0x5a:
            try:
                body = body[:1] + zlib.decompress(body[2:])
            except Exception:
                return None
        elif flag == 0xa5:
            body = body[:1] + body[2:]
    return body


def app_from(data):
    """application packet bytes -> (opcode, payload). Titanium app opcodes are
    2 bytes LE; an opcode whose low byte is 0 is sent with an extra 0 byte."""
    if len(data) < 2:
        return None
    if data[0] == 0:
        op = struct.unpack('<H', data[1:3])[0]
        return op, data[3:]
    op = struct.unpack('<H', data[0:2])[0]
    return op, data[2:]


def proto(st, p, out):
    """p = protocol packet starting with 00 xx"""
    if len(p) < 2:
        return
    if p[0] != 0:
        a = app_from(p)
        if a:
            out.append(a)
        return
    op = p[1]
    if op == 0x02 and len(p) >= 2 + 4 + 4 + 1 + 1 + 1 + 4:
        # SessionResponse: session u32, key u32, crc_length u8, format u8, unknown u8, maxlen u32
        crc_len = p[10]; fmt = p[11]
        st.crc = crc_len
        st.compressed = bool(fmt & 0x01)
    elif op == 0x03:  # combined
        i = 2
        while i < len(p):
            ln = p[i]; i += 1
            proto(st, p[i:i + ln], out); i += ln
    elif op == 0x19:  # app combined
        i = 2
        while i < len(p):
            ln = p[i]; i += 1
            if ln == 0xff:
                ln = struct.unpack('>H', p[i:i + 2])[0]; i += 2
            a = app_from(p[i:i + ln]); i += ln
            if a:
                out.append(a)
    elif op == 0x09:  # sequenced packet
        data = p[4:]
        if len(data) >= 2 and data[0] == 0 and data[1] == 0x19:
            proto(st, data, out)
        else:
            a = app_from(data)
            if a:
                out.append(a)
    elif op == 0x0d:  # fragment
        data = p[4:]
        if st.frag is None:
            st.frag_total = struct.unpack('>I', data[:4])[0]
            st.frag = bytearray(data[4:])
        else:
            st.frag += data
        if len(st.frag) >= st.frag_total:
            full = bytes(st.frag); st.frag = None
            if len(full) >= 2 and full[0] == 0 and full[1] == 0x19:
                proto(st, full, out)
            else:
                a = app_from(full)
                if a:
                    out.append(a)


# ---- field decoders (Titanium wire structs, titanium_structs.h) -------------
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def i32(b, o): return struct.unpack_from('<i', b, o)[0]


def cstr(b, o, n):
    s = b[o:o + n].split(b'\0')[0]
    return s.decode('latin-1')


def fields(name, b):
    try:
        if name == 'OP_MoveItem' and len(b) >= 12:
            return 'from=%d to=%d qty=%d' % (i32(b, 0), i32(b, 4), u32(b, 8))
        if name == 'OP_MoveCoin' and len(b) >= 20:
            return 'from_slot=%d to_slot=%d cointype1=%d cointype2=%d amount=%d' % (
                i32(b, 0), i32(b, 4), u32(b, 8), u32(b, 12), i32(b, 16))
        if name == 'OP_DeleteItem' and len(b) >= 12:
            return 'from=%d to=%d qty=%d' % (i32(b, 0), i32(b, 4), u32(b, 8))
        if name in ('OP_ItemPacket', 'OP_CharInventory', 'OP_TradeItemPacket'):
            # ItemPacket: u32 packet type, then serialized item text "...|slot|..."
            pt = u32(b, 0) if len(b) >= 4 else -1
            txt = b[4:200].decode('latin-1', 'replace')
            parts = txt.split('|')
            nm = ''
            for i, p in enumerate(parts):
                if re.match(r'^[A-Za-z][A-Za-z \'`-]{2,}$', p):
                    nm = p; break
            return 'type=%d slot=%s name=%s' % (pt, parts[1] if len(parts) > 1 else '?', nm)
        if name in ('OP_ShopRequest',) and len(b) >= 8:
            return 'merchant_id=%d player_id=%d' % (u32(b, 0), u32(b, 4)) + (' cmd=%d' % u32(b, 8) if len(b) >= 12 else '')
        if name in ('OP_ShopPlayerSell',) and len(b) >= 12:
            return 'npcid=%d itemslot=%d quantity=%d price=%d' % (u32(b, 0), u32(b, 4), u32(b, 8), u32(b, 12) if len(b) >= 16 else -1)
        if name in ('OP_ShopPlayerBuy',) and len(b) >= 24:
            # Titanium Merchant_Sell_Struct: npcid, playerid, itemslot, unknown12, quantity, price
            return 'npcid=%d playerid=%d itemslot=%d quantity=%d price=%d' % (u32(b, 0), u32(b, 4), u32(b, 8), u32(b, 16), u32(b, 20))
        if name in ('OP_LootRequest', 'OP_EndLootRequest') and len(b) >= 2:
            return 'corpse_id=%d' % struct.unpack_from('<H', b, 0)[0]
        if name == 'OP_LootItem' and len(b) >= 12:
            return 'lootee=%d looter=%d slot=%d auto_loot=%d' % (u32(b, 0), u32(b, 4), struct.unpack_from('<H', b, 8)[0], i32(b, 12) if len(b) >= 16 else -1)
        if name == 'OP_TradeSkillCombine' and len(b) >= 4:
            return 'container_slot=%d' % i32(b, 0)
        if name == 'OP_ClickObject' and len(b) >= 8:
            return 'drop_id=%d player_id=%d' % (u32(b, 0), u32(b, 4))
        if name == 'OP_ClickObjectAction' and len(b) >= 16:
            return 'player_id=%d drop_id=%d open=%d type=%d icon=%d' % (u32(b, 0), u32(b, 4), u32(b, 8), u32(b, 12), u32(b, 16) if len(b) >= 20 else -1)
        if name in ('OP_TradeRequest', 'OP_TradeRequestAck', 'OP_CancelTrade', 'OP_TradeAcceptClick', 'OP_FinishTrade') and len(b) >= 4:
            return ' '.join('%d' % u32(b, i) for i in range(0, min(len(b), 16) - 3, 4))
        if name == 'OP_TradeCoins' and len(b) >= 8:
            return 'trader=%d slot=%d amount=%d' % (u32(b, 0), struct.unpack_from('<H', b, 4)[0], u32(b, 8) if len(b) >= 12 else -1)
        if name in ('OP_GMTraining', 'OP_GMEndTraining') and len(b) >= 8:
            return 'npcid=%d playerid=%d len=%d' % (u32(b, 0), u32(b, 4), len(b))
        if name == 'OP_GMTrainSkill' and len(b) >= 12:
            return 'npcid=%d skillbank=%d skill_id=%d' % (u32(b, 0), u32(b, 4), u32(b, 8))
        if name in ('OP_ChannelMessage',) and len(b) >= 136:
            return 'target=%s sender=%s chan=%d msg=%r' % (cstr(b, 0, 64), cstr(b, 64, 64), u32(b, 132), b[148:].split(b'\0')[0].decode('latin-1')[:80])
        if name in ('OP_FormattedMessage',) and len(b) >= 12:
            return 'string_id=%d type=%d args=%r' % (u32(b, 4), u32(b, 8), [x.decode('latin-1') for x in b[12:].split(b'\0') if x][:4])
        if name in ('OP_SimpleMessage',) and len(b) >= 8:
            return 'string_id=%d color=%d' % (u32(b, 0), u32(b, 4))
        if name == 'OP_Track':
            n = len(b) // 8
            return 'entries=%d ' % n + ' '.join('%d:%.0f' % (u32(b, i * 8), struct.unpack_from('<f', b, i * 8 + 4)[0]) for i in range(min(n, 12)))
        if name in ('OP_TrackTarget',) and len(b) >= 4:
            return 'entity=%d' % u32(b, 0)
        if name == 'OP_FriendsWho':
            return repr(b[:80])
        if name == 'OP_TargetCommand' or name == 'OP_TargetMouse':
            return 'target=%d' % u32(b, 0) if len(b) >= 4 else ''
        if name == 'OP_BankerChange':
            return 'len=%d' % len(b)
        if name == 'OP_ItemLinkClick' or name == 'OP_ItemVerifyRequest':
            return 'slot=%d target=%d' % (i32(b, 0), u32(b, 4)) if len(b) >= 8 else ''
    except Exception as e:
        return 'decode-error %s' % e
    return ''


def decode(path):
    streams = {}
    rows = []
    for t, lt, d in pcap_records(path):
        r = ip_udp(lt, d)
        if not r:
            continue
        src, sp, dst, dp, pl = r
        if dp in SERVER_PORTS:
            dirn, cport, sport = 'C->S', sp, dp
        elif sp in SERVER_PORTS:
            dirn, cport, sport = 'S->C', dp, sp
        else:
            continue
        key = (cport, sport, dirn)
        st = streams.setdefault(key, Stream())
        # session params are learned from the S->C SessionResponse; share with C->S
        if len(pl) >= 2 and pl[0] == 0 and pl[1] == 2:
            for k2 in [(cport, sport, 'C->S'), (cport, sport, 'S->C')]:
                s2 = streams.setdefault(k2, Stream())
                s2.crc = pl[10]; s2.compressed = bool(pl[11] & 1)
            continue
        p = strip(st, pl)
        if p is None:
            continue
        out = []
        proto(st, p, out)
        for op, payload in out:
            nm = OPN.get(op, 'OP_?')
            rows.append((t, dirn, sport, cport, nm, op, payload))
    return rows


def main():
    a = sys.argv[1:]
    path = a[0]
    show_all = '--all' in a
    hexd = '--hex' in a
    for t, dirn, sport, cport, nm, op, payload in decode(path):
        if not show_all and nm in NOISE:
            continue
        ts = datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime('%H:%M:%S.%f')[:-3]
        line = '%s\t%s\t%d/%d\t%s\t0x%04x\t%d\t%s' % (ts, dirn, sport, cport, nm, op, len(payload), fields(nm, payload))
        if hexd:
            line += '\t' + payload[:64].hex()
        print(line)


if __name__ == '__main__':
    main()
