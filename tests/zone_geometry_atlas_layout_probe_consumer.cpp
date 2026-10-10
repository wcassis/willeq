// B950 (GitLab #268): the PLAIN-CONSUMER half of the ZoneMeshBuilder layout
// probe.
//
// This TU has NO explicit define of its own: it sees zone_geometry.h in the
// state a bare consumer of willeq_core's public interface sees it. Before
// the fix that means WITHOUT EQT_HAS_TEXTURE_ATLAS (the define was PRIVATE
// to willeq_core), so this half is 16 bytes smaller than the
// with-probe and the test is RED -- the same smaller object that
// test_placed_object_meshes.cpp constructs and that B944's own test hit the
// ASan stack-buffer-overflow on. After the fix the define is PUBLIC on
// willeq_core and flows into this TU through the link, so both halves agree
// and the test is GREEN.
//
// The self-report says which macro state this TU was actually compiled in,
// so a red run shows the mechanism, not just the byte count.

#include "client/graphics/eq/zone_geometry.h"

namespace {

constexpr bool atlasMacroWasDefinedHere =
#ifdef EQT_HAS_TEXTURE_ATLAS
    true;
#else
    false;
#endif

}  // namespace

namespace EQT {
namespace Graphics {
namespace b950_probe {

size_t zoneMeshBuilderSizeAsPlainConsumer() { return sizeof(ZoneMeshBuilder); }
bool plainConsumerSawTheAtlasMacro() { return atlasMacroWasDefinedHere; }

}  // namespace b950_probe
}  // namespace Graphics
}  // namespace EQT
