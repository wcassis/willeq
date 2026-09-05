# tests/cmake/commands.cmake -- included from tests/CMakeLists.txt.
# Action layer: ActionDispatcher, CommandProcessor, command families, bridges, combat, and the registry drift guards.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T12: ActionDispatcher routing + guard behavior tests. Same state-module
# dependency set as test_state_modules_secondary, plus action_dispatcher.cpp
# itself. No graphics, no Irrlicht, no EverQuest -- the action layer's only
# includes are game_state.h, output_renderer.h, logging.h and stdlib.
SET(ACTION_LAYER_STATE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    # T173: EntityManager ranks a living entity above its own corpse and
    # asks the ONE shared predicate; the TU is dependency-free by design.
    ${CMAKE_SOURCE_DIR}/src/client/corpse_predicate.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/group_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/pet_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/trade_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/world_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/game_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/door_state_manager.cpp
)

ADD_EXECUTABLE(test_action_dispatcher
    test_action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_social.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_guild_misc.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_action_dispatcher GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_action_dispatcher)

# T12: CommandProcessor tests -- registration, completion, category
# consistency, argument parsing, case sensitivity. Needs ActionDispatcher
# (real, driven through a recording IActionHandler fake) plus the same
# state-module set. IOutputRenderer is a pure interface satisfied entirely
# by a local test fake -- output_renderer.cpp / console_renderer.cpp /
# graphical_renderer.cpp (which pulls in Irrlicht) are NOT linked.
#
# T165: the cases are split across five .cpp files (core mechanics, then one
# per command family) sharing command_processor_test_fixture.h. They are all
# sources of THIS ONE target on purpose -- the 3000-line ceiling is a
# per-file rule, so splitting the file satisfies it without adding four more
# 80MB binaries and four more ctest discovery passes (T163's NFS cost).
ADD_EXECUTABLE(test_command_processor
    test_command_processor.cpp
    test_command_processor_migration.cpp
    test_command_processor_combat_guild.cpp
    test_command_processor_world.cpp
    test_command_processor_senders.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_social.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_guild_misc.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_bank.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_train.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_loot.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_registration.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_social.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_world.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_spells_gm.cpp  # J04
    ${ACTION_LAYER_STATE_SOURCES}
)
# T140: cmdMove/cmdMoveTo/cmdFollow use fmt::format directly (T88's
# dual-frame /move echo, and the original /moveto and /follow ack wording),
# so this target now needs fmt linked -- previously it didn't use fmt at all.
TARGET_LINK_LIBRARIES(test_command_processor GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_command_processor)

# T153: /appearance parse + validation. Same dependency set as
# test_command_processor (real ActionDispatcher + CommandProcessor driven
# through the recording IActionHandler fake), in its own target because
# test_command_processor.cpp was past check_file_sizes.sh's 3000-line FAIL
# ceiling when T153 landed. See refactor/T153_consider_corpse_and_face.md.
# T165 split that file, so the ceiling is no longer the reason this target
# exists -- it stays separate only because nothing is gained by merging it.
ADD_EXECUTABLE(test_appearance_command
    test_appearance_command.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_social.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_guild_misc.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_bank.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_train.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_loot.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_registration.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_social.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_world.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_spells_gm.cpp  # J04
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_appearance_command GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_appearance_command)

# T362 (B069): the shared slash-command completion helper
# (include/client/command_completion.h) and the registry half that used to
# drift from it. Header-only helper + one TU -- no action layer, no
# EverQuest, no Irrlicht.
ADD_EXECUTABLE(test_command_completion
    test_command_completion.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/ui/command_registry.cpp
)
TARGET_LINK_LIBRARIES(test_command_completion GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_command_completion)

# T143: registry-aware /help. Merges registry 1's CommandRegistry (game,
# graphics/ui) and registry 2's CommandProcessor (action) catalogs -- needs
# both, plus the same ACTION_LAYER_STATE_SOURCES dependency set as
# test_command_processor.cpp, but no Irrlicht, no EverQuest, no Application.
ADD_EXECUTABLE(test_merged_help
    test_merged_help.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_social.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_guild_misc.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_bank.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_train.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_loot.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_registration.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_social.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_world.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_spells_gm.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/graphics/ui/command_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/client/help/merged_help.cpp
    ${CMAKE_SOURCE_DIR}/src/client/help/renderer_command_catalog.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
# T223: the registry-3 drift guard parses the renderer's if/else chain source
# text and asserts its `cmd == "/x"` literal set equals the static catalog.
TARGET_COMPILE_DEFINITIONS(test_merged_help PRIVATE
    RENDERER_COMMAND_SOURCE="${CMAKE_SOURCE_DIR}/src/client/graphics/slash/renderer_slash_commands.cpp")  # J11; L06 moved the directory
TARGET_LINK_LIBRARIES(test_merged_help GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_merged_help)

# T223: Application::dispatchToActionOrRenderer()'s registry-3 stage on a
# bare (never initialize()d) Application -- m_graphicsInitialized is false,
# exactly as in a --no-graphics run, so a known renderer command must be
# REFUSED (outcome=failed with the "requires graphics mode" message), never
# queued, and an unknown name must be unrecognized. Links willeq_core the
# same way test_video_capture_flags does.
ADD_EXECUTABLE(test_headless_command_refusal
    test_headless_command_refusal.cpp
)
TARGET_LINK_LIBRARIES(test_headless_command_refusal GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_headless_command_refusal PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_headless_command_refusal)

# T223 (T198): every configured EQE1 global archive must exist as a client
# .s3d (asset-gated skip), and the built-in fallback copy of that list must
# not carry the phantom gequip7.
ADD_EXECUTABLE(test_eqe1_archive_config
    test_eqe1_archive_config.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eqe1_archive_config PRIVATE
    EQE1_ARCHIVES_JSON="${CMAKE_SOURCE_DIR}/config/eqe1_archives.json"
    RENDERER_EFFECTS_SOURCE="${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1/eqe1_archive_component.cpp")
TARGET_LINK_LIBRARIES(test_eqe1_archive_config GTest::gtest_main ${JSONCPP_LIBRARIES})
GTEST_DISCOVER_TESTS(test_eqe1_archive_config)

# T264: the intent/handler reachability sweep, pinned. Parses
# renderer_intents.h / eq_bridge.cpp / event_bus.h / action_dispatcher.{h,cpp}
# / eq.h and the src tree (WILLEQ_REPO_ROOT) and asserts the producer-less /
# unreachable sets equal the documented blocked lists, in both directions.
ADD_EXECUTABLE(test_reachability_sweep
    test_reachability_sweep.cpp
)
TARGET_LINK_LIBRARIES(test_reachability_sweep GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_reachability_sweep)

# G02: the intent-dispatch coverage net. Parses renderer_intents.h,
# bridge_component.h and bridge_component_intents.cpp (WILLEQ_REPO_ROOT) and
# asserts every RendererIntent alternative has exactly one canonical dispatch
# line, one declaration and one definition -- and no handler exists without an
# alternative. Anchored on std::variant_size_v, so there is no golden file.
ADD_EXECUTABLE(test_bridge_intent_table
    test_bridge_intent_table.cpp
)
TARGET_LINK_LIBRARIES(test_bridge_intent_table GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_bridge_intent_table)

# T295: parses src/client/graphics and pins the first-person camera to ONE
# formula (repositionFirstPersonCamera()), the tunables to one TU, and the
# two unreachable first-person helpers T295 deleted to staying deleted.
ADD_EXECUTABLE(test_first_person_camera_single_formula
    test_first_person_camera_single_formula.cpp
)
TARGET_LINK_LIBRARIES(test_first_person_camera_single_formula GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_first_person_camera_single_formula)

# T286: parses eq.h and include/client/state/*.h and asserts EverQuest keeps
# no second copy of a state module's member, except the documented list.
ADD_EXECUTABLE(test_duplicate_state_stores
    test_duplicate_state_stores.cpp
)
TARGET_LINK_LIBRARIES(test_duplicate_state_stores GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_duplicate_state_stores)

# T266: EQE1TextureIndex::resolve -- candidate fast path, R2 late-open for a
# name no candidate holds (the T263 post-load spawn FATAL), nothing for a name
# in no archive (decision #2 keeps that fatal).
ADD_EXECUTABLE(test_eqe1_texture_index
    test_eqe1_texture_index.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_texture_index.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_eqe1_texture_index PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${JSONCPP_INCLUDE_DIRS})
TARGET_COMPILE_DEFINITIONS(test_eqe1_texture_index PRIVATE
    EQE1_TEXTURE_INDEX_JSON="${CMAKE_SOURCE_DIR}/data/eqe1_texture_index.json")
TARGET_LINK_LIBRARIES(test_eqe1_texture_index GTest::gtest_main ${JSONCPP_LIBRARIES}
    ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_eqe1_texture_index)

# T284: a composite sheet opens ONE pack, not the whole sheet directory.
# Index-level guard (sheet names are self-owned, so resolve() can only name one
# pack) plus an integration guard that shells out to gles2_zone_benchmark --
# the only x86 harness that drives the composite path with no server -- the
# same popen() pattern test_entity_viewer_regression uses. Both skip without
# the NFS assets. The benchmark dependency is graphics-gated.
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_eqe1_composite_open
        test_eqe1_composite_open.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_texture_index.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_eqe1_composite_open PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${JSONCPP_INCLUDE_DIRS})
    TARGET_COMPILE_DEFINITIONS(test_eqe1_composite_open PRIVATE
        EQE1_TEXTURE_INDEX_JSON="${CMAKE_SOURCE_DIR}/data/eqe1_texture_index.json"
        GLES2_ZONE_BENCHMARK_BIN="$<TARGET_FILE:gles2_zone_benchmark>")
    TARGET_LINK_LIBRARIES(test_eqe1_composite_open GTest::gtest_main ${JSONCPP_LIBRARIES}
        ${FMT_LIB})
    ADD_DEPENDENCIES(test_eqe1_composite_open gles2_zone_benchmark)
    GTEST_DISCOVER_TESTS(test_eqe1_composite_open)
ENDIF()
# T268: loot window actions (take one item, loot all, close) driven through
# the real CommandProcessor -> ActionDispatcher -> EqActionHandler ->
# EverQuest/CombatManager chain and asserted on the wire via the T268
# QueuePacket capture seam (EverQuest::Test_GetQueuedPackets). Links
# willeq_core the same way test_eq_action_handler_vendor does.
ADD_EXECUTABLE(test_loot_window_actions
    test_loot_window_actions.cpp
)
TARGET_LINK_LIBRARIES(test_loot_window_actions GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_loot_window_actions PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_loot_window_actions)

# T15: InputActionBridge tests -- the T12 follow-on. Driven through a
# scriptable IInputHandler fake (action_test_fakes.h); observes both the
# ActionCallback and the recording handler behind a real ActionDispatcher.
# CommandProcessor is linked for the console-forwarding path. Same rule as
# the other two: no Irrlicht, no EverQuest.
ADD_EXECUTABLE(test_input_action_bridge
    test_input_action_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_social.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher_guild_misc.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_bank.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_train.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_loot.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_registration.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_social.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_world.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor_spells_gm.cpp  # J04
    ${CMAKE_SOURCE_DIR}/src/client/action/input_action_bridge.cpp
    # T174: the bridge now reads HotbarModel to route keyboard hotbar presses
    # by slot content type. hotbar_model.cpp is dependency-light (event_bus +
    # an inline GameStateBridge::pushEvent, both already in the state set).
    ${CMAKE_SOURCE_DIR}/src/client/hotbar/hotbar_model.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_input_action_bridge GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})  # T174: hotbar_model.cpp reads/writes jsoncpp
GTEST_DISCOVER_TESTS(test_input_action_bridge)

# T40/T227: GameStateSyncBridge tests -- since T227 the decorator projects
# nothing (one GameState, written at the source), so this covers only the
# forwarding hop: single-stage applyEvent() and the T220 stage/forward pair.
# Driven through a local recording fake standing in for
# IrrlichtBridge/ConsoleBridge, so no graphics or state deps needed.
ADD_EXECUTABLE(test_game_state_sync_bridge
    test_game_state_sync_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/client/bridge/game_state_sync_bridge.cpp
)
TARGET_LINK_LIBRARIES(test_game_state_sync_bridge GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_game_state_sync_bridge)

# T47: bridge-pointer teardown hygiene (EverQuest::m_bridge + the four
# managers'). Needs a real EverQuest (for setBridge()/GetHotbarModel()), so
# links willeq_core the same way test_spell_manager does.
ADD_EXECUTABLE(test_bridge_teardown
    test_bridge_teardown.cpp
)
TARGET_LINK_LIBRARIES(test_bridge_teardown GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_bridge_teardown PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_bridge_teardown)

# Test combat enums and structures
ADD_EXECUTABLE(test_combat
    test_combat.cpp
)
TARGET_LINK_LIBRARIES(test_combat GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_combat)

# T23: CombatManager behavior coverage (targeting, auto-attack/fire,
# cycling, consider, state machine, config round-trips, the loot-path
# blocking-sleep BUG_ pin). Reaches the live CombatManager through a bare
# EverQuest via GetCombatManager() -- Option A, no seam/interface
# extraction (refactor/T23_spell_combat_seam_scoping.md). Same replay-seam
# idiom as test_zone_entities.cpp (T18); uses willeq_core for the same
# reason.
ADD_EXECUTABLE(test_combat_manager
    test_combat_manager.cpp
)
TARGET_LINK_LIBRARIES(test_combat_manager GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_combat_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_combat_manager)

# T308 Leg 1: hunt-target filtering classifies on the server-sent body type,
# not on CombatManager::IsHumanoid()'s hand-written race-id table (the FIFTH
# live copy of the pre-JSON numbering; T267/T270/T271/T304 killed the other
# four). Same seam as test_combat_manager.cpp -- bare EverQuest via MakeEq(),
# live CombatManager, spawns replayed as real NewSpawn packets -- but every
# assertion is on the target the client ENGAGES through /pathfinding on +
# /combat on + /hunt on, never on the private predicate.
ADD_EXECUTABLE(test_hunt_target_body_type
    test_hunt_target_body_type.cpp
)
TARGET_LINK_LIBRARIES(test_hunt_target_body_type GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_hunt_target_body_type PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_hunt_target_body_type)

# T308 Leg 2: registry 2 (action::CommandProcessor) answers into the on-screen
# chat window as well as stdout. Drives the user path -- text typed into the
# chat window -> EverQuest::ProcessChatInput() -> registry 1 miss -> the
# fallback Application installs in wireUnifiedCommandDispatch() (the same
# production function that installs the chat sink) -- and asserts on the
# SystemMessage events reaching the bridge.
ADD_EXECUTABLE(test_command_output_routing
    test_command_output_routing.cpp
)
TARGET_LINK_LIBRARIES(test_command_output_routing GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_command_output_routing PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_command_output_routing)

# T28: coverage for the four eq_commands.cpp readers repointed from the
# never-populated state-layer CombatState target to the authoritative
# CombatManager (refactor/T28_state_layer_readers.md Part 2). Same seam as
# test_combat_manager.cpp (bare EverQuest via MakeEq(), live CombatManager
# via GetCombatManager()) plus ProcessChatInput() -- the same public entry
# point production chat input uses -- to reach the command-registry lambda
# handlers in eq_commands.cpp, and the bridge's SystemMessage event (same
# idiom as test_zone_chat.cpp) to observe AddChatSystemMessage() output.
# T102 already removed assist/shield/duel's cases (migrated to
# action::CommandProcessor), leaving only /bandage on this seam. T135
# MIGRATES bandage too and deletes its eq_commands.cpp registration --
# test_eq_commands_assist.cpp is REMOVED (not repointed), same shape as
# T102's test_eq_commands_attack.cpp removal below; equivalent coverage now
# lives in test_command_processor.cpp's T135 section (FakeActionHandler-
# based, matching every other migrated family). See
# refactor/T135_migrate_utility_family.md.

# T136: vendor/vendorlist/buy/sell coverage -- test_eq_commands_vendor.cpp
# (T48, the old game-registry ProcessChatInput seam) DELETED and replaced by
# test_eq_action_handler_vendor.cpp: same MakeEq()/TestBridge/ReplayZone()-
# driven vendor-open setup (HC_OP_ShopRequest, HC_OP_ItemPacket) and
# bridge.drainIntents()/drainEvents() assertions, but calling the new real
# entry points directly (EverQuest::RequestVendorToggle/RequestVendorBuy/
# RequestVendorSell, EqActionHandler::vendorList()) since eq_commands.cpp's
# RegisterVendorCommands() no longer exists. Argument-parsing cases moved to
# test_command_processor.cpp's T136 section (RecordingActionHandler, no wire
# replay needed). See refactor/T136_migrate_merchant_family.md.
# test_eq_action_handler_raid_guards.cpp (T183): the raid command guards --
# not-in-a-raid, not-the-leader, not-a-member, bad loot type -- against a real
# EverQuest whose raid state is established through the production packet path
# (ReplayZone + HC_OP_RaidUpdate), never by poking a setter. Argument arity
# lives in action::CommandProcessor and is covered in test_command_processor.
# See refactor/T183_gate_raid_remove.md.
ADD_EXECUTABLE(test_eq_action_handler_raid_guards
    test_eq_action_handler_raid_guards.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_raid_guards GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_raid_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_raid_guards)

ADD_EXECUTABLE(test_eq_action_handler_vendor
    test_eq_action_handler_vendor.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_vendor GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_vendor PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_vendor)

# T50: /bank, /train coverage (eq_commands_spells.cpp RegisterSpellCommands())
# -- same seam test_eq_commands_vendor.cpp (T48) used to use, before T136
# deleted it; see test_eq_action_handler_vendor.cpp above for the retargeted
# replacement. /bank asserts the pushed
# BankerInteractIntent's fields (bridge.drainIntents()); /train's new
# distance/class pre-validation is a direct check, not an intent, so those
# cases assert the printed SystemMessage text itself, per the plan's bar.
ADD_EXECUTABLE(test_eq_commands_bank_train
    test_eq_commands_bank_train.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_bank_train GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_bank_train PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_bank_train)

# T269: bank coin deposit / withdraw / convert (T264 F3). Real EverQuest:
# RequestBankCoinMove/Convert push the kept Bank* intents (refuse with the
# window closed); the eq_bridge.cpp arms' OP_MoveCoin bytes are asserted on
# a real DaybreakConnection over FakeUdpTransport (T234 idiom), plus the
# local currency projection (eqemu echoes nothing on MoveCoin). No network,
# no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_bank_coins
    test_eq_bank_coins.cpp
)
TARGET_LINK_LIBRARIES(test_eq_bank_coins GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_bank_coins PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_bank_coins)

# T273: read a book / note item (T264 F2). Command -> ReadItemIntent ->
# OP_ReadBook wire bytes (real DaybreakConnection over FakeUdpTransport,
# same idiom as test_eq_bank_coins above) -> reply parsed and surfaced into
# the chat window. See refactor/T273_read_book.md.
ADD_EXECUTABLE(test_read_book
    test_read_book.cpp
)
TARGET_LINK_LIBRARIES(test_read_book GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_read_book PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_read_book)

# T276: scribe a spell scroll (T264 F5). /scribe -> ScribeSpellIntent ->
# OP_MoveItem-then-OP_MemorizeSpell(scribing=0) wire bytes (real
# DaybreakConnection over FakeUdpTransport) -> the server echo adds the
# spell and consumes the scroll. Needs willeq.db for the class/level
# refusals and the spell ids (self-skips without it, test_spell_manager's
# precedent). See refactor/T276_scribe_scroll.md.
ADD_EXECUTABLE(test_scribe_scroll
    test_scribe_scroll.cpp
)
TARGET_COMPILE_DEFINITIONS(test_scribe_scroll PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_scribe_scroll GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_scribe_scroll PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_scribe_scroll)
# T278: manual tradeskill combine + container close (T264 F7). Command ->
# TradeskillCombineIntent/TradeskillCloseIntent -> OP_TradeSkillCombine /
# OP_ClickObjectAction(open=0) wire bytes (real DaybreakConnection over
# FakeUdpTransport, same idiom as test_read_book above), plus the
# OP_ClearObject misread this unit fixed. See
# refactor/T278_tradeskill_combine.md.
ADD_EXECUTABLE(test_tradeskill_combine
    test_tradeskill_combine.cpp
)
TARGET_LINK_LIBRARIES(test_tradeskill_combine GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_tradeskill_combine PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_tradeskill_combine)

# T280: train a skill at a trainer / close the trainer window (T264 F8).
# Real EverQuest, real DaybreakConnection over FakeUdpTransport (T234 idiom):
# the OP_GMTraining reply opens a session and its CAPS are kept, /train and
# /trainclose produce the two intents T264 listed as blocked, and the
# OP_GMTrainSkill / OP_GMEndTraining bytes are asserted field by field
# against eqemu's own struct layout (GMSkillChange_Struct's npcid is a
# uint16, and its per-field offset comments in eqemu are all wrong).
ADD_EXECUTABLE(test_train_skill
    test_train_skill.cpp
)
TARGET_LINK_LIBRARIES(test_train_skill GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_train_skill PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_train_skill)

# T140: final 16 of the game-registry migration programme (equip excluded).
# Real-EqActionHandler/EverQuest coverage for showLocation/warp/
# showOrSetZoning/campNow/debugLevel/perfReport/timeOfDay -- the net-new
# surface this unit added. sit/stand/move/moveto/follow/stopfollow REUSE
# pre-existing IActionHandler methods with no new state to exercise here
# (covered at the CommandProcessor layer, test_command_processor.cpp);
# bank/train stay in test_eq_commands_bank_train.cpp above (retargeted from
# ProcessChatInput to the new EverQuest::RequestBankToggle()/
# RequestTraining() entry points in this same unit). See
# refactor/T140_migrate_final_seventeen.md.
ADD_EXECUTABLE(test_eq_action_handler_movement_utility
    test_eq_action_handler_movement_utility.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_movement_utility GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_movement_utility PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_movement_utility)

# T66: /equip coverage (eq_commands.cpp RegisterEquipmentCommands()) -- same
# seam test_eq_commands_vendor.cpp (T48) used to use (see
# test_eq_action_handler_vendor.cpp above) and test_eq_commands_bank_train.cpp
# still uses. T136 (Merchant family migration) deliberately did NOT migrate
# /equip -- it stays registered in eq_commands.cpp/m_command_registry, so
# this file is unaffected; see refactor/T136_migrate_merchant_family.md.
# Asserts the pushed MoveItemIntent's fields directly (bridge.drainIntents())
# -- the SAME intent the inventory UI's drag/drop gesture pushes -- plus
# argument parsing and slot-validation refusal messages. The live leg
# (SendMoveItem's outgoing packet, and whether a real server actually equips
# a real item) is explicitly outstanding -- sibling T65 holds the eqemu lock
# this round; see refactor/T66_camera_and_equip_reachability.md's Review.
ADD_EXECUTABLE(test_eq_commands_equip
    test_eq_commands_equip.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_equip GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_equip PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_equip)

# T64: parseArguments()-only coverage for the video-capture evidence tool's
# CLI flags (--dump-frames-dir/--dump-every/--dump-max-frames). Static
# method, no renderer/GL context needed -- see the file header comment for
# what is and isn't covered here.
ADD_EXECUTABLE(test_video_capture_flags
    test_video_capture_flags.cpp
)
TARGET_LINK_LIBRARIES(test_video_capture_flags GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_video_capture_flags PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_video_capture_flags)

# T51 added coverage here for the then-consolidated attack path
# (eq_commands_movement.cpp's registry-1 attack/stopattack/aa). T102
# MIGRATES attack/stopattack/aa/target (plus assist/shield/duel from
# test_eq_commands_assist.cpp) to action::CommandProcessor and deletes the
# game-registry registrations this file's ProcessChatInput()-seam tests
# depended on -- test_eq_commands_attack.cpp is REMOVED (not repointed);
# equivalent coverage now lives in test_command_processor.cpp's T102
# section (FakeActionHandler-based, matching every other migrated family).
# See refactor/T102_migrate_combat_social.md.

# G03: the registration-ORDER net for CommandProcessor::registerBuiltinCommands().
# Pure text parse of src/client/action/command_processor_registration.cpp
# (WILLEQ_REPO_ROOT, set globally in tests/CMakeLists.txt) against
# tests/goldens/command_registration_order_g03.txt -- the registry stores
# commands in a std::map, so no runtime enumeration can witness registration
# order, and order is what decides shadowing (J04, T097/T098). No link deps.
ADD_EXECUTABLE(test_command_registration_order
    test_command_registration_order.cpp
)
TARGET_LINK_LIBRARIES(test_command_registration_order GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_command_registration_order)
