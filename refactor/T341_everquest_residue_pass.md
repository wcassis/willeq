# T341 — The EverQuest transition-handle residue: retire clusters 1–4 (B038), close B039

Structural, class B. Slot2. Measured at `57aa74c5`: 131 `owner_.` uses
across the twelve `src/client/<family>/` components (K13 left 83; K14–K16
and the T-units added a few), eleven `friend class eqt::…` lines in
`include/client/eq.h`.

## Rulings per cluster (B038)

1. **Renderer-lifetime state** (`m_constrained_config`, `m_graphics_initialized`,
   `m_constrained_preset`, `m_loading_status_ptr`) — bridge-only readers →
   MOVE to `BridgeComponent` (it owns the graphics lifecycle); the four
   `everquest.cpp` bodies that read them go through `Bridge().accessor()`.
2. **The loading-phase state machine** (`SetLoadingPhase`, `m_loading_phase`,
   `IsGraphicsReady`) → MOVE to `BridgeComponent` as `LoadingPhase` state
   with the same publish; `EverQuest` keeps a one-line forwarder ONLY if
   `Application` calls it (name the caller).
3. **Lifecycle entry points** (`RequestQuit`, `TickNetwork`, `Start/StopUpdateLoop`,
   `OnGameStateComplete`, `ConnectToZone`, `SetSneak`, `m_update_running`,
   `m_pending_logout_disconnect`, `m_zone_session_established`) — these are
   `EverQuest`'s own job; components reach them via a small
   `ctx_.lifecycle` service (`ILifecycle`-shaped struct of bound
   `std::function`s or a reference to a `LifecycleFacade` class holding
   them — the K13 `PacketSender` shape). Record the choice.
4. **Members with a public accessor carrying external callers**
   (`m_guild_id`/`m_guild_rank` → `GuildComponent`, with `GetGuildID`/
   `GetGuildRank` forwarding; `m_last_tell_sender` → `ChatComponent`, with
   `GetLastTellSender` forwarding) — the forwarders stay because the ~30
   external call sites are tests and action handlers (name them); count
   them.
5. Whatever remains after 1–4 is re-censused and boarded as the new B038
   text (expect ≤ 40, all lifecycle).

Then the friend census (two axes: `owner_.m_` AND qualified private
names/nested types — K13 ii, L09 38) per component; delete the `friend`
line and the `EverQuest&` ctor parameter wherever BOTH are empty (expect
several: gm already; guild, chat, group after cluster 4?). B039 closes by
citing `refactor/K_component_extraction.md` §K13 corrections (ii), which
already states the two-axis rule — strike it.

Proof: the K/L multiset discipline for moved declarations; every existing
test with assertions untouched; `eq.h` ratchet (no allowlist — it must
simply stay under 1,500; report the count, expect ≈ 850); full bar
(graphics files unchanged → gate/ARM skipped with evidence).

STOP: a lifecycle method needs a thread change; a cluster-4 accessor's
external callers exceed 40 (then leave that member and say so).

## Review

Executed alone in `wt/slot2` on `task/T341-everquest-residue`, branched from
`fable` at **`0ce3ecc3`**. Every number below is RE-MEASURED at that tip
unless it says otherwise. **Neither STOP fired**: no lifecycle method needed a
thread change, and the largest cluster-4 accessor carries 14 external call
sites, not 40.

### The headline: `owner_.` 83 -> 5, eleven `friend` lines -> three

The census is over `src/client/<family>/*.{cpp,h}` + `include/client/<family>/*.h`
for the twelve families, comment-filtered (`src/client/graphics/` is Batch L's
`IrrlichtRenderer&` handle and is not this handle at all).

| family | `owner_.` before | after | other `owner_` uses | `friend` line | `EverQuest&` handle |
|---|---|---|---|---|---|
| **guild** | 7 | **0** | 0 | **REMOVED** | **REMOVED** |
| **chat** | 6 | **0** | 0 | **REMOVED** | **REMOVED** |
| **merchant** | 2 | **0** | 0 | **REMOVED** | **REMOVED** |
| **entities** | 6 | **0** | 0 | **REMOVED** | **REMOVED** |
| **login_world** | 8 | **0** | 0 | **REMOVED** | **REMOVED** |
| **player** | 4 | **0** | 0 | **REMOVED** | **REMOVED** |
| **commands** | 2 | **0** | 0 | **REMOVED** | **REMOVED** |
| **bridge** | 22 | **0** | 2 (`castRefusalMessage(owner_, …)`) | **REMOVED** | KEPT |
| gm | 0 | 0 | 0 | (gone at K13) | (gone at K13) |
| group | 2 | 2 | 0 | kept | kept |
| movement | 6 | 3 | 22 (`&owner_` identity keys) | kept | kept |
| zone | 16 | 0 | 2 (`&owner_` in two `std::bind`s) | kept | kept |
| **total** | **83** | **5** | | **11 -> 3** | **11 -> 4** |

`eq.h`: **899 -> 899** (no allowlist entry; the 1,500 ceiling is enforced
tree-wide by ctest since M00). The declarations and members that left are
almost exactly balanced by the five forwarder bodies and the T341 notes; the
file was trimmed back to its dispatch value deliberately rather than allowed
to grow.

### Cluster 1 — renderer-lifetime state -> `BridgeComponent`

`m_graphics_initialized` (5), `m_constrained_config` (8),
`m_constrained_preset` (2), `m_loading_status_ptr` (1) — **16 uses, all
bridge** — moved verbatim from `include/client/eq.h`'s `EQT_HAS_GRAPHICS`
private block into `include/client/bridge/bridge_component.h`, under the same
`#ifdef`, comments and all. `SetConstrainedConfig`'s body moved with them
(`src/client/bridge/bridge_component.cpp`).

`m_zone_load_requested` did NOT move: no component ever reached it;
`Application` consumes it through `EverQuest::ConsumeZoneLoadRequest()`.

The four `everquest.cpp` bodies K13's board entry named turned out to be
**two**, not four, at this tip: `OnGameStateComplete()` reads
`m_graphics_initialized` (now `m_bridge_component->IsGraphicsInitialized()`)
and `SetConstrainedConfig()` wrote the other two (now the forwarder).

| forwarder kept on `EverQuest` | why | named callers |
|---|---|---|
| `SetConstrainedConfig` | `Application` wires the NxNxN config before `InitGraphics` | `src/client/application.cpp:626` |
| `SetLoadingStatus` | `Application` owns the loading thread's `LoadingStatus` | `src/client/application.cpp` (1) |

### Cluster 2 — the loading-phase state machine -> `BridgeComponent`

`SetLoadingPhase` (12) + `m_loading_phase` (5) + `IsGraphicsReady` (1) — **18
uses** — plus, because they are the same machine, `m_loading_status_text`,
`GetLoadingPhase`, `GetLoadingProgress`, `GetLoadingStatusText` and
`IsGameStateReady`.

K13's deviation 4 declined to fold this into `eqt::PlayerFacade` because it is
not player state. It is the GRAPHICS LIFECYCLE — `SetLoadingPhase` signals
`m_loading_status_ptr` (cluster 1, now this component's) and publishes
`ZoneLoading` through the bridge — which is `BridgeComponent`'s whole job, so
the machine and its store went there together.

Component call sites became `ctx_.Bridge().SetLoadingPhase(…)` (12) and
`ctx_.Bridge().GetLoadingPhase()` (5, `player_misc.cpp` and the two
`bridge`/`ui_audio` reads, which became plain member reads).

| forwarder kept on `EverQuest` | named callers |
|---|---|
| `SetLoadingPhase` | `application_zone_loading.cpp` 2, `everquest.cpp` 6 (`OnGameStateComplete`, `OnGraphicsComplete`, `ConnectToZone`, the reconnect arm), `tests/test_spell_manager_timing.cpp` 1 |
| `GetLoadingPhase` | **62 test sites**: `test_zone_connection_flow` 20, `test_zoning_graphics_integration` 12, `test_zoning_integration` 12, `test_login_world_flow` 10, `test_login_world_seam` 8 |
| `GetLoadingProgress` | `tests/test_zoning_graphics_integration.cpp` 2 |
| `IsGameStateReady` | `test_zone_connection_flow` 5, `test_zoning_integration` 5 |
| `IsGraphicsReady` | `src/client/application_frame.cpp:85`, `test_zoning_graphics_integration` 4, `test_spell_manager_timing` 2 |
| ~~`GetLoadingStatusText`~~ | **NO forwarder — zero callers on the `EverQuest` receiver tree-wide (§Mechanism 5).** Its only caller is `SetLoadingPhase`, in-family |

### Cluster 3 — the lifecycle service (`ctx_.lifecycle`)

**The plan's FIRST shape was chosen: an aggregate of bound `std::function`s
plus references to the three flags** — `eqt::LifecycleFacade`,
`include/client/lifecycle_facade.h`, a new fifth context service on
`ClientContext`. The `PacketSender` shape (a service that HOLDS the bodies)
was rejected on reading the bodies: `ConnectToZone` builds a
`DaybreakConnectionManager` and installs four `EverQuest` **private** member
functions as callbacks; `StopUpdateLoop` is called from `~EverQuest`;
`TickNetwork` pumps three connections owned by two different objects. Moving
them is not a mechanical move, and this unit's charter is mechanical. A bound
call changes **no body at all**, so cluster 3 costs zero behaviour risk and
its whole proof obligation is the call-site rewrite.

Members are spelled exactly as the `EverQuest` name they stand for (K13 (kk)),
so every retarget was `owner_.` -> `ctx_.lifecycle.` and nothing else — **20
sites, scripted on code lines only**, no comment touched:

| name | uses | families |
|---|---|---|
| `RequestQuit` | 7 | zone 6, player 1 |
| `TickNetwork` | 3 | bridge |
| `m_pending_logout_disconnect` | 3 | movement 2, player 1 |
| `StartUpdateLoop`, `StopUpdateLoop`, `OnGameStateComplete`, `ConnectToZone`, `SetSneak`, `m_update_running`, `m_zone_session_established` | 1 each | entities 4, login_world 1, movement 1, zone 1 |

`m_update_running` is now referenced by BOTH `eqt::PlayerFacade` (its third
`IsFullyZonedIn()` term, K13 deviation 5) and `LifecycleFacade` — the same
`std::atomic<bool>`, two readers, no copy. `DuplicateStateStores` passes.

### Cluster 4 — members with a public accessor carrying external callers

The plan's parenthetical named guild and tell; the cluster as B038 defines it
also holds `m_string_db`, `m_profile_appearance` and
`m_zone_server_host`/`m_zone_server_port`, and all of them are the same
mechanical shape, so all were done. Every forwarder stays, and every one is
under the STOP threshold of 40.

| member | uses | new home | `EverQuest` forwarder | named external callers |
|---|---|---|---|---|
| `m_guild_id` | 8 | `guild::GuildComponent` (+`SetGuildID` for the zone/entities writers) | `GetGuildID()` | **13**: `test_zone_connection_flow` 10, `test_guild_wire_rank` 2, `test_guild_component` 1 |
| `m_guild_rank` | 2 | `guild::GuildComponent` (+`SetGuildRank`) | `GetGuildRank()` | **7**: `test_guild_wire_rank` 6, `test_guild_component` 1 (plus a doc citation in `include/client/guild_packet_bodies.h`) |
| `m_last_tell_sender` | 2 | `chat::ChatComponent` | `GetLastTellSender()` | **10**: `action/eq_action_handler.cpp` 3 (T18's `getLastTellSender`), `test_zone_chat` 4, `test_chat_component` 3 |
| `m_profile_appearance` | 1 | `zone::ZoneComponent` | `GetProfileAppearance()` / `SetProfileAppearance()` | **8**: `action/eq_action_handler_spells_trade.cpp` 2 (the `/appearance` round trip), `test_profile_appearance` 6 |
| `m_zone_server_host` / `m_zone_server_port` | 2 | `login_world::LoginWorldComponent` | `GetZoneServerHost()` / `GetZoneServerPort()` | **4** test sites (`test_login_world_flow`), plus `EverQuest::ConnectToZone()`, which now reads them through `m_login_world->` |
| `m_string_db` reaches: `Strings()` 7, `m_string_db` 2 | 9 | — the member STAYS on `EverQuest` as `eqt::StringDb`'s referent (K13 (kk)) | — | `owner_.Strings()` -> `ctx_.strings` (chat 3, merchant 2, commands 2); `owner_.m_string_db.getString(` -> `ctx_.strings.Db().getString(` (chat 1, player 1) via a new `StringDb::Db()` |

**`EverQuest::GetStringDatabase()` was DELETED** — zero callers tree-wide
(§Mechanism 5). `StringDb::Db()` replaced it for the two callers that need
`getString()`'s empty-on-miss rather than `GetStringMessage()`'s built-in
fallback.

### The friend census, both axes (K13 (ii) / B039)

Axis 1 = a PRIVATE `EverQuest` member or method reached through the handle.
Axis 2 = an `EverQuest`-scoped private name by QUALIFICATION.

Measured BEFORE deleting anything, axis 2 was non-empty for **all twelve**
families: every one of them writes `EverQuest::s_debug_level` — **134 sites in
22 files**. On the one-axis rule seven families were clean; on the two-axis
rule none were, and the unit's friend half would have produced nothing.

**Deviation 1 (recorded, not a STOP): the 134 `EverQuest::s_debug_level`
qualifications inside the twelve families were rewritten to the PUBLIC
`EverQuest::GetDebugLevel()`.** This is K13 correction (jj) verbatim — the
same substitution K13 gave `PacketSender` for the same reason — applied to a
friend destination instead of a non-friend one. `GetDebugLevel()` is
`static int GetDebugLevel() { return s_debug_level; }`: the same static, no
other statement. Scripted on code lines only; no comment rewritten. Without
it, axis 2 could not be emptied for any family and no `friend` line could go.

| family | axis 1 after | axis 2 after | ruling |
|---|---|---|---|
| guild, chat, merchant, entities, login_world, player, commands | 0 | 0 | **friend + handle removed** |
| bridge | 0 | 0 | **friend removed; handle KEPT** — `bridge_component_intents.cpp:423,644` pass `owner_` BY REFERENCE to `EQ::castRefusalMessage(EverQuest&, CastResult)`, a free function that needs no private access |
| group | 2 (`m_test_bypass_send_guard`) | 0 | both kept |
| movement | 3 (`m_test_bypass_send_guard`) | 0 | both kept (plus 22 `&owner_` identity keys, K06/K07 (n)) |
| zone | 0 | 2 (`&EverQuest::ZoneOnStatusChangeReconnectDisabled`, a PRIVATE member function, in two `std::bind`s) | both kept |

**Deviation 2: `bridge` is the first family to lose friendship while KEEPING
the handle.** The two axes and the handle are three different questions, and
K13's table conflated the last two. A handle use that never touches a private
name — here, passing `EverQuest&` to a free function — buys no friendship and
so cannot justify a `friend` line. That is a THIRD rule the batch method did
not have and it is stated in `eq.h`'s new friend banner.

Seven `explicit <Family>Component(ClientContext& ctx)` ctors replaced the
two-argument form; `EverQuest`'s ctor drops `*this` for those seven. No other
site constructs a component (checked: `tests/`, `src/`, `include/`).

### Cluster 5 — what remains, and the new B038

**5 uses, one member, two families:** `m_test_bypass_send_guard` — movement 3
(`movement_update.cpp:573,596`, `movement_state.cpp:191`), group 2
(`group_component.cpp:84,112`). It is the T88 test seam gating the movement and
group send guards, set by `EverQuest::Test_SetBypassSendGuardForSeam`. It is a
TEST seam on a PRODUCTION guard, which is a design question (where does a
send-guard bypass belong once the families own their guards?), not a mechanical
move — so it is left, and it is the whole of the rewritten B038.

Beyond it, three couplings survive that are not `owner_.` at all and are
boarded with it: movement's 22 `&owner_` identity keys, zone's two
`std::bind`s on a private `EverQuest` member function, and bridge's two
`castRefusalMessage(owner_, …)` arguments.

### Proof (§Mechanism 8)

Reverse-substituted by name mapping on a copy of the new file
(`eqt::bridge::BridgeComponent::` -> `EverQuest::`, `ctx_.bridge` ->
`m_bridge`, `ctx_.self.` -> `m_player_facade.`), comment/blank-stripped,
sorted, diffed against `git show 0ce3ecc3:src/client/core/everquest.cpp`:

| moved body set | old code lines | new code lines | ADDED | REMOVED |
|---|---|---|---|---|
| `SetLoadingPhase` + `GetLoadingProgress` + `GetLoadingStatusText` + `SetConstrainedConfig` (was `src/client/core/everquest.cpp`) | 75 | 75 | **0** | **0** |

The moved DECLARATIONS (the six cluster-1/2 members, with their `#ifdef`
guard and their comments) went across byte-identical. Clusters 3 and 4 moved
NO body: cluster 3 binds calls, and cluster 4 moved members plus their inline
accessors, so there is nothing to prove but the multiset of declarations,
which is unchanged.

K10 correction (u) applies — the bodies came out of `everquest.cpp`, a TU that
is not the bridge family's — and that residual is the row above.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, first attempt at every stage (three staged builds: clusters 1-3, cluster 4, the friend/handle removal) |
| `ctest --test-dir build -j6 --output-on-failure` | **3,795 / 3,796 passed**, 214 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA host quirk) |
| `test_duplicate_state_stores` | `DuplicateStateStores.EverQuestKeepsNoCopyOfAStateModuleMember` **passes**, no file-list edit |
| `test_reachability_sweep` | all five **pass**, no file-list edit (`EverySenderHasACaller`, `EveryEventIsPublishedAndConsumed`, `EveryIntentHasAConsumerArm`, `ProducerlessIntentsAreExactlyTheBlockedList`, `UnreachableHandlerMethodsAreExactlyTheBlockedList`) |
| component suites | `test_guild_component`, `test_chat_component`, `test_bridge_component`, `test_zone_component`, `test_player_component`, `test_login_world_component`, `test_merchant_component`, `test_group_component`, `test_movement_component`, `test_entities_*` — all **pass**, every assertion as written |
| `FileSizeCeiling` / `DocCitations` | **pass** |
| `bash scripts/refactor/check_file_sizes.sh --all` | **874 ok, 0 warn, 0 FAIL** |
| `eq.h` | **899 lines**, no allowlist entry (M00 removed them) |
| gles2 / visual gate (standalone) / ARM | **SKIPPED, with evidence**: `git diff --name-only $(git merge-base fable HEAD) -- src/client/graphics include/client/graphics` is **EMPTY**. `visual_gate` (Test #3791) nonetheless ran and **PASSED** inside the full ctest run |

Assertions: **untouched everywhere.** Two TEST_F NAMES were changed because
they had become false claims —
`GuildComponentTest.ComponentReadsGuildIdAndRankThroughTheOwnerHandle` ->
`…WrittenByTheZoneFamily` and
`ChatComponentTest.ComponentWritesLastTellSenderThroughTheOwnerHandle` ->
`…ReadBackThroughEverQuest`. Neither name is referenced anywhere outside its
own file, and neither body changed by a character.

### Deviations

1. **The 134 `EverQuest::s_debug_level` -> `EverQuest::GetDebugLevel()`
   rewrite** (above). Recorded rather than STOPped: it is the substitution
   K13 (jj) already sanctions, it is behaviour-identical by inspection of a
   one-line accessor, and without it the unit's friend half is empty.
2. **`bridge` lost friendship but kept the handle** (above) — the third rule.
3. **Cluster 4 was executed as B038 defines it, not as the ruling's
   parenthetical enumerates it.** The ruling's heading names the CLASS
   ("members with a public `EverQuest` accessor carrying external callers");
   `m_string_db`, `m_profile_appearance` and `m_zone_server_host`/`_port` are
   in that class and in B038's cluster-4 text. Doing only the two worked
   examples would have left `zone` and `login_world` holding one and two
   reaches for no reason and would have left their `friend` lines standing.
4. **`GetLoadingStatusText()` got no forwarder** — zero callers on the
   `EverQuest` receiver (§Mechanism 5). Predicted six forwarders for cluster
   2; five were built.
5. **`EverQuest::GetStringDatabase()` deleted** — zero callers tree-wide, a
   §Mechanism 5 delete the plan did not anticipate. Checked for event
   publication first (K12 (ff)): it is a one-line const accessor.
6. **The cluster-5 residue is 5, not "≤ 40, all lifecycle".** The plan
   expected cluster 3 to leave lifecycle reaches behind; the bound-call shape
   removed all twenty, so the only survivor is the T88 send-guard seam.
7. **`eq.h` came out at 899, not the predicted ≈ 850.** The prediction assumed
   declarations leaving without forwarders replacing them; twelve forwarders
   stayed (five loading-phase, two constrained/loading-status, five
   cluster-4), each with a note saying why. The file was trimmed back to
   exactly its dispatch value rather than allowed to grow.

### Left for the board

- **B038** — rewritten: 83 -> 5, the surviving member named, plus the three
  non-`owner_` couplings.
- **B039** — was already CLOSED (2026-09-04) before this unit started; the
  two-axis rule it asked for is stated at
  `refactor/K_component_extraction.md` §K13 corrections (ii). This unit is its
  first full application and adds the THIRD rule (deviation 2) to the same
  place.
