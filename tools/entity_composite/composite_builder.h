// tools/entity_composite/composite_builder.h -- the seam between the parts of
// the entity composite texture builder.
//
// T385 (2026-09-05, board B167): tools/entity_composite_builder.cpp was 1,861
// lines, over the ceiling that check_file_sizes.sh did not apply to tools/
// until this unit. Split by function into three TUs plus main(); this header
// carries the shared value types and the functions that cross a TU boundary.
// Everything is inside `namespace ecb` because the tool's helpers have generic
// names (toLower, toUpper, applyVariant) and it links against most of
// src/client/graphics.

#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"

#ifdef EQT_HAS_ETC2COMP
#include "client/graphics/eqe1_format.h"
#include <Etc.h>
#include <EtcImage.h>
#endif

#include <json/json.h>

using namespace EQT::Graphics;
namespace fs = std::filesystem;

namespace ecb {

// ── Per-body-part texture record, built while scanning a model's geometry ────

struct BodyPartTexture {
    std::string textureName;      // Texture filename (may be variant-substituted)
    std::string originalName;     // Original name from model geometry (set during resolve)
    float minU = 1e9f, maxU = -1e9f;
    float minV = 1e9f, maxV = -1e9f;
    int texWidth = 0, texHeight = 0;
    bool isAnimated = false;      // P08f: true if any triangle referencing this texture
                                   // uses an animated (multi-frame) material (0x30/0x31 fragment)
    bool isMasked = false;        // P48: true if any triangle referencing this texture is
                                   // WLD Fragment 0x30 TransparentMasked (part->textureMasked(),
                                   // same parallel-array indexing as textureAnimations() above)

    int tilesU() const { return std::max(1, static_cast<int>(std::ceil(maxU - minU))); }
    int tilesV() const { return std::max(1, static_cast<int>(std::ceil(maxV - minV))); }
    int unrolledW() const { return texWidth * tilesU(); }
    int unrolledH() const { return texHeight * tilesV(); }
};

// ── Placement rect in composite sheet ───────────────────────────────────────

struct PlacedTexture {
    std::string textureName;      // Actual texture file used (may be variant-substituted)
    std::string originalName;     // Original name from model geometry (for UV lookup key)
    int x = 0, y = 0;            // Position in sheet
    int w = 0, h = 0;            // Unrolled size
    int tilesU = 1, tilesV = 1;  // Tile counts
    int origW = 0, origH = 0;    // Original texture size
    bool isMasked = false;       // P48: propagated from BodyPartTexture.isMasked
};

// ── BMP writer ──────────────────────────────────────────────────────────────

struct ModelResult {
    std::string raceName;
    int gender = 0;
    int variant = 0;
    int sheetW = 0, sheetH = 0;
    std::vector<PlacedTexture> placements;
    bool success = false;
    // P08f: animated-texture exclusion (plan A3, the Z35-class builder gap).
    // Set when checkAnimatedTextures was requested and a used body-part texture
    // is a multi-frame animated material — the model is skipped entirely rather
    // than silently baking frame 0.
    bool skippedAnimated = false;
    std::string animatedTextureName;
    // P48: true if any placement in this sheet is WLD-masked — the sheet gets a
    // real ETC1 alpha companion page (mirrors the zone ETC1 companion convention)
    // when this is set; otherwise output is byte-identical to pre-P48.
    bool sheetHasAlpha = false;
};


struct ArchiveModels {
    std::string archiveName;
    std::string archivePath;
    std::vector<std::shared_ptr<CharacterModel>> models;
};

struct RegisteredModel {
    std::string sourceArchive;
    std::vector<std::string> signature;
};

std::string toLower(std::string s) ;

std::string toUpper(std::string s) ;

bool decodeBMP(const std::vector<char>& data,
                      std::vector<uint8_t>& outRGBA,
                      int& outWidth, int& outHeight,
                      bool applyPaletteMask = false) ;

bool writeBMP(const std::string& path, const std::vector<uint8_t>& rgba,
                     int width, int height) ;

bool shelfPack(std::vector<PlacedTexture>& textures, int maxSheetSize,
                       int& sheetW, int& sheetH) ;

void writeSeamInset(std::vector<uint8_t>& sheetRGBA, int sheetW, int /*sheetH*/,
                            const PlacedTexture& pt, int pad) ;

bool writeEQE1(const std::string& outputPath,
                       const std::vector<uint8_t>& sheetRGBA,
                       int sheetW, int sheetH,
                       bool hasAlpha = false) ;

std::string applyVariant(const std::string& texName, int variant) ;

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
                                 int physicalTileCap = 1,
                                 int seamPad = 2,
                                 bool checkAnimatedTextures = false) ;

std::vector<ArchiveModels> loadGlobalArchives(const std::string& eqPath,
                                                      const std::string& filterRace,
                                                      bool verbose) ;

std::optional<ArchiveModels> loadZoneArchive(const std::string& eqPath,
                                                     const std::string& zoneShortName,
                                                     bool verbose) ;

std::vector<std::string> collectSignature(const std::shared_ptr<CharacterModel>& model) ;

void registerArchives(const std::vector<ArchiveModels>& archives,
                              std::map<std::string, RegisteredModel>& registry) ;

std::vector<int> discoverVariants(PfsArchive& archive,
                                          const std::shared_ptr<CharacterModel>& model,
                                          int maxVariant) ;

bool detectAnimatedTexture(const std::shared_ptr<CharacterModel>& model,
                                   std::string& outTextureName) ;

void mergeCompositeIndex(const std::string& outputDir,
                                 const std::vector<ModelResult>& newResults) ;

void loadPersistedRegistry(const std::string& outputDir,
                                   std::map<std::string, RegisteredModel>& registry) ;

void savePersistedRegistry(const std::string& outputDir,
                                   const std::map<std::string, RegisteredModel>& registry,
                                   const std::map<std::string, RegisteredModel>& globalOnly) ;

std::vector<std::string> loadZoneShortNames(const std::string& zonesJsonPath) ;

}  // namespace ecb
