# spellsnew.eff / spellsnew.edd: what each word means, from the client's code

B1141 (#461), 2026-10-23. Every meaning below is read from the official Titanium client's own code, not
from pixel correlation. Binaries (copied read-only from the oracle client, lane 5):

| file | sha256 | size | PE timestamp |
|---|---|---|---|
| eqgame.exe | ffa64c072c0c5e834a982720fd395ddc8974df26d1597020e1dbeb651b7ed957 | 3,981,312 | 2005-10-31 18:36:26 UTC |
| EQGraphicsDX9.dll | 3d66ac959f3163bc5f8da3e49020e83d11f55edd7932cca2cedebc9251b5fc15 | 2,318,336 | 2005-10-31 18:31:33 UTC |

Addresses are virtual addresses (eqgame base 0x400000, EQGraphicsDX9 base 0x10000000). The full per-word
tables with every read site, the Ghidra 12.1.4 project, the scripts and the cross-check against the 40
oracle-measured records are in `review/B1141/README.md` (`edd_fields_from_code.tsv`,
`eff_fields_from_code.tsv`, `crosscheck.tsv`). Fable reads the EFF record whole
(`include/client/spell/spells_new_eff_reader.h`) and, since B1144 (#464), draws by its slot words: the
level gate, the anchor and the attach point, Source only from a begin-cast, Middle as the missile
(B1152: the flight emitter rides the moving actor and arrival fires the Target stage), no landing
without a Target slot (`SpellVisualFX::stageSlots`, `refactor/B1144/README.md`,
`refactor/B1152/README.md`).
Of the EDD entry it reads the texture, the frame count and, since B1146, the colour words +0x68, +0x90,
+0x94, +0xA4 and +0xD4..+0xE8 (`include/client/spell/spells_new_edd_colour.h`, applied per texture
region by `SpellPresets::applyStageEmitterColours()`; since B1149 the alpha and blend words too, on every
spellsnew.eff emitter, white included). Since B1145 (#465) the EDD is read by ONE reader,
`include/client/spell/spells_new_edd_reader.h`, which also decodes +0x78..+0x8C, and every drawn slot is
timed from them (`include/client/spell/spell_fx_timing.h`, `refactor/B1145/README.md`): start = the
stage's measured latency + +0x8C, emission for the run minus the delay, each particle +0x7C. The
cast-animation term of the Source duration (`0x47ddd4`, model data) is MEASURED: 4.35 s
(`review/B1145/cast_anim/`). Since B1149 (#469) every drawn slot also emits the FILE's population -- the
+0x80 burst, then +0x84 per tick at +0x88 ticks/s (`EmitterConfig::ticksPerSecond`) -- and draws its sprite
size: a world-unit quad +0xCC..+0x184 wide, +0x17C..+0x180 tall (+0x18C one draw), times +0x16C, with
+0x98/+0x9C/+0x190/+0x194 over the life (`SpellsNewEddSprite`, `SpellPresets::applySlotEmitter()`,
`refactor/B1149/README.md`). Since B1164 (#483) the +0x6C / +0x19C attach scale s is
the actor's own: a Source / Target slot on attach point 1 of an old-world model is attached to the model's
root TRACK, s = 0x1005c4f0(track) x |row 0| of its world matrix (EQGraphicsDX9 0x10060ae8..0x10060b23), and
that row's length is the actor scale eqgame 0x47a0e9 writes, 0.2 x the spawn's size (the race default of
0x47a147 at size 0): read on lane 5 as 1.200000 on a size-6 human and 1.000000 on the size-0 dark-elf caster
(`include/client/spell/spell_fx_attach_scale.h`, `refactor/B1164/README.md` sec. 10). The Middle stage's
missile actor still gets 1. The +0xA0 density distance (80 on most entries) does not bite at the
harness/lane camera (29 units). Since B1150 (#470) the EDD shape and offsets are wired
(`SpellsNewEddReader::shapeAt`, passed into `SpellPresets::applySlotEmitter()`,
`refactor/B1150/README.md`), and since B1151 (#471) the motion words
(`SpellsNewEddReader::motionAt`, the same call, `refactor/B1151/README.md`).

**B1153 (#473) final state:** with every quantity above read from the file, the measured-row system
is RETIRED. The runtime no longer carries per-record `sourceRecords`/`targetRecords`/`animRecords`
rows — a record is drawn purely by its spellsnew.eff slots (level gate, anchor, attach point, EDD
timing, colour, population, sprite size, shape, motion), and the config carries the two default
rows only. The retired measured words became regression bands in the tests
(`tests/spell_fx_row_oracle.h`, `tests/spell_fx_shape_oracle.h`,
`tests/spell_fx_population_oracle.h`, `tests/spell_fx_timing_oracle.h`; the field-145 → record
mapping is `tests/spell_anim_rows.h`). Six `spellOverrides` STAY — 38/41/54/253/458/656, the
B1120/B1122 measured clouds where the lane shows the file is not the whole story; each is
documented with its numbers in `refactor/B1153/README.md`.

## Who does what

- eqgame `0x44d773` reads SpellsNew.eff whole (records of 0x10c bytes); a spell's record is
  spells_us.txt field 145 (`0x44dbf0`, used only when the record name is non-empty).
- eqgame `0x49b260` plays one stage: 0 Source (from begin-cast, `0x49b39d`; the only other stage-0
  call, `0x4bfe0f`, plays a Source `0x49b39d` deferred: no begin-cast, no Source), 1 Middle (ONLY from the
  missile spawner `0x476b12`: a spell with no missile never plays its Middle stage), 2 Target
  (`0x47f5e1`, `0x45448b`). `0x495cc3` walks the stage's four slots and calls the particle manager's
  vtable +0x14 = EQGraphicsDX9 `0x1005e1d0` once per slot.
- EQGraphicsDX9 `0x100644e0` loads SpellsNew.edd (`EDD\0`, `110\0`, entries of 0x1a0 bytes; it also
  migrates versions 101..109). `0x1005a430` (CEmitterDef::CreateEmitter) sizes and creates one CEmitter;
  `0x100605b0` (CParticleSystem::Render) spawns, integrates and draws every particle; `0x1005f400` sets the
  texture and blend state.

## spellsnew.eff record (268 bytes)

| offset | meaning | code |
|---|---|---|
| +0x00 | char[64] effect name; empty = no new effect | `0x44dbf0` |
| +0x40 + 0x44*s | stage s (0 Source, 1 Middle, 2 Target): int sound id, then 4 slots of 16 bytes | `0x49b260` |
| stage +0 | sound, played on the caster (Source) or the target (Target), never for Middle; -1 = none | `0x49b260` -> `0x479ee8` |
| slot +0 | spellsnew.edd index; < 1 = empty | `0x495cc3` |
| slot +4 | minimum caster level: drawn iff value <= caster level; an NPC caster counts as level 1 | `0x495cc3` (spawn +0x24c level, +0x244 NPC) |
| slot +8 | anchor: 0 fixed world position, 1 caster, 2 the missile, 3 target | `0x495cc3` |
| slot +12 | attach point: 0 actor position, not attached; 1 DEFAULT; 2 CHEST; 3 HEAD; 4 LHAND; 5 RHAND; 6 LFOOT; 7 RFOOT (2..7 fall back to DEFAULT); 8 actor origin; 9 LEYE; 10 REYE (slot skipped if absent); 11 MOUTH; 12 CHEST dropped to the floor, actor scale as size scale. Ignored for anchors 0 and 2 | `0x495cc3`, points bound by name `SPELLPOINT_*` at `0x47c17e` |

The duration eqgame passes per slot: Source = cast time + the cast animation's length (`0x49b977`),
Middle = spell field 18 (AE duration, `0x476b12`), Target = 0. Density and opacity are the
SpellParticleDensity / SpellParticleOpacity options.

## spellsnew.edd entry (416 bytes)

Notation: T emitter run time, L particle lifetime, age/remaining per particle, s = attach scale. Axis 1 is
the emitter's up axis (world up when unattached, the actor's up row when attached); positions are kept in
cylinder coordinates (angle in 512ths of a turn, radius, height along axis 1).

| offset | type | meaning (from use) |
|---|---|---|
| +0x00 | char[64] | name (dedup key only) |
| +0x40 | char[32] | texture file |
| +0x60 | int | orient to the attached track/bone (flag) |
| +0x64 | int | 1 = no depth write |
| +0x68 | int | 1 = additive blend (DESTBLEND ONE); else alpha blend |
| +0x6C | int | 1 = sizes and velocities scale with the attach scale |
| +0x70 | int | sprite mode: 0 billboard, 1 streak along axis 1, 2 world-oriented quad |
| +0x74 | int | 1 = live particles follow the emitter every frame |
| +0x78 | float s | emitter duration: < 0 fixed abs(value); >= 0 the caller's duration if given, else this. T >= 4000 loops |
| +0x7C | float s | particle lifetime L (< 0: lives for T) |
| +0x80 | int | burst: particles on the first frame |
| +0x84 | int | particles per emission tick |
| +0x88 | float /s | emission ticks per second, from +0x8C until T |
| +0x8C | float s | emission start delay |
| +0x90 / +0x94 | float s | alpha fade-in (by age) / fade-out (by remaining) times |
| +0x98 / +0x9C | float s | size grow-in / shrink-out times |
| +0xA0 | float | full-density distance: counts scale by +0xA0/distance beyond it |
| +0xA4 | float | peak opacity (alpha clamp) |
| +0xA8 | int | spawn shape: 0 point, 1 even ring, 2 stacked rings, 3 cylinder surface, 4 disc, 5 sphere surface, 6 box surface, 7 line/cone, 8 torus, 9 random ring |
| +0xAC / +0xB0 / +0xB4 | float | shape radius X / radius Y (0 = X) / height or Z radius |
| +0xB8 / +0xBC / +0xC0 | float | spawn offset along axes 1 / 2 / 3 |
| +0xC4 / +0xC8 | float | tilt of the emitter axes (512ths) |
| +0xCC, +0x184 | float | sprite width range (min, max) |
| +0x17C, +0x180 | float | sprite height range (min, max); +0x18C = 1 draws both from one random |
| +0xD0 | float | depth bias toward the camera |
| +0xD4..+0xDC, +0xE0..+0xE8 | int | start R,G,B and end R,G,B, linear in age/L |
| +0xEC, +0xF0, +0xF4 | float | axis-1 (up) speed min, max, acceleration |
| +0xF8, +0xFC, +0x100 | float | axis-2 speed min, max, acceleration |
| +0x104, +0x108, +0x10C | float | axis-3 speed min, max, acceleration |
| +0x110, +0x114, +0x118 | float | radial (outward) speed min, max, acceleration |
| +0x11C, +0x120, +0x124 | float | swirl speed min, max, acceleration (512ths/s) |
| +0x128 | float | world gravity (down) |
| +0x12C | float | world +X drift speed |
| +0x130 | int | frames: 1 whole texture, 2..4 a 2x2 atlas, 5..8 4 columns x 2 rows, 9..16 a 4x4 atlas, > 16 whole texture |
| +0x134 | float | animation frames per second |
| +0x138, +0x188 | float | sprite spin speed range (512ths/s); +0x174 = random start angle |
| +0x13C, +0x140, +0x148..+0x168 | | legacy particle-cloud words: written by the converter `0x1005eef0`, never read for drawing |
| +0x144 | int | fixed particle count (0 on every spellsnew entry) |
| +0x16C | float | sprite size multiplier |
| +0x170 | float | default opacity, used only for zone/actor emitters (`0x10058dfc`) |
| +0x178 | int | spin aligned to an attach axis (axis rows not traced) |
| +0x190 / +0x194 | float s | height / width shrink-out times |
| +0x198 | int | 1 = draw particles whose radius went negative |
| +0x19C | int | 1 = shape and offsets scale with the attach scale |

Refuted by this code (the old `tools/model_viewer/spell_effect_db.h` names): particles per second at
+0x80/+0x84 (burst and per-tick counts), radius +0x88 (a rate), velocity +0x94 (alpha fade-out),
lifetime +0xA0 (LOD distance), six ARGB colours (six 0..255 ints, two RGB triples), gravity +0x140
(never read; gravity is +0x128), size start/end +0x160/+0x164 (never read; sizes are +0xCC/+0x17C).
B1137's hypotheses refuted: +0x94 speed, +0xA4 lifetime (peak opacity), +0xB8 gravity (an offset).
