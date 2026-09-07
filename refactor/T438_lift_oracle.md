# T438 — unblock the oracle door click and record the Kelethin lift (B421 → B422/B423)

Class A, eqemu lock (atomic; must be ABSENT), slot3 after T428. Read
B421–B423, B426, B427, T433's §Review and `review/T433/README.md`
(the six click styles that failed, the two rig defects: enter-world.sh
clicks 15 px low; willeq_test slot 1 is Testcaster), T435's §Review
(doors now collide; the action-byte mapping; B424's game-thread z), the
T361/T405 oracle rigs.

## Leg A — B421, make the official client open a door

Try in order and record each: (a) the client's door-use keybind
(`eqclient.ini` / Options > Keys page — T377's captures show the
bindings: quote the "use/open door" row), (b) real multi-frame pointer
motion onto the door before the click (the client's 3D pick may need a
hover frame), (c) a walk-into (the client auto-opens some doors on
proximity? measure, do not assume), (d) as the fallback rig: a one-line
Perl quest under the eqemu quests dir calling the door's `ForceOpen()`
on a `/say` trigger — server CONTENT, installed for the capture and
REMOVED afterwards with the world down (record the file path and its
removal). The official client animates on `OP_MoveDoor` whichever
route triggers it, so (d) still measures the client's animation; say so.

## Leg B — the constants fable invents

Once a door opens under the rig, record VIDEO (never stills) and step
it: one opentype-0 door (rotation duration, easing), one opentype-59
lift cycle (per-frame platform z: travel time, speed profile, dwell,
whether the player standing on it is carried — stand Oraclerogue on the
lift 69 platform and read `/loc` per frame), one 100/105 spinner rate.
Then fable at the same doors, same rig, side by side; fix fable's
constants and the lift motion (`door_param` semantics from
`/mnt/projects/akk-stack/code/zone/doors.cpp` — quote absolutely) so the
traces match; the player rides the platform (T435's collision writer
updates every frame the door moves — prove the rider's z follows).
Pin each constant with a test that fails pre-fix.

## Leg C — B424's live trace

Instrument (a logging build is fine) every writer of the game-thread
player z (`PlayerFacade::SetPosLocal`/`SetPosition` and whatever else
T435 lists) and run ONE `#goto 340 137 6`: which handler wrote 77.0,
from which packet. Fix if it is fable's; board if it is the server's.

## Proof

Standard (background builds, `ctest -j6`, pre/post, G05, gate + ARM,
file sizes). Server values restored with the world DOWN; quest file
removed; `/camp` cleanly; lock released. Media under `review/T438/`
with a README. Strike B421/B422/B423/B424 as proven; B426/B427 annotated
with what the rig now allows. New items B437+.

STOP: no route opens a door (report all four); an image golden moves.

---

## Review

Slot3, branch `task/T438-lift-oracle`, base **`07c9d029`**. Session
2026-09-07. Full evidence, every video and every trace:
`review/T438/README.md`.

### The headline

**B421 is unblocked and every door constant fable shipped was invented and
wrong.** The measured set, all against the official Titanium client:

| | fable BEFORE | ORACLE |
|---|---|---|
| hinged OPEN rotation | 0.5 s | **0.95 s** |
| hinged CLOSE rotation | 0.5 s (the same number) | **0.60 s** |
| who closes an ordinary door | the server -- and it never does | **the CLIENT, 11.04 s after it opens** |
| opentype-59 lift travel | none; `door_param` read by nothing | **`door_param` units along z** |
| lift speed | -- | **8.67 units/s, constant** |
| lift dwell at the far end | -- | **20.5 s, then it returns by itself** |
| a player standing on a lift | not carried | **carried exactly** |
| spinner (opentype 105) | 42.35 deg/s | **84.4 deg/s -- a FULL turn in 4.267 s** |

Two live defects fell out that nobody had named:

* **`Doors::Process()` (`/mnt/projects/akk-stack/code/zone/doors.cpp:138`)
  transmits a close only for `open_type == 40` or `trigger_type == 1`.** For an
  ordinary door it flips its own state and sends nothing, and the official
  client closes the door anyway. fable drove doors purely from `OP_MoveDoor`,
  so **every door a player or an NPC opened stayed open for the session**.
* **B424 is not a ground-resolution bug.** It is a jump landing on a stale
  `jumpStartZ`; the `#goto` z survives three milliseconds. See Leg C.

The spin constant is the nicest correction: 42.35 deg/s is exactly HALF the
real rate. The 4.25 s period was right and was read as a half turn -- and the
blade's own 180-degree symmetry (centroid autocorrelation **-0.861 at lag 64
frames, +0.822 at 128**) is why that mistake was so easy to make.

### Leg A -- five routes, all reported

**(a) the keybind.** Will's ruling mid-unit named `u`, and **the binding is
real**: the client's Options > Keys page (category All) lists
**"Use centerscreen -- Keypress U"**
(`review/T438/probe/T438_oracle_options_keys_use_centerscreen_U.png`). My first
pass said there was none, from `eqgame.exe`'s `KEYMAPPING_*` name table -- that
table is movement/hotbar/target only and is NOT a census of the bindings.
**`u` still produced no `OP_ClickDoor`**: twelve presses across `FAYDOOR` 30
(facing at 12.6 units, and touching it), the lift platform (centred on screen
at 10 and at 4 units) and the `FELE2` call button (7 units, its trunk filling
the centre), with eqemu's Doors log at Debug 3 to gmsay AND the zone file. The
control that makes it a real negative: after Escape, two `u` presses leave the
chat input box empty, so the key reaches the game rather than the chat bar.

**(b) multi-frame pointer motion then click.** No. New diagnostic on top of
T433's six styles: hovering the Actions window's *Sit* button highlights it and
raises its `<Ctrl S>` tooltip, so the client tracks the absolute pointer
exactly. It is the 3D world-interaction pick -- mouse or centre-screen key --
that does not fire under Wine/Xvfb.

**(c) walk into it.** No, and it cannot work: `Mob::HandleDoorOpen()`'s only
caller is `mob_ai.cpp:985`, NPC AI. A client walking into a door emits nothing.
(The first attempt was void -- `xdotool key Up` moves nothing; `w` does.)

**(d) the quest fixture.** YES. `quest::forcedooropen()` on a `/say`, and
`Doors::ForceOpen` (`doors.cpp:658`) emits the same `OP_MoveDoor` a click
would, so the recording is the official CLIENT's animation.
**Rig lesson worth carrying to `/mnt/projects/eq-oracle`:** a zone that already
ships `quests/<zone>/player.lua` **shadows** a `player.pl` dropped beside it --
`GetQIByPlayerQuest()` tries every extension within each candidate path, so lua
wins and the perl file is never opened (a deliberately broken `player.pl`
produced silence, not an error). The fixture went in the first candidate path,
`quests/<zone>/v0/player.pl`, which outranks it. Both files and both `v0/`
directories are removed.

### Leg B -- the measurements

*Hinged*, five cycles of `FAYDOOR` 30 at 30 fps: open spans 0.967/0.933/0.967/
0.933/0.933 s, close 0.633/0.567/0.567/0.567/0.600 s, open-start to close-start
**11.00/11.07/11.10/11.03/11.00 s**. The server log shows the five
`Doors::Process` fires at exactly +5 s and no packet on the wire.

*Lift*, two doors with different `door_param`, from a 4 Hz server-side position
trace: door 69 (param 68) open-to-return **28.328 s**, door 77 (param 98)
**31.788 s**; 30 extra units cost 3.460 s -> **8.67 units/s** and a dwell of
**20.49 s** on both. Both platforms' walkable surface sits `pos_z + 3.650`
(identical on both), and their top ends compute to ~74, the Kelethin walkway --
so the travel distance IS `door_param`. The player parked on lift 69 rode it:
z 5.81 -> 10.93 -> 19.98 -> 29.30 -> 39.47 -> 49.87 -> 58.65. One packet runs
the whole cycle: the run with a 45 s server close timer and the run with a 5 s
one have their two motion bursts at the same times. **At rest the platform sits
at the FAR end** (`pos_z + door_param`) -- at the idle lift, `#goto` to z 5 and
to z 12 both resolved to the forest floor at -0.09, and only after a MoveDoor
did that spot resolve to the platform at 5.8066.

*Spinner*: opentype 105 only. **opentype 100 is unmeasured** and takes the same
constant because fable already used one for both -- boarded **B450**.

### Leg C -- B424, root-caused in one run

Both player-position writes now carry their CALL SITE
(`__builtin_FILE()`/`__builtin_LINE()` defaults, logged at `MOD_MOVEMENT`
TRACE). One `#goto 340 137 6`:

```
10:35:27.487  SetPosLocal (-320.0, -404.0, 14.4944)  movement_state.cpp:514
10:35:27.504  SetPosLocal (-320.0, -404.0, 16.1655)  movement_state.cpp:569
10:35:27.519  SetPosition (137.0,  340.0,   6.0000)  zone_component.cpp:138
10:35:27.522  SetPosLocal (137.0,  340.0,  17.7788)  movement_state.cpp:569
10:35:28.488  SetPosLocal (137.0,  340.0,  14.4944)  movement_state.cpp:553
```

A jump was in flight. `UpdateJump()` ends the arc by writing `jumpStartZ`
FLAT -- captured before the relocation. **No packet wrote 77.0**; the packet
wrote 6.0 and fable overwrote it 3 ms later. T433's 77.0 is the same arithmetic
with a `jumpStartZ` taken on the Kelethin platform, which also explains T435's
renderer/game-thread divergence (`SetPosLocal` writes only the game store).
Fixed in `PlayerState::setPosition()` -- the discrete-relocation path, the one
store -- and deliberately not in `setPositionNoEvent()`, the integrator.
Live after: `/loc` reads **`137.0, 340.0, 6.0`**.

### fable after, live

`/loc` after the same `#goto`: **6.0** (was 77.0 / 14.5). Doors 43 and 30
closed themselves 11.02 s and 11.04 s after opening with nothing on the wire.
The lift translates (`review/T438/fable/T438_fable_lift_translates_framesheet.png`).
**The RIDE is unit-proven but NOT live-proven and is OWED (B449)**: after a
`#goto` the player floats at the commanded z instead of settling onto the
platform, because `resolveMovementCollision()` only runs on a frame with
movement input (T435 Leg C). Proving it needs the player to WALK on. One real
bug was found and fixed between the two attempts -- the ride probe keyed its
BSP region on `ctx_.currentPvsRegion`, the CAMERA's region, where
`findGroundZBspFiltered()` derives it from the PLAYER's point.

### Deviations

* The plan's route (a) is answered twice: the keybind exists (Will) and does
  not fire (measured). Reported as a correction, not as agreement.
* opentype 100 not measured (B450); the lift ride not live-proven (B449).
* `door_manager.cpp` reached 1,511 lines and `DoorManager::update()` moved to
  a new `door_manager_animation.cpp`.
* `DoorVisual::liftDeltaZ` was written and never read; `WriteOnlyFields`
  caught it and it is deleted (`liftMovers()` carries the same information).

### Verification

`ctest -j6` **4,588/4,588**. Visual gate PASS, no golden touched. ARM exit 0.
`check_file_sizes.sh fable`: 47 checked, 47 ok. Pre/post test runs in
`review/T438/logs/test_*_{PRE,POST}FIX.log`. Server values restored with the
world DOWN and diffed (`character_data` identical, `eqtime` identical, weather
identical, no new corpse); quest fixture removed; lock released.
