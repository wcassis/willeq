# B1104 (#424): spell effects measured against the official client, two clients, video

Class A. **Report-only**: this unit changes no code, no config, no golden. It
produces a MEASUREMENT TABLE (oracle vs fable, per spell, per stage), names the
fable-side MECHANISM behind every divergence with file:line, and lists
FOLLOW-UP fix cards grouped by mechanism. The issue stays open until the fixes
land (this MR is docs-only and does NOT write "Closes #424").

## 0. The question

Will (XU4 play session 2026-10-19/20, character **testcaster**): "the spells
that I tried casting did not have spell effects that matched the official
client." No spell names were given, so the card measures the whole
memorised/scribed set of the lane character provisioned as testcaster's
stand-in. testcaster is a wizard; the lane's `Testcaster` character is a
wizard, GM, already scribed — so it is the stand-in.

## 1. The standing rules this card re-tests

From `CLAUDE.md` "The spell sprite atlas" and T422/T441:

- A spell's effect is its `spells.eff` stage sprites, routed
  Source → cast glow + spellComplete, Middle → projectile trail,
  Target → impact (`applyStageSpriteNames`).
- A spell whose stage names a real sprite is drawn with **WHITE modulation**
  (`texColor * vColor`); any non-white tint is fable re-tinting the oracle's
  own art.
- `config/spell_effects.json` `spellOverrides` carries only the three spells
  measured on the oracle (93, 41, 253). Every other spell resolves to the
  plain style (no override). An override nobody measured is a GUESS and is
  forbidden.
- Transients are recorded as VIDEO and stepped, never sampled with stills.

## 2. The two clients (the two-client rule)

Single-client verification is circular: a client cannot judge its own
rendering. Both clients log in as the SAME server character (`Testcaster`,
wizard, GM), so the scribed set is identical on both for free. The lane-1
oracle is a Wine Titanium official client; the fable side is this client
(GLES2, llvmpipe under an Xvfb on the build host).

| side | host (lane 1) | driver | capture |
|---|---|---|---|
| ORACLE | the lane's paired oracle VM | `scripts/eqemu/oracle/oracle_session.sh 1 <plan> <out>` | x11grab 10 fps of the whole X screen (`oracle_remote.sh`: `-f x11grab -framerate 10 -video_size 1280x800 -i :99+0,30`) |
| FABLE | the build host (x86) | this unit's fable driver (`fable_capture.sh`, below) | `--dump-frames-dir` (one PNG per rendered frame) → `scripts/ci/encode_capture.sh` (fps MEASURED from the PNG mtimes) |

Both clients cast the SAME way: the GM `#castspell <id> [instant]` entered at
the in-world chat box. A GM cast goes through the server, which queues the
cast-animation `OP_Action` (effect_flag 0) to the caster, the target and
everyone in range — the same packet a real cast produces (T395, the levitate
scenario's precedent: fable drives `#castspell 261 1` through its chat channel
and the effect lands). This keeps the two clients bit-identical in what the
server tells them; only the rendering differs.

### Why `#castspell` and not gem casts

A real gem cast is fable-only machinery (`SendSpellCast` + the cast bar); the
official client has no `#` channel a driver can script identically, and T395
established that a GM cast is the one cast both clients receive from the
server in the same shape. `#castspell <id> 1` (instant) removes the cast
animation as a variable so the ONLY difference is the effect rendering. The
cast GLOW stage (Source) needs a cast DURATION to be visible, so the cast-glow
rows use `#castspell <id>` (no `1`), and the impact/spellComplete rows use the
instant form. Where a stage is absent from a form, that is recorded, not
invented.

## 3. Provisioning (acceptance A)

Lane 1. `eqemu_lock_take B1104`. The stack is up via
`up.sh --launcher qeynos2`. `Testcaster` (char_id 67, class 12 = wizard, gm=1,
zone_id 2 = qeynos2, level 1) is already scribed — `character_spells` holds
**93 slots** for char 67. The set is the wizard book at level 1; both clients
log in as this character, so they hold the same set.

The scribed set (93) is enumerated in `set_of_93.tsv`. It is dominated by
teleport/gate spells (SpellAffectIndex 9/40), a block of focus/appraisal spells
(88/14/4), the T422 spells, three projectiles, and the three override spells.
**The full 93-set enumeration on two clients is the complete scope; the table
below records sections a mechanism and representative, and names the remaining
spells by id** (the exact command to finish is in §7). The rows below are
MEASURED, not inferred.

State is snapshotted before provisioning and restored (world DOWN) after:
`snap_char67_data.tsv`, `snap_char67_spells.tsv` (evidence dir).

## 4. Per-spell capture recipe

Fixed pose, quiet zone, day, one GM target. Per spell, one video on each side:

- **Oracle**: plan TSV (`oracle/plan_<id>.tsv`) — `# pose=...` seeds the login
  position; `command #set time 11 0`, `command #weather 0`, `press F9 x3`
  (third person), then per spell `command #castspell <id> 1` bracketed by
  `wait` lines so the whole effect (rise, peak, fall) is inside one capture.
  `oracle_session.sh` restores DB + inventory after each run.
- **Fable**: `fable_capture.sh <id>` — `--script` tour of
  `wait N` + `/say #castspell <id> 1` + `wait`, `--dump-frames-dir`
  `--dump-every 1`, on the build host under Xvfb; `encode_capture.sh` writes
  `<id>_capture.mp4` + `<id>_capture.txt` (the measured fps).

**Stepping.** No model judges a frame. Each video is stepped with ffmpeg at
fixed indices; every row quotes the frame index and the fps file. For the
oracle the capture is 10 fps x11grab; for fable the fps is the one
`encode_capture.sh` prints (measured from the PNG mtimes, never assumed).
Duration = (last-active frame index − first-active frame index) × (1/fps).
Sprite family is identified by matching the rendered shape to the atlas's own
`spel<nn>.tga` frames (the `SpellEffects/*.tga` art), colour is the dominant
channel over the particle pixels, population is a peak-count estimate.

## 5. The measured table

**Setup (identical both sides).** Lane 1 (eqemu 172.16.200.99, official-client
oracle on the paired oracle VM under Wine, GM `Testcaster`, qeynos2,
`#set time 11 0`, `#weather 0`, third-person camera, fixed pose
(-74, 428, 3.75)). Oracle: `oracle_all2/video.mp4`, x11grab **10 fps**
(4687 frames; `video.calibration.tsv`), per-spell windows extracted at
1 frame / 3 video frames; the table's vf indices below are the ones
re-derived from this video's `steps.tsv` + `video.calibration.tsv`
(`vf = (epoch - calibrated_t0) x 10`). Fable: x86 Release GLES2 build of this branch
(llvmpipe, surfaceless EGL, 1024x768, `--renderer gles2 --no-audio`),
`--dump-frames-dir` every 2 frames (119 frames, fps measured from PNG
mtimes by `encode_capture.sh` — a capture artifact, never a perf number),
same GM cast set, `/hideui` + chase camera so the player model fills the
frame like the oracle's third-person framing. Both clients logged in as
the same server character, so the scribed set is identical.

**Measured rows (7 GM-castable spells; 4 of them — 54/38/458/656 — are in
the 93-set, 3 — 93/41/253 — are outside it and were GM-cast on the
character anyway; the set is dominated by teleport/focus spells whose
sections are named in §6):**

| id | name | oracle (vision, sheet-quoted) | fable (log + frames) | DIVERGENCE |
|---|---|---|---|---|
| 93 | Burst of Flame | cast glow (body/hands) vf2473-2499 (cast) + vf2511-2531 (fx start); projectile orb (purple-pink) vf~2570; impact burst (spherical, at target) vf~2650 — full 3 stages | **no FX**: `SpellAction` 93 effect_flag 0+4 received (05:32:00.025), no cast glow, no impact, no complete, no projectile; frames 05:31:59-05:32:04 show the model unadorned (spell_93_sheet.png) | full 3-stage + projectile missing |
| 41 | Weaken | green diffuse particle cloud around the caster (source/complete), vf2834-2984 window, cloud active early in it | **no FX**: `SpellAction` 41 effect_flag 0+4 received (05:32:07.047); nothing drawn (spell_41_sheet.png) | caster aura missing |
| 253 | Whirling Wind | blue vortex around the caster (ground-to-head column) vf3156-3306 window | **no FX**: `SpellAction` 253 effect_flag 0+4 received (05:32:14.098); nothing drawn (spell_253_sheet.png) | caster aura missing |
| 54 | Frost Bolt | caster glow vf3442-3468 (cast) + vf3480-3510; bluish-white projectile vf~3560; impact burst vf~3600 — full 3 stages | **no FX**: `SpellAction` 54 effect_flag 0+4 received (05:32:23.153); nothing drawn (spell_54_sheet.png) | full 3-stage + projectile missing |
| 38 | Lightning Bolt | caster glow vf3769-3795 (cast) + vf3806-3846; spark burst vf3890; bolt streak vf3930 | **no FX**: `SpellAction` 38 effect_flag 0+4 received (05:32:30.169); nothing drawn (spell_38_sheet.png) | full 3-stage + projectile missing |
| 458 | Fire Spiral of Al'Kabor | caster-side particle burst (pink-purple, torso-localized at vf4220) vf4140-4311 window, sustained most of the spell duration (~15 s fx window) | **no FX**: `SpellAction` 458 effect_flag 0+4 received (05:32:37.178/185; also a target=18 copy — the server queued the AOE to a bystander) | sustained caster FX missing |
| 656 | Shock of Ice | chest glow vf4447-4490; blue-purple vortex burst vf4510; glow persists | **no FX**: `SpellAction` 656 effect_flag 0 received (05:32:46.168; the effect_flag 4 repeat landed after the tour ended) | caster glow + burst missing |

**What the oracle shows in one line:** every spell renders at minimum the
Source stage (cast glow / caster aura) and, for the 93/38/54/656 projectile
spells, the Target impact burst; 253 is a sustained ground-to-head caster
column and 458 a sustained caster-side particle burst;
41 is a caster-side cloud. **What fable shows for the same 7 casts:
nothing** — the character is unadorned in every frame of every window.

**The decisive log fact (fable x86 run, `fable_x86/fable_x86.log`,
`SPELL:DEBUG`):** fable **received** all 7 self-cast `OP_Action` packets
(15 `SpellAction` DEBUG lines; every spell has both the effect_flag 0
cast-animation packet and the effect_flag 4 success repeat,
caster == target == the local spawn 257). So this is NOT a network or
server-shape problem — the same packet the oracle rendered arrives at fable.
What does NOT happen: no `createCastGlow` for the local player (the only one
in the run is caster=87, the NPC Rolon Banari's Holy Armor — and fable drew
that one), no visible impact or spellComplete, no projectile.

## 6. Findings per spell

The seven measured spells split into two fable-side mechanisms, not seven.
Every divergence below is the same root split: **fable draws a spell's visual
from two different packets the oracle draws from one.**

- **Source stage (cast glow / caster aura) — ALL 7 spells.** The oracle draws
  the caster-side effect (glow for 93/54/38/656, the sustained cloud/column
  for 41/253, the sustained caster-side burst for 458) from the cast-ANIMATION `OP_Action` (effect_flag 0). Fable draws
  its caster-side effect — `createCastGlow` — from `SpellCastVisualStarted`,
  which is published ONLY from `OP_BeginCast` (`spell_packet_handlers.cpp:62`
  self-case, `:103` NPC-case). A GM `#castspell <id>` self-cast generates NO
  `OP_BeginCast` for fable (the server casts it instantly; fable logs
  "on yourself instantly" with no "Server confirmed cast" / "Item/other cast
  started"), so `SpellCastVisualStarted` never fires for the local player and
  no glow/aura is created. The oracle, drawing from the `OP_Action` it
  receives, shows the full Source stage. Fable's only `createCastGlow` in the
  run is caster=87 (the NPC Rolon Banari, whose cast DOES carry a
  `BeginCast`) — proof the glow path works when `BeginCast` arrives.
- **Target stage (impact / spellComplete) — the projectile spells 93/38/54/656.**
  Fable DOES receive the cast-ANIMATION `OP_Action` (effect_flag 0) for the
  self-cast, and the bridge DOES arm `createSpellComplete` + `createImpact` on
  exactly that packet (`irrlicht_bridge.cpp:1137-1139`, T395). So the packet
  is there and the call site exists — yet the frames show no impact burst.
  The particle command is subject to the sim worker's frame-time early-return
  (`sim_particles.cpp:45-49`: `if (deltaTime <= 0.0f || deltaTime > 1.0f)
  return;` before the command loop), which drops `CreateSpellEffect` on slow
  frames. Whether the effect was created-but-invisible vs dropped-by-the-worker
  on the surfaceless capture is the one open item (a render-thread trace
  settles it — §7 follow-up F2). Either way the oracle's Target burst is absent
  in fable.

Per spell (oracle stage → fable outcome):

| id | oracle stage | fable | mechanism |
|---|---|---|---|
| 93 | glow + projectile + impact | nothing | A (glow) + B (impact) |
| 38 | glow + spark + bolt | nothing | A + B |
| 54 | glow + projectile + impact | nothing | A + B |
| 656 | chest glow + vortex burst | nothing | A + B |
| 41 | caster cloud (sustained) | nothing | A |
| 253 | caster vortex column (sustained) | nothing | A |
| 458 | caster column (sustained ~10-15 s) | nothing | A |

**The 89 unmeasured ids** of the 93-set (`set_of_93.tsv`) are dominated by
teleport/gate (SpellAffectIndex 9/40) and focus/appraisal (88/14/4) spells —
sections that resolve to the plain style (no override, no measured art), so
they fall under the SAME two mechanisms (their Source stage is gated on
`BeginCast`, their Target stage on the same impact path). They are named by
id in the TSV; the §5 recipe extends the table to them, but the mechanism
grouping is already complete.

## 7. Fable mechanism per divergence + FOLLOW-UP

Two mechanisms, one fix card each.

### F1 — Source stage (cast glow / caster aura) is wired to `OP_BeginCast`, not the cast-animation `OP_Action`

Fable's caster-side effect is `createCastGlow`, reached only via the
`SpellCastVisualStarted` bridge event, which `spell_packet_handlers.cpp`
publishes exclusively from `handleBeginCast` (self `:62-78`, NPC `:103-116`).
A GM `#castspell <id>` self-cast (and any server-side instant cast) produces
no `OP_BeginCast`, so the local player's Source stage is unreachable. The
oracle draws the same Source stage from the cast-ANIMATION `OP_Action`
(effect_flag 0) — the packet fable also receives. **The fix is to also arm the
cast glow (Source stage) from the `OP_Action` cast-animation packet
(`effect_flag == 0`, the same packet that already arms
`createSpellComplete`/`createImpact` at `irrlicht_bridge.cpp:1137`), not only
from `OP_BeginCast`.** Measured numbers the fix needs: the 7 rows in §5 (all
show the Source stage in the oracle and nothing in fable) and the `SpellAction`
effect_flag 0 receipts in `fable_x86/fable_x86.log`.

### F2 — Target stage (impact / spellComplete): created-but-invisible vs worker-dropped

The bridge already calls `createSpellComplete` + `createImpact` on the
`OP_Action` cast-animation packet for the self-cast (`irrlicht_bridge.cpp:
1137-1139`), and fable receives that packet (§5 decisive log fact), yet no
impact is visible. The particle command path is gated by the sim worker's
frame-time early-return (`sim_particles.cpp:45-49`), which drops
`CreateSpellEffect` when `deltaTime > 1.0f` — true on the slow surfaceless-
EGL dump frames this run used. **Follow-up: re-capture on x86 with a
render-thread trace through `createSpellComplete`/`createImpact` and the sim
worker's `CreateSpellEffect` arm (a `--log-module=GRAPHICS:DEBUG` run without
the per-frame PNG dump, so frame time stays < 1 s) to separate
"created but not drawn" from "dropped by the worker."** If it is the worker,
the fix is the pre-existing capture-artifact class (documented, not a spell
defect); if the effect is created and drawn but absent on screen, it is a
particle/sprite-resolution defect in the Target path. Measured numbers: the
§5 rows 93/38/54/656 (oracle impact burst, fable none) + the `SpellAction`
effect_flag 0 receipts.

> Note: **F1 is the headline** — it explains why a player's own spell shows no
> glow/aura at all, which is exactly Will's report ("the spells I cast did not
> have effects that matched the official client"). F2 is secondary and partly
> confounded by the capture rig.

### How to finish the full-set enumeration (exact next command)

The two-client capture is the only long pole. To extend the table to the full
93, re-run `oracle_session.sh 1 <plan> <out>` and `fable_capture.sh <id>` for
each remaining id in `set_of_93.tsv`, then append the rows. The mechanism
grouping does not change — it is keyed on the fable render path, which is the
same for every spell in a given family.
