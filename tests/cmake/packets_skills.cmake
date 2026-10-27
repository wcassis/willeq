# tests/cmake/packets_skills.cmake -- included from tests/CMakeLists.txt.
# Captured-packet replay, skill gates/possession, tradeskill + hotbar handlers, entity size/collision, group/chat packet shapes.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T147: captured-bytes harness (tests/captured_packets.h) -- pins packet structs
# to real observed wire bytes rather than to sizeof(). See the header's docstring.
ADD_EXECUTABLE(test_captured_packets test_captured_packets.cpp)
TARGET_LINK_LIBRARIES(test_captured_packets GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_captured_packets DISCOVERY_TIMEOUT 120)

# T149: real-EverQuest/EqActionHandler coverage for the four tradeskill recipe
# commands -- specifically that object_type is resolved from the OPEN
# container (T144's WorldObject::object_type + the drop_id
# ZoneProcessClickObjectAction records) and never supplied by the caller.
# Drives T144's real captured OP_GroundSpawn bytes through the production
# replay seam. Same headless EverQuest + EqActionHandler idiom as
# test_eq_action_handler_trade above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_tradeskill
    test_eq_action_handler_tradeskill.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_tradeskill GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_tradeskill PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_tradeskill DISCOVERY_TIMEOUT 120)

# T316: the interact-world-object no-op. Drives T144's captured OP_GroundSpawn
# bytes through a real EverQuest into EqActionHandler::findNearestWorldObject()
# and on through ActionDispatcher::interactNearestWorldObject() -- the path the
# interact keybind reaches. Headless: no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_interact_world_object
    test_interact_world_object.cpp
)
TARGET_LINK_LIBRARIES(test_interact_world_object GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_interact_world_object PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_interact_world_object DISCOVERY_TIMEOUT 120)

# T230: keyboard hotbar Item route below the action layer --
# EqActionHandler::findItemSlotByItemId (the id->slot hop) and
# EqActionHandler::useItem -> EverQuest::UseInventoryItem (the one click-
# effect sender, shared with the mouse path). Same headless EverQuest +
# RecordingBridge idiom as test_eq_action_handler_tradeskill above.
ADD_EXECUTABLE(test_eq_action_handler_hotbar_item
    test_eq_action_handler_hotbar_item.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_hotbar_item GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_hotbar_item PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_hotbar_item DISCOVERY_TIMEOUT 120)

# T239: real-EverQuest coverage for the keyboard hotbar Emote/Macro/Combat
# route below the action layer: EqActionHandler::activateHotbarCommandSlot
# -> EverQuest::ActivateHotbarCommandSlot (eq_bridge.cpp; the ONE command-slot
# body, shared with the mouse HotbarActivateIntent arm).
ADD_EXECUTABLE(test_eq_action_handler_hotbar_command
    test_eq_action_handler_hotbar_command.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_hotbar_command GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_hotbar_command PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_hotbar_command DISCOVERY_TIMEOUT 120)

# T152: the two skill-opcode bodies this client had to build from scratch
# (OP_Disarm, OP_PickPocket), pinned field-by-field at named offsets against
# eqemu's Disarm_Struct / PickPocket_Struct. Header-only -- no willeq_core, no
# network, no graphics. NOTE the authority is eqemu SOURCE, not captured wire
# bytes; see the file's docstring and T152's Review.
ADD_EXECUTABLE(test_skill_packet_bodies test_skill_packet_bodies.cpp)
TARGET_LINK_LIBRARIES(test_skill_packet_bodies GTest::gtest_main)
TARGET_INCLUDE_DIRECTORIES(test_skill_packet_bodies PRIVATE ${CMAKE_SOURCE_DIR}/include)
GTEST_DISCOVER_TESTS(test_skill_packet_bodies DISCOVERY_TIMEOUT 120)

# T159: the skill possession gate and the real per-class cap table behind it.
# Loads the SHIPPED config/skill_caps.json by path (WILLEQ_REPO_ROOT) -- the
# gate's data must be the file the client ships, not a fixture. Same headless
# EverQuest seam as test_combat_manager_*.cpp (MakeEq() + live CombatManager /
# SkillManager + TestBridge); no network, no graphics device, no eqemu.
# New file: test_command_processor.cpp is past the 3000-line FAIL ceiling.
ADD_EXECUTABLE(test_skill_possession_gate
    test_skill_possession_gate.cpp
)
TARGET_LINK_LIBRARIES(test_skill_possession_gate GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_skill_possession_gate PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_skill_possession_gate DISCOVERY_TIMEOUT 120)

# T317: the two skill gates that were not gates -- checkCombatRequirement()
# substituted "has a target" for "in combat", and checkPositionRequirement()
# returned true unconditionally AND had no caller. Same headless EverQuest
# seam as test_skill_possession_gate.cpp; no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_skill_gates
    test_skill_gates.cpp
)
TARGET_LINK_LIBRARIES(test_skill_gates GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_skill_gates PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_skill_gates DISCOVERY_TIMEOUT 120)

# B894 (GitLab #210): the Forage/Fishing precondition table and the caught
# item on the cursor. Same headless EverQuest + TestBridge seam as
# test_skill_gate_bypasses.cpp; the eqstr refusal lines come from the SHIPPED
# string table (WILLEQ_DB_REAL_PATH, as test_camp defines it), never from
# literals. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_forage_fishing
    test_forage_fishing.cpp
)
TARGET_LINK_LIBRARIES(test_forage_fishing GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_forage_fishing PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
TARGET_COMPILE_DEFINITIONS(test_forage_fishing PRIVATE
    "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
GTEST_DISCOVER_TESTS(test_forage_fishing DISCOVERY_TIMEOUT 120)

# T317: the guild opcodes' rank fields, and where the player's real rank lives
ADD_EXECUTABLE(test_guild_wire_rank
    test_guild_wire_rank.cpp
)
TARGET_LINK_LIBRARIES(test_guild_wire_rank GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_guild_wire_rank PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_guild_wire_rank DISCOVERY_TIMEOUT 120)

# K01 (Batch K): the extracted eqt::guild::GuildComponent, driven directly
ADD_EXECUTABLE(test_guild_component
    test_guild_component.cpp
)
TARGET_LINK_LIBRARIES(test_guild_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_guild_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_guild_component DISCOVERY_TIMEOUT 120)

# T162: the commands that emitted a skill opcode without passing T159's gate
ADD_EXECUTABLE(test_skill_gate_bypasses
    test_skill_gate_bypasses.cpp
)
TARGET_LINK_LIBRARIES(test_skill_gate_bypasses GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_skill_gate_bypasses PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_skill_gate_bypasses DISCOVERY_TIMEOUT 120)

# T301 Leg B: the first-person eye is inside the head and derived per race
# from the mesh. Pure-geometry parts need nothing; the shipped-mesh parts use
# the same EDT_NULL + willeq_core gate as test_race_330_loader and skip
# without the db or the client tree. NOTHING HERE RASTERIZES -- see the file
# header; T301 Leg E is the live gate.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_eye_anatomy
        test_first_person_eye_anatomy.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_eye_anatomy PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_eye_anatomy GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_first_person_eye_anatomy PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_first_person_eye_anatomy DISCOVERY_TIMEOUT 120)
ENDIF()

# T318: shrink/grow/illusion reach the renderer (wire -> event -> node scale),
# and OP_GuildInviteAccept's rank byte. EDT_NULL + willeq_core gate; the
# real-node half skips without the shipped db or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_size_change
        test_entity_size_change.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_size_change PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_size_change GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_size_change PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")"
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_size_change DISCOVERY_TIMEOUT 120)
ENDIF()

# B1172 (#491): the official client's actor scale (race / gender / spawn
# size -> drawn scale, include/client/graphics/actor_scale.h). Header-only.
ADD_EXECUTABLE(test_actor_scale
    test_actor_scale.cpp
)
TARGET_LINK_LIBRARIES(test_actor_scale GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_actor_scale DISCOVERY_TIMEOUT 120)

# T454 (board B473): a corpse holds the death pose's last frame. Same
# EDT_NULL + willeq_core gate as test_entity_size_change; the real-node half
# skips without the shipped db or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_corpse_death_pose
        test_corpse_death_pose.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_corpse_death_pose PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_corpse_death_pose GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_corpse_death_pose PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")"
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_corpse_death_pose DISCOVERY_TIMEOUT 120)
ENDIF()

# B1022 (GitLab #342): the swing plays at the model's native animation length;
# the swing rate is the server's OP_Animation cadence (100 x item delay ms).
# The source half (no device) asserts the delay-based stretch is gone from the
# production paths; the behavioral half (EDT_NULL + willeq_core, the same gate
# as test_corpse_death_pose) skips without the db or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b1022_swing_native
        test_b1022_swing_native.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b1022_swing_native PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b1022_swing_native GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_b1022_swing_native PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")"
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")"
        "EQ_CLIENT_DATA_PATH=WILLEQ_REPO_PATH(\"data/eq_client\")")
    GTEST_DISCOVER_TESTS(test_b1022_swing_native DISCOVERY_TIMEOUT 120)
ENDIF()

# B1148 (GitLab #468): the client's corpse-fade list (alpha from 1.3 s,
# removed past 3499 ms after OP_DeleteSpawn) and the per-caster cast-animation
# term. Device-free half always; the EDT_NULL half skips without the db or the
# client tree (same gate as test_b1022_swing_native).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_corpse_fade
        test_corpse_fade.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_corpse_fade PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_corpse_fade GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_corpse_fade PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")"
        "EQ_CLIENT_DATA_PATH=WILLEQ_REPO_PATH(\"data/eq_client\")")
    GTEST_DISCOVER_TESTS(test_corpse_fade DISCOVERY_TIMEOUT 120)
ENDIF()

# B1157 (GitLab #476): the caster's cast animation -- on OP_BeginCast every casting
# entity (the player included) plays the spell's CastingAnim track (field 120
# through eqgame's code table) on its model, started at the packet, held/looped
# for the channel, ended at OP_Action / interrupt; the shared helper
# (spell_cast_animation.h) is the client's and the FX harness's one code path.
# Device-free half always; the EDT_NULL half skips without the db or the
# client tree (same gate as test_corpse_fade).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_cast_animation
        test_cast_animation.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_cast_animation PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_cast_animation GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_cast_animation PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")"
        "EQ_CLIENT_DATA_PATH=WILLEQ_REPO_PATH(\"data/eq_client\")")
    GTEST_DISCOVER_TESTS(test_cast_animation DISCOVERY_TIMEOUT 120)
ENDIF()

# B973 (GitLab #291): post-load entity build scheduling -- guaranteed progress
# in every governor state, bounded per-frame work, and B966's create unit
# promoting materials before the node's first draw. Part 1 is device-free; the
# staged-build half skips without the db or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_build_scheduler
        test_entity_build_scheduler.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_build_scheduler PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_build_scheduler GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_build_scheduler PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_build_scheduler DISCOVERY_TIMEOUT 120)
ENDIF()

# T310 Leg B: collisionZOffset is the origin-to-feet distance, not half the
# bbox. Same EDT_NULL + willeq_core gate as the eye anatomy tests; the
# shipped-mesh half skips without the db or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_collision_z_offset
        test_entity_collision_z_offset.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_collision_z_offset PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_collision_z_offset GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_collision_z_offset PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_collision_z_offset DISCOVERY_TIMEOUT 120)
ENDIF()

# T320: OP_GroupUpdate carries TWO structs of the SAME 452-byte size --
# GroupJoin_Struct for groupActJoin/groupActLeave, GroupUpdate_Struct for the
# rest -- so only the action field distinguishes them. Reading a JOIN as a
# GroupUpdate_Struct took `leadersname` out of the join struct's padding and
# published a group with real members and an EMPTY leader, on the LEADER's own
# client, at formation, with no zoning involved (review/T320/). Also pins that
# CleanupZone() drops group state so the server's zone-entry refresh can
# re-establish it, instead of the client carrying a phantom group across.
ADD_EXECUTABLE(test_group_packet_shapes
    test_group_packet_shapes.cpp
)
TARGET_LINK_LIBRARIES(test_group_packet_shapes GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_group_packet_shapes PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_group_packet_shapes DISCOVERY_TIMEOUT 120)

# K04 (Batch K): the extracted eqt::group::GroupComponent, driven directly
ADD_EXECUTABLE(test_group_component
    test_group_component.cpp
)
TARGET_LINK_LIBRARIES(test_group_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_group_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_group_component DISCOVERY_TIMEOUT 120)

# K07 (Batch K): the extracted eqt::movement::MovementComponent, driven directly
ADD_EXECUTABLE(test_movement_component
    test_movement_component.cpp
)
TARGET_LINK_LIBRARIES(test_movement_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_movement_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_movement_component DISCOVERY_TIMEOUT 120)

# T343 (B032): EqActionHandler::stopAllMovement() after the game-side
# keyboard-movement cluster was deleted -- /stop must clear the movement target
# and the path, which the four Stop* calls it used to make never did.
ADD_EXECUTABLE(test_stop_all_movement
    test_stop_all_movement.cpp
)
TARGET_LINK_LIBRARIES(test_stop_all_movement GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_stop_all_movement PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_stop_all_movement DISCOVERY_TIMEOUT 120)

# T443 (board B418): OP_GuildsList parsed into a guild-id -> name lookup.
# Device-free, database-free; the census cases run against the pre-fix tree.
ADD_EXECUTABLE(test_guild_name_table
    test_guild_name_table.cpp
)
TARGET_LINK_LIBRARIES(test_guild_name_table GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_guild_name_table PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_guild_name_table DISCOVERY_TIMEOUT 120)

# K10 (Batch K): the extracted eqt::login_world::LoginWorldComponent, driven
# directly through EverQuest::LoginWorld().
ADD_EXECUTABLE(test_login_world_component
    test_login_world_component.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_component DISCOVERY_TIMEOUT 120)
# K08 (Batch K): the extracted eqt::player::PlayerComponent, driven directly,
# plus the cut -- the player-state facade that stayed on EverQuest.
ADD_EXECUTABLE(test_player_component
    test_player_component.cpp
)
TARGET_LINK_LIBRARIES(test_player_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_player_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_player_component DISCOVERY_TIMEOUT 120)

# K09 (Batch K): the extracted eqt::zone::ZoneComponent, driven directly --
# the accessor identity, the moved handshake/zone-out state and its seams, and
# the K09 boundary (what deliberately stayed on EverQuest).
ADD_EXECUTABLE(test_zone_component
    test_zone_component.cpp
)
TARGET_LINK_LIBRARIES(test_zone_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_component DISCOVERY_TIMEOUT 120)

# G01 (Batch G): the opcode-coverage net for the DispatchZonePacket table --
# the golden list of every case label the pre-G01 switch had, asserted against
# ZoneOpcodeTable() + IgnoredZoneOpcodes() in both directions.
ADD_EXECUTABLE(test_zone_dispatch_table
    test_zone_dispatch_table.cpp
)
TARGET_LINK_LIBRARIES(test_zone_dispatch_table GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_dispatch_table PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_dispatch_table DISCOVERY_TIMEOUT 120)

# K11 (Batch K): the extracted eqt::bridge::BridgeComponent, driven directly --
# the accessor identity, the FIVE sanctioned lifecycle forwarders that stayed
# on EverQuest, the loot cluster and the inspect/config/audio state that moved.
ADD_EXECUTABLE(test_bridge_component
    test_bridge_component.cpp
)
TARGET_LINK_LIBRARIES(test_bridge_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_bridge_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_bridge_component DISCOVERY_TIMEOUT 120)

# K16 (board B006): EverQuest::InitGraphics takes EQT::Graphics::IZoneLoadSurface*,
# the two-method zone-load surface, instead of a concrete IrrlichtRenderer*.
# The test drives the real InitGraphics body with a stub that implements only
# those two methods -- which is only possible if the narrowing is complete.
# Graphics-gated: InitGraphics itself is behind EQT_HAS_GRAPHICS, and
# zone_load_surface.h pulls RendererConfig (and so irrlicht.h) from
# renderer_types.h.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_load_surface
        test_zone_load_surface.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_load_surface PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_load_surface GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_load_surface DISCOVERY_TIMEOUT 120)
ENDIF()

# T320 (Leg C): one chat-channel name table. eq_chat.cpp built the name twice
# from two ternary chains forty lines apart and they had drifted -- auction
# missing from the console chain, channel 14 (TellEcho) missing from both.
# The last test is the guard against a third copy appearing; it reads
# src/client/core/eq_chat.cpp via WILLEQ_REPO_ROOT (set at the top of this
# file), the same shape as the T314 / T308 guards.
# K03 (Batch K): the extracted eqt::chat::ChatComponent, driven directly
ADD_EXECUTABLE(test_chat_component
    test_chat_component.cpp
)
TARGET_LINK_LIBRARIES(test_chat_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_chat_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_chat_component DISCOVERY_TIMEOUT 120)

# K05 (Batch K): the extracted eqt::merchant::MerchantComponent, driven
# directly -- construction, the ClientContext binding, the one member that
# moved (m_trainer_skill_caps) and the owner_ transition handle.
ADD_EXECUTABLE(test_merchant_component
    test_merchant_component.cpp
)
TARGET_LINK_LIBRARIES(test_merchant_component GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_merchant_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_merchant_component DISCOVERY_TIMEOUT 120)

ADD_EXECUTABLE(test_chat_channel_names
    test_chat_channel_names.cpp
)
TARGET_LINK_LIBRARIES(test_chat_channel_names GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_chat_channel_names PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_chat_channel_names DISCOVERY_TIMEOUT 120)

# T429 Leg B (board B298 entity half, board B390): the /con line, composed
# from the shipped eqstr table. Free composer -- no server, no device.
ADD_EXECUTABLE(test_consider_text
    test_consider_text.cpp
)
TARGET_LINK_LIBRARIES(test_consider_text GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_consider_text PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
TARGET_COMPILE_DEFINITIONS(test_consider_text PRIVATE
    "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
GTEST_DISCOVER_TESTS(test_consider_text DISCOVERY_TIMEOUT 120)
