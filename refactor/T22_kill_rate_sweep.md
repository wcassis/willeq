# T22 — Mutation kill-rate sweep across the "well-covered" parsers

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY at commit time.** Mutations are temporary
edits to `src/` that you MUST revert; `git status --short src/ include/`
must be empty when you commit. If a fix looks warranted, STOP and
report — this unit strengthens assertions, it does not fix source.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why this unit

Closes the open half of the benchmark session's mutation-testing intel
(`refactor/deferred.md`, "→ FOR THE TESTING SESSION"). Their finding:
`wld_loader.cpp` had the most covered lines in the repo (952) yet killed
only 1 of 8 injected faults. **T10 confirmed it independently** —
measured 0/8, strengthened to 4/8 — which established two things this
unit builds on:

1. Line coverage is a misleading prioritization signal; kill rate is
   the real one.
2. The technique is cheap: mutate, build ONE test target, run, revert.
   Minutes per mutation.

What T10 did for WLD, this does for the other parsers that *look*
covered. A file with a passing test suite that survives an inverted
comparison is a file whose tests execute it without checking it.

## Targets (in priority order)

Each already has a test binary, so "covered" is the claim under test:

| Source | Lines | Test binary |
|---|---:|---|
| `src/client/graphics/eq/s3d_loader.cpp` | 1322 | `test_asset_loading` / `test_pfs_archive` |
| `src/client/graphics/eq/pfs.cpp` | 304 | `test_pfs_archive` |
| `src/client/audio/xmi_decoder.cpp` | 483 | `test_xmi_decoder` |
| `src/client/string_database.cpp` | 353 | `test_string_database` |
| `src/client/graphics/eqe1_loader.cpp` | 254 | `test_eqe1_loader` |
| `src/client/spell/spell_database.cpp` | 151 | `test_spell_database` |

**Deliberately excluded:** `hc_map.cpp` and `zone_lines.cpp` (T19 just
added assertions + did their own spot-check), `wld_*` (T10/T11 own it),
and anything under `src/client/action/` or the eq domain TUs (T12–T18
covered them recently).

## Method (follow T10's, it is proven)

For each target, inject **6–8 mutations** spread across distinct
behaviors — not eight variations of one line. Good mutation classes for
parsers:

- flip a bounds/size comparison (`<` ↔ `<=`, `>` ↔ `>=`)
- off-by-one an offset or stride
- swap two adjacent field reads
- invert a branch condition (endianness/version/flag checks)
- drop an accumulation (`+=` → `=`)
- return a default instead of a parsed value

For each: apply, build ONLY that target, run ONLY that binary, record
**killed** (some test fails) or **survived**, revert, confirm the revert.
Batch nothing — one mutation live at a time, so an unreverted edit can
never be mistaken for a passing baseline.

**Then strengthen.** For every survivor, add or tighten assertions until
it is killed, and re-run the mutation to prove the kill. Report the
before/after rate per file. Strengthening is where the unit's value is;
the measurement alone is only a report.

## What "good" looks like

T10's shape: baseline 0/8 → 4/8, plus two dead-code findings that fell
out of reading the parser closely enough to mutate it well. A file that
starts at 6/8 needs no work — say so and move on. **Do not manufacture
assertions to inflate a number**; a survivor that represents genuinely
unobservable behavior (a debug log, a field nothing reads) should be
reported as such, and is itself a finding worth writing down.

## STOP conditions

- Any `src/`/`include/` change surviving at commit time → STOP.
- A real bug found while mutating (the parser is already wrong, or a
  mutation reveals a latent crash) → pin with a clearly-named test,
  report prominently, do NOT fix.
- A target that cannot be driven without live EQ assets AND has no
  fixture → skip it, say so, do not invent a fixture under `data/`.

## Fixtures

Real EQ assets resolve via `EQ_CLIENT_PATH` or the `data/eq_client`
symlink (check `ls -la data/` in the slot; recreate as
`ln -s /mnt/projects/EverQuestP1999 <slot>/data/eq_client` if missing).
Captures: `data/captures` → `/mnt/projects/fable/captures`. Both are
READ-ONLY.

## Verification bar (tests-only unit, x86)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exception `AudioManagerTest.PreloadCommonSounds` (no ALSA).
   Also expect the `RDPServerFrameTest`/`RDPServerLifecycleTest` flake
   family under parallel load — root-caused in T03b, perf territory, NOT
   yours; rerun isolated to confirm before reporting it as a failure.
   Baseline is currently 1617/1618.
3. `git status --short src/ include/` — MUST be empty.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

(Executor fills: per-file before/after kill rate with the mutation list
and what each one did, assertions added, survivors deliberately left
with justification, bugs pinned, deviations.)
