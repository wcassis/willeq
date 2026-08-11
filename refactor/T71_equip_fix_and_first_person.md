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

(executor fills: the `/equip` root cause and which layer it was in;
whether the shared drag/drop path was affected; packet evidence; the
first-person captures with what each shows; whether direct DB writes were
still needed and why; teardown confirmation; deviations)

Board: re-title T66's deferred `/equip` live-leg item as closed, and
record the first-person verdict — this is the last third of a request
that has been open since T63.
