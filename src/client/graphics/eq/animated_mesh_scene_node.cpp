#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "common/simd_detect.h"
#include "common/logging.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>

namespace EQT {
namespace Graphics {

// ============================================================================
// SIMD Normal Transform + Normalize Helper
// ============================================================================

// Transform normal by bone matrix rotation (no translation) and normalize.
// Modifies tnx/tny/tnz in place.
static inline void transformAndNormalizeNormal(
    const BoneMat4& boneMat,
    float nx, float ny, float nz,
    float& tnx, float& tny, float& tnz)
{
#ifdef EQT_HAS_NEON
    // Normal rotation: 3 dot products using matrix columns (rotation only)
    float32x4_t col0 = vld1q_f32(&boneMat.m[0]);
    float32x4_t col1 = vld1q_f32(&boneMat.m[4]);
    float32x4_t col2 = vld1q_f32(&boneMat.m[8]);
    float32x4_t nRes = vmulq_n_f32(col0, nx);
    nRes = vmlaq_n_f32(nRes, col1, ny);
    nRes = vmlaq_n_f32(nRes, col2, nz);

    // Fast normalize using NEON reciprocal sqrt estimate + Newton-Raphson
    float32x4_t sq = vmulq_f32(nRes, nRes);
    float32x2_t sum = vadd_f32(vget_low_f32(sq), vget_high_f32(sq));
    float32x2_t lenSq = vpadd_f32(sum, sum);  // x+z, y+w -> sum in both lanes
    // Guard against zero-length normals
    if (vget_lane_f32(lenSq, 0) > 0.00000001f) {
        float32x2_t invLen = vrsqrte_f32(lenSq);
        // Newton-Raphson refinement: invLen = invLen * (3 - lenSq * invLen^2) / 2
        invLen = vmul_f32(invLen, vrsqrts_f32(vmul_f32(lenSq, invLen), invLen));
        nRes = vmulq_f32(nRes, vdupq_lane_f32(invLen, 0));
    }

    tnx = vgetq_lane_f32(nRes, 0);
    tny = vgetq_lane_f32(nRes, 1);
    tnz = vgetq_lane_f32(nRes, 2);
#elif defined(EQT_HAS_SSE2)
    __m128 col0 = _mm_loadu_ps(&boneMat.m[0]);
    __m128 col1 = _mm_loadu_ps(&boneMat.m[4]);
    __m128 col2 = _mm_loadu_ps(&boneMat.m[8]);
    __m128 vnx = _mm_set1_ps(nx);
    __m128 vny = _mm_set1_ps(ny);
    __m128 vnz = _mm_set1_ps(nz);
    __m128 nRes = _mm_add_ps(_mm_add_ps(_mm_mul_ps(col0, vnx), _mm_mul_ps(col1, vny)),
                              _mm_mul_ps(col2, vnz));

    // Normalize: compute dot(nRes, nRes) then multiply by rsqrt
    __m128 sq = _mm_mul_ps(nRes, nRes);
    __m128 shuf = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2,3,0,1));
    __m128 sums = _mm_add_ps(sq, shuf);
    shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0,1,2,3));
    __m128 lenSqV = _mm_add_ps(sums, shuf);  // lenSq broadcast in all lanes

    // Guard against zero
    if (_mm_cvtss_f32(lenSqV) > 0.00000001f) {
        __m128 invLen = _mm_rsqrt_ps(lenSqV);
        // Newton-Raphson: invLen = invLen * (1.5 - 0.5 * lenSq * invLen * invLen)
        __m128 half = _mm_set1_ps(0.5f);
        __m128 three_half = _mm_set1_ps(1.5f);
        __m128 refined = _mm_mul_ps(invLen, _mm_sub_ps(three_half,
            _mm_mul_ps(half, _mm_mul_ps(lenSqV, _mm_mul_ps(invLen, invLen)))));
        nRes = _mm_mul_ps(nRes, refined);
    }

    EQT_ALIGN(16) float out[4];
    _mm_store_ps(out, nRes);
    tnx = out[0]; tny = out[1]; tnz = out[2];
#else
    tnx = boneMat.m[0]*nx + boneMat.m[4]*ny + boneMat.m[8]*nz;
    tny = boneMat.m[1]*nx + boneMat.m[5]*ny + boneMat.m[9]*nz;
    tnz = boneMat.m[2]*nx + boneMat.m[6]*ny + boneMat.m[10]*nz;

    float nlen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
    if (nlen > 0.0001f) {
        tnx /= nlen;
        tny /= nlen;
        tnz /= nlen;
    }
#endif
}

// ============================================================================
// Shared Vertex Transformation Helper (Phase 4 Consolidation)
// ============================================================================

// Apply bone transforms to mesh vertices - shared implementation used by both
// EQAnimatedMesh and EQAnimatedMeshSceneNode to avoid code duplication.
//
// Parameters:
//   destMesh - Destination mesh to write transformed vertices
//   boneStates - Current bone world transforms from animator
//   vertexPieces - Vertex-to-bone mapping (which vertices belong to which bone)
//   originalVertices - Original vertex positions (for multi-buffer mode)
//   vertexMapping - Vertex-to-buffer mapping (for multi-buffer mode)
//   baseMesh - Source mesh for single-buffer mode (may be nullptr)
//   extraRotCos, extraRotSin - Extra Y rotation for player (1.0/0.0 for no rotation)
//
static void applyBoneTransformsToMeshImpl(
    irr::scene::SMesh* destMesh,
    const std::vector<AnimatedBoneState>& boneStates,
    const std::vector<VertexPiece>& vertexPieces,
    const std::vector<irr::video::S3DVertex>& originalVertices,
    const std::vector<VertexMappingList>& vertexMapping,
    irr::scene::IMesh* baseMesh,
    float extraRotCos,
    float extraRotSin)
{
    if (!destMesh || vertexPieces.empty() || boneStates.empty()) {
        return;
    }

    // Check if we're using multi-buffer mode (with vertex mapping) or single-buffer mode
    bool useMultiBuffer = !originalVertices.empty() && !vertexMapping.empty();

    // Track per-buffer bounding boxes inline during vertex transforms,
    // avoiding the expensive second pass of recalculateBoundingBox() which
    // iterates all vertices again. On a 500-vertex entity this eliminates
    // ~500 reads per buffer per frame.
    irr::u32 bufferCount = destMesh->getMeshBufferCount();
    struct BufBounds {
        float minX, minY, minZ, maxX, maxY, maxZ;
        bool initialized;
    };
    // Stack-allocate for typical case (<=8 buffers), heap for more
    BufBounds stackBounds[8];
    std::vector<BufBounds> heapBounds;
    BufBounds* bufBounds;
    if (bufferCount <= 8) {
        bufBounds = stackBounds;
    } else {
        heapBounds.resize(bufferCount);
        bufBounds = heapBounds.data();
    }
    for (irr::u32 b = 0; b < bufferCount; ++b) {
        bufBounds[b].initialized = false;
    }

    // Helper to update per-buffer bounds with a transformed vertex (Irrlicht Y-up coords)
    auto updateBounds = [&](irr::u32 bufIdx, float ix, float iy, float iz) {
        auto& bb = bufBounds[bufIdx];
        if (!bb.initialized) {
            bb.minX = bb.maxX = ix;
            bb.minY = bb.maxY = iy;
            bb.minZ = bb.maxZ = iz;
            bb.initialized = true;
        } else {
            if (ix < bb.minX) bb.minX = ix; else if (ix > bb.maxX) bb.maxX = ix;
            if (iy < bb.minY) bb.minY = iy; else if (iy > bb.maxY) bb.maxY = iy;
            if (iz < bb.minZ) bb.minZ = iz; else if (iz > bb.maxZ) bb.maxZ = iz;
        }
    };

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

                // P08D-RETAKE: a vertex shared across texture groups/UV seams may have
                // MULTIPLE destinations (see VertexMappingList) -- compute the transform
                // once, then write it to every duplicate so none are left frozen at bind
                // pose. Empty list = unmapped vertex (skipped entirely), same as before.
                const auto& mappings = vertexMapping[vIdx];
                if (mappings.empty()) continue;

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

                // Transform normal and normalize (SIMD-optimized)
                float tnx, tny, tnz;
                transformAndNormalizeNormal(boneMat, nx, ny, nz, tnx, tny, tnz);

                // Apply extra Y-rotation if requested (for player with follow camera)
                // In EQ coordinates (Z-up), Y-rotation = rotation around Z axis
                if (extraRotCos != 1.0f || extraRotSin != 0.0f) {
                    float rx = px * extraRotCos - py * extraRotSin;
                    float ry = px * extraRotSin + py * extraRotCos;
                    px = rx;
                    py = ry;
                    float rnx = tnx * extraRotCos - tny * extraRotSin;
                    float rny = tnx * extraRotSin + tny * extraRotCos;
                    tnx = rnx;
                    tny = rny;
                }

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                float ix = px, iy = pz, iz = py;  // Irrlicht coords

                for (const auto& mapping : mappings) {
                    if (mapping.bufferIndex >= bufferCount) continue;

                    irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
                        destMesh->getMeshBuffer(mapping.bufferIndex));
                    if (!dstBuffer || mapping.localIndex >= dstBuffer->Vertices.size()) continue;

                    dstBuffer->Vertices[mapping.localIndex].Pos.X = ix;
                    dstBuffer->Vertices[mapping.localIndex].Pos.Y = iy;
                    dstBuffer->Vertices[mapping.localIndex].Pos.Z = iz;
                    dstBuffer->Vertices[mapping.localIndex].Normal.X = tnx;
                    dstBuffer->Vertices[mapping.localIndex].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                    dstBuffer->Vertices[mapping.localIndex].Normal.Z = tny;  // EQ Y -> Irrlicht Z

                    // Track bounding box inline — no second vertex pass needed
                    updateBounds(mapping.bufferIndex, ix, iy, iz);
                }
            }

            vertexIdx += piece.count;
        }

        // Set pre-computed bounding boxes and mark dirty (no recalculateBoundingBox)
        for (irr::u32 b = 0; b < bufferCount; ++b) {
            irr::scene::SMeshBuffer* buf = static_cast<irr::scene::SMeshBuffer*>(
                destMesh->getMeshBuffer(b));
            if (buf) {
                if (bufBounds[b].initialized) {
                    buf->setBoundingBox(irr::core::aabbox3df(
                        bufBounds[b].minX, bufBounds[b].minY, bufBounds[b].minZ,
                        bufBounds[b].maxX, bufBounds[b].maxY, bufBounds[b].maxZ));
                }
                buf->setDirty();
            }
        }
    } else {
        // Single-buffer mode (legacy): vertices are in order in buffer 0
        irr::scene::IMeshBuffer* srcBuffer = baseMesh ? baseMesh->getMeshBuffer(0) : nullptr;
        irr::scene::SMeshBuffer* dstBuffer = static_cast<irr::scene::SMeshBuffer*>(
            destMesh->getMeshBuffer(0));

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

                // Transform normal and normalize (SIMD-optimized)
                float tnx, tny, tnz;
                transformAndNormalizeNormal(boneMat, nx, ny, nz, tnx, tny, tnz);

                // Apply extra Y-rotation if requested (for player with follow camera)
                if (extraRotCos != 1.0f || extraRotSin != 0.0f) {
                    float rx = px * extraRotCos - py * extraRotSin;
                    float ry = px * extraRotSin + py * extraRotCos;
                    px = rx;
                    py = ry;
                    float rnx = tnx * extraRotCos - tny * extraRotSin;
                    float rny = tnx * extraRotSin + tny * extraRotCos;
                    tnx = rnx;
                    tny = rny;
                }

                // Convert from EQ (Z-up) to Irrlicht (Y-up): (x, y, z) -> (x, z, y)
                float ix = px, iy = pz, iz = py;
                dstBuffer->Vertices[vIdx].Pos.X = ix;
                dstBuffer->Vertices[vIdx].Pos.Y = iy;
                dstBuffer->Vertices[vIdx].Pos.Z = iz;
                dstBuffer->Vertices[vIdx].Normal.X = tnx;
                dstBuffer->Vertices[vIdx].Normal.Y = tnz;  // EQ Z -> Irrlicht Y
                dstBuffer->Vertices[vIdx].Normal.Z = tny;  // EQ Y -> Irrlicht Z

                updateBounds(0, ix, iy, iz);
            }

            vertexIdx += piece.count;
        }

        if (bufBounds[0].initialized) {
            dstBuffer->setBoundingBox(irr::core::aabbox3df(
                bufBounds[0].minX, bufBounds[0].minY, bufBounds[0].minZ,
                bufBounds[0].maxX, bufBounds[0].maxY, bufBounds[0].maxZ));
        }
        dstBuffer->setDirty();
    }

    // Build mesh bounding box from per-buffer boxes (no vertex iteration)
    irr::core::aabbox3df meshBox;
    bool meshBoxInit = false;
    for (irr::u32 b = 0; b < bufferCount; ++b) {
        if (bufBounds[b].initialized) {
            irr::core::aabbox3df bb(
                bufBounds[b].minX, bufBounds[b].minY, bufBounds[b].minZ,
                bufBounds[b].maxX, bufBounds[b].maxY, bufBounds[b].maxZ);
            if (!meshBoxInit) {
                meshBox = bb;
                meshBoxInit = true;
            } else {
                meshBox.addInternalBox(bb);
            }
        }
    }
    if (meshBoxInit) {
        destMesh->setBoundingBox(meshBox);
    }
}

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

void EQAnimatedMesh::setVertexMapping(const std::vector<VertexMappingList>& mapping) {
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
            LOG_DEBUG(MOD_GRAPHICS, "EQAnimatedMeshSceneNode: Set skeleton model='{}' animations={}",
                      skeleton->modelCode, skeleton->animations.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "EQAnimatedMeshSceneNode: No skeleton from mesh!");
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

    // Performance optimization: skip if animation frame hasn't changed
    int currentFrame = animator_.getCurrentFrame();
    irr::core::vector3df nodeRot = getRotation();

    if (isPlayerNode_) {
        // For player: also check rotation since we bake it into vertices
        bool frameChanged = (currentFrame != lastAppliedFrame_);
        bool rotChanged = (std::abs(nodeRot.Y - lastAppliedRotY_) > 0.1f);
        if (!frameChanged && !rotChanged) {
            return;  // Nothing changed, skip vertex transforms
        }
    } else {
        // For NPCs: only check frame (rotation handled by node transform)
        if (currentFrame == lastAppliedFrame_) {
            return;  // Frame unchanged, skip vertex transforms
        }
    }

    // Calculate rotation for player vertex baking
    // FIX: Enable vertex baking for player to compensate for VIEW/WORLD rotation cancellation.
    // When camera follows player, VIEW (camera orientation) and WORLD (model rotation) are both
    // based on the same heading, causing them to cancel in ETS_CURRENT = VIEW * WORLD.
    // Pre-baking rotation into vertices (+90° offset) ensures the model's back faces the camera.
    float cosRot = 1.0f;
    float sinRot = 0.0f;
    if (isPlayerNode_) {
        float rotRadians = (nodeRot.Y + 90.0f) * 3.14159f / 180.0f;
        cosRot = std::cos(rotRadians);
        sinRot = std::sin(rotRadians);
    }

    // Use shared helper for vertex transformation
    applyBoneTransformsToMeshImpl(
        instanceMesh_,
        boneStates,
        vertexPieces,
        originalVertices,
        vertexMapping,
        eqMesh_->getBaseMesh(),
        cosRot,
        sinRot
    );

    boundingBox_ = instanceMesh_->getBoundingBox();

    // Update tracking for frame caching optimization
    lastAppliedFrame_ = currentFrame;
    lastAppliedRotY_ = nodeRot.Y;
}

void EQAnimatedMeshSceneNode::OnRegisterSceneNode() {
    // T309: THE FIRST-PERSON DRAW RULE'S ONE SEAM.
    //
    // withheldFromRenderList_ skips the DRAW and nothing else. It does not
    // touch IsVisible, deliberately: Irrlicht gates OnAnimate() on IsVisible,
    // and the draw rule needs this skeleton to keep running while it is
    // hidden so an attack animation is at the right phase the instant it
    // becomes visible (see first_person_body_draw.h; T296 learned this the
    // hard way). OnAnimate() is driven from ISceneManager::drawAll()'s
    // separate animation pass, not from registration, so a withheld node
    // still animates, still updates its absolute transform, and still drives
    // its children's transforms.
    //
    // The child recursion goes with it: the rule draws the whole model WITH
    // its equipped weapon, or none of it, and the equipment nodes are
    // ordinary children of this node.
    //
    // T300's note that "the player node registers like any other node in
    // first person, the clamp does the hiding" was wrong about the mechanism
    // -- T301 Leg B and T307 both refuted the clamp -- and is superseded here.
    if (withheldFromRenderList_) {
        return;
    }
    if (IsVisible) {
        SceneManager->registerNodeForRendering(this);
    }
    ISceneNode::OnRegisterSceneNode();
}

void EQAnimatedMeshSceneNode::render() {
    if (!instanceMesh_ || !SceneManager) return;

    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    if (!driver) return;

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

    IAnimatedMeshSceneNode::OnAnimate(timeMs);
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
    // T127: same missing balancing drop() as RaceModelLoader's node-factory
    // functions (animated_mesh_creation.cpp) -- newParent->addChild() already
    // grab()'d newNode; this factory must release its own `new`-acquired
    // reference before returning the raw pointer. Not currently reachable
    // from any call site (no caller of clone() exists in this codebase today)
    // but fixed alongside the live bug to avoid leaving the same defect for
    // the next caller.
    newNode->drop();

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

size_t EQAnimatedMeshSceneNode::getInstanceMeshIndexCount() const {
    if (!instanceMesh_) return 0;
    size_t total = 0;
    for (irr::u32 b = 0; b < instanceMesh_->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(b);
        if (mb) total += mb->getIndexCount();
    }
    return total;
}

bool EQAnimatedMeshSceneNode::getBoneWorldPosition(int boneIndex, irr::core::vector3df& outPosition) const {
    const auto& boneStates = animator_.getBoneStates();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(boneStates.size())) {
        return false;
    }

    const BoneMat4& worldXform = boneStates[boneIndex].worldTransform;
    float eqX = worldXform.m[12];
    float eqY = worldXform.m[13];
    float eqZ = worldXform.m[14];

    // Apply node scale and convert EQ(x,y,z) to Irrlicht(x,z,y)
    irr::core::vector3df nodePos = getAbsolutePosition();
    irr::core::vector3df nodeScale = getScale();
    irr::core::vector3df nodeRot = getRotation();

    float localX = eqX * nodeScale.X;
    float localY = eqZ * nodeScale.Z;  // Height: EQ Z -> Irrlicht Y
    float localZ = eqY * nodeScale.Y;  // Depth:  EQ Y -> Irrlicht Z

    // Apply Y-axis rotation (model facing direction)
    float rotY = nodeRot.Y * 3.14159265f / 180.0f;
    float cosY = std::cos(rotY);
    float sinY = std::sin(rotY);
    float rotatedX = localX * cosY + localZ * sinY;
    float rotatedZ = -localX * sinY + localZ * cosY;

    outPosition.X = rotatedX + nodePos.X;
    outPosition.Y = localY + nodePos.Y;
    outPosition.Z = rotatedZ + nodePos.Z;

    return true;
}

bool EQAnimatedMeshSceneNode::getBoneWorldDirection(int boneIndex, irr::core::vector3df& outDirection) const {
    if (!eqMesh_) return false;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return false;

    const auto& boneStates = animator_.getBoneStates();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(boneStates.size())) {
        return false;
    }

    // Get this bone's world position
    irr::core::vector3df bonePos;
    if (!getBoneWorldPosition(boneIndex, bonePos)) return false;

    // Walk up the bone hierarchy to find an ancestor with meaningful spatial separation.
    // Attachment points (R_POINT, L_POINT) are very close to their parent (hand bone),
    // so parent→child gives a tiny noisy vector. Walk up to forearm/upper arm level
    // where we get actual arm direction.
    static const float MIN_SEPARATION_SQ = 0.5f * 0.5f;  // Need at least 0.5 units of separation
    int ancestorIdx = skeleton->bones[boneIndex].parentIndex;
    int maxWalk = 4;  // Don't walk more than 4 levels
    while (ancestorIdx >= 0 && ancestorIdx < static_cast<int>(boneStates.size()) && maxWalk > 0) {
        irr::core::vector3df ancestorPos;
        if (getBoneWorldPosition(ancestorIdx, ancestorPos)) {
            irr::core::vector3df diff = bonePos - ancestorPos;
            float lenSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (lenSq >= MIN_SEPARATION_SQ) {
                float len = std::sqrt(lenSq);
                outDirection = diff / len;
                return true;
            }
        }
        ancestorIdx = skeleton->bones[ancestorIdx].parentIndex;
        --maxWalk;
    }

    // Fallback: direction from character center to bone (outward from body)
    irr::core::vector3df nodePos = getAbsolutePosition();
    outDirection = bonePos - nodePos;
    float len = outDirection.getLength();
    if (len > 0.0001f) {
        outDirection /= len;
    }
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

// T300: setFirstPersonMode(), applyFirstPersonIndexFilter() and
// buildArmBoneIndices() were DELETED here. T74 added them so first person
// could draw arm-bone triangles only (and hide the player entirely when a
// skeleton yielded no arm bones); T296 built the viewmodel pass on top of
// them. T299 measured the actual mechanism in two client versions: the
// official client draws the ORDINARY, unfiltered player model from an
// eye-point camera and hides it with a CLAMPED DOWN-PITCH (T300 Leg A: the
// client clips nothing) -- geometry reappears whenever an animation rotates
// it up into frame. There is
// therefore no geometry to select and no fallback to fall back to. Do not
// reintroduce a first-person triangle filter.
//
// T309: the rest of that sentence used to read "if the body shows, the
// down-pitch clamp is wrong". It is not the clamp. The body shows exactly
// while an ATTACK animation plays (first_person_body_draw.h), enforced by
// EntityRenderer::updateFirstPersonBodyDrawRule() withholding this node from
// the render list -- see setWithheldFromRenderList().

} // namespace Graphics
} // namespace EQT
