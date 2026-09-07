# T433 — doors, lifts, switches and traps: the census and the oracle (B420, Will 2026-09-07)

Measurement + survey; eqemu lock for the oracle capture (atomic; must be
ABSENT). Read B420, `src/client/graphics/door_manager.cpp`,
`include/client/door_state_manager.h`, the `OP_SpawnDoor`/`OP_MoveDoor`/
`OP_ClickDoor` handling (T374/T376), eqemu's `doors` table and its door
code (`/mnt/projects/akk-stack/code/zone/doors.cpp`, `common/eq_packet_structs.h`
`Door_Struct`/`MoveDoor_Struct`: `opentype`, `doorid`, `incline`, `size`,
`invert_state`, `lift`, `dest_zone`, `trigger_door`, `trigger_type`), and
the oracle rig (`review/T426/scripts`, `review/T405/scripts`).

## Leg A — the census (no lock)

1. Over the live `doors` table (read-only SQL, mariadb only — take the
   lock; T355's read-only precedent): count doors per zone; distribution
   of `opentype` values; every distinct name prefix (`ELEV*`, `LIFT*`,
   `TRAP*`, `SWITCH*`, `LEVER*`, `TELE*` …); which rows have `dest_zone`,
   `trigger_door`, `lift` set. Table in the Review. Name the zones with
   lifts (gfaydark, kelethin, …), traps, switches, teleporters.
2. What fable does with each `opentype` today: read `door_manager.cpp`
   — which opentypes animate (rotate/slide/lift), whether any door mesh
   is registered as a collision/walkable surface, what happens on
   `OP_MoveDoor` for a lift. Table: opentype → fable behaviour → gap.
3. eqemu's own door semantics per opentype (its `doors.cpp` has the
   client-side expectations encoded in comments and the `lift` handling)
   — quote.

## Leg B — the oracle (lock)

4. Official client under Wine in gfaydark at a lift: video the lift
   cycle (click, rise, top, descent), the player standing on it and
   riding up (position packets while riding — the client sends its own
   position, so a walkable surface means the client moves the player
   with the platform), and one trap zone if a Titanium-era one is
   reachable by `#zone` (name it from the census). Frame-step: the
   lift's travel time, speed profile, whether the player is carried.
5. Fable at the same lift: what happens on click (does it animate?), and
   what happens to a player standing on it (falls through? stays?).
   Video both.

## Leg C — the plan

6. Write the implementation units as a section in the Review: (a) door
   animation per opentype matched to the oracle's timing; (b) lifts as
   walkable moving surfaces (collision + carry the player); (c) switches
   → trigger_door chains; (d) traps → the damage comes from the server
   (`OP_Damage`?) — what the client must do is animate and play sounds;
   (e) teleporters (`dest_zone`). Each with its oracle evidence and
   STOPs. Board them (B421+). Strike nothing; annotate B420 with the
   census.

Restore server values (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the official client cannot reach a lift under the
rig (report the blocker).
