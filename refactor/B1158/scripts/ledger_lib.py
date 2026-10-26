#!/usr/bin/env python3
"""B1158: shared parsing for the visual FX ledger (no model judges a frame).

Every number a clip quotes comes from a measured file:
  * oracle captures:  video.calibration.tsv (calibrated_t0) or the B1130
    measure header (video_t0 -- the same quantity: launch_epoch +
    (marker_epoch - launch_epoch) - marker_frame/25, i.e. the epoch of the
    video's frame 0), all-opcodes / measure headers (BeginCast/Action
    epochs), ffprobe (fps, frame count).
  * harness runs:     timing.tsv (BeginCast/Action frames) or meta.json
    (t_begincast/t_action, seconds of the 30 fps video), b1130_rows.tsv
    (landing_end_s).

The alignment rule (stated once in refactor/B1158/README.md, printed per
clip, no hand offsets):
  oracle video frame F  <->  epoch t0 + F/fps        (t0 = calibrated_t0)
  F_bc = round((bc_epoch - t0) * fps)
  willeq frame F        <->  time F/30 s             (30 fps video)
  F_bc = round(bc_t * 30)
A clip cuts BOTH sides from BC - 1.0 s to Act + max(8.0, measured_end + 0.5)
s in each side's own timeline, so the two BeginCasts sit at the same
clip-time by construction; the quoted frames are the exact source frames of
BeginCast / Action.
"""
import json
import os
import re
import sqlite3
import subprocess


def ffprobe(path):
    """(fps, nb_frames) for the first video stream of path."""
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=r_frame_rate,nb_frames", "-of", "csv=p=0", path],
        capture_output=True, text=True, check=True).stdout.strip()
    rate, frames = out.split(",")
    num, den = (int(x) for x in rate.split("/"))
    fps = num / den
    nf = None if frames in ("N/A", "") else int(frames)
    return fps, nf


_DB = {"conn": None}  # type: ignore[assignment]


def db_spell_name(spell_id):
    """data/willeq.db is the shipped spell table; one connection per process."""
    if _DB["conn"] is None:
        repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
        p = os.path.join(repo, "data", "willeq.db")
        if os.path.exists(p):
            _DB["conn"] = sqlite3.connect(p)
    if _DB["conn"] is None:
        return None
    row = _DB["conn"].execute("select name from spells where id=?", (spell_id,)).fetchone()
    return row[0] if row else None


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    return p.returncode, p.stdout, p.stderr


# ---------------------------------------------------------------- oracle --

def read_calibration(legdir):
    """video.calibration.tsv -> dict (launch_epoch, marker_epoch,
    marker_frame, calibrated_t0). The file is one header line + one data
    line; calibrated_t0 is the epoch of the video's frame 0 (the marker's
    frame is at the rig's 10/25 Hz marker rate, whatever it was)."""
    with open(os.path.join(legdir, "video.calibration.tsv")) as f:
        lines = [l.rstrip("\n") for l in f if l.strip()]
    if len(lines) < 2:
        raise ValueError(f"{legdir}: bad video.calibration.tsv")
    header = lines[0].split("\t")
    vals = lines[1].split("\t")
    d = {k: float(v) for k, v in zip(header, vals)}
    if "calibrated_t0" not in d:
        raise ValueError(f"{legdir}: no calibrated_t0 column")
    if "launch_epoch" in d and abs(d["calibrated_t0"] - d["launch_epoch"]) > 300:
        raise ValueError(f"{legdir}: calibrated_t0 implausible vs launch")
    return d


B1130_MEASURE_RE = re.compile(
    r"^\# (\S+) (\S+) step (\S+) BeginCast (\S+) \(cast_ms (\d+)\) Action (\S+) "
    r"target=(\S+) source=(\S+) video_t0 (\S+) window to (\S+)")


def parse_b1130_measure(path):
    """B1130 measure/<cap>_<kind>.txt header -> dict with bc/act epochs and
    video_t0 (the capture's calibrated_t0)."""
    with open(path) as f:
        for line in f:
            m = B1130_MEASURE_RE.match(line)
            if m:
                return dict(
                    capture=m.group(1), kind=m.group(2), step=m.group(3),
                    bc_epoch=float(m.group(4)), cast_ms=int(m.group(5)),
                    act_epoch=float(m.group(6)), video_t0=float(m.group(9)),
                )
    raise ValueError(f"{path}: no B1130 measure header line")


LEG_MEASURE_RE = re.compile(
    r"^## spell (\d+) rep (\d+) source (\d+) target (\d+)(?: \((\w+)\))?"
    r" BC ([\d.]+) \(cast_ms (\d+)(?:, [^)]*)?\) Act ([\d.]+) cast ([\d.]+)s")


def parse_leg_measure(path):
    """B1134/B1143 measure txt header (## spell ... BC ... Act ...) -> dict.
    The leg's t0 comes from the leg dir's video.calibration.tsv."""
    with open(path) as f:
        for line in f:
            m = LEG_MEASURE_RE.match(line)
            if m:
                return dict(
                    spell=int(m.group(1)), rep=int(m.group(2)),
                    target_kind=m.group(5) or "",
                    bc_epoch=float(m.group(6)), cast_ms=int(m.group(7)),
                    act_epoch=float(m.group(8)),
                )
    raise ValueError(f"{path}: no '## spell' measure header line")


def first_line_of(path):
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                return line
    return ""


def read_tsv_rows(path, want_header=True):
    rows = []
    with open(path) as f:
        for i, line in enumerate(f):
            line = line.rstrip("\n")
            if not line or (want_header and i == 0):
                continue
            if line.startswith("#"):
                continue
            rows.append(line.split("\t"))
    return rows


def meta_json(path):
    with open(path) as f:
        return json.load(f)


def find_video(primary, mirrors, required=True):
    """primary path, else the first existing mirror (ThinkPad-NFS fetch)."""
    for p in [primary] + list(mirrors):
        if os.path.exists(p):
            return p
    if required:
        raise FileNotFoundError(f"no video for {primary} (mirrors: {mirrors})")
    return None


def pick_leg_video(legdir_candidates, bc_epoch, act_epoch):
    """Choose the leg video whose calibration t0 puts the measured cast
    frames inside the file (exactly one must). Returns (legdir, t0, fps)."""
    hits = []
    for d in legdir_candidates:
        cal = read_calibration(d)
        t0 = cal["calibrated_t0"]
        fps, nf = ffprobe(os.path.join(d, "video.mp4"))
        if nf is None:
            continue
        f_bc = (bc_epoch - t0) * fps
        f_act = (act_epoch - t0) * fps
        if 0 <= f_bc < nf and 0 <= f_act < nf and f_bc > fps:
            hits.append((d, t0, fps, nf))
    if len(hits) != 1:
        raise ValueError(f"{len(hits)} candidate leg videos fit the measured cast "
                         f"(expected 1): {[h[0] for h in hits]}")
    return hits[0][0], hits[0][1], hits[0][2]
