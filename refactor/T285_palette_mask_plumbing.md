# T285 — applyPaletteMask plumbing gaps (P46d, still open at the tip)

Slot: slot1. x86. NO eqemu (offline asset + pixel work). Class A.
Territory: `src/client/graphics/eq/mesh_building.cpp`'s
`loadTextureFromBMP()` fallback and `animated_mesh_creation.cpp`'s
equipment-variant swap (the two sites P46d named), the palette-mask helper
itself, tests. Do NOT touch the composite scan (T284 slot2) or inventory.

## Gap (deferred.md index, P46d)
`applyPaletteMask` is not threaded through two paths: `mesh_building.cpp`'s
`loadTextureFromBMP()` fallback and `animated_mesh_creation.cpp`'s
equipment-variant swap. Both were listed with "0 hits" at T53 and no unit
has touched them since. Verify that is still true at the tip before doing
anything (T215's lesson: the board can be stale).

## Scope
1. Confirm the two sites still bypass the mask, and establish what the mask
   DOES for the paths that do call it (which textures, which palette, what
   the visible difference is) — P21/P37/P46 are the prior art.
2. Thread it through both, matching the existing callers exactly (no second
   implementation — T230/T239 one-body rule).
3. Prove the difference in pixels: a texture that the mask changes, loaded
   through each of the two paths, before and after. If the mask makes NO
   difference on those paths (e.g. they only ever see already-masked
   textures), say so with the evidence and close the item as not-a-defect
   rather than adding dead plumbing.
4. Tests: red/green on the pixel assertion.

## Verification
Full ctest; visual gate (goldens must stay untouched unless the mask
genuinely changes a golden texture — if it does, STOP and report, that is a
re-bless decision); Review; evidence review/T285/. Commit:
`T285: palette mask reaches the BMP fallback and the equipment swap` or the
not-a-defect form.

## Review (executor, 2026-08-26)

### Verdict: NOT A DEFECT — the board entry was stale. Both sites already
### thread the mask (P50 fixed them); what was missing was a pin.

Scope item 1 ("confirm the two sites still bypass the mask") is the whole
finding. They do not, and have not since P50:

| Site | At the tip (`6ad62863`) | Fixed by |
|---|---|---|
| `mesh_building.cpp` `buildMeshFromGeometry()` fallback | `isMaskedMaterial` hoisted above the load block and threaded into all four `getOrLoadTexture()`/`loadTextureFromBMP()` calls (L153, L249/253/255/259) | P50 Gap 1 |
| `animated_mesh_creation.cpp` equipment-variant swap | `isMaskedMaterial` read off the buffer's `MaterialType` (L529) and threaded into both `loadTextureFromBMP()` calls (L601, L618) | P50 Gap 2 |

`refactor/P50_mask_plumbing_gaps.md` carries the fix's own Review; it is on
the tip, so P50 merged. `refactor/deferred.md` was never updated — the bullet
at line ~229 and the inline mention at line ~71 both still read "still open",
and the P46d "Open follow-up items" paragraph still reads "flagged below, not
fixed". All three are corrected in this commit. **No production code changed.**

### Evidence that the mask actually reaches both paths at the tip

Re-ran P50's own instrument, `tools/hwcaps/p50_mask_plumbing_probe.cpp`,
against the real client tree (`/mnt/projects/EverQuestP1999`) at the tip
build. Full logs in `review/T285/`.

| Race | Gap 1 (fresh-cache on-demand fallback) | Unmasked control | Gap 2 (equipped variant) |
|---|---|---|---|
| 60 SKE | 10/10 referenced masked textures `hasAlpha=true` | 16/16 stay opaque, 0 false positives | `skefa0101.bmp` (Wrist, materialId=1) `hasAlpha=true` |
| 70 ZOM | 2/2 referenced-with-triangles: `zomlg0001.bmp` true; `zomch0001.bmp` false | 24/24 stay opaque | `zomlg0101.bmp` (Legs) `hasAlpha=true` |

`zomch0001.bmp` reproduces P50's own documented result and its control: the
raw BMP's active pixel region contains **0/16384 palette-index-0 texels**, so
there is nothing for the mask to punch (the "citywlivy class"). T05's
independent, known-good masked decode returns the identical `false` for it.
Not a plumbing failure.

**Pixel magnitude** (what the mask actually does on this path): the probe's
raw-BMP scan of SKE's `skefa0001.bmp` reports **94 of 2048 texels are palette
index 0** — those 94 texels are the difference between the masked and unmasked
decode of that texture, and they are exactly what the fallback path would have
lost. `skefa0001.bmp` is referenced by 52 triangles of the SKE mesh.

### What this unit adds: the missing pin

`tests/test_palette_mask_plumbing.cpp` (3 tests, EDT_NULL, offline, skips
without `data/willeq.db` or the client tree — same gate as
`test_race_330_loader`):

1. `OnDemandFallbackDecodesMaskedTexturesMasked` — a fresh loader/cache pair
   resolves race 60 through `buildMeshFromGeometry()`'s fallback (the state
   P13b's deferral leaves a texture in); every masked texture with triangles
   must report `hasAlpha(name, true)`.
2. `OnDemandFallbackLeavesUnmaskedTexturesOpaque` — the same path must leave
   unmasked textures opaque (the flag is the WLD's, not a blanket).
3. `EquipmentVariantSwapKeepsTheMask` — `createAnimatedNodeWithEquipment()`
   with `equipment[Wrist]=1` must upload the swapped variant under the masked
   cache key AND that entry must carry alpha.

**Red/green, per site** (temporary local revert, restored; logs in
`review/T285/`):

| Revert | Result |
|---|---|
| `mesh_building.cpp`'s 4 args → `false` | test 1 FAILS with 10 named textures ("`skefa0001.bmp` (texIdx 15, 52 triangles) decoded opaque"); tests 2 and 3 still pass |
| `animated_mesh_creation.cpp`'s 2 args → `false` | test 3 FAILS ("equipped variant `skefa0101.bmp` was not uploaded as a masked variant"); tests 1 and 2 still pass |
| neither | 3/3 pass |

The two failures are disjoint, so each test pins its own site.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --test-dir build --output-on-failure` | **3162/3163** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk). Includes this unit's 3 new tests |
| `bash scripts/build-gles2-x86.sh` | PASS |
| `bash scripts/ci/visual_gate.sh` | **PASS**, goldens untouched (`git status` clean of any golden path — no production code changed, so no golden could move) |
| `bash scripts/build-arm-noble.sh` | PASS; `build-arm-noble/bin/willeq` verified ARM EABI5 ELF |
| `check_file_sizes.sh --files tests/test_palette_mask_plumbing.cpp` | ok, 242 lines |

### Deviations / notes

1. **No STOP triggered.** The golden re-bless STOP could not fire: this unit
   changes no production code, so no rendered pixel moved.
2. `check_file_sizes.sh fable` reports a pre-existing FAIL on
   `tools/hwcaps/zone_bench_loader.cpp` (3232 lines) and a WARN on
   `irrlicht_renderer.h` — both belong to T284/T287, which merged onto `fable`
   while this unit ran, not to this branch. Against this branch's own base
   (`6ad62863`) the only C++ file is the new test.
3. `refactor/deferred.md` is edited here and was also edited on `fable` by the
   T284 merge — expect a trivial conflict at merge time; the three T285 edits
   are localized (lines ~71, ~229, ~3295 of the pre-merge file).
4. The reason this item outlived its fix by ~4 weeks is structural: P50's
   verification was a hand-run tool, not a test. That is now corrected.
