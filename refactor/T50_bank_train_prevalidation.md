# T50 — Route `/bank` through the gesture's validated intent, and pre-validate `/train` instead of failing silently

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the acceptance is a live bank
window that STAYS OPEN and a trainer window that actually OPENS, both
driven entirely by piped commands.**
No sibling unit.

**Territory: the `/bank` and `/train` handlers in
`src/client/eq_commands_spells.cpp`, plus tests.** Do **not** change
`BankerInteractIntent`'s handler (`eq_bridge.cpp:902-940`),
`OpenBankWindow`/`CloseBankWindow`, `RequestTrainerWindow`, the movement
proximity gate (`eq_movement.cpp:524-540`), or `NPC_INTERACTION_DISTANCE`
itself. This unit changes what the two COMMANDS do before they reach
existing, working code.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T48_vendor_reachability.md` (the precedent this mirrors almost
exactly), then `refactor/T49_npc_interaction_state.md`'s Review section
(it found both defects and its `/npcstate` is your observability tool),
then the two deferred entries "`/bank` opens and self-closes instantly"
and "`/train` sends `HC_OP_GMTraining` and the server never answers".

## Why this unit exists

T49's live acceptance exercised vendor fully but could not exercise
banker or trainer. Will's steer — that bank/train/trade all legitimately
require standing close to the NPC, and that the server likely defines its
own max distance — reframed both failures and I confirmed it against the
eqemu server source. **The proximity requirements are correct. Both
commands are simply worse than the renderer gesture doing the same
thing.**

## Ground truth I established — verify, do not re-derive

**Server-side (`/mnt/projects/akk-stack/code/`, the real eqemu source —
use it, it settles protocol questions):**

- `USE_NPC_RANGE2 = 40000` (`common/features.h:215`) — **200 units** —
  gates ~10 interaction handlers in `zone/client_packet.cpp`: vendor
  (`:2288`), banker (`:3640`), tribute master (`:11005`) and more.
  **Every one is a silent `return` — the server tells the client
  nothing.**
- `Client::OPGMTraining` (`zone/client_process.cpp:1612`) has **three**
  silent returns, in this order: (1) trainer missing / not an NPC / class
  outside `WarriorGM..BerserkerGM`; (2) **cross-class** —
  `RuleB(Character, AllowCrossClassTrainers)` **defaults to `false`**
  (`common/ruletypes.h:174`), and `trainerClass - (WarriorGM - Warrior)`
  i.e. `trainerClass - 19` must equal the player's class; (3) range.
- **T49's `/train` died on (2), not (3).** Testchar is class 1 (Warrior);
  `Miltiades_Tal'Azroth` is class 22 = PaladinGM, which trains class 3.
  `/list` in the same transcript showed every NPC at `0.0 units away`.
  **A Warrior needs a `WarriorGM` (class 20) trainer.**

**Client-side:**

- `NPC_INTERACTION_DISTANCE = 15.0f` (`eq.h:676`) — the client is far
  **stricter** than the server's 200. See the design decision below.
- `BankerInteractIntent` (`renderer_intents.h:108`) **has a producer**:
  `renderer_overlays.cpp:600`, the renderer gesture. Its handler
  (`eq_bridge.cpp:902-940`) validates `npc_type`, distance, and
  `class_id == CLASS_BANKER (40)`, **messages the user on each failure**,
  then calls `OpenBankWindow(i.npcId)` **with the real npc id**.
- `/bank` (`eq_commands_spells.cpp:302-312`) bypasses every bit of that
  and calls the no-arg `OpenBankWindow()`, which falls back to
  `bankerNpcId != 0 ? bankerNpcId : 1` — **a hardcoded dummy id of 1**.
  Spawn 1 is `zone_controller000` at extreme coordinates, so the movement
  tick measures ~31690 units and closes the window it just opened.
- `/train` (`eq_commands_spells.cpp:320-358`) validates target,
  `npc_type` and GM-class-range, but has **no distance check and no
  player-class-vs-trainer-class check** — so it sends a request the
  server drops in silence.

## Piece 1 — `/bank` pushes the gesture's intent

Make `/bank` push `BankerInteractIntent{targetId}` for the current target,
exactly as T48 made the vendor commands push `VendorToggleIntent`/
`VendorBuyIntent`/`VendorSellIntent`. The validated handler already exists
and already does the right thing; you are removing a bypass, not writing
logic. Do not duplicate its checks in the command.

**This is a deliberate behaviour change, and you must state it plainly:**
`/bank` today takes no target and "works" (degenerately, opening a window
that self-closes). Afterwards it will require a targeted banker, like the
gesture. That is the point — but it means the toggle's close half needs
thought: closing should keep working without a valid target (you may
already have the window open and have since changed target). **Say what
you chose.**

## Piece 2 — `/train` pre-validates instead of failing silently

Add the two missing checks so the user gets a message rather than
silence:

1. **Distance.**
2. **Class match** — `trainerClass - 19 == playerClass`.

### A real design decision, which I want made explicitly and justified

- **Distance**: the client constant is 15, the server's is 200. Checking
  at 15 makes `/train` **stricter than the server** and would refuse
  trains the server would accept at 15-200 units. Checking at 200
  contradicts the client's own interaction convention (the banker gesture
  and vendor path both use 15). **My lean: use `NPC_INTERACTION_DISTANCE`
  for consistency with every other client-side interaction, and say in
  the Review that the server would have allowed more.** If you disagree,
  argue it — but do not introduce a third constant.
- **Class**: `AllowCrossClassTrainers` defaults false, but a server
  *could* enable it, in which case a hard client-side block would break
  training that the server would allow. **My lean: block, with a message
  naming both classes** ("Miltiades Tal`Azroth trains Paladins; you are a
  Warrior"), since the default is false and silence is the thing we are
  fixing. Note the cross-class-server caveat in the Review and add it to
  deferred rather than building configuration for it now.

Either way the message must be **actionable** — it should tell the player
what is wrong, not just that something is.

## STOP conditions

- `/bank` via the intent turns out to need something the command path
  cannot reach → STOP and report what.
- Making `/bank` require a target breaks an existing caller or test in a
  way you cannot cleanly justify → STOP and report rather than papering
  over it.
- You cannot find a `WarriorGM` (class 20) NPC to test against → STOP and
  say so; do **not** switch characters or edit the server's rules to make
  a green run (that would prove nothing about the client).
- Any change to the intent handler, the merchant/trainer protocol, or
  `NPC_INTERACTION_DISTANCE` → STOP; different unit.
- **Do not add scratch hooks.** T48's standard: if you need one, that is
  itself the finding.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP unwaived (T35),
   golden replay does not skip (T36); pinned tests from T43-T49 stay
   green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `scripts/ci/visual_gate.sh` PASS, goldens untouched.
5. Unit tests for both pieces: `/bank` with no target, with a non-banker
   target, with a valid banker; `/train` with too-far and wrong-class
   targets (assert the *message*, not just the refusal). Follow
   `test_command_processor.cpp`'s `/loot` (T34) and T48's vendor cases.
6. **Live acceptance — this is the deliverable, and it closes T49's two
   open caveats:**
   - **Bank**: spawn a real banker (class 40), target it, `/bank` → the
     window **opens and STAYS OPEN** across several seconds of movement
     ticks → **`/npcstate` reports the banker** (T49 could never show
     this) → `/bank` again closes it → `/npcstate` reports none.
   - **Trainer**: spawn a **`WarriorGM` (class 20)** trainer for Testchar
     (class 1) — find one in the DB and say which npc id you used → 
     `/train` → **the trainer window actually opens** → `/npcstate`
     reports the trainer. This is the first time this path will have
     worked end to end.
   - **The negative legs too**, since they are the user-visible payoff:
     `/train` against a wrong-class trainer (T49's Miltiades, class 22)
     now prints an actionable message instead of silence; and a
     too-far target likewise.
   - **Both trees.** eqemu LOCK FIRST; `up.sh` only; restore Testchar to
     zone_id=36, x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after.
     Evidence under `benchmarks/audits/T50/`.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## What to report in the Review

- The two design decisions above, with your justification.
- What `/bank`'s close half does without a valid target.
- The npc ids and classes you tested against, so the run is reproducible.
- Whether `/npcstate` now shows banker and trainer state live — **that
  retires T49's "reachable-but-blocked" caveat**, and the board entry
  should be updated to say so.
- Whether the automation-reachability pattern is now closed or merely
  narrowed again. Be honest: this is its fifth instance, and the running
  list is `/combat on` (T26), the `EverQuest` registry (T29), `/loot`
  (T34), vendor (T42→T48), and `/bank` (here).
