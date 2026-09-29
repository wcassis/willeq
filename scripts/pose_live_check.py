#!/usr/bin/env python3
"""B874 / #218 -- check a LIVE --pose-log TSV against sources that are not the
log itself. Called by scripts/pose_live_capture.sh; exits 0 when every check
passes, 1 otherwise, and writes one comparison row per check.

Independent sources:
  * the SEED: the character_data row the rig wrote (and read back) before the
    client logged in -- the server's own pose for the character at zone-in;
  * the FINAL row: character_data after the zone's shutdown save, i.e. where
    the SERVER believes the character ended up;
  * kChaseRig (include/client/graphics/player/camera_rig.h: azimuth 0,
    pitch 6 deg, distance 4.0, look-at 7.0 above the feet), with the per-race
    eye scale and feet Z the client printed in its own `/camera get` lines;
  * the host wall clock around the run, and the client log's own
    "Script: executing" lines (UTC, the rig runs the client with TZ=UTC) to
    tell the stationary window from the movement.

The documented client->server transform (pose_log_format.h, docs/CLAUDE.md):
  server_x = playerY, server_y = playerX,
  server_heading = (128 - heading0_512) mod 512, both in EQ 0-512 units
  (character_data.heading is m_Position.w = EQ12toFloat(raw) = raw/4).

Tolerances are fixed HERE, before any run was looked at, and are not tuned to
a result: position 0.5, heading 1.0 unit (0.7 deg), z 2.0, camera 0.05.
"""
import argparse
import datetime as dt
import math
import re
import sys

HEADER = ("ts_unix_ms\tframe\tcameramode\tcamX\tcamY\tcamZ\ttargetX\ttargetY\ttargetZ\t"
          "playerX\tplayerY\tplayerZ\theading0_512\tmovestate")
CHASE = dict(az=0.0, pitch=6.0, dist=4.0, height=7.0)  # camera_rig.h kChaseRig
TOL_POS, TOL_HEAD, TOL_Z, TOL_CAM = 0.5, 1.0, 2.0, 0.05

ap = argparse.ArgumentParser()
ap.add_argument("--pose", required=True)
ap.add_argument("--client-log", required=True)
ap.add_argument("--seed", required=True, help="zone_id x y z heading (server frame, TAB or space)")
ap.add_argument("--final", required=True)
ap.add_argument("--after-quit", required=True, help="character_data straight after the client left")
ap.add_argument("--run-start-ms", type=int, required=True)
ap.add_argument("--run-end-ms", type=int, required=True)
ap.add_argument("--out", required=True)
a = ap.parse_args()

results = []  # (check, log value, independent value, source, delta, verdict)


def record(check, logv, indep, source, delta, ok):
    results.append((check, logv, indep, source, delta, "PASS" if ok else "FAIL"))


def info(check, logv, indep, source, delta=""):
    results.append((check, logv, indep, source, delta, "INFO"))


def hdiff(a_, b_):
    d = (a_ - b_) % 512.0
    return d - 512.0 if d > 256.0 else d


def to_server(px, py, h):
    return py, px, (128.0 - h) % 512.0


def compare(label, p, sx_, sy_, sz_, sh_, source):
    """One pose-log row against one server-frame (x, y, z, heading) source."""
    _, _, s_h_ = to_server(p["px"], p["py"], p["h"])
    record(f"{label}: server_x = playerY", f"{p['py']:.4f}", sx_, source, f"{p['py'] - sx_:+.4f}",
           abs(p["py"] - sx_) <= TOL_POS)
    record(f"{label}: server_y = playerX", f"{p['px']:.4f}", sy_, source, f"{p['px'] - sy_:+.4f}",
           abs(p["px"] - sy_) <= TOL_POS)
    record(f"{label}: z", f"{p['pz']:.4f}", sz_, source, f"{p['pz'] - sz_:+.4f}", abs(p["pz"] - sz_) <= TOL_Z)
    record(f"{label}: (128 - heading0_512) mod 512", f"{s_h_:.4f} (heading0_512 {p['h']:.4f})", sh_, source,
           f"{hdiff(s_h_, sh_):+.4f}", abs(hdiff(s_h_, sh_)) <= TOL_HEAD)


# ---- the log --------------------------------------------------------------
with open(a.pose) as f:
    lines = f.read().split("\n")
header = lines[0]
rows = [l.split("\t") for l in lines[1:] if l]
record("header", header == HEADER and "exact" or "differs", "14-column spec",
       "pose_log_format.h poseLogHeaderLine()", "", header == HEADER)
bad = [i for i, r in enumerate(rows) if len(r) != 14]
record("rows have 14 columns", f"{len(rows) - len(bad)}/{len(rows)}", "all", "format spec", len(bad), not bad)
rows = [r for r in rows if len(r) == 14]
if not rows:
    record("rows present", 0, ">0", "", "", False)
P = [dict(ts=int(r[0]), frame=int(r[1]), mode=r[2], cam=tuple(map(float, r[3:6])),
          tgt=tuple(map(float, r[6:9])), px=float(r[9]), py=float(r[10]), pz=float(r[11]),
          h=float(r[12]), ms=int(r[13])) for r in rows]

# ---- the client log: phase times and /camera get --------------------------
ts_re = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")
steps = []  # (unix ms, command)
camget = []  # list of dicts, one per /camera get block
locs = []  # (unix ms the client logged it, x, y, z, heading) -- the server's #loc
loc_re = re.compile(r"XYZ: ([-\d.]+), ([-\d.]+), ([-\d.]+) Heading: ([-\d.]+)")
last_t = None
with open(a.client_log, errors="replace") as f:
    for line in f:
        m = ts_re.match(line)
        t = None
        if m:
            t = int(dt.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")
                    .replace(tzinfo=dt.timezone.utc).timestamp() * 1000)
            last_t = t
        lm = loc_re.search(line)
        if lm and (t is not None or last_t is not None):
            v = tuple(float(x) for x in lm.groups())
            tt = t if t is not None else last_t
            if not locs or locs[-1][1:] != v or tt - locs[-1][0] > 500:
                locs.append((tt,) + v)
        s = re.search(r"Script: executing '([^']*)'", line)
        if s and t is not None:
            steps.append((t, s.group(1)))
        g = re.search(r"\[CAMERA-GET\] player=\(([-\d.]+),([-\d.]+),([-\d.]+)\) feetZ=([-\d.]+) heading=([-\d.]+)", line)
        if g:
            camget.append(dict(t=t, feetZ=float(g.group(4))))
        e = re.search(r"\[CAMERA-GET\] chase eye_above_feet=([-\d.]+) fit_eye_above_feet=([-\d.]+) scale=([-\d.]+)", line)
        if e and camget:
            camget[-1]["scale"] = float(e.group(3))


def step_time(prefix, nth=0):
    hits = [t for t, c in steps if c.startswith(prefix)]
    return hits[nth] if len(hits) > nth else None


t_chase, t_get1, t_turn = step_time("/camera chase"), step_time("/camera get"), step_time("/turn")
t_move, t_get2 = step_time("/move"), step_time("/camera get", 1)
info("script commands executed", len(steps), "", "client log 'Script: executing'")
info("server #loc replies", len(locs), "", "client log")

# ---- A. clock, frames -------------------------------------------------------
ts = [p["ts"] for p in P]
record("ts inside the run's wall-clock window",
       f"{ts[0]}..{ts[-1]}", f"{a.run_start_ms}..{a.run_end_ms}", "host clock (date +%s%3N) around the client",
       "", a.run_start_ms - 2000 <= ts[0] and ts[-1] <= a.run_end_ms + 2000)
nondec = all(b >= a_ for a_, b in zip(ts, ts[1:]))
record("ts non-decreasing", f"{sum(1 for a_, b in zip(ts, ts[1:]) if b < a_)} backwards steps", "0", "", "", nondec)
fr = [p["frame"] for p in P]
inc = all(b > a_ for a_, b in zip(fr, fr[1:]))
record("frame strictly increasing", f"{fr[0]}..{fr[-1]}", "increasing", "", "", inc)
info("rows / time span", len(P), f"{(ts[-1] - ts[0]) / 1000:.2f} s", "")

# ---- B. stationary window vs the SEED ---------------------------------------
seed = [float(x) for x in a.seed.split()]
final = [float(x) for x in a.final.split()]
_, sx, sy, sz, sh = seed
want_h = (128.0 - sh) % 512.0
if t_chase is None or t_turn is None:
    record("stationary window found", "missing /camera chase or /turn step", "", "client log", "", False)
    stat = []
else:
    stat = [p for p in P if t_chase + 1000 <= p["ts"] < t_turn and p["mode"] == "Chase Camera"]
record("stationary Chase rows", len(stat), ">= 10", "rows between /camera chase + 1 s and /turn", "", len(stat) >= 10)
if stat:
    p0 = stat[0]
    spread = max(max(abs(p["px"] - p0["px"]), abs(p["py"] - p0["py"]), abs(p["pz"] - p0["pz"]),
                     abs(hdiff(p["h"], p0["h"]))) for p in stat)
    record("stationary: pose constant", f"max spread {spread:.4f}", "0", "the log's own window", f"{spread:.4f}", spread <= 0.01)
    mv = sorted({p["ms"] for p in stat})
    record("stationary: movestate", mv, "[0]", "no input", "", mv == [0])
    compare("seed", p0, sx, sy, sz, sh, "character_data (seeded before login, read back)")
    loc1 = [l for l in locs if t_chase is not None and t_turn is not None and t_chase <= l[0] < t_turn]
    if loc1:
        compare("#loc (stationary)", p0, loc1[0][1], loc1[0][2], loc1[0][3], loc1[0][4],
                "server #loc reply (GetPosition, live)")
    else:
        record("#loc (stationary) reply", "absent", "present", "client log", "", False)

    # ---- C. Chase camera vs kChaseRig ------------------------------------------
    g = next((c for c in camget if t_get1 is not None and c["t"] is not None and c["t"] >= t_get1 - 50), None)
    worst_d = worst_pitch = worst_az = 0.0
    for p in stat:
        dx, dy, dz = (p["tgt"][i] - p["cam"][i] for i in range(3))
        d = math.sqrt(dx * dx + dy * dy + dz * dz)
        pitch = math.degrees(math.atan2(-dz, math.hypot(dx, dy)))  # camera ABOVE the look-at: dz < 0
        hr = p["h"] / 512.0 * 2 * math.pi
        az = math.degrees(math.atan2(dx, dy) - hr)
        az = (az + 180.0) % 360.0 - 180.0
        worst_pitch = max(worst_pitch, abs(pitch - CHASE["pitch"]))
        worst_az = max(worst_az, abs(az - CHASE["az"]))
        worst_d = max(worst_d, d)
    info("chase: camera->look-at distance (max)", f"{worst_d:.4f}", f"{CHASE['dist']} x eye scale", "kChaseRig")
    record("chase: pitch (worst |delta|, deg)", f"{worst_pitch:.4f}", CHASE["pitch"], "kChaseRig.pitchDeg",
           f"{worst_pitch:.4f}", worst_pitch <= 0.5)
    record("chase: behind the heading (worst |az|, deg)", f"{worst_az:.4f}", CHASE["az"], "kChaseRig.azimuthDeg",
           f"{worst_az:.4f}", worst_az <= 0.5)
    if g is None or "scale" not in g:
        record("chase: /camera get eye scale available", "absent", "present", "client log [CAMERA-GET]", "", False)
    else:
        S, feet = g["scale"], g["feetZ"]
        info("chase: eye scale / feetZ from /camera get", f"{S:.4f} / {feet:.4f}", "", "client log [CAMERA-GET]")
        info("chase: feetZ - playerZ", f"{feet - p0['pz']:+.4f}", "", "client log vs pose log")
        worst = 0.0
        for p in stat:
            hr = p["h"] / 512.0 * 2 * math.pi
            dist, hgt, pr = CHASE["dist"] * S, CHASE["height"] * S, math.radians(CHASE["pitch"])
            horiz = dist * math.cos(pr)
            want_t = (p["px"], p["py"], feet + hgt)
            want_c = (p["px"] - math.sin(hr) * horiz, p["py"] - math.cos(hr) * horiz, feet + hgt + dist * math.sin(pr))
            for i in range(3):
                worst = max(worst, abs(p["tgt"][i] - want_t[i]), abs(p["cam"][i] - want_c[i]))
        p = stat[0]
        record("chase: camera + look-at vs kChaseRig x eye scale (worst |delta|)",
               f"cam=({p['cam'][0]:.3f},{p['cam'][1]:.3f},{p['cam'][2]:.3f}) tgt=({p['tgt'][0]:.3f},{p['tgt'][1]:.3f},{p['tgt'][2]:.3f})",
               f"dist {CHASE['dist'] * S:.3f}, look-at feetZ+{CHASE['height'] * S:.3f}",
               "kChaseRig (camera_rig.h:141) + /camera get scale", f"{worst:.4f}", worst <= TOL_CAM)

# ---- D. movement --------------------------------------------------------------
if t_turn is not None:
    after_turn = [p for p in P if p["ts"] >= t_turn + 1000 and (t_move is None or p["ts"] < t_move)]
    if after_turn and stat:
        info("after /turn 90: heading0_512", f"{after_turn[-1]['h']:.4f}", f"before {stat[0]['h']:.4f}", "pose log")
    loc_t = [l for l in locs if l[0] >= t_turn and (t_move is None or l[0] < t_move)]
    if loc_t:
        before = [p for p in P if p["ts"] <= loc_t[0][0]]
        if before:
            compare("#loc (after /turn)", before[-1], loc_t[0][1], loc_t[0][2], loc_t[0][3], loc_t[0][4],
                    "server #loc reply (GetPosition, live) vs the last row logged before it")
    else:
        record("#loc (after /turn) reply", "absent", "present", "client log", "", False)
if t_move is not None:
    moving = [p for p in P if p["ts"] >= t_move]
    ms = sorted({p["ms"] for p in moving})
    record("movement: movestate changes after /move", ms, "contains a non-zero state", "the /move step", "",
           any(m != 0 for m in ms))
    if stat and moving:
        disp = max(math.hypot(p["px"] - stat[0]["px"], p["py"] - stat[0]["py"]) for p in moving)
        record("movement: player displacement after /move", f"{disp:.3f}", ">= 5 (commanded 15)", "the /move step",
               "", disp >= 5.0)
else:
    record("movement: /move step executed", "absent", "present", "client log", "", False)

# ---- E. the end vs the SERVER's final view -----------------------------------
last = P[-1]
if t_get2 is not None:
    record("coverage: log still writing at the 2nd /camera get", last["ts"], f">= {t_get2}",
           "client log", "", last["ts"] >= t_get2)
loc2 = [l for l in locs if t_move is not None and l[0] >= t_move]
if loc2:
    before = [p for p in P if p["ts"] <= loc2[0][0]]
    if before:
        compare("#loc (after /move)", before[-1], loc2[0][1], loc2[0][2], loc2[0][3], loc2[0][4],
                "server #loc reply (GetPosition, live) vs the last row logged before it")
else:
    record("#loc (after /move) reply", "absent", "present", "client log", "", False)
with open(a.client_log, errors="replace") as f:
    saved = any("been successfully saved" in line for line in f)
record("#save reply (server wrote the character)", saved and "successfully saved" or "absent",
       "successfully saved", "client log (zone/gm_commands/save.cpp)", "", saved)
quit_row = [float(x) for x in a.after_quit.split()]
compare("after quit", last, quit_row[1], quit_row[2], quit_row[3], quit_row[4],
        "character_data straight after the client left (world up)")
compare("final", last, final[1], final[2], final[3], final[4],
        "character_data after the zone's shutdown save")

# ---- report -------------------------------------------------------------------
with open(a.out, "w") as f:
    f.write("check\tlog_value\tindependent_value\tsource\tdelta\tverdict\n")
    for r in results:
        f.write("\t".join(str(x) for x in r) + "\n")
for r in results:
    print(f"{r[5]:4}  {r[0]}: log={r[1]}  indep={r[2]}  delta={r[4]}  [{r[3]}]")
fails = [r for r in results if r[5] == "FAIL"]
print(f"{len(fails)} FAIL / {sum(1 for r in results if r[5] == 'PASS')} PASS")
sys.exit(1 if fails else 0)
