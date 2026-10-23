# B1118 (#438): spell particle effects, official client vs fable, measured at real frame rate

Class A, **docs-only**. This unit changes no code, config or golden. It measures what each
client draws for the B1107 seven, per stage, names the fable mechanism behind every
divergence with file:line, and lists one follow-up fix card per mechanism. #424 stays open.
Evidence (videos, pcaps, logs, strips, scripts, the per-spell TSV):
`/mnt/projects/fable/review/B1118/` (index: its `README.md`).

## 0. Result

**The official client's spell effects are large, dense, multi-second effects. Fable's are
small and short.** Each oracle effect fills a volume several model heights across. Fable
draws a thin scatter of dots one to two pixels wide at waist height.

Measured on the same character, the same gems, real gem casts on both clients, and the same
model-height scale:

- **Source (cast glow).**
  - The oracle draws a body-wide cloud that covers **0.08-3.9 model²**. It starts at
    **BeginCast + 0.20-0.45 s** and lasts until the action packet.
  - Fable never rises above the noise floor (**≤ 0.07 model²**). Seen frame by frame, it is
    5-15 dots of 1-2 px at the hand bones.
- **Landing (spellComplete + Target impact; both anchors coincide in this tour).**

  | | oracle | fable | ratio |
  |---|---|---|---|
  | Peak coverage | 1.2-7.3 model² | 0.12-0.26 model² (253: ≤ 0.033) | 5-37× (253: > 200×) |
  | Onset after OP_Action | +0.40-0.80 s | +0.30-0.68 s | no divergence |
  | End after OP_Action | +4.85-6.52 s | +2.2-2.3 s | 2.3-3.7× in duration |
  | Sprite p90 diameter | 0.056-0.110 model height | 0.022-0.040 model height | 2-4× |

- **Shape.** The oracle draws columns, domes and swirls that reach ≥ 2 model heights. Fable
  draws a flat disc at about 0.6 model height.
- **Colour.** Four of seven spells diverge:
  - 458: oracle cyan (hue 196-199), fable **white**.
  - 54/656: oracle violet (hue 243, sat 0.47), fable pale blue (hue 217, sat 0.25).
  - 93/38: oracle yellow fire (hue 46, val 0.91), fable orange dots (hue 26-32).

**Two defects outside the particle code were found on the way. Neither is a capture
artifact:**
1. **The official client refuses gem casts that the class cannot use.** The message is "You
   are not high enough level to cast this spell!" (`oracle_leg/24_fx41.png`). This, not the
   gem pitch, is why B1107 got 3/7 (B1129: B1107's own table lists 41 as confirmed on a
   wizard, which this refusal rules out; `refactor/B1129/README.md` "B1107 reconciled"). The fable client does not check this, and the server
   does not either.
2. **Fable deletes the self-targeted local player when the server sends `OP_DeleteSpawn(0)`.**
   The server sends that packet after every NPC death. The code is
   `entities_component_appearance.cpp:73-78`. After the delete, every later cast on that
   client has no caster model and no hand or centre anchor (run A1, §5 M9).

## 1. Rig (acceptance 1)

| | ORACLE | FABLE |
|---|---|---|
| host | ThinkPad 172.16.200.205, lane 5, eqemu `/opt/akk-stack` | same box, same server, same character |
| client | Wine official client, `:99` headless NVIDIA Xorg, window 1280x800 at root (0,30) | `~/src/willeq` = fable **c3275a16**, `build-gles2` Release (configured `-DCMAKE_BUILD_TYPE=Release`), `--renderer gles2 --egl-window -r 1280 800`: real GL, **EGL X11 on the NVIDIA Quadro RTX 3000** (`EGL (X11): vendor=NVIDIA`) |
| capture | `ffmpeg -f x11grab -framerate 25 -fps_mode passthrough` of root `+0,30` 1280x800 (the rig's 10 fps raised to 25) | `ffmpeg -f x11grab -framerate 30 -copyts -fps_mode passthrough` of the client window's own geometry (`xwininfo`: 1280x800+0+0), mkv with epoch pts |
| measured grab rate | **25.00 fps**, max frame gap 40 ms, in every window | **30.00 fps**, max frame gap 34 ms, in every window |
| liveness | 0 stale grabs over Act+0.3..2.3 s in all 7 windows (a repeated image would change < 20 px) | the same, 0/59 in all 7 |
| packets | tcpdump on 5998/5999/9000-9001/7000-7030 (veth copies dropped) | the same |
| time base | the video's pts + the timing-marker calibration (`video.calibration.tsv`) | the frame pts **is** epoch (`-copyts`) |

**`--dump-frames-dir` was not used.** Fable frame time comes from fable's own log over the
whole tour of every leg:
- `--frame-timing` logs `FRAME TIMING BREAKDOWN (60 frames, …)` once per 1.000 s, so the
  render loop ran at a steady **60 frames/s** (vsync), i.e. **16.7 ms frames**.
- The CPU work per frame was **0.8-1.3 ms** (the `[entity-build] frame_ms avg`); the worst
  single frame in any 1 s window was **7.6 ms**.
- **0** `PERF: Previous frame took … (slow!)` warnings (`renderer_frame.cpp:84`, which fires
  at > 100 ms) appear in any leg.
- The sim worker's `deltaTime > 1 s` path (B1104/B1106) therefore never fired.

**Crops were checked against the window, not the root.**
- Oracle: the rig grabs the client window's area at root (0,30) (`marked.mp4.geom`:
  `0 30 1280 800`). The client's own UI confirms the alignment: the CastSpellWnd gem bar sits
  at window x=0, the gem cells are at image y=199+30i (`oracle_calib/11_gems_ready.png`), and
  the Actions window ends inside y<800.
- Fable: the grab is the window's own `xwininfo` geometry (`fable_*/window.xwininfo`).

**Shortest effect vs fps.** The shortest stage measured is the oracle 54 Source,
**0.80 s = 21 frames at 25 fps**. The shortest fable stage is 54/41 Landing, 1.57-1.60 s =
47-48 frames at 30 fps. Every transient is sampled by ≥ 20 frames; nothing is read from a
still.

**Camera.** The oracle uses F9×3 from first person, which lands on User Defined Camera 1: in
front of the caster, about 210 px model height. Fable's `user1` default looks from behind
(`fable_camtrial/`), so fable uses `/camera chase` + `/camera set az 180 pitch 12 dist 32`.
That gives a front view with a 235 px caster. **Every size and coverage figure is normalised
by each leg's own caster height in pixels** (oracle 210, fable 235; both are the same dark-elf
Testcaster, head top to feet), and the strips are cut to
the same 3.6 × 2.3 model-height box.

## 2. Set, gems, provisioning, tour

The set and gems are the same as B1107:

| gem | spell | target |
|---|---|---|
| 1 | 93 | NPC |
| 2 | 38 | NPC |
| 3 | 54 | NPC |
| 4 | 41 | self |
| 5 | 253 | self |
| 6 | 458 | self |
| 7 | 656 | NPC |

- Server-confirmed cast times from `OP_BeginCast`: 93 = 1500 ms, 38 = 2500, 54 = 1000,
  41 = 1500, 253 = 1500, 458 = 3600, 656 = 2000.
- Provisioning is in-tour with GM commands, as in B1107 §3: `#memspell <id> <gem0>` ×7,
  `#level 60`, `#mana`, `#set time 11 0`, `#weather 0`. Each bolt gets a fresh `#spawn <Name>`
  NPC.
- Character: `Testcaster` (char 67, account `willeq_test`, status 200, wizard).
- Pose: qeynos2 (-74, 428, 3.75), heading 160, seeded by SQL before login and restored
  afterwards.

**Oracle gem pitch, measured on this box** (`oracle_calib/`, B1107 G1). The pitch is **30 px**.
Gem *i* (1-based) is centred at root **(25, 213+30i)**, which is 34 px lower than B1107's
lane-1 coordinates. With these coordinates every click lands.

**Why B1107 got 3/7, and why this run needs three legs.** The 3 clicks that "missed" were
**refused by the official client**: Testcaster is a wizard, and 41 is ENC-only, 93 is
DRU/SHM/MAG and 253 is DRU (`spells_us.txt` fields 104-119). The client prints "You are not
high enough level to cast this spell!" and sends no `OP_CastSpell`
(`oracle_leg/24_fx41.png`). So each client ran three legs, with the class switched by SQL for
that leg only and put back after the world-down restore (`scripts/class_run.sh`):

| leg | class | casts | oracle | fable |
|---|---|---|---|---|
| A | 12 wizard | 458, 38, 54, 656 | `oracle_leg` | `fable_A` |
| B | 6 druid | 253, 93 | `oracle_druid` | `fable_B` |
| C | 14 enchanter | 41 | `oracle_enc` | `fable_C` |

**All 7 casts are server-confirmed on both clients:** `OP_CastSpell` → `OP_BeginCast` →
`OP_Action` with effect_flag 0 then 4, decoded with `scripts/pcap_ops.py`. Fable logs
`createCastGlow … RH=true LH=true center=true` for all 7.

Fable-only tour notes:
- After each `/cast` the fable tour sends `/cleartarget` (BeginCast + 0.3 s). Fable's
  colour-cycling target outline is a fable-only overlay, and with it on screen the outline
  dominated the pixel census (the `fable_*_outline` legs keep that version). The server
  already holds the cast target, and no cast was interrupted.
- 458 runs last in fable leg A, for the M9 reason in §5.

## 3. Method (no model judges a frame)

Per cast, `scripts/fx_measure.py` keeps **every** captured frame from BeginCast − 1.2 s to
Action + 10 s. There is no resampling, and each frame carries its own pts.

- **Source** = frames from BeginCast to before the first OP_Action. **Landing** = frames from
  the first OP_Action on.
- **Lit pixels**: pixels whose value rose by more than 80 over a median baseline.
  - The Source baseline is the pre-cast second.
  - The Landing baseline is the last 1.5 s of the window: effect gone, corpse already on the
    ground, outline cleared. This keeps a lasting scene change from reading as a long effect.
  - "Active" means lit pixels > max(4 × noise p95, 0.003 model²).
- **Coverage** = lit px / model_px².
- **Onset and end** are the first and last active frames (`scripts/fx_timing.py`). The
  Landing onset is the first frame above 1.5 × the Source tail.
- **Sprite size** = the equivalent-diameter p90 of 8-connected blobs at the peak
  (`fx_blobs.py`).
- **Population**: the blob count is a lower bound, because overlapping sprites merge.
- **Colour** = the mean RGB of the lit pixels in the current frame.
- **Sprite family** is matched by eye against the `SpellEffects/spel*.tga` art
  (`sprites/sprite_sheet.png`). Each row also cites the contact-sheet frame index.
- **Anchor**: in this tour the spellComplete (caster) and impact (target NPC) anchors coincide,
  because the NPC is `#spawn`ed on the caster. Landing is therefore measured as one stage. No
  Middle (projectile) stage can be separated at distance 0; see §6.

## 4. The measured table (per spell, per stage)

The full TSV is `review/B1118/spell_fx_table.tsv`. Frame indices refer to each leg's own
video: oracle at 25 fps, fable at 30 fps. "Cov" is the peak coverage in model².

| spell | stage | ORACLE | FABLE | DIVERGENCE |
|---|---|---|---|---|
| 93 Burst of Flame | Source | spelc2 orange stars + spelc4 flares, a body-wide cloud ~3.9 model wide. f7498 BC+0.449 → f7524 BC+1.489 (= Action). Cov 0.384. RGB 187,132,48. p90 0.070 | 5-10 orange dots of 1-2 px at the hands (`sheets/fable_source_zoom.png`, f2096). Below the floor (≤ 0.070) | size, count, extent (stage effectively invisible) |
| 93 | Landing | White-hot flash, then a yellow **fire column** ≥ 2.5 model tall + spark cloud. f7538 Act+0.547 → f7646 Act+4.867 (4.32 s). Peak f7573 Act+1.947. Cov **5.85**. Hue 46, val 0.91 | Orange dots in a flat disc at about 0.6 model height. f2120 Act+0.419 → f2177 Act+2.319 (1.90 s). Peak f2159. Cov **0.160**. Hue 26 | size ×1.9, coverage ÷37, duration ÷2.3, colour, shape (no flash, no column) |
| 38 Lightning Bolt | Source | Same art as 93 (spells.eff record 0). f9695 BC+0.429 → f9746 BC+2.469. Cov 0.382. p90 0.077 | A few orange dots at the hands. ≤ 0.056 | size, count, extent |
| 38 | Landing | Flash + fire column ~2 model tall. f9760 Act+0.530 → f9868 Act+4.850 (4.32 s). Peak f9800. Cov **3.89**. Hue 47 | Orange dots, flat disc. f1805 Act+0.427 → f1861 Act+2.294 (1.87 s). Cov **0.169**. Hue 32 | as 93 (coverage ÷23) |
| 54 Frost Bolt | Source | spelu1 violet squiggles around the body. f10632 BC+0.200 → f10652 BC+1.000. Cov 0.483. Hue 244. p90 0.032 | A few violet dots at the hands. ≤ 0.055 | size, count, extent |
| 54 | Landing | spelu1 **violet swirl** ~4 model wide + spelu2 glints. f10663 Act+0.423 → f10780 Act+5.103 (4.68 s). Peak f10670 Act+0.703. Cov **2.46**. Hue 243, sat 0.47 | Pale blue-white dots (speld2/spelu2), flat disc. f2350 Act+0.619 → f2398 Act+2.219 (1.60 s). Cov **0.117**. Hue 217, sat 0.25 | sprite (no squiggles), colour, size, duration |
| 41 Weaken | Source | speln1 green-yellow balls in a dome 3.9 model wide. f6382 BC+0.412 → f6409 BC+1.492. Cov **1.23**. Hue 76. p90 **0.104** | castSpray: a few dots sprayed down from the hands. ≤ 0.034 (the BC+0.226 blip is the `/cleartarget` outline) | size, count, extent |
| 41 | Landing | Yellow **pillar** ~2 model tall + a ball dome settling to the ground. f6420 Act+0.424 → f6565 Act+6.224 (5.80 s). Peak f6448. Cov **4.15**. Hue 72. p90 0.110 | Small yellow-green dots, flat disc. f1541 Act+0.680 → f1588 Act+2.246 (1.57 s). Cov **0.128**. p90 0.027 | size ×4.1, coverage ÷32, duration ÷3.7, no pillar |
| 253 Whirling Wind | Source | spela2 blue stars laid in **long curved ribbons** > 4 model wide. f6496 BC+0.429 → f6522 BC+1.469. Cov **3.92**. Hue 214 | A few cyan dots at the hands. ≤ 0.032 | shape (no ribbons), size, count |
| 253 | Landing | Dense star field + ribbons filling the frame. f6543 Act+0.802 → f6686 Act+6.522 (5.72 s). Peak f6552. Cov **7.28** | ~15-30 sparse cyan dots, Act+1.0 → about Act+5.1 (sheet f1559 → f1679). Never above threshold (max 0.033) | population about 2 orders lower, size |
| 458 Fire Spiral | Source | Small **cyan** sparkles in a narrow body column. f7621 BC+0.419 → f7700 BC+3.579. Cov 0.082. Hue 199 | A few **white** dots at the hands. ≤ 0.021 | colour, size, count |
| 458 | Landing | **Cyan** glowing balls bursting out + sparks, ~3 model wide. f7711 Act+0.422 → f7824 Act+4.942 (4.52 s). Peak f7773 Act+2.902. Cov **1.24**. Hue 196, sat 0.51 | **White** procedural SoftCircles, flat disc, drawn twice (one per AE target). f3463 Act+0.296 → f3520 Act+2.196 (1.90 s). Cov 0.258. Sat 0.13 | sprite (missing spela3 → SoftCircle), colour, duration, shape |
| 656 Shock of Ice | Source | spelu1 violet squiggles. f11578 BC+0.217 → f11622 BC+1.977. Cov 0.625. Hue 240 | A few violet dots. ≤ 0.063 | size, count, extent |
| 656 | Landing | Violet swirl + glints (record 23, same as 54). f11633 Act+0.404 → f11748 Act+5.004 (4.60 s). Peak f11639 Act+0.644. Cov **2.49**. Hue 243 | Pale blue-white dots, flat disc. f2962 Act+0.601 → f3011 Act+2.234 (1.63 s). Cov **0.116**. Hue 217 | as 54 |

**One-line verdicts per spell:**
- **93 and 38:** the oracle draws a spark cloud while casting, then a white flash and a fire
  column ~2-2.5 model tall that lasts 4.3 s. Fable draws invisible hand dots, then a 1.9 s
  waist-high disc of orange dots.
- **54 and 656:** the oracle draws violet squiggles while casting, then a 4.6 s violet swirl
  ~4 model wide. Fable draws invisible hand dots, then a 1.6 s disc of pale blue dots.
- **41:** the oracle draws a dome of green-yellow balls while casting, then a yellow pillar
  and a dome that settles, 5.8 s in all. Fable draws a hand spray, then a 1.6 s disc of
  small dots.
- **253:** the oracle draws blue star ribbons that fill the frame for 7+ s. Fable draws about
  20 sparse cyan dots.
- **458:** the oracle draws a cyan sparkle column, then a 4.5 s burst of cyan balls. Fable
  draws white dots, then a 1.9 s disc of white circles.

**What does NOT diverge:**
- Landing onset: oracle +0.40-0.80 s after OP_Action, fable +0.30-0.68 s.
- The trigger packets: both clients receive the identical BeginCast/Action sequence.
- The stage-to-sprite family for the slot sprites that fable does draw: orange
  spelc2-family for record 0, speln1 green for record 20, spela2 blue for record 35.

## 5. Mechanisms (fable side, file:line) — one per divergence class

The fable side was traced read-only through the tree at c3275a16. All sizes are world units;
on screen, a point sprite is `size × screenH / clip.w` px, clamped to 1-100 px
(`src/client/graphics/environment/unified_particle_renderer.cpp:58-69`).

- **M1. Sprite size.** Every spell style's sizes come from `config/spell_effects.json`, not
  from spells.eff:
  - castGlow `sizeStart 0.08-0.12 → 0.04-0.06` (`:54-57`)
  - castSpray `0.04-0.08 → 0.02-0.04` (`:31-34`)
  - spellComplete `0.095-0.157 → 0.19-0.28` (`:79-82`)
  - impact `0.08-0.125 → 0.157-0.235` (`:102-105`)

  The measured p90 sprite diameter at the landing peak is 0.022-0.040 model height in fable
  against 0.056-0.110 on the oracle.
- **M2. Emitter geometry and motion.**
  - spellComplete is a RING r=0.5 with BURST velocity (3.0 ±1 horizontal, 1.5 ±0.5 vertical,
    gravity −2) (`config/spell_effects.json:66-88`, `sim_particles.cpp:867-871`).
  - impact is RADIAL_EXPAND (4.0 horizontal, 1.0 vertical, gravity −2) at target origin +1
    (`:89-112`, `sim_particles.cpp:862-866`).

  Both stay below about 0.6 model height. The oracle draws columns, domes and swirls reaching
  ≥ 2-2.5 model heights.
- **M3. Landing duration.** Both landing styles run a 0.6 s emitter with 1.6-2.0 s particles
  (`config/spell_effects.json:69, 92`; the T422 values). Fable's landing therefore ends at
  Act+2.2-2.3 s. The oracle's ends at Act+4.85-6.52 s.
- **M4. The Source stage is a hand-bone pinpoint.** `createCastGlow` forces
  `orbitalRadius = 0.4` and `positionOffset = 0` on hand-bone emitters
  (`src/client/graphics/spell_visual_fx.cpp:417-421`) at 24/s per hand (`spell_effects.json:42`).
  The spray variant for Alteration and Abjuration is chosen at `:365-371`. The oracle's
  Source stage is a body-wide cloud covering 0.08-3.9 model².
- **M5. Population.** spawnRate × emitterLifetime gives ≈ 360 spellComplete + 288 impact
  particles (`spell_effects.json:67, 90`; 93/41 overrides at `:231-238`). For 253 the
  measured override is **7.4/s and 9.3/s with emitterLifetime 3.6** (`:241-257`), which gives
  ~27-33 particles; its max coverage is 0.033 model². The oracle's 253 fills the frame (coverage 7.28 model²). The T422 253
  override is the plainest population mismatch.
- **M6. A missing sprite file becomes a white circle.**
  - `GENA20_SPRITE` resolves to `spela3.tga`, which ships in neither the fable nor the oracle
    client directory.
  - fable falls back to `ParticleAtlas::SoftCircle`
    (`src/client/graphics/environment/spell_effects_config.cpp:91-97`).
  - `applyStageSpriteNames` still forces the modulation to white
    (`src/client/graphics/environment/spell_emitter_presets.cpp:170-174`).

  The oracle draws **cyan** for the same record (Source hue 199, Landing hue 196, sat 0.51).
  What the official client draws in place of spela3 is **oracle-owed**. A plausible source is
  record 33's own secondary sprite list (see M7), which names GENA10 = spela2, blue stars,
  and GEND00-20 = speld1-3, blue.
- **M7. The 12-entry secondary sprite list in each spells.eff stage is ignored.**
  `SpellsEffReader` keeps only the three slot names. The other 800 bytes per block are
  `unknown_bytes`, read by nothing (`include/client/spell/spells_eff_reader.h:21-91`,
  `src/client/spell/spells_eff_reader.cpp:34-59`). Measured consequences:
  - 54/656: the oracle Landing is dominated by violet **spelu1** squiggles (hue 243). spelu1
    is named in record 23's Source slots and in its Target block's secondary list. Fable's
    Target slots are speld2/spelu2 (cyan/white stars, hue 217).
  - 458: as in M6.

  How the official client uses the list (count, timing, which stage) is **oracle-owed**. The
  per-slot numeric arrays in the same 800 bytes (likely durations / scales / rates, decoded
  structurally in the evidence `mechanism_notes`) are likewise unused.
- **M8. Single-image 64×64 sheets are cut into 16 frames.**
  - `tools/spell_sprite_atlas_builder.cpp:139-175` assumes every sheet is a 4×4 grid.
  - spelc3.tga (a yellow-white star) and spelc4.tga (an orange flare) are 64×64
    **single images** (`review/B1118/sprites/sprite_sheet.png`).
  - fable therefore draws 16×16 fragments of them, playing as "frames".

  These are record 0's Target sprite (spelc3) and Source sprite (spelc4), i.e. 93/38. The
  oracle's 93/38 Landing opens with a large white-yellow flash at Act+0.5-0.9 s (f7537-7549,
  f9759-9769) that fable has no counterpart for. Whether that flash is spelc3 drawn whole is
  **oracle-owed**.
- **M9. `OP_DeleteSpawn(0)` deletes the current target, even when the target is yourself.**
  `EntitiesComponent::ZoneProcessDeleteSpawn` maps id 0 to "current target"
  (`src/client/entities/entities_component_appearance.cpp:73-78`).
  - The server sends DeleteSpawn(0) after every NPC death. This is in both pcaps, e.g.
    `oracle_leg` 18:59:54.518 and `fable_A1_selfdelete` 19:25:48.933.
  - In run A1, the self-targeted 458 killed a rodent. The local player 259 was erased
    (`Combat target 259 (Testcaster) despawned`), and every later `createCastGlow` logged
    `RH=false LH=false center=false`: no caster model, no anchors.
  - The official client keeps its caster through the same packet.
- **M10. The Middle (projectile) stage is never drawn.** `createProjectile`
  (`spell_visual_fx.cpp:512`) has no production caller (`spell_visual_fx.h:128-146`).
  `travelType` is unread. This tour cannot measure a Middle stage, because the target spawns
  at distance 0. That makes it **oracle-owed** (§6).

Not exercised by this tour, and so not graded:
- `createCastGlow`'s unfiltered `removeSpellEffectsForEntity(caster_id)`
  (`spell_visual_fx.cpp:378`). It would wipe a live landing if a new cast starts within
  about 2 s.
- The inert alpha fade under additive blend (`unified_particle_renderer.cpp:631-632`; the
  vertex alpha only feeds the discard).
- AE repetition (`irrlicht_bridge.cpp:1147-1149`). Fable's 458 drew 2 landings for 2 targets;
  the oracle's 458 hit only the caster, so no pair exists.

## 6. What could not be measured, and why

- **The Middle stage (projectile).** eqemu has no GM command to place a live NPC at a
  distance (B1107 §2), so every bolt here is at distance 0. A follow-up must move the
  *caster* away from a `#spawn`ed NPC, e.g. `#goto` or scripted walking, on both clients.
- **Separating spellComplete (caster) from impact (target).** These have coincident anchors
  for the same reason. Landing is the union of both.
- **Extents.** These are crop-limited at 4.28 × 2.57 model on the oracle; the oracle values
  are lower bounds.
- **Particle animation rate.** At 25 fps the per-sprite frame rate cannot be told apart from
  sprite turnover. It is not claimed.
- **Gem state before this unit.** Testcaster's `character_memmed_spells` before the
  calibration session was not snapshotted. It now holds the 7 gems 93/38/54/41/253/458/656.
  Class, level, zone and pose were restored by every leg; char 67 is class 12, level 1,
  zone 36.

## 7. FOLLOW-UP (fix cards, one per mechanism; oracle numbers only)

Workers do not cut cards; these are for the orchestrator.

1. **FX-SIZE (M1).** Raise the spell sprite sizes so that the **p90 sprite diameter at the
   landing peak** reaches the oracle band:
   - 93 0.074, 38 0.058, 54 0.064, 656 0.056, 41 0.110, 253 0.059, 458 0.067 model height
   - Source: 93 0.070, 38 0.077, 41 0.104, 253 0.075, 54 0.032, 656 0.031, 458 0.038

   Fable today is 0.022-0.040 (Source 0.014-0.019). Gate: `scripts/fx_blobs.py` on a re-capture.
2. **FX-VOLUME (M2).** Reshape the landing emitters, spellComplete and impact, from a
   waist-high disc to the oracle's volume. Peak coverage targets:

   | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
   |---|---|---|---|---|---|---|---|
   | model² | 5.85 | 3.89 | 2.46 | 2.49 | 4.15 | 7.28 | 1.24 |

   Height ≥ 2 model heights for 93/38/41: the column/pillar frames f7573, f9800, f6448.
3. **FX-DURATION (M3).** Make the landing last from OP_Action + 0.40-0.80 s (onset, already
   matching) to an end of:

   | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
   |---|---|---|---|---|---|---|---|
   | end after Action (s) | 4.87 | 4.85 | 5.10 | 5.00 | 6.22 | 6.52 | 4.94 |

   That is 4.3-5.8 s active, against fable's 1.6-1.9 s. The T422 "1.6-2.0 s" lifetime is a
   per-particle life on llvmpipe-era evidence. It must not be read as the effect duration.
4. **FX-SOURCE (M4).** Replace the hand-bone pinpoint glow with a body-wide Source emitter.
   - Onset BeginCast + 0.20-0.45 s, lasting to OP_Action.
   - Peak coverage:

     | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
     |---|---|---|---|---|---|---|---|
     | model² | 0.38 | 0.38 | 0.48 | 0.63 | 1.23 | 3.92 | 0.08 |

   - Extent 1.8-3.9 model wide, centroid 0.7-1.2 model above the feet.
5. **FX-253-OVERRIDE (M5).** The T422 253 override (7.4/9.3 per s) yields max coverage 0.033
   model² against the oracle's Source 3.92 and Landing 7.28. Re-measure and replace it. It is
   a measured-but-wrong override, so the rule "no invented overrides" does not protect it.
5a. The same check applies to the 93/41 overrides (93 Landing 0.160 vs 5.85; 41 0.128 vs
    4.15).
6. **FX-MISSING-SPRITE (M6).** spela3 (`GENA20`) must not draw a white SoftCircle. The oracle
   measures cyan: Source RGB 124,165,183 / hue 199; Landing RGB 90,160,185 / hue 196 / sat
   0.51. **Oracle-owed first**: identify which art the official client draws (frame-step
   `oracle_leg` f7711-7824 against `sprites/`).
7. **FX-SECONDARY-LIST (M7). Oracle-owed decode.** Decode and route the stage blocks'
   secondary sprite list and per-slot arrays. Targets: 54/656 Landing hue 243 / sat 0.47
   violet spelu1 squiggles (fable 217 / 0.25). The decode must be proven on the oracle per
   field before any value is used.
8. **FX-ATLAS-SINGLE-IMAGE (M8).** Pack 64×64 single-image sprites (spelc3, spelc4, and any
   other non-grid sheet; census them) as one frame, not 16 fragments. The oracle reference is
   93/38's Act+0.5-0.9 s white-yellow flash.
9. **DELETESPAWN-ZERO (M9, class A, not FX).** `OP_DeleteSpawn(0)` must not delete the local
   player (and arguably nothing: the server follows it with a real `DeleteSpawn(<id>)` 1.0 s
   later in every pcap here). Repro: `fable_A1_selfdelete/` (self-target, AE kills an NPC).
10. **FX-MIDDLE (M10). Oracle-owed measurement.** Capture the Middle stage at distance, as
    in §6, before any projectile work.
11. ~~**ORACLE-RIG (process).** Plans that click gems must use a character of a class that can
    cast the spell, or the official client refuses the cast. Record the gem pitch per lane:
    lane 5 is (25, 213+30i).~~ **Done by B1129 (#449):** `scripts/eqemu/oracle/gem_pitch.tsv`,
    `oracle_plan_check.py` (the driver refuses such a plan), `oracle_class_leg.sh`.
