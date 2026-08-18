# T138 — Triage the 61 commands still in the game registry

**Slot: whichever is free. x86. NO eqemu lock. NO Pi. ANALYSIS ONLY.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T138/`. READ-ONLY on
`src/` and `include/` — this unit changes no production code.** If you find
yourself editing a `.cpp`, stop; that is a finding for the table, not work for
this unit.

Read T97's Review first (the reachability + divergence audit of the *action*
registry — this is its mirror image), then T136's (the architectural gap that
constrains the answer).

## Why this unit exists, and a correction it inherits

The command-migration programme (T99→T136) moved the **gameplay-action**
families. It was reported — by me, wrongly — as finishing the job. It did not:
I had enumerated only `src/client/eq_commands.cpp`. **Two more files register
game-registry commands**, and **61 remain**:

| file | count |
|---|---|
| `src/client/eq_commands.cpp` | 1 (`equip`) |
| `src/client/eq_commands_movement.cpp` | 43 |
| `src/client/eq_commands_spells.cpp` | 17 |

An older board note called this correctly and was under-weighted: *"Utility
goes last and needs triage, not batching — it holds the renderer commands that
must never migrate."*

**So the remaining work is a decision, not a migration.** This unit produces
the decision; it does not act on it.

## The deliverable: one evidence-backed row per command

A checked-in table — `review/T138/triage_table.tsv`, following T97's
`divergence_table.tsv` precedent — with **all 61** commands and, for each:

- **what it actually calls** (sender / bridge intent / manager / pure query /
  renderer state)
- **which layer owns the state it touches**
- **recommendation**, from a fixed vocabulary (below)
- **the evidence** for that recommendation — a file:line, not an impression

**Every one of the 61 gets a row.** A command you cannot classify is itself a
finding: say so in the row rather than omitting it.

### The recommendation vocabulary — use exactly these

- **`migrate-action`** — a gameplay action that belongs in
  `action::CommandProcessor`, same as the seven families already moved.
- **`stay-game`** — belongs where it is, with the reason.
- **`move-renderer`** — belongs in the renderer registry. **See the ack
  warning below; this is not a free move.**
- **`delete`** — dead, unreachable, or duplicated by an already-migrated
  command. **Given this project's dominant defect class, actively look for
  these.**
- **`needs-decision`** — genuinely ambiguous; state the options and the
  tradeoff for Will.

## Two constraints that should drive the answer

### 1. Moving a command to the renderer registry LOSES its acknowledgement

`Application::printCommandResult()` documents four vocabularies
(chat/game/action/renderer). Registries 1 and 2 report `outcome=applied` or
`failed` — the handler ran and said which. **Registry 3 reports `outcome=queued`,
which is explicitly "fire-and-forget onto the render-thread bridge, no ack
path, NOT a claim the effect landed."**

Automation synchronises on that line. **So `move-renderer` trades away the
ack.** For a diagnostic toggle that may be fine; for anything a script needs to
sequence on, it is not. **Say which side of that line each `move-renderer`
recommendation falls on.**

### 2. `EqActionHandler` cannot push bridge intents

T136 found this and it is load-bearing: `action::EqActionHandler` has no
`GameStateBridge*`. T136 worked around it by adding three
`EverQuest::Request*` methods.

Many of the 61 are renderer-facing and would need exactly that. **If a whole
class of `migrate-action` recommendations depends on closing that gap, say so
explicitly and size it** — that would be its own unit and a prerequisite, and
it is better known now than discovered per-command later.

## A starting hypothesis — verify it, do not inherit it

Eyeballing the names suggests roughly: ~18 gameplay-ish (`loc`, `sit`,
`stand`, `move`, `moveto`, `warp`, `follow`, `camp`, `sort`, `skills`, `bank`,
`train`, `hotkeys`…), ~5 audio (`music`, `sound`, `volume`, `musicvolume`,
`effectsvolume`), and ~38 renderer/diagnostic (`mergedraws`, `stencil`,
`shaderdebug`, `frametiming`, `simworker`, `sceneprofile`, `dumpscene`,
`plight`/`olight`/`zlight`, `clipdist`, `renderdist`, `skytype`, `screenshot`…).

**That is a guess from names and nothing more.** Names have misled this project
before — `gu` looked like a guild-management command and is guild *chat*;
`equip` sits in the Commerce block and is inventory. **Classify from what the
handler does.**

## Actively hunt for the dead ones

T97 found **34 of 65** action-registry commands unreachable. This is the mirror
audit and the same defect class is likely present. Check specifically for:

- commands **shadowed** by an already-migrated action command (game is tried
  first, so a stale `game` entry silently wins — that is T97's exact bug);
- handlers whose state is **never populated**, the shape T132 found with
  `m_guild_id` (declared, guarded on, assigned nowhere);
- **duplicates** across the three files.

**A confirmed-dead command is the most valuable row in the table** — it is
work that can be deleted rather than migrated.

## Do not

- **Do not migrate anything.** Not even an obvious one-liner. The value here is
  a complete, comparable survey; a half-done migration mixed into it makes the
  table untrustworthy.
- **Do not take the eqemu lock or the Pi.** This is a reading exercise. If you
  believe a specific row needs live confirmation, mark the row and say why
  rather than taking the lock for the unit.
- **Do not pad the table with confident guesses.** `needs-decision` with a
  clear statement of the tradeoff is worth more than a wrong `migrate-action`.

## Verification

- x86 suite unchanged (**2361**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) — you changed no code, so this is a
  cleanliness check that your tree is clean.
- **Row count must be exactly 61**, and the file-by-file counts must match the
  table above. If your enumeration disagrees, **your enumeration is the
  finding** — mine was wrong once already; say so and show the command.

## Review

(executor fills: the row-count reconciliation against the three files; the
recommendation histogram; every `delete` with its evidence; every
`needs-decision` with its tradeoff stated for Will; whether a class of
`migrate-action` is blocked on the `GameStateBridge*` gap and how big that
prerequisite is; the ack-loss call for each `move-renderer`; deviations)

## Board

Give the **shape of the remaining work**, not just a list: how many commands
are actually migration work, how many are deletions, how many are decisions
for Will, and what the prerequisite units are. That is what turns a corrected
mistake into a plan.
