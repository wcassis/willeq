# T317 — Three things the client asserts that are not true: guild rank, skill gates, and caps lock

**Slot: slot3. Branch `task/T317` cut from `fable`. x86 only. No Pi.
eqemu is held by slot1 — headless acceptance; live leg only if the lock frees,
and label live vs headless evidence.**

Closes **tier 2 items 6, 7 and 18** of `refactor/deferred.md`'s class-A list.
The first two are one defect class — **the client tells the server something it
has not checked**. The third is unrelated and tiny; it rides along because it is
four lines and would otherwise never be worth a dispatch.

## 1. Guild rank is HARDCODED on the wire (item 6)

`src/client/eq_guild.cpp:54` — `invite.officer = 2;  // TODO: Use actual rank`
(reached from `/guildinvite`), and `:105` — `remove.leaderstatus = 2;`
(from `/guildremove`).

*Impact:* both commands claim officer/leader status the player may not have.
The server's answer is whatever it makes of the lie.

**Find where the player's real guild rank lives** before writing anything —
`GuildState` / the player profile / the guild-member roster are the candidates.
If the client genuinely does not know its own rank, that is the finding: say so,
say what packet would carry it, and **send the honest unknown rather than a
confident 2**. Do not invent a rank.

⚠ Read eqemu's handler for both opcodes and say what it does with these fields
— if the server ignores them entirely, the fix is still right but the impact
line on the board is wrong and should be corrected (T314 set that precedent).

## 2. Skill combat and position gates are unenforced (item 7)

Four sites in `src/client/skill/skill_manager.cpp`:

| site | current state |
|---|---|
| `:272-274` | `if (skill.requires_combat) { /* TODO: Check if in combat */ }` — empty body |
| `:283-285` | `if (skill.requires_behind) { /* TODO */ }` — empty body |
| `:734-740` | `checkCombatRequirement()` substitutes **"has a target"** for "in combat" |
| `:758-761` | `checkPositionRequirement()` unconditionally `return true` |

*Impact:* Backstab's behind-the-target requirement is never checked client-side,
so the client sends attempts the server refuses.

**The comment at `:276-283` is load-bearing — read it before you touch this.**
T159 removed a refusal message that *lied* (it blamed facing for what was
actually an endurance failure), and left a written instruction that **the check
and the message go back in together.** Honour that: if you implement the facing
check, restore the message; if you cannot, leave both out and say why. Do not
restore a message without the check.

For the combat gate: "has a target" is not "in combat". `CombatManager` is right
there — find whether it exposes a real in-combat state and use it; if it only
exposes a target, that is the finding.

For facing: it is a heading comparison between the player and the target.
⚠ **`docs/CLAUDE.md`'s Heading/Rotation Conventions warning applies** — players
and NPCs do not share default rotations, and that asymmetry is load-bearing and
must NOT be harmonised (see the memory `heading-90-offset-load-bearing`). Derive
the behind-test from the server's own definition if eqemu has one, and say which
convention you used.

## 3. RDP remote input ignores caps lock (item 18)

`src/client/graphics/renderer_rdp.cpp:159` —
`bool capsLock = false;  // TODO: track caps lock state`, feeding
`rdpScancodeToChar(scancode, shift, capsLock)`.

*Impact:* typing over RDP produces the wrong case whenever caps lock is on.

Track the state from the RDP key stream (the sibling `shift` two lines above is
the template). Note caps lock is a **toggle**, not a modifier — a key-down
flips it; it is not held. FreeRDP also carries the client's lock state in its
sync/`KBD_SYNC` message; prefer that if it is reachable.

## Acceptance

- Guild: a test asserting the sent packet carries the player's real rank, for at
  least two different ranks — or, if the client cannot know it, a test pinning
  the honest-unknown value with the reason in the test name.
- Skills: a test per gate, each asserting the **negative** (the gate refuses when
  it should) — a test that only proves the allowed case passes on today's code.
  Include the T159 case: a Backstab failing for endurance must not blame facing.
- RDP: a table test over caps-lock on/off × shift on/off (four cases; caps+shift
  is the one that catches a naive OR).
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`;
  visual gate PASS, goldens untouched; `check_file_sizes.sh --files` no new FAILs.

⚠ **If the facing check turns out to need real work, STOP that piece, deliver
the other two, and report** — do not let it swallow the unit.

## Territory

`src/client/eq_guild.cpp`, `src/client/skill/skill_manager.cpp`,
`src/client/graphics/renderer_rdp.cpp`, plus your tests.
⚠ Concurrent: slot1 T311 Leg B (`eq_movement.cpp`, `eq_bridge.cpp`,
`eq_entities.cpp`, `spell_cast_checks.cpp`), slot2 T316
(`eq_action_handler.cpp`, `action_dispatcher.cpp`, `spell/spell_database.cpp`,
`willeq_database`). **`src/client/spell/` is slot2's — `skill/` is yours; do not
stray between them.**

---

## Review

**Executed in slot3 on `task/T317`, cut from `fable` @ `f716a56c`. x86 only, no Pi.
eqemu was held by slot1 (T311 Leg B) for the whole unit — EVERY piece of evidence
below is HEADLESS (unit tests + source reading of the eqemu tree at
`/mnt/projects/akk-stack/code`). No live leg was run and none is claimed.**

All three defects are fixed. Two of the three had a *wrong premise on the board*,
and correcting the premise changed the fix.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | PASS (EXIT=0) |
| `ctest --test-dir build -j6` | **3427 tests, 1 failed** — `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk. Zero new failures vs `refactor/test_baseline.txt`. |
| `scripts/build-gles2-x86.sh` | PASS (EXIT=0) |
| Visual gate | **PASS**, goldens untouched (`git status` clean of `refactor/goldens`) |
| `scripts/build-arm-noble.sh` | PASS (EXIT=0) |
| `check_file_sizes.sh fable` | 19 ok / 6 warn / **1 fail — pre-existing**: `src/client/eq.cpp` was already 3913 lines (> 3000) on `fable`; this unit takes it to 3923. See "Deviations". |

New tests: **36** (`test_skill_gates` 16, `test_guild_wire_rank` 10,
`test_rdp_input_handler` +10). All negative-first.

---

### 1. Guild rank on the wire — THE BOARD'S IMPACT LINE IS WRONG (T314 precedent)

The board said both commands "claim officer/leader status the player may not
have". Reading both eqemu handlers in full says otherwise, and neither field is
the sender's rank.

**`OP_GuildInvite` — `GuildCommand_Struct::officer` is the rank GRANTED TO THE
INVITEE.** `Client::Handle_OP_GuildInvite` (`zone/client_packet.cpp:8152`):

```
auto rank = gc->officer;
if (ClientVersion() < RoF) switch (gc->officer) {
    case GUILD_MEMBER_TI (0):  rank = GUILD_MEMBER  (5); break;
    case GUILD_OFFICER_TI(1):  rank = GUILD_OFFICER (3); break;
    case GUILD_LEADER_TI (2):  rank = GUILD_LEADER  (1); break;
    default:                   rank = GUILD_RANK_NONE(0); break;
}
```

`rank` is then compared against the **target's** `client->GuildRank()` to choose
promote vs demote, or handed to `guild_mgr.RecordInvite(target_char_id,
guild_id, rank)`. The **sender's** authority is taken from the server's own
`GuildRank()` through `guild_mgr.CheckPermission(...)` — the packet field is
never consulted for it.

So `officer = 2` was not a lie about us. It was **`/guildinvite Bob` asking the
server to install Bob as GUILD LEADER** (or, if Bob was already a member, to
promote him to leader). The only thing standing in the way was our own
server-side permission check. **The real severity is higher than the board's,
not lower.** Fixed: a plain invite now sends `GUILD_MEMBER_TI` (0).

**`OP_GuildRemove` — `leaderstatus` is NEVER READ.**
`Client::Handle_OP_GuildRemove` (`:8672`) casts the body to
`GuildCommand_Struct` and reads exactly one field, `othername` at offset 0
(which is `GuildRemove_Struct::target`; a test pins that the two offsets agree).
Authority again comes from the server's own `GuildRank()` via
`CheckPermission(GUILD_ACTION_MEMBERS_REMOVE)`. **Whatever we put there is
discarded.** The fix is still right — we now send the truth — but *nothing
observable changes server-side*, and the board should say so.

**Where the real rank lives (asked for, found, wired).** `PlayerProfile_Struct`
offset **13055**, one byte after `gm@13054` — the field T148 verified live. The
server fills it in `common/patches/titanium.cpp:1494`, folding its internal
0..8 scale to the three Titanium values (member 0 / officer 1 / leader 2, else
`GUILD_RANK_NONE_TI` = 9). The client simply was not reading the byte. It now
does, in the same once-per-login shape as `m_guild_id`/`m_gm_mode`, exposed as
`EverQuest::GetGuildRank()`, and `/guildremove` sends it.

*Evidence:* `test_guild_wire_rank` (10 tests) — rank parsed through the real
`HC_OP_PlayerProfile` path for **all three** ranks; a test that the byte is not
confused with the `gm` byte beside it (a one-byte slip would make them track);
negative assertions that invite is `!= kRankLeader` and `!= kRankOfficer`, and
that remove carries member/officer/leader distinctly rather than a constant 2.

⚠ *Testability limit, stated rather than papered over:* `QueuePacket()` no-ops
without a zone connection and leaves nothing inspectable (the same limitation
`test_combat_manager.cpp`'s T23 note records), so the bytes are pinned on the
pure builders in the new `include/client/guild_packet_bodies.h` — which are now
the **only** thing those two senders use to fill their structs — plus a
reachability test that both senders still run to completion.

### 2. Skill gates

**The combat gate.** `checkCombatRequirement()` returned
`GetCombatManager()->HasTarget()`. Every `requires_combat` skill also sets
`requires_target`, so `checkTargetRequirement()` had already asked exactly that
question — the combat gate **could not refuse anything**.

`CombatManager` *does* expose a real in-combat state, and it is not
auto-combat-only: `EnableAutoAttack()`/`EnableAutoFire()` set
`COMBAT_STATE_ENGAGED` directly (`combat.cpp:489`, `:607`) with no `m_enabled`
check, and both are reachable from `/aa` and the Combat hotbar slot. Plus
T247's aggro memory (`IsRecentAttacker`) answers "is something hitting me".
In-combat is now: engaged, **or** auto-attack/auto-fire on, **or** the current
target has recently attacked us.

⚠ **DELIBERATELY STRICTER THAN THE SERVER — a product call for Will.**
`Client::OPCombatAbility` (`zone/special_attacks.cpp:325`) has **no in-combat
requirement at all**: target exists, not feared/stunned/mezzed/dead/casting,
`MaxSkill()`, target id matches, `IsAttackAllowed`. So this gate can refuse a
Kick that eqemu would have accepted (auto-attack off, no aggro). I implemented
it because the plan directed it explicitly; if it is unwanted the fix is to
clear `requires_combat` in `skill_data.h`, not to re-hollow the function — the
comment at the call site says so.

**The facing gate — implemented, not stopped.** It did *not* turn out to need
real work, for one reason: `Mob::BehindMob` uses the **target's** heading and
both positions, and **never the attacker's heading**. All entities in
`EverQuest::GetEntities()` carry the unflipped server heading; only the
player's own heading is flipped (`90 - server_heading_deg`). So the
load-bearing asymmetry `docs/CLAUDE.md` warns about **never enters this
computation** and nothing was harmonised.

*Convention used:* **the server's, in server coordinates.** `MobAngle`
(`zone/mob.cpp:4576`) ported byte-for-byte into
`src/client/skill/behind_target.cpp` — same operation order, same
`3.1415`-not-`M_PI`, same `acos` domain clamps. Callers undo only the client's
x/y **swap** (`entity.x` is server y). Behind is `MobAngle > 90`, eqemu's
`mob.h:222`. One deliberate departure: at coincident positions eqemu divides by
zero; we return 0 (not behind).

Keeping eqemu's `3.1415` is load-bearing, and the tests measured why: a dead
flank comes out at 89.9999924 and just-over-90 on the two sides — the razor
edge **straddles** the threshold by ~1e-5° because the facing vector is built
from a hair-off-square rotation. Which side it falls on is float noise **in the
server too**; running the identical arithmetic is what makes client and server
land on the same side. Do not tidy the constant.

**`checkPositionRequirement()` had no caller** — `canUseSkill()` never consulted
it. Implementing it without wiring it in would have left the feature exactly as
unreachable as before (this board's dominant defect class). It is now called.

**T159's instruction honoured: BOTH went back in.** The check is implemented
*and* `"You must be behind your target"` is restored — and it sits **below** a
repeat of the resource predicate, so the T159 case still says endurance.
Separately, both `getSkillUseError()` branches were **empty bodies**, so
`canUseSkill()` could refuse while the error string came back `""` and
`activateSkill()` fired its failure callback with a blank reason. Both now
return real text.

*Evidence:* `test_skill_gates` (16 tests). Negatives:
`NEGATIVE_KickIsRefusedWithATargetButNoCombat` (the exact state the old gate
passed), `NEGATIVE_AggroFromSomeoneElseDoesNotUnlockTheGate`,
`NEGATIVE_BackstabIsRefusedFromTheFrontOfTheTarget` (+ no cooldown started, so
`sendSkillUsePacket()` demonstrably never ran),
`TheGateFollowsTheTargetsHeading_NotThePlayers` (spin the target 180°, answer
flips — this is the test that catches a "harmonised" heading),
`AnUnknownTargetDoesNotProduceAFacingComplaint`, and
`T159_ABackstabShortOfEnduranceBlamesEnduranceNotFacing` — player genuinely
facing the target *and* out of endurance; endurance wins.

### 3. RDP caps lock

`rdpScancodeToChar()`'s own `shift ^ capsLock` was already right; only the call
site lied (`bool capsLock = false;`). New `RDPLockKeyState`
(`rdp_input_handler.h/.cpp`) remembers it, fed from two places:

- the key stream — scancode `0x3A`, **key-down only**, and **only on the
  up→down transition** so RDP auto-repeat is one toggle, not seven;
- the client's `KBD_SYNC` message. `onSynchronizeEvent` was discarding its
  flags; it now forwards them through a new `RDPServer::setSyncCallback`. This
  is the **only** way to learn caps lock was already on when the session
  connected — the key stream cannot report a press from before we were
  listening.

⚠ `KBD_SYNC_CAPS_LOCK` is **`0x00000004`**, verified against
`/usr/include/freerdp3/freerdp/input.h`. `0x2` is `KBD_SYNC_NUM_LOCK`; a test
asserts our constant equals FreeRDP's and that num/scroll do not set caps.

*Evidence:* `test_rdp_input_handler` +10 tests, including the required
4-case table (caps+shift → lowercase, catching a naive OR), the negative that
caps must **not** turn `1` into `!`, the toggle-not-modifier negative, and
auto-repeat.

---

### Found and deliberately NOT fixed

1. **`OP_GuildInviteAccept` conflates "accept/decline" with "rank", and DECLINE
   IS UNREACHABLE FOR TITANIUM.** `Handle_OP_GuildInviteAccept`
   (`client_packet.cpp:8332`) maps the client's `response` through the same TI
   switch (`0 → GUILD_RECRUIT(8)`, `1 → GUILD_OFFICER(3)`, else
   `GUILD_RANK_NONE(0)`) and then tests `response >= GUILD_INVITE_DECLINE (9)`.
   **No value a Titanium client can send survives that mapping as ≥ 9**, so a
   Titanium decline is processed as an accept. Worse, our `SendGuildInviteAccept`
   sends `response = 1`, so **accepting a guild invite joins you as
   GUILD_OFFICER**. Not fixed: it is a third guild defect outside this plan's
   two named sites, the decline half is a **server** bug we cannot fix
   client-side, and the accept half needs a decision about which rank to send.
   Recommend a board entry.
2. **`SkillData::endurance_cost` and `mana_cost` are 0 for every skill.**
   Nothing in `initializeSkillData()` ever sets them, so
   `checkResourceRequirement()` can never fail from shipped data — the "Not
   enough endurance"/"Not enough mana" messages are unreachable in production.
   The T159 test reaches the branch through the existing public mutable
   `getSkill()`. Not fixed: populating real costs is a data question.
3. **`requires_combat` corresponds to nothing eqemu enforces** (see §2). Left in
   place and implemented as directed, flagged above for Will.

### Deviations

- **Territory.** Three one-to-few-line edits outside the plan's named files, all
  unavoidable and all flagged: `src/client/eq.cpp` + `include/client/eq.h` (the
  `guildrank` parse must live in `ZoneProcessPlayerProfile`, beside `m_gm_mode`
  — there is no other site) and `include/common/packet_structs.h` (comment-only:
  both struct comments described the fields wrongly, which is how the board's
  premise got written). Also touched: `rdp/rdp_server.{h,cpp}`,
  `rdp/rdp_input_handler.{h,cpp}` and `irrlicht_renderer.h` — the RDP module
  around `renderer_rdp.cpp`, unclaimed by any concurrent unit. Nothing in
  `src/client/spell/`, `eq_movement.cpp`, `eq_bridge.cpp`, `eq_entities.cpp` or
  `spell_cast_checks.cpp`.
- **`check_file_sizes.sh` FAIL on `src/client/eq.cpp` is PRE-EXISTING**: 3913
  lines on `fable` (already > 3000), 3923 here. The +10 is the guildrank parse
  and its comment; it cannot move out of that function.
- **No live evidence.** eqemu was locked by slot1 for the unit's whole duration.
  The guild fix in particular is source-derived from the eqemu tree and
  test-pinned, **not wire-captured** — worth one live `/guildinvite` when a lock
  frees.
