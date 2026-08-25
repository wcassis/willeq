# T226 — magenta patch in qeynos2 ceiling geometry (T220 side-finding): reproduce, attribute, fix if narrow

Slot: slot2. x86. OWNS eqemu for the reproduction (noclobber lock per protocol;
T224 in slot3 may hold it first — if so, do the offline part, then check
again; never spin-wait, report pending if still held). Class B.
Do NOT touch eq_*.cpp, spell_manager.cpp, combat.cpp, bridge/* (T221) or
who_response / eq_player_info.cpp (T224). Zone-geometry loader + texture
resolution territory only (wld_loader, s3d/eqe1 loaders, renderer_zone_load*,
constrained_texture_cache, mesh_building).

## Lead
`review/T220/README.md` notes a magenta patch in qeynos2 CEILING geometry in
its live screenshot. T211 fixed the 0x2C material-list guess for EQUIPMENT
models; this is zone (0x36 mesh / 0x31 material list) geometry — a different
path, so T211 does not cover it. Zero NOT-FOUND was reported by T214's
EQE1 request audit in qeynos2, so the miss may be a mapping/palette issue
rather than a missing asset.

## Scope
1. Reproduce: same spot as T220 (its README has the coordinates / `#zone`
   line). Run with `EQT_EQE1_REQUEST_LOG_DIR=<review dir>` and
   `--dump-frames-dir`. Capture the frame; crop the patch; confirm it is the
   magenta placeholder (exact RGB the placeholder uses — grep the source).
2. Attribute: which mesh/material produces it. Use `/dumpscene` if it lists
   materials, or add a temporary LOG_DEBUG at the placeholder-assignment
   site naming the texture and fragment. Determine whether the texture name
   is (a) absent from every open archive (NOT-FOUND in the request log),
   (b) present but in an archive not opened (rule R2 violation — T214), or
   (c) present and loaded but rejected (format/alpha/palette). State which,
   with the log line.
3. Fix ONLY if the cause is narrow (a wrong name-mapping, a missed archive
   name, a format branch). If it needs a loader redesign, STOP after
   attribution and report.
4. If fixed: before/after crops, 0 magenta pixels in the patch region (count
   them like T212 did), visual gate PASS with goldens untouched (qeynos2 is
   not a golden zone — verify), full ctest.

## Verification
Review in this file; evidence review/T226/ (README, frames, request-log TSV,
crops). Commit: `T226: qeynos2 ceiling magenta -- <cause> fixed` or
`T226: qeynos2 ceiling magenta attributed to <cause> (not fixed: <why>)`.
