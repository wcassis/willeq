# B1143 — rig residue: the 93-miss attribution + the new combat-ability / discipline rows

Lane 5. Char 67 (Testcaster, caster) + char 103 (Oraclewatch, observer), the B1130 rig
(caster `:98` Xvfb/mesa, observer `:99` NVIDIA — the leg's fixed geometry). Evidence:
`/home/user/kb-evidence/kb-b1143-rig-residue/`.

## Item 1 — the `hasTargetStage` gate (VERIFIED, present and pinned)

The fix is at `src/client/graphics/spell_visual_fx.cpp:849`:

    // B1143: a spell that names no Target stage (e56, e262, e13) draws its
    // Source only; the gate here stops it from also drawing a Target at the
    // player even when the effect carries a target id.
    if (!visuals::HasTargetStage(rec, name) &&
        !ctx_.spellEffects()->hasTargetStageFor(name) &&
        (e.type == GameEventType::Effect || e.type == GameEventType::EffectFlag)
        && e.data.targetID != e.data.spawnerID &&
        e.data.targetID != EQT::GameState::PLAYER_ENTITY_ID)
        continue;

`tests/test_has_target_stage.cpp` pins the rule with the three measured rows
(e56 `Reap Soul` source-only, e262 `Ritual of Rebirth` no-target, e13 `Fear` /
`Spook the Dead` source+target). A regression that re-enables the Target for a
no-target record is a red test. **Item 1 is complete.**

## Item 2 — the 93 Burst of Flame rig miss (leg captured; the casts did not land)

The leg `oracle/p3_rig93.fix/` (evidence `/home/user/kb-evidence/kb-b1143-rig-residue/`)
ran the first attempt at the card's design:
- caster (Testcaster) class 6 Druid, `#goto -71.6,431.6,3.75,225` heading 225 (note: the
  comma-form `#goto` failed, below);
- observer (Oraclewatch) at the TrollF spot, fixed-camera sweeps + 36 s settle + record;
- target `#spawn TrollF` (named `TrollF000` by the server) x2 plus `#target Oraclewatch`
  (the player) x2; 4 casts, `/cast 1` (gem 0 = 93).

Captured: `video.mp4` (94 MB), `video-caster.mp4`, `capture.pcap` (1.9 MB, 41 648
packets), 100 stills, `timeline.tsv`, `video.calibration.tsv` (epoch 1792935218
basis, 30 fps), `measure/p3_rig93.fix_casts.json`.

**FINDING — the 4 casts did not land.** Decoded from `capture.pcap` with
`scripts/eqemu/oracle/vendor/eqpcap.py` (connections: caster `7001/51876`, observer
`7001/51542`): **the casts never left the caster client.** At the four `/cast 1`
steps (14:27:53, 14:28:27, 14:29:14, 14:29:48) the C->S stream of `7001/51876`
contains **no cast packet** — only `OP_ClientUpdate` / `OP_FloatListThing` / the GM
`OP_ChannelMessage` commands — and no `OP_BeginCast` / `OP_Action` / `OP_Effect` is
sent to either client anywhere in the leg. (The two `OP_BeginCast` 0x3990 in
`all-opcodes.tsv` at 14:25:24.405/.421 are on the caster connection during the
zone-in, spell 93 / 2500 ms / channel 10 — a server-side cast state the leg started
with; the caster client sent nothing that started it, and it never completes in the
pcap. It is residue, not the cause below.)

**Root cause, from the quoted lines.** The plan targeted with the GM form
`#target <name>`, which this eqemu build does not have:

- 14:27:46.340 C->S `OP_ChannelMessage` 7001/51876 `msg='#target TrollF000'`;
  14:27:46.355 S->C `OP_SpecialMesg` decodes to **`Command '#target TrollF000' not
  recognized`** (identically at 14:28:19.776/.791 for cast b, and for both
  `#target Oraclewatch` lines). The caster therefore **never had a target**, and the
  official client refuses a targeted spell locally — hence zero cast packets and the
  empty chat in the caster stills.

Two more silent failures in the same leg (quoted from the same pcap), each of which
would have broken the measurement even with a working target:

- `#stun TrollF000 120000` (14:26:34.264) → S->C `OP_SpecialMesg`
  **`Usage: #stun [Duration]`** (14:26:34.290): `#stun` takes a duration only and
  stuns the **current target** (B1130 r51: `/target TrollF` line 26 then `#stun
  120000` line 27) — so the TrollF was never stunned.
- `#goto -67.6,434.4,3.75,225` (14:26:37.9) → S->C `OP_SpecialMesg`
  **`Player -67.6,434.4,3.75,225 could not be found.`** (14:26:38.496): the comma
  string was parsed as a player name (B1130 r51 line 24 uses the space form
  `#goto -67.6 434.4 3.75`). The caster therefore never left its `pose=` position
  and all three actors co-located at -74,428 — the B1130 9.05-unit geometry was not
  achieved (the caster still `24_time_settle.png` shows TrollF000 1–2 m in front and
  the scene never matches the design).

**The fixed plan** (`plans/p3_rig93.tsv`) therefore (1) uses the client-side
`/target <name>` — the form that worked in B1130 r51 and B1134 P2; (2) `/target`s
then `#stun 120000`s (re-stun before each cast, as B1130 r51); (3) drops `#goto`
entirely and places both clients with the driver's `pose=` / `observer-pose=`
directives in the B1134 P2b geometry: caster at the TrollF spot
(-74,428,3.75, h160), observer at B1130 r51's caster pose (-67.6,434.4,3.75, h160)
with F9 x3, TrollF000 #spawned at the caster's position (eqemu `#spawn` takes no
coordinates). The spawned NPC's exact name is `TrollF000` (OP_NewSpawn 14:26:30.274,
name bytes `54 72 6f 6c 6c 46 30 30 30`). A probe leg (`plans/p3_rig93.probe.tsv`,
2 NPC casts only) confirms OP_Action lands before the full leg is spent.

## Item 3 — the record-325 disciplines + record-1 combat abilities (spells identified)

From `spells_us.txt` (field 145 = the record id, `^`-delimited):
- **Record 325** — Leechcurse Discipline (1256), Reaper Strike (1257), Lich
  Sting (1258) + five recourse spells (1259–1263).
- **Record 1** — ~190 spells (the combat-ability kits). Char 67's class (ninja)
  subset: e.g. 4505 Crippling Strike, 4506 Thunderkick, 4504 Defensive
  Discipline.

Status: the `/disc` activation leg and the measurement are **not done** (the
record-325 owning class is pending — a DB query on `spells_new.class` / the
spell→class mapping), and the rows are not yet measured.

## Intimidation → Fear (record 13) — the skill-triggered addition (NOT done)

Will's 2026-10-25 13:05Z addition. The server casts Fear 229 (record 13, the
same record as 209 *Spook the Dead*, the B1130 e13 'skull cloud') from a
successful Intimidation via `SpellOnTarget(229, who, 0, true, -2000)` — the
clients see an **OP_Action with NO OP_BeginCast**. The leg (char 67 as a class
with Intimidation, `#setskill`, `/intimidate` on a `#spawn` NPC at the TrollF
spot, measure the NPC's record-13 Target-stage landing + confirm NO Source on
the intimidator) is **not run** — see the FOLLOW-UP.

## FOLLOW-UP (what's needed to finish)

1. **Item 2 — make the casts land (done at the diagnosis level).** The cause is the
   quoted `Command '#target TrollF000' not recognized` (plus the `#stun`/`#goto`
   syntax failures, above); the fixed plan is `plans/p3_rig93.tsv` (client-side
   `/target`, `#stun` after `/target`, no `#goto`, B1134 P2b pose geometry). The
   probe leg `plans/p3_rig93.probe.tsv` (2 NPC casts) confirms OP_Action lands, then
   the full leg measures the NPC (TrollF000) vs player (Oraclewatch) bands in the
   observer view against B1130 r51 row 0.
2. **Item 3 — the /disc leg.** Determine the record-325 owning class (DB), make
   char 67 that class, `#traindisc` + activate each record-325/record-1
   discipline through the Combat Abilities window with the observer recording,
   measure Source + landing, restore char 67 to class 12 / gems.
3. **Intimidation → Fear.** Char 67 as a class with Intimidation (`#class`/
   `#level`, `#setskill`), `/intimidate` on a `#spawn` NPC at the TrollF spot,
   repeat until one success (the pcap OP_Action 229 / the 'You intimidate'
   chat). Measure the NPC's record-13 Target-stage (skulls) landing against the
   B1130 row-13 bands and confirm NO Source on the intimidator (B1144).

Rows land in `refactor/B1143/rows/*.tsv` in the B1131/B1132 format, with
`animRecords` entries in this README (NOT in `config/spell_effects.json`), as
the measurements complete.
