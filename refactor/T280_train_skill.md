# T280 — train a skill at a trainer / close the trainer window (T264 F8, the last of the eight)

Slot: slot1. x86. OWNS eqemu (noclobber; T279 slot3 is asset-only, T278
slot2 may hold the lock — bounded wait). Class A. Territory: the
train/close-trainer intents + handler hops T264 KEPT (delist in
`tests/test_reachability_sweep.cpp` when wired), the OP_GMTrainSkill /
trainer senders, CommandProcessor `/train` family (T50 found the server's
max-distance rule), `EverQuest`'s trainer-window state (T136 opened it),
tests. Do NOT touch EQE1/builder/assets (T279 slot3), tradeskill (T278
slot2).

## Gap
`/train` opens the trainer window (T136); nothing can actually train a
skill or close the window. T50 already established the server's
max-distance refusal.

## Scope — verify before coding, three lessons apply
1. **Wire truth from eqemu, not from a member name** (T273/T277): read
   `Handle_OP_GMTrainSkill` / `Handle_OP_GMTrainSkillConfirm` (or whatever
   Titanium actually uses — `patch_Titanium.conf` decides; T277's plan
   named an Underfoot-only opcode and was wrong). Establish: which packet
   trains, what it costs, what the server validates (class, level, skill
   cap, distance, money), and what it sends back.
2. **Check the consumer arm's arguments and its local-state writes**
   (T272/T277 class): if the arm exists, verify it does not remove/spend
   locally and optimistically, and that its argument order matches the
   callee.
3. Command first (T156 rule): `/train <skill|id>` and `/trainclose`.
   Refuse locally only what the client can know; let the server refuse the
   rest. Route the click (if the trainer panel has one) through the same
   body (T230/T239 one-body rule) — if there is no panel hit-test, say so
   like T277 did.
4. Tests: train request bytes for a skill; close request; the skill value
   and the money change come from the server's reply, NOT optimistically.
   Red/green.
5. Live: a trainer NPC (`#npctypespawn` — T227's banker recipe shows the
   pattern; find a class-appropriate trainer for Testchar the warrior),
   `/train <skill>` → `character_skills` gains/raises the row and the coin
   is deducted server-side; `/trainclose`. Restore rows.

## Verification
Full ctest; visual gate if a panel changed; Review; evidence review/T280/.
Commit: `T280: skills can be trained at a trainer`.

---

## Review (executor, 2026-08-26, slot1)

### 1. Wire truth, established from eqemu — and it contradicted a member name

Read out of `/mnt/projects/akk-stack/code`, not assumed:

| Question | Answer | Source |
|---|---|---|
| Which opcodes does **Titanium** map? | `OP_GMTraining=0x238f`, `OP_GMEndTraining=0x613d`, `OP_GMTrainSkill=0x11d2`. `OP_GMEndTrainingResponse` is **commented out**; `OP_GMTrainSkillConfirm` is **absent entirely**. | `utils/patches/patch_Titanium.conf:147-150` |
| What trains? | `OP_GMTrainSkill` carrying `GMSkillChange_Struct`, **12 bytes**, hard size-gated. | `zone/client_packet.cpp:7054` |
| Struct layout | `npcid` is a **uint16** at 0, `skillbank` u16 at 4, `skill_id` u16 at 8. eqemu's own per-field offset comments are wrong — every one of them reads `/*002*/`. | `common/eq_packet_structs.h:622` |
| What does the server validate? | training points != 0; npc class 20..35; `trainerClass - 19 == playerClass` unless `AllowCrossClassTrainers` (default false); `DistanceSquared <= USE_NPC_RANGE2 == 40000` (200 units); `CanHaveSkill`; `MaxSkill != 0`; not already at `MaxSkill` / the tradeskill / research / specialization sub-caps. | `zone/client_process.cpp:1701` |
| What does it cost? | `Cost = (current - 10)^3 / 100` copper, integer, off the **pre-train** value. Free below 11. | same |
| What comes back on Titanium? | **One packet: `OP_SkillUpdate`.** `GMTrainSkillConfirm` is gated on `ClientVersion() >= SoF`. `TakeMoneyFromPP(Cost)` is called with `update_client` defaulting to **false** (`zone/client.h:899`) and every `SendMoneyUpdate()` inside it is behind that flag. `OPGMEndTraining`'s reply uses the unmapped `OP_GMEndTrainingResponse`. | same + `zone/client.cpp:2773` |

**Consequence, stated plainly:** on Titanium the server deducts the coin and the
training point and never tells the client. Nothing in this unit writes money
locally — there is no reply to write from, and guessing would be exactly the
T272/T277 optimistic-local-write mistake. The trainer window's money figure is
stale until the next zone or relog. **Measured live**: copper went 17 -> 7 in
the DB (10c = (20-10)^3/100) while the client's own money was never updated.

The T277 lesson applied cleanly: the plan's guess would have been an
Underfoot/SoF-only confirm packet, and `patch_Titanium.conf` said no.

### 2. What the existing arm did wrong

The consumer arms (`eq_bridge.cpp:1383-1387`) were **correct** — `TrainSkillIntent{skillId}
-> TrainSkill(skillId)`, `CloseTrainerIntent -> CloseTrainerWindow()`, no swapped
arguments, no local writes. So was the byte-level sender (`npcid` already truncated
to uint16, `skillbank = 0`). Two real defects were found elsewhere:

1. **The training price was invented.** `ZoneProcessGMTraining()` computed
   `cost = (current_value + 1) * 10` with a comment calling it "the EQ training
   cost formula". It agrees with eqemu at **no skill value at all**. At skill 50
   it quoted 510 copper for something the server charges 640. Replaced by
   `EverQuest::TrainingCost()`, eqemu's own cubic, verbatim. **Measured red:**
   `TrainSession.TrainerWindowEventQuotesTheServersPrice` reported `510 vs 640`
   (`review/T280/red_cost_formula.txt`).
2. **The caps were read once and thrown away.** The server's per-skill caps
   arrived in `GMTrainee_Struct::skills[]`, were used to build one event, and
   were discarded — so nothing could ever answer "does this trainer teach that".
   Now kept in `m_trainer_skill_caps` for the life of the session and cleared by
   `CloseTrainerWindow()`.

The producer gap was the actual F8 blockage: `/train` took no argument,
`/trainclose` did not exist, and both intents were on
`test_reachability_sweep`'s `kBlockedIntents` list. **Measured red:** with the
two `pushIntent` calls removed, 5 of the 23 new tests fail
(`review/T280/red_no_producer.txt`).

### 3. Scope delivered

- `/train` (no args) keeps its pre-T280 meaning exactly (T50's target/npc_type/
  class/distance pre-validation, unchanged).
- `/train <skill name|skill id>` trains at the already-open session.
- `/train list` prints the session's offerings with the real price.
- `/trainclose` (alias `/traindone`) ends the session.
- Route: `CommandProcessor::cmdTrain/cmdTrainClose` (new TU
  `command_processor_train.cpp` — `command_processor.cpp` is at the 3000-line
  FAIL ceiling) -> `ActionDispatcher::trainSkill/closeTrainer` ->
  `EqActionHandler` -> `EverQuest::RequestTrainSkill/RequestCloseTrainer`
  (new TU `eq_commands_train.cpp`) -> the kept intents.
- Local refusals only for what the client can know: no session, no training
  points, unknown skill name, ambiguous skill name, out-of-range skill id, a
  skill this trainer does not teach (cap 0), a skill already at this trainer's
  cap. Everything else is left to the server.
- **No panel hit-test exists.** There is no trainer UI anywhere in
  `graphics/ui`, so the command is the only producer today — said out loud
  (T277's precedent) rather than implied. `TrainerToggleIntent` (F3 keybind)
  already existed and is untouched.
- `TrainSkillIntent` and `CloseTrainerIntent` delisted from `kBlockedIntents`;
  the sweep's `ProducerlessIntentsAreExactlyTheBlockedList` pins it.

### 4. A caveat found live, not reasoned about

The caps in `GMTrainee_Struct` are the trainer's **MaxLevel** caps, not the
player's current-level caps — eqemu fills them with
`GetMaxSkillAfterSpecializationRules(sk, MaxSkill(sk, class, RuleI(Character, MaxLevel)))`.
So `/train list` shows a level-2 warrior "1H Slash 1/310". The server's own
train check uses `MaxSkill(skill)` at the player's **actual** level and refuses
above it. Observed live: with 1H Slash at 20 and Testchar at level 2, the client
sent (local cap 310 allowed it) and Brin Stolunger answered *"You are more
skilled than I! What could I possibly teach you?"* — a real, visible server
refusal, not silence. Left as the server's job on purpose: the client would have
to reimplement eqemu's whole `MaxSkill` table to pre-empt it, and this refusal
is one of the few the server actually answers.

### 5. A duplicate store noted, not "fixed"

Practice points live in two places: `EverQuest::m_practice_points` (what
`eq.cpp`'s `OP_SkillUpdate` arm gates on) and `PlayerState::m_practicePoints`
(what `GetPracticePoints()` reads). Both are written from the same PlayerProfile
(`eq.cpp:2323`/`2341`) and both are decremented together, so they cannot drift in
production — but a test that seeds only one gets a silently wrong answer (this
one did, first run). Flagged, not touched: it is outside this unit's territory.

### 6. Live evidence (eqemu, lock held T280 2026-08-26, released)

Testchar (warrior, befallen), trainer `Brin_Stolunger` (npc_types 1063, class 20)
GM-spawned with `#npctypespawn`.

| | before | after run 1 | after run 4 (cost proof) | restored |
|---|---|---|---|---|
| `character_skills` skill 1 (1H Slash) | 1 | **2** | **21** | 1 |
| `character_data.points` | 10 | **9** | 93 (level-up) | 10 |
| `character_currency.copper` | 17 | 17 (cost 0 below skill 11) | **7** (-10c) | 17 |

Client-side, in order: trainer window opened with 35 trainable skills; `/train list`
printed them with prices; `/train 1H Slash` printed `Training 1H Slash (1 -> 2).`;
the server answered `You have become better at 1H Slash! (2)` via `OP_SkillUpdate`;
`/trainclose` drew Brin's goodbye emote (`Use your skills wisely Testchar.`),
which only fires after `OPGMEndTraining` passes its own class + distance checks —
so the 8-byte `OP_GMEndTraining` was accepted.

All four local refusals fired live with the exact expected text
(`review/T280/live_run2.log`):
`Brin Stolunger cannot train you in Forage.` /
`"instruments" matches 4 skills: ...` /
`There is no skill 999 (skill ids are 0-74).` /
`You are not interacting with a trainer. Target your guildmaster and use /train.`

**Restored:** level 2, exp 1000, points 10, zone 36 / (-77.9232, 21.9086, 3) h190,
the original 14 `character_skills` rows at their original values, currency
974/16/16/17, and the one `zone_state_spawns` row for npc 1063 deleted
(6178 -> 6177). Server down, lock deleted.

### 7. Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest` (full) | **3131/3132** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `test_train_skill` (new, 23 tests) | 23/23 PASS; red measured twice (see above) |
| `scripts/build-gles2-x86.sh` | PASS |
| Visual gate | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | 16 checked, 8 ok, 8 warn, **0 fail** |

### 8. Deviations

- Added `/train list`, which the plan did not name. It is not decoration: with
  no trainer panel and caps that only exist inside a live session, it is the only
  way a player can find out what is trainable and what it costs. ~30 lines.
- The plan's item 4 says the money change should come from the server's reply.
  On Titanium **there is no such reply** (section 1). The test asserts the
  absence instead — money must not move locally — and the live run proves the
  server-side deduction in the DB.
