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

## Review (executor, 2026-08-25)

Evidence: `review/T226/` (README.md is the index).

### Reproduced (step 1)
Same spot as T220 (Testchar staged to qeynos2 at 84.5/14.6/1 h0 via the
documented DB write; `#set time 11 0`; `/screenshot`), slot2 build, T220's
config and driver shape. Run A (no `--eqe1-path`, T220-identical) shows
the identical patch: 12,492 magenta-ish pixels in the T220 patch region
(x 380..659, y 0..169), 13,211 in the frame. The pixels are the placeholder
checkerboard under warm lighting -- (255,0,139) and (38,27,18) are 0xFF00FF
and 0x202020 (`getPlaceholderTextureInternal()`) times the interior light,
bilinear-blended over ~80 px, i.e. the 8x8 placeholder stretched across a
wall. They are not a colour-keyed texture: no BMP in qeynos2.s3d /
qeynos2_obj.s3d is magenta except the orphan `trees.bmp` (no WLD material
references it, and it has an alpha companion), and no ETC1 payload in
either .eqe1 is magenta (own ETC1 decoder, max fraction 0.003).

### Attributed (step 2)
Not (a), not (b), not (c)-as-written. Every qeynos2/qeynos2_obj name is
served by its own archive (run B request log: NOT-FOUND = 0; T214's
cold-load TSV agrees) and decodes/uploads fine. The textures are then
EVICTED by the constrained cache's LRU while region meshes of the current
zone still reference them, and `RegionMeshTextureGuard::onTextureEvicted`
swaps those materials to the placeholder. Run A: 29 evictions, all 29 with
live region-mesh swaps (1..69 materials each; e.g. `16roof.bmp` 69,
`3wall.bmp` 43, `7roof.bmp` 34). T220's own DEBUG log carries the same 29,
same order. The log line (now WARN, this unit):

    [WARN ] [GRAPHICS] RegionMeshTextureGuard: evicted texture '16roof.bmp' was still referenced by 69 live region-mesh material(s) of the current zone -> replaced with the placeholder checkerboard (magenta/dark-grey). The constrained texture budget is smaller than this zone's working set; the geometry using '16roof.bmp' now renders the placeholder until the texture is re-uploaded.

Cause of the eviction: the 64MB texture budget (OrangePi preset, and the
"Custom" preset T220's config resolves to) vs. qeynos2's working set on the
S3D/BMP path -- 349,525 B per 256x256 RGBA texture with mips, ~192 fit,
the zone requests ~230 zone+object names before entities. Two controls
isolate the variable: run B (`--eqe1-path /mnt/projects/EverQuest_ETC1`,
ETC1 = 43,704 B per texture): 0 evictions, 0 magenta pixels; run C (S3D
path, `--constrained 512x256x10` = 256MB budget): 0 evictions, 0 magenta.

Which of the 29 the ceiling patch is was not pinned; run B's frame shows
the real ceiling. All 29 are the same defect.

### Fix (steps 3/4): STOPPED after attribution -- not narrow
The cause is a budget-vs-working-set policy, not a name mapping, archive
name or format branch. No LRU choice can avoid it when the working set
exceeds the budget; the remedies are a bigger budget (a preset decision)
or smaller textures -- which is exactly EQE1, Will's settled decision #2
(the S3D path is dev-only, "no S3D fallback"). Recommendation: T220's
driver omitting `--eqe1-path` is the mistake; live x86 runs that want to
look at pixels should pass it. If the S3D path is ever expected to render
clean, the lever is the preset's `textureMemoryBytes` (run C: 256MB is
enough for qeynos2).

Change made: `RegionMeshTextureGuard::onTextureEvicted` now logs the swap
at WARN with the texture name (was DEBUG, `[mergedraws]`, nameless), so a
normal-log-level run reports the defect instead of rendering it silently.
Territory note: the edit is in `renderer_zone_merge.cpp` (the zone-geometry
texture guard), which is neither T221's nor T224's territory but is not in
the plan's explicit file list either -- flagged, not absorbed.

### Verification
- `cmake --build slot2/build -j24`: exit 0.
- ctest: **2787/2788**, sole failure `AudioManagerTest.PreloadCommonSounds`
  (documented ALSA quirk). Suite grew from 2746 by units merged since.
- `build-gles2-x86.sh`: exit 0. `build-arm-noble.sh`: exit 0.
- Visual gate: PASS (`review/T226/visual_gate.txt`). qeynos2 IS a golden zone (plan asked to
  verify; it is one of the three) -- goldens untouched, this unit changes a
  log level only.
- `check_file_sizes.sh fable`: `fable` has moved past this branch's base
  (dc494ef7 vs 357e6071), so it reports 16 files from other merged units;
  the one FAIL (`eq.cpp`, 3934 lines) is pre-existing and not in this
  diff. `--files` on this unit's one changed file: ok (566 lines).
- eqemu: lock taken with noclobber (T224 had released it), up with a
  qeynos2 zone, three client runs, Testchar restored to zone 36
  (-77.9232/21.9086/3, h190), clean down.sh, lock deleted.

### Deviations
- Step 1 asked for `--dump-frames-dir`; a single `/screenshot` per run was
  sufficient (the patch is static) and is what T220 used, so the frames are
  directly comparable.
- Step 4 (before/after crops, 0 magenta after a fix) does not apply: no
  fix. Runs B and C are the controls, not an after.
