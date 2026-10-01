#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/zone_stream/zone_stream_component.h"
#include "client/graphics/irrlicht_renderer.h"  // ZoneDrawEntry
#include "client/graphics/entity_renderer.h"    // EntityVisual, EQAnimatedMeshSceneNode
#include "client/graphics/door_manager.h"       // DoorVisual
#include "common/logging.h"
#include "client/core/gem_owner_census.h"  // B962

#include <irrlicht.h>

#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {

// H02b/Q02: byte-identical convention to renderer_zone_draw.cpp's own
// (anonymous-namespace, file-local) textureGroupId() -- and to
// tools/hwcaps/zone_bench_merge.cpp's copy. Keep all three in sync if the
// hash ever changes (documented duplication precedent, not an oversight).
uint32_t textureGroupId(const irr::video::SMaterial& mat) {
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}

constexpr int kMaxVertsPerBucket = 65535;  // uint16 index space

} // namespace

// One "generation" of a per-texture merged buffer: a persistent world-space-
// baked vertex array (never touched again after build()) plus an immutable
// template index array recording each contributing region-buffer's
// [offset, offset+count) slice to gather from at draw time.
struct ZoneMergeBuffers::MergedGeneration {
    irr::scene::SMeshBuffer* buf = nullptr;
    std::vector<uint16_t> templateIndices;
};

struct ZoneMergeBuffers::RegionBufferRange {
    uint32_t texId;
    int generation;
    int indexOffset;
    int indexCount;
};

struct ZoneMergeBuffers::PoolMaterials {
    std::vector<irr::video::SMaterial> list;
};

ZoneMergeBuffers::ZoneMergeBuffers() = default;
ZoneMergeBuffers::~ZoneMergeBuffers() { destroy(); }

void ZoneMergeBuffers::build(const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes,
                              const std::function<bool(irr::video::ITexture*)>& isTextureLive) {
    destroy();

    int skippedNodes = 0;
    int skippedNonStandardBuffers = 0;
    int skippedInvalidTextures = 0;

    for (const auto& [regionIdx, node] : regionMeshNodes) {
        if (!node) { ++skippedNodes; continue; }
        irr::scene::IMesh* mesh = node->getMesh();
        if (!mesh) { ++skippedNodes; continue; }

        const irr::core::vector3df regionPos = node->getPosition();

        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Defensive: only EVT_STANDARD (S3DVertex) is safe to bake into
            // our persistent S3DVertex-typed merge buckets -- matches the
            // format ZoneMeshBuilder::buildTexturedMesh/buildColoredMesh
            // actually produce for zone region geometry. Any other type is
            // simply left out of ranges_, so renderMerged() falls back to
            // drawing it unmerged (never observed to trigger on real zone
            // data, per the P09 prototype's own finding).
            if (buf->getVertexType() != irr::video::EVT_STANDARD) {
                ++skippedNonStandardBuffers;
                continue;
            }

            // P10b defensive check (see header comment + RegionMeshTextureGuard):
            // validate the texture BEFORE textureGroupId() ever dereferences
            // it. A non-null-but-invalid (freed) ITexture* is exactly the
            // qeynos2 SIGSEGV's mechanism -- skip this one buffer (falls
            // back to unmerged for this region's draw, never drops
            // geometry, never crashes) rather than trust the raw pointer.
            if (isTextureLive) {
                irr::video::ITexture* tex0 = buf->getMaterial().getTexture(0);
                if (tex0 && !isTextureLive(tex0)) {
                    ++skippedInvalidTextures;
                    continue;
                }
            }

            uint32_t texId = textureGroupId(buf->getMaterial());
            auto& gens = buckets_[texId];
            if (gens.empty() ||
                gens.back()->buf->getVertexCount() + static_cast<int>(buf->getVertexCount()) > kMaxVertsPerBucket) {
                auto* g = new MergedGeneration();
                g->buf = new irr::scene::SMeshBuffer();
                gens.push_back(g);
            }
            MergedGeneration* gen = gens.back();

            uint16_t vertexBase = static_cast<uint16_t>(gen->buf->getVertexCount());
            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vtx = srcVerts[v];
                // Bake the region's (translation-only) world position into
                // the persistent vertex, matching the P09/P09c-validated
                // design exactly -- a merged draw uses an identity world
                // transform. This is the architecturally-inherent
                // FP-transform-order delta P09c quantified and Will
                // reviewed (baked-position + identity transform vs.
                // per-region world matrix + local position -- mathematically
                // equivalent, not bit-identical in float32).
                vtx.Pos += regionPos;
                gen->buf->Vertices.push_back(vtx);
            }

            int indexOffset = static_cast<int>(gen->templateIndices.size());
            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 ix = 0; ix < buf->getIndexCount(); ++ix) {
                gen->templateIndices.push_back(static_cast<uint16_t>(vertexBase + srcIdx[ix]));
            }
            int indexCount = static_cast<int>(buf->getIndexCount());

            ranges_[buf] = RegionBufferRange{texId, static_cast<int>(gens.size()) - 1,
                                              indexOffset, indexCount};
        }
    }

    totalBytes_ = 0;
    size_t totalBuckets = buckets_.size();
    size_t totalGens = 0;
    for (auto& [texId, gens] : buckets_) {
        for (auto* gen : gens) {
            totalBytes_ += static_cast<size_t>(gen->buf->getVertexCount()) * sizeof(irr::video::S3DVertex);
            totalBytes_ += gen->templateIndices.size() * sizeof(uint16_t);
        }
        totalGens += gens.size();
    }

    built_ = true;
    if (skippedInvalidTextures > 0) {
        LOG_WARN(MOD_GRAPHICS, "[mergedraws] {} buffer(s) skipped for an invalid/stale texture "
                 "(see RegionMeshTextureGuard/P10b) -- those regions fall back to unmerged draw",
                 skippedInvalidTextures);
    }
    LOG_INFO(MOD_GRAPHICS, "[mergedraws] built {} texture buckets ({} generations, {} bytes), "
             "{} nodes skipped, {} non-EVT_STANDARD buffers skipped, {} invalid-texture buffers skipped",
             totalBuckets, totalGens, totalBytes_, skippedNodes, skippedNonStandardBuffers,
             skippedInvalidTextures);
}

void ZoneMergeBuffers::destroy() {
    for (auto& [texId, gens] : buckets_) {
        for (auto* gen : gens) {
            if (gen->buf) gen->buf->drop();
            delete gen;
        }
    }
    buckets_.clear();
    ranges_.clear();
    for (auto& gens : poolBuckets_) {
        for (auto* gen : gens) {
            if (gen->buf) gen->buf->drop();
            delete gen;
        }
    }
    poolBuckets_.clear();
    std::vector<ZoneSlice>().swap(slices_);
    std::vector<RegionSpan>().swap(regionSpans_);
    delete poolMaterials_;
    poolMaterials_ = nullptr;
    pooled_ = false;
    built_ = false;
    totalBytes_ = 0;
}

// ---------------------------------------------------------------------------
// T504 POOLED mode (see the header). The per-region meshes never outlive
// appendRegion(): their vertices are baked to world space into the pool of
// their MATERIAL, their indices into that pool's template array, and one
// 16-byte ZoneSlice remembers where.
// ---------------------------------------------------------------------------
void ZoneMergeBuffers::beginPooled() {
    destroy();
    pooled_ = true;
    poolMaterials_ = new PoolMaterials();
}

int ZoneMergeBuffers::appendRegion(size_t regionIdx, irr::scene::IMesh* mesh, float px, float py, float pz) {
    if (!pooled_ || !mesh) return 0;
    const irr::core::vector3df regionPos(px, py, pz);
    const uint32_t firstSlice = static_cast<uint32_t>(slices_.size());

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0) continue;
        if (buf->getVertexType() != irr::video::EVT_STANDARD) {
            LOG_WARN(MOD_GRAPHICS, "[zonepools] region {} buffer {} is not EVT_STANDARD -- not drawn", regionIdx, i);
            continue;
        }
        if (buf->getVertexCount() > static_cast<irr::u32>(kMaxVertsPerBucket)) {
            LOG_WARN(MOD_GRAPHICS, "[zonepools] region {} buffer {} has {} vertices (> u16 index space) -- not drawn",
                     regionIdx, i, buf->getVertexCount());
            continue;
        }

        // Material table: a few dozen distinct materials per zone, so a linear
        // search by value is cheap and keeps the table minimal.
        const irr::video::SMaterial& mat = buf->getMaterial();
        auto& mats = poolMaterials_->list;
        size_t materialId = 0;
        for (; materialId < mats.size(); ++materialId) {
            if (mats[materialId] == mat) break;
        }
        if (materialId == mats.size()) {
            if (mats.size() >= 65535) continue;  // uint16 id space; never reached in practice
            mats.push_back(mat);
            poolBuckets_.emplace_back();
        }

        auto& gens = poolBuckets_[materialId];
        if (gens.empty() ||
            gens.back()->buf->getVertexCount() + buf->getVertexCount() > static_cast<irr::u32>(kMaxVertsPerBucket)) {
            auto* g = new MergedGeneration();
            g->buf = new irr::scene::SMeshBuffer();
            gens.push_back(g);
        }
        MergedGeneration* gen = gens.back();

        const uint16_t vertexBase = static_cast<uint16_t>(gen->buf->getVertexCount());
        const irr::video::S3DVertex* srcVerts = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
            irr::video::S3DVertex vtx = srcVerts[v];
            vtx.Pos += regionPos;
            gen->buf->Vertices.push_back(vtx);
        }
        const int32_t indexOffset = static_cast<int32_t>(gen->templateIndices.size());
        const irr::u16* srcIdx = buf->getIndices();
        for (irr::u32 ix = 0; ix < buf->getIndexCount(); ++ix) {
            gen->templateIndices.push_back(static_cast<uint16_t>(vertexBase + srcIdx[ix]));
        }

        slices_.push_back(ZoneSlice{static_cast<uint32_t>(regionIdx), static_cast<uint16_t>(materialId),
                                     static_cast<uint16_t>(gens.size() - 1), indexOffset,
                                     static_cast<int32_t>(buf->getIndexCount())});
    }

    const uint32_t added = static_cast<uint32_t>(slices_.size()) - firstSlice;
    if (added > 0) {
        regionSpans_.push_back(RegionSpan{static_cast<uint32_t>(regionIdx), firstSlice, added});
    }
    return static_cast<int>(added);
}

void ZoneMergeBuffers::finishPooled() {
    if (!pooled_) return;
    // appendRegion() is called in ascending region order by the load loop;
    // sort anyway so slicesForRegion()'s binary search is a guarantee.
    std::sort(regionSpans_.begin(), regionSpans_.end(),
              [](const RegionSpan& a, const RegionSpan& b) { return a.regionIdx < b.regionIdx; });
    slices_.shrink_to_fit();
    regionSpans_.shrink_to_fit();

    totalBytes_ = 0;
    size_t totalGens = 0, totalVerts = 0;
    for (auto& gens : poolBuckets_) {
        for (auto* gen : gens) {
            gen->templateIndices.shrink_to_fit();
            totalVerts += gen->buf->getVertexCount();
            totalBytes_ += static_cast<size_t>(gen->buf->getVertexCount()) * sizeof(irr::video::S3DVertex);
            totalBytes_ += gen->templateIndices.size() * sizeof(uint16_t);
        }
        totalGens += gens.size();
    }
    totalBytes_ += slices_.size() * sizeof(ZoneSlice) + regionSpans_.size() * sizeof(RegionSpan);
    built_ = true;
    LOG_INFO(MOD_GRAPHICS, "[zonepools] built: {} regions, {} slices, {} materials, {} generations, {} vertices, {} bytes",
             regionSpans_.size(), slices_.size(), poolMaterials_->list.size(), totalGens, totalVerts, totalBytes_);
}

const ZoneSlice* ZoneMergeBuffers::slicesForRegion(size_t regionIdx, size_t& countOut) const {
    countOut = 0;
    if (!pooled_ || regionSpans_.empty()) return nullptr;
    const uint32_t key = static_cast<uint32_t>(regionIdx);
    auto it = std::lower_bound(regionSpans_.begin(), regionSpans_.end(), key,
                               [](const RegionSpan& s, uint32_t k) { return s.regionIdx < k; });
    if (it == regionSpans_.end() || it->regionIdx != key) return nullptr;
    countOut = it->count;
    return slices_.data() + it->first;
}

size_t ZoneMergeBuffers::materialCount() const {
    return poolMaterials_ ? poolMaterials_->list.size() : 0;
}

irr::video::SMaterial& ZoneMergeBuffers::material(size_t id) {
    return poolMaterials_->list[id];
}

int ZoneMergeBuffers::swapTexture(irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!poolMaterials_ || !from) return 0;
    int changed = 0;
    for (auto& mat : poolMaterials_->list) {
        changed += swapTextureInMaterial(mat, from, to);
    }
    return changed;
}

void ZoneMergeBuffers::forEachRegionTriangle(
        size_t regionIdx, const std::function<void(const float*, const float*, const float*)>& fn) const {
    size_t n = 0;
    const ZoneSlice* s = slicesForRegion(regionIdx, n);
    for (size_t k = 0; k < n; ++k) {
        const MergedGeneration* gen = poolBuckets_[s[k].materialId][s[k].generation];
        const irr::video::S3DVertex* verts = static_cast<const irr::video::S3DVertex*>(gen->buf->getVertices());
        for (int32_t off = s[k].indexOffset; off + 2 < s[k].indexOffset + s[k].indexCount; off += 3) {
            const irr::core::vector3df& a = verts[gen->templateIndices[off]].Pos;
            const irr::core::vector3df& b = verts[gen->templateIndices[off + 1]].Pos;
            const irr::core::vector3df& c = verts[gen->templateIndices[off + 2]].Pos;
            fn(&a.X, &b.X, &c.X);
        }
    }
}

size_t ZoneMergeBuffers::regionRuns(size_t regionIdx, size_t first, TriangleRun* out,
                                    size_t maxOut) const {
    size_t n = 0;
    const ZoneSlice* s = slicesForRegion(regionIdx, n);
    size_t written = 0;
    for (size_t k = first; k < n && written < maxOut; ++k) {
        const MergedGeneration* gen = poolBuckets_[s[k].materialId][s[k].generation];
        TriangleRun& r = out[written++];
        r.vertices = static_cast<const irr::video::S3DVertex*>(gen->buf->getVertices());
        r.indices = gen->templateIndices.data() + s[k].indexOffset;
        r.indexCount = s[k].indexCount > 0 ? static_cast<size_t>(s[k].indexCount) : 0;
    }
    return written;
}

std::vector<size_t> ZoneMergeBuffers::regionsWithTriangles() const {
    // A region counts iff forEachRegionTriangle() would call its fn at least
    // once -- the same test the pre-B909 copy applied (it kept a region only
    // when its triangle vector was non-empty).
    std::vector<size_t> out;
    out.reserve(regionSpans_.size());
    for (const RegionSpan& span : regionSpans_) {
        for (uint32_t k = 0; k < span.count; ++k) {
            if (slices_[span.first + k].indexCount >= 3) {
                out.push_back(span.regionIdx);
                break;
            }
        }
    }
    return out;
}

int ZoneMergeBuffers::renderMerged(irr::video::IVideoDriver* driver,
                                    const std::vector<ZoneDrawEntry>& sortedEntries) {
    if (!built_ || sortedEntries.empty() || !driver) return 0;
    eqt::memory::GemTag gemTag("draw:zone");  // B962: GEM census owner

    irr::core::matrix4 identity;
    int drawCalls = 0;

    if (pooled_) {
        // T504: every entry carries a slice. A run is a maximal stretch of equal
        // materialKey + materialId (the list is sorted by materialKey); within a
        // run, each generation that appears is gathered and drawn once. There is
        // no unmerged path: a single slice is just a gather of one.
        const size_t total = sortedEntries.size();
        size_t a = 0;
        std::vector<uint16_t> gensInRun;
        while (a < total) {
            const ZoneDrawEntry& first = sortedEntries[a];
            if (!first.slice) {
                // Not zone geometry (placeable objects share this list): draw
                // the mesh buffer itself, exactly as the unmerged path does.
                if (first.buffer) {
                    driver->setTransform(irr::video::ETS_WORLD, first.worldMat);
                    if (first.materialOverride >= 0) {
                        irr::video::SMaterial om = first.buffer->getMaterial();
                        om.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(first.materialOverride);
                        driver->setMaterial(om);
                    } else {
                        driver->setMaterial(first.buffer->getMaterial());
                    }
                    driver->drawMeshBuffer(first.buffer);
                    ++drawCalls;
                }
                ++a;
                continue;
            }
            size_t b = a + 1;
            while (b < total && sortedEntries[b].slice &&
                   sortedEntries[b].materialKey == first.materialKey &&
                   sortedEntries[b].slice->materialId == first.slice->materialId) {
                ++b;
            }
            gensInRun.clear();
            for (size_t k = a; k < b; ++k) {
                const uint16_t g = sortedEntries[k].slice->generation;
                if (std::find(gensInRun.begin(), gensInRun.end(), g) == gensInRun.end()) gensInRun.push_back(g);
            }
            irr::video::SMaterial mat = poolMaterials_->list[first.slice->materialId];
            if (first.materialOverride >= 0) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(first.materialOverride);
            }
            for (uint16_t g : gensInRun) {
                MergedGeneration* gen = poolBuckets_[first.slice->materialId][g];
                gen->buf->Indices.set_used(0);
                for (size_t k = a; k < b; ++k) {
                    const ZoneSlice* sl = sortedEntries[k].slice;
                    if (sl->generation != g) continue;
                    for (int32_t off = sl->indexOffset; off < sl->indexOffset + sl->indexCount; ++off) {
                        gen->buf->Indices.push_back(gen->templateIndices[off]);
                    }
                }
                gen->buf->Material = mat;
                driver->setTransform(irr::video::ETS_WORLD, identity);
                driver->setMaterial(mat);
                driver->drawMeshBuffer(gen->buf);
                ++drawCalls;
            }
            a = b;
        }
        return drawCalls;
    }
    const size_t n = sortedEntries.size();
    size_t i = 0;

    // P09b root-cause fix, reproduced exactly: group runs by FULL SMaterial
    // equality (Irrlicht's own operator==, a complete field walk -- every
    // TextureLayer's texture/wrap/filter/matrix, Lighting, BackfaceCulling,
    // ZBuffer, ZWriteEnable, colors, FogEnable, ...), not just materialKey
    // (which only folds effective MaterialType + texture identity). This can
    // only SHRINK runs relative to materialKey-only grouping, never grow
    // them -- strictly a correctness hardening, costs nothing when materials
    // already match (the common case).
    while (i < n) {
        size_t j = i + 1;
        const irr::video::SMaterial& runMat = sortedEntries[i].buffer->getMaterial();
        while (j < n && sortedEntries[j].materialKey == sortedEntries[i].materialKey &&
               sortedEntries[j].buffer->getMaterial() == runMat) {
            ++j;
        }
        size_t runLen = j - i;

        auto drawUnmergedRange = [&](size_t lo, size_t hi) {
            for (size_t k = lo; k < hi; ++k) {
                const ZoneDrawEntry& de = sortedEntries[k];
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
        };

        if (runLen == 1) {
            drawUnmergedRange(i, j);
            i = j;
            continue;
        }

        // All entries in the run must map to the SAME merge bucket/generation
        // (should always hold since materialKey encodes texture identity, but
        // any buffer not captured at build() time -- non-EVT_STANDARD, or
        // simply not part of the region set build() saw, e.g. an entry that
        // slipped in after a rebuild -- falls back to unmerged rather than
        // silently dropping geometry).
        bool allMergeable = true;
        uint32_t bucketTexId = 0;
        int bucketGen = -1;
        for (size_t k = i; k < j && allMergeable; ++k) {
            auto it = ranges_.find(sortedEntries[k].buffer);
            if (it == ranges_.end()) { allMergeable = false; break; }
            if (k == i) { bucketTexId = it->second.texId; bucketGen = it->second.generation; }
            else if (it->second.texId != bucketTexId || it->second.generation != bucketGen) {
                allMergeable = false;
            }
        }

        if (!allMergeable) {
            drawUnmergedRange(i, j);
            i = j;
            continue;
        }

        MergedGeneration* gen = buckets_[bucketTexId][bucketGen];

        // Per-frame gather: copy this run's index slices in EXACTLY the same
        // order sortedEntries is walked (the same order the runLen==1 path
        // above draws these same entries) -- same triangles, same order,
        // same state (guaranteed by the operator== split above) =>
        // rasterization-order-identical (P09b fix item 7).
        gen->buf->Indices.set_used(0);
        for (size_t k = i; k < j; ++k) {
            const RegionBufferRange& rr = ranges_[sortedEntries[k].buffer];
            for (int off = rr.indexOffset; off < rr.indexOffset + rr.indexCount; ++off) {
                gen->buf->Indices.push_back(gen->templateIndices[off]);
            }
        }

        const ZoneDrawEntry& first = sortedEntries[i];
        irr::video::SMaterial mat = first.buffer->getMaterial();
        if (first.materialOverride >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(first.materialOverride);
        }
        gen->buf->Material = mat;

        driver->setTransform(irr::video::ETS_WORLD, identity);
        driver->setMaterial(mat);
        // gen->buf is never registered with the driver's HWBufferMap, so
        // drawMeshBuffer() takes the client-side-vertex-array fallback path
        // (same path already used for entities/doors/UI) -- the production
        // instance of the P09 A-gate's "client-index" primitive.
        driver->drawMeshBuffer(gen->buf);
        ++drawCalls;

        i = j;
    }

    return drawCalls;
}

// ---------------------------------------------------------------------------
// T124: shared eviction-swap primitives (see header comment for the full
// rationale) -- the ONE loop every guard below composes instead of
// re-implementing.
// ---------------------------------------------------------------------------

int swapTextureInMaterial(irr::video::SMaterial& mat, irr::video::ITexture* from, irr::video::ITexture* to) {
    int swapped = 0;
    for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
        if (mat.getTexture(t) == from) {
            mat.setTexture(t, to);
            ++swapped;
        }
    }
    return swapped;
}

int swapTextureInMesh(irr::scene::IMesh* mesh, irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!mesh) return 0;
    int swapped = 0;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf) continue;
        swapped += swapTextureInMaterial(buf->getMaterial(), from, to);
    }
    return swapped;
}

int swapTextureInNodeMaterials(irr::scene::ISceneNode* node, irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!node) return 0;
    int swapped = 0;
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        swapped += swapTextureInMaterial(node->getMaterial(i), from, to);
    }
    return swapped;
}

int swapTextureInMeshSceneNode(irr::scene::IMeshSceneNode* node, irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!node) return 0;
    // 1. Mesh-buffer materials. 2. The node's own override materials array
    // (ISceneNode::Materials, what render() actually reads for the default
    // ReadOnlyMaterials=false -- see ObjectNodeTextureGuard's doc comment
    // in the header for the T123 forensics establishing this).
    return swapTextureInMesh(node->getMesh(), from, to) + swapTextureInNodeMaterials(node, from, to);
}

// ---------------------------------------------------------------------------
// RegionMeshTextureGuard -- P10b root-cause fix (see header comment)
// ---------------------------------------------------------------------------

RegionMeshTextureGuard::RegionMeshTextureGuard(
    const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes,
    irr::video::ITexture* placeholder)
    : regionMeshNodes_(regionMeshNodes), placeholder_(placeholder) {}

void RegionMeshTextureGuard::onTextureEvicted(const std::string& name,
                                               irr::video::ITexture* texture) {
    if (!texture) return;

    // MUST NOT call back into ConstrainedTextureCache here (mutex_ is held
    // by the calling evictTexture()/evictUntilAvailable()/uploadTexture()
    // frame, non-recursive -- see header comment). placeholder_ was
    // resolved once, safely, at construction time.
    irr::video::ITexture* placeholder = placeholder_;
    int swapped = 0;
    if (poolSwap_) swapped += poolSwap_(texture, placeholder);  // T504: pooled zone materials

    // T124: migrated onto swapTextureInMesh() -- behaviour-preserving,
    // this guard only ever scanned mesh-buffer materials (region mesh
    // nodes carry no node-level override materials of their own), so this
    // is exactly the same scan as before, just de-duplicated.
    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (!node) continue;
        swapped += swapTextureInMesh(node->getMesh(), texture, placeholder);
    }

    if (swapped > 0) {
        // T226: this is the user-visible magenta/dark-grey checkerboard in
        // ZONE geometry (T220's qeynos2 "ceiling patch"). A region mesh of
        // the CURRENT zone still references the texture, so the constrained
        // texture budget was exceeded by the zone's own working set and the
        // LRU gave back something that is still on screen. It used to be
        // whispered at DEBUG, under a "[mergedraws]" tag that says nothing
        // about a rendering defect; name the texture and say what it looks
        // like, at WARN, so a normal-log-level run reports it.
        // B944: since GitLab #262 this fires only when the texture is really
        // DESTROYED -- an eviction the cache can restore keeps the object and
        // never reaches a listener -- so this is an eviction with no restore
        // source (or clear()/replacement), and it stays until a reload.
        LOG_WARN(MOD_GRAPHICS, "RegionMeshTextureGuard: destroyed texture '{}' was still referenced by {} "
                 "live region-mesh material(s) of the current zone -> replaced with the placeholder "
                 "checkerboard (magenta/dark-grey). The cache evicted it with no source to restore it "
                 "from; the geometry using '{}' now renders the placeholder.", name, swapped, name);
    }
}

// ---------------------------------------------------------------------------
// EntityMeshTextureGuard — P17 (entity-mesh sibling of RegionMeshTextureGuard)
// ---------------------------------------------------------------------------

EntityMeshTextureGuard::EntityMeshTextureGuard(const std::map<uint16_t, EntityVisual>& entities,
                                                irr::video::ITexture* placeholder)
    : entities_(entities), placeholder_(placeholder) {}

void EntityMeshTextureGuard::onTextureEvicted(const std::string& /*name*/,
                                               irr::video::ITexture* texture) {
    if (!texture) return;

    // Same load-bearing constraint as RegionMeshTextureGuard: MUST NOT call
    // back into ConstrainedTextureCache here (mutex_ is held by the calling
    // frame). placeholder_ was resolved once, safely, at construction time.
    irr::video::ITexture* placeholder = placeholder_;
    int swapped = 0;

    // T124: migrated onto the shared primitives -- behaviour-preserving.
    // EntityMeshTextureGuard's two-part scan doesn't match
    // swapTextureInMeshSceneNode() (EQAnimatedMeshSceneNode isn't an
    // IMeshSceneNode and its second material source is getInstanceMesh(),
    // not getMesh()), but each half maps onto one of the two lower-level
    // primitives exactly.
    //
    // T126: added a THIRD scan target, visual.primaryEquipNode/
    // secondaryEquipNode (right-hand/left-hand weapon-and-shield meshes,
    // EntityRenderer::attachEquipment()). These are real IMeshSceneNode
    // instances attached as children of the entity's own scene node --
    // unlike the animated body above, they were never scanned by this guard
    // at all despite T123's holder-inventory table claiming "equipment" was
    // covered under "Entity/character + equipment". Confirmed the dominant
    // still-open UAF holder via T126's write-time return-address probe: 118
    // of 118 UAF TEXPARAMS writes across a 10-run acceptance gate resolved
    // to the IDENTICAL call chain applyMaterialRenderState() <-
    // applyCustomShaderState() <- applyMaterialState() <- setMaterial() <-
    // CMeshSceneNode::render()+1122 <- CSceneManager::drawAll() -- i.e. a
    // live, attached, normally-rendered IMeshSceneNode, not a
    // detached/hidden one (the shape every earlier guard in this arc
    // assumed). See refactor/T126_uaf_attribution.md.
    for (const auto& [spawnId, visual] : entities_) {
        EQAnimatedMeshSceneNode* node = visual.animatedNode;
        if (node) {
            // 1. The node's own materials_ vector (what render() actually
            //    binds at draw time — see class doc comment in
            //    renderer_zone_merge.h).
            swapped += swapTextureInNodeMaterials(node, texture, placeholder);

            // 2. The per-instance mesh buffers' own materials (e.g.
            //    attached equipment pieces as separate buffers).
            swapped += swapTextureInMesh(node->getInstanceMesh(), texture, placeholder);
        }

        // 3. T126: the actual attached weapon/shield IMeshSceneNode
        //    instances (mesh-buffer materials AND per-node override
        //    materials -- swapTextureInMeshSceneNode() covers both, same as
        //    ObjectNodeTextureGuard/DoorTextureGuard do for their nodes).
        if (visual.primaryEquipNode) {
            swapped += swapTextureInMeshSceneNode(visual.primaryEquipNode, texture, placeholder);
        }
        if (visual.secondaryEquipNode) {
            swapped += swapTextureInMeshSceneNode(visual.secondaryEquipNode, texture, placeholder);
        }
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[entitytex] EntityMeshTextureGuard: swapped {} entity-mesh material "
                  "reference(s) to evicted texture -> placeholder (P17 — closes the entity-mesh gap "
                  "RegionMeshTextureGuard's own doc comment already flagged: region meshes were "
                  "covered by P10b, entity meshes were not; T126 — closes the equip-node gap this "
                  "guard's own scan never actually covered despite T123's inventory claiming it did)",
                  swapped);
    }
}

// ---------------------------------------------------------------------------
// ObjectNodeTextureGuard — T123 (object-node sibling of the two guards above)
// ---------------------------------------------------------------------------

ObjectNodeTextureGuard::ObjectNodeTextureGuard(
    const std::vector<irr::scene::IMeshSceneNode*>& objectNodes,
    irr::video::ITexture* placeholder)
    : objectNodes_(objectNodes), placeholder_(placeholder) {}

void ObjectNodeTextureGuard::onTextureEvicted(const std::string& /*name*/,
                                               irr::video::ITexture* texture) {
    if (!texture) return;

    // Same load-bearing constraint as the other two guards: MUST NOT call
    // back into ConstrainedTextureCache here. placeholder_ was resolved
    // once, safely, at construction time.
    irr::video::ITexture* placeholder = placeholder_;
    int swapped = 0;

    // T124: migrated onto swapTextureInMeshSceneNode() -- behaviour-
    // preserving, this is byte-for-byte the same two-part scan (mesh-buffer
    // materials, mirrors RegionMeshTextureGuard; node-level override
    // materials, what ConstrainedTextureCache::clearTextureReferences()
    // already checks for reachable nodes -- T123 confirmed via live core
    // dump this is the holder type an observed UAF write traced back to),
    // just de-duplicated into the shared primitive.
    for (irr::scene::IMeshSceneNode* node : objectNodes_) {
        swapped += swapTextureInMeshSceneNode(node, texture, placeholder);
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[objecttex] ObjectNodeTextureGuard: swapped {} object-node material "
                  "reference(s) to evicted texture -> placeholder (T123 — closes the detached-object-node "
                  "gap: applySimulationOutput() detaches invisible objectNodes_ entries from the scene "
                  "graph, making clearTextureReferences()'s scene walk unable to reach them while hidden)",
                  swapped);
    }
}

// ---------------------------------------------------------------------------
// DoorTextureGuard — T124 (doors_ sibling of ObjectNodeTextureGuard, see
// header comment for the confirmed detach/reattach mechanism)
// ---------------------------------------------------------------------------

DoorTextureGuard::DoorTextureGuard(const std::map<uint8_t, DoorVisual>& doors,
                                    irr::video::ITexture* placeholder)
    : doors_(doors), placeholder_(placeholder) {}

void DoorTextureGuard::onTextureEvicted(const std::string& /*name*/,
                                         irr::video::ITexture* texture) {
    if (!texture) return;

    // Same load-bearing constraint as every guard above: MUST NOT call
    // back into ConstrainedTextureCache here. placeholder_ was resolved
    // once, safely, at construction time.
    irr::video::ITexture* placeholder = placeholder_;
    int swapped = 0;

    for (const auto& [doorId, visual] : doors_) {
        // The door's actual mesh/materials live on sceneNode (an
        // IMeshSceneNode child of pivotNode) — pivotNode itself is an
        // addEmptySceneNode() with no mesh and (in practice) no materials
        // of its own, so scanning sceneNode covers everything doors_ can
        // hold. sceneNode is a child of pivotNode either way, so when
        // pivotNode is detached (setDoorState()/update()/
        // rebuildSingleDoor()'s PVS-driven remove()/addChild() cycle,
        // matching applySimulationOutput()'s objectNodes_ pattern exactly),
        // sceneNode goes with it and clearTextureReferences()'s
        // root-down scene walk cannot reach it either way — scanning
        // doors_ directly bypasses that reachability question entirely,
        // same as ObjectNodeTextureGuard.
        swapped += swapTextureInMeshSceneNode(visual.sceneNode, texture, placeholder);
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[doortex] DoorTextureGuard: swapped {} door material reference(s) to "
                  "evicted texture -> placeholder (T124 — closes the same detached-node gap "
                  "ObjectNodeTextureGuard fixed for objectNodes_, for door pivotNode/sceneNode pairs "
                  "detached during PVS-driven visibility updates)", swapped);
    }
}

// L08: out-of-line so DoorTextureGuard/ForwardingTextureGuard are complete
// here rather than in every consumer of irrlicht_renderer.h (see the component
// header's forward-declaration note).
ZoneStreamComponent::ZoneStreamComponent(RendererContext& ctx, IrrlichtRenderer& owner)
    : ctx_(ctx), owner_(owner) {}

ZoneStreamComponent::~ZoneStreamComponent() = default;

// T124: shutdown()-time unregister for objectNodeTextureGuard_ (T123 --
// registered in initLoadingScreen() but never unregistered before this
// unit) and the four T124 guards. Defined here, not irrlicht_renderer.cpp
// (which was at the check_file_sizes.sh budget when this was written).
void ZoneStreamComponent::unregisterRemainingT124Guards() {
    unregisterGuard(ctx_.constrainedTextureCache, objectNodeTextureGuard_);
    unregisterGuard(ctx_.constrainedTextureCache, doorTextureGuard_);
    unregisterGuard(ctx_.constrainedTextureCache, spellVisualFXTextureGuard_);
    unregisterGuard(ctx_.constrainedTextureCache, boidsTextureGuard_);
}

// T124: defined here (not irrlicht_renderer.cpp, which was at the
// check_file_sizes.sh budget when this was written). T468 (board B590): called
// from ZoneLifecycleComponent::ensureDoorManager(), the one DoorManager
// construction body. Its previous only caller was the uncalled
// loadGlobalAssets(), so this guard was never registered before T468. See
// DoorTextureGuard's doc comment (header) for the gap closed.
void ZoneStreamComponent::ensureDoorTextureGuard() {
    if (doorTextureGuard_ || !ctx_.constrainedTextureCache || !ctx_.doorManager) return;
    doorTextureGuard_ = std::make_unique<DoorTextureGuard>(
        ctx_.doorManager->getDoors(), ctx_.constrainedTextureCache->getPlaceholderTexture());
    ctx_.constrainedTextureCache->addEvictionListener(doorTextureGuard_.get());
}

// T124: idempotent, safe to call from either of boidsManager_'s two
// lazy-construction sites (renderer_init.cpp, renderer_hud_env.cpp). See BoidsManager::
// swapEvictedTexture()'s doc comment for the gap closed.
void ZoneStreamComponent::ensureBoidsTextureGuard() {
    if (boidsTextureGuard_ || !ctx_.constrainedTextureCache || !ctx_.boidsManager) return;
    Environment::BoidsManager* boids = ctx_.boidsManager.get();
    boidsTextureGuard_ = std::make_unique<ForwardingTextureGuard>(
        [boids](irr::video::ITexture* from, irr::video::ITexture* to) { boids->swapEvictedTexture(from, to); },
        ctx_.constrainedTextureCache->getPlaceholderTexture());
    ctx_.constrainedTextureCache->addEvictionListener(boidsTextureGuard_.get());
}

} // namespace Graphics
} // namespace EQT
