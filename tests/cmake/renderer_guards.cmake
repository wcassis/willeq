# tests/cmake/renderer_guards.cmake -- included from tests/CMakeLists.txt.
# Texture/bone/attachment guards, renderer command echo, and bridge/queue ownership races.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T60: the shared bone-lookup fallback chain (bone_attachment.h) — pure
# string logic over a synthetic skeleton, no device/Irrlicht driver needed
# (just the irr::core math types, header-only via willeq_core's include
# path). Nothing tested this before convergence.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bone_attachment
        test_bone_attachment.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bone_attachment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bone_attachment GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_bone_attachment)
ENDIF()

# T92: ConstrainedTextureCache coverage — the P50 masked-variant query-method
# gap (hasAlpha/getTexture/getAlphaTexture/isETC1Texture by-name overloads
# now take applyPaletteMask) and the Part 1 empty-input-vs-decode-failure
# message distinction at getOrLoad(). EDT_NULL for the bulk of it (no
# window/GPU/DISPLAY); one fixture (ConstrainedTextureCacheETC1Test) opens a
# real EDT_OGLES2 surfaceless device + the real qeynos2_obj.eqe1 archive
# (same asset test_eqe1_loader.cpp depends on) to exercise the ETC1-only
# fields (isETC1/alphaTexture), which only the real upload path can set —
# self-skips if either is unavailable.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_constrained_texture_cache
        test_constrained_texture_cache.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_constrained_texture_cache PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_constrained_texture_cache GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_constrained_texture_cache)
ENDIF()

# T123: ObjectNodeTextureGuard regression coverage — a detached object node's
# node-level Materials copy (what CMeshSceneNode::render() actually reads,
# ReadOnlyMaterials=false default) must not go dangling after
# ConstrainedTextureCache::clear() (the real zone-unload path) destroys its
# texture. See refactor/T123_texture_holder_audit.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_object_node_texture_guard
        test_object_node_texture_guard.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_object_node_texture_guard PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_object_node_texture_guard GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_object_node_texture_guard)
ENDIF()

# T124: shared eviction-swap primitive coverage (swapTextureInMaterial/
# swapTextureInMesh/swapTextureInNodeMaterials/swapTextureInMeshSceneNode)
# plus DoorTextureGuard (doors_ sibling of T123's ObjectNodeTextureGuard)
# and the singleton-subsystem holders (SpellVisualFX::swapEvictedTexture,
# BoidsManager::swapEvictedTexture). See
# refactor/T124_remaining_texture_holders.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_t124_texture_holder_guards
        test_t124_texture_holder_guards.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_t124_texture_holder_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_t124_texture_holder_guards GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_t124_texture_holder_guards)
ENDIF()

# T126: the dominant UAF holder named by this unit's write-time
# return-address probe -- EntityVisual::primaryEquipNode/secondaryEquipNode
# (weapon/shield meshes) were never scanned by EntityMeshTextureGuard despite
# T123's holder-inventory table claiming "equipment" was covered, AND
# EquipmentModelLoader's own persistent meshCache_ was never registered as a
# TextureEvictionListener at all. See refactor/T126_uaf_attribution.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_t126_equip_texture_guard
        test_t126_equip_texture_guard.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_t126_equip_texture_guard PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_t126_equip_texture_guard GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_t126_equip_texture_guard)
ENDIF()

# T66: /camera coverage (irrlicht_renderer.cpp processSlashCommand()) --
# genuinely headless end-to-end: exercises the real processSlashCommand() ->
# setCameraMode()/cycleCameraMode()/getCameraModeString() chain via a
# default-constructed, never-Init()'d IrrlichtRenderer (no Irrlicht device
# needed at all -- see the file header comment for why that's safe). Same
# "minimal fake bridge" idiom test_static_ui_panels.cpp (T54) uses to
# capture the command's chat() replies.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_irrlicht_camera_command
        test_irrlicht_camera_command.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_irrlicht_camera_command PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_irrlicht_camera_command GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_irrlicht_camera_command)
ENDIF()

# T310 Leg A: the first-person camera tunables (/fpcam, --fpcam). Same bare,
# never-Init()'d IrrlichtRenderer idiom as test_irrlicht_camera_command above,
# plus the catalog gate and Application::parseArguments -- so ONE binary covers
# both command paths and the startup form.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_tunables
        test_first_person_tunables.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_tunables PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_tunables GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_first_person_tunables)
ENDIF()

# T233: registry-3 (renderer) command replies must echo to the console log
# as well as the chat panel. Same bare-IrrlichtRenderer idiom as
# test_irrlicht_camera_command above, plus the T31 stdout-capture idiom
# from test_logging.cpp to observe the logging drain thread.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_renderer_command_echo
        test_renderer_command_echo.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_renderer_command_echo PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_renderer_command_echo GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_renderer_command_echo)
ENDIF()

# T294: the `I` keybind must reach a working IrrlichtRenderer::toggleInventory()
# (it was an empty stub left by U07c2, so the inventory popup was reachable
# only via /newinv). Drives the real RendererEventReceiver + drainActions() +
# processPlayerInput() on a bare, never-Init()'d IrrlichtRenderer -- same
# no-device idiom as test_spell_gem_key_race below.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_inventory_toggle
        test_inventory_toggle.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_inventory_toggle PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_inventory_toggle GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_inventory_toggle)
ENDIF()

# T235: spell-gem hotkeys reach the game thread -- drives the real
# RendererEventReceiver + IrrlichtRenderer::processChatInput() (bare, no
# device, friend access) and asserts GraphicsInputHandler still sees the
# request. No device needed -- same reasoning as test_renderer_command_echo.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spell_gem_key_race
        test_spell_gem_key_race.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spell_gem_key_race PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spell_gem_key_race GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_spell_gem_key_race)
ENDIF()

# T319: ConsoleRenderer's target-HP readout. IOutputRenderer::
# updateCurrentTargetHP() had no production caller, so the headless status
# line froze the target's HP at acquisition. No graphics, no device.
ADD_EXECUTABLE(test_console_target_hp
    test_console_target_hp.cpp
)
TARGET_LINK_LIBRARIES(test_console_target_hp GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_console_target_hp)

# T319: the R key (ReplyToTell) end to end -- OnEvent decode, the
# mutex-guarded bridgeQueue_, GraphicsInputHandler, the real
# InputActionBridge + ActionDispatcher, the action callback Application
# installs, and IrrlichtRenderer::processChatInput() writing the prefill
# into chatPanelState_. No device needed -- same reasoning as
# test_spell_gem_key_race above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_reply_to_tell_key
        test_reply_to_tell_key.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_reply_to_tell_key PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_reply_to_tell_key GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_reply_to_tell_key)
ENDIF()

# T312: RendererEventReceiver's cross-thread state -- the bridgeQueue_
# heap-corruption race, the key-array races, and the mouse-delta double
# consumer. Doubles as the ThreadSanitizer driver (build a separate
# -fsanitize=thread tree per the P23/T35 precedent). No device needed --
# same reasoning as test_spell_gem_key_race above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bridge_queue_race
        test_bridge_queue_race.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bridge_queue_race PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bridge_queue_race GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_bridge_queue_race)
ENDIF()

# T315: the read-and-reset double-consumer sweep. The click one-shots
# (wasLeftButtonClicked/Released) were the fourth instance of the pattern;
# this target holds the deterministic before/after test and the
# source-scanning guard that fails when any of the swept accessors grows a
# second caller. No device needed -- same reasoning as
# test_bridge_queue_race above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_read_and_reset_ownership
        test_read_and_reset_ownership.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_read_and_reset_ownership PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_read_and_reset_ownership GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_read_and_reset_ownership PRIVATE
        WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_read_and_reset_ownership)
ENDIF()

# T111: coverage for IrrlichtRenderer::isEntityBlockingProgressiveLoad(), the
# pure predicate extracted from checkProgressiveLoadingComplete() to fix the
# deferred-entity-pickup bug T109 shipped inert around. No device needed --
# same reasoning as test_irrlicht_camera_command above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_progressive_load_rearm
        test_progressive_load_rearm.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_progressive_load_rearm PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_progressive_load_rearm GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_progressive_load_rearm)
ENDIF()

# T116: coverage for the SimulationOutput dangling-pointer class made
# impossible by construction -- applySimulationOutput()/
# bindFrameSimulationBuffers() split by lifetime contract. FRIEND_TEST-based
# access to private members, same "default-constructed, never-Init()'d
# IrrlichtRenderer, no device needed" idiom as test_irrlicht_camera_command
# above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_simulation_output_lifetime
        test_simulation_output_lifetime.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_simulation_output_lifetime PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_simulation_output_lifetime GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_simulation_output_lifetime)
ENDIF()

# Integration test: Beneficial Spell Casting (connects to real EQEmu server with graphics enabled)
# This test verifies beneficial spell casting works correctly - casting buffs on self and verifying
# they land properly. Tests casting multiple spells in series and buff refresh behavior.
# Run manually with: DISPLAY=:99 ./bin/test_beneficial_spell_casting
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_beneficial_spell_casting
        test_beneficial_spell_casting.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_beneficial_spell_casting PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_beneficial_spell_casting GTest::gtest_main willeq_core)
    # T164 (2026-08-21): registered with ctest ONLY under -DEQT_LIVE_TESTS=ON, and
    # labelled "live" so it can never join the default suite. Discovery is safe --
    # each main() only parses args and prints before RUN_ALL_TESTS(), so
    # --gtest_list_tests returns without touching a server (verified: 4 / 7 / 8
    # tests listed in <1s with no eqemu running). These binaries are still BUILT
    # unconditionally; before T164 they were built on every unit and run by
    # nothing.
    #   cmake -S . -B build -DEQT_LIVE_TESTS=ON
    #   ctest --test-dir build -L live
    IF(EQT_LIVE_TESTS)
        GTEST_DISCOVER_TESTS(test_beneficial_spell_casting PROPERTIES LABELS "live")
    ENDIF()
ENDIF()
