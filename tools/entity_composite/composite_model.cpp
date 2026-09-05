// tools/entity_composite/composite_model.cpp -- per-model processing: resolve the body-part
// textures, pack them into a sheet, write the sheet and its index entry.
// Moved from tools/entity_composite_builder.cpp by T385 (board B167).
// Bodies are verbatim; the only edits are the ones the split forces --
// `static` dropped from the functions that now cross a TU boundary (they
// are declared in composite_builder.h) and the whole file wrapped in
// namespace ecb.

#include "entity_composite/composite_builder.h"

namespace ecb {

// ── Model processing ────────────────────────────────────────────────────────

// (moved to entity_composite/composite_builder.h)
// Get texture dimensions from BMP header in archive
static bool getTextureDimensions(PfsArchive& archive, const std::string& name,
                                  int& width, int& height) {
    std::vector<char> data;
    if (!archive.get(name, data) || data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    if (d[0] != 'B' || d[1] != 'M') return false;
    width = *reinterpret_cast<const int32_t*>(d + 18);
    height = std::abs(*reinterpret_cast<const int32_t*>(d + 22));
    return width > 0 && height > 0;
}

// Decode a texture from archive to RGBA pixels
static bool decodeTextureFromArchive(PfsArchive& archive, const std::string& name,
                                      std::vector<uint8_t>& outRGBA,
                                      int& outW, int& outH,
                                      bool applyPaletteMask = false) {
    std::vector<char> data;
    if (!archive.get(name, data) || data.empty()) return false;

    if (DDSDecoder::isDDS(data)) {
        // DDS carries its own alpha channel already (not an 8bpp-paletted
        // format) — the WLD-masked palette-0 convention does not apply.
        auto decoded = DDSDecoder::decode(data);
        if (!decoded.isValid()) return false;
        outW = decoded.width;
        outH = decoded.height;
        outRGBA = std::move(decoded.pixels);
        return true;
    }

    return decodeBMP(data, outRGBA, outW, outH, applyPaletteMask);
}

// Apply variant substitution to a texture name.
// Pattern: {race}{part}00{page}.bmp → {race}{part}{variant:02d}{page}.bmp
// E.g., humch0001.bmp with variant=1 → humch0101.bmp
 std::string applyVariant(const std::string& texName, int variant) {
    if (variant == 0) return texName;

    // EQ texture naming: {race3}{part2}{variant2}{page2}.bmp
    // The variant digits are at positions 5-6 (0-indexed) in the stem
    std::string lower = toLower(texName);
    auto dot = lower.rfind('.');
    std::string stem = (dot != std::string::npos) ? lower.substr(0, dot) : lower;
    std::string ext = (dot != std::string::npos) ? lower.substr(dot) : "";

    // Need at least 7 chars: 3 race + 2 part + 2 variant (+ optional page digits)
    if (stem.size() < 7) return texName;

    // Replace variant digits (positions 5-6)
    char varStr[3];
    std::snprintf(varStr, sizeof(varStr), "%02d", variant);
    std::string result = stem.substr(0, 5) + varStr + stem.substr(7) + ext;
    return result;
}

 ModelResult processModel(const std::string& eqPath,
                                 const std::shared_ptr<CharacterModel>& model,
                                 const std::string& archivePath,
                                 PfsArchive& archive,
                                 int variant,
                                 int maxTiles,
                                 int maxSheetSize,
                                 const std::string& outputDir,
                                 bool writeEqe1,
                                 bool verbose,
                                 int physicalTileCap,
                                 int seamPad,
                                 bool checkAnimatedTextures) {
    ModelResult result;
    result.raceName = toUpper(model->name);
    result.gender = 0;  // TODO: parse from model name if needed
    result.variant = variant;

    // Collect UV extents per texture across all parts
    std::map<std::string, BodyPartTexture> bodyParts;

    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;

        const auto& texNames = part->textureNames();
        const auto& texAnims = part->textureAnimations();  // same indexing as texNames()
        const auto& texMasked = part->textureMasked();     // same indexing as texNames()

        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;

            std::string texName = toLower(texNames[tri.textureIndex]);
            auto& bp = bodyParts[texName];
            if (bp.textureName.empty()) {
                bp.textureName = texName;
            }
            // P48: WLD-masked detection, unconditional (unlike the animated-texture
            // check, which is gated on checkAnimatedTextures to keep the global
            // --race/--all path's byte-identical guarantee) — this signal only ever
            // affects OUTPUT (whether an alpha companion page gets written), never
            // which models get discovered/built, so there is no equivalent byte-
            // identity risk to gate against for the existing global path.
            if (tri.textureIndex < texMasked.size() && texMasked[tri.textureIndex]) {
                bp.isMasked = true;
            }
            if (checkAnimatedTextures && tri.textureIndex < texAnims.size() &&
                texAnims[tri.textureIndex].isAnimated) {
                bp.isAnimated = true;
            }

            for (uint32_t vi : {tri.v1, tri.v2, tri.v3}) {
                if (vi >= part->vertices.size()) continue;
                const auto& v = part->vertices[vi];
                bp.minU = std::min(bp.minU, v.u);
                bp.maxU = std::max(bp.maxU, v.u);
                bp.minV = std::min(bp.minV, v.v);
                bp.maxV = std::max(bp.maxV, v.v);
            }
        }
    }

    if (bodyParts.empty()) {
        if (verbose) std::cerr << "  No body-part textures found for " << model->name << std::endl;
        return result;
    }

    // P08f plan A3: animated-texture exclusion (the Z35-class builder gap). Baking a
    // single frame of a multi-frame material would silently freeze it — skip the whole
    // model instead and let the caller report it as ineligible-by-animation.
    if (checkAnimatedTextures) {
        for (const auto& [key, bp] : bodyParts) {
            if (bp.isAnimated) {
                result.skippedAnimated = true;
                result.animatedTextureName = key;
                if (verbose) {
                    std::cerr << "  SKIP (animated texture " << key << "): " << model->name << std::endl;
                }
                return result;
            }
        }
    }

    // Get texture dimensions and apply variant substitution
    std::map<std::string, std::pair<int,int>> texDimCache;
    auto getTexDims = [&](const std::string& name) -> std::pair<int,int> {
        auto it = texDimCache.find(name);
        if (it != texDimCache.end()) return it->second;
        int w = 0, h = 0;
        getTextureDimensions(archive, name, w, h);
        texDimCache[name] = {w, h};
        return {w, h};
    };

    // Resolve textures with dimensions and variant substitution
    std::vector<BodyPartTexture> resolved;
    int skippedTiles = 0;

    int skippedHelm = 0;
    for (auto& [key, bp] : bodyParts) {
        // Filter out helmet model textures — these are rendered as separate
        // equipment draw calls, not applied to the base/robe body mesh.
        {
            const std::string& name = bp.textureName;
            if (name.substr(0, 4) == "helm" ||
                name.substr(0, 11) == "helmleather" ||
                name.substr(0, 7) == "magecap") {
                if (verbose) std::cerr << "    Skip (helmet model): " << name << std::endl;
                skippedHelm++;
                continue;
            }
        }

        // Save original name before variant substitution (for JSON lookup key)
        std::string originalName = bp.textureName;
        // Apply variant substitution
        std::string variantName = applyVariant(bp.textureName, variant);
        // Check if variant texture exists
        std::vector<char> testData;
        if (variant > 0 && archive.get(variantName, testData)) {
            bp.textureName = variantName;
        }
        bp.originalName = originalName;

        auto dims = getTexDims(bp.textureName);
        bp.texWidth = dims.first;
        bp.texHeight = dims.second;

        if (bp.texWidth <= 0 || bp.texHeight <= 0) {
            if (verbose) std::cerr << "    Skip (no dims): " << bp.textureName << std::endl;
            continue;
        }

        // Check tile count against max
        if (bp.tilesU() > maxTiles || bp.tilesV() > maxTiles) {
            if (verbose) {
                std::cerr << "    Skip (tiles " << bp.tilesU() << "x" << bp.tilesV()
                          << " > " << maxTiles << "): " << bp.textureName << std::endl;
            }
            skippedTiles++;
            continue;
        }

        resolved.push_back(bp);
    }

    if (resolved.empty()) {
        if (verbose) std::cerr << "  No valid textures for " << model->name << std::endl;
        return result;
    }

    if ((skippedTiles > 0 || skippedHelm > 0) && verbose) {
        if (skippedTiles > 0)
            std::cerr << "  " << skippedTiles << " textures skipped (exceeded max tiles)" << std::endl;
        if (skippedHelm > 0)
            std::cerr << "  " << skippedHelm << " textures skipped (helmet models)" << std::endl;
    }

    // Build placement list with unrolled dimensions.
    //
    // P16 unroll discipline: tilesU()/tilesV() (the LOGICAL tile count) reflect
    // the actual max UV extent the geometry uses — that part was already
    // correct. The inflation P13 measured (3.25x avg, up to 6.06x) comes from
    // physically duplicating that many pixel-identical copies of the source
    // texture into the sheet. Since the source textures are GL_REPEAT-tileable
    // by construction (that's why the geometry's UVs exceed [0,1] at all), a
    // capped PHYSICAL copy count reproduces pixel-identical output to the full
    // unroll as long as sampling wraps modulo the physical count instead of
    // dividing by the logical one — see the matching runtime change in
    // mesh_building.cpp (fractU = fmod(shiftedU, tilesU) / tilesU). tilesU in
    // the JSON therefore now means "physical copies stored / wrap divisor",
    // capped at physicalTileCap; the shift math (which only depends on
    // floor(minU), independent of this field) is unaffected.
    std::vector<PlacedTexture> placements;
    for (const auto& bp : resolved) {
        PlacedTexture pt;
        pt.textureName = bp.textureName;
        pt.originalName = bp.originalName.empty() ? bp.textureName : bp.originalName;
        pt.tilesU = std::min(bp.tilesU(), std::max(1, physicalTileCap));
        pt.tilesV = std::min(bp.tilesV(), std::max(1, physicalTileCap));
        pt.w = bp.texWidth * pt.tilesU;
        pt.h = bp.texHeight * pt.tilesV;
        pt.origW = bp.texWidth;
        pt.origH = bp.texHeight;
        pt.isMasked = bp.isMasked;
        placements.push_back(pt);
    }

    // Pack into composite sheet (each placement reserves a seamPad-texel gutter)
    int sheetW = 0, sheetH = 0;
    if (!shelfPack(placements, maxSheetSize, sheetW, sheetH)) {
        std::cerr << "  FAILED to pack " << model->name << " (variant " << variant << ")" << std::endl;
        return result;
    }

    result.sheetW = sheetW;
    result.sheetH = sheetH;
    result.placements = placements;

    if (verbose) {
        std::cout << "  " << model->name << " variant=" << variant
                  << " textures=" << placements.size()
                  << " sheet=" << sheetW << "x" << sheetH << std::endl;
        for (const auto& pt : placements) {
            std::cout << "    " << pt.textureName
                      << " @ (" << pt.x << "," << pt.y << ") "
                      << pt.w << "x" << pt.h
                      << " tiles=" << pt.tilesU << "x" << pt.tilesV << std::endl;
        }
    }

    // Render composite sheet
    std::vector<uint8_t> sheetRGBA(sheetW * sheetH * 4, 0);

    bool sheetHasAlpha = false;
    for (const auto& pt : placements) {
        // Load and decode source texture. P48: WLD-masked placements decode with
        // the palette-index-0 -> alpha=0 convention (mirrors eqe1_builder.cpp);
        // unmasked placements are untouched (applyPaletteMask=false, same as
        // pre-P48 — alpha stays a constant 255).
        std::vector<uint8_t> srcRGBA;
        int srcW = 0, srcH = 0;
        if (!decodeTextureFromArchive(archive, pt.textureName, srcRGBA, srcW, srcH, pt.isMasked)) {
            std::cerr << "    WARNING: Cannot decode " << pt.textureName << std::endl;
            continue;
        }
        if (pt.isMasked) sheetHasAlpha = true;

        // Tile (unroll) the source texture into the sheet
        for (int ty = 0; ty < pt.tilesV; ++ty) {
            for (int tx = 0; tx < pt.tilesU; ++tx) {
                for (int py = 0; py < srcH; ++py) {
                    for (int px = 0; px < srcW; ++px) {
                        int dstX = pt.x + tx * srcW + px;
                        int dstY = pt.y + ty * srcH + py;
                        if (dstX >= sheetW || dstY >= sheetH) continue;

                        int srcIdx = (py * srcW + px) * 4;
                        int dstIdx = (dstY * sheetW + dstX) * 4;
                        sheetRGBA[dstIdx + 0] = srcRGBA[srcIdx + 0];
                        sheetRGBA[dstIdx + 1] = srcRGBA[srcIdx + 1];
                        sheetRGBA[dstIdx + 2] = srcRGBA[srcIdx + 2];
                        sheetRGBA[dstIdx + 3] = srcRGBA[srcIdx + 3];
                    }
                }
            }
        }

        // P16 seam fix: overwrite the outer seamPad texels of this placement's
        // core rect in place with edge-duplicated interior content, so
        // bilinear sampling near a tile boundary never blends against a
        // neighboring placement's pixels. Clamp pad so the untouched interior
        // is always at least half of each dimension (degenerate for tiny
        // textures otherwise) — effPad is also what the JSON UV inset below
        // must agree with.
        int effPad = std::min({seamPad, pt.w / 4, pt.h / 4});
        if (effPad > 0) writeSeamInset(sheetRGBA, sheetW, sheetH, pt, effPad);
    }

    // Determine output basename
    std::string baseName = toLower(model->name) + "_" + std::to_string(result.gender)
                         + "_" + std::to_string(variant);

    // Write BMP
    std::string bmpPath = outputDir + "/" + baseName + ".bmp";
    if (!writeBMP(bmpPath, sheetRGBA, sheetW, sheetH)) {
        std::cerr << "  FAILED to write: " << bmpPath << std::endl;
        return result;
    }
    std::cout << "    BMP: " << bmpPath << " (" << sheetW << "x" << sheetH << ")" << std::endl;

    // Write UV mapping JSON
    Json::Value root;
    root["race"] = toUpper(model->name);
    root["gender"] = result.gender;
    root["variant"] = variant;
    root["sheetWidth"] = sheetW;
    root["sheetHeight"] = sheetH;
    // P48: sheet-capability gate signal — CompositeTextureData::hasAlpha
    // (runtime, composite_texture_data.h) reads this to decide eligibility;
    // true only when at least one placement was WLD-masked and therefore the
    // EQE1 output below carries a real alpha companion page. Sheets built by
    // a pre-P48 builder have no "hasAlpha" key at all — CompositeTextureData
    // defaults it false, which is exactly the safe "exclude" behavior for
    // stale/global sheets not yet regenerated (per the plan).
    root["hasAlpha"] = sheetHasAlpha;

    Json::Value textures(Json::objectValue);
    for (const auto& pt : placements) {
        // P16: uvScale/uvOffset key off the INSET interior rect (see
        // writeSeamInset above), not the full placement — this is what keeps
        // sampled UVs off the edge-duplicated seam border. Must use the same
        // effPad formula as the pixel-writing loop.
        int effPad = std::min({seamPad, pt.w / 4, pt.h / 4});
        int ix = pt.x + effPad, iy = pt.y + effPad;
        int iw = pt.w - 2 * effPad, ih = pt.h - 2 * effPad;

        Json::Value entry;
        entry["x"] = pt.x;
        entry["y"] = pt.y;
        entry["w"] = pt.w;
        entry["h"] = pt.h;
        entry["tilesU"] = pt.tilesU;
        entry["tilesV"] = pt.tilesV;
        entry["seamPad"] = effPad;
        entry["uvScaleU"] = static_cast<double>(iw) / sheetW;
        entry["uvScaleV"] = static_cast<double>(ih) / sheetH;
        entry["uvOffsetU"] = static_cast<double>(ix) / sheetW;
        entry["uvOffsetV"] = static_cast<double>(iy) / sheetH;
        // Key by original (geometry) name so the mesh builder can look up textures
        // using the names from the model data, regardless of variant substitution.
        std::string jsonKey = pt.originalName.empty() ? pt.textureName : pt.originalName;
        textures[jsonKey] = entry;
    }
    root["textures"] = textures;

    std::string jsonPath = outputDir + "/" + baseName + ".json";
    std::ofstream jsonOut(jsonPath);
    if (jsonOut.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &jsonOut);
        jsonOut << std::endl;
        std::cout << "    JSON: " << jsonPath << std::endl;
    }

    // Optional EQE1 output
#ifdef EQT_HAS_ETC2COMP
    if (writeEqe1) {
        std::string eqe1Path = outputDir + "/" + baseName + ".eqe1";
        writeEQE1(eqe1Path, sheetRGBA, sheetW, sheetH, sheetHasAlpha);
    }
#else
    if (writeEqe1) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "WARNING: --eqe1 requested but ETC2Comp not available. Skipping." << std::endl;
            warned = true;
        }
    }
#endif

    result.sheetHasAlpha = sheetHasAlpha;
    result.success = true;
    return result;
}

}  // namespace ecb
