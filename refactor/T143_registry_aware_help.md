# T143 — Make `/help` registry-aware

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `/help`'s handler (`src/client/eq_commands_movement.cpp:135`),
`src/client/application.cpp`, `src/client/action/`,
`include/client/action/`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T143/`.**

**Will has made this decision — make `/help` registry-aware.** This plan
implements it.

Read T97's Review (which first documented the blindness) and T138's
`needs-decision` row for `help`.

## Why this is now urgent, and partly our own doing

`/help` introspects **only the game registry's** catalog. T97 documented that
neither registry's help can see the other's — but the migration programme
changed the scale dramatically:

**`/help` now describes 11 commands out of 122.** It sees **9%** of the
client's vocabulary. That is a user-facing regression caused by T139/T140, not
a pre-existing quirk, and it is why "leave it and document the gap" is no
longer the reasonable option it was at 61/95.

## The good news — this is mechanical, not architectural

Both registries **already expose the identical introspection API**:

| | game registry | action registry |
|---|---|---|
| header | `include/client/graphics/ui/command_registry.h` | `include/client/action/command_processor.h` |
| all | `getAllCommands()` → `const Command*` | `getAllCommands()` → `CommandInfo` |
| by category | `getCommandsByCategory()` | `getCommandsByCategory()` |
| categories | `getCategories()` | `getCategories()` |

Same shape, different value types. **Merging is a mapping exercise, not a
design problem.**

## Where it should live — the constraint that decides it

`EverQuest` owns `m_command_registry`. `Application` owns the action
`CommandProcessor` and wires the fallback between them. **Only `Application`
sees both.**

So either `/help` moves to a place with both catalogs, or something gives it a
merged view. **Pick one and argue it** — do not let the choice fall out of
whichever compiles first. Note `/help` is currently a `stay-game` command
precisely *because* T138 judged migrating it as-is would only relocate the
blindness.

## THE SCOPE BOUNDARY — say this plainly, do not let it blur

**Registry 3 has no catalog.** I checked: `processSlashCommand()` has no help
branch, no command list, no query interface — and
`Application::printCommandResult()`'s own comment concedes *"the renderer-side
registry has no query interface reachable from the game thread."*

**So "registry-aware" can mean two registries, not three.** And the ~34
commands T139 deleted from the game registry now live **only** as `cmd == "/x"`
branches in a 340-line if/else chain — **they are currently invisible to any
help, permanently, until the renderer gets a catalog.**

That is a direct consequence of our own deletion work. **Do not report
"/help is now registry-aware" without stating this.** Giving the renderer a
queryable command table is real, separate work — **record it as a follow-up
with a size estimate; do not start it here.**

## What to build

- `/help` with no arguments lists categories across **both** registries.
- `/help <category>` and `/help <command>` resolve across both.
- **Deduplicate**: if a name or alias exists in both, show it once and say
  which registry answers it — remember **game is tried first**, so that is the
  one that wins. A help that lists a command the user cannot reach is worse
  than one that omits it.
- Preserve the existing output format unless you have a reason to change it —
  and if you do, say why.

## Verification

- x86 suite (current total **2411**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests**: assert `/help` surfaces commands from **both** registries, and
  that a name present in both appears once. That second case is the one that
  will rot.
- **Live**: run `/help` and confirm the count. **State the before and after
  numbers** — 11 today; it should be ~122 minus any dedup.
- **Report which registries are covered and which are not**, explicitly.

## Review

**Where the merged view lives, and why.** `Application::
dispatchToActionOrRenderer()` (`src/client/application.cpp`) — the same
method that already implements registries 2/3's dispatch stages, reached
from both the live chat window (via `EverQuest::
SetUnrecognizedCommandFallback`) and stdin/`--script` (via
`Application::DispatchSlashCommand`). It is the only object in the process
that holds a pointer to both catalogs: `EverQuest` owns registry 1
(`m_command_registry`) and `Application` owns registry 2
(`m_commandProcessor`), and nothing before this unit gave either side a way
to see the other's. Rather than teach `EverQuest` about
`action::CommandProcessor` (wrong-direction dependency — `eq.h`/`eq.cpp`
have zero action-layer includes today, and should stay that way) or hand
`Application` a copy of registry 1's data, I added one narrow, read-only
accessor — `EverQuest::GetCommandRegistry() const` — and deleted `/help`'s
registration from registry 1 entirely (it used to be a `stay-game` command
specifically because migrating it as-is would only relocate the blindness;
now there is nothing to migrate — it's gone from registry 1 for good). With
no registration to intercept it, `/help` now falls through registry 1's
dispatch and reaches `dispatchToActionOrRenderer()` through the SAME
fallback path every other action/renderer command already uses. The actual
merge logic (`BuildHelpResponse`/`ComputeHelpCoverage`) lives in its own TU
(`include/client/help/merged_help.h`, `src/client/help/merged_help.cpp`),
not inlined into `application.cpp` (already over the 1500-line warn
threshold pre-existing) — this also makes it independently unit-testable
without linking `Application`, `EverQuest`, or Irrlicht at all (see
`tests/test_merged_help.cpp`).

**Before/after command counts (live-measured, `review/T143/`).** Before:
**11** (registry 1 only, current fable tip). After: **114** distinct
primary command names in the no-args `/help` overview, verified
programmatically to contain zero duplicates. This is short of "122 minus
dedup" from the plan's framing — the plan's 122 total included the ~36
renderer-only (registry 3) commands as part of the client's total
vocabulary; 114 is registries 1+2's combined total after 1 collision was
deduplicated (see below), which is the true ceiling of what `/help` can
now describe.

**Duplicates and aliases.** Dedup is by identifier (primary name OR any
alias), not just primary name — a registry-2 command is hidden from the
merged view if either its name or any of its aliases collides with any
registry-1 identifier. This matters because dispatch order is
identifier-based too (`Application::dispatchToActionOrRenderer` resolves by
whatever name/alias the user typed, not by "primary name"), so an
alias-only collision is just as unreachable as a primary-name collision
would be. Registry 1 always wins (matches the real dispatch order: registry
1 is tried first via `EverQuest::DispatchSlashCommand`, registry 2 only
gets a look if registry 1 doesn't recognize the name). In the no-args
overview, a deduplicated command is shown once, under whichever category it
lives in (silently — the overview format is otherwise unchanged from
before this unit, per the plan's "preserve unless you have a reason"
guidance). In the specific-command detail view (`/help <command>`), I added
one new line — `Registry: game` or `Registry: action` — to every lookup
result, not just ambiguous ones; this is the one deliberate format change,
made because the plan explicitly calls out "say which registry answers it"
for the collision case, and printing it unconditionally is simpler and
strictly more informative than only printing it when a collision is
detected. `/help <category>` is genuinely new (neither registry supported
category-name lookup before this unit) and needed its own display; it lists
each merged command as `/name - description (game|action)`.

**Registry 3 (renderer) — explicitly EXCLUDED, not deprioritized.** The
~36 commands the T139/T142 deletions left living only as `cmd == "/x"`
branches in `IrrlichtRenderer::processSlashCommand()` remain completely
invisible to `/help` after this unit. Nothing in this change touches
registry 3 or gives it a catalog — `Application::printCommandResult()`'s
own comment already documented why ("the renderer-side registry has no
query interface reachable from the game thread") and that remains true.
See `refactor/deferred.md`'s new T143 entry for the full statement and a
follow-up size estimate (~1 medium/large unit for a retrofit-in-place
catalog beside the existing if/else chain plus a new cross-thread query
path over the bridge; 2-3 units if the chain itself is restructured into a
real registry first — the more durable option, argued in that entry).

**Verification.**
- x86 suite: **2432/2433** (`ctest --test-dir build`), sole failure the
  documented `AudioManagerTest.PreloadCommonSounds` ALSA host quirk. Total
  grew from 2416 to 2433 — 17 new tests in `tests/test_merged_help.cpp`
  (gating, no-args overview merge, dedup-by-name, dedup-by-alias, category
  lookup + its case-insensitivity, command detail for game-only/
  action-only/shared, unknown-command, null-registry defensiveness,
  coverage counters).
- ARM cross-compile (`scripts/build-arm-noble.sh`): succeeds,
  `build-arm-noble/bin/willeq` produced.
- `scripts/build-gles2-x86.sh`: succeeds (visual-gate tree).
- Visual gate: PASS (`ctest`'s `visual_gate` test, part of the 2432/2433
  run above), goldens untouched — this unit touches no rendering code.
- `scripts/refactor/check_file_sizes.sh fable`: no new FAILs. `eq.h` and
  `application.cpp` are pre-existing WARNs (already over the 1500-line
  threshold before this unit); each grew by only a handful of lines (one
  accessor, one dispatch intercept) — the actual new logic (~250 lines)
  went into the new `merged_help.h`/`.cpp` TU instead, per the protocol's
  "put substantial new code in new TUs" guidance.
- Live (`review/T143/README.md`): headless run against the real eqemu
  server (zone `befallen`, matching Testchar's saved position — confirmed
  unchanged before/after via a DB read, no restore needed), `--script`
  driving `/help`, `/help move`, `/help gameonly_does_not_exist`,
  `/help Chat`, `/camp`. All five behave as designed; full log and config
  are in the review directory.

**Deviations from the plan.**
1. The plan's live-verification framing expected "~122 minus dedup"; the
   measured after-count is 114 (registries 1+2 only — see "Before/after"
   above for why 122 was never reachable by this unit's design, given
   registry 3's exclusion is explicit and unchanged).
2. Added a `Registry: game`/`Registry: action` line to every
   specific-command `/help` result (not just collisions) — a small,
   justified format change beyond "preserve unless you have a reason",
   argued above.
3. Added category-name lookup (`/help <category>`) — new capability, not
   previously supported by either registry, needed to satisfy the plan's
   "`/help <category>` ... resolve across both" requirement (there was
   nothing to "preserve" here since neither side had this before).
4. Put the merge logic in a new TU (`src/client/help/merged_help.cpp`)
   rather than inline in `application.cpp`, for the file-size and
   testability reasons given above — not explicitly requested by the plan,
   but consistent with `EXECUTOR_PROTOCOL.md`'s file-size guidance.

## Board

`/help` now covers registries 1 (`EverQuest::m_command_registry`) and 2
(`action::CommandProcessor`) — 114 of 114 commands in those two registries,
deduplicated, zero blind spots between them. Registry 3 (the renderer's
~36-command if/else chain, the destination of T139/T142's deletions)
remains **completely invisible to `/help`** — it has no catalog at all, and
this unit does not add one. Follow-up on the board: **give the renderer a
queryable command table** so registry 3 can join the merge — sized at
~1 medium/large unit (retrofit-in-place metadata + new cross-thread query
path) to 2-3 units (if the if/else chain is restructured into a real
registry first, the more durable option) — see `refactor/deferred.md`'s
T143 entry for the full reasoning.
