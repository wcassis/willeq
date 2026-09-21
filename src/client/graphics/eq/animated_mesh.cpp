// EQAnimatedMesh -- the IAnimatedMesh holding the skeleton and base mesh.
//
// T451: split out of the 1,287-line
// src/client/graphics/eq/animated_mesh_scene_node.cpp. Bodies are contiguous
// line ranges of that file, moved verbatim.

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "common/simd_detect.h"
#include "common/logging.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>

#include "client/graphics/eq/animated_mesh_skinning.h"

namespace EQT {
namespace Graphics {

EQAnimatedMesh::EQAnimatedMesh()
    : baseMesh_(nullptr)
    , animatedMesh_(nullptr)
    , lastAppliedFrame_(-1) {
    boundingBox_.reset(0, 0, 0);
}

EQAnimatedMesh::~EQAnimatedMesh() {
    if (animatedMesh_) {
        animatedMesh_->drop();
    }
    // Don't drop baseMesh_ - we don't own it
}

irr::u32 EQAnimatedMesh::getFrameCount() const {
    auto skeleton = animator_.getSkeleton();
    if (!skeleton || skeleton->animations.empty()) {
        return 1;
    }

    // Return max frame count across all animations
    irr::u32 maxFrames = 1;
    for (const auto& [code, anim] : skeleton->animations) {
        if (anim && anim->frameCount > static_cast<int>(maxFrames)) {
            maxFrames = static_cast<irr::u32>(anim->frameCount);
        }
    }
    return maxFrames;
}

irr::f32 EQAnimatedMesh::getAnimationSpeed() const {
    return animator_.getPlaybackSpeed();
}

void EQAnimatedMesh::setAnimationSpeed(irr::f32 fps) {
    // Convert fps to playback speed (assuming 10 fps as base)
    animator_.setPlaybackSpeed(fps / 10.0f);
}

irr::scene::IMesh* EQAnimatedMesh::getMesh(irr::s32 frame, irr::s32 detailLevel,
                                            irr::s32 startFrameLoop, irr::s32 endFrameLoop) {
    // Apply animation if frame changed
    if (frame != lastAppliedFrame_) {
        applyAnimation();
        lastAppliedFrame_ = frame;
    }
    return animatedMesh_;
}

irr::u32 EQAnimatedMesh::getMeshBufferCount() const {
    return animatedMesh_ ? animatedMesh_->getMeshBufferCount() : 0;
}

irr::scene::IMeshBuffer* EQAnimatedMesh::getMeshBuffer(irr::u32 nr) const {
    return animatedMesh_ ? animatedMesh_->getMeshBuffer(nr) : nullptr;
}

irr::scene::IMeshBuffer* EQAnimatedMesh::getMeshBuffer(const irr::video::SMaterial& material) const {
    return animatedMesh_ ? animatedMesh_->getMeshBuffer(material) : nullptr;
}

const irr::core::aabbox3d<irr::f32>& EQAnimatedMesh::getBoundingBox() const {
    return boundingBox_;
}

void EQAnimatedMesh::setBoundingBox(const irr::core::aabbox3df& box) {
    boundingBox_ = box;
}

void EQAnimatedMesh::setMaterialFlag(irr::video::E_MATERIAL_FLAG flag, bool newvalue) {
    if (animatedMesh_) {
        animatedMesh_->setMaterialFlag(flag, newvalue);
    }
}

void EQAnimatedMesh::setHardwareMappingHint(irr::scene::E_HARDWARE_MAPPING newMappingHint,
                                             irr::scene::E_BUFFER_TYPE buffer) {
    if (animatedMesh_) {
        animatedMesh_->setHardwareMappingHint(newMappingHint, buffer);
    }
}

void EQAnimatedMesh::setDirty(irr::scene::E_BUFFER_TYPE buffer) {
    if (animatedMesh_) {
        animatedMesh_->setDirty(buffer);
    }
}

void EQAnimatedMesh::setBaseMesh(irr::scene::IMesh* mesh) {
    baseMesh_ = mesh;
    copyBaseMesh();
}

void EQAnimatedMesh::setSkeleton(std::shared_ptr<CharacterSkeleton> skeleton) {
    animator_.setSkeleton(skeleton);
}

void EQAnimatedMesh::setVertexPieces(const std::vector<VertexPiece>& pieces) {
    vertexPieces_ = pieces;
}

void EQAnimatedMesh::setOriginalVertices(const std::vector<irr::video::S3DVertex>& verts) {
    originalVertices_ = verts;
}

void EQAnimatedMesh::setVertexMapping(const std::vector<VertexMappingList>& mapping) {
    vertexMapping_.assign(mapping);  // T504: stored flat
}

void EQAnimatedMesh::copyBaseMesh() {
    if (animatedMesh_) {
        animatedMesh_->drop();
        animatedMesh_ = nullptr;
    }

    if (!baseMesh_) {
        return;
    }

    // Create a new mesh by copying all mesh buffers
    animatedMesh_ = new irr::scene::SMesh();

    for (irr::u32 i = 0; i < baseMesh_->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* srcBuffer = baseMesh_->getMeshBuffer(i);
        if (!srcBuffer) continue;

        // Create a new mesh buffer with copied data
        irr::scene::SMeshBuffer* dstBuffer = new irr::scene::SMeshBuffer();

        // Copy material
        dstBuffer->Material = srcBuffer->getMaterial();

        // Copy vertices (assuming S3DVertex format)
        irr::video::S3DVertex* srcVerts = static_cast<irr::video::S3DVertex*>(srcBuffer->getVertices());
        irr::u32 vertCount = srcBuffer->getVertexCount();

        dstBuffer->Vertices.reallocate(vertCount);
        for (irr::u32 v = 0; v < vertCount; ++v) {
            dstBuffer->Vertices.push_back(srcVerts[v]);
        }

        // Copy indices
        irr::u16* srcIndices = srcBuffer->getIndices();
        irr::u32 indexCount = srcBuffer->getIndexCount();

        dstBuffer->Indices.reallocate(indexCount);
        for (irr::u32 idx = 0; idx < indexCount; ++idx) {
            dstBuffer->Indices.push_back(srcIndices[idx]);
        }

        dstBuffer->recalculateBoundingBox();
        animatedMesh_->addMeshBuffer(dstBuffer);
        dstBuffer->drop();
    }

    animatedMesh_->recalculateBoundingBox();
    boundingBox_ = animatedMesh_->getBoundingBox();
    lastAppliedFrame_ = -1;
}

void EQAnimatedMesh::applyAnimation() {
    static bool debugOnce = true;

    if (!animatedMesh_ || vertexPieces_.empty()) {
        LOG_DEBUG_IF(MOD_GRAPHICS, debugOnce, "EQAnimatedMesh::applyAnimation - early exit: animatedMesh={} vertexPieces={}",
                     (animatedMesh_ ? "yes" : "no"), vertexPieces_.size());
        return;
    }

    const auto& boneStates = animator_.getBoneStates();
    if (boneStates.empty()) {
        LOG_DEBUG_IF(MOD_GRAPHICS, debugOnce, "EQAnimatedMesh::applyAnimation - no bone states");
        return;
    }

    // Check if we're using multi-buffer mode (with vertex mapping) or single-buffer mode
    bool useMultiBuffer = !originalVertices_.empty() && !vertexMapping_.empty();

    if (debugOnce) {
        LOG_DEBUG(MOD_GRAPHICS, "EQAnimatedMesh::applyAnimation - processing {} mesh buffers, {} vertex pieces, {} bone states multiBuffer={}",
                  animatedMesh_->getMeshBufferCount(), vertexPieces_.size(), boneStates.size(), (useMultiBuffer ? "yes" : "no"));
        debugOnce = false;
    }

    // Use shared helper - no extra rotation for EQAnimatedMesh (shared mesh, not player-specific)
    applyBoneTransformsToMeshImpl(
        animatedMesh_,
        boneStates,
        vertexPieces_,
        originalVertices_,
        vertexMapping_,
        baseMesh_,
        1.0f,  // extraRotCos = 1.0 (no rotation)
        0.0f   // extraRotSin = 0.0 (no rotation)
    );

    boundingBox_ = animatedMesh_->getBoundingBox();
}

void EQAnimatedMesh::updateBoundingBox() {
    if (animatedMesh_) {
        animatedMesh_->recalculateBoundingBox();
        boundingBox_ = animatedMesh_->getBoundingBox();
    }
}

// ============================================================================
// EQAnimatedMeshSceneNode Implementation
// ============================================================================


} // namespace Graphics
} // namespace EQT
