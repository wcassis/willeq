# T98 — Delete the 34 shadowed `CommandProcessor` commands

**Slot: whichever is free. Headless. Live spot-check at the end if the lock is
free — otherwise flag it outstanding.**

**Territory: `src/client/action/command_processor.cpp` and its header,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T98/`. See "The cascade" —
`ActionDispatcher` is deliberately NOT in territory.**

Read T97's Review and `/mnt/projects/fable/review/T97/reachability_table.tsv`
first. **Will has ruled: delete the shadowed code.**

## The list is data, not prose — use the table

`/mnt/projects/fable/review/T97/reachability_table.tsv` is authoritative:
column 3 is `Shadowed` (34), `Reachable-via-alias` (1), or `Reachable` (30).

Delete **only** the 34 marked `Shadowed`:

```
say ooc auction tell reply gsay gu emote loc sit stand camp move moveto
follow stopfollow target cast interrupt invite disband decline who help
quit q debug timestamp skills gems mem forget pet filter
```

**`shout` must NOT be deleted.** It is `Reachable-via-alias` — its primary
name is shadowed but `/sh` reaches it, because the `game` registry's shout
alias is spelled `sho`. Deleting it would remove live behaviour.

Re-derive the list from the TSV rather than copying it from here. If your
derivation disagrees with the block above, **stop and report** — one of us is
wrong and finding out which matters more than proceeding.

## What "delete" means

For each of the 34: remove the `registerCommand(...)` registration **and** its
`cmdX()` handler, plus any helper that becomes unreferenced as a result.

**Verify before removing each helper** that no surviving command uses it.
`CommandProcessor` has 31 commands left (30 reachable + `shout`) and they may
share parsing or formatting helpers with the deleted ones. A helper still used
by a survivor stays.

## The cascade — report it, do not follow it

Deleting `cmdCast`, `cmdPet`, `cmdMem` and friends will leave methods on
`ActionDispatcher` and `EqActionHandler` with no remaining production caller —
including the `/pet` wrappers T96 just fixed and the gem-slot conversions T93
tested.

**Do not delete those.** `ActionDispatcher` is a deliberate action-layer API and
may be intended for a future scripting surface; that is Will's call, not a side
effect of this unit. **Produce a list of what becomes newly unreferenced** and
report it. That list is a deliverable.

## Tests

Many tests exercise the deleted commands. Handle them deliberately:

- A test that **only** covers a deleted command goes with it.
- A test that covers **shared logic** through a deleted command must be
  re-pointed at a surviving command, not deleted — otherwise this unit silently
  reduces coverage of live code.
- **State the count**: tests deleted, tests re-pointed, and the suite total
  before and after. A large drop that is not accounted for is a red flag.

Note T93's and T95's tests target `EqActionHandler` and `ActionDispatcher`
directly, not through `CommandProcessor`, so they should survive untouched —
confirm rather than assume.

## Do not bury the group-invite gap

T97 found that accepting a pending group invite via bare `/follow` is dead in
both registries: `game`'s Social `follow_cmd` (which carries that behaviour) is
overwritten by Movement's `follow`, and `action`'s fallback is shadowed.
Deleting `action`'s copy removes one of the two implementations.

**Before deleting it, record in `refactor/deferred.md` what the deleted code
did**, so the feature can be restored from the surviving `game`-side Social
registration rather than rediscovered. Also note whether
`eq_bridge.cpp`'s `GroupAcceptIntent` handler has a live producer — if it does,
a UI path still works and the gap is narrower than it looks.

## Verification — the bar is "nothing that worked stopped working"

- **Enumerate the surviving 31 commands and confirm each still dispatches**, by
  name and by every alias. This is the check that matters; a deletion that
  clips a survivor is the failure mode.
- Full build and suite. **Account for the test-count delta explicitly** against
  the current 2102/2103 baseline; the sole pre-existing failure is the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`.
- Visual gate — goldens must not move.
- If the eqemu lock is free, spot-check a handful of survivors live and confirm
  they still log `registry=` as before. If it is held, say the live leg is
  outstanding.

## What you may not do

- **Do not delete `shout`.**
- Do not reorder the registries or touch the `game` registry at all.
- Do not delete `ActionDispatcher`/`EqActionHandler` methods — report them.
- Do not delete a helper without proving no survivor uses it.
- **STOP and report** if deletion would require changing `Application`'s
  dispatch logic. That is a different unit.

## Review

(executor fills: the list as re-derived from the TSV and whether it matched;
what was deleted per command; helpers removed and how their disuse was proven;
the newly-unreferenced `ActionDispatcher`/`EqActionHandler` list; tests deleted
vs re-pointed with counts; the surviving-31 dispatch confirmation; the
group-invite record; deviations)

## Board

Close T97's entry with the disposition taken. Record the newly-unreferenced
action-layer methods as an open question for Will — deleting them is a separate
decision.
