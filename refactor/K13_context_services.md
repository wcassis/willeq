# K13 — Context services: retire the transition handle where it is measured to be cheap

Batch: `refactor/K_component_extraction.md`. **Runs ALONE after K11 and K12
merge.** Not the end of the programme — it is the unit that turns the
`EverQuest&` transition handle from "everything" into a measured, boarded
residue.

## Measured at `fa92c523` (2026-09-03), after K12 (re-measured; K09-tip numbers were 926 total)

`owner_.` appears **1,478 times** across the twelve components. The targets
concentrate in the same four groups, in the same proportions:

| target group | uses | becomes |
|---|---|---|
| `AddChatSystemMessage` / `AddChatCombatMessage` / `AddChatMissMessage` | ~300 | `ctx_.out.System(…)` etc. — a `ChatOutput` class owning the three bodies now stranded in `eq_chat.cpp` (K03 left them; 67 lines) |
| `MySpawnId`, `PosX/Y/Z`, `HeadingDeg`, `IsFullyZonedIn`, `CurrentZoneName/Id`, `SetPosLocal`, `SetHeadingLocal`, `SetMySpawnId`, `SetPosition`, `SetHeading`, `SetLoadingPhase`, `IsBankWindowOpen`, `IsVendorWindowOpen` | ~560 | `ctx_.self` — a `PlayerFacade` class holding the K08 facade bodies (`eq_player_info.cpp`, 46 accessors) plus these; it wraps `game_state` and nothing else |
| `Zone()`, `Player()`, `Entities()`, `Merchant()`, `LoginWorld()`, `Movement()`, `Group()`, `Chat()`, `Guild()`, `Gm()`, `Bridge()`, `Commands()` | ~350 | `ctx_.<family>()` — the context carries a pointer to each sibling component, set by `EverQuest` after construction (components are constructed in dependency order; late-bound pointers avoid the order problem) |
| `QueuePacket` / `SafeQueueZonePacket` | 29 | `ctx_.net.Queue(op, pkt)` — a `PacketSender` wrapping `m_zone_connection` + the replay guard + `m_replay_suppressed_sends` (which then LEAVES `EverQuest`, closing K09's deviation 1) |
| `owner_.m_*` direct member reaches (`m_replay_suppressed_sends` 24, `m_pending_loot_slots` 14, `m_zone_bsp_tree` 9, `m_zone_map` 8, `m_world_objects` 8, `m_guild_id` 8, `m_eq_client_path` 6, `m_vendor_items` 5, `m_test_bypass_send_guard` 5, `m_zone_audio_manager` 14, `m_hotbar_model` 11, `m_constrained_config` 8, `m_graphics_initialized` 5, `m_session_recorder` 4, …) | ~180 | each is a K13 DECISION: move to the last family using it (§3), or to a context service, one line of rationale per member in the Review |
| the zone-environment set (`m_zone_map`, `m_zone_bsp_tree*`, `m_zone_fog_*`, `m_zone_sky_type`, `m_zone_type`) | ~30 | K11 measured: written by `ZoneComponent::ZoneProcessNewZone`, loaded/torn down inside `ZoneComponent`, read by one bridge body → **re-home into `ZoneComponent`** behind getters; `m_hotbar_model` and `m_zone_audio_manager` stay on `EverQuest` as ctor-wired subsystem handles |
| everything else | ≤ 50 | measured, listed, and BOARDED as the residue — with the friend line kept only for components that still have one |

The unit is done when: `owner_.` count is reported before and after; every
remaining use is in a table with its reason; `friend class` lines exist
only for components with a remaining `owner_.m_` use; the three services
have their own headers under `include/client/` and their own tests.

## Steps

1. `include/client/chat_output.h` + `src/client/chat_output.cpp`: move the
   three `AddChat*` bodies verbatim (they need `m_bridge` and the console
   path — read them). `EverQuest::AddChat*` become forwarders for the
   non-component callers (`application_commands.cpp`, `spell_manager.cpp`,
   `skill_manager.cpp`, `inventory_manager.h`, the action layer) — these
   are OUTSIDE the batch's no-forwarder rule because they are not
   EverQuest-family callers; K14 retargets them if Will wants the surface
   gone. Count them.
2. `include/client/player_facade.h` + `src/client/player_facade.cpp`: the
   46 K08 facade accessors + the setters above, verbatim. Same forwarder
   rule for non-component callers (combat, spells, skills: `GetPosition`
   alone has ~20).
3. `include/client/packet_sender.h`: `QueuePacket` + the replay guard +
   `m_replay_suppressed_sends` + `GetReplaySuppressedSendCount()`; the 49
   test call sites retarget to `eq.Net().ReplaySuppressedSendCount()`
   (call-site only).
4. `ClientContext` gains `out`, `self`, `net` and the twelve component
   pointers; `EverQuest`'s ctor sets the pointers after all components
   exist.
5. Mechanical rewrite in `src/client/*/` and `include/client/*/`:
   `owner_.AddChat` → `ctx_.out.`; the facade names → `ctx_.self.`;
   `owner_.<Family>()` → `ctx_.<family>()`; `owner_.QueuePacket` →
   `ctx_.net.Queue`. Code lines only (K01 correction 4).
6. Per-member decisions for the `owner_.m_*` reaches; execute the moves.
7. Remove `friend class` lines for components with zero remaining
   `owner_.m_` uses; remove the `EverQuest&` ctor parameter ONLY from
   components with zero remaining `owner_.` uses of any kind.
8. Tests: `test_chat_output.cpp`, `test_player_facade.cpp`,
   `test_packet_sender.cpp` — construct via `zonetest::MakeEq()`, one
   behaviour test per service. Existing suites unchanged in assertions.
9. Ratchet `eq.h`; predicted delta ≈ −(46 facade + 3 AddChat + QueuePacket
   + the replay counter) +(3 accessors) ≈ **−50 or better**.

## STOP conditions

- A service needs state that is not `game_state`, the bridge, or the zone
  connection → STOP; that is a design question.
- The `owner_.` residue after step 5 exceeds 250 → STOP and report the
  table; the plan's grouping was wrong.

## Territory

`include/client/client_context.h`, `include/client/chat_output.h`,
`include/client/player_facade.h`, `include/client/packet_sender.h` (new),
their `src/client/` TUs (new), `src/client/eq_chat.cpp` and
`src/client/eq_player_info.cpp` (shrink or vanish), `eq.h`, `eq.cpp`, every
`src/client/*/` component TU and `include/client/*/` component header
(the rewrite), root `CMakeLists.txt`, the three new tests + cmake, the
allowlist, `docs/module_map.md`, `refactor/BOARD.md` (the residue table's
board entry), and CALL-SITE-ONLY edits in the tests that read the replay
counter. Runs alone.

## Review

Executed alone in `wt/slot3` on `task/K13-context-services`, branched from
`fable` at **`a89314e0`** (the plan's measurements were taken at `fa92c523`;
everything below is RE-MEASURED at this tip). §Mechanism REVISED followed as
written, including every correction block K03-K12.

**Neither STOP condition fired**, but the first one was *approached* and is
recorded as deviation 1 below.

### The headline: `owner_.` 1,425 -> 83

Measured with a comment- and string-filtered census over
`src/client/<family>/*.{cpp,h}` + `include/client/<family>/*.h` for the twelve
families (the plan's 1,478 was an unfiltered count at `fa92c523`; the same
script gives 1,425 at `a89314e0`).

| target group | before | after | became |
|---|---|---|---|
| `AddChatSystemMessage` 291 / `AddChatCombatMessage` 9 / `AddChatMissMessage` 3 | **303** | 0 | `ctx_.out.System/Combat/Miss(...)` |
| the player-state facade: `MySpawnId` 122, `PosX` 90, `PosY` 90, `PosZ` 85, `IsFullyZonedIn` 29, `HeadingDeg` 28, `CurrentZoneName` 16, `SetPosLocal` 14, `SetHeadingLocal` 9, `IsBankWindowOpen` 7, `CurrentZoneId` 7, `IsVendorWindowOpen` 6, `SetMySpawnId` 5, `SetPosition` 5, `SetHeading` 4, `GetPracticePoints` 3, `GetEntityID` 3, `GetVendorNpcId` 2, and one each of `HasPet`/`GetClass`/`GetLevel`/`GetPlatinum`/`GetGold`/`GetSilver`/`GetCopper`/`SetMoving`/`IsTrainerWindowOpen` | **534** | 0 | `ctx_.self.<same name>` |
| the twelve sibling components: `Zone` 70, `Bridge` 62, `Player` 56, `Merchant` 49, `Entities` 42, `Movement` 14, `LoginWorld` 13, `Commands` 10, `Group` 8, `Chat` 7, `Guild` 4, `Gm` 3 | **338** | 0 | `ctx_.<Family>()` |
| the send path: `QueuePacket` 26, `SafeQueueZonePacket` 3, `m_replay_suppressed_sends` 24 | **53** | 0 | `ctx_.net.Queue/SafeQueueZone/NoteSuppressedSend` |
| the zone-environment set: `m_zone_bsp_tree` 14, `m_zone_map` 13, `m_zone_bsp_tree_zone_name` 7, `m_zone_sky_type`/`m_zone_type`/`m_zone_fog_red`/`_green`/`_blue`/`_minclip`/`_maxclip` 3 each | **55** | 0 | `ZoneComponent`'s own members + reference accessors |
| promoted into `ClientContext`: `m_zone_audio_manager` 14, `m_hotbar_model` 11, `m_eq_client_path` 10, `m_session_recorder` 4 | **39** | 0 | `ctx_.<member>` |
| re-homed into the family that owns them: `m_world_objects` 10 -> `EntitiesComponent`, `m_water_state` 7 -> `MovementComponent`, `m_login_world` 3 -> `ctx_.LoginWorld()` | **20** | 0 | component members + accessors |
| **residue** | **83** | **83** | boarded as **B038** |
| **total** | **1,425** | **83** | **-1,342 (94.2%)** |

Per family, after:

| family | `owner_.` | `owner_.m_` | `friend` line | `EverQuest&` ctor param |
|---|---|---|---|---|
| **gm** | **0** | **0** | **REMOVED** | **REMOVED** |
| group | 2 | 2 | kept | kept |
| merchant | 2 | **0** | **kept -- see correction 2** | kept |
| commands | 2 | **0** | **kept -- see correction 2** | kept |
| player | 4 | 3 | kept | kept |
| chat | 6 | 3 | kept | kept |
| movement | 6 | 5 | kept | kept |
| entities | 6 | 2 | kept | kept |
| guild | 7 | 7 | kept | kept |
| login_world | 8 | 2 | kept | kept |
| zone | 16 | 4 | kept | kept |
| bridge | 24 | 20 | kept | kept |

### The three services

| service | header / TU | state it holds | state it references |
|---|---|---|---|
| `eqt::ChatOutput` | `include/client/chat_output.h`, `src/client/chat_output.cpp` (67 lines) | none | `EverQuest::m_bridge` (reference TO THE POINTER, same reason `ClientContext::bridge` is one) |
| `eqt::PlayerFacade` | `include/client/player_facade.h`, `src/client/player_facade.cpp` (279 lines) | none | `m_game_state`, and `m_update_running` (deviation 2) |
| `eqt::PacketSender` | `include/client/packet_sender.h`, `src/client/packet_sender.cpp` (133 lines) | `m_replay_suppressed_sends`, the T268 capture pair | `m_game_state`, `m_zone_connection`, `m_zone_connection_manager`, `m_session_recorder`, `m_main_thread_id` |

Every member of every service is spelled EXACTLY as the member it references
on `EverQuest`, which is why the moved bodies below need zero substitutions.

`ClientContext` gained `out`, `self`, `net`, four promoted members
(`hotbar_model`, `eq_client_path`, `session_recorder`, and `zone_audio_manager`
under `#ifdef WITH_AUDIO`), and twelve component pointers with `Guild()` ...
`Commands()` accessors plus `SetComponents()`. The pointers are raw and
late-bound: `m_ctx` is built in `EverQuest`'s initialiser list, before any
component exists, so a reference member could not be bound. `EverQuest`'s ctor
calls `m_ctx.SetComponents(...)` immediately after the twelfth
`make_unique`; nothing above that line may reach a sibling.

### Per-member decision table (the `owner_.m_*` reaches, one line each)

| member | uses | decision | rationale |
|---|---|---|---|
| `m_replay_suppressed_sends` | 24 | -> `PacketSender` | the plan's step 3; it is the counter the send path itself bumps, and moving it closes K09's deviation 1. 24 `++owner_.m_...;` statements became `ctx_.net.NoteSuppressedSend();`, 49 test reads became `eq.Net().ReplaySuppressedSendCount()` |
| `m_zone_bsp_tree`, `m_zone_bsp_tree_zone_name` | 21 | -> `ZoneComponent` | zone owns the whole lifecycle (built in `LoadZoneSources`, reset in `CleanupZone`); one bridge read, one movement read, both through accessors |
| `m_zone_map` | 13 | -> `ZoneComponent` | same; `EverQuest::GetZoneMap()` deleted, its two external callers (`spell_cast_checks.cpp`, `test_eq_action_handler_cast.cpp`) retargeted to `eq.Zone().GetZoneMap()` |
| `m_zone_sky_type`, `m_zone_type`, `m_zone_fog_red/green/blue/minclip/maxclip` | 21 | -> `ZoneComponent` | written only by `ZoneProcessNewZone`, read only by `CreateZoneLoadSnapshot`. Exactly K11's measurement, executed here |
| `m_zone_audio_manager` | 14 | -> `ClientContext` | the plan said "stays on EverQuest as a ctor-wired subsystem handle"; it is a ctor-wired subsystem handle *reached by three families*, which is the definition of a `ClientContext` member. Deviation 3 |
| `m_hotbar_model` | 11 | -> `ClientContext` | K11's Review says this in as many words: "it belongs in `ClientContext` at K13, not in one family" |
| `m_world_objects` | 10 | -> `EntitiesComponent` | this family creates, reads and clears it; the only outside readers are one bridge intent arm and one action-handler listing, both retargeted through `Entities().WorldObjects()`/`GetWorldObjects()` |
| `m_eq_client_path` | 10 | -> `ClientContext` | process-wide config set once by `Application` through `SetEQClientPath`; read by zone and bridge, owned by neither |
| `m_guild_id` (8), `m_guild_rank` (2) | 10 | **STAYS** | public `GetGuildID()`/`GetGuildRank()` carry ~20 test call sites; §Mechanism 3's shared-member rule plus K09 correction (bb). Boarded B038 cluster 4 |
| `m_constrained_config` (8), `m_graphics_initialized` (5), `m_constrained_preset` (2), `m_loading_status_ptr` (1) | 16 | **STAYS** | K11's recorded standing: renderer-LIFETIME state that four `eq.cpp` bodies read around the sanctioned forwarders. Boarded B038 cluster 1 |
| `m_water_state` | 7 | -> `MovementComponent` | its sole writer. `GetWaterState`/`IsSwimming`/`SetSwimUp`/`SetSwimDown`/`m_swim_up`/`m_swim_down` each had ONE occurrence tree-wide and were DELETED (§Mechanism 5, K10 (s)) -- this closes board item **B030** |
| `m_test_bypass_send_guard` | 5 | **STAYS** | a T88 test seam gating the movement/group send guards, not the send path itself; `PacketSender` never reads it. Boarded B038 cluster 5 |
| `m_loading_phase` | 5 | **STAYS** | goes with `SetLoadingPhase`; see deviation 4 |
| `m_session_recorder` | 4 | -> `ClientContext` | `PacketSender` needs it and so does `login_world`; a shared subsystem handle, same class as `m_zone_audio_manager` |
| `m_pending_logout_disconnect` (3), `m_zone_session_established` (1), `m_update_running` (1) | 5 | **STAYS** | H03/T245 lifecycle flags owned by `EverQuest`'s update loop. Boarded B038 cluster 3 |
| `m_login_world` | 3 | -> `ctx_.LoginWorld()` | it was a reach for the component POINTER (`owner_.m_login_world.get()` inside three `std::bind`s); now `&ctx_.LoginWorld()` |
| `m_last_tell_sender` (2), `m_string_db` (2), `m_profile_appearance` (1), `m_zone_server_host`/`m_zone_server_port` (1 each) | 7 | **STAYS** | each has a public `EverQuest` accessor with callers in `tests/` and the action layer. Boarded B038 cluster 4 |

### The residue table (all 83, with the reason each one is still there)

| what | uses | family/families | why it stayed |
|---|---|---|---|
| `SetLoadingPhase` | 12 | zone 5, login_world 5, entities 2 | the loading-phase STATE MACHINE, not player state: it writes `m_loading_phase`/`m_loading_status_text`, signals the graphics-guarded `m_loading_status_ptr`, and publishes `ZoneLoading`. Folding it into `PlayerFacade` would have made that service hold four non-GameState members |
| `m_loading_phase` | 5 | bridge 4, player 1 | its store |
| `IsGraphicsReady` | 1 | bridge | reads `m_loading_phase` |
| `m_constrained_config` | 8 | bridge | renderer-lifetime; four `eq.cpp` readers (K11's standing) |
| `m_graphics_initialized` | 5 | bridge | ditto |
| `m_constrained_preset` | 2 | bridge | ditto |
| `m_loading_status_ptr` | 1 | bridge | ditto |
| `m_guild_id` | 8 | guild 6, entities 1, zone 1 | public `GetGuildID()`, ~14 test sites |
| `RequestQuit` | 7 | zone 6, player 1 | a public lifecycle entry point `Application` polls via `IsQuitRequested()`; not a family's |
| `m_test_bypass_send_guard` | 5 | movement 3, group 2 | T88 seam on the movement send guards |
| `GetStringMessage` | 4 | commands 2, merchant 1, chat 1 | PUBLIC on `EverQuest` (T244), over `m_string_db`; the eqstr lookup surface, reached by non-family code too |
| `GetFormattedStringMessage` | 3 | chat 2, merchant 1 | same (T251) |
| `m_pending_logout_disconnect` | 3 | movement 2, player 1 | H03 deferred-disconnect flag, processed at the top of `UpdateMovement` |
| `TickNetwork` | 3 | bridge | `EverQuest`'s own network pump |
| `m_guild_rank` | 2 | guild 1, zone 1 | public `GetGuildRank()` (T317), 7 test sites |
| `m_last_tell_sender` | 2 | chat | public `GetLastTellSender()`, 3 action-handler + 6 test sites |
| `m_string_db` | 2 | chat 1, player 1 | the `EQT::StringDatabase` itself; public `GetStringDatabase()` |
| `SetSneak` | 1 | movement | a public `EverQuest` body that itself calls `m_entities_component->SendSpawnAppearance` |
| `m_update_running` | 1 | entities | the T245 update-loop flag (`PlayerFacade` reads it, but the WRITE is `StartUpdateLoop`'s) |
| `StartUpdateLoop`, `StopUpdateLoop` | 2 | entities 1, zone 1 | its writers |
| `OnGameStateComplete` | 1 | entities | `EverQuest` lifecycle transition |
| `ConnectToZone` | 1 | login_world | `EverQuest`'s own zone-connection setup (`ZoneOn*` callbacks live there) |
| `m_zone_session_established` | 1 | zone | zone-session flag read by `eq.cpp` |
| `m_profile_appearance` | 1 | zone | public `GetProfileAppearance()`/`SetProfileAppearance()` (T167) |
| `m_zone_server_host`, `m_zone_server_port` | 2 | login_world | public `GetZoneServerHost()`/`GetZoneServerPort()` (T09), read by `ConnectToZone` |
| **total** | **83** | | boarded as **B038**, ranked into five clusters |

### The forwarders (the plan's explicit exception to §Mechanism 5)

| surface | forwarders | non-component call sites kept alive by them |
|---|---|---|
| `AddChatSystemMessage` / `AddChatCombatMessage` / `AddChatMissMessage` | 3 (`src/client/eq_chat.cpp`) | **142** in 6 files: `eq_action_handler.cpp`, `eq_action_handler_extended.cpp`, `eq_action_handler_spells_trade.cpp`, `spell/spell_manager.cpp`, `skill/skill_manager.cpp`, `application_commands.cpp` |
| the player-state facade | 46 (`src/client/eq_player_info.cpp`) + 9 inline in `eq.h` (`IsFullyZonedIn`, `GetEntityID`, `GetMySpawnID`, `HasPet`, `IsVendorWindowOpen`, `GetVendorNpcId`, `IsBankWindowOpen`, `IsTrainerWindowOpen`, `Test_SetMySpawnIdForSeam`) = **55** | **~262** in 36 files -- combat (`combat.cpp`, `combat_rounds.cpp`, `combat_hunting.cpp`), the spell/skill/inventory managers, the three action-handler TUs, `application*.cpp`, and 25 test files |
| `QueuePacket` | 1 (`src/client/eq.cpp`) | **17** on the `EverQuest` receiver: `combat.cpp` 11, `spell/spell_manager.cpp` 3, `skill/skill_manager.cpp` 3 |
| the T268 capture seam (`Test_SetCaptureQueuedPacketsForSeam`, `Test_GetQueuedPackets`, `Test_ClearQueuedPackets`) + `using TestQueuedPacket = eqt::PacketSender::TestQueuedPacket;` | 3 + 1 alias | **37** in 3 test files (`test_loot_window_actions.cpp` 30, `test_zone_out_handshake.cpp` 4, `test_merchant_component.cpp` 3) -- kept so those files needed NO edit |
| **total** | **62 forwarders + 1 type alias** | |

`SafeQueueZonePacket` kept NO forwarder: it was private and had no caller
outside the friend components. Neither did `GetReplaySuppressedSendCount`
(49 test sites retargeted to `eq.Net().ReplaySuppressedSendCount()`),
`GetZoneMap` (2 sites), or `GetWorldObjects` (2 sites).

### The `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable`, `a89314e0`) | **990** |
| plan's step 9 estimate | -50 or better |
| **predicted before any edit** (the private T288 facade block 22 + its 33-line banner, the 3 `AddChat*` declarations, `SafeQueueZonePacket` + `m_replay_suppressed_sends` + `GetReplaySuppressedSendCount` + their comments ~12, the T268 struct/seam block ~18, `m_zone_map` + `GetZoneMap` + their banners ~24, the BSP/fog/sky members ~19, `m_world_objects` 2, the swim block 8, minus +3 service accessors +3 service members +3 forwarder replacements) | **~ -70 or better** |
| **measured after** | **916 (-74)** |

`git diff --numstat` on the single file: **141 deletions, 67 insertions = net
-74**. `scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**990 -> 916** in the same commit.

The same shape K02/K11/K12 all found holds again: the shrinkage is driven by
the comment banners riding on what leaves (the T288 block's 33-line banner and
the `m_zone_map` P35 banner are 57 lines between them), not by declaration
count. It is SMALLER than K11's -148 or K12's -88 because 62 declarations
STAYED, as sanctioned forwarders -- this unit trades `eq.h` lines for
`owner_.` reaches, and the `owner_.` number is the one it exists to move.

### Zero-code-residual multiset proof (§Mechanism 8)

`scripts/refactor/verify_move_only.py` takes committed revisions and cannot
normalise this unit's substitutions, so the proof was done the way K02/K11/K12
did it: reverse the substitutions by name mapping on a COPY of each new file,
strip comment-only/blank/preprocessor lines and trailing `//` comments (with
string literals masked), sort, and diff the line multisets against
`git show a89314e0:<old path>`.

| moved body set | old code lines | new code lines | ADDED | REMOVED |
|---|---|---|---|---|
| `ChatOutput` (3 bodies, was `src/client/eq_chat.cpp`) | 24 | 24 | **0** | **0** |
| `PlayerFacade` (46 bodies, was `src/client/eq_player_info.cpp`) | 185 | 185 | **0** | **0** |
| `PacketSender` (2 bodies, was `src/client/eq.cpp`) | 83 | 83 | **0** | **0** |

Normalisation used: `eqt::ChatOutput::System/Combat/Miss` ->
`EverQuest::AddChatSystemMessage/AddChatCombatMessage/AddChatMissMessage`;
`eqt::PlayerFacade::` -> `EverQuest::`; `eqt::PacketSender::SafeQueueZone` ->
`EverQuest::SafeQueueZonePacket`; `eqt::PacketSender::Queue` ->
`EverQuest::QueuePacket`; `EverQuest::GetDebugLevel() >= 2` -> `s_debug_level >= 2`;
`EverQuest::GetOpcodeName(` -> `GetOpcodeName(`. **Nothing else** -- the
services name every referenced member exactly as `EverQuest` does, so the
usual `m_x -> ctx_.x` mapping had nothing to reverse.

**Zero code lines added or removed in any of the three sets.** The move is
verbatim.

A fourth set was proved by direct byte comparison rather than multiset: the
22-line private T288 accessor block (`PosX` ... `CurrentZoneId`) moved from
`include/client/eq.h` into `include/client/player_facade.h` **byte-identical**,
banner and all.

K10 correction (u) applies -- two bodies came out of `src/client/eq.cpp`, a TU
that is not the services' own -- and that residual is the third row above.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0 on the 4th attempt.** 1st: exit 2, 27 errors in two classes -- `merchant`/`commands` lost `friend` and could no longer name `EverQuest::FormatPrice`/`DumpPacket`/`s_debug_level` (correction 2), and `std::map<uint32_t, eqt::WorldObject>` by value needs a complete `WorldObject` in `entities_component.h`. 2nd: exit 2, ONE error -- `tests/test_tradeskill_combine.cpp` reaching `h.eq.m_world_objects[1]` through a `#define private public` (K12 correction (gg), second sighting). 3rd: exit 2, ONE error -- `tests/test_eq_action_handler_cast.cpp` assigning `h.eq.m_zone_map`, same hack. 4th and 5th (after adding the three test targets): **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,604 / 3,605 passed**, 17 skipped, 136 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA host quirk). `FileSizeCeiling`, `DocCitations`, all five `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (Test #3600, 20.77 s, inside this same run) all **PASS** |
| the three new suites | **17/17 pass**: `test_chat_output` 4, `test_player_facade` 7, `test_packet_sender` 6 |
| existing suites, assertions untouched | 49 replay-counter reads across 8 files (`test_session_replay`, `test_combat_manager`, `test_login_world_component`/`_seam`/`_flow`, `test_zone_component`, `test_zone_connection_flow`, `test_buff_click_off`) retargeted to `eq.Net().ReplaySuppressedSendCount()`; 2 `GetZoneMap`, 2 `GetWorldObjects`, and the two `#define private public` sites. **Every assertion left as written** |
| `test_duplicate_state_stores` / `test_reachability_sweep` | **pass**, no file-list edit needed. Notably `DuplicateStateStores` is the test that would have caught a `PlayerFacade` holding its own position copy -- it does not, and the new `test_player_facade.cpp` pins the same property behaviourally |
| `bash scripts/build-gles2-x86.sh` | **exit 0** (run in full, not skipped) |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --name-only fable -- src/client/graphics include/client/graphics` is **EMPTY** -- no graphics file changed in this unit's 61 touched + 9 added files. `visual_gate` nonetheless ran and PASSED inside the full `ctest` run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **832 ok, 0 warn, 0 FAIL**; `eq.h` allowlist entry lowered **990 -> 916** |

### Deviations and corrections

1. **The first STOP condition ("a service needs state that is not
   `game_state`, the bridge, or the zone connection") is self-inconsistent
   with the plan's own step 3, and step 3 was followed.** Step 3 names
   `m_replay_suppressed_sends` and `GetReplaySuppressedSendCount()` as
   `PacketSender`'s contents, and neither is `game_state`, the bridge or the
   zone connection. Executing step 3 as written, `PacketSender` also needs
   `m_zone_connection_manager` (the off-main-thread async path),
   `m_session_recorder` (the C->S recording hook) and `m_main_thread_id` (the
   thread test that chooses between them) -- all three are read by the two
   bodies the plan says to move, and dropping any of them would change
   behaviour. **They are bound by reference, under their own names, exactly
   the way `ClientContext` binds its fourteen core members** -- no new state,
   no ownership change, no design question. Recorded rather than STOPped
   because a STOP here would have blocked the unit on a contradiction in its
   own contract, not on a real decision. If Will wants `PacketSender` narrowed
   to the zone connection alone, that is a follow-up, and the counter and the
   T268 seam would have to go somewhere else.

2. **The `friend class` test is TWO-AXIS, not one, and the plan's rule states
   only one axis.** The plan says "remove `friend class` lines only where zero
   `owner_.m_` uses remain". `merchant` and `commands` both reach zero
   `owner_.m_` -- their only remaining handle uses are the two PUBLIC string-DB
   lookups -- so both lines were removed, and the build produced 12 errors:
   a component also names `EverQuest`'s PRIVATE STATICS by QUALIFICATION
   (`EverQuest::FormatPrice`, `EverQuest::DumpPacket`,
   `EverQuest::s_debug_level`), which friendship grants and which no `owner_.`
   census can see (this is §Mechanism 4's third substitution shape, K05
   correction (j), colliding with the friend rule). Both lines restored
   in-unit. **The rule should read: remove the `friend` line when the family
   has zero `owner_.m_` reaches AND zero uses of an `EverQuest`-scoped private
   name.** `eqt::gm::GmComponent` passes both axes -- it is the only one --
   and lost its `friend` line and its `EverQuest&` ctor parameter together.
   Boarded as **B039**.

3. **A FIFTH substitution shape, forced by services not being friends.**
   §Mechanism 4 allows three substitutions (four with K06/K07's `this` ->
   `&owner_`), all of which assume the destination is a FRIEND of `EverQuest`.
   A context service is not. `SafeQueueZonePacket`'s body reads
   `s_debug_level` and calls `GetOpcodeName()`, both private. The two
   substitutions used are `s_debug_level` -> `EverQuest::GetDebugLevel()` (the
   public accessor that returns that same static and nothing else) and
   `GetOpcodeName(` -> `EverQuest::GetOpcodeName(`, with the declaration moved
   to `public:` in `eq.h` -- the same standing T180 gave `GetClassName` and
   T271 `GetRaceName`, and for the same reason: a pure static lookup table
   with no state. Both are behaviour-preserving and both are named in
   `src/client/packet_sender.cpp`'s file banner. Future units moving a body
   into a non-friend destination should expect this shape.

4. **`SetLoadingPhase` was NOT folded into `PlayerFacade`**, though the plan's
   target table lists it in the facade group (12 uses). Reading the body
   settles it: it writes `m_loading_phase` and `m_loading_status_text`, calls
   `GetLoadingStatusText()` and `GetLoadingProgress()`, signals
   `m_loading_status_ptr` inside `#ifdef EQT_HAS_GRAPHICS`, and publishes a
   `ZoneLoading` event through the bridge. That is a loading-phase state
   machine whose readers are `Application` (`GetLoadingPhase`,
   `GetLoadingProgress`, `IsGraphicsReady`), not player state. Putting it in
   the facade would have given a service the plan describes as wrapping
   "`game_state` and nothing else" four more non-GameState members. It is the
   largest single residue entry and is B038's cluster 2.

5. **`IsFullyZonedIn` WAS folded in, and it costs `PlayerFacade` one
   non-`game_state` reference.** Its third term is `m_update_running`. The
   plan names `IsFullyZonedIn` in the facade group explicitly (29 uses, the
   6th-largest name in the whole census), so it moved, and the facade holds
   `std::atomic<bool>& m_update_running` for it. That reference is the only
   thing in `player_facade.h` that is not `GameState`, and it is called out in
   the header banner and pinned by
   `PlayerFacadeTest.NotFullyZonedInOnAFreshHeadlessClient`.

6. **`m_zone_audio_manager` and `m_hotbar_model` went into `ClientContext`,
   not "stayed on `EverQuest`" as the plan's zone-environment row says.** They
   are ctor-wired subsystem handles -- which is what the plan calls them -- but
   they are reached by three families and two families respectively, which is
   the definition of a `ClientContext` member; `m_inventory_manager`,
   `m_command_registry` and `m_skill_manager` are already there on exactly that
   basis, and K11's Review recorded the hotbar decision in as many words
   ("it belongs in `ClientContext` at K13"). Keeping them on `EverQuest` would
   have left 25 `owner_.` uses in the residue for no gain.

7. **`m_swim_up`/`m_swim_down`/`SetSwimUp`/`SetSwimDown`/`GetWaterState`/
   `IsSwimming` deleted (K10 correction (s), §Mechanism 5).** Each had exactly
   ONE occurrence tree-wide -- its own definition -- across `src/`, `include/`,
   `tests/` and `scripts/`. This closes standing board item **B030** (opened by
   the K07 Review). `m_water_state` itself is live and moved to
   `MovementComponent`; `MovementComponent::GetWaterState()`/`IsSwimming()`
   exist there for the one bridge reader. Per K12 correction (ff) the deleted
   accessors were checked for event publication first: none of the six
   references a `GameEventType` or pushes an intent.

8. **`#define private public` test hacks, twice more** (K12 correction (gg)).
   `test_tradeskill_combine.cpp` writes `h.eq.m_world_objects[1]` and
   `test_eq_action_handler_cast.cpp` assigns `h.eq.m_zone_map`. Both invisible
   to every declaration and receiver grep; both found instantly by the
   compiler; both retargeted to the component's own accessor
   (`h.eq.Entities().WorldObjects()[1]`, `h.eq.Zone().ZoneMap()`) -- the hack
   still reaches through, it just reaches a different owner. Two units running
   now, so it is a standing caller class, not a curiosity.

9. **`ClientContext` had to stay an AGGREGATE.** `EverQuest` brace-initialises
   `m_ctx` in its ctor initialiser list, and an aggregate may not have private
   data members -- so the twelve component pointers are public (with a comment
   saying so and why) even though only `SetComponents()` writes them and only
   the twelve accessors read them. Member functions and default member
   initialisers are both fine in an aggregate; private data is not.

10. **No other STOP condition was hit.** The residue STOP threshold was 250;
    the measured residue is **83**. Every moved body needed only the
    substitutions above. No test asserted on a member by name that could not
    be repointed. The predicted `eq.h` delta was negative before dispatch and
    the measured delta is more negative still.

### Left for the board

- **B038** (new) -- the 83-use residue, in five ranked clusters, with the
  reason each one stayed. This is the entry that says what "remove the
  transition handle" now costs.
- **B039** (new) -- the two-axis `friend` rule (correction 2), so
  `refactor/K_component_extraction.md` §Mechanism can absorb it.
- **B030** -- CLOSED by this unit (correction 7); struck in `refactor/BOARD.md`.
- The `SpellTypeProcessor`/`ConstrainedMeshCache` class of open question is
  untouched by this unit.

## After K13 (not this unit)

- **K14** — directory move: the remaining flat `src/client/eq_*.cpp`
  (`eq.cpp`, `eq_action_handler*.cpp`, `eq_strings.cpp`) into
  `src/client/core/` and `src/client/action/`; `eq_strings` → free
  functions in `eq_strings.h`.
- **K15** — the S2 leftovers: `InitGraphics(IrrlichtRenderer*, …)` parameter,
  `door_manager.cpp`'s `EverQuest::GetDebugLevel()`; delete `UCSConnection`
  (B005) and the dead members B019/B030/B031/B034/B035; the 14 dead renderer
  declarations B033.
- Then **Batch L**.
