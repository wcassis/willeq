# B733 (issue #70) — the race model builder selects parts by NAME, so a listed part with another name is dropped

## Verdict: CONFIRMED (Class A), fixed at the root, gate byte-identical

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
[       OK ] B733Race146LoaderTest.BuiltModelHoldsEveryMeshTheSkeletonReferences (757 ms)
[  PASSED  ] 1 test.
```

Also via ctest: `ctest -R B733Race146LoaderTest` → `1/1 Test ... Passed 0.79 sec`.

## The fix (root, two parts)

1. **The loader must store every mesh the skeleton references.**
   `S3DLoader::loadCharacters` (`src/client/graphics/eq/s3d_loader_characters.cpp`)
   previously stored only the meshes `flattenSkeleton` walks off the BONES.
   It now also stores every mesh in the skeleton's 0x200 mesh list
   (`skeleton->meshRefs`) that the bones did not name, with an unskinned copy
   for animation, exactly as the existing prefix-match path does. When the 0x200
   list is empty this block is a no-op.

2. **The base builders must select the skeleton-referenced part set.**
   New `selectCharacterParts()` (`src/client/graphics/eq/geometry_combiner.cpp`,
   declared in `include/client/graphics/eq/geometry_combiner.h`): when the
   skeleton's 0x200 list is non-empty, the selection is the whole stored part
   set (the loader stored exactly the referenced set, so "select all stored" ==
   "select what the skeleton references"); when the list is empty, the old
   race-code name selection runs verbatim. Wired into the five base-builder
   call sites: `model_loading_chr.cpp` (`loadModelFromZoneChr`,
   `loadModelFromCachedChr` — the path race 146's db row drives),
   `model_loading_search.cpp` (`searchAndBuildModel`), and `model_loading.cpp`
   (the global-archive + search paths).

`variant_loading_animation.cpp` is deliberately NOT changed: `selectVariantParts`
selects an explicitly REQUESTED variant name (HE01/HE02, body variant N) with a
HE00/body-0 fallback — a different job from the base builder's "pick the default
body + head". The base parts (HE00 + body) now come from the skeleton set;
variant requests still resolve by name, which is the correct behaviour there.

## The gate is byte-identical (no bless, no golden touched) [measured]

The card's hard constraint: the five gate zones' frames must be byte-identical.
Two independent checks:

**Memory-coverage run renders only races 1–5.**
`scripts/ci/visual_gate.py`: `MEMORY_COVERAGE_TEST_RACES = [1, 2, 3, 4, 5]`
(line 248) and the coverage run passes `--entities 5 --test-races 1,2,3,4,5`
(lines 398–399). I built `built_model_identity` (tools/b733/built_model_identity.cpp)
against the HEAD build and the fixed build; it drives the FULL production path
(`RaceModelLoader::ensureModelDataLoaded`) and fingerprints the exact
`combinedGeometry` bytes (vertices + triangles + vertexColors). Races 1–5, both
genders, 10 hashes, are IDENTICAL between HEAD and fixed [measured]:

```
RACE 1 g=0 nv=791 nt=444 nvc=0 h=f5b9dfa5a7f909f8   (== fixed)
RACE 1 g=1 nv=793 nt=430 nvc=0 h=ea136a357df98eed   (== fixed)
RACE 2 g=0 nv=896 nt=462 nvc=0 h=550c15cc79caadbc   (== fixed)
RACE 2 g=1 nv=784 nt=414 nvc=0 h=f6537ba7f5b97355   (== fixed)
RACE 3 g=0 nv=862 nt=411 nvc=0 h=55a716b3bcfd68d9   (== fixed)
RACE 3 g=1 nv=810 nt=427 nvc=0 h=1b588880fa017543   (== fixed)
RACE 4 g=0 nv=767 nt=440 nvc=0 h=888dedb675a8d123   (== fixed)
RACE 4 g=1 nv=902 nt=485 nvc=0 h=8668d594dfb920f4   (== fixed)
RACE 5 g=0 nv=877 nt=460 nvc=0 h=c62d28cfb2384883   (== fixed)
RACE 5 g=1 nv=872 nt=454 nvc=0 h=18a104e225055fde   (== fixed)
RACE 146 g=0 nv=3359 nt=2402 nvc=0 h=f131b4517108f8c0   ->  nv=4333 nt=3074 h=fe9196266384e647 (the fix)
RACE 146 g=1 nv=3359 nt=2402 nvc=0 h=f131b4517108f8c0   ->  nv=4333 nt=3074 h=fe9196266384e647 (the fix)
```

Raw: `/home/user/kb-evidence/b733-race-model-parts/identity_head.txt` and
`identity_fixed.txt`.

**Golden-image runs render no character models at all.** `visual_gate.py`
(lines 73–74, 217–218): the golden runs never pass `--eqe1-path`/`--objects`/
`--entities`, so no race model is rendered into any golden frame. Nothing this
unit changes can move a golden pixel.

## Full blast radius of the fix [measured]

`selection_census` (tools/b733/selection_census.cpp) compares, per character in
every reachable `_chr.s3d` archive (all 56 `race_models.s3d_file` values + the
era globals), what the OLD name selection draws vs what the NEW stored-set
selection draws. The only characters whose DRAWN set changes:

| code | race | old drew | new draws (added) |
|------|------|----------|-------------------|
| SSN  | 146 Spectral Sarnak | body only | + `SNNHE00_DMSPRITEDEF` (the head) |
| FAF  | 25  Fairy            | body + HE00 | + `FAFHE01_DMSPRITEDEF` |
| PIF  | 56  Pixie            | body + HE00 | + `PIFHE01_DMSPRITEDEF` |
| GOM  | (monster, no race row) | body + HE00 | + `GOMBI_L`, `GOMFO_L` limbs |
| TRI  | 151 Tribunal         | body + HE00 | + `TRIHE01_DMSPRITEDEF` |

None of these is a gate race (1–5) and none is rendered in a golden frame.
They are all the same class of bug as #70: a part the skeleton's 0x200 list
references that the name selection (which only knows `{CODE}_DMSPRITEDEF` and
`{CODE}HE00_DMSPRITEDEF`) could not name, so it was dropped. The fix recovers
them. Full listing: `/home/user/kb-evidence/b733-race-model-parts/census/selection_identity/selection_diffs.txt`.

## Not verified / left open

- The visual gate itself (`scripts/ci/visual_gate.py`, all five manifest zones)
  runs on the CI `gate` job, not in this worker (4 GiB cap; no local gate).
  The byte-identity above is the local proof that no golden/coverage byte moves;
  the CI gate job is the authoritative run. [assumed that the CI gate passes
  given the two local proofs; CI trace will confirm]
- `TexturesReproducible` / `PackReproducible` / `DbReproducible` fail in THIS
  environment (also fail on a pristine `16c9cf44` worktree) — pre-existing and
  environmental (they re-derive shipped data bytes and compare to committed
  pins), NOT caused by this unit, which touches no shipped data, generator, or
  pin file. FileSizeCeiling / DocCounts / DocClaims / CommentClaims /
  DataPathRouting all pass [measured].
