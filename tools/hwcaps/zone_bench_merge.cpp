// P09 Phase B: cross-region same-material zone draw merge prototype
// implementation. See zone_bench_merge.h for the chosen design + rationale.

#include "zone_bench_merge.h"
#include "zone_bench_loader.h"

#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace ZoneBench {

namespace {

// H02b: byte-identical copy of zone_bench_loader.cpp's textureGroupId (kept
// private/anonymous there) -- same duplication convention already used by
// production's renderer_zone_draw.cpp (see that function's header comment).
// Keep in sync with zone_bench_loader.cpp's copy if the hash ever changes.
uint32_t textureGroupId(const irr::video::SMaterial& mat) {
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}

constexpr int kMaxVertsPerBucket = 65535; // uint16 index space

// One "generation" of a per-texture merged buffer: a persistent
// world-space-baked vertex array (never touched again after
// buildMergedZoneBuffers) plus an immutable template index array that
// records, for each contributing region×buffer, a contiguous [offset,
// offset+count) slice to gather from at draw time.
struct MergedGeneration {
    irr::scene::SMeshBuffer* buf = nullptr;   // owns persistent Vertices;
                                               // Indices reused as scratch
                                               // for whichever run is being
                                               // drawn THIS call.
    std::vector<uint16_t> templateIndices;    // immutable gather source
};

struct RegionBufferRange {
    uint32_t texId;
    int generation;
    int indexOffset;
    int indexCount;
};

std::unordered_map<uint32_t, std::vector<MergedGeneration>> g_buckets;
std::unordered_map<const void*, RegionBufferRange> g_ranges;
bool g_built = false;

} // namespace

void buildMergedZoneBuffers(ZoneBenchData& data) {
    destroyMergedZoneBuffers();

    int skippedNonStandardBuffers = 0;
    int skippedRegions = 0;

    for (auto& [regionIdx, rd] : data.regions) {
        if (!rd.mesh) { ++skippedRegions; continue; }
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = rd.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Defensive: only EVT_STANDARD (S3DVertex) is safe to bake into
            // our persistent S3DVertex-typed merge buckets. buildTexturedMesh
            // / buildColoredMesh (the paths zone_bench_loader.cpp uses for
            // region geometry) always produce this format; any other type
            // just keeps drawing via the normal per-region path (see
            // renderMergedDrawEntries's run==1 / not-found fallback).
            if (buf->getVertexType() != irr::video::EVT_STANDARD) {
                ++skippedNonStandardBuffers;
                continue;
            }

            uint32_t texId = textureGroupId(buf->getMaterial());
            auto& gens = g_buckets[texId];
            if (gens.empty() || gens.back().buf->getVertexCount() + static_cast<int>(buf->getVertexCount()) > kMaxVertsPerBucket) {
                MergedGeneration g;
                g.buf = new irr::scene::SMeshBuffer();
                gens.push_back(g);
            }
            MergedGeneration& gen = gens.back();

            uint16_t vertexBase = static_cast<uint16_t>(gen.buf->getVertexCount());
            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vtx = srcVerts[v];
                // Bake the region's (translation-only) world transform into
                // the persistent vertex position, so the merged draw can use
                // an identity world transform (a single glDrawElements can't
                // carry more than one per-vertex world matrix).
                vtx.Pos += rd.position;
                gen.buf->Vertices.push_back(vtx);
            }

            int indexOffset = static_cast<int>(gen.templateIndices.size());
            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 ix = 0; ix < buf->getIndexCount(); ++ix) {
                gen.templateIndices.push_back(static_cast<uint16_t>(vertexBase + srcIdx[ix]));
            }
            int indexCount = static_cast<int>(buf->getIndexCount());

            g_ranges[buf] = RegionBufferRange{texId, static_cast<int>(gens.size()) - 1,
                                               indexOffset, indexCount};
        }
    }

    g_built = true;
    size_t totalBuckets = 0, totalGens = 0;
    for (auto& [texId, gens] : g_buckets) { totalBuckets++; totalGens += gens.size(); }
    printf("  [merge] built %zu texture buckets (%zu generations), %d regions skipped, "
           "%d non-EVT_STANDARD buffers skipped\n",
           totalBuckets, totalGens, skippedRegions, skippedNonStandardBuffers);
}

void destroyMergedZoneBuffers() {
    for (auto& [texId, gens] : g_buckets) {
        for (auto& gen : gens) {
            if (gen.buf) gen.buf->drop();
        }
    }
    g_buckets.clear();
    g_ranges.clear();
    g_built = false;
}

int renderMergedDrawEntries(ZoneBenchData& data,
                             irr::video::IVideoDriver* driver,
                             const std::vector<DrawEntry>& sortedEntries) {
    (void)data;
    if (!g_built || sortedEntries.empty()) return 0;

    irr::core::matrix4 identity;
    int drawCalls = 0;

    size_t i = 0;
    const size_t n = sortedEntries.size();
    while (i < n) {
        size_t j = i + 1;
        while (j < n && sortedEntries[j].materialKey == sortedEntries[i].materialKey) ++j;
        size_t runLen = j - i;

        if (runLen == 1) {
            // Unmerged path, byte-identical to the production/benchmark
            // baseline draw loop.
            const DrawEntry& de = sortedEntries[i];
            driver->setTransform(irr::video::ETS_WORLD, de.worldMat);
            if (de.materialOverride >= 0) {
                irr::video::SMaterial mat = de.buffer->getMaterial();
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
                driver->setMaterial(mat);
            } else {
                driver->setMaterial(de.buffer->getMaterial());
            }
            driver->drawMeshBuffer(de.buffer);
            ++drawCalls;
            ++i;
            continue;
        }

        // Try to coalesce the whole run into one draw. All entries share
        // .materialKey (texture identity + effective MaterialType already
        // folded in), so they also share the effective material to bind.
        // If any entry's buffer wasn't captured in g_ranges (skipped at
        // build time -- non-EVT_STANDARD, defensive fallback) or spans a
        // different bucket/generation than its neighbors expect (shouldn't
        // happen since materialKey encodes texId), fall back to drawing
        // that run unmerged rather than silently dropping geometry.
        bool allMergeable = true;
        uint32_t bucketTexId = 0;
        int bucketGen = -1;
        for (size_t k = i; k < j && allMergeable; ++k) {
            auto it = g_ranges.find(sortedEntries[k].buffer);
            if (it == g_ranges.end()) { allMergeable = false; break; }
            if (k == i) { bucketTexId = it->second.texId; bucketGen = it->second.generation; }
            else if (it->second.texId != bucketTexId || it->second.generation != bucketGen) {
                allMergeable = false;
            }
        }

        if (!allMergeable) {
            for (size_t k = i; k < j; ++k) {
                const DrawEntry& de = sortedEntries[k];
                driver->setTransform(irr::video::ETS_WORLD, de.worldMat);
                if (de.materialOverride >= 0) {
                    irr::video::SMaterial mat = de.buffer->getMaterial();
                    mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
                    driver->setMaterial(mat);
                } else {
                    driver->setMaterial(de.buffer->getMaterial());
                }
                driver->drawMeshBuffer(de.buffer);
                ++drawCalls;
            }
            i = j;
            continue;
        }

        MergedGeneration& gen = g_buckets[bucketTexId][bucketGen];

        if (getenv("P09_MERGE_DEBUG")) {
            printf("[merge-debug] run i=%zu len=%zu matKey=%u texId(bucket)=%u gen=%d\n",
                   i, runLen, sortedEntries[i].materialKey, bucketTexId, bucketGen);
            for (size_t k = i; k < j; ++k) {
                const irr::video::SMaterial& m = sortedEntries[k].buffer->getMaterial();
                const irr::video::ITexture* t = m.getTexture(0);
                printf("    k=%zu buf=%p tex=%p name=%s matType=%d param=%.3f param2=%.3f vtxType=%d idxCnt=%u\n",
                       k, (void*)sortedEntries[k].buffer, (void*)t,
                       t ? t->getName().getPath().c_str() : "(null)",
                       (int)m.MaterialType, m.MaterialTypeParam, m.MaterialTypeParam2,
                       (int)sortedEntries[k].buffer->getVertexType(),
                       sortedEntries[k].buffer->getIndexCount());
            }
        }

        // Per-frame gather: copy this run's index slices, in the same
        // front-to-back sorted order collectDrawEntries() already produced,
        // into the bucket's scratch Indices array.
        gen.buf->Indices.set_used(0);
        for (size_t k = i; k < j; ++k) {
            const RegionBufferRange& rr = g_ranges[sortedEntries[k].buffer];
            for (int off = rr.indexOffset; off < rr.indexOffset + rr.indexCount; ++off) {
                gen.buf->Indices.push_back(gen.templateIndices[off]);
            }
        }

        const DrawEntry& first = sortedEntries[i];
        irr::video::SMaterial mat = first.buffer->getMaterial();
        if (first.materialOverride >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(first.materialOverride);
        }
        gen.buf->Material = mat;

        driver->setTransform(irr::video::ETS_WORLD, identity);
        driver->setMaterial(mat);
        // gen.buf is never registered with the driver's HWBufferMap, so
        // drawMeshBuffer() takes the client-side-vertex-array fallback path
        // (same code path already used for entities/doors/UI) -- this is
        // exactly the "client-index" A-gate primitive.
        driver->drawMeshBuffer(gen.buf);
        ++drawCalls;
#ifdef EQT_HAS_GLES2
        if (getenv("P09_MERGE_DEBUG")) {
            GLenum err = glGetError();
            printf("[merge-debug] drew gen.buf verts=%u indices=%u glErr=0x%x samplePos=(%.2f,%.2f,%.2f) sampleUV=(%.3f,%.3f)\n",
                   gen.buf->getVertexCount(), gen.buf->getIndexCount(), err,
                   gen.buf->Vertices[0].Pos.X, gen.buf->Vertices[0].Pos.Y, gen.buf->Vertices[0].Pos.Z,
                   gen.buf->Vertices[0].TCoords.X, gen.buf->Vertices[0].TCoords.Y);
        }
#endif

        i = j;
    }

    return drawCalls;
}

} // namespace ZoneBench
