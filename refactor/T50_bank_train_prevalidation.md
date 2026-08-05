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

## Review

### Ground truth verification

Every claim in the plan's "Ground truth I established" section was verified
against the real eqemu source at `/mnt/projects/akk-stack/code/` before
writing any code, not re-derived:

- `USE_NPC_RANGE2 = 40000` confirmed at `common/features.h:215` (comment
  "arbitrary right now"). 200² = 40000, so 200 units, exactly as stated.
- `Client::OPGMTraining` (`zone/client_process.cpp:1612`) read in full: the
  three silent-`return` checks are in the plan's exact order — trainer
  missing/not-NPC/class-out-of-range first, then (if
  `!RuleB(Character, AllowCrossClassTrainers)`) `trains_class =
  pTrainer->GetClass() - (Class::WarriorGM - Class::Warrior)` compared
  against `GetClass()`, then `DistanceSquared(...) > USE_NPC_RANGE2`.
  `Class::WarriorGM = 20` confirmed at `common/classes.h:44`.
- `RuleB(Character, AllowCrossClassTrainers, false, ...)` confirmed at
  `common/ruletypes.h:174` — default `false`, exactly as the plan states.
- `Handle_OP_BankerChange` (`zone/client_packet.cpp:3640`, the real line
  number cited) confirmed: `entity_list.GetClosestBanker(this, distance)`
  then `if (!banker || distance > USE_NPC_RANGE2)` — a silent early return
  (only a `PlayerEvent::POSSIBLE_HACK` log entry, no client-visible
  message), confirming the plan's "every one is a silent return" claim for
  the banker path too.

All matched the plan exactly. Nothing turned out wrong; there was nothing
to correct.

### Design decision 1 — `/train`'s distance check: 15, not 200

Used `NPC_INTERACTION_DISTANCE` (15), per the plan's lean. Justification,
stated in the code comment at the check site
(`src/client/eq_commands_spells.cpp`, the `/train` handler): the client
already has exactly one convention for "am I close enough to interact with
an NPC" and every other client-side check uses it — the banker gesture's
`BankerInteractIntent` handler (`eq_bridge.cpp`) and the movement
proximity gate (`eq_movement.cpp:533`) both check against 15, not 200.
Introducing a second, looser distance for `/train` alone would make the
client internally inconsistent about what "close enough" means, for a
marginal gain (trains between 15 and 200 units away that the server would
accept). The tradeoff is real and stated plainly: this makes `/train`
**stricter than the server**, so a small number of technically-valid
trains (target 15-200 units away) are refused here that `Handle_OP_...`
`OPGMTraining` would have accepted. No third constant was introduced.

### Design decision 2 — `/train`'s class check: hard block, message names both classes

Implemented as a hard client-side refusal whenever `trainerClass - 19 !=
playerClass`, per the plan's lean, since `AllowCrossClassTrainers` defaults
`false` and the entire point of this unit is turning the server's silent
refusal into an actionable message. The message
(`"{trainer} trains {class}s; you are a {class}."`, using the existing
`EverQuest::GetClassName()`) names both the class the trainer teaches and
the player's own class, exactly as the plan's example specifies. **The
caveat, stated per the plan rather than built around**: a server that sets
`RuleB(Character, AllowCrossClassTrainers)` to `true` would make this
refusal stricter than the server actually requires — the client would
block a cross-class train the server would allow. Not built as
client-side configuration; recorded here and left as a `deferred.md`-style
caveat rather than solved, per the plan's explicit instruction.

### `/bank`'s close half without a valid target

`BankerInteractIntent`'s handler (`eq_bridge.cpp`, untouched) is
**open-only** — reading it end to end shows no toggle/close branch at all
(if the bank window is already open, it just logs and returns). So `/bank`
does **not** route its close half through the intent: `CloseBankWindow()`
is still called directly, exactly as before this unit, and it deliberately
does **not** require a valid (or any) current target — confirmed by the
new `Bank_AlreadyOpen_ClosesDirectly_WithoutRequiringTarget` unit test,
which opens the bank window via the real `OpenBankWindow()` entry point
with no `CombatManager` target set at all, then shows `/bank` still closes
it and pushes no intent. This is a deliberate asymmetry: **open** now
requires a targeted banker (this unit's behaviour change), **close** never
required a target and still doesn't — you may have walked away or changed
target since opening, and closing must keep working regardless.

### The deliberate behaviour change, stated plainly

Before this unit, `/bank` took no target at all and "worked" in a
degenerate sense: it always opened a window (using a hardcoded dummy npc
id of 1 when the caller supplied none), which the very next movement tick
then closed because that id happened to belong to an unrelated, far-away
entity in the test zone (T49's live-reproduced finding). After this unit,
`/bank` requires a targeted banker to open — no target now prints "You
don't have a target - target a banker first." and pushes no intent. This
mirrors T48's `/vendor` exactly (same message shape, same
target-required-to-open / no-target-required-to-close asymmetry).

### NPC ids and classes tested (live, both trees)

Confirmed live against the DB before spawning, via `mysql_exec` (the
scripts/eqemu/env.sh helper) against the `peq` database:

| role | npc id | name | class |
|---|---|---|---|
| Banker (`CLASS_BANKER`) | 772 | `SummonClockworkBanker` | 40 |
| Trainer, matching Testchar's class | 1063 | `Brin_Stolunger` | 20 (`WarriorGM`, trains class 20-19=1=Warrior) |
| Trainer, wrong class (T49's exact repro NPC) | 1007 | `Miltiades_Tal\`Azroth` | 22 (`PaladinGM`, trains class 3=Paladin) |

Testchar is class 1 (Warrior, confirmed via `SELECT ... FROM
character_data WHERE name='Testchar'` before the run) — **a real
`WarriorGM` was found in the DB** (`SELECT id, name, class, level FROM
npc_types WHERE class=20` returned 20+ rows; `Brin_Stolunger`, id 1063, was
the first, at level 61 like the other class-20 NPCs), so the plan's STOP
condition ("cannot find a `WarriorGM`") did not trigger.

### Live acceptance — both trees, one continuous session each, no scratch hooks

`benchmarks/audits/T50/run_verification_primary.sh`, parameterized by
`WILLEQ_BIN`/`LOG_SUFFIX` exactly like T48/T49's driver, run once against
`build/bin/willeq` (`transcript_primary.txt`) and once against
`build-nogfx/bin/willeq` (`transcript_nogfx.txt`). eqemu LOCK FIRST
(`/mnt/projects/fable/.eqemu-lock` confirmed absent, created with
`T50-bank-train-prevalidation 2026-08-05`, removed after clean shutdown and
confirmed absent again); `scripts/eqemu/up.sh arena` only; both `run_...`
invocations share the **same** eqemu session (no restart between them,
matching T48/T49's practice of one session covering both trees).

**Banker leg — opens and STAYS OPEN, closes cleanly, `/npcstate` reports
it live (retiring T49's caveat):**

```
[System] Banker: none            <- baseline
...                                  /target <fresh banker>, /bank
[System] Banker: id=194          <- opened
... (8s of movement ticks pass) ...
[System] Banker: id=194          <- STILL open, same id -- T49's dummy-id
                                     bug (self-closed on the very next
                                     movement tick) is fixed
...                                  /bank (close)
[System] Banker: none            <- closed
```

Reproduced identically in the nogfx tree (`id=198`, same shape). This is
the first time in this project's history the bank window has been shown
open, held open, and read live via `/npcstate` — T49 could only reproduce
the self-closing bug, never a working session.

**Trainer leg (positive) — window ACTUALLY OPENS for the first time:**

```
[System] Trainer: Brin Stolunger (id=195)
```

(`id=199` in the nogfx tree.) Reached via `/target Brin_Stolunger00N`
(fresh `#npctypespawn 1063` spawn) then `/train`, which matched
`"Trainer window opened for"` in the log within 1s in both trees — the
server actually responded this time, because Testchar's own class (1)
matches what this trainer trains (20-19=1), unlike T49's Miltiades.

**Negative leg 1 — wrong-class trainer (T49's exact repro NPC), actionable
message replaces T49's silence:**

```
Miltiades_Tal`Azroth004 trains Paladins; you are a Warrior.
```

(`...005` in the nogfx tree, otherwise byte-identical.) T49 spawned this
exact same NPC (id 1007, class 22) and got total silence — no log line, no
message, ever. Here the refusal is immediate and names both classes.

**Negative leg 2 — too-far target:**

```
Warped to (2000.0, 2000.0, 3.0)
...
You are too far away to train with Brin_Stolunger002.
```

Reproduced identically in both trees. **How this leg was actually
produced is itself a finding, not just a mechanical detail** — two more
direct approaches were tried and rejected first:

1. The real GM command `#goto x y z` (`command_goto` →
   `Client::MovePC`, `zone/gm_commands/goto.cpp`) was tried first, since it
   is a sanctioned server *operation* (same class as `#npctypespawn`,
   `#givemoney`). It triggers a full same-zone client re-entry
   (`OP_ZoneChange` round trip, visible in the transcript as "Server
   requests zone change" → "Zone change response"). The server-approved
   position that came back was **not** the requested destination in either
   attempt — an out-of-bounds guess `(1000, 1000, 3)` and befallen's own
   real safe point (`zone` table, `zoneidnumber=36`: `safe_x=35,
   safe_y=-82`, confirmed by direct query) both echoed back Testchar's
   DB-persisted `character_data` position instead, with `success=1`. This
   looks like a real eqemu same-zone-teleport quirk in this environment;
   not investigated further (server-side, out of this unit's territory —
   noted here as a finding, not fixed).
2. Disconnecting (`/camp`), writing `character_data.x/y` directly (a
   sanctioned "documented character-position write"), and reconnecting
   **does** land the new session at the written coordinates (confirmed
   live via `/loc`) — but a probe `/who` after a full reconnect showed the
   `#npctypespawn`'d test NPCs from the prior session **gone**: they do
   not survive a full client disconnect/reconnect cycle in this
   environment (likely tied to depop-on-player-leave or a repop between
   sessions), so a fresh session would have had no trainer left to
   re-target.

**What actually worked**: `/warp x y z` (`src/client/eq_commands_movement.cpp`,
pre-existing, not added by this unit) — a purely client-side command that
sets `m_x`/`m_y`/`m_z` directly and calls `SendPositionUpdate()`. It moves
exactly the state `/train`'s own new distance check reads
(`CalculateDistance2D(m_x, m_y, target.x, target.y)`), needs no server
round trip, and leaves the still-targeted, still-alive trainer NPC exactly
where it was spawned. This is **not** a scratch hook added for this unit's
acceptance run (the plan's STOP condition) — it is pre-existing, real
`Command` registry code with its own production purpose, used here as a
GM-equivalent teleport the same way `#npctypespawn`/`#givemoney` were used
in T32-T48's live runs.

**One coordinate-convention wrinkle, caught and corrected, not silently
absorbed**: `/warp -77.9232 21.9086 3` (issued to return to the canonical
restore position before `/camp`) persisted to `character_data` as
`x=21.9086, y=-77.9232` — x/y **swapped** relative to the plan's restore
values, consistent with this codebase's well-documented client/server
x/y-swap convention (`eq.cpp`/`eq_zone.cpp` comments: "Server sends y
first"). `/warp`'s argument order is the client's own `(x, y, z)`, not the
server's wire order, so a literal restore-position argument list does not
round-trip through `SendPositionUpdate()` back to the exact same DB row.
Caught by a `SELECT` immediately after both runs (not assumed), and fixed
directly with `UPDATE character_data SET x=-77.9232, y=21.9086, z=3,
heading=190, zone_id=36 WHERE name='Testchar'` — confirmed by a second
`SELECT` to read back exactly `x=-77.9232, y=21.9086, z=3, heading=190,
zone_id=36` before `down.sh`.

**No scratch hooks**: `git status` for this unit touches only
`src/client/eq_commands_spells.cpp`, `tests/CMakeLists.txt`,
`tests/test_eq_commands_bank_train.cpp`, and new files under
`benchmarks/audits/T50/` — no temporary code was added to or reverted from
`eq_bridge.cpp`, `eq_merchant.cpp`, or anywhere else to make either run
work.

Server brought down cleanly (`down.sh`, all four containers stopped and
removed), lock removed and confirmed absent.

### Verification bar

1. Both configures build clean: `build` (`EQT_GRAPHICS=ON`,
   `EQT_GLES2=ON`) and `build-nogfx` (`EQT_GRAPHICS=OFF`) — clean rebuilds
   after every source change, no new warnings from this unit's files.
2. `ctest --output-on-failure`: `build` 1899/1900 passed, `build-nogfx`
   1467/1468 passed — the sole failure in each tree is the documented
   `AudioManagerTest.PreloadCommonSounds` (ALSA) exception. RDP unwaived:
   `RDPServer*` filtered run in `build` was 24/24 passing (no RDP suite in
   the `build-nogfx` tree — native RDP is a graphics-tree-only feature, not
   a gap this unit introduced). Golden replay confirmed to have **run**,
   not skipped, in both trees via direct `WILLEQ_SESSION_LOG=...
   test_session_replay` invocation: both logged real zone-map lookups from
   the capture (`befallen.map`, `qeynos2.map` "Unable to open" errors are
   expected/benign — same as T48/T49 — confirming the real
   recorded-session branch ran, not the baseline-creation fallback).
   `test_eq_commands_bank_train`'s 10 new cases pass in both trees
   (confirmed by direct binary run and by name in the `ctest` output).
3. Golden replay: see above.
4. `scripts/ci/visual_gate.sh`: `PASS` (qeynos2/gfaydark/befallen all
   verified); `git status` on `data/` empty before and after — goldens
   untouched.
5. Unit tests: `tests/test_eq_commands_bank_train.cpp`, 10 cases, modelled
   on T48's `test_eq_commands_vendor.cpp` seam — `/bank` no-target,
   non-banker target (asserts the intent still pushes; the class check is
   the untouched handler's job, not duplicated here), valid-banker target,
   already-open-closes-without-target; `/train` no-target, not-an-NPC,
   class-out-of-GM-range, too-far (asserts the message text, not just a
   refusal), wrong-class (asserts both class names appear in the message),
   matching-class-in-range (asserts no refusal). All 10 pass in both
   trees.
6. Live acceptance: see above — both legs of both pieces, plus both
   negative legs, reproduced identically in both trees, evidence under
   `benchmarks/audits/T50/`.
7. `bash scripts/refactor/check_file_sizes.sh --files <4 touched/added
   paths>`: `eq_commands_spells.cpp` 877 lines (`ok`), `tests/CMakeLists.txt`
   1240 lines (`ok`), `test_eq_commands_bank_train.cpp` 237 lines (`ok`),
   `benchmarks/audits/T50/run_verification_primary.sh` 241 lines (`ok`). No
   new FAILs.

### Does `/npcstate` now show banker and trainer state live?

**Yes, both — this retires T49's "reachable-but-blocked" caveat in full.**
T49 could migrate the state layer and land `/npcstate` but could not
exercise banker or trainer through it live, because both were blocked by
the exact two pre-existing bugs this unit's plan was written to fix (the
`/bank` dummy-id self-close, `/train`'s silent server drop on a
cross-class target). Both are now shown live, in both trees: `Banker:
id=194` persisting across 8 seconds of movement ticks, and `Trainer: Brin
Stolunger (id=195)` after a real, successful `HC_OP_GMTraining` round
trip. The board entry for T49 should be updated to say the
reachable-but-blocked caveat is closed, not merely narrowed.

### Is the automation-reachability pattern closed or narrowed again?

**Narrowed, not closed — and this unit is honest about that, per the
plan's request.** This is its fifth instance (`/combat on` T26, the
`EverQuest` registry T29, `/loot` T34, vendor T42→T48, `/bank`/`/train`
here). What this unit closes: `/bank` and `/train` are now both fully
reachable AND usable from the command channel with real, actionable
outcomes on every path (success and all four refusal shapes) — not just
technically callable the way `/bank` "worked" before. What it does not
close: T48's own Review flagged `AttackIntent`/`ToggleAutoAttackIntent` as
a likely-dead but unconfirmed instance of the same pattern, untouched
here (out of this unit's territory); and this unit's own live-acceptance
work surfaced a **new**, adjacent finding worth recording — the `#goto`
same-zone-teleport quirk (section above) is not the reachability pattern
itself, but it is the kind of "environment behaves unlike its surface
description" trap that has cost prior units real time (T49's Review
opens with exactly this framing), so it is flagged here rather than left
implicit in a shell script comment only.

### Deviations from the plan, flagged rather than absorbed

- The plan's literal restore recipe implicitly assumed a same-coordinate-system
  round trip; `/warp`'s client-side `(x, y, z)` argument order does not
  match `character_data`'s DB column order, so the restore required one
  extra direct `UPDATE` beyond what the plan's wording suggests. Confirmed
  correct via `SELECT` before `down.sh` (see above).
- The negative "too-far" leg uses `/warp` (pre-existing client-side
  command) rather than a GM server operation, after two GM-operation
  approaches were tried and found not to produce the intended effect in
  this environment (see the two rejected approaches above). This is not a
  scratch hook — `/warp` is production code with its own registered
  purpose, unmodified by this unit — but it is a different mechanism than
  T48/T49's practice of exclusively using GM `#` commands for live-run
  setup, so it is called out explicitly rather than silently substituted.
