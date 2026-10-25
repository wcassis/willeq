# B1154 (#474) CORPSE-FADE ONSET: the alpha's render path, why the "0.6 s late onset" is a fit artifact, and what the low-alpha tail actually is

Branch `task/kb-b1154-corpse-fade` (wt kb-b1154), cut from fable 7bdf14a4. Evidence:
`/home/user/kb-evidence/kb-b1154-corpse-fade/` (per-frame curves, the 135-row table, the
oracle-vs-fable strip, the measure scripts, and the logs — including the harness run log
`logs/harness_run.log`).

## 0. Result

**(b) from step 3 of the card, on both observations in #474:**

1. **The "0.61 s late onset" is a linear-fitting artifact of a window that included the
   death animation — not a client/capture/display latency.** The fade walk's timing
   constants and delete threshold (re-verified in the .dis this unit, sec. 2) are: fade
   start 1300 ms (0x514), slope 0.0005/ms (0x63f4d4), clamps 1.0 (0x63f4d0) / 0.0
   (0x63f4cc), delete once `now − stamp ≥ 0xdac` (**3500 ms**). The stored alpha is
   consumed per frame with no threshold, floor, or extra timer in the fade walk itself,
   so the code's curve is 1.0 to +1.300, linear to 0.0 at +3.300, model removed at +3.500.
   Measured per frame from the raw leg-C1 video, the fade's **first falling frame is
   +1.352 true / +1.367 nominal** and the delete's **last visible frame is +3.519 true /
   +3.533 nominal** — the code's +1.300 and +3.500 within one capture frame each. Onset
   and delete matching within one frame rules out a uniform capture/display latency (one
   would shift both boundaries by the same amount).

2. **The low-alpha tail (code alpha 0, +3.300…+3.500) is an oracle-renderer residual, not
   the fade curve and not latency — documented with frame-level evidence (sec. 3.2), not
   fitted.** At code alpha 0 the oracle's pixels still show a visible, structured,
   decaying, semi-transparent corpse (box contrast 0.750 → 0.392 raw, i.e. 0.553 → 0.195
   above the 0.197 compression floor; a vision check of the tail frames finds a coherent
   figure, not noise), held until the model is removed at +3.5. The stored fade alpha
   (the client's curve) is exactly 0.0 in that window (the floor constant 0x63f4cc is
   0.0f, re-verified), so the residual is a behaviour of the official renderer's D3D draw
   between stored-alpha-zero and removal, not of the fade-alpha curve. fable's
   `ApplyCorpseFadeAlpha` writes the mesh's vertex alpha from the fade alpha (byte =
   alpha·255), so it is fully transparent at +3.300 and does **not** reproduce the
   residual tail. **No constant is fitted to the frames** (Will's standing rule): the
   residual's exact D3D rule (a minimum-opacity floor, a delayed/extended fade, or a
   second pass) sits in the downstream mesh-draw the decompilation splits across many
   functions, which this unit traced as far as it cleanly resolves (sec. 2.3) and found
   no multiply-alpha floor in. Fable's fade is left as the verified stored curve.

No code change: `corpse_fade.cpp/.h` and their test are untouched by this card — the
client's stored-fade-alpha schedule is the one re-confirmed above, so step 3's option
(a) (a threshold/quantisation rule to implement in `corpse_fade.cpp`) does not exist in
the traced client path; option (b) is the one the evidence supports. The existing tests
still pass (sec. 5).

## 1. Prior work found

- `refactor/B1148/README.md` sec. 2–3: the walk (0x401127), the store (the alpha reaches
  the model instance), the "0.61 s later onset, fit rms 0.040 of a 2.52 plateau"
  observation, and the two open follow-ups (this card is follow-up 1; 2 stays open).
  B1148's store description ("the alpha is copied to the model instance's per-frame alpha
  `+0x94`") is **corrected in sec. 2**: `+0x94` is an angle, and the fade alpha is
  `model+0x88`.
- `review/B1148/corpse_curve_oracle.tsv` (B1148's box, normalised to despawn+0.5):
  0.733 from −0.114 (the body standing), 1.000 by +0.5, the death-animation peak 2.74 at
  +1.3, falling, then the 0.197 compression floor after the model is removed.
- `review/B1130/README.md`: no packet-to-screen latency numbers exist there (grep for
  latenc/offset/shift/delay/first-frame: no hits; the only latency-adjacent value is
  B1118's NPC `releaseDelay` 0.33 s, a sim-side emitter release, unrelated to this render
  path). So this card's decision rests on the per-frame boundary match, not on a latency
  table.

## 2. The code (eqgame.exe / EQGraphicsDX9.dll, `review/B1141/ghidra/out/`)

### 2.1 The fade walk and the store (eqgame → the model instance)

| address | what (re-verified in the .dis this unit) |
|---|---|
| eqgame 0x401100 | the fade-list stamp: `call 0x46d655` (clock, ms), `+0xc = +0x10 = now`. Called from 0x401346 when the corpse's list entry is created (the death → fade-list add path, B1148 sec. 2), i.e. at list entry, not a later event. |
| eqgame 0x401127 | the per-corpse walk: `t = now − stamp` (0x401140/0x401149, the walk's `ebx` is the clock arg, `+0xc` the stamp). `t ≤ 0x3e8 (1000 ms)`: nothing (0x401152/0x401154). `1000 < t ≤ 0x514 (1300)`: arm the one-shot 0x5e windows (hands/chest/feet, via the actor's vtbl +0x134/+0x1a4). `t > 1300`: `alpha = 0x63f4d0 (1.0) − (t − 0x514) x 0x63f4d4 (0.0005)`, clamped to `[0x63f4cc (0.0), 1.0]` (0x401162–0x4011a8), then `call [actor->vtbl + 0x1b0]` with the alpha (0x4011b0). |
| DLL 0x10040110 (actor vtbl +0x1b0 = slot 108) | the store thunk: `add ecx, 0x64; jmp 0x1018fc20` — the actor (the fade entry's object at `entry+0x8`) reaches its model instance at `actor+0x64`. |
| DLL 0x1018fc20 | the model fade-alpha setter: `mov eax, [esp+4]; mov [ecx+0x88], eax; ret 4` — **the fade alpha is stored at `model+0x88`**. Its getter is the sibling thunk 0x10040120 → 0x1018fc10 (reads `model+0x88`). |
| eqgame 0x4012bb / 0x4012db–0x4012e7 | the list walk: runs 0x401127 per entry, then `now − stamp ≥ 0xdac` (**3500 ms**) → 0x401110 → **0x439a46 (the delete)** (0x4012e3 `cmp eax, 0xdac`; 0x4012e7 `jb`, i.e. delete once age ≥ 0xdac; the second branch 0x401316 is the same test on the second list). The fable harness's own log confirms the constant: "corpse removed at t 6.033 (3500 ms after despawn)" (`logs/harness_run.log`). |

**Correction to B1148's store:** B1148 sec. 2 said the alpha "is copied to the model
instance's per-frame alpha `+0x94`." That is not what the .dis shows. The fade walk's
`actor->vtbl[0x1b0]` dispatch lands on 0x10040110, which stores at **`model+0x88`**
(0x1018fc20). The field `+0x94` is a **different** field: the model update writes it with
the **orientation angle** (see 2.2), not the fade alpha.

### 2.2 `+0x94` is an angle, not the alpha (the flag-gated update the card named)

| address | what (re-verified in the .dis this unit) |
|---|---|
| DLL 0x1001d66f–0x1001d6b4 | the model update: `st(0) = (float)template->+0x68` (an angle in degrees, 0x1001d672), wrapped if negative (0x1001d679) and scaled by the degrees→radians constant (0x1001d67f, `[0x101c68a0]`). Then `model->+0x7c = st(0)` **always** (0x1001d68e, the primary angle). `test ah, 1` on `model->+0x140` (0x1001d685/0x1001d68b): **flag set** → `model->+0x94 = st(0)` (0x1001d693, a copy of the +0x7c angle) and `+0x170`/`+0xa4 = ebp`; **flag not set** (the 0x1001d685 else-branch, 0x1001d6a7) → `+0x170`/`+0xa4 = 0x3f000000` (0.5f) and `+0x94` is left as-is. So `+0x94` is a flag-gated copy of the `+0x7c` orientation angle, in radians — not the fade alpha. |
| DLL 0x1005f0f5–0x1005f0fc | the other model-update path: `test ecx, ecx; je` (skip if the gate is clear), else `model->+0x94 = model->+0x7c` (0x1005f0f9/0x1005f0fc) — the same angle copy, gated. |

The census of every `+0x94` read/write in the DLL (`census_94.py`, this unit) is
consistent: the object-field writes are these two angle sites; the rest are `esp+0x94`
stack temporaries.

### 2.3 The per-draw consumer and where the pixel alpha is actually applied

| address | what (re-verified in the .dis this unit) |
|---|---|
| DLL 0x1005c9f0 | the per-draw block the card named: it initialises a 4x4 identity (0x3f800000 = 1.0f on the diagonal, 0x1005ca01–0x1005ca2a) in the descriptor and then builds the world matrix from a per-frame global (0x102326f8) via its vtable (0x1005ca49/0x1005ca62/0x1005ca7a/0x1005ca92, negating the `+0x18` term). It is a **matrix builder**, not the vertex-format / blend-stage / alpha-test setup, and not the site where the fade alpha becomes a pixel alpha. |
| (downstream mesh draw) | the actual pixel-alpha application — the D3D material/vertex-format/blend-stage/alpha-test state and the mesh `Draw*` call that samples `model+0x88` — is split across the DLL's mesh-draw functions, which the decompilation does not present as a single traceable body. This unit searched the traced region for a multiply-alpha floor (`and reg, 0xff00ff` + `shl reg, 0x10` + loop-back: **0 candidates**; the DLL's only 0xff00ff sites are 0x100b35ab/0x1011d647, texture-format constants) and found none. So no floor/threshold/quantisation is **visible in the traced region**, but the final pixel rule sits in the opaque downstream draw and is not resolvable to a single instruction from the text decompiles. |

**Mechanism statement (acceptance A):** the stamp is taken at fade-list entry (eqgame
0x401100/0x401346); the per-frame walk (0x401127) computes `alpha = 1.0 − (t−1300) x
0.0005`, clamped `[0.0, 1.0]`, from `now − stamp` with the constants
0x3e8/0x514/0x63f4d4/0x63f4d0/0x63f4cc, and stores it at **`model+0x88`** via
`actor->vtbl[0x1b0]` (0x10040110) → 0x1018fc20 (the `+0x94` field is a separate,
flag-gated orientation-angle copy, not the alpha); no clamp, floor, threshold, or second
timer sits in the fade walk between the store and the model field; and the model is
deleted by 0x439a46 once the walk sees `now − stamp ≥ 0xdac` (**3500 ms**). The fade
alpha's final application to pixels is in the downstream mesh draw (2.3), which this
unit traced as far as the decompilation cleanly resolves and found no multiply-alpha
floor in.

## 3. The measurement (this unit, raw leg-C1 frames)

### 3.1 Method and the time convention

Frames: `ffmpeg -ss 458 -i video.mp4 -t 12 -fps_mode passthrough` (360 frames, 30 fps);
despawn video_t = 462.381 (`review/B1148/c1_events.tsv`, OP_DeleteSpawn); box
740,515,930,598 (B1148's); reference frame despawn+4.5 s (after the model is gone);
contrast = box |frame − ref| sum, normalised to despawn+0.5 (B1148's method, re-measured
here: `measure_corpse.py`, `fit_corpse.py`, `pin_two.py`, `compare_curves.py`,
`make_table.py`, `find_residual.py`, all in this host's evidence dir;
`corpse_curve_table.tsv` is the full 135-frame table).

**Two time axes, and the table uses both:** **nominal** `t = (frame − 132)/30`, where
frame 132 is the video frame at the despawn (video_t ≈ 462.367); **true** `t = nominal −
0.014`, where 0.014 s is the sub-frame offset (the despawn event at video_t 462.381 lands
0.014 s *after* frame 132's nominal time). So a boundary read on a frame index is ~0.014 s
late in nominal terms; the "true" column is the boundary relative to the actual despawn
event. (The fable harness has no such offset — its despawn is a sim tick, so its frames
are quoted nominal, and its nominal ≈ true to the frame step.)

### 3.2 Key frames (oracle column = leg-C1 video, fable column = harness, same normalisation)

| t nominal | t true | oracle frame | fable frame | oracle contrast | fable contrast | code alpha |
|---|---|---|---|---|---|---|
| +0.500 | +0.486 | 147 | 92 | 1.000 (standing) | 1.000 | 1.0 |
| +1.200 | +1.186 | 168 | 113 | 2.699 (animation lifting the body) | 1.000 | 1.0 |
| +1.300 | +1.286 | 171 | 116 | 2.740 (fade starts this frame, code) | 1.000 | 1.0 |
| +1.333 | +1.319 | 172 | 117 | **2.754 (oracle peak)** | **0.998 (fable first falling frame)** | 0.984 |
| **+1.367** | **+1.352** | **173** | 118 | **2.736 (oracle first falling frame)** | 0.996 | 0.967 |
| +1.800 | +1.786 | 186 | 131 | 2.551 | 0.928 | 0.750 |
| +2.300 | +2.286 | 201 | 146 | 2.054 | 0.746 | 0.500 |
| +2.800 | +2.786 | 216 | 161 | 1.454 | 0.450 | 0.250 |
| +3.167 | +3.153 | 227 | 172 | 0.949 | **0.116** | 0.067 |
| +3.233 | +3.219 | 229 | 174 | 0.854 | 0.036 (fable last visible) | 0.033 |
| +3.267 | +3.253 | 230 | 175 | 0.810 | 0.000 | 0.016 |
| **+3.300** | +3.286 | 231 | 176 | **0.750 (code alpha 0; oracle still visible)** | 0.000 | **0.0** |
| +3.400 | +3.386 | 234 | 179 | 0.601 | 0.000 | 0.0 |
| +3.500 | +3.486 | 237 | 182 | 0.441 | 0.000 | 0.0 |
| **+3.533** | **+3.519** | **238** | 183 | **0.392 (oracle last visible; fable model removed)** | 0.000 | 0.0 |
| +3.567 | +3.552 | 239 | 184 | 0.197 (compression floor) | 0.000 | 0.0 |

Notes on reading the table:

1. **The oracle's contrast is the box diff against a reference taken AFTER the model is
   gone**, so during the fade it is `visible_model(t) − empty_ground + compression`, and
   the dying body's pose adds to the diff. That is why the oracle's curve is convex in the
   low-alpha region and its peak is 2.754 at +1.333 (the death animation's own motion on
   top of the standing height) rather than 1.0. The fable harness uses a static death pose
   and PNG frames, so its contrast tracks the fade alpha closely (its first falling frame
   +1.333 = 0.998 against the code's 0.984, its +3.167 = 0.116 against the code's 0.067 —
   the box-diff reads slightly above the alpha at low values for the same silhouette
   reason). Its boundaries (first falling frame +1.333, model removed +3.533 per
   `logs/harness_run.log`) are the code's +1.300/+3.500 themselves, one frame on in a
   30 fps capture.
2. **The onset boundary.** The code's fade starts at +1.300 (alpha begins to fall). The
   oracle's *visible* first falling frame is **+1.352 true / +1.367 nominal** (frame 173,
   2.736), one-to-two frames after the code's +1.300 because the death animation's own
   contrast rise still dominates until the peak at +1.333 (2.754); the fade's first frames
   of fall are masked by that rise, then the contrast drops. This is the animation, not a
   fade delay and not a latency. The fable (static pose) first falling frame is +1.333,
   i.e. the code's onset within one frame.
3. **The delete boundary is independent of any fit.** On the oracle side the last frame
   with model contrast above the 0.197 compression floor is +3.533 nominal / +3.519 true
   (0.392); the next, +3.567, is at the floor (0.197, zero box pixels >60/756) — the first
   frame at/after the code's 3500 ms delete. On the fable side the model is removed from
   the scene at +3.533 (`logs/harness_run.log`: "corpse removed at t 6.033 (3500 ms after
   despawn)"), the same 3500 ms delete.

### 3.3 The low-alpha tail (code alpha 0, +3.300…+3.500)

This is the second observation in #474 ("~24 % visible at +3.3"), and it is what round 1
of review flagged as unexplained. The evidence:

- **The stored fade alpha is exactly 0.0 in this window.** The fade walk clamps at
  `0x63f4cc`, which `pe_floats.py` re-verified against the eqgame.exe image is **0.0f**
  (alongside 0x63f4d0 = 1.0 and 0x63f4d4 = 0.0005). So the client's fade-alpha curve — the
  thing `corpse_fade.cpp` computes — is 0.0 from +3.300, and the model is removed at
  +3.500. There is no floor above 0.0 in the client curve.
- **The oracle's pixels are not 0.0 in this window.** Above the 0.197 compression floor,
  the oracle's box contrast is **0.553 at +3.300, 0.504 at +3.333, 0.452 at +3.367, 0.404
  at +3.400, 0.346 at +3.433, 0.305 at +3.467, 0.244 at +3.500, 0.195 at +3.533** — a
  visible, *decaying* signal held until the model is removed, then the 0.197 floor. It is
  not a constant floor (it decays), so it is a residual fade the renderer is still
  running, not a fixed minimum opacity.
- **It is a structured corpse, not noise.** A vision check of the tail frames
  (`resid_3_crop.png`, `resid_4_crop.png`, the +3.3 s and +3.5 s crops) finds a coherent,
  semi-transparent humanoid figure lying on the ground (~10–22 % opacity by eye); the
  per-pixel |frame(+4.2) − frame(+4.5)| map (after the model is gone) has zero pixels
  above 60/756, so the 0.197 floor itself is video compression, not a held model — the
  tail signal above it, in the +3.300…+3.533 window where the model is still present, is
  the model.
- **Fable does not reproduce it.** `ApplyCorpseFadeAlpha` (corpse_fade.cpp) sets every
  mesh vertex's alpha byte to `alpha·255` and switches the material to a blended type; at
  alpha 0 the byte is 0 and the corpse is fully transparent, so fable's contrast is 0.000
  from +3.267 (the model is fully faded but still in the scene) and it is removed from the
  scene at +3.533. Fable's last visible frame is +3.233 (0.036); the oracle's is +3.519
  true / +3.533 nominal — a ~9-frame gap, all inside the code-alpha-0 window.

**Why it is not latency and not the fade curve, and why it is not fitted:** a uniform
capture/display latency would shift *both* boundaries by the same amount, but the onset
and the delete each match the code within one capture frame (3.2), so no uniform lag is
present. The stored fade alpha (the client curve) is 0.0 in the window (the 0x63f4cc floor
constant), so the residual is not the fade curve. It is a behaviour of the official
renderer's D3D draw between stored-alpha-zero and removal. Its exact rule (a
minimum-opacity floor, a delayed/extended fade, or a second pass) is in the downstream
mesh-draw (2.3), which the text decompiles do not present as a single traceable body and
which this unit found no multiply-alpha floor in. Reproducing the residual in
`corpse_fade.cpp` would therefore require either resolving that opaque D3D rule or fitting
a residual constant to the frames — the latter is exactly what Will's "never fit a
constant to the frames" rule forbids. It is documented, not implemented.

**Acceptance B, restated honestly:** fable's fade matches the oracle **at the two
boundaries of the fade curve** within one capture frame — the fade's start (code +1.300;
fable first falling frame +1.333; oracle visible first fall +1.352 true, the 1–2 frame
offset being the death animation's own rise) and the delete (code +3.500; fable model
removed +3.533; oracle last visible +3.519 true) — with the fall between them at the
code's 0.5/s slope. **It does not match the oracle's low-alpha tail**: in the
code-alpha-0 window (+3.300…+3.500) the oracle holds a visible, decaying, semi-transparent
corpse (sec. 3.3) that fable, which tracks the stored fade alpha to 0, does not render.
That tail is an oracle-renderer residual, documented with the per-frame evidence above; it
is not attributed to latency (the boundary match rules that out) and it is not fitted into
fable's curve.

## 4. Lane / harness

No lane-5 observer leg was run: the card allows one only if a second capture is required
to separate latency from mechanism, and the per-frame boundary match on B1148's existing
leg-C1 capture already does that separation (a uniform latency would shift both
boundaries by the same amount; they do not). The fable side was rendered on the ThinkPad
with B1148's recipe and measured on this host (B1139-style box contrast), as the card
prescribes. The harness run log is `logs/harness_run.log` (camera fit, the death/despawn
ticks, and the "corpse removed at t 6.033 (3500 ms after despawn)" line).

One run detail, so the harness run is reproducible: B1148's render ran on the ThinkPad
with the Intel card's DRM unusable (the run.log shows "DRM: no usable /dev/dri/card*
(no card opens) - falling back to surfaceless EGL", llvmpipe). A plain run this unit
auto-selected the card's 4K mode and the camera fit put every model off-frame (396
byte-identical frames). Re-running with `WILLEQ_DRM_DEVICE=/dev/dri/card99` (the device's
explicit override: "card does not open (explicit override, no fallback)", surfaceless
llvmpipe) reproduces B1148's camera fit exactly ("camera b1118: dist 29.050 look 5.552 …
rows 378..605", its run.log's numbers, `logs/harness_run.log`) and the real frames. Run
dir on the ThinkPad `/home/user/kb-frames/kb-b1154-corpse-fade/b1154b_sep_4499_die/`,
synced here as `fable_frames_b/`.

## 5. Builds / tests

- `rbuild kb-b1154-corpse-fade configure` (CI's Release GLES2 configure) — clean.
- `rbuild kb-b1154-corpse-fade --target test_corpse_fade --target model_viewer` — clean.
- `rbuild kb-b1154-corpse-fade test -R 'Entities|DeleteSpawn|CorpseFade'` with
  `EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999`: 11/11 pass; the EDT_NULL half of
  `test_corpse_fade.cpp` (6 tests) skips under the rbuild ctest wrapper (the client path
  does not propagate into the wrapper's ctest environment in this build tree). The
  **direct binary run on the ThinkPad passes all 13** (`logs/test_corpse_fade_full.txt`).
  `logs/ctest_targeted.log` is the rbuild run.
- `rbuild kb-b1154-corpse-fade hygiene`: 48/48 (`logs/ctest_hygiene.log`).
- **The MR diff is docs-only** (`refactor/B1154/README.md`; no C++/test changed), so by
  **B1042 the MR pipeline runs the hygiene stage only — no build or gate jobs are
  created**. (Round 1's metadata called pipeline 2074 "hygiene + build + gate"; that was
  the error — a docs-only diff produces a hygiene-only pipeline. This round's pipeline is
  the same shape.) No whole-tree CI-flag build is needed for a docs-only diff.

## FOLLOW-UP

1. (B1148's follow-up 2 stays as is) Source term for other races: only Dark Elf
   female/male are measured; `cast_tracks.tsv` lists the rest from model data.
2. The low-alpha tail (sec. 3.3) is the natural home for a follow-up that resolves the
   official renderer's residual-opacity rule in the D3D mesh draw (or a controlled
   second capture) — out of this card's file territory (the mesh draw is not
   `corpse_fade.cpp`), and it must not be closed by fitting a constant to the frames.
3. The ctest propagation of `EQ_CLIENT_PATH` through the rbuild wrapper (the EDT_NULL
   half of `test_corpse_fade.cpp` runs green only as a direct binary run on the build
   host in this tree) is a tooling gap, not a corpse-fade one: a one-line fix (export the
   var in the wrapper's test env) belongs to the rbuild/B1087 tooling, not this card's
   file territory.
