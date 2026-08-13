# T101 — Migrate the Spells and Pet families (the hard case, deliberately early)

**Slot: slot2. Headless work, then a LIVE leg. eqemu lock normally — T100 is
running in parallel and also wants it, so do all headless work first and
contend for the lock at the end. If it is held, wait, then report the live leg
outstanding rather than stealing it.**

**Territory: `src/client/action/`, `src/client/eq_commands_spells.cpp` and
`eq_commands*.cpp` (removals only), `src/client/bridge/game_state_sync_bridge.*`
if coverage is missing, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T101/`.**

**Follow the migration recipe in T99's Review.** Read it first, in full. This
plan only adds what is specific to Spells and Pet.

## Why this family, and why now

T99 estimated 60-90 hours for the remaining ~120 commands and **flagged
Spells, `cast`, `interrupt` and `pet` as the costliest**. That is exactly why
this runs second rather than last.

If the recipe breaks anywhere, it breaks here. Finding that out now — before
tens of hours go into the easy families — is worth more than momentum. **A
clear "the recipe does not fit, and here is why" is a success for this unit**,
not a failure.

## Scope

`game`'s **Spells** category (6) plus **Pet** (1). Confirm exact membership
from the registrations.

## What makes this family hard — know before you start

- **`cast` and `interrupt` are real state machines**, not thin senders. Casting
  has gem slots, targets, interrupt conditions and timing. The action layer's
  `castSpell`/`castSpellOnTarget`/`interruptCast` exist but T98 reported
  `interruptCast` and `forgetSpell` as orphaned at the dispatcher layer.
- **T93 already tested the gem-slot conversion** (1-indexed UI slot to
  0-indexed `SpellSlot`) in `tests/test_eq_action_handler_transforms.cpp`, and
  found it *correct*. Do not re-litigate it; do not break it.
- **T95 added the `sendPetCommand` guard** (membership check over real
  enumerators, gaps at 0/15/22+) and **T96 fixed the ten `/pet` wrappers** to
  named enumerators. Those wrappers are currently orphaned. Migrating Pet gives
  them callers — the same shape as T99's Group win.
- **T97 found `game`'s `/pet` vocabulary is a strict superset of the action
  side's**, even after T96. So porting must take `game`'s richer vocabulary and
  express it through the (now correct) dispatcher wrappers, adding wrappers
  where `game` has commands the dispatcher lacks. Say which you added.
- **`/pet hold` and `/pet focus` draw no server response** on this eqemu build
  — T96 root-caused it to an AA gate neither test character's class can
  purchase. **That is not your bug.** Do not chase it; note it and move on.

## The recipe's addendum — do this first

**Check `GameStateSyncBridge` coverage BEFORE the live leg.** T99 found zero
cases for Group events and had to fix it mid-unit. Spell and pet state are
prime candidates for the same gap. Fix narrowly, following the file's pattern,
if missing.

## Atomicity

`game` is tried before `action`. Add and remove **in the same change**; verify
`registry=action` per command; keep an unmigrated control that still logs
`registry=game` in the same session, as T99 did with `/target`.

## Tests

Per T99 and the T95 standard, with every new test **proven able to fail in both
directions**. Confirm T93's and T95's existing tests still pass untouched — if
migration requires changing them, say so and explain, because that would mean
the migration altered behaviour rather than routing.

## Live leg — the honest bar

A caster character is needed. `Testcaster` exists on `willeq_test`.

- **Cast a real spell and observe the effect**, not an ack.
- **Summon a pet and exercise the migrated pet vocabulary**, confirming the pet
  obeys — T96 established this standard and found `/pet attack` had been
  sending dismiss.
- Confirm `registry=action` for every migrated command.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
never guess an item or spell id (resolve via `data/willeq.db`); restore DB
state and confirm by `SELECT` **after logout**; clean teardown, delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`** — the
  dependency cycle. Migration goes upward only.
- Do not migrate renderer commands. `eq_commands_spells.cpp` contains
  `rendererCmd()` helpers routing to the `RendererCommand` bridge queue —
  **those must stay**, because calling `processSlashCommand()` off the game
  thread runs GL with no EGL context and Mesa fails silently rather than
  crashing.
- Do not weaken T95's pet-command guard or re-number `PetCommand`.
- Do not migrate beyond Spells and Pet.
- **STOP and report if the recipe does not fit.** State precisely which step
  fails and why. Do not force it, and do not leave a half-migrated family.

## Verification

Full build and suite (expect 2121 total plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

(executor fills: confirmed membership; per-command source ported; dispatcher
wrappers added for `game`-only pet commands; `GameStateSyncBridge` coverage
before/after; atomicity proof with the control; live evidence a spell was cast
and the pet obeyed; whether T93/T95 tests needed changes and why; **whether the
recipe generalised to the hard family, and if not exactly where it failed**;
actual hours vs T99's estimate; deviations)

## Board

This is the batch that validates or invalidates T99's 60-90 hour estimate for
the whole programme. **Record the revised estimate** — Will is deciding how much
of the remaining ~110 commands to commit to on the strength of it.
