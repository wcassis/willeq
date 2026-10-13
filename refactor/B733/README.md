# B733 (issue #70) — the race model builder selects parts by NAME, so a listed part with another name is dropped

## Verdict: CONFIRMED (Class A), fixed at the root

The defect is real and reproducible on origin/fable. Race 146 (Spectral Sarnak,
code `SSN`, archive `cabeast_chr.s3d`) is the proof subject: its skeleton
`SSN_HS_DEF` references, via its 0x200 mesh list, two meshes — the body
`SSN_DMSPRITEDEF` (3359 verts) and the head `SNNHE00_DMSPRITEDEF` (974 verts).
The "SNN" spelling differs from the race code "SSN", so the name-based
selection `{CODE}_DMSPRITEDEF` / `{CODE}HE00_DMSPRITEDEF` (which computes
`SSN_DMSPRITEDEF` and `SSNHE00_DMSPRITEDEF`) selects the body but never the
head. The built model holds 3359 verts instead of the 4333 the skeleton
references.

## The part list for race 146, as the test derived it

The ctest `B733Race146LoaderTest.BuiltModelHoldsEveryMeshTheSkeletonReferences`
(`tests/test_b733_skeleton_mesh_refs.cpp`) does NOT hand-type this list. It
re-parses `cabeast_chr.s3d` with the production `S3DLoader`, reads the
race's skeleton track (`SSN_HS_DEF`) out of the WLD data, takes the union of
the bone-attached meshes (`SkeletonBone::modelRef`) and the 0x200 mesh list
(`SkeletonTrack::meshRefs`, T476), resolves each through the loader's own
`modelRefs` → `getGeometryByFragmentIndex`, and asserts the built
`combinedGeometry` holds every one. Output on the FIXED build [measured]:

```
B733: SSN_HS_DEF references [SNNHE00_DMSPRITEDEF(974), SSN_DMSPRITEDEF(3359)] total=4333
```

## RED on origin/fable (unpatched HEAD 16c9cf44)

Built the test against a detached `16c9cf44` worktree (no B733 loader/selection
change) and ran it:

```
B733: SSN_HS_DEF references [SNNHE00_DMSPRITEDEF(974), SSN_DMSPRITEDEF(3359)] total=4333
.../test_b733_skeleton_mesh_refs.cpp:217: Failure
Expected equality of these values:
  actual
    Which is: 3359
  expected
    Which is: 4333
built model holds 3359 of the 4333 verts the skeleton references; the
referenced meshes it dropped are the 0x200-listed ones whose names the
name-based selection (SSN/SSNHE00) does not match
[  FAILED  ] B733Race146LoaderTest.BuiltModelHoldsEveryMeshTheSkeletonReferences (783 ms)
```

`SNNHE00_DMSPRITEDEF` (the 974-vert head) is the dropped part. Raw capture:
`/home/user/kb-evidence/b733-race-model-parts/red_test_output.txt`.

## GREEN on the fix

```
[ RUN      ] B733Race146LoaderTest.BuiltModelHoldsEveryMeshTheSkeletonReferences
B733: SSN_HS_DEF references [SNNHE00_DMSPRITEDEF(974), SSN_DMSPRITEDEF(3359)] total=4333
[       OK ] B733Race146LoaderTest.BuiltModelHoldsEveryMeshTheSkeletonReferences (711 ms)
[  PASSED  ] 1 test.
```

## The fix (root, two parts)

1. **The loader must store every mesh the skeleton references.**
   `S3DLoader::loadCharacters` (`src/client/graphics/eq/s3d_loader_characters.cpp`)
   stored only the meshes `flattenSkeleton` walks off the BONES. It now stores
   the **union**: the bone meshes, plus — when the bone walk stored nothing —
   the existing prefix-match family (verbatim, same order), plus every mesh in
   the skeleton's 0x200 mesh list (`skeleton->meshRefs`) that neither of those
   stored, appended AFTER the old stored set with an unskinned copy for
   animation (the prefix path's device). Appending after (not interleaving) is
   what keeps every character the old loader stored non-empty for in the OLD
   PART ORDER, so every old byte stays identical. Each appended mesh is also
   recorded in `CharacterModel::skeletonReferenced` (a pointer tag into
   `parts`) so the builders can tell which stored part is a #70-class addition
   and which is old name-selection material.

2. **The base builders must select the old part set plus the skeleton's
   additions.**
   `selectCharacterParts()` (`src/client/graphics/eq/geometry_combiner.cpp`,
   declared in `include/client/graphics/eq/geometry_combiner.h`) runs the OLD
   name selection verbatim (in stored order, so unchanged characters keep the
   exact same part order) and adds every `skeletonReferenced` geometry the name
   selection did not pick. It is wired into the five base-builder call sites:
   `model_loading_chr.cpp` (`loadModelFromZoneChr`, `loadModelFromCachedChr` —
   the path race 146's db row drives), `model_loading_search.cpp`
   (`searchZoneChrFilesForModel`), and `model_loading.cpp` (the global-archive
   path `loadModelFromGlobalChr` and the numbered-global path
   `loadModelFromNumberedGlobal`, `model_loading_chr.cpp:632`).

Why NOT "select the whole stored set" (the first, reverted approach): the
stored set now includes the prefix family's VARIANT meshes (HUM01, HUMHE01,
...), which the old name selection deliberately excluded from the BASE model —
the variant path (`preloadVariantModel`, `variant_loading_preload.cpp`) does
its own name-based selection over the stored parts and never calls
`selectCharacterParts`. Selecting the whole stored set dragged every variant
mesh into the base model: HUM variant(1,1) went 714→791 verts in CI (pipeline
779, four `EntitySourcePruning` golden failures) and every other multi-variant
race the same way. The union-selection above restores all of those rows
byte-identically (see blast radius).

`variant_loading_animation.cpp` is deliberately NOT changed: `selectVariantParts`
selects an explicitly REQUESTED variant name (HE01/HE02, body variant N) with a
HE00/body-0 fallback — a different job from the base builder's "pick the default
body + head". Variant requests still resolve by name against the stored parts,
which is the correct behaviour there.

## Gate impact: the gate-zone character frames DO change — 6 lines per zone [measured]

The first pushed attempt (stored-set selection, 2 commits `98fafd1a`,
`c69aeb97`) was RED in CI: pipeline 779, four
`EntitySourcePruning.ModelDataIsByteIdenticalToTheGolden` failures,
~53–60 changed fingerprint lines per gate zone, dominated by VARIANT rows
regressing to the base vertex count (HUM 714→791) because the prefix family's
variant meshes were no longer stored. That was the wrong fix, reverted.

The corrected fix was re-measured with `tools/hwcaps/model_fingerprint`
(the exact tool the four `EntitySourcePruning` goldens diff against), against
the OLD-code output (baseline worktree at pre-B733 `16c9cf44`, which
reproduces all four committed goldens byte-identically — verified, 0 changed
lines) in all four golden zones. Per zone the diff is EXACTLY 6 lines, and all
six are #70-class base-row recoveries:

| zone | changed lines |
|------|---------------|
| gfaydark | 6 |
| dreadlands | 6 |
| iceclad | 6 |
| qeynos2 | 6 |

The lines, identical in all four zones (gfaydark shown, byte-for-byte):

```
- race=127 gender=0 variant=0,0 code=IVM verts=73 tris=80 geom=2449dd4bcdfed4c2 bones=26 skel=7ff80fd6c12ba4a2 anims=- xf=14650fb0739d0383
- race=127 gender=0 variant=1,1 none
- race=146 gender=0 variant=0,0 code=SSN verts=3359 tris=2402 geom=4d1ec6f1c663cbc2 bones=98 skel=661d75a36800a555 anims=c05,d02,d05,l01,l02,o01,p01,p03,t06 xf=8489b7daa48b4b76
- race=146 gender=1 variant=0,0 code=SSN verts=3359 tris=2402 geom=4d1ec6f1c663cbc2 bones=98 skel=661d75a36800a555 anims=c05,d02,d05,l01,l02,o01,p01,p03,t06 xf=8489b7daa48b4b76
- race=240 gender=0 variant=0,0 code=IVM verts=73 tris=80 geom=2449dd4bcdfed4c2 bones=26 skel=7ff80fd6c12ba4a2 anims=- xf=14650fb0739d0383
- race=240 gender=0 variant=1,1 none
+ race=127 gender=0 variant=0,0 code=IVM verts=506 tris=360 geom=0704dc261a0b8ba5 bones=26 skel=7ff80fd6c12ba4a2 anims=- xf=14650fb0739d0383
+ race=127 gender=0 variant=1,1 code=IVM verts=506 tris=360 geom=0704dc261a0b8ba5 bones=26 skel=7ff80fd6c12ba4a2 anims=c01,...,t09 xf=3b73e3e8c7b5ff48
+ race=146 gender=0 variant=0,0 code=SSN verts=4333 tris=3074 geom=34a3881c6e2a691e bones=98 skel=661d75a36800a555 anims=c05,d02,d05,l01,l02,o01,p01,p03,t06 xf=8489b7daa48b4b76
+ race=146 gender=1 variant=0,0 code=SSN verts=4333 tris=3074 geom=34a3881c6e2a691e bones=98 skel=661d75a36800a555 anims=c05,d02,d05,l01,l02,o01,p01,p03,t06 xf=8489b7daa48b4b76
+ race=240 gender=0 variant=0,0 code=IVM verts=506 tris=360 geom=0704dc261a0b8ba5 bones=26 skel=7ff80fd6c12ba4a2 anims=c01,...,t09 xf=3b73e3e8c7b5ff48
+ race=240 gender=0 variant=1,1 code=IVM verts=506 tris=360 geom=0704dc261a0b8ba5 bones=26 skel=7ff80fd6c12ba4a2 anims=c01,...,t09 xf=3b73e3e8c7b5ff48
```

Mechanism per changed row, from the loader's own DEBUG log
(`/home/user/kb-evidence/b733-race-model-parts/loader_debug_gfaydark.txt`):

- **SSN (race 146, both genders):** the skeleton `SSN_HS_DEF` names nothing off
  its bones (`flattenSkeleton: parts=0`); its 0x200 list adds
  `SNNHE00_DMSPRITEDEF` (974 verts) on top of the prefix body+head. Base row
  3359→4333 verts — the #70 head.
- **IVM (races 127 and 240, code IVM):** the bone walk stored only
  `IVMHE00_DMSPRITEDEF` (73 verts — the head); the skeleton's 0x200 list adds
  the body `IVM_DMSPRITEDEF` (506 verts). Base row 73→506. The variant(1,1)
  row gains a model (`none` → the same 506-vert geometry) purely as a
  consequence: the variant path falls back to the base body+head when its
  requested names are absent, and the base now holds the body.

Every other line in all four zone files — all variant rows of every
multi-variant race (HUM 714/791, GRM 715, ...), every other base row — is
byte-identical to the old code [measured, `b733_bytelines.py` line-diff].

This is a gate-zone character change, so per acceptance criterion 2 it was
blessed by Will (2026-10-11, commit `bdf9f802` on this branch): the four
`EntitySourcePruning` goldens were regenerated with the test's own command,
exactly these 6 lines per zone. **No image golden moved anywhere** (the CI
gate's zone image comparisons pass at `4139cee6`; only the memory metric
below moved). The memory metric was a SEPARATE bless question, answered by
Will at `4463dd3b` (next section).

## Gate memory metric: `characterModelsBytes` moves in all five zones -- blessed by Will at `4463dd3b`

The CI `gate` job (pipeline 782, head `4139cee6`, all 3 in-script attempts,
deterministic) fails on the `memoryCoverage` values in
`tests/goldens/manifest.json` only:

| zone | blessed | got | delta |
|------|---------|-----|-------|
| qeynos2 | 1,445,267 | 1,445,411 | +144 |
| gfaydark | 1,445,267 | 1,445,411 | +144 |
| befallen | 1,445,267 | 1,445,411 | +144 |
| iceclad | 1,522,794 | 1,522,962 | +168 |
| timorous | 1,465,276 | 1,465,468 | +192 |

Mechanism, proven [measured]: `CharacterModel` grew by exactly one member --
`std::vector<const ZoneGeometry*> skeletonReferenced` (s3d_loader.h:149), the
#70 tag. `sizeof(CharacterModel)` = 160, the added member = 24 bytes (a
compiled probe at `4139cee6`). `calcCharModels()`
(model_loading_search.cpp:918) charges `sizeof(CharacterModel)` per retained
model and never counts `skeletonReferenced`'s contents (empty or filled), so
the gate's `--test-races 1..5` run gains 24 B per loaded model: +144 = 6
models (the three classic zones), +168 = 1,522,962 - 1,522,794 = 7 models
(iceclad), and +192 = 1,465,468 - 1,465,276 = 8 x 24 B (timorous).
No new geometry, no new skeleton data, no frame change: the five gate zones'
image goldens are byte-identical in the same CI run. Races 127/146/240 are
outside `MEMORY_COVERAGE_TEST_RACES` (visual_gate.py:248), so the recovered
IVM/SSN meshes are not in the sample at all -- the entire delta is the struct
member.

`manifest.json` is BLESS-ONLY (Will's rule); the 2026-10-11 bless at
`bdf9f802` covered the four `model_fingerprint/*.txt` goldens, not these
`memoryCoverage` values. **BLESSED (Will, 2026-10-11, commit `4463dd3b` on
this branch):** `memoryCoverage.zones[*].characterModelsBytes` re-pinned to
the `got` column above (the +24 per loaded model is a permanent consequence
of the #70 tag; the alternatives -- dropping the tag or special-casing the
metric -- both break the 100 %-accounting shape of `calcCharModels`).

## The #69 (B732) row: what fable now fixes, and what still rides the held branch (2026-10-12)

B732 (issue #69) is the same loader defect at full T479 scope: 49 zone-chr
skeletons whose 0x200-listed meshes carry no race-code prefix stored NO
model, plus 8 names that missed listed meshes (GOM, SSN, UDK, SRG, TRT,
CCD, GOJ, IVM). B733's loader fix IS the loader fix: the 0x200 collect
(`s3d_loader_characters.cpp:262-279`, UNCONDITIONAL — it runs for every
skeleton and collects every listed mesh the bone walk did not store; the
`boneWalkStoredNothing` gate at `:282` is on the PREFIX-FALLBACK block
below it, not the collect) appends those extras AFTER the prefix fallback
(`357-395`), so on fable ALL of B732's chr-archive population — the 49
no-prefix skeletons and the 8 missed names — now have their listed meshes
stored. QA t_cbfd7f9a's censuses (2026-10-11, over all
378 `*_chr.s3d` archives and all 138 db races at the B733 tip): the
#70-class recovery is exactly SSN (7 archives: cabeast/charasis/chardok/
citymist/kaesora/karnor/kurn), IVM, FAF, PIF, TRI, GOM, and only races
127/146/240 change in the built-model byte census (127: 73→579, 240:
73→579, 146: 3359→4333 both genders) — every other race byte-identical.
What STILL rides the held T479 branch: (a) the test pin
`SkeletonMeshListLoaders.EveryMeshASkeletonListsIsAPart` (tests/ has no
such suite on fable; `B733Race146LoaderTest` pins the race-146 shape, not
the full 419-archive census); (b) the T479 companion fixes B730/B731
(object loader posing, skeleton double-skinning); (c) the merge blockers
(Pi OOM B750 / lever B752, qeynos2 gate bless). So B732's defect is FIXED
on fable by B733's loader; what remains open is the test net and the
T479-branch merge.

## Not verified / left open

- The visual gate itself (`scripts/ci/visual_gate.py`, all five manifest zones)
  runs on the CI `gate` job, not in this worker (4 GiB cap; no local gate).
  The `model_fingerprint` byte-diff above is the local proof of exactly which
  character frames move and why; the CI gate job is the authoritative run.
- **BLESSED (Will, 2026-10-11):** the four `EntitySourcePruning` goldens were
  re-blessed by Will himself at `bdf9f802` with the test's own command
  (`WILLEQ_PRUNE_SOURCES=1 model_fingerprint --zone <z> --eq data/eq_client
  --db data/willeq.db`) — exactly the 6 lines/zone above, every other line
  byte-identical. The old-code baseline proving the old goldens reflected
  pre-B733 behaviour is at
  `/home/user/kb-evidence/b733-race-model-parts/baseline_<zone>.txt`.
- `TexturesReproducible` / `PackReproducible` / `DbReproducible` fail in THIS
  environment (also fail on a pristine `16c9cf44` worktree) — pre-existing and
  environmental (they re-derive shipped data bytes and compare to committed
  pins), NOT caused by this unit, which touches no shipped data, generator, or
  pin file. FileSizeCeiling / DocCitations / DocCounts / DocClaims /
  CommentClaims all pass [measured].
