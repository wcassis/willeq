# B1097 (#417): re-bless the Pi (lane 1) visual-gate goldens at the fable tip

Will's RULING on #417 (note of 2026-10-19 ~19:45Z, relayed by the orchestrator,
verbatim from the issue):

> **RULING (Will, 2026-10-19 ~19:45Z, relayed by the orchestrator).** B1023
> (#343, MR !255) corrected the premise: the Pi itself renders D24 (Lima never
> offers an XRGB8888+Z16 config), so the committed Pi goldens are a pre-B976
> **D24** image, lane 2 (Rock64, Lima, D24) is the Pi's twin, and lane 3
> (Panfrost, D16) is the odd one out.
> - Decision 1: **no bias change, no per-surface goldens.** The gap is simply
>   that the Pi goldens predate B976: **re-bless the Pi goldens on lane 1 at
>   the fable tip** (bless pre-approved by Will; card B1097 on the board, the
>   golden commit is the bless). Then measure lane 2 against the new goldens,
>   report only.
> - Decision 2: **no per-lane golden sets.** If/when a Panfrost lane is
>   gated, a frame-wide 1-LSB bound counts as driver-identical. No code now.
> - **No upstream Mesa ask** (the Lima mixed_color_depth_bits one-liner) for
>   now -- Will's explicit hold.

BLESS EXCEPTION (same ruling): this card MAY write `tests/goldens/pi/**` and
the `platforms.pi` section of `tests/goldens/manifest.json`, and nothing else
bless-only. The golden commit is the bless. NO code, NO bias, NO preset.

## Inputs (verified present at run start, logged in `lane1_run1/device.txt`)

- **Binary sha: `c03c44a03712fcf7efee72207a3dc14451d48867`** (fable tip at
  run start; >= the card's floor `c03c44a0`, which contains B1096/B1099;
  zone_depth_tie_break.h -- the B976 fix -- is present at this sha and absent
  at `55ec17b3`, the sha the committed goldens were blessed at, B1037).
  - Lane 1: `/mnt/projects/fable/ci-arm/c03c44a0.../bin/gles2_zone_benchmark-nollvm`
    (armhf, BUILD_INFO: built 2026-10-19T17:41:56+00:00 on gitlab-runner-1,
    Mesa 25.2.8, the shipped LLVM-less prefix `/home/user/mesa-nollvm-25.2.8`).
  - Lane 2: `/mnt/projects/fable/ci-arm64/c03c44a0.../bin/gles2_zone_benchmark`
    (aarch64, plain name -- the aarch64 publish ships no `-nollvm` binary,
    system Mesa; BUILD_INFO: built 2026-10-19T17:44:11+00:00 on thinkpad).
- Gate invocation source of truth: `scripts/ci/visual_gate.py`
  (`run_benchmark()` with the `--platform pi` `PlatformConfig`: bench,
  `--zone`, `--eq-path /mnt/projects/EverQuestP1999`, `--frames 60 --warmup 5
  --res 800 600`, `--dump-frames 0,1,2`, `--dump-png-prefix <out>/<zone>`,
  `--csv`, `--memory-json`, `--occluder-budget 16`,
  `productionConfig.args`, the zone's `extraArgs` verbatim from the manifest,
  then `--drm`). `--bless --platform pi` is refused by design; the Pi set is
  captured with `--capture DIR` (the device-side equivalent runs the same
  command lines under the lane lock) and committed by the orchestrator -- here
  by this card under the bless exception.
- The committed (pre-B976) Pi goldens `tests/goldens/pi/<zone>/f<n>.png` +
  `manifest["platforms"]["pi"]`; lanes per `scripts/pi/env.sh`.

## Recipe (this file was written before any run)

### Lane 1 (lane 1, the Pi): capture run 1 + capture run 2

`PI_LANE=1`, `pi_lock_take B1097` (atomic noclobber ON the device; non-zero
exit = stop), `pi_hdmi_force_on` (headless DRM; `pi_hdmi_restore` at cleanup,
both from env.sh). The driver (`b1097_driver_lane1.sh`):

1. `mkdir /tmp/b1097g/{run1,run2}`; copy the worktree's
   `tests/goldens/manifest.json` to each as `manifest.json` (poses read from
   the manifest's OWN `platforms.pi` section -- never transcribed by hand, so
   each command line is byte-identical to `run_benchmark()`).
2. Per run, per zone (qeynos2, gfaydark, befallen, iceclad, timorous -- the
   five manifest gate zones, manifest order): the gate's exact `--platform pi`
   benchmark invocation (B1023's proven runner shape: the benchmark runs as a
   live process; the driver resolves the benchmark's child pid through
   `/proc/<pid>/task/<pid>/children`, then snapshots `/proc/<pid>/maps` the
   moment libgallium is mapped -- the gate's `poll_maps` driver-load proof --
   and a watchdog enforces a 400 s per-zone bound so the run can never hold
   the lane open-ended). Per-zone benchmark stdout is kept in
   `run<n>/<zone>.log`; flat frames are moved into the committed layout
   `run<n>/<zone>/f<n>.png`. `WILLEQ_DATA_ROOT=<publish sha dir>`,
   `WILLEQ_CACHE_DIR=/tmp/b1097g/run<n>/cache`.
3. Pull both run dirs back over ssh to
   `/home/user/kb-evidence/kb-b1097-pi-rebless/lane1_run1/` and
   `.../lane1_run2/`; `pi_hdmi_restore`; `pi_lock_release B1097`; verify
   `pgrep -af gles2_zone_benchmark` empty on the lane. The lane is left as
   found (never rebooted; connector status restored from env.sh's record).

`capture.log` = the concatenation of run 1's per-zone benchmark stdouts
(`<zone>.log`), with a header line per zone, in manifest zone order.

### Local compares (the gate's OWN compare rule, never a new one)

`b1097_compare.py` imports `scripts/ci/visual_gate.py` from the B1097 worktree
and calls `compare_images_exact()` unmodified (decoded-pixel RGB, the one
compare rule), plus the gate's metrics rule (draws/stateChanges/polys exact
vs the section):

- **BEFORE table** (`mode before`): lane1_run1 frames vs the COMMITTED
  goldens + metrics vs the committed `platforms.pi` section. Acceptance B:
  it must reproduce B1023's finding -- 3 frames identical
  (qeynos2 f1, befallen f1, befallen f2), 12 frames differing by exactly
  B1037's pre-fix lane-2 pixel counts (134 492 / 290 / 36 / 565 / 693 /
  37 / 1 231 / 51 940 / 13 619 / 114 732 / 1 218 / 143 514), metrics bit-exact
  in all 15. ANY frame outside that pattern = STOP + `kanban block` with the
  numbers.
- **Determinism** (`mode determinism`): lane1_run1 vs lane1_run2 decoded
  pixels, 15/15 identical required. If not identical: the per-frame diff
  counts go in this README and the task blocks.
- **Lane 2** (`mode lane2`): lane2_run1 frames vs the NEW goldens after the
  bless commit + metrics vs the NEW section. Report only.

### The bless (the golden commit)

- Copy lane1_run1's 15 frames into `tests/goldens/pi/<zone>/f<n>.png`
  (the committed layout, `golden_frame_path()`).
- Build the new `platforms.pi` section with the gate's OWN
  `capture_pi()`/`_pi_memory_fields()` against a staging dir assembled from
  lane1_run1 (frames + CSVs + mem JSONs + zone logs; the maps files are
  absent locally, so `check_maps_text` reports them and the section write
  happens before the failure -- the device maps proofs are kept separately in
  the evidence). The staging layout matches `capture_pi`'s exactly, so the
  section is the gate's own output, not a hand-write.
- `tests/goldens/manifest.json`: replace `manifest["platforms"]["pi"]` with
  the captured section, changing NO other manifest field. One commit,
  message `B1097: re-bless Pi goldens post-B976 (Will 2026-10-19, #417)`,
  listing the 15 sha256s and, per manifest field that changed, old -> new and
  why (the metrics are expected bit-exact vs the committed section -- the
  committed section's metrics already come from post-B976 renders' CSV rows;
  say so if they are).

### Lane 2 (lane 2, Rock64): one run against the NEW goldens

`PI_LANE=2`, `pi_lock_take B1097`, `pi_hdmi_force_on`; the SAME published
`c03c44a0` aarch64 binary, the gate's exact invocation, into
`/tmp/b1097g/lane2`; pull back to
`/home/user/kb-evidence/kb-b1097-pi-rebless/lane2_run1/`;
`pi_hdmi_restore`; `pi_lock_release B1097`; leftover check. REPORT ONLY:
per-frame diff px vs the NEW goldens, side by side with B1037's pre-fix
lane-2 numbers. Expected per the ruling: the floor locus now matches (the
new goldens ARE the D24 image, and lane 2 is the Pi's Lima D24 twin); say
what remains. NO lane-2 goldens, NO bound change.

### Finish

- `docs/ci.md`: one sentence -- the Pi goldens are post-B976 as of
  2026-10-19 (#417); a Panfrost lane, if gated, is judged with a frame-wide
  1-LSB bound (ruling, no code).
- This README: both tables, the ruling verbatim (above), the sha.
- `rbuild <wt> hygiene` green; push WITH CI; `glab mr create` (Closes #417);
  the pipeline's visual gate is x86 (unaffected -- the Pi lane is not in CI,
  so the lane-1 evidence here is the proof); `capture.log` + both tables
  linked from the MR.

## Results

**Published sha (quoted everywhere): `c03c44a03712fcf7efee72207a3dc14451d48867`**
(>= the card's floor `c03c44a0`). Lane 1 ran
`gles2_zone_benchmark-nollvm` (shipped LLVM-less Mesa 25.2.8 prefix, the
gate's own maps proof per zone); lane 2 ran `gles2_zone_benchmark`
(system Mesa, no -nollvm binary in the aarch64 publish). Both runs used the
worktree's manifest (poses from its OWN platforms.pi section; the runner's
self-check verified every pose word against each logged command line).

### Lane 1 BEFORE table (run 1 vs the committed pre-B976 goldens; the gate's own compare_images_exact + metrics rule)

| zone | frame | pixels vs OLD golden | metrics |
|---|---|---|---|
| qeynos2 | f0 | differs (134,492 px) | bit-exact |
| qeynos2 | f1 | IDENTICAL | bit-exact |
| qeynos2 | f2 | differs (290 px) | bit-exact |
| gfaydark | f0 | differs (36 px) | bit-exact |
| gfaydark | f1 | differs (565 px) | bit-exact |
| gfaydark | f2 | differs (693 px) | bit-exact |
| befallen | f0 | differs (37 px) | bit-exact |
| befallen | f1 | IDENTICAL | bit-exact |
| befallen | f2 | IDENTICAL | bit-exact |
| iceclad | f0 | differs (1,231 px) | bit-exact |
| iceclad | f1 | differs (51,940 px) | bit-exact |
| iceclad | f2 | differs (13,619 px) | bit-exact |
| timorous | f0 | differs (114,732 px) | bit-exact |
| timorous | f1 | differs (1,218 px) | bit-exact |
| timorous | f2 | differs (143,514 px) | bit-exact |

**Acceptance B MET**: 3 identical (qeynos2 f1, befallen f1, befallen f2) and
the 12 differing frames carry exactly B1037's pre-fix lane-2 pixel counts
(refactor/B1037/README.md, the lane-2 table) -- 134,492 / 290 / 36 / 565 /
693 / 37 / 1,231 / 51,940 / 13,619 / 114,732 / 1,218 / 143,514 -- with metrics
bit-exact in all 15. `compare_before.txt` in the evidence.

### Lane 1 determinism (runs 1 vs 2 vs 3, decoded pixels, the gate's rule)

15/15 IDENTICAL for run1-vs-run2 AND run1-vs-run3. **Acceptance C MET.**
Tables: `lane1_run1/run1/compare_determinism.txt` (run 1 vs run 2) and
`lane1_run3/compare_determinism.txt` (run 1 vs run 3), under the evidence dir.

Note: qeynos2's `s3dZoneSourceBytes` is 501081 in ALL THREE runs (the
committed section had 501079 from the 55ec17b3-era bless); the only fields
that move run-to-run are the two report-only ones
(`processRssBytes`/`untrackedBytes`), which the gate prints and never
asserts. So the new section's exact-baselined fields are stable by
measurement, not hope.

### The bless

Run-1's 15 frames are the new `tests/goldens/pi/`; the new `platforms.pi`
section is the gate's own section-builder output over run 1's artifacts
(see `b1097_bless.py`, which stages run 1 exactly the way `capture_pi()`
lays out its run and reuses the gate's `extract_gl_version` /
`_pi_memory_fields` / CSV metrics code). Commit `996df5fe`
("B1097: re-bless Pi goldens post-B976 (Will 2026-10-19, #417)") lists all 15
sha256s and every manifest field change with its reason (its body is
`commit_msg_bless.txt` in the evidence). `blessedUnder` is unchanged
("OpenGL ES 2.0 Mesa 25.2.8" both sides). `rbuild hygiene` green at the
commit.

### Lane 2 table (run 1, Rock64/Mali-450/Lima/system Mesa 25.2.8, vs the NEW goldens; REPORT ONLY)

| zone | frame | pixels vs NEW golden | B1037 pre-fix lane-2 px | metrics |
|---|---|---|---|---|
| qeynos2 | f0 | IDENTICAL | 134,492 | bit-exact |
| qeynos2 | f1 | IDENTICAL | 0 (already) | bit-exact |
| qeynos2 | f2 | IDENTICAL | 290 | bit-exact |
| gfaydark | f0 | IDENTICAL | 36 | bit-exact |
| gfaydark | f1 | IDENTICAL | 565 | bit-exact |
| gfaydark | f2 | IDENTICAL | 693 | bit-exact |
| befallen | f0 | IDENTICAL | 37 | bit-exact |
| befallen | f1 | IDENTICAL | 0 (already) | bit-exact |
| befallen | f2 | IDENTICAL | 0 (already) | bit-exact |
| iceclad | f0 | IDENTICAL | 1,231 | bit-exact |
| iceclad | f1 | IDENTICAL | 51,940 | bit-exact |
| iceclad | f2 | IDENTICAL | 13,619 | bit-exact |
| timorous | f0 | IDENTICAL | 114,732 | bit-exact |
| timorous | f1 | IDENTICAL | 1,218 | bit-exact |
| timorous | f2 | IDENTICAL | 143,514 | bit-exact |

**15/15 IDENTICAL, metrics bit-exact in all 15** (`compare_lane2.txt`). The
ruling's expectation holds exactly: the floor locus now matches -- the new
goldens ARE the Lima D24 image and lane 2 is the Pi's Lima D24 twin (both
`EGL: Config depth=24`, confirmed in the lane-2 zone logs), so what remained
is NOTHING: every frame is pixel-identical, not just 1-LSB-close. No lane-2
goldens were written, no bound changed -- report only, per the ruling.
(Lane 2's maps show system Mesa with libLLVM, as expected for the aarch64
publish; the gate's maps proof is lane-1-only, the -nollvm one.)

### Finish state

- `docs/ci.md` one sentence added (the Pi-goldens line in "Two golden sets").
- `rbuild <wt> hygiene` green (45/45, at the golden commit; re-run after the
  docs commit).
- Push WITH CI + `glab mr create` ("Closes #417") pending here.
- Acceptance G: lane 1 leftover check clean (bracket-trick pgrep empty, lock
  released, HDMI connected as found, never rebooted); lane 2 the same.
- Evidence: `capture.log` (lane 1 run 1, the five zone logs in manifest
  order), `compare_before.txt`, `compare_determinism.txt` x2,
  `compare_lane2.txt`, the maps proofs, and the per-run dirs, all under
  `/home/user/kb-evidence/kb-b1097-pi-rebless/`.

