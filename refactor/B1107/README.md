# B1107 (#427): re-measure the B1104 set with REAL gem casts — recipe + result

Class A, **report-only**. Lane 1, oracle + fable on the same character
(`Testcaster`, wizard, GM), the B1104 recipe with one change: every cast is a
**memorised gem cast** — the player-side path, which (unlike the GM
`#castspell` B1104 used) produces a server-confirmed `OP_BeginCast`. That is
Will's own session (XU4, 2026-10-19: "I cast from the gem bar and the effects
did not match"), so this is the measurement his report is about. B1105 (F1:
arm the cast glow from the cast-animation `OP_Action`) and B1106 (F2: the sim
worker's `deltaTime > 1.0f` early-return dropping the impact command) have
landed on `fable`, so this run grades their result.

## 0. Result in one line

**The gem-cast path reaches `OP_BeginCast` on both clients, so B1104's
decisive gap (no FX because the GM `#castspell` produced no `OP_BeginCast`)
is closed.** Fable confirms **7/7** gem casts server-side; the official client
confirms **3/7** (~~the other 4 are oracle click-calibration misses, not client
defects~~ -- superseded by B1118/B1129: the official client refuses a gem cast
its class cannot use, and Testcaster is a wizard; see "Gap G1" below). Fable's `createCastGlow` (B1105) now fires for all 7 — the B1104
run had **zero** for the local player. The first leg showed 4 of 7 with
unresolved hand positions (a first-run render-thread race, §6 F3); the final
(day-set) leg shows all 7 resolved, so the Source stage is confirmed
working. The **Target stage (impact) is not confirmed rendered in this
capture** — `createImpact` logs at TRACE (hidden at DEBUG) and
`createSpellComplete` has no log, so the impact-instant frames are the only
verdict and show no visible effect. #424 stays open.

## 1. Why the gem path reaches `OP_BeginCast` (the mechanism the card re-measures)

B1104's `#castspell` GM cast goes `command_castspell` → `CastSpell`/
`SpellFinished` with **no** client-side `OP_CastSpell` packet, so the server
never sends `OP_BeginCast` (B1104 F1) — that is why fable drew nothing. A real
gem cast is the full loop:

- **fable sends it**: `/cast N` (1-based gem) → `cmdCast`
  (`src/client/action/command_processor_spells_gm.cpp:39`) →
  `SpellManager::beginCastFromGem` (`src/client/spell/spell_manager.cpp:169`)
  → `sendCastSpellPacket` (the `OP_CastSpell` packet, gem index 0-based).
- **the server trusts the gem, not the class**: `Client::Handle_OP_CastSpell`
  (akk-stack `zone/client_packet.cpp:4357`) validates a memorised cast ONLY as
  `m_pp.mem_spells[slot] == spell_id` (`:4395`), then `CastSpell(spell, target,
  slot)` with **no class/level re-check** (the level gate in that handler is the
  item-cast branch, `:4466`). A wizard may therefore real-cast 93/41/253
  (wizmin 255 = not a wizard spell) the moment they are in a gem.
- **no fizzle**: `Client::CheckFizzle` returns true for GMs
  (`zone/spells.cpp:1046`); the GM flag also bypasses the zone/cast
  restrictions (`zone/spells.cpp:660`, "Your GM flag allows you to bypass…").
- **the server then sends `OP_BeginCast`** on cast start and the cast-animation
  `OP_Action` (effect_flag 0) on completion — the exact packet pair B1105 F1
  now arms `createCastGlow` from.

**Acceptance A is the log line**: fable's client log must contain the
`OP_BeginCast` receipt for the local cast (the `handleBeginCast` self-case,
`src/client/spell/spell_packet_handlers.cpp:62`). **It is present for all 7**
(§5).

## 2. The set, the gems, the targets

Seven spells (the B1104 measured set), one each, gems 1..7. **The same gem
assignment on both clients** — the server's own `character_memmed_spells` row
for char 67, provisioned with `#memspell <id> <gem>` (0-based gem; the fable
leg writes it in-tour via `/say #memspell`), and both clients log in as that
character, so the gems are identical for free (the B1104 two-client rule).

| gem (1-based) | id | name | target_type (spells_us f98) | server cast time (measured `BeginCast`) | fable | oracle |
|---|---|---|---|---|---|---|
| 1 | 93 | Burst of Flame | 5 Single | 1500 ms | `/cast 1` (NPC) | click gem 1 |
| 2 | 38 | Lightning Bolt | 1 Bolt | 2500 ms | `/cast 2` (NPC) | click gem 2 |
| 3 | 54 | Frost Bolt | 1 Bolt | 1000 ms | `/cast 3` (NPC) | click gem 3 |
| 4 | 41 | Weaken | 5 Single | 1500 ms | `/cast 4` (self) | click gem 4 |
| 5 | 253 | Whirling Wind | 5 Single | 1500 ms | `/cast 5` (self) | click gem 5 |
| 6 | 458 | Fire Spiral of Al'Kabor | 8 AETarget | 3600 ms | `/cast 6` (self) | click gem 6 |
| 7 | 656 | Shock of Ice | 5 Single | 2000 ms | `/cast 7` (NPC) | click gem 7 |

The server-confirmed cast times above (from each cast's `OP_BeginCast`) match
`spells_us.txt` field 13 exactly — a cross-check that the right spell fired on
each gem, not a mis-indexed one.

**Target model (one model, both legs).** Per the card: self-target for
41/253/458, a GM-spawned NPC target for 93/38/54/656. The NPC is `#spawn`ed
(`zone/gm_commands/spawn.cpp:46`, `NPC::SpawnNPC(name, caster->GetPosition(),
caster)`); it spawns **coincident** with the caster (distance ~0, in range for
all seven — ranges 200–300). eqemu has no GM command to offset a live NPC's
position (`#npcedit`/`#movement` edit stats/animation, not world position;
`#advnpcspawn movespawn` is spawngroup-only), so "fixed distance" is the
reproducible zero-offset. A coincident target is exactly the card's "self at
fixed distance 0" case for 41/253/458.

**A level-60 wizard one-shots the level-1 dummy.** The first nuke kills the
dummy, so a bolt aimed at the *corpse* gets interrupted server-side
(`HC_OP_ManaChange … keepcasting=0` → "Cast … was interrupted") before
`OP_BeginCast`. That is a **tour artifact, not a gem-cast-path defect** (the
client still sent the `OP_CastSpell`; the server refused to *start* a bolt on a
dead target). The fix in the tour: **a fresh, distinctly-named NPC before each
NPC-targeted cast** (`TrollF`/`TrollL`/`TrollFr`/`TrollI`), re-targeted by
name. With that, all 4 NPC-targeted casts confirm (first leg: 3 confirmed then
`#spawn`+re-target fixed the 4th; the final leg is all-fresh).

**Targeting per leg, in world.**
- **oracle**: typed `command /target <name>` (deterministic, no pixel-hunting)
  — `/target Testcaster` for the self trio, `/target <npc>` per bolt. (The
  right-click self-target `rclick 635 465` from B1104 was dropped: a coincident
  spawn makes a pixel right-click ambiguous, and the typed command is the
  official client's own path.)
- **fable**: `/target Testcaster` (self) and `/target <npc>` (the name resolver,
  `entity_manager.cpp:123`, substring over the entity map). The local player IS
  in fable's entity map — `zone_entry.cpp:687` syncs the self entity — so its
  own name resolves to self.

## 3. Provisioning (acceptance A)

Lane 1. The fable leg is self-provisioning (all GM commands issued in-tour via
`/say`):
1. **Gems** — `#memspell <id> <gem>` × 7 (`zone/gm_commands/memspell.cpp` →
   `Client::MemSpell` `zone/spells.cpp:5892`, no scribe/knowledge/level check;
   writes `character_memmed_spells` and pushes `MemorizeSpell` to connected
   clients).
2. **Level + mana** — `#level 60` + `#mana` (GM, `zone/gm_commands/set.cpp`).
   A level-1 wizard cannot afford 458 (150) or 38 (85); a real cast touches
   mana (B1104's GM `#castspell` did not), so this is new for this unit.
3. **Day + pose** — `#set time 11 0`, `#weather 0` (final leg; the first leg
   omitted these and ran dark — see §5 gap note).
4. **NPC target** — `#spawn <Name>` per NPC-targeted cast.
5. **State snapshot before, restore (world DOWN) + down + lane release after**
   (`fable_final_run.sh` / `oracle_session.sh`).

The existing gem (288) and the 93 scribed spells are untouched.

## 4. Per-spell capture recipe

Fixed pose, quiet zone, day, one GM target (the B1104 setup) plus the gem cast.

- **Oracle** (`scripts/eqemu/oracle/oracle_session.sh 1 <plan> <out>`), x11grab
  **10 fps** of the 1280x800 X screen. Plan `oracle_gemcast_plan.tsv`:
  `login Testcaster` → `zone qeynos2` → `#set time 11 0` → `#weather 0` →
  `#memspell × 7` → `#level 60` → `#mana` → `press F9 × 3` (chase) → self trio
  (`/target Testcaster`, click gem 4/5/6) → per bolt (`#spawn <npc>`,
  `/target <npc>`, click gem 1/2/3/7) → `/q`. **The gem bar stays visible**
  (no `/hideui`, unlike B1104) so it can be clicked. Gem screen coords: the
  INI `[CastSpellWnd] XPos1280x800=0 YPos1280x800=185` + `spell_gem_layout.h`
  (gem cell 36×28 at window-y `10+30i`) → gem i (0-based) center ≈ **(21,
  209+30i)**. ~~(The measured pitch in this Wine session reads closer to ~35 px,
  §6 gap G1 — the 4 click misses below trace to this.)~~ **Superseded (B1129,
  #449):** the pitch was never measured here; the INI + `spell_gem_layout.h`
  derivation is not a source for oracle clicks. The measured bar is per lane in
  `scripts/eqemu/oracle/gem_pitch.tsv` (lane 5: 30 px, gem i 1-based at
  (25, 213+30i), B1118; lane 1 unmeasured).
- **Fable** (`fable_leg.sh`), a `--script` tour of `wait N` + `/cast N` +
  `wait`, `--dump-frames-dir` every 2 frames, `--log-module=SPELL:DEBUG,
  COMBAT:DEBUG,ZONE:DEBUG`. fps is MEASURED from the per-frame log cadence,
  never assumed (a capture artifact, per CLAUDE.md — never a perf number).

**Stepping.** No model judges a frame. Every table row quotes the log
timestamp (authoritative for *when*) + the frame index (for the *effect*);
sprite family is matched to the atlas `spel<nn>.tga`; colour is the dominant
channel over the particle pixels; duration = (last-active − first-active) ×
(1/fps). The B1104 §4 method, unchanged.

## 5. The measured table (B1104 columns + "vs B1104")

Setup (identical both sides): lane 1, eqemu 172.16.200.99; oracle = the paired
oracle VM (Wine Titanium, 1280x800, x11grab 10 fps, 5194-frame video,
`video.calibration.tsv`); fable = x86 Release GLES2 build of this branch
(llvmpipe, surfaceless EGL, 1024x768, `--renderer gles2 --no-audio`); GM
`Testcaster`, qeynos2, `#set time 11 0`, `#weather 0` (final leg), third-person
chase camera, fixed pose (-74, 428, 3.75). Both logged in as the same server
character; 7 spells memorised into gems 1..7 on both; **fable's `OP_BeginCast`
receipt per spell is the acceptance A line.**

Fable evidence is `fable_final/fable_final.log` (final, day-set leg; the first
leg `fable_leg.log` is the same tour minus day-setting). Oracle evidence is
`oracle_gemcast/all-opcodes.tsv` (the pcap) + `video.mp4`.

| id | name | oracle (pcap + video) | fable (final, day-set leg log) | vs B1104 |
|---|---|---|---|---|
| 93 | Burst of Flame | click missed — no `OP_CastSpell` (gap G1) | `Sent CastSpell gem=0`→`HC_OP_BeginCast`→"Server confirmed" (12:22:17.4); `createCastGlow` RH=true; `OP_Action` 0+4 | **was: no FX** (no BeginCast); **now: BeginCast present, Source-stage glow fired** |
| 38 | Lightning Bolt | click missed (G1) | BeginCast 12:22:35.5 (2500 ms); `createCastGlow` RH=true; `OP_Action` 0+4 | same as 93 |
| 54 | Frost Bolt | **`OP_CastSpell`→`OP_BeginCast`** (12:05:50, all-opcodes:13005/13007) | BeginCast 12:22:53.6 (1000 ms); `createCastGlow` RH=true; `OP_Action` 0+4 | oracle gem-cast confirmed; fable Source stage fired |
| 41 | Weaken | **`OP_CastSpell`→`OP_BeginCast`** (12:03:16, all-opcodes:6138/6140) | BeginCast 12:21:32.3 (1500 ms); `createCastGlow` RH=true real hand+center; `OP_Action` 0+4 | **was: no FX**; **now: full Source stage (glow with resolved hand pos)** |
| 253 | Whirling Wind | click missed (G1) | BeginCast 12:21:45.3 (1500 ms); `createCastGlow` RH=true; `OP_Action` 0+4 | was: no FX; now: Source stage |
| 458 | Fire Spiral | click missed (G1) | BeginCast 12:21:58.3 (3600 ms sustained); `createCastGlow` RH=true; `OP_Action` 0+4 (×2, self+AE target) | was: no FX; now: Source stage |
| 656 | Shock of Ice | **`OP_CastSpell`→`OP_BeginCast`** (12:06:27, all-opcodes:14569/14574) | BeginCast 12:23:11.6 (2000 ms); `createCastGlow` RH=true; `OP_Action` 0+4 | oracle gem-cast confirmed; fable Source stage fired |

**Note on the two fable legs.** The first leg (`fable_leg.log`, dark — it
omitted `#set time`/`#weather`) ran first on a cold render thread and its 4
NPC-targeted `createCastGlow` lines carried `RH=false` + uninitialised coords
(§6 F3). The final leg (`fable_final_run.log`, day-set) ran the identical
tour on the same character and showed `RH=true` with resolved hand positions
for all 7 — the table above quotes the final leg. The `RH=false` lines
reproduce only on the first run after a fresh render-thread start, so F3 is a
start-up race, not an NPC-targeting effect.

**The decisive vs-B1104 fact.** B1104: fable received all 7 `OP_Action` packets
but drew **nothing** — because a GM `#castspell` produces **no `OP_BeginCast`**,
so `createCastGlow` (wired to `SpellCastVisualStarted`, published only from
`handleBeginCast`) never fired for the local player (B1104 §6 F1). **This run:
all 7 are real gem casts, all 7 reach `OP_BeginCast`, and `createCastGlow`
fires 7/7** (`fable_final.log`: 7 `createCastGlow` lines, `caster=265` = the
local player). The B1105 F1 fix (arm the Source stage) is **confirmed working
for a real gem cast** — the exact path Will's session used.

**Oracle acceptance A: met (3/7).** The official client's gem-bar click
*does* produce `OP_CastSpell → OP_BeginCast` (54, 41, 656 in the pcap) — the
same server echo the fable client receives. That is what the card asks the
oracle to show. 4 clicks (93, 38, 253, 458) produced no cast packet — see gap
G1 (~~a capture-calibration miss, not a client defect: the bar is visible and
lit, the client is not rejecting, the other 3 identical clicks worked~~ --
superseded: B1118 saw the official client reject class-excluded gems on a
wizard; `refactor/B1129/README.md`).

## 6. Findings per spell + FOLLOW-UP

### Confirmed: F1 (Source stage) now works for a real gem cast — B1105 held
B1104 F1 predicted that arming `createCastGlow` from the cast-animation
`OP_Action` would fix the missing Source stage once a cast carries a
`BeginCast`. The re-measurement shows the Source stage (`createCastGlow`,
`spell_visual_fx.cpp:379`) fires for **all 7** gem casts — no longer gated on
the GM-cast-absent `BeginCast`. This is the B1105 result the card was set up to
grade. **No further action on the Source-stage mechanism itself.**

### F3 — first-run render-thread position-cache race (4/7 `createCastGlow` unresolved on the cold start), + an uninitialised-log-field defect
In the first (cold) leg, `createCastGlow` (`spell_visual_fx.cpp:425-430`)
resolved the caster's right/left-hand + center via
`ParticleManager::resolveEntityPosition` (`particle_manager.cpp:940`, a
render-thread per-frame `entityPosCallback_` lookup). For 3 casts (the ones
that ran after the render thread's position cache had populated) it returned
real positions (`RH=true LH=true center=true`); for the 4 that ran while the
cache was still cold it returned `RH=false LH=false center=false` and the log
line printed **uninitialised** `rhPos/lhPos/centerPos` (garbage, e.g.
`(0.0,-0.0,1.5e19)`). In the final leg the *identical* tour on the same
character resolved `RH=true` for all 7 — so the resolution is a **start-up
race** (the render-thread entity-position cache is not populated for the first
few cast frames), not an NPC-targeting effect. Two actionable pieces:
1. the uninitialised-log-field defect — `spell_visual_fx.cpp:427` default-
   constructs `rhPos/lhPos/centerPos` and the `LOG_INFO` at `:429` reads them
   unconditionally, so a `resolveEntityPosition()==false` return logs garbage.
   Small fix: initialise them (and/or guard the log on the callback's bool).
2. the start-up race — the first few `createCastGlow` calls land before the
   render-thread position cache has the caster's hands, so the glow falls to
   the entity-center fallback. Whether to prime the cache or defer the glow is
   a design call, not measured here.

### Target stage (impact / spellComplete) — not confirmed rendered in this capture
All 7 cast-animation `OP_Action` (effect_flag 0) packets are received and the
bridge arms `createSpellComplete` + `createImpact` on that packet
(`irrlicht_bridge.cpp:1147-1149`, T395). But this capture does **not** confirm
the Target stage renders: `createImpact` logs at `LOG_TRACE`
(`spell_visual_fx.cpp:653`) and `createSpellComplete` has no log line at all,
so neither is visible in a DEBUG run; and the impact-instant frames (f119 =
12:22:54.5 Frost-Bolt hit; the others) show no visible burst. This is the
B1104 F2 question (created-but-invisible vs dropped) re-posed for a gem cast
— the log level here is too coarse to separate them, so a follow-up either
re-runs at TRACE or adds a `createImpact`/`createSpellComplete` INFO line to
make the Target stage observable. B1106's sim-worker delta-clamp is in the
build but not the thing this capture measures.

### Gap G1 — oracle gem-click calibration (4/7 clicks missed) — SUPERSEDED by B1118 + B1129

**Struck (B1129, #449).** The ~35 px "pitch" below was a vision estimate, not a
measurement; lane 5's bar measures 30 px (B1118 `oracle_calib/`), and the
official client REFUSES a gem whose `spells_us.txt` class level excludes the
caster ("You are not high enough level to cast this spell!", no
`OP_CastSpell`): 93 (DRU/SHM/MAG) and 253 (DRU) can never go on a wizard.
The driver now refuses such a plan (`oracle_plan_check.py`) and multi-class
sets run one leg per class (`oracle_class_leg.sh`); `refactor/B1129/README.md`.
The original text, kept for the record:

The oracle leg's gem clicks used the INI-derived coords (21, 209+30i). The
client confirmed 3 (41, 54, 656) and produced no `OP_CastSpell` for 4 (93, 38,
253, 458). The bar is visible and all 7 gems lit (vision-confirmed in the
`gem*_cast*` stills); the misses are not client rejections (the 3 that worked
are the same click shape). A vision read of the oracle's own gem stills
estimates the on-screen gem pitch at ~35 px (gem centers ≈ 210/245/280/315/
350/385/420), not the 30 px in `spell_gem_layout.h` — so the click y-drift
grows down the bar and some land just outside the 28-px cell. **Follow-up:
re-measure the oracle's actual gem pitch on the lane and recalibrate the click
coords, then re-run the 4 missed gems.** (Acceptance A is still met — 3/7 prove
the official gem-cast path reaches `OP_BeginCast`.)

### Gap G2 — fable capture fps is an artifact (documented, not a finding)
The fable leg's `--dump-frames-dir` fps is a capture-mechanism artifact
(per-frame `glReadPixels` + PNG encode), never a performance number (CLAUDE.md).
The first leg's frame mtimes were clobbered by a staging `tar --mtime` during
evidence transfer, so that leg's frames are used only for the *count* and the
*effect* rows are keyed to the log timestamps (authoritative); the final leg
pulls frames by raw bytes to keep them intact.

## 7. Files
- `oracle_gemcast_plan.tsv` — the oracle leg plan (gem clicks + typed targets).
- `fable_leg.sh` — the fable leg (self-provisioning tour, 7 gem casts, fresh
  NPC per bolt, day-set in the final run).
- `fable_final_run.sh` — the fable leg with full lane lifecycle (lease → up →
  snapshot → ThinkPad run → restore DOWN → down → release).
- `oracle_gemcast/` — oracle leg output (video.mp4, all-opcodes.tsv, per-step
  stills, calibration).
- `fable_final/` — fable final leg (log + frames).

## 8. Closes
**#424 stays open.** The gem-cast path is confirmed to reach `OP_BeginCast`
on both clients and the B1105 Source-stage fix is confirmed working for a real
gem cast (all 7 `createCastGlow` fire with resolved positions in the final
leg). But the card's all-match bar is not yet met because:
(a) the **Target stage (impact) is not confirmed rendered** in this capture —
`createImpact` logs at TRACE and `createSpellComplete` has no log, and the
impact-instant frames show no visible burst, so B1104 F2's
created-but-invisible-vs-dropped question is still open for a gem cast;
(b) the **F3 start-up race** leaves the first few `createCastGlow` calls
(4/7 on a cold render thread) falling to the entity-center fallback, and the
uninitialised `rhPos/lhPos/centerPos` log fields are a real (small) defect;
(c) ~~the **oracle leg needs the G1 click recalibration** to confirm all 7 gems
on the official client (3/7 confirmed now)~~ -- done by B1118 (class legs,
lane-5 measured pitch); see `refactor/B1129/README.md`.

### FOLLOW-UP (not cut here — workers do not cut cards; Will's call)
1. **Target-stage observability (the #424 blocker):** add a `LOG_INFO` to
   `createImpact` / `createSpellComplete` (or re-run this leg at TRACE) and
   re-capture, to settle whether the impact is created-but-invisible or
   dropped (B1104 F2 for a gem cast). The §4 video pair is the frame verdict
   once the commands are visible.
2. **F3:** initialise `rhPos/lhPos/centerPos` in
   `spell_visual_fx.cpp:427` (and/or guard the `:429` log on the callback's
   bool), and decide whether to prime the render-thread position cache or defer
   the first `createCastGlow` past the cache-warm frames.
3. ~~**G1:** re-measure the oracle's real gem pitch on the lane, recalibrate the
   click coords, and re-run the 4 missed gems (93, 38, 253, 458).~~ **Done:**
   B1118 measured lane 5 and ran the class legs; B1129 made both a driver
   feature (`scripts/eqemu/oracle/gem_pitch.tsv`, `oracle_plan_check.py`,
   `oracle_class_leg.sh`).
