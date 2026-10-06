# tests/cmake/action_handlers.cmake -- included from tests/CMakeLists.txt.
# EqActionHandler families: casting, transforms, GM commands, who/inspect, trade, raid, login/world seam.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T240: castSpell()/castSpellOnTarget()/memorizeSpell() against the real
# EverQuest + EQ::SpellManager + a real DaybreakConnection over
# FakeUdpTransport (T234 WireHarness idiom), gems loaded through the same
# setSpellGems() call the PlayerProfile handler makes, outbound
# OP_CastSpell / OP_MemorizeSpell bytes asserted. Replaces T93's
# test_eq_action_handler_transforms (its two tests observed the
# CombatManager gem map/IsCasting() that T240 deleted; the 1-based gem
# conversion they pinned is pinned here on the real store). DB-gated cases
# skip without data/willeq.db. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_cast
    test_eq_action_handler_cast.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eq_action_handler_cast PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_eq_action_handler_cast GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_cast PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_cast DISCOVERY_TIMEOUT 120)

# T234: the five transforms T93 listed but could not reach (targetSelf,
# targetGroupMember, hailTarget, setMovementMode, setPositionState) pinned
# against the real EverQuest: entities/pet/group via replayed NewSpawn +
# GroupUpdate packets, and a real DaybreakConnection over FakeUdpTransport
# installed as m_zone_connection so the outbound TargetMouse/ChannelMessage
# bytes can be asserted. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_transforms_real
    test_eq_action_handler_transforms_real.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_transforms_real GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_transforms_real PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_transforms_real DISCOVERY_TIMEOUT 120)

# T167: PlayerProfile appearance parse + the /appearance merge. Pins the seven
# PlayerProfile offsets (verified against eqemu's titanium_structs.h) and the
# merge that keeps an unnamed field from being reset to zero on the server.
# Same headless EverQuest + EqActionHandler idiom as the transforms tests
# above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_profile_appearance
    test_profile_appearance.cpp
)
TARGET_LINK_LIBRARIES(test_profile_appearance GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_profile_appearance PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_profile_appearance DISCOVERY_TIMEOUT 120)

# T217: group-accept inviter name (the T99 aliasing bug) and the OP_GMHideMe
# sender body (the T148 misroute). Headless EverQuest + TestBridge + the T88
# send-guard bypass; the hideme body is read through a T167-style seam.
ADD_EXECUTABLE(test_group_accept_gm_hideme
    test_group_accept_gm_hideme.cpp
)
TARGET_LINK_LIBRARIES(test_group_accept_gm_hideme GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_group_accept_gm_hideme PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_group_accept_gm_hideme DISCOVERY_TIMEOUT 120)

# T229: the two remaining T148 GM-sender defects -- SendGMSummon's zeroed
# coordinates (eqemu READS them) and ZoneProcessGMFind's int32 decode of a
# float reply. Pins both structs against eqemu's field order/types.
ADD_EXECUTABLE(test_gm_summon_find
    test_gm_summon_find.cpp
)
TARGET_LINK_LIBRARIES(test_gm_summon_find GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_gm_summon_find PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_gm_summon_find DISCOVERY_TIMEOUT 120)

# K02 (Batch K): the extracted eqt::gm::GmComponent, driven directly
ADD_EXECUTABLE(test_gm_component
    test_gm_component.cpp
)
TARGET_LINK_LIBRARIES(test_gm_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_gm_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_gm_component DISCOVERY_TIMEOUT 120)

# K12 (Batch K): the extracted eqt::commands::CommandsComponent (commands +
# combat_actions), driven directly
ADD_EXECUTABLE(test_commands_component
    test_commands_component.cpp
)
TARGET_LINK_LIBRARIES(test_commands_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_commands_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_commands_component DISCOVERY_TIMEOUT 120)

# K13 (Batch K): the three CONTEXT SERVICES -- eqt::ChatOutput,
# eqt::PlayerFacade and eqt::PacketSender, driven directly.
FOREACH(k13svc test_chat_output test_player_facade test_packet_sender)
    ADD_EXECUTABLE(${k13svc} ${k13svc}.cpp)
    TARGET_LINK_LIBRARIES(${k13svc} GTest::gtest_main willeq_core)
    IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
        TARGET_INCLUDE_DIRECTORIES(${k13svc} PRIVATE ${IRRLICHT_INCLUDE_DIR})
    ENDIF()
    GTEST_DISCOVER_TESTS(${k13svc} DISCOVERY_TIMEOUT 120)
ENDFOREACH()

# T404 Leg C (board B254): the OP_MemorizeSpell response's gem arms, which did
# not exist -- the handler acted only on scribing == 0 (a spellbook scribe).
ADD_EXECUTABLE(test_memorize_into_occupied_gem test_memorize_into_occupied_gem.cpp)
TARGET_COMPILE_DEFINITIONS(test_memorize_into_occupied_gem PRIVATE
    WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_memorize_into_occupied_gem GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_memorize_into_occupied_gem PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_memorize_into_occupied_gem DISCOVERY_TIMEOUT 120)

# T404 Leg B (board B251): the self entity row is a PROJECTION of the one
# position store, and must follow both funnels that move the player.
ADD_EXECUTABLE(test_self_entity_position test_self_entity_position.cpp)
TARGET_LINK_LIBRARIES(test_self_entity_position GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_self_entity_position PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_self_entity_position DISCOVERY_TIMEOUT 120)

# K06 (Batch K): the extracted eqt::entities::EntitiesComponent, driven
# directly
ADD_EXECUTABLE(test_entities_component
    test_entities_component.cpp
)
TARGET_LINK_LIBRARIES(test_entities_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_entities_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_entities_component DISCOVERY_TIMEOUT 120)

# T374 Leg A (BOARD B136): pending door clicks cleared at zone change.
ADD_EXECUTABLE(test_door_click_zone_reset
    test_door_click_zone_reset.cpp
)
TARGET_LINK_LIBRARIES(test_door_click_zone_reset GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_door_click_zone_reset PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_door_click_zone_reset DISCOVERY_TIMEOUT 120)

# T435 Leg B (BOARD B428): the OP_MoveDoor action byte is only "open" or
# "close" after invert_state is applied (eqemu zone/doors.cpp:37-40).
ADD_EXECUTABLE(test_door_invert_state
    test_door_invert_state.cpp
)
TARGET_LINK_LIBRARIES(test_door_invert_state GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_door_invert_state PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_door_invert_state DISCOVERY_TIMEOUT 120)

# T435 Leg A (BOARD B425): doors have collision, and the map that carries it
# finally has a writer.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_door_collision
        test_door_collision.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_door_collision PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_door_collision GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_door_collision DISCOVERY_TIMEOUT 120)
ENDIF()

# T438 Leg C (BOARD B424): a discrete relocation cancels an in-flight jump.
ADD_EXECUTABLE(test_jump_relocation
    test_jump_relocation.cpp
)
TARGET_LINK_LIBRARIES(test_jump_relocation GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_jump_relocation DISCOVERY_TIMEOUT 120)

# T438 (BOARD B421/B422/B423): every door-motion constant, measured against the
# official client, plus the lift cycle that plays them.
ADD_EXECUTABLE(test_door_motion
    test_door_motion.cpp
)
TARGET_LINK_LIBRARIES(test_door_motion GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_door_motion PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_door_motion DISCOVERY_TIMEOUT 120)

# T437 (BOARD B435, B436): the ground query's honest answer shape, the two
# decisions that read it, and HCMap::FindBestZ() against hand-built triangle
# sets (floor below / ceiling only / both / empty column).
ADD_EXECUTABLE(test_ground_sentinels
    test_ground_sentinels.cpp
)
TARGET_LINK_LIBRARIES(test_ground_sentinels GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_ground_sentinels PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_ground_sentinels DISCOVERY_TIMEOUT 120)

# T376 Leg A (BOARD B140): the door / world-object click senders go out
# through eqt::PacketSender (null-guard, T268 seam, session recorder).
ADD_EXECUTABLE(test_click_senders_packet_sender
    test_click_senders_packet_sender.cpp
)
TARGET_LINK_LIBRARIES(test_click_senders_packet_sender GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_click_senders_packet_sender PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_click_senders_packet_sender DISCOVERY_TIMEOUT 120)

# T379 (BOARD B146): the login-stream and world-stream senders go out through
# eqt::PacketSender -- the R01 recorder and the T268 seam now see the client's
# half of the login/world handshake. WILLEQ_SOURCE_DIR backs the census pin
# case, which greps the routed TU for a direct connection->QueuePacket().
ADD_EXECUTABLE(test_login_world_senders_packet_sender
    test_login_world_senders_packet_sender.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_senders_packet_sender GTest::gtest_main willeq_core)
TARGET_COMPILE_DEFINITIONS(test_login_world_senders_packet_sender
    PRIVATE WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_senders_packet_sender PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_senders_packet_sender DISCOVERY_TIMEOUT 120)

# T169: OP_RaidUpdate decoding -- the action codes against eqemu's
# common/raid.h and the per-action body shapes (actions 8, 10, 14 and 30 do
# NOT have `parameter` where the general body does). Links willeq_core for
# EQT::DecodeRaidUpdate + EQ::Net::Packet; no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_raid_update_decode
    test_raid_update_decode.cpp
)
TARGET_LINK_LIBRARIES(test_raid_update_decode GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_raid_update_decode PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_raid_update_decode DISCOVERY_TIMEOUT 120)

# T177: OP_Bind_Wound reply outcome mapping. Pure function, no network/graphics;
# links only the single TU under test.
ADD_EXECUTABLE(test_bind_wound_outcome
    test_bind_wound_outcome.cpp
    ${CMAKE_SOURCE_DIR}/src/client/bind_wound_outcome.cpp
)
TARGET_LINK_LIBRARIES(test_bind_wound_outcome GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_bind_wound_outcome DISCOVERY_TIMEOUT 120)

# T178: OP_WhoAllResponse %1 count substitution. Pure function, no deps.
ADD_EXECUTABLE(test_who_format
    test_who_format.cpp
    ${CMAKE_SOURCE_DIR}/src/client/who_format.cpp
)
TARGET_LINK_LIBRARIES(test_who_format GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_who_format DISCOVERY_TIMEOUT 120)

# T224: OP_WhoAllResponse entry parsing/formatting (pure, who_response.cpp)
# plus the handler end-to-end through the ReplayZonePacket seam, observed as
# SystemMessage bridge events (same idiom as test_zone_chat.cpp).
ADD_EXECUTABLE(test_who_response
    test_who_response.cpp
)
TARGET_LINK_LIBRARIES(test_who_response GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_who_response PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
# T271: /who renders the race name, which is served from race_models now --
# the fixture opens the shipped db the way EverQuest::OpenDatabase() does.
TARGET_COMPILE_DEFINITIONS(test_who_response PRIVATE
    WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
GTEST_DISCOVER_TESTS(test_who_response DISCOVERY_TIMEOUT 120)

# T170: OP_InspectAnswer generation -- the field inversion between the request
# and the answer (TargetID is the requester in an answer, the inspected player
# in a request) and the sender itself through a real headless EverQuest.
ADD_EXECUTABLE(test_inspect_answer
    test_inspect_answer.cpp
)
TARGET_LINK_LIBRARIES(test_inspect_answer GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_inspect_answer PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_inspect_answer DISCOVERY_TIMEOUT 120)

# T95: regression tests for the previously-unvalidated int->enum casts in
# EqActionHandler::setMovementMode()/setPositionState()/sendPetCommand() --
# T93's deferred finding. MovementMode/PositionState are plain enums with no
# fixed underlying type, so an out-of-range static_cast into them was
# undefined behaviour, not merely a wrong result. Same headless EverQuest +
# EqActionHandler idiom as T88/T90/T93 above -- no network, no graphics, no
# eqemu.
ADD_EXECUTABLE(test_eq_action_handler_enum_guards
    test_eq_action_handler_enum_guards.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_enum_guards GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_enum_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_enum_guards DISCOVERY_TIMEOUT 120)

# T103: regression tests proving EqActionHandler's trade methods
# (requestTrade/acceptTrade/cancelTrade/declineTrade) read precondition/
# display state from state::TradeState (the projection T103 added) rather
# than reaching into TradeManager directly. Same headless EverQuest +
# EqActionHandler idiom as T95 above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_trade
    test_eq_action_handler_trade.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_trade GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_trade PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_trade DISCOVERY_TIMEOUT 120)

# T101: real-EqActionHandler coverage for the SpellManager-backed methods
# the Spells+Pet migration added/repointed (beginCastFromGem(),
# interruptCast() [repointed from CombatManager], forgetSpell() [repointed
# from a stub], memorizeSpellByName()). Same headless EverQuest +
# EqActionHandler idiom as T88/T90/T93/T95 above -- no network, no
# graphics, no eqemu. DB-dependent cases follow test_spell_manager.cpp's
# hasDb() + GTEST_SKIP precedent.
ADD_EXECUTABLE(test_eq_action_handler_spell_pet
    test_eq_action_handler_spell_pet.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eq_action_handler_spell_pet PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_eq_action_handler_spell_pet GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_spell_pet PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_spell_pet DISCOVERY_TIMEOUT 120)

# T221 (Option A stage 2): every READ-model domain the GameStateSyncBridge
# projection was the sole writer of (pet, spells, entity position/appearance,
# combat target/auto-attack, inventory occupancy) is now written at the source
# (EverQuest::m_game_state) by the owning handler. Each case runs with and
# without a bridge attached (the no-bridge leg proves the writes precede the
# bridge guards). Headless EverQuest + ReplayZonePacket seam, same idiom as
# test_combat_manager / test_eq_action_handler_spell_pet (DB-dependent cast
# case follows the hasDb() + GTEST_SKIP precedent). Own main() registers the
# global willeq.db, so gtest_main is NOT linked.
ADD_EXECUTABLE(test_game_state_source_writes
    test_game_state_source_writes.cpp
)
TARGET_COMPILE_DEFINITIONS(test_game_state_source_writes PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_game_state_source_writes GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_game_state_source_writes PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_game_state_source_writes DISCOVERY_TIMEOUT 120)

# T288: position/heading, spawn id and zone identity have one store each.
# Coherence assertions across the public EverQuest accessors and the state
# layer, driven through the real packet handlers (replay seam). No network,
# no graphics, no eqemu.
ADD_EXECUTABLE(test_position_store_coherence
    test_position_store_coherence.cpp
)
TARGET_LINK_LIBRARIES(test_position_store_coherence GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_position_store_coherence PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_position_store_coherence DISCOVERY_TIMEOUT 120)

# T05: login/world replay seam smoke test (ReplayLoginPacket/ReplayWorldPacket
# -> DispatchLoginPacket/DispatchWorldPacket). Same replay-seam idiom as
# test_zone_connection_flow above -- no network, no graphics, no live
# connection -- so safe to auto-discover.
ADD_EXECUTABLE(test_login_world_seam
    test_login_world_seam.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_seam GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_seam PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_seam DISCOVERY_TIMEOUT 120)

# T18: entity-domain zone-stream tests (eq_entities.cpp) -- real-capture
# full-stream replay + synthetic Spawn_Struct decode and malformed-packet
# guards. Same replay-seam idiom as test_zone_connection_flow.
ADD_EXECUTABLE(test_zone_entities
    test_zone_entities.cpp
)
TARGET_LINK_LIBRARIES(test_zone_entities GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_entities PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_entities DISCOVERY_TIMEOUT 120)

# T18: chat-domain zone-stream tests (eq_chat.cpp) -- ChannelMessage channel
# mapping, /reply tracking, size guards, and real-capture chat replay,
# observed through GameStateBridge events.
ADD_EXECUTABLE(test_zone_chat
    test_zone_chat.cpp
)
TARGET_LINK_LIBRARIES(test_zone_chat GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_chat PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_chat DISCOVERY_TIMEOUT 120)

# T06: login/world flow coverage (real-capture replay, synthetic sequencing,
# and robustness cases) -- the thing the T05 seam above was built for. Same
# replay-seam idiom -- no network, no graphics, no live connection -- so safe
# to auto-discover. Real-capture replay self-skips when data/captures/session.sleq
# is absent.
ADD_EXECUTABLE(test_login_world_flow
    test_login_world_flow.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_flow GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_flow PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_flow DISCOVERY_TIMEOUT 120)

# B888 (#204): native character creation -- the server's rules and tables,
# data/char_create.tsv (the era filter), --create-character, the four packets
# byte for byte and the world-stage flow through the replay seam.
ADD_EXECUTABLE(test_char_create
    test_char_create.cpp
)
TARGET_LINK_LIBRARIES(test_char_create GTest::gtest_main willeq_core)
TARGET_COMPILE_DEFINITIONS(test_char_create PRIVATE WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_char_create PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_char_create DISCOVERY_TIMEOUT 120)

# T378 (BOARD B145): every zone-stream sender goes out through
# eqt::PacketSender::SafeQueueZone -- null-guard (the ten B140 crash-shape
# sites) plus the R01 session recorder, one case pair per family.
ADD_EXECUTABLE(test_direct_senders_packet_sender
    test_direct_senders_packet_sender.cpp
)
TARGET_LINK_LIBRARIES(test_direct_senders_packet_sender GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_direct_senders_packet_sender PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_direct_senders_packet_sender DISCOVERY_TIMEOUT 120)

# T378 (BOARD B145): the direct-zone-sender census as a source-reader pin --
# a new ctx_.zone_connection->QueuePacket() anywhere under src/client or
# include/client fails ctest, naming the file and line. Pure source scan; no
# artefact, no server, no display.
ADD_EXECUTABLE(test_direct_sender_census
    test_direct_sender_census.cpp
)
TARGET_LINK_LIBRARIES(test_direct_sender_census GTest::gtest_main)
TARGET_COMPILE_DEFINITIONS(test_direct_sender_census PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
GTEST_DISCOVER_TESTS(test_direct_sender_census DISCOVERY_TIMEOUT 120)
