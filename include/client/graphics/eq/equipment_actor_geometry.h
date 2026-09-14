#pragma once

// T472 (board B588): the geometry an equipment actor (IT###) DRAWS, resolved
// through the WLD reference chain rather than guessed from mesh names.
//
// Before this, EquipmentModelLoader kept a mesh only if its NAME contained
// "IT###_". That holds for most items (IT1 draws IT1_DMSPRITEDEF) and fails
// for every item whose actor is hierarchical: IT48 -- the Gloomingdeep
// Lantern -- is
//
//   IT48_ACTORDEF (0x14) -> 0x11 skeleton ref 4599 -> IT48_HS_DEF (0x10)
//       IT48_DAG
//         LANTERN01_DAG  (rest rotation + shift) -> 0x2D 1541 -> LANTERN01_DMSPRITEDEF
//
// and no mesh in that chain is called IT48_*. So the lantern was never indexed,
// and every attach path skipped it ("not found in item_models.json"). T472's
// census over the seventeen *equip*.s3d archives found 19 such actors
// (tests/test_held_item_attachment.cpp pins the count at zero).
//
// The resolved geometry is in the ITEM's frame, EQ coordinates (Z up):
//   - each 0x36 mesh has its CENTRE added (its vertices are stored relative to
//     it, wld_fragments_mesh.cpp). The centre is what puts an item's origin in
//     its grip: IT1 (a sword) spans x -1.37..1.37 about its centre and
//     -0.51..2.24 about its origin, pommel at -0.51 and crossguard at +0.29;
//   - a mesh hung from a skeleton bone is moved by that bone's accumulated REST
//     transform (T * R * S, the order SkeletalAnimator uses).
// When the chain yields nothing, the pre-T472 name-prefix match is used, so no
// item that drew before draws less now.

#include <memory>
#include <string>

namespace EQT {
namespace Graphics {

class WldLoader;
struct ZoneGeometry;

// The combined geometry actor `actorName` (e.g. "IT48" -- WldLoader strips
// _ACTORDEF) draws, or nullptr when neither the reference chain nor the name
// match finds a mesh. Texture indices in the result refer to its own
// textureNames(); the masked / invisible / animation flags travel with them.
std::shared_ptr<ZoneGeometry> resolveEquipmentActorGeometry(const WldLoader& wld,
                                                            const std::string& actorName);

}  // namespace Graphics
}  // namespace EQT
