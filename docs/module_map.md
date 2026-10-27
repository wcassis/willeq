# Module Map

Post-Batch-F layout (2026-07-23), refreshed by T215 (2026-08-25), by J21
(2026-09-02) after Batch J's splits, and by K00 (2026-09-03, `3808ebe9`). The former
mega-files are split into domain translation units; classes are unchanged —
only method definitions moved. Line counts are `wc -l` at `3808ebe9`; they
drift with normal work. Ceiling: **1,500 is a FAIL**, not a warning —
`scripts/refactor/check_file_sizes.sh --all` (the `FileSizeCeiling` ctest
test) fails any file over it that is not in
`scripts/refactor/file_size_allowlist.txt`, and M00 emptied that list
(`docs/decisions.md` 2026-09-04). 3,000 stays as a second line of defence.
Only the diff/`--files` modes still report >1,500 as a WARN, which is what
`docs/decisions.md`'s 2026-07-22 entry describes. T385 (2026-09-05) widened
`--all` to `tools/` and `benchmarks/` as well, so the scan is genuinely
tree-wide; **every file under `src/`, `include/` and `tests/` is under 1,500**
— the grandfathered pair this map used to name (`irrlicht_renderer.cpp`,
`entity_renderer.cpp`) were split by J11 and J03, and `eq.h` (899) and
`irrlicht_renderer.h` (1,343) fell to Batches K and L. **`tools/` is clean too
and `FileSizeCeiling` is GREEN**: T387 closed board **B185**, so
`tools/hwcaps/zone_bench_loader.cpp` was 977 lines at that time and
`tools/hwcaps/gles2_zone_benchmark.cpp` 992, not the 3,280 and 3,060 earlier
revisions of this paragraph reported; `check_file_sizes.sh --all` reads
1,181 files / 0 warn / 0 fail (re-measured T451; 1,162 at T447, 1,129 at T444). Run
the checker, do not quote this paragraph. The pre-split cluster analysis this map replaced is preserved in git
history (docs/module_map.md @ commit a0c02cd..93a3c2a era) and in each unit's
review section under `refactor/F*.md` and `refactor/J*.md`.

## Game state — EverQuest class (`include/client/eq.h`)

`src/client/core/everquest.cpp` + `src/client/core/` (K14: the last flat
`eq_*.cpp` TUs) + `src/client/action/` + the extracted components under
`src/client/<family>/` (Batch K).

| TU | Lines | Contents |
|---|---|---|
| src/client/core/everquest.cpp | 1,152 | K14: moved from the old flat `eq.cpp` (git mv, no line change) plus `DumpEntityAppearance` (moved in from `eq_strings.cpp` -- it reads `m_entities`, not a pure lookup). Core after J02: ctor/lifecycle, session recording, DB/spell-system init, ConnectToLogin/World/Zone, TickNetwork, loading-phase state, PublishFullStateSnapshot, zone connection callbacks, ZoneOnPacketRecv, ReplayZonePacket, SafeQueueZonePacket, QueuePacket, Start/StopUpdateLoop, SetEQClientPath. T341: the loading-phase state machine and `SetConstrainedConfig` moved to `eqt::bridge::BridgeComponent` |
| src/client/core/zone_connect.cpp | 118 | B1072 (#392) + B1079 (#399): the bounded zone-IN connect, EVERY phase -- `EverQuest::OpenZoneConnection` (the zone UDP session, shared by `ConnectToZone` and the retry), `ZonePhaseBoundsMs` (the per-phase table the arm sites arm the watchdog with, built off `zone_connect_watchdog.h`'s constants), `TickZoneConnect` (ticked from `UpdateMovement`: walks the CURRENT loading phase against that table, one retry of the same ZoneServerInfo limited to `ZONE_CONNECTING`, WARN naming host:port, the stuck phase and the last opcode) and `AbandonZoneConnect` (a fresh world login: character select with the screen or the scenario opt-in, the configured character's Enter World headless); the pure deadline is `include/client/zone/zone_connect_watchdog.h` |
| src/client/action/eq_action_handler.cpp | 783 | K14: moved from the old flat `eq_action_handler.cpp` (git mv, no content change). `action::EqActionHandler` — the `IActionHandler` implementation wrapping `EverQuest` for `ActionDispatcher`/`CommandProcessor` (action-layer migration T99–T143; raid guards T179–T184; skill gate T159/T162) |
| src/client/movement/movement_component.cpp | 492 | K07: eqt::movement::MovementComponent -- Move/MoveTo/MoveToEntity, Follow/StopFollow, StartCombatMovement, Face/FaceEntity, StopMovement |
| src/client/bridge/bridge_component.cpp | 846 | K11: `eqt::bridge::BridgeComponent` — graphics lifecycle, the game-thread ticks, zone-load snapshot, BSP-tree install, the spawn/pet/movement bridge marshalling and the T269 bank-coin projection helpers |
| src/client/merchant/merchant.cpp | 689 | K05: `eqt::merchant::MerchantComponent` — inventory item movement, ScribeSpellFromScroll, the trade request/coin/accept/cancel senders, ReadBook and ZoneProcessTradePartnerItem, moved out of `class EverQuest` (was `eq_merchant.cpp`) |
| src/client/commands/commands_component.cpp | 1,123 | K12: `eqt::commands::CommandsComponent` — RegisterCommands driver + the game-registry remainder after T139 deleted 34 redundant commands (registry 1 of `docs/decisions.md` 2026-08-03), moved out of `class EverQuest` (was `eq_commands.cpp`) |
| src/client/zone/zone_component.cpp | 1,092 | K09: `eqt::zone::ZoneComponent` — zone change/teardown, CleanupZone, DisconnectFromZone, ProcessDeferredZoneChange, BeginGracefulZoneOut (T322), LoadZoneMap/Lines/Sources, CheckZoneLine, GetBestZ/FixZ, in-zone teleport (T231) |
| src/client/zone/zone_entry.cpp | 1,057 | K09: `eqt::zone::ZoneComponent` — ZoneProcessNewZone, ZoneProcessPlayerProfile (the LOAD-BEARING player heading flip), ZoneProcessAnimation, ZoneProcessGuildMOTD, CheckZoneRequestPhaseComplete |
| src/client/zone/zone_display_name.cpp | `wc -l src/client/zone/zone_display_name.cpp` | B1088 (#408): `eqt::ZoneDisplayNames`, the loading screen's zone-name VALUE source (device-free; header `include/client/zone/zone_display_name.h`) — the era table's long name via the ONE parser `LoadEraZoneLongNames()`, the server's `OP_NewZone zone_long_name` override, the stored "Entering <name>..." line read at draw time (`refresh()` / `noteNewZone()` / `clearEntry()`, INFO-logged) |
| src/client/zone/zone_dispatch.cpp | 962 | G01: the zone-stream opcode TABLE. `DispatchZonePacket` is a short lookup (`ZoneOpcodeTable()` → call, else `IgnoredZoneOpcodes()` → nothing, else the unhandled-opcode WARN), preceded since T482 by one `SpellManager::noteZonePacket(opcode)` call that settles a released cast's outcome before any arm runs. The 145 former `case` arms are 143 table entries — a captureless lambda each, calling either a sibling's `ctx_.<Family>().ZoneProcess*(p)` or one of the 39 named private `On*` handlers this file also holds — plus 2 deliberate no-ops in `kIgnoredOpcodes` with their reason (T469 moved `HC_OP_InterruptCast` from the ignored set to a handler; re-measured T486). `tests/goldens/zone_opcodes_g01.txt` + `tests/test_zone_dispatch_table.cpp` pin the arm set both ways |
| src/client/login_world/login_world_component.cpp | 1,004 | K10: eqt::login_world::LoginWorldComponent -- login-server + world-server connection handling (ConnectToLogin/ConnectToWorld, the Login*/World* handshakes, character select, chat-server info, zone handoff), eqcrypt_block |
| src/client/login_world/login_world_char_create.cpp | 251 | B888 (#204): `LoginWorldComponent`'s creation half -- `RequestCreateCharacter` (OP_ApproveName -> OP_CharacterCreate), `RequestRandomName`, `RequestDeleteCharacter`, the two reply handlers and the world-SCRIPT driver run off the character list (create -> CRC pair -> OP_EnterWorld, so world's StartInTutorial is cleared; delete; list) |
| src/client/login_world/char_create_rules.cpp / char_create_data.cpp / char_create_packets.cpp / char_create_faces.cpp / world_script.cpp | `wc -l src/client/login_world/char_create_{rules,data,packets,faces}.cpp src/client/login_world/world_script.cpp` | B888: the server's creation tables quoted from `CheckCharCreateInfoTitanium` + the point allocator + the name pre-check; `data/char_create.tsv` parsed with the era filter (post-Velious HIDDEN); the four packets byte for byte; the measured FacePick range TSV shared with the UI and the `--create-character` grammar |
| src/client/application_world_script.cpp | 50 | B888: `Application::applyWorldScript()` -- `--create-character` / `--delete-character` / `--list-characters` into `LoginWorldComponent::SetWorldScript()` |
| src/client/login_world/character_select.cpp / character_select_service.cpp / login_world_char_select.cpp | 145 / 98 / 94 | B998 (#318): the character-select screen's game side -- `CharacterSelectChannel` (the one mutex-guarded seam between the threads), `CharacterSelectScreenWanted()` (shown only with graphics, no script, and no configured character or `--character-select`), the rows' composed text ("Name [level Class]", the zone's long name from `data/era_zones.tsv`) and the eqstr ids to pre-resolve; `CharacterSelectService::tick()` (drain requests into the `Request*` bodies, publish the view, take the screen down once `OP_EnterWorld` is out); `LoginWorldComponent::EnterWorldFromSelect()`; B1005 (#324) `CampReturnsToCharacterSelect()`, `LoginWorldComponent::ReturnToCharacterSelect()` (the camp's fresh world login, the screen held until the fresh list) and `ReconnectWorld()` (shared with the zone-to-zone path) |
| src/client/application_char_select.cpp | 79 | B998: `Application::characterSelectWanted()` / `applyCharacterSelect()` (init step 70 of the G04 golden), `serviceCharacterSelect()` (game tick) and `renderCharacterSelect()` (the pre-zone slot of `Application::render()`, where the handshake loading screen is drawn) |
| src/client/application_quit.cpp / quit_signal.cpp | `wc -l src/client/application_quit.cpp src/client/quit_signal.cpp` | B1099 (#419): quit from every loading phase -- `Application::serviceQuitSignals()` (render thread: a latched SIGINT/SIGTERM -> `requestQuit()`), `serviceConnectFailure()` (game thread: the pre-zone connect bound gave up -> headless/`--scenario` quit, graphics keeps the message), `pollLoadingScreenQuit()` (the loading-screen frames pump the device; ESC / window close quits); `eqt::InstallQuitSignalHandlers()` (first signal latches, second chains to the device's keyboard restore, then dies) |
| src/client/login_world/login_connect_bound.cpp | `wc -l src/client/login_world/login_connect_bound.cpp` | B1099 (#419): `LoginWorldComponent::TickLoginConnect()` / `AbandonPreZoneConnect()` -- the pre-zone connect bound (`login_connect_watchdog.h`), armed on entry to LOGIN_CONNECTING / LOGIN_AUTHENTICATING / WORLD_CONNECTING, give-up = session torn down, "Could not connect to ..." on the loading screen, exit 3 |
| src/client/core/eq_strings.cpp | 699 | K14: split. Ten name/price lookups (class/race/deity/opcode/body-type/chat-type/equip-slot/npc-type, `FormatPrice`) and `DumpPacket` are free functions in namespace `eqt::strings` (`include/client/eq_strings.h`); `GetStringMessage`/`GetFormattedStringMessage`/`LoadStringFiles` became `eqt::StringDb`, the fourth context service, reached as `ctx_.strings` / `EverQuest::Strings()`. `DumpEntityAppearance` moved out to `src/client/core/everquest.cpp` (it reads `m_entities`, not a pure lookup) |
| src/client/player/player_misc.cpp | 747 | K08: `eqt::player::PlayerComponent` — assist/split/LFG/shielding/discipline/banker, save, duel, bindwound, track, cosmetic misc social, camera/mp3/sound handlers |
| src/client/player/player_component.cpp | 612 | K08: `eqt::player::PlayerComponent` — emote/AFK/anon/roleplay, rezz/who/inspect (+ OP_InspectAnswer reply, T170), corpse/consent; the T145 double-prefix senders SendWhoAllRequest/SendInspectRequest |
| src/client/player/camp_timer.cpp / player_camp.cpp | 85 / 153 | B1005 (#324): `CampTimer`, the measured /camp state machine (`camp_timer.h`: sit + OP_Camp u32 mode, eqstr 12293/12684/12290, OP_Logout at +30 s, the two cancels, where the server's reply lands); PlayerComponent's camp (StartCampTimer/CancelCamp/UpdateCampTimer/SendCamp/SendLogout/ZoneProcessLogoutReply) |
| src/client/core/memory_ledger.cpp | 565 | T491: the allocator-level memory ledger (`include/client/core/memory_ledger.h`, `eqt::memory`). THE ONLY TU that replaces the allocator entry points -- `malloc`/`free`/`calloc`/`realloc`/`memalign`/`posix_memalign`/`aligned_alloc`/`valloc`/`pvalloc`/`malloc_usable_size` and every `operator new`/`delete` overload -- with thin wrappers over glibc's `__libc_*` backends that charge each block's whole chunk to the calling thread's `MemoryScope` category. Also the lifecycle-point API (`recordPoint`, the `[memledger]` log line, `toJson`, `toReportText`, `gapToTarget`) that `/pmem`, `gles2_zone_benchmark`'s memory JSON, `scripts/ci/visual_gate.py` and `scripts/ci/memory_cap_hunt.sh` all read. Built into every binary when `-DEQT_MEMORY_LEDGER=ON` (the default); OFF replaces no symbol and `MemoryScope` is an empty struct. B1062 (#382): `callerIsGlStack()`'s Darwin `#elif` arm delegates to `memory_ledger_gpu_driver_classifier_darwin.cpp` (a one-line arm plus a same-line include, so the Linux preprocessed output is unchanged) |
| src/client/core/memory_ledger_gpu_driver_classifier_darwin.cpp | 210 | B1062 (#382): the Darwin arm of the ledger's `gpu_driver` caller classifier (T504's return-address classification, `memory_ledger.cpp`'s `#if defined(__linux__)` arm uses `dl_iterate_phdr`; Darwin has none). Same mechanism off dyld's image table: `_dyld_image_count()`/`_dyld_get_image_header()` fill a double-buffered sorted table with ONE range per image -- its `__TEXT` segment (slide applied; `__PAGEZERO` is a virtual guard range with no code and is never recorded; one-per-image because recording every non-`__PAGEZERO` segment overflows the 1,024-range table on the ~380-image set a benchmark loads). Image 0 (the main executable) is never a GPU image, the rest tested against the ANGLE dylibs (`libEGL`, `libGLESv2`) and Apple's GPU frameworks (Metal, IOSurface, CoreDisplay) -- the same `gpu_driver` category as Mesa on Linux. The hot path is one atomic table load + binary search (no `dladdr`, no `backtrace`, hook-safe by the Linux arm's argument); the table is populated by the first unknown-address rebuild and rebuilt, rate-limited to every 1024th hit, only on a later unknown address. `measureClassifierCostNs()` times the spec's cost A/B (`dladdr(pc)` vs `backtrace(8)` + `dladdr` per frame) once, printing its `[classifier-cost]` line -- the unit test calls it, never a hook; the measured figures are quoted in `docs/macos.md` ("What differs on Darwin"). Compiled and linked on Darwin only (`#if defined(__APPLE__)`); `tests/test_gpu_driver_classifier.cpp` (the two-platform test) links it on macOS and the host TU's `__linux__` arm on Linux |
| src/client/core/memory_ledger_b924.cpp | 55 | B924 (#243): the ledger's measurement devices, split out so `memory_ledger.cpp` stays under the 1,500-line ceiling. `noteRenderedFrame()` (called per frame from `renderer_frame.cpp`) counts rendered frames -- so a `[memledger]` point's `frames=` can say whether a category grows with FRAMES or with wall time -- and paces the render loop when `WILLEQ_FPS_CAP=<fps>` is set (off by default). `idleMemmapEnabled()` reads `WILLEQ_IDLE_MEMMAP=1` (the full `[memmap]` census at every idle point of `pollSteadyState()`). Both are measurement-only, never used by any gate; the acceptance net built on them is `scripts/b924/b924_net_leg.sh` + `tests/cmake/commands.cmake`'s `B924IdleHoldLive`/`B924IdleHoldCappedLive` |
| src/client/core/gpu_buffer_census.cpp | 355 | B955 (#273): the `gpu_buffers` ledger row (`include/client/core/gpu_buffer_census.h`) -- GPU buffer memory outside RssAnon (Lima GEM/shmem on the Orange Pi). Reads the DRM fds' fdinfo `drm-*` keys, the `/dev/dri` mappings in `/proc/self/maps`, and the process's own memory cgroup (`memory.stat` shmem when the process is alone in it), and decides the row's source; on a software rasteriser it reports the renderer's published tally, which already sits in `gpu_driver`. `snapshot()` fills `Snapshot::gpuBuffers`; `recordPoint()` prints the `[memgpu]` line |
| src/client/core/gl_driver_census.cpp | 217 | B946 (#264): the `[gldriver]` line's /proc + ELF half (`include/client/core/gl_driver_census.h`) -- which GL driver the process REALLY loaded, at GL init: the mapped libgallium/libLLVM out of `/proc/self/maps` (a SECOND, minimal maps parser, `scanMapsText` -- the `[memmap]` census reads `/proc/self/smaps`, a different file, for T491's fold) and the nollvm prefix out of the binary's own DT_RPATH (`/proc/self/exe`, `parseElfRpath`, fixture-tested). `glDriverInfo()` fills `GlDriverInfo`, `makeGlDriverLine()` the exact line the client logs once per process (the emission sits in `renderer_init.cpp`'s `glDriverInitLog()`); the rig's summarizer half is `scripts/pi/rendered_zone_summarize.py`'s `mesa_driver_field()`. B1016 (#337): `nollvmPrefixFromRpath()` returns the rpath entry AS-IS (the publish bakes in `<prefix>/lib`), so `mesaPrefixMissing()` tests the prefix's `lib/` directory itself -- a partial deploy that lost only `lib/` is caught instead of passing (the old code stripped `/lib` and tested the still-present parent, silently falling back to system Mesa); `gldriver_line_emit` (`tests/gldriver_line_emit.cpp`) exposes `makeGlDriverLine()` to the `GldriverLineFormatContract` ctest |
| src/client/core/eq_chat.cpp | 35 | K13: three forwarders (K14 moved the file, no content change). `EverQuest::AddChatSystemMessage`/`AddChatCombatMessage`/`AddChatMissMessage` call `src/client/chat_output.cpp`; the receiver is kept for the callers outside the EverQuest family (action layer, spell/skill managers, `src/client/application_commands.cpp`) |
| src/client/chat_output.cpp | 67 | K13: `eqt::ChatOutput` (`include/client/chat_output.h`) — the three chat output primitives (system / combat / combat-miss), the context service every component reaches as `ctx_.out`. Publishes `SystemMessage`/`ChatMessage` through the bridge and logs to console |
| src/client/player/player_buffs.cpp | 613 | K08: `eqt::player::PlayerComponent` — task/DZ, spell-cast, mana, buff and pet-buff handlers, poison, bind wound, track |
| src/client/player/player_state.cpp | 547 | K08: `eqt::player::PlayerComponent` — SetCombatStopDistance (SetMoveSpeed deleted in B603: its only callers were tests; the live writer is the state's own `setMoveSpeed()` from movement_state.cpp), PlayerStateAdd/Remove, Death, Stamina, ZonePlayerToBind, LevelUpdate, tradeskill combine. The position/heading/moving and AFK/anon/roleplay/camping accessors stayed on `class EverQuest` in `src/client/core/eq_player_info.cpp` |
| src/client/commands/combat_actions.cpp | 534 | K12: `eqt::commands::CommandsComponent` — Consider/Action/Damage/MoneyOnCorpse/Stun handlers, Bind_Wound reply (T177). T268 deleted the headless ZoneProcessLootItem parser; moved out of `class EverQuest` (was `eq_combat_actions.cpp`) |
| src/client/commands/commands_spells.cpp | 495 | K12: `eqt::commands::CommandsComponent` — RegisterSpellCommands (spell/skill/hotkey/detail/audio commands), moved out of `class EverQuest` (was `eq_commands_spells.cpp`) |
| src/client/core/eq_player_info.cpp | 253 | K13: 46 forwarders (K14 moved the file, no content change). Every player-state accessor on `class EverQuest` now calls `src/client/player_facade.cpp`; the receiver is kept for the callers outside the EverQuest family (combat, the spell/skill/inventory managers, the action layer, the UI) |
| src/client/player_facade.cpp | 279 | K13: `eqt::PlayerFacade` (`include/client/player_facade.h`) — the player-state read/write surface over `eqt::state::GameState`, the context service every component reaches as `ctx_.self`. Stat/currency/weight getters, position/heading/moving, AFK/anon/roleplay/camping, group accessors; the header also carries the T288 position/spawn-id/zone-identity accessor block (`PosX`/`PosY`/`PosZ`/`HeadingDeg`/`SetPosLocal`/`SetHeadingLocal`/`MySpawnId`/`SetMySpawnId`/`CurrentZoneName`/`CurrentZoneId`), `IsFullyZonedIn` and the vendor/bank/trainer window predicates |
| (header only) `include/client/lifecycle_facade.h` | 76 | T341 (B038 cluster 3): `eqt::LifecycleFacade` — the fifth context service, reached as `ctx_.lifecycle`. An AGGREGATE of seven bound `std::function`s over `EverQuest`'s own lifecycle entry points (`RequestQuit`, `TickNetwork`, `Start`/`StopUpdateLoop`, `OnGameStateComplete`, `ConnectToZone`, `SetSneak`) plus references to the three lifecycle flags (`m_update_running`, `m_pending_logout_disconnect`, `m_zone_session_established`). It holds no bodies — the header says why |
| src/client/packet_sender.cpp | 133 | K13: `eqt::PacketSender` (`include/client/packet_sender.h`) — the zone-stream outbound path, the context service every component reaches as `ctx_.net`. `Queue` (opcode + body framing, session recording, the main-thread/async split) and `SafeQueueZone` (the guarded `EQ::Net::Packet` path); owns the Batch R suppressed-send counter and the T268 queued-packet capture seam |
| src/client/player/player_info.cpp | 186 | K08: `eqt::player::PlayerComponent` — tribute/AA handlers (SendAATable/RespondAA/TributeInfo/SendGuildTributes), exp/zonein, world-objects-sent, SyncEntityToGameState/RemoveEntityFromGameState |
| src/client/zone/zone_handshake.cpp | 320 | K09: `eqt::zone::ZoneComponent` — the ZoneSend* handshake senders (ZoneEntry, ReqClientSpawn, ClientReady, SetServerFilter, StreamIdentify, Ack, ReqNewZone, AA/tribute requests, SendExpZonein). ZoneSendSessionReady and ZoneSendSpawnAppearance were deleted, zero callers |
| src/client/commands/commands_movement.cpp | 321 | K12: `eqt::commands::CommandsComponent` — RegisterMovementCommands (movement/combat/utility-debug/sky commands), moved out of `class EverQuest` (was `eq_commands_movement.cpp`) |
| chat_component.cpp (in `src/client/chat/`) | 607 | K03: `eqt::chat::ChatComponent` — channel message send/process, `SendChatMessage`, `ProcessChatInput`, `DispatchSlashCommand` (registry 1 entry point), emote send/process, colored/formatted/simple text, and `ChatChannelName()` (T320), moved out of `class EverQuest` (was `src/client/core/eq_chat.cpp`) |
| group_component.cpp (in `src/client/group/`) | 960 | K04: `eqt::group::GroupComponent` — group invite/update/disband, raid (T169–T184: OP_RaidUpdate decode, raid senders), pet accessors/commands, moved out of `class EverQuest` (was `eq_group.cpp`) |
| guild_component.cpp (in `src/client/guild/`) | 276 | K01: `eqt::guild::GuildComponent` — the guild senders + processors, moved out of `class EverQuest` (was `eq_guild.cpp`; m_guild_id populated T133) |
| gm_component.cpp (in `src/client/gm/`) | 317 | K02: `eqt::gm::GmComponent` — the GM command senders + processors, moved out of `class EverQuest` (was `eq_gm.cpp`; wired T148) |
| src/client/commands/commands_train.cpp | 265 | K12: `eqt::commands::CommandsComponent` — T280: `/train` and `/trainclose` game-registry side, moved out of `class EverQuest` (was `eq_commands_train.cpp`) |
| src/client/commands/commands_containers.cpp | 170 | B899 (#215): `eqt::commands::CommandsComponent` — the carried container window's game side: `/bag` open/close (`RequestToggleBag`), the close intent (`CloseCarriedContainer`) and the bag-left-its-slot close (`OnInventorySlotChanged`, called from `MerchantComponent::SyncInventorySlotToGameState`) |
| src/client/graphics/ui/container_panel.cpp | 285 | B899 (#215 / #151 Leg B): the era container window (`EQUI_Container.xml`, geometry in `container_layout.h`) — `ContainerWindowsState`, its appliers, draw and hit tests, world container and carried bags alike |
| src/client/graphics/ui/container_input.cpp | 98 | B899: `StaticUIInput::processContainerInput()` / `processContainerEscape()` and the container-cell click, routed through B442's shared `slotClick()` / `CursorGesture` |
| src/client/graphics/ui/trade_panel.cpp | 323 | B896 (#212): the trade window (`EQUI_TradeWnd.xml`, `trade_layout.h`) and the NPC give window (`EQUI_GiveWnd.xml`, `give_layout.h`) — `TradePanelState`, its appliers (the seven Trade* events and the 3000..3007 slots), draw, hit test and the measured red/green name rule |
| src/client/graphics/ui/trade_input.cpp | 101 | B896: `StaticUIInput::processTradeInput()` — an own-slot click through B442's shared `slotClick()` / `CursorGesture` (slot 3000+n, held item into an empty slot only), the coin rows, Trade/Give and Cancel as `TradeWindowIntent` |
| src/client/graphics/ui/item_tooltip.cpp | 231 | B1004 (#323): the item-name tooltips — the Alt-held labels over a bag or the bank (every occupied slot, name only, left column at the slot's (+35,+1), right (+35,+21)) and the hover tooltip (600 ms dwell, bottom edge on the pointer); `resolveTooltipTarget()` finds the window and slot under the pointer topmost first. Measured rule: `review/UIGAP-ORACLE/323/` |
| src/client/bridge/bridge_component_trade.cpp | 151 | B896 (#212): `BridgeComponent::RunTradeWindowIntent()` (the window's buttons and coin rows onto `TradeManager`), `TradeGestureRefusal()` / `AfterTradeGesture()` (the cursor gesture's trade-slot rules: no withdrawal, our change resets both accepts) and `OfferHeldItemOnTradeOpen()` (the held item into 3000 when our request is acknowledged). The `TradeWindowIntent` handler stays in `bridge_component_intents.cpp` |
| src/client/player/profile_appearance.cpp | 20 | PlayerProfile appearance fields stored so `/appearance` merges faithfully (T167); header `include/client/player/profile_appearance.h` |
| src/client/action/eq_action_handler_extended.cpp | 957 | J12: the `EqActionHandler` remainder — GM/debug, camp/logout, report, world-object and tradeskill actions. K14 moved the file (one call-site retarget: `FormatVendorPrice` inlined to `eqt::strings::FormatPrice`, and `GetClassName` re-qualified to `eqt::strings::GetClassName`) |
| src/client/action/eq_action_handler_spells_trade.cpp | 570 | J12: `EqActionHandler`'s spell, pet, trade and `/appearance` actions (SendFaceChange's producer, T153). K14 moved the file, no content change |
| src/client/bridge/bridge_component_intents.cpp | 803 | K11: `eqt::bridge::BridgeComponent::ProcessBridgeIntents` and `ProcessBridgeEvents`. **G02**: the pump is now pure dispatch (47 lines) — a `std::visit` overload set with one canonical line per `RendererIntent` alternative, calling one private `on<Alternative>` handler each (36 of them, bodies verbatim from the old `is_same_v` arms). No generic fallback lambda, so a new alternative without a handler is a compile error; `tests/test_bridge_intent_table.cpp` + `test_reachability_sweep.cpp` pin the arm/declaration/definition triple |
| src/client/bridge/bridge_component_social.cpp | 26 | B897 (#213): `BridgeComponent::SyncSocialLists()`, the per-GameTick friends/ignore load/save (`state::SocialListsSync`) and the ONE publisher of `GameEventType::SocialListsChanged`. The `SocialListIntent` handler stays in `bridge_component_intents.cpp` (the intent-table tests pin every handler there) |
| src/client/state/social_lists.cpp / social_lists_io.cpp | 370 / 102 | B897 (#213): `state::SocialListsModel` (`GameState::social()`), the oracle's eqstr strings, the command bodies, the `<Char>_<server>.ini` [Friends]/[Ignored] shape; `_io` is the cache-dir read/write and the sync tick, kept apart so the command-layer test binaries link no disk code |
| src/client/bridge/bridge_component_ui_audio.cpp | 710 | K11: `eqt::bridge::BridgeComponent` — inventory stat sync, the loot window, per-client config / hotbar / inspect-message persistence, the audio lifecycle and hotbar slot activation |
| src/client/merchant/merchant_trade.cpp | 715 | K05: `eqt::merchant::MerchantComponent` — the trade/loot/vendor/bank/tradeskill Setup*Callbacks, the loot transfer handlers and the shop request/buy/sell/money/end handlers (was `eq_merchant_trade.cpp`, J13) |
| src/client/merchant/merchant_vendor_bank.cpp | 633 | K05: `eqt::merchant::MerchantComponent` — the vendor window packet + open/buy/sell/close senders, the bank window, the GM trainer (owner of the moved `m_trainer_skill_caps`) and the Phase 4 tradeskill recipe senders/handlers (was `eq_merchant_vendor_bank.cpp`, J13) |
| src/client/merchant/merchant_trainer.cpp | 139 | B898 (#214): `MerchantComponent::PublishTrainerWindow()` (the `TrainerWindowOpened` builder, moved out of `ZoneProcessGMTraining`; re-called by the `OP_SkillUpdate` arm to refresh an open trainer window) and `TrainRefusal()`, the one body for the local train refusals shared by `/train` and the window's Train button |
| entities_component.cpp (in `src/client/entities/`) | 905 | K06: `eqt::entities::EntitiesComponent` — zone spawns, time of day, spawn doors, zonepoints, spawn appearance, moved out of `class EverQuest` (was `eq_entities.cpp`) T486: the ZoneSpawns parser sets `Entity::is_corpse` from the Spawn_Struct NPC byte through `IsCorpseNpcType()` — the flag's ONE owner (with the NewSpawn parser in `_updates`) |
| entities_component_appearance.cpp (in `src/client/entities/`) | 668 | K06: `eqt::entities::EntitiesComponent` — teardown/health/appearance cluster (DeleteSpawn, MobHealth, HPUpdate, ListEntities, WearChange, ApplyAppearanceToGameState, UpdatePlayerAppearanceFromInventory, Illusion), moved out of `class EverQuest` (was `eq_entities_appearance.cpp`; entity size T318) |
| entities_component_updates.cpp (in `src/client/entities/`) | 807 | K06: `eqt::entities::EntitiesComponent` — ground spawns, weather, new spawns, exp updates, ClientUpdate interpolation, moved out of `class EverQuest` (was `eq_entities_updates.cpp`) T486: the NewSpawn parser sets `Entity::is_corpse` through `IsCorpseNpcType()` (see `entities_component.cpp`); `tests/test_corpse_death_pose.cpp` fails on a parser that stores `npc_type` without it |
| entities_component_world.cpp (in `src/client/entities/`) | 406 | K06: `eqt::entities::EntitiesComponent` — doors, spawn-appearance/animation/click-door senders, world-object (tradeskill container) senders and handlers, moved out of `class EverQuest` (was `eq_entities_world.cpp`) |
| src/client/movement/movement_state.cpp | 896 | K07: MovementComponent -- pathfinding (LoadPathfinder/FindPath/FollowPath), SetMovementMode / SetPositionState, jump, movement history, water and region state, keyboard control |
| src/client/movement/movement_update.cpp | 850 | K07: MovementComponent -- UpdateMovement (the per-tick pump), SendPositionUpdate and the wire packing (wire z = groundZ + 0.625*size, T311) |

### Family grouping (source: `refactor/K_component_extraction.md` §Families)

The flat `eq_*.cpp` split names families that Batch K extracts into components.
This table and that plan must agree; change both together. Batch K extracted
twelve components, one `include/client/<family>/<family>_component.h` header
each; the last two rows below (`core` and `action_handler`) are NOT components
and have no such header. The count is netted by the `DocCounts` test (T373).

| family | TUs | `EverQuest::` defs | single-family members | K unit |
|---|---|---|---|---|
| guild | `guild/guild_component` | 11 | 3 | **K01 DONE** (pilot; `ClientContext` in `include/client/client_context.h`) |
| gm | `gm/gm_component` | 12 | 2 | **K02 DONE** |
| chat | `chat/chat_component`, `core/eq_chat` (AddChat* only), `core/eq_strings` | 13 | 1 | **K03 DONE**, **eq_strings split by K14** (10 of 13 defs moved; the three AddChat* output primitives stay on `EverQuest`; `eq_strings` split into `eqt::strings` free functions + `eqt::StringDb`, K14) |
| group | `group/group_component` | 23 | 5 | **K04 DONE** (group, raid and pet together; `DismissPet`/`UpdateGroupMemberFromEntity` deleted, zero callers) |
| merchant | `merchant/merchant`, `_trade`, `_vendor_bank` | 54 | 1 | **K05 DONE** (54 defs, not the 56 the batch table claimed; all 54 moved. `m_trainer_skill_caps` moved; the loot/scribe members stay on `EverQuest` pending K09/K11; `m_vendor_items`/`GameVendorItem` moved to `CommandsComponent` by K12, the last family reading it -- reached from Merchant's writers via `owner_.Commands().VendorItems()`) |
| entities | `entities/entities_component` (+ `_appearance`/`_updates`/`_world`) | 28 | 1 | **K06 DONE** (`m_entities`, the spawn-id -> Entity map, stays a core member — the component owns the handlers) |
| movement | `src/client/movement/movement_component`, `movement_state`, `movement_update` | 42 | 8 | **K07 DONE** (`eqt::movement::MovementComponent`; `HasReachedDestination` deleted, zero callers; `m_last_movement_update` boarded dead) |
| player | `src/client/player/player_component`, `player_misc`, `player_state`, `player_buffs`, `player_info`, `profile_appearance`; facade in `src/client/core/eq_player_info.cpp` | 139 | 11 | **K08 DONE** (139 defs, not the 142 the batch table claimed; 93 moved, the 46-body player-state facade stayed on `EverQuest`) |
| zone | `src/client/zone/zone_component`, `zone_dispatch`, `zone_entry`, `zone_handshake` | 34 | 39 | **K09 DONE** (`eqt::zone::ZoneComponent`; `DispatchZonePacket` moved whole; `ZoneSendSessionReady`/`ZoneSendSpawnAppearance` deleted and `m_zone_sequence` boarded dead; the zone connection itself — `ConnectToZone`, the `ZoneOn*` callbacks, `ReplayZonePacket`, `SafeQueueZonePacket` — stays in `src/client/core/everquest.cpp`) |
| login_world | `src/client/login_world/login_world_component` | 37 | 22 | **K10 DONE** (`eqt::login_world::LoginWorldComponent`; both connection managers moved, `ConnectToLogin`/`ConnectToWorld` moved in from `src/client/core/everquest.cpp`; `WorldSendSessionReady` and the two `World*ChannelMessage` declarations deleted, zero callers) |
| bridge | `src/client/bridge/bridge_component`, `bridge_component_intents`, `bridge_component_ui_audio` | 45 | 14 | **K11 DONE** (`eqt::bridge::BridgeComponent`; the loot cluster and the T171 inspect message moved in; `SaveEntityDataToFile`, `PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`, `PlayUISound` and seven dead inline accessors deleted, zero callers; FIVE sanctioned lifecycle forwarders — `setBridge`, `InitGraphics`, `ShutdownGraphics`, `GameTick`, `PostRenderTick` — stay on `EverQuest`, defined in `src/client/core/everquest.cpp`) |
| commands + combat | `src/client/commands/commands_component`, `_movement`, `_spells`, `_train`, `combat_actions` | 28 | 2 | **K12 DONE** (`eqt::commands::CommandsComponent`; combat folded in, per the batch's revised note; `m_stunned_until` moved single-family, `m_vendor_items`/`GameVendorItem` moved in from `EverQuest` -- K05 had left it there naming K12 as the last family reading it; `runLoadDiagnostics` has zero callers but is kept, private -- it is the sole publisher of `DiagnosticsSceneDump`, still consumed by `irrlicht_bridge.cpp`) |
| core | `core/everquest` | 38 | 3 | stays — ctor/dtor, wiring, `Test_*` seams. K14 moved `eq.cpp` to `src/client/core/everquest.cpp` |
| action_handler | `action/eq_action_handler*` | 19 | 0 | K14 moved into `src/client/action/`; retarget onto components is a later measured unit |

## Application (`include/client/application.h`) — split by J06

| TU | Lines | Contents |
|---|---|---|
| src/client/application.cpp | 1040 | Core: initialize, run, shutdown, requestQuit, gameThreadLoop, mainLoop. **G04**: `initialize()` is a 26-line ordered list of 17 `bool initX(const ApplicationConfig&)` subsystem steps (fp-cam overrides, config validation, frame-dump dir, required files, signal handlers, EverQuest client, spell system, action layer, scripted tour, game mode, input bridge, command-output routing, graphics [constrained config + renderer/InitGraphics], console bridge, login connection). The order is load-bearing and pinned by `tests/test_application_init_order.cpp` against `tests/goldens/application_init_order_g04.txt` |
| application_commands.cpp | 552 | processNetworkEvents/processInput, processPreConnectionLifecycleCommands, loadScriptFile/updateScript, dispatchToActionOrRenderer (the renderer-catalog gate), printCommandResult, wireUnifiedCommandDispatch, `Application::DispatchSlashCommand` |
| application_zone_loading.cpp | 401 | start/joinLoadingThread, checkLoadingComplete, loadZoneGraphicsOnThread (registers snapshot entities into the EntityRenderer), setupRDPAudio, updateLoadingProgress. T470 (B622) deleted the uncalled synchronous `loadZoneGraphics()`. B917: the loader reads its own `m_activeLoadSnapshot`, taken out of `eqt::ZoneLoadHandoff` |
| application_config.cpp | 434 | parseArguments, loadConfigFile |
| application_frame.cpp | 224 | updateGameState, render |

The zone-load handoff between the game thread and the render thread (B917) is
`eqt::ZoneLoadHandoff` in `src/client/zone_load_handoff.cpp`
(`include/client/zone_load_handoff.h`): post / take / finishLoad /
takeApplicableCompletion, plus the B918 loader-failure record main() turns into
exit code 4. Device-free; `tests/test_zone_load_handoff.cpp` drives it. Its
B918 sibling `src/client/fatal_exit.cpp` (`include/client/fatal_exit.h`) is the
process-wide record for the other FATAL-then-quit sites (`LoadZoneSources()`).

Steps 1-17 above are all defined in `application.cpp` itself and therefore
inlined by the G04 golden. The 18th, **`initScenario()`** (T410), is defined in
`src/client/scenario/scenario_application_wiring.cpp` instead, so the golden
pins its POSITION (after the console bridge, before the login connection) but
not its statements -- the scenario lane is opt-in machinery and an ordinary run
constructs nothing in it.

## Scenario lane (`include/client/scenario/`) — added by T410 (B138/B139)

Live verification with a process exit code. See CLAUDE.md's "The scenario
lane" for the taxonomy and how it hangs off `AutomatedMode`'s per-tick hook.

| TU | Lines | Contents |
|---|---|---|
| src/client/scenario/scenario_runner.cpp | 126 | `ScenarioRunner::Tick`, the pre-flight (zoned-in or exit 3), the timeout, `ScenarioExitCodeName`; B1005 `Conclude()` (the scenario's one last look after a stop the runner did not cause) |
| src/client/scenario/scenario_registry.cpp | 47 | `MakeScenario` / `ScenarioNames` / `IsScenarioName` — the ONLY place the three names are listed |
| src/client/scenario/scenario_zone_in.cpp | 45 | `zone_in` |
| src/client/scenario/scenario_two_client_survive.cpp | 105 | `two_client_survive` (peer arrives, both hold) |
| src/client/scenario/scenario_cast_lands.cpp | 131 | `cast_lands` (T401's prep recipe, gem cast, buff landing) |
| src/client/scenario/scenario_create_character.cpp | 76 | `create_character` (B888): with `--create-character`, the created character is in world, in one of its home city's zones, never a tutorial zone |
| src/client/scenario/scenario_camp.cpp / scenario_world_app_camp.cpp | 205 / 42 | B1005 (#324): `camp_to_charselect` (/camp, no zone-out before the countdown, the character-select state on a FRESH list, Enter World again) and `camp_desktop` (stops only after the countdown and the server's reply, judged in `OnClientStopped`); the world's `CampPhase` / `AtCharacterSelect` / `CharacterListNames` / `EnterWorldAs` |
| src/client/scenario/scenario_zone_connect_timeout.cpp | 146 | B1072 (#392): `zone_connect_timeout` -- /camp to character select, the dead-zone lever for two connects (`SimulateUnreachableZoneConnects`), Enter World, back at character select after exactly two connects and no sooner than two bounds, Enter World again and zone in; the world's `ZoneConnectOpens` / `SimulateUnreachableZoneConnects` live in scenario_world_app_camp.cpp |
| src/client/scenario/scenario_world_app.cpp | 72 | `ApplicationScenarioWorld` — the only `IScenarioWorld` that touches `EverQuest`; game-thread only |
| src/client/scenario/scenario_application_wiring.cpp | 91 | `Application::initScenario` + the three result accessors |

## Slash commands — action::CommandProcessor — split by J04 (+ T268/T269/T280)

| TU | Lines | Contents |
|---|---|---|
| command_processor_social.cpp | 798 | Chat, target/attack, group/raid, duel, trade, who/inspect, guild |
| command_processor_registration.cpp | 1015 | G03: `registerBuiltinCommands()` is now 22 lines — the ORDERED list of 21 per-family `registerXCommands()` calls that hold the same **158** `registerCommand({…})` calls (T347 inserted `/cleartarget`, B897 `/friends` and `/ignore`, B899 `/bag`, B442 `/pickup` `/putdown` `/cursor` `/coin`, B1140 `/autoinventory`), the whole registry-2 vocabulary. Order is load-bearing (a later registration shadows an earlier name/alias, J04/T097) and is pinned by `tests/test_command_registration_order.cpp` against `tests/goldens/command_registration_order_g03.txt` |
| command_processor_spells_gm.cpp | 713 | Cast/mem/gems/buffs/spellbook, pet, stance and movement modes, logging controls, track/find, the GM family, /appearance |
| command_processor_world.cpp | 490 | Corpse/consent, world objects, tradeskill recipes, vendor/buy/sell, equip/read/scribe, loc/sit/stand/move/warp/follow, quit/camp, debug/perf/time |
| src/client/action/command_processor.cpp | 284 | Core: processCommand/processInput, registerCommand, lookup/completion/category queries, parsing helpers, message sinks |
| command_processor_bank.cpp | 158 | T269: `/bank` and its coin sub-commands |
| command_processor_loot.cpp | 131 | T268: the loot-window commands |
| command_processor_friends.cpp | 53 | B897 (#213): `/friends` (`/friend`), `/ignore`, `/friendswho` -- thin callers of `include/client/state/social_lists.h`, the same bodies the Friends window's `SocialListIntent` handler runs |
| command_processor_train.cpp | 43 | T280: `/train`, `/trainclose` |
| command_processor_internal.h | 60 | J04 private header: the shared statics/helpers the four split TUs need |

## Inventory — InventoryManager — split by J01

| TU | Lines | Contents |
|---|---|---|
| inventory_cursor.cpp | 887 | Cursor pickup/place, partial stacks, cursor money, scribe scroll, swap/move, equip validation, `/autoinventory` (B1140 `autoInventoryCursor`) |
| inventory_manager.cpp | 581 | Core: slot get/set/remove, item cache, per-container accessors, stat/weight calculation, destroy |
| inventory_item_parser.cpp | 503 | The wire item-blob parser |
| inventory_bags.cpp | 445 | Bag/world-container/bank-bag placement rules, lore conflicts, bag contents, the `/autoinventory` slot choice (B1140 `findAutoInventorySlot`, measured on the oracle) |
| inventory_packets.cpp | 300 | processCharInventory, processItemPacket, move/delete responses (B1140: a server OP_DeleteItem takes one unit off a stack), dumpInventory |
| inventory_constants.cpp | 125 | Slot constants and name tables |

## Spells — SpellManager (`include/client/spell/spell_manager.h`) — split by T407

`SpellManager` is a plain class, not an extracted `EverQuest` component: there
is no `ClientContext`, no `owner_` handle and no friendship. Its TUs are
therefore a straight split of one class's bodies — each re-opens `namespace EQ`
and defines `SpellManager::` methods, exactly as T246's `spell_cast_checks.cpp`
already did. The header is a single declaration list; each of its banners names
the TU that defines the bodies under it.

| TU | Lines | Contents |
|---|---|---|
| spell_manager.cpp | 389 | Core: ctor/dtor, `initialize`, the per-frame `update` (+ `updateGemCooldowns`, `updateMemorization`; its duration + 5 s branch is a last resort that ends a cast as Interrupted with a WARN, never as a success — T482), the cast state machine's own transitions — `beginCastFromGem`, `interruptCast`, `getCastProgress`, `getCastTimeRemaining`, `completeCast`. The player-reachable path starts here: gem click / `/cast` -> `beginCastFromGem()` -> `validateCast()` |
| spell_gems.cpp | 350 | The gem bar and the spellbook — one storage family (`m_spell_gems`, `m_gem_states`, `m_gem_cooldown_start`/`_duration`, `m_memorize_*`, `m_spellbook`): `memorizeSpell`, `forgetSpell`, the six gem accessors, `calculateMemorizeTime`, `findGemSlotForSpell`, `startGemCooldown`, `scribeSpell`, `hasSpellScribed`, `getSpellbookSpell`, `findFreeSpellbookSlot`, `getScribedSpells`, `setSpellbook`, `setSpellGems`, `applyServerGemUpdate` (T404's server-driven gem edit) |
| spell_packet_handlers.cpp | 520 | The inbound-packet arms the zone dispatcher calls — `handleBeginCast`, `handleAction`, `handleInterrupt` (T469), `handleManaChange`, `handleSpellBarRefresh` (T482, `OP_MemorizeSpell` scribing 3) and `noteZonePacket` (T482) — with T469's `endOwnCastByServer` and T482's `releaseConfirmedCast` / `resolvePendingOutcome` / `publishOwnCastReleased`: how the server ends our own cast. Plus `showSpellMessages` / `showBeginCastMessage`, whose only callers they are. T357's deleted spell-land hook sat inside `handleAction()`; `SpellLandHookIsGone` in `tests/test_spell_manager_timing.cpp` reads this TU (and every sibling) as text to keep it gone |
| spell_cast_validation.cpp | 222 | The pre-cast decision layer: `adjustTargetForSpellType`, `isValidCreatureTarget`, then `validateCast` and the three checks it owns — `checkMana`, `checkEndurance` (T355), `checkRange` |
| spell_cast_checks.cpp | 120 | T246: the other pre-cast checks — `checkLineOfSight`, `checkPlayerState`. Each is permissive wherever the client has no state to judge from. No reagent check: B1140 measured the official client sending a cast with its component carried nowhere (the server judges components at cast end) and removed T246's `checkReagents`/`countCarriedItem` |
| spell_publish.cpp | 148 | Everything that leaves the class: the three outbound packets (`sendCastSpellPacket`, `sendInterruptPacket`, `sendMemorizePacket`) and the three bridge publishers (`publishCastStarted`, `publishCastComplete` — T44 — and `publishGemChanged` — U06b), each of which also does T221's source-side `GameState` write first |

Beside it in `src/client/spell/`, the spell VISUAL data readers (no `SpellManager` bodies):

| TU | Lines | Contents |
|---|---|---|
| spells_new_edd_reader.cpp | 123 | B1145: `EQ::SpellsNewEddReader` -- the one reader of spellsnew.edd's 416-byte entries (magic, entry bytes, name, texture, frame count) and the timing / emission words per entry (`SpellsNewEddTiming`: +0x78 duration, +0x7C lifetime, +0x80 burst, +0x84 per tick, +0x88 ticks/s, +0x8C delay) and, B1149, the sprite-size words (`SpellsNewEddSprite`: +0xCC/+0x184 width, +0x17C/+0x180 height, +0x16C, +0x18C, +0x6C, +0x98/+0x9C/+0x190/+0x194); `SpellsNewEffReader` (`edd()`) and the colour table read through it. The timing rules are header-only in `include/client/spell/spell_fx_timing.h` (`SpellFxSlotTimingFor`, `SpellFxCallerDurationMs`, B1148's `SpellCastAnimationCode` -- eqgame's CastingAnim id -> track code table -- and the measured fallback `kSpellFxCastAnimationSeconds`); the caster's track length comes from `CastAnimationTrackMs()` (`spell_fx_bone_anchor.h`) |
| spells_new_edd_colour.cpp | 87 | B1146: `EQ::SpellsNewEddColourTable` -- the colour words (+0x68 additive, +0x90/+0x94 fades, +0xA4 peak, +0xD4..+0xE8 start/end RGB; `docs/spellsnew_format.md`) decoded per emitter, read through `SpellsNewEffReader`/`SpellsNewEddReader` (B1145), `stageColours()` in `SpellsNewEffReader::stageTextures()` order; the alpha formula `eddParticleAlpha()` is inline in the header, shared with `UnifiedParticle::eddAlpha()` |

## Renderer — IrrlichtRenderer class (`include/client/graphics/irrlicht_renderer.h`)

K16 (board B006): `IrrlichtRenderer` now implements
`EQT::Graphics::IZoneLoadSurface` (`include/client/graphics/zone_load_surface.h`,
37 lines) — the two-method zone-load surface, `initLoadingScreen(const
RendererConfig&)` and `initDeferredGraphics()`. That is the whole of what
`BridgeComponent::InitGraphics` ever did with the renderer pointer, so
`EverQuest::InitGraphics` / `BridgeComponent::InitGraphics` take the interface
and the game side names no renderer class. It is the class's only base.

`src/client/graphics/renderer/` (the class's own four TUs, moved there by L12,
plus T460's `software_cursor_texture.cpp` -- a free function, not a class body --
and B998's `renderer_pre_zone.cpp`, one more `IrrlichtRenderer` body)
plus the eleven components Batch L extracted, each in its own directory.

Batch L (`refactor/L_renderer_component_extraction.md`) is doing to
`IrrlichtRenderer` what Batch K did to `EverQuest`. L01 created
`include/client/graphics/renderer_context.h` (284 lines) —
`EQT::Graphics::RendererContext`, the renderer's counterpart to
`include/client/client_context.h`. L12 (the close-out) widened it to **54 core
members + 44 promoted shared members + 11 sibling-component pointers**: the
promoted set is every `IrrlichtRenderer` member the census measured at three or
more components, and the pointers are what `owner_.<Family>()` became
(`ctx_.<Family>()`, set by `SetComponents()` after the last `make_unique`).
L12 also removed `RdpComponent`'s `IrrlichtRenderer&` (zero reaches on both
census axes) and `SlashCommandsComponent`'s `friend` line. The residue was
boarded as **B050**.

**L13 (`refactor/L13_pairwise_ownership.md`) closed B050's clusters 1, 2, 3 and
5.** The `owner_.` residue across the eleven components fell **1,065 -> 151
(-86%)** and `irrlicht_renderer.h` fell **1,556 -> 1,343**. Three mechanisms:

1. **Four nested types were hoisted out of the class** so the members they type
   could finally be promoted — `FireGlowLight`, `RendererZoneLightData` (renamed
   from `ZoneLightData`: two other structs of that name exist, so a namespace-
   scope `EQT::Graphics::ZoneLightData` would silently SHADOW `EQT::ZoneLightData`
   for unqualified uses inside `EQT::Graphics`), `DoorCollisionData` and
   `FrameTimings` went to `renderer_types.h`; `RenderPassTimer`, a class with a
   virtual base, went to its own `include/client/graphics/render_pass_timer.h`
   and `IrrlichtRenderer` gained `friend class RenderPassTimer` (as a nested
   class it had that access implicitly). `class IrrlichtRenderer` keeps a
   `using` alias for each, so every existing spelling still compiles. The four
   members then promoted into `RendererContext` on L12's unchanged `>= 3` rule:
   `fireGlowLights`, `zoneLightData`, `renderPassTimer`, `doorCollisionData`.
   `RendererContext` is now **54 + 48 promoted + 11 pointers**.
2. **Pairwise ownership.** Batch K Mechanism 3's "the last family moves it" was
   applied to two-component members: a member moves to the component that
   WRITES it, or on a tie to the one with more uses, and the other side reaches
   it through `ctx_.<Owner>().<accessor>()` — a `const T&` getter where nothing
   outside the owner writes, a `T&` where something does. **69 members moved
   into nine component headers**, `frameTimings_`/`frameTimingsAccum_` (335
   reaches between them) among them.
3. **Reasoned stays, listed in the L13 Review.** Nothing crossing a thread
   boundary moved: `simulationWorker_`, `loading_` (the `std::atomic<bool>` the
   loading thread sets), the two EQE1 mutexes, the GPU-upload handshake
   (`pendingGPUResults_`, `pendingVBOUploads_`), `backgroundThreadPool_`, and
   the four SimulationWorker-output pointers `test_simulation_output_lifetime.cpp`
   guards as a group (T115/T116).

The 151 L13 left were 81 uses of `IrrlichtRenderer`'s **public methods** (the
renderer's external API, B050 cluster 6 — not a defect) and 70 in those stays.
**That number drifts with ordinary work and is not current**: T366 read 163,
T371 read 165, and a comment-filtered count at this tip (T388) reads **174**.
Re-measure; do not quote a number from this paragraph.

| Component | Header | TU |
|---|---|---|
| `EQT::Graphics::RdpComponent` (no `friend` line, and no `IrrlichtRenderer&` since L12) | `include/client/graphics/rdp/rdp_component.h` | `src/client/graphics/rdp/rdp_component.cpp` |
| `EQT::Graphics::Eqe1ArchiveComponent` | `include/client/graphics/eqe1/eqe1_archive_component.h` | `src/client/graphics/eqe1/eqe1_archive_component.cpp` |
| `EQT::Graphics::OverlaysComponent` | `include/client/graphics/overlays/overlays_component.h` | `src/client/graphics/overlays/overlays_component.cpp`, `src/client/graphics/overlays/overlays_hud_env.cpp`, `src/client/graphics/overlays/overlays_env_managers.cpp` (T467: `ensureBoidsManager()` / `ensureTumbleweedManager()` / `ensureDetailManager()` / `ensureWeatherEffects()`, the ONE construction of each -- renderer init and the lazy settings apply both call them; `tests/test_environment_manager_construction.cpp` fails on a second construction under `src/`), `src/client/graphics/overlays/target_ring_draw.cpp` (B994: `drawTargetRing()` and the ring's one draw call), plus the device-free `src/client/graphics/overlays/target_ring.cpp` (TargetIndicator.ini's ring geometry) |
| `EQT::Graphics::DiagnosticsComponent` | `include/client/graphics/diagnostics/diagnostics_component.h` | `src/client/graphics/diagnostics/diagnostics_component.cpp`, `src/client/graphics/diagnostics/diagnostics_debug.cpp` |
| `EQT::Graphics::PlayerCameraComponent` | `include/client/graphics/player/player_camera_component.h` | `src/client/graphics/player/renderer_player_physics.cpp`, `src/client/graphics/player/renderer_player_collision.cpp`, `src/client/graphics/player/renderer_camera.cpp` (plus the device-free `src/client/graphics/player/ground_query.cpp` and `src/client/graphics/player/bsp_ground_query.cpp`, which hold no component method — see the TU table), `src/client/graphics/player/renderer_player_lift.cpp` (T438: the lift rider carry) |
| `EQT::Graphics::SimBridgeComponent` | `include/client/graphics/sim/sim_bridge_component.h` | `src/client/graphics/sim/renderer_sim_bridge.cpp` |
| `EQT::Graphics::ZoneStreamComponent` | `include/client/graphics/zone_stream/zone_stream_component.h` | `src/client/graphics/zone_stream/renderer_zone_stream.cpp`, `src/client/graphics/zone_stream/renderer_zone_merge.cpp` |
| `EQT::Graphics::ZoneLifecycleComponent` | `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` | `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp`, `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp`, `src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp` |
| `EQT::Graphics::ZoneLoadComponent` | `include/client/graphics/zone_load/zone_load_component.h` (with the private `include/client/graphics/zone_load/zone_load_context.h`) | `src/client/graphics/zone_load/renderer_zone_load.cpp`, `src/client/graphics/zone_load/renderer_entity_load_gate.cpp`, `src/client/graphics/zone_load/renderer_zone_load_objects.cpp`, `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`, `src/client/graphics/zone_load/renderer_zone_load_env.cpp` |
| `EQT::Graphics::FrameComponent` | `include/client/graphics/frame/frame_component.h` | `src/client/graphics/frame/renderer_frame.cpp`, `src/client/graphics/frame/renderer_frame_input.cpp`, `src/client/graphics/frame/renderer_frame_render.cpp`, `src/client/graphics/frame/renderer_frame_preferences.cpp`, `src/client/graphics/frame/renderer_frame_options.cpp`, `src/client/graphics/frame/renderer_frame_friends.cpp`, `src/client/graphics/frame/renderer_frame_cursor.cpp`, `src/client/graphics/frame/renderer_frame_trade.cpp`, `src/client/graphics/frame/renderer_frame_tips.cpp`, `src/client/graphics/frame/renderer_frame_tooltips.cpp`, `src/client/graphics/frame/renderer_frame_entity_build.cpp` |

| TU | Lines | Contents |
|---|---|---|
| src/client/graphics/player/renderer_player_physics.cpp | 997 | `EQT::Graphics::PlayerCameraComponent` — the per-frame local movement path. G06 (Batch G) split the 806-line `updatePlayerMovement()` into **fourteen bodies**: the driver (14 lines) is the ordered list of eight phases — `readMovementInput`, `updateHeadingAndPitch`, `computeMovementVector`, `applyProposedPositionAndJumpPhysics`, `resolveMovementCollision`, `publishPositionAndCamera`, `sendPositionUpdateToServer`, `updatePlayerMovementAnimation` — and phase 5 dispatches the file's three collision systems (`resolveIrrlichtCollision` → `resolveIrrlichtClearPath` / `resolveIrrlichtBlockedPath`, `resolveHCMapCollision`, `resolveUncollidedMovement`). The cross-phase locals live in the private `PlayerCameraComponent::MovementFrame`. The statement order is pinned by `tests/test_movement_order.cpp` against `tests/goldens/movement_order_g06.txt` (extracted pre-split). Extracted from IrrlichtRenderer by L05 (Batch L). Header `include/client/graphics/player/player_camera_component.h`. The basename is a string literal in `tests/test_first_person_camera_single_formula.cpp` — do not rename, and do not move a phase out of this TU: G06 gave that guard a reachability clause |
| src/client/graphics/diagnostics/diagnostics_component.cpp | 1,160 | EQT::Graphics::DiagnosticsComponent -- dumpScene, scene-breakdown and frame-timing profiling, getMemoryReport/getMemoryReportBreakdown, EQE1 usage report (T207/T214). Extracted from IrrlichtRenderer by L04 (Batch L). Header include/client/graphics/diagnostics/diagnostics_component.h |
| src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp | 1,266 | `EQT::Graphics::ZoneLifecycleComponent` — createZoneMesh, sorted/portal zone drawing, region meshes, VBO helpers. Also hosts `RenderPassTimer::OnRenderPassPreRender`; the class itself moved to `include/client/graphics/render_pass_timer.h` in L13, its body did not. Extracted from IrrlichtRenderer by L09 (Batch L). Header `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` |
| src/client/graphics/zone_load/renderer_zone_load_env.cpp | 1,140 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep10_Sky, 11_Env, 12_Lights, 12b_Simulation, 13_Cleanup. Extracted from IrrlichtRenderer by L10 (Batch L). Header `include/client/graphics/zone_load/zone_load_component.h` |
| src/client/graphics/sim/renderer_sim_bridge.cpp | 1,114 | `EQT::Graphics::SimBridgeComponent` — SimulationWorker marshaling: buildSimulationZoneData, worker start/stop, postSimulationInput, applySimulationResults, and applySimulationOutput/bindFrameSimulationBuffers (split by lifetime contract, T116). Extracted from IrrlichtRenderer by L07 (Batch L). Header `include/client/graphics/sim/sim_bridge_component.h`. SimulationWorker itself stays owned by IrrlichtRenderer — L07 changed no thread ownership |
| src/client/graphics/overlays/overlays_component.cpp | 1,057 | `EQT::Graphics::OverlaysComponent` — map/navmesh overlays, target outline/selection (the outline is an inverted hull with no stencil since B1064, `target_outline_pass.h`), mouse targeting/LOS, zone-line bounding boxes, FPS counter. (The zone-line DEBUG BORDER overlay was deleted by T354/B042 -- it could never turn on.) Extracted from IrrlichtRenderer by L03 (Batch L). Header `include/client/graphics/overlays/overlays_component.h` |
| src/client/graphics/zone_load/renderer_zone_load.cpp | 1,009 | `EQT::Graphics::ZoneLoadComponent` — the loadZoneSequential() driver (the loading thread's entry) + steps 0_EQE1, 1_S3D, 2_BSP, 4_Regions, 5_Assets, with ZoneLoadContext (Batch G01; split T166). Extracted from IrrlichtRenderer by L10 (Batch L). Header `include/client/graphics/zone_load/zone_load_component.h`; the private ZoneLoadContext definition is `include/client/graphics/zone_load/zone_load_context.h` |
| src/client/graphics/renderer/irrlicht_renderer.cpp | 1077 | Core after J11: ctor/dtor, run(), saveScreenshot, sky/door/world-object façades, player/vision state, createEntityRenderer, diagnostics setters. T448 moved four bodies here out of the facade header: `setRenderDistance()`, `requestChatPrefill()`/`takeChatPrefill()` and `setNightAmbientProbe()`, and deleted the zero-caller `getSkyDebugInfo()` and `removeWorldObject()` |
| src/client/graphics/zone_stream/renderer_zone_stream.cpp | 546 | `EQT::Graphics::ZoneStreamComponent` — deferred-object indexing, progressive load, PVS depth and neighbourhood bookkeeping, entity-prep request queue, deferred-entity pickup (T111/T210). T475 deleted `buildDeferredObject()` and the P1.6/P1.7 texture-arrival drains (board B670): no producer since T04. Extracted from IrrlichtRenderer by L08 (Batch L). Header `include/client/graphics/zone_stream/zone_stream_component.h` |
| src/client/graphics/renderer/renderer_init.cpp | 802 | J11: init/shutdown, loading screen (initLoadingScreen, show/hideLoadingScreen; T468 deleted the uncalled loadGlobalAssets(), whose door-manager build moved to `ZoneLifecycleComponent::ensureDoorManager()`, and T448's `setLoadingProgress()`, moved out of the facade header), plus the `RendererContext` aggregate initialiser, the eleven component constructions and `ctx_.SetComponents()`. Moved into `renderer/` by L12. Since T467 it builds the boids, tumbleweed, detail and weather-effects managers only through `OverlaysComponent::ensure*()` (`overlays_env_managers.cpp`). Applies the Lima BO-cache policy before `createDeviceEx` (`lima_buffer_policy.h`; B988: cache on unless `WILLEQ_LIMA_NOBOCACHE=1`) and logs the effective policy |
| src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp | 766 | `EQT::Graphics::ZoneLifecycleComponent` — unloadZone (retains persistent EQE1 archives, T214), placeholder build/destroy, instant scene, zone-ready state. Extracted from IrrlichtRenderer by L09 (Batch L). Since T468 it holds `ensureDoorManager()`, the ONE `DoorManager` construction body (called by `setupInstantScene()` and Step 7), which registers the T124 door texture guard. B606 deleted `setZoneEnvironment`/`applyStoredZoneEnvironment` (the zone-load Step 10 sky pass applies a strict superset) |
| src/client/graphics/overlays/overlays_hud_env.cpp | 675 | `EQT::Graphics::OverlaysComponent` (second TU) — fog setup, loading-screen draw (the `EQT::Graphics::drawLoadingFrame` shared body, B1088), environmental display settings, HUD setup/update, inventory paperdoll |
| src/client/graphics/overlays/target_outline_pass.cpp | 30 | B1064 (#384): the target outline's GL state plan, device-free -- `TargetOutlinePass::plan()` (an inverted hull on every surface: front faces culled, depth LESSEQUAL, no depth write, scale 1.10, NEVER the stencil, so the B971 stencil-off orangepi surface is correct for it) and `outlineTransform()` (the scale about the entity origin). `OverlaysComponent::drawTargetOutline()` applies it; `tests/test_target_outline_pass.cpp` pins it |
| src/client/graphics/overlays/target_ring.cpp | 104 | B994 (#314): the 3D target ring's geometry, device-free -- `uifiles/default/TargetIndicator.ini`'s four circles, con tints and fade distances (`include/client/graphics/overlays/target_ring.h`, assumptions R1-R5) |
| src/client/graphics/overlays/loading_frame_layout.cpp | 96 | B1088 (#408): the loading screen's ONE layout, device-free against its driver/font -- `EQT::Graphics::drawLoadingFrame()`, the single body both draw bodies delegate to (`LoadingThread::drawLoadingFrame`, the render thread's 0-50 % network phase, and `OverlaysComponent::drawLoadingScreen`, the loading thread) so they cannot drift; B1088's zone-name line (`entryText`, e.g. `Entering North Qeynos...`) is drawn centred at `barY - 24`, between the title and the bar's top border, and NOT at all when empty. `tests/test_zone_display_name.cpp`'s pixel leg pins the centring and the empty-name case |
| src/client/graphics/overlays/target_ring_draw.cpp | 86 | `EQT::Graphics::OverlaysComponent` (fourth TU, B994) -- `drawTargetRing()`, the Options > Display "Show 3D Target Ring" draw after the target outline, and `TargetRing::drawGeometry()`, the one draw call the overlay and `tools/hwcaps/target_ring_capture` share |
| src/client/graphics/overlays/overlays_env_managers.cpp | 113 | `EQT::Graphics::OverlaysComponent` (third TU, T467 / board B550) — `ensureBoidsManager()`, `ensureTumbleweedManager()`, `ensureDetailManager()`, `ensureWeatherEffects()`: the ONE construction of each environment manager that has both an init-time and a lazy site. Each is idempotent and does all construction-time wiring (cache, `init()`, the boids eviction guard); `initLoadingScreen()` and `applyEnvironmentalDisplaySettings()` both call them. `tests/test_environment_manager_construction.cpp` fails on a second construction under `src/` |
| src/client/graphics/slash/renderer_slash_commands.cpp | 594 | `EQT::Graphics::SlashCommandsComponent` — `processSlashCommand()`, the registry-3 renderer slash-command if-chain (593 lines, moved whole; a Batch G decomposition candidate). Extracted from IrrlichtRenderer by L06 (Batch L). Header `include/client/graphics/slash/slash_commands_component.h`. The basename is compiled into `tests/test_merged_help.cpp` as `RENDERER_COMMAND_SOURCE` and parsed there — do not rename |
| src/client/graphics/zone_load/renderer_zone_load_objects.cpp | 686 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep6_Objects, 7_Doors, 9_Collision. Extracted from IrrlichtRenderer by L10 (Batch L) |
| src/client/graphics/diagnostics/diagnostics_debug.cpp | 577 | EQT::Graphics::DiagnosticsComponent -- runtime debug toggles/cycles, debug shader swaps, indoor region map load. Same header |
| src/client/graphics/diagnostics/scene_node_census.cpp | 48 | `computeSceneNodeCensus()` (T443, B453) -- the scene-breakdown census behind DiagnosticsComponent::dumpScene: subtracts every known zone-side node (incl. `zoneMeshNode`) and reports the unaccounted rest. Header `include/client/graphics/diagnostics/scene_node_census.h` |
| src/client/graphics/diagnostics/gpu_buffer_tally.cpp | 55 | B955 (#273): `publishGpuBufferTally()` / `publishGpuBufferTallyThrottled()` -- reads the GLES2 driver's gpuTextureBytes + vboEboBytes (and GL_RENDERER once) on the GL-owning thread and publishes them to the ledger's `gpu_buffers` row (`include/client/core/gpu_buffer_census.h`). Called by `loadZoneSequential()`'s `sampleStep` (loading thread) and `Application::mainLoop()` (render thread, once a second). Header `include/client/graphics/diagnostics/gpu_buffer_tally.h` |
| src/client/graphics/player/renderer_camera.cpp | 538 | `EQT::Graphics::PlayerCameraComponent` (third TU) — camera/projection per mode, `repositionFirstPersonCamera()` (THE one first-person formula), first-person pitch clamp and vFOV, `/fpcam` dump, `setCameraMode()` / `cycleCameraMode()` (T461: the official cycle), player position/vision/swimming state. The basename is a string literal in `tests/test_first_person_camera_single_formula.cpp` — do not rename |
| src/client/graphics/player/renderer_camera_modes.cpp | 269 | `EQT::Graphics::PlayerCameraComponent` (fifth TU, T461) — the official camera set's rig modes (Overhead, Chase since T474, User Defined 1/2, Tether): the per-mode rigs, `updateRigCamera()` (T474: Chase's race scale from `chaseEyeAboveFeet()` and `CameraController::placeChaseCamera()`), `zoomCamera()` (keys and wheel), `orbitCamera()` (the orbit keys), `setUserCameraRigs()` (persistence) and `/camera`'s execution (`runCameraCommand()`, `buildCameraGetLines()`). Writes no camera node directly and never branches on first person. Same header T486: `resetChaseEyeScale()`, the Chase race scale's half of the T467 identity reset |
| src/client/graphics/player/camera_rig.cpp | 179 | T461, device-free — the camera set's name table, cycle and ini mappings (`include/client/graphics/player/camera_mode.h`, with the `eqstr_us.txt` 9500-9505 evidence) and the orbit rig's geometry and clamps (`include/client/graphics/player/camera_rig.h`), plus T474's Chase race scale `scaleChaseRigForEye()` |
| src/client/graphics/slash/camera_command.cpp | 130 | T461, device-free — `ParseCameraCommand()` / `CameraCommandUsageLines()`, `/camera`'s grammar. Header `include/client/graphics/slash/camera_command.h` |
| src/client/graphics/player/renderer_player_collision.cpp | 743 | `EQT::Graphics::PlayerCameraComponent` (second TU) — movement collision, world-triangle raycasts, ground-height queries, the one-shot setupMinimalZoneCollision() and the collision debug lines. (The incremental collision-registration API was deleted by T354/B047 -- zero callers ever; collision is built once at load step 9.) Same header |
| src/client/graphics/player/renderer_player_lift.cpp | 130 | `EQT::Graphics::PlayerCameraComponent` (fourth TU, T438) — `carryPlayerOnLifts()`: the player standing on an opentype-59 platform rides it. Called once per frame from `FrameComponent` right after `syncDoorCollision()`. Same header |
| src/client/graphics/door_motion.cpp | 89 | `EQT::Graphics::LiftCycle` and the door-motion helpers (T438). Every constant is MEASURED against the official client — see `include/client/graphics/door_motion.h`, which carries the session that produced each one. Pure: no scene node, no device |
| src/client/graphics/door_manager_animation.cpp | 170 | `DoorManager::update()` (T438) — the per-frame door animation: the lift cycle, the client-side auto-close, the rotation and spin rates. Split out of `door_manager.cpp` when that TU reached 1,511 lines |
| src/client/graphics/door_manager.cpp | 368 | `DoorManager` core after T453: ctor/dtor, `setZone`, the two PVS-visibility predicates, `calculateOpenHeading`, `isDoorMeshBuilt`/`getUnbuiltDoors`, `setDoorState`, the two picking queries (`getDoorAtScreenPos`, `getNearestDoor`), `hasDoor`/`getDoor` and `clearDoors`. Header `include/client/graphics/door_manager.h` is unchanged, so no include site moved |
| src/client/graphics/door_manager_mesh.cpp | 374 | `DoorManager` mesh sourcing (T453) — `findDoorMesh` (the zone `_obj` archive lookup), `invalidateMeshCache`, `cacheDoorMesh` (T470), and the procedural `createPlaceholderMesh` with its two file-static helpers `colorFromName` and `createOctagonalPrism`, whose only callers are in this TU. The door-mesh reference ownership rule (T470, B621) is written on `doorMeshCache_` in `door_manager.h` |
| src/client/graphics/door_manager_create.cpp | 488 | `DoorManager` construction (T453) — `createDoor`, `registerDoor` (the deferred form) and `buildDoorMesh`, plus the `INVISIBLE_OPENTYPES` table (opentypes 50/53/54), which stays file-static because both of its readers are here |
| src/client/graphics/door_manager_rebuild.cpp | 239 | `DoorManager` rebuild and bulk visibility (T453) — `recomputeAllBspRegions`, `rebuildSingleDoor`, `rebuildPlaceholderDoors`, `setAllDoorsVisible`, `getDoorSceneNodes` |
| src/client/graphics/detail/detail_manager.cpp | 547 | `DetailManager` core after T453: lifecycle, the texture-cache eviction hook, zone enter/exit and config, `update`, density and per-category enables, the season override, foliage disturbance, footprints, the debug/census accessors, and the two exclusion predicates |
| src/client/graphics/detail/detail_manager_chunks.cpp | 169 | `DetailManager` chunk residency (T453) — `updateVisibleChunks`, `loadChunk`, `unloadDistantChunks`, `rebuildAllChunkMeshes` |
| src/client/graphics/detail/detail_manager_surface.cpp | 554 | `DetailManager` surface queries (T453) — `getGroundInfo` (the ground raycast), `getTextureFromMeshNode`, `getSurfaceTypeAtPosition` and `classifyTexture`, the texture-name classifier the surface-map work in `docs/plans/surface_map_expansion_plan.md` is about |
| src/client/graphics/zone_stream/renderer_zone_merge.cpp | 576 | ZoneMergeBuffers (build/destroy/renderMerged) + the texture-eviction guards: RegionMeshTextureGuard, EntityMeshTextureGuard, ObjectNodeTextureGuard (T123), DoorTextureGuard/boids (T124), plus `EQT::Graphics::ZoneStreamComponent`'s three guard-registration bodies. Extracted from IrrlichtRenderer by L08 (Batch L). Its header `include/client/graphics/renderer_zone_merge.h` did NOT move — it declares namespace-scope classes and free functions, and twelve TUs plus three guard tests include it by that path |
| src/client/graphics/zone_load/renderer_zone_load_entities.cpp | 552 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep8_Entities + 8a_BaseModelPrep, 8b_VariantModelPrep, 8c_VariantTextures, 8d_EquipmentPrep, 8e_DistributePrepResults, 8f_BuildEntityMesh. Extracted from IrrlichtRenderer by L10 (Batch L). The T109 load gate (ensureEntityPrepWorkerStarted, deferEntityForLoadGate) is ZoneLoadComponent's own since L12, in src/client/graphics/zone_load/renderer_entity_load_gate.cpp |
| src/client/graphics/frame/renderer_frame_input.cpp | 1063 | `EQT::Graphics::FrameComponent` — frame phases 1-3: processFrameInput, processCommonInput, processPlayerInput, processInputDeltas, processChatInput, processFrameVisibility, processFrameSimulation. Extracted from IrrlichtRenderer by L11 (Batch L). Header `include/client/graphics/frame/frame_component.h`. Read AS TEXT by `tests/test_static_ui_scroll.cpp` and `tests/test_debug_hotkey_arms.cpp` — do not rename Also read AS TEXT by `tests/test_camera_mode_wiring.cpp`, which pins the frame's ONE `updateRigCamera()` call after the lift carry (T486) |
| src/client/graphics/frame/renderer_frame_render.cpp | 752 | `EQT::Graphics::FrameComponent` — frame phase 4: processFrameRender, now a 28-line ORDERED LIST over sixteen private render passes (runSceneProfileIfScheduled, beginSceneAndDrawWorld, collectScenePassTimings, renderFootprints, checkFrameBudgetsAndLogStats, renderTargetIndicator, renderParticleEffects, renderFireGlowAndBoids, renderWeatherEffects, renderDebugOverlays, renderEntityCastingBars, renderLegacyGui, renderStaticUI, renderZoneLineOverlays, renderFrameTail, endSceneAndSwap). G05 cut the old 544-line body at its `measureSection()` brackets, verbatim, so the frameTimings_ categories are unchanged and no local crosses a cut. THE PASS ORDER IS LOAD-BEARING and is pinned by `tests/test_frame_render_order.cpp` against `tests/goldens/frame_render_order_g05.txt`. Extracted from IrrlichtRenderer by L11 (Batch L). Same header. Read AS TEXT by `tests/test_ui_hide_command.cpp` — do not rename, and do not orphan renderStaticUI() |
| src/client/graphics/frame/renderer_frame_options.cpp | 237 | `EQT::Graphics::FrameComponent` — T434 (board B430-B434): `applyOptionsSetting()`, the ONE body both the Options-window click path and the preferences load path go through. Its three persisted fields are the first with a SETTING behind them rather than just a struct member: music and effects volume go to the game thread as absolute-step intents (T439, `include/client/audio/volume_steps.h`), the clip plane is `IrrlichtRenderer::setRenderDistance()`. Same header B430/B433: also `applyOptionsSettingB430()` (every later setting: `GameOptionIntent` for the server-owned ones, the one `OP_SetServerFilter` payload for the filters, PC/NPC names, the Mouse page, Reload defaults), `applyAllOptionsSettings()` (the load path), `runOptionsCommand()` (`/option`, `/filter`) and `processOptionsKeyCapture()` (the Keys page) |
| src/client/graphics/ui/options_settings.cpp | 322 | B430/B433: the Options window's settings TABLE (`options_settings.h`) -- per setting its owner (Legacy/Renderer/Game/Marker), kind, `/option` name, `eqclient.ini` key, default and range, choices; the era filter set and the `OP_SetServerFilter` payload |
| src/client/graphics/ui/options_values.cpp | 72 | B430: setting a value (`setOptionsValue`), mirroring a game-owned one (`applyGameOptionChanged`), the anon/roleplay fan-out |
| src/client/graphics/ui/options_lists.cpp | 186 | B430/B433: `resolveOptionsClick()` (the ONE click resolver) and the wired combobox/listbox draws (Filters list, Keys list) |
| src/client/graphics/ui/options_classification.cpp | 197 | B430/B431/B433: every Options control row's decision -- WIRED or excluded with its reason; a census read by `OptionsClassification.*` |
| src/client/graphics/ui/options_command.cpp | 173 | B430/B433: `/option` and `/filter` grammar, device-free |
| src/client/graphics/ui/options_keys.cpp | 143 | B433: the Keys page -- categories, row text, `finishKeyCapture()` |
| src/client/bridge/bridge_component_options.cpp | 64 | `eqt::bridge::BridgeComponent` -- B430: `ApplyGameOption()` (OP_SpawnAppearance / OP_SetServerFilter), `PublishGameOption()`, `PublishProfileGameOptions()` |
| src/client/input/hotkey_manager_user.cpp | 95 | `eqt::input::HotkeyManager` -- B433: `setBinding()`, the user copy in the cache dir (`saveUserBindings`, `restoreShippedBindings`, `startupBindingsPath`) |
| src/client/graphics/frame/renderer_frame_cursor.cpp | 150 | `EQT::Graphics::FrameComponent` — B442b (#151 remainder): `processCursorGestureFrame()` (B1066: first settles every pending mouse press as UI- or camera-owned via `pointerOwnedByUI()`, `include/client/graphics/press_ownership.h`; the motionless world click with an item held -> `CursorGestureOp::RequestDrop`, rows 31-33; the item display's held/kept lifetime, rows 24-26) and `processQuantityKeyInput()` (the QuantityWnd box's key gate, row 5). Widgets: `src/client/graphics/ui/confirmation_dialog.cpp`, `src/client/graphics/ui/static_ui_cursor.cpp`; the merchant select-to-sell model (#152) is `src/client/graphics/ui/static_ui_merchant.cpp`. Same header |
| src/client/graphics/frame/renderer_frame_friends.cpp | 131 | `EQT::Graphics::FrameComponent` — B897 (#213): the Friends window's toggle (Alt+F, `/friendswindow`), click pass, Editbox key gate and action applier (`SocialListIntent`, the Contact chat prefill). Panel and geometry: `src/client/graphics/ui/friends_panel.cpp`, `include/client/graphics/ui/friends_layout.h`. Same header |
| src/client/graphics/frame/renderer_frame_trade.cpp | 45 | `EQT::Graphics::FrameComponent` — B896 (#212): the trade / give window's click pass, run right after the QuantityWnd; a click over the window is spent there so the inventory beside it does not also act. Panel: `src/client/graphics/ui/trade_panel.cpp`. Same header |
| src/client/graphics/frame/renderer_frame_tips.cpp | 122 | `EQT::Graphics::FrameComponent` — B834 (#202): the tip windows' per-frame triggers (Tip of the Day after zone-in, a context tip on a window's rising edge), the lazy `help/tips/` text load and the click pass. Window: `src/client/graphics/ui/tip_window.cpp`, `include/client/graphics/ui/tip_layout.h`. Same header |
| src/client/graphics/frame/renderer_frame_tooltips.cpp | 69 | `EQT::Graphics::FrameComponent` — B1004 (#323): `renderItemTooltipLayer()`, the tooltips' frame hop: gathers the item windows, the pointer, Alt and the clock, and draws the result as its own top-most layer (below only the cursor attachment). Logic: `src/client/graphics/ui/item_tooltip.cpp`. Same header |
| src/client/graphics/frame/renderer_frame_entity_build.cpp | 126 | `EQT::Graphics::FrameComponent` — B973 (#291): `runEntityBuildSlice()`, the post-load entity work of one frame (prep queue/dispatch/distribution every frame; texture-upload, create, finalize and equip units under `EntityRenderer::buildScheduler()`'s slice — the first unit always, in every governor state), and `finishEntityBuildFrame()`, the `[entity-build]` window line and the `[entity-build-frame]` trace (`WILLEQ_ENTITY_BUILD_TRACE=1`). Called from `processFrame()` and from `ZoneStreamComponent::processFrameProgressiveLoad()` for a re-armed load without the loading screen. Same header |
| src/client/graphics/player/ground_query.cpp | 43 | Device-free free functions in `EQT::Graphics`, NOT a component method — `GroundZ` (T437: `Floor(z)` / `NoFloor()`, so a missing floor cannot be read as a float), `ShouldLandJump()`, `ShouldClampSwimToFloor()` and T440's `ClassifyBspGroundRay()`. T437 deleted the `feetZ + 1000.0f` ceiling sentinel and the two `+100` swim heuristics here; T440 made a total ray miss and an absent BSP tree return `NoFloor()` instead of a phantom floor at the player's feet. Header `include/client/graphics/player/ground_query.h`. Pinned by `tests/test_ground_sentinels.cpp` |
| src/client/graphics/player/bsp_ground_query.cpp | 63 | Device-free, NOT a component method — B1067 (#387): `FindBspGround()`, the keyboard-movement two-phase ground ray tested against EVERY BSP region the ray crosses (`BspTree::collectRegionsAlongSegment`, `src/client/zone_bsp.cpp`), behind a `RegionRayTest` callback that `PlayerCameraComponent::findGroundZBspFiltered()` fills with the region triangles + objects + doors. Before it only the centre point's region was tested and a floor stored in a thin leaf below the centre read as NoFloor. Header `include/client/graphics/player/bsp_ground_query.h`. Pinned by `tests/test_bsp_ground_step.cpp` |
| src/client/graphics/frame/renderer_frame_preferences.cpp | 157 | `EQT::Graphics::FrameComponent` — T432 (board B275): THE per-character persistence path. uiPreferencesBinding(), setUiPreferencesIdentity() (called once per zone-in by Application, the one place the character and server names are both known) and syncUiPreferences() (called once per frame from processFrame() and once from IrrlichtRenderer::shutdown(), i.e. /camp). syncUiPreferences() COMPARES the whole collected value against the last one written rather than hanging a save off each toggle. Also holds IrrlichtRenderer::setUiPreferencesIdentity(), the pass-through Application calls |
| src/client/graphics/frame/renderer_frame.cpp | 563 | `EQT::Graphics::FrameComponent` — processFrame, the four-phase orchestrator, plus the frame-timing accumulation and the spike/RED/budget diagnostics. Extracted from IrrlichtRenderer by L11 (Batch L). Same header, plus the component's out-of-line ctor/dtor. Also hosts `FrameComponent::measureSection()` — L12 finished the move L04 predicted and L11 was told to leave, so the declaration and `sectionStart_` are the component's now; the write-only `frameStart_` was deleted in the same unit |
| src/client/graphics/renderer/renderer_entities.cpp | 502 | Entity/inventory/combat-anim façades (thin forwards). Moved into `renderer/` by L12 |
| src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp | 223 | `EQT::Graphics::ZoneLifecycleComponent` — GPU upload thread init/drain/apply, region VBOs only. T475 (board B670) deleted the texture-arrival arm (no producer since T04, proved by T473) with the pending-texture queues it fed. GPUUploadThread still owns its own thread; the handle stays on RendererContext. Extracted from IrrlichtRenderer by L09 (Batch L) |
| src/client/graphics/renderer/renderer_event_receiver.cpp | 441 | J11: `RendererEventReceiver::OnEvent` and the key/mouse query surface. B1066 (#386): the held buttons are a `PressOwnership` (`include/client/graphics/press_ownership.h`, header-only) -- `isLeft/RightButtonDown()` is the camera-owned LOOK view, `isLeft/RightButtonHeld()` the physical hold. Moved into `renderer/` by L12 — **read AS TEXT by `tests/test_ui_hide_command.cpp`, `tests/test_static_ui_scroll.cpp` and `tests/test_debug_hotkey_arms.cpp`, all three repointed in the same unit** T486: the zoom keys use `kCameraZoomStepUnits` (camera_rig.h), pinned by `tests/test_camera_mode_wiring.cpp` |
| src/client/graphics/text_batch.cpp | 193 | U01: `TextBatch` — the batched UI text renderer (`addText`, flushed once per frame; `addText3D` has no caller -- entity name tags are `NameTagBatchNode`, B975); since T462 its glyph runs go through `UiQuadBatch`. Header `include/client/graphics/text_batch.h` |
| src/client/graphics/ui/char_create_screen.cpp / char_create_panel.cpp / face_pick_panel.cpp | `wc -l src/client/graphics/ui/{char_create_screen,char_create_panel,face_pick_panel}.cpp` | B888 (#204): the two-page Character Creation screen and modal FacePick state, drawing and click resolution. Geometry `include/client/graphics/ui/char_create_layout.h` and `face_pick_layout.h` quotes the stock XML; FacePick ranges come from `data/char_create_faces.tsv`; hosted by the Character Select screen (B998) and pinned by `tests/test_char_create_screen.cpp` |
| src/client/graphics/ui/char_select_screen.cpp / char_select_panel.cpp / char_select_host.cpp | 200 / 222 / 261 | B998 (#318): the Character Select screen -- the state machine (select, Create opens B888's screen, Delete -> eqstr 9065 Yes/No closing itself after 10 s (measured), Enter World, Quit; the reply to the request in flight), its draw + click resolution, and `CharacterSelectHost`, the render-thread pre-zone host (keyboard, the two click latches, `IrrlichtRenderer::drawPreZoneUI()`); the host and panel carry B888-CC3's Explore wiring (button live, Enter/Esc, overlay, list hide). Geometry `include/client/graphics/ui/char_select_layout.h` (quotes `EQUI_CharacterSelect.xml` + the measured placement); pinned by `tests/test_character_select.cpp` |
| src/client/graphics/ui/explore_screen.cpp | 58 | B888-CC3 (#204): the Explore mode's device-free state machine (open/esc, click-to-row selection with a miss keeping the cursor, Enter entering with the LIST cursor per oracle-b1, the free-camera verb -- forward on `d` is the only MEASURED verb, the rest are the unmeasured orbit/zoom seam; rowed `EXEMPT_TESTS_GROUPS` until the 3D clz scene wires it). Geometry `include/client/graphics/ui/explore_layout.h` (the EXW_ExploreModeWindow overlay quoted from the stock XML); pinned by `tests/test_explore_screen.cpp` (state machine, overlay, and the headless `clz.eqg` zone load) |
| src/client/graphics/ui/trainer_panel.cpp | 404 | B898 (#214): the skill trainer window -- `TrainerPanelState` appliers, `renderTrainerWindow()`, its hit tests and `StaticUIInput::processTrainerInput()`. Geometry `include/client/graphics/ui/train_layout.h` (quotes `EQUI_TrainWindow.xml`); pinned by `tests/test_trainer_panel.cpp` |
| src/client/graphics/ui/ui_quad_batch.cpp | 178 | T462 (B506): `UiQuadBatch` — 2D quad runs as one GLES2 draw call, pixel-identical to the driver's per-quad calls (the run's first quad is drawn by the driver to establish state, the rest in one `glDrawElements`); non-GLES2 drivers get the per-quad calls. Header `include/client/graphics/ui/ui_quad_batch.h`; pinned pixel-for-pixel by `tests/test_static_ui_frame_cost.cpp` |
| src/client/graphics/ui/chat_wrap_cache.cpp | 88 | T462 (B505): `ChatWrapCache` — the chat panel's word-wrap done on CHANGE (width, timestamp setting, buffer generation, appended messages), not every frame; `wrapChatText()` is the pre-T462 loop verbatim. Header `include/client/graphics/ui/chat_wrap_cache.h` |
| src/client/graphics/name_tag_batch.cpp | — | B975 (GitLab #293): `NameTagNode` (the per-entity `ITextSceneNode`, hands itself to the batch under `CTextSceneNode`'s exact registration condition) + `NameTagBatchNode` (ONE node in the transparent pass, camera distance 0; sorts the tags far-to-near with the scene manager's own comparison, projects with the ONE proj x view product of the frame, draws every glyph through a compact 80 B/quad run — driver-owned 2D state setup without a bootstrap draw, one `glDrawElements` per run, split at the index limit, using bounded reusable VBO/EBO storage and a grow-once index prefix; non-GLES2 drivers keep the old `UiQuadBatch` per-quad path). `[nametags]` 5 s window row, owner row `ent.nametag_batch`. `WILLEQ_NAMETAG_BATCH=0` is the per-node A/B. Header `include/client/graphics/name_tag_batch.h`; `tools/hwcaps/nametag_drawcost.cpp` is the per-pose pixel + per-component cost net using the client's DRM Lima policy; `tests/test_name_tag_batch.cpp` pins pixel identity and the bounded-draw invariant and shorter-run/chunk reuse, GPU accounting and release. Measure size with `wc -l src/client/graphics/name_tag_batch.cpp` |
| src/client/graphics/ui/bitmap_glyph_table.cpp | 60 | T462's (B506) glyph table of Irrlicht's built-in bitmap font, moved out of `TextBatch` by B975 (#293) so the entity name-tag batch and the static UI's `TextBatch` share ONE table. Header `include/client/graphics/ui/bitmap_glyph_table.h` |
| src/client/graphics/eq/equipment_actor_geometry.cpp | 197 | T472 (B588): `resolveEquipmentActorGeometry()` — the geometry an equipment actor (IT###) draws, resolved through the WLD reference chain (0x14 → 0x11 → 0x10 skeleton, bone meshes at their rest transform, T476's skinned pieces) rather than by mesh-name prefix; T479 (held branch) also uses it for skeletal zone objects. Header `include/client/graphics/eq/equipment_actor_geometry.h`. **T485 (slot1) is editing this territory** |
| include/client/graphics/gles2_vertex_color.h | 38 | T484 (B726), header-only: `gles2VertexColor()` writes an `SColor` whose stored bytes reach the GLES2 built-in shaders as R,G,B,A — the driver uploads `SColor`'s little-endian B,G,R,A unconverted (board B780). Used by the sky dome tint only; every other vertex-colour consumer still gets the swap. `tests/test_sky_dome_tint_channel_order.cpp` pins the driver convention and fails when it changes, at which point this helper goes |
| src/client/graphics/renderer/renderer_pre_zone.cpp | 48 | B998 (#318): `IrrlichtRenderer::drawPreZoneUI()` -- one pre-zone UI frame (device pump, clear, the static UI renderer, the DRM software cursor) for the character-select host; no scene graph |
| src/client/graphics/renderer/software_cursor_texture.cpp | 28 | T460 (board B520): `AcquireSoftwareCursorTexture()`, the DRM/KMS software cursor's load + upload + `protectTexture()` -- the cursor is a process-lifetime UI asset and used to be evicted by zone unload's `clear()` and never reloaded. Called by `IrrlichtRenderer::createSoftwareCursor()`; **read AS TEXT by `tests/test_software_cursor_pack_name.cpp`** (the cache-name literal) |
| src/client/graphics/eqe1/eqe1_archive_component.cpp | 726 | EQT::Graphics::Eqe1ArchiveComponent -- EQE1 flat-pack archive open/resolve/index/report plus the fire-glow icosphere renderer (EQT_HAS_DRM), extracted from IrrlichtRenderer by L02 (Batch L). Header include/client/graphics/eqe1/eqe1_archive_component.h |
| src/client/graphics/eqe1/eqe1_early_resolution.cpp | 53 | B1096 (#416): `initializeEQE1EarlyResolution()` -- installs the EQE1 name->archive resolution (index load + `setEQE1ArchiveResolver`) in `initLoadingScreen`, before the pre-zone player-model build. Header include/client/graphics/eqe1/eqe1_early_resolution.h |
| src/client/graphics/rdp/rdp_component.cpp | 233 | EQT::Graphics::RdpComponent -- RDP server integration (WITH_RDP guard), extracted from IrrlichtRenderer by L01 (Batch L). Header include/client/graphics/rdp/rdp_component.h |
| src/client/graphics/zone_load/renderer_entity_load_gate.cpp | 78 | `EQT::Graphics::ZoneLoadComponent` — ensureEntityPrepWorkerStarted, deferEntityForLoadGate, the entity load distance gate (T109/T111). Folded into the component and moved into `zone_load/` by L12 (L10 deviation 1, closed) |
| src/client/graphics/diagnostics/scene_node_census.cpp | 48 | `EQT::Graphics::DiagnosticsComponent` — `sceneNodeCensus()`, the recursive scene-graph walk the `/dumpscene` breakdown reports over. Split out of `diagnostics_component.cpp` by T443 (board B453), which made the census count the zone mesh. Same header |

## Zone shader — split by J09

| TU | Lines | Contents |
|---|---|---|
| zone_shader_sources_gles2.h | 1,017 | GLSL ES 1.0 shader sources (private header, `zone_shader.cpp` only) |
| zone_shader_callbacks.h | 702 | `ShaderCallback` and friends (private header, `zone_shader.cpp` only) |
| src/client/graphics/zone_shader.cpp | 537 | Material registration and the `getActive*()` accessors; B1148's three corpse-fade twins (`getMaterialTypeFade*`, base EMT_TRANSPARENT_VERTEX_ALPHA) |

The two headers are `#include`d by `zone_shader.cpp` and by nothing else;
they hold file-local `static` definitions and are not a public interface.
T326 deleted a third, desktop-GL sources header — its build configuration was
retired by P30/P32 and no supported build parsed it.

## Simulation worker — SimulationWorker class

| TU | Lines | Contents |
|---|---|---|
| sim_particles.cpp | 1,269 | Particle system + spell VFX |
| sim_boids.cpp | 743 | Boids + tumbleweeds |
| sim_visibility.cpp | 710 | Regions, PVS, portals, occlusion, light selection, frustum helper |
| simulation_worker.cpp | 561 | Lifecycle core: ctor, start/stop, data intake, workerLoop, computeAll dispatch |
| sim_animation.cpp | 538 | Fire flicker, tree/vertex/detail animation, light animation |
| sim_entity_sync.cpp | 510 | Entity sync/interp/visibility, name tags |
| sim_weather.cpp | 305 | Weather system, sky state |

## Entity renderer — EntityRenderer class (split by J03)

| TU | Lines | Contents |
|---|---|---|
| entity_updates.cpp | 645 | J03: createEntity, updateEntity, pending-update queues, main-thread entity state |
| entity_mesh_build.cpp | 612 | J03: registerEntity, buildEntityMesh, prep-result distribution, composite path selection |
| entity_renderer.cpp | 544 | Core after J03: ctor/dtor, con colors, global character loading, per-race scale/color, placeholder mesh, animation/pose/weapon/light setters T486: `markEntityAsCorpse()` (a witnessed death) goes through `ApplyCorpseDeathPose(witnessed=true)` — the third copy of the ladder is gone |
| entity_build_step.cpp | 628 | J03: processOneEntityBuildStep — the frame-budgeted build step. B973: asks the scheduler before a unit and records its measured cost; the player's target after in-progress entities; a not-ready Placeholder is skipped instead of blocking the ones behind it; the create unit runs NodeSetup too (B966); `countBuildBacklog()`; the one-texture upload unit (`processOnePendingTextureUpload()`) |
| entity_build_scheduler.cpp | 130 | B973 (#291): `EntityBuildScheduler`, device-free — the per-frame slice by governor state (Green 20 % / Yellow 12 % / Red 6 % of the target frame time), the guaranteed first unit, the per-kind cost predictor, the `[entity-build]` window accounting, `WILLEQ_ENTITY_BUILD_LEGACY=1`. Header `include/client/graphics/entity_build_scheduler.h`; `tests/test_entity_build_scheduler.cpp` |
| entity_prep_worker.cpp | 476 | EntityPrepWorker: background entity prep (start/stop, requestPrep, priority sort, prepVariantTextures, prepEquipmentModels) |
| entity_lifecycle.cpp | 506 | J03: removeEntity, corpse decay, appearance update, clearEntities, visibility/name-tag/material toggles. B1148: `beginCorpseFade()` puts a corpse (race not 216/472) on the client's corpse-fade list at OP_DeleteSpawn; `updateMainThreadEntityState()` (`entity_updates.cpp`) fades its model from 1.3 s and removes it past 3499 ms |
| entity_combat_anim.cpp | 437 | Combat animation queues + animation queries |
| entity_first_person_draw.cpp | 401 | T309: the attack-animation draw rule, first-person mode, player entity position/animation |
| entity_interpolation.cpp | 372 | J03: updateInterpolation |
| entity_castbars.cpp | 264 | Casting bar state + rendering |
| entity_equipment.cpp | 243 | Equipment models, attachment; placement is `bone_attachment.h`'s `placeHeldItem()` (T485: the one placement the paperdoll and the model viewer also call, with the correction chosen by search against the official client) |
| entity_spatial.cpp | 218 | Spatial grid, BSP occlusion cache, boats |
| equipment_actor_geometry.cpp (under `src/client/graphics/eq/`) | 197 | T476: resolves an IT actor's reference chain (0x14 -> 0x2D mesh, or 0x11 skeleton with rest-pose-placed pieces) into one `ZoneGeometry` with the item's centre added, the frame `computeHeldItemTransform()` expects |
| entity_size.cpp | 58 | setEntitySize |
| corpse_death_pose.cpp | 66 | T456 (B477): `ApplyCorpseDeathPose()`, the ONE d05/d02/d01 death-pose ladder — held on the last frame for a corpse found lying there (both build paths), played through for a witnessed death (`markEntityAsCorpse()`, T486). Header `include/client/graphics/corpse_death_pose.h`; `tests/test_corpse_death_pose.cpp` drives all three production paths into it and fails on a second copy of the ladder under `src/` |
| corpse_fade.cpp | 50 | B1148 (#468): `ApplyCorpseFadeAlpha()` -- a fading corpse's vertex alpha + its materials' blended twins. Header `include/client/graphics/corpse_fade.h`: the client's fade-list schedule (eqgame 0x401127/0x4012bb: alpha 1 - (t - 1300) x 0.0005, deleted past 3499 ms), the 216/472 race rule, effect 0x5e; shared with `tools/model_viewer`'s FX harness; `tests/test_corpse_fade.cpp` |

## WLD loader — WLDLoader class (split by J05)

| TU | Lines | Contents |
|---|---|---|
| wld_loader.cpp | 475 | Core: parseFromArchive/parseWldBuffer, the fragment dispatch loop, memory accounting, region bounds/lookup, checkZoneLine |
| wld_fragments_actor.cpp | 504 | 0x14/0x15 actor def+ref, 0x2C/0x2D mesh reference |
| wld_fragments_bsp.cpp | 502 | 0x37, 0x21 BSP tree, 0x22 region, 0x29 region flags, zone-line string decode |
| wld_fragments_mesh.cpp | 484 | 0x36 mesh, combined/per-region geometry accessors, PVS presence |
| wld_fragments_skeleton.cpp | 344 | 0x10–0x13 skeleton track/def/ref |
| wld_fragments_light.cpp | 261 | 0x1B/0x1C light source, 0x28 point light, 0x2A/0x35 ambient, 0x2F |
| wld_fragments_material.cpp | 159 | 0x03/0x04/0x05 texture, 0x30/0x31 material |
| wld_internal.h | 18 | J05 private header shared by the seven TUs |

## S3D loader — S3DLoader (`include/client/graphics/eq/s3d_loader.h`) — split by T451

The `.s3d` archive front end: what a zone's archive set contains and how it
becomes geometry, textures, characters and lights. One class, five TUs; the
class header is unchanged, so no include site moved.

| TU | Lines | Contents |
|---|---|---|
| s3d_loader.cpp | 228 | The static empties, both `loadZone` overloads, `loadZoneFromPreParsed`, `extractZoneName`, `loadLights` / `loadLightsFromArchive` |
| s3d_loader_characters.cpp | 436 | `loadCharacters`, `flattenSkeleton`, `deriveChrArchivePath` (the `_chr` archive and its skeletons) |
| s3d_loader_skinning.cpp | 407 | The file-local `Mat4`, `applySkinning`, `buildAnimatedSkeleton` |
| s3d_loader_objects.cpp | 237 | `loadObjects` — the `_obj` archive's placeable geometry and instances |
| s3d_loader_textures.cpp | 127 | `loadTextures`, `loadObjectTextures`, `loadCharacterTextures` |

## Animated character mesh — EQAnimatedMesh / EQAnimatedMeshSceneNode — split by T451

`include/client/graphics/eq/animated_mesh_scene_node.h` is unchanged; the one
new header is the seam.

| TU | Lines | Contents |
|---|---|---|
| animated_mesh_scene_node.cpp | 546 | `EQAnimatedMeshSceneNode`: construction, `createInstanceMesh`, `applyAnimation`, `OnRegisterSceneNode`, `render`, materials, `OnAnimate`, the frame/loop API, `setMesh`, the MD2/joint stubs |
| animated_mesh_skinning.cpp | 375 | `applyBoneTransformsToMeshImpl` — the CPU skinning kernel — and the SIMD normal helper it uses (still file-local) |
| animated_mesh.cpp | 232 | `EQAnimatedMesh`: the `IAnimatedMesh` holding the skeleton, base mesh, vertex pieces and mapping |
| animated_mesh_bones.cpp | 209 | `getBoneWorldPosition` / `getBoneWorldDirection`, `getBoneIndexByName`, `findRightHandBoneIndex` / `findLeftHandBoneIndex` |
| `include/client/graphics/eq/animated_mesh_skinning.h` | 26 | Seam: the kernel was a file-static and its two callers are now in different TUs |

## Race model / variant loader — RaceModelLoader (`include/client/graphics/eq/race_model_loader.h`)

Character model loading from S3D archives. `variant_loading.cpp` resolves a
race/appearance to a mesh and owns the variant cache's negative entry;
`variant_loading_preload.cpp` holds the cache and preloader;
`variant_loading_animation.cpp` holds the animation-aware variant loader
(split out by J35, de-duplicated by T340 Leg B).

| TU | Lines | Contents |
|---|---|---|
| variant_loading_animation.cpp | 576 | `loadModelFromGlobalChrWithVariantsForAnimation` and the four functions T340 Leg B (B052) cut out of it. The method was 815 lines because it was DUPLICATED — the animation-merge body written out four times (byte-identical once normalised) and the whole match-select-combine-register character body written out twice, 0.906 similar (`refactor/G07_variant_loader.md`). Now: `buildCodesToTry` (the candidate list + the animation source code), `selectVariantParts` (this variant's meshes out of one character, with the variant-0 fallbacks), `mergeAnimationsFrom` (the four copies, once) and `tryBuildVariantFrom` (the character body, shared by the zone and global paths, parameterised by `fromGlobal`), plus the candidate loop itself. **The loop is a SEARCH, not a pipeline**: its `continue`s mean "try the next candidate", and returning false makes the caller write a permanent negative cache entry — so a race whose primary code misses but whose `fallback_code` hits must still LOAD (Will's EQE1 all-or-nothing rule). Pinned by `tests/test_variant_loader_candidates.cpp` (T340, six cases, real shipped S3D archives, seven of the eleven semantic exits driven) through the read-only `variantLoadTrace()` seam |

## Event bus (`include/client/state/`) — split by T451

`event_bus.h` used to hold the `GameEventType` enum, every payload struct and
the `GameEvent` variant in one header riding the ceiling. T451 split it; the
enum and the payload families are now separate headers and `event_bus.h` is
the variant plus `GameEvent` (the count of enumerators is measured by
`DocCounts` against `event_types.h`, never quoted by hand):

| Header | Lines | Contents |
|---|---|---|
| `include/client/state/event_types.h` | 170 | `enum class GameEventType` — THE list of game→renderer events (T264: every enumerator has a publisher and a consumer, `tests/test_reachability_sweep.cpp`) |
| `include/client/state/event_bus.h` | 259 | `GameEvent` and the `std::variant` of every payload struct; includes the five payload headers |
| `include/client/state/event_payloads_entity.h` | 227 | entity/spawn/movement/corpse/death payloads (`EntitySpawnedData`, `EntityMovedData`, `EntityDeathAnimationData`, `CorpseDecayStartedData` ...) |
| `include/client/state/event_payloads_inventory.h` | 171 | inventory, loot, bank, merchant and item-display payloads |
| `include/client/state/event_payloads_social.h` | 240 | chat, group, guild, trade, target and player-state payloads |
| `include/client/state/event_payloads_spell.h` | 136 | cast, buff, gem and spell-effect payloads |
| `include/client/state/event_payloads_ui.h` | 245 | window, HUD, volume and UI-state payloads |

## Test build — tests/CMakeLists.txt (split by J20)

`tests/CMakeLists.txt` is now a driver that `include()`s **19** per-domain
fragments from `tests/cmake/`: `action_handlers`, `archives`, `audio`,
`commands`, `core_utils`, `data_formats`, `database`, `eqe1`, `graphics`,
`hygiene` (50 tests: B1171's `FxCaptureLevel`, B1134's `OracleLoginPatch`, B1093's `ArmContainerGuard`, `FacePickXmlGeometry`, `FileSizeCeiling`, `DocCitations` (+ B1020's
`DocCitationsSelfTest`, the anchored-citation probes), B1094's
`InstalledPackMatchesPin` (the installed shared pack vs the pin, hard only
on fable) + `PackCheckContract` (the `--check`-is-pin-only /
`--check-installed` contract, stub builders), B1030's
`DockerfileArm64Derived` (the arm64 Dockerfile stays arch-swapped armhf) and
`PiEnvLaneResolution` (`scripts/pi/env_selftest.sh`: a PI_LANE that disagrees
with a listed PI_HOST is refused, never rewritten), B1057's
`OracleSessionSafety` + `OracleRemoteStopSafety` (the oracle driver's lane
gate and its never-kill-a-foreign-display stop path), B1129's
`OracleGemCastPlanCheck` + `OracleClassLegSafety` (gem casts the login
class cannot make are refused; the multi-class leg helper's hooks), B1058's
`OracleLocateTemplates` (the template library over the archived oracle
corpus; SKIPs where the corpus or NumPy is absent), B1059's
`OracleGlyphReader` (the glyph-atlas text reader over pinned, labelled crops of
the same archive; SKIPs likewise), `DocClaims` and
`DocCounts` from T371/T373, plus `CommentClaims`, `TexturesReproducible`,
`PackReproducible`, `WriteOnlyFields` (T414 Leg C, the field half of the
reachability ratchet), B1018's `WriteOnlyFieldsSelfTest` (`check_write_only_fields.py --self-test`, the multi-declarator split's own probes) and T450's `DbReproducible`, T455's
`ZeroCallers` + `ZeroCallersSelfTest` (the classifier's own probes,
`check_zero_callers.py --self-test`), B1089's `LeaseCoreSelfTest`
(`scripts/eqemu/lease_selftest.sh`: the lane lease's hermetic net — take,
renew, TTL, reap, per-host kill and verified release — no ssh, no network),
T491's `VisualGateLedgerAssertions`
+ `MemoryCapHuntScript` (the memory-ledger half of the visual gate and the
cap-hunt script, both pure-script checks), T498's `ItemModelsMatchServer`
(`data/item_models.json` against the committed server export
`data/item_idfile.tsv`, a ratchet over
`data/item_models_drift_baseline.txt`), B824's `ItemModelMapDeletion`
(`scripts/refactor/check_item_model_map.py`, the deleted item-to-model JSON
map's ratchet — the map's identifiers stay out of the tree and
`getModelIdForItem()` stays db-only) and B849's `BoardToIssuesHygiene`
(`scripts/gitlab/test_board_to_issues.py`, the board-to-issue migration
script's regression net — all-states dedup, `→ issue #N` pointer
honouring, the fixed-header anomaly probe, the parser's body
termination, and the `--label-missing-status` plan; pure python with an
injected http stub, so no network, no GitLab token, no artefact) and B811's
`LlmBenchHeldBeforeAfterHygiene` (`scripts/llmbench/test_timeline_held_check.py`,
the llmbench held-item before/after gate — a timeline step that claims to
have MOVED an item into a slot the character already held it in must be
INVALID at compile time and may never carry a grading label; pure python over
committed TSVs) and B793's `LlmBenchTimelineHeldFromSnapshot`
(`scripts/llmbench/test_timeline_held_snapshot.py`, the timeline's `held=`
claims against the committed T492 reference inventory snapshot — every
claim is the snapshot's, the unestablishable `/equip` after-state is not
claimed, a missing snapshot is a hard error; pure python over committed
TSVs) and B908's `BoardDirNumbers`
(`scripts/refactor/check_board_dir_numbers.py`, the refactor/B###/-directory-
vs-BOARD.md-header collision net — a directory's declared subject must share
a keyword with every BOARD.md header naming its number; pure source/doc scan)
and B888-CC4's `CharCreateGateStateList`
(`scripts/refactor/check_charcreate_gate_state_list.py`, the charcreate
gate's three-way state-list agreement — `GATE_STATES` in
`scripts/ci/charcreate_gate_controls.py`, the `kStates[]` array in
`tools/hwcaps/charcreate_capture.cpp`, and the scores table in
`refactor/B888-CC4/README.md` must name the same states; pure text scan)
and B907's `CheckBuildTreeOrphansPrune`
(`scripts/ci/test_check_build_tree_orphans.py`, the regression net for
`check_build_tree_orphans.py --prune` — the CI warm-tree orphan prune added to
stop one branch's leftover binary failing the NEXT branch's pipeline; asserts
an orphan is removed, a claimed binary is kept, a non-build-tree and an
escaping `bin/` are both refused untouched, and nothing outside `<build>/bin/`
is ever touched; pure Python against a hand-built fake CMake file-API reply,
no real `cmake` invocation) and B923's `ClientLedgerBudgetCheck`
(`scripts/ci/test_client_ledger_check.py`, the repeatable net for
`client_ledger_table.py check` — the client-ledger capture assertion the
`ClientLedgerLive` live ctest and the CI `client-ledger-live` job run: a
capture-shaped fixture in the client's own [memledger] line format must pass
at exit 0, turn red at exit 1 when a client-only owner's category exceeds its
orangepi budget, and exit 3 for a run without a `steady_60s` point; budgets
come from `config/constrained_presets.json`, never from the test; pure
Python, no server, no display, no binary), plus B975's `NameTagCrowdTiming`
(`scripts/b975/test_frametiming_table.py`, excludes zone-hop warmup from
idle timings) and `RenderedZoneFinalVisit`
(`scripts/pi/test_rendered_zone_final_visit.py`, requires the final tour visit
to reach the requested zone and steady state without borrowing earlier evidence),
plus B987's `CrowdObservationControls`
(`scripts/b964/test_observation_controls.py`, both wrappers' duration forwarding
(defaults, canonical-integer consumption, `--env` passthrough) and
fail-closed observation, the ONE accepted duration spelling (plain ASCII
decimal 0..86400, rejected before any lock/stack/Pi resource, including the
sweep's self-validation with its real scripts) and a bounded x86 deadline
that stops a continuously logging child), plus B1026's `CiBuildTreeStamp`
(`scripts/ci/test_ci_build_tree_stamp.py`, `.gitlab-ci.yml` as text: `test`
and `client-ledger-live` verify the host+sha build-tree stamp before they
consume the tree, the build steps exist once and claim the tree only after
`cmake --build`, and `.goldfarm` no longer claims a single tagged runner;
B1038: the `build-headless` job builds `EQT_GRAPHICS=OFF` through that same
function into its own tree and runs `ctest -LE hygiene` in-job) and B1042's
`CiQueueRules` (`scripts/ci/test_ci_queue_rules.py`, `.gitlab-ci.yml` as
text: the docs-only diff skips the C++ jobs while hygiene always runs,
build+test is one job, and every x86 configure goes through the guarded
ccache launcher),
and B1024's `VisualGatePlatform`
(`scripts/ci/test_visual_gate_platform.py`, the per-platform golden-set
resolver: the x86 gate's benchmark invocation must stay byte-identical to
`tests/data/b1024/x86_gate_plan.txt`, and the pi half pins the
`tests/goldens/pi/<zone>/f<n>.png` + `platforms.pi` resolution, the NOT YET
BLESSED verify, the `--bless --platform pi` refusal, the `--capture` layout
(nothing under `tests/goldens/`) and the Mesa-prefix maps proof; pure
python, no benchmark, no build artefact), and B1034's `EglConfigDumpB1034`
(`scripts/ci/egl_config_dump.sh verify`: the x86 surfaceless-EGL fixture at
`tests/data/b1034/surfaceless_es2.json` re-dumped live on the build host with
CI's GL pin, `__EGL_VENDOR_LIBRARY_FILENAMES` -> `50_mesa.json` +
`LIBGL_ALWAYS_SOFTWARE=1`; a driver update that changes llvmpipe's config
list goes RED -- re-dump with `dump <dir>`), B1034's
`GbmConfigSelectForsB1034` (`scripts/ci/test_gbm_config_select_forks.py`:
the `chooseConfigForGbmFormat` wrapper is hand-copied into BOTH device forks
and must stay byte-identical; the shared logic lives in
`docker/irrlicht-drm/gbm_config_select.h`, unit-tested by
`test_egl_config_select` in the build stage) and B1034's
`PiHdmiForceSelftest` (`scripts/pi/hdmi_force_selftest.sh`: the
`pi_hdmi_force_on`/`pi_hdmi_restore` contract of `scripts/pi/env.sh` against
a fake device surface -- shadowed `ssh`/`sudo`, local status/record files --
no device, no ssh, no root; force-on records the pre-force status, restore
writes `detect` back (hotplug, never the literal recorded status), a foreign
owner's record is refused, and a failed sudo is a refusal, not a forced lane)
-- the three
`*Reproducible` tests are the pins on the three derived assets, see
`CLAUDE.md`'s recipe table; `WriteOnlyFields` and `ZeroCallers` are the two
halves of the reachability ratchet, a field written and never read and a
function declared and never called -- and since T458 `ZeroCallers` also
ratchets the functions reached ONLY from `tests/`, each classified in
`EXEMPT_TESTS_GROUPS` as a `seam`, an `observer` or a boarded B600-B606 row;
re-measure with `check_zero_callers.py --bucket all`, do not quote a count), `midi`, `network`, `packets_skills`, `renderer_guards`,
`spells`, `state`, `ui_rendering`, `world_geometry`, `zone_integration`.
Add a new test to the fragment for its domain, not to the driver.

Shared test-side headers live beside the tests, not under `include/`:
`tests/zone_stream_test_utils.h` (packet replay, `NewSpawnPayloadAt`),
`tests/spell_wire_payloads.h` (T482: the four spell wire builders T469 wrote
— `OP_BeginCast`, `OP_Action`, `OP_InterruptCast`, `OP_MemorizeSpell` —
shared by `test_spell_interrupt_wire.cpp`, `test_spell_cast_end_wire.cpp`
and `test_ninth_spell_gem.cpp`) and `tests/action_dispatcher_test_fixture.h`.

Four test targets are ONE target built from SEVERAL sources plus a shared
fixture header, because a ctest test name is `Suite.Case` and separate
binaries carrying the same suite would register the same name twice:
`test_command_processor` (T165), and T446's `test_combat_manager`,
`test_eq_commands_equip` and `test_zone_connection_flow`. T453 made
`test_action_dispatcher` the fifth: `tests/action_dispatcher_test_fixture.h`
(the fixture, in the named namespace `action_dispatcher_test`, because an
anonymous-namespace fixture in a header gives each TU its own type and GTest
aborts at startup) plus `test_action_dispatcher_routing.cpp` (30 cases),
`test_action_dispatcher_families.cpp` (18) and
`test_action_dispatcher_guards.cpp` (15) — 63 cases, the same 63 names.

## Tools (`tools/`) — the split multi-TU tools

`tools/` is scanned by the ceiling since T385, and the tools that breached it
were split the same way `src/` was: one target, several sources, a shared
header beside them in a per-tool subdirectory. The rest of `tools/` is still
one file per tool.

| tool (target) | seam header | TUs |
|---|---|---|
| `model_viewer` (T385, B1138) | `tools/model_viewer/model_viewer_state.h`; `model_viewer_fx_harness.h` (B1138: the spell FX harness's options, plan, cast events, particle step) | `model_viewer.cpp` (main) + `model_viewer/` startup, loop, input, mesh, entity, equipment, spell_test (cast steps = the client's SpellCastVisual* payloads through `src/client/bridge/spell_cast_fx.cpp`), fx_harness (`--plan` fixed-dt run: texture cache + willeq.eqe1, ParticleManager -> SimulationWorker step, `--camera b1118` fit with B1139's lane-rig defaults, frame/timing/scene writers, B1144's plan `caster_level` / `action` columns and `slots.tsv`; `tools/model_viewer/README.md`). The fable-side FX gate; `tests/test_fx_harness.cpp` (label `graphics`) runs it |
| `entity_composite_builder` (T385) | `tools/entity_composite/composite_builder.h` | `entity_composite_builder.cpp` (usage+main) + `entity_composite/` texture_io, model, archives |
| `gles2_fog_volume_benchmark` (T385, T453) | `tools/hwcaps/fog_volume/fog_volume_shaders.h` (GLSL sources), `fog_volume_bench.h` (T453 seam: the five shared structs and every cross-TU declaration) | `gles2_fog_volume_benchmark.cpp` (`main`) + `tools/hwcaps/fog_volume/fog_volume_shaders.cpp` (the ten GLSL ES sources) + `fog_volume_egl.cpp` (DRM/GBM and X11 EGL bring-up, page flip, teardown, shader compile/link) + `fog_volume_scene.cpp` (FP16 sqrt/exp LUTs, the subdivided alley, the fog billboard quad, the checker texture, the mat4 helpers, the eight fog-sphere positions) + `fog_volume_passes.cpp` (the zone and fog draw passes and the three timed runners) |
| `zone_bench_loader`, `gles2_zone_benchmark` (T387) | — | split in place under `tools/hwcaps/`. B1016 (#337): `zone_bench_loader` now emits the one-time `[gldriver]` line after `createDeviceEx` (the CI `gate:` job's only real GLES2 run), and links `src/client/core/gl_driver_census.cpp` for it — the gate's `assert_gldriver_line` (`scripts/ci/visual_gate.py`) requires the line present and parseable per zone |
| `willeq_db_builder` (T447) | `tools/willeq_db/db_builder.h` | `willeq_db_builder.cpp` (helpers, metadata, main) + `tools/willeq_db/db_loaders_text.cpp` (the client's `*.txt`) + `tools/willeq_db/db_loaders_json.cpp` (the WillEQ/config `*.json`). T452 deleted its fourth loader, `loadS3dIndex()`, and the `--s3d-index` flag with it: every table the tool can build now ships. |
| `wld_dump` (T447) | `tools/wld_dump/wld_dump.h` | `wld_dump.cpp` (archive I/O + main) + `tools/wld_dump/wld_dump_core.cpp` (header, string table, fragment index) + `wld_dump_geometry.cpp` (0x10/0x36/lights) + `wld_dump_animation.cpp` (0x12–0x2D + hex fallback) |
| `gles2_shader_perpixel_benchmark` (T447) | `tools/hwcaps/shader_perpixel/shader_perpixel.h` | benchmark (runner + main) + `shader_perpixel_egl.cpp` + `shader_perpixel_shaders.cpp` (13 GLSL ES sources) + `shader_perpixel_scene.cpp` |
| `test_gl_points` (T453) | `tools/gl_points/gl_points.h` | `test_gl_points.cpp` (the six-test `main`, left whole — its test blocks share locals, so cutting it would not be a structural move) + `tools/gl_points/gl_points_egl.cpp` (DRM/GBM and X11 EGL bring-up, teardown, the GL error helpers, shader compile/link) + `gl_points_fbo.cpp` (the offscreen FBO, the pixel readback analyser, the four GLSL ES sources, the pass/fail tally) |
| `held_item_candidate_search` (T485) | — | `tools/hwcaps/held_item_candidate_search.cpp`, one TU: renders all 24 proper signed permutations of the held-item correction through `placeHeldItem()` on an `EntityRenderer`-built Human male and writes `candidates.tsv` + `rpoint_frames.txt`; the instrument `kHeldItemCorrection` was chosen with |
| `zone_line_editor` (T451) | `tools/zone_line_editor/zone_line_editor.h` | `zone_line_editor.cpp` (lifecycle + `main`) + `tools/zone_line_editor/zle_io.cpp` (zone geometry, portal data, zone-line JSON) + `zle_input.cpp` (the key/mouse edit operations and the view transforms) + `zle_render.cpp` (the 2D draw passes and the HUD) |

Each of those targets carries `${CMAKE_SOURCE_DIR}/tools` on its include path,
so the seam header is reached as `"<tool>/<header>.h"`.

## Conventions

- New extractions follow the patterns established in Batch F unit reviews
  (`refactor/F14`–`F24`), T166 (`refactor/T166_split_renderer_zone_load.md`)
  and Batch J (`refactor/J01_*` … `refactor/J20_*`):
  TUs re-open `namespace EQT { namespace Graphics }`, add full includes for
  types the class header only forward-declares, and file-local static
  helpers live with their sole callers.
- File-size ceiling: **1,500 is a FAIL** under
  `scripts/refactor/check_file_sizes.sh --all` (the `FileSizeCeiling` ctest
  test), which since T385 scans `src/`, `include/`, `tests/`, `tools/` and
  `benchmarks/`; 3,000 is the second-line hard cap. The allowlist is empty
  (M00, `docs/decisions.md` 2026-09-04 and 2026-09-05). Only the diff and
  `--files` modes still report >1,500 as a WARN.
- Behavior-preserving moves are verified with
  `scripts/refactor/verify_move_only.py BASE..HEAD`.
- **Cite a function or symbol, not a line number.** Every Batch J split
  invalidated hundreds of `file.cpp:NNNN` citations at once; J21 had to
  repair them by hand. A stale line number is worse than no line number.
