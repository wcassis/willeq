// RaceModelLoader mesh building methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class

#include "client/graphics/eq/race_model_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace EQT {
namespace Graphics {

std::map<std::string, std::shared_ptr<TextureInfo>> RaceModelLoader::getMergedTextures() {
    // T352 (B016): otherChrCacheMutex_ over this WHOLE body.
    //
    // MEASURED under TSAN, 3 of 4 runs (review/T352/tsan_report.*): this
    // function ran completely unsynchronised while EntityPrepWorker's
    // background thread mutated the very state it reads. Both stacks:
    //
    //   worker : storeOtherChrCacheLocked (race_model_loader.cpp:650,
    //            holding otherChrCacheMutex_) <- preloadModelData
    //   render : getMergedTextures (this line) <- loadModelFromGlobalChr
    //            <- ensureModelDataLoaded
    //
    // TSAN names the 1-byte mergedTexturesCacheValid_ flag because that is
    // the first byte the two threads collide on, but the same window covers
    // step 4 below, which ITERATES otherChrCaches_ while the writer inserts
    // into it and LRU-EVICTS from it -- iterator invalidation, not a stale
    // bool. The writer already holds otherChrCacheMutex_ at every mutation
    // site, so taking the same mutex here closes the window with one lock.
    //
    // Deadlock-free by construction, checked at all 12 call sites: no caller
    // holds otherChrCacheMutex_ when it calls this (model_loading_chr.cpp's
    // T114 comment at line 336 makes releasing it first an explicit
    // invariant, and model_loading.cpp:181's guard closes two lines later),
    // and nothing this function calls -- loadNumberedGlobalModels() (T452
    // removed the second callee, loadArmorTextures()) -- acquires it or any
    // other loader mutex. No new
    // lock-ordering edge is introduced.
    std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);

    // Performance optimization: return cached result if valid
    if (mergedTexturesCacheValid_) {
        return cachedMergedTextures_;
    }

    // Build a merged texture map from all sources, matching model viewer behavior:
    // Order: global_chr.s3d -> global2-7_chr.s3d -> armor textures -> zone_chr.s3d (overrides)

    // Ensure numbered globals are loaded (they contain some armor textures)
    // Skip eager loading when archive index is active (per-archive loads only)
    if (!numberedGlobalsLoaded_ && !graphicsArchiveIndex_) {
        loadNumberedGlobalModels();
    }

    // T452: there is no armor-texture PRE-load any more. loadArmorTextures()
    // was called here and did nothing but set a flag -- it was gutted in
    // 2026-03 when the lookup moved to a database table that never shipped
    // (board B468). global17-23_amr.s3d are opened by getArmorTexture(), one
    // texture at a time, only for a name whose variant says they hold it.

    std::map<std::string, std::shared_ptr<TextureInfo>> merged;

    // 1. Start with global textures
    merged = globalTextures_;

    // 2. Add textures from numbered globals (only add new, don't override)
    for (const auto& [num, texMap] : numberedGlobalTextures_) {
        for (const auto& [name, tex] : texMap) {
            if (merged.find(name) == merged.end()) {
                merged[name] = tex;
            }
        }
    }

    // 3. Add already-cached armor textures (loaded on demand from global17-23_amr.s3d).
    // T452: a NULL value is a cached miss, not a texture -- never merge one.
    for (const auto& [name, tex] : armorTextureCache_) {
        if (!tex) continue;
        if (merged.find(name) == merged.end()) {
            merged[name] = tex;
        }
    }

    // 4. Add textures from JSON-specified chr files (otherChrCaches_)
    // These are zone-specific chr files loaded for races specified in race_models.json
    // (e.g., steamfont_chr.s3d for kobolds, qeynos_chr.s3d for qeynos citizens)
    for (const auto& [filename, cache] : otherChrCaches_) {
        for (const auto& [name, tex] : cache.textures) {
            if (merged.find(name) == merged.end()) {
                merged[name] = tex;
            }
        }
    }

    // 5. Add/override with zone textures (current zone takes highest precedence)
    for (const auto& [name, tex] : zoneTextures_) {
        merged[name] = tex;
    }

    // Cache the result
    cachedMergedTextures_ = merged;
    mergedTexturesCacheValid_ = true;

    return cachedMergedTextures_;
}

irr::scene::IMesh* RaceModelLoader::buildMeshFromGeometry(
    const std::shared_ptr<ZoneGeometry>& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    uint8_t bodyTextureVariant,
    const std::string& raceCode) {

    if (!geometry) {
        return nullptr;
    }

    // Build multi-buffer textured mesh while maintaining vertex mapping for animation
    // Group triangles by texture, but track where each original vertex ends up

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Prepare lowercase race code for texture matching
    // The raceCode might be a full model name like "QCF_DMSPRITEDEF", so extract just the 3-letter prefix
    std::string lowerRaceCode = raceCode;
    // Remove any suffix (find underscore or use first 3 chars)
    size_t underscorePos = lowerRaceCode.find('_');
    if (underscorePos != std::string::npos) {
        lowerRaceCode = lowerRaceCode.substr(0, underscorePos);
    }
    // Race codes are typically 3 characters (HUM, ELF, QCF, etc.)
    if (lowerRaceCode.length() > 3) {
        lowerRaceCode = lowerRaceCode.substr(0, 3);
    }
    std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Group triangles by texture index
    std::map<irr::u32, std::vector<size_t>> trianglesByTexture;
    for (size_t i = 0; i < geometry->triangles.size(); ++i) {
        trianglesByTexture[geometry->triangles[i].textureIndex].push_back(i);
    }

    // Build original vertices array (in order, for bone transforms)
    originalVerticesForAnimation_.clear();
    originalVerticesForAnimation_.reserve(geometry->vertices.size());
    for (const auto& v : geometry->vertices) {
        irr::video::S3DVertex vert;
        // Store in EQ coordinates - conversion happens during animation
        vert.Pos.X = v.x;
        vert.Pos.Y = v.y;
        vert.Pos.Z = v.z;
        vert.Normal.X = v.nx;
        vert.Normal.Y = v.ny;
        vert.Normal.Z = v.nz;
        vert.TCoords.X = v.u;
        vert.TCoords.Y = 1.0f - v.v;  // Flip V for character models
        vert.Color = irr::video::SColor(255, 255, 255, 255);
        originalVerticesForAnimation_.push_back(vert);
    }

    // Initialize vertex mapping (original index -> ALL destination buffer locations).
    // P08D-RETAKE: was a single VertexMapping per original vertex, silently dropping
    // every duplicate but the last-registered one for vertices shared across texture
    // groups (UV seams) -- see the VertexMappingList comment in
    // animated_mesh_scene_node.h. Each vIdx now gets a list; empty list = unmapped.
    vertexMappingForAnimation_.clear();
    vertexMappingForAnimation_.resize(geometry->vertices.size());

    irr::u32 bufferIndex = 0;
    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // P50: compute isMaskedMaterial BEFORE the texture-load block (was
        // previously computed only after, at MaterialType-assignment time —
        // see below) so it can be threaded into the getOrLoadTexture()/
        // loadTextureFromBMP() fallback calls this function uses to resolve
        // textures on-demand. Closes P46d Part B gap #1: composite-deferred
        // per-part textures (P13b's deferredPerPartTextureNames_) resolved
        // here via this fallback previously always decoded opaque, since
        // this same flag was computed too late to reach the load call.
        bool isMaskedMaterial = (texIdx < geometry->textureMasked().size()) &&
                                 geometry->textureMasked()[texIdx];

        // Load texture for this buffer
        irr::video::ITexture* texture = nullptr;
        if (texIdx < geometry->textureNames().size() && meshBuilder_) {
            const std::string& texName = geometry->textureNames()[texIdx];
            if (!texName.empty()) {
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Apply equipment texture override if variant > 0
                // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
                // e.g., qcfch0001.bmp -> qcfch0101.bmp (texture=1, leather)
                std::string finalTexName = lowerTexName;
                bool overrideApplied = false;

                // Helper: check if a texture is available (has raw data OR is already loaded in cache)
                auto isTextureAvailable = [&](const std::string& name) -> bool {
                    // Check meshBuilder cache first (texture already uploaded to GPU)
                    if (meshBuilder_->hasTexture(name)) {
                        return true;
                    }
                    // Check raw data in textures map
                    auto it = textures.find(name);
                    if (it != textures.end() && it->second && it->second->present()) {
                        return true;
                    }
                    // Check armor texture archives (lazy-loaded)
                    auto armorTex = getArmorTexture(name);
                    if (armorTex && armorTex->present()) {
                        return true;
                    }
                    return false;
                };

                if (bodyTextureVariant > 0) {
                    // Check for CLK (robe) textures first
                    // Pattern: clkXXYY.bmp where XX is robe clk number (04-10), YY is variant (01-06)
                    // Robe textures 10-16 map to clk04-clk10
                    if (lowerTexName.length() >= 10 && lowerTexName.substr(0, 3) == "clk" &&
                        bodyTextureVariant >= 10 && bodyTextureVariant <= 16) {
                        // Extract the page number (last 2 digits before .bmp)
                        // clk0401.bmp -> page = "01"
                        std::string pageNum = lowerTexName.substr(5, 2);

                        // Calculate target CLK number from robe texture
                        // Texture 10 -> clk04, 11 -> clk05, ..., 16 -> clk10
                        int targetClk = bodyTextureVariant - 6;

                        char clkTexName[32];
                        snprintf(clkTexName, sizeof(clkTexName), "clk%02d%s.bmp", targetClk, pageNum.c_str());
                        std::string clkTexNameStr = clkTexName;

                        // Check if target CLK texture exists (raw data or already cached in GPU)
                        if (isTextureAvailable(clkTexNameStr)) {
                            finalTexName = clkTexNameStr;
                            overrideApplied = true;
                        }
                    }
                    // Check if texture name starts with race code and has "00" pattern
                    // Body parts: ch (chest), lg (legs), ft (feet), ua (upper arms), fa (forearms), hn (hands)
                    // Pattern: {race}{part}00{page}.bmp where part is 2 chars
                    else if (!lowerRaceCode.empty() &&
                             lowerTexName.length() >= lowerRaceCode.length() + 6 &&
                             lowerTexName.substr(0, lowerRaceCode.length()) == lowerRaceCode) {
                        // Find "00" after race code + 2-char body part code
                        size_t partEnd = lowerRaceCode.length() + 2;
                        if (partEnd + 2 <= lowerTexName.length() &&
                            lowerTexName.substr(partEnd, 2) == "00") {
                            // Build equipment variant texture name
                            char variantStr[8];
                            snprintf(variantStr, sizeof(variantStr), "%02d", bodyTextureVariant);
                            std::string equipTexName = lowerTexName.substr(0, partEnd) + variantStr +
                                                       lowerTexName.substr(partEnd + 2);

                            // Check if equipment variant texture exists (raw data or already cached in GPU)
                            if (isTextureAvailable(equipTexName)) {
                                finalTexName = equipTexName;
                                overrideApplied = true;
                            }
                        }
                    }
                }

                // Helper lambda: try to find texture data from passed map or armor index
                auto findTextureData = [&](const std::string& name) -> std::shared_ptr<TextureInfo> {
                    auto texIt = textures.find(name);
                    if (texIt != textures.end() && texIt->second && texIt->second->present()) {
                        return texIt->second;
                    }
                    // Fallback: try lazy-loading from armor texture archives
                    return getArmorTexture(name);
                };

                texture = meshBuilder_->getOrLoadTexture(finalTexName, isMaskedMaterial);
                if (!texture) {
                    auto texInfo = findTextureData(finalTexName);
                    if (texInfo && texInfo->present()) {
                        texture = meshBuilder_->loadTextureFromBMP(finalTexName, texInfo->data, isMaskedMaterial);
                    } else if (overrideApplied) {
                        texture = meshBuilder_->getOrLoadTexture(lowerTexName, isMaskedMaterial);
                        if (!texture) {
                            texInfo = findTextureData(lowerTexName);
                            if (texInfo && texInfo->present()) {
                                texture = meshBuilder_->loadTextureFromBMP(lowerTexName, texInfo->data, isMaskedMaterial);
                            }
                        }
                    }
                }
            }
        }

        buffer->Material.Lighting = false;
        buffer->Material.BackfaceCulling = false;
        if (texture) {
            buffer->Material.setTexture(0, texture);
            // P37: WLD Fragment 0x30 materialType (TransparentMasked) is the
            // ground-truth signal for era-authentic per-material transparency
            // (zombie flesh gaps, helm holes) — mirrors ZoneMeshBuilder's H02d
            // check (zone_geometry.cpp). Marking EMT_TRANSPARENT_ALPHA_CHANNEL_REF
            // here feeds EntityRenderer::applyAlphaTestMaterialType() (P21),
            // which picks the ETC1-companion or RGBA-direct alpha-test shader
            // variant at material-assignment time. Masked-but-decode-opaque
            // textures (citywlivy class) still render solid: the shader's
            // alpha-test simply never discards when the source alpha is 255.
            // P50: isMaskedMaterial now computed once, above, before the
            // texture-load block — reused here rather than recomputed.
            buffer->Material.MaterialType = isMaskedMaterial
                ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF
                : irr::video::EMT_SOLID;
        } else {
            buffer->Material.DiffuseColor = irr::video::SColor(255, 200, 180, 160);
        }

        // Map from global vertex index to local buffer index
        std::map<irr::u32, irr::u16> globalToLocal;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry->triangles[triIdx];

            for (irr::u32 vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    // Add vertex to buffer
                    const auto& v = geometry->vertices[vidx];
                    irr::video::S3DVertex vert;
                    // Store in EQ coordinates - conversion happens during animation
                    vert.Pos.X = v.x;
                    vert.Pos.Y = v.y;
                    vert.Pos.Z = v.z;
                    vert.Normal.X = v.nx;
                    vert.Normal.Y = v.ny;
                    vert.Normal.Z = v.nz;
                    vert.TCoords.X = v.u;
                    vert.TCoords.Y = 1.0f - v.v;
                    vert.Color = irr::video::SColor(255, 255, 255, 255);

                    irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                    globalToLocal[vidx] = localIdx;
                    buffer->Vertices.push_back(vert);

                    // Record vertex mapping for animation. P08D-RETAKE: APPEND (not
                    // overwrite) -- this vertex may already have a mapping from an
                    // earlier texture group's buffer if it's a UV-seam vertex shared
                    // across materials; both duplicates must be tracked so skinning
                    // updates all of them, not just the last one registered.
                    if (vidx < vertexMappingForAnimation_.size()) {
                        vertexMappingForAnimation_[vidx].push_back({bufferIndex, localIdx});
                    }
                }
            }

            // Add triangle indices
            buffer->Indices.push_back(globalToLocal[tri.v1]);
            buffer->Indices.push_back(globalToLocal[tri.v2]);
            buffer->Indices.push_back(globalToLocal[tri.v3]);
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
        bufferIndex++;
    }

    mesh->recalculateBoundingBox();

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Built multi-texture animated mesh with {} buffers, {} original vertices",
              mesh->getMeshBufferCount(), geometry->vertices.size());

    return mesh;
}

irr::scene::IMesh* RaceModelLoader::buildCompositeMeshFromGeometry(
    const std::shared_ptr<ZoneGeometry>& geometry,
    irr::video::ITexture* compositeTexture,
    const CompositeTextureData& compositeData) {

    if (!geometry || !compositeTexture) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    // P37 investigation: composite sheets carried NO alpha data in either
    // deployed form as of P37/P45 — entity_composite_builder.cpp hardcoded
    // hasAlpha=0/EQE1_TYPE_ETC1_RGB and a 24bpp-only BMP. P37's fix was
    // eligibility-side: exclude any race/gender/variant whose composite
    // includes a WLD-masked source texture from composite eligibility
    // entirely (falls back to the per-material buildMeshFromGeometry() path,
    // which honors textureMasked()).
    //
    // P48: the builder now CAN emit a real ETC1 alpha companion page for
    // sheets containing WLD-masked source parts (entity_composite_builder.cpp
    // writeEQE1(), mirrors the zone ETC1 companion convention). Eligibility
    // in race_model_loader.cpp is now sheet-capability-gated: a composite
    // whose masked source parts are covered by a sheet WITH a companion
    // (CompositeTextureData::hasAlpha, parsed from the builder's JSON
    // "hasAlpha" key) is allowed through to this function instead of being
    // excluded. Reusing the SAME production promotion path
    // EntityRenderer::applyAlphaTestMaterialType() already uses for the
    // per-part path (P46b: never fork it) needs nothing more than this
    // function setting the marker MaterialType here — entity_renderer.cpp's
    // existing generic "iterate every buffer material, promote
    // EMT_TRANSPARENT_ALPHA_CHANNEL_REF" loop (createEntity()) already
    // applies to composite mesh buffers exactly the same as multi-buffer
    // ones, with no call-site change needed. compositeData.hasAlpha is false
    // for sheets built by a pre-P48 builder (JSON has no "hasAlpha" key), so
    // this stays EMT_SOLID — correct-by-construction — until sheets are
    // regenerated.
    // Single composite texture, CLAMP_TO_EDGE (tiling is pre-baked in the sheet)
    buffer->Material.Lighting = false;
    buffer->Material.BackfaceCulling = false;
    buffer->Material.setTexture(0, compositeTexture);
    buffer->Material.MaterialType = compositeData.hasAlpha
        ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF
        : irr::video::EMT_SOLID;
    buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
    buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
    buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;

    // Build original vertices array (in EQ coordinates for bone transforms)
    originalVerticesForAnimation_.clear();
    originalVerticesForAnimation_.reserve(geometry->vertices.size());
    for (const auto& v : geometry->vertices) {
        irr::video::S3DVertex vert;
        vert.Pos.X = v.x;
        vert.Pos.Y = v.y;
        vert.Pos.Z = v.z;
        vert.Normal.X = v.nx;
        vert.Normal.Y = v.ny;
        vert.Normal.Z = v.nz;
        vert.TCoords.X = v.u;
        vert.TCoords.Y = 1.0f - v.v;  // Flip V for character models
        vert.Color = irr::video::SColor(255, 255, 255, 255);
        originalVerticesForAnimation_.push_back(vert);
    }

    // Initialize vertex mapping (P08D-RETAKE: one-to-many, see mesh_building.cpp's
    // buildMeshFromGeometry comment / animated_mesh_scene_node.h's VertexMappingList
    // comment -- a composite mesh still duplicates an original vertex once per
    // distinct (vertex, texture) pair it's used with, even though every duplicate
    // shares the same destination BUFFER, because each duplicate needs a different
    // remapped UV into the composite sheet).
    vertexMappingForAnimation_.clear();
    vertexMappingForAnimation_.resize(geometry->vertices.size());

    // ── Pass 1: Scan UV extents per texture ──
    // Character models use flipV (1.0 - v.v) for individual textures (see
    // buildMeshFromGeometry). The composite sheet was tiled using the source BMP
    // in raster order (y=0 at top), and GL loads all textures with V=0 at top
    // (both BMP-decoded via glTexImage2D and ETC1 via glCompressedTexImage2D use
    // the same convention in this pipeline). So correctedV = 1.0 - v.v gives us
    // the right V to map into the composite sheet, matching how the zone composite
    // path works (buildCompositeMesh in zone_geometry.cpp, line 733).
    //
    // We scan correctedV extents here so the shift aligns with correctedV space.
    struct TexUVExtent {
        float minU = 1e9f, minCorrV = 1e9f;
    };
    std::map<uint32_t, TexUVExtent> texExtents;  // keyed by textureIndex

    for (const auto& tri : geometry->triangles) {
        uint32_t texIdx = tri.textureIndex;
        if (texIdx >= geometry->textureNames().size()) continue;
        const std::string& texName = geometry->textureNames()[texIdx];
        if (texName.empty() || !compositeData.lookup(texName)) continue;

        auto& ext = texExtents[texIdx];
        for (irr::u32 vidx : {tri.v1, tri.v2, tri.v3}) {
            if (vidx >= geometry->vertices.size()) continue;
            const auto& v = geometry->vertices[vidx];
            ext.minU = std::min(ext.minU, v.u);
            ext.minCorrV = std::min(ext.minCorrV, 1.0f - v.v);
        }
    }

    // Compute per-texture shift: bring UVs into [0, tilesU] × [0, tilesV] range.
    struct TexShift { float shiftU = 0, shiftCorrV = 0; };
    std::map<uint32_t, TexShift> texShifts;
    for (const auto& [texIdx, ext] : texExtents) {
        TexShift s;
        s.shiftU = -std::floor(ext.minU);
        s.shiftCorrV = -std::floor(ext.minCorrV);
        texShifts[texIdx] = s;
    }

    // Diagnostic: track UV ranges per texture for logging
    struct UVDiag {
        float rawMinU = 1e9f, rawMaxU = -1e9f, rawMinV = 1e9f, rawMaxV = -1e9f;
        float remapMinU = 1e9f, remapMaxU = -1e9f, remapMinV = 1e9f, remapMaxV = -1e9f;
        int tilesU = 0, tilesV = 0;
        float shiftU = 0, shiftV = 0;
        int vertCount = 0;
    };
    std::map<std::string, UVDiag> uvDiags;

    // ── Pass 2: Build composite mesh with remapped UVs ──
    std::map<uint64_t, irr::u16> globalToLocal;
    size_t unmappedTriangles = 0;
    irr::u32 currentBufferIndex = 0;

    for (size_t i = 0; i < geometry->triangles.size(); ++i) {
        const auto& tri = geometry->triangles[i];
        uint32_t texIdx = tri.textureIndex;

        std::string texName;
        if (texIdx < geometry->textureNames().size()) {
            texName = geometry->textureNames()[texIdx];
        }
        const CompositeTextureEntry* entry = nullptr;
        if (!texName.empty()) {
            entry = compositeData.lookup(texName);
        }
        if (!entry) {
            unmappedTriangles++;
            continue;
        }

        // Get the shift for this texture (computed in pass 1)
        float shiftU = 0, shiftCorrV = 0;
        auto shiftIt = texShifts.find(texIdx);
        if (shiftIt != texShifts.end()) {
            shiftU = shiftIt->second.shiftU;
            shiftCorrV = shiftIt->second.shiftCorrV;
        }

        for (irr::u32 vidx : {tri.v1, tri.v2, tri.v3}) {
            uint64_t key = (static_cast<uint64_t>(vidx) << 32) | texIdx;
            auto it = globalToLocal.find(key);
            if (it != globalToLocal.end()) {
                buffer->Indices.push_back(it->second);
            } else {
                if (buffer->Vertices.size() >= 65535) {
                    buffer->recalculateBoundingBox();
                    mesh->addMeshBuffer(buffer);
                    buffer->drop();

                    buffer = new irr::scene::SMeshBuffer();
                    buffer->Material.Lighting = false;
                    buffer->Material.BackfaceCulling = false;
                    buffer->Material.setTexture(0, compositeTexture);
                    // P48: same hasAlpha-gated MaterialType as the buffer created
                    // above — a >65535-vertex composite mesh spills into a second
                    // buffer using the SAME composite texture/sheet, so it must
                    // carry the identical alpha-test decision.
                    buffer->Material.MaterialType = compositeData.hasAlpha
                        ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF
                        : irr::video::EMT_SOLID;
                    buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                    buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
                    buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;
                    globalToLocal.clear();
                    currentBufferIndex++;
                }

                const auto& v = geometry->vertices[vidx];
                irr::video::S3DVertex vert;
                vert.Pos.X = v.x;
                vert.Pos.Y = v.y;
                vert.Pos.Z = v.z;
                vert.Normal.X = v.nx;
                vert.Normal.Y = v.ny;
                vert.Normal.Z = v.nz;
                vert.Color = irr::video::SColor(255, 255, 255, 255);

                // Remap UV to composite sheet coordinates.
                //
                // Character models use flipV (1.0 - v.v) — same as the standard
                // multi-buffer path (buildMeshFromGeometry). The corrected V maps
                // directly into composite sheet space: offset + fract * scale, with
                // NO additional 1.0-rasterV flip. This matches the zone composite
                // path (zone_geometry.cpp buildCompositeMesh).
                //
                // Shift handles negative/tiled UVs: bring correctedV into [0, tilesV].
                //
                // P16 unroll discipline: entry->tilesU/V is the PHYSICAL tile count
                // the builder actually unrolled into the sheet, which may be capped
                // below the geometry's logical UV extent (entity_composite_builder.cpp,
                // --tile-cap). shiftedU/V can therefore legitimately exceed tilesU/V —
                // fmod folds it back into the physically-stored, seam-gutter-padded
                // region instead of dividing by a logical count that has no matching
                // pixel data. When physical == logical (the untapped-cap common case)
                // shiftedU < tilesU always holds and fmod(x, n) == x, so this is exactly
                // the previous plain-division behavior — no change for uncapped textures.
                float shiftedU = v.u + shiftU;
                float correctedV = 1.0f - v.v;
                float shiftedV = correctedV + shiftCorrV;
                float tilesUf = static_cast<float>(entry->tilesU);
                float tilesVf = static_cast<float>(entry->tilesV);
                float fractU = (tilesUf > 0.0f) ? std::fmod(shiftedU, tilesUf) / tilesUf : 0.0f;
                float fractV = (tilesVf > 0.0f) ? std::fmod(shiftedV, tilesVf) / tilesVf : 0.0f;
                if (fractU < 0.0f) fractU += 1.0f;  // fmod can return negative for edge cases
                if (fractV < 0.0f) fractV += 1.0f;
                vert.TCoords.X = entry->uvOffsetU + fractU * entry->uvScaleU;
                vert.TCoords.Y = entry->uvOffsetV + fractV * entry->uvScaleV;

                // Track UV diagnostics
                auto& diag = uvDiags[texName];
                diag.tilesU = entry->tilesU; diag.tilesV = entry->tilesV;
                diag.shiftU = shiftU; diag.shiftV = shiftCorrV;
                diag.rawMinU = std::min(diag.rawMinU, v.u);
                diag.rawMaxU = std::max(diag.rawMaxU, v.u);
                diag.rawMinV = std::min(diag.rawMinV, v.v);
                diag.rawMaxV = std::max(diag.rawMaxV, v.v);
                diag.remapMinU = std::min(diag.remapMinU, vert.TCoords.X);
                diag.remapMaxU = std::max(diag.remapMaxU, vert.TCoords.X);
                diag.remapMinV = std::min(diag.remapMinV, vert.TCoords.Y);
                diag.remapMaxV = std::max(diag.remapMaxV, vert.TCoords.Y);
                diag.vertCount++;

                irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                globalToLocal[key] = localIdx;
                buffer->Vertices.push_back(vert);
                buffer->Indices.push_back(localIdx);

                // P08D-RETAKE: append, don't overwrite -- see comment above.
                if (vidx < vertexMappingForAnimation_.size()) {
                    vertexMappingForAnimation_[vidx].push_back({currentBufferIndex, localIdx});
                }
            }
        }
    }

    if (buffer->Vertices.size() > 0) {
        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
    }
    buffer->drop();

    mesh->recalculateBoundingBox();

    if (unmappedTriangles > 0) {
        LOG_INFO(MOD_GRAPHICS, "buildCompositeMesh: {} triangles not in composite (skipped)", unmappedTriangles);
    }

    // Dump per-texture UV diagnostics
    for (const auto& [name, d] : uvDiags) {
        LOG_INFO(MOD_GRAPHICS, "  COMPOSITE UV '{}': tiles={}x{} shift=({:.0f},{:.0f}) | "
                 "rawUV=[{:.3f},{:.3f}]x[{:.3f},{:.3f}] -> remapUV=[{:.4f},{:.4f}]x[{:.4f},{:.4f}] ({} verts)",
                 name, d.tilesU, d.tilesV, d.shiftU, d.shiftV,
                 d.rawMinU, d.rawMaxU, d.rawMinV, d.rawMaxV,
                 d.remapMinU, d.remapMaxU, d.remapMinV, d.remapMaxV, d.vertCount);
    }

    LOG_INFO(MOD_GRAPHICS, "buildCompositeMesh: built mesh with {} buffers, {} verts, {} textures mapped",
              mesh->getMeshBufferCount(), geometry->vertices.size(), uvDiags.size());

    return mesh;
}

irr::scene::IMesh* RaceModelLoader::buildMeshWithEquipment(
    const std::shared_ptr<ZoneGeometry>& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    const std::string& raceCode,
    const uint32_t* equipment) {

    // For now, delegate to standard mesh building
    // Equipment texture swapping is handled at the scene node level
    return buildMeshFromGeometry(geometry, textures);
}

} // namespace Graphics
} // namespace EQT
