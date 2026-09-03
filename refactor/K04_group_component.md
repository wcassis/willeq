# K04 — `GroupComponent` (family: group, includes raid and pet)

Batch: `refactor/K_component_extraction.md` — §Mechanism (REVISED). Runs
alongside K02/K03 (Wave 1).

## Measured at `7ffc4522` (2026-09-03)

`src/client/eq_group.cpp`, 980 lines, 25 `EverQuest::` bodies (group + raid
+ pet; they share `m_game_state.pet()`/`group()` and stay together — the
component is `GroupComponent`, say "group, raid and pet" in its header
comment), one root `CMakeLists.txt` site.

| method | callers outside the family | tests |
|---|---|---|
| `AcceptGroupInvite`, `GetGroupMember`, `SendGroupDecline`, `SendGroupInvite` | `eq_action_handler.cpp` | 1/1/0/0 |
| `SendLeaveGroup` | `eq_action_handler.cpp`, `action/command_processor_social.cpp` | 0 |
| `SendGroupDisband` | `action/command_processor_social.cpp` | 0 |
| `SendPetCommand` | `eq_action_handler_spells_trade.cpp` | 1 |
| `SendRaidCommand` | `eq_action_handler_extended.cpp`, `action/action_dispatcher.cpp`, `action/command_processor_social.cpp`, `action/action_handler.h` | 0 |
| `ClearGroup` | `eq_zone.cpp` | 0 |
| `GetPetLevel`, `GetPetName` | `eq.cpp` | 0 |
| `ZoneProcessGroupCancelInvite`, `ZoneProcessGroupDisband`, `ZoneProcessGroupFollow`, `ZoneProcessGroupInvite`, `ZoneProcessGroupUpdate` | `eq_zone_dispatch.cpp` | 0 |
| `ZoneProcessRaidUpdate` | `eq_entities_updates.cpp`, `eq_zone_dispatch.cpp`, `raid_update.h` | 1 |
| `DeclineGroupInvite`, `SendGroupFollow` | none in src/include | 1 each — keep, retarget tests |
| `DismissPet`, `FillGroupMemberFromEntities`, `FindGroupMemberByName`, `GetPetEntity`, `UpdateGroupMemberFromEntity`, `ZoneProcessGroupJoin` | none in src/include, 0 tests | **internal to the family if called within `eq_group.cpp`; if a method has zero callers INCLUDING inside the family, delete it and board nothing (§5)** |

Re-measure at your tip. `eq.cpp`'s `GetPetLevel`/`GetPetName` callers: read
them — if they are in the ctor/wiring, retarget; if they are `Test_` seams,
move the seam with the method.

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_in_raid`, `m_pending_inviter_name`, `m_pending_raid_inviter`, `m_pet_button_states`, `m_raid_leader`, `m_raid_locked`, `m_raid_roster` | 1–6 | none | **move** (7 members) |
| `m_test_bypass_send_guard` | 2 | `eq_movement_state.cpp`, `eq_movement_update.cpp`, `Test_SetBypassSendGuardForSeam()` | shared non-core: stays, `owner_.` |
| `m_zone_connection_manager` (5), `m_zone_connection`, `m_bridge`, `m_character`, `m_entities`, `m_game_state` (67) | core | | `ctx_.` |

Predicted `eq.h` delta: −25 declarations (fewer if some are already
private-only and grouped) −7 members −(the "Pet queries" T40 comment block
travels with the pet getters) +4 scaffolding ≈ **−28 or better**.

## Steps

Per §Mechanism. New files `include/client/group/group_component.h`,
`src/client/group/group_component.cpp` (`git mv`), `tests/test_group_component.cpp`
(register next to the existing group/raid tests — grep `SendGroupFollow` in
`tests/cmake/`). Port two cases (accept invite; raid command) to the component.

## Territory

`include/client/group/`, `src/client/group/` (new), `src/client/eq_group.cpp`
(moves), `include/client/eq.h`, `src/client/eq.cpp` (ctor + the two pet-getter
call sites), root `CMakeLists.txt`, `tests/test_group_component.cpp` + cmake,
allowlist (one number), `docs/module_map.md` (group row), `include/client/raid_update.h`
(one call site if it is a real call), and CALL-SITE-ONLY edits in
`eq_action_handler.cpp`, `eq_action_handler_extended.cpp`,
`eq_action_handler_spells_trade.cpp`, `eq_zone_dispatch.cpp`, `eq_zone.cpp`,
`eq_entities_updates.cpp`, `action/command_processor_social.cpp`,
`action/action_dispatcher.cpp`, `action/action_handler.h`, and the tests that
call retargeted methods.

⚠ Same collision note as K02.

## Verification / Review

As K02.

## Review

Executed in `wt/slot3` on `task/K04-group-component`, branched from `fable`
at `feb6b10b`. Ran alongside K02 (slot1) and K03 (slot2).

### Measured vs the plan's leads

Re-measured with `grep -rw` over `src/ include/ tests/` at `feb6b10b`.

**Methods.** `src/client/eq_group.cpp` has **25** `EverQuest::` bodies, exactly
as the plan says. The caller table is confirmed with **four differences, all in
the same direction — the plan counted COMMENTS as callers:**

| method | plan's lead | measured |
|---|---|---|
| `SendGroupDisband` | caller in `action/command_processor_social.cpp` | that line (222) is a **comment** in prose. **Zero external callers.** One INTRA-family caller (`SendLeaveGroup`, line 148) → moved, made **private** |
| `SendLeaveGroup` | `eq_action_handler.cpp` + `command_processor_social.cpp` | `command_processor_social.cpp:221` is a **comment**. One real caller: `eq_action_handler.cpp:725` |
| `SendRaidCommand` | 4 files | only `eq_action_handler_extended.cpp` (**9** call sites) is real; `action_dispatcher.cpp:308`, `command_processor_social.cpp:239` and `action/action_handler.h:384` are all **comments** naming it in prose. Left as-is (they still describe the behaviour accurately) |
| `ZoneProcessRaidUpdate` | `eq_entities_updates.cpp`, `eq_zone_dispatch.cpp`, `raid_update.h` | `eq_entities_updates.cpp:8` and `raid_update.h:4` are **comments**. One real caller: `eq_zone_dispatch.cpp:178` |

**The six flagged methods — keep/delete decision (§Mechanism 5):**

| method | intra-family callers | decision |
|---|---|---|
| `FillGroupMemberFromEntities` | 2 (lines 484, 692) | **keep, private** |
| `FindGroupMemberByName` | 1 (line 542) | **keep, private** |
| `GetPetEntity` | 2 (`GetPetName`, `GetPetLevel`) | **keep, private** |
| `ZoneProcessGroupJoin` | 1 (line 431, from `ZoneProcessGroupUpdate`) | **keep, private** |
| `DismissPet` | **0** | **DELETED** (5 lines). Zero callers anywhere, inside the family or out |
| `UpdateGroupMemberFromEntity` | **0** | **DELETED** (28 lines). Zero callers anywhere |

`SendGroupDisband` joins the private group by the same measurement, making it
**five private members and 23 moved bodies**, not 25.

`eq.cpp`'s `GetPetName()`/`GetPetLevel()` callers (lines 752–753) are **not**
`Test_` seams: they are inside the state-snapshot publisher that builds
`PetCreatedData`. Retargeted to `m_group->GetPetName()` / `->GetPetLevel()`.

**Members.** The plan lists seven to move. **Only five exist.** Two are ghosts:

| member | plan | measured |
|---|---|---|
| `m_in_raid` | move | 5 refs + `IsInRaid()`. **Moved** |
| `m_raid_leader` | move | 5 refs + `GetRaidLeader()`. **Moved** |
| `m_pending_raid_inviter` | move | 4 refs + `GetPendingRaidInviter()`/`ClearPendingRaidInvite()`. **Moved** |
| `m_raid_roster` | move | 1 ref + `GetRaidMembers()`. **Moved** |
| `m_raid_locked` | move | 6 refs + `IsRaidLocked()`. **Moved** |
| `m_pending_inviter_name` | move | **DOES NOT EXIST.** Both hits in `eq_group.cpp` (62, 166) are COMMENTS; T286 moved the real storage into `GroupState`. Nothing to move |
| `m_pet_button_states` | move | **DOES NOT EXIST.** The single hit (line 240) is a COMMENT describing what the *official* client does. Nothing to move |
| `m_test_bypass_send_guard` | shared, stays | confirmed — `eq_movement_state.cpp`, `eq_movement_update.cpp`, `Test_SetBypassSendGuardForSeam()`. **Stays**, `owner_.m_test_bypass_send_guard` |
| core members | `ctx_.` | confirmed: `m_game_state` (67), `m_bridge` (18), `m_character` (15), `m_zone_connection` (9), `m_zone_connection_manager` (5), `m_entities` (4) |

**A dependency the plan's tables did not list.** The five moved members are read
by **six inline accessors on `EverQuest`** that the plan's method table never
names, because they are inline in `eq.h` and have no `EverQuest::` body:
`IsInRaid`, `GetRaidLeader`, `GetPendingRaidInviter`, `ClearPendingRaidInvite`,
`GetRaidMembers`, `IsRaidLocked`. They cannot stay behind once their storage
moves, so **they moved into the component too** and their callers
(`eq_action_handler_extended.cpp` ×15, `tests/test_eq_action_handler_raid_guards.cpp`
×5) were retargeted. This is the K04 analogue of K01's "two dependencies found
only by compiling", except it was found by measuring — see correction 1.

**Owner methods the moved bodies still call** (the `owner_.` substitution):
`IsFullyZonedIn()`, `AddChatSystemMessage()`, `HasPet()`, `MySpawnId()`,
`OnPetButtonStateChanged()`. **No `s_debug_level`** in this family, so K01's
correction-2 third substitution was not needed here.

**Text readers of `eq.h`.** `test_duplicate_state_stores` and
`test_reachability_sweep` both **pass unchanged**; no file-list edit was needed.

### The three `eq.h` numbers

| | lines | vs dispatch |
|---|---|---|
| at dispatch (`fable` `feb6b10b`) | **2,024** | — |
| **predicted** before starting | **1,973** | −51 |
| **measured** after | **1,970** | **−54** |

The plan's pre-dispatch estimate was "−28 or better"; the executor's own
prediction, computed line-by-line from the measured decl/member/banner counts
before any edit, was −51. Measured −54. The three-line overshoot is the T40
pet-comment trim (the old note explained why the moved getters *stay*, which is
no longer true) plus the empty `// Raid actions` banner, which had nothing under
it and was this family's.

`scripts/refactor/file_size_allowlist.txt` lowered **2,024 → 1,970**.
⚠ **Shared file**: K02/K03 also lower this same line. Measured value for K04
alone is **1970**; the orchestrator resolves at merge.

Breakdown of the −54 (57 deleted, 3 net added):

| deleted from `eq.h` | lines |
|---|---|
| `GetGroupMember` decl | 1 |
| "Group actions" banner + 5 senders + blank | 7 |
| `AcceptGroupInvite`/`DeclineGroupInvite` | 2 |
| empty `// Raid actions` banner + blank | 2 |
| pet getters ×3, and the T40 comment retargeted (net) | 6 |
| "Pet commands" banner + `SendPetCommand` + `DismissPet` + blank | 4 |
| `SendRaidCommand` (2 lines) + its T179 comment | 3 |
| the six raid accessors + their T179/T180/T182 comments | 13 |
| `ZoneProcessRaidUpdate` decl | 1 |
| the five raid members + comment + blank | 7 |
| "Group helper methods" + "Group packet handlers" banners, 10 decls, T320 comment, blanks | 16 |
| **added** | |
| `#include "client/group/group_component.h"` | +1 |
| `Group()` accessor + 2 comment lines | +3 |
| `friend class eqt::group::GroupComponent;` | +1 |
| `std::unique_ptr<...> m_group;` | +1 |
| **net** | **−54** |

K01's prediction that "K02 onward start from a +2 baseline" holds: K04 paid
exactly +6 of scaffolding (4 code, 2 comment), no `ClientContext` cost at all.

### Retarget list (no forwarders — §Mechanism 5)

| file | sites | change |
|---|---|---|
| `include/client/eq.h` | 25 declarations + 6 inline accessors + 5 members | **deleted** |
| `src/client/eq.cpp` | 2 | `GetPetName()`/`GetPetLevel()` → `m_group->…` (+ ctor construction of `m_group`) |
| `src/client/eq_action_handler.cpp` | 6 | `m_eq.X(` → `m_eq.Group().X(` |
| `src/client/eq_action_handler_spells_trade.cpp` | 2 | `SendPetCommand` |
| `src/client/eq_action_handler_extended.cpp` | 24 | 9 `SendRaidCommand` + 15 raid-accessor reads |
| `src/client/eq_zone_dispatch.cpp` | 7 | `ZoneProcessGroup*(p)` / `ZoneProcessRaidUpdate(p)` → `m_group->…` |
| `src/client/eq_zone.cpp` | 1 | `ClearGroup()` → `m_group->ClearGroup()` |
| `tests/test_group_accept_gm_hideme.cpp` | 7 | `eq.X(` → `eq.Group().X(`. **Assertions untouched** |
| `tests/test_eq_action_handler_raid_guards.cpp` | 5 | same, inside `ASSERT_*` — the call is retargeted, the assertion's meaning is not |
| `tests/test_eq_action_handler_transforms_real.cpp` | 5 | same |
| `include/client/raid_update.h` | **0** | its `ZoneProcessRaidUpdate` mention is a COMMENT, not a call |
| `include/client/action/action_handler.h` | **0** | line 384 is a comment |
| `src/client/action/command_processor_social.cpp`, `action_dispatcher.cpp` | **0** | comments only |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/eq.h` | 2,024 | **1,970** (−54) |
| `src/client/eq_group.cpp` → `src/client/group/group_component.cpp` | 980 | 960 (+13 header comment, −33 the two deleted bodies) |
| `include/client/group/group_component.h` | — | 112 (new) |
| `tests/test_group_component.cpp` | — | 152 (new) |

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides before `sort`:
`s/eqt::group::GroupComponent:://`, `s/EverQuest:://`,
`s/ctx_\.game_state/m_game_state/`, and the same for `bridge`, `character`,
`zone_connection_manager` (BEFORE `zone_connection` — it is a prefix),
`zone_connection`, `entities`; then `s/owner_\.//`, trailing whitespace.

Run 1 (old file as-is vs new): residual is the 13 added lines below **plus**
the 33 lines of the two deleted bodies. Run 2 (old file with those two bodies
excised): residual is **13 lines, all ADDED, zero deleted, zero code lines
modified** —

```
> //
> // K04 (Batch K, refactor/K_component_extraction.md): moved again, from
> // src/client/eq_group.cpp to here, and the bodies rebased from EverQuest onto
> // eqt::group::GroupComponent. Still verbatim -- only the substitutions
> // Mechanism 4 allows (m_game_state/m_bridge/m_character/m_zone_connection/
> // m_zone_connection_manager/m_entities -> ctx_.*; m_test_bypass_send_guard ->
> // *, it stays on EverQuest; IsFullyZonedIn/AddChatSystemMessage/HasPet/
> // MySpawnId/OnPetButtonStateChanged -> *). The raid state members
> // (m_in_raid, m_raid_leader, m_pending_raid_inviter, m_raid_roster,
> // m_raid_locked) moved into the component and are unqualified here as before.
> // DismissPet() and UpdateGroupMemberFromEntity() were deleted, not moved:
> // zero callers anywhere. See the K04 Review.
> #include "client/group/group_component.h"
```

(the `owner_.` in two of those comment lines is the normalisation stripping its
own prefix out of the note that documents it — it is the new comment, not code.)
As K01 correction 7 says, no scaffolding, friend or accessor line appears here:
those are all in `eq.h`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, clean on the first attempt. (The first invocation hit a 2-minute tool timeout, exit 143; re-run to a log file and confirmed exit 0 with `grep -c "error:"` = 0 and every target reporting Built) |
| `ctest -j6 --output-on-failure` | **3,509 / 3,510 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling` **PASSES**, `DocCitations` **PASSES**, `DuplicateStateStores` and `ReachabilitySweep` **PASS unchanged** |
| `test_group_component` (new) | 5 tests, all pass |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate | **not run, and correctly so**: `git diff --cached --name-only -M` matches nothing under `src/client/graphics` or `include/client/graphics` — the 16 changed paths are `CMakeLists.txt`, `include/client/eq.h`, `include/client/group/group_component.h`, `scripts/refactor/file_size_allowlist.txt`, `src/client/eq.cpp`, `src/client/eq_action_handler{,_extended,_spells_trade}.cpp`, `src/client/eq_zone{,_dispatch}.cpp`, `src/client/group/group_component.cpp`, `tests/cmake/packets_skills.cmake`, `tests/test_eq_action_handler_{raid_guards,transforms_real}.cpp`, `tests/test_group_{accept_gm_hideme,component}.cpp`, `docs/module_map.md`. It also ran inside the ctest above and passed |
| `scripts/build-arm-noble.sh` | **not run**, same evidence |
| `scripts/refactor/check_file_sizes.sh --all` | **801 ok, 0 warn, 0 fail** (ratchet lowered to 1,970) |

### Corrections to §Mechanism

1. **§Mechanism 3 and every plan's member table must also list the INLINE
   ACCESSORS that read a moving member.** `eq.h` holds ~180 one-line inline
   getters. They have no `EverQuest::` body, so they are invisible to the
   "count the `EverQuest::` defs in the family TU" measurement every K plan is
   built on — and yet a member cannot move without them. K04's six raid
   accessors are 8 code lines and 5 comment lines, a seventh of the whole
   `eq.h` saving, and they carry 20 external call sites that the plan's caller
   table did not know about. **Every K plan from K05 on must add a step: for
   each member marked "move", `grep -n "m_<name>" include/client/eq.h` and list
   the inline accessors that read it, with their own caller counts.**

2. **A plan's caller table must be built with a comment filter.** Four of
   K04's caller leads and two of its seven members were COMMENTS — prose
   mentions of a symbol, not references to it. `grep -rw` alone cannot tell
   them apart, and a plan that counts them overstates the retarget cost and,
   worse, invents members that no longer exist (`m_pending_inviter_name` and
   `m_pet_button_states` were both removed by T286/T40 and survive only as
   commentary). Planning should quote the matching LINE, not the count.

3. **§Mechanism 5's "delete a method with zero callers" needs "including
   inside the family" spelled out in the ratchet arithmetic.** K04 deleted two
   bodies (33 lines) — real dead code, the reachability pattern again — but
   also made four methods private rather than deleting them. Both outcomes
   remove a declaration from `eq.h`; only the first removes a body. The plan's
   predicted delta should count declarations, not bodies, which it did.

4. **The substitution pass must order prefix-overlapping member names longest
   first.** `m_zone_connection_manager` contains `m_zone_connection`. A
   `\bm_zone_connection\b` regex is safe (the word boundary saves it), but the
   §Mechanism 8 *normalisation* `sed` is a plain `s///` and is NOT: applied in
   the wrong order it turns `ctx_.zone_connection_manager` into
   `m_zone_connection_manager` only by accident. K01 correction 3 gave the
   name-mapping rule; this adds the ordering rule to it.

5. **§Mechanism 4's comment exclusion needs a block-comment case.** K01's
   correction 4 says "exclude comment lines". The pass used here excludes
   `//`-leading lines, `/* … */` blocks, and the trailing `//` tail of a code
   line. `eq_group.cpp` has a trailing-comment case on line 88
   (`ctx_.game_state.group().clearPendingInvite();  // T286: only store`) where
   substituting the code half and preserving the tail is exactly right.

### Left for the board

- `EverQuest::DismissPet()` and `EverQuest::UpdateGroupMemberFromEntity()` were
  deleted, not boarded — §Mechanism 5 deletes outright, and they are gone. Noted
  here only because they are the reachability pattern's next two instances (33
  lines of dead code inside a family that three units had already touched).
