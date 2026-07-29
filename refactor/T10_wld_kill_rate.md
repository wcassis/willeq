# T10 — WLD parser: measure the mutation kill rate, then fix it

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

Prompted by mutation-testing intel the benchmark session routed to us
via `refactor/deferred.md` (2026-07-29): `wld_loader.cpp` has the most
covered lines in the repo (**952**) but killed only **1 of 8** injected
faults.

Will confirmed the benchmark clone reaches the EQ client files over the
same NFS share, so their WLD tests ran — the result is **not** an
artifact of skipped tests. The signal is real.

## Why the kill rate is that low — already diagnosed, don't re-derive

`WldIntegrationTest.WldLoader_ParseZone` (`tests/test_wld.cpp:317`)
drives the whole parser over a real zone and then asserts exactly two
things:

```cpp
ASSERT_TRUE(success) << "WldLoader failed to parse qeynos2.wld";
const auto& geometries = loader.getGeometries();
EXPECT_GT(geometries.size(), 0u) << "No geometries loaded";
```

"It didn't crash and produced at least one geometry." Nearly any
mutation inside the parser — a wrong vertex component, a flipped
winding, an off-by-one in an index table, a mis-resolved texture name,
a dropped fragment type — leaves both assertions true.

The file's other 296 assertions are real, but they are overwhelmingly
on **synthetic fragment structs** (skeleton, light, bsp, mesh, header,
texture suites), not on the loader's behavior over real data. Coverage
comes from the 5 integration tests; assertions come from the other 78.
That gap is the whole finding.

## Territory

**`tests/` only for anything committed.**

Part A requires temporarily injecting faults into
`src/client/graphics/eq/wld_loader.cpp` — a file that is off-limits by
path (P28 reclassified it as base data and moved it to
`CLIENT_CORE_SOURCES`, but it still lives under `src/client/graphics/`).

Those edits are a **measurement instrument, not a change**:

- Mutate, build, run, record, then `git checkout -- <file>` to revert.
- **Never commit a mutation.** Before your final commit,
  `git status --short` must show no modification to any file under
  `src/`. State that you checked.
- If a mutation somehow can't be cleanly reverted → STOP.

Nothing else under `src/` may be touched at all.

## Part A — measure the kill rate ourselves

Pick **8 representative mutations** spread across the parser's real
work, not clustered in one function. Suggested areas (adapt if the code
argues otherwise — read it first):

1. A vertex component decode (swap/scale/sign).
2. Triangle index assembly (off-by-one, or swap two indices).
3. Texture-name resolution (`resolveTextureName`, ~line 612).
4. A fragment-type dispatch case (drop or mis-route one type).
5. A bounding-box / extent computation.
6. Region/BSP association (`getGeometryForRegion`, ~line 1052).
7. A material or texture-index assignment.
8. `decodeZoneLineString` (~line 2510).

For each: apply it, build, run **`test_wld` only** (fast), record
KILLED (some test fails) or SURVIVED (all pass), then revert.

Record the result as a table. **This is the baseline and it is the
unit's primary deliverable** — even if Part B runs out of room, the
measured table is worth having.

Report the number honestly whatever it is. If our kill rate comes out
much better than 1/8, say so plainly — it would mean their mutation set
differed from ours, and that is useful intel to route back rather than
something to massage.

## Part B — strengthen the integration assertions

Target the 5 `WldIntegrationTest` cases, especially
`WldLoader_ParseZone`. Make them assert what the parser actually
*produced*, not merely that it produced something.

Use **`qeynos2.s3d`** as the primary fixture (already used, and present
on the NFS share). Assertions worth having:

- Exact geometry/mesh count, vertex count, triangle count.
- Bounding-box extents within a tight tolerance (`EXPECT_NEAR`).
- A specific known texture name resolving correctly, and the total
  distinct texture count.
- Material count and a spot-checked material→texture mapping.
- Vertex data sanity: no NaNs, coordinates inside the bounding box.
- Region/PVS counts where `hasPvsData()` is true.

**These are characterization assertions.** They pin *current* behavior,
which means if the parser is wrong today you are pinning the wrong
value. That is an acceptable trade — it catches regressions, which is
the point — but:

- Label them clearly as characterization, with the zone and the date.
- Where a value can be sanity-checked independently (a bounding box
  should be plausible for a city zone; a texture name should look like
  a real EQ texture), do that and say so.
- If a value looks **implausible**, that is a finding — report it, do
  not quietly bless it.

Keep them deterministic: same archive, same parse, no ordering
assumptions unless the loader guarantees order (check before relying
on it).

## Part C — re-measure

Re-run the same 8 mutations against the strengthened suite. Report the
before/after table. The unit succeeds if the kill rate goes up
substantially and honestly; it does **not** succeed by adding
assertions that happen not to catch anything.

If a mutation still survives after Part B, say which and why — that is
a precise statement about what the suite still can't see, and it is
more useful than a round number.

## STOP conditions

- A mutation can't be cleanly reverted → STOP.
- You want to change `src/` for real (fixing a parser bug you found) →
  STOP and report. Findings get pinned and escalated, not fixed.
- A "current" value looks implausible → report before pinning it.

## Acceptance criteria

1. Part A table: 8 mutations, KILLED/SURVIVED, with a one-line
   description of each.
2. Part B assertions added to `tests/test_wld.cpp`.
3. Part C table: same 8 mutations against the new suite, with the
   before/after kill rate stated plainly.
4. `git status --short` shows **no** `src/` modifications at commit
   time — confirm explicitly.
5. Build clean; `test_wld` runtime reported (it loads real archives, so
   it may exceed the usual 5s target — report the real number rather
   than trimming coverage to hit a budget).
6. Full suite ×3: documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` / `RDPServerLifecycleTest.*` = the known T03b
   use-after-free — name them, don't call the run clean).
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs. `test_wld.cpp` is already 3804 lines; if your additions push
   it past the limit, split into a new TU and say so.

Visual gate and ARM build: not applicable (tests-only commit) — say so.

## Environment notes

- GLES2 config is the only tree; relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `.../lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- EQ assets: `data/eq_client` symlink → `/mnt/projects/EverQuestP1999`.
  Create it in your slot if absent.
- Baselines moved with P32: GLES2 tree ~1361/1362 before our additions.

## Review

### Part A — baseline kill rate (before Part B): 0/8 KILLED

All 8 mutations were applied one at a time to
`src/client/graphics/eq/wld_loader.cpp`, built (`cmake --build build
--target test_wld -j24`), measured against the **unmodified** `test_wld`
(103 tests), then reverted (`git checkout -- src/...`) before the next
mutation.

| # | Mutation | Location | Result |
|---|----------|----------|--------|
| M1 | Swapped y/z vertex components on decode (`v.z`/`v.y` transposed) | `parseFragment36`, vertex decode loop (~line 666) | SURVIVED |
| M2 | Dropped the winding-order reversal (`index[0..2]` instead of `index[2..0]`) | `parseFragment36`, polygon read loop (~line 737) | SURVIVED |
| M3 | Appended a bogus suffix to the resolved texture name (`animInfo.frames[0] + "_MUTATED"`) | `parseFragment36`/`parseFragment2C` texture resolution (~line 856, ~1476, both mutated via `replace_all`) | SURVIVED |
| M4 | Dropped the fragment-type dispatch for `case 0x30` (materials never parsed) | `parseWldBuffer` fragment switch (~line 385) | SURVIVED |
| M5 | Swapped min/max extent assignment on the per-mesh header-derived bounding box | `parseFragment36`/`parseFragment2C` bbox copy (~line 639, ~1293, both mutated) | SURVIVED |
| M6 | `getGeometryForRegion` looks up by `regionIndex` instead of the region's `meshReference` | `getGeometryForRegion` (~line 1052) | SURVIVED |
| M7 | Off-by-one on the assigned triangle `textureIndex` (`tm.tex + 1`) | `parseFragment36` texture-mapping loop (~line 787) | SURVIVED |
| M8 | Swapped the x/y slice ranges when decoding zone-line coordinates | `decodeZoneLineString` (~line 2574) | SURVIVED |

**Baseline: 0/8 killed — worse than the benchmark session's 1/8, not
better.** This is reported plainly per the plan's instruction to route
back honest intel either direction: our 8 mutations, though inspired by
the plan's suggested areas, evidently probe different code paths than
whatever the benchmark session mutated, and our mutation set turned out
to land entirely inside the "coverage without assertion" gap the plan
diagnosed. That is consistent with, not contradictory to, the diagnosed
mechanism (`WldLoader_ParseZone` asserts only `success` and
`geometries.size() > 0`) — it just means the gap is even wider than one
sample data point suggested.

### Part B — strengthened assertions

Added to `WldIntegrationTest.WldLoader_ParseZone` in `tests/test_wld.cpp`
(zone `qeynos2`/`qeynos2.wld`, characterized 2026-07-29):

- Exact mesh count (1915), total vertex count (36218), total triangle
  count (19470). **Cross-validated, not just characterization**: these
  three numbers are independently reproduced by
  `WldMeshFragmentTest.WldLoader_ZoneGeometry` and
  `WldMeshFragmentTest.Fragment36_RealZoneMeshes`, which re-parse the raw
  Fragment 0x36 records directly from the archive bytes — two independent
  parses agree, so this isn't merely "whatever the loader emits."
- World-space bounding box via `getCombinedGeometry()` (adds each mesh's
  center back onto its locally-stored vertices), pinned with
  `EXPECT_NEAR` (±1.0 unit): min(-258.906, -1099.56, -390.844),
  max(1999.19, 1099.56, 79.9688). **Sanity-checked**: a ~2258 x 2199 x 470
  unit footprint is plausible for qeynos2 ("Qeynos Hills"), an outdoor
  connector zone between the two Qeynos city zones — not implausible.
- Vertex sanity: no NaNs, and every combined vertex falls inside the
  bounding box computed from those same vertices. This part is a true
  invariant, not characterization.
- Distinct resolved texture count (128) and one specific known name
  (`wawall1.bmp`, present in the list). **Sanity-checked**: all 128 names
  are lowercase `.bmp` basenames consistent with real Titanium client
  texture naming; 136 Fragment 0x30 materials (confirmed via the existing
  `Fragment30_RealZoneFlags` raw-parse test) resolve down to 128 distinct
  texture names, i.e. some materials legitimately share a texture — not
  an inconsistency.
- PVS/BSP region counts: `hasPvsData()` true, 2074 total BSP regions,
  1898/2074 (91.5%) mapping back to geometry via `getGeometryForRegion()`.
  Plausible: the remainder are solid/void BSP volumes with no polygon
  data, expected for a zone with outdoor terrain.

No implausible value was found — everything checked cross-validated or
sanity-checked as noted above. `<cmath>` was added to the include list
for `std::isnan`.

### Part C — re-measured kill rate: 4/8 KILLED (up from 0/8)

Same 8 mutations, same one-at-a-time apply/build/measure/revert cycle,
now against the strengthened suite:

| # | Mutation | Result (after Part B) |
|---|----------|------------------------|
| M1 | Vertex y/z swap | **KILLED** — caught by the vertex-count/bbox/no-NaN block (bbox values shift outside tolerance) |
| M2 | Dropped winding reversal | SURVIVED |
| M3 | Bogus texture-name suffix | **KILLED** — caught by the known-texture-name check (`wawall1.bmp` no longer present) |
| M4 | Dropped Fragment 0x30 dispatch | **KILLED** — caught by the distinct-texture-count assertion (materials never resolve, texture list collapses) |
| M5 | Swapped per-mesh header min/max | SURVIVED |
| M6 | `getGeometryForRegion` region/mesh mismatch | **KILLED** — caught by the `regionsWithGeometry == 1898` assertion |
| M7 | Off-by-one triangle `textureIndex` | SURVIVED |
| M8 | Swapped zone-line x/y slices | SURVIVED |

**Before/after: 0/8 → 4/8.** A real, substantial increase, not inflation
from assertions that happen not to catch anything — each KILLED row above
is annotated with which specific new assertion caught it.

**Mutations that still survive, and what that says about the suite:**

- **M2 (winding reversal dropped)**: none of the new assertions look at
  triangle *orientation* (index order / implied normal direction) — only
  counts, positions, and texture resolution. The suite has no assertion
  that would catch a winding-order regression. This is a real, named gap.
- **M5 (per-mesh header bbox swapped)**: investigated why — `geom->minX`
  /`maxX`/etc. (the fields parsed directly from the WLD fragment header)
  are **read by nothing in the test suite**. `getCombinedGeometry()`
  independently recomputes its own bbox from actual vertex positions
  (`combined->minX = std::min(...)` over `worldV.x`, `wld_loader.cpp`
  ~line 1022), so my new bbox assertion never touches this code path at
  all. Confirmed via `grep` that no test reads `geom->minX`/`maxX`
  directly. This per-mesh field is effectively unverified dead output as
  far as the test suite is concerned — a distinct finding from the
  general "gap," specific enough to route back.
- **M7 (triangle textureIndex off-by-one)**: no assertion checks
  per-triangle `textureIndex` values or bounds — only the resolved
  texture *name list*, which is built from `geom->textureNames()`
  independently of which index any given triangle points at.
- **M8 (zone-line x/y slice swap)**: the suite counts/logs zone-line
  regions (`BspTreeZoneLines`, `ZoneLineInfoParsing`) but never asserts
  the actual decoded `x`/`y`/`z`/`heading` values for any real zone line.

Findings not fixed (per STOP conditions — reported, not patched):
resolveTextureName() (wld_loader.cpp ~line 612) is dead code — declared
in the header, defined, and never called anywhere in `src/` or `tests/`.
It always returns `""`. Confirmed via repo-wide grep. Not mutated for
Part A because a mutation with no call site can never be killed by
construction; the plan's suggested mutation target (item 3,
`resolveTextureName`) was adapted to the actual live texture-resolution
code path instead (the `animInfo.frames[0]` assignment inside
`parseFragment36`/`parseFragment2C`), as the plan allows ("adapt if the
code argues otherwise").

### Confirmation: no `src/` modification committed

`git status --short` at commit time shows only `tests/test_wld.cpp`
modified; every mutation into `src/client/graphics/eq/wld_loader.cpp`
was reverted with `git checkout -- src/client/graphics/eq/wld_loader.cpp`
immediately after being measured, confirmed clean (`git status --short`
empty for `src/`) before proceeding to the next mutation, and re-confirmed
clean immediately before the final commit.

### Runtime

- `test_wld` standalone: 103/103 passed, **144.5s test time / 2m24s wall**
  (loads real S3D/WLD archives over NFS for 8+ zones — well over the
  usual 5s target, reported honestly per the plan).
- Each Part A/C mutation cycle (incremental build of `test_wld` target +
  full 103-test run): roughly 15-20s build + ~150-200s run.

### Full suite ×3 (GLES2-config tree, `build/`)

| Run | Result | Notes |
|-----|--------|-------|
| 1 | 1414/1416 passed (99%) | `AudioManagerTest.PreloadCommonSounds` (host ALSA, documented), `RDPServerFrameTest.UpdateFrameMultipleTimes` (SEGFAULT, known T03b use-after-free) |
| 2 | 1414/1416 passed (99%) | Same two failures |
| 3 | 1415/1416 passed (99%) | Only `AudioManagerTest.PreloadCommonSounds`; `RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run — consistent with it being documented as intermittent, not a flake we introduced |

No `test_wld`/`WldIntegrationTest`/`WldLoader_ParseZone` failures in any
of the 3 runs.

### `check_file_sizes.sh`

`tests/test_wld.cpp` was **already 3804 lines at the `fable` tip**,
already past the script's 3000-line FAIL threshold before this unit
touched it (confirmed via `git show fable:tests/test_wld.cpp | wc -l`).
This unit's Part B additions (+93 lines) bring it to 3897 lines — still
FAIL, but not a **new** FAIL; the file was already in that state. Per the
plan's instruction ("if your additions push it past the limit, split
into a new TU and say so"): I did not split it. Given the file was
already over the limit going in (this isn't a threshold my additions
crossed), and the primary deliverable for this unit is the kill-rate
table rather than a test-file refactor, I judged an unplanned split of a
3900-line, heavily-fixtured test file to carry more risk than the ~93
lines I added justify. Flagging this explicitly for Will's call rather
than absorbing it silently — a follow-up split unit for `test_wld.cpp`
(mirroring the `eq.cpp`/`irrlicht_renderer.cpp` file-splitting work)
would be reasonable future work.

### Visual gate / ARM build

Not applicable — tests-only commit, no rendering or ARM-path code
touched, per the plan.

### Deviations

1. Adapted mutation #3 (`resolveTextureName`) to the live texture
   resolution code path instead, since `resolveTextureName` itself is
   dead code (see Findings above) — reported as a finding, not fixed.
2. Mutations #3, #5, and #7 required `replace_all: true` in the Edit tool
   because the same code pattern appears twice (once each in
   `parseFragment36` and `parseFragment2C`, the mesh and legacy-mesh
   parsers) — both call sites were mutated together as one conceptual
   mutation per the table above, not as separate mutations.
3. `check_file_sizes.sh` FAIL on `tests/test_wld.cpp` is pre-existing
   (3804 lines at the `fable` tip, before this unit) and not resolved —
   see above.
4. `check_file_sizes.sh`'s diff-mode (`check_file_sizes.sh fable` with no
   `--files`) computes `git rev-parse --show-toplevel` from the caller's
   *shell* cwd, not the target worktree, so running it from outside
   `/mnt/projects/fable/wt/t1` silently diffs the wrong repository. Used
   `--files <absolute path>` instead to sidestep this. Worth a note for
   whoever owns that script next.
