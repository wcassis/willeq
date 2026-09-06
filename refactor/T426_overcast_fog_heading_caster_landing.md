# T426 — the overcast fog swap (B360); a server heading reaches the camera (B399); the caster's own landing draws (B400)

Class A ×3; eqemu lock (atomic; must be ABSENT); slot1. Read B360
(T418: the oracle's weather is a FOG COLOUR + SKY swap — near wall ×1.42
R, far ×2.73 R, sky blue → grey; fable has `getWeatherFogColor()`,
`getFogDensityModifier()`, `getRainFogSettings()`, `SkyRenderer` all
unmeasured), B399 (`#goto x y z h` sets the heading once and the client's
own heading echo overwrites it with 480 — every rig has fought this),
B400 (a landing draws on the observer but intermittently not on the
caster's own client), T418's rig and README (`review/T418/scripts/`,
`#weather 3 1 <i>`), T405's rig, T422's rig and traps (`video started`
mark lags ffmpeg ~5 s; `scan_excursion.py`).

## Leg A — B399 first (it fixes the rig for Leg B)

Read the heading flow: `[ZONE-IN] setPlayerPosition heading=160` then
`480` — the client's own movement echo (`OP_ClientUpdate` round trip or
the local integrator) reapplies the pre-goto heading. Find the writer
that overrides a server-authoritative position update and make a
server relocation win (the same shape T404 used for the self entity
row: the server's relocation is authoritative until the player moves).
Test pre-fix failing: a synthetic `#goto`-style position packet with a
new heading → the camera yaw equals it on the next frame and stays
until input. Live: `#goto -74 428 3.751 160` turns the camera; the T405
composition control passes without hand-turning.

## Leg B — B360 (the fog swap, live A/B)

With heading now pinned by `#goto`: at T418's spot, day and night, rain 5
and clear, capture both clients (30 fps grab; note the oracle renders
~5/s). Measure per depth strip (T418's three strips) and the sky: the
oracle's fog colour and the distance at which the ratio saturates. Then
A/B fable's `getWeatherFogColor()` / fog start-end / sky overcast one
parameter at a time until each strip and the sky match within the noise
floor; every value carries its probe. Tests pin the solved values;
strike B360.

## Leg C — B400

Reproduce with the caster capture: which path differs between caster
and observer for the same `OP_Action`? (The caster also gets the
`effect_flag=4` repeat — does the second packet's arm REMOVE the effect
the first created? T397's per-entity removal, T395's keying.) Headless
test pre-fix failing: caster receives 0 then 4 for its own cast → the
impact effect is alive at +0.5 s. Fix; live re-check on the caster
capture. Strike B400.

Restore every server value (world DOWN); `down.sh`; delete the lock.

STOP: lock present; Leg A's writer is the movement integrator's own
prediction (then report the shape — a prediction that ignores server
relocation is a design point, not a one-line fix).
