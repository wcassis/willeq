# T389 — spell effects against the oracle (B162 hypothesis, B022 ruling, T380 Leg B2 match)

Measurement first; eqemu lock (atomic; must be ABSENT — if present read
it and STOP); the slot that frees after T386. Read `refactor/T380_spell_visuals_survey.md`
§3 (the three oracle spells and the capture recipe) and §4 Legs B2/B3,
the B162/B165/B022 board text, `review/T361/README.md` + scripts (the
official client rig), and T381/T384's Reviews (what the vocabulary now
carries).

## Leg A — capture (both clients, one session)

Official client (T361 rig, Oraclealpha or a Wizard-capable GM char),
qeynos2, daylight (`#set time 11 0`), continuous `ffmpeg x11grab` at 5+
fps for the whole session; OCR-verified `#castspell <id>` for **54 Frost
Bolt** (populated, Target stage, travel_type 3 → projectile), **93 Burst
of Flame** (fire, populated), **1494 Flame Jet** (EmptyRecord), **6594 GM
Cleansing** (NoAffectIndex); `#nukebuffs` + 25 s between. Then the same
four on fable at the same tip, same spot, same camera. Step the videos:
for each spell, per stage (Source = caster, Middle = travel, Target =
impact): does anything draw, what colour, what sprite shape (crop and
enlarge the particle), duration in frames. Table in `review/T389/`.

1. **B162:** the sprite the oracle draws for 54 and 93 versus the two
   candidate mappings (`GENx0` = `spelx0` vs `spelx1`, files under
   `/mnt/projects/EverQuestP1999/SpellEffects/`): which file's image
   matches the enlarged crop? Settle the hypothesis or record that neither
   matches (then the name is not a texture file — say what it is).
2. **B022 / Leg B3:** 1494 vs 6594 — do both draw the generic effect, both
   nothing, or differ? If they DIFFER, STOP that leg for Will.
3. **Projectile:** does 54 show a travelling object between caster and
   target on the oracle, and over how many frames?

## Leg B — the smallest match (T380 Leg B2)

With the oracle frames in hand: route Frost Bolt's Target-stage sprite
names into `EmitterConfig::textureRegions[]` via
`SpellEffectsConfig::getTextureIndex()`, mapping the measured names onto
EXISTING atlas tiles that best match the oracle crop (pick by comparing
tile against crop, record the comparison). Unit test on the name→tile
mapping (pre-fix failing); the fable frame for 54 re-captured and placed
beside the oracle frame. STOP before extending `ParticleAtlas` past 16
tiles or loading `spel*.tga` — if the oracle's sprite is not
approximable by any tile, that is the finding: board it (B195+) with the
crops as the Will decision (atlas from EQ textures = new render
feature).

Restore every server value; `down.sh`; delete the lock. Strike/annotate
B162, B022, B165 with the measured answers.

STOP: lock present; the official client will not cast (10 minutes,
record why); Leg A item 2 differs; Leg B needs the atlas.
