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
        GTEST_DISCOVER_TESTS(test_zoning_graphics_integration PROPERTIES LABELS "live")
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_inventory_model_view)
ENDIF()

# T309: the measured first-person draw rule (predicate + the withhold seam).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_body_draw_rule
        test_first_person_body_draw_rule.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_body_draw_rule PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_body_draw_rule GTest::gtest willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_first_person_body_draw_rule PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_first_person_body_draw_rule)
ENDIF()

# T301 Leg C: first-person weapon attachment geometry (probe).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_first_person_weapon_attachment
        test_first_person_weapon_attachment.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_first_person_weapon_attachment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_first_person_weapon_attachment GTest::gtest willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_first_person_weapon_attachment PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_first_person_weapon_attachment)
ENDIF()

# T301 Leg D: the UI-hide toggle, from the paths a user or a capture script
# reaches (/hideui and the F11 hotkey), plus source pins on the two single-site
# steps that need a GL device to execute.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ui_hide_command
        test_ui_hide_command.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_hide_command PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_ui_hide_command GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_ui_hide_command PRIVATE
        WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_ui_hide_command)
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
        WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_debug_hotkey_arms)
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
        WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_autorun_arm)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_race_330_loader)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_chr_cache_lru)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_chr_path_derivation)
ENDIF()

# T340 Leg A (B052): drives RaceModelLoader's variant/animation loader through
# every exit G07 enumerated -- primary hit, primary-miss/fallback-hit, an
# unusable matched character, all candidates exhausted, and an empty candidate
# list. Same EDT_NULL + willeq_core gate as test_race_330_loader; opens the
# shipped data/willeq.db and the real client tree and skips without either.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_variant_loader_candidates
        test_variant_loader_candidates.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_variant_loader_candidates PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_variant_loader_candidates GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_variant_loader_candidates PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_variant_loader_candidates)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_palette_mask_plumbing)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_race_names)
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
    GTEST_DISCOVER_TESTS(test_static_ui_panels)
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
    GTEST_DISCOVER_TESTS(test_ui_preferences)
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
    GTEST_DISCOVER_TESTS(test_options_window)
ENDIF()

IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_tradeskill_combat_panels
        test_tradeskill_combat_panels.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_tradeskill_combat_panels PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_tradeskill_combat_panels GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_tradeskill_combat_panels)
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
    GTEST_DISCOVER_TESTS(test_recipe_list)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_item_display)
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
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_spell_display)
ENDIF()

# T429 Leg A (board B390): the two display composers take their format strings
# from the shipped eqstr table rather than from their own source. Needs no
# device -- it is registered here beside the two windows it covers.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_display_text_from_db
        test_display_text_from_db.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_display_text_from_db PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_display_text_from_db GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_display_text_from_db PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    GTEST_DISCOVER_TESTS(test_display_text_from_db)
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
    GTEST_DISCOVER_TESTS(test_merchant_bank_cursor)
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
    GTEST_DISCOVER_TESTS(test_options_merchant_residue)
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
    GTEST_DISCOVER_TESTS(test_dead_renderer_members)
ENDIF()

# T421 Leg B (board B365): the ONE text-entry widget, and the tradeskill
# search box using it.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_search_entry
        test_search_entry.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_search_entry PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_search_entry GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_search_entry)
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
    GTEST_DISCOVER_TESTS(test_pet_buttons)
ENDIF()

# T413 (board B285): the software cursor's cache name vs the pack's entry name.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_software_cursor_pack_name
        test_software_cursor_pack_name.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_software_cursor_pack_name PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_software_cursor_pack_name GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_software_cursor_pack_name PRIVATE
        WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_software_cursor_pack_name)
ENDIF()

# T413 (board B296): the bridge payload fields nothing drew. Pure state +
# geometry over static_panels/UILayout — no device, no server, no DISPLAY.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bridge_payload_fields
        test_bridge_payload_fields.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bridge_payload_fields PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bridge_payload_fields GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_bridge_payload_fields)
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
    GTEST_DISCOVER_TESTS(test_ui_atlas_pixels)
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
        WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    GTEST_DISCOVER_TESTS(test_static_ui_scroll)
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
    GTEST_DISCOVER_TESTS(test_eqe1_alpha_companion_mip)
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
    GTEST_DISCOVER_TESTS(test_alpha_blend_companion)
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
    GTEST_DISCOVER_TESTS(test_spell_sprite_sheet_draw)
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
    GTEST_DISCOVER_TESTS(test_skill_reachability)
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
    TARGET_COMPILE_DEFINITIONS(test_spellbook_memorize PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_INCLUDE_DIRECTORIES(test_spellbook_memorize PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spellbook_memorize GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_spellbook_memorize)
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
    TARGET_COMPILE_DEFINITIONS(test_buff_click_off PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_INCLUDE_DIRECTORIES(test_buff_click_off PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_buff_click_off GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_buff_click_off)
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
    GTEST_DISCOVER_TESTS(test_loot_panel)
ENDIF()

# T424 (board B351): the UI sprite roster -- the enum, the JSON name map, the
# builder and the shipped ui_atlas.json must name the same fifteen sprites, and
# every one of them must have a consumer. Reads files only (no device, no GPU);
# links willeq_core for EQE1Loader to read the pack's raw ui_atlas.json entry.
ADD_EXECUTABLE(test_ui_sprite_roster
    test_ui_sprite_roster.cpp
)
TARGET_COMPILE_DEFINITIONS(test_ui_sprite_roster PRIVATE
    WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
TARGET_LINK_LIBRARIES(test_ui_sprite_roster GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_ui_sprite_roster)
