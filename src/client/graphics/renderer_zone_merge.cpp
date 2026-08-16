#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/irrlicht_renderer.h"  // ZoneDrawEntry
#include "client/graphics/entity_renderer.h"    // EntityVisual, EQAnimatedMeshSceneNode
#include "common/logging.h"

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
    built_ = false;
    totalBytes_ = 0;
}

int ZoneMergeBuffers::renderMerged(irr::video::IVideoDriver* driver,
                                    const std::vector<ZoneDrawEntry>& sortedEntries) {
    if (!built_ || sortedEntries.empty() || !driver) return 0;

    irr::core::matrix4 identity;
    int drawCalls = 0;
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
// RegionMeshTextureGuard -- P10b root-cause fix (see header comment)
// ---------------------------------------------------------------------------

RegionMeshTextureGuard::RegionMeshTextureGuard(
    const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes,
    irr::video::ITexture* placeholder)
    : regionMeshNodes_(regionMeshNodes), placeholder_(placeholder) {}

void RegionMeshTextureGuard::onTextureEvicted(const std::string& /*name*/,
                                               irr::video::ITexture* texture) {
    if (!texture) return;

    // MUST NOT call back into ConstrainedTextureCache here (mutex_ is held
    // by the calling evictTexture()/evictUntilAvailable()/uploadTexture()
    // frame, non-recursive -- see header comment). placeholder_ was
    // resolved once, safely, at construction time.
    irr::video::ITexture* placeholder = placeholder_;
    int swapped = 0;

    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (!node) continue;
        irr::scene::IMesh* mesh = node->getMesh();
        if (!mesh) continue;

        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
            if (!buf) continue;

            irr::video::SMaterial& mat = buf->getMaterial();
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                if (mat.getTexture(t) == texture) {
                    mat.setTexture(t, placeholder);
                    ++swapped;
                }
            }
        }
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[mergedraws] RegionMeshTextureGuard: swapped {} region-mesh material "
                  "reference(s) to evicted texture -> placeholder (covers nodes detached from the "
                  "scene graph for front-to-back manual draw, which clearTextureReferences() cannot "
                  "reach)", swapped);
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

    for (const auto& [spawnId, visual] : entities_) {
        EQAnimatedMeshSceneNode* node = visual.animatedNode;
        if (!node) continue;

        // 1. The node's own materials_ vector (what render() actually binds
        //    at draw time — see class doc comment in renderer_zone_merge.h).
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                if (mat.getTexture(t) == texture) {
                    mat.setTexture(t, placeholder);
                    ++swapped;
                }
            }
        }

        // 2. The per-instance mesh buffers' own materials (e.g. attached
        //    equipment pieces as separate buffers).
        if (irr::scene::SMesh* instMesh = node->getInstanceMesh()) {
            for (irr::u32 i = 0; i < instMesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = instMesh->getMeshBuffer(i);
                if (!buf) continue;
                irr::video::SMaterial& mat = buf->getMaterial();
                for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                    if (mat.getTexture(t) == texture) {
                        mat.setTexture(t, placeholder);
                        ++swapped;
                    }
                }
            }
        }
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[entitytex] EntityMeshTextureGuard: swapped {} entity-mesh material "
                  "reference(s) to evicted texture -> placeholder (P17 — closes the entity-mesh gap "
                  "RegionMeshTextureGuard's own doc comment already flagged: region meshes were "
                  "covered by P10b, entity meshes were not)", swapped);
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

    for (irr::scene::IMeshSceneNode* node : objectNodes_) {
        if (!node) continue;

        // 1. Mesh-buffer materials — what most object nodes actually bind
        //    at render time (mirrors RegionMeshTextureGuard).
        if (irr::scene::IMesh* mesh = node->getMesh()) {
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf) continue;
                irr::video::SMaterial& mat = buf->getMaterial();
                for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                    if (mat.getTexture(t) == texture) {
                        mat.setTexture(t, placeholder);
                        ++swapped;
                    }
                }
            }
        }

        // 2. The node's own override materials array (ISceneNode::Materials,
        //    via getMaterial(i)/getMaterialCount()) — what
        //    ConstrainedTextureCache::clearTextureReferences() already checks
        //    for nodes reachable from the scene-graph root. T123 confirmed
        //    via a live core dump (irrAllocator<SMaterial> vtable match on
        //    the array's embedded allocator) that this is exactly the
        //    holder type an observed use-after-free write traced back to.
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                if (mat.getTexture(t) == texture) {
                    mat.setTexture(t, placeholder);
                    ++swapped;
                }
            }
        }
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[objecttex] ObjectNodeTextureGuard: swapped {} object-node material "
                  "reference(s) to evicted texture -> placeholder (T123 — closes the detached-object-node "
                  "gap: applySimulationOutput() detaches invisible objectNodes_ entries from the scene "
                  "graph, making clearTextureReferences()'s scene walk unable to reach them while hidden)",
                  swapped);
    }
}

} // namespace Graphics
} // namespace EQT
