# B1051 xu4 oracle-verify leg — verdict (2026-10-19)

Task t_7af659bb: prove the XU4 (Orange Pi, GLES2/ETC1) willeq client renders a
named NPC at the locked spots — oracle still vs x86 willeq vs xu4 willeq at the
same pose — after the ETC1 variant-cache fix merged to fable.

## Verdict: PASSED

At all three locked spots, the xu4 client renders the same NPC, in the same
scene position, with distinct per-part armour (the MR !173 + B1051-ETC1 fix
visible on device). The recurring "xu4 shows walls / 0 NPCs" reading from
earlier legs was a FRAME-TIMING artifact of the capture recipe, not a
rendering defect — see "Why the earlier legs were misleading" below.

## The three legs (all 2026-10-19, lane 1 eqemu, character Testchar)

| spot | pose (loc, heading) | oracle | x86 willeq | xu4 willeq |
|---|---|---|---|---|
| s1 | (255,210) /turn 88 | `oracle_s1/09_view.png` 17:12 | `final3/x86_s1/frame_000034.png` 16:07 | `xu4x3/s1/frame_000023.png` 18:24 |
| s2 | (250,200) /turn 90 | `oracle_s2/09_view.png` 17:29 | `final3/x86_s2/frame_000023.png` 16:13 | `xu4x3/s2/frame_000022.png` 18:27 |
| s3 | (248,218) /turn 100 | `oracle_s3/09_view.png` 17:20 | `final3/x86_s3/frame_000039.png` 16:26 | `xu4x3/s3/frame_000021.png` 18:30 |

Pose is proven per client from its own run.log (`Your location is X, Y, 3.1`
+ `Turned to heading N` in each `sN/` dir here). Oracle pose proven from its
`05_goto.opcodes.tsv` (`#goto 210 255 125.2` / `#goto 200 250 …` /
`#goto 218 248 142.2`) and the `Your Location is …` line in its chat capture.
Oracle z differs (GM #goto flies at 125-142 then settles) — heading/xy match;
the camera is third-person so the ground figures line up.

Provenance:
- x86: willeq from `/home/user/willeq-builds/kb-b1051-xu4-oracle-verify/build`
  (merged fable head incl. the B1051-ETC1 fix), llvmpipe via EGL surfaceless.
- xu4: the ci-arm armhf publish at
  `/mnt/projects/fable/ci-arm/by-branch/fable/bin/willeq` (see `xu4_run.log`
  "WilleqDatabase: opened /mnt/projects/fable/ci-arm/by-branch/fable/…"),
  DRM /dev/dri/card0 driver=exynos, GLES2, ETC1 pack.

## What the frames show

**s1 — Guard / "Kane Bayle" (Warrior Guildmaster), right side of frame next to
the chair.** All three panels: same figure, same relative position. Oracle:
distinct plate parts (chest, pauldrons, greaves, boots). x86: same figure,
distinct parts, near-identical. xu4: same figure, same position, distinct
parts with the known ETC1 colour-shift (slightly warmer tint — expected, see
ROOT_CAUSE history; the earlier "smear" is gone).

**s2 — spawn 5 at (270,202,3.8) dist 20.** Both willeq clients log the SAME
single SHOWN entity (`EntityVis: spawn=5 SHOWN pos=(270.0,202.0,3.8)`) in the
same scene; oracle shows one guard (name tag "Guard Treitan (Warrior Guildmaster)"
per vision read of the oracle still) at the same relative spot. x86 figure has
distinct armour + tabard + sword; xu4 figure has distinct multi-part armour.
The two willeq clients show the same server entity in the same position —
alignment is ground-truth from the logs, not from the vision read of small
name tags (which the downscale makes unreliable).

**s3 — spawn 5 + spawn 22.** x86 and xu4 show the same figure(s) in the same
position (x86: distinct parts + sword; xu4: distinct parts + sword, top-5
share 0.352 vs x86 0.335 in the crop metric — same band). The oracle still
at this spot is heavily UI-covered (Tip of Day + chat over the scene centre);
vision reads of it disagree between passes (robed figure vs empty world), so
oracle s3 is the weakest panel of the nine — but the willeq pair agrees
with each other and with the EntityVis logs.

## Per-NPC crop metrics (refactor/B1051/b1051_still_compare.py)

Central-band crop (x 0.30-0.72, y 0.12-0.72), real per-spot output:

```
spot  client                        coarse    gold     red    top5  hist_dist
s1    OFFICIAL                          75  0.0000  0.0000   0.682          —
      x86 frame_000034                  34  0.0000  0.0000   0.837     0.2682
      xu4 frame_000023                 128  0.0317  0.0000   0.400     0.5548
s2    OFFICIAL                         170  0.0000  0.0000   0.784          —
      x86 frame_000023                 298  0.0062  0.0204   0.474     0.5560
      xu4 frame_000022                 383  0.0556  0.1116   0.249     0.8151
s3    OFFICIAL                         279  0.0013  0.0005   0.519          —
      x86 frame_000039                 285  0.0249  0.0169   0.335     0.3286
      xu4 frame_000021                 297  0.0662  0.0201   0.352     0.4729
```

(Verbatim in `evidence/metrics.txt`. The fixed script unpacks
`crop_metrics()`'s 6 return values and ravels the 2-D histogram codes — two
bugs fixed this leg, see `git diff refactor/B1051/b1051_still_compare.py`.)

Reading, honestly:
- The crop is a FIXED central band; it measures the figure only when the
  figure is centred. At s1 the guard sits at the RIGHT edge of the frame, so
  the x86 s1 crop is scenery (34 coarse / top5 0.837 = flat wall+floor) and
  the xu4 s1 crop is a partial figure sliver (128 coarse / top5 0.400 =
  non-flat, i.e. textured) — the s1 row is NOT a clean per-NPC measurement
  for the x86 panel. Do not quote the s1 x86 "flat" row as evidence of a
  smear: the vision read of the full frame shows the x86 guard with distinct
  plate parts in the same spot as the oracle's.
- At s2 and s3 the figure is inside the band for both willeq panels. xu4
  reads ABOVE x86 in colour diversity (s2: 383 vs 298 coarse, top5 0.249 vs
  0.474; s3: 297 vs 285, top5 0.352 vs 0.335) and carries the ETC1
  gold-channel shift (s2 gold 0.056 vs 0.006, red 0.112 vs 0.020) — the
  known, documented ETC1 colour difference, not a missing-texture defect.
  The pre-fix smear signature (one dominant mottle bucket, top5 ≈ 0.8+) is
  absent from every in-band willeq panel of this leg.
- The oracle rows are reference frames for POSITION and IDENTITY, not for
  the crop metric: its still is a different window size with its UI open, so
  the central band catches mostly wall/floor there.
- The verdict rests on (a) identity + position ground truth from the
  EntityVis/`#goto` logs and (b) the full-resolution vision reads of the
  three-way composites (all three willeq panels: same figure(s), same
  relative position, distinct armour parts in both willeq clients), with the
  in-band crop metrics (s2, s3) as the mechanical corroboration.

## Why the earlier legs were misleading (the root cause of this task's churn)

1. **Frame timing, not rendering.** The v1/v2 xu4 recipes used a 40-frame
   dump budget (`--dump-max-frames 40`, `--dump-every N`) with a 60 s
   pre-turn wait; at the Pi's 12-40 fps the budget filled in ~33 s, so every
   dumped frame PRE-dated `/turn` — i.e. every "xu4 still" was the wall view
   behind the character, before the camera faced the guard. The xu4x3 recipe
   moves `/turn` to t≈12 s and extends the capture window
   (`--dump-every 8 --dump-max-frames 200`), so post-turn frames are
   guaranteed at any fps in the measured band. Every xu4x3 frame was verified
   post-`/turn` from its run.log timestamp before being treated as evidence.
2. **Hereditarily-eaten stdin.** The v2 remote runner was an `ssh bash -s`
   heredoc; willeq inherited that stdin and consumed the post-run
   `echo/ls/wc` tail as game commands, so the frames-list file came back
   empty and the leg reported "0 frames" although 40 frames existed on the
   device. v3 uploads the runner as a FILE (scp + sed per spot) and runs
   willeq with `< /dev/null`.
3. **A silent fleet-down no-op.** The 17:51 "relaunch" pulled stale 13:5x
   logs: the eqemu fleet was down at seed time, `mysql_exec` died on the
   absent mariadb container, the zone query came back empty, and all three
   remote runs logged in at Testchar's saved pose (qeynos, 464,-442) and
   dumped 0 frames. This leg's runner hard-downs before each spot, brings
   mariadb up, and aborts (rc≠0) if the stack isn't ready or the frame count
   is < 10 — a no-op can no longer masquerade as a result.

## Environment left as required

- **DB restored**: Testchar back to its original row
  (zone_id=1, x=-442, y=464, z=1.5, heading=259); the decoy GM rows renamed
  back to plain names (verified post-leg via
  `b1051_db_check.sh`).
- **Lease FREE**: `eqemu_lock_take`/`release` under identity
  `LANE_LEASE_CARD=t_7af659bb LANE_LEASE_RUN=1333`; `eqemu_lock_show`
  reports state=FREE after the leg.
- **XU4 as found**: no willeq process, no `/tmp/b1051xu4*` scratch
  (only the normal `~/willeq-cache/` shader cache remains, which the client
  writes on every run); no reboot, no package/kernel/governor changes.

## Evidence index (this directory)

```
s1/  s2/  s3/   oracle.jpg + x86.jpg + xu4.jpg + x86_run.log.excerpt.log + xu4_run.log.excerpt.log
s{1,2,3}_full.jpg   3-way composite, full resolution (720 px panels): OFFICIAL | x86 | XU4
s{1,2,3}_3way.jpg   3-way composite, downsampled
metrics.txt          b1051_still_compare.py output for all three spots
```

Stills are downscaled JPEGs (x86 1600x900 from 3840x2160; oracle 1280x800;
xu4 1280x720); the log excerpts keep the pose lines (`Turned to heading`,
`Your location is`, `EntityVis: spawn=… SHOWN`, first/last `EntityVis:
N/M visible`) and the build/driver lines, not the full ~1.5 MB logs.

Raw full-resolution frames, full logs, oracle captures, and the runner
scripts live under
`/home/user/kb-evidence/kb-b1051-xu4-oracle-verify/` (not in the repo):
`oracle_rerun/oracle_sN/`, `final3/x86_sN/`, `xu4x3/sN/`, `b1051_xu4x3.sh`,
`b1051_pick_frames.py`, `b1051_sidebyside.py`.

## Residual / not closed

- Oracle s3 is UI-heavy; if Will wants an unambiguous oracle panel for s3,
  the spot's goto needs a heading that avoids the Tip-of-Day window, or the
  oracle leg re-run with its UI closed (the oracle runner does not hide UI —
  willeq does via /hideui).
- The s2/s3 name tags in the willeq panels are small; identity of the
  figure is established from the EntityVis logs (spawn ids + positions match
  between the two willeq clients and the oracle's goto target), not from
  OCR of the tags.
