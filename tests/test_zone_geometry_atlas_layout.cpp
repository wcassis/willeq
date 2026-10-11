// B950 (GitLab #268): a test TU and willeq_core must see the SAME
// ZoneMeshBuilder layout.
//
// willeq_core compiles zone_geometry.cpp with EQT_HAS_TEXTURE_ATLAS as a
// PRIVATE target definition, and the class's member layout in
// include/client/graphics/eq/zone_geometry.h depends on it (four #ifdef
// blocks; only the two data-member blocks -- the shaderMaterialAtlas*
// members -- change sizeof, the forward declaration and the inline-method
// block do not): a TU that links willeq_core WITHOUT the
// define then sees an 8-byte-smaller class while calling the core's
// constructor, which writes the two atlas members: the ASan
// stack-buffer-overflow B944 measured in ZoneMeshBuilder::ZoneMeshBuilder,
// and the same violation by construction in test_placed_object_meshes.cpp
// (this issue's second half).
//
// The net: two probe TUs in this target. The CONSUMER half has no explicit
// define -- it sees the public header in the state a bare willeq_core
// consumer does (without the macro before the fix, with it after the define
// becomes PUBLIC). The CORE half carries the macro explicitly, the state
// willeq_core's own sources are in. Each reports its sizeof(ZoneMeshBuilder)
// and the two must agree. A per-target PRIVATE define, re-applied only to
// whatever test a unit remembers about, leaves consumers behind -- exactly
// what this fails on.
//
// No device, no zone files: sizeof needs nothing but the header.

#include <gtest/gtest.h>

#include "client/graphics/eq/zone_geometry.h"

// The two probe TUs compiled into this target (see renderer_guards.cmake).
namespace EQT {
namespace Graphics {
namespace b950_probe {
size_t zoneMeshBuilderSizeAsPlainConsumer();  // no explicit define
bool plainConsumerSawTheAtlasMacro();         // its self-report
size_t zoneMeshBuilderSizeAsCore();           // explicit -D
}  // namespace b950_probe
}  // namespace Graphics
}  // namespace EQT

namespace {
using EQT::Graphics::b950_probe::plainConsumerSawTheAtlasMacro;
using EQT::Graphics::b950_probe::zoneMeshBuilderSizeAsCore;
using EQT::Graphics::b950_probe::zoneMeshBuilderSizeAsPlainConsumer;
}  // namespace

TEST(ZoneGeometryAtlasLayout, ConsumerAndCoreSeeTheSameLayout) {
    const size_t asPlainConsumer = zoneMeshBuilderSizeAsPlainConsumer();
    const size_t asCore = zoneMeshBuilderSizeAsCore();
    EXPECT_EQ(asPlainConsumer, asCore)
        << "ZoneMeshBuilder's layout depends on EQT_HAS_TEXTURE_ATLAS and the two "
           "probe TUs disagree (plain consumer="
        << asPlainConsumer << " core=" << asCore << "): a TU that links "
           "willeq_core without the define constructs an object smaller than "
           "the core's constructor writes (B944's ASan stack-buffer-overflow). "
           "The define must be PUBLIC on willeq_core so every consumer of the "
           "header sees the same class.";
    // And the consumer state this TU itself (a plain consumer of the public
    // header, no explicit define) sees must be one of the two agreeing
    // states -- the point of the fix, stated for the reader of the log.
    const bool consumerSawMacro = plainConsumerSawTheAtlasMacro();
    EXPECT_TRUE(consumerSawMacro)
        << "a bare consumer of zone_geometry.h still does NOT see "
           "EQT_HAS_TEXTURE_ATLAS: the define is not PUBLIC on willeq_core "
           "(pre-B950 shape). Every TU that links willeq_core and constructs "
           "a ZoneMeshBuilder (test_placed_object_meshes, test_zone_mesh_"
           "capacity in test_graphics, test_inventory_model_view, ...) "
           "constructs the smaller object against the core's constructor.";
}
