# tests/cmake/graphics.cmake -- included from tests/CMakeLists.txt.
# Renderer, hotkey manager and asset-loading tests.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Test constrained renderer configuration (presets, resolution calculation, clamping)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_constrained_renderer
        test_constrained_renderer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_LINK_LIBRARIES(test_constrained_renderer GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_constrained_renderer DISCOVERY_TIMEOUT 120)
ENDIF()

# Test graphics components (PFS, DDS decoder, etc.)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_graphics
        test_graphics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_source_residency.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/era_model_sources.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading_chr.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading_search.cpp
        # T500: model_loading_search.cpp reads eqt::memory::stats() for the
        # [modelmem] split line, so the ledger TU comes with it.
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_trace.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/gpu_buffer_census.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading_animation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading_preload.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry_atlased.cpp
        # T490: BspRegion/BspTree PVS methods moved into zone_bsp.cpp (RLE store).
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_objects.cpp
        # B734: loadObjects() poses skinned objects through T476's resolver.
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_actor_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_characters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_skinning.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_skinning.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_bones.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_upload_queue.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_eviction.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_compressed.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_residency.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/archive_texture_source.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_graphics PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    # B904: built meshes hold exactly their data (ZoneMeshBuilder capacity).
    TARGET_SOURCES(test_graphics PRIVATE test_zone_mesh_capacity.cpp)
    TARGET_INCLUDE_DIRECTORIES(test_graphics PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # T270: the race-code tests read the shipped race_models rows instead of
    # restating a hardcoded numbering (they skip if the db is absent).
    TARGET_COMPILE_DEFINITIONS(test_graphics PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_graphics GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES} dl)
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(test_graphics EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment in the top-level CMakeLists.txt (same P25/P26
        # parity gap: CIrrDeviceFB.cpp's windowed-EGL backend references X11
        # symbols the static archive needs resolved at link time).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_graphics X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — the
    # mandatory-Irrlicht gate in the top-level CMakeLists.txt guarantees
    # EQT_DRM OR EQT_GLES2 whenever IRRLICHT_LIBRARY is set
    # (refactor/desktop_gl_elimination_plan.md §6 Phase 3).
    GTEST_DISCOVER_TESTS(test_graphics DISCOVERY_TIMEOUT 120)

    # Test HotkeyManager (requires Irrlicht for key codes)
    ADD_EXECUTABLE(test_hotkey_manager
        test_hotkey_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/input/hotkey_manager.cpp
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotkey_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotkey_manager GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_hotkey_manager DISCOVERY_TIMEOUT 120)
ENDIF()

# Test asset loading (equipment, race models, textures, animations, lights)
# These tests verify EQ assets load without errors
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_asset_loading
        test_asset_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        # T490: BspRegion/BspTree PVS methods moved into zone_bsp.cpp (RLE store).
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_objects.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_characters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_skinning.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry_atlased.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_source_residency.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/era_model_sources.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading_chr.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading_search.cpp
        # T500: model_loading_search.cpp reads eqt::memory::stats() for the
        # [modelmem] split line, so the ledger TU comes with it.
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_trace.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/gpu_buffer_census.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading_animation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading_preload.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_actor_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_skinning.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_bones.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_upload_queue.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_eviction.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_compressed.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_residency.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/archive_texture_source.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_asset_loading PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(test_asset_loading PRIVATE ${IRRLICHT_INCLUDE_DIR})
    # T270: VerifyRaceCodeMappings compares against the shipped race_models
    # rows rather than a restated pre-JSON table.
    TARGET_COMPILE_DEFINITIONS(test_asset_loading PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_asset_loading GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES} dl)
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(test_asset_loading EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment in the top-level CMakeLists.txt (same P25/P26
        # parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_asset_loading X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # test_graphics comment above (same mandatory-Irrlicht gate).
    GTEST_DISCOVER_TESTS(test_asset_loading DISCOVERY_TIMEOUT 120)
ENDIF()

# B654 (issue #56): display-less regression test for the X11-keysym -> EKEY_CODE
# map. It compiles the pure header docker/irrlicht-drm-x86/x11_keysym_map.h
# (the same header the X11 device TU includes) and pins the numpad-digit AND
# numpad-navigation-alias decisions directly -- no Display*, no X11 connection,
# no graphics driver. Needs only gtest + <Keycodes.h> (IRRLICHT_INCLUDE_DIR) +
# the system X11 headers, so it links no driver and runs in the normal suite.
# RED before the fix (the ten nav-alias cases returned 0), GREEN after.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_x11_keysym_map
        test_x11_keysym_map.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_x11_keysym_map PRIVATE
        ${IRRLICHT_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/docker/irrlicht-drm-x86)
    TARGET_LINK_LIBRARIES(test_x11_keysym_map GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_x11_keysym_map DISCOVERY_TIMEOUT 120)
ENDIF()

# B654 (issue #56): the Xvfb end-to-end regression. Drives the REAL
# CIrrDeviceFB::run() -> pollX11Events() -> x11KeysymToIrrlicht() ->
# HotkeyManager::getAction() chain: a private Xvfb, a real CIrrDeviceFB created
# via createDeviceEx (EDT_OGLES2 + the WILLEQ_EGL_X11 opt-in), a real xdotool
# KP_7/KP_1 press, and the resulting EKEY_CODE resolved through the shipped
# HotkeyManager so the Numpad7/Numpad1 camera actions fire. Links the patched
# Irrlicht (for CIrrDeviceFB / createDeviceEx) + its device deps (EGL/GLESv2/
# drm/gbm/X11, the same set test_graphics links for the windowed-EGL backend)
# plus the HotkeyManager TU. SKIPs loudly only when Xvfb/xdotool are genuinely
# absent; any device-path failure is a hard FAIL. RED before the fix, GREEN after.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_x11_numpad_e2e
        test_x11_numpad_e2e.cpp
        ${CMAKE_SOURCE_DIR}/src/client/input/hotkey_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_x11_numpad_e2e PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_x11_numpad_e2e
        GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES} ${IRRLICHT_LIBRARY})
    IF(EQT_DRM OR EQT_GLES2)
        TARGET_LINK_LIBRARIES(test_x11_numpad_e2e EGL GLESv2 drm gbm)
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_x11_numpad_e2e X11)
        ENDIF()
    ENDIF()
    GTEST_DISCOVER_TESTS(test_x11_numpad_e2e DISCOVERY_TIMEOUT 120)
ENDIF()

# T400 (board B225): the particle atlas must carry its alpha.
#
# Three things are pinned here and they only mean something together:
# the shipped data/textures/particle_atlas.bmp is 32 bpp and its alpha
# matches generateParticleAtlas() texel for texel; the willeq.eqe1 entry
# carries that alpha IN-BAND (RGBA8888) rather than in an ETC1 `_alpha`
# companion no particle shader samples; and ParticleManager's material is
# alpha-BLEND, which is what makes straight (non-premultiplied) RGB the
# right thing to store. Change the blend mode and the file has to change
# with it -- hence one test binary, not three.
#
# texture_generators.cpp is compiled in directly: willeq_core does not
# contain it (it had no CMake consumer at all until particle_atlas_builder).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_particle_atlas_alpha
        test_particle_atlas_alpha.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_generators.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_particle_atlas_alpha PRIVATE
        WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    TARGET_INCLUDE_DIRECTORIES(test_particle_atlas_alpha PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_particle_atlas_alpha GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_particle_atlas_alpha DISCOVERY_TIMEOUT 120)
ENDIF()

# T417 (board B300): the shipped spell sprite atlas versus the official
# client's own SpellEffects/*.tga, cell for cell. Files only -- no device, no
# GPU, no willeq_core; the TGA decode is the builder's own header-only reader,
# so the expected pixels are re-derived from the source rather than from a
# golden nobody can re-check.
ADD_EXECUTABLE(test_spell_sprite_atlas
    test_spell_sprite_atlas.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spell_sprite_atlas PRIVATE
    WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
TARGET_INCLUDE_DIRECTORIES(test_spell_sprite_atlas PRIVATE
    ${CMAKE_SOURCE_DIR}/tools
    ${CMAKE_SOURCE_DIR}/include)
TARGET_LINK_LIBRARIES(test_spell_sprite_atlas GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_spell_sprite_atlas DISCOVERY_TIMEOUT 120)

# T430 (board B376): the shipped item icon sheets versus the official client's
# own uifiles/default/dragitem*.tga, cell for cell, plus the column-major
# id->cell mapping EQUI_Animations.xml declares. Files only -- no device, no
# GPU, no willeq_core; same shape as test_spell_sprite_atlas above.
ADD_EXECUTABLE(test_item_icon_atlas
    test_item_icon_atlas.cpp
)
TARGET_COMPILE_DEFINITIONS(test_item_icon_atlas PRIVATE
    WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
TARGET_INCLUDE_DIRECTORIES(test_item_icon_atlas PRIVATE
    ${CMAKE_SOURCE_DIR}/tools
    ${CMAKE_SOURCE_DIR}/include)
TARGET_LINK_LIBRARIES(test_item_icon_atlas GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_item_icon_atlas DISCOVERY_TIMEOUT 120)

# T406 (board B266): the census of every shipped BMP's alpha, its consumer's
# blend, and the willeq.eqe1 payload that carries it. Reads files only (the
# BMPs, the pack, and the consumer sources) -- no device, no GPU. Links
# willeq_core for EQE1Loader.
ADD_EXECUTABLE(test_texture_alpha_census
    test_texture_alpha_census.cpp
)
TARGET_COMPILE_DEFINITIONS(test_texture_alpha_census PRIVATE
    WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
TARGET_LINK_LIBRARIES(test_texture_alpha_census GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_texture_alpha_census DISCOVERY_TIMEOUT 120)

# T445 (board B336): the configured detail view distance drives the chunk cull
# radius. Pins the arithmetic (detailChunkRadius), the DetailManager wiring
# (setZoneConfig is the one place the radius is derived) and the shipped
# config's own numbers. No device: DetailManager is constructed with null
# scene manager and driver, which touches neither. Links willeq_core for
# DetailManager and DetailConfigLoader. Graphics builds only (B1038): every
# case is DetailManager's, and detail_types.h includes irrlicht.h.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_detail_view_distance
        test_detail_view_distance.cpp
    )
    TARGET_LINK_LIBRARIES(test_detail_view_distance GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_detail_view_distance DISCOVERY_TIMEOUT 120)
ENDIF()

# B967/B968 (GitLab #285/#286): the per-pixel player light. The LUT and the
# FP16 headroom of the VS-formed light vector, GL-free (header-only, no
# willeq_core) ...
ADD_EXECUTABLE(test_player_light_lut
    test_player_light_lut.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_player_light_lut PRIVATE ${CMAKE_SOURCE_DIR}/include)
TARGET_LINK_LIBRARIES(test_player_light_lut GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_player_light_lut DISCOVERY_TIMEOUT 120)
# ... and the rendered curve of the SHIPPED GLSL on llvmpipe (surfaceless EGL):
# monotonic, zero past the light's range, the per-vertex family's shape, the
# same at the world origin and the iceclad f2 pose, and no garbage on a quad
# reaching 40,000 units (gles2_player_light_probe --assert).
IF(TARGET gles2_player_light_probe)
    ADD_TEST(NAME PlayerLightProbeMonotonic COMMAND gles2_player_light_probe --assert)
ENDIF()
