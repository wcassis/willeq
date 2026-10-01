// p50_mask_plumbing_probe — evidence tool for P50 (refactor/P50_mask_plumbing_gaps.md)
//
// P46d Part B found two pre-existing gaps where applyPaletteMask never
// reaches loadTextureFromBMP()/getOrLoadTexture():
//   Gap 1: mesh_building.cpp's buildMeshFromGeometry() on-demand fallback
//          (getOrLoadTexture()/loadTextureFromBMP()), reached whenever this
//          race/gender's per-part textures were not already decoded through
//          a texture cache this call can see -- in production this is the
//          P13b composite-deferral case (deferredPerPartTextureNames_).
//   Gap 2: animated_mesh_creation.cpp's per-slot equipment texture-variant
//          swap (materialId != 0).
//
// Investigation note (recorded here since it changed this tool's design):
// race_model_loader.cpp's composite-eligibility check (preloadModelData,
// "composite EXCLUDED ... source includes WLD-masked texture") means a race
// with a WLD-masked texture baked into its own composite sheet NEVER gets a
// composite hit -- so P37 ground-truth masked races (SKE/ZOM) never
// naturally exercise the P13b per-part deferral for THEIR masked textures
// through the composite path specifically. Gap 1 is still real (a masked
// texture excluded from the composite sheet itself, or any future
// composite-eligible masked race, would still hit
// deferredPerPartTextureNames_ and this exact fallback) -- but reproducing
// it end-to-end via a live composite hit on today's data isn't possible.
// Instead, this tool exercises the FIXED CODE PATH directly and
// deterministically: it builds a mesh from a race's real geometry + real raw
// per-part texture bytes (extracted via preloadModelData/T05 on one
// RaceModelLoader instance) through a SECOND, freshly-constructed
// RaceModelLoader/ConstrainedTextureCache pair that has never seen these
// textures before -- reproducing the exact "not yet resolved, must fall
// through to loadTextureFromBMP()" state P13b's deferral produces, using
// the SAME buildMeshFromGeometry() call gap 1's fix lives in.
//
// Usage:
//   p50_mask_plumbing_probe --eq-path <EQ client dir> [--race <id>] [--gender <0|1>]
//
// Default race 70 (ZOM) -- P37 ground truth: zombie chest/legs are masked.
// Race 21 (SKE, forearms/shins masked) also works via --race 21.

#include "common/logging.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/willeq_database.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance

#include <irrlicht.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <memory>
#include <unistd.h>

using namespace EQT::Graphics;

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// ConstrainedTextureCache's public hasAlpha(name)/getTexture(name) query the
// SAME cache_ map that uploadTexture(name, applyPaletteMask) writes to under
// cacheKey(name, applyPaletteMask) -- for applyPaletteMask=true that's
// `name + "\x01masked"` (constrained_texture_cache.cpp, ConstrainedTextureCache::
// cacheKey(), private -- this mirrors its documented format), NOT the plain
// name. The public query methods take only `name` with no mask parameter and
// look it up literally, so a plain-name query for a masked-variant entry
// always misses -- a separate, pre-existing gap in the read-side query API
// discovered while writing this probe (see file-header addendum below).
// Passing the pre-mangled key string as the `name` argument is the only way
// to reach the masked variant's cache_ entry through the public API.
static std::string maskedKey(const std::string& name) {
    return name + "\x01masked";
}

int main(int argc, char** argv) {
    InitLogging(argc, argv);
    std::string eqPath;
    uint16_t raceId = 70;  // ZOM
    uint8_t gender = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eq-path") && i + 1 < argc) eqPath = argv[++i];
        else if (!strcmp(argv[i], "--race") && i + 1 < argc) raceId = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gender") && i + 1 < argc) gender = (uint8_t)atoi(argv[++i]);
    }

    if (eqPath.empty()) {
        fprintf(stderr, "Usage: %s --eq-path <dir> [--race N] [--gender N]\n", argv[0]);
        return 2;
    }

    // Race S3D file lookup (getRaceS3DFile, race_codes.cpp) is served from
    // WilleqDatabase's on-demand SQLite lookup — without opening it, a race
    // like ZOM's canonical model archive (befallen_chr.s3d, per
    // config/race_models.json) is never found and preloadModelData() fails
    // outright. Mirrors Application::run()'s
    // EverQuest::OpenDatabase("data/willeq.db") call.
    EQT::WilleqDatabase willeqDb;
    if (willeqDb.open("data/willeq.db")) {
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb);
        printf("Opened data/willeq.db\n");
    } else {
        fprintf(stderr, "WARNING: could not open data/willeq.db -- race S3D lookup will fail\n");
    }

    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    params.Fullscreen = true;
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: failed to create Irrlicht device\n");
        return 1;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    irr::scene::ISceneManager* smgr = device->getSceneManager();
    std::string raceCode = toLower(RaceModelLoader::getRaceCode(raceId));

    // ===== Loader #1: real production load (source of truth geometry + raw
    // per-part texture bytes, and Gap 2's evidence — its shared cached mesh
    // buffers get MaterialType set by the (now-fixed) buildMeshFromGeometry,
    // and the equipment-variant swap in createAnimatedNodeWithEquipment
    // reads that to decide isMaskedMaterial). =====
    ConstrainedRendererConfig config1 = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
    ConstrainedTextureCache textureCache1(config1);
    textureCache1.setDriver(driver);
    RaceModelLoader loader1(smgr, driver, device->getFileSystem());
    loader1.setClientPath(eqPath);
    loader1.setConstrainedTextureCache(&textureCache1);
    if (auto* mb1 = loader1.getMeshBuilder()) {
        mb1->setConstrainedTextureCache(&textureCache1);
        textureCache1.setMeshBuilder(mb1);
    }
    loader1.loadGlobalModels();
    loader1.loadNumberedGlobalModels();

    bool ok = loader1.preloadModelData(raceId, gender);
    loader1.promotePreparedModels();
    textureCache1.uploadAllPending();
    printf("preloadModelData(race=%d, gender=%d) = %s\n", raceId, gender, ok ? "OK" : "FAILED");

    auto modelData = loader1.getRaceModelData(raceId, gender);
    if (!modelData || !modelData->rawGeometry) {
        fprintf(stderr, "ERROR: no model data / raw geometry for race %d gender %d\n", raceId, gender);
        return 1;
    }
    bool compositeHit = (bool)modelData->compositeData;
    printf("Composite hit: %s (expected 'no' for a masked race -- see file header note)\n",
           compositeHit ? "YES" : "no");

    const auto& texNames = modelData->rawGeometry->textureNames();
    const auto& texMasked = modelData->rawGeometry->textureMasked();
    printf("\n--- Masked per-part textures for race=%d (%zu total textures) ---\n", raceId, texNames.size());
    std::string firstMaskedTex, firstUnmaskedTex;
    for (size_t i = 0; i < texNames.size(); ++i) {
        bool masked = i < texMasked.size() && texMasked[i];
        std::string lower = toLower(texNames[i]);
        if (masked) {
            printf("  masked texture[%zu]='%s'\n", i, lower.c_str());
            if (firstMaskedTex.empty()) firstMaskedTex = lower;
        } else if (firstUnmaskedTex.empty() && modelData->textures.count(lower)) {
            firstUnmaskedTex = lower;
        }
    }
    if (firstMaskedTex.empty()) {
        fprintf(stderr, "ERROR: race %d has no WLD-masked textures -- pick a different --race\n", raceId);
        return 1;
    }

    // Diagnostic: inspect the raw BMP header of firstMaskedTex directly.
    // decodeBMP()'s applyPaletteMask punch only fires for 8bpp paletted BMP
    // AND only when the pixel data actually contains palette index 0 -- a
    // masked WLD material with none of its pixels using palette slot 0 (or a
    // 24/32bpp source) will legitimately show hasAlpha=false regardless of
    // the fix (P46d's own "citywlivy class" note).
    {
        auto it = modelData->textures.find(firstMaskedTex);
        if (it != modelData->textures.end() && it->second && it->second->data.size() >= 30) {
            const auto& d = it->second->data;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(d.data());
            bool isBmpMagic = (d.size() >= 2 && p[0] == 'B' && p[1] == 'M');
            uint16_t bpp = *reinterpret_cast<const uint16_t*>(p + 28);
            uint32_t paletteEntries = (d.size() >= 50) ? *reinterpret_cast<const uint32_t*>(p + 46) : 0;
            int32_t bmpW = *reinterpret_cast<const int32_t*>(p + 18);
            int32_t bmpH = *reinterpret_cast<const int32_t*>(p + 22);
            uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(p + 10);
            int rowSize = ((bmpW + 3) / 4) * 4;
            int absH = bmpH < 0 ? -bmpH : bmpH;
            int zeroIdxCountActivePixels = 0, zeroIdxCountTotal = 0;
            if (bpp == 8) {
                for (int y = 0; y < absH; ++y) {
                    size_t rowStart = dataOffset + (size_t)y * rowSize;
                    for (int x = 0; x < bmpW && x < rowSize; ++x) {
                        size_t i = rowStart + x;
                        if (i < d.size()) {
                            if (static_cast<uint8_t>(d[i]) == 0) ++zeroIdxCountActivePixels;
                            ++zeroIdxCountTotal;
                        }
                    }
                }
            }
            printf("\n--- Raw BMP header for '%s' (%zu bytes) ---\n", firstMaskedTex.c_str(), d.size());
            printf("  magic=%s bitsPerPixel=%u paletteEntries=%u width=%d height=%d rowSize=%d dataOffset=%u\n",
                   isBmpMagic ? "BM" : "NOT-BMP", bpp, paletteEntries, bmpW, bmpH, rowSize, dataOffset);
            printf("  active-pixel scan: %d/%d pixels are palette index 0\n", zeroIdxCountActivePixels, zeroIdxCountTotal);
        } else {
            printf("\n--- Raw BMP header for '%s': NOT FOUND in modelData->textures ---\n", firstMaskedTex.c_str());
        }
    }

    // Isolation test: exercise ConstrainedTextureCache::getOrLoad()/
    // uploadTexture()/hasAlpha() directly on the raw bytes, bypassing all
    // RaceModelLoader/geometry texIdx plumbing entirely -- confirms whether
    // the fundamental decodeBMP(applyPaletteMask=true) -> hasAlpha mechanism
    // itself works for this exact texture, independent of Gap 1/Gap 2 code.
    {
        auto it = modelData->textures.find(firstMaskedTex);
        if (it != modelData->textures.end() && it->second) {
            ConstrainedRendererConfig config3 = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
            ConstrainedTextureCache textureCache3(config3);
            textureCache3.setDriver(driver);
            std::string isoName = "iso_" + firstMaskedTex;
            textureCache3.getOrLoad(isoName, it->second->data, "isolation_test", "probe", /*applyPaletteMask=*/true);
            irr::video::ITexture* isoTex = textureCache3.uploadTexture(isoName, /*applyPaletteMask=*/true);
            printf("\n--- Isolation test: getOrLoad+uploadTexture(applyPaletteMask=true) directly on '%s' ---\n",
                   firstMaskedTex.c_str());
            printf("  uploadTexture returned %s, hasAlpha=%s\n", isoTex ? "non-null" : "NULL",
                   textureCache3.hasAlpha(maskedKey(isoName)) ? "true (mechanism works)" : "false (mechanism itself produces no alpha for this data)");
        }
    }

    // Control: loader1's cache went through T05 (preloadModelData's own
    // pre-existing, already-correct masked decode loop -- composite was
    // excluded for this race, so T05 ran unconditionally). If hasAlpha() is
    // ALSO false here for the same texture names, the texture data itself
    // doesn't produce alpha (e.g. no palette-index-0 pixels present, or not
    // an 8bpp BMP) -- independent of this unit's fix -- and Gap 1's result
    // below must be read in that light rather than as a fix failure.
    printf("\n--- Control: T05's own masked decode (loader1/textureCache1, pre-existing, unrelated to this fix) ---\n");
    for (size_t i = 0; i < texNames.size(); ++i) {
        if (i < texMasked.size() && texMasked[i]) {
            std::string lower = toLower(texNames[i]);
            printf("  T05 masked texture[%zu]='%s' hasAlpha=%s\n", i, lower.c_str(),
                   textureCache1.hasAlpha(maskedKey(lower)) ? "true" : "false");
        }
    }

    // ===== GAP 1: fresh cache/loader, never seen these textures before =====
    // ensureModelDataLoaded() (called internally by createAnimatedNode() via
    // getAnimatedMeshForRace()) uses its own JSON-driven S3D load path,
    // entirely independent of preloadModelData()/T05 -- it never sets
    // modelData->compositeData, so the composite branch is always skipped
    // and buildMeshFromGeometry() (this unit's Gap 1 fix) is always what
    // resolves these textures, with a texture cache that has never seen them
    // before -- reproducing the exact state P13b's deferral leaves a texture
    // in (raw bytes retained, nothing yet in any texture cache this call can
    // see).
    ConstrainedRendererConfig config2 = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
    ConstrainedTextureCache textureCache2(config2);
    textureCache2.setDriver(driver);
    RaceModelLoader loader2(smgr, driver, device->getFileSystem());
    loader2.setClientPath(eqPath);
    loader2.setConstrainedTextureCache(&textureCache2);
    if (auto* mb2 = loader2.getMeshBuilder()) {
        mb2->setConstrainedTextureCache(&textureCache2);
        textureCache2.setMeshBuilder(mb2);
    }

    auto* node2 = loader2.createAnimatedNode(raceId, gender, nullptr, -1);
    printf("\n--- Gap 1: fresh-cache createAnimatedNode() -> %s ---\n", node2 ? "built" : "FAILED");

    auto modelData2 = loader2.getRaceModelData(raceId, gender);
    bool gap1CompositeHit = modelData2 && (bool)modelData2->compositeData;
    printf("Gap 1 loader's own composite hit: %s (must be 'no' -- ensureModelDataLoaded never sets compositeData)\n",
           gap1CompositeHit ? "YES (INVALID SCENARIO)" : "no");
    if (modelData2) {
        printf("loader2 modelData2->textures.size()=%zu, has '%s'=%s\n",
               modelData2->textures.size(), firstMaskedTex.c_str(),
               modelData2->textures.count(firstMaskedTex) ? "yes" : "NO");
    }

    // Debug: how many triangles actually reference each masked texIdx in
    // loader1's rawGeometry (a masked texIdx with zero triangles never gets
    // a load call at all -- not a fix failure, just an orphan table entry).
    {
        std::vector<size_t> triCount(texNames.size(), 0);
        for (const auto& tri : modelData->rawGeometry->triangles) {
            if (tri.textureIndex < triCount.size()) ++triCount[tri.textureIndex];
        }
        printf("\n--- Triangle counts for masked texIdx (loader1 rawGeometry) ---\n");
        for (size_t i = 0; i < texNames.size(); ++i) {
            if (i < texMasked.size() && texMasked[i]) {
                printf("  texIdx=%zu name='%s' triangles=%zu\n", i, texNames[i].c_str(), triCount[i]);
            }
        }
    }

    int gap1Pass = 0, gap1Total = 0;
    for (size_t i = 0; i < texNames.size(); ++i) {
        if (i < texMasked.size() && texMasked[i]) {
            std::string lower = toLower(texNames[i]);
            bool hasAlpha = textureCache2.hasAlpha(maskedKey(lower));
            ++gap1Total;
            if (hasAlpha) ++gap1Pass;
            printf("  masked texture[%zu]='%s' post-on-demand-load hasAlpha=%s\n",
                   i, lower.c_str(), hasAlpha ? "true (PASS)" : "false (FAIL)");
        }
    }
    printf("Gap 1 result: %d/%d masked textures now carry mask alpha after on-demand load\n", gap1Pass, gap1Total);

    // Unmasked-texture byte-identity sanity: applyPaletteMask must stay
    // false (default) for unmasked textures resolved through the SAME
    // fallback -- hasAlpha must be false.
    int unmaskedChecked = 0, unmaskedFalsePositive = 0;
    for (size_t i = 0; i < texNames.size(); ++i) {
        bool masked = i < texMasked.size() && texMasked[i];
        if (!masked) {
            std::string lower = toLower(texNames[i]);
            if (textureCache2.getTexture(lower)) {
                ++unmaskedChecked;
                if (textureCache2.hasAlpha(lower)) ++unmaskedFalsePositive;
            }
        }
    }
    printf("Unmasked textures resolved through the same fallback: %d, false-positive hasAlpha: %d (expect 0)\n",
           unmaskedChecked, unmaskedFalsePositive);

    // ===== GAP 2: per-slot equipment texture-variant swap (loader1, real
    // production path -- ZOM/SKE always fall to the multi-buffer path since
    // they're composite-excluded, so no compositeAllowed override needed) =====
    EntityAppearance appearance;
    // race 60 = SKE (forearms/shins masked, P37), race 70 = ZOM (chest/legs
    // masked). EquipSlot: Head=0 Chest=1 Arms=2 Wrist=3 Hands=4 Legs=5 Feet=6.
    uint8_t maskedSlot = (raceId == 60) ? 3 /* Wrist/forearms, SKE */
                        : (raceId == 70) ? 5 /* Legs, ZOM */
                        : 1 /* Chest, generic fallback */;
    appearance.equipment[maskedSlot] = 1;  // non-default variant
    auto* eqNode = loader1.createAnimatedNodeWithEquipment(raceId, gender, appearance, nullptr, -1,
                                                             /*compositeAllowed=*/true);
    printf("\n--- Gap 2: createAnimatedNodeWithEquipment(equipment[%d]=1) -> %s ---\n",
           maskedSlot, eqNode ? "built" : "FAILED");

    // Ground truth: dump every material's actual texture name straight off
    // the built node -- driver_->addTexture() registers GPU textures under
    // ConstrainedTextureCache's internalKey (P46d), so a masked-variant
    // texture's Irrlicht name IS the mangled "name\x01masked" key; query
    // hasAlpha() with that exact string (no guessing needed).
    int gap2Found = 0, gap2Pass = 0;
    if (eqNode) {
        printf("  node material textures:\n");
        for (irr::u32 m = 0; m < eqNode->getMaterialCount(); ++m) {
            auto* tex = eqNode->getMaterial(m).getTexture(0);
            std::string texName = tex ? tex->getName().getPath().c_str() : "";
            bool isMaskedVariant = texName.find("masked") != std::string::npos && !texName.empty();
            printf("    [%u] %s%s\n", m, texName.empty() ? "(none)" : texName.c_str(),
                   isMaskedVariant ? "  <- masked-variant key" : "");
            // The equipped variant (variant "01") is what we're after --
            // {race}{part}{variant:02d}{page}.bmp, variant digits sit right
            // after the 3-letter race code + 2-letter part code (position
            // 5-6) -- distinct from the untouched "00" (base) buffers that
            // share this node.
            bool isVariant01 = isMaskedVariant && texName.size() > 6 && texName.substr(5, 2) == "01";
            if (isVariant01) {
                bool hasAlpha = textureCache1.hasAlpha(texName);
                ++gap2Found;
                if (hasAlpha) ++gap2Pass;
                printf("      -> variant candidate, hasAlpha=%s\n", hasAlpha ? "true (PASS)" : "false");
            }
        }
    }
    printf("Gap 2 result: %d variant-candidate masked texture(s) found on node, %d report hasAlpha=true\n",
           gap2Found, gap2Pass);

    printf("\n=== SUMMARY ===\n");
    printf("Gap 1: %s\n", (gap1Total > 0 && gap1Pass == gap1Total && unmaskedFalsePositive == 0)
                               ? "PASS" : "INCONCLUSIVE/FAIL (see detail above)");
    printf("Gap 2: %s\n", (gap2Pass > 0) ? "PASS (at least one masked variant carries alpha)"
                                          : "INCONCLUSIVE/FAIL (see detail above)");

    // Intentionally skip device->drop()/cleanup — two independently-owned
    // RaceModelLoader/ConstrainedTextureCache pairs sharing one Irrlicht
    // device's destruction order segfaults (same rationale as
    // zone_bench_loader.cpp's ZoneBenchData destructor comment). Short-lived
    // evidence tool; let the OS reclaim everything. Flush the threaded
    // logger explicitly first since LOG_INFO/DEBUG output would otherwise be
    // lost if any teardown path aborts before it's flushed.
    FlushThreadLog();
    fflush(stdout);
    _exit(0);
}
