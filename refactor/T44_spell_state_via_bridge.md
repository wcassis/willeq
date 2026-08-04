# T44 — Feed spell state from the bridge, and settle how countdowns work

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `src/client/bridge/game_state_sync_bridge.cpp` (+ header),
`syncSpellState()` in `src/client/application.cpp`,
`src/client/state/spell_state.cpp` (+ header) if the countdown model
changes, the spell publish sites in `src/client/spell/spell_manager.cpp`
and `src/client/eq.cpp`, and tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T40_pet_state_via_bridge.md` (goal, target invariant, decorator
seam) and `refactor/T42_inventory_publishes_and_convert.md` (what an
events-first fix looks like when coverage is incomplete).

Third cut of the Application→EverQuest decoupling. Pet and inventory are
done.

## Why this one is harder, and what to decide first

`syncSpellState()` polls two genuinely different kinds of thing:

**Discrete state** — casting started/stopped, which spell is in which
gem, gem state transitions. This is exactly like pet and inventory: it
maps onto events cleanly.

**Continuous values** — `updateCastProgress(getCastTimeRemaining())` runs
*every tick while casting*, and `setGemCooldown(remaining, total)` runs
*every tick while a gem refreshes*. These are countdowns. There is no
event for "another 16 ms elapsed", and **publishing one every tick would
be a poll with extra steps** — strictly worse than what is there now.

**The event schema already anticipates this.** `SpellGemChangedData`
carries `cooldownRemainingMs`, `cooldownTotalMs` *and* `memorizeTotalMs`,
with the comment "(for progress computation)"; `SpellCastStartedData`
carries `castTimeMs`. Whoever designed these intended **publish once,
consumer computes elapsed** — not per-tick republishing.

So the first thing to settle, before writing conversion code:

- **Preferred**: `SpellState` stores start-time + total and computes
  remaining/progress on read. No ticking, no polling, and the events
  already carry what it needs.
- **Acceptable**: the state is advanced by a *local* tick that reads
  nothing from `EverQuest`. That still satisfies this arc's goal — the
  point is removing the cross-boundary reach, not event purism.
- **Not acceptable**: publishing a progress event per tick.

Say which you chose and why. If changing `SpellState`'s model would break
its readers, that is worth knowing — check who reads
`getCastProgress()`/cooldown values before committing.

## A property worth not breaking

`SpellState` **already publishes** `SpellGemChanged`/`CastingStateChanged`
onto the EventBus from its own setters (`spell_state.cpp`), and
`ConsoleRenderer` subscribes. So today the EventBus fan-out works — it is
just driven by the poll.

After conversion, the decorator drives those same setters from bridge
events and the EventBus fan-out keeps working unchanged. Preserve that;
do not have the decorator publish to the EventBus itself, or consumers
will see duplicates.

## Coverage — check before converting

`SpellGemChanged` has 3 publish sites, `SpellCastStarted` has 1, and
**`SpellCastComplete` has none — defined but never published**, the same
shape as `PetStatsChanged` before T40 and the inventory gaps before T42.

Establish end-to-end whether casting state (start, progress basis,
completion, interruption) is fully reconstructible from what the game
side publishes to the bridge. If it is not, fix the publishes first, as
T42 did — and if that turns out to be a large job, **STOP and report
rather than half-converting**; a partially-fed spell state is worse than
the poll, which self-corrects.

Spell state is in the full-state snapshot (`SpellGemChanged`,
`SpellbookSnapshot`), so initial state is covered since T41 — that half
you can rely on.

## Scope

1. Settle the countdown model (above) and implement it.
2. Fix any publish gaps needed to make casting state reconstructible.
3. Extend `GameStateSyncBridge` with the spell cases.
4. Delete `syncSpellState()`, its `m_lastIsCasting`/`m_lastCastingSpellId`
   members, and the `GetSpellManager()` read from `Application` — if that
   accessor loses its last caller, say so.
5. Tests: decorator cases, and the countdown model (a cast that starts,
   progresses and completes without any per-tick publish).

## STOP conditions

- Casting state cannot be made reconstructible without a large publish
  campaign → STOP, report the gap list, do not half-convert.
- The countdown cannot be modelled without per-tick publishing → STOP and
  report. **"Spell keeps a local tick" is a legitimate answer**; "spell
  publishes 60 events a second" is not.
- Changing `SpellState`'s countdown model breaks readers in a way that
  needs its own unit → STOP with the reader list.
- Any behaviour change beyond where spell state comes from → STOP.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35);
   the golden replay does not skip (T36); `FullStateSnapshot.
   HeadlessZoneInPopulatesGameState` (T43) asserts spell gems reach the
   bridge headless — if it fails, you broke the thing T43 just pinned.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. Unit tests per item 5, including that no progress event is published
   per tick.
5. **Live acceptance**: a real eqemu session casting a spell — gem goes
   to Casting, cast completes, gem enters and leaves cooldown — with
   `syncSpellState()` deleted, and cast progress/cooldown visibly
   advancing without per-tick events. eqemu LOCK FIRST; `up.sh` only;
   restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190;
   `down.sh` after. Evidence under `benchmarks/audits/T44/`.
   Note Testchar's class may not have useful spells — if casting cannot
   be exercised, say so plainly and lean on the unit tests rather than
   inventing a fixture.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: the countdown model chosen and why, whether casting
state proved reconstructible and what publishes were added, confirmation
the decorator does not double-publish to the EventBus, what was deleted,
the live cast evidence or an honest account of why not, deviations.)
