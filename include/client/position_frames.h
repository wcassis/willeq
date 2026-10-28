#ifndef EQT_CLIENT_POSITION_FRAMES_H
#define EQT_CLIENT_POSITION_FRAMES_H

// T311 -- THE TWO Z FRAMES, AND THE ONE PLACE THAT CONVERTS BETWEEN THEM.
//
// There are two different vertical quantities in this client and they are NOT
// the same number. Conflating them is the defect this whole arc has been
// unwinding; keep them apart.
//
//   WIRE frame   -- what an EverQuest client puts in OP_ClientUpdate's z_pos,
//                   and what every entity's z means in a spawn/position
//                   packet. MEASURED against the official Titanium client
//                   (T311 Leg A, live server, server-side hexdump of the
//                   C->S packet; the Titanium DECODE for this opcode is
//                   IN()-only, so the dump IS the transmitted value):
//
//                       wireZ = groundZ + 0.625f * size
//
//                   i.e. 0.2 * size * 3.125. Three sizes (3/6/12), two races,
//                   two ground heights. Size is exactly linear.
//
//                   There is NO race term. Under a Dwarf illusion the SERVER's
//                   own Mob::GetZOffset() moved (EQEmu's Dwarf constant is 0.7)
//                   while the client kept transmitting the 3.125 value. The
//                   agreement between this constant and Mob::GetZOffset() is a
//                   coincidence of EQEmu's DEFAULT -- do not reimplement
//                   GetZOffset() and do not add a per-race table here.
//
//                   (The official client also adds a +0.001..0.003 collision
//                   epsilon as it settles. That is push-out-of-the-floor noise,
//                   not part of the constant, and is deliberately NOT copied.)
//
//   RENDER frame -- where the scene node's ORIGIN sits. The EQ meshes are
//                   authored with their origin INSIDE the body, so putting the
//                   feet on the ground means
//
//                       nodeOriginZ = groundZ + collisionZOffset
//
//                   where collisionZOffset is |bbox.MinEdge.Y| * scale, MEASURED
//                   from the shipped mesh (T310 Leg B,
//                   EQT::Graphics::collisionZOffsetFromBounds). For a Human male
//                   it is 3.004.
//
// 0.625 * size (3.75 for a default humanoid) is NOT any landmark of the shipped
// mesh -- not the origin-to-feet distance (3.004), not half the bbox height
// (3.339), not the crown (3.674). So the two frames differ, for a Human male, by
// 0.746 -- exactly the 3.00-vs-3.75 gap T310 Leg C measured between fable and the
// official client.
//
// WHERE EACH FRAME LIVES IN THIS CODEBASE:
//   * The renderer (IrrlichtRenderer::playerZ_, EntityVisual node positions) is
//     the RENDER frame.
//   * GameState's player z, i.e. PosZ(), is ALSO the RENDER frame -- it is fed
//     straight from the renderer's PlayerPositionChanged intent and fed straight
//     back to IrrlichtRenderer::setPlayerPosition().
//   * Every PACKET -- inbound spawn/position for any entity, outbound
//     OP_ClientUpdate for us -- is the WIRE frame.
// So the conversion belongs at the packet edges, and only there. The two
// functions below are the only expression of it.

namespace eqt {

// The official Titanium client's z offset per unit of size, measured T311 Leg A.
inline constexpr float kWireZOffsetPerSize = 0.625f;

// The size used when NO size has been resolved yet. It is not the client's
// rule for "the spawn said none": the client resolves a size word that is not
// > 0 to the race/gender default and writes it back (eqgame 0x47fb30,
// EQT::Graphics::clientResolvedActorSize in client/graphics/actor_scale.h),
// and the z it transmits follows that default -- MEASURED at the qeynos2 spot
// (-74, 428), collision ground 0.00: T311 Leg A's Barbarian, OP_ZoneEntry
// size word 0.000, sends 4.376 = 0.625 x 7 (not x 6); the B1130/B1118
// oracle's dark-elf caster, size word 0.000, sends 3.128 = 0.625 x 5
// (refactor/B1172/README.md sec. 3). So every self path passes the resolved
// size (zone_entry.cpp, zone_dispatch.cpp, entities_component*.cpp); this 6.0
// is only PlayerState's value before the first OP_PlayerProfile.
inline constexpr float kDefaultEntitySize = 6.0f;

// The distance from collision ground up to the transmitted z, for an entity of
// this size. No race term -- see the note above.
inline constexpr float wireZOffsetForSize(float size) {
    return kWireZOffsetPerSize * (size > 0.0f ? size : kDefaultEntitySize);
}

// RENDER -> WIRE. `collisionZOffset` is the entity's own measured
// origin-to-feet distance.
//
// When collisionZOffset is not known yet (<= 0: no model loaded, or a mesh
// authored on its base) BOTH conversions are the IDENTITY. That is deliberate:
// with no measured mesh there is nothing to convert through, and the identity
// reproduces the pre-T311 behaviour ("the node origin is the server z") until
// the model arrives and the renderer's ground snap corrects it. Guessing an
// offset here would put the player at a made-up height.
inline constexpr float renderZToWireZ(float nodeOriginZ, float collisionZOffset, float size) {
    if (collisionZOffset <= 0.0f) return nodeOriginZ;
    return nodeOriginZ - collisionZOffset + wireZOffsetForSize(size);
}

// WIRE -> RENDER. The exact inverse of renderZToWireZ.
inline constexpr float wireZToRenderZ(float wireZ, float collisionZOffset, float size) {
    if (collisionZOffset <= 0.0f) return wireZ;
    return wireZ - wireZOffsetForSize(size) + collisionZOffset;
}

}  // namespace eqt

#endif  // EQT_CLIENT_POSITION_FRAMES_H
