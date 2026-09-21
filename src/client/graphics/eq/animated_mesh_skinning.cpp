// CPU vertex skinning: bone transforms applied to a mesh's vertices.
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
void applyBoneTransformsToMeshImpl(
    irr::scene::SMesh* destMesh,
    const std::vector<AnimatedBoneState>& boneStates,
    const std::vector<VertexPiece>& vertexPieces,
    const std::vector<irr::video::S3DVertex>& originalVertices,
    const FlatVertexMapping& vertexMapping,
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
                const auto mappings = vertexMapping[vIdx];  // T504: a span into the flat store
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


} // namespace Graphics
} // namespace EQT
