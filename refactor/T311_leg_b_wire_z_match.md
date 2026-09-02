# T311 Leg B — send what the official client sends: `groundZ + 0.625 * size`

**Slot: slot1. Branch `task/T311-legB` cut from `fable`. Owns eqemu when it
takes the live leg (the lock file is FREE — take it atomically, release it).
Needs a two-client live session. No Pi.**

Executes **Will's ruling 3 (2026-09-01): the official client is the golden for
player position; willeq is updated to send what it sends.** Leg A measured what
that is. Read `refactor/T311_oracle_position_and_death_capture.md`'s Review IN
FULL before touching code — it is the contract, and it contains more than the
formula.

## What Leg A established, on the wire

```
wireZ = groundZ + 0.625f * size          // = 0.2f * size * 3.125f
```
plus a +0.001..0.003 collision epsilon. **No race term.** Canonical packet
(Human male, size 6, qeynos2 `(-74,428)`, server raycast `Best Z = -0.00`):
`z_pos@26 = 64 10 70 40 = 3.75100040435791`, against fable's `3.00`.

Trustworthy because Titanium's `DECODE(OP_ClientUpdate)`
(`titanium.cpp:2633-2653`) is `IN()`-only — no arithmetic — so the server-side
hexdump IS the client's transmitted value.

**Size is exactly linear** (`#size 3/6/12` → 1.8780 / 3.7510 / 7.5010;
Barbarian size 7 → 4.376). **Race is not a term:** under a Dwarf illusion the
server's `Fixed Z` moved to 28.84 (EQEmu's Dwarf constant 0.7) while the client
kept sending 31.75. So the client behaves as if every model used **3.125**, and
the agreement with `Mob::GetZOffset()` is a coincidence of EQEmu's default —
**do not reimplement `GetZOffset()`; implement the constant.**

## The structural point — two variables, not one

`0.625 * size` is **no landmark of the shipped mesh** (Human male origin-to-feet
3.004, half-height 3.339, crown 3.674). So the wire z and the render node origin
must become **separate variables**:

```
nodeOriginZ = groundZ + collisionZOffset          // unchanged (T310 Leg B)
wireZ       = nodeOriginZ - collisionZOffset + 0.625f * size
```

Do not "simplify" one into the other; T310 Leg B's `collisionZOffsetFromBounds()`
is measured from the mesh and is correct for rendering. Conflating them is the
exact mistake this arc has been unwinding.

## Sites (measured at `e68f8aa5` — re-verify against your tip)

**Outbound:** `src/client/graphics/renderer_player_physics.cpp:768`;
**`src/client/eq_bridge.cpp:754` — Leg A names this the cleanest conversion
point**; `src/client/eq_movement.cpp:1101`, whose comment at `:1100` is now
provably wrong and must die with the change.

**Inbound (the mirror):** `src/client/eq_entities.cpp:416` and `:1195`;
`src/client/spell/spell_cast_checks.cpp:53-54`;
`src/client/eq_movement.cpp:1741-1742`;
`src/client/graphics/entity_renderer.cpp:2227` (**the correct model — copy it**);
`include/client/graphics/entity_renderer.h:24-52` (the convention note, update it).

⚠ **Leg A flags the inbound mirror as an INFERENCE, not a measurement.** It is
to be settled by this leg's two-client acceptance, not assumed. If the two-client
result contradicts the mirror, report that and STOP rather than forcing symmetry.

## Also in scope (small, same subsystem, found by Leg A)

- `eq_movement.cpp:1741` computes `headZ` and **never uses it** —
  `findRegionForPoint` on the next line takes `PosZ()`. A live dead local in
  water detection. Delete it or use it; say which and why.
- **Wire heading is `heading × 4`** (the oracle's `#loc 480` rode as 1920).
  Leg A did NOT check whether fable packs the same scaling at
  `eq_movement.cpp:1113`. **Check it.** If it disagrees, that is a second live
  wire defect — report it; fix it here only if it is a one-liner, otherwise
  board it.

## Explicitly NOT in scope

**The death sequence.** Leg A answered it (see below) and it needs no code in
this leg. Do not implement a dead-state position policy here.

## Acceptance

- **Two-client live** (memory `two-client-live-testing` — single-client
  verification is circular): an OBSERVING second instance sees fable's player at
  the same height as the official client at the same surveyed spot. Screenshots
  + the server's `#loc` for both, to `/mnt/projects/fable/review/T311/legB/`.
- **A unit test pinning the expression** against size inputs — including a
  non-default size, since size-linearity is the measured property.
- A packet-level check: fable's own `OP_ClientUpdate` z at the canonical
  qeynos2 spot reads `3.751`, not `3.00`.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- `check_file_sizes.sh --files` no new FAILs.

## Territory

`src/client/eq_bridge.cpp`, `src/client/eq_movement.cpp`,
`src/client/eq_entities.cpp`, `src/client/spell/spell_cast_checks.cpp`,
`include/client/graphics/entity_renderer.h`, plus your test.
⚠ `src/client/graphics/renderer_player_physics.cpp` and
`src/client/graphics/entity_renderer.cpp` are **T312's territory** (slot2, the
thread-safety unit) — if T312 has not merged when you get there, coordinate
through the orchestrator rather than editing them concurrently.
