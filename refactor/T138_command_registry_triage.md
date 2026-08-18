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

Executed in `/mnt/projects/fable/wt/slot3`, task branch
`task/T138-registry-triage`, at `fable` tip `b4af33a1`. Read-only audit — no
`src/`/`include/` files touched (`git status --short` empty against `fable`
throughout). Full table + README: `/mnt/projects/fable/review/T138/`
(`triage_table.tsv`, `README.md`). Two read-only subagents did the mechanical
file-by-file tracing (movement/renderer-diagnostic block; spells/audio/detail
block); every claim below was independently cross-checked by reading the
cited source directly, not taken on the subagents' word alone.

### Row-count reconciliation

Static extraction of `.name = "..."` assignments, one file at a time:

| file | expected | found | match |
|---|---|---|---|
| `eq_commands.cpp` | 1 | 1 (`equip`, line 751) | yes |
| `eq_commands_movement.cpp` | 43 | 43 | yes |
| `eq_commands_spells.cpp` | 17 | 17 | yes |
| **total** | **61** | **61** | **yes** |

`triage_table.tsv` has exactly 61 data rows (verified programmatically, not
by eye), and a per-file breakdown of the `source` column reproduces the same
1/43/17 split. The plan's enumeration is correct this time — no discrepancy
to report.

Cross-checked for **shadowing** against `action::CommandProcessor`'s current
name space (95 primary names + aliases, extracted from
`command_processor.cpp`, all the migrations through T136 already applied):
**zero overlap** with any of the 61 primary names or aliases. T97's 34
shadowed commands were already removed from `action` at T98; nothing in this
batch re-creates that defect. Checked for **duplicate primary names within**
the three `game`-registry files (T97's `who`/`follow` shape): none found — all
61 names are unique across the three files.

### Recommendation histogram

```
delete           34
migrate-action    17
stay-game          7
needs-decision      3
```
(61 total, machine-counted from the TSV.)

### The headline finding: most of the "renderer" bucket isn't move-renderer, it's delete

The plan hypothesized a "~38 renderer/diagnostic" bucket needing
`move-renderer`. That hypothesis doesn't survive contact with
`IrrlichtRenderer::processSlashCommand()` (`src/client/graphics/
irrlicht_renderer.cpp:1031-1370`): **32 of the 34 `delete` rows are
diagnostic/renderer toggles whose `game`-registry handler is a
`rendererCmd(name)` lambda that pushes `RendererCommand{"/name"}` onto the
bridge — and `processSlashCommand()` ALREADY has a native, working branch for
that exact name.** `Application::dispatchToActionOrRenderer()`'s registry-3
fallback (`application.cpp:1150-1174`) reaches that same native branch
automatically the instant the `game`-registry copy is deleted — there is
**nothing to move**, the destination implementation already exists and is
live. This is why `move-renderer` appears **zero times** in the table: every
candidate for it turned out to already BE there.

Two of those 32 are dead on both ends, not just redundant: `timestamp` and
`filter` push into `processSlashCommand` branches that unconditionally reply
"not available (WindowManager removed)" (`irrlicht_renderer.cpp:1259-1262`).
A third, `placeholder`, is dead on the renderer end alone — its own
description promises a toggle ("Toggle zone placeholder mesh visibility")
that the implementation never performs (`irrlicht_renderer.cpp:1363-1367`).
**Correction to a past Review**: T97's divergence table characterized
action-registry's now-deleted dead `/filter` as the worse side of a
comparison because "live pushes a real renderer-side toggle" — that "real
toggle" is the same dead stub found here. Not a defect this unit introduced,
but the record should show T97's "live is better" framing for `filter` was
half-wrong; both sides were non-functional.

The remaining 2 `delete` rows are in `eq_commands_spells.cpp`'s "Detail
System" group (7 of those 7 route through the identical duplicate pattern —
`detail`/`togglegrass`/`toggleplants`/`togglerocks`/`toggledebris`/`season`/
`detailinfo`, all matched natively in `processSlashCommand:1282-1333`) plus
`skills` (`eq_commands_spells.cpp:136-150`), which is dead for a different
reason: its bridge event `ToggleSkillsWindow` has exactly one consumer
(`irrlicht_bridge.cpp:724-726`) and that consumer is a single `LOG_TRACE`
with no state change at all — T132's `m_guild_id` shape (declared/guarded/
never wired), not a duplicate-implementation case. **The feature isn't
actually missing**: a real, working skills-window toggle exists under a
different name, `/newskills` (`irrlicht_renderer.cpp:1237-1243`, flips
`skillsPopupState_.isOpen` for real) — registered in NEITHER the game nor
action registry, reachable only by already knowing to type it. Recorded as a
separate discoverability finding in `refactor/deferred.md`, not fixed here.

### Every `delete`, by evidence category

- **Redundant duplicate of a live registry-3 handler** (30 rows): `sort`,
  `mergedraws`, `screenshot`, `upload`, `portal`, `stencil`, `governor`,
  `plight`, `olight`, `zlight`, `lightmode`, `shaderdebug`, `fire`,
  `renderdist`, `clipdist`, `sky`, `skytype`, `frametiming`, `simworker`,
  `sceneprofile`, `dumpscene`, `reloadeffects`, `reloadweather`, `detail`,
  `togglegrass`, `toggleplants`, `togglerocks`, `toggledebris`, `season`,
  `detailinfo` — each has a named `processSlashCommand` branch cited in the
  TSV. Deleting these is pure cleanup: zero functional loss, since registry
  3's fallback already answers the exact same name today whenever registries
  1/2 don't recognize it.
- **Dead on both ends** (2 rows): `timestamp`, `filter` — see above.
- **Dead stub at the destination** (1 row): `placeholder` — see above.
- **Dead event with zero consumer** (1 row): `skills` — see above.

### Every `needs-decision`, tradeoff stated

- **`help`** (`eq_commands_movement.cpp:356-398`) — reads only its own
  registry's catalog. T97 already documented that BOTH registries' `/help`
  share this exact blindness. Migrating verbatim just relocates the same gap
  (`action`'s own dead `cmdHelp` had it too, deleted at T98). Options: (a)
  leave in `game`, document the gap; (b) migrate as-is, inherit the same
  blindness on the other side; (c) build a registry-spanning `/help` — small
  but genuine new design, benefits both directions, not a mechanical
  migration.
- **`spellreload`** (`eq_commands_movement.cpp:530-544`) — calls
  `EQT::Graphics::Environment::SpellEffectsConfig::instance().reload()`
  DIRECTLY from the game thread. Self-flagged in-file (`movement.cpp:104-
  107`) as one of two exceptions bypassing the documented bridge-only
  communication rule (`CLAUDE.md`, "Key Patterns"). No renderer-3 equivalent
  exists for this name — this is not a duplicate, it's the only live
  implementation, just architecturally irregular, and no lock was found
  guarding `SpellEffectsConfig`'s fields against a concurrent render-thread
  read. Options: (a) leave as-is (works today, apparently data-only); (b)
  route through the bridge like every sibling reload command (small new
  `processSlashCommand` branch) — real design work (is a lock actually
  needed, or does bridge-only access make the race moot?), not mechanical.
- **`weatherquality`** (`eq_commands_movement.cpp:555-586`) — the second
  self-flagged direct-singleton exception (`WeatherConfigLoader::instance()`),
  identical shape and identical tradeoff to `spellreload`.

### `migrate-action` — 17 rows, and the `GameStateBridge*` gap does NOT block a whole class

T136 found `action::EqActionHandler` has no `GameStateBridge*` and worked
around it with three new `EverQuest::Request*` wrapper methods. The plan
asked whether a whole class of this unit's `migrate-action` rows depends on
closing that gap and, if so, to size it as a prerequisite unit.

**Traced every one of the 17 individually rather than assuming: no
prerequisite unit is needed.** The gap is real but already fully solved by
the established per-command recipe (T99–T136), and every row here fits it:

- **12 of 17 call an EverQuest method that is ALREADY public** (verified by
  reading `eq.h`'s access-specifier blocks directly, not assumed):
  `SetPositionState` (`eq.h:991`, used by `sit`/`stand`/`camp`), `Move`
  (`eq.h:833`, `move`), `MoveToEntity` (`eq.h:834`, `moveto`),
  `SetZoningEnabled`/`IsZoningEnabled` (`eq.h:854-855`, `zoning`), `Follow`
  (`eq.h:840`), `StopFollow` (`eq.h:841`), `RequestQuit` (`eq.h:908`,
  `quit`), `StartCampTimer`/`SendCamp` (`eq.h:1015,1019`, `camp`),
  `RequestTrainerWindow` (`eq_merchant.cpp:1472`, `train` — already a
  proper `Request*`-shaped wrapper, the cheapest row in the table), plus
  `loc`'s bare member reads and `perf`'s singleton read (no EverQuest call
  at all). `EqActionHandler` calling `m_eq.<PublicMethod>()` needs nothing
  new — this is the exact shape every migrated family (T99-T136) already
  uses.
- **3 of 17 need ONE new thin wrapper method each** (`warp`, `time`, `bank`)
  — their logic currently lives inline in the command lambda rather than as
  a callable `EverQuest` method. Each is a direct candidate for exactly
  T136's `RequestVendorToggle`/`RequestVendorBuy`/`RequestVendorSell`
  pattern: extract the existing lambda body verbatim into a new
  `EverQuest::Request*` method. `warp` (~80 lines, direct position write +
  `SendPositionUpdate` + a `PlayerMoved` bridge push), `time` (~15 lines,
  member write + `TimeOfDayChanged` push), `bank` (~40 lines, target
  validation + `BankerInteractIntent` push) — all three push a bridge event
  or intent, but from **inside** the new `EverQuest` method, which already
  owns `m_bridge` privately. No `GameStateBridge*` needs to reach
  `EqActionHandler` at all for any of these three.
- **2 of 17 sit outside this shape**: `debug`/`perf` are migrate-action on a
  different basis (precedent: `action::CommandProcessor` already hosts
  identically-shaped local diagnostics — `logmodule`/`logexclude`/
  `logclear`/`logonly`, `command_processor.cpp:720-735` — so these two just
  join their own siblings), no bridge involvement either way.
- **`equip`** (the 1 row from `eq_commands.cpp`) is the one genuine
  prerequisite case, and it is NOT new: T136 already found and sized it —
  `InventoryManager` is not reachable from `EqActionHandler` the way
  `CombatManager` is via `getCombatManager()`, and fixing that (plus
  `equip`'s three-input-form dispatch, T71 cursor-vs-slot staging, T77
  pre-flight legality) is real design work, already deferred as its own
  unit. This table adds no new instance of that class — it's the same one
  T136 flagged, carried forward.

**Bottom line for the plan's specific question: the `GameStateBridge*` gap
does not block a whole class of this unit's `migrate-action` rows.** It
constrains HOW three of them would be implemented (new wrapper methods, not
new plumbing) and it is the reason `equip` is its own unit — but it is not a
new prerequisite this unit is surfacing. `refactor/deferred.md` records this
explicitly so a future migration unit doesn't re-discover it.

### Ack-loss call

**`move-renderer` was recommended zero times** — every command that looked
like a `move-renderer` candidate on inspection turned out to already have a
live registry-3 implementation, so the recommendation is `delete`
(functionality already present at the destination) rather than
`move-renderer` (functionality would newly land there). Nothing in this
table trades away an ack that mattered: the 34 `delete` rows are either (a)
diagnostic toggles whose CURRENT ack, via the `game` registry, is already
`outcome=applied` reported unconditionally the moment
`EverQuest::DispatchSlashCommand` returns true (`application.cpp:1280-1285`)
— true regardless of whether the renderer thread actually received/acted on
the bridge push, since registry 1 has no way to know that either — or (b)
confirmed-dead paths with no ack worth preserving. **Deleting these rows and
letting them fall through to the registry-3 fallback is a truthfulness
IMPROVEMENT, not a regression**: today they're mislabeled `outcome=applied`
under `registry=game`; after deletion the identical fire-and-forget bridge
push is honestly labeled `outcome=queued` under `registry=renderer`
(`printCommandResult`, `application.cpp:1180-1231`). A script currently
synchronizing on `registry=game`/`outcome=applied` for any of these 32 names
would see its dispatch line change shape — worth flagging for whoever acts
on this table, since it is an observable behavior change even though no
functionality is lost.

None of the 17 `migrate-action` rows touch the ack vocabulary at all — they
move from `registry=game`/`outcome=applied` (or `failed`) to
`registry=action`/`outcome=applied` (or `failed`), the same acknowledged
vocabulary, exactly as T99-T136 already established for every prior
migrated family.

### The shape of the remaining work

- **34 deletions** — the largest bucket by far, and the cheapest: 32 are
  pure redundant-duplicate cleanup (delete the `game`-registry entry, the
  registry-3 fallback already answers), 2 more (`skills`, `placeholder`) are
  dead-code removal with no live behavior anywhere to preserve. A single
  follow-up unit could plausibly clear most or all of this bucket in one
  pass — it is deletion, not design.
- **17 migrations**, but NOT a uniform batch: 12 are same-recipe-as-T99-T136
  (call an already-public `EverQuest` method), 3 need one new thin wrapper
  method each (small, same shape as T136's vendor wrappers), 1 (`equip`) is
  its own unit-sized prerequisite (already known from T136, not new here).
  A sensible follow-up split: one unit for the 14 zero-new-plumbing rows
  (`loc`/`sit`/`stand`/`move`/`moveto`/`zoning`/`follow`/`stopfollow`/
  `quit`/`camp`/`debug`/`perf`/`train`/plus `warp`/`time`/`bank`'s three
  wrapper methods bundled in, since they're cheap), leaving `equip` for its
  own already-deferred unit.
- **7 stay-game** — `hotkeys`, `pmem`, and the 5 audio commands
  (`music`/`sound`/`volume`/`musicvolume`/`effectsvolume`). No action
  needed; each has a stated reason (local config I/O, a T108-fixed
  diagnostic that would regress if moved, or client-preference state outside
  the action registry's established gameplay-action scope).
- **3 decisions for Will** — `help` (registry-spanning help or accept the
  shared blindness), `spellreload`/`weatherquality` (leave the two
  self-flagged bridge-bypass exceptions as-is, or give them a real
  `processSlashCommand` branch). None are blocking; all are pre-existing
  conditions this unit found, not introduced.
- **No new prerequisite unit.** The `GameStateBridge*` gap T136 flagged is
  fully absorbed by the existing per-command wrapper recipe; `equip`'s
  `InventoryManager`-reachability gap is the one real prerequisite, already
  known and already deferred by T136, not a new discovery here.

### Verification

1. No `src/`/`include/` changes — `git status --short` empty against
   `fable` throughout (analysis-only unit, per the plan). `git diff --stat`
   against `fable` on this branch shows only `refactor/`,
   `/mnt/projects/fable/review/T138/` (outside the checkout, per the
   sanctioned media-path exception), and `refactor/deferred.md`.
2. `cmake --build build -j24` — clean (exit 0). `ctest --test-dir build
   --output-on-failure` — **99% tests passed, 1 tests failed out of 2361**;
   the sole failure is the documented ALSA `AudioManagerTest.
   PreloadCommonSounds`. Matches the plan's expected unchanged baseline
   exactly — confirms the tree is clean, as this step was framed as a
   formality (zero production code touched).
3. `scripts/refactor/check_file_sizes.sh fable` — expected "nothing was
   checked, this is not a pass" (zero `src/`/`include/` changes), matching
   T97's identical precedent for a read-only audit unit.

### Deviations

None from the plan's territory or scope. No eqemu lock taken, no Pi use, no
production code edited. The two read-only subagents (movement/renderer-
diagnostic block; spells/audio/detail block) each traced their assigned
commands independently; every mechanism claim in the table was additionally
spot-checked against the cited source directly in this session before being
written into the TSV, rather than accepted from the subagent reports
verbatim.

## Board

**61/61 triaged, all evidence-backed (`review/T138/triage_table.tsv`): 34
delete, 17 migrate-action, 7 stay-game, 3 needs-decision for Will, 0
move-renderer.**

The plan's own "~38 renderer/diagnostic" hypothesis was wrong in kind, not
just count: 32 of the 34 deletions are commands whose `game`-registry
handler pushes to the exact same renderer bridge command that
`IrrlichtRenderer::processSlashCommand()` **already implements natively** —
so there is nothing to *move*, the registry-3 destination already exists and
`Application::dispatchToActionOrRenderer`'s existing fallback reaches it the
instant the `game`-registry copy is deleted. `move-renderer` never fires in
this table because every candidate for it turned out to already be there.

**Prerequisite units, sized:**
- **`equip`** — its own migration unit (already known/deferred by T136:
  `InventoryManager` isn't reachable from `EqActionHandler` the way
  `CombatManager` is; three input forms, T71 staging, T77 pre-flight
  legality). Not a new discovery, carried forward.
- **No new `GameStateBridge*` prerequisite.** Traced all 17
  `migrate-action` rows individually: 12 call an `EverQuest` method that's
  already public, 3 (`warp`/`time`/`bank`) need one new thin wrapper method
  each in the established T136 shape, 2 (`debug`/`perf`) match an existing
  action-registry diagnostic precedent. The gap T136 flagged is real but
  fully absorbed by the recipe already in use — no unit needed to "fix" it
  first.

**Suggested follow-up units**, in priority order:
1. **Delete the 34** — mechanical, near-zero risk (30 are provably
   redundant, 2 dead on both ends, 1 dead stub, 1 dead event). Biggest
   cleanup-to-effort ratio in the programme so far.
2. **Migrate the 16 zero/cheap-plumbing commands** (everything in the
   `migrate-action` bucket except `equip`) — same recipe as T99-T136,
   three of them (`warp`/`time`/`bank`) each need one new wrapper method.
3. **`equip`** — standalone, already deferred, unchanged by this unit.
4. **Will's three decisions** (`help`, `spellreload`, `weatherquality`) can
   land whenever convenient — none block 1-3.

Also recorded in `refactor/deferred.md`: the `skills`/`/newskills` naming
split (real feature exists, under a different, unregistered name) and the
correction to T97's `filter` "live is better" characterization (the "real"
renderer toggle it praised is itself a dead stub).
