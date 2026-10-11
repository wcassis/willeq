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
// This selects the parts the skeleton actually REFERENCES instead, with the
// race-code names as the fallback for skeletons that reference none. When the
// skeleton's 0x200 mesh list (SkeletonTrack::meshRefs, T476) is non-empty the
// skinned selection is every stored part whose geometry the loader stored for
// that skeleton (the loader stores exactly the geometries the skeleton
// references: the bone-attached meshes plus the 0x200-listed ones), so the
// selection is the whole stored set -- no name matching. When the list is
// empty the old name selection runs verbatim, so every skeleton that lists
// none draws exactly what it drew before. The raw (unskinned, animation)
// selection mirrors the skinned one.
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
