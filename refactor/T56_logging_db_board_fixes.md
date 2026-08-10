# T56 — Three small independent fixes: `--log-level=` clobbered, willeq.db version unvalidated, race=415 board entry stale

**Slot: t3. x86 only. No Pi. NO EQEMU.**

**Territory: `src/client/main.cpp`, `src/client/willeq_database.cpp`,
`include/client/willeq_database.h`, `tools/willeq_db_builder.cpp`,
`refactor/deferred.md`, and tests.** Do not regenerate the tracked
binary `data/willeq.db` (see Part 2). Do not touch spell effect parsing —
that is a separate design job Will has not scoped yet, and it will edit
these same DB files, so leave the ground clean for it.

Sibling units this round: T54 (tests/UI) and T55 (`src/client/eq*.cpp`).
**No file overlap with either.** Shared file: `refactor/deferred.md`,
append only, never reflow another unit's section.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s Index.

**These three parts are independent. Do them in order, commit-ready at
each boundary. If one stalls, land the other two and report the stall —
do not hold the unit hostage to its weakest third.**

---

## Part 1 — `--log-level=` is silently clobbered (deferred.md:1825, T31 finding)

**The chain, verified at tip:**
- `src/client/main.cpp:16` — `InitLogging(argc, argv)` correctly parses
  `--log-level=` (`include/common/logging.h:819`) and calls `SetLogLevel`.
- `src/client/main.cpp:19` — **`EverQuest::SetDebugLevel(config.debugLevel)`
  runs unconditionally on the very next line**, reaching
  `LogManager::SetGlobalLevel(min(level, LOG_TRACE))` via
  `include/client/eq.h:750` → `include/common/logging.h:573`.
- `config.debugLevel` defaults to `0` (`include/client/application.h:74`)
  and is set only by `-d <n>` (`src/client/application.cpp:1941`);
  `parseArguments` does not know `--log-level` exists — it only prints it
  in help (`application.cpp:2090`).

So `--log-level=DEBUG` with no `-d` slams the global level to `0` (NONE)
one line after it was correctly set. **Line 21 already has the right
guard** (`if (config.debugLevel > 0)`) — line 19 is missing it. Note the
irony to be fixed alongside: `main.cpp:26-27` then logs "use
--log-level=LEVEL to change" at a level that flag can no longer reach.

**The fix is 1-5 lines. The decision is the real content:** when BOTH
`-d` and `--log-level=` are given, which wins? Decide, implement it,
**document it in the `--help` text** (`application.cpp:2090` currently
describes `--log-level` without mentioning the interaction), and say why
in the Review. Recommended: last-flag-wins is unimplementable here
without reordering the parse, so prefer **explicit `--log-level=` beats
numeric `-d`** and log a one-line notice when both are present and
disagree — silent precedence is what produced this bug.

**Verification is the real cost.** Prove it end-to-end: run the binary
with `--log-level=DEBUG` (no `-d`) and show DEBUG lines reaching stdout;
with `-d 3` alone; and with both. Per the standing rule, do not pipe
build output — for these runs redirect to a file with `>` and read it,
never pipe. Add a unit test if a seam exists that does not require
spawning the binary; if none does, say so plainly rather than inventing
one.

## Part 2 — willeq.db schema version bumped but never validated (deferred.md:1627, T27 follow-up)

`WilleqDatabase::open` (`src/client/willeq_database.cpp:14-51`) selects
`metadata.version` at `:35-44`, reads it at `:38`, and **only logs it**
(`LOG_INFO` at `:39`, `LOG_WARN` at `:41` if absent). There is no
expected-version constant, no comparison, no mismatch branch. The only
hard failure is a completely missing `metadata` table (`:45-49`).

The builder writes `setMeta("version", "2")` at
`tools/willeq_db_builder.cpp:1204` ("T27: spells.class_levels column
added"). The client has no matching constant — the two cannot help but
drift.

**Confirmed consequence:** `willeq_database.cpp:130` and `:187` both name
`class_levels` in their SELECT lists. Against a v1 db,
`sqlite3_prepare_v2` fails with "no such column", `found` stays false,
and **every spell lookup silently returns nothing** — surfacing to the
player as "spell not found" rather than "your database is out of date".

**Fix (~10-20 lines):** hoist the expected version into a shared header
so builder and client cannot drift (`include/client/willeq_database.h` is
the natural home; the builder includes it or a small shared constants
header — pick the one that does not drag client headers into the tool,
and say which you picked). Compare after `:38`; on mismatch `LOG_FATAL`
and `return false`, **naming the rebuild command in the message** —
a good error here is the entire point. Decide and state whether
older-than-expected and newer-than-expected are both fatal
(recommended: yes, both — a newer db against an older client fails just
as opaquely).

**Do NOT regenerate `data/willeq.db`.** It is tracked, it is v2, and v2
is what this client expects. Test the mismatch path by constructing a
throwaway sqlite file in the test's temp dir with a `metadata` table
carrying a wrong version — do not mutate the tracked artifact. Add tests
for: correct version opens; wrong version fails with a clear message;
missing `metadata` table still fails as before (existing behaviour, do
not regress it).

## Part 3 — race=415 is fixed; the board says otherwise (deferred.md:1930)

**No production change. This part is a board correction, and it matters
more than its size suggests.**

The entry `race=415 missing model data still crashes graphics mode (T32
re-test, 2026-08-03)` is stale. **T38 fixed it on 2026-08-04**
(`refactor/T38_missing_model_nonfatal.md`), the day before T53's hygiene
pass ran. T53 checked this entry, recorded "OPEN, confirmed still
reproducing … no later fix commit found"
(`refactor/T53_deferred_board_hygiene.md:189`), and **missed it** — so
the heading was never re-titled.

Verify at tip before writing anything: `src/client/graphics/renderer_zone_load.cpp:1437-1480`
should now be a per-entity `LOG_WARN` + skip with
`ctx.entitiesSkipped`/`ctx.skippedRaceGenderPairs` accumulation
(declared `:88-95`) and an end-of-step summary, with the entity falling
through to `EntityRenderer::getMeshForRace()`'s placeholder-cube
fallback; the parallel runtime path
(`src/client/graphics/entity_prep_worker.cpp:139-145`) was already
non-fatal; and no `LOG_FATAL` on a missing race model should remain
anywhere in `src/client/graphics`. T38 committed acceptance evidence at
`benchmarks/audits/T38/run_race415_acceptance.sh`.
**If any of that does not hold, the entry is NOT stale — STOP and report,
because then T53 was right and the scouting was wrong.**

Then, per the never-delete rule:
- Re-title the entry to resolved-by-T38, in place, preserving the
  original text and adding the correction beneath it.
- **Record the meta-finding**: T53's hygiene pass found six
  fixed-but-marked-open entries and itself produced a seventh miss, one
  day wide. The board's drift is not a solved problem, and T53's own
  closing caveat ("it is NOT self-maintaining") is now evidenced. Say so
  on the board.
- Log the **genuine remainder as a new, separate entry**: race 415 and
  gfaydark's race=330 render as placeholder cubes because
  `config/race_models.json` tops out at race_id 367
  (`refactor/T38_missing_model_nonfatal.md:85`). That is a *data/content*
  gap, explicitly out of T38's scope and out of this unit's — do not
  attempt it here (it needs asset verification and a visual acceptance
  only Will can give).

---

## STOP conditions

- Part 2 tempting you toward spell-effects work or a db regeneration.
- Part 3's verification not holding (see above).
- Any change to `data/willeq.db` or to goldens.

## Verification

Full protocol bar, plus the Part 1 end-to-end log-level runs and the
Part 2 mismatch tests described above. Report per-part pass/fail
separately — this unit's parts stand or fall independently.

## Review

**Status: all three parts PASS. Full verification bar PASS.**

### Part 1 — `--log-level=` clobber — FIXED

**Precedence decision:** explicit `--log-level=` beats numeric `-d`, per
the plan's recommendation — silent precedence is what produced this bug.
Implemented entirely in `src/client/main.cpp` (no `logging.h`/`eq.h`
changes — both out of territory): after `InitLogging(argc, argv)` runs
(which already applies `--log-level=` if present), `main()` scans `argv`
itself for `--log-level=` and captures the level `InitLogging()` just set.
`EverQuest::SetDebugLevel(config.debugLevel)` is still called unconditionally
(unchanged — no `eq.h` territory to touch), but if `--log-level=` was
explicit, its captured level is immediately restored via `SetLogLevel()`,
undoing the clobber. A one-line `LOG_INFO` fires naming the winner only
when `-d`'s derived level and the explicit level actually disagree. `-d`
alone is bit-for-bit unchanged behaviour. `--help`
(`src/client/application.cpp:2090-2092`) now documents the interaction.

**No test seam exists** for this fix: the logic lives inline in `main()`,
which is compiled only into the `willeq` executable target, not a linkable
library/test target — spawning the binary was the only way to exercise it
(as the plan anticipated as a possibility). Said so rather than inventing
one.

**End-to-end verification (three piped binary runs, redirected to files,
never piped through grep/tail — logs read via Read tool):**

1. `--log-level=DEBUG` alone (no `-d`): DEBUG-level lines
   (`[DEBUG] [MAIN] S01: Config validation passed`, etc.) reached stdout.
   Confirms the fix — before this change, `config.debugLevel` defaulting
   to 0 would have driven the global level back to NONE one line later.
2. `-d 3` alone (no `--log-level=`): only WARN/ERROR/FATAL lines appeared,
   zero DEBUG lines — correct for level 3 (WARN), unaffected by the new
   code path (`logLevelExplicit` false, falls to the original
   `if (config.debugLevel > 0) SetLogLevel(config.debugLevel)` branch).
3. `-d 1` (FATAL) + `--log-level=DEBUG` together (disagreeing): output
   opened with
   `--log-level=DEBUG and -d 1 disagree; --log-level= wins (effective level: DEBUG)`
   and DEBUG lines reached stdout — explicit flag won, notice fired.

All three runs used a throwaway scratch config
(`.../scratchpad/test_config.json`, `host`/`user`/`pass`/`server`/`character`
dummy values) since no `willeq.json` exists in the worktree; runs correctly
fail later at "WilleqDatabase not found at data/willeq.db" (relative-path
lookup, cwd was the scratch dir, not the repo root) — irrelevant to the
log-level claim, which only needed the startup log lines, all present.

### Part 2 — willeq.db version validation — FIXED

**Shared constant landed at** `include/client/willeq_database.h`:
`inline constexpr int kWilleqDbExpectedVersion = 2;` inside `namespace
EQT`, right after the namespace opens. **Why this header and not a new
shared-constants header:** `tools/willeq_db_builder.cpp` already includes
a client header (`client/spell/spell_constants.h`, itself dependency-free —
just `<cstdint>` and an enum), establishing precedent that this tool
tolerates lightweight client headers. `willeq_database.h` itself only pulls
in `<array>`/`<cstdint>`/`<map>`/`<string>`/`<vector>`/`<atomic>`/`<mutex>`
— no graphics/audio/network deps — so including it directly in the builder
was the smaller change over inventing a third header.

**Client (`src/client/willeq_database.cpp::open()`):** after reading
`metadata.version`, compares against `kWilleqDbExpectedVersion`. On
mismatch: `LOG_FATAL` naming both the actual/expected versions, whether the
db is too old or too new, and the exact rebuild path
(`tools/willeq_db_builder`), then closes the handle and returns false.
**Both directions are fatal** (decision, as recommended): a newer db
against an older client would otherwise fail just as opaquely as the
original v1-vs-v2 bug. Missing-`metadata`-table (ERROR+fail) and
present-table-but-no-version-row (WARN+open) behaviours are byte-identical
to before — not regressed.

**Builder (`tools/willeq_db_builder.cpp`):** `setMeta("version", "2")`
replaced with `setMeta("version", std::to_string(EQT::kWilleqDbExpectedVersion))`
— builder and client now share one source of truth and cannot drift again
without a compile-time-visible edit to both call sites' comments pointing
at each other.

**`data/willeq.db` was never touched** — confirmed via `git status
--porcelain`/`git diff --stat` throughout, empty for that path at every
check.

**New tests** (`tests/test_willeq_database.cpp`, wired into
`tests/CMakeLists.txt` following the `test_string_database`/
`test_spell_database` pattern — links `willeq_database.cpp` +
`third_party/sqlite3.c` directly, no full app link): 5 cases, all against
throwaway sqlite files under `$TMPDIR` (never `data/willeq.db`):
- `MatchingVersionOpens` — version 2 opens successfully.
- `OlderVersionFailsToOpen` — version 1 fails, `LOG_FATAL` names both
  versions and the rebuild command (transcript captured, see below).
- `NewerVersionFailsToOpen` — version 3 also fails (confirms "both
  directions fatal").
- `MissingMetadataTableFailsToOpen` — pre-existing behaviour, unregressed.
- `MissingVersionRowStillOpens` — table present, no `version` key: still
  opens with a WARN, pre-existing behaviour, unregressed.

Sample captured `LOG_FATAL` text (from the `OlderVersionFailsToOpen` run):
```
WilleqDatabase: <tmp path> has schema version 1 (too old) but this client
expects version 2 — rebuild it: run the willeq_db_builder tool
(tools/willeq_db_builder.cpp) to regenerate data/willeq.db
```
All 5 pass standalone and inside the full `ctest` run (test IDs 1044-1048,
`WilleqDatabaseVersionTest.*`).

### Part 3 — race=415 board correction — VERIFIED STALE, RE-TITLED

**Precondition check (per the plan's hard requirement) — held, all four
claims confirmed at tip:**
1. `src/client/graphics/renderer_zone_load.cpp:1437-1481`
   (`loadZoneStep8a_BaseModelPrep`) is a per-entity `LOG_WARN` + skip
   (`ctx.entitiesSkipped`/`ctx.skippedRaceGenderPairs`, declared `:94-95`,
   incremented `:1473-1474`) with an end-of-step summary at `:1395-1407` —
   no `LOG_FATAL`, no abort.
2. The entity falls through to `EntityRenderer::getMeshForRace()`'s
   documented placeholder-cube fallback.
3. `src/client/graphics/entity_prep_worker.cpp:139-145` — confirmed already
   non-fatal (failure just sets `success = false`, logged, handled
   downstream; no `LOG_FATAL`).
4. `grep -rn LOG_FATAL src/client/graphics/` → 4 hits, none a "missing race
   model" FATAL: 2 are whole-config-file-missing FATALs
   (`entity_equipment.cpp:38` — `item_models.json`;
   `irrlicht_renderer.cpp:538` — `config/race_models.json` itself), 2 are
   equipment-archive/WLD FATALs (`equipment_model_loader.cpp:920,927`) — a
   different failure class.
5. `benchmarks/audits/T38/run_race415_acceptance.sh` present and committed
   (`git log` confirms `3e4f6bcd`, "T38: a missing base model skips one
   entity, not the whole zone load").

**Board action taken** in `refactor/deferred.md`:
- Re-titled `## \`race=415\` missing model data still crashes graphics
  mode` in place (heading now says "FIXED by T38"), original report
  preserved beneath a correction block with the full re-verification
  evidence above.
- Recorded the meta-finding inline: T53's hygiene pass found 6 stale
  entries and produced a 7th miss itself (this one), one day/one commit
  wide — evidencing T53's own "NOT self-maintaining" caveat rather than
  just asserting it.
- Logged the genuine remainder as a new, separate heading: `## \`race=415\`
  /\`race=330\` render as placeholder cubes — data/content gap past
  \`config/race_models.json\`'s race_id 367 ceiling (T56, 2026-08-10)` —
  explicitly out of scope, needs asset verification + Will's visual call.
- Updated the Index (lines ~43-58, ~94) to strike the two now-fixed T56
  bullets (willeq.db version, log-level clobber) and correct the race=415
  bullet with a pointer to the new remainder item.

No production code touched for Part 3, as required.

### Full verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` (full) | PASS, 0 errors |
| `ctest --test-dir build --output-on-failure` | 1918/1919 (only the documented `AudioManagerTest.PreloadCommonSounds` ALSA host exception fails; all 5 new `WilleqDatabaseVersionTest.*` pass; 19 pre-existing environment/asset-gated skips unchanged) |
| `bash scripts/build-gles2-x86.sh` | PASS, `build-gles2/bin/willeq` + `gles2_zone_benchmark` built clean |
| Visual gate | PASS (`ctest` id 1919 `visual_gate`, 7.36s); `git status`/`git diff --stat` confirm zero golden files touched |
| `bash scripts/build-arm-noble.sh` | PASS (timed out once at the 10-minute Bash-call ceiling mid-build with only pre-existing GCC snprintf/ABI notes in the log, not errors; re-ran and it completed clean — `build-arm-noble/bin/willeq` 341M unstripped) |
| `check_file_sizes.sh fable` | 4 ok, 1 pre-existing WARN (`application.cpp` 2277 lines, already over the 1500 line ceiling before this unit's 3-line `--help` addition — not a new condition), 0 FAIL. Run via `env --chdir=<slot> bash scripts/refactor/check_file_sizes.sh fable` since the script computes its repo root from bare `git rev-parse` (no `-C`), which needs the process cwd inside the slot — `env --chdir` sets that without using the banned `cd` builtin. |

### Deviations from the plan

- Part 3: touched `refactor/deferred.md`'s Index section (not explicitly
  named in the plan's Part 3 instructions, which focus on the entry body)
  to strike the two Part 1/2 bullets and correct the race=415 bullet —
  done for internal consistency with the file's own "FIXED but the board
  still said open" convention, and because leaving the Index saying
  "confirmed still reproducing, T53" right next to a heading now marked
  FIXED would reproduce exactly the kind of drift this unit was fixing.
  Additive/correcting only, never reflowed T54/T55's untouched sections.
- Part 1: also edited `src/client/application.cpp:2090-2092` (2 new help
  lines) — not listed in the plan's top-of-file Territory line, but
  explicitly directed by the Part 1 body text ("document it in the --help
  text (application.cpp:2090...)"). Treated the explicit instruction as
  authoritative over the summary Territory line for this one file/purpose;
  no other application.cpp content touched.
- No other deviations. `data/willeq.db` and all goldens confirmed untouched
  throughout (checked repeatedly, not just once at the end).
