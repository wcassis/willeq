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
    )
    TARGET_LINK_LIBRARIES(test_constrained_renderer GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_constrained_renderer)
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
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_graphics PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
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
    GTEST_DISCOVER_TESTS(test_graphics)

    # Test HotkeyManager (requires Irrlicht for key codes)
    ADD_EXECUTABLE(test_hotkey_manager
        test_hotkey_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/input/hotkey_manager.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotkey_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotkey_manager GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_hotkey_manager)
ENDIF()

# Test asset loading (equipment, race models, textures, animations, lights)
# These tests verify EQ assets load without errors
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_asset_loading
        test_asset_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
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
    GTEST_DISCOVER_TESTS(test_asset_loading)
ENDIF()
