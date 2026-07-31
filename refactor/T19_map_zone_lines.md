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

(Executor fills: header-layout findings, pinned values + how discovered,
malformed-input matrix, mutation results, deviations.)
