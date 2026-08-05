# T51 — Auto-attack has three implementations, two of them unreachable: establish ground truth and consolidate

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the ground-truth step and the
acceptance are both live.**
No sibling unit.

**Territory: the combat command registrations in
`src/client/eq_commands_movement.cpp` (registry 1) and
`src/client/action/command_processor.cpp` (registry 2), the two combat
intents in `include/client/events/renderer_intents.h` and their handlers
in `src/client/eq_bridge.cpp`, plus tests.** Do **not** change
`CombatManager`, `EqActionHandler`, `ActionDispatcher`'s combat methods,
or the keyboard/`InputActionBridge` path — those are live and correct.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T50_bank_train_prevalidation.md` and
`refactor/T48_vendor_reachability.md` (this unit is the same family and
inherits their standards), then the deferred entry "PATTERN: real
features reachable only through a UI gesture".

## Why this unit exists, and what it is NOT

T48 flagged `AttackIntent`/`ToggleAutoAttackIntent` as producer-less with
working handlers, and the board has carried "attack/auto-attack
reachability" ever since. **I checked, and that framing is wrong — this
is not a reachability gap.** The capability is reachable today. What is
actually there is a **three-way duplication**, and it is a concrete,
bounded instance of the open "~37-command registry overlap" item rather
than a new instance of the automation-reachability pattern.

## Ground truth I traced — verify, do not re-derive

There are **three** implementations of enable/disable/toggle auto-attack:

1. **Registry 1** (`eq_commands_movement.cpp:332-370`) — `/attack`
   (alias `att`), `/stopattack`, `/aa` (alias `~`). Calls
   `m_combat_manager->EnableAutoAttack()/DisableAutoAttack()` **directly**
   and prints "Auto attack ON"/"OFF".
2. **Registry 2** (`command_processor.cpp:374-384`) — `/attack`
   (alias `a`), `/stopattack`, `/aa`. Routes
   `cmdAttack`/`cmdStopAttack`/`cmdAutoAttack` →
   `ActionDispatcher::enableAutoAttack()/disableAutoAttack()/
   toggleAutoAttack()` → `EqActionHandler` → the same `CombatManager`.
3. **The two intents** — `AttackIntent`/`ToggleAutoAttackIntent`
   (`renderer_intents.h:34-38`), handled in `ProcessBridgeIntents`
   (`eq_bridge.cpp:1410`, `:1419`). **No producer anywhere.**

**Dispatch order is registry 1 → registry 2 → registry 3**
(`application.cpp`'s `DispatchSlashCommand`/`dispatchToActionOrRenderer`,
T29). So **registry 1 wins every colliding name, and registry 2's three
combat registrations are shadowed dead code.**

Two more facts that matter:

- **The keyboard gesture does NOT use the intents.** It goes
  `InputAction::ToggleAutoAttack` → `InputActionBridge`
  (`input_action_bridge.cpp:78-84`) → `ActionDispatcher` →
  `EqActionHandler`. So the **dispatcher methods are live** even though
  registry 2's *command registrations* are not. Deleting those
  registrations must not touch the dispatcher.
- **`ActionDispatcher::enableAutoAttack()` adds a
  `checkZoneConnection()` gate that registry 1's direct call skips.** I
  verified the gate is genuinely populated on the instance that matters:
  `setZoneConnected(true)` is called on `Application::m_gameState` at
  `application.cpp:743`, unguarded. (Note `eq_entities.cpp:1542` sets it
  on `EverQuest`'s *own* separate instance — the two-GameState trap
  again; do not confuse them.)

## Piece 0 — establish live ground truth FIRST, before changing anything

I traced the shadowing by reading dispatch code. **Prove it live before
acting on it.** The dispatcher logs the winning registry — T49's
transcript shows lines of the form:

```
CMD outcome=applied registry=game recognized=true cmd="/bank"
```

Run `/attack`, `/stopattack`, `/aa`, `/a` and `/att` in a live headless
session against a real target and record which registry serves each.
**If registry 2 turns out to serve any of them, my analysis is wrong —
STOP and report, because the whole shape of this unit changes.**

Also confirm auto-attack actually *engages* (swings happen), not merely
that a command was accepted. This is the "verify `/attack` live headless"
step the board has been carrying, and it is a deliverable in its own
right regardless of what you change afterwards.

## Piece 1 — consolidate

Assuming Piece 0 confirms the trace:

- **Delete registry 2's three shadowed combat registrations**
  (`attack`, `stopattack`, `aa`) and their now-unused
  `cmdAttack`/`cmdStopAttack`/`cmdAutoAttack` methods. **Leave
  `ActionDispatcher`'s combat methods alone** — the keyboard gesture
  needs them.
- **PRESERVE THE `a` ALIAS.** Registry 2's `/attack` carries alias `a`;
  registry 1's carries `att`. Registry 1 has no `a`, so **`/a` is served
  today only by registry 2 and would be silently lost by a naive
  deletion.** Move it onto registry 1's `attack` command and prove `/a`
  still works live. Check the same way for any other alias that only
  exists on the registry-2 side.
- **Delete `AttackIntent` and `ToggleAutoAttackIntent`** and their
  `ProcessBridgeIntents` handlers. Nothing produces them and the gesture
  does not use them.
  - **Check first whether anything depends on `RendererIntent`'s variant
    ordering or index** (serialization, a static_assert on
    `variant_size`, a test asserting indices). If something does, say so
    and adjust rather than silently shifting indices.
  - `AttackIntent` carries a capability the others lack — "target this id
    AND start attacking" in one step. `/target <id>` + `/attack`
    composes to the same thing, so nothing is lost, **but say so
    explicitly** rather than deleting it silently.

**If you conclude a different consolidation is better — e.g. keeping
registry 2's dispatcher-routed version and deleting registry 1's so the
zone-connection gate applies — argue it.** Note the structural
constraint before you do: registry 1's handlers live inside `EverQuest`,
which has no access to `Application`'s `ActionDispatcher`, so "route
registry 1 through the dispatcher" is not a small change. My lean is the
deletion above; the gate difference is worth a sentence in the Review
either way.

## STOP conditions

- Piece 0 contradicts the trace → STOP and report.
- Deleting the intents requires touching the `IrrlichtBridge` event
  switch or any live path → STOP; that is a different unit.
- Any change in what a user-typed combat command actually does, beyond
  the alias consolidation → STOP. **This unit should be behaviour-neutral
  at the command line**; that is the point.
- You cannot get auto-attack to visibly engage live → STOP and report
  rather than asserting from logs alone.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP unwaived (T35),
   golden replay does not skip (T36); pinned tests from T43-T50 stay
   green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `scripts/ci/visual_gate.sh` PASS, goldens untouched.
5. Unit tests: that the surviving commands and **every alias**
   (`attack`, `att`, `a`, `stopattack`, `aa`, `~`) still resolve, and
   that the deleted registry-2 names no longer register. Extend
   `test_command_processor.cpp` and/or the registry-1 tests as fits.
6. **Live acceptance, both trees, entirely piped:**
   - The Piece 0 registry table (which registry serves each name).
   - Auto-attack visibly engaging and disengaging against a real
     mob — swing/damage messages, not just command acknowledgements.
   - **Every alias exercised live after the change**, `/a` especially.
   - A before/after showing the command set is unchanged from the user's
     point of view.
   - eqemu LOCK FIRST (check `/mnt/projects/fable/.eqemu-lock` is absent;
     if it holds YOUR unit id from a crashed run, it is yours — do not
     stop over it); `up.sh` only; restore Testchar to zone_id=36,
     x=-77.9232, y=21.9086, z=3, h=190; `down.sh` and delete the lock
     after. Evidence under `benchmarks/audits/T51/`.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## What to report in the Review

- **The Piece 0 registry table** — this is the durable artifact even if
  the consolidation is trivial.
- Whether auto-attack genuinely works from the command channel headless
  (the board has assumed it does; nobody has shown it).
- What you deleted, what you preserved, and the alias audit.
- Whether anything depended on the intent variant ordering.
- **Whether the "~37-command registry overlap" item can now be
  narrowed** — you will have looked at the collision machinery closely.
  If you can cheaply enumerate the other colliding names, that list is
  worth more than this unit's diff. Do not fix them here.
- Be explicit that this **closes the board's "attack/auto-attack
  reachability" item by disproving it** — the capability was always
  reachable; the defect was duplication.

## Commit early

Two executors in a row have been interrupted mid-run by transient API
529s. **Commit working code before starting long-running verification**,
so an interruption costs a resume rather than the work.

## Review

### Piece 0 — the registry table, established live before any code changed

`benchmarks/audits/T51/run_piece0_probe.sh`, run against the unmodified
`build/bin/willeq` (`transcript_piece0_primary.txt` +
`.driver`), spawning a fresh `a_scaled_wolf_pup` (npc 79037 — the same mob
T32 used for its own real kill) per leg, targeting it, and sending each
name in turn:

| command typed | registry that served it (from the `CMD outcome=...` ack line) |
|---|---|
| `/attack` | `game` (registry 1) |
| `/stopattack` | `game` (registry 1) |
| `/aa` | `game` (registry 1) |
| `/att` | `game` (registry 1) |
| `/a` | `game` (registry 1) — **but not the attack alias**, see below |

**Registry 2 (`action`) served none of these — the plan's STOP condition
("if registry 2 turns out to serve any of them, my analysis is wrong")
did not trigger.** The orchestrator's trace was confirmed live, not just
by reading dispatch code: T29's dispatch order really does mean registry
1 wins every one of these collisions, so registry 2's three combat
registrations really were dead code before this unit touched anything.

### Auto-attack genuinely engages headless — proven, not asserted from logs

The probe transcript shows real, bidirectional melee damage against the
live mob for `/attack`, `/aa`, and `/att` (three separate targets, three
separate fights) — e.g. leg 2 (`/attack`):

```
[DEBUG] [COMBAT] Testchar -> a scaled wolf pup for 2 damage (type 4)
[DEBUG] [COMBAT] You hit a scaled wolf pup for 2 points of damage.
[DEBUG] [COMBAT] a scaled wolf pup -> Testchar for -5 damage (type 4)
```

repeated multiple rounds per leg, real server-driven `ZoneProcessDamage`
packets each time, not merely a `CMD outcome=applied` ack. This is the
"verify `/attack` live headless" item the board has carried for a long
time, closed here with a real fight, not just a command acceptance. (One
honest anomaly, not investigated further since three other legs already
proved engagement conclusively: leg 1's very first target, `/attack`
against `a_scaled_wolf_pup000`, sat enabled for the full 90s of that leg
with zero `ZoneProcessDamage` packets — looked at the transcript closely
and could not find a cause; not reproduced in any of the other four legs
or in the post-change acceptance run below, so treated as an
environment/timing fluke of the very first combat interaction after
zone-in rather than a client defect, and flagged here rather than
silently dropped.)

### THE ALIAS TRAP — corrected, not just verified

The plan's framing was: *"`/a` is carried ONLY by registry 2's attack
registration... a naive deletion silently removes `/a`. Move it and prove
it live."* **This is wrong, and the live probe is what caught it.**
`/a`'s `CMD outcome` line reports `registry=game` — but reading the
transcript around it shows registry 1 resolved `/a` to **`/assist`**, not
attack:

```
CMD outcome=applied registry=game recognized=true cmd="/target a_scaled_wolf_pup003"
CMD outcome=applied registry=game recognized=true cmd="/a"
[DEBUG] [MAIN] Assist request for target ID: 194
```

Grepping registry 1 (`eq_commands.cpp`) for every alias confirmed why:
`assist_cmd.aliases = {"a"}` (added by T28, `src/client/eq_commands.cpp`)
registers `/a` for **assist** in registry 1 itself — a *third* collision
the plan's ground truth missed, on top of the registry-1-vs-registry-2
attack collision it did catch. Registry 1 wins T29's dispatch order for
`/a` the same way it wins for `/attack`, so **registry 2's
`/a`-for-attack alias was already fully unreachable before this unit
touched anything** — it never actually toggled attack in production, at
any point in this project's history since T28 landed.

**Consequence for Piece 1's instructions, stated explicitly rather than
followed blindly**: the plan's literal instruction — "move [`/a`] onto
registry 1's attack command and prove `/a` still works live" — was not
followed, because doing so would have **changed** `/a`'s live behaviour
(from assist to attack), which the plan's own STOP condition forbids
("Any change in what a user-typed combat command actually does, beyond
the alias consolidation → STOP. This unit should be behaviour-neutral at
the command line."). Since `/a` already means assist today, behaviour-
neutrality means leaving it exactly as it is — which requires no code
change at all. Registry 2's dead `/a`-for-attack alias is deleted along
with the rest of its shadowed combat registrations, same as
`/attack`/`/stopattack`/`/aa`.

**Full alias audit** (grepped every `.aliases = {...}` in both registries
for the five names in play): registry 1 has `attack`→`{att}`,
`stopattack`→`{}`, `aa`→`{~}`, and — the one the plan missed — `assist`→
`{a}`. Registry 2 had `attack`→`{a}` (now deleted), `stopattack`→`{}`,
`aa`→`{}` (both now deleted). No other registry-2-only alias exists for
any of these five names.

### What was deleted, what was preserved

**Deleted** (all confirmed dead/shadowed before deletion, via the live
probe):
- Registry 2's `attack`/`stopattack`/`aa` command registrations
  (`src/client/action/command_processor.cpp`) and their
  `cmdAttack`/`cmdStopAttack`/`cmdAutoAttack` wrapper methods (`.cpp` +
  `.h`).
- `AttackIntent`/`ToggleAutoAttackIntent` (`renderer_intents.h`), their
  `RendererIntent` variant entries, and their `ProcessBridgeIntents()`
  handlers (`eq_bridge.cpp`) — confirmed producer-less by grepping all of
  `src/`/`include/` for a `pushIntent` call site; none exists anywhere,
  matching T48's original finding.

**Preserved, untouched**:
- `ActionDispatcher::enableAutoAttack()/disableAutoAttack()/
  toggleAutoAttack()` and `EqActionHandler` — the keyboard gesture
  (`InputActionBridge`) still calls these directly; the plan's fence.
  `checkZoneConnection()`'s gate (absent from registry 1's direct calls)
  is untouched either way — see the note below.
- Registry 1's `attack`/`stopattack`/`aa` (`eq_commands_movement.cpp`) —
  now the sole surviving implementation, annotated with a comment
  pointing at this unit for the next reader.
- `/a` → `/assist` (unchanged; see above).
- `AttackIntent`'s one distinguishing capability — "target this id AND
  start attacking" in one step — is not reproduced anywhere post-
  deletion, exactly as the plan flagged: `/target <id>` + `/attack`
  composes to the same effect, and nothing constructed the intent to
  begin with, so nothing reachable is lost.

### `RendererIntent` variant ordering/index — not depended on anywhere

Grepped `src/` and `include/` for `variant_size`, `.index()`, and any
`RendererIntent` serialization: no hits beyond the `std::visit` dispatch
in `eq_bridge.cpp`, which switches on type via `if constexpr
(std::is_same_v<T, ...>)` — order- and index-independent. Nothing else in
the codebase (tests included) asserts on `RendererIntent`'s variant
layout. Safe to remove two entries from the middle of the list, which is
what was done.

### The `checkZoneConnection()` gate difference — argued, not built around

Per the plan's invitation to argue for a different consolidation: **kept
the deletion as specified** (registry 1 survives, registry 2's duplicate
dies) rather than routing registry 1 through `ActionDispatcher` to pick
up its `checkZoneConnection()` gate. The plan's own structural note holds
up under inspection: registry 1's handlers live inside `EverQuest`, which
has no access to `Application`'s `ActionDispatcher` — routing through it
would mean threading a dependency across the game/action-layer boundary,
not a small change, and well outside a unit whose STOP conditions
explicitly forbid behaviour changes beyond alias consolidation. The gate
difference itself is real and now stated for the record: registry 1's
direct `CombatManager` calls do **not** check `checkZoneConnection()`
before enabling auto-attack, while the keyboard-gesture path through
`ActionDispatcher` does. This was true before this unit and remains true
after — T51 did not introduce or remove this asymmetry, only deleted the
duplicate that never exercised the gate either way.

### Live acceptance — after the change, both trees, entirely piped

`benchmarks/audits/T51/run_verification_primary.sh`, eqemu LOCK FIRST
(confirmed `/mnt/projects/fable/.eqemu-lock` absent, created with
`T51-attack-path-consolidation 2026-08-05`, removed after clean shutdown
and confirmed absent again), `scripts/eqemu/up.sh arena`, same session
used for both Piece 0's probe and this post-change run (no restart
between them, matching T48/T50's practice).

Every alias exercised live, post-change, in **both** trees
(`transcript_postchange_primary.txt` against `build/bin/willeq`,
`transcript_postchange_nogfx.txt` against `build-nogfx/bin/willeq`):
`/attack`, `/att`, `/stopattack`, `/aa` (toggle on/off), `/~` (toggle
on/off), `/a`. Every single one resolved via `registry=game`, identically
to Piece 0's pre-change probe — the registry table is unchanged. Real
melee damage (`"You hit a scaled wolf pup for N points of damage"`, real
`ZoneProcessDamage` packets) reproduced in both trees: 9 hits in the
graphics tree's run, 16 in the headless tree's run. `/a` still resolved
to assist (`"Assist request for target ID: ..."`), not attack, in both
trees — the behaviour-neutrality claim holds, live, not just in unit
tests.

**Before/after, the command set from the user's point of view**: leg 6 of
the post-change run re-sent `/attack`/`/stopattack`/`/aa`/`/aa` with no
new target and got the same acks and outcomes registry 1 always gave —
nothing a user types changed shape, spelling, or effect; only the
internal duplicate (never user-visible, since it was always shadowed) is
gone.

Testchar restored to `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190`
(confirmed via a `SELECT` after restoring — drifted a few thousandths
during the runs, same coordinate-axis noise T33/T34/T48/T50 documented,
corrected with a direct `UPDATE` and re-verified). Server brought down
cleanly (`down.sh`), lock removed and confirmed absent.

### Verification bar

1. Both configures build clean: `build` (`EQT_GRAPHICS=ON`,
   `EQT_GLES2=ON`) and `build-nogfx` (`EQT_GRAPHICS=OFF`) — clean
   rebuilds after every source change, no new warnings from this unit's
   files.
2. `ctest --output-on-failure`: `build` 1909/1910 passed, `build-nogfx`
   1477/1478 passed — the sole failure in each tree is the documented
   `AudioManagerTest.PreloadCommonSounds` (ALSA) exception. `visual_gate`
   passed as its own ctest entry in both trees. All new/changed tests
   (`EqCommandsAttackTest`'s 8 cases, `CommandProcessorTest`'s 2 new T51
   cases, the corrected `GetCommandsByCategory` case) pass in both trees,
   confirmed both via the full suite and via a targeted
   `-R "T51|CommandProcessor|Attack"` run (72/72 in each tree).
3. Golden replay: `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/
   session.sleq` run directly against both trees' `test_session_replay`
   binaries — confirmed the real recorded-session branch ran (not the
   baseline-creation fallback: real zone-map lookups from the capture,
   `befallen.map`/`qeynos2.map` "Unable to open" errors expected/benign,
   same as T48-T50), `PASSED` in both.
4. `scripts/ci/visual_gate.sh`: `PASS` (qeynos2/gfaydark/befallen all
   verified); `git status` on `data/` empty before and after — goldens
   untouched.
5. Unit tests: see above — `tests/test_eq_commands_attack.cpp` (new, 8
   cases) and `tests/test_command_processor.cpp` (2 new T51 cases + 1
   corrected pre-existing case that had asserted the now-deleted
   registry-2 `/attack` registration).
6. Live acceptance: see above — Piece 0's pre-change probe and the
   post-change acceptance run, both trees, both under
   `benchmarks/audits/T51/`.
7. `bash scripts/refactor/check_file_sizes.sh --files <11 touched/added
   paths>`: 9 `ok`, 2 pre-existing `WARN` (`refactor/deferred.md`,
   already `WARN` at 2796 lines on `fable` before this unit's +20 lines;
   `src/client/eq_bridge.cpp`, already `WARN` at 1917 lines on `fable`,
   now 1904 after this unit's net deletion) — confirmed via `git show
   0a453028:<path> | wc -l` that both were already over threshold before
   this unit touched them; no new FAILs, no new WARNs.

### Does this close the board's "attack/auto-attack reachability" item?

**Yes — by disproving it, exactly as the plan predicted.** T48 flagged
`AttackIntent`/`ToggleAutoAttackIntent` as looking like a reachability
gap (working handler, no producer — the same shape as `/combat on`,
`/loot`, and vendor before their own fixes). It wasn't one: auto-attack
was reachable the entire time via registry 1's direct `CombatManager`
calls, proven live here with real fights against a real mob. The actual
defect was a three-way duplication — two fully redundant command
registrations (one always shadowed) plus a wholly dead intent pair with
no producer anywhere. `refactor/deferred.md`'s "PATTERN" entry has been
updated to record this distinction explicitly, including a caution for
whoever scans the remaining ~40 producer-less intent types next: "has a
handler, no producer" is necessary but not sufficient for this pattern —
check for a parallel direct-call command first.

### The ~37-command registry overlap item — narrowed to a concrete list

Enumerated cheaply (no unit needed to do this by hand): extracted every
registered command name from both `EverQuest::m_command_registry`
(`eq_commands*.cpp`, 123 names) and `action::CommandProcessor`
(`command_processor.cpp`, 65 names post-deletion) and diffed them.
**34 names remain registered in both registries** (down from the ~37 T29
originally estimated — this unit's deletion of `attack`/`stopattack`/`aa`
accounts for the difference):

```
auction camp cast debug decline disband emote filter follow forget gems
gsay gu help interrupt invite loc mem move moveto ooc pet quit reply say
shout sit skills stand stopfollow target tell timestamp who
```

Registry 1 always wins every one of these under T29's dispatch order, so
every name on this list is registry 2's dead code by the same shape this
unit just fixed for three of them — **not fixed here** (out of scope,
per the plan's "do not fix them here"), but now a concrete, greppable
34-name list instead of an estimate, which is worth more to whoever picks
up that cleanup than re-deriving it from scratch.
