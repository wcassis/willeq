# T397 — `spell_effects.json` resolved like every other data file (B221); `removeSpellEffect` (B220)

Class B ×2, slot2, no server. Read the B221 and B220 board text and
T395's §Review.

## Leg A — B221

`ParticleManager` loads `config/spell_effects.json` by a CWD-relative
path; started from anywhere but the repo root the load fails silently and
every preset falls back to a DIFFERENT effect (permanent 10/s emitter vs
a 36-particle 0.1 s burst). Resolve it the way `willeq.db` and the config
root are resolved (read `Application`/the config loader for the one
data-root resolution — reuse it, do not add a second); log at ERROR when
the file is missing (a silent fallback to a different effect is the
defect). Tests, pre-fix failing: constructing the manager with the CWD
elsewhere still loads the shipped config; a missing file logs and the
preset is the shipped one, not the struct default (or the load is fatal —
decide from how the other data files behave and say which). Census: any
other CWD-relative data path under `src/client` (grep for `"config/`,
`"data/`, `fopen(`, `std::ifstream(` with relative literals) — route or
board each. Strike B221.

## Leg B — B220

`ParticleManager::removeSpellEffect()` writes `preAssignedEffectID` while
the worker matches `effectID` (never set, defaults 0, ids start at 1) —
it can never remove anything, and it has zero callers (six directories).
Read T395's fix to the lifecycle: does anything NEED removal now (a
spell interrupted mid-burst; `removeCastGlow` on interrupt — how is the
glow removed today)? If a live path needs it, fix the id mismatch and
wire the caller with a test (pre-fix failing: an effect removed by id is
gone from the worker's set); if nothing needs it, delete it (K12 ff).
Say which. Strike B220.

## Proof

Build, `ctest -j6` (no expected failures), pre-fix/post-fix runs, file
sizes; graphics change → visual gate standalone + ARM (the gate runs from
the build dir — if the gate's zones show a DIFFERENT spell-free frame
after the path fix, that is the confound made visible: STOP with the
contact sheet).

STOP: the census finds a data path whose resolution changes the visual
gate's inputs.
