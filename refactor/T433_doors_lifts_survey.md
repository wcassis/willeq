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

---

# Review (T433, executed 2026-09-07, slot3, base `a47f31db`)

Survey + measurement unit. **No production code changed.** One STOP was hit
(Leg B item 4, the oracle lift cycle) and is reported below with the evidence
that establishes it as a rig limit, not a guess.

## Leg A.1 — the census

Source: the live `peq.doors` table, read-only SQL under the eqemu lock
(T355 precedent), mariadb only. Raw dumps in `review/T433/data/`.

**Totals.** 19,249 door rows over 429 zones. Restricted to the era Will named
(launch -> Velious, i.e. `zone.expansion <= 3`, `zone.version = 0`):
**6,099 rows over 140 zones**.

### opentype distribution

| opentype | all rows | era rows | what fable does with it today |
|---|---|---|---|
| 0 | 2,863 | 1,678 | rotates 90 deg over 0.5 s |
| 55 | 3,104 | 417 | animation runs but rotates by **0 deg** |
| 53 | 2,159 | 926 | **not created at all** (INVISIBLE_OPENTYPES) |
| 58 | 1,744 | 616 | 0 deg |
| 5 | 1,321 | 395 | rotates 90 deg over 0.5 s |
| 54 | 496 | 384 | **not created at all** |
| 25 | 1,226 | 4 | 0 deg |
| 57 | 662 | 34 | 0 deg |
| **59** | **563** | **35** | **0 deg — this is the lift/elevator type** |
| 76 / 77 | 544 / 505 | 110 / 214 | 0 deg |
| 1 / 2 / 6 / 7 | 460/444/440/322 | 160/262/60/156 | 0 deg |
| 40 (levers) | 288 | 34 | 0 deg |
| 105 | 196 | 7 | continuous Z spin, 42.35 deg/s |
| 100 | 74 | 66 | continuous Y spin, 42.35 deg/s |
| 71 | 67 | 18 | 0 deg |
| 120 | 16 | 2 | 0 deg |
| 50 | 2 | 0 | not created at all |
| 56 | 6 | 6 | rotates 90 deg over 0.5 s |
| (49 further opentypes) | | | 0 deg |

Sixty-five distinct opentypes exist; **fable gives distinct treatment to
exactly seven of them** (0, 5, 56 rotate; 100, 105 spin; 50, 53, 54 vanish).
Every other opentype takes the default path, and because `incline` is 0 on
almost all of them `calculateOpenHeading()` returns the closed heading, so the
"animation" interpolates from a value to itself: **the door changes state and
nothing on screen moves.**

### name prefixes and functional classes

Whole table (all 19,249 rows) grouped by what the name says:

| class (name match) | rows | opentypes seen |
|---|---|---|
| SWITCH | 373 | 0,5,6,25,40,54,55,57,58,59,66,70,76,77,109,156,159 |
| TELE | 359 | 0,55,57,58,59,156 |
| LEVER | 175 | 0,40,55,76 |
| LEVATOR (elevator) | 71 | 40,54,59 |
| TRAP | 57 | 57,58,71,120 |
| PLAT | 54 | 55,59 |
| ELE (other) | 50 | 58,59,109 |
| LIFT | 35 | 54,55,57,58,59 |
| BLADE/SAW/SPIKE | 16 | 56,59,105,156 |
| everything else | 18,059 | all 65 |

**The name is not a reliable classifier and the opentype is.** `OBJ_ELEVATOR`
(35 rows) is opentype 59; so is `OBJ_LIFT` (5), `OBJ_LIFT_NATURE` (7),
`CMLIFT101`, `MISTLIFT`, `PALIFT101` and `FAYLEVATOR`. But 52 rows named
`OBJ_BLUETORCHSWITCH` are *also* opentype 59, and 16 `FAYLEVATOR` rows in
gfaydark are opentype **54**. The 4-char prefix census (331 distinct prefixes,
`data/name_prefixes_all.txt`) is dominated by `OBJ_` (5,879), `CRAT` (2,223),
`TAKD` (1,216), `DOOR` (947), `OBP_` (603), `BIG_` (568), `SHOJ` (512).

### the fields that matter

| field | rows set | notes |
|---|---|---|
| `dest_zone` (teleport) | 1,370 (era 260 at opentype 58, 95 at 0, 31 at 5) | 691 at opentype 58, 416 at 57 |
| `triggerdoor` (chains) | 591 (era 457) | `triggertype` 0 on 558, 1 on 29, 255 on 4 |
| `keyitem` | 114 | |
| `lockpick` | 191 | |
| `guild` | 0 | |
| `door_param` | non-zero on every real lift | **hypothesis, not measured: the lift's travel distance in EQ z-units.** eqemu forwards it to the client verbatim and never interprets it (`doors.cpp:870`), so only the official client can settle it — see the STOP |

**There is no `lift` column** in `peq.doors` and **no `lift`, `trigger_door` or
`trigger_type` field in the wire `Door_Struct`.** The plan named those as
struct fields; they are not. `Door_Struct` (Titanium, 80 bytes,
`common/eq_packet_structs.h:2899`) carries only `name[32]`, `yPos`, `xPos`,
`zPos`, `heading`, `incline`, `size`, `doorId`, `opentype`, `state_at_spawn`,
`invert_state` and `door_param`. Trigger chains and destination zones are
**server-side only**: the server resolves them and the client only ever sees
`OP_MoveDoor{doorid, action}` or a zone/position change. **Deviation from the
plan, flagged not absorbed.**

### the zones with lifts / traps / teleporters (era)

**Lifts (opentype 59, era, `door_param` > 0 = real travel):**

| zone | doorid | name | door_param | invert | triggerdoor | pos (x,y,z) |
|---|---|---|---|---|---|---|
| gfaydark | 69 | FAYLEVATOR | **68** | 1 | - | 350.014, 137.463, 2.1582 |
| gfaydark | 73 | FELE2 | 1 | 0 | **69** | 238.669, 128.170, 8.9092 |
| gfaydark | 74 | FELE2 | 1 | 0 | **69** | 271.107, 152.396, 77.4681 |
| gfaydark | 77 | FAYLEVATOR | **98** | 1 | - | 221.448, 872.302, -27.6523 |
| gfaydark | 78 | FELE2 | 1 | 0 | **77** | 236.218, 950.314, 77.9202 |
| gfaydark | 79 | FELE2 | 1 | 0 | **77** | 212.270, 983.996, -20.4945 |
| gfaydark | 80 | FAYLEVATOR | **69** | 1 | - | -136.173, -88.6519, 2.4082 |
| gfaydark | 81 | FELE2 | 1 | 0 | **80** | -146.716, 24.2785, 9.58386 |
| gfaydark | 82 | FELE2 | 1 | 0 | **80** | -121.795, -10.2559, 78.1702 |
| paineel | 25 | PALIFT101 | 69 | 0 | - | 475.842, 629.576, -98.2159 |
| citymist | 70 | CMLIFT101 | 72 | 0 | - | 439.886, 0.00963, -1.998 |
| mistmoore | 3 | MISTLIFT | 15 | 0 | - | 357.845, -243.779, -155.937 |
| acrylia | 27 | ACDOORS310 | 61 | 0 | - | 803.158, -374.993, -91.7478 |
| echo | 7 | EMELVTOR301 | 38 | 0 | - | 275.058, -1306.43, -42.248 |

The remaining 21 era opentype-59 rows have `door_param` 0 or -1 and are
decorative (grobb `GRBSIGN404`, `HFCANBALL400`; acrylia `ACTORCH301` x8;
arena `OBP_TOWER`). **The three Kelethin lifts are gfaydark 69 / 77 / 80,
and the `door_param` values 68 / 98 / 69 are in the same range as (but not
equal to) the vertical travel each needs**, which is why travel-distance is
the working hypothesis and not a finding (69: z 2.16 -> the top button at 77.47, i.e. 75; 77:
z -27.65 -> top button at 77.92, i.e. 105; 80: z 2.41 -> 78.17, i.e. 76). The
`FELE2` rows are the call buttons at the two ends, each with `triggerdoor`
pointing at its lift and `door_param` 1.

Post-Velious lifts, for completeness: mansion (137 opentype-59 rows),
frostcrypt (52), monkeyrock (50), yxtta (27), gunthak (26), mechanotus (25),
pillarsalra (24, `OBJ_LIFT_NATURE`), moors (21, `OBJ_ELEVPADA`), somnium (20),
shipworkshop (17).

**Traps (era):** soldunga `SPEARDOWN` x2 (opentype **120** — the only era 120
rows in the game); sebilis `SBDOOR103` x14 and bazaar `BAZIGATE302/303` x4
(opentype **71**); steamfont `WMBLADE` x3 and airplane `AIRWMBLDS` x2
(opentype **105**, the windmill blades — already spun by fable). There is no
era row whose name says TRAP. `OBJ_SPIKE_TRAP` (opentype 120, thuledream) and
`TRAPDOOR` (opentype 57, delveb/stillmoona) are post-Velious.

**Teleporters (era):** 260 rows at opentype 58, 95 at opentype 0, 31 at
opentype 5, 19 at 155, 17 at 77, 16 at 54. The recognisable families are
`POKTELE500` (30 rows across 25 zones), `POJTELE500` (15) and `TMTELEPORT800`
(22+9) — all Planes-of-Power-era and out of scope under the era rule; the era
teleport doors that matter are the plain opentype-0/5 zone doors that carry a
`dest_zone`.

**Switches / levers (era):** 34 rows at opentype 40 (`PLEVER` x16 in paw,
`LEVER1` x8 in droga/soldunga, `OBJ_LEVER_NARROW`, `OBJ_ELVTR_LEVER`), plus
the 457 era rows with a non-zero `triggerdoor`.

## Leg A.2 — what fable does with each opentype today

Read from `src/client/graphics/door_manager.cpp` (1,394 lines) and
`src/client/graphics/player/renderer_player_collision.cpp`.

| opentype | fable behaviour | gap vs. what the data implies |
|---|---|---|
| 50, 53, 54 | `createDoor()`/`registerDoor()` return early: **no mesh, no scene node, no collision, no state**. 926 era `CRATEG` rows (53) and 384 era rows (54, incl. 16 gfaydark `FAYLEVATOR` and 115 city `DOOR1`) are simply absent from the world | opentype 54 doors are demonstrably live — the server logged `door open and timer triggered door_id [93] open_type [54]` repeatedly during this session. Whether they are *meant* to be invisible is **unmeasured**; the blanket skip is a guess in the code with no citation |
| 0, 5, 56 | `calculateOpenHeading()` substitutes `incline = 128` (90 deg) when `incline == 0`; `update()` lerps the pivot's Y rotation over `1/ANIM_SPEED = 0.5 s` | the 0.5 s is not measured against anything |
| 100 | continuous Y spin, `SPIN_SPEED = 42.35 deg/s`, never stops, ignores open/close | unmeasured constant |
| 105 | continuous Z spin, same speed | unmeasured constant |
| **59 (lifts)** | falls through to the default: `openHeading == closedHeading`, so `update()` animates a rotation of **exactly 0 degrees**. The mesh never moves, never translates, and `isAnimating` is true for 0.5 s each way | **no translation code exists anywhere in the renderer.** `door_param` is not read by fable at all — `DoorState::doorParam` is stored and never consumed outside logging |
| 40, 55, 57, 58, 66, 70, 71, 76, 77, 120, +49 others | same 0-degree default | every one of them is a visible no-op |

Two further findings from the same read:

1. **Doors have no collision at all, and the code that would give them
   collision has no writer.** `DoorCollisionData` (`renderer_types.h:414`,
   with `closedTriangles` / `openTriangles` / `hasCollision`) and the
   `ctx_.doorCollisionData` map are read by `rayTestDoorsAndObjects()` — which
   *is* wired into both `checkCollisionBspFiltered()` (walls) and
   `findGroundZBspFiltered()` (standing surfaces) — but a whole-tree grep over
   `src/ include/ tests/ tools/ benchmarks/ docker/` finds **only two
   `clear()` calls, one `.size()` in a log line, one context binding and the
   one read. Nothing ever inserts.** The map is always empty, so every door in
   every zone is walk-through and non-standable. `hasCollision` is likewise
   never assigned; its comment ("false for invisible, teleporter, spinning,
   lifts, hazardous") describes an intent that was never implemented.
   `DoorManager::getDoorSceneNodes()` is documented "for collision detection"
   and its only production caller is a **material swap** in
   `diagnostics_debug.cpp:296`.
2. `door->isAnimating` disables a door's collision entirely ("walk-through
   during animation"). For a hinged door that is defensible; **for a lift it
   is exactly backwards** — a lift is animating for its whole ride, which is
   precisely when the player must be standing on it.

## Leg A.3 — eqemu's own door semantics, quoted

`/mnt/projects/akk-stack/code/zone/doors.cpp`, `#define OPEN_DOOR 0x02`,
`CLOSE_DOOR 0x03`, `OPEN_INVDOOR 0x03`, `CLOSE_INVDOOR 0x02` — so **the wire
action byte means "open" or "close" only after `invert_state` is applied**, and
fable's `ZoneProcessMoveDoor` reads `is_open = (action == 0x03)` with no
inversion. For the three Kelethin lifts (`invert_state = 1`) that reading is
inverted.

`opentype 58 never latches`: the open/close decision is
`if (!IsDoorOpen() || (m_open_type == 58))` at five separate sites, i.e. an
opentype-58 door emits OPEN on *every* click and never CLOSE.

`opentype 57 and 58 are the teleport types`:
```
	// teleport door
	if (EQ::ValueWithin(m_open_type, 57, 58) && HasDestinationZone()) {
```
followed by `sender->MovePC(...)` — so a teleport door's effect is a **server
push, not a client action**; the client's only job is the animation and the
sound.

`trigger chains are server-side`:
```
	if ((GetTriggerDoorID() != 0) && (GetTriggerType() == 1)) {
		Doors *trigger_door_entity = entity_list.FindDoor(GetTriggerDoorID());
		if (trigger_door_entity && !trigger_door_entity->triggered) {
			triggered = true;
			trigger_door_entity->HandleClick(sender, 1);
```
The client sends one `OP_ClickDoor` and receives *two* `OP_MoveDoor`s. Nothing
about the chain is client knowledge.

The load-bearing comment for lifts is in `zone/client_packet.cpp:4681`:
```
	// distance gate this because some doors are client controlled and the client
	// will spam door click even across the zone to force a door back into desired state
```
i.e. **eqemu's own authors record that the elevator's motion is client-owned**;
the server only relays the state byte. That is consistent with `door_param`
being forwarded verbatim (`doors.cpp:870`, `e.door_param =
static_cast<uint8>(GetDoorParam())`) and never interpreted server-side.

Finally, `Mob::HandleDoorOpen()` (`mob.cpp:8478`) — the NPC auto-open path —
contains `if (d->GetDoorParam() > 0) { continue; }`, so **an NPC never opens a
lift**. This closed off the only click-free way to make the server emit a
`MoveDoor` for a lift (see the STOP below).

## Leg B — the oracle

Rig: official Titanium client under Wine on Xvfb `:99`, `willeq_rogue` /
`Oraclerogue` (status 250, GM), gfaydark, `#weather 0` + `#set time 11 0`,
1024x768. Two rig fixes were needed and are recorded in
`review/T433/README.md`: `enter-world.sh`'s "Enter World" click at (180, 624)
is **below the button**, which is at (180, 609) at this geometry (screenshot
`probe/T433_oracle_charselect_1024x768.png`); and slot 1 on `willeq_test` is
**Testcaster**, not Testchar.

### What was measured

The Kelethin lift, gfaydark doorid 69 (`FAYLEVATOR`, opentype 59,
`door_param` 68, `invert_state` 1), is a large flat wooden platform sitting on
the forest floor at the base of the tree, with two posts:
`probe/T433_aim_a384_1024x768.png` and `probe/T433_near_n340_1024x768.png`
(player at 340/320, 137, ground, heading 384). Its call buttons (`FELE2` 73 and
74) are ~110 units away at the tree itself, one at z 8.9 and one at z 77.5:
`probe/T433_near_b73n*_1024x768.png`. A plain Kelethin gate door (`FAYDOOR`,
opentype 0) at the platform level is in
`probe/T433_fd_fd192_1024x768.png`.

**The oracle's own `/loc` at `#goto 320 137 8` returned `137.00, 320.00,
-0.09`** — the official client resolves that spot to the forest floor at
z ~= 0.

### STOP — the lift cycle could not be triggered under the rig

The plan's STOP is "the official client cannot reach a lift under the rig".
The lift was reached; what could not be done is **click it**. Evidence, not
inference:

- eqemu logs every arriving `OP_ClickDoor` before any gating
  (`client_packet.cpp:4671`, `LogDoors("Door [{}] client handle, client
  distance from door [{:.2f}]")`). `#logs set gmsay 10 3` routes that to the
  client's own chat panel, live.
- With that on, **six different synthetic click styles** (plain `xdotool click`;
  press/hold/release in place; double-click; right-click; the project's
  `jclick` with the load-bearing 1px move; hover-then-click) produced **zero
  `client handle` lines** — on the lift *and* on an ordinary opentype-0
  `FAYDOOR` at 3 units' range:
  `probe/T433_oracle_clickstyles_doorslog_6styles.png`.
- A 30-point click grid (6x5) over the whole visible platform, recorded,
  produced no motion and no log line:
  `video/T433_grid_lift69.mkv`, sheet
  `probe/T433_oracle_lift69_clickgrid_framesheet.png`.
- The log was demonstrably **live** throughout: NPC-driven
  `[Doors] [ForceOpen] door_id [n] starting timer` / `[Process] door open and
  timer triggered door_id [n] open_type [0|54]` lines streamed continuously
  from guards walking through the Kelethin gates.
- World clicks *do* reach the client — an earlier click with an item on the
  cursor raised the client's own "ARE YOU SURE you wish to drop your Spider
  Venom on the ground?" dialog. So the failure is specifically the client's
  3D door pick under synthetic input, not lost input.
- The click-free route is closed by the server: `Mob::HandleDoorOpen()` skips
  every door with `door_param > 0`, which is every lift and every `FELE2`
  button, so no NPC will ever open one; and there is no GM command that opens a
  door (`#door` is a geometry editor with no open/toggle subcommand).

**Therefore: lift travel time, speed profile, and whether the player is
carried are UNMEASURED.** Any number for them would be invented. The two
plausible ways forward, for the orchestrator to choose between, are (a) drive
the official client's mouse through a path that the client accepts — most
likely by moving the pointer with real motion events over several frames and
releasing on the door, or by finding the client's own door-use keybind in
`eqclient.ini` — or (b) a one-line Perl quest script in the akk-stack content
tree calling `$entity_list->GetDoorsByDoorID(69)->ForceOpen($client)`, which
is server *content* rather than server code and needs Will's ruling before an
executor touches it.

Videos kept as the negative evidence:
`video/T433_oracle_lift69_beside.mkv` (835 frames @ 10 fps, 83.5 s — the click
that hit the drop dialog), `video/T433_oracle_lift69_beside2.mkv` (835 frames,
clean cursor, no motion; sheet
`probe/T433_oracle_lift69_beside2_framesheet.png`),
`video/T433_oracle_lift69_close.mkv` (783 frames, click at 10 units).

## Leg B.5 — fable at the same lift

Same server, same character, same `#goto`, `build-gles2/bin/willeq` at
`a47f31db`. Two runs, `logs/fable_session_lift69.log` and `…_lift69b.log`.

| what | oracle | fable |
|---|---|---|
| `#goto 340 137 6` then `/loc` | `137.00, 320.00, -0.09` (forest floor) at the neighbouring probe | **`137.0, 340.0, 77.0`** — 71 units too high, standing on the Kelethin platform above |
| `#goto 350 137 8` then `/loc` | not run | **`137.0, 350.0, 77.0`**, twice, both runs |
| view at that pose | open forest floor, lift platform ahead (`probe/T433_aim_a384…png`) | camera jammed inside a tree trunk (`fable/T433_fable_lift69b_A_beside_lift_1024x768.png`, `…_C_on_lift_settled_…png`) |
| the lift mesh | drawn, at the foot of the tree | never in frame, because the player is 71 units above it |
| interacting | click (blocked, above) | `Interact` is bound to **U** (`hotkey_manager.cpp:519`); three presses next to the lift produced no `SendClickDoor`, no `OP_MoveDoor` and no visible change (`video/T433_fable_lift69_ukey.mkv`) — expected, since the player is nowhere near the lift |

The z discrepancy is **reproducible across both runs at two different
coordinates**, on the same server that put the official client at z ~= 0, so it
is fable's ground resolution and not the server's placement. It is boarded
separately (B424) because it blocks any further lift work in gfaydark: an
executor cannot stand next to the Kelethin lift in fable today.

fable's own door bookkeeping is otherwise correct at this zone: it logs all
three `FAYLEVATOR` opentype-59 rows and all six `FELE2` rows with the right
positions and `invert=1` on the lifts, and it prints the server's own
`[Doors] [ForceOpen]` gmsay lines. Note fable's `[ENTITY] Door n:` log line
prints the position as **(y, x, z)** — the wire order — which reads as
transposed against the DB; that is the log line's format, not a bug.

## Leg C — the implementation plan

Boarded as **B421**-**B426**. Each unit below names its oracle evidence and its
STOPs. **B421 is the gate**: units 2-5 all depend on being able to see the
official client animate a door, and that is exactly what this unit could not
do.

### Unit 1 (B421) — make the official client clickable, then measure
Prerequisite for everything else. Deliverable: one recorded lift cycle
(gfaydark 69) with per-frame platform Z, giving travel time, speed profile
(constant velocity vs. eased), dwell at the top, and whether a player standing
on the platform rises with it. Then the same for one opentype-0 door
(rotation duration), one opentype-40 lever, and one opentype-100/105 spinner
(to confirm or refute fable's invented 0.5 s and 42.35 deg/s).
**STOP:** if no input method works, do not proceed by inference — bring the
Perl-quest option to Will as a decision.

### Unit 2 (B422) — lifts: translate the mesh by `door_param`
`DoorVisual` gains a translation animation; `DoorState::doorParam` becomes the
travel distance in EQ z-units; open = at the far end, closed = at spawn, with
`invert_state` applied to the wire action byte (which fable currently ignores —
see Leg A.3). Applies to opentype 59 with `door_param > 0`; opentype 59 with
`door_param` 0 or -1 is decorative and must keep today's behaviour.
**STOP:** the direction and the timing come from unit 1's video, never from
the `door_param` arithmetic alone.

### Unit 3 (B423) — doors as real collision, lifts as moving walkable surfaces
Two halves. (a) Give `doorCollisionData` its missing writer at zone load so the
existing, already-wired ray tests stop reading an empty map — this is a
standalone class-A fix and is boarded on its own as **B425**. (b) Once a lift
translates, its triangles must be re-baked (or transformed) each frame while it
moves, `isAnimating` must stop suppressing collision for lift opentypes, and
the player standing on the platform must be carried: fable owns player
position and sends it to the server, so "carried" means fable adds the
platform's per-frame delta to the player's Z while the player's feet are on it.
**STOP:** whether the official client carries the player, or whether the player
simply keeps colliding with a rising surface, is unit 1's measurement.

### Unit 4 (B426) — switches, levers and trigger chains
Client work is animation + sound only: the chain itself is server-side
(quoted above), so a click on a `FELE2` produces two `OP_MoveDoor`s and fable
must simply animate both. Requires opentypes 40, 55, 59-button, 66, 76, 77 to
stop being 0-degree no-ops. **STOP:** each opentype's motion (rotate about
which axis, slide along which, how far) is per-opentype and must come from the
oracle, one opentype at a time.

### Unit 5 (B426, second half) — traps
Era traps are soldunga `SPEARDOWN` (opentype 120) x2, sebilis `SBDOOR103`
(opentype 71) x14, and the already-spinning `WMBLADE`/`AIRWMBLDS`
(opentype 105). **The damage is entirely the server's**: eqemu's trap damage
path reaches the client as ordinary `OP_Damage`, which fable already handles.
The client work is the animation and the sound. **STOP:** confirm against the
oracle that opentype 71 and 120 animate at all before building anything.

### Unit 6 (B426, third half) — teleporters
Nothing to build beyond animation: `MovePC` is a server push. The era-relevant
set is the opentype-0/5 zone doors carrying a `dest_zone`, which fable already
animates as ordinary doors. Verify only.

### Also boarded out of this survey (not lift work)
- **B424** — fable resolves ground 71 units too high at gfaydark
  (350, 137); the official client puts the same spot at z ~= 0.
- **B425** — `doorCollisionData` has no writer; every door in the game is
  walk-through and non-standable.
- **B427** — opentypes 50/53/54 are dropped on the floor with no measurement
  behind the decision; 1,310 era rows are affected, and the server visibly
  opens and closes opentype-54 doors.
- **B428** — `ZoneProcessMoveDoor` reads `is_open = (action == 0x03)` without
  applying `invert_state`; the three Kelethin lifts and every other
  `invert_state = 1` door are inverted.

## Verification

| step | result |
|---|---|
| production code changed | **none** — survey unit |
| build / ctest / gates | **not run**: no source file touched (the only tree edits are this plan's Review, `refactor/BOARD.md` and `review/T433/`) |
| eqemu lock | taken atomically as `T433 2026-09-07` (noclobber, exit 0); deleted at the end |
| server values changed | `character_data` id 81 (`Oraclerogue`) moved to gfaydark for the capture; id 67 (`Testcaster`) moved by a `#goto` during the first, mis-targeted recon |
| restored | both set back with the world DOWN and verified by `SELECT` — `data/char_snapshot_before.txt` vs `data/char_snapshot_after_restore.txt` are identical for ids 65-68 and 81 |
| runtime-only changes | `#logs set gmsay 10 3` (zone-process memory only, gone with the process); `#weather 0`, `#set time 11 0` |
| clients | official client `/camp`ed cleanly then killed; both fable runs `/camp`ed then `/quit` |
| server | `down.sh` run twice (once after the captures, once after the DB restore); `docker ps -a` empty |
