#include "zone_bench_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/portal_system.h"
#include "client/zone_bsp.h"

#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/race_model_loader.h"

#ifdef EQT_HAS_TEXTURE_ATLAS
#include "client/graphics/texture_atlas.h"
#endif

#ifdef EQT_HAS_GLES2
extern bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer);
#endif

using namespace EQT::Graphics;

namespace ZoneBench {

ZoneBenchData::~ZoneBenchData() {
    // Intentionally skip cleanup — Irrlicht's destruction order with
    // manually-managed meshes, VBOs, and shared textures causes segfaults.
    // This is a short-lived benchmark tool; let the OS reclaim everything.
}

// Create Irrlicht device matching production GLES2/DRM setup.
static irr::IrrlichtDevice* createDevice(const BenchConfig& config) {
    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(config.width, config.height);
    params.Fullscreen = true;
    params.Stencilbuffer = true;
    params.Vsync = false;  // No vsync for benchmarking
    params.AntiAlias = 0;  // No MSAA for clean measurements

#ifdef EQT_HAS_DRM
    if (config.useDRM) {
        params.DeviceType = irr::EIDT_FRAMEBUFFER;
    }
#endif

    printf("Creating Irrlicht device: %dx%d, DRM=%d\n",
           config.width, config.height, config.useDRM);

    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: Failed to create Irrlicht device\n");
        return nullptr;
    }

    printf("  Driver: %s\n",
           device->getVideoDriver()->getName() ?
           irr::core::stringc(device->getVideoDriver()->getName()).c_str() : "unknown");
    return device;
}

// Upload all mesh buffers in a mesh to GPU via static VBOs.
static void uploadMeshHWBuffers(irr::video::IVideoDriver* driver, irr::scene::IMesh* mesh) {
#ifdef EQT_HAS_GLES2
    if (!mesh || !driver) return;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf && buf->getVertexCount() > 0)
            gles2CreateStaticHWBuffer(driver, buf);
    }
#else
    (void)driver; (void)mesh;
#endif
}

std::unique_ptr<ZoneBenchData> loadZone(const BenchConfig& config) {
    auto data = std::make_unique<ZoneBenchData>();
    auto totalStart = std::chrono::steady_clock::now();

    // ── Step 1: Create Irrlicht device ──
    data->device = createDevice(config);
    if (!data->device) return nullptr;
    data->driver = data->device->getVideoDriver();
    data->smgr = data->device->getSceneManager();

    // ── Step 2: Load S3D zone ──
    printf("Loading zone: %s\n", config.zoneName.c_str());
    auto loadStart = std::chrono::steady_clock::now();

    std::string archivePath = config.eqPath + "/" + config.zoneName + ".s3d";
    S3DLoader loader;
    S3DLoadOptions opts;
    opts.loadObjects = false;
    opts.loadCharacters = false;
    opts.computeCombinedGeometry = false;

    if (!loader.loadZone(archivePath, opts)) {
        fprintf(stderr, "ERROR: Failed to load zone: %s (%s)\n",
                archivePath.c_str(), loader.getError().c_str());
        return nullptr;
    }

    data->zone = loader.getZone();
    if (!data->zone || !data->zone->wldLoader) {
        fprintf(stderr, "ERROR: Zone loaded but no WLD data\n");
        return nullptr;
    }

    auto bspTree = data->zone->wldLoader->getBspTree();
    if (!bspTree || bspTree->regions.empty()) {
        fprintf(stderr, "ERROR: Zone has no BSP regions\n");
        return nullptr;
    }
    data->bspTree = bspTree;

    auto loadEnd = std::chrono::steady_clock::now();
    printf("  S3D loaded: %zu regions, %zu textures (%.0f ms)\n",
           bspTree->regions.size(),
           data->zone->textures.size(),
           std::chrono::duration<double, std::milli>(loadEnd - loadStart).count());

    // ── Step 3: Set up constrained renderer config (OrangePi preset) ──
    auto constrainedConfig = ConstrainedRendererConfig::fromPreset(
        ConstrainedRenderingPreset::OrangePi);
    constrainedConfig.atlasPath = config.atlasPath;

    // ── Step 4: Set up texture cache ──
    data->textureCache = std::make_unique<ConstrainedTextureCache>(constrainedConfig);
    data->textureCache->setDriver(data->driver);

    // ── Step 5: Set up shaders ──
    auto* gpu = data->driver->getGPUProgrammingServices();
    if (gpu) {
        data->zoneShader = std::make_unique<ZoneShaderManager>(data->driver, gpu);
        if (data->zoneShader->isAvailable()) {
            printf("  Shader system: available (solid=%d, alphaTest=%d)\n",
                   data->zoneShader->getMaterialTypeSolid(),
                   data->zoneShader->getMaterialTypeAlphaTest());
        } else {
            printf("  Shader system: NOT available\n");
        }
    }

    // ── Step 6: Load texture atlas ──
#ifdef EQT_HAS_TEXTURE_ATLAS
    if (config.useAtlas && !config.atlasPath.empty()) {
        auto atlasStart = std::chrono::steady_clock::now();
        std::string atlasFile = config.atlasPath;
        if (!atlasFile.empty() && atlasFile.back() != '/') atlasFile += '/';
        atlasFile += config.zoneName + ".atlas";

        auto preload = TextureAtlas::preloadFromFile(atlasFile);
        if (preload.valid) {
            data->zoneAtlas = std::make_shared<TextureAtlas>();
            int pageIdx = 0;
            bool done = false;
            while (!done) {
                done = data->zoneAtlas->uploadPreloadedPage(preload, pageIdx);
                ++pageIdx;
            }
            data->zoneAtlas->finalizePreload(preload);

            // Set atlas page textures on shader
            if (data->zoneShader && data->zoneShader->isAtlasAvailable()) {
                std::vector<uint32_t> pageTextures;
                for (uint16_t p = 0; p < data->zoneAtlas->getPageCount(); ++p)
                    pageTextures.push_back(data->zoneAtlas->getPageTexture(p));
                data->zoneShader->setAtlasPageTextures(pageTextures);
            }

            auto atlasEnd = std::chrono::steady_clock::now();
            printf("  Atlas loaded: %d pages, %d tiles (%.0f ms)\n",
                   data->zoneAtlas->getPageCount(),
                   data->zoneAtlas->getTileCount(),
                   std::chrono::duration<double, std::milli>(atlasEnd - atlasStart).count());
        } else {
            printf("  Atlas not found or invalid: %s (falling back to per-texture)\n",
                   atlasFile.c_str());
        }
    }
#endif

    // ── Step 7: Build per-region meshes ──
    auto meshStart = std::chrono::steady_clock::now();

    ZoneMeshBuilder builder(data->smgr, data->driver, data->device->getFileSystem());
    builder.setConstrainedTextureCache(data->textureCache.get());

    if (data->zoneShader && data->zoneShader->isAvailable()) {
        builder.setShaderMaterialTypes(
            data->zoneShader->getActiveSolid(),
            data->zoneShader->getActiveAlphaTest());
    }
#ifdef EQT_HAS_TEXTURE_ATLAS
    if (data->zoneShader && data->zoneShader->isAtlasAvailable()) {
        builder.setAtlasShaderMaterialTypes(
            data->zoneShader->getActiveAtlasSolid(),
            data->zoneShader->getActiveAtlasAlpha());
    }
#endif

    bool useAtlas = data->zoneAtlas && data->zoneAtlas->isLoaded() &&
                    data->zoneShader && data->zoneShader->isAtlasAvailable();

    auto wldLoader = data->zone->wldLoader;
    size_t totalRegions = bspTree->regions.size();
    size_t regionsBuilt = 0, regionsSkipped = 0;
    int totalBuffers = 0;

    // Track zone bounds
    float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
    float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;

    for (size_t i = 0; i < totalRegions; ++i) {
        auto geom = wldLoader->getGeometryForRegion(i);
        if (!geom || geom->vertices.empty()) {
            ++regionsSkipped;
            continue;
        }

        irr::scene::IMesh* mesh = nullptr;
        if (!data->zone->textures.empty() && !geom->textureNames().empty()) {
#ifdef EQT_HAS_TEXTURE_ATLAS
            if (useAtlas) {
                mesh = builder.buildAtlasedMesh(*geom, data->zone->textures, *data->zoneAtlas);
            } else
#endif
            {
                mesh = builder.buildTexturedMesh(*geom, data->zone->textures);
            }
        } else {
            mesh = builder.buildColoredMesh(*geom);
        }

        if (!mesh) continue;

        // Upload VBOs
        uploadMeshHWBuffers(data->driver, mesh);

        // Compute EQ-coord bounding box from geometry
        float vMinX = std::numeric_limits<float>::max();
        float vMinY = vMinX, vMinZ = vMinX;
        float vMaxX = std::numeric_limits<float>::lowest();
        float vMaxY = vMaxX, vMaxZ = vMaxX;
        for (const auto& v : geom->vertices) {
            float wx = geom->centerX + v.x;
            float wy = geom->centerY + v.y;
            float wz = geom->centerZ + v.z;
            if (wx < vMinX) vMinX = wx; if (wy < vMinY) vMinY = wy; if (wz < vMinZ) vMinZ = wz;
            if (wx > vMaxX) vMaxX = wx; if (wy > vMaxY) vMaxY = wy; if (wz > vMaxZ) vMaxZ = wz;
        }

        // Update zone bounds
        zbMinX = std::min(zbMinX, vMinX); zbMinY = std::min(zbMinY, vMinY); zbMinZ = std::min(zbMinZ, vMinZ);
        zbMaxX = std::max(zbMaxX, vMaxX); zbMaxY = std::max(zbMaxY, vMaxY); zbMaxZ = std::max(zbMaxZ, vMaxZ);

        // Store region data
        // Irrlicht position: EQ (x, y, z) → Irrlicht (x, z, y)
        RegionData rd;
        rd.mesh = mesh;
        rd.position = irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY);
        rd.eqMinX = vMinX; rd.eqMinY = vMinY; rd.eqMinZ = vMinZ;
        rd.eqMaxX = vMaxX; rd.eqMaxY = vMaxY; rd.eqMaxZ = vMaxZ;
        data->regions[i] = rd;

        totalBuffers += mesh->getMeshBufferCount();
        ++regionsBuilt;
    }

    data->boundsMinX = zbMinX; data->boundsMinY = zbMinY; data->boundsMinZ = zbMinZ;
    data->boundsMaxX = zbMaxX; data->boundsMaxY = zbMaxY; data->boundsMaxZ = zbMaxZ;

    // Upload any pending textures from constrained cache
    int uploaded = data->textureCache->uploadAllPending();

    auto meshEnd = std::chrono::steady_clock::now();
    printf("  Meshes built: %zu regions (%zu skipped), %d buffers, %d textures uploaded (%.0f ms)\n",
           regionsBuilt, regionsSkipped, totalBuffers, uploaded,
           std::chrono::duration<double, std::milli>(meshEnd - meshStart).count());

    // ── Step 8: Build portal system ──
    data->portalSystem = std::make_unique<PortalSystem>();
    data->portalSystem->buildFromBsp(*bspTree, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
    printf("  Portal system: %zu portals, hasPortals=%d\n",
           data->portalSystem->getData().portals.size(),
           data->portalSystem->hasPortals());

    // ── Step 9: Set up frustum culler ──
    data->frustumCuller = std::make_unique<FrustumCuller>();
    data->frustumCuller->setEnabled(true);

    // ── Step 10: Extract occluder triangles for software occlusion culling ──
    {
        OcclusionCullerConfig occConfig;
        occConfig.width = 64;
        occConfig.height = 32;
        occConfig.maxOccluderRegions = 16;
        occConfig.minOccluderArea = 20.0f;
        occConfig.maxTrianglesPerRegion = 12;
        data->occlusionCuller = std::make_unique<SoftwareOcclusionCuller>(occConfig);

        int totalOccluders = 0;
        int regionsWithOccluders = 0;

        for (size_t i = 0; i < totalRegions; ++i) {
            auto geom = wldLoader->getGeometryForRegion(i);
            if (!geom || geom->vertices.empty() || geom->triangles.empty()) continue;

            // Extract large triangles as occluders
            std::vector<OccluderTriangle> occluders;
            for (const auto& tri : geom->triangles) {
                if (tri.v1 >= geom->vertices.size() ||
                    tri.v2 >= geom->vertices.size() ||
                    tri.v3 >= geom->vertices.size()) continue;

                const auto& va = geom->vertices[tri.v1];
                const auto& vb = geom->vertices[tri.v2];
                const auto& vc = geom->vertices[tri.v3];

                // World-space positions (add region center offset)
                float ax = geom->centerX + va.x, ay = geom->centerY + va.y, az = geom->centerZ + va.z;
                float bx = geom->centerX + vb.x, by = geom->centerY + vb.y, bz = geom->centerZ + vb.z;
                float cx = geom->centerX + vc.x, cy = geom->centerY + vc.y, cz = geom->centerZ + vc.z;

                // Cross product for area
                float ex = bx - ax, ey = by - ay, ez = bz - az;
                float fx = cx - ax, fy = cy - ay, fz = cz - az;
                float nx = ey * fz - ez * fy;
                float ny = ez * fx - ex * fz;
                float nz = ex * fy - ey * fx;
                float area = 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz);

                if (area < occConfig.minOccluderArea) continue;

                OccluderTriangle ot;
                ot.v0[0] = ax; ot.v0[1] = ay; ot.v0[2] = az;
                ot.v1[0] = bx; ot.v1[1] = by; ot.v1[2] = bz;
                ot.v2[0] = cx; ot.v2[1] = cy; ot.v2[2] = cz;
                ot.area = area;
                occluders.push_back(ot);
            }

            // Keep only the largest triangles per region
            if (static_cast<int>(occluders.size()) > occConfig.maxTrianglesPerRegion) {
                std::sort(occluders.begin(), occluders.end(),
                          [](const OccluderTriangle& a, const OccluderTriangle& b) {
                              return a.area > b.area;
                          });
                occluders.resize(occConfig.maxTrianglesPerRegion);
            }

            if (!occluders.empty()) {
                totalOccluders += occluders.size();
                regionsWithOccluders++;
                data->occlusionCuller->setRegionOccluders(i, std::move(occluders));
            }
        }

        printf("  Occlusion culler: %d occluder triangles from %d regions (64x32 depth buffer)\n",
               totalOccluders, regionsWithOccluders);
    }

    // ── Step 11: Load entity models ──
    if (config.entityCount > 0) {
        auto entityStart = std::chrono::steady_clock::now();

        data->raceModelLoader = std::make_unique<RaceModelLoader>(
            data->smgr, data->driver, data->device->getFileSystem());
        data->raceModelLoader->setClientPath(config.eqPath);
        data->raceModelLoader->setConstrainedTextureCache(data->textureCache.get());

        // Wire constrained cache and shader materials on the internal mesh builder
        auto* mb = data->raceModelLoader->getMeshBuilder();
        if (mb) {
            mb->setConstrainedTextureCache(data->textureCache.get());
            if (data->zoneShader && data->zoneShader->isAvailable()) {
                mb->setShaderMaterialTypes(
                    data->zoneShader->getActiveSolid(),
                    data->zoneShader->getActiveAlphaTest());
            }
        }

        printf("  Loading entity models...\n");
        data->raceModelLoader->loadGlobalModels();
        data->raceModelLoader->loadNumberedGlobalModels();
        data->raceModelLoader->loadZoneModels(config.zoneName);

        // Upload any textures decoded during model loading
        data->textureCache->uploadAllPending();

        // Common EQ race IDs for variety
        uint16_t raceIds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        int numRaces = sizeof(raceIds) / sizeof(raceIds[0]);

        // Pre-load meshes for each race (cache them)
        std::vector<irr::scene::IMesh*> raceMeshes;
        for (int r = 0; r < numRaces; ++r) {
            auto* mesh = data->raceModelLoader->getMeshForRace(raceIds[r], 0);
            if (mesh) {
                // Upload VBOs
                uploadMeshHWBuffers(data->driver, mesh);
                raceMeshes.push_back(mesh);
            }
            // Upload textures that were decoded during mesh building
            data->textureCache->uploadAllPending();
        }

        if (raceMeshes.empty()) {
            printf("  WARNING: No entity meshes loaded, skipping entity placement\n");
        } else {
            printf("  Loaded %zu race meshes\n", raceMeshes.size());

            // Entity positions will be set later from the camera path (see main)
            // For now, just store the meshes. Placement happens after path generation.
            data->entities.resize(config.entityCount);
            for (int i = 0; i < config.entityCount; ++i) {
                data->entities[i].mesh = raceMeshes[i % raceMeshes.size()];
            }
        }

        auto entityEnd = std::chrono::steady_clock::now();
        printf("  Entity models loaded in %.0f ms (%d entities, %zu race meshes)\n",
               std::chrono::duration<double, std::milli>(entityEnd - entityStart).count(),
               config.entityCount, raceMeshes.size());
    }

    // ── Summary ──
    auto totalEnd = std::chrono::steady_clock::now();
    printf("Zone loaded in %.0f ms\n",
           std::chrono::duration<double, std::milli>(totalEnd - totalStart).count());
    printf("  Zone bounds (EQ): X=[%.0f,%.0f] Y=[%.0f,%.0f] Z=[%.0f,%.0f]\n",
           zbMinX, zbMaxX, zbMinY, zbMaxY, zbMinZ, zbMaxZ);
    printf("  Center (EQ): (%.0f, %.0f, %.0f)\n",
           (zbMinX + zbMaxX) * 0.5f, (zbMinY + zbMaxY) * 0.5f, (zbMinZ + zbMaxZ) * 0.5f);

    return data;
}

void collectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    bool materialSort,
    bool portalCull,
    bool frustumCull,
    bool occlusionCull,
    bool frontToBack,
    int maxDraws,
    int maxPolys,
    std::vector<DrawEntry>& outEntries,
    int& outRegionsVisible,
    int& outRegionsDrawn,
    int& outRegionsOccluded)
{
    outEntries.clear();
    outRegionsVisible = 0;
    outRegionsDrawn = 0;
    outRegionsOccluded = 0;

    float camEqX = cam.eqX, camEqY = cam.eqY, camEqZ = cam.eqZ;

    // Find camera's BSP region for PVS
    size_t camRegion = data.bspTree->findRegionIndexForPoint(camEqX, camEqY, camEqZ);

    int polyBudget = (maxPolys > 0) ? maxPolys : std::numeric_limits<int>::max();
    int drawBudget = (maxDraws > 0) ? maxDraws : std::numeric_limits<int>::max();
    int polysCollected = 0;

    // Collect visible regions and their draw entries
    struct RegionEntry {
        size_t idx;
        float distSq;
    };
    std::vector<RegionEntry> visibleRegions;

    for (auto& [regionIdx, rd] : data.regions) {
        // PVS culling
        if (portalCull && camRegion != SIZE_MAX && data.bspTree) {
            auto& camReg = data.bspTree->regions[camRegion];
            if (camReg && regionIdx < camReg->visibleRegions.size() &&
                !camReg->visibleRegions[regionIdx]) {
                continue;
            }
        }

        // Frustum culling
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testAABB(
                    rd.eqMinX, rd.eqMinY, rd.eqMinZ,
                    rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ)) {
                continue;
            }
        }

        // Distance (squared) from camera to nearest AABB edge
        float dx = std::max(0.0f, std::max(rd.eqMinX - camEqX, camEqX - rd.eqMaxX));
        float dy = std::max(0.0f, std::max(rd.eqMinY - camEqY, camEqY - rd.eqMaxY));
        float dz = std::max(0.0f, std::max(rd.eqMinZ - camEqZ, camEqZ - rd.eqMaxZ));
        float distSq = dx * dx + dy * dy + dz * dz;

        visibleRegions.push_back({regionIdx, distSq});
        outRegionsVisible++;
    }

    // Sort regions front-to-back for draw entry collection and occlusion rasterization
    if (frontToBack || occlusionCull) {
        std::sort(visibleRegions.begin(), visibleRegions.end(),
                  [](const RegionEntry& a, const RegionEntry& b) {
                      return a.distSq < b.distSq;
                  });
    }

    // Software occlusion culling: rasterize nearby occluders, then test AABBs
    if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
        // Compute camera right/up vectors from forward direction (EQ Z-up)
        float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
        float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen, fZ = cam.dirZ / fwdLen;
        // Right = forward × Z-up (then normalize)
        float rX = fY * 0.0f - fZ * 0.0f;  // fwd × (0,0,1) simplified
        float rY = fZ * 0.0f - fX * 0.0f;
        // Actually: right = normalize(forward × (0,0,1))
        rX = fY;   // cross product with Z-up: (fY*1 - fZ*0, fZ*0 - fX*1, fX*0 - fY*0)
        rY = -fX;
        float rZ = 0.0f;
        float rLen = std::sqrt(rX * rX + rY * rY);
        if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }
        // Up = right × forward
        float uX = rY * fZ - rZ * fY;
        float uY = rZ * fX - rX * fZ;
        float uZ = rX * fY - rY * fX;

        data.occlusionCuller->setCamera(camEqX, camEqY, camEqZ,
                                         fX, fY, fZ, rX, rY, rZ, uX, uY, uZ,
                                         cam.fovRadV, cam.aspect);
        data.occlusionCuller->clear();
        data.occlusionCuller->resetStats();

        // Rasterize occluders from nearest regions
        int maxOccluderRegions = data.occlusionCuller->getConfig().maxOccluderRegions;
        int rasterized = 0;
        for (const auto& re : visibleRegions) {
            if (rasterized >= maxOccluderRegions) break;
            const auto& occluders = data.occlusionCuller->getRegionOccluders(re.idx);
            if (occluders.empty()) continue;
            for (const auto& tri : occluders) {
                data.occlusionCuller->rasterizeTriangle(tri.v0, tri.v1, tri.v2);
            }
            rasterized++;
        }

        // Test AABBs and remove occluded regions
        if (rasterized > 0) {
            auto it = visibleRegions.begin();
            while (it != visibleRegions.end()) {
                auto& rd = data.regions[it->idx];
                if (data.occlusionCuller->testAABB(rd.eqMinX, rd.eqMinY, rd.eqMinZ,
                                                    rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ)) {
                    outRegionsOccluded++;
                    outRegionsVisible--;
                    it = visibleRegions.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // Collect mesh buffers from visible regions
    for (const auto& re : visibleRegions) {
        auto& rd = data.regions[re.idx];
        if (!rd.mesh) continue;

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(rd.position);

        bool budgetExceeded = false;
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = rd.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            int bufPolys = buf->getIndexCount() / 3;
            if (polysCollected + bufPolys > polyBudget) { budgetExceeded = true; break; }
            if (static_cast<int>(outEntries.size()) >= drawBudget) { budgetExceeded = true; break; }

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = 0;
            if (mat.getTexture(0)) {
                texId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
            }
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            outEntries.push_back({matKey, re.distSq, buf, worldMat});
            polysCollected += bufPolys;
        }

        outRegionsDrawn++;
        if (budgetExceeded) break;
    }

    // Material sort (primary key: material, secondary: distance)
    if (materialSort) {
        std::sort(outEntries.begin(), outEntries.end(),
                  [](const DrawEntry& a, const DrawEntry& b) {
                      if (a.materialKey != b.materialKey)
                          return a.materialKey < b.materialKey;
                      return a.distanceSq < b.distanceSq;
                  });
    }
}

void placeEntitiesAlongPath(ZoneBenchData& data,
                             const std::vector<glm::vec3>& pathPositions) {
    if (data.entities.empty() || pathPositions.empty()) return;

    int n = static_cast<int>(data.entities.size());
    int pathLen = static_cast<int>(pathPositions.size());

    for (int i = 0; i < n; ++i) {
        // Distribute evenly along path
        int pathIdx = (i * pathLen / n) % pathLen;
        const auto& pos = pathPositions[pathIdx];

        // Offset slightly to the side so entities aren't on the camera path
        float offsetX = (i % 2 == 0) ? 5.0f : -5.0f;
        float offsetY = ((i / 2) % 2 == 0) ? 3.0f : -3.0f;

        data.entities[i].eqX = pos.x + offsetX;
        data.entities[i].eqY = pos.y + offsetY;
        data.entities[i].eqZ = pos.z - 6.0f;  // Remove eye height, place on ground

        // Offset upward so feet touch ground (not model center)
        float meshOffsetY = 0;
        if (data.entities[i].mesh) {
            auto bb = data.entities[i].mesh->getBoundingBox();
            // Irrlicht Y-up: MinEdge.Y is typically negative (below origin)
            meshOffsetY = -bb.MinEdge.Y;
        }

        // Irrlicht coords: EQ (x,y,z) -> Irrlicht (x,z,y)
        data.entities[i].position = irr::core::vector3df(
            data.entities[i].eqX,
            data.entities[i].eqZ + meshOffsetY,
            data.entities[i].eqY);
    }

    printf("  Placed %d entities along camera path\n", n);
}

void collectEntityDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    std::vector<DrawEntry>& outEntries,
    int& outEntityDrawCalls,
    int& outEntityPolys) {

    outEntityDrawCalls = 0;
    outEntityPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        // Distance cull
        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        // Frustum cull (use entity position as a sphere test with radius ~5)
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(ent.position);

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = 0;
            if (mat.getTexture(0)) {
                texId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
            }
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            outEntries.push_back({matKey, distSq, buf, worldMat});
            outEntityDrawCalls++;
            outEntityPolys += buf->getIndexCount() / 3;
        }
    }
}

int renderBatchedEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    // Collect visible entity mesh buffers grouped by material key
    struct BatchEntry {
        irr::scene::IMeshBuffer* buffer;
        irr::core::vector3df entityPos;
    };
    std::map<uint32_t, std::vector<BatchEntry>> materialBuckets;
    irr::video::SMaterial firstMaterial;  // Track first material per bucket
    std::map<uint32_t, irr::video::SMaterial> bucketMaterials;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        if (dx * dx + dy * dy + dz * dz > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = 0;
            if (mat.getTexture(0)) {
                texId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
            }
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            materialBuckets[matKey].push_back({buf, ent.position});
            if (bucketMaterials.find(matKey) == bucketMaterials.end())
                bucketMaterials[matKey] = mat;
        }
    }

    if (materialBuckets.empty()) return 0;

    // For each material bucket: merge vertices with baked transforms, single draw
    irr::core::matrix4 identityMat;
    data.driver->setTransform(irr::video::ETS_WORLD, identityMat);

    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;
    int drawCalls = 0;

    for (auto& [matKey, entries] : materialBuckets) {
        mergedVerts.clear();
        mergedIndices.clear();

        for (const auto& entry : entries) {
            irr::scene::IMeshBuffer* buf = entry.buffer;
            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());

            // Check 16-bit index overflow
            if (baseVertex + buf->getVertexCount() > 65535) break;

            // Copy vertices with world transform baked in
            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += entry.entityPos;
                mergedVerts.push_back(vert);
            }

            // Copy indices with base offset
            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx) {
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }
        }

        if (mergedIndices.empty()) continue;

        data.driver->setMaterial(bucketMaterials[matKey]);
        data.driver->drawVertexPrimitiveList(
            mergedVerts.data(), mergedVerts.size(),
            mergedIndices.data(), mergedIndices.size() / 3,
            irr::video::EVT_STANDARD,
            irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);

        outEntityPolys += mergedIndices.size() / 3;
        drawCalls++;
    }

    return drawCalls;
}

void findSurfacePosition(const ZoneBenchData& data,
                          float& outX, float& outY, float& outZ) {
    // Find the region with the most polygons whose center Z is in the
    // top quartile of Z heights (surface-level, not underground).
    float zRange = data.boundsMaxZ - data.boundsMinZ;
    float zThreshold = data.boundsMinZ + zRange * 0.5f;  // Top half of Z range

    int bestPolys = 0;
    float bestX = (data.boundsMinX + data.boundsMaxX) * 0.5f;
    float bestY = (data.boundsMinY + data.boundsMaxY) * 0.5f;
    float bestZ = data.boundsMaxZ - 10.0f;  // Default: near top

    for (const auto& [idx, rd] : data.regions) {
        if (!rd.mesh) continue;

        // Region center Z (EQ coords)
        float regionCenterZ = (rd.eqMinZ + rd.eqMaxZ) * 0.5f;
        if (regionCenterZ < zThreshold) continue;  // Skip underground

        // Count polys in this region
        int polys = 0;
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            auto* buf = rd.mesh->getMeshBuffer(i);
            if (buf) polys += buf->getIndexCount() / 3;
        }

        if (polys > bestPolys) {
            bestPolys = polys;
            bestX = (rd.eqMinX + rd.eqMaxX) * 0.5f;
            bestY = (rd.eqMinY + rd.eqMaxY) * 0.5f;
            bestZ = regionCenterZ;
        }
    }

    outX = bestX;
    outY = bestY;
    outZ = bestZ;

    printf("  Surface position: (%.1f, %.1f, %.1f) [%d polys in region]\n",
           outX, outY, outZ, bestPolys);
}

} // namespace ZoneBench
