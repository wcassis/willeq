# T19 — hc_map + zone_lines parsing tests

**Slot: t1 (dispatch after T17 merges). x86 only — no Pi, no eqemu, no
live server.** Sibling: T18 (zone entities/chat) ran in t2 and may
already be merged; its files (`tests/test_zone_*`,
`tests/zone_stream_test_utils.h`) are not yours to touch.

**Territory: `tests/` ONLY.** No `src/`, no `include/`. If a source
change looks necessary, STOP and report.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why this unit

T01 backlog item 6: `src/client/hc_map.cpp` (949 lines, 4.1% — the only
coverage is incidental) and `src/client/zone_lines.cpp` (578 lines, 0%).
Both are file-format parsers — exactly the code where malformed input
bites and where fixtures make strong tests cheap.

## Fixtures — verified to exist, use them read-only

- **Binary zone maps (V1/V2 HCMap format)**:
  `/mnt/projects/akk-stack/server/maps/base/*.map` (e.g. `befallen.map`,
  `qeynos2.map`). READ-ONLY: these belong to the eqemu server install;
  never write there, never copy them into the repo (gitignore risk +
  size) — reference them via an env var your tests read (follow the
  `WILLEQ_SESSION_LOG` pattern from `tests/test_login_world_flow.cpp`),
  with GTEST_SKIP when unset/absent.
- **Zone lines JSON**: `data/zone_lines/*.json` (checked in, per-zone)
  plus `data/zone_lines.json` and `data/zone_points.json` at repo root
  `data/`. These are repo files — usable directly.

## Study first

`include/client/hc_map.h` (HCMap API: load, raycast/LOS entry points,
V1/V2 distinction) and `src/client/hc_map.cpp` (format headers, version
switch); `src/client/zone_lines.cpp` (JSON schema it expects, error
paths). Report the actual V1/V2 header layout in your Review — the plan
deliberately doesn't restate it so the tests come from the code, not
from this file.

## Priorities

1. **HCMap happy path on real fixtures.** Load `befallen.map` and
   `qeynos2.map`; assert loaded-ness and a small set of discovery-pinned
   queries (e.g. FindBestZ / LOS between two known points — pin exact
   values from a discovery run, the T14 deliberate-fail-then-pin method).
2. **HCMap malformed input.** Truncated header, truncated body, wrong
   magic/version, zero-size file, oversized counts (exact-size synthetic
   buffers/files per the T08 lesson — heap slack hides over-reads).
   Write synthetic files to the test's temp dir (per-PID unique paths,
   T03b hygiene rule).
3. **zone_lines happy path.** Parse a real per-zone JSON; pin a known
   zone line's fields. Cross-check one entry against
   `data/zone_lines.json` if the schemas relate.
4. **zone_lines malformed input.** Missing keys, wrong types, empty
   file, non-JSON garbage — no crash, sane fallback.
5. **Mutation spot-check (T10 practice):** 2-3 mutations in a scratch
   copy (e.g. flip a bounds comparison in the map loader), confirm a
   test fails, revert; record results. `git status` must show no `src/`
   modification at commit time.

## STOP conditions

- Any change needed under `src/` or `include/` → STOP and report.
- Real bug found (crash, OOB, wrong parse) → pin with a clearly-named
  BUG_ test, report prominently, do NOT fix.
- If HCMap cannot be constructed/loaded standalone (hidden graphics or
  EverQuest dependency), STOP and report what the seam would need.

## File layout

- `tests/test_hc_map.cpp`, `tests/test_zone_lines.cpp` (each ≤800
  lines). Wire into `tests/CMakeLists.txt` linking only what each needs
  (check the include graph first; prefer per-TU source lists over
  willeq_core if the dependency set is small). No Irrlicht.

## Verification bar (narrowed per T12/T17 precedent — tests-only, x86)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exception `AudioManagerTest.PreloadCommonSounds` (no ALSA).
3. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
4. Visual gate / ARM build: not applicable.

Note: fable moves fast — merge fable into the task branch and re-verify
before finishing if it has advanced.

## Review

**Status: DONE.** `tests/test_hc_map.cpp` (559 lines) + `tests/test_zone_lines.cpp`
(383 lines), wired into `tests/CMakeLists.txt` as two new independent
targets (`test_hc_map`, `test_zone_lines`), each linking only
`hc_map.cpp`/`raycast_mesh.cpp`/`compression.cpp` (+ jsoncpp for
zone_lines) — no willeq_core, no Irrlicht. 41 new tests total (20 +
21), all passing. Full suite: 1614/1615 (only the documented
`AudioManagerTest.PreloadCommonSounds` ALSA-host exception fails, same
as baseline). `check_file_sizes.sh fable` reports `ok` for both new
files. No `src/`/`include/` changes in the final diff (two temporary
mutations were made and reverted — see Mutation results below;
`git status` confirms clean).

### Header layout (discovered from src/client/hc_map.cpp, not restated
### from this plan)

```
uint32_t version;              // 0x01000000 = V1, 0x02000000 = V2

V1 (uncompressed triangle soup, no bounds-checked reads beyond fread()'s
own return-value checks):
  uint32_t face_count;
  uint16_t node_count;         // read, unused
  uint32_t facelist_count;     // read, unused
  repeated face_count times: glm::vec3 a,b,c; float normals[4];
  (each field individually fread()'d with an explicit `!= 1` failure
  check -- truncation anywhere fails cleanly)

V2 (zlib-compressed; the interesting structure is inside the
decompressed payload):
  uint32_t data_size;           // compressed byte length on disk
  uint32_t buffer_size;         // exact decompressed byte length
  <data_size bytes of zlib-compressed data>
  -- after EQ::InflateData() into a `buffer_size`-byte vector<char> --
  uint32_t vert_count, ind_count, nc_vert_count, nc_ind_count,
           model_count, plac_count, plac_group_count, tile_count,
           quads_per_tile;
  float    units_per_vertex;
  ... vertices / indices / non-collidable geometry / models /
      placements / placement-groups / terrain tiles, ALL read via the
      unchecked `read_val<T>(char*&)` helper (memcpy + pointer advance)
      with NO comparison against `buffer.data() + buffer.size()` at any
      point. See BUG_ below -- this is a real, reproduced defect.
```

Both real fixtures (`befallen.map`, `qeynos2.map`) turned out to be V2,
despite befallen being a "classic" zone -- confirms the map format
version is independent of expansion era. A real V1 fixture does exist
(`sky.map`, one of only 6 V1 files out of 513 under `maps/base/`), used
for `HCMapRealFixtures.SkyV1_LoadsPinnedStats` so the V1 branch gets
real-fixture coverage too, not just synthetic.

### Pinned values (how discovered)

All pinned via a standalone discovery harness (`hc_map.cpp` +
`raycast_mesh.cpp` + `compression.cpp` linked into a throwaway `main()`,
compiled directly with g++, run against the real fixtures) before
writing any assertions — the T14 method. Headline numbers:

| Fixture | Format | Verts | Faces | Y-bounds (Irrlicht Y-up) |
|---|---|---|---|---|
| befallen.map | V2 | 69534 | 43018 | -90.622269 .. 26.143688 |
| qeynos2.map | V2 | 32276 | 24285 | -390.843750 .. 79.968750 |
| sky.map | V1 | 77496 | 25832 | -0.686529 .. 0.557747 |

`FindBestZ`/`CheckLOS`/`CheckLOSWithHit` pinned values in the test file
comments; notably `GetZRange()`'s `minZ`/`maxZ` output turned out to
literally alias the same `m_impl->minZ`/`maxZ` fields `GetVertexBounds()`
reports as `minY`/`maxY` (both track the Irrlicht Y-up vertical extent
despite the "Z" naming in `GetZRange` — not a bug, just a naming
artifact worth flagging since it's confusing on first read).

zone_lines pinned values came straight from the checked-in JSON's raw
floats (`data/zone_lines/qeynos2.json`, `data/zone_lines.json`), cross-
checked against a standalone `zone_lines.cpp` + `hc_map.cpp` discovery
harness the same way.

### Malformed-input matrix

**HCMap (13 synthetic cases, all handled gracefully by production code
except the one BUG_ below):** zero-size file, nonexistent file, wrong
magic/version, truncated after version only, V1 truncated mid-header,
V1 truncated mid-face-data, V1 zero-face-count (degenerate-but-legal),
V1 one-valid-face round-trip, V2 minimal-valid round-trip, V2 truncated
after sizes only, V2 truncated compressed body (data_size lies), V2
corrupted compressed data (not valid zlib), V2 zero buffer_size. All 13
return `false`/`IsLoaded()==false` cleanly — the V1 branch's
per-`fread()` return-value checks and the V2 branch's `decompressed_size
== 0` check both do their job.

**zone_lines (12 synthetic cases):** root-is-array, zone_lines-not-array,
missing trigger_box/destination (defaults to 0, no crash), unknown
target_zone (skipped with a warning), zone-name mismatch, missing bounds
sub-fields (silently defaults to 0 -- jsoncpp's `asFloat()` on a
*missing* key returns 0.0 rather than throwing; documented as a soft
data-quality gap, not a crash), empty file, non-JSON garbage, nonexistent
file, empty zone_lines array. All handled gracefully. Contrast with the
BUG_ tests below: a *present-but-wrong-typed* field is a different, much
worse failure mode than a *missing* field.

### Real bugs found and pinned (STOP condition — not fixed, out of `tests/`
### territory)

1. **`BUG_V2_OversizedVertCount_CrashesLoad`** (`test_hc_map.cpp`): the
   V2 decompressed-payload parser trusts `vert_count` (and by the same
   logic `ind_count`/`model_count`/`plac_count`/`plac_group_count`/
   `tile_count`) read from *inside* the untrusted decompressed payload,
   with no check against `buffer_size` (which only bounds the *total*
   decompressed size, not any individual count). A payload whose
   `vert_count` lies about how much vertex data follows makes
   `read_val<float>()` walk straight off the end of the
   `buffer_size`-byte heap-allocated `std::vector<char>`. Reproduced
   standalone outside gtest first: **SIGSEGV**, confirmed reliably. In
   the real test suite this is pinned with `ASSERT_DEATH` (forks a
   child, so the crash doesn't take down the whole binary/ctest run).
   Same root cause likely applies to the other five counts; only
   `vert_count` was exercised as the representative case.

2. **`BUG_SpecificFormat_NonNumericBoundsField_Throws` /
   `BUG_ExtractedFormat_NonNumericTriggerBoxField_Throws`**
   (`test_zone_lines.cpp`): both `loadFromZoneSpecificJson()` and
   `loadFromExtractedJson()` call `Json::Value::asFloat()` directly on
   `bounds`/`trigger_box`/`destination` sub-fields with no
   `isNumeric()`/`isConvertibleTo()` guard. jsoncpp throws
   `Json::LogicError` ("Value is not convertible to float.") when
   `asFloat()` hits a non-numeric JSON value (e.g. a string where a
   number is expected). Neither function — nor anything further up the
   real call chain (`ZoneLines::loadFromZone()` or its callers) — wraps
   this in a try/catch, so a single malformed field in an otherwise
   well-formed zone-lines JSON file crashes the whole process via
   `std::terminate`. Reproduced standalone outside gtest first:
   **SIGABRT** via uncaught `Json::LogicError`. Pinned in the real test
   suite with `ASSERT_THROW` rather than `ASSERT_DEATH` — googletest's
   death-test harness installs its own exception barrier around the
   forked child's statement (to produce a clean "threw an exception"
   diagnostic instead of a raw core dump), so the exception never
   reaches an actual signal inside the child and `ASSERT_DEATH`'s regex
   never gets a chance to match; confirmed this the hard way (ran it
   first, saw "Result: threw an exception" instead of a signal death,
   switched to `ASSERT_THROW`). `ASSERT_THROW` pins the identical defect
   (uncaught exception escapes the parser) without depending on that
   harness's signal-vs-exception distinction.

### Mutation results (T10 practice)

Two mutations made directly in `src/` (temporarily), rebuilt, confirmed
red, reverted, rebuilt again, confirmed green — `git status` clean
throughout except during the mutation window:

1. **`src/client/hc_map.cpp` V2 `GetZRange`/`GetVertexBounds` min/max
   comparisons** (lines ~702-703): flipped `if (v.y < minZ)` /
   `if (v.y > maxZ)` to `>` / `<`. Result: `test_hc_map` failed 1 test
   (`BefallenV2_LoadsAndReportsPinnedStats`, wrong `minY`/`maxY`
   values) — caught. Reverted via `git checkout -- src/client/hc_map.cpp`.

2. **`src/client/zone_lines.cpp` `checkExtractedZoneLines` trigger-box
   comparison** (line 502): flipped `x >= ezl.minX` to `x <= ezl.minX`.
   Result: `test_zone_lines` failed 3 tests
   (`Qeynos2_PreserveXLandingResolvesToCurrentPosition`,
   `Qeynos2_PositionOutsideAllBoxesReportsNoZoneLine`,
   `LegacyFormat_AkanonPinnedFields`) — caught. Reverted via
   `git checkout -- src/client/zone_lines.cpp`.

### Deviations from the plan

- **`LoadFromZone_EqClientPathRelativeCandidate` test-authoring bug,
  found and fixed mid-unit (not a production bug):** an earlier version
  of this test pointed `eqClientPath` at a nonexistent directory
  (`<repo>/fake_eq_client_subdir`) to exercise `loadFromZone()`'s
  `eqClientPath + "/../data/zone_lines/<zone>.json"` candidate.
  `fopen()`/the OS resolve `..` *through the real filesystem* — the
  intermediate component must actually exist as a directory, or the
  open fails with ENOENT regardless of what the fully-resolved target
  path would lexically be. This passed when run manually from the repo
  root (because the CWD-relative 1st candidate silently masked the
  bug) but failed under the real `ctest` run (whose working directory
  is not the repo root) — caught by the full-suite verification step,
  not by manual spot-checking. Fixed by pointing `eqClientPath` at
  `<repo>/tests` (always exists) instead; re-verified green from three
  different CWDs (`build/tests`, `/tmp`, repo root) plus the full
  `ctest` run.
- Otherwise no deviations: both real fixtures happened to be V2 (plan
  didn't specify format), so a V1 fixture (`sky.map`) was located and
  added for real-fixture V1 coverage, matching the plan's spirit
  ("actual header layout... from the code") more completely than the
  two named fixtures alone would have.
