# P44b — P37 bless-evidence RETAKE: pose-identical static-benchmark stills

**Slot: slot2. x86 ONLY. No Pi, no eqemu (this unit is fully offline via
gles2_zone_benchmark — no live server session).**

Will REJECTED P44's live-client before/after pairs (see
`refactor/P44_p37_bless_evidence.md` Review, now annotated). Two
compounding, correctly-identified problems: (1) live aggroed mobs ran
attack animations and `/screenshot` was not animation-synchronized, so
BEFORE/AFTER caught different poses — the pixel diff was dominated by pose,
proving nothing about alpha; (2) the vantage put dark stone/shadow behind
the ribcages, where pre-P37 masked-opaque and post-P37 alpha-test are
visually indistinguishable against a dark background. This unit's job:
retake the evidence with both failure modes structurally impossible by
construction — deterministic camera/entity placement (no live pose drift)
and framing that puts bright background behind the mask regions.

## Work

Design requirements (both must hold by construction, not by care):
1. POSE-IDENTICAL: `gles2_zone_benchmark`'s deterministic camera/entity
   placement, not a live client.
2. BACKGROUND CONTRAST: frame so bright surfaces sit behind mask regions;
   close-up orbit framing (P08d retake precedent); contact sheet of
   multiple angles/elevations; select working shots, name the ones that
   fail and why; STOP and report if NO angle shows the effect.

## Review

**HEADLINE — LEAD WITH THIS**: the visual claim is a NULL RESULT on this
instrument. BEFORE and AFTER binaries render PIXEL-IDENTICAL entity
silhouettes for skeleton (SKE) and zombie (ZOM) at every angle/radius/scale
tested in befallen. This is NOT evidence P37's fix is wrong — a temporary
numeric diagnostic (reverted before commit, same precedent as P37's own
Review) confirms the AFTER binary's `mesh_building.cpp` correctly classifies
every ground-truth masked texture from P37's own table (texture-for-texture
match, no exceptions) and assigns the alpha-test MaterialType marker. The
null is root-caused to a real gap in `gles2_zone_benchmark`'s entity
rendering fidelity: the tool never links `entity_renderer.cpp` (confirmed
via `nm` symbol absence), so the companion-alpha-texture wiring that
production's live client uses to actually make the discard shader see real
alpha data never happens in this tool — the correct MaterialType is set,
but no shader ever receives alpha to discard on. Full technical root-cause,
per-texture diagnostic trace, and all pixel-diff numbers are in the
committed manifest: `benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt`.
Stills at `/mnt/projects/fable/videos/p44b_bless_retake/` (NFS, gitignored):
`pairs/` (before/after PNGs at 3 angles x 2 races), `contact_sheet/` (16-frame
angle/elevation survey per race + 3-radius comparison), `crops/` (400%
zoomed torso/leg/chest detail), `diffs/` (ImageMagick AE diff overlays).

**Design requirement 1 (pose-identical) — MET, but not via the literal
route the plan suggested.** The plan's literal suggestion ("static
scenegraph route... no `--animate`... entities hold bind pose... the static
path is byte-deterministic") turned out to not exercise P37's fix at all:
empirically, `gles2_zone_benchmark`'s no-`--animate` entity path calls
`RaceModelLoader::getMeshForRace` → `ZoneMeshBuilder::buildTexturedMesh`
(`zone_geometry.cpp`), a DIFFERENT, older masked-texture check ("H02d",
zone-geometry-only, unrelated to P37 — confirmed zero diff in that file
between `fda2e34` and current fable tip) — NOT
`RaceModelLoader::buildMeshFromGeometry` (`mesh_building.cpp`, P37's actual
fix). `buildMeshFromGeometry` is only reached via the ANIMATED node path
(`createAnimatedNode`, gated behind `--animate` in the benchmark's own CLI
handling) — confirmed by adding the temporary diagnostic and observing it
fire only with `--animate` present. So `--animate` was REQUIRED, contrary
to the plan's assumption, to even reach the code P37 touched. Verified this
still satisfies "pose-identical by construction": the benchmark's animation
clock is `simTimeMs = frame * 33.3f`, a pure function of the (fixed)
`--dump-frames` index, not wall-clock time — so a given frame index
produces a bit-identical bone pose on every run of either binary. This is
the SAME guarantee the plan wanted from the static path, reached by a
different but equally deterministic route. Confirmed empirically: PNG file
sizes match within ~40 bytes and pixel diffs are 0 or near-0 (see table
below) at every angle, i.e. the pairs really are aligned, not just
assumed to be.

**Design requirement 2 (background contrast) — MET as framing, moot given
the null result.** `--orbit-radius 8 --orbit-height-offset 6` puts the
entity silhouetted against befallen's flat navy "sky" clear-color (RGB
40,40,60) at several points in the orbit — high, uniform contrast against
both stone geometry and bone/flesh texture colors, exactly what the plan
asked for (see `crops/ske_AFTER_f26_torso_zoom.png`). This framing choice
turned out not to matter for the outcome (see root cause), but the method
itself is sound and reusable for a future retake once the tool gap below
is closed.

**Alpha-test-path verification** (the plan's "log or counter, not
assumption" requirement): temporary `fprintf(stderr, ...)` diagnostic in
`mesh_building.cpp` immediately after the `isMaskedMaterial` assignment
(same call site P37's own Review documents), reverted via
`git checkout --` before any final build/commit — `git status` clean
before, during (isolated to one rebuild), and after. Trace output matches
P37's ground-truth table exactly for both SKE and ZOM (skech0001/
skefa0001/skeft0001/skeft0002/skelg0001 and zomch0001/zomlg0001 all
correctly flagged `isMasked=1`/`MaterialType=14`; all sibling unmasked
textures correctly flagged `isMasked=0`/`MaterialType=0`). Full trace in
the manifest.

**Pixel-diff results** (3 orbit angles x 2 races, `--orbit-radius 8
--orbit-height-offset 6 --animate`, frames 12/26/42 of 48):

| pair | frame | AE (px/480000) | verdict |
|---|---|---|---|
| ske | f12 | 44 (0.0092%) | FP-noise, off-entity (zone-geometry edge), below P37's own 0.05% floor |
| ske | f26 | 0 | pixel-identical |
| ske | f42 | 0 | pixel-identical |
| zom | f12 | 44 (0.0092%) | same off-entity noise, race-independent |
| zom | f26 | 0 | pixel-identical |
| zom | f42 | 0 | pixel-identical |

Extreme close-up re-check (`--orbit-radius 3`, ~2.5x closer): 0 AE at all
sampled frames — rules out "too small to register a pixel" as an
alternative explanation for the null.

**Zombie chest (zomch0001) — expectation confirmed, doubly-null.** Per the
plan's own upfront framing, `zomch0001` is P37's documented citywlivy-class
case (WLD masked=true, decode-opaque, unchanged BY DESIGN) — the diagnostic
confirms it still gets classified `MaterialType=14`, exactly as intended;
the null here is CORRECT and expected independent of the tool-wiring gap.
`zomlg0001` (legs, real alpha per P37's table) is null for the different,
tool-wiring reason described above — it is NOT citywlivy-class, and would
be expected to show a real (if possibly subtle at this mob's screen size,
per P37's own live-capture finding) effect on an instrument that actually
exercised the companion-texture path.

**Bear (`beahe0008`, qeynos2) — DROPPED**, per the plan's explicit
allowance. `--test-variant` only selects body-armor variants 0-4
(naked/cloth/chain/plate/leather); `beahe0008.bmp` is head-texture PAGE 8,
which the benchmark has no CLI hook to select (P44 reached it only via a
live GM `#set texture 8` command with no benchmark equivalent). Not
pursued — P37's own numeric trace for this texture is already solid, and
the SKE/ZOM findings above already answer "does the benchmark demonstrate
the effect at all" (no, for a tool reason unrelated to which race).

**Contact sheets**: `contact_sheet/montage_{ske,zom}_r8h6.png` (16-frame
sweep, entity visible ~5-6/16 tiles — the rest are empty zone geometry, the
entity simply outside the camera frustum at that orbit angle, not a
lighting/background failure), plus `montage_ske_r15h6.png` /
`montage_ske_r25h6.png` for the two other radii tried (r15: entity smaller,
narrower visible window; r25: entity larger/closer, visible window shifted
later in the orbit). r8 was selected as primary for the most consistent
multi-angle coverage against the navy-sky background.

**Root cause (why "confirmed taking the alpha-test path" ≠ "visibly
different pixels")**: `mesh_building.cpp`'s fix sets `MaterialType` and
binds the MAIN texture on unit 0 only — it does not (and per P37's Review,
was never meant to; that wiring belongs to `entity_renderer.cpp`) bind a
companion alpha texture on unit 1. `entity_renderer.cpp` (P21/P37's
`applyAlphaTestMaterialType()`, which does that companion binding /
RGBA-variant selection) is NOT compiled into `gles2_zone_benchmark`
(`nm -C build/bin/gles2_zone_benchmark | grep applyAlphaTestMaterialType`
→ no match). Without it, the entity keeps Irrlicht's built-in
`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` type, which
`COpenGLES2Driver::applyMaterialState` maps to the built-in
`EOGLES2SP_ALPHA3D` shader — and that shader's fragment source
(`ALPHA3D_FS_BASE`/`_DERIVATIVES`, `COGLES2Shaders.cpp`) discards based on
`texture2D(uTexture, vTexCoord).a` on unit 0 ONLY. ETC1-loaded textures
have no alpha plane on unit 0 at all (confirmed: also tested dropping
`--eqe1-path` to force raw-BMP decode instead — still 0 diff, so the raw
BMP path doesn't deliver usable unit-0 alpha for this shader either). This
is a `gles2_zone_benchmark` fidelity gap relative to production, not a
finding against P37.

**Recommendation (not actioned this unit, out of territory — this is an
evidence errand)**: a future unit could either (a) wire
`entity_renderer.cpp`'s `applyAlphaTestMaterialType()` (or an equivalent
companion-binding step) into `gles2_zone_benchmark`'s entity load path so
this tool can visually demonstrate entity alpha-test fixes going forward,
or (b) attempt a carefully pose-synchronized, well-lit LIVE capture (P44's
original approach, fixed for both of Will's specific objections) as a
one-off for this particular bless. Recommend (a): it's a one-time tool fix
that de-risks every future entity-rendering bless errand, not just this one.

**Errand bar**: no production source diff vs fable tip (`git status` shows
only this plan file + `refactor/P44_p37_bless_evidence.md` annotation +
this manifest; the temporary `mesh_building.cpp` diagnostic was reverted
and confirmed clean before any commit — verified twice, once immediately
after `git checkout --` and once after the final rebuild). Both binaries
verified non-stub by size before capture. Builds clean (`willeq` +
`gles2_zone_benchmark`, both trees). No eqemu/live-server session this
unit — offline only. Full ctest/gate not run (no source changes to gate,
same narrowed bar P44 used).
