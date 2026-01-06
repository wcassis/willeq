#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "common/logging.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>

namespace EQT {
namespace Graphics {

// ============================================================================
// EQAnimatedMesh Implementation
// ============================================================================

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

void EQAnimatedMesh::setVertexMapping(const std::vector<VertexMapping>& mapping) {
    vertexMapping_ = mapping;
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

    if (useMultiBuffer) {
        // Multi-buffer mode: transform original vertices and copy to mapped buffer locations
        size_t totalOrigVerts = originalVertices_.size();

        // Apply bone transforms to vertices based on vertex pieces
        size_t vertexIdx = 0;
        for (const auto& piece : vertexPieces_) {
            int boneIdx = piece.boneIndex;

            // Validate bone index
            if (boneIdx < 0 || boneIdx >= static_cast<int>(boneStates.size())) {
                vertexIdx += piece.count;
                continue;
            }

            const BoneMat4& boneMat = boneStates[boneIdx].worldTransform;

            // Transform each vertex in this piece
            for (int i = 0; i < piece.count; ++i) {
                size_t vIdx = vertexIdx + i;
                if (vIdx >= totalOrigVerts || vIdx >= vertexMapping_.size()) break;

                // Get original vertex position and normal (in EQ Z-up coordinates)
                const auto& srcVert = originalVertices_[vIdx];
                float px = srcVert.Pos.X;
                float py = srcVert.Pos.Y;
                float pz = srcVert.Pos.Z;

                float nx = srcVert.Normal.X;
                float ny = srcVert.Normal.Y;
                float nz = srcVert.Normal.Z;

                // Transform position using bone matrix (in EQ coordinate space)
                boneMat.transformPoint(px, py, pz);

                // Transform normal (rotation only)
                float tnx = boneMat.m[0]*nx + boneMat.m[4]*ny + boneMat.m[8]*nz;
                float tny = boneMat.m[1]*nx + boneMat.m[5]*ny + boneMat.m[9]*nz;
                float tnz = boneMat.m[2]*nx + boneMat.m[6]*ny + boneMat.m[10]*nz;

                // Normalize
                float nlen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
                if (nlen > 0.0001f) {
                    tnx /= nlen;
                    tny /= nlen;
                    tnz /= nlen;
                }

                // Get the destination buffer and local index
                const auto& mapping = vertexMapping_[vIdx];
                if (mapping.bufferIndex >= animatedMesh_->getMeshBufferCount()) continue;

                irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
                    animatedMesh_->getMeshBuffer(mapping.bufferIndex));
                if (!dstBuffer || mapping.localIndex >= dstBuffer->Vertices.size()) continue;

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                dstBuffer->Vertices[mapping.localIndex].Pos.X = px;
                dstBuffer->Vertices[mapping.localIndex].Pos.Y = pz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[mapping.localIndex].Pos.Z = py;  // EQ Y -> Irrlicht Z
                dstBuffer->Vertices[mapping.localIndex].Normal.X = tnx;
                dstBuffer->Vertices[mapping.localIndex].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[mapping.localIndex].Normal.Z = tny;  // EQ Y -> Irrlicht Z
            }

            vertexIdx += piece.count;
        }

        // Mark all buffers as dirty and recalculate bounds
        for (irr::u32 b = 0; b < animatedMesh_->getMeshBufferCount(); ++b) {
            irr::scene::SMeshBuffer* buf = static_cast<irr::scene::SMeshBuffer*>(
                animatedMesh_->getMeshBuffer(b));
            if (buf) {
                buf->recalculateBoundingBox();
                buf->setDirty();
            }
        }
    } else {
        // Single-buffer mode (legacy): vertices are in order in buffer 0
        irr::scene::IMeshBuffer* srcBuffer = baseMesh_ ? baseMesh_->getMeshBuffer(0) : nullptr;
        irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
            animatedMesh_->getMeshBuffer(0));

        if (!srcBuffer || !dstBuffer) return;

        irr::video::S3DVertex* srcVerts = static_cast<irr::video::S3DVertex*>(srcBuffer->getVertices());
        irr::u32 vertCount = srcBuffer->getVertexCount();

        // Apply bone transforms to vertices based on vertex pieces
        size_t vertexIdx = 0;
        for (const auto& piece : vertexPieces_) {
            int boneIdx = piece.boneIndex;

            // Validate bone index
            if (boneIdx < 0 || boneIdx >= static_cast<int>(boneStates.size())) {
                vertexIdx += piece.count;
                continue;
            }

            const BoneMat4& boneMat = boneStates[boneIdx].worldTransform;

            // Transform each vertex in this piece
            for (int i = 0; i < piece.count; ++i) {
                size_t vIdx = vertexIdx + i;
                if (vIdx >= vertCount) break;

                // Get original vertex position and normal (in EQ Z-up coordinates)
                float px = srcVerts[vIdx].Pos.X;
                float py = srcVerts[vIdx].Pos.Y;
                float pz = srcVerts[vIdx].Pos.Z;

                float nx = srcVerts[vIdx].Normal.X;
                float ny = srcVerts[vIdx].Normal.Y;
                float nz = srcVerts[vIdx].Normal.Z;

                // Transform position using bone matrix (in EQ coordinate space)
                boneMat.transformPoint(px, py, pz);

                // Transform normal (rotation only)
                float tnx = boneMat.m[0]*nx + boneMat.m[4]*ny + boneMat.m[8]*nz;
                float tny = boneMat.m[1]*nx + boneMat.m[5]*ny + boneMat.m[9]*nz;
                float tnz = boneMat.m[2]*nx + boneMat.m[6]*ny + boneMat.m[10]*nz;

                // Normalize
                float nlen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
                if (nlen > 0.0001f) {
                    tnx /= nlen;
                    tny /= nlen;
                    tnz /= nlen;
                }

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                dstBuffer->Vertices[vIdx].Pos.X = px;
                dstBuffer->Vertices[vIdx].Pos.Y = pz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[vIdx].Pos.Z = py;  // EQ Y -> Irrlicht Z
                dstBuffer->Vertices[vIdx].Normal.X = tnx;
                dstBuffer->Vertices[vIdx].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[vIdx].Normal.Z = tny;  // EQ Y -> Irrlicht Z
            }

            vertexIdx += piece.count;
        }

        dstBuffer->recalculateBoundingBox();
        dstBuffer->setDirty();
    }

    animatedMesh_->recalculateBoundingBox();
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

EQAnimatedMeshSceneNode::EQAnimatedMeshSceneNode(
    EQAnimatedMesh* mesh,
    irr::scene::ISceneNode* parent,
    irr::scene::ISceneManager* mgr,
    irr::s32 id,
    const irr::core::vector3df& position,
    const irr::core::vector3df& rotation,
    const irr::core::vector3df& scale)
    : IAnimatedMeshSceneNode(parent, mgr, id, position, rotation, scale)
    , eqMesh_(mesh)
    , sceneManager_(mgr)
    , instanceMesh_(nullptr)
    , lastTimeMs_(0)
    , animationSpeed_(1.0f)
    , startFrame_(0)
    , endFrame_(0)
    , looping_(true)
    , readOnlyMaterials_(false)
    , animationEndCallback_(nullptr) {

    if (eqMesh_) {
        eqMesh_->grab();

        // Set up per-instance animator with shared skeleton
        auto skeleton = eqMesh_->getSkeleton();
        if (skeleton) {
            animator_.setSkeleton(skeleton);
        }

        // Create per-instance mesh copy for animation
        createInstanceMesh();

        // Copy materials from the instance mesh
        if (instanceMesh_) {
            for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
                if (mb) {
                    materials_.push_back(mb->getMaterial());
                }
            }
        }

        endFrame_ = static_cast<irr::s32>(eqMesh_->getFrameCount()) - 1;
    }
}

EQAnimatedMeshSceneNode::~EQAnimatedMeshSceneNode() {
    if (instanceMesh_) {
        instanceMesh_->drop();
    }
    if (eqMesh_) {
        eqMesh_->drop();
    }
    if (animationEndCallback_) {
        animationEndCallback_->drop();
    }
}

void EQAnimatedMeshSceneNode::createInstanceMesh() {
    if (instanceMesh_) {
        instanceMesh_->drop();
        instanceMesh_ = nullptr;
    }

    if (!eqMesh_) {
        return;
    }

    irr::scene::IMesh* baseMesh = eqMesh_->getBaseMesh();
    if (!baseMesh) {
        return;
    }

    // Create a new mesh by copying all mesh buffers from the base mesh
    instanceMesh_ = new irr::scene::SMesh();

    for (irr::u32 i = 0; i < baseMesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* srcBuffer = baseMesh->getMeshBuffer(i);
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
        instanceMesh_->addMeshBuffer(dstBuffer);
        dstBuffer->drop();
    }

    instanceMesh_->recalculateBoundingBox();
    boundingBox_ = instanceMesh_->getBoundingBox();
}

void EQAnimatedMeshSceneNode::applyAnimation() {
    if (!instanceMesh_ || !eqMesh_) {
        return;
    }

    const auto& vertexPieces = eqMesh_->getVertexPieces();
    const auto& originalVertices = eqMesh_->getOriginalVertices();
    const auto& vertexMapping = eqMesh_->getVertexMapping();

    if (vertexPieces.empty()) {
        return;
    }

    const auto& boneStates = animator_.getBoneStates();
    if (boneStates.empty()) {
        return;
    }

    // TRACE: Log bone transform info for player to verify animation isn't overriding node rotation
    if (isPlayerNode_) {
        static int animTraceCounter = 0;
        if (++animTraceCounter % 60 == 0) {  // Every second at 60fps
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] === Player applyAnimation Debug ===");
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] vertexPieces={} boneStates={} originalVerts={}",
                     vertexPieces.size(), boneStates.size(), originalVertices.size());

            // Log first bone (usually root) transform
            if (!boneStates.empty()) {
                const BoneMat4& rootBone = boneStates[0].worldTransform;
                float boneRotY = std::atan2(rootBone.m[8], rootBone.m[0]) * 180.0f / 3.14159f;
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] Bone[0] worldTransform rotY~{:.1f}", boneRotY);
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] Bone[0] row0: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]",
                         rootBone.m[0], rootBone.m[1], rootBone.m[2], rootBone.m[3]);
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] Bone[0] row2: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]",
                         rootBone.m[8], rootBone.m[9], rootBone.m[10], rootBone.m[11]);
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] Bone[0] translation: ({:.2f}, {:.2f}, {:.2f})",
                         rootBone.m[12], rootBone.m[13], rootBone.m[14]);
            }

            // Log a sample original vertex before transform
            if (!originalVertices.empty()) {
                const auto& v0 = originalVertices[0];
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-ANIM] OrigVertex[0] BEFORE bone transform: ({:.2f}, {:.2f}, {:.2f})",
                         v0.Pos.X, v0.Pos.Y, v0.Pos.Z);
            }
        }
    }

    // Check if we're using multi-buffer mode (with vertex mapping) or single-buffer mode
    bool useMultiBuffer = !originalVertices.empty() && !vertexMapping.empty();

    // Get node rotation for vertex baking
    irr::core::vector3df nodeRot = getRotation();

    // Enable vertex baking for player, disable for NPCs
    // NPCs work with world transform, player doesn't - so try baking for player
    float rotRadians = 0.0f;
    float cosRot = 1.0f;
    float sinRot = 0.0f;

    // FIX: Enable vertex baking for player to compensate for VIEW/WORLD rotation cancellation.
    // When camera follows player, VIEW (camera orientation) and WORLD (model rotation) are both
    // based on the same heading, causing them to cancel in ETS_CURRENT = VIEW * WORLD.
    // Pre-baking rotation into vertices (+90° offset) ensures the model's back faces the camera.
    if (isPlayerNode_) {
        rotRadians = (nodeRot.Y + 90.0f) * 3.14159f / 180.0f;
        cosRot = std::cos(rotRadians);
        sinRot = std::sin(rotRadians);
    }

    // Track rotation application for debugging
    if (isPlayerNode_) {
        static int frameNum = 0;
        static float lastAppliedRotY = -9999.0f;
        static int lastLogFrame = -10;
        frameNum++;

        bool rotChanged = std::abs(nodeRot.Y - lastAppliedRotY) > 0.5f;
        bool shouldLog = (frameNum <= 10) || rotChanged || (frameNum - lastLogFrame >= 60);
        if (shouldLog) {
            LOG_TRACE(MOD_GRAPHICS, "[ROT-WARP] frame={} nodeRot.Y={:.1f} lastApplied={:.1f} changed={} cos={:.3f} sin={:.3f}",
                     frameNum, nodeRot.Y, lastAppliedRotY, rotChanged ? "YES" : "no", cosRot, sinRot);
            lastLogFrame = frameNum;
        }
        lastAppliedRotY = nodeRot.Y;
    }

    // Log mesh buffer info for debugging
    if (isPlayerNode_) {
        static int bufPtrLogCounter = 0;
        if (++bufPtrLogCounter % 60 == 0 && instanceMesh_) {
            irr::u32 bufCount = instanceMesh_->getMeshBufferCount();
            irr::u32 totalVerts = 0;
            for (irr::u32 i = 0; i < bufCount; ++i) {
                irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
                if (mb) totalVerts += mb->getVertexCount();
            }
            LOG_TRACE(MOD_GRAPHICS, "[ROT-BUFFER] applyAnimation: instanceMesh_={} bufferCount={} totalVerts={} useMultiBuf={}",
                     (void*)instanceMesh_, bufCount, totalVerts, useMultiBuffer ? "yes" : "no");
        }
    }

    if (useMultiBuffer) {
        // Multi-buffer mode: transform original vertices and copy to mapped buffer locations
        size_t totalOrigVerts = originalVertices.size();

        // Apply bone transforms to vertices based on vertex pieces
        size_t vertexIdx = 0;
        for (const auto& piece : vertexPieces) {
            int boneIdx = piece.boneIndex;

            // Validate bone index
            if (boneIdx < 0 || boneIdx >= static_cast<int>(boneStates.size())) {
                vertexIdx += piece.count;
                continue;
            }

            const BoneMat4& boneMat = boneStates[boneIdx].worldTransform;

            // Transform each vertex in this piece
            for (int i = 0; i < piece.count; ++i) {
                size_t vIdx = vertexIdx + i;
                if (vIdx >= totalOrigVerts || vIdx >= vertexMapping.size()) break;

                // Get the destination buffer and local index
                const auto& mapping = vertexMapping[vIdx];
                if (mapping.bufferIndex >= instanceMesh_->getMeshBufferCount()) continue;

                irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
                    instanceMesh_->getMeshBuffer(mapping.bufferIndex));
                if (!dstBuffer || mapping.localIndex >= dstBuffer->Vertices.size()) continue;


                // Get original vertex position and normal (in EQ Z-up coordinates)
                const auto& srcVert = originalVertices[vIdx];
                float px = srcVert.Pos.X;
                float py = srcVert.Pos.Y;
                float pz = srcVert.Pos.Z;

                float nx = srcVert.Normal.X;
                float ny = srcVert.Normal.Y;
                float nz = srcVert.Normal.Z;

                // Transform position using bone matrix (in EQ coordinate space)
                boneMat.transformPoint(px, py, pz);

                // Transform normal (rotation only, from bone)
                float tnx = boneMat.m[0]*nx + boneMat.m[4]*ny + boneMat.m[8]*nz;
                float tny = boneMat.m[1]*nx + boneMat.m[5]*ny + boneMat.m[9]*nz;
                float tnz = boneMat.m[2]*nx + boneMat.m[6]*ny + boneMat.m[10]*nz;

                // Apply node's Y-rotation to vertices (for player with follow camera)
                // In EQ coordinates (Z-up), Y-rotation = rotation around Z axis
                // Rotate position: new_x = x*cos - y*sin, new_y = x*sin + y*cos
                float rx = px * cosRot - py * sinRot;
                float ry = px * sinRot + py * cosRot;
                px = rx;
                py = ry;
                // Also rotate normal
                float rnx = tnx * cosRot - tny * sinRot;
                float rny = tnx * sinRot + tny * cosRot;
                tnx = rnx;
                tny = rny;

                // Normalize
                float nlen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
                if (nlen > 0.0001f) {
                    tnx /= nlen;
                    tny /= nlen;
                    tnz /= nlen;
                }

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                dstBuffer->Vertices[mapping.localIndex].Pos.X = px;
                dstBuffer->Vertices[mapping.localIndex].Pos.Y = pz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[mapping.localIndex].Pos.Z = py;  // EQ Y -> Irrlicht Z
                dstBuffer->Vertices[mapping.localIndex].Normal.X = tnx;
                dstBuffer->Vertices[mapping.localIndex].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[mapping.localIndex].Normal.Z = tny;  // EQ Y -> Irrlicht Z
            }

            vertexIdx += piece.count;
        }

        // Mark all buffers as dirty and recalculate bounds
        for (irr::u32 b = 0; b < instanceMesh_->getMeshBufferCount(); ++b) {
            irr::scene::SMeshBuffer* buf = static_cast<irr::scene::SMeshBuffer*>(
                instanceMesh_->getMeshBuffer(b));
            if (buf) {
                buf->recalculateBoundingBox();
                buf->setDirty();
            }
        }
    } else {
        // Single-buffer mode (legacy): vertices are in order in buffer 0
        irr::scene::IMesh* baseMesh = eqMesh_->getBaseMesh();
        irr::scene::IMeshBuffer* srcBuffer = baseMesh ? baseMesh->getMeshBuffer(0) : nullptr;
        irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
            instanceMesh_->getMeshBuffer(0));

        if (!srcBuffer || !dstBuffer) return;

        irr::video::S3DVertex* srcVerts = static_cast<irr::video::S3DVertex*>(srcBuffer->getVertices());
        irr::u32 vertCount = srcBuffer->getVertexCount();

        // Apply bone transforms to vertices based on vertex pieces
        size_t vertexIdx = 0;
        for (const auto& piece : vertexPieces) {
            int boneIdx = piece.boneIndex;

            // Validate bone index
            if (boneIdx < 0 || boneIdx >= static_cast<int>(boneStates.size())) {
                vertexIdx += piece.count;
                continue;
            }

            const BoneMat4& boneMat = boneStates[boneIdx].worldTransform;

            // Transform each vertex in this piece
            for (int i = 0; i < piece.count; ++i) {
                size_t vIdx = vertexIdx + i;
                if (vIdx >= vertCount) break;

                // Get original vertex position and normal (in EQ Z-up coordinates)
                float px = srcVerts[vIdx].Pos.X;
                float py = srcVerts[vIdx].Pos.Y;
                float pz = srcVerts[vIdx].Pos.Z;

                float nx = srcVerts[vIdx].Normal.X;
                float ny = srcVerts[vIdx].Normal.Y;
                float nz = srcVerts[vIdx].Normal.Z;

                // Transform position using bone matrix (in EQ coordinate space)
                boneMat.transformPoint(px, py, pz);

                // Transform normal (rotation only, from bone)
                float tnx = boneMat.m[0]*nx + boneMat.m[4]*ny + boneMat.m[8]*nz;
                float tny = boneMat.m[1]*nx + boneMat.m[5]*ny + boneMat.m[9]*nz;
                float tnz = boneMat.m[2]*nx + boneMat.m[6]*ny + boneMat.m[10]*nz;

                // WORKAROUND: Apply node's Y-rotation here instead of relying on world transform
                // In EQ coordinates (Z-up), Y-rotation = rotation around Z axis
                // Rotate position: new_x = x*cos - y*sin, new_y = x*sin + y*cos
                float rx = px * cosRot - py * sinRot;
                float ry = px * sinRot + py * cosRot;
                px = rx;
                py = ry;
                // Also rotate normal
                float rnx = tnx * cosRot - tny * sinRot;
                float rny = tnx * sinRot + tny * cosRot;
                tnx = rnx;
                tny = rny;

                // Normalize
                float nlen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
                if (nlen > 0.0001f) {
                    tnx /= nlen;
                    tny /= nlen;
                    tnz /= nlen;
                }

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                dstBuffer->Vertices[vIdx].Pos.X = px;
                dstBuffer->Vertices[vIdx].Pos.Y = pz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[vIdx].Pos.Z = py;  // EQ Y -> Irrlicht Z
                dstBuffer->Vertices[vIdx].Normal.X = tnx;
                dstBuffer->Vertices[vIdx].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[vIdx].Normal.Z = tny;  // EQ Y -> Irrlicht Z
            }

            vertexIdx += piece.count;
        }

        dstBuffer->recalculateBoundingBox();
        dstBuffer->setDirty();
    }

    instanceMesh_->recalculateBoundingBox();
    boundingBox_ = instanceMesh_->getBoundingBox();
}

void EQAnimatedMeshSceneNode::OnRegisterSceneNode() {
    if (IsVisible) {
        SceneManager->registerNodeForRendering(this);
    }
    ISceneNode::OnRegisterSceneNode();
}

void EQAnimatedMeshSceneNode::render() {
    if (!instanceMesh_ || !SceneManager) return;

    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    if (!driver) return;

    // TRACE: Comprehensive transform pipeline logging for player node
    if (isPlayerNode_) {
        static int traceLogCounter = 0;
        if (++traceLogCounter % 60 == 0) {  // Every second at 60fps
            // === 1. Scene Node Hierarchy ===
            irr::scene::ISceneNode* parentNode = getParent();
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] === Player Node Transform Debug ===");
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] Parent: ptr={} name='{}'",
                     (void*)parentNode, parentNode ? parentNode->getName() : "null");
            if (parentNode) {
                irr::core::vector3df parentRot = parentNode->getRotation();
                irr::core::vector3df parentPos = parentNode->getPosition();
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] Parent rot=({:.1f},{:.1f},{:.1f}) pos=({:.1f},{:.1f},{:.1f})",
                         parentRot.X, parentRot.Y, parentRot.Z, parentPos.X, parentPos.Y, parentPos.Z);
            }

            // === 2. Node's Own Transforms ===
            irr::core::vector3df relPos = getPosition();
            irr::core::vector3df relRot = getRotation();
            irr::core::vector3df relScale = getScale();
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] RelativePos=({:.1f},{:.1f},{:.1f}) RelativeRot=({:.1f},{:.1f},{:.1f}) Scale=({:.2f},{:.2f},{:.2f})",
                     relPos.X, relPos.Y, relPos.Z, relRot.X, relRot.Y, relRot.Z, relScale.X, relScale.Y, relScale.Z);

            // === 3. Absolute Transformation Matrix ===
            const irr::core::matrix4& absM = AbsoluteTransformation;
            float absRotY = std::atan2(absM[8], absM[0]) * 180.0f / 3.14159f;
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] AbsoluteTransform rotY~{:.1f} (from matrix)", absRotY);
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] AbsMatrix row0: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", absM[0], absM[1], absM[2], absM[3]);
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] AbsMatrix row1: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", absM[4], absM[5], absM[6], absM[7]);
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] AbsMatrix row2: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", absM[8], absM[9], absM[10], absM[11]);
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] AbsMatrix row3: [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", absM[12], absM[13], absM[14], absM[15]);

            // === 4. Sample Vertex from Mesh Buffer ===
            if (instanceMesh_->getMeshBufferCount() > 0) {
                irr::scene::IMeshBuffer* mb0 = instanceMesh_->getMeshBuffer(0);
                if (mb0 && mb0->getVertexCount() > 0) {
                    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(mb0->getVertices());
                    // Log first 3 vertices (model space)
                    for (int vi = 0; vi < 3 && vi < (int)mb0->getVertexCount(); ++vi) {
                        LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] Vertex[{}] model-space: ({:.2f}, {:.2f}, {:.2f})",
                                 vi, verts[vi].Pos.X, verts[vi].Pos.Y, verts[vi].Pos.Z);

                        // Manually transform vertex by AbsoluteTransformation to show expected world pos
                        irr::core::vector3df modelPos = verts[vi].Pos;
                        irr::core::vector3df worldPos;
                        absM.transformVect(worldPos, modelPos);
                        LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] Vertex[{}] world-space (expected): ({:.2f}, {:.2f}, {:.2f})",
                                 vi, worldPos.X, worldPos.Y, worldPos.Z);
                    }
                }
            }

            // === 5. Driver's Current World Transform BEFORE we set it ===
            irr::core::matrix4 driverWorldBefore = driver->getTransform(irr::video::ETS_WORLD);
            float driverRotYBefore = std::atan2(driverWorldBefore[8], driverWorldBefore[0]) * 180.0f / 3.14159f;
            LOG_TRACE(MOD_GRAPHICS, "[TRACE-RENDER] Driver ETS_WORLD BEFORE setTransform: rotY~{:.1f}", driverRotYBefore);
        }
    }

    // Check if AbsoluteTransformation matches getRotation() for debugging
    if (isPlayerNode_) {
        static int absCheckCounter = 0;
        if (++absCheckCounter % 30 == 0) {
            float absRotY = std::atan2(AbsoluteTransformation[8], AbsoluteTransformation[0]) * 180.0f / 3.14159f;
            float nodeRotY = getRotation().Y;
            LOG_TRACE(MOD_GRAPHICS, "[ROT-ABS] AbsoluteTransformation rotY={:.1f} vs getRotation().Y={:.1f} match={}",
                     absRotY, nodeRotY, (std::abs(absRotY - nodeRotY) < 1.0f) ? "YES" : "NO!");
        }
    }

    // Log right before setTransform for debugging
    {
        static int setTransformLogCounter = 0;
        int logFrequency = isPlayerNode_ ? 30 : 120;
        if (++setTransformLogCounter % logFrequency == 0) {
            irr::core::vector3df pos = AbsoluteTransformation.getTranslation();
            irr::core::vector3df rot = AbsoluteTransformation.getRotationDegrees();
            LOG_TRACE(MOD_GRAPHICS, "[ROT-PRE] {} pos=({:.1f},{:.1f},{:.1f}) rot=({:.1f},{:.1f},{:.1f})",
                     isPlayerNode_ ? "PLAYER" : "NPC", pos.X, pos.Y, pos.Z, rot.X, rot.Y, rot.Z);
        }
    }

    // Set transformation
    // For player: use position-only transform since rotation is baked into vertices
    // For NPCs: use full AbsoluteTransformation
    if (isPlayerNode_) {
        // Create a position-only transform (no rotation) for player
        // The rotation is already baked into the vertices in applyAnimation()
        irr::core::matrix4 posOnlyTransform;
        posOnlyTransform.setTranslation(AbsoluteTransformation.getTranslation());
        driver->setTransform(irr::video::ETS_WORLD, posOnlyTransform);
    } else {
        driver->setTransform(irr::video::ETS_WORLD, AbsoluteTransformation);
    }

    // Render each mesh buffer from our per-instance mesh
    for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
        if (!mb) continue;

        // Set material
        if (i < materials_.size() && !readOnlyMaterials_) {
            driver->setMaterial(materials_[i]);
        } else {
            driver->setMaterial(mb->getMaterial());
        }

        // TRACE: Check if setMaterial modified the world transform (for first buffer only)
        if (isPlayerNode_ && i == 0) {
            static int drawLoopCounter = 0;
            if (++drawLoopCounter % 60 == 0) {
                irr::core::matrix4 worldAtDraw = driver->getTransform(irr::video::ETS_WORLD);
                irr::core::matrix4 viewAtDraw = driver->getTransform(irr::video::ETS_VIEW);
                float drawRotY = std::atan2(worldAtDraw[8], worldAtDraw[0]) * 180.0f / 3.14159f;
                float viewRotY = std::atan2(viewAtDraw[8], viewAtDraw[0]) * 180.0f / 3.14159f;
                float expectedRotY = std::atan2(AbsoluteTransformation[8], AbsoluteTransformation[0]) * 180.0f / 3.14159f;
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-DRAW] WORLD rotY~{:.1f} VIEW rotY~{:.1f} (expected WORLD {:.1f}) match={}",
                         drawRotY, viewRotY, expectedRotY, (std::abs(drawRotY - expectedRotY) < 0.1f) ? "YES" : "NO!");
                // Calculate net rotation after VIEW*WORLD
                float netRotY = drawRotY - viewRotY;  // Approximate net rotation
                LOG_TRACE(MOD_GRAPHICS, "[TRACE-DRAW] Net rotation (WORLD-VIEW)~{:.1f} deg - if ~0, camera follows model",
                         netRotY);

                // Also log the first vertex from this specific buffer we're about to draw
                irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
                if (mb->getVertexCount() > 0) {
                    // Store last vertex position to detect if vertices are changing between frames
                    static irr::core::vector3df lastVert0Pos(0,0,0);
                    irr::core::vector3df currentVert0 = verts[0].Pos;
                    bool vertexChanged = (lastVert0Pos.X != 0 || lastVert0Pos.Y != 0 || lastVert0Pos.Z != 0) &&
                                        (std::abs(currentVert0.X - lastVert0Pos.X) > 0.001f ||
                                         std::abs(currentVert0.Y - lastVert0Pos.Y) > 0.001f ||
                                         std::abs(currentVert0.Z - lastVert0Pos.Z) > 0.001f);
                    LOG_TRACE(MOD_GRAPHICS, "[TRACE-DRAW] Buffer0 Vert0: ({:.3f},{:.3f},{:.3f}) changed_since_last={}",
                             currentVert0.X, currentVert0.Y, currentVert0.Z, vertexChanged ? "YES" : "NO");
                    lastVert0Pos = currentVert0;
                }
            }
        }

        // Draw the mesh buffer
        driver->drawMeshBuffer(mb);
    }

    // Debug: draw bounding box if enabled
    if (DebugDataVisible & irr::scene::EDS_BBOX) {
        irr::video::SMaterial debugMat;
        debugMat.Lighting = false;
        driver->setMaterial(debugMat);
        driver->draw3DBox(boundingBox_, irr::video::SColor(255, 255, 255, 255));
    }
}

const irr::core::aabbox3d<irr::f32>& EQAnimatedMeshSceneNode::getBoundingBox() const {
    return boundingBox_;
}

irr::video::SMaterial& EQAnimatedMeshSceneNode::getMaterial(irr::u32 i) {
    if (i < materials_.size()) {
        return materials_[i];
    }
    return materials_.empty() ? ISceneNode::getMaterial(0) : materials_[0];
}

irr::u32 EQAnimatedMeshSceneNode::getMaterialCount() const {
    return static_cast<irr::u32>(materials_.size());
}

void EQAnimatedMeshSceneNode::OnAnimate(irr::u32 timeMs) {
    if (lastTimeMs_ == 0) {
        lastTimeMs_ = timeMs;
    }

    // Calculate delta time
    irr::u32 deltaMs = timeMs - lastTimeMs_;
    lastTimeMs_ = timeMs;

    // Update our per-instance animator
    if (deltaMs > 0) {
        // Apply animation speed
        float adjustedDelta = static_cast<float>(deltaMs) * animationSpeed_;
        animator_.update(adjustedDelta);

        // Check for animation end
        if (animator_.getState() == AnimationState::Stopped && animationEndCallback_) {
            animationEndCallback_->OnAnimationEnd(this);
        }

        // Apply animation to our per-instance mesh
        applyAnimation();
    }

    // Debug: Check if parent's OnAnimate modifies rotation
    if (isPlayerNode_) {
        irr::core::vector3df rotBefore = getRotation();
        IAnimatedMeshSceneNode::OnAnimate(timeMs);
        irr::core::vector3df rotAfter = getRotation();
        if (std::abs(rotBefore.Y - rotAfter.Y) > 0.1f) {
            LOG_DEBUG(MOD_GRAPHICS, "[ROT-ANIMATE] PLAYER OnAnimate ROTATION CHANGED! before.Y={:.1f} after.Y={:.1f}",
                      rotBefore.Y, rotAfter.Y);
        }
    } else {
        IAnimatedMeshSceneNode::OnAnimate(timeMs);
    }
}

void EQAnimatedMeshSceneNode::setCurrentFrame(irr::f32 frame) {
    // Not directly supported - use playAnimation instead
}

bool EQAnimatedMeshSceneNode::setFrameLoop(irr::s32 begin, irr::s32 end) {
    startFrame_ = begin;
    endFrame_ = end;
    return true;
}

void EQAnimatedMeshSceneNode::setAnimationSpeed(irr::f32 framesPerSecond) {
    // Convert frames per second to speed multiplier
    // Assuming 10 fps as base speed
    animationSpeed_ = framesPerSecond / 10.0f;
    animator_.setPlaybackSpeed(animationSpeed_);
}

irr::f32 EQAnimatedMeshSceneNode::getAnimationSpeed() const {
    return animationSpeed_ * 10.0f;  // Convert back to fps
}

irr::f32 EQAnimatedMeshSceneNode::getFrameNr() const {
    return static_cast<irr::f32>(animator_.getCurrentFrame());
}

irr::s32 EQAnimatedMeshSceneNode::getStartFrame() const {
    return startFrame_;
}

irr::s32 EQAnimatedMeshSceneNode::getEndFrame() const {
    return endFrame_;
}

void EQAnimatedMeshSceneNode::setLoopMode(bool playAnimationLooped) {
    looping_ = playAnimationLooped;
}

bool EQAnimatedMeshSceneNode::getLoopMode() const {
    return looping_;
}

void EQAnimatedMeshSceneNode::setAnimationEndCallback(irr::scene::IAnimationEndCallBack* callback) {
    if (animationEndCallback_) {
        animationEndCallback_->drop();
    }
    animationEndCallback_ = callback;
    if (animationEndCallback_) {
        animationEndCallback_->grab();
    }
}

void EQAnimatedMeshSceneNode::setReadOnlyMaterials(bool readonly) {
    readOnlyMaterials_ = readonly;
}

bool EQAnimatedMeshSceneNode::isReadOnlyMaterials() const {
    return readOnlyMaterials_;
}

void EQAnimatedMeshSceneNode::setMesh(irr::scene::IAnimatedMesh* mesh) {
    // Only accept EQAnimatedMesh
    EQAnimatedMesh* eqMesh = dynamic_cast<EQAnimatedMesh*>(mesh);
    if (eqMesh) {
        if (eqMesh_) {
            eqMesh_->drop();
        }
        eqMesh_ = eqMesh;
        eqMesh_->grab();

        // Update per-instance animator with new skeleton
        auto skeleton = eqMesh_->getSkeleton();
        if (skeleton) {
            animator_.setSkeleton(skeleton);
        }

        // Recreate per-instance mesh
        createInstanceMesh();

        // Update materials from the new instance mesh
        materials_.clear();
        if (instanceMesh_) {
            for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
                if (mb) {
                    materials_.push_back(mb->getMaterial());
                }
            }
        }

        endFrame_ = static_cast<irr::s32>(eqMesh_->getFrameCount()) - 1;
    }
}

irr::scene::IAnimatedMesh* EQAnimatedMeshSceneNode::getMesh(void) {
    return eqMesh_;
}

const irr::scene::SMD3QuaternionTag* EQAnimatedMeshSceneNode::getMD3TagTransformation(
    const irr::core::stringc& tagname) {
    return nullptr;  // Not supported
}

void EQAnimatedMeshSceneNode::setJointMode(irr::scene::E_JOINT_UPDATE_ON_RENDER mode) {
    // Not applicable - we use our own animation system
}

void EQAnimatedMeshSceneNode::setTransitionTime(irr::f32 Time) {
    // Not supported yet - could be used for animation blending
}

void EQAnimatedMeshSceneNode::animateJoints(bool CalculateAbsolutePositions) {
    // Apply animation to our per-instance mesh
    applyAnimation();
}

void EQAnimatedMeshSceneNode::setRenderFromIdentity(bool On) {
    // Not supported
}

irr::scene::ISceneNode* EQAnimatedMeshSceneNode::clone(irr::scene::ISceneNode* newParent,
                                                        irr::scene::ISceneManager* newManager) {
    if (!newParent) newParent = Parent;
    if (!newManager) newManager = SceneManager;

    EQAnimatedMeshSceneNode* newNode = new EQAnimatedMeshSceneNode(
        eqMesh_, newParent, newManager, ID, RelativeTranslation, RelativeRotation, RelativeScale);

    newNode->setAnimationSpeed(getAnimationSpeed());
    newNode->setLoopMode(looping_);
    newNode->setFrameLoop(startFrame_, endFrame_);

    return newNode;
}

irr::scene::IShadowVolumeSceneNode* EQAnimatedMeshSceneNode::addShadowVolumeSceneNode(
    const irr::scene::IMesh* shadowMesh,
    irr::s32 id,
    bool zfailmethod,
    irr::f32 infinity) {
    return nullptr;  // Not supported
}

irr::scene::IBoneSceneNode* EQAnimatedMeshSceneNode::getJointNode(const irr::c8* jointName) {
    return nullptr;  // Not supported - we use our own bone system
}

irr::scene::IBoneSceneNode* EQAnimatedMeshSceneNode::getJointNode(irr::u32 jointID) {
    return nullptr;  // Not supported
}

irr::u32 EQAnimatedMeshSceneNode::getJointCount() const {
    auto skeleton = animator_.getSkeleton();
    if (skeleton) {
        return static_cast<irr::u32>(skeleton->bones.size());
    }
    return 0;
}

bool EQAnimatedMeshSceneNode::setMD2Animation(irr::scene::EMD2_ANIMATION_TYPE anim) {
    // Map MD2 animation types to EQ animation codes
    switch (anim) {
        case irr::scene::EMAT_STAND:
            return playAnimation("o01", true);  // Stand idle
        case irr::scene::EMAT_RUN:
            return playAnimation("l02", true);  // Run
        case irr::scene::EMAT_ATTACK:
            return playAnimation("c01", false); // Combat
        case irr::scene::EMAT_PAIN_A:
        case irr::scene::EMAT_PAIN_B:
        case irr::scene::EMAT_PAIN_C:
            return playAnimation("d01", false); // Damage
        case irr::scene::EMAT_JUMP:
            return playAnimation("l03", false); // Jump
        case irr::scene::EMAT_CROUCH_STAND:
            return playAnimation("l08", true);  // Crouch
        case irr::scene::EMAT_CROUCH_WALK:
            return playAnimation("l06", true);  // Crouch walk
        case irr::scene::EMAT_DEATH_FALLBACK:
        case irr::scene::EMAT_DEATH_FALLFORWARD:
        case irr::scene::EMAT_DEATH_FALLBACKSLOW:
            return playAnimation("d05", false); // Death
        default:
            return playAnimation("o01", true);  // Default to standing
    }
}

bool EQAnimatedMeshSceneNode::setMD2Animation(const irr::c8* animationName) {
    return playAnimation(animationName, looping_);
}

// EQ-specific methods

bool EQAnimatedMeshSceneNode::playAnimation(const std::string& animCode, bool loop, bool playThrough) {
    return animator_.playAnimation(animCode, loop, playThrough);
}

void EQAnimatedMeshSceneNode::stopAnimation() {
    animator_.stopAnimation();
}

bool EQAnimatedMeshSceneNode::hasAnimation(const std::string& animCode) const {
    return animator_.hasAnimation(animCode);
}

std::vector<std::string> EQAnimatedMeshSceneNode::getAnimationList() const {
    return animator_.getAnimationList();
}

const std::string& EQAnimatedMeshSceneNode::getCurrentAnimation() const {
    return animator_.getCurrentAnimation();
}

bool EQAnimatedMeshSceneNode::isPlayingThrough() const {
    return animator_.isPlayingThrough();
}

bool EQAnimatedMeshSceneNode::getBoneWorldPosition(int boneIndex, irr::core::vector3df& outPosition) const {
    const auto& boneStates = animator_.getBoneStates();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(boneStates.size())) {
        return false;
    }

    const BoneMat4& worldXform = boneStates[boneIndex].worldTransform;
    float px = worldXform.m[12];
    float py = worldXform.m[13];
    float pz = worldXform.m[14];

    // Convert EQ(x,y,z) to Irrlicht(x,z,y) and account for node transform
    irr::core::vector3df localPos(px, pz, py);

    // Apply node's world transform (scale and position)
    irr::core::vector3df nodePos = getAbsolutePosition();
    irr::core::vector3df nodeScale = getScale();

    outPosition.X = nodePos.X + localPos.X * nodeScale.X;
    outPosition.Y = nodePos.Y + localPos.Y * nodeScale.Y;
    outPosition.Z = nodePos.Z + localPos.Z * nodeScale.Z;

    return true;
}

int EQAnimatedMeshSceneNode::getBoneIndexByName(const std::string& boneName) const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;
    return skeleton->getBoneIndex(boneName);
}

int EQAnimatedMeshSceneNode::findRightHandBoneIndex() const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;

    // Get race code from skeleton's modelCode
    std::string raceCode = skeleton->modelCode;

    // Common right hand bone name patterns
    std::vector<std::string> suffixes = {
        "r_point", "R_POINT", "R_POINT_DAG", "BO_R_DAG", "TO_R_DAG",
        "_r_point", "_R_POINT"
    };

    // Try with race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(raceCode + suffix);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        idx = skeleton->getBoneIndex(lowerRace + suffix);
        if (idx >= 0) return idx;
    }

    // Try without race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(suffix);
        if (idx >= 0) return idx;
    }

    return -1;
}

int EQAnimatedMeshSceneNode::findLeftHandBoneIndex() const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;

    // Get race code from skeleton's modelCode
    std::string raceCode = skeleton->modelCode;

    // Common left hand bone name patterns
    std::vector<std::string> suffixes = {
        "l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG",
        "shield_point", "SHIELD_POINT", "_l_point", "_L_POINT"
    };

    // Try with race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(raceCode + suffix);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        idx = skeleton->getBoneIndex(lowerRace + suffix);
        if (idx >= 0) return idx;
    }

    // Try without race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(suffix);
        if (idx >= 0) return idx;
    }

    return -1;
}

void EQAnimatedMeshSceneNode::setFirstPersonMode(bool enabled) {
    if (firstPersonMode_ == enabled) {
        return;  // No change
    }

    firstPersonMode_ = enabled;

    // Build arm bone indices on first enable
    if (enabled && !armBonesBuilt_) {
        buildArmBoneIndices();
    }

    LOG_DEBUG(MOD_GRAPHICS, "EQAnimatedMeshSceneNode::setFirstPersonMode: enabled={} armBones={}",
              enabled, armBoneIndices_.size());
}

void EQAnimatedMeshSceneNode::buildArmBoneIndices() {
    armBoneIndices_.clear();
    armBonesBuilt_ = true;

    auto skeleton = animator_.getSkeleton();
    if (!skeleton) {
        LOG_WARN(MOD_GRAPHICS, "buildArmBoneIndices: No skeleton available");
        return;
    }

    // First, find the arm root bones and all their children
    // EQ bone naming conventions:
    // - Arms: contains "ARM" (e.g., HUFRARMVIS, HUFLARMVIS, RARM, LARM)
    // - Hand attachment points: contains "POINT" (e.g., R_POINT, L_POINT, SHIELD_POINT)
    // - Weapon/shield related: contains "DAG", "SHIELD"
    // We want to show any bone that is part of the arm hierarchy

    // Build a map of bone indices that are arm-related
    std::set<int> armBones;

    // Log all bone names for diagnostics
    LOG_DEBUG(MOD_GRAPHICS, "buildArmBoneIndices: Skeleton has {} bones:", skeleton->bones.size());
    for (size_t i = 0; i < skeleton->bones.size(); ++i) {
        LOG_DEBUG(MOD_GRAPHICS, "  bone[{}] = '{}'", i, skeleton->bones[i].name);
    }

    // Pass 1: Find bones with arm-related names
    for (size_t i = 0; i < skeleton->bones.size(); ++i) {
        const std::string& name = skeleton->bones[i].name;

        // Convert to uppercase for case-insensitive matching
        std::string upperName = name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

        // Check for arm-related keywords (check ALL conditions, not mutually exclusive)
        bool isArmBone = false;

        // Primary arm bones (explicit ARM in name)
        if (upperName.find("ARM") != std::string::npos) {
            isArmBone = true;
        }
        // Hand attachment points for weapons
        if (upperName.find("POINT") != std::string::npos) {
            isArmBone = true;
        }
        // Weapon-related bones
        if (upperName.find("DAG") != std::string::npos) {
            isArmBone = true;
        }
        // Shield bones
        if (upperName.find("SHIELD") != std::string::npos) {
            isArmBone = true;
        }
        // Hand/wrist bones
        if (upperName.find("HAND") != std::string::npos ||
            upperName.find("WRIST") != std::string::npos ||
            upperName.find("FINGER") != std::string::npos) {
            isArmBone = true;
        }
        // Forearm bones (some models use FA suffix)
        if (upperName.find("FOREARM") != std::string::npos) {
            isArmBone = true;
        }
        // EQ-specific arm bone naming: BI (bicep/upper arm), FO (forearm), FI (fingers)
        // Format: {race_code}BI_R, {race_code}FO_R, {race_code}FI_R, etc.
        // e.g., HUMBI_R, HUMFO_R, HUMFI_R for human right arm
        if (upperName.find("BI_R") != std::string::npos ||
            upperName.find("BI_L") != std::string::npos ||
            upperName.find("FO_R") != std::string::npos ||
            upperName.find("FO_L") != std::string::npos ||
            upperName.find("FI_R") != std::string::npos ||
            upperName.find("FI_L") != std::string::npos) {
            isArmBone = true;
        }

        if (isArmBone) {
            armBones.insert(static_cast<int>(i));
            LOG_DEBUG(MOD_GRAPHICS, "buildArmBoneIndices: Found arm bone[{}] '{}'", i, name);
        }
    }

    // Pass 2: Add all children of arm bones (to get full arm hierarchy)
    // This ensures we get forearms, hands, fingers, etc.
    bool addedChild = true;
    while (addedChild) {
        addedChild = false;
        for (size_t i = 0; i < skeleton->bones.size(); ++i) {
            if (armBones.count(static_cast<int>(i)) > 0) {
                continue;  // Already in the set
            }

            int parentIdx = skeleton->bones[i].parentIndex;
            if (parentIdx >= 0 && armBones.count(parentIdx) > 0) {
                armBones.insert(static_cast<int>(i));
                addedChild = true;
                LOG_DEBUG(MOD_GRAPHICS, "buildArmBoneIndices: Added child bone[{}] '{}' (parent={})",
                          i, skeleton->bones[i].name, parentIdx);
            }
        }
    }

    // Convert set to vector
    armBoneIndices_.assign(armBones.begin(), armBones.end());

    LOG_INFO(MOD_GRAPHICS, "buildArmBoneIndices: Found {} arm-related bones out of {} total",
             armBoneIndices_.size(), skeleton->bones.size());
}

} // namespace Graphics
} // namespace EQT
