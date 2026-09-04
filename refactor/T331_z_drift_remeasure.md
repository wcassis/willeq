# T331 — Re-measure the login z-compounding (B049): does T311's fix hold?

Class A suspicion, measurement first (`verify-dont-report`). Needs the
eqemu lock; slot1. No code change in this unit.

## What is known

- T311 Leg B (2026-09-02) matched the official client's wire z
  (`groundZ + 0.625*size`) and reported the per-login compounding closed.
- T328 Leg B (2026-09-04) found `Testchar` at `z=11.8806` after being left
  at `z=3.751` — exactly 13 × 0.625 — across the sessions between.

## Measure

1. Lock eqemu (noclobber). Start the server (`scripts/eqemu/up.sh`).
2. Read `Testchar`'s persisted position from the DB (the `character_data`
   row: `x, y, z, heading, zone_id`) — record it.
3. Run fable headless (`build/bin/willeq --no-graphics`, account
   willeq_test / Testchar), let it fully zone in, wait 10 s, `/camp` or
   quit cleanly. Read the DB row again. Repeat the login/quit cycle
   **three times**, recording z after each.
4. If z grows by 0.625 per cycle: the writer. Instrument by reading the
   packet log (`--log-level` for NET at DEBUG) for the FIRST
   `OP_ClientUpdate` fable sends after zone-in, and the `OP_ZoneEntry` /
   `OP_PlayerProfile` z it received; diff them. Name the code path that
   adds the offset (`movement/` `SendPositionUpdate` packing, or a
   `SetPosition` on zone-in that re-applies `+0.625*size` to an already
   adjusted z).
5. Control: the same three cycles with the OFFICIAL client under Wine
   (T328 Leg A's rig), reading the DB row after each — the oracle's z must
   stay constant. If the official client ALSO drifts, the server is the
   writer and B049 is closed as "not fable".
6. Restore `Testchar` to `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190`
   (SELECT first — it may not be there); `down.sh`; delete the lock.

## Deliverable

`review/T331/README.md` with the z table per cycle (fable, official),
the packet excerpts, and the named writer or the "server" verdict;
B049 updated on the board (fixed in a follow-up unit if fable is the
writer — that fix is NOT this unit).

STOP: lock held; the client cannot log in; the DB row cannot be read.

---

# Review (slot1, `task/T331-z-drift`, 2026-09-04)

**Measurement only, no code change, as contracted. Full evidence, tables,
packet excerpts and screenshots: `/mnt/projects/fable/review/T331/README.md`.**

## Verdict — B049 STANDS; fable is the writer, not the server

The compounding T311 Leg B reported closed is **still live on the graphical
path**. The official Titanium client, measured on the same character at the same
spot in the same session, re-persists its z **unchanged** three times running.

**Named writer:** `eqt::bridge::BridgeComponent::OnGraphicsMovement`,
`src/client/bridge/bridge_component.cpp:717`, which clears
`zAwaitingFrameConversion` **without** performing the paired conversion. Its
comment asserts *"A z the RENDERER produced is definitively in the render frame,
whatever state the self-spawn left behind (T311)"* — and that premise is false at
zone-in: the renderer did not derive the z, it was **seeded** with the raw wire
value and echoes it straight back.

## The three tables

`Testchar` (id 65), Human male race=1 size 6, qeynos2 `(-74, 428)`, standing
still, camped out cleanly. z is `character_data.z`.

| cycle | fable `--no-graphics` | fable with graphics | official Titanium client |
|---|---|---|---|
| start | 3.751 | 3.75 | 3.751 |
| 1 | 3.75 | **4.38063** | **3.751** |
| 2 | 3.75 | **5.00563** | **3.751** |
| 3 | 3.75 | **5.63063** | **3.751** |
| step | 0 | **+0.625/login** | **0** |

The oracle's `/loc` read `428.00, -74.00, 3.75` on all three cycles.

**The headless path cannot exercise the defect and must not be used to clear
it.** With no renderer there is no `PlayerPositionChanged`, so
`OnGraphicsMovement` never runs, the flag is never cleared, and the send path
passes the stored wire z through verbatim — accidentally correct. That is the
same blind spot that let T311 close with a green suite, and it is why T311's
three-login regression test does not fail today.

## The arithmetic, cross-checked against two prior independent observations

Measured from the run log: `collisionZOffset = 3.11937`, `0.625 * size = 3.75`.
Each login re-applies `renderZToWireZ` to a z that is already a wire value:

```
z_next = quantise8(z_persisted) + (3.75 - 3.11937)
       = quantise8(z_persisted) + 0.63063
```

`quantise8` is the spawn packet's 1/8-unit position encoding — that is why the
observed step reads 0.625 rather than 0.63063, and it is not a defect. The model
predicts all three measured cycles to the last digit, and reproduces two
observations it was never fitted to: T311 Leg B's `4.25 -> 4.880627`, and T328
Leg B's `3.751 -> 11.8806`, which is exactly **13 logins**, not 13 × a mystery
constant.

## The discriminator that names the writer

Over the whole graphical run there is exactly **one** distinct value in each of

```
setPlayerPosition: pos=(428.00,-74.00,3.75)
client_pos=(428.00,-74.00,3.75)
```

The correct deferred conversion (`onPlayerModelGeometryChanged`,
`bridge_component_intents.cpp:132-146`) would have called `SetPosition` with
`wireZToRenderZ(3.75, 3.11937, 6) = 3.12`. **No 3.12 appears anywhere in the
log**, so its guard had already been falsified — the flag was cleared elsewhere,
by the only other site that clears it.

The chain, in timestamps (cycle 1): PlayerProfile delivers the wire z 3.75
(09:33:36.498) -> renderer seeded with it, no model yet (09:33:37.397) -> four
correct verbatim sends (to 09:33:43.520) -> `createEntity[Testchar]`,
`collisionZOffset=3.12` (09:33:43.958) -> **`[ZONE-IN] setPlayerPosition` fires
AGAIN with the same raw wire z, after the model loaded** (09:33:44.627, .841) ->
first wrong send, render z unchanged (09:33:45.034).

This is structurally the same defect T311 diagnosed — "the two conversions
disagree across TIME" — one level up. T311 fixed the race between the self-spawn
and the model load; it did not cover the **zone-load snapshot re-seed**, which
re-introduces an un-converted wire z after the model is already present.

## Left for the fix unit (NOT this one)

- Whether to convert at the zone-load snapshot hand-offs (`application.cpp:1785`
  / `:1898`), to make `OnGraphicsMovement` refuse to clear the flag while the
  renderer's z is still the seeded value, or to make the geometry conversion
  authoritative and idempotent, is a design call and should be measured, not
  argued.
- The regression net must run **with a renderer present** (or replay the
  zone-load snapshot). A headless three-login loop is green against this defect.

## Deviations, flagged

1. **The plan's step 3 (`--no-graphics`) is insufficient on its own** and was
   run as specified, then supplemented with three graphics-enabled cycles. Had
   the unit stopped at step 3 it would have reported "no drift" and wrongly
   closed B049. Both arms are reported.
2. **Restore target.** The plan's step 6 names `zone_id=36, x=-77.9232,
   y=21.9086, z=3, h=190`. `Testchar` was **not** there on arrival — it was at
   the T311 canonical qeynos2 pose (`zone_id=2, x=-74, y=428, z=3.751, h=160`),
   exactly as `EXECUTOR_PROTOCOL.md` warns. It was restored to the pose it was
   **found** in, following T311 Leg A's precedent: that pose is the surveyed spot
   this arc is calibrated against.
3. **Character-select slot 1 is `Testcaster`, not `Testchar`** — T328's finding
   confirmed. `enter-world.sh` hard-codes slot 1; the control used a slot-2
   variant (`review/T331/scripts/t331_enter_world_slot2.sh`) and the selection is
   evidenced on screen before Enter World.

## Server and host state

eqemu locked atomically, `up.sh` (arena) + a `qeynos2` zone process, `down.sh`
exit 0, **lock deleted**. `Testchar` restored to its found pose; `Testcaster` and
`Testmelee` never written. The Wine client was quit from character select;
`~/eq-oracle/client` was not modified. The only DB write in the unit was one
`UPDATE` returning `Testchar` to its found pose before the control arm, so both
arms started identical.
