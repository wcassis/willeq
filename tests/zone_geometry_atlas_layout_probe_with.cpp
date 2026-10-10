// B950 (GitLab #268): the WILLEQ_CORE half of the ZoneMeshBuilder layout
// probe.
//
// Compiled WITH EQT_HAS_TEXTURE_ATLAS -- the exact macro state willeq_core's
// own sources are in, because the library compiles zone_geometry.cpp and
// zone_geometry_atlased.cpp that way (the constructor and
// buildAtlasedMesh were generated against this layout). The explicit
// per-source define is belt-and-braces: before the fix it is the only
// source of the macro for this TU, and after the fix it merely agrees with
// the PUBLIC define the link carries in, so the half is stable in both
// shapes.
//
// zone_geometry_atlased.cpp's buildAtlasedMesh takes a const TextureAtlas&,
// so the header's #ifdef forward-declares it; this TU includes the complete
// header, mirroring willeq_core's own state.

#include "client/graphics/eq/zone_geometry.h"
#ifdef EQT_HAS_TEXTURE_ATLAS
#include "client/graphics/texture_atlas.h"
#endif

namespace EQT {
namespace Graphics {
namespace b950_probe {

size_t zoneMeshBuilderSizeAsCore() { return sizeof(ZoneMeshBuilder); }

}  // namespace b950_probe
}  // namespace Graphics
}  // namespace EQT
