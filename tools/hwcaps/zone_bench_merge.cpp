// P09 Phase B: cross-region same-material zone draw merge prototype
// implementation. See zone_bench_merge.h for the chosen design + rationale.

#include "zone_bench_merge.h"
#include "zone_bench_loader.h"

#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// P09b root-cause fix (item 6/7 support): see the long comment at the top
// of renderMergedDrawEntries() for why the process's very first draw call
// must go through the exact unmerged single-buffer path once, regardless
// of merging, before any coalesced draw runs.
bool g_firstDrawDone = false;

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
    size_t startIdx = 0;

    // P09b root cause (see Review): COpenGLES2Driver::getOpaqueAlphaFallback()
    // (docker/irrlicht-drm/COpenGLES2Driver.cpp -- shared production driver,
    // out of scope for this benchmark-only unit to edit) lazily creates its
    // 1x1 white "no companion texture" fallback on first use by calling
    // glBindTexture() WITHOUT first selecting texture unit 1 -- so on its
    // FIRST-EVER invocation in the process it binds the white fallback to
    // whatever unit is *currently active* (unit 0, left over from the
    // diffuse-texture bind moments earlier in the same
    // applyMaterialRenderState() call), clobbering the real diffuse texture
    // on unit 0 for that one draw call. This is a pre-existing latent bug,
    // not introduced by merging: in the unmerged path every draw call
    // re-binds its own unit-0 texture at the very start of its own
    // setMaterial(), so only the process's single very-first "no companion
    // texture" draw is ever affected -- historically an invisible one-
    // triangle-fan sliver (present today in the unmerged/production path
    // and its goldens; out of scope to fix here). The merge prototype
    // coalesces many regions into ONE setMaterial()+drawMeshBuffer() call,
    // so when that single call happens to BE the process's first "no
    // companion texture" draw (materialKey-sorted order makes this
    // reproducible -- it's exactly gfaydark's fayroof1.bmp run in the
    // golden4 pose), the corruption blast radius grows from one sliver to
    // the entire merged surface: textured surfaces render as a flat,
    // fully-lit gradient -- the "UNATTRIBUTED texture corruption" P09 hit.
    // P09's SMaterial field audit (and this unit's exhaustive operator!=
    // audit above) never had a chance to catch it: it is a texture-UNIT
    // binding ORDER bug in the driver, not a material-field mismatch
    // between coalesced buffers -- outside what any per-buffer SMaterial
    // comparison could ever detect.
    //
    // Fix, kept entirely inside the merge TU per this unit's scope, and
    // provably byte-identical to the unmerged path rather than a guess at
    // the driver's internals: draw the process's very first entry
    // (sortedEntries[0], on the very first call ever) through the EXACT
    // SAME single-buffer setMaterial()+drawMeshBuffer() sequence the
    // unmerged path uses for its own first entry, before any coalesced
    // draw runs. Whatever the driver does on that first call -- corrupt or
    // not -- now happens identically in both paths, and the driver's
    // lazy fallback cache is warm for every subsequent draw either way
    // (merged or not), so no coalesced run can ever inherit the blast-
    // radius amplification. Costs exactly one extra draw call, once, for
    // the lifetime of the process.
    if (!g_firstDrawDone) {
        g_firstDrawDone = true;
        const DrawEntry& de0 = sortedEntries[0];
        driver->setTransform(irr::video::ETS_WORLD, de0.worldMat);
        if (de0.materialOverride >= 0) {
            irr::video::SMaterial mat0 = de0.buffer->getMaterial();
            mat0.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de0.materialOverride);
            driver->setMaterial(mat0);
        } else {
            driver->setMaterial(de0.buffer->getMaterial());
        }
        driver->drawMeshBuffer(de0.buffer);
        ++drawCalls;
        startIdx = 1;
    }

    // P09b Phase A debug tooling: P09_MERGE_ONLY_RUN=<index> renders ONLY
    // the coalesced run at that index (0-based over materialKey-runs of
    // length > 1; singleton runs are skipped) and draws nothing else -- used
    // to isolate the single corrupted run during bisection (P09's tooling
    // only supported whole-frame merged-vs-unmerged dumps). Member-region
    // bisection within an isolated run is done by further restricting via
    // P09_MERGE_ONLY_RUN_HALF={0,1} (first/second half of that run's member
    // entries), applied only when P09_MERGE_ONLY_RUN is also set.
    const char* onlyRunEnv = getenv("P09_MERGE_ONLY_RUN");
    const int onlyRun = onlyRunEnv ? atoi(onlyRunEnv) : -1;
    const char* onlyHalfEnv = getenv("P09_MERGE_ONLY_RUN_HALF");
    const int onlyHalf = onlyHalfEnv ? atoi(onlyHalfEnv) : -1;
    const char* onlyCountEnv = getenv("P09_MERGE_ONLY_RUN_COUNT");
    const int onlyCount = onlyCountEnv ? atoi(onlyCountEnv) : -1;  // first K members only
    const char* onlySkipEnv = getenv("P09_MERGE_ONLY_RUN_SKIP");
    const int onlySkip = onlySkipEnv ? atoi(onlySkipEnv) : 0;  // skip first N members before COUNT
    int mergedRunIndex = -1;

    // P09b root-cause fix: materialKey (effective MaterialType + texId, see
    // zone_bench_loader.cpp collectDrawEntries) is a COARSE grouping key --
    // it does not capture TextureLayer[i] wrap/filter/matrix state, texture
    // unit 1 (H02d companion texture), or the remaining SMaterial flags
    // (Lighting, BackfaceCulling, ZBuffer, ZWriteEnable, colors, FogEnable,
    // ...). P09's per-buffer state audit compared only a curated subset
    // (texture0 name/pointer, MaterialType, MaterialTypeParam/2) and missed
    // this. Two buffers can share a materialKey while differing in any of
    // those fields; the old code applied ONLY the first buffer's full
    // SMaterial to the whole coalesced run (see `mat = first.buffer->
    // getMaterial()` below), silently overwriting every other member's
    // real render state -- e.g. a CLAMP-wrapped first buffer's state
    // applied to a REPEAT-tiled buffer produces exactly the observed
    // flat-gradient smear. Irrlicht's SMaterial::operator!= already does a
    // complete field walk (every TextureLayer, every flag, every color) --
    // use it directly instead of re-curating a subset, and split runs at
    // any point where the full material differs from the run's first
    // member. This can only shrink runs relative to the old (incorrect)
    // grouping, never grow them, so it is strictly a correctness fix.
    size_t i = startIdx;
    const size_t n = sortedEntries.size();
    while (i < n) {
        size_t j = i + 1;
        const irr::video::SMaterial& runMat = sortedEntries[i].buffer->getMaterial();
        while (j < n && sortedEntries[j].materialKey == sortedEntries[i].materialKey &&
               sortedEntries[j].buffer->getMaterial() == runMat) {
            ++j;
        }
        size_t runLen = j - i;

        if (runLen == 1) {
            if (onlyRun >= 0) { ++i; continue; }  // debug isolation: singletons skipped
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
#ifdef EQT_HAS_GLES2
            if (getenv("P09_MERGE_DEBUG3")) {
                GLint prog = 0, tex0 = 0, activeUnit = 0;
                glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
                glActiveTexture((GLenum)activeUnit);
                printf("[merge-debug3] singleton i=%zu program=%d tex0=%d\n", i, prog, tex0);
            }
#endif
            ++i;
            continue;
        }

        ++mergedRunIndex;
        if (onlyRun >= 0 && mergedRunIndex != onlyRun) { i = j; continue; }  // debug isolation
        size_t debugLo = i, debugHi = j;
        if (onlyRun >= 0 && onlyHalf >= 0) {
            size_t mid = i + runLen / 2;
            if (onlyHalf == 0) { debugLo = i; debugHi = mid; }
            else { debugLo = mid; debugHi = j; }
        }
        if (onlyRun >= 0 && onlyCount >= 0) {
            debugLo = std::min(j, i + static_cast<size_t>(onlySkip));
            debugHi = std::min(j, debugLo + static_cast<size_t>(onlyCount));
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
            for (size_t k = debugLo; k < debugHi; ++k) {
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

        // Per-frame gather: copy this run's index slices, in EXACTLY the
        // same order collectDrawEntries() already sorted them into (the
        // same order the unmerged/runLen==1 path above walks this same
        // sortedEntries vector) -- same triangles, same order, same state
        // (guaranteed by the operator== split above) => rasterization-order-
        // identical => max_delta=0 is achievable, not just approachable
        // (P09b fix item 7; kills the z-fight class alongside item 6's
        // state-correctness fix).
        gen.buf->Indices.set_used(0);
        for (size_t k = debugLo; k < debugHi; ++k) {
            const RegionBufferRange& rr = g_ranges[sortedEntries[k].buffer];
            if (getenv("P09_MERGE_DEBUG4")) {
                irr::scene::IMeshBuffer* srcBuf = sortedEntries[k].buffer;
                const irr::video::S3DVertex* srcVerts =
                    static_cast<const irr::video::S3DVertex*>(srcBuf->getVertices());
                printf("[merge-debug4] member k=%zu srcVertCount=%u srcIdxCount=%u rr.off=%d rr.cnt=%d\n",
                       k, srcBuf->getVertexCount(), srcBuf->getIndexCount(), rr.indexOffset, rr.indexCount);
                {
                    const irr::u16* rawIdx = srcBuf->getIndices();
                    printf("  src raw indices:");
                    for (irr::u32 ii = 0; ii < srcBuf->getIndexCount(); ++ii) printf(" %u", rawIdx[ii]);
                    printf("\n");
                }
                for (irr::u32 vv = 0; vv < srcBuf->getVertexCount() && vv < 4; ++vv) {
                    const irr::video::S3DVertex& sv = srcVerts[vv];
                    printf("  src v=%u pos=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f) uv=(%.3f,%.3f) color=0x%08x\n",
                           vv, sv.Pos.X, sv.Pos.Y, sv.Pos.Z, sv.Normal.X, sv.Normal.Y, sv.Normal.Z,
                           sv.TCoords.X, sv.TCoords.Y, sv.Color.color);
                }
                for (int off = rr.indexOffset; off < rr.indexOffset + rr.indexCount && off < rr.indexOffset + 4; ++off) {
                    uint16_t gi = gen.templateIndices[off];
                    const irr::video::S3DVertex& gv = gen.buf->Vertices[gi];
                    printf("  gen idx=%u -> globalVert=%u pos=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f) uv=(%.3f,%.3f) color=0x%08x\n",
                           off - rr.indexOffset, gi, gv.Pos.X, gv.Pos.Y, gv.Pos.Z, gv.Normal.X, gv.Normal.Y, gv.Normal.Z,
                           gv.TCoords.X, gv.TCoords.Y, gv.Color.color);
                }
            }
            for (int off = rr.indexOffset; off < rr.indexOffset + rr.indexCount; ++off) {
                gen.buf->Indices.push_back(gen.templateIndices[off]);
            }
        }

        if (getenv("P09_MERGE_DEBUG2")) {
            float nMinX=1e9f,nMaxX=-1e9f,nMinY=1e9f,nMaxY=-1e9f,nMinZ=1e9f,nMaxZ=-1e9f;
            float uMin=1e9f,uMax=-1e9f,vMin=1e9f,vMax=-1e9f;
            for (irr::u32 ix = 0; ix < gen.buf->Indices.size(); ++ix) {
                uint16_t vi = gen.buf->Indices[ix];
                const irr::video::S3DVertex& vv = gen.buf->Vertices[vi];
                nMinX = std::min(nMinX, vv.Normal.X); nMaxX = std::max(nMaxX, vv.Normal.X);
                nMinY = std::min(nMinY, vv.Normal.Y); nMaxY = std::max(nMaxY, vv.Normal.Y);
                nMinZ = std::min(nMinZ, vv.Normal.Z); nMaxZ = std::max(nMaxZ, vv.Normal.Z);
                uMin = std::min(uMin, vv.TCoords.X); uMax = std::max(uMax, vv.TCoords.X);
                vMin = std::min(vMin, vv.TCoords.Y); vMax = std::max(vMax, vv.TCoords.Y);
            }
            printf("[merge-debug2] run i=%zu len=%zu idxCount=%u normalRange=[%.2f,%.2f]x[%.2f,%.2f]x[%.2f,%.2f] uvRange=[%.3f,%.3f]x[%.3f,%.3f]\n",
                   i, runLen, gen.buf->Indices.size(), nMinX,nMaxX,nMinY,nMaxY,nMinZ,nMaxZ, uMin,uMax,vMin,vMax);
        }

        const DrawEntry& first = sortedEntries[debugLo];
        irr::video::SMaterial mat = first.buffer->getMaterial();
        if (first.materialOverride >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(first.materialOverride);
        }
        gen.buf->Material = mat;

        if (getenv("P09_MERGE_DEBUG3")) {
            const irr::video::ITexture* t = mat.getTexture(0);
            printf("[merge-debug3] about to setMaterial: tex=%p name=%s matType=%d\n",
                   (void*)t, t ? t->getName().getPath().c_str() : "(null)", (int)mat.MaterialType);
        }
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
        if (getenv("P09_MERGE_DEBUG3")) {
            GLint prog = 0, tex0 = 0, activeUnit = 0, arrayBuf = 0, elemBuf = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
            glActiveTexture((GLenum)activeUnit);
            GLint vaEnabled[4] = {0,0,0,0};
            for (int a = 0; a < 4; ++a) glGetVertexAttribiv(a, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vaEnabled[a]);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elemBuf);
            printf("[merge-debug3] run i=%zu program=%d tex0=%d attribsEnabled=[%d,%d,%d,%d] arrayBuf=%d elemBuf=%d\n",
                   i, prog, tex0, vaEnabled[0], vaEnabled[1], vaEnabled[2], vaEnabled[3], arrayBuf, elemBuf);
        }
#endif

        i = j;
    }

    return drawCalls;
}

} // namespace ZoneBench
