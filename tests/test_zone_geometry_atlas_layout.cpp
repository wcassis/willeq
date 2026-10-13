// B950 (GitLab #268) + B1019 (GitLab #340): a plain consumer and willeq_core
// must see the SAME ZoneMeshBuilder layout.
//
// willeq_core compiles zone_geometry.cpp with EQT_HAS_TEXTURE_ATLAS (PUBLIC
// on the target since B950, CMakeLists.txt). That define USED to matter:
// the class's member layout in include/client/graphics/eq/zone_geometry.h
// depended on it (the two #ifdef data-member blocks -- the
// shaderMaterialAtlas* members -- changed sizeof, 8 bytes total on x86-64),
// so a TU that linked willeq_core WITHOUT the define saw an 8-byte-smaller
// class while calling the core's constructor, which wrote the two atlas
// members: the ASan stack-buffer-overflow B944 measured in
// ZoneMeshBuilder::ZoneMeshBuilder, and the same violation by construction in
// test_placed_object_meshes.cpp (B950's second half). B494 (this MR) deleted
// the guarded members, so zone_geometry.h now has zero #ifdef blocks and the
// layout no longer depends on the define; the define and this net are kept
// but vacuous (retirement is a FOLLOW-UP) and still assert both sides agree.
//
// The net, as B1019 tightened it: the CORE side is measured INSIDE
// willeq_core. ZoneMeshBuilder::layoutSize() (zone_geometry.h, defined in
// zone_geometry.cpp -- the TU willeq_core compiles) reports the size the
// core was built for. This target compiles ONE test TU with NO explicit
// define and links willeq_core, replacing the old shape -- a sibling "core"
// probe TU carrying a hand-placed per-source COMPILE_DEFINITIONS -- which
// pinned the macro state of THIS test target, not of willeq_core: willeq_core
// could have dropped the define and stayed self-consistent while the net
// went falsely RED. A per-target PRIVATE define, re-applied only to whatever
// test a unit remembers about, leaves consumers behind -- exactly what this
// fails on. (No target links willeq_core and compiles zone_geometry.cpp
// itself, so the core's layoutSize() definition cannot collide with a local
// copy.)
//
// No device, no zone files: sizeof needs nothing but the header.
#include <gtest/gtest.h>

#include <cstddef>

#include "client/graphics/eq/zone_geometry.h"

namespace {

// The state THIS TU (a bare consumer of the public header, no explicit
// define) was compiled in -- the self-report a red run prints.
constexpr bool thisTuSawTheAtlasMacro =
#ifdef EQT_HAS_TEXTURE_ATLAS
    true;
#else
    false;
#endif

}  // namespace

TEST(ZoneGeometryAtlasLayout, ConsumerAndCoreSeeTheSameLayout) {
    const size_t asConsumer = sizeof(EQT::Graphics::ZoneMeshBuilder);
    const size_t asCore = EQT::Graphics::ZoneMeshBuilder::layoutSize();
    EXPECT_EQ(asConsumer, asCore)
        << "ZoneMeshBuilder's layout depends on EQT_HAS_TEXTURE_ATLAS and the "
           "consumer disagrees with the size willeq_core was built for "
           "(consumer="
        << asConsumer << " core=" << asCore << "): a TU that links willeq_core "
           "without the define constructs an object different from the one the "
           "core's constructor writes (B944's ASan stack-buffer-overflow). "
           "The define must be PUBLIC on willeq_core so every consumer of the "
           "header sees the same class.";
    // And the consumer state this TU itself (a plain consumer of the public
    // header, no explicit define) sees must be one of the two agreeing
    // states -- the point of the fix, stated for the reader of the log.
    EXPECT_TRUE(thisTuSawTheAtlasMacro)
        << "a bare consumer of zone_geometry.h still does NOT see "
           "EQT_HAS_TEXTURE_ATLAS: the define is not PUBLIC on willeq_core "
           "(pre-B950 shape). Every TU that links willeq_core and constructs "
           "a ZoneMeshBuilder (test_placed_object_meshes.cpp:91, and B944's "
           "test_texture_eviction_residency) constructs the smaller object "
           "against the core's constructor. (test_graphics does not link "
           "willeq_core -- tests/cmake/graphics.cmake:97 -- and "
           "test_inventory_model_view never allocates a builder: its "
           "RaceModelLoader's is allocated in the core, "
           "race_model_loader.cpp:36.)";
}
