# T367 — fable's night against the oracle (B080)

Class A; slot1; eqemu lock (atomic; must be ABSENT — if present read it and
STOP); official client under Wine (T361's rig in `review/T361/scripts/`,
character Oraclealpha). Read the B080 board text and `review/T361/README.md`
first: at the same spot, zone, forced 22:00 and the same 300x300 near-wall
crop, the oracle reads 0.0643 linear grey and fable 0.0018 (35x); whole
frame 0.0332 vs 0.0019 (17x). T361's lead: fable's 218 zone point lights are
"data-only" — they scale colours but light nothing — while the oracle's
lamps plainly light the near wall.

## Leg A — decompose the gap (measurement)

Do not tune anything until the gap is decomposed. At the T361 spot, night:
1. Oracle: the near-wall crop with the camera turned so NO zone lamp is in
   the frame or near (find such a spot; record heading), and with a lamp
   near. The difference isolates lamp light from ambient/sky.
2. Oracle at noon, same crops: the day/night ratio of the oracle
   (T358 measured fable's as 4x).
3. Fable: the same four frames. Also dump what fable actually feeds the
   pipeline at night: the global ambient values `updateTimeOfDay()` sets
   (smgr ambient + `ZoneShader::setAmbientColor`), the vertex-colour /
   lightmap path if any, and what the 218 lights do (read the zone shader:
   are they uploaded as point lights, or only used to tint?).
4. Table: client × (day/night) × (lamp/no-lamp) → grey, plus fable's
   ambient numbers. State which term carries the 35x: ambient floor, lamp
   contribution, vertex colours, gamma/colour space of the output (check
   the oracle's and fable's framebuffer format — an sRGB-vs-linear mismatch
   is a ~2.2 exponent and would look exactly like "35x at the dark end").

## Leg B — match the dominant term

Fix the term the table names, by A/B search against the oracle's night
no-lamp crop (ambient) and lamp crop (lights), until both are within the
still noise floor (T358: 1.3%; T361's edges were one frame wide). If it is
the point lights, make them light geometry (the renderer's light budget is
`adaptiveLightingZoneBudget` = 5 on the Pi — respect the budget; lights are
already selected per frame by `computeOnce()` — check what consumes
`selectedLights`). If it is gamma, fix the output stage, then re-run
T361's residuals (they must tighten — record the new table). Tests: a unit
test on whatever mapping changed (pre-fix failing), the live A/B frames in
`review/T367/`. Strike B080; update T361's residual note on B067.

## Proof

Build, `ctest -j6`, visual gate standalone (pixel goldens WILL move if the
night brightness changes — the gate's zones are captured at their own time
of day; if a golden moves, STOP for Will with the contact sheet, never
self-bless), ARM build, file sizes. Restore server values; `down.sh`;
delete the lock.

STOP: a pixel golden moves (report + contact sheet, do not bless); the
dominant term is not reachable without a renderer restructure (report the
table and the shape).
