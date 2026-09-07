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
