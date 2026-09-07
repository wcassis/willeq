#pragma once

// T451: the CPU skinning kernel behind BOTH EQAnimatedMesh::applyAnimation()
// and EQAnimatedMeshSceneNode::applyAnimation(). It was a file-static in the
// 1,287-line animated_mesh_scene_node.cpp; the split put its two callers in
// different TUs, so the declaration is stated once, here. The body -- and the
// SIMD normal helper it uses, which stays file-local -- is in
// src/client/graphics/eq/animated_mesh_skinning.cpp.

#include "client/graphics/eq/animated_mesh_scene_node.h"

namespace EQT {
namespace Graphics {

void applyBoneTransformsToMeshImpl(
    irr::scene::SMesh* destMesh,
    const std::vector<AnimatedBoneState>& boneStates,
    const std::vector<VertexPiece>& vertexPieces,
    const std::vector<irr::video::S3DVertex>& originalVertices,
    const std::vector<VertexMappingList>& vertexMapping,
    irr::scene::IMesh* baseMesh,
    float extraRotCos,
    float extraRotSin);

} // namespace Graphics
} // namespace EQT
