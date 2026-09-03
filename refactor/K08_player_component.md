# K08 — `PlayerComponent` (family: player — social, buffs, misc, state, camp, duel, corpse, inspect, AA/tribute handlers)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K06/K07). Wave 3; runs alongside K10. **K09 (zone) waits for both.**

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

Six TUs, 142 `EverQuest::` bodies: `eq_player_info.cpp` (46),
`eq_player_misc.cpp` (31), `eq_player_social.cpp` (29), `eq_player_state.cpp`
(21), `eq_player_buffs.cpp` (15), `eq_profile_appearance.cpp` (0 —
free functions; move the file with the family, no class change).

**The cut this unit must make.** `eq_player_info.cpp` is dominated by thin
STATE ACCESSORS that read `m_game_state` (`GetSTR`…`GetWIS`, `GetLevel`,
`GetClass`, `GetRace`, `GetGender`, `GetDeity`, `GetCurrentHP/Mana/Endurance`,
`GetMax*`, `GetPlatinum/Gold/Silver/Copper`, `GetBank*`, `GetWeight`,
`GetMaxWeight`, `GetPosition`, `SetPosition`, `SetHeading`, `GetHeading`,
`IsMoving`, `SetMoving`, `IsAFK`, `IsAnonymous`, `IsRoleplay`, `IsCamping`,
`IsInGroup`, `IsGroupLeader`, `GetGroupLeaderName`, `GetGroupMemberCount`,
`HasPendingGroupInvite`, `GetPendingInviterName`, `GetPracticePoints`,
`GetClassName`, `GetRaceName`, `Test_GetGameStatePlayerPosition`). They are
the **player-state facade** the whole tree reads (`GetPosition` alone has
callers in combat, skills, spells, the action handlers). **They STAY in
`EverQuest`**, in `eq_player_info.cpp`, exactly as the three `AddChat*`
primitives stayed in K03. K13 decides whether the facade becomes a
`ClientContext` service. Record how many bodies stayed.

Everything else moves: every `Send*`, every `ZoneProcess*`, `PerformEmote`,
`CancelCamp`/`StartCampTimer`/`UpdateCampTimer`, `PublishPlayerBuffs`,
`PublishPlayerBuffUpdated`, `RequestCancelBuff`, `RemoveEntityFromGameState`,
`SyncEntityToGameState`, `SetCombatStopDistance`, `SetMoveSpeed`, `SetAFK`,
`SetAnonymous`, `SetRoleplay`. If a mover is a one-line accessor with a
tree-wide reader set, argue it in the Review and leave it with the facade.

Callers by compiler (K05 rule). Known large clusters: `eq_action_handler*.cpp`
(~40 sites), `eq_zone_dispatch.cpp` (~45 handlers), `eq_bridge_intents.cpp`,
`eq_bridge_ui_audio.cpp`, `combat*.cpp`, `spell_manager.cpp`,
`skill_manager.cpp`, `action/command_processor_*.cpp`,
`action/action_dispatcher_guild_misc.cpp`; and the already-extracted
components (`group_component.cpp` reads `GetGroupLeaderName` etc. via
`owner_.` — those stay with the facade, so no change there).

Members (single-family → move): `m_dragged_corpse_name`, `m_duel_initiator_id`,
`m_duel_target_id`, `m_has_pending_duel`, `m_is_dueling`, `m_is_lfg`,
`m_is_on_boat`, `m_pending_rezz`, `m_test_last_face_change` (+ its `Test_`
accessor), `m_test_last_inspect_answer` (+ accessor), `m_transport`,
`m_inspect_message` (1 reader in `eq_bridge_ui_audio.cpp` → `Player()`
getter). Shared → `owner_.`: `m_has_pending_rezz` and `m_is_dragging_corpse`
(`action_dispatcher_guild_misc.cpp` reads them — retarget those two reads
to a `Player()` getter and MOVE the members; record it), `m_pending_logout_disconnect`
(movement), `m_replay_suppressed_sends` (login/zone), the zone flags, and
the tribute/AA counters (zone-entry writes; K09). Confirm each exists first
(K04 h). Grep `eq.h` for inline accessors on every moving member (K04 f).

Predicted `eq.h` delta: −(≈95 declarations) −12 members −2 `Test_` accessors
−banners +4 ≈ **−100 or better**.

## Territory

`include/client/player/` + `src/client/player/` (new; the six TUs move
except `eq_player_info.cpp`, which stays and shrinks), `eq.h`, `eq.cpp`
(ctor), root `CMakeLists.txt`, `tests/test_player_component.cpp` + cmake,
allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the caller files
the compiler names, including inside `src/client/*/` components (write
`owner_.Player().Foo()` there). K10 runs alongside and edits the login/world
lines of the same shared files; K09 does not run until both merge.

## Verification / Review

Executed in `wt/slot1` on `task/K08-player-component`, branched from `fable` at
`e6d42422`. Ran alongside K10 (slot2). §Mechanism REVISED (through the
K03-K07 correction blocks) followed as written; **no STOP condition was hit**.

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `e6d42422`) | **1,732** |
| predicted before any edit | **1,542** (-190) |
| measured after | **1,543** (**-189**) |

`git diff --numstat` on the single file: **198 deletions, 9 insertions = net
-189**. The prediction was computed before touching anything, as an explicit
list of 23 line RANGES in the header (196 lines) plus +6 scaffolding. The
one-line miss is that the `Player()` accessor got a three-line comment instead
of the two the arithmetic assumed -- comment, not code. The plan file's
pre-dispatch estimate was "-100 or better"; the measured shrink is nearly
double that, because the plan counted declarations and members and not the
~60 banner/comment lines that travel with them.

Where the 196 deleted lines came from:

| block | lines | contents |
|---|---|---|
| T167 + T170 test seams | 21 | two comment blocks, `struct TestSentFaceChange`, `struct TestSentInspectAnswer`, and their two inline `Test_Get*` accessors -- K04 correction (f) in its purest form: **none of these is an `EverQuest::` body, so no method count sees them**, and between them they carried 8 external call sites |
| `SetCombatStopDistance`, `SetMoveSpeed` | 2 | the two one-line mutators that moved out of the "Public movement interface" banner (the banner itself stays -- `SetHeading`/`SetPosition`/`SetMoving`/`GetPosition` are facade) |
| `SendTradeSkillCombine`, "Skill-specific packet sends" + `SendApplyPoison` | 5 | |
| `PerformEmote`..`UpdateCampTimer` | 7 | `IsCamping()` stays (facade) |
| `SendCamp`..`SendInspectRequest` incl. `HasPendingRezz` and the two-line `SendWhoAllRequest` | 7 | |
| T170 comment + `SendInspectAnswer` | 8 | |
| the Phase 3/Phase 4 public block: corpse, consent, assist, boat, split, LFG, shielding, misc, duel | 35 | 21 declarations + 10 banners + the dead "Phase 3: Travel System" boat banner + the 5-line T102 note |
| "Phase 4: Skills" + `SendBindWound`/`SendTrackTarget` | 3 | |
| "Phase 4: Cosmetic" + `SendSurname` + two-line `SendFaceChange` | 4 | |
| "Phase 4: Misc" + 5 senders + the 7-line T150 note + `SendFriendsWho` | 14 | |
| "Phase 7.4: Entity sync helpers" + `SyncEntityToGameState`/`RemoveEntityFromGameState` | 3 | |
| the private `ZoneProcess*` blocks (7 runs) | 49 | 46 handler declarations + the "Phase 3 packet handlers" / "Phase 4 packet handlers" / "Pet packet handlers" banners |
| the private member block: camp timer, rezz, corpse drag, LFG, duel | 18 | 9 members, `CAMP_TIMER_SECONDS`, 6 banners, 2 blanks |
| the two test-seam members + their notes | 6 | |
| the T277 buff block | 14 | `RequestCancelBuff`, `SendBuffCancelRequest`, `PublishPlayerBuffUpdated`, `PublishPlayerBuffs` and their two comment blocks |
| **added** | | `#include "client/player/player_component.h"` (+1), `Player()` + 3 comment lines (+4), `friend class eqt::player::PlayerComponent` (+1), `std::unique_ptr<...> m_player` (+1), and the one-word repointing of the `eq_profile_appearance.h` citation (+2/-2 in the numstat, net 0) |

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**1,732 -> 1,543**. ⚠ **Shared file:** K10 lowers the same line. **The measured
value for K08 alone is 1543**; the orchestrator resolves at merge.
`check_file_sizes.sh --all`: **815 ok, 0 warn, 0 FAIL**.

### THE CUT: 139 bodies, 93 moved, 46 stayed

`grep -nE '^[A-Za-z_].*EverQuest::'` over the six TUs at this tip returns
**139**, not the batch table's 142: `eq_player_info.cpp` 46,
`eq_player_misc.cpp` 31, `eq_player_social.cpp` **26** (the plan says 29),
`eq_player_state.cpp` 21, `eq_player_buffs.cpp` 15,
`eq_profile_appearance.cpp` 0. The `eq_profile_appearance.cpp` file is free
functions only and moved with the family unchanged
(`src/client/player/profile_appearance.cpp`, header
`include/client/player/profile_appearance.h`).

**The 46 facade bodies that STAYED on `EverQuest`**, now all in one TU,
`src/client/eq_player_info.cpp` (285 lines):

| cluster | bodies |
|---|---|
| stats (was `eq_player_info.cpp`) | `GetLevel`, `GetClass`, `GetRace`, `GetGender`, `GetSTR`, `GetSTA`, `GetDEX`, `GetAGI`, `GetINT`, `GetWIS`, `GetCHA`, `GetCurrentHP`, `GetMaxHP`, `GetCurrentMana`, `GetMaxMana`, `GetCurrentEndurance`, `GetMaxEndurance`, `GetDeity` (18) |
| currency + weight | `GetPlatinum`, `GetGold`, `GetSilver`, `GetCopper`, `GetBankPlatinum`, `GetBankGold`, `GetBankSilver`, `GetBankCopper`, `GetPracticePoints`, `GetWeight`, `GetMaxWeight` (11) |
| group | `IsInGroup`, `IsGroupLeader`, `GetGroupMemberCount`, `GetGroupLeaderName`, `HasPendingGroupInvite`, `GetPendingInviterName` (6) |
| position/heading/moving (was `eq_player_state.cpp`) | `Test_GetGameStatePlayerPosition`, `GetPosition`, `GetHeading`, `IsMoving`, `SetHeading`, `SetPosition`, `SetMoving` (7) |
| behavioural flags (was `eq_player_state.cpp`) | `IsAFK`, `IsAnonymous`, `IsRoleplay`, `IsCamping` (4) |

That is the K03 split-TU shape applied to a much bigger family: `eq_chat.cpp`
kept 3 of 13 bodies, `eq_player_info.cpp` keeps 46 of 139. Every one of them
is a one-to-three-line read (or, for the four setters, write) of
`m_game_state`; not one of them touches a member that moved, so the cut costs
zero `owner_.` traffic in either direction. **The facade is recorded here as a
`ClientContext`-service candidate for K13**, the same standing as K03's
`AddChat*` trio.

Note the asymmetry the cut deliberately preserves: `IsAFK()`/`IsAnonymous()`/
`IsRoleplay()`/`IsCamping()` stayed (they are reads of the one store, called
tree-wide) while `SetAFK()`/`SetAnonymous()`/`SetRoleplay()`/`StartCampTimer()`
moved (each is a wire send plus a spawn-appearance broadcast, i.e. player
BEHAVIOUR). `SetMoveSpeed`/`SetCombatStopDistance` moved for the same reason
the plan named them: they are not part of the read surface anything else uses.

### Compiler-derived caller list

All 93 declarations plus the 8 inline accessors were deleted from `eq.h` in one
pass; the pre-computed retarget was applied in the same pass; the build then
went **clean on the second attempt** (the first named exactly the sites below
plus 73 `s_debug_level` scope errors). A clean build is the compiler's proof
that no caller was missed.

| file | sites | receiver form | change |
|---|---|---|---|
| `src/client/eq_zone_dispatch.cpp` | **47** | bare, inside `EverQuest::DispatchZonePacket` | -> `m_player->` (46 `ZoneProcess*` + one `SyncEntityToGameState` at the spawn-sync site) |
| `src/client/eq_action_handler_extended.cpp` | **23** | `m_eq.` | -> `m_eq.Player().` |
| `src/client/eq_action_handler.cpp` | **11** | `m_eq.` | -> `m_eq.Player().` (duel trio + `GetPendingDuelInitiatorId`/`HasPendingDuel`, assist, shielding, AFK/anon) |
| `src/client/eq_action_handler_spells_trade.cpp` | 2 | `m_eq.` | `RequestCancelBuff`, `SendFaceChange` |
| `src/client/eq_bridge_intents.cpp` | 2 | bare | `SendBuffCancelRequest`, `SendTradeSkillCombine` -> `m_player->` |
| `src/client/eq_zone_entry.cpp` | 2 | bare | `SyncEntityToGameState`, `PublishPlayerBuffs` -> `m_player->` |
| `src/client/skill/skill_manager.cpp` | 2 | `m_eq->` | `SendApplyPoison`, `SendBindWound` -> `m_eq->Player().` |
| `src/client/combat.cpp` | 1 | `m_eq->` | `SendConsiderCorpse` |
| `src/client/combat_rounds.cpp` | 1 | `m_eq->` | `SetCombatStopDistance` |
| `src/client/entities/entities_component.cpp` | 1 | `owner_.` | `SyncEntityToGameState` -> `owner_.Player().` |
| `src/client/entities/entities_component_appearance.cpp` | 1 | `owner_.` | `RemoveEntityFromGameState` -> `owner_.Player().` |
| `src/client/entities/entities_component_updates.cpp` | 1 | `owner_.` | `SyncEntityToGameState` -> `owner_.Player().` |
| `src/client/movement/movement_state.cpp` | 1 | `owner_.` | `CancelCamp` -> `owner_.Player().` |
| `src/client/movement/movement_update.cpp` | 1 | `owner_.` | `UpdateCampTimer` -> `owner_.Player().` |
| `tests/test_buff_click_off.cpp` | 8 | `eq.` | -> `eq.Player().`. **Assertions untouched** |
| `tests/test_profile_appearance.cpp` | 6 | `eq.` | `Test_GetLastFaceChange` -> `eq.Player().`. **Assertions untouched** |
| `tests/test_inspect_answer.cpp` | 3 | `eq.` | `SendInspectAnswer`, `Test_GetLastInspectAnswer`. **Assertions untouched** |

**Total: 113 real call sites in 17 files** (12 production, 5 in already-extracted
components or their siblings, 3 test). Per §Mechanism K06/K07 correction (r),
the five sites inside `entities/` and `movement/` were written
`owner_.Player().Foo()` because those families are already on `fable`.

**Leads that resolved to nothing.** The plan named `eq_bridge_ui_audio.cpp`,
`action/command_processor_*.cpp`, `action/action_dispatcher_guild_misc.cpp` and
`spell_manager.cpp` as caller clusters. **None of them calls a moved method.**
`include/client/action/command_processor.h:371`'s `SendConsentDeny()`/
`SendRezzAnswer()` mention is a comment; `action_dispatcher_guild_misc.cpp:119`
and `action_handler.h:352` are comments about `m_is_dragging_corpse`/
`m_has_pending_rezz` explicitly saying they are NOT duplicated there;
`spell_manager.cpp`'s two `eq_player_buffs.cpp` hits are prose.
`tests/test_wire_layout_t145.cpp`'s four hits (`SendCamp`, `SendLogout`,
`SendWhoAllRequest`, `SendInspectRequest`) are all TRAILING comments -- K04
correction (g) again, and the reason the comment filter has to cover trailing
comments and not just full-line ones.

**Zero-caller candidates: none. Nothing was deleted.** Every one of the 93 has
at least one caller counting in-family ones (K03 correction (a)).

### Members

| member | plan's decision | measured | outcome |
|---|---|---|---|
| `m_dragged_corpse_name` (5), `m_duel_initiator_id` (2), `m_duel_target_id` (2), `m_has_pending_duel` (4), `m_is_dueling` (1), `m_is_lfg` (2), `m_pending_rezz` (6), `m_test_last_face_change` (9), `m_test_last_inspect_answer` (3) | move | confirmed single-family | **MOVED** (9) |
| `m_has_pending_rezz` (4), `m_is_dragging_corpse` (3) | "shared -- `action_dispatcher_guild_misc.cpp` reads them; retarget those two reads to a `Player()` getter and MOVE" | **the plan is wrong about the reads.** Both hits (`action_dispatcher_guild_misc.cpp:119`, `action_handler.h:352`) are COMMENTS -- and the comment's own text says the state is not duplicated there. Zero code references outside the family | **MOVED**, with **no retarget at all**. Same class as K05's `m_trainer_skill_caps` |
| `CAMP_TIMER_SECONDS` | not in the plan | `static constexpr int` on `EverQuest`; its only two references tree-wide are in `eq_player_social.cpp` | **MOVED** into the component. Not a `m_*` member, so no member grep would have found it |
| `m_inspect_message` | "1 reader in `eq_bridge_ui_audio.cpp` -> `Player()` getter" | **4 references in `eq_bridge_ui_audio.cpp` -- three of them WRITES** (`SetInspectMessage`, which is defined there, and the config save/load). One read in this family | **⚠ STAYS**, `owner_.m_inspect_message`, with its inline `GetInspectMessage()` and the T171 note. §Mechanism 3's shared rule: bridge outweighs player 4:1 and `SetInspectMessage()` is a bridge-family body. K11 moves it |
| `m_is_on_boat` | move | **the member does not exist** -- zero hits tree-wide. The boat sender was already gone; only its dead `// Phase 3: Travel System` banner survived, and that banner is part of this unit's deletion | stale lead (K04 correction h) |
| `m_transport` | move | **not `EverQuest`'s.** Every hit is `EQ::Net::DaybreakConnection::m_transport` (`include/common/net/daybreak_connection.h:354`) -- a different class, same name, K06 correction 4 exactly | stale lead |
| `m_pending_logout_disconnect` (1), `m_replay_suppressed_sends` (1), `m_zone_to_zone_in_progress`, `m_zone_out_handshake_pending`, `m_zone_out_acknowledged`, `m_loading_phase`, `m_exp_zonein_sent`, `m_send_exp_zonein_received`, `m_aa_table_count`, `m_tribute_count`, `m_guild_tribute_count`, `m_string_db` | shared -> `owner_.` | confirmed shared (zone/login/handshake) | **stay**, `owner_.m_*`; K09/K10/K12 own them |
| core: `m_game_state` (134), `m_zone_connection` (52), `m_entities` (24), `m_spell_manager` (14), `m_bridge` (12), `m_buff_manager` (11), `m_audio_manager` (9), `m_trade_manager` (7), `m_combat_manager` (6), `m_character` (4), `m_inventory_manager` (2), `m_active_tradeskill_object_id` (1) | `ctx_.` | 12 of the 15-member core used; `skill_manager`, `zone_connection_manager` and `command_registry` unused here | `ctx_.` |
| `m_merchant` (3), `m_entities_component` (4), `m_movement` (1) | not in the plan | other components' pointers, reached through `owner_` | rewritten to `owner_.Merchant().` / `owner_.Entities().` / `owner_.Movement().` per K06/K07 correction (r) |

**Eleven members moved** (the plan predicted twelve; `m_inspect_message` stayed,
`m_is_on_boat` and `m_transport` do not exist, and `CAMP_TIMER_SECONDS` was
found that the plan did not list).

**Inline `eq.h` accessors on a moving member (K04 correction f) -- eight, and
they carried 17 of the 113 external call sites**: `HasPendingRezz`,
`IsDraggingCorpse`, `IsLFG`, `IsInDuel`, `HasPendingDuel`,
`GetPendingDuelInitiatorId`, `Test_GetLastFaceChange`,
`Test_GetLastInspectAnswer`. Two of them (`Test_Get*`) came with a nested
`struct` each, which had to move too; `tests/test_profile_appearance.cpp` and
`tests/test_inspect_answer.cpp` now name
`eqt::player::PlayerComponent::TestSentFaceChange` / `TestSentInspectAnswer`
only through the accessor's return type, so no type name needed rewriting.

### Dependencies no lead table named

Found by cross-referencing every `identifier(` in the moving files against
`eq.h`'s declaration set before building (K06 correction 5, mechanised):
**18 stays-behind methods, 113 call sites**, all rewritten `owner_.Foo()` --
`AddChatSystemMessage` (55), `MySpawnId` (19), `AddChatCombatMessage` (6),
`QueuePacket` (6), `CheckZoneRequestPhaseComplete` (4), `GetEntityID` (3),
`IsFullyZonedIn` (3), `CurrentZoneId` (2), and one each of `PosX`, `PosY`,
`PosZ`, `IsVendorWindowOpen`, `RequestQuit`, `RequestZoneChange`, `SetHeading`,
`SetPosition`, `UpdateInventoryStats`, `ZoneSendSendExpZonein`. Note
`owner_.SetHeading()` / `owner_.SetPosition()`: **a moved body calling back into
the facade it left behind** is the cut working as intended, not a smell.

`EverQuest`-scoped names needing the third substitution: `s_debug_level`
(**23 sites**, the most of any K unit) and `DumpPacket` (1).

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides, longest-name-first (K04 correction i):
`eqt::player::PlayerComponent::` -> `EverQuest::`;
`EverQuest::s_debug_level` -> `s_debug_level` and `EverQuest::DumpPacket` ->
`DumpPacket` (K06 correction 6 -- a qualification-only change needs its own
un-mapping or it reads as a residual on every site);
`owner_.Merchant().` -> `m_merchant->`, `owner_.Entities().` ->
`m_entities_component->`, `owner_.Movement().` -> `m_movement->`;
`ctx_.<core>` -> `m_<core>` for the twelve core members used;
`owner_.m_` -> `m_`; `owner_.` -> ``; trailing whitespace. Comment, blank and
`#include` lines filtered; both sides sorted.

Old side: the six `eq_player*.cpp` / `eq_profile_appearance.cpp` at
`e6d42422` -- **1,834 code lines**. New side: `src/client/eq_player_info.cpp`
plus the six files under `src/client/player/` -- **1,826 code lines**.

**Residual: 8 lines, and all eight are preprocessor lines, not code** --
four `#ifdef EQT_HAS_GRAPHICS` / `#endif` pairs, the guards around the
**vestigial `client/graphics/irrlicht_renderer.h` includes** the batch doc's
S2-leftovers paragraph assigns to whichever K unit owns the file. Four of the
eight tree-wide sat in this family's TUs; they are removed here and the build
is green without them. **Every other line is byte-identical between the two
sorted multisets** (`diff` otherwise empty). The move is verbatim.

### Text readers of the moved TUs (K03 correction c)

`grep -rn 'eq_player\|eq_profile_appearance' tests/ scripts/ docs/` BEFORE
moving anything: 12 hits in `tests/`, **every one a prose comment**
(`test_wire_layout_t145`, `test_combat_manager`, `test_eq_action_handler_cast`,
`test_zone_connection_flow`, `test_zone_position_convention`,
`test_movement_component`). **No test reads any of the six TUs as data**, the
way `test_chat_channel_names.cpp` read `eq_chat.cpp`. The one real dependency
was `tests/test_profile_appearance.cpp`'s
`#include "client/eq_profile_appearance.h"`, repointed with the header.
`test_duplicate_state_stores.cpp` and `test_reachability_sweep.cpp` both scan
`eq.h` (and, since K05, the component headers) and **both pass unchanged** --
no file-list edit was needed, so no STOP.

`DocCitations` caught one stale citation the plan did not name:
**`refactor/BOARD.md:182` (B023, `EverQuest::SendReport`'s wire-shape defect)**
cited `src/client/eq_player_misc.cpp`. Repointed to
`src/client/player/player_misc.cpp` and the symbol renamed to
`eqt::player::PlayerComponent::SendReport`; `DocCitations` then passes
(234 citations, 233 ok, 0 fail, 1 skip). This is the third K unit in a row
where the doc guard, not the plan, found the stale path.

### Files

| file | lines |
|---|---|
| `include/client/eq.h` | 1,732 -> **1,543** (-189) |
| `src/client/eq_player_info.cpp` (the facade; STAYS) | 369 -> **285** |
| `src/client/eq_player_social.cpp` -> `src/client/player/player_component.cpp` | 704 -> 701 |
| `src/client/eq_player_misc.cpp` -> `src/client/player/player_misc.cpp` | 750 -> 747 |
| `src/client/eq_player_buffs.cpp` -> `src/client/player/player_buffs.cpp` | 616 -> 613 |
| `src/client/eq_player_state.cpp` -> `src/client/player/player_state.cpp` | 606 -> 507 |
| `src/client/player/player_info.cpp` | **186 (new)** -- the mover half of `eq_player_info.cpp` |
| `src/client/eq_profile_appearance.cpp` -> `src/client/player/profile_appearance.cpp` | 20 (pure rename) |
| `include/client/eq_profile_appearance.h` -> `include/client/player/profile_appearance.h` | 69 (pure rename) |
| `include/client/player/player_component.h` | **263 (new)** |
| `tests/test_player_component.cpp` | **111 (new)** |

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Four attempts, none timed out (no exit 143, so no truncation scan was needed): (1) the deliberate declaration-deletion pass, exit 2, 73 errors -- 23 `s_debug_level` scope errors and the 50 call sites the pre-computed retarget had not yet been applied to; (2) exit 0 after the retarget; (3) exit 0 after removing the four vestigial `irrlicht_renderer.h` includes; (4) exit 0 with `test_player_component` added |
| `ctest --test-dir build -j6 --output-on-failure` | **3,555 / 3,556 passed**, 17 skipped, 149 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, all 5 `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (19.48 s, inside this same run) all **PASS**. One intermediate run had a second failure, `DocCitations` on the stale `BOARD.md` citation above; fixed in-unit and re-run green |
| `tests/test_player_component.cpp` (new) | **7/7 pass**: `PlayerReturnsTheSameComponentEveryTime`, `MovedFlagsCarryTheirInlineAccessors`, `FaceChangeSeamMovedWithItsStruct`, `InspectAnswerSeamMovedWithItsStruct`, `TheFacadeStayedOnEverQuest`, `MovedMutatorsStillWriteTheOneStore`, `CampTimerRoundTripsThroughTheFacadeFlag` |
| retargeted suites | `test_buff_click_off`, `test_inspect_answer`, `test_profile_appearance` -- all pass, assertions untouched |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --name-only $(git merge-base fable HEAD)..HEAD -- src/client/graphics include/client/graphics` is **empty**, and so is `git status --short` over the same two paths -- this unit changed no graphics file. Goldens untouched. `visual_gate` nonetheless ran and passed inside the full `ctest` run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **815 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,543 |

### Corrections to §Mechanism

1. **A family can be split by ROLE, not just by TU, and §Mechanism should say
   so.** K03 established "some bodies stay behind"; K08 is the first unit where
   the stay-behind set is a third of the family (46 of 139) and is defined by a
   PROPERTY -- "is a thin read of the one store that other families use" --
   rather than by a filename. The test for it is mechanical and worth stating:
   a body stays with `EverQuest` if it touches no member that moves AND its
   callers are spread across families. Every one of the 46 passes both halves;
   not one of the 93 movers does.

2. **The comment filter must cover TRAILING comments, not only full-line ones.**
   K04 correction (g) and K07 correction 4 both say "comment-filtered"; every
   implementation so far (including the first pass of this unit's) tests
   `line.lstrip().startswith('//')`, which passes `EQ::Net::DynamicPacket
   packet;  // exactly what SendCamp() now builds` straight through.
   `test_wire_layout_t145.cpp` produced four such false caller leads. State it
   as: strip from `//` to end of line before matching, and mask string
   literals in the same pass (K07 correction 4's `LOG_WARN` case).

3. **`EverQuest`'s non-`m_` statics move with the family too, and no member
   grep finds them.** `CAMP_TIMER_SECONDS` (`static constexpr int`) is used by
   exactly two bodies, both in this family, and lives in the private member
   block between two `m_*` members. §Mechanism 3 says "single-family `m_*`
   members move"; it needs to read "single-family private members and
   file-scope-equivalent statics".

4. **The whole retarget can be computed BEFORE the first build, and should
   be.** K05 correction 3 allows delete+retarget-the-lead+build. K08 went
   further: the caller set, the `owner_.Foo()` set and the `ctx_.` set were all
   derived mechanically (parse `eq.h` for declarations, parse the component
   headers for the already-extracted families, then scan the moving TUs for
   bare `identifier(`) and applied in ONE pass. The result was a 2,700-line
   move that reached a clean build on the **second** compile, with the first
   compile's error list containing nothing the analysis had not already
   predicted except the `s_debug_level` qualification. This is the productised
   form K06 correction 5 asked for; it is worth keeping for K09 (zone), which
   is the next family with this shape.

5. **No STOP condition was hit.** No moved body needed an edit beyond the four
   substitutions §Mechanism 4 allows; no test asserted on a moved member by
   name; the predicted delta was -190 before dispatch and the measured delta is
   -189.

### Left for the board

- **`m_inspect_message` is the bridge's, not the player's.** Three of its four
  writes and its setter (`SetInspectMessage`) live in `eq_bridge_ui_audio.cpp`;
  the player family only reads it when answering an inspect. K11 should move it
  with the bridge, not leave it on `EverQuest`.
- **`m_is_on_boat` and the boat sender are gone but the `// Phase 3: Travel
  System` banner and its two-line T145 note survived them in `eq.h`.** Deleted
  here. Nothing else in the tree references a boat: `/boat` is unimplemented,
  not broken.
- **`SendReport` (B023) moved with the family** and its board entry now points
  at `src/client/player/player_misc.cpp`. The defect is unchanged -- the wire
  shape still does not match eqemu's `OP_Report`, so `AddReport()` is still
  never reached. Class C, still Will's call.
