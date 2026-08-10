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

(executor fills: the `-d` vs `--log-level` precedence decision and its
rationale; the log-level run transcripts; where the shared version
constant landed and why; the Part 3 re-verification evidence; deviations)
