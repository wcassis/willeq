# tests/cmake/ui_rendering.cmake -- included from tests/CMakeLists.txt.
# Graphics-side integration: zoning graphics, inventory/model view, first-person body, UI panels and atlases.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Integration test: Zoning with Graphics (connects to real EQEmu server with graphics enabled)
# This test is NOT auto-discovered because it requires a running server and X display.
# Run manually with: DISPLAY=:99 ./bin/test_zoning_graphics_integration
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zoning_graphics_integration
        test_zoning_graphics_integration.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zoning_graphics_integration PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zoning_graphics_integration GTest::gtest_main willeq_core)
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
        GTEST_DISCOVER_TESTS(test_zoning_graphics_integration PROPERTIES LABELS "live" DISCOVERY_TIMEOUT 120)
    ENDIF()
ENDIF()

# Headless character-model loading/animation suite (RaceModelLoader +
# EQAnimatedMeshSceneNode). Originally an EQEmu-server + DISPLAY + paperdoll-
# UI integration test (InventoryModelViewTest); T54 (2026-08-10) deleted that
# fixture — its six TEST_F cases were unconditionally GTEST_SKIP()'d
# ("WindowManager has been deleted"), not environment-gated, because the
# paperdoll UI they exercised (WindowManager/InventoryWindow/
# CharacterModelView) was itself deleted in 0f25cb57. The salvageable
# assertions (textures applied, loads for race/gender, animation coherence)
# now live in RaceModelAnimationHeadlessTest (added P12B, extended T54),
# which needs neither a server nor a display — EDT_NULL device only. Safe
# to auto-discover.
#
# Filename kept as-is for this unit (renaming would churn CMake/git history
# for no test value) — T54's Review recommends test_race_model_loading.cpp
# as a truer name for a future rename.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_inventory_model_view
        test_inventory_model_view.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_inventory_model_view PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_inventory_model_view GTest::gtest_main willeq_core)
    # T270: the fixture opens the shipped db as the race source (race codes
    # are db-only now).
    TARGET_COMPILE_DEFINITIONS(test_inventory_model_view PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_inventory_model_view DISCOVERY_TIMEOUT 120)
ENDIF()

# B1084 (#404): the ONE robe predicate, table-tested over the measured range
# 10..16 (official-client oracle + the shipped S3D archives; evidence in
# refactor/B1084/README.md). Pure header test -- no device, no db, no client
# tree -- so it gates on graphics only.
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_robe_texture_range
        test_robe_texture_range.cpp
    )
    TARGET_LINK_LIBRARIES(test_robe_texture_range GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_robe_texture_range DISCOVERY_TIMEOUT 60)
ENDIF()

# B1084 (#404) entity-prep leg: chest material 10 (the naked-body bug) must
# select the robe body variant (bodyVariant=1, the {RACE}01 mesh) through the
# production per-entity entry createAnimatedNodeWithEquipment(). Same
# EDT_NULL + willeq_core gate as test_inventory_model_view; opens the shipped
# db and the real client tree and skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_robe_entity_prep
        test_robe_entity_prep.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_robe_entity_prep PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_robe_entity_prep GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_robe_entity_prep PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_robe_entity_prep DISCOVERY_TIMEOUT 120)
ENDIF()

# T309: the measured first-person draw rule (predicate + the withhold seam).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_body_draw_rule
        test_first_person_body_draw_rule.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_body_draw_rule PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_body_draw_rule GTest::gtest willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_first_person_body_draw_rule PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_first_person_body_draw_rule DISCOVERY_TIMEOUT 120)
ENDIF()

# T301 Leg C: first-person weapon attachment geometry (probe).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_weapon_attachment
        test_first_person_weapon_attachment.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_weapon_attachment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_weapon_attachment GTest::gtest willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_first_person_weapon_attachment PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_first_person_weapon_attachment DISCOVERY_TIMEOUT 120)
ENDIF()

# T472: held items (the lantern's index, the one attach transform) and the
# stationary player's facing -- geometric pins against the real S3D data.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_held_item_attachment test_held_item_attachment.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_held_item_attachment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_held_item_attachment GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_held_item_attachment PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")"
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_held_item_attachment DISCOVERY_TIMEOUT 120)

    # B824 (issue #86): the item -> IT-model lookup is willeq.db-only.
    # The in-memory JSON map (itemToModelMap_, loadItemModelMapping()) is
    # deleted; this suite pins that getModelIdForItem() answers from the
    # database with no JSON map loaded. The mechanical half is the
    # ItemModelMapDeletion hygiene test (scripts/refactor/check_item_model_map.py).
    ADD_EXECUTABLE(test_item_model_mapping test_item_model_mapping.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_item_model_mapping PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_item_model_mapping GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_item_model_mapping DISCOVERY_TIMEOUT 120)

    # T476: census of every IT actor in the seventeen equipment archives --
    # which route resolves it, rest transforms, and its principal axis.
    ADD_EXECUTABLE(test_equipment_item_census test_equipment_item_census.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_equipment_item_census PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_equipment_item_census GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_equipment_item_census DISCOVERY_TIMEOUT 120)
ENDIF()

# T301 Leg D: the UI-hide toggle, from the paths a user or a capture script
# reaches (/hideui and the F10 hotkey -- F11 at T301's time, moved to the
# official F10 by B582), plus source pins on the two single-site
# steps that need a GL device to execute.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_hide_command
        test_ui_hide_command.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_hide_command PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_hide_command GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_ui_hide_command PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_ui_hide_command DISCOVERY_TIMEOUT 120)
ENDIF()

# T330 (B043): the four debug-hotkey switch arms in processCommonInput that had
# never existed (wireframe / HUD / name tags / old models). Behavioural on a
# device-less renderer for three of them; the HUD arm has no public reader and
# is source-pinned, as are the receiver's four queue sites.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_debug_hotkey_arms
        test_debug_hotkey_arms.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_debug_hotkey_arms PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_debug_hotkey_arms GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_debug_hotkey_arms PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_debug_hotkey_arms DISCOVERY_TIMEOUT 120)
ENDIF()

# B1063 (GitLab #383): F12 and /screenshot share the ONE body
# (SlashCommandsComponent::runScreenshot). The default-name sanitizer is
# tested behaviourally; the three route sites (the Receiver's RA::Screenshot
# queue, FrameComponent's deferred arm, the /screenshot chain arm) are
# source-pinned, as this suite does for other single-site invariants.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_screenshot_one_body
        test_screenshot_one_body.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_screenshot_one_body PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_screenshot_one_body GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_screenshot_one_body PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_screenshot_one_body DISCOVERY_TIMEOUT 120)
ENDIF()

# B905 (#225): every default and shipped key binding is unshadowed and reaches
# a dispatch consumer on a real RendererEventReceiver.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_hotkey_dispatch_census
        test_hotkey_dispatch_census.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotkey_dispatch_census PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotkey_dispatch_census GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_hotkey_dispatch_census PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_hotkey_dispatch_census DISCOVERY_TIMEOUT 120)
ENDIF()

# B1025-H4 (#345): the hotbar window's page and toggle keys -- the shipped
# table's Shift+1..0 / Alt+H resolve to the right receiver outputs, and
# the production consumer pushes the page intent / flips the window flag.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_hotbar_key_routing
        test_hotbar_key_routing.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotbar_key_routing PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotbar_key_routing GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_hotbar_key_routing PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_hotbar_key_routing DISCOVERY_TIMEOUT 120)
ENDIF()

# T343 (B002): the autorun arm in processCommonInput. Behavioural on a
# device-less renderer (the flag lives on PlayerCameraComponent), plus the two
# source-pinned ends the behavioural test cannot reach -- the receiver's queue
# site and phase 1's read/cancel pair.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_autorun_arm
        test_autorun_arm.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_autorun_arm PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_autorun_arm GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_autorun_arm PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_autorun_arm DISCOVERY_TIMEOUT 120)
ENDIF()

# T267: race 330 (playable Froglok) resolves to and loads from the row's
# globalpcfroglok_chr.s3d. Same EDT_NULL + willeq_core gate as
# test_inventory_model_view; opens the shipped data/willeq.db as the race
# source and skips without it or the client tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_race_330_loader
        test_race_330_loader.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_race_330_loader PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_race_330_loader GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_race_330_loader PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_race_330_loader DISCOVERY_TIMEOUT 120)
ENDIF()

# B733 (issue #70): the race model builder selects parts by NAME, so a
# character whose skeleton's 0x200 mesh list references a part under another
# name loses it. Race 146 (Spectral Sarnak, code SSN, cabeast_chr.s3d): the
# skeleton SSN_HS_DEF's 0x200 list names SNNHE00_DMSPRITEDEF (the head, "SNN"
# spelling) + SSN_DMSPRITEDEF (the body). The loader's flattenSkeleton walks
# the BONES, so the head is never stored, and the name-based selection
# (SSN / SSNHE00) cannot reach it. This test DERIVES the expected mesh set from
# the archive's own WLD data (the 0x200 meshRefs + bone refs of the race's
# skeleton) -- never hand-typed -- and asserts the built model holds all of
# it. Same EDT_NULL + willeq_core gate as test_race_330_loader; opens the
# shipped data/willeq.db and the real client tree and skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b733_skeleton_mesh_refs
        test_b733_skeleton_mesh_refs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b733_skeleton_mesh_refs PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b733_skeleton_mesh_refs GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_b733_skeleton_mesh_refs PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_b733_skeleton_mesh_refs DISCOVERY_TIMEOUT 120)
ENDIF()

# T364 Leg B (B065): the _chr.s3d cache's LRU accounting -- every cached entry
# must be registered in chrCacheLruOrder_ and the maxChrCacheEntries_ cap must
# actually evict. Same EDT_NULL + willeq_core gate as test_race_330_loader;
# opens the shipped data/willeq.db and the real client tree and skips without
# either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_chr_cache_lru
        test_chr_cache_lru.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_chr_cache_lru PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_chr_cache_lru GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_chr_cache_lru PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_chr_cache_lru DISCOVERY_TIMEOUT 120)
ENDIF()

# T369 (B105): the character archive is derived from the FILENAME, never from
# the whole path -- a directory component containing `_chr` used to truncate
# the path and silently zero every character load. Same EDT_NULL +
# willeq_core gate as test_chr_cache_lru; uses the real client tree, and the
# shipped data/willeq.db for the WARN case. Skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_chr_path_derivation
        test_chr_path_derivation.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_chr_path_derivation PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_chr_path_derivation GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_chr_path_derivation PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_chr_path_derivation DISCOVERY_TIMEOUT 120)
ENDIF()

# T340 Leg A (B052): drives RaceModelLoader's variant/animation loader through
# every exit G07 enumerated -- primary hit, primary-miss/fallback-hit, an
# unusable matched character, all candidates exhausted, and an empty candidate
# list. Same EDT_NULL + willeq_core gate as test_race_330_loader; opens the
# shipped data/willeq.db and the real client tree and skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    # T504: the original client's character-model sources (GlobalLoad.txt + <zone>_chr.txt), era-pruned
    ADD_EXECUTABLE(test_era_model_sources test_era_model_sources.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_era_model_sources PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_era_model_sources GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_era_model_sources DISCOVERY_TIMEOUT 120)

    # T504b Leg 2b: unload of despawned races (hysteresis + budget pressure)
    ADD_EXECUTABLE(test_entity_source_unload test_entity_source_unload.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_entity_source_unload PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_source_unload GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_source_unload PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_source_unload DISCOVERY_TIMEOUT 120)

    ADD_EXECUTABLE(test_variant_loader_candidates
        test_variant_loader_candidates.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_variant_loader_candidates PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_variant_loader_candidates GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_variant_loader_candidates PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_variant_loader_candidates DISCOVERY_TIMEOUT 120)
ENDIF()

# T452 (board B468): getArmorTexture() resolves the 17-23 armor tier from
# global17-23_amr.s3d. Until this unit it queried an `s3d_assets` table the
# shipped database has never contained, so it returned nullptr for everything
# and the whole high-tier armor set rendered as the base skin. Same EDT_NULL +
# willeq_core gate as test_race_330_loader; uses the real client tree only (no
# database, no server, no DISPLAY) and skips without it.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_armor_texture_archive
        test_armor_texture_archive.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_armor_texture_archive PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_armor_texture_archive GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_armor_texture_archive DISCOVERY_TIMEOUT 120)
ENDIF()

# T285: pins the two applyPaletteMask plumbing sites (P46d Part B / P50) --
# mesh_building.cpp's on-demand texture fallback and animated_mesh_creation.cpp's
# equipment texture-variant swap. Same EDT_NULL + willeq_core gate as
# test_race_330_loader; opens the shipped data/willeq.db and the real client
# tree and skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_palette_mask_plumbing
        test_palette_mask_plumbing.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_palette_mask_plumbing PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_palette_mask_plumbing GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_palette_mask_plumbing PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_palette_mask_plumbing DISCOVERY_TIMEOUT 120)
ENDIF()

# T271: race DISPLAY NAMES come from the db (EverQuest::GetRaceName was
# shifted -- 14 "Iksar", 128 "Vah Shir", 130 "Froglok", 330 "Drakkin" -- and
# it is printed to the player). Opens the shipped data/willeq.db and skips
# without it; no device, no server. Gated on graphics only because the
# T274 deleted the caller-less graphics-side twin EQT::Graphics::getRaceName;
# the target stays graphics-gated because EQT::Graphics::getRaceScale (which
# does have callers) is still held to the same rows here.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_race_names
        test_race_names.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_race_names PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_race_names GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_race_names PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_race_names DISCOVERY_TIMEOUT 120)
ENDIF()

# T54: first tests for the static-panel UI (StaticUIInput hit-testing,
# processKey popup dismissal, and an EDT_NULL renderInventoryPopup smoke
# test). Pure hit-testing needs no device at all; the optional rendering
# tail uses EDT_NULL like test_door_loading's DoorManagerGraphicsTest.
# Same gate as test_inventory_model_view (needs Irrlicht + the graphics UI
# sources in willeq_core) — safe to auto-discover, nothing here needs a
# server or DISPLAY.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_static_ui_panels
        test_static_ui_panels.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_static_ui_panels PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_static_ui_panels GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_static_ui_panels DISCOVERY_TIMEOUT 120)
ENDIF()

# T415 (board B321 + B320): the tradeskill container window and the combat
# abilities window. Pure state + geometry, no device -- same gate as
# test_static_ui_panels above only because both panels live in the graphics
# UI sources inside willeq_core.
# T432 (board B275): per-character UI persistence. Pure state + a disk round
# trip under a private WILLEQ_CACHE_DIR -- no device, no server. Same gate as
# the panel tests above only because the persisted panel structs live in the
# graphics UI sources inside willeq_core.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_preferences
        test_ui_preferences.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_preferences PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_preferences GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_ui_preferences DISCOVERY_TIMEOUT 120)
ENDIF()

# T467 (board B560): an identity change resets the persisted UI state to its
# defaults before the new character's file is read. Bare renderer + a private
# WILLEQ_CACHE_DIR, as test_camera_zone_in_preference.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_preferences_identity
        test_ui_preferences_identity.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_preferences_identity PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_preferences_identity GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_ui_preferences_identity DISCOVERY_TIMEOUT 120)
ENDIF()

# B897 (#213): the Friends window -- geometry against EQUI_FriendsWnd.xml and
# defaults.ini, the friends/ignore model and its oracle strings, the commands,
# the <Char>_<server>.ini shape and persistence tick, both tabs' hit tests,
# the ignore filter and the window intent through a real EverQuest, Alt+F.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_friends_window
        test_friends_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_friends_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_friends_window GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_friends_window DISCOVERY_TIMEOUT 120)
ENDIF()

# B888 (#204): the two-page Character Creation screen -- geometry against
# EQUI_CharacterCreate.xml, the hidden-entry layout, the page flow and its
# refusals (eqstr 1576/1577/1580), click resolution. Device-free.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_char_create_screen
        test_char_create_screen.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_char_create_screen PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_char_create_screen GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_char_create_screen PRIVATE "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_char_create_screen DISCOVERY_TIMEOUT 120)
ENDIF()

# B1025-H2 (#345): stock Titanium hotbar window, controls and three holders
# for every slot, checked against EQUI_HotButtonWnd.xml when present.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_hotbar_layout test_hotbar_layout.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_hotbar_layout PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotbar_layout GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_hotbar_layout DISCOVERY_TIMEOUT 120)
ENDIF()

# B1075 (#395): the layout table's required co-open sets -- every set, at
# every supported resolution, no two members overlap and all are on screen,
# plus the negative (a colliding table fails) and the 1x10 / 2x5 hotbar
# orientation from the same table. Headless, header-only layout code.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_layout_coopen test_ui_layout_coopen.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_ui_layout_coopen PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_layout_coopen GTest::gtest_main)
    TARGET_COMPILE_DEFINITIONS(test_ui_layout_coopen PRIVATE "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_ui_layout_coopen DISCOVERY_TIMEOUT 120)
ENDIF()

# B999 (#319): no two co-open HUD panels overlap at any supported
# resolution, the group x pet pair named in the table's co-open sets (the
# B992 hud_all capture's finding), and a rect dump per window per resolution
# (refactor/B999/README.md copies its stdout). Same shape as
# test_ui_layout_coopen: header-only layout code behind the irrlicht rect
# type, so it is gated at registration (B1038).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_layout_rects test_ui_layout_rects.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_ui_layout_rects PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_layout_rects GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_ui_layout_rects DISCOVERY_TIMEOUT 120)
ENDIF()

# B1078 (#398): every item-icon decal rect is SQUARE at every supported
# resolution -- each drawItemIcon() call site, through the same layout
# functions the render bodies call. The other half of B1078's net:
# test_item_icon_atlas pins squareIn() itself, this pins that every caller
# hands it a square cell. Same shape as test_ui_layout_coopen: header-only
# layout code behind the irrlicht rect type, so it is gated at registration
# (B1038).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_item_icon_rects test_item_icon_rects.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_item_icon_rects PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_item_icon_rects GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_item_icon_rects DISCOVERY_TIMEOUT 120)
ENDIF()

# B998 (#318): the character-select screen -- layout vs EQUI_CharacterSelect.xml,
# the list from a recorded OP_SendCharInfo, buttons -> requests -> packets
# through the real service and the world replay seam, the configured-character
# auto-select pinned unchanged, Create opening B888's screen. Device-free.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_character_select
        test_character_select.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_character_select PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_character_select GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_character_select PRIVATE "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_character_select DISCOVERY_TIMEOUT 120)
ENDIF()

# B888-CC2 (#204): the pre-zone 3D preview -- the device-free appearance
# selection per state (select row's T14 body; creation race+gender+FacePick)
# and the headless model leg (CharacterModelView on the production
# RaceModelLoader, EDT_NULL, no zone): the right race model stands up,
# distinct races resolve to distinct models, the rotation round-trips.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_char_preview
        test_char_preview.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_char_preview PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_char_preview GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_char_preview PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")"
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_char_preview DISCOVERY_TIMEOUT 120)
ENDIF()

# B888-CC3 (#204): the Explore mode -- the device-free state machine
# (open/esc, click-selection, Enter enters with the list cursor, the free-
# camera verbs) and the EXW_ExploreModeWindow overlay layout against the stock
# EQUI_CharacterSelect.xml. Device-free.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_explore_screen
        test_explore_screen.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_explore_screen PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_explore_screen GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_explore_screen PRIVATE "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_explore_screen DISCOVERY_TIMEOUT 120)
ENDIF()

# B888-CC3 (#204): the 3D clz backdrop scene (the "3D slice") -- headless on an
# EDT_NULL device (the CC-2 headless idiom, no zone): the clz showroom stands
# up from clz.eqg's members (terrain + 29 .mod, one mesh per distinct model,
# the free camera), the held `d` camera verb moves the camera (the measured
# forward), the click projects to the account's row (miss -> -1), the
# per-row stands round-trip, and the pre-zone memory delta is measured.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_explore_backdrop
        test_explore_backdrop.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_explore_backdrop PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_explore_backdrop GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_explore_backdrop PRIVATE "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_explore_backdrop DISCOVERY_TIMEOUT 120)
ENDIF()

# T434: the Options window -- geometry against EQUI_OptionsWindow.xml, the
# T363 disabled-control rule, the three wired sliders, the Alt+O binding and
# the persistence round trip. Device-free like its siblings.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_options_window
        test_options_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_options_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_options_window GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_options_window DISCOVERY_TIMEOUT 120)
ENDIF()

# B430/B431/B433: the Options window's controls wired -- the classification
# census, the settings table, and per page click -> setting -> persisted ->
# reloaded plus the /option and /filter grammar. Device-free.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_options_wiring
        test_options_wiring.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_options_wiring PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_options_wiring GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_options_wiring DISCOVERY_TIMEOUT 120)
ENDIF()

# B993 (#313): the Colors page -- the [TextColors] table, the colour picker,
# persistence, /option textcolor, and a chat line's pixels in the chosen
# colour (headless GLES2 + willeq.eqe1; that one self-skips without them).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_options_colors
        test_options_colors.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_options_colors PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_options_colors GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_options_colors DISCOVERY_TIMEOUT 120)
ENDIF()

# B994 (#314): the 3D target ring -- TargetIndicator.ini's numbers, the
# geometry following the target, the draw decision, the Options row, and the
# ring's pixels through the client's own draw (headless GLES2; self-skips).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_target_ring
        test_target_ring.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_target_ring PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_target_ring GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_target_ring DISCOVERY_TIMEOUT 120)
ENDIF()

# B1088 (#408): the loading screen's zone-name lookup -- unknown id -> empty,
# verbatim name (no "The" stripping), OP_NewZone override, the stored
# "Entering <name>..." line read at draw time, and the line's pixels through
# the shared layout (headless GLES2; self-skips without a device).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_display_name
        test_zone_display_name.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_display_name PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_display_name GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_display_name DISCOVERY_TIMEOUT 120)
ENDIF()

# B995 (#315): the Display > Particles sub-pages -- sub-tab switching, the
# Spell/Env knobs persisted as eqclient.ini floats, and each knob reaching the
# particle systems (the worker's spawn density, the shared draw rule).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_options_particles
        test_options_particles.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_options_particles PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_options_particles GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_options_particles DISCOVERY_TIMEOUT 120)
ENDIF()

IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_tradeskill_combat_panels
        test_tradeskill_combat_panels.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_tradeskill_combat_panels PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_tradeskill_combat_panels GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_tradeskill_combat_panels DISCOVERY_TIMEOUT 120)
ENDIF()

# T419 Leg A (board B335): the tradeskill recipe list -- the packet decode,
# the bridge appliers and COMBW_LeftSideArea's rects. Pure state + geometry,
# no device -- same gate as the T415 pair above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_recipe_list
        test_recipe_list.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_recipe_list PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_recipe_list GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_recipe_list DISCOVERY_TIMEOUT 120)
ENDIF()

# T421 Leg A (board B298, item half): the item display window -- the text
# composer over a real ItemInstance, the rects against EQUI_ItemDisplay.xml,
# and the real click that asks for it. No device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_item_display
        test_item_display.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_item_display PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_item_display GTest::gtest_main willeq_core)
    # T429 (board B390): the composer reads its formats out of the shipped db.
    TARGET_COMPILE_DEFINITIONS(test_item_display PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_item_display DISCOVERY_TIMEOUT 120)
ENDIF()

# T425 Leg A (board B298, spell half): the spell display window -- the text
# composer over a real SpellData, the oracle's omissions, and the real
# shift+click that asks for it. No device, no database, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spell_display
        test_spell_display.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spell_display PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spell_display GTest::gtest_main willeq_core)
    # T429 (board B390): the composer reads its formats out of the shipped db.
    TARGET_COMPILE_DEFINITIONS(test_spell_display PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_spell_display DISCOVERY_TIMEOUT 120)
ENDIF()

# T429 Leg A (board B390): the two display composers take their format strings
# from the shipped eqstr table rather than from their own source. Needs no
# device -- it is registered here beside the two windows it covers.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_display_text_from_db
        test_display_text_from_db.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_display_text_from_db PRIVATE ${IRRLICHT_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/third_party)
    TARGET_LINK_LIBRARIES(test_display_text_from_db GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_display_text_from_db PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_display_text_from_db DISCOVERY_TIMEOUT 120)
ENDIF()

# T436 (board B429): the merchant window, the bank window and the cursor
# attachment -- the source census of the defect, the geometry against the
# three oracle XMLs, the appliers driven with the real publishers' data
# structs, and the clicks against a real GameStateBridge. No device, no
# database, no server. WILLEQ_REPO_ROOT is set globally in
# tests/CMakeLists.txt, which is what the census cases read the tree with.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_merchant_bank_cursor
        test_merchant_bank_cursor.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_merchant_bank_cursor PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_merchant_bank_cursor GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_merchant_bank_cursor DISCOVERY_TIMEOUT 120)
ENDIF()

# B898 (#214): the skill trainer window -- layout vs EQUI_TrainWindow.xml,
# the OP_GMTraining reply through the real EverQuest into the real applier,
# the Train/Done clicks against the /train intents and their exact bytes on a
# fake Daybreak connection. No device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_trainer_panel
        test_trainer_panel.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_trainer_panel PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_trainer_panel GTest::gtest_main willeq_core)
    # UIGAP-residue: the measured trainer lines are eqstr 12056 / 12930.
    TARGET_COMPILE_DEFINITIONS(test_trainer_panel PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_trainer_panel DISCOVERY_TIMEOUT 120)
ENDIF()

# B899 (#215 / #151 Leg B): the era container window -- the geometry parsed
# out of EQUI_Container.xml, the appliers, the Combine / Done / ESC clicks on
# a real GameStateBridge, /bag through the real command chain on a real
# EverQuest, and the world container no longer opening the recipe window.
# No device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_container_window
        test_container_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_container_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_container_window GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_container_window DISCOVERY_TIMEOUT 120)
ENDIF()

# B896 (GitLab #212): the player trade window and the NPC give window --
# geometry against EQUI_TradeWnd.xml / EQUI_GiveWnd.xml, the appliers, the
# clicks on a real bridge, the game side through a real EverQuest (auto-ack,
# auto-place, withdrawal refusal, coin move, eqstr 1450, no completion line)
# and the green-name state machine from review/UIGAP-ORACLE/212/trade.tsv.
# No device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_trade_window
        test_trade_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_trade_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_trade_window GTest::gtest_main willeq_core)
    # UIGAP-residue: the no-drop / banker refusals print eqstr 12921 / 8505.
    TARGET_COMPILE_DEFINITIONS(test_trade_window PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_trade_window DISCOVERY_TIMEOUT 120)
ENDIF()

# T439 (boards B432/B444/B434): the T434/T436 residue -- the absolute volume
# round trip, the zone-in snapshot's cursor and bank slots, and the deleted
# PopupType::Options enumerator. Half the cases are source censuses that read
# the tree through WILLEQ_REPO_ROOT (set globally in tests/CMakeLists.txt), so
# the same binary measures the pre-fix and post-fix sources; the rest are
# ordinary behaviour over the real appliers. No device, no database, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_options_merchant_residue
        test_options_merchant_residue.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_options_merchant_residue PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_options_merchant_residue GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_options_merchant_residue DISCOVERY_TIMEOUT 120)
ENDIF()

# T442 (boards B450/B451/B452): three dead renderer/UI members, deleted. Every
# case is a source census read through WILLEQ_REPO_ROOT, so the same binary
# measures the pre-fix and post-fix sources -- which is the pre-fix red for a
# unit whose whole fix is a deletion. No device, no database, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_dead_renderer_members
        test_dead_renderer_members.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_dead_renderer_members PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_dead_renderer_members GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_dead_renderer_members DISCOVERY_TIMEOUT 120)
ENDIF()

# T443 (board B454): ESC on a server-owned window sends its close intent
# instead of hiding the panel (the tradeskill container joins the loot, bank
# and merchant windows). Census cases run against the pre-fix tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_esc_server_owned_windows
        test_esc_server_owned_windows.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_esc_server_owned_windows PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_esc_server_owned_windows GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_esc_server_owned_windows DISCOVERY_TIMEOUT 120)
ENDIF()

# B441 (#26): the bank's Change button -- the OP_MoveCoin steps, replayed
# through eqemu's own conversion arithmetic, and the button's intent.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bank_change
        test_bank_change.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bank_change PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bank_change GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_bank_change PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_bank_change DISCOVERY_TIMEOUT 120)
ENDIF()

# B834 (#202): the tip windows -- parse, catalog, rules, [Tips] persistence,
# and the frame's rising-edge triggers and ESC order on a device-less renderer.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_tip_window
        test_tip_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_tip_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_tip_window GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_tip_window DISCOVERY_TIMEOUT 120)
ENDIF()

# B460 (#156): ESC closes the most recently opened window, one per press --
# the open-order stack and FrameComponent::handleEscapeKey() on a device-less
# renderer, driven through opened-order sequences.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_esc_window_stack
        test_esc_window_stack.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_esc_window_stack PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_esc_window_stack GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_esc_window_stack DISCOVERY_TIMEOUT 120)
ENDIF()

# T443 (board B453): /dumpscene's unaccounted count, against a hand-built
# scene graph on an EDT_NULL device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_scene_node_census
        test_scene_node_census.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_scene_node_census PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_scene_node_census GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_scene_node_census DISCOVERY_TIMEOUT 120)
ENDIF()

# T421 Leg B (board B365): the ONE text-entry widget, and the tradeskill
# search box using it.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_search_entry
        test_search_entry.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_search_entry PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_search_entry GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_search_entry DISCOVERY_TIMEOUT 120)
ENDIF()

# T419 Leg B (board B352): the pet command grid. Header-only geometry, so it
# needs no willeq_core body at all -- but it links it for UILayout's TU-free
# inline definitions, the same way the panel tests above do.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_pet_buttons
        test_pet_buttons.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_pet_buttons PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_pet_buttons GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_pet_buttons DISCOVERY_TIMEOUT 120)
ENDIF()

# T413 (board B285): the software cursor's cache name vs the pack's entry name.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_software_cursor_pack_name
        test_software_cursor_pack_name.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_software_cursor_pack_name PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_software_cursor_pack_name GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_software_cursor_pack_name PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_software_cursor_pack_name DISCOVERY_TIMEOUT 120)
ENDIF()

# T413 (board B296): the bridge payload fields nothing drew. Pure state +
# geometry over static_panels/UILayout — no device, no server, no DISPLAY.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bridge_payload_fields
        test_bridge_payload_fields.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bridge_payload_fields PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bridge_payload_fields GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_bridge_payload_fields DISCOVERY_TIMEOUT 120)
ENDIF()

# T275: pixel-level assertions on the ATLAS-backed UIRenderer path (a real
# headless GLES2 device + the real ui_atlas out of willeq.eqe1). Every UI
# capture harness before this ran with atlas=nullptr, so the atlas path had
# no pixel coverage at all -- see refactor/T275_uirenderer_drawrect_invisible.md.
# Self-skips without the EQE1 asset or a usable GLES2 device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_atlas_pixels
        test_ui_atlas_pixels.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_atlas_pixels PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_atlas_pixels GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_ui_atlas_pixels DISCOVERY_TIMEOUT 120)
ENDIF()

# B972 (GitLab #290): the self-contained stream-buffer cuts -- the particle
# point draw and the impostor page draws move from client arrays to bounded
# reused VBOs. This test pins their PIXEL IDENTITY (old path vs new path on
# the same headless GLES2 device) plus the logical-tally accounting. Needs
# texture_generators.cpp compiled in directly (willeq_core does not contain
# it) and EQT_HAS_GLES2 so the renderer class and the driver's accounting
# seams are visible. Self-skips without a GLES2 device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b972_stream_buffers
        test_b972_stream_buffers.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_generators.cpp
    )
    # B1019: the PRIVATE EQT_HAS_GLES2 copy here was deleted -- the define
    # flows in globally (ADD_DEFINITIONS at CMakeLists.txt:1027, every
    # EQT_GLES2 configuration), exactly as the other graphics test targets
    # already rely on.
    TARGET_INCLUDE_DIRECTORIES(test_b972_stream_buffers PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b972_stream_buffers GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b972_stream_buffers DISCOVERY_TIMEOUT 120)
ENDIF()

# T462 (boards B505/B506): the static UI's per-frame cost -- draw-count pins
# for the batched UIRenderer flush and the chat wrap cache, plus a timing
# account that runs only with WILLEQ_T462_ACCOUNT=1. Same headless GLES2 +
# willeq.eqe1 harness as test_ui_atlas_pixels; self-skips without them.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_static_ui_frame_cost
        test_static_ui_frame_cost.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_static_ui_frame_cost PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_static_ui_frame_cost GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_static_ui_frame_cost DISCOVERY_TIMEOUT 120)
ENDIF()

# B992 (GitLab #312): static-UI text respects panel z-order -- a pixel
# assertion over overlapping windows and the layered flush's draw-call pins.
# Same headless GLES2 + willeq.eqe1 harness; self-skips without them.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_panel_z_order
        test_ui_panel_z_order.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_panel_z_order PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_panel_z_order GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_ui_panel_z_order DISCOVERY_TIMEOUT 120)
ENDIF()

# B1004 (GitLab #323): item-name tooltips -- the Alt-held labels and the hover
# tooltip, against review/UIGAP-ORACLE/323/tooltips.tsv. The logic cases need
# no device; the pixel cases use the headless GLES2 + willeq.eqe1 harness and
# self-skip without it.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_item_tooltip
        test_item_tooltip.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_item_tooltip PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_item_tooltip GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_item_tooltip DISCOVERY_TIMEOUT 120)
ENDIF()

# B975 (GitLab #293): the entity name-tag batch -- pixel-identical to the
# per-node CTextSceneNodes, one draw per bounded run (fallbacks split it). Needs a
# headless GLES2 device; self-skips without one.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_name_tag_batch
        test_name_tag_batch.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_name_tag_batch PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_name_tag_batch GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_name_tag_batch DISCOVERY_TIMEOUT 120)
ENDIF()

# T313: the static UI's missing scroll INPUT -- chat scrollback, the skills
# list and the loot list all honoured a scrollOffset nothing ever wrote.
# Mostly device-free (it drives StaticUIInput and IrrlichtRenderer's slash
# command directly, test_ui_hide_command.cpp's idiom); the chat leg alone
# needs a headless GLES2 device + willeq.eqe1 and self-skips without them,
# because renderChatPanel() is the only place the wrapped-line window exists.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_static_ui_scroll
        test_static_ui_scroll.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_static_ui_scroll PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_static_ui_scroll GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_static_ui_scroll PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_static_ui_scroll DISCOVERY_TIMEOUT 120)
ENDIF()

# T279: pixel proof for the EQE1 ALPHA COMPANION mip filter. Builds synthetic
# complete/truncated ETC1 packs and draws the companion minified through a real
# headless GLES2 device -- see refactor/T279_complete_mip_chains.md.
# Self-skips without a usable GLES2 device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_eqe1_alpha_companion_mip
        test_eqe1_alpha_companion_mip.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_eqe1_alpha_companion_mip PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_eqe1_alpha_companion_mip GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_eqe1_alpha_companion_mip DISCOVERY_TIMEOUT 120)
ENDIF()

# B1017 (#338), criteria (d)+(e): the test that REACHES THE STALE-PACK FATAL
# through the real ConstrainedTextureCache::getOrLoad() miss path (a fork
# child runs the LOG_FATAL + std::exit(1) site; the parent greps its
# stdout) and the mixed-case .BMP request that still gets the diagnosis.
# The pre-B1017 B930 leg (test_eqe1_stale_pack) drives
# diagnoseStaleEQE1Pack() and _exit(1)s unconditionally -- deleting the
# diagnosis CALL at the FATAL site left it green (audit finding 7).
# Links willeq_core (which carries constrained_texture_cache*.cpp +
# eqe1_loader.cpp, the client's own TU set); no GL device is needed -- the
# miss path never touches a driver.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_eqe1_real_miss
        test_eqe1_real_miss.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_eqe1_real_miss PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_eqe1_real_miss GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_eqe1_real_miss DISCOVERY_TIMEOUT 120)
ENDIF()

# B948 (GitLab #266): getGpuTextureMemoryUsage() counts each ETC1 alpha chain
# exactly once -- upload a synthetic ETC1+alpha pair and assert the driver's
# figure is main + alpha once each, through the B944 eviction/restore round
# trip too. Same headless-GLES2 idiom (and self-skip) as
# test_eqe1_alpha_companion_mip above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_texture_etc1_alpha_count
        test_texture_etc1_alpha_count.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_texture_etc1_alpha_count PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_texture_etc1_alpha_count GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_texture_etc1_alpha_count DISCOVERY_TIMEOUT 120)
ENDIF()

# T411 (board B261/B287): pixel proof that the ETC1 `_alpha` companion reaches
# the three alpha-BLEND draw paths -- draw2DImage's UI2D_FS, an
# EMT_TRANSPARENT_ALPHA_CHANNEL mesh through SOLID3D_FS, and the
# UnifiedParticleRenderer point sprite. Same headless-GLES2 idiom (and same
# self-skip) as test_eqe1_alpha_companion_mip above; see
# refactor/T411_etc1_alpha_companion.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_alpha_blend_companion
        test_alpha_blend_companion.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_alpha_blend_companion PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_alpha_blend_companion GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_alpha_blend_companion DISCOVERY_TIMEOUT 120)
ENDIF()

# B954 (GitLab #272): COpenGLES2Driver::createScreenShot() frees the frame it
# returns -- N screenshots on a headless GLES2 device grow the heap by less than
# one frame. Same headless-GLES2 idiom and self-skip as the test above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_screenshot_leak
        test_screenshot_leak.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_screenshot_leak PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_screenshot_leak GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_screenshot_leak DISCOVERY_TIMEOUT 120)
ENDIF()

# B959 (GitLab #277): the driver's program-binary cache lands in the resolved
# cache directory ($WILLEQ_CACHE_DIR here) and nowhere under the CWD. Same
# headless-GLES2 idiom and self-skip as test_screenshot_leak.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_shader_cache_path
        test_shader_cache_path.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_shader_cache_path PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_shader_cache_path GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_shader_cache_path DISCOVERY_TIMEOUT 120)
ENDIF()

# T484 (board B726): the sky dome's time-of-day vertex tint reaches SOLID3D_FS
# in R,G,B order. Real sky.s3d / sky.ini / sky.eqe1 through the real
# SkyRenderer on a headless GLES2 device; self-skips without the device or the
# assets. See refactor/T484_sky_colour_mismatch.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_sky_dome_tint_channel_order
        test_sky_dome_tint_channel_order.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sky_dome_tint_channel_order PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_sky_dome_tint_channel_order GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_sky_dome_tint_channel_order DISCOVERY_TIMEOUT 120)
ENDIF()

# T495 (board B789/B790): the time-of-day sky colour table's channel lerp,
# pure. No device, no assets -- the table moved to its own TU
# (src/client/graphics/sky_color_table.cpp) so it could be driven this way.
# See refactor/T495_sky_fog_colour_underflow.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_sky_color_interpolation
        test_sky_color_interpolation.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sky_color_interpolation PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_sky_color_interpolation GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_sky_color_interpolation DISCOVERY_TIMEOUT 120)
ENDIF()

# T417 (board B300): pixel proof that the SECOND particle sheet -- the spell
# sprite atlas -- reaches the point sprite and that a cell index selects the
# right cell on it. Same headless-GLES2 idiom and self-skip as the two above;
# see refactor/T417_real_spell_sprites.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spell_sprite_sheet_draw
        test_spell_sprite_sheet_draw.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spell_sprite_sheet_draw PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spell_sprite_sheet_draw GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_spell_sprite_sheet_draw DISCOVERY_TIMEOUT 120)
ENDIF()

# T156: /useskill + the hotbar/skills-popup intent producers. Same graphics
# gate as test_static_ui_panels above (it drives StaticUIInput), and no device
# is created — see refactor/T156_skill_reachability.md. Deliberately a NEW
# file: test_command_processor.cpp was past the 3000-line FAIL ceiling when
# T156 landed (T165 has since split it).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_skill_reachability
        test_skill_reachability.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_skill_reachability PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_skill_reachability GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_skill_reachability DISCOVERY_TIMEOUT 120)
ENDIF()

# B1025-H3 (GitLab #345): the hotbar assignment gesture's state machine --
# the hold timer (tickHotbarHold), the deferred-action holder
# (HotButtonHolder), the pickup clear (stepHotbarGesture), the /hotbutton
# command's button (applyHotbarAssignPending), the placement displacement,
# and the end-to-end press loops through the REAL StaticUIInput. Same
# graphics gate as test_skill_reachability (it drives StaticUIInput); no
# device, no layout assets -- the slot geometry is hotbar_layout.h's own
# functions. See refactor/B1025-H3/README.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_hotbar_assignment
        test_hotbar_assignment.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotbar_assignment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotbar_assignment GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_hotbar_assignment DISCOVERY_TIMEOUT 120)
ENDIF()

# T272: the spellbook popup's click -> MemorizeSpellIntent -> SpellManager
# route (destination-gem policy, the real StaticUIInput producer, and the
# eq_bridge.cpp consumer arm whose arguments were swapped). Needs Irrlicht
# headers for static_panels.h and willeq.db for the consumer leg (self-skips
# without it, test_spell_manager's precedent).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spellbook_memorize
        test_spellbook_memorize.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_spellbook_memorize PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_INCLUDE_DIRECTORIES(test_spellbook_memorize PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spellbook_memorize GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_spellbook_memorize DISCOVERY_TIMEOUT 120)
ENDIF()

# B1076 (GitLab #396) fix leg 2, review t_d00ada41 findings 1-3: the
# edge-arrow spread-turn (one body, pinned against the slash commands),
# the scribe gauge lifecycle (real bridge arms), and the swap commit
# (the committing right-click pushes SwapSpellbookSlotsIntent -- the
# measured OP_SwapSpell wire -- plus the model exchange). Needs Irrlicht
# headers for static_panels.h/static_ui_input.h and willeq.db for the
# spellbook-model leg (self-skips without it, test_spellbook_memorize's
# precedent).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spellbook_fix2
        test_spellbook_fix2.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_spellbook_fix2 PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_INCLUDE_DIRECTORIES(test_spellbook_fix2 PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spellbook_fix2 GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_spellbook_fix2 DISCOVERY_TIMEOUT 120)
ENDIF()

# T277: clicking a buff off the buff bar. Geometry (hitTestBuffBar), the
# click producer (StaticUIInput), the OP_Buff request bytes, and the real
# eq_bridge consumer arm asserting the buff is NOT removed optimistically.
# Needs Irrlicht headers for static_panels.h and willeq.db for the consumer
# legs (self-skips without it, test_spellbook_memorize's precedent).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_buff_click_off
        test_buff_click_off.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_buff_click_off PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_INCLUDE_DIRECTORIES(test_buff_click_off PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_buff_click_off GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_buff_click_off DISCOVERY_TIMEOUT 120)
ENDIF()

# T290: the loot window, restored. The four bridge-arm bodies (applyLoot*),
# the row/button geometry that renderLootPopup() shares with the hit tests,
# and the real StaticUIInput producer for the three loot intents. Same
# graphics gate as test_buff_click_off (needs Irrlicht headers for
# static_panels.h); no device, no server, no db.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_loot_panel
        test_loot_panel.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_loot_panel PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_loot_panel GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_loot_panel DISCOVERY_TIMEOUT 120)
ENDIF()

# T424 (board B351): the UI sprite roster -- the enum, the JSON name map, the
# builder and the shipped ui_atlas.json must name the same fifteen sprites, and
# every one of them must have a consumer. Reads files only (no device, no GPU);
# links willeq_core for EQE1Loader to read the pack's raw ui_atlas.json entry.
# Graphics builds only (B1038): EQE1Loader is a renderer TU (GRAPHICS_SOURCES)
# and the roster is the rendered UI's.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_sprite_roster
        test_ui_sprite_roster.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_ui_sprite_roster PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    TARGET_LINK_LIBRARIES(test_ui_sprite_roster GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_ui_sprite_roster DISCOVERY_TIMEOUT 120)
ENDIF()

# T500 (B836/B837): the entity_models byte split and the merged-texture-map
# sharing it paid for. Same EDT_NULL + willeq_core gate as test_chr_cache_lru;
# opens the shipped data/willeq.db and the real client tree and skips without
# either. It asserts in LEDGER bytes, so it is one of the B819 tests that must
# be run at -O0 as well as -O2.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_model_sharing
        test_entity_model_sharing.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_model_sharing PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_model_sharing GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_model_sharing PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_model_sharing DISCOVERY_TIMEOUT 120)
ENDIF()

# B442 (GitLab #151): the click-to-cursor gesture -- the button map, synthetic
# clicks over the inventory/purse/bank/loot/merchant/QuantityWnd rects, the
# game-thread ApplyCursorGesture() over a real InventoryManager (the
# MoveItemIntent sequence), and /pickup /putdown /cursor /coin. No device, no
# server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_cursor_gesture
        test_cursor_gesture.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_cursor_gesture PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_cursor_gesture GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_cursor_gesture DISCOVERY_TIMEOUT 120)

    # B442b (#151 remainder, #152): the bag toggle, the held/kept item
    # display, coin on the cursor and the bank's coin rows, the drop/destroy
    # confirmation dialog and its wire (OP_GroundSpawn, OP_MoveItem 30->-1),
    # the typed Quantity box, and the merchant's select-to-sell model -- the
    # render half with no device, the game half on a REAL EverQuest over
    # FakeUdpTransport with the shipped eqstr table.
    ADD_EXECUTABLE(test_cursor_gesture_completion
        test_cursor_gesture_completion.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_cursor_gesture_completion PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_cursor_gesture_completion GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_cursor_gesture_completion PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_cursor_gesture_completion DISCOVERY_TIMEOUT 120)
ENDIF()

# B895 (GitLab #211): the Tracking Window -- its XML layout, the direction
# rule across the compass against eqemu's DoTracking thresholds, the list
# rules, and the whole route (OP_Track reply -> TrackingListChanged -> panel
# -> TrackingIntent -> "You begin tracking" -> direction ticks). A headless
# EverQuest and the shipped eqstr table; no device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_tracking_window
        test_tracking_window.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_tracking_window PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_tracking_window GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_tracking_window PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_tracking_window DISCOVERY_TIMEOUT 120)
ENDIF()

# B1066 (GitLab #386): a mouse press is owned by the static UI or the camera,
# never both -- PressOwnership, the event receiver over real Irrlicht mouse
# events, and the wiring (mouse-look reads the camera-owned view, the item
# display the physical hold). No device, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_press_ownership
        test_press_ownership.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_press_ownership PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_press_ownership GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_press_ownership DISCOVERY_TIMEOUT 120)
ENDIF()
