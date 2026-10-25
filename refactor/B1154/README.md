# B1154 (#474) CORPSE-FADE ONSET: the alpha's render path, and why the "0.6 s late onset" is a fit artifact

Branch `task/kb-b1154-corpse-fade` (wt kb-b1154), cut from fable 7bdf14a4. Evidence:
`/home/user/kb-evidence/kb-b1154-corpse-fade/` (per-frame curves, table, strip, logs, measure
scripts).

## 0. Result

**(b) from step 3 of the card: the code produces the 1300 ms curve, and the oracle's video
shows that curve frame-for-frame. fable's fade is left as is (no code change), and the
"0.61 s late onset" that B1148's fit reported is a linear-fitting artifact of a window that
included the death animation, not a client/capture/display latency.**

- The fade walk's timing constants and delete threshold, re-verified in the .dis this unit
  (sec. 2): 1300 ms (0x514) start, 0.0005/ms (0x63f4d4) slope, 1.0 (0x63f4d0) / 0.0 (0x63f4cc)
  clamps, delete once `now − stamp > 0xdac` (3499 ms). The timer `t` is `clock() − stamp`,
  and the stamp is taken at the moment the corpse is put on the fade list (0x401100, called
  from 0x401346 when the list entry is created) — not from a later event.
- The stored alpha is consumed per frame with **no threshold, floor, quantisation gate, or
  extra timer** in between (sec. 2, DLL side): the walk writes `actor+0x48` (DLL 0x1003d880),
  the model update copies it to the model instance's per-frame alpha `+0x94`
  (0x1001d693/0x1005efd4), and the per-draw block copies it into the vertex-colour/alpha
  state (0x1005cbc7/0x1005cc7b). No D3DXColorMultiplyAlpha-style loop exists in the DLL
  (search for `and reg, 0xff00ff` + `shl reg, 0x10` + loop: 0 candidates; the only 0xff00ff
  sites are texture-format constants). So the alpha reaches the pixels as float→8-bit
  vertex alpha; the only quantisation is the 1/255 byte step, invisible at 30 fps sampling.
- **Measured per frame from the raw leg-C1 video** (30 fps; despawn at video 462.381 s;
  B1148's box 740,515,930,598; contrast normalised to despawn+0.5, B1148's method,
  re-measured here — `corpse_curve_table.tsv` has all 135 frames): the model's contrast is
  at 1.000 (its standing height in this normalisation) from +0.5, and the fade's first
  falling frame is **+1.333** — the first frame at/after the code's +1.300 (the previous
  frame, +1.300, is still 1.000-equivalent; in absolute contrast it is the peak, 2.740,
  because the death animation is still lifting the body). The contrast then falls,
  frame-by-frame, to 0.441 at **+3.500** and cuts to the video's compression floor (0.197,
  zero box pixels >60/756) at **+3.567** — the first frame at/after the code's 3499 ms
  delete. Onset +1.333 and delete +3.567 match the code's +1.300/+3.499 **within one
  capture frame each**, so a uniform capture/display latency is impossible (one would shift
  both boundaries by the same amount).
- The post-fade "0.197 residual" B1148 reported from +3.59..+4.29 is **video compression**,
  not a held model: the per-pixel |frame(+4.2) − frame(+4.5)| map has zero pixels above 60/756
  (JPEG noise ≤ ~20/756), a vision check of the +3.5 s and +4.2 s frames finds only empty
  ground in the box, and adjacent post-fade frames differ frame-wide by ~1e6 (the
  observer's own idle animation; the corpse box is 2.9 % of that frame-wide motion).
- Fable's harness, run with B1148's exact recipe (surfaceless llvmpipe, `--camera b1118`,
  `sep_4499_nolanding`, target death at Act, despawn +1.027, seed 1 — see the WILLEQ_DRM_DEVICE
  note in sec. 4), reproduces the same shape: contrast 1.000 to +1.300, first falling frame
  +1.333 (0.998), 0.014 at +3.167, 0.000 from +3.267 (the code's alpha reaches 0 at +3.300),
  and the model is removed from the scene at +3.533 (harness log: "corpse removed at t 6.033
  (3500 ms after despawn)", despawn t 2.533) — the code's 3499 ms delete, one frame later.
- **Why B1148's fit said "+0.61 s":** it fitted a fixed slope 0.5/s over a window that
  included +0.6..+1.9, where the measured contrast is still CLIMBING (the troll is mid-fall
  at +1.6 in the video — the death animation's own timing: 0.733 at +0.0 → 1.000 at +0.5 →
  2.74 peak at +1.3). Fitting a line to data that rises on the left side of the window and is
  clipped at the compression floor on the right pulls the intercept late (B1148: +1.91, i.e.
  +0.61 past the code's +1.3; free-slope fit: +0.43). The per-frame table shows the climb is
  the animation and the fall starts at the code's +1.300. **No constant is fitted to the
  frames in this unit** — the comparison is per-frame measured vs per-frame computed.

No code change: `corpse_fade.cpp/.h` and their test are untouched by this card (their
schedule is the client's schedule, re-confirmed above), so step 3's option (a) (a
threshold/quantisation to implement) does not exist in the code, and option (b) is the one
the evidence supports. No unit test red→green is needed or made; the existing tests still
pass (sec. 5).

## 1. Prior work found

- `refactor/B1148/README.md` sec. 2–3: the walk (0x401127), the store (DLL 0x1003d880 → actor
  +0x48 → model +0x94), the "0.61 s later onset, fit rms 0.040 of a 2.52 plateau" observation,
  and the two open follow-ups (this card is follow-up 1; 2 stays open).
- `review/B1148/corpse_curve_oracle.tsv` (B1148's box, normalised to despawn+0.5): 0.733 from
  −0.114 (the body standing), 1.000 by +0.5, falling to 0.000 by +3.55..+3.7, then the 0.197
  compression floor until +4.29.
- `review/B1130/README.md`: no packet-to-screen latency numbers exist there (grep for
  latenc/offset/shift/delay/first-frame: no hits; the only latency-adjacent value is B1118's
  NPC `releaseDelay` 0.33 s, a sim-side emitter release, unrelated to this render path).
  So this card's decision rests on the per-frame boundary match, not on a latency table.

## 2. The code (eqgame.exe / EQGraphicsDX9.dll, `review/B1141/ghidra/out/`)

| address | what (re-verified in the .dis this unit) |
|---|---|
| eqgame 0x401100 | the fade-list stamp: `call 0x46d655` (clock, ms), `+0xc = +0x10 = now`. Called from 0x401346 when the corpse's list entry is created (the death → fade-list add path, B1148 sec. 2), i.e. at list entry, not at a later event. |
| eqgame 0x401127 | the per-corpse walk: `ebx = now − stamp` (0x401149-0x40114b, the walk's `eax` is the clock, `+0xc`/`+0x10` the stamp). `t ≤ 0x3e8 (1000 ms)`: nothing (0x401152/0x401154). `t > 0x514 (1300)`: `alpha = 0x63f4d0 (1.0) − (t − 0x514) x 0x63f4d4 (0.0005)`, clamped to `[0x63f4cc (0.0), 1.0]` (0x401162-0x4011a8), passed to the actor's vtbl +0x1b0 (0x4011b0). Between 1000 and 1300 ms the walk arms the one-shot 0x5e windows (hands 0x4011b6-0x4011ee, chest 0x40121b-0x40124d, feet 0x401260-0x4012a6, via vtbl +0x134). |
| eqgame 0x4012bb/0x4012db-0x4012e7 | the list walk: runs 0x401127 per entry, then `now − stamp > 0xdac (3499 ms)` → 0x401110 → **0x439a46 (the delete)** (0x4012e3/0x4012e7; the second branch 0x401316 is the same test on the second list). |
| DLL 0x1003d880 (actor vtbl +0x1b0) | stores the alpha at `actor+0x48` (B1148 sec. 2: `mov eax, [esp+4]` at 0x1003d880 of the .dis, the thunk tail calling the model side) and forwards it to the model instance. |
| DLL 0x1001d693 / 0x1005efd4 | the model update: per frame the alpha is written to the model instance's `+0x94` (0x1001d693 `fstp [esi+0x94]` from the current alpha in `esi+0x7c`; 0x1005efd4 the same in the other update path; 0x1005f0fc copies `+0x7c` → `+0x94` when set). `+0x94` is a plain float field: 17 read sites in the DLL; all of them `esp+0x94` stack-matrix temporaries **except** two object-field reads, 0x1005cbc7 and 0x1005cc7b, both of which copy `+0x94` (with +0x90/+0x98/+0x9c/+0xa0) into a contiguous per-draw state block right before the draw call (0x1005c9f0 / the sibling). |
| render use | the block those copies feed is the per-draw vertex-colour/alpha state. No D3DXColorMultiplyAlpha-style loop exists (the `and 0xff00ff` + `shl 0x10` + loop search returns 0 hits; the DLL's only 0xff00ff sites are 0x100b35ab/0x1011d647, texture-format constants). So the stored alpha reaches the pixels as the vertex alpha with **no threshold, floor, alpha-test cutoff, or DLL-side timer** in between — at most the 8-bit alpha byte (1/255 steps, invisible at 30 fps). |
| fable | `include/client/graphics/corpse_fade.h` holds the same schedule: untouched to 1300 ms, `alpha = clamp(1 − (t−1300) x 0.0005, 0, 1)`, deleted on the first walk past 3499 ms — the code's numbers, one copy; `ApplyCorpseFadeAlpha` writes vertex alpha + the blended material twins (B1148). |

**Mechanism statement (acceptance A):** the stamp is taken at fade-list entry (eqgame
0x401100/0x401346); the per-frame walk (0x401127) computes the alpha from `clock() − stamp`
with the constants 0x3e8/0x514/0x63f4d4/0x63f4d0/0x63f4cc and stores it (actor +0x48 via
DLL 0x1003d880 → model +0x94 via 0x1001d693/0x1005efd4 → the per-draw block via
0x1005cbc7/0x1005cc7b); no clamp, floor, threshold, or second timer sits between the store
and the pixels; the model is deleted by 0x439a46 once the walk sees `now − stamp > 0xdac`
(3499 ms).

## 3. The measurement (this unit, raw leg-C1 frames)

Frames: `ffmpeg -ss 458 -i video.mp4 -t 12 -fps_mode passthrough` (360 frames, 30 fps,
frame N → 458 + (N−1)/30); despawn video_t = 462.381 (B1148 `c1_events.tsv`); box
740,515,930,598 (B1148's); reference frame despawn+4.5 s; contrast = box |frame−ref| sum,
normalised to despawn+0.5 (B1148's method, re-measured here: `measure_corpse.py`,
`fit_corpse.py`, `pin_two.py`, `compare_curves.py`, `make_table.py`, `find_residual.py`,
all in this host's evidence dir; `corpse_curve_table.tsv` is the full 135-frame table).

Key frames (oracle column = leg-C1 video, fable column = harness, same normalisation):

| t after despawn | oracle frame | fable frame | oracle contrast | fable contrast | code alpha |
|---|---|---|---|---|---|
| +0.500 | 147 | 92 | 1.000 (standing) | 1.000 | 1.0 |
| +1.200 | 168 | 113 | 2.699 (animation lifting the body) | 1.000 | 1.0 |
| +1.300 | 171 | 116 | 2.740 (peak; fade starts this frame, code) | 1.000 | 1.0 |
| **+1.333** | **172** | **117** | **2.754 → 2.736 (first falling frame)** | **0.998** | **0.984** |
| +1.800 | 186 | 131 | 2.551 | 0.928 | 0.750 |
| +2.300 | 201 | 146 | 2.054 | 0.746 | 0.500 |
| +2.800 | 216 | 161 | 1.454 | 0.450 | 0.250 |
| +3.200 | 228 | 173 | 0.899 | 0.076 | 0.050 |
| +3.500 | 237 | 182 | 0.441 (last fading frame) | 0.000 | 0.0 |
| **+3.533** | **238** | **183** | **0.392 → 0.197 (cut: first frame at/after the 3499 ms delete)** | **0.000** | **0.0** |
| +3.567 | 239 | 184 | 0.197 (compression floor) | 0.000 | 0.0 |

Two notes on reading the table:

1. **The oracle's contrast is the box diff against a reference taken AFTER the model is
gone**, so during the fade it is `visible_model(t) − empty_ground + compression`, and the
dying body's pose (where it lies, how it overlaps the ground) adds to the diff. That is why
the oracle's curve is convex in the low-alpha region (B1148 noted the same in the harness:
"the model draws without depth write, so overlapping parts stack") and why its peak is
2.74 instead of 1.0 (the death animation's own motion on top of the standing height). The
fable harness uses a static death pose and PNG frames, so its contrast is the pure alpha —
its first-falling-frame (+1.333, 0.998) and delete (+3.533) are the code's boundaries
themselves, and the oracle's are within one capture frame of the same.
2. **The delete boundary is independent of any fit.** On the oracle side the last frame with
model contrast is +3.533 (0.392), the next +3.567 is at the compression floor (0.197, zero
box pixels >60/756) — the first frame at/after 3.499 s, exactly the code's delete. On the
fable side the contrast is 0.000 from +3.267 because the code's alpha itself reaches 0 at
+3.300 (the model is fully faded but still in the scene), and it is removed from the scene
at +3.533 — the same 3499 ms delete, one frame later in a 30 fps capture.

**Acceptance B:** fable's fade matches the oracle within one capture frame step at both
boundaries (fade start +1.300 code / +1.333 first falling frame in both; delete +3.499 code
/ +3.533..+3.567 first frame at/after it), with the fall between them at the code's slope
within fit noise — the table (and `corpse_curve_table.tsv`) is the evidence; no latency
attribution is needed, and none is claimed.

## 4. Lane / harness

No lane-5 observer leg was run: the card allows one only if a second capture is required to
separate latency from mechanism, and the per-frame boundary match on B1148's existing
leg-C1 capture already does that separation (a uniform latency would shift both boundaries
by the same amount; they do not). The fable side was rendered on the ThinkPad with B1148's
recipe and measured on this host (B1139-style box contrast), as the card prescribes.

One run detail, so the harness run is reproducible: B1148's render ran on the ThinkPad with
the Intel card's DRM unusable (the run.log shows "DRM: no usable /dev/dri/card* (no card
opens) - falling back to surfaceless EGL", llvmpipe). A plain run this unit auto-selected
the card's 4K mode and the camera fit put every model off-frame (396 byte-identical
frames). Re-running with `WILLEQ_DRM_DEVICE=/dev/dri/card99` (the device's explicit
override: "card does not open (explicit override, no fallback)", surfaceless llvmpipe)
reproduces B1148's camera fit exactly ("camera b1118: dist 29.050 look 5.552 … rows
378..605", its run.log's numbers) and the real frames. Run dir on the ThinkPad
`/home/user/kb-frames/kb-b1154-corpse-fade/b1154b_sep_4499_die/`, synced here as
`fable_frames_b/`.

## 5. Builds / tests

- `rbuild kb-b1154-corpse-fade configure` (CI's Release GLES2 configure) — clean.
- `rbuild kb-b1154-corpse-fade --target test_corpse_fade --target model_viewer` — clean.
- `rbuild kb-b1154-corpse-fade test -R 'Entities|DeleteSpawn|CorpseFade'` with
  `EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999`: 11/11 pass; the EDT_NULL half of
  `test_corpse_fade.cpp` (6 tests) skips under the rbuild ctest wrapper (the client path does
  not propagate into the wrapper's ctest environment in this build tree — B1148's tree ran it
  because its build dir sat under the NFS the wrapper's cwd saw). The **direct binary run on
  the ThinkPad passes all 13** (`logs/test_corpse_fade_full.txt`, the same net B1148 used for
  this binary). `logs/ctest_targeted.log` is the rbuild run.
- `rbuild kb-b1154-corpse-fade hygiene`: 48/48.
- No code changed, so no new build is required for the MR; the whole-tree CI-flag build runs
  in the MR pipeline.

## FOLLOW-UP

1. (B1148's follow-up 2 stays as is) Source term for other races: only Dark Elf female/male
   are measured; `cast_tracks.tsv` lists the rest from model data.
2. The ctest propagation of `EQ_CLIENT_PATH` through the rbuild wrapper (the EDT_NULL half of
   `test_corpse_fade.cpp` runs green only as a direct binary run on the build host in this
   tree) is a tooling gap, not a corpse-fade one: a one-line fix (export the var in the
   wrapper's test env) belongs to the rbuild/B1087 tooling, not this card's file territory.
