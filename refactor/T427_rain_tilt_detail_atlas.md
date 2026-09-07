# T427 — the rain overlay tilts (B330); the detail atlas matches its generator (B265)

Class B ×2, slot2, no server. Read B330 (T414 deleted the inert tilt keys;
honouring them needs a rotated-quad draw — `RainOverlay::render()` tiles
axis-aligned `draw2DImage` rects), B265 (T403: `detail_atlas.bmp` is the
one BMP its generator does not reproduce — 29,509 px differ: an
A1R5G5B5 round-trip signature (every byte a multiple of 8) plus real
drift in the twelve footprint tiles where the generator now draws
per-biome colours), T403's §Review, T418's oracle rain videos
(`review/T418/oracle/` — does the official rain tilt with wind? measure
the streak angle in the frames), `tools/generate_textures.cpp`
(`detail_atlas`), `DetailTextureAtlas::createAtlasImage()`, and the
detail consumers (DetailManager) — the gate's outdoor zones DRAW this
atlas.

## Leg A — B330

1. Oracle first: from T418's rain frames, the streak angle vs vertical
   (and whether it changes with intensity). If the oracle's rain is
   vertical, the tilt keys were fiction — strike B330 as "oracle draws
   vertical rain" and stop the leg. If it tilts: implement a rotated
   quad path for the overlay (the 2D driver has `draw2DImage` with a
   rotation? read Irrlicht's; else draw the tiles as two triangles via
   the 3D material path the particle renderer uses), restore the two keys
   with the MEASURED angle, test pre-fix failing (the streak texture's
   principal axis in a rendered frame equals the configured angle).

## Leg B — B265

1. Separate the two causes: regenerate at full colour depth and compare
   to the shipped file — which pixels differ by the A1R5G5B5 quantisation
   only, which by the footprint drift. The generator is the truth for the
   shipped asset (the tree's rule since T403), so the shipped file must
   match it — BUT the gate's outdoor zones draw it, so the pixel goldens
   WILL move if the regenerated atlas differs visibly. Do the comparison,
   regenerate into a temp path, run the gate against it (`--eqe1-path`
   with a temp pack built from a temp textures dir — do NOT install), and
   report whether any golden moves and by how much (contact sheet). If
   none moves: ship it (cascade in one commit; install atomically with no
   ctest/gate/willeq running; report the mapped-bytes delta). If one
   moves: STOP with the contact sheet — that is a re-bless for Will.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, `TexturesReproducible`
/`PackReproducible` green, visual gate standalone + ARM, file sizes.

STOP: Leg B moves a golden (report; do not install).

## Review

Branch `task/T427-rain-tilt-detail-atlas`, base **`0e3742cd`**, slot2
(RelWithDebInfo, `EQT_GLES2_IRRLICHT_DIR` = slot2's own tree). No server
touched; no eqemu lock taken or needed (T426 holds it).

### Leg A — B330: STRUCK, "oracle draws vertical rain". No code change.

The plan's first step decided the leg. **The official Titanium client's rain
is vertical at every intensity, in rain and in snow, day and night**, so
`windTiltBase` / `windTiltIntensity` described a behaviour the oracle does not
have. T414 was right to delete them and there is nothing to restore.

**Method.** T418's oracle captures are 5 fps video of a static scene, so a
consecutive-frame difference is the moving rain and nothing else. Each
difference image is sheared by a candidate angle (each row shifted by
`tan(theta) * y`) and the variance of the column sums is maximised: streaks
parallel to the shear stack into tall columns, streaks at any other angle
smear. The curve is aggregated over every frame-pair whose difference energy
is above the run's median, then refined at 0.1 deg. Script:
`review/T427/legA/streak_angle_scan.py`.

**Result — angle FROM VERTICAL, positive = leaning right:**

| oracle state | source | frame-pairs | best angle | peak/median |
|---|---|---|---|---|
| day 11:00, rain intensity 9 | `T418_oracle_o1.mkv` @600s | 99 | **+0.0 deg** | 4.30 |
| day 11:00, rain intensity 5 | `T418_oracle_o5.mkv` @470s | 174 | **+1.1 deg** | 3.02 |
| night 22:00, rain intensity 9 | `T418_oracle_o4.mkv` @175s | 174 | **+0.3 deg** | 1.39 |
| night 22:00, snow intensity 5 | `T418_oracle_o4.mkv` @470s | 174 | **+0.6 deg** | 1.16 |

The strongest run (o1, peak/median 4.30 — the day-i9 state, most rain against
the brightest background) peaks in the **0.0 deg bin exactly**, and its curve
falls off symmetrically (0.92 at -0.5, 0.97 at +0.5, 0.63 at -3.0, 0.84 at
+3.0). The three weaker runs land inside 1.1 deg of it.

**Twelve single-frame measurements corroborate and show the noise floor.**
Taking active-minus-clear on the eight paired stills (intensities 2/5/9 and
snow 5, two sessions) and scanning two crops of each — the sky band at
`600..1020 x 0..340` and the wall at `0..600 x 0..460` — gives -0.15 to
-3.30 deg on the sky crops and +1.05 to +1.85 deg on the wall crops of the
**same frames**. A real tilt cannot change sign between two crops of one
image; that is a null with a ~2 deg noise floor, and it bounds the true angle
well inside anything a "wind tilt" would mean.

**Intensity does nothing to it** (2, 5 and 9 span 1.1 deg), so even a
tilt-scaled-by-intensity reading of the two deleted keys has no support. This
is the same shape as T418's own finding for the ambient: the intensity byte
reaches the client and drives particle density, and drives nothing else.

fable already matches: `RainOverlay::render()` tiles a vertical streak texture
with axis-aligned `draw2DImage` rects. **No rotated-quad path was written and
no key was restored** — the plan's own stop for this branch.

### Leg B — B265: SHIPPED. Nothing moved, and both of B265's claims were wrong.

**1. The separation the plan asked for.** Regenerating all 64 into a temp
directory (`build_textures.sh --textures-dir`) reproduces T403 exactly: 63
byte-identical, `detail_atlas.bmp` differing in **29,509 of 262,144 pixels**
with a byte-identical 54-byte header.

| cause | pixels | signature |
|---|---|---|
| A1R5G5B5 quantisation ALONE (`shipped == (regen >> 3) << 3`) | **18,913** | max channel delta 7 |
| everything else | **10,596** | confined to twelve 64x64 tiles, 883 px each |

**2. The 10,596 are NOT code drift — they are the same defect.** B265 says the
shipped footprints are "near-black (BGR 0..24) where `drawFootprintLeft/Right`
now draws the per-biome colour". They are not near-black: they carry the
per-biome colour at **~0.71x**. At the 114 pixels per tile where the generator
writes the base colour exactly (its `alpha == 1` pixels):

| biome | generator argument (r,g,b) | shipped (r,g,b) | ratio |
|---|---|---|---|
| Grass | (40, 35, 25) | (24, 24, 16) | 0.600 0.686 0.640 |
| Dirt | (50, 40, 30) | (32, 24, 16) | 0.640 0.600 0.533 |
| Sand | (140, 120, 80) | (104, 88, 56) | 0.743 0.733 0.700 |
| Snow | (180, 190, 210) | (128, 136, 152) | 0.711 0.716 0.724 |
| Swamp | (35, 45, 30) | (24, 32, 16) | 0.686 0.711 0.533 |
| Jungle | (45, 40, 35) | (32, 24, 24) | 0.711 0.600 0.686 |

Every shipped value is a multiple of 8 and every one is ~0.7 of its argument;
the per-channel scatter is exactly one 8-wide quantisation bucket. That is
**RGB premultiplied by alpha**, and the ratio over a whole footprint tile runs
0..0.729 — capped at the painter's own `alpha * 200` divided by 255 (0.784),
minus a bucket.

Why only twelve tiles: premultiplication is invisible wherever alpha is 0 or
255, and `drawFootprintLeft` / `drawFootprintRight`
(`src/client/graphics/detail/detail_texture_atlas_grass.cpp:96` and `:180`)
are the **only two writes in the entire atlas that emit a fractional alpha**
— `SColor(static_cast<irr::u32>(alpha * 200), ...)`. A grep of all three atlas
TUs for `SColor(` returns alpha 255 or the transparent fill for everything
else. So B265 was **one** defect (the retired tool's software-driver round
trip) with two symptoms, not two independent ones, and **no code ever drifted
from the asset**.

**3. The bigger correction: the visual gate has never drawn this texture.**
B265 and this plan both say `detail_atlas` "is drawn by every outdoor zone the
visual gate covers", and that is the STOP that guarded the item for two units.
It is false. `tools/hwcaps/gles2_zone_benchmark.cpp` — the binary the gate
runs — **never instantiates `DetailManager` and never names `detail_atlas`**;
`grep -rn "DetailManager\|detail_atlas" tools/hwcaps/` returns nothing at all.
No golden frame can contain a detail-atlas texel, so the STOP could not have
fired. It was measured, not assumed: see the gate result below.

**4. The gate, against a TEMP pack, before any install.**
`scripts/build_willeq_pack.sh` gained a `--textures-dir` flag (mirroring
`build_textures.sh`'s), so a candidate pack can be built from a regenerated
temp directory without touching the tree or the shared asset. Two packs were
built into the scratchpad:

| pack | textures dir | sha256 | bytes |
|---|---|---|---|
| control | `data/textures` (shipped) | `19e78745…` | 6,930,116 |
| candidate | regenerated temp dir | `ac78f333…` | 6,930,116 |

The **control reproduces the pinned hash exactly**, which proves the new flag
is inert by default. The candidate is the same **6,930,116 bytes** — ETC1 is a
fixed-rate codec, so an entry's byte count cannot follow its content.

The gate then ran with `data/eqe1_client` repointed at a staging tree holding
the candidate pack (2,503 symlinks to `/mnt/projects/EverQuest_ETC1` plus the
candidate as `willeq.eqe1`) and the regenerated BMP in `data/textures/`:

**`=== VISUAL GATE: PASS ===`**, and measured directly against the goldens:

| zone | f0 | f1 | f2 | eqe1MappedBytes measured | manifest |
|---|---|---|---|---|---|
| qeynos2 | **0 px** | **0 px** | **0 px** | 124,063,684 | 124,063,684 |
| gfaydark | **0 px** | **0 px** | **0 px** | 122,729,776 | 122,729,776 |
| befallen | **0 px** | **0 px** | **0 px** | 117,214,272 | 117,214,272 |

**Zero differing pixels in all nine frames** (max channel delta 0), and
**zero** mapped-bytes delta — the numbers are the manifest's, unedited. No
golden moves, no re-bless, no STOP. The `*_diff.png` files in `ci_artifacts/`
are stale from an earlier unit's run; the comparison above is against
`tests/goldens/*.png` directly. Log:
`review/T427/legB/visual_gate_temp_pack.log`. The symlink was restored to
`/mnt/projects/EverQuest_ETC1` immediately afterwards.

**5. Shipped.** `NOT_REPRODUCIBLE` is now **EMPTY** and
`build_textures.sh --check` reports **`65 byte-identical, 0 drifted, 0
exempt`** — the whole of `data/textures/` is derived from the tree, which is
what B260 existed to reach.

### Tests

| test | pre-fix | post-fix |
|---|---|---|
| `TexturesReproducible` (`build_textures.sh --check`, exemption removed) | **FAIL exit 1** — `FAIL[RECIPE]: detail_atlas.bmp shipped=aedfad70… regenerated=6400d849…`, `64 byte-identical, 1 drifted, 0 exempt` | **PASS exit 0** — `65 byte-identical, 0 drifted, 0 exempt` |
| `PackReproducible` (`build_willeq_pack.sh --check`) | would FAIL on the new pin | PASS |

Pre-fix run reproduced by restoring the shipped BMP with
`git show HEAD:data/textures/detail_atlas.bmp > …` (never `git checkout --`).
Logs: `review/T427/legB/textures_check_{PREFIX,POSTFIX}.log`.

### Deviations

- **`scripts/build_willeq_pack.sh` gained a `--textures-dir` flag**, which the
  plan did not name. It is what "a temp pack built from a temp textures dir"
  requires: the script hard-coded `$REPO_ROOT/data/textures`, so without it
  the only way to build a candidate pack is to dirty the shipped directory
  first — exactly the "install before the comparison is known" the plan
  forbids. The default path is unchanged and the control build proves it
  byte-for-byte.
- **Leg A produced no code and no test**, per the plan's own branch ("If the
  oracle's rain is vertical, the tilt keys were fiction — strike B330 … and
  stop the leg").

### What surprised me

1. **The STOP that guarded B265 for two units was guarding nothing.** The
   claim "the gate's outdoor zones draw this atlas" was inherited from T403
   into the board and into this plan and was never checked against the
   benchmark binary; the benchmark has no `DetailManager` at all. A whole
   class of caution was spent on a texture the gate cannot see.
2. **B265's "two independent ways" were one way.** The premultiply is the same
   software-driver round trip as the quantisation, and it is visible in
   exactly the twelve tiles whose painter is the only one in the atlas that
   writes a fractional alpha. "Near-black where the generator draws colour"
   was a misreading of a 0.71x scale.
3. **ETC1 being fixed-rate makes an asset content change free in the pack.**
   The candidate pack is the same size to the byte, so `eqe1MappedBytes` — the
   number T424 had to have re-blessed for a 378-byte move — cannot move at all
   for a same-format texture edit.
4. **`detail_atlas.bmp` is 24-bit while both consumers alpha-BLEND it**
   (`DetailChunk::rebuildMesh`, `FootprintManager`, both
   `EMT_TRANSPARENT_ALPHA_CHANNEL`), and every tile is painted on a
   transparent ground. That is the B266 shape T406 fixed for sixty files; this
   one was skipped only because B265 had it flagged. It is a LOOK change, not
   a cascade, so it is boarded as **B410 (C)** rather than absorbed here.

### Board

- **B330 STRUCK** — "oracle draws vertical rain", with the four-run table.
- **B265 STRUCK** — regenerated; both of its stated claims corrected in place.
- **B410 (C) NEW** — the detail atlas is 24-bit and alpha-blended.
- Stale mentions struck too: `CLAUDE.md`, `tools/retired/README.md`, and the
  two B265 cross-references inside the struck B266 and B268 entries.
