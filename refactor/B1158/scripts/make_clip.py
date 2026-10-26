#!/usr/bin/env python3
"""B1158: build ONE ledger clip (oracle LEFT / willeq RIGHT) + its 8-cell strip.

usage: make_clip.py <spec.json | <rec-id>> <out-dir>

spec.json (written by ledger_inventory.py):
{
  "id": "r000_93_fixed_npc",
  "title": "rec 0 | 93 Burst of Flame | fixed_npc",
  "oracle": {"video": "...", "t0": 1792678587.641,
             "bc_epoch": 1792678886.463, "act_epoch": 1792678887.973,
             "label": "B1130 r51 (25 fps)"},
  "willeq": {"video": "...", "bc_t": 1.5, "act_t": 3.0,
             "label": "B1155 fixed_npc_r0 (30 fps)"}   |  {"frames": "..."}  |  null
  "verdicts": [["Source-onset", "PASS"], ...],
  "measured_end_s": 6.67,          # optional, Act+ seconds
  "note": "optional second strip line"
}

Alignment (stated once in refactor/B1158/README.md, printed per clip,
NO hand offsets): oracle frame F <-> epoch t0 + F/fps (t0 = the capture's
calibrated_t0, i.e. the epoch of its frame 0); willeq frame F <-> F/30 s.
Both sides are cut from BC - 1.0 s to Act + max(8.0, measured_end + 0.5) s
in their OWN timelines, so the two BeginCasts share the same clip time by
construction; the printed frames are the exact source frames of BeginCast /
Action. fps normalisation (25 -> 30) may shift a cut by at most one frame.

stdout: one JSON object (the clip's alignment facts, collected into
index.tsv). stderr: the human alignment line.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ledger_lib as L

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
W, H = 1280, 800          # one half
TOP, BOT = 56, 64         # title bar, verdict strip
OUT_W, OUT_H = W * 2, H + TOP + BOT
PRE = 1.0                 # clip starts BC - PRE s
BASE_END = 8.0            # clip ends Act + BASE_END s (or the measured end)

VERDICT_COLOR = {"PASS": "0x3fb950", "short": "0xd29922", "over": "0xf85149",
                 "n-a": "0x8b949e", "n/a": "0x8b949e", "PASS*": "0x3fb950"}


def esc(text):
    # drawtext text is single-quoted inside the filtergraph, where a single
    # quote cannot be escaped; display apostrophes go to U+2019 instead.
    return (str(text).replace("\\", "\\\\")
            .replace(":", "\\:")
            .replace("'", "\u2019"))


def dt(text, x, y, size, color="white", bold=False):
    return (f"drawtext=fontfile={FONTB if bold else FONT}"
            f":text='{esc(text)}':x={x}:y={y}:fontsize={size}:fontcolor={color}")


def fps_g(fps):
    return str(int(fps)) if abs(fps - round(fps)) < 1e-6 else f"{fps:.2f}"


def build(spec, out_dir):
    oracle = spec["oracle"]
    willeq = spec.get("willeq")
    title = spec["title"]
    note = spec.get("note", "")

    end_off = max(BASE_END, (spec.get("measured_end_s") or 0.0) + 0.5)
    dur = PRE + end_off
    N = int(math.ceil(dur * 30))

    # ---- oracle side ------------------------------------------------------
    ovideo = L.find_video(oracle["video"], oracle.get("mirrors", []))
    fps_o, nf_o = L.ffprobe(ovideo)
    t0 = float(oracle["t0"])
    bc_rel = float(oracle["bc_epoch"]) - t0
    act_rel = float(oracle["act_epoch"]) - t0
    if bc_rel < PRE:
        raise ValueError(f"{spec['id']}: oracle cast {bc_rel:.3f}s from frame 0, "
                         f"clip needs {PRE}s of lead-in")
    total_o = nf_o / fps_o if nf_o else None
    if total_o is not None and bc_rel - PRE + dur > total_o + 0.05:
        raise ValueError(f"{spec['id']}: clip (BC-{PRE}s..Act+{end_off:.1f}s) runs "
                         f"past the end of the oracle video ({total_o:.3f}s)")
    f_bc_o = int(round(bc_rel * fps_o))
    f_act_o = int(round(act_rel * fps_o))
    start_o = bc_rel - PRE

    # ---- willeq side ------------------------------------------------------
    w_side = None
    if willeq:
        fps_w = 30
        if "video" in willeq:
            wvideo = L.find_video(willeq["video"], willeq.get("mirrors", []))
            fps_w, _ = L.ffprobe(wvideo)
        else:
            wvideo = None
        bc_t = float(willeq["bc_t"])
        act_t = float(willeq["act_t"])
        if bc_t < PRE:
            raise ValueError(f"{spec['id']}: harness BC {bc_t}s < {PRE}s before start")
        w_side = dict(video=wvideo, frames=willeq.get("frames"),
                      fps=fps_w, bc_t=bc_t, act_t=act_t,
                      f_bc=int(round(bc_t * fps_w)), f_act=int(round(act_t * fps_w)),
                      label=willeq.get("label", ""))

    act_off_o = act_rel - start_o                 # clip-time of the oracle Act
    act_off_w = (w_side["act_t"] - w_side["bc_t"]) + PRE if w_side else None

    # ---- ffmpeg ------------------------------------------------------------
    os.makedirs(out_dir, exist_ok=True)
    clip = os.path.join(out_dir, spec["id"] + ".mp4")
    strip = os.path.join(out_dir, spec["id"] + "_strip.png")

    in_args = ["-ss", f"{start_o:.6f}", "-t", f"{dur:.6f}", "-i", ovideo]
    if w_side:
        if w_side["frames"]:
            s = int(math.floor((w_side["bc_t"] - PRE) * w_side["fps"]))
            in_args += ["-start_number", str(s), "-framerate", str(w_side["fps"]),
                        "-t", f"{dur:.6f}", "-i",
                        os.path.join(w_side["frames"], "frame_%05d.png")]
        else:
            in_args += ["-ss", f"{w_side['bc_t'] - PRE:.6f}", "-t", f"{dur:.6f}",
                        "-i", w_side["video"]]
        right_src = f"[1:v]fps=30,scale={W}:{H}:flags=lanczos,setsar=1[w]"
    else:
        right_src = (f"color=c=0x161b21:s={W}x{H}:r=30:d={dur:.6f},"
                     f"drawtext=fontfile={FONTB}:text='oracle-only leg'::"
                     f"x=(w-text_w)/2:y=(h-text_h)/2-16:fontsize=26:fontcolor=0x8b949e,"
                     f"drawtext=fontfile={FONT}:text='{esc(willeq_note(spec))}'::"
                     f"x=(w-text_w)/2:y=(h-text_h)/2+18:fontsize=15:fontcolor=0x57606a[w]")

    f = [f"[0:v]fps=30,scale={W}:{H}:flags=lanczos,setsar=1[o]",
         right_src,
         "[o][w]hstack=inputs=2[s]",
         f"[s]pad={OUT_W}:{OUT_H}:0:{TOP}:color=0x101418[c]"]
    d = [f"drawbox=x=0:y={TOP}:w={OUT_W}:h=1:color=0x3a4148:t=fill",
         f"drawbox=x=0:y={OUT_H-BOT}:w={OUT_W}:h=1:color=0x3a4148:t=fill",
         f"drawbox=x={W}:y={TOP}:w=1:h={H}:color=0x3a4148:t=fill"]
    xb = 2                                         # BC line (clip t = 0)
    xa = int(round(act_off_o * 30))                # oracle Act line
    d.append(f"drawbox=x={xb}:y={TOP+1}:w=2:h={H-1}:color=0x37d5d3@0.55:t=fill")
    d.append(f"drawbox=x={xb+W}:y={TOP+1}:w=2:h={H-1}:color=0x37d5d3@0.55:t=fill")
    if act_off_o < dur:
        d.append(f"drawbox=x={xa}:y={TOP+1}:w=2:h={H-1}:color=0xf0b429@0.60:t=fill")
        d.append(f"drawbox=x={xa+W}:y={TOP+1}:w=2:h={H-1}:color=0xf0b429@0.60:t=fill")
    align = (f"oracle: {oracle.get('label','')} frame {f_bc_o} (BC) / {f_act_o} (Act) "
             f"@ {fps_g(fps_o)} fps, t0={t0:.3f}"
             + (f"  |  willeq: {w_side['label']} frame {w_side['f_bc']} (BC) / "
                f"{w_side['f_act']} (Act) @ 30 fps" if w_side else "  |  willeq: no render in this gate")
             + f"  |  window BC-{PRE:.1f}s..Act+{end_off:.1f}s ({N} frames @30)")
    d.append(dt(title, 14, 8, 21, bold=True))
    d.append(dt(align, 14, 34, 13, color="0xc9d1d9"))
    # clocks, each half in ITS OWN Act-relative time
    d.append(dt("t = Act%%{eif\\:t-%s\\:+0.000}" % f"{act_off_o:.6f}",
                14, H + TOP - 30, 17, color="0x58d5d3"))
    if act_off_w is not None:
        d.append(dt("t = Act%%{eif\\:t-%s\\:+0.000}" % f"{act_off_w:.6f}",
                    W + 14, H + TOP - 30, 17, color="0x58d5d3"))
    x = 14
    for name, verdict in spec.get("verdicts", []):
        col = VERDICT_COLOR.get(verdict, "0x8b949e")
        seg = f"{name} {verdict}"
        d.append(dt(seg, x, OUT_H - BOT + 8, 17, color=col, bold=True))
        x += len(seg) * 11 + 22
    if note:
        d.append(dt(note[:150], 14, OUT_H - BOT + 34, 13, color="0x8b949e"))

    # The upstream graph (f) produces pad [c]; the overlays (d) consume it.
    filter_complex = ",".join(f) + "," + "[c]" + ",".join(d)
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"] + in_args + [
        "-filter_complex", filter_complex, "-frames:v", str(N),
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "21",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart", clip]
    rc, out, err = L.run(cmd)
    if rc != 0:
        raise RuntimeError(f"ffmpeg clip failed for {spec['id']}:\n{err[-3000:]}")

    # ---- strip: 8 cells across the clip ------------------------------------
    offs = [-0.5, 0.0, 0.5, 1.5, 3.0, 5.0, 7.0, max(7.5, end_off - 0.5)]
    n_cells = "+".join(
        f"eq(n,{int(min(max(round((act_off_o + o) * 30), 0), N - 1))})" for o in offs)
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", clip,
           "-vf", f"select='{n_cells}',scale={W//2}:450:flags=lanczos,tile=8x1",
           "-frames:v", "1", strip]
    rc, out, err = L.run(cmd)
    if rc != 0:
        raise RuntimeError(f"ffmpeg strip failed for {spec['id']}:\n{err[-2000:]}")

    line = (f"[{spec['id']}] oracle {oracle.get('label','')} f{f_bc_o} (BC) / f{f_act_o} (Act) "
            f"@ {fps_g(fps_o)} fps (t0 {t0:.3f})"
            + (f" | willeq {w_side['label']} f{w_side['f_bc']} (BC) / f{w_side['f_act']} (Act) @ 30 fps"
               if w_side else " | willeq: oracle-only leg")
            + f" | window BC-{PRE:.1f}..Act+{end_off:.1f}s, {N} frames")
    print(line, file=sys.stderr)
    print(json.dumps({
        "id": spec["id"], "title": title, "group": spec.get("group", ""),
        "oracle_label": oracle.get("label", ""), "oracle_fps": fps_o,
        "oracle_t0": t0, "oracle_frame_bc": f_bc_o, "oracle_frame_act": f_act_o,
        "willeq_label": w_side["label"] if w_side else "",
        "willeq_frame_bc": w_side["f_bc"] if w_side else None,
        "willeq_frame_act": w_side["f_act"] if w_side else None,
        "bc_epoch": oracle["bc_epoch"], "act_epoch": oracle["act_epoch"],
        "window": f"BC-{PRE:.1f}..Act+{end_off:.1f}",
        "verdicts": "; ".join(f"{a}={b}" for a, b in spec.get("verdicts", [])),
        "clip": os.path.basename(clip), "strip": os.path.basename(strip),
    }))


def willeq_note(spec):
    g = spec.get("group", "")
    return {"observer-b1134": "B1134 observer leg -- no willeq render for this gate",
            "observer-b1143": "B1143 observer leg -- no willeq render for this gate"}.get(g,
            "no willeq render for this gate")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    arg, out_dir = sys.argv[1], sys.argv[2]
    if arg.endswith(".json"):
        spec_path = arg
    else:  # record id: the spec was written by make_ledger.sh into <out>/specs/
        spec_path = os.path.join(out_dir, "specs", arg + ".json")
    with open(spec_path) as f:
        spec = json.load(f)
    build(spec, out_dir)


if __name__ == "__main__":
    main()
