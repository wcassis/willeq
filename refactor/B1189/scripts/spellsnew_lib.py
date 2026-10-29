# B1189 (#508): shared parsers for the official client's spell-visual files.
#
# All byte layouts are read from the client's own code, not assumed:
#   * spellsnew.eff: eqgame 0x44d773 reads the whole file, records of 0x10c bytes,
#     no header (418 records in the P1999 file). docs/spellsnew_format.md "Who does
#     what".
#   * spellsnew.edd: EQGraphicsDX9 0x100644e0 loads it ("EDD\0" + version int, then
#     entries of 0x1a0 bytes; the P1999 file is version 110, 2118 entries).
#   * spells_us.txt: "^"-separated fields ("^^" is a literal "^"). The official
#     client parses the spellsnew.eff record index into spell[0x8e] and the legacy
#     spells.eff index into spell[0x8c] (0x44dbf0); see spellsnew_format.md and
#     tests/spell_anim_rows.h for the field. The P1999 file is NOT a uniform
#     Titanium layout, so the record field is located per-spell by scanning a small
#     window of candidate columns (see SPELLSNEW_CANDIDATES) and taking the value
#     that names a real, non-empty spellsnew.eff record.
#
# This module is import-safe and read-only; it opens no network and writes nothing.

import struct

EFF_RECORD = 0x10C          # 268 bytes
EDD_ENTRY = 0x1A0           # 416 bytes
EDD_HEADER = 8              # "EDD\0" + version int

# spells_us columns (0-based) that can carry the spellsnew.eff record index in the
# P1999 file. tests/spell_anim_rows.h pins the field to 145 for the Titanium file;
# in the P1999 file the same logical column sits 5 columns earlier for most spells
# and in an adjacent column for a few. The window is small and every hit is
# validated against a real non-empty EFF record name, so a false positive would have
# to name a record that exists AND match the survey.
SPELLSNEW_CANDIDATES = (139, 140, 141, 142, 143, 144, 145)


def split_eq(line):
    """Split one spells_us.txt line on '^', where '^^' is a literal '^'."""
    out, buf, i = [], "", 0
    while i < len(line):
        c = line[i]
        if c == "^":
            if i + 1 < len(line) and line[i + 1] == "^":
                buf += "^"
                i += 2
                continue
            out.append(buf)
            buf = ""
            i += 1
        else:
            buf += c
            i += 1
    out.append(buf)
    return out


def _cstr(b, start, end):
    n = b.find(b"\x00", start, end)
    return b[start:n if n != -1 else end].decode("latin1")


class EddEntry:
    __slots__ = ("name", "texture",
                 "orient_track", "no_depth", "additive", "attach_scale",
                 "sprite_mode", "live_follow",
                 "run_time", "lifetime", "burst", "per_tick", "ticks_per_sec",
                 "start_delay", "fade_in", "fade_out", "grow_in", "shrink_out",
                 "density_dist", "peak_opacity",
                 "shape", "shape_rx", "shape_ry", "shape_rz",
                 "off1", "off2", "off3", "tilt1", "tilt2",
                 "w_min", "w_max", "h_min", "h_max", "one_draw", "depth_bias",
                 "sr", "sg", "sb", "er", "eg", "eb",
                 "up_min", "up_max", "up_acc",
                 "ax2_min", "ax2_max", "ax2_acc",
                 "ax3_min", "ax3_max", "ax3_acc",
                 "rad_min", "rad_max", "rad_acc",
                 "sw_min", "sw_max", "sw_acc",
                 "gravity", "drift_x",
                 "frames", "fps", "spin_min", "spin_max",
                 "fixed_count", "size_mult", "default_opacity", "spin_align",
                 "shrink_h", "shrink_w", "neg_radius", "shape_scale")

    def __init__(self, i):
        self.name = i[0]
        self.texture = i[1]
        self.orient_track, self.no_depth, self.additive, self.attach_scale = i[2], i[3], i[4], i[5]
        self.sprite_mode, self.live_follow = i[6], i[7]
        self.run_time, self.lifetime = i[8], i[9]
        self.burst, self.per_tick, self.ticks_per_sec, self.start_delay = i[10], i[11], i[12], i[13]
        self.fade_in, self.fade_out = i[14], i[15]
        self.grow_in, self.shrink_out = i[16], i[17]
        self.density_dist, self.peak_opacity = i[18], i[19]
        self.shape = i[20]
        self.shape_rx, self.shape_ry, self.shape_rz = i[21], i[22], i[23]
        self.off1, self.off2, self.off3 = i[24], i[25], i[26]
        self.tilt1, self.tilt2 = i[27], i[28]
        self.w_min, self.w_max = i[29], i[30]
        self.h_min, self.h_max = i[31], i[32]
        self.one_draw = i[33]
        self.depth_bias = i[34]
        self.sr, self.sg, self.sb, self.er, self.eg, self.eb = i[35], i[36], i[37], i[38], i[39], i[40]
        self.up_min, self.up_max, self.up_acc = i[41], i[42], i[43]
        self.ax2_min, self.ax2_max, self.ax2_acc = i[44], i[45], i[46]
        self.ax3_min, self.ax3_max, self.ax3_acc = i[47], i[48], i[49]
        self.rad_min, self.rad_max, self.rad_acc = i[50], i[51], i[52]
        self.sw_min, self.sw_max, self.sw_acc = i[53], i[54], i[55]
        self.gravity = i[56]
        self.drift_x = i[57]
        self.frames = i[58]
        self.fps = i[59]
        self.spin_min, self.spin_max = i[60], i[61]
        self.fixed_count = i[62]
        self.size_mult = i[63]
        self.default_opacity = i[64]
        self.spin_align = i[65]
        self.shrink_h, self.shrink_w = i[66], i[67]
        self.neg_radius = i[68]
        self.shape_scale = i[69]


def parse_edd(path):
    raw = open(path, "rb").read()
    if raw[:4] != b"EDD\x00":
        raise ValueError("not an EDD file: %r" % raw[:4])
    version = struct.unpack_from("<i", raw, 4)[0]
    n = (len(raw) - EDD_HEADER) // EDD_ENTRY
    entries = []
    for idx in range(n):
        b = raw[EDD_HEADER + idx * EDD_ENTRY: EDD_HEADER + (idx + 1) * EDD_ENTRY]
        f32 = lambda off: struct.unpack_from("<f", b, off)[0]
        i32 = lambda off: struct.unpack_from("<i", b, off)[0]
        vals = [
            _cstr(b, 0, 0x40),
            _cstr(b, 0x40, 0x60),
            i32(0x60), i32(0x64), i32(0x68), i32(0x6C), i32(0x70), i32(0x74),
            f32(0x78), f32(0x7C), i32(0x80), i32(0x84), f32(0x88), f32(0x8C),
            f32(0x90), f32(0x94), f32(0x98), f32(0x9C),
            f32(0xA0), f32(0xA4), i32(0xA8),
            f32(0xAC), f32(0xB0), f32(0xB4),
            f32(0xB8), f32(0xBC), f32(0xC0),
            f32(0xC4), f32(0xC8),
            f32(0xCC), f32(0x184), f32(0x17C), f32(0x180), i32(0x18C),
            f32(0xD0),
            i32(0xD4), i32(0xD8), i32(0xDC), i32(0xE0), i32(0xE4), i32(0xE8),
            f32(0xEC), f32(0xF0), f32(0xF4),
            f32(0xF8), f32(0xFC), f32(0x100),
            f32(0x104), f32(0x108), f32(0x10C),
            f32(0x110), f32(0x114), f32(0x118),
            f32(0x11C), f32(0x120), f32(0x124),
            f32(0x128), f32(0x12C),
            i32(0x130), f32(0x134),
            f32(0x138), f32(0x188),
            i32(0x144), f32(0x16C), f32(0x170), i32(0x178),
            f32(0x190), f32(0x194), i32(0x198), i32(0x19C),
        ]
        entries.append(EddEntry(vals))
    return version, entries


class EffSlot:
    __slots__ = ("edd", "min_level", "anchor", "attach")

    def __init__(self, e, m, a, p):
        self.edd, self.min_level, self.anchor, self.attach = e, m, a, p


class EffRecord:
    # name: char[64]; stages: list of 3 (sound:int, slots:list[EffSlot]) for
    # stage 0 Source / 1 Middle / 2 Target (EFF layout, docs/spellsnew_format.md).
    __slots__ = ("name", "stages")

    def __init__(self, name, stages):
        self.name, self.stages = name, stages


def parse_eff(path):
    raw = open(path, "rb").read()
    n = len(raw) // EFF_RECORD
    records = []
    for idx in range(n):
        b = raw[idx * EFF_RECORD: (idx + 1) * EFF_RECORD]
        name = _cstr(b, 0, 0x40)
        rec = []
        for stage in range(3):
            base = 0x40 + 0x44 * stage
            sound = struct.unpack_from("<i", b, base)[0]
            slots = []
            for s in range(4):
                sb = base + 4 + 16 * s
                e, m, a, p = struct.unpack_from("<iiii", b, sb)
                slots.append(EffSlot(e, m, a, p))
            rec.append((sound, slots))
        records.append(EffRecord(name, rec))
    return records


def load_spells(path):
    """Return {spell_id: fields(list of str)} for spells_us.txt."""
    out = {}
    for line in open(path, "rb").read().decode("latin1").split("\n"):
        if not line.strip():
            continue
        line = line.rstrip("\r")
        c = split_eq(line)
        if not c[0].isdigit():
            continue
        out[int(c[0])] = c
    return out


def spellsnew_record(spell_fields, eff_records):
    """The spellsnew.eff record index a spell points at, or None.

    Scans SPELLSNEW_CANDIDATES and returns the first column holding an index that
    names a non-empty EFF record (the client's own gate at 0x44dbf0: 0 < idx <
    record count AND the record name is non-empty). Returns (index, column) or
    (None, None).
    """
    max_rec = len(eff_records)
    for col in SPELLSNEW_CANDIDATES:
        if col >= len(spell_fields):
            continue
        v = spell_fields[col]
        if not (v.lstrip("-").isdigit()):
            continue
        idx = int(v)
        if 0 < idx < max_rec and eff_records[idx].name != "":
            return idx, col
    return None, None


def motion_signature(e):
    """Classify an EDD entry's motion by which velocity words are non-zero."""
    parts = []
    if e.up_min or e.up_max or e.up_acc:
        parts.append("up")
    if e.ax2_min or e.ax2_max or e.ax2_acc:
        parts.append("ax2")
    if e.ax3_min or e.ax3_max or e.ax3_acc:
        parts.append("ax3")
    if e.rad_min or e.rad_max or e.rad_acc:
        parts.append("radial")
    if e.sw_min or e.sw_max or e.sw_acc:
        parts.append("swirl")
    if e.gravity:
        parts.append("gravity")
    if e.drift_x:
        parts.append("drift")
    return "+".join(parts) if parts else "static"


def fps_bucket(v):
    if v == 0:
        return "0"
    if v < 1:
        return "<1"
    if v <= 10:
        return "1-10"
    if v <= 30:
        return "11-30"
    if v <= 60:
        return "31-60"
    return ">60"


def frames_bucket(v):
    if v <= 1:
        return "1 (whole)"
    if v <= 4:
        return "2-4 (2x2)"
    if v <= 8:
        return "5-8 (4x2)"
    if v <= 16:
        return "9-16 (4x4)"
    return ">16 (whole)"
