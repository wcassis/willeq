# B967 -- the player light's vector in the VS, the B968 shape fix, the 15 goldens re-blessed

GitLab #285 (B967) and #286 (B968); unblocks #287 (B969). Branch
`task/B967-player-light-vs-rebless`, cut from `fable` `ee6353f9` and merged
with `fable` `d3375b65` (B964) before any measurement below. Will's ruling,
2026-10-01, in chat: **"I agree, re-bless the 15 goldens."** Raw evidence:
`/mnt/projects/fable/review/B967/`.

**MERGED** into `fable` 2026-10-02, merge commit `2c7d5be4` (MR !74, pipeline
455 green on final head `a30b8a56`; test 5145/5145, gate PASS 0 pixel
mismatch). Will, 2026-10-02, in chat (verbatim, also as an MR comment):
**"I've reviewed the delta table (refactor/B967/README.md) and the contact
sheet (review/B967/sheet/) for MR !74 and the changes are approved."**

## STATE AT HAND-OFF 2026-10-01

**DONE (with evidence)**

1. Shader change, committed (`202df783` "B967/B968: player light vector formed in the
   VS ..."; follow-up `98846a79` "B967: probe monotonic check ..."):
   - `include/client/graphics/player_light_shader.h` (new): the GLSL lines
     `EQT_PLAYER_LIGHT_VS_LINE` / `EQT_PLAYER_LIGHT_FS_TERM(N)` and the LUT
     (`PlayerLight::lutRange/lutScale/lutAtten/fillLut`). Spliced into all six
     player-light FS and the three per-pixel VS in
     `src/client/graphics/zone_shader_sources_gles2.h`; `zone_shader.cpp/.h` fill the
     LUT and set `uLUTScale` from it.
   - VS: `vPlayerLightVec = (uPlayerLightPos - worldPos) * 0.015625` (FP32, pre-scaled
     1/64); the FS no longer declares `uPlayerLightPos` or reads a world position
     (`vWorldPos` varying is gone from the per-pixel programs).
   - FS (branchless): `d2 = dot(v,v); invD = inversesqrt(clamp(d2, 2^-14, 60000));
     lut = texture2D(uAttenLUT, vec2(clamp(d2*invD*uLUTScale, 0, 1), 0.5));
     pLn = max(dot(N, v), 0) * invD; pLight = color * (pLn * lut.a)`. B965's clamp
     on the coordinate is kept.
   - **B968 formula**: `pLight = color * max(N.L^, 0) * A(d)`,
     `A(d) = 1/(c + l*d + q*d^2 + 1e-4)` -- the per-vertex zone-light loop's own
     term. LUT indexed LINEARLY in d (coordinate d/R), R = the d where
     `c + l*d + q*d^2 = 255` (P07d's 1/255 demotion threshold), clamped to
     [64, 4096]; last texel A = 0, so the light is 0 from R on. Lightstone level 9:
     R = 336.38, uLUTScale = 64/R = 0.190262. L channel = d/R (debug only:
     `--shader-debug 3` red = distance, green = attenuation).
   - Varying range: |v| = d/64; the FS square overflows FP16 only past 15,677
     units (clamped to 60,000 there, which yields coordinate 1 -> A = 0). Zone-extent
     survey NOT done (see REMAINS 4).
2. Nets (green locally and on CI pipeline 399 `test`): `tests/test_player_light_lut.cpp`
   (4 tests, GL-free) and ctest `PlayerLightProbeMonotonic`
   (`gles2_player_light_probe --assert`, llvmpipe): monotonic (from 0.5 unit out,
   tolerance 1 step), within 2 steps of the per-vertex family, origin 0 == iceclad
   f2 pose, 0 past R, quad to 40,000 units draws 0 past R. RED proof:
   `gles2_player_light_probe --assert-b965` (the pre-B967 FS + LUT) fails 9 of 10
   checks (wall curve 17 at 64 units -> 105 at 399; origin diff 178).
3. Goldens regenerated with the gate's own invocation (`review/B967/scripts/regen_goldens.py`,
   which calls `scripts/ci/visual_gate.py run_benchmark()` per manifest zone, twice:
   run1 vs run2 = 0 px in all 15) and INSTALLED in `tests/goldens/*.png` (this WIP
   commit). Builds 9e57bc4d and 98846a79 give pixel-identical frames.
   `tests/goldens/manifest.json`: only the `approval` block changed (Will, 2026-10-01,
   the ruling quoted). No metric/memory/CPU/coverage value edited.

   x86 BEFORE (committed goldens at ee6353f9) vs AFTER, per frame
   (`review/B967/x86/after_final/golden_delta.tsv`; >32 / >8 / any = pixels whose
   max channel diff exceeds it, of 480,000; mean = mean abs over all RGB values):

   | frame | >32 | >8 | any | max | mean | all >32 darker? |
   |---|---|---|---|---|---|---|
   | qeynos2_f0 | 207 | 14,066 | 89,508 | 58 | 0.486 | yes |
   | qeynos2_f1 | 922 | 17,621 | 108,693 | 69 | 0.514 | yes |
   | qeynos2_f2 | 58 | 21,401 | 74,626 | 41 | 0.692 | yes |
   | gfaydark_f0 | 5,011 | 160,302 | 275,273 | 56 | 4.432 | yes |
   | gfaydark_f1 | 20,491 | 39,983 | 153,651 | 128 | 2.356 | yes |
   | gfaydark_f2 | 10,265 | 65,938 | 141,557 | 126 | 2.483 | yes |
   | befallen_f0 | 0 | 0 | 94,033 | 8 | 0.200 | -- |
   | befallen_f1 | 174 | 3,044 | 188,703 | 63 | 0.339 | yes |
   | befallen_f2 | 0 | 0 | 19,965 | 1 | 0.015 | -- |
   | iceclad_f0 | 522 | 16,437 | 93,811 | 49 | 0.759 | yes |
   | iceclad_f1 | 80,680 | 110,415 | 213,593 | 89 | 10.702 | yes |
   | iceclad_f2 | 7,780 | 95,911 | 249,179 | 50 | 4.010 | 7,766 darker, 14 brighter |
   | timorous_f0 | 0 | 69 | 187,031 | 11 | 0.361 | -- |
   | timorous_f1 | 5 | 7,234 | 235,228 | 43 | 0.673 | yes |
   | timorous_f2 | 21,150 | 75,688 | 273,736 | 98 | 4.075 | 12,394 darker, 8,756 brighter |

   B969 measured pLv-in-VS ALONE at >32 only in iceclad_f2 (546) and timorous_f2
   (16,085); the darkening elsewhere is B968 (the light no longer brightens past 64
   units; it is 0 from 336 units on).
4. Manifest fields the golden-image run would bless that differ from the committed
   manifest: ONLY `memory.textureCacheBytesLimit` 67,108,864 -> 53,477,376 in all five
   zones. NOT caused by this unit (51 MiB is B943's 2026-09-30 texture-cache ratchet;
   the field is display-only, never asserted -- the fable gate passes with it stale).
   **Left unchanged, not blessed.** Nothing else moved.
5. Local visual gate on the new goldens: **0 PIXEL MISMATCH in 15 frames**. It FAILED
   on host-load items only (load average ~52): CPU p95 iceclad 18.4 > 9.53, timorous
   30.8 > 24.2 (B937), memory-coverage iceclad slack 36.8 % > 35 % (B931).
6. Pi (Orange Pi One, Lima, Mesa prefix 25.2.8 proven per run in `mesa_maps.log`;
   binaries `review/B967/arm_bin/{before_d3375b65,after_9e57bc4d}/*-nollvm`, from CI
   `build-arm` jobs 2063 (fable d3375b65) and 2068 (this branch 9e57bc4d)).
   Pi gate frames vs the NEW x86 goldens, >32 px (B969's "before" in brackets):

   | zone | f0 | f1 | f2 |
   |---|---|---|---|
   | qeynos2 | 0 (0) | 2 (2) | 2 (2) |
   | gfaydark | 409 (429) | 55 (66) | 162 (163) |
   | befallen | 4 (4) | 3 (3) | 0 (0) |
   | iceclad | 2 (2) | 21 (29) | 6 (335) |
   | timorous | 2 (2) | 0 (0) | 15 (3,175) |

   Bar "tens or better" met in 13 of 15; gfaydark f0/f2 (409/162) equal B960's
   player-light-OFF counts (409/54/162): not the player light, cause not isolated
   (to file, REMAINS 5). iceclad f2 6 = light-off 6.
   Probe on the Pi (`review/B967/pi/probe/after/`): the `ship` curve is monotonic
   within 1 step (Lima rounds n+0.5 values both ways: 17,16,17,16), within 1 step of
   the per-vertex family, identical at both origins, 0 past R, and the 40,000-unit
   quad draws 0 past R (`assert_offline_final_criterion.txt`; the on-Pi `assert.log`
   predates the 1-step tolerance and says FAIL on the strict rule only).
   Mali-400 PP instruction count (LIMA_DEBUG=pp, `pp_instr_counts.tsv`): `ship` 9,
   B965's shipped `prod_saturate` 10.
   Frame time A/B (`review/B967/pi_frame_time_ab.tsv`, 60 frames each, interleaved
   before/after per zone in one session): total p50 qeynos2 -0.5 %, gfaydark -1.5 %,
   befallen -0.2 %, iceclad -2.8 %, timorous -1.2 %; GPU p50 -1.9 / -2.0 / -3.6 /
   -4.1 / -2.4 %; p95 total -0.5 / -2.3 / +0.4 / -2.7 / **+22.9 %** (timorous p95 is
   the noisy GPU tail B965 also saw: 110 vs 135 ms).
7. Review media: `review/B967/sheet/full/<frame>_before_after_diff.png` (15) and
   `review/B967/sheet/crops/*_crop4x.png` (iceclad_f2 floor bands, timorous_f2 step
   lines, iceclad_f1 + gfaydark_f1 far-lit surfaces with a 4th panel = the
   `--shader-debug 3` distance map). `review/B967/README.md` NOT written yet.
8. Docs edited: `docs/decisions.md` (2026-10-01 entry quoting Will verbatim),
   `src/client/graphics/CLAUDE.md` (lighting section + Mali rules).

**Locks/stacks:** Pi lock `b967` taken twice (session 1: 8 invocations -- probe x3,
gate x5; session 2: 10 timing runs) and RELEASED both times; Pi `/tmp` clean,
governor ondemand untouched. No eqemu lock or stack used.

**REMAINS (checklist)**

1. ~~`review/B967/README.md`: describe every file (sheet/, x86/, pi/, arm_bin/,
   scripts/); no verdicts.~~ **DONE** (`refactor/B967/REVIEW_MEDIA.md`, committed
   on this branch; the card's NFS rule redirects the write here -- the orchestrator
   copies it to `review/B967/README.md` on the Pi/NFS). Describes every file
   under `/mnt/projects/fable/review/B967/` (arm_bin, pi, scripts, sheet, x86,
   pi_frame_time_ab.tsv); no verdicts. scp'd to `user@10.0.30.192:
   /mnt/projects/fable/review/B967/README.md` 2026-10-01 11:57 UTC (verified on
   the Pi).
2. ~~`refactor/BOARD.md`: strike B967, B968 (done), B969 (bar met: timorous_f2
   15 px); keep the gfaydark residual as a new item (step 5).~~ **DONE**
   (committed this unit): B967/B968 struck **FIXED**, B969 struck **CLOSED**
   (timorous_f2 3,175 -> 15 px, bar met); the gfaydark Pi-vs-x86 residual kept as
   a new open item **B980** (issue #299); B965's follow-up line updated.
3. ~~Re-run the local gate on a quiet host for a clean record:
   `bash scripts/ci/visual_gate.sh` (must show no PIXEL MISMATCH; CPU/slack misses =
   B937/B931 if load).~~ **DONE** (2026-10-01, kanban unit, host load ~5-8):
   **0 PIXEL MISMATCH in 15 frames** (log `~/kb-evidence/b967-player-light/
   local_gate.log`). It FAILED on a host-load item only: `iceclad: p95
   process-CPU ms 9.841 exceeds ceiling 9.530 (baseline 4.765 x2.0)` (B937's
   CPU item; not a pixel failure). The CI `gate` job on this branch's final head
   is the record of record (card rule REMAINS 3).
4. ~~Varying-range survey: run `bash scripts/ci/zone_content_sweep.sh
   --out-dir <dir>` (all 118 zones, ~7 min) and parse `Zone bounds (EQ)` from
   the per-zone logs; record the largest diagonal and the five gate zones' in
   this README; confirm diag < 15,677 (the FS clamp point) or note which zones
   exceed it (beyond it the light is 0 by construction, not garbage).~~ **DONE**
   (2026-10-01, kanban unit): `zone_content_sweep.sh --out-dir
   ~/kb-evidence/b967-player-light/sweep` (118 zones, every row CLEAN, exit 0;
   raw per-zone logs kept there, not committed). `Zone bounds (EQ)` parsed with
   `refactor/B967/zone_bounds_survey.py`; full output committed at
   `refactor/B967/evidence/zone_bounds_survey.txt`.
   - The player-light distance `d` is a 3D distance, so a zone's full 3D
     diagonal is the upper bound on player-to-surface distance within it. The
     FS clamp point is 15,677 units (d where the LUT coordinate saturates to 1
     and A = 0). A zone whose diagonal exceeds it has its far corner receive 0
     player light by construction -- intended, not garbage.
   - **Largest diagonal: iceclad, full 3D 41,305 units** (XZ 17,729). No zone's
     XZ diagonal is used as the bound; the full diagonal is.
   - 17 zones' full 3D diagonal exceed 15,677: iceclad 41,305; timorous 28,883;
     oot 25,368; eastwastes 24,617; overthere 22,394; firiona 19,967; qey2hh1
     19,699; westwastes 19,440; dreadlands 18,776; stonebrunt 18,676;
     warslikswood 18,375; lakeofillomen 17,078; skyfire 16,420; greatdivide
     16,240; burningwood 15,988; southkarana 15,922; frontiermtns 15,850.
   - Five gate zones: befallen full 1,083; qeynos2 full 3,188; gfaydark full
     7,816 (all under 15,677 -- the light is never clamped to 0 anywhere in
     these zones); iceclad full 41,305 (EXCEEDS); timorous full 28,883
     (EXCEEDS). So in the two large gate zones the far regions are at
     player-light 0 by construction, which the re-blessed goldens already show
     (the B968 darkening past 336 units).
5. ~~File issues (B numbers from the max `B###:` title; B976 = #294 last known):
   gfaydark gate frames Pi vs x86 409/55/162 px >32 with the player light OFF too
   (cause not isolated); manifest `textureCacheBytesLimit` display field stale since
   B943 (67,108,864 vs live 53,477,376; not asserted).~~ **DONE** (2026-10-01,
   re-read the max `B###:` title just before filing: max was B979 = #298, so this
   unit took B980/B981):
   - **B980 = issue #299** (class::B, lane::pi, status::triage): the gfaydark
     409/55/162 px >32 Pi-vs-x86 residual, present with the player light OFF too.
   - **B981 = issue #300** (class::B, lane::offline, status::triage): the stale
     `textureCacheBytesLimit` display field (64 MiB manifest vs 51 MiB live).
6. ~~Full suite + hygiene: `ctest --test-dir build -j6` (record the summary line),
   `bash scripts/refactor/check_file_sizes.sh -C . fable`.~~ **DONE** (2026-10-01,
   kanban unit, on CI per the card rule): CI pipeline **454** on the final head
   `8d0bc2ed` -- hygiene/build/**test**/**gate** all **success** (pipeline 453 on
   `450f327c` identical; 454 re-ran after the docs-only REMAINS-6 commit).
   - Full suite (CI `test` job): **"100% tests passed, 0 tests failed out of
     5145"**.
   - Gate (CI `gate` job): **"=== VISUAL GATE: PASS ==="**, **0 PIXEL MISMATCH
     in 15 frames**.
   - File sizes (`check_file_sizes.sh -C . --all`, local this host):
     "checked 1331 file(s): 1331 ok, 0 warn, 0 fail"; CLAUDE.md 58,989 bytes
     (< 60,000 ceiling).
7. ~~Issues #285/#286 (and #287) -> `status::review`; MR description final
   (Closes #285, #286, #287); un-draft.~~ **DONE** (2026-10-01, kanban unit):
   #285/#286/#287 labels are now `[class::A, lane::pi, status::review]`
   (verified via API); MR !74 description final, ends with `Closes #285, #286,
   #287`; MR un-drafted and re-titled (draft marker removed).

## Not verified

- Live client on the Pi with `/plight` (only benchmark poses + the probe).
- Zone-extent survey for the varying range (REMAINS 4; completed 2026-10-01 by
  the kanban unit, `refactor/B967/zone_bounds_survey.py` + the README REMAINS 4
  entry).
- Mesa versions other than 25.2.8; the system Mesa on the Pi.
- The on-Pi `--assert` run of the FINAL probe binary (criterion recomputed offline
  from the same Pi pixels; the shader and A channel are identical).
