#!/usr/bin/env python3
"""B1206 -- Daybreak wire decode for the observer/caster pcaps.

Replays one B1134-leg capture.pcap through the same state machine willeq's
receiver uses (src/common/net/daybreak_connection_decode.cpp):

  * per-UDP-datagram: CRC strip (crc32 over keyLE+data, the session's
    crc_bytes), then encode passes (1=0x5a/0xa5 zlib marker, 4=XOR chain);
  * framing: OP_Combined / OP_AppCombined batches, OP_Packet..4 reliable
    streams with in-order sequencing + out-of-order queueing, OP_Fragment..4
    assembly with the 0x5a/0xa5 marker on the assembled buffer;
  * application packets (first byte != 0) are emitted, in order.

Output TSV columns:
  sess  dir  stream  seq  ts  opcode  spawn  len  data_hex

where sess = "port" (the server port of the session: 5998 login, 9000 world,
7000.. zone), dir = s2c | c2s, opcode/spawn are the app-packet's first two
u16s little-endian (the Titanium opcode + spawn id), data_hex the whole
app packet.

Usage:  python3 pcap_decode.py <capture.pcap> [--server-ip IP] [--out TSV]

Every cited number in refactor/B1206/README.md must be reproducible from
this script's output on the leg's capture.pcap.
"""
import struct
import sys
import zlib
from collections import defaultdict


# Frame opcodes (include/common/net/daybreak_connection.h, DbProtocolOpcode).
OP_PADDING = 0x00
OP_SESSION_REQUEST = 0x01
OP_SESSION_RESPONSE = 0x02
OP_COMBINED = 0x03
OP_SESSION_DISCONNECT = 0x05
OP_KEEP_ALIVE = 0x06
OP_PING = 0x1C
OP_OUT_OF_SESSION = 0x1D
OP_PACKET0 = 0x09
OP_FRAGMENT0 = 0x0D
OP_ACK0 = 0x15
OP_APP_COMBINED = 0x19


def iter_udp_datagrams(path, server_ip=None):
    """Yield (ts, sport, dport, sip, dip, payload) for every UDP datagram."""
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4"), "not a pcap"
    le = data[:4] == b"\xd4\xc3\xb2\xa1"
    bo = "<" if le else ">"
    net = struct.unpack(bo + "I", data[20:24])[0]
    i = 24
    while i + 16 <= len(data):
        ts_sec, ts_usec, incl, _ = struct.unpack(bo + "IIII", data[i:i + 16])
        i += 16
        pkt = data[i:i + incl]
        i += incl
        if net == 276:  # LINUX_SLL2; this rig's tcpdump puts the IP at 20
            ip = None
            for off in (20, 16):
                if len(pkt) > off + 20 and (pkt[off] >> 4) == 4 and pkt[off + 9] == 17:
                    ip = pkt[off:]
                    break
            if ip is None:
                continue
        elif net == 1:  # ethernet
            if len(pkt) < 14 or struct.unpack(">H", pkt[0:2])[0] != 0x0800:
                continue
            ip = pkt[14:]
        else:
            continue
        if len(ip) < 20 or (ip[0] >> 4) != 4 or ip[9] != 17:
            continue
        ihl = (ip[0] & 0xF) * 4
        udp = ip[ihl:]
        if len(udp) < 8:
            continue
        sport, dport = struct.unpack(">HH", udp[:4])
        sip = ".".join(str(b) for b in ip[12:16])
        dip = ".".join(str(b) for b in ip[16:20])
        if server_ip and sip != server_ip and dip != server_ip:
            continue
        yield ts_sec + ts_usec / 1e6, sport, dport, sip, dip, udp[8:]


class Session:
    """One Daybreak session in one direction; decodes app packets."""

    def __init__(self):
        self.crc_bytes = 0
        self.key = 0
        self.pass1 = 0  # index 0 in the C++ m_encode_passes
        self.pass2 = 0  # index 1
        self.established = False
        # per reliable stream 0..3
        self.seq_in = [0] * 4
        self.queue = [defaultdict(bytes) for _ in range(4)]
        self.frag = [b"" for _ in range(4)]
        self.frag_total = [0] * 4
        self.dropped_crc = 0
        self.dropped_other = 0

    # -- per-datagram path (ProcessPacket) -------------------------------
    def feed(self, raw, ts, out, dir_tag, sess_id):
        if len(raw) < 2:
            self.dropped_other += 1
            return
        op = raw[1]
        if raw[0] == 0 and op == OP_SESSION_RESPONSE and not self.established:
            # DaybreakConnectReply (17 bytes): zero, opcode, connect_code,
            # encode_key, crc_bytes, encode_pass1, encode_pass2,
            # max_packet_size  (32-bit fields network order)
            self.key = struct.unpack(">I", raw[6:10])[0]
            self.crc_bytes = raw[10]
            self.pass1 = raw[11]
            self.pass2 = raw[12]
            self.established = True
            return
        if raw[0] == 0 and op in (OP_KEEP_ALIVE, OP_PING):
            return
        body = raw
        # CRC (ValidateCRC): crc32 over keyLE+body minus the trailing bytes
        if self.crc_bytes:
            if len(body) < self.crc_bytes:
                self.dropped_crc += 1
                return
            actual = struct.unpack(
                (">H" if self.crc_bytes == 2 else ">I"),
                body[-self.crc_bytes:])[0]
            calc = zlib.crc32(
                struct.pack("<I", self.key & 0xFFFFFFFF) + body[:-self.crc_bytes]
            ) & 0xFFFFFFFF
            if actual != (calc & 0xFFFF if self.crc_bytes == 2 else calc):
                self.dropped_crc += 1
                return
            body = body[:-self.crc_bytes]
        # Encode passes, applied i=1 then i=0 (the C++ loop order).
        for pi in (self.pass2, self.pass1):
            if pi == 1:  # EncodeCompression
                off = 2 if body[0] == 0 else 1
                body = body[:off] + self._inflate(body[off:])
            elif pi == 4:  # EncodeXOR
                off = 2 if body[0] == 0 else 1
                body = body[:off] + self._xordecode(body[off:])
        self.dispatch(body, ts, out, dir_tag, sess_id)

    @staticmethod
    def _inflate(chunk):
        if len(chunk) < 1:
            return b""
        marker = chunk[0]
        if marker == 0x5A:  # zlib
            try:
                return zlib.decompress(chunk[1:])
            except zlib.error:
                return chunk[1:]
        if marker == 0xA5:  # stored raw
            return chunk[1:]
        return chunk

    @staticmethod
    def _xordecode(chunk):
        # Decode() in daybreak_connection_send.cpp: int32 XOR chain, then
        # the trailing bytes XOR'd with the low byte of the final key.
        key = 0
        out = bytearray(chunk)
        i = 0
        while i + 4 <= len(out):
            cur = int.from_bytes(out[i:i + 4], "little", signed=False)
            out[i:i + 4] = (cur ^ key).to_bytes(4, "little")
            key = cur
            i += 4
        kc = key & 0xFF
        for j in range(i, len(out)):
            out[j] ^= kc
        return bytes(out)

    # -- decoded-packet path (ProcessDecodedPacket) ----------------------
    def dispatch(self, p, ts, out, dir_tag, sess_id):
        if len(p) >= 2 and p[0] == 0:
            op = p[1]
            if OP_PACKET0 <= op <= OP_PACKET0 + 3:
                self._reliable(p, op - OP_PACKET0, ts, out, dir_tag, sess_id)
            elif OP_FRAGMENT0 <= op <= OP_FRAGMENT0 + 3:
                self._fragment(p, op - OP_FRAGMENT0, ts, out, dir_tag, sess_id)
            elif op == OP_COMBINED:
                cur, end = 2, len(p)
                while cur < end:
                    ln = p[cur]
                    if cur + 1 + ln > end:
                        break
                    self.dispatch(p[cur + 1:cur + 1 + ln], ts, out, dir_tag, sess_id)
                    cur += 1 + ln
            elif op == OP_APP_COMBINED:
                cur, end = 2, len(p)
                while cur < end:
                    if p[cur] == 0xFF:
                        if cur + 3 > end:
                            break
                        if p[cur + 1] == 0xFF and p[cur + 2] == 0xFF:
                            if cur + 7 > end:
                                break
                            ln = int.from_bytes(p[cur + 3:cur + 7], "big")
                            cur += 7
                        else:
                            ln = (p[cur + 1] << 8) | p[cur + 2]
                            cur += 3
                    else:
                        ln = p[cur]
                        cur += 1
                    if cur + ln > end:
                        break
                    self.dispatch(p[cur:cur + ln], ts, out, dir_tag, sess_id)
                    cur += ln
            return
        # Application packet.
        op16 = int.from_bytes(p[0:2], "little") if len(p) >= 2 else -1
        spawn = int.from_bytes(p[2:4], "little") if len(p) >= 4 else -1
        out.append((sess_id, dir_tag, -1, -1, ts, op16, spawn,
                    len(p), p.hex()))


    def _reliable(self, p, stream, ts, out, dir_tag, sess_id):
        seq = struct.unpack(">H", p[2:4])[0]
        q = self.queue[stream]
        expected = self.seq_in[stream]
        # flush any queued packet that now matches
        while expected in q:
            held = q.pop(expected)
            self._deliver(held, expected, stream, ts, out, dir_tag, sess_id)
            expected += 1
        if seq == expected:
            self._deliver(p, seq, stream, ts, out, dir_tag, sess_id)
        elif seq > expected:
            q[seq] = p
        # seq < expected: past, skipped (the C++ path sends an ack)

    def _deliver(self, p, seq, stream, ts, out, dir_tag, sess_id):
        self.seq_in[stream] = seq + 1
        self.dispatch(p[4:], ts, out, dir_tag, sess_id)

    def _fragment(self, p, stream, ts, out, dir_tag, sess_id):
        seq = struct.unpack(">H", p[2:4])[0]
        expected = self.seq_in[stream]
        if seq > expected:
            # out-of-order fragments: hold them; flush when expected arrives
            self.queue[stream][1_000_000 + seq] = p
            return
        if seq < expected:
            return
        self.seq_in[stream] = seq + 1
        self._frag_chunk(p, stream, ts, out, dir_tag, sess_id)
        # flush any held fragments in order
        while (1_000_000 + expected) in self.queue[stream]:
            held = self.queue[stream].pop(1_000_000 + expected)
            self._frag_chunk(held, stream, ts, out, dir_tag, sess_id)
            expected += 1

    def _frag_chunk(self, p, stream, ts, out, dir_tag, sess_id):
        if not self.frag[stream]:
            total = struct.unpack(">I", p[4:8])[0]
            self.frag_total[stream] = total
            self.frag[stream] = p[8:]
        else:
            self.frag[stream] += p[4:]
        if len(self.frag[stream]) >= self.frag_total[stream]:
            assembled = self.frag[stream][:self.frag_total[stream]]
            self.frag[stream] = b""
            self.frag_total[stream] = 0
            if assembled[:1] in (b"\x5a", b"\xa5"):
                assembled = self._inflate(assembled)
            self.dispatch(assembled, ts, out, dir_tag, sess_id)


def main():
    args = [a for a in sys.argv[1:]]
    pcap = out = None
    server_ip = None
    i = 0
    while i < len(args):
        if args[i] == "--server-ip":
            server_ip = args[i + 1]
            i += 2
        elif args[i] == "--out":
            out = args[i + 1]
            i += 2
        else:
            pcap = args[i]
            i += 1
    if not pcap:
        print(__doc__)
        return 2

    sessions = {}  # (sip, sport, dip, dport) -> Session (server->client view)
    rows = []
    for ts, sport, dport, sip, dip, payload in iter_udp_datagrams(pcap, server_ip):
        key = (sip, sport, dip, dport)
        s = sessions.setdefault(key, Session())
        s.feed(payload, ts, rows, "s2c" if sip != dip else "?", str(dport))

    rows.sort(key=lambda r: r[4])
    if out:
        with open(out, "w") as f:
            f.write("sess\tdir\tstream\tseq\tts\topcode\tspawn\tlen\tdata_hex\n")
            for r in rows:
                f.write("\t".join(str(x) for x in r) + "\n")
        print(f"wrote {len(rows)} app packets -> {out}")
    else:
        for r in rows:
            print("\t".join(str(x) for x in r))
    for key, s in sessions.items():
        if s.established or s.dropped_crc:
            print(f"session {key}: crc={s.crc_bytes} key={s.key:#x} "
                  f"passes=({s.pass1},{s.pass2}) dropped_crc={s.dropped_crc}",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
