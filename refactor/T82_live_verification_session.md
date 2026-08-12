# T82 — One live session, three deferred legs, and proof `/who` and `/face` work

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T82/`, throwaway
session scripts. Production code is READ-ONLY unless Part 3 finds a defect
whose fix is genuinely small and in `eq_commands.cpp` — see Part 3.**

Read `refactor/EXECUTOR_PROTOCOL.md` (**eqemu section — lock first; and
the daylight directive in the media section**), then the Reviews of
**T81** (entity projection), **T77** (`/equip` pre-flight) and **T76**
(which reproduced the equip rejection).

Three units deferred a live leg for want of the server, and they converge
on one session. `/equip` is also how you arm a character for the rest, so
the parts are ordered deliberately.

## Part 1 — `/equip` live leg (T77's outstanding leg)

T66 added `/equip`; T71 fixed its client-model half and proved the wire
path correct with three live round trips; T77 added pre-flight validation
and item-name resolution but **could not test any of it live**.

Exercise, against a real character, and report what each does:
- `/equip list` — does it show equipped items by slot and carried items
  with resolved destinations?
- `/equip <item name>` — does name resolution find the item and pick a
  legal slot?
- `/equip <from> <to>` — the explicit form T71 verified.
- The rejection paths: empty source slot, illegal destination. **The point
  of T77 was error messages a script can act on** — confirm "slot 23 is
  empty" and "X cannot go in Y" rather than "invalid slot".

## Part 2 — `/who` and `/face` must be seen working

T81 populated `Application::m_gameState->entities()` and passed its
falsification test (zero consumer edits). But **nobody has watched these
work**, and this whole thread began with them being broken.

- `/who` — must list real nearby entities, not an empty header.
- `/face <target>` — must turn toward a visible NPC, not report "Entity
  not found".
- Also worth exercising: `follow`, `moveToEntity`, interact-nearest.

**If they still fail, that is the finding**, and it means T81's projection
is populated in tests but not in a live session — trace where the chain
breaks (is `EntityStateSnapshot` published? does `GameStateSyncBridge`
handle it? does the projection contain entities at the moment the command
runs?) and report. Do not fix blind.

## Part 3 — why does the server reject that equip move?

T75 hit `"The selected slot was invalid."` moving slot 23 → 13. T76
reproduced it fresh. **Nobody has explained it.** T71 separately proved
`SendMoveItem` correct with three successful round trips, so the wire path
works for *some* moves — the question is what is different about this one.

Establish the actual cause. Candidates worth checking, not a checklist to
tick: is slot 23 what the client thinks it is; is the item in it equippable
in slot 13 at all; does the character meet its restrictions; is the client's
inventory model out of sync with the server's before the move is sent.

Note T77's pre-flight should now catch several of these **before** sending
— so if `/equip` now refuses locally with a clear reason, that IS the
answer, and Part 3 is closed by Part 1.

**If the cause is a small, clearly-scoped defect in `eq_commands.cpp`, fix
it** and re-verify live. If it is anywhere else, or larger, report and stop
— that is a separate unit.

## Setup and teardown

- **Lock first.** Sanctioned start (`scripts/eqemu/up.sh`), clean shutdown
  (`down.sh`), lock deleted.
- **Force daylight**: `/say #set time 11 0` (space-separated), verified
  early per the protocol.
- `Testmelee` for equipment work (dual wield skill), `Testchar` otherwise.
- Resolve items through `data/willeq.db`'s `item_models`; state item id
  **and** resolved model id for anything equipped. T60's phantom bug came
  from guessing an id and getting a sack.
- **Restore everything** and confirm by `SELECT`. If you need direct DB
  writes to equip, that is a finding about Part 1, not a footnote.

## Verification

The deliverable is evidence, so the build/test bar is a formality — run it
to prove you broke nothing. Put the effort into the captures and the
written findings.

Media to `/mnt/projects/fable/review/T82/` with a README. Video for
anything temporal, stills are fine for command output. **Describe what
each shows; do not grade it.**

## Review

(executor fills: per-part outcome; the exact `/equip` behaviours observed;
whether `/who` and `/face` work and the evidence; the root cause of the
server rejection; anything fixed and why it qualified as small; teardown
confirmation; deviations)

Board: close T77's and T81's outstanding live-leg notes with the result,
and record the equip-rejection cause. Three entries currently say "live
leg outstanding" — they should not still say that after this unit.
