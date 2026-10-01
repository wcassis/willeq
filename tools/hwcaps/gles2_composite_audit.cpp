// gles2_composite_audit — Visual audit of composite entity textures
//
// Renders each race/gender/variant combo at 512x512 and saves as PNG.
// Uses a lightweight loader that builds dynamic ETC1 atlases per entity —
// no zone data loaded, minimal memory footprint.
//
// Usage:
//   gles2_composite_audit --drm --eq-path /path/to/EQ --eqe1-path /path/to/ETC1 --output audit/
//   gles2_composite_audit --drm --eq-path /path/to/EQ --eqe1-path /path/to/ETC1 --output audit/ --race 1

#include "common/logging.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/composite_texture_data.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes);

// ============================================================================
// Options
// ============================================================================

struct AuditOptions {
    std::string eqPath;
    std::string eqe1Path;
    std::string outputDir = "audit";
    int rttSize = 512;
    int filterRace = 0;
};

static void printUsage(const char* prog) {
    printf("Usage: %s --drm --eq-path <path> --eqe1-path <path> [options]\n\n", prog);
    printf("  --eq-path <path>     EverQuest client directory\n");
    printf("  --eqe1-path <path>   EQE1/ETC1 texture directory\n");
    printf("  --output <dir>       Output directory (default: audit/)\n");
    printf("  --race <N>           Single race ID (default: all 1-12)\n");
    printf("  --drm                Use DRM/KMS display\n");
}

static AuditOptions parseArgs(int argc, char** argv) {
    LOG_DEBUG(MOD_GRAPHICS, "parseArgs: argc={}", argc);
    AuditOptions opts;
    for (int i = 1; i < argc; ++i) {
        LOG_TRACE(MOD_GRAPHICS, "parseArgs: argv[{}]=\"{}\"", i, argv[i]);
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) {
            opts.eqPath = argv[++i];
            LOG_DEBUG(MOD_GRAPHICS, "parseArgs: eqPath=\"{}\"", opts.eqPath);
        }
        else if (strcmp(argv[i], "--eqe1-path") == 0 && i + 1 < argc) {
            opts.eqe1Path = argv[++i];
            LOG_DEBUG(MOD_GRAPHICS, "parseArgs: eqe1Path=\"{}\"", opts.eqe1Path);
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            opts.outputDir = argv[++i];
            LOG_DEBUG(MOD_GRAPHICS, "parseArgs: outputDir=\"{}\"", opts.outputDir);
        }
        else if (strcmp(argv[i], "--race") == 0 && i + 1 < argc) {
            opts.filterRace = atoi(argv[++i]);
            LOG_DEBUG(MOD_GRAPHICS, "parseArgs: filterRace={}", opts.filterRace);
        }
        else if (strcmp(argv[i], "--drm") == 0) {
            LOG_TRACE(MOD_GRAPHICS, "parseArgs: --drm flag (consumed by device creation)");
        }
        else if (strncmp(argv[i], "--log-", 6) == 0) {
            LOG_TRACE(MOD_GRAPHICS, "parseArgs: logging flag \"{}\" (handled by InitLogging)", argv[i]);
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            LOG_DEBUG(MOD_GRAPHICS, "parseArgs: help requested, exiting");
            printUsage(argv[0]); exit(0);
        }
        else {
            LOG_WARN(MOD_GRAPHICS, "parseArgs: unknown argument \"{}\"", argv[i]);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "parseArgs: final opts eqPath=\"{}\" eqe1Path=\"{}\" outputDir=\"{}\" filterRace={} rttSize={}",
              opts.eqPath, opts.eqe1Path, opts.outputDir, opts.filterRace, opts.rttSize);
    return opts;
}

// ============================================================================
// Dynamic ETC1 Atlas Builder
// ============================================================================

struct AtlasPlacement {
    std::string textureName;
    int x = 0, y = 0, w = 0, h = 0;
    int tilesU = 1, tilesV = 1;
    int srcW = 0, srcH = 0;
};

static bool shelfPack(std::vector<AtlasPlacement>& placements, int maxSize,
                      int& outW, int& outH) {
    LOG_DEBUG(MOD_GRAPHICS, "shelfPack: {} placements, maxSize={}", placements.size(), maxSize);
    std::sort(placements.begin(), placements.end(),
              [](const AtlasPlacement& a, const AtlasPlacement& b) { return a.h > b.h; });
    LOG_TRACE(MOD_GRAPHICS, "shelfPack: sorted by height descending, tallest={}",
              placements.empty() ? 0 : placements.front().h);
    for (int tryW = 256; tryW <= maxSize; tryW *= 2) {
        LOG_TRACE(MOD_GRAPHICS, "shelfPack: trying width={}", tryW);
        int curX = 0, curY = 0, shelfH = 0, maxY = 0;
        bool fits = true;
        for (size_t pi = 0; pi < placements.size(); ++pi) {
            auto& p = placements[pi];
            int aW = (p.w + 3) & ~3, aH = (p.h + 3) & ~3;
            LOG_TRACE(MOD_GRAPHICS, "shelfPack:   [{}] \"{}\" w={} h={} aligned={}x{} curX={} curY={} shelfH={}",
                      pi, p.textureName, p.w, p.h, aW, aH, curX, curY, shelfH);
            if (curX + aW > tryW) {
                LOG_TRACE(MOD_GRAPHICS, "shelfPack:   new shelf: curX=0, curY += shelfH={}", shelfH);
                curX = 0; curY += shelfH; shelfH = 0;
            }
            if (curY + aH > maxSize) {
                LOG_TRACE(MOD_GRAPHICS, "shelfPack:   overflow: curY({}) + aH({}) > maxSize({})", curY, aH, maxSize);
                fits = false; break;
            }
            p.x = curX; p.y = curY;
            LOG_TRACE(MOD_GRAPHICS, "shelfPack:   placed at ({}, {})", p.x, p.y);
            curX += aW; shelfH = std::max(shelfH, aH);
            maxY = std::max(maxY, curY + aH);
        }
        if (fits) {
            outW = tryW; outH = 1;
            while (outH < maxY) outH *= 2;
            if (outH > maxSize) outH = maxSize;
            LOG_DEBUG(MOD_GRAPHICS, "shelfPack: success -> {}x{} (maxY={})", outW, outH, maxY);
            return true;
        }
        LOG_TRACE(MOD_GRAPHICS, "shelfPack: width={} did not fit, trying next", tryW);
    }
    LOG_WARN(MOD_GRAPHICS, "shelfPack: FAILED — {} placements could not fit in maxSize={}", placements.size(), maxSize);
    return false;
}

static void blitETC1Blocks(const uint8_t* src, int srcW, int srcH,
                           uint8_t* dst, int dstW, int dstH,
                           int dstX, int dstY, int tilesU, int tilesV) {
    LOG_TRACE(MOD_GRAPHICS, "blitETC1Blocks: src={}x{} dst={}x{} at ({},{}) tiles={}x{}",
              srcW, srcH, dstW, dstH, dstX, dstY, tilesU, tilesV);
    int srcBW = srcW / 4, srcBH = srcH / 4, dstBW = dstW / 4;
    LOG_TRACE(MOD_GRAPHICS, "blitETC1Blocks: srcBlocks={}x{} dstBlocksW={}", srcBW, srcBH, dstBW);
    int blocksCopied = 0, blocksSkipped = 0;
    for (int ty = 0; ty < tilesV; ++ty)
        for (int tx = 0; tx < tilesU; ++tx)
            for (int by = 0; by < srcBH; ++by) {
                int dstBy = (dstY / 4) + ty * srcBH + by;
                int dstBx = (dstX / 4) + tx * srcBW;
                if (dstBy >= dstH / 4 || dstBx + srcBW > dstBW) {
                    blocksSkipped++;
                    continue;
                }
                memcpy(dst + (dstBy * dstBW + dstBx) * 8,
                       src + by * srcBW * 8, srcBW * 8);
                blocksCopied++;
            }
    LOG_TRACE(MOD_GRAPHICS, "blitETC1Blocks: copied {} block rows, skipped {} (out-of-bounds)",
              blocksCopied, blocksSkipped);
}

static std::string applyVariant(const std::string& name, int variant) {
    LOG_TRACE(MOD_GRAPHICS, "applyVariant: name=\"{}\" variant={}", name, variant);
    if (variant == 0) {
        LOG_TRACE(MOD_GRAPHICS, "applyVariant: variant==0, returning unchanged \"{}\"", name);
        return name;
    }
    std::string stem = name;
    auto dot = stem.rfind('.');
    std::string ext = (dot != std::string::npos) ? name.substr(dot) : "";
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    LOG_TRACE(MOD_GRAPHICS, "applyVariant: stem=\"{}\" ext=\"{}\" stemLen={}", stem, ext, stem.size());
    if (stem.size() < 7) {
        LOG_TRACE(MOD_GRAPHICS, "applyVariant: stem too short (<7), returning unchanged \"{}\"", name);
        return name;
    }
    char v[3]; std::snprintf(v, sizeof(v), "%02d", variant);
    std::string result = stem.substr(0, 5) + v + stem.substr(7) + ext;
    LOG_TRACE(MOD_GRAPHICS, "applyVariant: result=\"{}\" (replaced chars 5-6 with \"{}\")", result, v);
    return result;
}

static const EQT::Graphics::EQE1Loader::TextureEntry* lookupETC1(
    const std::string& name, const std::vector<EQT::Graphics::EQE1Loader*>& loaders) {
    LOG_TRACE(MOD_GRAPHICS, "lookupETC1: name=\"{}\" across {} loaders", name, loaders.size());
    std::string n = name;
    auto dot = n.rfind('.');
    if (dot != std::string::npos) {
        n = n.substr(0, dot);
        LOG_TRACE(MOD_GRAPHICS, "lookupETC1: stripped extension -> \"{}\"", n);
    }
    for (size_t li = 0; li < loaders.size(); ++li) {
        auto* e = loaders[li]->lookup(n);
        if (e) {
            LOG_TRACE(MOD_GRAPHICS, "lookupETC1: FOUND \"{}\" in loader[{}] ({}x{}, {}B)",
                      n, li, e->width, e->height, e->dataSize);
            return e;
        }
    }
    LOG_TRACE(MOD_GRAPHICS, "lookupETC1: NOT FOUND \"{}\"", n);
    return nullptr;
}

struct DynamicAtlas {
    std::vector<uint8_t> etc1Data;
    int width = 0, height = 0;
    std::vector<AtlasPlacement> placements;
    EQT::Graphics::CompositeTextureData compositeData;
};

static bool buildDynamicAtlas(
    const std::shared_ptr<EQT::Graphics::ZoneGeometry>& geometry,
    const std::vector<EQT::Graphics::EQE1Loader*>& loaders,
    int variant, DynamicAtlas& out) {
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: variant={} geometry={} loaders={}",
              variant, (geometry ? "valid" : "null"), loaders.size());
    if (!geometry) {
        LOG_WARN(MOD_GRAPHICS, "buildDynamicAtlas: null geometry, returning false");
        return false;
    }

    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: geometry has {} triangles, {} vertices, {} textures",
              geometry->triangles.size(), geometry->vertices.size(), geometry->textureNames().size());

    struct TI { std::string name; float minU=1e9f, maxU=-1e9f, minV=1e9f, maxV=-1e9f; };
    std::map<std::string, TI> infos;
    int skippedTexIdx = 0, skippedHelm = 0, skippedVertIdx = 0;
    for (size_t ti = 0; ti < geometry->triangles.size(); ++ti) {
        const auto& tri = geometry->triangles[ti];
        if (tri.textureIndex >= geometry->textureNames().size()) {
            skippedTexIdx++;
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: tri[{}] textureIndex={} >= textureNames.size()={}, skipping",
                      ti, tri.textureIndex, geometry->textureNames().size());
            continue;
        }
        std::string tn = geometry->textureNames()[tri.textureIndex];
        std::transform(tn.begin(), tn.end(), tn.begin(), [](unsigned char c){return std::tolower(c);});
        if (tn.substr(0,4)=="helm" || tn.substr(0,7)=="magecap") {
            skippedHelm++;
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: tri[{}] skipping helm/magecap texture \"{}\"", ti, tn);
            continue;
        }
        auto& i = infos[tn]; if (i.name.empty()) i.name = tn;
        for (uint32_t vi : {tri.v1, tri.v2, tri.v3}) {
            if (vi >= geometry->vertices.size()) {
                skippedVertIdx++;
                LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: tri[{}] vertex index {} >= vertices.size()={}, skipping",
                          ti, vi, geometry->vertices.size());
                continue;
            }
            const auto& v = geometry->vertices[vi];
            i.minU=std::min(i.minU,v.u); i.maxU=std::max(i.maxU,v.u);
            i.minV=std::min(i.minV,v.v); i.maxV=std::max(i.maxV,v.v);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: scanned triangles -> {} unique textures (skipped: {} bad texIdx, {} helm/magecap, {} bad vertIdx)",
              infos.size(), skippedTexIdx, skippedHelm, skippedVertIdx);
    if (infos.empty()) {
        LOG_WARN(MOD_GRAPHICS, "buildDynamicAtlas: no valid texture infos found, returning false");
        return false;
    }

    for (const auto& [k, i] : infos) {
        LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: texture \"{}\" UV range U=[{:.3f},{:.3f}] V=[{:.3f},{:.3f}]",
                  k, i.minU, i.maxU, i.minV, i.maxV);
    }

    out.placements.clear();
    int notFoundCount = 0;
    for (auto& [k, i] : infos) {
        std::string vn = applyVariant(i.name, variant);
        const auto* e = (variant > 0) ? lookupETC1(vn, loaders) : nullptr;
        if (variant > 0 && e) {
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: variant texture \"{}\" found ({}x{})", vn, e->width, e->height);
        } else if (variant > 0) {
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: variant texture \"{}\" not found, falling back to \"{}\"", vn, i.name);
        }
        if (!e) e = lookupETC1(i.name, loaders);
        if (!e) {
            notFoundCount++;
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: texture \"{}\" not found in any EQE1 loader, skipping", i.name);
            continue;
        }
        int tU = std::min(8, std::max(1, (int)(std::ceil(i.maxU)-std::floor(i.minU))));
        int tV = std::min(8, std::max(1, (int)(std::ceil(i.maxV)-std::floor(i.minV))));
        LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: \"{}\" src={}x{} tiles={}x{} -> placement={}x{}",
                  i.name, e->width, e->height, tU, tV, (int)e->width*tU, (int)e->height*tV);
        out.placements.push_back({i.name, 0,0, (int)e->width*tU, (int)e->height*tV, tU, tV, (int)e->width, (int)e->height});
    }
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: {} placements built ({} textures not found in EQE1)",
              out.placements.size(), notFoundCount);
    if (out.placements.empty()) {
        LOG_WARN(MOD_GRAPHICS, "buildDynamicAtlas: no placements, returning false");
        return false;
    }
    if (!shelfPack(out.placements, 2048, out.width, out.height)) {
        LOG_WARN(MOD_GRAPHICS, "buildDynamicAtlas: shelfPack failed for {} placements", out.placements.size());
        return false;
    }
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: shelfPack -> atlas {}x{}", out.width, out.height);

    size_t etc1Size = EQT::Graphics::etc1DataSize(out.width, out.height);
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: allocating ETC1 buffer {}B for {}x{}", etc1Size, out.width, out.height);
    out.etc1Data.resize(etc1Size, 0);

    int blitCount = 0, blitSkipped = 0;
    for (const auto& p : out.placements) {
        std::string vn = applyVariant(p.textureName, variant);
        const auto* e = (variant > 0) ? lookupETC1(vn, loaders) : nullptr;
        if (!e) e = lookupETC1(p.textureName, loaders);
        if (!e) {
            blitSkipped++;
            LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: blit skipped \"{}\" (not found in EQE1)", p.textureName);
            continue;
        }
        LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: blitting \"{}\" ({}x{}) -> atlas at ({},{}) tiles={}x{}",
                  p.textureName, e->width, e->height, p.x, p.y, p.tilesU, p.tilesV);
        blitETC1Blocks(e->data, e->width, e->height,
                       out.etc1Data.data(), out.width, out.height,
                       p.x, p.y, p.tilesU, p.tilesV);
        blitCount++;
    }
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: blitted {} textures, skipped {}", blitCount, blitSkipped);

    out.compositeData.sheetWidth = out.width;
    out.compositeData.sheetHeight = out.height;
    out.compositeData.textures.clear();
    LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: building compositeData (sheet={}x{})", out.width, out.height);
    for (const auto& p : out.placements) {
        EQT::Graphics::CompositeTextureEntry e;
        e.x=p.x; e.y=p.y; e.w=p.w; e.h=p.h; e.tilesU=p.tilesU; e.tilesV=p.tilesV;
        e.uvScaleU=(float)p.w/out.width; e.uvScaleV=(float)p.h/out.height;
        e.uvOffsetU=(float)p.x/out.width; e.uvOffsetV=(float)p.y/out.height;
        LOG_TRACE(MOD_GRAPHICS, "buildDynamicAtlas: compositeEntry \"{}\" pos=({},{}) size={}x{} tiles={}x{} uvScale=({:.4f},{:.4f}) uvOffset=({:.4f},{:.4f})",
                  p.textureName, e.x, e.y, e.w, e.h, e.tilesU, e.tilesV, e.uvScaleU, e.uvScaleV, e.uvOffsetU, e.uvOffsetV);
        out.compositeData.textures[p.textureName] = e;
    }
    LOG_DEBUG(MOD_GRAPHICS, "buildDynamicAtlas: success — {}x{} atlas, {} composite entries, {}B ETC1 data",
              out.width, out.height, out.compositeData.textures.size(), out.etc1Data.size());
    return true;
}

// ============================================================================
// ETC1 GPU upload
// ============================================================================

static irr::video::ITexture* uploadETC1Atlas(
    irr::video::IVideoDriver* driver, const char* name,
    const uint8_t* data, uint32_t size, int w, int h) {
    LOG_DEBUG(MOD_GRAPHICS, "uploadETC1Atlas: name=\"{}\" size={}B dimensions={}x{}", name, size, w, h);
#ifdef EQT_HAS_GLES2
    GLuint tex = 0;
    glGenTextures(1, &tex);
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: glGenTextures -> GL name={}", tex);
    if (!tex) {
        LOG_ERROR(MOD_GRAPHICS, "uploadETC1Atlas: glGenTextures returned 0");
        return nullptr;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: glBindTexture(GL_TEXTURE_2D, {})", tex);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x8D64/*GL_ETC1_RGB8_OES*/, w, h, 0, size, data);
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: glCompressedTexImage2D(ETC1, {}x{}, {}B)", w, h, size);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "uploadETC1Atlas: glCompressedTexImage2D FAILED with GL error 0x{:04X}", (unsigned)err);
        glDeleteTextures(1, &tex);
        return nullptr;
    }
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: glCompressedTexImage2D succeeded");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: set LINEAR/CLAMP_TO_EDGE");
    glBindTexture(GL_TEXTURE_2D, 0);
    LOG_TRACE(MOD_GRAPHICS, "uploadETC1Atlas: calling gles2WrapTexture(driver, \"{}\", {}, {}x{}, {}B)",
              name, tex, w, h, size);
    auto* t = static_cast<irr::video::ITexture*>(gles2WrapTexture(driver, name, tex, w, h, size));
    if (!t) {
        LOG_ERROR(MOD_GRAPHICS, "uploadETC1Atlas: gles2WrapTexture returned null, deleting GL texture {}", tex);
        glDeleteTextures(1, &tex);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "uploadETC1Atlas: success — ITexture*={} GL name={}", (void*)t, tex);
    }
    return t;
#else
    LOG_WARN(MOD_GRAPHICS, "uploadETC1Atlas: GLES2 not available (compiled without EQT_HAS_GLES2)");
    (void)driver;(void)name;(void)data;(void)size;(void)w;(void)h;
    return nullptr;
#endif
}

static void freeAtlasTexture(irr::video::IVideoDriver* driver, irr::video::ITexture* t) {
    LOG_TRACE(MOD_GRAPHICS, "freeAtlasTexture: t={}", (void*)t);
    if (!t) {
        LOG_TRACE(MOD_GRAPHICS, "freeAtlasTexture: null pointer, nothing to free");
        return;
    }
    LOG_TRACE(MOD_GRAPHICS, "freeAtlasTexture: removing texture via driver->removeTexture()");
    driver->removeTexture(t);
    LOG_TRACE(MOD_GRAPHICS, "freeAtlasTexture: texture removed from driver");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    InitLogging(argc, argv);
    LOG_INFO(MOD_GRAPHICS, "main: gles2_composite_audit starting");
    FlushThreadLog();
    AuditOptions opts = parseArgs(argc, argv);
    if (opts.eqPath.empty() || opts.eqe1Path.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "main: missing required --eq-path or --eqe1-path");
        printUsage(argv[0]); return 1;
    }

    printf("Composite Entity Audit (dynamic ETC1 atlas)\n");
    printf("  EQ: %s\n  EQE1: %s\n  Output: %s/\n", opts.eqPath.c_str(), opts.eqe1Path.c_str(), opts.outputDir.c_str());
    LOG_DEBUG(MOD_GRAPHICS, "main: creating output directory \"{}\"", opts.outputDir);
    mkdir(opts.outputDir.c_str(), 0755);

    // Device
    LOG_DEBUG(MOD_GRAPHICS, "main: creating Irrlicht device (rttSize={})", opts.rttSize);
#ifdef EQT_HAS_DRM
    LOG_INFO(MOD_GRAPHICS, "main: using DRM/KMS device with OGLES2 driver");
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(opts.rttSize, opts.rttSize);
    params.Bits = 24; params.Stencilbuffer = true; params.Vsync = false;
    LOG_TRACE(MOD_GRAPHICS, "main: device params: driver=OGLES2 size={}x{} bits={} stencil={} vsync={}",
              opts.rttSize, opts.rttSize, params.Bits, params.Stencilbuffer, params.Vsync);
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
#else
    LOG_INFO(MOD_GRAPHICS, "main: using X11 device with OpenGL driver");
    irr::IrrlichtDevice* device = irr::createDevice(irr::video::EDT_OPENGL,
        irr::core::dimension2d<irr::u32>(opts.rttSize, opts.rttSize), 24, false, true, false);
#endif
    if (!device) {
        LOG_FATAL(MOD_GRAPHICS, "main: Irrlicht device creation FAILED");
        fprintf(stderr, "Device failed\n"); return 1;
    }
    LOG_INFO(MOD_GRAPHICS, "main: Irrlicht device created successfully");
    auto* driver = device->getVideoDriver();
    auto* smgr = device->getSceneManager();
    LOG_DEBUG(MOD_GRAPHICS, "main: driver={} smgr={}", (void*)driver, (void*)smgr);

    LOG_DEBUG(MOD_GRAPHICS, "main: creating RTT {}x{}", opts.rttSize, opts.rttSize);
    irr::video::ITexture* rtt = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(opts.rttSize, opts.rttSize), "audit_rtt");
    if (!rtt) {
        LOG_FATAL(MOD_GRAPHICS, "main: RTT creation FAILED");
        fprintf(stderr, "RTT failed\n"); device->drop(); return 1;
    }
    LOG_DEBUG(MOD_GRAPHICS, "main: RTT created successfully, ITexture*={}", (void*)rtt);

    // No upfront EQE1 loading — per-race eqe1 files are loaded on demand
    // inside the race/gender loop to avoid holding all archives in memory.
    LOG_INFO(MOD_GRAPHICS, "main: EQE1 archives will be loaded per-race from \"{}\"", opts.eqe1Path);

    // Texture cache + model loader
    LOG_DEBUG(MOD_GRAPHICS, "main: creating ConstrainedRendererConfig from OrangePi preset");
    auto cconfig = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
        EQT::Graphics::ConstrainedRenderingPreset::OrangePi);
    cconfig.eqe1Path = opts.eqe1Path;
    LOG_DEBUG(MOD_GRAPHICS, "main: config texMemory={}B maxTexDim={} eqe1Path=\"{}\"",
              cconfig.textureMemoryBytes, cconfig.maxTextureDimension, cconfig.eqe1Path);

    LOG_DEBUG(MOD_GRAPHICS, "main: creating ConstrainedTextureCache");
    auto texCache = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(cconfig);
    texCache->setDriver(driver);
    LOG_TRACE(MOD_GRAPHICS, "main: texCache->setDriver({})", (void*)driver);

    LOG_DEBUG(MOD_GRAPHICS, "main: creating RaceModelLoader");
    auto modelLoader = std::make_unique<EQT::Graphics::RaceModelLoader>(smgr, driver, device->getFileSystem());
    modelLoader->setClientPath(opts.eqPath);
    LOG_TRACE(MOD_GRAPHICS, "main: modelLoader->setClientPath(\"{}\")", opts.eqPath);
    modelLoader->setConstrainedTextureCache(texCache.get());
    LOG_TRACE(MOD_GRAPHICS, "main: modelLoader->setConstrainedTextureCache({})", (void*)texCache.get());
    // Old models (pre-Luclin classic) — the only mode WillEQ supports.
    // No upfront loadGlobalModels() call needed: loadModelFromGlobalChr()
    // lazily loads global_chr.s3d on first getMeshForRace() call.
    modelLoader->setUseOldModels(true);
    LOG_TRACE(MOD_GRAPHICS, "main: modelLoader->setUseOldModels(true) — lazy global loading");
    auto* mb = modelLoader->getMeshBuilder();
    LOG_TRACE(MOD_GRAPHICS, "main: meshBuilder={}", (void*)mb);
    if (mb) {
        mb->setConstrainedTextureCache(texCache.get());
        texCache->setMeshBuilder(mb);
        LOG_TRACE(MOD_GRAPHICS, "main: wired meshBuilder <-> texCache");
    } else {
        LOG_WARN(MOD_GRAPHICS, "main: getMeshBuilder() returned null");
    }

    // Prime the driver with an initial frame (ensures internal state is initialized)
    LOG_DEBUG(MOD_GRAPHICS, "main: priming driver with initial beginScene/endScene");
    driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    driver->endScene();
    LOG_TRACE(MOD_GRAPHICS, "main: prime frame complete");

    // No upfront model loading — models loaded per-race via getMeshForRace()
    printf("  Ready (per-race loading mode).\n");

    struct RG { uint16_t id; uint8_t g; const char* label; };
    RG playable[] = {
        {1,0,"hum"},{1,1,"huf"},{2,0,"bam"},{2,1,"baf"},
        {3,0,"erm"},{3,1,"erf"},{4,0,"elm"},{4,1,"elf"},
        {5,0,"him"},{5,1,"hif"},{6,0,"dam"},{6,1,"daf"},
        {7,0,"ham"},{7,1,"haf"},{8,0,"dwm"},{8,1,"dwf"},
        {9,0,"trm"},{9,1,"trf"},{10,0,"ogm"},{10,1,"ogf"},
        {11,0,"hom"},{11,1,"hof"},{12,0,"gnm"},{12,1,"gnf"},
    };
    int variants[] = {0,1,2,3,4};
    int rendered = 0, failed = 0;
    int totalPlayable = sizeof(playable) / sizeof(playable[0]);
    int totalVariants = sizeof(variants) / sizeof(variants[0]);
    LOG_INFO(MOD_GRAPHICS, "main: beginning audit — {} race/gender combos x {} variants = {} total",
             totalPlayable, totalVariants, totalPlayable * totalVariants);

    for (int rgi = 0; rgi < totalPlayable; ++rgi) {
        auto& rg = playable[rgi];
        if (opts.filterRace > 0 && rg.id != opts.filterRace) {
            LOG_TRACE(MOD_GRAPHICS, "main: skipping {} (race={}) due to filterRace={}", rg.label, rg.id, opts.filterRace);
            continue;
        }
        printf("  %s (race=%d gender=%d)\n", rg.label, rg.id, rg.g);
        LOG_INFO(MOD_GRAPHICS, "main: === processing {} (race={} gender={}) ===", rg.label, rg.id, rg.g);

        // --- Per-race EQE1 loading: only load archives needed for this race ---
        std::vector<std::unique_ptr<EQT::Graphics::EQE1Loader>> eqe1Storage;
        std::vector<EQT::Graphics::EQE1Loader*> eqe1Ptrs;
        {
            // Per-race eqe1 (e.g., globalhum_chr.eqe1) — contains all textures for this race
            char raceFile[64];
            snprintf(raceFile, sizeof(raceFile), "global%s_chr.eqe1", rg.label);
            std::string racePath = opts.eqe1Path + "/" + raceFile;
            LOG_DEBUG(MOD_GRAPHICS, "main: opening per-race EQE1 \"{}\"", racePath);
            auto raceLoader = std::make_unique<EQT::Graphics::EQE1Loader>();
            if (raceLoader->open(racePath)) {
                printf("    EQE1: %s (%d entries)\n", raceFile, raceLoader->getEntryCount());
                LOG_INFO(MOD_GRAPHICS, "main: per-race EQE1 \"{}\" opened — {} entries", raceFile, raceLoader->getEntryCount());
                eqe1Ptrs.push_back(raceLoader.get());
                eqe1Storage.push_back(std::move(raceLoader));
            } else {
                LOG_WARN(MOD_GRAPHICS, "main: per-race EQE1 \"{}\" failed to open", racePath);
            }

            // Fallback: global_chr.eqe1 for shared textures not in per-race file
            std::string globalPath = opts.eqe1Path + "/global_chr.eqe1";
            LOG_DEBUG(MOD_GRAPHICS, "main: opening shared EQE1 \"{}\"", globalPath);
            auto globalLoader = std::make_unique<EQT::Graphics::EQE1Loader>();
            if (globalLoader->open(globalPath)) {
                LOG_INFO(MOD_GRAPHICS, "main: shared EQE1 global_chr.eqe1 opened — {} entries", globalLoader->getEntryCount());
                eqe1Ptrs.push_back(globalLoader.get());
                eqe1Storage.push_back(std::move(globalLoader));
            } else {
                LOG_WARN(MOD_GRAPHICS, "main: shared EQE1 \"{}\" failed to open", globalPath);
            }
        }
        if (eqe1Ptrs.empty()) {
            printf("    NO EQE1 TEXTURES\n");
            LOG_WARN(MOD_GRAPHICS, "main: {} — no EQE1 loaders available, skipping (+5 failed)", rg.label);
            failed += 5; continue;
        }

        // Load mesh for this race/gender (loads per-race S3D on demand via getMeshForRace)
        LOG_DEBUG(MOD_GRAPHICS, "main: getMeshForRace(race={}, gender={})", rg.id, rg.g);
        auto* baseMesh = modelLoader->getMeshForRace(rg.id, rg.g);
        if (!baseMesh) {
            printf("    NO MESH\n");
            LOG_WARN(MOD_GRAPHICS, "main: {} — getMeshForRace returned null, skipping all variants (+5 failed)", rg.label);
            failed += 5; continue;
        }
        LOG_DEBUG(MOD_GRAPHICS, "main: {} baseMesh={} buffers={}", rg.label, (void*)baseMesh, baseMesh->getMeshBufferCount());

        LOG_DEBUG(MOD_GRAPHICS, "main: getRaceModelData(race={}, gender={})", rg.id, rg.g);
        auto modelData = modelLoader->getRaceModelData(rg.id, rg.g);
        if (!modelData || !modelData->combinedGeometry) {
            printf("    NO GEOMETRY\n");
            LOG_WARN(MOD_GRAPHICS, "main: {} — modelData={} combinedGeometry={}, skipping all variants (+5 failed)",
                     rg.label, (void*)modelData.get(),
                     modelData ? (void*)modelData->combinedGeometry.get() : nullptr);
            failed += 5; continue;
        }
        LOG_DEBUG(MOD_GRAPHICS, "main: {} modelData: {} triangles, {} vertices, {} textures, scale={}",
                  rg.label, modelData->combinedGeometry->triangles.size(),
                  modelData->combinedGeometry->vertices.size(),
                  modelData->combinedGeometry->textureNames().size(),
                  modelData->scale);

        for (int vi = 0; vi < totalVariants; ++vi) {
            int v = variants[vi];
            LOG_INFO(MOD_GRAPHICS, "main: --- {} v{:02d} ---", rg.label, v);
            FlushThreadLog();

            // Build dynamic atlas
            LOG_DEBUG(MOD_GRAPHICS, "main: building dynamic atlas for {} v{:02d}", rg.label, v);
            DynamicAtlas atlas;
            if (!buildDynamicAtlas(modelData->combinedGeometry, eqe1Ptrs, v, atlas)) {
                printf("    %s v%02d: ATLAS FAILED\n", rg.label, v);
                LOG_WARN(MOD_GRAPHICS, "main: {} v{:02d} — buildDynamicAtlas FAILED", rg.label, v);
                failed++; continue;
            }
            LOG_DEBUG(MOD_GRAPHICS, "main: {} v{:02d} atlas built: {}x{}, {} placements, {}B ETC1",
                      rg.label, v, atlas.width, atlas.height, atlas.placements.size(), atlas.etc1Data.size());

            // Upload ETC1 atlas
            char tn[64]; snprintf(tn, sizeof(tn), "dyn_%s_v%02d", rg.label, v);
            LOG_DEBUG(MOD_GRAPHICS, "main: uploading ETC1 atlas \"{}\" ({}x{}, {}B)", tn, atlas.width, atlas.height, atlas.etc1Data.size());
            FlushThreadLog();
            auto* atlasTex = uploadETC1Atlas(driver, tn, atlas.etc1Data.data(),
                                              atlas.etc1Data.size(), atlas.width, atlas.height);
            if (!atlasTex) {
                printf("    %s v%02d: UPLOAD FAILED\n", rg.label, v);
                LOG_ERROR(MOD_GRAPHICS, "main: {} v{:02d} — uploadETC1Atlas FAILED", rg.label, v);
                failed++; continue;
            }
            LOG_DEBUG(MOD_GRAPHICS, "main: {} v{:02d} atlas uploaded, ITexture*={}", rg.label, v, (void*)atlasTex);

            // Build composite mesh
            LOG_DEBUG(MOD_GRAPHICS, "main: building composite mesh for {} v{:02d}", rg.label, v);
            FlushThreadLog();
            auto* compositeMesh = modelLoader->buildCompositeEntityMesh(
                modelData->combinedGeometry, atlasTex, atlas.compositeData);
            if (!compositeMesh) {
                printf("    %s v%02d: MESH FAILED\n", rg.label, v);
                LOG_ERROR(MOD_GRAPHICS, "main: {} v{:02d} — buildCompositeEntityMesh returned null", rg.label, v);
                freeAtlasTexture(driver, atlasTex); failed++; continue;
            }
            LOG_DEBUG(MOD_GRAPHICS, "main: {} v{:02d} composite mesh built, {} buffers",
                      rg.label, v, compositeMesh->getMeshBufferCount());

            // Apply idle pose
            LOG_DEBUG(MOD_GRAPHICS, "main: creating animated node for {} v{:02d} (idle pose)", rg.label, v);
            irr::scene::IMesh* meshToDraw = compositeMesh;
            auto* animNode = modelLoader->createAnimatedNode(rg.id, rg.g, smgr->getRootSceneNode());
            if (animNode) {
                LOG_TRACE(MOD_GRAPHICS, "main: animNode created, playing o01 idle animation");
                animNode->playAnimation("o01", true);
                animNode->forceAnimationUpdate();
                if (animNode->getInstanceMesh()) {
                    meshToDraw = animNode->getInstanceMesh();
                    LOG_TRACE(MOD_GRAPHICS, "main: using animated instance mesh ({} buffers)", meshToDraw->getMeshBufferCount());
                } else {
                    LOG_TRACE(MOD_GRAPHICS, "main: no instance mesh from animNode, using composite mesh");
                }
            } else {
                LOG_WARN(MOD_GRAPHICS, "main: createAnimatedNode returned null for {} — using static composite mesh", rg.label);
            }

            // Bbox
            LOG_TRACE(MOD_GRAPHICS, "main: computing bounding box from {} mesh buffers", meshToDraw->getMeshBufferCount());
            irr::core::aabbox3df bbox; bool bi = false;
            int totalVerts = 0, emptyBuffers = 0;
            for (irr::u32 b = 0; b < meshToDraw->getMeshBufferCount(); ++b) {
                auto* buf = meshToDraw->getMeshBuffer(b);
                if (!buf || buf->getVertexCount() == 0) {
                    emptyBuffers++;
                    LOG_TRACE(MOD_GRAPHICS, "main: bbox buf[{}] empty/null, skipping", b);
                    continue;
                }
                auto* vs = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                LOG_TRACE(MOD_GRAPHICS, "main: bbox buf[{}] {} vertices, {} indices",
                          b, buf->getVertexCount(), buf->getIndexCount());
                for (irr::u32 vii = 0; vii < buf->getVertexCount(); ++vii) {
                    if (!bi) { bbox.reset(vs[vii].Pos); bi = true; } else bbox.addInternalPoint(vs[vii].Pos);
                }
                totalVerts += buf->getVertexCount();
            }
            LOG_DEBUG(MOD_GRAPHICS, "main: bbox computed from {} vertices ({} empty buffers)", totalVerts, emptyBuffers);
            if (!bi) {
                LOG_WARN(MOD_GRAPHICS, "main: {} v{:02d} — no vertices found for bounding box", rg.label, v);
                if (animNode) { animNode->remove(); LOG_TRACE(MOD_GRAPHICS, "main: removed animNode"); }
                compositeMesh->drop(); freeAtlasTexture(driver, atlasTex);
                failed++; continue;
            }
            auto center = bbox.getCenter(); auto ext = bbox.getExtent();
            LOG_DEBUG(MOD_GRAPHICS, "main: {} v{:02d} bbox center=({:.1f},{:.1f},{:.1f}) extent=({:.1f},{:.1f},{:.1f})",
                      rg.label, v, center.X, center.Y, center.Z, ext.X, ext.Y, ext.Z);

            // Render to RTT
            LOG_DEBUG(MOD_GRAPHICS, "main: rendering {} v{:02d} to RTT", rg.label, v);
            FlushThreadLog();
            auto sv = driver->getTransform(irr::video::ETS_VIEW);
            auto sp = driver->getTransform(irr::video::ETS_PROJECTION);
            LOG_TRACE(MOD_GRAPHICS, "main: saved current view/projection transforms");
            driver->setRenderTarget(rtt, true, true, irr::video::SColor(255, 40, 40, 60));
            LOG_TRACE(MOD_GRAPHICS, "main: setRenderTarget(rtt, clear color+depth, bg=40,40,60)");

            float hw = ext.Z * 0.55f, hh = ext.Y * 0.55f;
            if (hw < 1) hw = 1; if (hh < 1) hh = 1;
            LOG_TRACE(MOD_GRAPHICS, "main: ortho halfWidth={:.2f} halfHeight={:.2f}", hw, hh);
            irr::core::matrix4 proj; proj.buildProjectionMatrixOrthoLH(hw*2, hh*2, 0.1f, 500);
            driver->setTransform(irr::video::ETS_PROJECTION, proj);
            LOG_TRACE(MOD_GRAPHICS, "main: set ortho projection {}x{} near=0.1 far=500", hw*2, hh*2);
            irr::core::matrix4 view;
            view.buildCameraLookAtMatrixLH(center + irr::core::vector3df(100,0,0), center, irr::core::vector3df(0,-1,0));
            driver->setTransform(irr::video::ETS_VIEW, view);
            LOG_TRACE(MOD_GRAPHICS, "main: set view — camera at ({:.1f},{:.1f},{:.1f}) looking at center, up=(0,-1,0)",
                      center.X+100, center.Y, center.Z);
            irr::core::matrix4 world; driver->setTransform(irr::video::ETS_WORLD, world);
            LOG_TRACE(MOD_GRAPHICS, "main: set identity world transform");

            int drawnBuffers = 0, skippedBuffers = 0;
            for (irr::u32 b = 0; b < meshToDraw->getMeshBufferCount(); ++b) {
                auto* buf = meshToDraw->getMeshBuffer(b);
                if (!buf || buf->getVertexCount() == 0) {
                    skippedBuffers++;
                    LOG_TRACE(MOD_GRAPHICS, "main: draw buf[{}] empty/null, skipping", b);
                    continue;
                }
                auto mat = buf->getMaterial();
                mat.FogEnable = false; mat.BackfaceCulling = false;
                LOG_TRACE(MOD_GRAPHICS, "main: draw buf[{}] verts={} indices={} tex0={} fog=off cull=off",
                          b, buf->getVertexCount(), buf->getIndexCount(),
                          mat.getTexture(0) ? "set" : "null");
                driver->setMaterial(mat);
                driver->drawMeshBuffer(buf);
                drawnBuffers++;
            }
            LOG_DEBUG(MOD_GRAPHICS, "main: drew {} buffers, skipped {} empty", drawnBuffers, skippedBuffers);

            driver->setRenderTarget(nullptr, false, false);
            LOG_TRACE(MOD_GRAPHICS, "main: setRenderTarget(null) — back to default framebuffer");
            driver->setTransform(irr::video::ETS_VIEW, sv);
            driver->setTransform(irr::video::ETS_PROJECTION, sp);
            LOG_TRACE(MOD_GRAPHICS, "main: restored view/projection transforms");

            // Blit to screen and screenshot
            LOG_DEBUG(MOD_GRAPHICS, "main: blitting RTT to screen for screenshot");
            driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            irr::core::rect<irr::s32> dr(0,0,opts.rttSize,opts.rttSize);
            LOG_TRACE(MOD_GRAPHICS, "main: draw2DImage(rtt, rect(0,0,{},{}), useAlpha=true)", opts.rttSize, opts.rttSize);
            driver->draw2DImage(rtt, dr, dr, nullptr, nullptr, true);
            driver->endScene();
            LOG_TRACE(MOD_GRAPHICS, "main: endScene complete");

            char fn[256]; snprintf(fn, sizeof(fn), "%s/%s_v%02d.png", opts.outputDir.c_str(), rg.label, v);
            LOG_DEBUG(MOD_GRAPHICS, "main: creating screenshot -> \"{}\"", fn);
            auto* img = driver->createScreenShot();
            LOG_TRACE(MOD_GRAPHICS, "main: createScreenShot -> {}", (void*)img);
            bool ok = img && driver->writeImageToFile(img, fn);
            LOG_TRACE(MOD_GRAPHICS, "main: writeImageToFile -> {}", ok);
            if (img) { img->drop(); LOG_TRACE(MOD_GRAPHICS, "main: dropped screenshot image"); }

            if (animNode) { animNode->remove(); LOG_TRACE(MOD_GRAPHICS, "main: removed animNode"); }
            LOG_TRACE(MOD_GRAPHICS, "main: dropping composite mesh");
            compositeMesh->drop();
            LOG_TRACE(MOD_GRAPHICS, "main: freeing atlas texture");
            FlushThreadLog();
            freeAtlasTexture(driver, atlasTex);

            if (ok) {
                printf("    %s v%02d: %dx%d (%zuB) -> %s\n", rg.label, v, atlas.width, atlas.height, atlas.etc1Data.size(), fn);
                LOG_INFO(MOD_GRAPHICS, "main: {} v{:02d} RENDERED — atlas={}x{} etc1={}B -> {}",
                         rg.label, v, atlas.width, atlas.height, atlas.etc1Data.size(), fn);
                rendered++;
            } else {
                printf("    %s v%02d: SCREENSHOT FAILED\n", rg.label, v);
                LOG_ERROR(MOD_GRAPHICS, "main: {} v{:02d} SCREENSHOT FAILED (img={} writeOk={})",
                          rg.label, v, (void*)img, ok);
                failed++;
            }
        }

        // --- Per-race cleanup: free all S3D model data, mesh caches, and eqe1 archives ---
        // Release modelData shared_ptr before clearing caches
        modelData.reset();

        LOG_DEBUG(MOD_GRAPHICS, "main: cleaning up after {} — clearing model caches", rg.label);
        size_t texFreed = modelLoader->releaseRawTextureData();
        LOG_INFO(MOD_GRAPHICS, "main: {} — released {} bytes of raw texture data", rg.label, texFreed);
        modelLoader->clearCache();
        LOG_INFO(MOD_GRAPHICS, "main: {} — model caches cleared", rg.label);

        // Close per-race EQE1 loaders (frees file data buffers)
        size_t eqe1Freed = 0;
        for (auto& loader : eqe1Storage) {
            if (loader && loader->isOpen()) {
                eqe1Freed += loader->getEntryCount(); // approximate
                loader->close();
            }
        }
        eqe1Storage.clear();
        eqe1Ptrs.clear();
        LOG_INFO(MOD_GRAPHICS, "main: {} — EQE1 loaders closed ({} entries freed)", rg.label, eqe1Freed);
        printf("    %s: cleanup complete (texData=%zuB freed, caches cleared)\n", rg.label, texFreed);
    }

    LOG_INFO(MOD_GRAPHICS, "main: audit complete — {} rendered, {} failed", rendered, failed);
    FlushThreadLog();
    printf("\n=== Done: %d rendered, %d failed ===\n", rendered, failed);
    LOG_DEBUG(MOD_GRAPHICS, "main: dropping Irrlicht device");
    device->drop();
    LOG_INFO(MOD_GRAPHICS, "main: gles2_composite_audit finished");
    FlushThreadLog();
    // Allow drain thread to flush remaining log messages
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
