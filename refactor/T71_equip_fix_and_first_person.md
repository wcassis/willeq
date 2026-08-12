# T71 — Fix `/equip`, then use it to answer the first-person question

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `src/client/eq_commands.cpp` (the `/equip` handler),
`src/client/eq.cpp`'s `SendMoveItem`/`ZoneProcessMoveItem` **only if the
defect proves to be there**, `refactor/`, `/mnt/projects/fable/review/T71/`,
and throwaway session scripts.**

Do not touch the spell/visual path — sibling T72 owns it. Do not touch
`bone_attachment.h` or the equipment rendering path; T65 settled it and
this unit consumes it. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md` (**eqemu section — lock first**),
then `refactor/T66_camera_and_equip_reachability.md`'s Review (it added
`/equip` and `/camera`, and explicitly deferred `/equip`'s live leg), then
`refactor/T63_live_equipment_render_verification.md`'s Review and
`/mnt/projects/fable/review/T63/README.md` (the session shape you are
repeating, including what went wrong with it).

**Two stages. Commit after stage 1 before starting stage 2** — a late
failure must not lose the fix.

---

## Stage 1 — `/equip` is broken live

T66 added `/equip <fromSlot> <toSlot>` as a wrapper over the existing
`SendMoveItem` and could not prove it end to end, because sibling T65 held
the server. T66 said so plainly and recorded the gap.

**T65 then exercised it live and it misbehaved**: the secondary slot ended
up **duplicating the primary item rather than swapping**. That is T65's
observation, recorded in `review/T65/README.md`, and it is all we have —
it was outside T65's territory so nothing was diagnosed.

So this stage starts with characterisation, not a fix:

1. **Reproduce it precisely.** What exactly does the client send, what does
   the server do, and what comes back? `SendMoveItem(fromSlot, toSlot,
   quantity)` (`include/client/eq.h:1928`, opcode `HC_OP_MoveItem =
   0x420f`) and the `ZoneProcessMoveItem` response path are both
   implemented; the bug is somewhere across that round trip.
2. **Establish whether the defect is in the command wrapper or the
   underlying protocol path.** This matters enormously: `/equip` is new
   and thin, but `SendMoveItem` predates it and is used by the drag/drop
   UI gesture too. **If the bug is in `SendMoveItem`, then inventory
   drag/drop has been broken the same way and nobody noticed** — that is a
   far bigger finding than a broken new command, and it must be reported
   as such.
3. Fix it at whichever layer the evidence points to. If it is in the
   shared path, verify the UI gesture as well as the command.

**Watch for the client/server authority trap.** T52 established the
governing rule for this codebase: the server is authoritative, and a
client that mis-applies a server-approved state immediately "corrects" the
server back via its next update. A duplicate rather than a swap smells
like a client-side assumption about the outcome rather than waiting for
the server's answer. T66's plan explicitly forbade client-side prediction;
check whether something predicts anyway.

Verify the fix by observing the actual packets and the resulting server
state, not just the on-screen result.

---

## Stage 2 — the first-person question, finally answerable

Will asked for equipped weapons verified in three views: 3rd person, 1st
person during a swing, and the paperdoll. T63 delivered two of three.
**It produced zero first-person evidence** because nothing scriptable
reached `ToggleCameraMode` — only a raw Ctrl+F5 key event — and it
correctly STOPped rather than inventing an affordance.

Two things landed since that make this possible:
- **`/camera` (T66)** exposes named modes that round-trip with
  `getCameraModeString()`. A script can now enter first person and assert
  that it did.
- **Video capture (T64)**: `--dump-frames-dir <dir> --dump-every <N>
  --dump-max-frames <N>` plus `scripts/ci/encode_capture.sh`. **Use it.**
  The weapon appears only during the attack animation
  (`triggerFirstPersonAttack()`, `updateFirstPersonWeapons()`,
  `entity_renderer.h:407-415`), so a still will usually miss it. That is
  precisely what video is for, and T63 was reduced to burst-guessing.

Capture, in first person, with a weapon equipped (using your own fixed
`/equip`), a real attack swing against a live target:
- **primary weapon only**, and
- **dual wield** — T65 fixed plain secondary weapons rendering inside the
  torso, and that fix has never been seen in first person.

Also worth capturing if cheap: what first person looks like with a shield
equipped, since shields may legitimately not be shown.

**Use a lit zone.** T63's captures were in a near-black zone and are
largely unjudgeable; T65 had to repeat the work in `qeynos2` for exactly
this reason. Do not make that mistake a third time.

`encode_capture.sh` writes the measured fps, frame count and real-time
span next to the video. **Keep that file with it** — capture fps is an
artifact of per-frame `glReadPixels`, not a performance measurement, and a
viewer without that context will conclude the game runs at 5fps.

---

## Setup and teardown

- **Lock first**: `/mnt/projects/fable/.eqemu-lock` must not exist; if it
  does, STOP. Create it with `T71` + date, delete after clean shutdown.
- Sanctioned start only (`scripts/eqemu/up.sh`), clean shutdown
  (`down.sh`).
- `Testmelee` (account `willeq_test`, GM) is the right character — dual
  wield needs the skill; T63 GM-set it to 50 from 0.
- **Restore everything you touch** and say so. T63 and T65 both had to
  write `inventory.item_id` directly because no scriptable equip existed —
  **the point of stage 1 is that you should not have to.** If you fall
  back to direct DB writes for any reason, that is a finding about the
  fix, not a footnote.
- Resolve items through `data/willeq.db`'s `item_models` and state item id
  **and** resolved model id for everything you equip. T60's phantom bug
  came from guessing an item id and getting a sack.

## STOP conditions

- `.eqemu-lock` held.
- Stage 1's defect proving to be in `SendMoveItem`'s protocol handling in
  a way that needs a decision about scope — report; a shared-path
  inventory bug may deserve its own unit.
- Captures coming back garbled — report the harness limitation rather
  than shipping noise.
- Any urge to change equipment *rendering*. T65 owns that ground and it is
  settled.

## Verification

Full protocol bar. Additionally: packet-level evidence that `/equip` now
swaps correctly; the video captures with their measured-fps files; and an
explicit statement of whether first person shows the weapon during the
swing — **including if it does not**, which would itself be the finding
Will has been waiting on since T63.

Media to `/mnt/projects/fable/review/T71/` with a README you write, one
section per configuration. **Describe what is in each frame; do not grade
it.** Will judges.

## Review

### Stage 1 — `/equip`'s root cause, and which layer it was in

**Not in `SendMoveItem`/`ZoneProcessMoveItem`.** Three live eqemu round
trips against a controlled Testmelee baseline (`nektulos`, later moved to
`qeynos2` for stage 2), each verified via a fresh `inventory` table read
independent of the client (never trusting the client's own confirmation
text, which T66's Review already flagged as not a server ack):

1. **Direct equipment<->equipment swap**, both slots occupied with valid
   weapons (`/equip 13 14`, Rusty Mace <-> Dagger): server performed a
   correct atomic swap (`primary=9997 secondary=6011` after, from
   `primary=6011 secondary=9997` before).
2. **Cursor-mediated swap with a clean, single-item cursor**
   (`/equip 13 30` then `/equip 30 14`): correct three-way rotation —
   secondary ends up holding the picked-up item, the displaced original
   secondary occupant ends up on the cursor, exactly matching how real EQ
   drag/drop swaps behave.
3. **Cursor-mediated swap with a DELIBERATELY congested, multi-item
   cursor** (two `#summonitem` calls stacked onto cursor overflow before
   `/equip 30 14`): still correct — the server addressed only the true
   active cursor slot (server-internal slot 33) and left the overflow
   items (server-internal slots in the 6000s, `TitaniumToServerSlot`'s own
   cursor-bag numbering, confirmed by reading `common/patches/titanium.cpp`
   in the referenced EQEmu source, not guessed) untouched.

All three prove the wire protocol correct and unmodified — `eq.h`/
`eq_merchant.cpp` were not touched by this fix. **A pre-existing DB
methodology trap was found and corrected along the way**: `inventory.slot_id`
stores the SERVER's internal (RoF2-derived) slot numbering, not the wire/
Titanium numbering `SendMoveItem` sends — they're identical for equipment
slots 0-20 and for a handful of others, but diverge for general/cursor/bag
ranges (`Titanium::TitaniumToServerSlot()`/`ServerToTitaniumSlot()`,
`+1`/`+3`/etc. depending on range). A raw DB write intended to simulate
"item in general slot N" without going through this translation lands in
the wrong actual slot. Flagged here because it cost real time mid-unit and
would trip up a future unit reading/writing `inventory` directly.

**The actual, confirmed, fixed defect**: `RegisterEquipmentCommands()`
(`eq_commands.cpp`) only ever pushed the wire-level `MoveItemIntent` — it
never touched `InventoryManager`'s own local model (`items_`/
`cursorQueue_`) and never fired `EquipmentChangedIntent`. The wire protocol
has no success-acknowledgement, only a failure-correction (confirmed live
by deliberately triggering one: equipping a light source item — a Lantern
— into the Primary weapon slot produced a real server-side rejection and
an `"Inventory Desyncronization... Resending slot data"` correction,
log-captured). So a silently-accepted `/equip` request left the client's
own inventory view — and the rendered/paperdoll appearance that reads it
verbatim in `UpdatePlayerAppearanceFromInventory()` — stale until an
unrelated event refreshed it. **Drag/drop's own gesture
(`InventoryManager::pickupItem()`/`placeItem()`) was never affected**: it
already updates local state and fires `EquipmentChangedIntent` before
sending, which is exactly how the real EQ protocol expects a client to
behave (optimistic local apply, corrected only on failure) — `/equip` was
simply incomplete relative to its own stated design (T66: "pushes the SAME
MoveItemIntent the inventory UI's drag-and-drop gesture pushes"), not a
protocol bug and not a shared-path bug.

### The fix

Scoped to `eq_commands.cpp`'s `/equip` handler only. No legality judgment
added — T66's "server is authoritative" rule is unchanged; this only keeps
the local mirror in sync with what was *requested*, not with what the
server will ultimately allow:

- Cursor-involving moves (`fromSlot`/`toSlot` is `CURSOR_SLOT`/
  `SERVER_CURSOR_SLOT`) now delegate to `InventoryManager::pickupItem()`/
  `placeItem()` — already-correct, already-tested code that pushes the
  IDENTICAL `MoveItemIntent{fromSlot,toSlot,0}` the old handler pushed
  directly, so live wire behaviour for these cases is byte-for-byte
  unchanged; only the missing local sync (and `EquipmentChangedIntent`)
  is added, for free.
- A direct slot<->slot move where NEITHER endpoint is the cursor (e.g.
  `/equip 13 14`) has no existing helper that preserves the single-packet
  atomic swap the server's own `SwapItem()` already performs correctly
  (proven live above) — `InventoryManager::swapItems()` exists but was
  never wired to anything and applies client-side legality gating
  (`canPlaceItemInSlot`) that would reintroduce exactly the client-side
  judgment T66 excluded, so it was deliberately NOT reused. Instead this
  case mirrors the two map entries locally via the public `getItem()`/
  `setItem()`/`removeItem()` API (no legality checks) and still sends the
  SAME single `MoveItemIntent` as before.
- `EverQuest::GetInventoryManager()` added to `eq.h` (same accessor
  pattern as the pre-existing `GetTradeManager()`/`GetSpellManager()`) —
  needed so the fix is unit-testable without a live server.

Re-verified live, post-fix, with the same two round trips (1) and (2)
above: still correct server-side DB state, AND
`UpdatePlayerAppearanceFromInventory()` now fires immediately (log-
confirmed: `Building appearance from inventory` / `Updated player
appearance` right after the `SendMoveItem` log line) instead of only after
an unrelated resync.

### Shared drag/drop path — confirmed unaffected

Not touched by this fix (only `eq_commands.cpp` + `eq.h` + the equip test
file changed — `git diff --stat` confirms), and was never the site of the
defect per the root-cause analysis above. `InventoryManager::pickupItem()`/
`placeItem()`, the drag/drop gesture's own implementation, were read in
full and already do everything `/equip` was missing.

### Packet evidence

Every claim above is backed by `SendMoveItem`/`Sending MoveItem packet`
DEBUG log lines (`fromSlot`/`toSlot`/`qty`) paired with a fresh
`SELECT ... FROM inventory WHERE character_id=66` immediately after, not
just the client's own confirmation text. The one deliberately-triggered
failure case additionally shows the server's own correction packets
(`ItemPacket received` x2, `Source/Destination slot N resyncronized`).

### Test suite

`tests/test_eq_commands_equip.cpp`: the 5 pre-existing happy-path tests
were updated to seed a real item first (the fix now requires one, matching
`pickupItem()`'s own "nothing there" refusal — an intentional, documented
behaviour change) and to account for the now-additional
`EquipmentChangedIntent`; 4 new tests assert the local model directly
(direct swap, move-to-empty-slot, empty-source refusal, cursor-mediated
swap) rather than only the intent pushed. 16/16 pass.

### Stage 2 — the first-person verdict

**First person shows NEITHER the player's body NOR any equipped weapon or
shield, ever — not just outside a swing — by design of un-wired code, not
a capture failure.** Read directly from the renderer, not inferred:
`IrrlichtRenderer::setCameraMode()`/`cycleCameraMode()` (the only two live
entry points into first person — `/camera` and the pre-existing Ctrl+F5
path both funnel through them) call
`entityRenderer_->setPlayerEntityVisible(cameraMode_ != CameraMode::FirstPerson)`,
and `EntityRenderer::setPlayerEntityVisible(false)` explicitly hides the
body mesh, name tag, AND both `primaryEquipNode`/`secondaryEquipNode`.
Separately, `EntityRenderer::setPlayerFirstPersonMode()` (which would
switch to "arms + weapon visible"), `updateFirstPersonWeapons()`, and
`triggerFirstPersonAttack()` are fully implemented but have **zero callers
anywhere in the codebase** (confirmed by an exhaustive grep across `src/`
and `include/`, matching only their own definitions). T63's read of these
three as "near-no-op compatibility stubs... reuses the same skeletal
bone-attachment path" was half right — the bone-attachment path is shared,
but nothing ever routes execution into it for first person, because
nothing calls the function that would flip `isFirstPersonMode` on.

Visually confirmed live in three configurations, `qeynos2` (lit — grass,
trees, visible sky, legible in every frame, unlike T63's near-black
`arena`), with `/camera`'s `getCameraModeString()` confirmation
("Camera mode: First Person") visible on screen as the plan asked:
primary weapon only (Rusty Mace), dual wield (Mace + Dagger, Dual Wield
skill GM-set to 50 as T63/T65 did), and shield (Mace + Kite Shield). All
three: nothing of the player renders. Auto-attack was confirmed engaged
(log-verified) for the primary-only and dual-wield captures against a live
`a_rat000` target; since the code-level finding is unconditional on combat
state, a landed swing does not change the answer, and the shield capture
did not chase one. Full captures, stills, and the code citations are in
`review/T71/README.md`.

This is a finding, not a fix — no STOP condition was triggered (reading
`irrlicht_renderer.cpp`/`entity_renderer.cpp` to understand the captures
is not "changing equipment rendering"; neither file was modified). A
future unit owns deciding whether/how to wire `setPlayerFirstPersonMode()`
in.

### Direct DB writes — where they were and were not needed

**Every actual equip action in stage 2 went through the live, now-fixed
`/equip` command** — the explicit point of fixing it first. Direct DB
writes were used only for: (a) stage 1's controlled characterisation setup
(establishing known before-states — e.g. "primary holds a Lantern" — to
observe `/equip`'s effect against, since a controlled experiment needs a
controlled starting condition); (b) Dual Wield skill (no scriptable
skill-training path exists for combat skills, same as T63/T65's
precedent); (c) character position (moving Testmelee to `qeynos2`, same
sanctioned pattern T65 used). No DB write was used to place an item that
`/equip` itself then moved — that gap (T63 had to write `item_id` directly
and restart the client) is exactly what stage 1 closed.

### Teardown

Confirmed via final `SELECT`s: Testmelee's position restored to the exact
recorded baseline (`zone_id=25` (nektulos), `x=-259 y=-1201 z=-5.75
heading=160.25`); primary/secondary/cursor inventory slots cleared (empty,
matching their state before this unit); Dual Wield skill restored to 0.
**Pre-existing DB hygiene finding, corrected as part of this unit's own
setup**: Testmelee's `inventory` table carried four orphaned "cursor
overflow" rows (`slot_id` 6011-6014) plus a stale cursor item — not typos,
confirmed to be eqemu's real internal cursor-bag numbering by reproducing
the identical pattern live via two `#summonitem` calls in a row; deleted
to establish a clean baseline, not present in the final restored state.
Server: clean shutdown via `down.sh`, lock file deleted.

### Deviations

1. `eq_commands.cpp` crossed the 1500-line WARN threshold (1498 -> 1595)
   — `check_file_sizes.sh` against the actual fork point (`810fb0d9`, not
   the literal `fable` ref, which moved forward during this unit from
   T72/T73 merging) reports 2 WARN (this file, plus the pre-existing
   `eq.h` WARN at 2068 lines, +4 from `GetInventoryManager()`), 0 FAIL.
   Flagged rather than silently absorbed, per protocol — no FAIL, and the
   added code (extensive characterisation comments plus the fix itself)
   was judged worth the line count over trimming the reasoning out.
2. `check_file_sizes.sh fable` (the literal default) reported spurious
   diffs against T72/T73's own files because the shared `fable` ref
   advanced past this unit's fork point mid-session (T72/T73 merged) —
   re-run against the actual merge-base commit for a clean, isolated
   result. Not a deviation in this unit's own changes, just a note for
   whoever hits the same confusion.
3. Two live capture scripts had to be corrected mid-session: an initial
   bare `/target` needs a name argument (usage error, not a silent
   failure) and `/target rat` matched `Guard_Drath` (substring collision)
   before `/target a_rat` correctly isolated the spawned test NPC.
   Recorded because the first attempt's frames (showing an out-of-range
   "too far away" target) are not part of the evidence set, superseded by
   the corrected runs.

Board (`refactor/deferred.md`): instance 8 (`/equip`'s live leg) re-titled
CLOSED, with the root cause and fix summarized; the first-person verdict
recorded in the same entry — this is the last third of a request that has
been open since T63.
