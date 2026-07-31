# P48 — Composite-sheet alpha support: recover the P37 perf cost, keep the blessed masked look

**Slot: slot1. OWNS THE PI (P02 protocol; ≤6 invocations, for the
final re-measure only). Authorized by Will 2026-07-31 (option 1 of
the P37 regress-response).**

## Goal

P37 excluded masked races from composite eligibility because sheets
carry zero alpha (builder hardcodes hasAlpha=0). P45 measured that
cost: +18.3ms p95 / +207% draws at a ZOM-heavy real roster. This unit
gives sheets companion alpha so masked races composite again — the
perf comes back AND the P47-blessed masked cutouts render on
composited entities.

## Background (read first)

refactor/P37_*.md (exclusion + per-variant sheets), P45 Review (the
cost tables + real-roster invocation), P46/P46d Reviews (palette-0
decode + cache keying), P47 Review (the magenta viewer + regression
pin — your verification instrument), P16 Review (sheet
regeneration/deploy precedent incl. backup convention), P08f
(builder zone-archive support), deferred.md's P37/P46d entries.

## Work

1. **Builder alpha**: entity_composite_builder + eqe1 pipeline emit a
   companion alpha page for sheets containing WLD-masked source
   textures (mirror the existing zone ETC1 companion convention:
   R=G=B=alpha, palette-0 → 0). Unmasked sheets byte-identical to
   current output (verify by hash on a sample).
2. **Eligibility**: replace P37's blanket masked-race exclusion with
   a sheet-capability gate — masked races composite when their sheet
   has the alpha companion; the exclusion stays for sheets without it
   (safety for stale/global sheets not yet regenerated).
3. **Rendering**: composite meshes whose sheet is masked get
   EMT_TRANSPARENT_ALPHA_CHANNEL_REF + companion binding through the
   SAME production promotion path entity_renderer already uses (P46b
   established it is paired — do not fork a new path).
4. **Sheet regeneration + deploy**: regenerate affected sheets (the
   ~15 masked races' variants; global + zone archives per P08f).
   Deploy to /mnt/projects/EverQuest_ETC1 (NFS) WITH a dated backup
   dir first (P16 convention). Audit tool must pass on the new
   sheets.
5. **Visual verification (the P47 instrument — no crop-squinting)**:
   extend/reuse the magenta viewer to render composited masked races
   (ETC1 leg, composites active): cutouts must show as enclosed
   magenta, textures correct at full size. Add the composite leg to
   the P47 regression test (masked composited >0, control ==0).
   Produce a small contact sheet for Will — this is NEW blessed
   territory (masked composites were never seen before); final bless
   is his.
6. **Pi re-measure (the payoff)**: repeat P45's real-roster A/B
   invocation shape on the canonical befallen e205 replay: current
   tip (masked races composited w/ alpha sheets) vs P45's recorded
   tip numbers (57.650ms p95 / draws 348.3). Target: recovery toward
   the pre-P37 39.3ms / 113.6-draw level. Also 1 canonical-replay
   spot (KPI guard, vs 32.24-32.5ms band). DRM mode-line confirmation
   ("Found matching mode 800x600") recorded per run (P43 rule).

## Rules

- Deploy only with backup; NFS deploy is part of this unit (P16
  precedent) but NO preset changes.
- Goldens untouched (gate PASS); production promotion path reused,
  not duplicated.
- Full bar: GLES2 ctest baseline (~1617/1619 + extended regression),
  visual gate PASS, ARM cross-build clean, file sizes.
- Pi protocol absolute (CmaFree gate, /tmp-only timed writes,
  drop_caches never reboot, pristine exit).
- STOP-with-memo: sheet format change breaks existing consumers; or
  eligibility/rendering plumbing cascades absurdly; or the Pi
  re-measure REGRESSES the canonical KPI spot.
- Sibling units run in slot2 (DRM device file) and slot3
  (mesh_building/animated_mesh_creation mask plumbing) — do NOT touch
  docker/irrlicht-drm/CIrrDeviceFB.cpp, and coordinate-free overlap
  with slot3 is expected to be nil; if you find you must edit
  mesh_building.cpp or animated_mesh_creation.cpp, note it
  prominently in your report for merge-order attention.

## Review

(executor fills in)
