#ifndef EQT_GRAPHICS_GEOMETRY_COMBINER_H
#define EQT_GRAPHICS_GEOMETRY_COMBINER_H

#include "wld_loader.h"  // For ZoneGeometry, Vertex3D, Triangle, VertexPiece
#include "s3d_loader.h"  // For CharacterPart
#include <memory>
#include <vector>

namespace EQT {
namespace Graphics {

// Combine character model parts into a single geometry (basic combination)
std::shared_ptr<ZoneGeometry> combineCharacterParts(
    const std::vector<std::shared_ptr<ZoneGeometry>>& parts);

// Combine character model parts with bone transforms applied (for static rendering)
std::shared_ptr<ZoneGeometry> combineCharacterPartsWithTransforms(
    const std::vector<CharacterPart>& parts);

// Combine character model parts WITHOUT transforms (raw for animation)
std::shared_ptr<ZoneGeometry> combineCharacterPartsRaw(
    const std::vector<CharacterPart>& parts);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_GEOMETRY_COMBINER_H
