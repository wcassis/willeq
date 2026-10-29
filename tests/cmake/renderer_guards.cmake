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
    GTEST_DISCOVER_TESTS(test_bone_attachment DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_constrained_texture_cache DISCOVERY_TIMEOUT 120)
ENDIF()

# B1083 Step 7 (GitLab #403, ruling 2026-10-17 option A): the driver-facing
# name of a cache entry is decided ONCE at store time (family-first) and
# stored -- a single-archive family keeps the pre-B1083 name (which restores
# the gate zones' base draw order), a cross-archive twin takes the full
# internalKey (the willow survives the driver's name-dedup), and an
# evict->restore re-creates the entry under the SAME name. EDT_NULL for the
# name decision; the evict/restore round-trip uses a real EDT_OGLES2
# surfaceless device and self-skips without EGL.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_texture_driver_name
        test_texture_driver_name.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_texture_driver_name PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # Post-fix state: the stored driverName field exists, so the test's
    # private-field assertions compile in (the red-before run at the pre-fix
    # tree omits the define and gets the public-API red instead).
    TARGET_COMPILE_DEFINITIONS(test_texture_driver_name PRIVATE B1083_STEP7_DRIVER_NAME_STORED)
    TARGET_LINK_LIBRARIES(test_texture_driver_name GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_texture_driver_name DISCOVERY_TIMEOUT 120)
ENDIF()

# B1083 (GitLab #403): a zone family's packs (zone + _obj) hold the same name
# with different content; the (archive, name) cache key + source-first EQE1
# resolution keeps each copy for the material that came from it. Two
# synthetic v1 packs mirror the measured qeytoqrg tree70 collision (opaque
# vs alpha companion). Real EDT_OGLES2 surfaceless device (the ETC1 upload
# needs live GL); self-skips when unavailable.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_eqe1_source_archive
        test_eqe1_source_archive.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_eqe1_source_archive PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_eqe1_source_archive GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_eqe1_source_archive DISCOVERY_TIMEOUT 120)
ENDIF()

# B944 (GitLab #262): eviction under the ceiling keeps the ITexture object and
# the driver's first bind restores it. Live consumers (materials the cache does
# not know) are driven past the ceiling; the objects must stay valid, the
# accounting must count resident storage only, and a draw must restore the
# same pixels. Real EDT_OGLES2 surfaceless device; the ETC1 half also needs
# qeynos2_obj.eqe1. Self-skips when either is unavailable.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_texture_eviction_residency
        test_texture_eviction_residency.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_texture_eviction_residency PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # (B950, #268 removed this test's per-target EQT_HAS_TEXTURE_ATLAS
    # define: it is PUBLIC on willeq_core now, so it flows in through the
    # link, and test_zone_geometry_atlas_layout keeps the two states equal.)
    TARGET_LINK_LIBRARIES(test_texture_eviction_residency GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_texture_eviction_residency DISCOVERY_TIMEOUT 120)
ENDIF()

# B947 (GitLab #265): every evictable texture has a restore source. A REAL
# cached animated character mesh (RaceModelLoader, S3D path) is pushed far
# below the working set; every texture its materials hold must stay the
# cache's live object and re-bind to the same pixels. Also: equipment origins,
# plain-file origins + the digest guard, pinning, protected textures, and an
# EQE1 character pack opened late (T266). Real EDT_OGLES2 surfaceless device;
# self-skips without EGL, data/eq_client, the shipped db or the EQE1 tree.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_texture_eviction_character_restore
        test_texture_eviction_character_restore.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_texture_eviction_character_restore PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # (B950, #268 removed this test's EQT_HAS_TEXTURE_ATLAS half of the
    # compile definitions: PUBLIC on willeq_core now, flows in through the
    # link; the WILLEQ_DB_REAL_PATH half is unrelated and stays.)
    TARGET_COMPILE_DEFINITIONS(test_texture_eviction_character_restore PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_LINK_LIBRARIES(test_texture_eviction_character_restore GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_texture_eviction_character_restore DISCOVERY_TIMEOUT 120)
ENDIF()

# B950 (GitLab #268) + B1019 (GitLab #340): a plain consumer and willeq_core
# must see the SAME ZoneMeshBuilder layout. One test TU, NO explicit define
# of its own: it sees zone_geometry.h in the state a bare willeq_core
# consumer does (WITHOUT EQT_HAS_TEXTURE_ATLAS before the fix, WITH it once
# the define is PUBLIC on willeq_core and flows in through the link), and it
# compares its own sizeof against willeq_core's own
# ZoneMeshBuilder::layoutSize() -- the sizeof of the class as willeq_core's
# zone_geometry.cpp was built. Before the fix the consumer is 8 bytes
# smaller and the test is RED. B1019 replaced the old shape -- a sibling
# "core" probe TU carrying a hand-placed per-source define -- which pinned
# the macro state of THIS test target, not of willeq_core.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_geometry_atlas_layout
        test_zone_geometry_atlas_layout.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_geometry_atlas_layout PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_geometry_atlas_layout GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_geometry_atlas_layout DISCOVERY_TIMEOUT 120)
ENDIF()

# B1051 (GitLab #371): a multi-buffer character mesh must resolve its per-part
# textures from the ConstrainedTextureCache when the raw S3D bytes were
# released (EQE1 authoritative). Reds on the pre-fix code: getOrLoadTexture()
# only knew its own textureCache_/pendingTextures_ maps, so a cache-resident
# per-part texture resolved to nullptr and the buffer drew the textureless
# (255,200,180,160) diffuse -- the live ETC1 "smear" on multi-buffer
# characters (Guard_Treitan, race 71) while single-buffer zone meshes and the
# raw-RGBA run (bytes not released) rendered correctly. EDT_NULL, same idiom
# as test_constrained_texture_cache.cpp.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b1051_mesh_texture_fallback
        test_b1051_mesh_texture_fallback.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b1051_mesh_texture_fallback PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b1051_mesh_texture_fallback GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b1051_mesh_texture_fallback DISCOVERY_TIMEOUT 120)
ENDIF()

# B949 (GitLab #267): LRU follows DRAW order, not load order. Drives a real
# EDT_OGLES2 surfaceless device through beginScene -> draw -> endScene ->
# endFrame "frames" and asserts that a texture bound every frame survives
# eviction pressure from loads (and that a no-draw control still evicts in
# load order, which is what makes the first test discriminating). Self-skips
# without EGL, exactly like test_texture_eviction_residency above.
if(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_texture_lru_draw_order
        test_texture_lru_draw_order.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_texture_lru_draw_order PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # (B950, #268 removed this test's per-target EQT_HAS_TEXTURE_ATLAS
    # define: PUBLIC on willeq_core now, flows in through the link.)
    TARGET_LINK_LIBRARIES(test_texture_lru_draw_order GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_texture_lru_draw_order DISCOVERY_TIMEOUT 120)
endif()

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
    GTEST_DISCOVER_TESTS(test_object_node_texture_guard DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_t124_texture_holder_guards DISCOVERY_TIMEOUT 120)
ENDIF()

# T460 (board B520): the DRM software cursor survives zone-unload clear() and
# LRU pressure (it used to be evicted and never reloaded).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_software_cursor_eviction
        test_software_cursor_eviction.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_software_cursor_eviction PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_software_cursor_eviction GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_software_cursor_eviction PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_software_cursor_eviction DISCOVERY_TIMEOUT 120)
ENDIF()

# T463 (boards B522-B524): storm clouds, tumbleweeds and boids keep drawing a
# live texture across zone-unload clear() and LRU pressure, plus the census
# that makes every getOrLoad() site in the renderer declare a lifetime policy.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_environment_texture_eviction
        test_environment_texture_eviction.cpp
        test_texture_cache_holder_census.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_environment_texture_eviction PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_environment_texture_eviction GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_environment_texture_eviction PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_environment_texture_eviction DISCOVERY_TIMEOUT 120)
ENDIF()

# T467 (board B550): a lazily-built environment manager is wired exactly like
# the init-time one -- the renderer's own applyEnvironmentalDisplaySettings()
# on an EDT_NULL device with the real cache -- plus the census that each of
# the four has ONE construction site under src/.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_environment_manager_construction
        test_environment_manager_construction.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_environment_manager_construction PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_environment_manager_construction GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_environment_manager_construction PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_environment_manager_construction DISCOVERY_TIMEOUT 120)
ENDIF()

# T468 (board B590): a door's texture across LRU eviction and zone unload,
# through the door manager setupInstantScene() really builds, plus the census
# that DoorManager has ONE construction site (the one that registers the T124
# door texture guard).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_door_texture_eviction
        test_door_texture_eviction.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_door_texture_eviction PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_door_texture_eviction GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_door_texture_eviction PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_door_texture_eviction DISCOVERY_TIMEOUT 120)
ENDIF()

# T381 (board B160): the spell visual vocabulary -- resist type, casting
# school and spell_affect_index -- reaches SpellVisualFX across the bridge by
# value, so the six resist colours, the five school colours, the spray cast
# style and the whole spells.eff resolution layer stop being dead code.
# Needs willeq.db (spell rows) and the EQ client data (spells.eff); both are
# GTEST_SKIP'd, never failed, when absent.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spell_visual_vocabulary
        test_spell_visual_vocabulary.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spell_visual_vocabulary PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_COMPILE_DEFINITIONS(test_spell_visual_vocabulary PRIVATE
        "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_LINK_LIBRARIES(test_spell_visual_vocabulary GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_spell_visual_vocabulary DISCOVERY_TIMEOUT 120)
ENDIF()

# B1181 (#500): the +0x65 IT model on a type != 9 missile object -- the name
# from the payload bytes, the per-frame orientation rule, removal at
# lifetime / hit, and the untouched type-9 path. The device-free half is
# pure logic; the FX half drives SpellVisualFX on an EDT_NULL device with
# the real equipment model loader (GTEST_SKIP'd without the client tree).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b1181_item_missile
        test_b1181_item_missile.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b1181_item_missile PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b1181_item_missile GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b1181_item_missile DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_t126_equip_texture_guard DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_irrlicht_camera_command DISCOVERY_TIMEOUT 120)

    # T461: the F9 arm (which had never existed), the keyboard-orbit arms, zoom
    # and the rig surviving a player move -- through FrameComponent on the same
    # bare, never-Init()'d IrrlichtRenderer.
    ADD_EXECUTABLE(test_camera_mode_wiring
        test_camera_mode_wiring.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_camera_mode_wiring PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_camera_mode_wiring GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_camera_mode_wiring DISCOVERY_TIMEOUT 120)

    # T464 (B532): a zone load keeps the player's saved camera -- through
    # IrrlichtRenderer::placeCameraForZoneIn() and the real syncUiPreferences().
    ADD_EXECUTABLE(test_camera_zone_in_preference
        test_camera_zone_in_preference.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_camera_zone_in_preference PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_camera_zone_in_preference GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_camera_zone_in_preference DISCOVERY_TIMEOUT 120)

    # T464 (B507/B508): the Chase camera's BSP ceiling clamp, on constructed
    # BSP trees and on nro's real one (skips without the EQ client files).
    ADD_EXECUTABLE(test_camera_bsp_ceiling
        test_camera_bsp_ceiling.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_camera_bsp_ceiling PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_camera_bsp_ceiling GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_camera_bsp_ceiling DISCOVERY_TIMEOUT 120)

    # T474 (B650): the Chase camera at T466's oracle fit -- pose, heading,
    # zoom, wall pull-in, the eye-region ceiling and `/camera set` in Chase --
    # on a bare renderer with a real EDT_NULL camera node.
    ADD_EXECUTABLE(test_chase_camera_fit
        test_chase_camera_fit.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_chase_camera_fit PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_chase_camera_fit GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_chase_camera_fit DISCOVERY_TIMEOUT 120)
ENDIF()

# L01 (Batch L, refactor/L01_rdp_component.md): the RendererContext binding
# and the RdpComponent extracted from IrrlichtRenderer. Same bare,
# never-Init()'d IrrlichtRenderer idiom as test_irrlicht_camera_command above.
# Registered here rather than next to test_rdp_input_handler in audio.cmake
# because it links willeq_core; the RDP assertions are #ifdef WITH_RDP so the
# binary builds and passes in a configure without FreeRDP.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_rdp_component
        test_rdp_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_rdp_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_rdp_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_rdp_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L02 (Batch L, refactor/L02_eqe1_effects_component.md): the EQE1 archive
# family + fire-glow icospheres extracted from IrrlichtRenderer. Same bare,
# never-Init()'d IrrlichtRenderer idiom as test_rdp_component above; the one
# asset-dependent case skips when the EQE1 tree is not mounted.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_eqe1_archive_component
        test_eqe1_archive_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_eqe1_archive_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_eqe1_archive_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_eqe1_archive_component DISCOVERY_TIMEOUT 120)
ENDIF()

# B1096 (#416) -- the parent-side RED: initLoadingScreen must install the
# EQE1 name->archive resolution before the pre-zone model build. Pure source
# scan (no new symbol, no assets), so this TU COMPILES ON THE PARENT too --
# that is what makes it the red: on the parent the anchor is missing and the
# test fails. The behavioral death+green (test_pre_zone_eqe1_resolution)
# names the new seam and so builds only at the head.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_pre_zone_eqe1_resolution_order
        test_pre_zone_eqe1_resolution_order.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_pre_zone_eqe1_resolution_order PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_pre_zone_eqe1_resolution_order GTest::gtest_main)
    TARGET_COMPILE_DEFINITIONS(test_pre_zone_eqe1_resolution_order PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_pre_zone_eqe1_resolution_order DISCOVERY_TIMEOUT 120)
    # (No SET_TESTS_PROPERTIES: under PRE_TEST discovery (T163) the test does
    # not exist at configure, so a properties line would fail the configure.
    # None is needed anyway: WILLEQ_SOURCE_DIR resolves to the checkout at
    # RUN time via willeq_paths.txt, so the scan reads the checkout, not the
    # build tree, and ctest's CWD is irrelevant.)
ENDIF()

# B1096 (#416): the behavioral net -- death test (the FATAL branch) + the
# early-resolution green. Same bare-renderer idiom as the test above; the two
# asset-dependent cases skip when the ETC1 tree is not mounted. The death
# test forks, so it needs no display or driver -- getOrLoad() exits before
# any GL call.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_pre_zone_eqe1_resolution
        test_pre_zone_eqe1_resolution.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_pre_zone_eqe1_resolution PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_pre_zone_eqe1_resolution GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_pre_zone_eqe1_resolution PRIVATE
        "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_pre_zone_eqe1_resolution DISCOVERY_TIMEOUT 120)
    # (No SET_TESTS_PROPERTIES: same PRE_TEST reason as the scan test above.
    # The index -- a shipped-data load -- is found instead because the test
    # itself pins WILLEQ_DATA_ROOT to the checkout at run time, where
    # ctest's CWD is <build>/tests.)
ENDIF()

# L03 (Batch L, refactor/L03_overlays_component.md): the OverlaysComponent
# extracted from IrrlichtRenderer. Same bare, never-Init()'d IrrlichtRenderer
# idiom as test_rdp_component above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_overlays_component
        test_overlays_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_overlays_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_overlays_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_overlays_component DISCOVERY_TIMEOUT 120)
ENDIF()

# B1064 (#384): the target outline needs no stencil buffer -- the pass plan
# (inverted hull on every surface, stencil bits 0 included), the hull's
# transform against hand-computed matrices, and a scan of drawTargetOutline()
# for any stencil call. GL-free: compiles its one TU, no willeq_core.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_target_outline_pass
        test_target_outline_pass.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/overlays/target_outline_pass.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_target_outline_pass PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${IRRLICHT_INCLUDE_DIR})
    TARGET_COMPILE_DEFINITIONS(test_target_outline_pass PRIVATE
        "TARGET_OUTLINE_OVERLAY_SOURCE=WILLEQ_REPO_PATH(\"src/client/graphics/overlays/overlays_component.cpp\")")
    TARGET_LINK_LIBRARIES(test_target_outline_pass GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_target_outline_pass DISCOVERY_TIMEOUT 120)
ENDIF()

# L04 (Batch L, refactor/L04_diagnostics_component.md): the DiagnosticsComponent
# extracted from IrrlichtRenderer, and the breadth test of RendererContext --
# both the ctx_ path and the owner_ transition-handle path are pinned by
# mutation. Same bare, never-Init()'d IrrlichtRenderer idiom.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_diagnostics_component
        test_diagnostics_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_diagnostics_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_diagnostics_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_diagnostics_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L05 (Batch L, refactor/L05_player_camera_component.md): PlayerCameraComponent
# extracted from IrrlichtRenderer -- both binding paths (ctx_ and the owner_
# transition handle) pinned by mutation, plus the six surviving forwarders.
# Same bare, never-Init()'d IrrlichtRenderer idiom.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_player_camera_component
        test_player_camera_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_player_camera_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_player_camera_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_player_camera_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L06 (Batch L, refactor/L06_slash_commands_component.md): SlashCommandsComponent
# extracted from IrrlichtRenderer -- the surviving forwarder must reach the
# component, and all four routes the moved body uses (owner_.member_,
# owner_.Method(), ctx_ and the two sibling components) must land on the
# renderer's own storage. Same bare, never-Init()'d IrrlichtRenderer idiom.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_slash_commands_component
        test_slash_commands_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_slash_commands_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_slash_commands_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_slash_commands_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L07 (Batch L, refactor/L07_sim_bridge_component.md): SimBridgeComponent
# extracted from IrrlichtRenderer -- both binding paths (ctx_ and the owner_
# transition handle) pinned by mutation, plus the worker-handle early-outs that
# prove simulationWorker_ stayed on IrrlichtRenderer (no thread-ownership
# change). Same bare, never-Init()'d IrrlichtRenderer idiom.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_sim_bridge_component
        test_sim_bridge_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sim_bridge_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_sim_bridge_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_sim_bridge_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L08 (Batch L): ZoneStreamComponent -- progressive load, PVS neighbourhood,
# deferred objects and the T124 texture guards. Same bare, never-Init()'d
# IrrlichtRenderer idiom as test_sim_bridge_component above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_stream_component
        test_zone_stream_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_stream_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_stream_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_stream_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L09 (Batch L): ZoneLifecycleComponent -- zone teardown/environment/ready
# state, the manual + portal zone draw, and the GPU upload thread's
# renderer-side driver. Same bare, never-Init()'d IrrlichtRenderer idiom as
# test_zone_stream_component above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_lifecycle_component
        test_zone_lifecycle_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_lifecycle_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_lifecycle_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_lifecycle_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L10 (Batch L): ZoneLoadComponent -- loadZoneSequential() and the twenty
# per-step methods that build a zone, extracted from IrrlichtRenderer. Same
# bare, never-Init()'d IrrlichtRenderer idiom as test_zone_lifecycle_component
# above; the test drives the real loading-thread entry point end to end on a
# renderer with no zone, which is the cheapest whole-substitution-set proof
# available without a device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_load_component
        test_zone_load_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_load_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_load_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_load_component DISCOVERY_TIMEOUT 120)
ENDIF()

# L11 (refactor/L11_frame_component.md): FrameComponent -- the per-frame loop,
# the input drain and the static UI panel state, extracted from
# IrrlichtRenderer. Same bare, never-Init()'d IrrlichtRenderer idiom as
# test_zone_load_component above; every phase this test drives null-guards the
# device it would otherwise need.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_frame_component
        test_frame_component.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_frame_component PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_frame_component GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_frame_component DISCOVERY_TIMEOUT 120)
ENDIF()

# B1085 (GitLab #405): the three chat key latches (enter/escape/slash) latch
# ONLY while the chat is unfocused and every setChatInputFocused()
# transition resets them. The frame-input half runs the production policy
# bodies (processChatInput's enter/slash checks, applyTextEntryKey(),
# handleEscapeKey()) through the same bare, never-Init()'d IrrlichtRenderer
# idiom as test_frame_component above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b1085_chat_latches
        test_b1085_chat_latches.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b1085_chat_latches PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b1085_chat_latches GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b1085_chat_latches DISCOVERY_TIMEOUT 120)
ENDIF()

# T329 (B041): the NavmeshChanged bridge arm forwarding to
# OverlaysComponent::setNavmesh. Same bare, never-Init()'d IrrlichtRenderer
# idiom as test_overlays_component above, plus a real eqt::bridge::
# IrrlichtBridge -- willeq_core already carries irrlicht_bridge.cpp under
# EQT_GRAPHICS.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_navmesh_overlay_wire
        test_navmesh_overlay_wire.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_navmesh_overlay_wire PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_navmesh_overlay_wire GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_navmesh_overlay_wire DISCOVERY_TIMEOUT 120)

    ADD_EXECUTABLE(test_vision_buff_fade_wire
        test_vision_buff_fade_wire.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_vision_buff_fade_wire PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # T339 (B055): the landing half drives a real BuffManager over the real
    # spell DB and reads src/client/core/everquest.cpp, so it owns its main()
    # (GTest::gtest, not gtest_main) and needs the DB path.
    TARGET_COMPILE_DEFINITIONS(test_vision_buff_fade_wire PRIVATE
        "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    TARGET_LINK_LIBRARIES(test_vision_buff_fade_wire GTest::gtest willeq_core)
    GTEST_DISCOVER_TESTS(test_vision_buff_fade_wire DISCOVERY_TIMEOUT 120)

    # B448 (GitLab #29): a race change on the LOCAL player (OP_Illusion /
    # `#race N`) reaches setPlayerRace() through the bridge -- real EverQuest
    # publisher, real IrrlichtBridge consumer, bare renderer.
    ADD_EXECUTABLE(test_self_race_vision
        test_self_race_vision.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_self_race_vision PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_self_race_vision GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_self_race_vision DISCOVERY_TIMEOUT 120)
ENDIF()

# T333 (B048): the navmesh overlay's DRAW -- it drew nothing and wiped the 2D
# UI. Driver-state defect, so this one needs a real headless GLES2 device
# (surfaceless EGL / llvmpipe) and a framebuffer readback, the same harness as
# test_ui_atlas_pixels.cpp; it self-skips when that device is unavailable. No
# EQE1 asset and no server -- the triangle list is seeded through a test seam.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_navmesh_overlay_draw
        test_navmesh_overlay_draw.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_navmesh_overlay_draw PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_navmesh_overlay_draw GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_navmesh_overlay_draw DISCOVERY_TIMEOUT 120)
ENDIF()

# T335 (B054): the same defect and the same proof shape for the two siblings
# that share the debug-overlay pass -- the Ctrl+M map overlay and the
# collisionDebug lines. Both now go through WorldLineBatch
# (include/client/graphics/world_line_batch.h). Real headless GLES2 device;
# they self-skip when it is unavailable, and need no EQE1 asset and no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_map_overlay_draw
        test_map_overlay_draw.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_map_overlay_draw PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_map_overlay_draw GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_map_overlay_draw DISCOVERY_TIMEOUT 120)

    ADD_EXECUTABLE(test_collision_debug_draw
        test_collision_debug_draw.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_collision_debug_draw PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_collision_debug_draw GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_collision_debug_draw DISCOVERY_TIMEOUT 120)
ENDIF()

# T335b (B054): the last of the three siblings -- the Ctrl+V frustum debug
# wireframe. It lives inside FrameComponent::renderDebugOverlays(), every
# statement of which is pinned by tests/goldens/frame_render_order_g05.txt, so
# the test drives the whole pass with every other overlay in it left OFF.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_frustum_debug_draw
        test_frustum_debug_draw.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_frustum_debug_draw PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_frustum_debug_draw GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_frustum_debug_draw DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_first_person_tunables DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_renderer_command_echo DISCOVERY_TIMEOUT 120)
ENDIF()

# T294: the `I` keybind must reach a working IrrlichtRenderer::toggleInventory()
# (it was an empty stub left by U07c2, so the inventory popup was reachable
# only via /inventory). Drives the real RendererEventReceiver + drainActions() +
# processPlayerInput() on a bare, never-Init()'d IrrlichtRenderer -- same
# no-device idiom as test_spell_gem_key_race below.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_inventory_toggle
        test_inventory_toggle.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_inventory_toggle PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_inventory_toggle GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_inventory_toggle DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_spell_gem_key_race DISCOVERY_TIMEOUT 120)
ENDIF()

# T319: ConsoleRenderer's target-HP readout. IOutputRenderer::
# updateCurrentTargetHP() had no production caller, so the headless status
# line froze the target's HP at acquisition. No graphics, no device.
ADD_EXECUTABLE(test_console_target_hp
    test_console_target_hp.cpp
)
TARGET_LINK_LIBRARIES(test_console_target_hp GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_console_target_hp DISCOVERY_TIMEOUT 120)

# B1027 (#347): the headless wire leg of the same readout -- an OP_MobHealth
# replay must move the ConsoleRenderer's status-line target HP. At
# origin/fable this is RED: the state EventBus publish EntityStatsChanged
# had no publisher (the MobHealth bridge publish feeds the render-thread
# bridge, not the bus the console subscribes to). The fix's state-bus
# publish in ZoneProcessMobHealth is pinned here and by the state-bus
# census test.
ADD_EXECUTABLE(test_console_target_hp_feed
    test_console_target_hp_feed.cpp
)
TARGET_LINK_LIBRARIES(test_console_target_hp_feed GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_console_target_hp_feed DISCOVERY_TIMEOUT 120)

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
    GTEST_DISCOVER_TESTS(test_reply_to_tell_key DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_bridge_queue_race DISCOVERY_TIMEOUT 120)
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
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_read_and_reset_ownership DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_progressive_load_rearm DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_simulation_output_lifetime DISCOVERY_TIMEOUT 120)
ENDIF()

# T360 Leg A (B066): the vision-type -> zone-light mapping. Drives the two real
# production functions (applyVisionWeatherToZoneLights, computeLightAnimations)
# through SimulationWorker::computeOnce() -- no Irrlicht device, no thread.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_vision_zone_lights
        test_vision_zone_lights.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_vision_zone_lights PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_vision_zone_lights GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_vision_zone_lights DISCOVERY_TIMEOUT 120)
ENDIF()

# T412 Leg A (B291): the skipEntityTextureUpload debug knob must actually skip.
# Drives EntityRenderer::uploadPendingEntityTextures() against a real
# ConstrainedTextureCache on an EDT_NULL device -- no EQ client data, no server.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_texture_upload_skip
        test_entity_texture_upload_skip.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_texture_upload_skip PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_texture_upload_skip GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_entity_texture_upload_skip DISCOVERY_TIMEOUT 120)
ENDIF()

# T412 Leg C (B294): the deleted forcePvsUpdate_ flag and the trigger that
# replaced it. Drives calculateRegions()/computeVisibility() through
# SimulationWorker::computeOnce() -- no Irrlicht device, no thread, no assets --
# plus a source guard (WILLEQ_REPO_ROOT is defined globally in tests/CMakeLists.txt).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_pvs_render_distance_trigger
        test_pvs_render_distance_trigger.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_pvs_render_distance_trigger PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_pvs_render_distance_trigger GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_pvs_render_distance_trigger DISCOVERY_TIMEOUT 120)
ENDIF()

# B1071 (GitLab #391): the current target is never culled by the sim worker
# (distance / PVS / frustum / cap) -- the player's exemption is the template.
# A culled target leaves the scene graph (renderer_sim_bridge.cpp) and is
# invisible on screen while the target outline keeps painting its hull:
# B1064's solid-silhouette still. Drives computeEntityVisibility() through
# SimulationWorker::computeOnce() -- no device, no thread, no assets.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_target_cull_exemption
        test_target_cull_exemption.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_target_cull_exemption PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_target_cull_exemption GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_target_cull_exemption DISCOVERY_TIMEOUT 120)
ENDIF()

# T367 (B080): zone/object lights whose influence sphere contains the camera
# must survive PVS and frustum culling. Drives computeLightVisibility,
# computeObjectLightVisibility and computeLightSelection through
# SimulationWorker::computeOnce() -- no Irrlicht device, no server, no assets.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_light_culling
        test_zone_light_culling.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_light_culling PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_light_culling GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_light_culling DISCOVERY_TIMEOUT 120)
ENDIF()

# T490: the simulation worker's PVS candidate list is built for the camera
# region on demand (SimulationZoneData::pvsCandidatesFor), replacing the
# per-region table the sim bridge used to build at zone load.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_sim_pvs_candidates
        test_sim_pvs_candidates.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sim_pvs_candidates PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_sim_pvs_candidates GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_sim_pvs_candidates DISCOVERY_TIMEOUT 120)
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
        GTEST_DISCOVER_TESTS(test_beneficial_spell_casting PROPERTIES LABELS "live" DISCOVERY_TIMEOUT 120)
    ENDIF()
ENDIF()

# T361 (B067): the vision type -> global ambient floor. Pure logic over
# VisionType (include/client/graphics/vision_ambient.h) -- no device, no
# server, no assets -- but VisionType lives in renderer_types.h, so it needs
# the Irrlicht include path like its neighbours here.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_vision_ambient
        test_vision_ambient.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_vision_ambient PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_vision_ambient GTest::gtest_main willeq_core)
    # T396 (B211): the composition-order cases read the renderer TU to prove
    # updateTimeOfDay() still calls composeSceneAmbient().
    TARGET_COMPILE_DEFINITIONS(test_vision_ambient PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_vision_ambient DISCOVERY_TIMEOUT 120)
ENDIF()

# T428 (B280): the night base of the day/night curve, solved live against the
# official client, plus the curve's own continuity and the day arm's
# independence from it (which is why no daytime golden moved).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_night_ambient
        test_night_ambient.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_night_ambient PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_night_ambient GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_night_ambient DISCOVERY_TIMEOUT 120)
ENDIF()

# T396 (B211): the weather ambient modifier that multiplies the same ambient
# the vision floor then clamps. Drives the real WeatherEffectsController with
# four null device pointers (nothing it touches here needs a device) plus two
# text-level reachability pins, so it needs the Irrlicht include path and the
# repo root like its neighbours.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_weather_ambient_modifier
        test_weather_ambient_modifier.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_weather_ambient_modifier PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_weather_ambient_modifier GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_weather_ambient_modifier PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_weather_ambient_modifier DISCOVERY_TIMEOUT 120)
ENDIF()

# T418 (B231): the ACTIVE-weather curve, measured against the official client
# and mimicked -- the half of B211 T396 deliberately left alone. Same four-null
# controller as its neighbour plus text-level pins on the three ramp copies and
# on the fog blend.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_weather_darkening_oracle
        test_weather_darkening_oracle.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_weather_darkening_oracle PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_weather_darkening_oracle GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_weather_darkening_oracle PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_weather_darkening_oracle DISCOVERY_TIMEOUT 120)
ENDIF()

# T423 (B362): the scene ambient recomposes on every input, not only on a
# clock packet. Behavioural cases on a default-constructed, never-Init()'d
# IrrlichtRenderer (the test_diagnostics_component idiom -- no GL context)
# plus text pins on the sites a headless renderer cannot reach.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_scene_ambient_recompute
        test_scene_ambient_recompute.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_scene_ambient_recompute PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_scene_ambient_recompute GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_scene_ambient_recompute PRIVATE
        "WILLEQ_REPO_ROOT=WILLEQ_REPO_PATH(\"\")")
    GTEST_DISCOVER_TESTS(test_scene_ambient_recompute DISCOVERY_TIMEOUT 120)
ENDIF()

# T391 (B202): EntityVisual::lightNode ownership -- the render-thread
# use-after-free a second player in the zone triggered. Drives the real
# SimBridgeComponent::applySimulationOutput() cull/un-cull seam over a real
# EDT_NULL scene graph; the entity half skips without the shipped db or the
# client tree, the same gate as test_entity_size_change.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_entity_light_node_lifetime
        test_entity_light_node_lifetime.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_entity_light_node_lifetime PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_entity_light_node_lifetime GTest::gtest_main willeq_core)
    TARGET_COMPILE_DEFINITIONS(test_entity_light_node_lifetime PRIVATE
        "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
    GTEST_DISCOVER_TESTS(test_entity_light_node_lifetime DISCOVERY_TIMEOUT 120)
ENDIF()
