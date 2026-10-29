# tests/cmake/state.cmake -- included from tests/CMakeLists.txt.
# EventBus + state-module unit tests (T02).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T02: EventBus unit tests (src/client/state/event_bus.cpp). No graphics,
# no Irrlicht -- links only the EventBus TU itself.
ADD_EXECUTABLE(test_event_bus
    test_event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
)
TARGET_LINK_LIBRARIES(test_event_bus GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_event_bus DISCOVERY_TIMEOUT 120)

# B1183 (#502): the client's per-spawn stand-state byte (+0x251) as one
# StandState owner per spawn (include/client/state/stand_state.h). Header-only
# machine -- device-free, links only gtest. Pins every transition (the direct
# writes, the setter's gates, the B1174 rule) and the B1174 reader gate.
ADD_EXECUTABLE(test_stand_state
    test_stand_state.cpp
)
TARGET_LINK_LIBRARIES(test_stand_state GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_stand_state DISCOVERY_TIMEOUT 120)

# T02: state module unit tests, part 1 (EntityManager, PlayerState,
# SpellState, InventoryState -- the priority-1 modules). No graphics, no
# Irrlicht.
ADD_EXECUTABLE(test_state_modules
    test_state_modules.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    # T173: EntityManager ranks a living entity above its own corpse and
    # asks the ONE shared predicate; the TU is dependency-free by design.
    ${CMAKE_SOURCE_DIR}/src/client/corpse_predicate.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
)
TARGET_LINK_LIBRARIES(test_state_modules GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_state_modules DISCOVERY_TIMEOUT 120)

# T02: state module unit tests, part 2 (GroupState, PetState, WorldState,
# CombatState, GameState; TradeskillState deleted T232). Split from test_state_modules
# purely to stay under the project's file-size check
# (scripts/refactor/check_file_sizes.sh warns above 1500 lines).
# game_state.cpp also pulls in door_state_manager.cpp (client/state/game_state.h
# dependency). No graphics, no Irrlicht.
ADD_EXECUTABLE(test_state_modules_secondary
    test_state_modules_secondary.cpp
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
TARGET_LINK_LIBRARIES(test_state_modules_secondary GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_state_modules_secondary DISCOVERY_TIMEOUT 120)
