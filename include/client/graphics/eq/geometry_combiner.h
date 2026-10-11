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

// B733 (issue #70): the base-model builders used to pick a character's drawn
// parts by NAME -- exactly "{CODE}_DMSPRITEDEF" and "{CODE}HE00_DMSPRITEDEF",
// falling back to all parts when neither matched. A part whose mesh carries a
// name the race code does not predict (race 146 SSN's head is
// "SNNHE00_DMSPRITEDEF": the "SNN" spelling differs from the race code "SSN")
// was never selected, so it was dropped even though the character's skeleton
// referenced it.
//
// This selects the UNION of that old name selection (run verbatim, so every
// character the old code drew keeps drawing exactly what it drew, variant
// meshes included) and the meshes the skeleton's 0x200 list (T476) names that
// the name selection missed (CharacterModel::skeletonReferenced, recorded by
// the loader). A skeleton whose list adds nothing has an empty tag and draws
// byte-identically to before; a skeleton whose list names a mesh the name
// selection cannot reach (the #70 head) draws it in addition.
//
// Returns the (selectedSkinned, selectedRaw) part vectors the caller hands to
// combineCharacterPartsWithTransforms / combineCharacterPartsRaw.
struct SelectedCharacterParts {
    std::vector<CharacterPart> skinned;
    std::vector<CharacterPart> raw;
};
SelectedCharacterParts selectCharacterParts(const CharacterModel& character,
                                            const std::string& upperRaceCode);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_GEOMETRY_COMBINER_H
