# B900 — a single ranged attack (the packet half)

GitLab issue **#216**. Class B feature gap. Will's ruling (2026-09-23): a ranged
attack is triggered **ONE SHOT AT A TIME** by the Range Attack button/key or a
slash command; there is **no autofire**. This card is the **PACKET half** — the
single `OP_CombatAbility{target, slotRange, skill}` and its two triggers. The
**missile draw** (`HC_OP_SomeItemPacketMaybe` decode) is a follow-on card, not
this one, and is left open below.

## What the defect was

`CombatManager::UseAbility` hard-codes `m_atk = 100`
(`src/client/combat.cpp`, the T-precedent comment names melee abilities only),
so no fable `OP_CombatAbility` packet could ever reach the server's
`RangedAttack()` / `ThrowingAttack()`. The server takes a manual ranged attack
as `OP_CombatAbility` with `m_atk == EQ::invslot::slotRange` (11) and `m_skill`
Archery(7) or Throwing(51) (`zone/special_attacks.cpp:376-397`); the autofire
split reads the range item's ItemType, `client_process.cpp:345-380`.

## What changed (measured against eqemu source — Leg W)

New `CombatManager::RangedAttack()` — the single-shot trigger shared by `/range`
(and its aliases `/rangedattack`, `/fire`) and the Range Attack key. It:

1. no target → clean no-op (no packet, no message);
2. resolves the range-slot item (`SLOT_RANGE` = 11, Titanium numbering — the
   same 11 as `EQ::invslot::slotRange`, `rof2/titanium_limits.h:124` /
   Titanium);
3. picks `m_skill` from the item's eqemu ItemType carried in
   `ItemInstance::skillType` (wire field 56): `Bow(5)` → `kSkillArchery(7)`,
   `LargeThrowing(7)` / `SmallThrowing(19)` → `kSkillThrowing(51)` — mirroring
   eqemu's `client_process.cpp:345-380` split. An arrow (`ItemTypeArrow`, 27)
   or any non-bow/non-throwing item is NOT a ranged weapon;
4. with no usable range item: the official client's refusal, eqstr **9150**
   "You do not have a ranged weapon equipped." — `AddChatSystemMessage`, and
   **no packet**;
5. otherwise: one `OP_CombatAbility {target, m_atk = 11, m_skill}`.

Constants, [measured] against the eqemu source on lane 4
(`172.16.200.96`, `/mnt/projects/akk-stack/code/`):

| constant | value | source |
|---|---|---|
| `slotRange` | 11 | `common/rof2/rof2_limits.h:124` (Titanium aliases) |
| `SkillArchery` | 7 | `common/skills.h` |
| `SkillThrowing` | 51 | `common/skills.h` |
| `ItemTypeBow` | 5 | `common/item_data.h:61` |
| `ItemTypeLargeThrowing` | 7 | `common/item_data.h` |
| `ItemTypeSmallThrowing` | 19 | `common/item_data.h` |
| `ItemTypeArrow` | 27 | `common/item_data.h` |
| refusal eqstr | 9150 | `eqstr_us.txt:9150` |

The skill wire ids live in namespace `EQ` (`EQ::kSkillArchery` /
`EQ::kSkillThrowing`, `skill_constants.h`); the item-type constants live in
namespace `EQ::skill` (`EQ::skill::kItemTypeBow` / `LargeThrowing` /
`SmallThrowing`, `skill_packet_bodies.h`).

The **command leg** mirrors `toggleAutoFire` through the full chain:
`CommandProcessor::cmdRange` (registered `range`, aliases `rangedattack`,
`fire`, Combat category) → `ActionDispatcher::rangedAttack()` (zone-connection
gate) → `EqActionHandler::rangedAttack()` → `CombatManager::RangedAttack()`.

The **key leg** adds `HotkeyAction::RangeAttack` through the full 5-hop chain
(`renderer_event_receiver.cpp` → `graphics_input_handler.cpp` →
`input_action_bridge.cpp` → `ActionDispatcher` → `EqActionHandler`) and lists it
on the Options Keys page (Commands category). It is deliberately **UNBOUND** by
default — see the owed item below.

## The live scenario (criterion 3)

`ranged_single_shot` (registered in `scenario_registry.cpp`) runs the kit and
the shot against the lane's server and passes only when the SERVER agrees:

- The kit is DB-seeded by `ranged_single_shot_seed.sql` (the seed runs AFTER
  the snapshots, so the lane's inventory restore takes it out again).
- Prep: `/say #spawn Ranged Dummy`, `/target Ranged Dummy`,
  `/say #goto 70 5 5`. Both GM commands go out as `/say` -- `#` commands reach
  the server only as chat text for a GM character, the recipe
  `scenario_cast_lands.cpp:101` uses for `#set` / `#memspell`. `#spawn`
  (zone/gm_commands/spawn.cpp:46, `SpawnNPC(name, c->GetPosition(), c)`)
  spawns the dummy AT the character's own position -- the numbers after the
  name are race/level/texture/health/gender/class, NOT an offset -- so the
  CHARACTER then moves 50 units away. Testchar's stored position in freporte
  (zone 10) is (20, 5, 5) [measured, lane 4 character_data 2026-10-08], so
  `#goto 70 5 5` (zone/gm_commands/goto.cpp's position branch) lands exactly
  50 units away: inside the near bound MinRangedAttackDist = 25
  (common/ruletypes.h:576, checked at zone/special_attacks.cpp:969) and the
  far bound bow range 215 + arrow range 25 = 240, both [measured] from the
  lane's items table (Bloodscream 111529 `range` 215 / attack 37; Arrow 8005
  `range` 25, stackable, itemtype 27).
- ONE `/range` at t=16s; the pass condition is a post-baseline combat chat
  line that starts "You ", names the target, and says "points of damage" or
  "but miss" (a hit and a miss both broadcast OP_Damage, so either proves the
  server ran `RangedAttack()`).
- The ammo count is the POST-CHECK's half: `ranged_single_shot.post.sh` (run
  with the world still up) greps the client log for that combat line AND
  asserts the DB arrow stack dropped 10 → 9
  (`DeleteItemInInventory(ammo_slot, 1, true)`, special_attacks.cpp:1001).
  A client pass with neither in the log is a FAIL.

## Evidence (live, lane 4)

Criterion 3 is VERIFIED on lane 4 (`172.16.200.96`, 2026-10-08, 2 runs — a
hit and a miss, both verdicts):

- Run 1 (hit): `SCENARIO ranged_single_shot pass exit=0 reason=server fired
  one shot: 'combat_self||You hit Ranged Dummy for 1 points of damage.'
  1.43s after /range`; post-check `PASS: the server consumed exactly one
  arrow (10 -> 9 in the DB ammo slot)`; snapshot restored byte-identically,
  lock released.
- Run 2 (miss): same shape, `combat_miss||You try to hit Ranged Dummy but
  miss!` — the miss verdict is a pass too (both broadcast OP_Damage), which
  is why the pass condition names the target rather than demanding damage.

Two live defects were found and fixed on the lane (offline tests stayed green
through both, which is why they live in this run's commit):

1. **`#spawn`'s name is one token and gets a spawn-index suffix.**
   `#spawn Ranged Dummy` spawned a one-word NPC "Ranged" (the second token
   became the race field) that `/target Ranged Dummy` could not match →
   `RangedAttack: No target` in the client's own MOD_COMBAT log. The spawned
   entity's raw name is `Ranged_Dummy000` (underscore preserved, "000"
   appended — the same suffix the zone's named mobs carry, e.g. "Palatos
   Kynarn`s_pet000"), so the target is `/target Ranged_Dummy` — a substring
   of the raw name (entity_manager.cpp:186), and the combat line renders it
   via `toDisplayName` (name_utils.h:20) as "Ranged Dummy".
2. **scenario.sh's inventory snapshot ran BEFORE `INVENTORY_CHARS` was set**,
   so for this scenario (the only one whose kit is inventory rows) the
   snapshot was empty, the restore a no-op, and the seeded kit survived the
   run — duplicate-key on the next seed. The snapshot now runs inside the
   seed block, after `INVENTORY_CHARS` is derived (scenario.sh, section 3b).

## Evidence (offline)

- `tests/test_ranged_attack_wire.cpp` — **8 tests**:
  - 5 wire (criterion 1): bow → `m_atk=11, m_skill=7`; small/large throwing →
    `m_skill=51`; no range item → eqstr 9150 refusal + no packet; a non-range
    item (an arrow) in the range slot → refusal. RED-first: the RED errors were
    exactly `RangedAttack` undefined + the 5 missing constants
    (`/home/user/kb-evidence/b900-ranged-single-shot/RED_test_ranged_attack_wire.txt`).
  - 3 key (criterion 2): the action is on the Options Keys page; it is unbound
    in BOTH the compiled-in defaults and the shipped `config/hotkeys.json`;
    the two device-bound arms exist (source-parsed, the
    `test_ui_hide_command.cpp` idiom).
- `test_command_processor` — 414 (410 + 4 new `/range` dispatch cases,
  including all three aliases).
- `test_command_registration_order` (G03) — the new `range` row inserted
  after `autofire`, count 155 → 156. This golden is the one the test itself
  documents as "adding a genuinely new command is meant to require a line in
  the golden — that edit is the review moment"; B442/B895/B897/B899 all updated
  it in place. (The BLESS rule forbids editing memory/budget ratchets; the G03
  registration-order golden is explicitly the exception the test carves out.)
- Unchanged-and-green: `test_hotkey_manager` 62 (T464 defaults↔file agreement —
  an unbound action is in neither table), `test_hotkey_dispatch_census` 4,
  `test_options_wiring` 37 (every action in exactly one category, category
  fits), `test_input_action_bridge` 41, `test_frame_component` 21,
  `test_reachability_sweep` 5, `test_combat_manager` 66, `test_autorun_arm` 4,
  `test_debug_hotkey_arms` 6, `test_x11_keysym_map` 4, `test_x11_numpad_e2e` 1,
  `test_bridge_queue_race` 3. The full `willeq` binary builds clean.

## Not verified / left open

- **Criterion 3 — the live leg (lane 4):** VERIFIED — see "Evidence (live,
  lane 4)" above (hit and miss runs, exit 0, DB ammo 10→9).
- **The Range Attack default key is [assumed]-owed (Leg O).** The issue says
  the binding is "to be read from the oracle's `eqclient.ini` [KeyMaps], not
  assumed." That mapping is in the oracle user's *live* `eqclient.ini`, which
  is not in the shipped client data: the shipped `config/hotkeys.json` and the
  oracle inis carry no such row (checked `data/eq_client` and the oracle VM's
  `eqclient*.ini`), and `eqgame.exe` carries no printable "Range Attack"
  keysym string. So the key is wired through the full chain but left **unbound**
  rather than invented — the Options Keys page lists it so a user (or Leg O's
  oracle capture) can bind the measured key. This is the "[assumed] and owed"
  the card sanctions, not a fabricated default.
- **The local 12698 "target too close" check is not implemented.** eqemu's
  comment ("client has built in los check") means the min-range/facing/LOS
  client-side checks are the oracle leg's to decide which are local; I did not
  invent a min-range constant. The only client-specified refusal implemented is
  the no-range-weapon 9150, which eqemu itself emits
  (`special_attacks.cpp:880`).
- **The missile draw is owed** (`HC_OP_SomeItemPacketMaybe = 0x4033` decode +
  projectile move): a separate card (issue Leg M). This card is the packet half
  only, per its acceptance criteria.

## Wire facts

Full server routing (opcode entry, the ranged branch, the autofire split, the
`RangedAttack`/`ThrowingAttack` preconditions, and the `slotRange` lineage) is
recorded at
`/home/user/kb-evidence/b900-ranged-single-shot/wire_facts.md`.
