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

(executor fills: where the merged view lives and the argument for it; the
before/after command counts; how duplicates and aliases are handled; the
**explicit statement of registry-3's exclusion** and the follow-up estimate
for giving the renderer a catalog; deviations)

## Board

Say what `/help` now covers and what it still cannot see. The renderer-catalog
follow-up should go on the board with a size, since T139's deletions are what
made those ~34 commands undiscoverable.
