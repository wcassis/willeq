#include "zone_bench_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <unordered_map>

#include "common/logging.h"

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
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/load_mem_audit.h"

#include <filesystem>


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

    // P04: --load-mem-audit — same [LOAD-MEM-AUDIT] line format as
    // loadZoneSequential's per-step sampling (load_mem_audit.h) so benchmark
    // and willeq runs are directly comparable. No mesh cache / entity prep
    // pools in the benchmark path; those columns stay 0.
    auto sampleStep = [&](const char* stepName) {
        if (!config.loadMemAudit) return;
        size_t eqe1Bytes = 0;
        for (const auto& l : data->eqe1Loaders) {
            if (l) eqe1Bytes += l->getMappedBytes();
        }
        size_t texBytes = data->textureCache ? data->textureCache->getCurrentUsage() : 0;
        EQT::Graphics::logLoadMemAuditStep(stepName, eqe1Bytes, texBytes, 0, 0);
    };

    sampleStep("bench_00_start");

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
    sampleStep("bench_02_s3d");

    // ── Step 3: Set up constrained renderer config (OrangePi preset) ──
    auto constrainedConfig = ConstrainedRendererConfig::fromPreset(
        ConstrainedRenderingPreset::OrangePi);
    constrainedConfig.eqe1Path = config.eqe1Path;

    // ── Step 4: Set up texture cache ──
    data->textureCache = std::make_unique<ConstrainedTextureCache>(constrainedConfig);
    data->textureCache->setDriver(data->driver);

    // ── Step 4b: Load EQE1 flat packs (zone-specific only) ──
    if (!config.eqe1Path.empty()) {
        // Only load archives needed for this zone + global assets
        std::vector<std::string> eqe1Names = {
            config.zoneName,                    // zone geometry textures
            config.zoneName + "_obj",           // zone object textures (trees, lamps)
            config.zoneName + "_chr",           // zone character textures
            "global_chr",                       // global character textures
            "global2_chr", "global3_chr", "global4_chr",
            "global5_chr", "global6_chr", "global7_chr",
            "gequip", "gequip2", "gequip3", "gequip4",
            "gequip5", "gequip6", "gequip8",
            "gatesequip", "ldonequip",
            "lgequip", "lgequip2", "lgequip_amr", "lgequip_amr2",
            "loyequip", "vequip", "frogequip", "obequip_lexit",
            "willeq",                           // composite entity textures
        };

        int eqe1Count = 0;
        for (const auto& name : eqe1Names) {
            std::string path = config.eqe1Path + "/" + name + ".eqe1";
            auto loader = std::make_unique<EQE1Loader>();
            if (loader->open(path)) {
                data->textureCache->addEQE1Loader(loader.get());
                data->eqe1Loaders.push_back(std::move(loader));
                ++eqe1Count;
            }
        }
        printf("  EQE1 loaders: %d archives loaded\n", eqe1Count);
    }
    sampleStep("bench_04b_eqe1");

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

        // H02d: --shader-debug diagnostic mode. Set the debug mode on the
        // shader manager (drives FRAGMENT_SHADER_DEBUG_SRC's branch) and
        // record the debug material type so draw entry collection can swap
        // per-pixel materials to it — mirrors the renderer's /shaderdebug
        // swapToDebugShader, applied at draw time instead of mutating mesh
        // buffers permanently (see collectDrawEntries/collectObjectDrawEntries).
        if (config.shaderDebugMode > 0 && data->zoneShader->isAvailable() &&
            data->zoneShader->isDebugShaderAvailable()) {
            data->zoneShader->setShaderDebug(config.shaderDebugMode);
            data->shaderDebugMaterial = data->zoneShader->getDebugMaterialType();
            printf("  Shader debug mode: %d (debug material=%d)\n",
                   config.shaderDebugMode, data->shaderDebugMaterial);
        }
    }

    // Enable per-pixel player light BEFORE building meshes so
    // getActiveSolid() returns the per-pixel variant
    if (data->zoneShader) {
        data->zoneShader->setPerPixelPlayerLight(true);
    }

    // ── Step 7: Build per-region meshes ──
    auto meshStart = std::chrono::steady_clock::now();

    ZoneMeshBuilder builder(data->smgr, data->driver, data->device->getFileSystem());
    builder.setConstrainedTextureCache(data->textureCache.get());

    if (data->zoneShader && data->zoneShader->isAvailable()) {
        builder.setShaderMaterialTypes(
            data->zoneShader->getActiveSolid(),
            data->zoneShader->getActiveAlphaTest(),
            data->zoneShader->getActiveAlphaTestRGBA());
    }

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
            mesh = builder.buildTexturedMesh(*geom, data->zone->textures);
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
    sampleStep("bench_07_meshes");

    // ── Step 7b: Load and build zone objects ──
    if (config.loadObjects) {
        auto objStart = std::chrono::steady_clock::now();

        // Re-load zone with objects enabled
        S3DLoader objLoader;
        S3DLoadOptions objOpts;
        objOpts.loadObjects = true;
        objOpts.loadCharacters = false;
        objOpts.computeCombinedGeometry = false;

        if (objLoader.loadZone(archivePath, objOpts)) {
            auto objZone = objLoader.getZone();
            if (objZone) {
                // Build meshes for each placed object
                ZoneMeshBuilder objBuilder(data->smgr, data->driver, data->device->getFileSystem());
                objBuilder.setConstrainedTextureCache(data->textureCache.get());
                if (data->zoneShader && data->zoneShader->isAvailable()) {
                    objBuilder.setShaderMaterialTypes(
                        data->zoneShader->getActiveSolid(),
                        data->zoneShader->getActiveAlphaTest(),
                        data->zoneShader->getActiveAlphaTestRGBA());
                }

                int objCount = 0, objBuffers = 0;
                for (const auto& instance : objZone->objects) {
                    if (!instance.geometry || instance.geometry->vertices.empty()) continue;

                    const std::string& objName = instance.placeable ? instance.placeable->getName() : "";

                    // Build mesh — use object textures merged with zone textures
                    auto mergedTextures = objZone->objectTextures;
                    for (const auto& [name, tex] : objZone->textures) {
                        if (mergedTextures.find(name) == mergedTextures.end()) {
                            mergedTextures[name] = tex;
                        }
                    }

                    irr::scene::IMesh* objMesh = nullptr;
                    if (!mergedTextures.empty() && !instance.geometry->textureNames().empty()) {
                        objMesh = objBuilder.buildTexturedMesh(*instance.geometry, mergedTextures);
                    } else {
                        objMesh = objBuilder.buildColoredMesh(*instance.geometry);
                    }
                    if (!objMesh) continue;

                    uploadMeshHWBuffers(data->driver, objMesh);

                    // Build world transform matrix (EQ → Irrlicht coordinate swap)
                    const auto& p = instance.placeable;
                    irr::core::matrix4 mat;

                    // Scale (EQ x,y,z → Irrlicht x,z,y)
                    irr::core::matrix4 scaleMat;
                    scaleMat.setScale(irr::core::vector3df(
                        p->getScaleX(), p->getScaleZ(), p->getScaleY()));

                    // Rotation
                    irr::core::matrix4 rotMat;
                    rotMat.setRotationDegrees(irr::core::vector3df(
                        p->getRotateX(), p->getRotateY(), p->getRotateZ()));

                    // Translation (EQ x,y,z → Irrlicht x,z,y)
                    irr::core::matrix4 transMat;
                    transMat.setTranslation(irr::core::vector3df(
                        p->getX(), p->getZ(), p->getY()));

                    mat = transMat * rotMat * scaleMat;

                    // Disable backface culling on object materials (matching production)
                    for (irr::u32 mi = 0; mi < objMesh->getMeshBufferCount(); ++mi) {
                        objMesh->getMeshBuffer(mi)->getMaterial().BackfaceCulling = false;
                    }

                    ZoneBenchData::PlacedObject obj;
                    obj.mesh = objMesh;
                    obj.worldTransform = mat;
                    obj.eqX = p->getX();
                    obj.eqY = p->getY();
                    obj.eqZ = p->getZ();
                    obj.name = objName;

                    // Compute world-space AABB in EQ coords from all mesh buffer vertices
                    obj.eqMinX = std::numeric_limits<float>::max();
                    obj.eqMinY = obj.eqMinX; obj.eqMinZ = obj.eqMinX;
                    obj.eqMaxX = std::numeric_limits<float>::lowest();
                    obj.eqMaxY = obj.eqMaxX; obj.eqMaxZ = obj.eqMaxX;
                    for (irr::u32 bi = 0; bi < objMesh->getMeshBufferCount(); bi++) {
                        auto* buf = objMesh->getMeshBuffer(bi);
                        auto* verts = (const irr::video::S3DVertex*)buf->getVertices();
                        for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                            // Transform vertex to world space (Irrlicht coords)
                            irr::core::vector3df worldPos;
                            mat.transformVect(worldPos, verts[vi].Pos);
                            // Convert Irrlicht (x,y,z) → EQ (x,z,y) for AABB
                            float ex = worldPos.X, ey = worldPos.Z, ez = worldPos.Y;
                            obj.eqMinX = std::min(obj.eqMinX, ex);
                            obj.eqMinY = std::min(obj.eqMinY, ey);
                            obj.eqMinZ = std::min(obj.eqMinZ, ez);
                            obj.eqMaxX = std::max(obj.eqMaxX, ex);
                            obj.eqMaxY = std::max(obj.eqMaxY, ey);
                            obj.eqMaxZ = std::max(obj.eqMaxZ, ez);
                        }
                    }

                    data->objects.push_back(obj);

                    objBuffers += objMesh->getMeshBufferCount();
                    ++objCount;
                }

                // Precompute BSP region overlap for each object
                if (data->bspTree && !data->bspTree->regions.empty()) {
                    // Build region AABBs from zone geometry
                    size_t numRegions = data->bspTree->regions.size();
                    int totalRegionHits = 0;
                    for (auto& obj : data->objects) {
                        for (const auto& [regionIdx, rd] : data->regions) {
                            // Test AABB overlap between object and region (both in EQ coords)
                            if (obj.eqMinX <= rd.eqMaxX && obj.eqMaxX >= rd.eqMinX &&
                                obj.eqMinY <= rd.eqMaxY && obj.eqMaxY >= rd.eqMinY &&
                                obj.eqMinZ <= rd.eqMaxZ && obj.eqMaxZ >= rd.eqMinZ) {
                                obj.bspRegions.push_back(regionIdx);
                            }
                        }
                        totalRegionHits += obj.bspRegions.size();
                    }
                    printf("  Object PVS: %d objects, %d total region hits (avg %.1f regions/obj)\n",
                           (int)data->objects.size(), totalRegionHits,
                           data->objects.empty() ? 0.0 : (double)totalRegionHits / data->objects.size());
                }

                // Upload any textures queued during object mesh building
                int objUploaded = data->textureCache->uploadAllPending();

                auto objEnd = std::chrono::steady_clock::now();
                printf("  Objects built: %d objects, %d buffers, %d textures (%.0f ms)\n",
                       objCount, objBuffers, objUploaded,
                       std::chrono::duration<double, std::milli>(objEnd - objStart).count());
            }
        } else {
            printf("  Objects: none (no _obj.s3d or objects.wld)\n");
        }
    }

    // ── Step 8: Portal system ──
    // NOTE: Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
    // The PortalSystem builds AABB portal quads from BSP boundaries but the
    // benchmark never used them for culling — it uses PVS bitvectors from the
    // WLD file (Fragment 0x22) which is a completely separate mechanism.
    // Commented out to avoid confusion between PVS culling (works) and
    // portal culling (not implemented).
    //
    // data->portalSystem = std::make_unique<PortalSystem>();
    // data->portalSystem->buildFromBsp(*bspTree, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
    // printf("  Portal system: %zu portals, hasPortals=%d\n",
    //        data->portalSystem->getData().portals.size(),
    //        data->portalSystem->hasPortals());

    // ── Step 9: Set up frustum culler ──
    data->frustumCuller = std::make_unique<FrustumCuller>();
    data->frustumCuller->setEnabled(true);

    // ── Step 10: Extract occluder triangles for software occlusion culling ──
    {
        OcclusionCullerConfig occConfig;
        occConfig.width = 64;
        occConfig.height = 32;
        occConfig.maxOccluderRegions = (config.occluderBudget > 0) ? config.occluderBudget : 16;  // P11: run-time override, -1 = prior hardcoded 16
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
            data->textureCache->setMeshBuilder(mb);
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

        // Wire composite entity textures (same as production renderer)
        if (config.compositeEntities && !config.eqe1Path.empty()) {
            data->raceModelLoader->setCompositeEntityPath(config.eqe1Path);

            // Open per-race composite EQE1 files from the eqe1 directory
            namespace fs = std::filesystem;
            if (fs::is_directory(config.eqe1Path)) {
                int compositeCount = 0;
                for (const auto& entry : fs::directory_iterator(config.eqe1Path)) {
                    if (entry.path().extension() != ".eqe1") continue;
                    std::string stem = entry.path().stem().string();
                    // Composite files match pattern: {race}_hs_def_{gender}_{variant}
                    if (stem.find("_hs_def_") == std::string::npos &&
                        stem.find("_actordef_") == std::string::npos) continue;
                    auto loader = std::make_unique<EQE1Loader>();
                    if (loader->open(entry.path().string())) {
                        data->textureCache->addEQE1Loader(loader.get());
                        data->eqe1Loaders.push_back(std::move(loader));
                        ++compositeCount;
                    }
                }
                printf("  Composite EQE1: %d archives loaded\n", compositeCount);
            }
        }

        // Upload any textures decoded during model loading
        data->textureCache->uploadAllPending();

        static const uint16_t kDefaultRaceIds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};  // or --test-races
        const uint16_t* raceIds = config.testRaceIds.empty() ? kDefaultRaceIds : config.testRaceIds.data();
        int numRaces = config.testRaceIds.empty() ? (int)(sizeof(kDefaultRaceIds) / sizeof(kDefaultRaceIds[0])) : (int)config.testRaceIds.size();

        // When composite entities are enabled, use preloadModelData() to populate
        // composite texture data (JSON + EQE1 lookup) before building meshes.
        // This mirrors the production renderer's EntityPrepWorker flow.
        if (config.compositeEntities && !config.eqe1Path.empty()) {
            printf("  Composite: compositeEntityPath='%s'\n", config.eqe1Path.c_str());
            int preloadOk = 0, preloadFail = 0;
            for (int r = 0; r < numRaces; ++r) {
                bool ok = data->raceModelLoader->preloadModelData(raceIds[r], 0);
                printf("    preloadModelData(race=%d, gender=0) = %s\n",
                       raceIds[r], ok ? "OK" : "FAILED");
                if (ok) preloadOk++; else preloadFail++;
            }
            printf("  Composite: preloaded %d OK, %d failed\n", preloadOk, preloadFail);

            data->raceModelLoader->promotePreparedModels();
            printf("  Composite: promotePreparedModels() done\n");

            // Verify composite data survived promotion
            for (int r = 0; r < numRaces; ++r) {
                auto md = data->raceModelLoader->getRaceModelData(raceIds[r], 0);
                if (md) {
                    printf("    race=%d: raceName='%s' compositeData=%s compositeBmpPath='%s' "
                           "rawGeometry=%s combinedGeometry=%s\n",
                           raceIds[r], md->raceName.c_str(),
                           md->compositeData ? "YES" : "NO",
                           md->compositeBmpPath.c_str(),
                           md->rawGeometry ? "yes" : "no",
                           md->combinedGeometry ? "yes" : "no");
                } else {
                    printf("    race=%d: getRaceModelData returned NULL\n", raceIds[r]);
                }
            }

            data->textureCache->uploadAllPending();
        }

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

            // Create animated scene nodes when animation is enabled
            if (config.animate) {
                // Mix of animations for variety
                static const char* animCodes[] = {"o01", "l01", "l02", "c01", "p01"};
                static const int numAnims = 5;

                data->animatedNodes.resize(config.entityCount, nullptr);
                int animatedCount = 0;

                for (int i = 0; i < config.entityCount; ++i) {
                    uint16_t raceId = raceIds[i % numRaces];
                    auto* node = data->raceModelLoader->createAnimatedNode(
                        raceId, 0, data->smgr->getRootSceneNode());
                    if (node) {
                        data->animatedNodes[i] = node;
                        // P12B: do NOT register the per-instance animated mesh
                        // (node->getInstanceMesh()) as a GLES2 hardware buffer here.
                        // uploadMeshHWBuffers()/gles2CreateStaticHWBuffer() ->
                        // COpenGLES2Driver::createStaticHardwareBuffer() uploads a
                        // ONE-TIME GPU-side VBO snapshot; COpenGLES2Driver::drawMeshBuffer()
                        // then draws straight from that frozen VBO forever for any
                        // mesh buffer found in HWBufferMap -- it never checks
                        // IMeshBuffer::setDirty()/ChangedID, so it never re-uploads.
                        // EHM_STREAM here was aspirational (the comment's intent),
                        // but the actual call always creates a STATIC buffer
                        // regardless of the hint. Since applyAnimation() re-skins
                        // instanceMesh_'s CPU vertex data every frame (OnAnimate ->
                        // applyAnimation -> applyBoneTransformsToMeshImpl), and this
                        // upload ran BEFORE the first real pose was ever applied
                        // (createAnimatedNode() only calls playAnimation(), not
                        // forceAnimationUpdate()), every animated entity was drawn
                        // from a frozen snapshot of the RAW, per-bone-local unposed
                        // mesh (each body piece stacked near its own bone origin) --
                        // the "crumpled blob" root cause. Production's
                        // EQAnimatedMeshSceneNode::render() never registers a static
                        // hardware buffer for instanceMesh_ either (see
                        // animated_mesh_creation.cpp/entity_renderer.cpp) -- it always
                        // draws from the live CPU vertex array via the driver's
                        // client-side-array fallback in drawMeshBuffer(), which is
                        // what correctly picks up each frame's re-skinned vertices.
                        // Leaving instanceMesh_ unregistered here makes this
                        // benchmark path do the same thing.
                        // Assign animation with variety: cycle through codes
                        const char* animCode = animCodes[i % numAnims];
                        if (!node->playAnimation(animCode, true)) {
                            // Fallback to idle if requested animation not available
                            node->playAnimation("o01", true);
                        }
                        // Stagger start time so entities aren't in sync
                        // Advance each entity by a different offset
                        float offsetMs = static_cast<float>(i * 137 % 1000);
                        node->getAnimator().update(offsetMs);
                        node->forceAnimationUpdate();
                        ++animatedCount;
                    }
                }
                // Upload any textures from animated mesh creation
                data->textureCache->uploadAllPending();
                printf("  Created %d animated scene nodes\n", animatedCount);
            }
        }

        auto entityEnd = std::chrono::steady_clock::now();
        printf("  Entity models loaded in %.0f ms (%d entities, %zu race meshes)\n",
               std::chrono::duration<double, std::milli>(entityEnd - entityStart).count(),
               config.entityCount, raceMeshes.size());
    }
    sampleStep("bench_11_entities");

    // ── Summary ──
    auto totalEnd = std::chrono::steady_clock::now();
    printf("Zone loaded in %.0f ms\n",
           std::chrono::duration<double, std::milli>(totalEnd - totalStart).count());
    printf("  Zone bounds (EQ): X=[%.0f,%.0f] Y=[%.0f,%.0f] Z=[%.0f,%.0f]\n",
           zbMinX, zbMaxX, zbMinY, zbMaxY, zbMinZ, zbMaxZ);
    printf("  Center (EQ): (%.0f, %.0f, %.0f)\n",
           (zbMinX + zbMaxX) * 0.5f, (zbMinY + zbMaxY) * 0.5f, (zbMinZ + zbMaxZ) * 0.5f);
    sampleStep("bench_99_done");

    return data;
}

// H02b: address-independent 16-bit texture grouping ID for materialKey.
// The old key was `(uintptr_t)texture & 0xFFFF` -- the low bits of a heap
// POINTER. That made the material-sorted draw order depend on allocation
// addresses, so ASLR (or merely a different environment-block size shifting
// the layout) reordered equal-material draws between otherwise identical
// runs -> z-fighting pixel flips on coplanar geometry and a wobbling
// stateChanges count. This silently broke the golden-frame CI gate's
// exact-match bar (1-33 differing pixels between identical invocations,
// only reproducible across processes). An FNV-1a hash of the texture NAME
// is just as good a grouping key and is stable across runs.
// Q02 applied this same fix to production (renderer_zone_draw.cpp has a
// byte-identical copy of this helper) -- keep the two copies in sync.
static uint32_t textureGroupId(const irr::video::SMaterial& mat) {
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}

// H02d: if --shader-debug is active and this buffer uses one of
// ZoneShaderManager's custom per-pixel materials (solid, alpha-test, RGBA
// alpha-test, lightweight variants, etc. — anything >= getMaterialTypeSolid(),
// mirroring IrrlichtRenderer::swapToDebugShader's threshold check), return the
// debug visualization material type to use as a draw-time override. Returns
// -1 (no override) otherwise.
static irr::s32 debugMaterialOverride(const ZoneBench::ZoneBenchData& data, irr::s32 currentMaterialType) {
    if (data.shaderDebugMaterial < 0 || !data.zoneShader) return -1;
    if (currentMaterialType >= data.zoneShader->getMaterialTypeSolid()) return data.shaderDebugMaterial;
    return -1;
}

// P06: mirrors IrrlichtRenderer's (anonymous-namespace) estimateRegionScreenArea
// in src/client/graphics/renderer_zone_draw.cpp -- same method (project 8
// world-space AABB corners with the current view-projection, clamped
// screen-space bounding-rect area; any corner at/behind the near plane ->
// conservative full-screen area) so benchmark area-mode numbers predict
// production area-mode behavior.
static float estimateRegionScreenAreaBench(const irr::core::aabbox3df& worldBox,
                                            const irr::core::matrix4& viewProj,
                                            float screenW, float screenH) {
    const float fullScreenArea = screenW * screenH;
    const irr::core::vector3df& mn = worldBox.MinEdge;
    const irr::core::vector3df& mx = worldBox.MaxEdge;
    const irr::core::vector3df corners[8] = {
        {mn.X, mn.Y, mn.Z}, {mx.X, mn.Y, mn.Z}, {mn.X, mx.Y, mn.Z}, {mx.X, mx.Y, mn.Z},
        {mn.X, mn.Y, mx.Z}, {mx.X, mn.Y, mx.Z}, {mn.X, mx.Y, mx.Z}, {mx.X, mx.Y, mx.Z}
    };

    float minSx = std::numeric_limits<float>::max();
    float maxSx = -std::numeric_limits<float>::max();
    float minSy = std::numeric_limits<float>::max();
    float maxSy = -std::numeric_limits<float>::max();
    for (int i = 0; i < 8; ++i) {
        float clip[4];
        viewProj.transformVect(clip, corners[i]);
        if (clip[3] <= 0.0001f) {
            return fullScreenArea;  // At/behind near plane -- conservative.
        }
        const float invW = 1.0f / clip[3];
        const float sx = (clip[0] * invW * 0.5f + 0.5f) * screenW;
        const float sy = (1.0f - (clip[1] * invW * 0.5f + 0.5f)) * screenH;
        minSx = std::min(minSx, sx);
        maxSx = std::max(maxSx, sx);
        minSy = std::min(minSy, sy);
        maxSy = std::max(maxSy, sy);
    }

    minSx = std::max(0.0f, minSx);
    maxSx = std::min(screenW, maxSx);
    minSy = std::max(0.0f, minSy);
    maxSy = std::min(screenH, maxSy);

    if (maxSx <= minSx || maxSy <= minSy) return 0.0f;
    return (maxSx - minSx) * (maxSy - minSy);
}

// P07d: mirrors IrrlichtRenderer's (anonymous-namespace) computeLightEffectiveRadius
// in src/client/graphics/renderer_zone_draw.cpp -- same attenuation solve
// (1/(c + l*d + q*d^2) < 1/255, saturating NdotL/wrap-lighting at 1.0,
// conservative) so benchmark radius-mode numbers predict production behavior.
static float computeLightEffectiveRadiusBench(const float atten[3], float margin) {
    const float c = atten[0];
    const float l = atten[1];
    const float q = atten[2];
    const float target = 255.0f - c;
    float r;
    if (q > 0.0f) {
        const float disc = l * l + 4.0f * q * target;
        if (disc < 0.0f || target <= 0.0f) {
            r = 0.0f;
        } else {
            r = (-l + std::sqrt(disc)) / (2.0f * q);
        }
    } else if (l > 0.0f) {
        r = (target > 0.0f) ? (target / l) : 0.0f;
    } else {
        r = std::numeric_limits<float>::max();
    }
    if (r >= std::numeric_limits<float>::max() / margin) {
        return std::numeric_limits<float>::max();
    }
    return r * margin;
}

void collectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    bool materialSort,
    bool pvsCull,
    bool frustumCull,
    bool occlusionCull,
    bool frontToBack,
    int maxDraws,
    int maxPolys,
    std::vector<DrawEntry>& outEntries,
    int& outRegionsVisible,
    int& outRegionsDrawn,
    int& outRegionsOccluded,
    float* outLightRadius,
    int* outRegionsDemotedRadius,
    float* outCollectCullMs,
    float* outSortMs,
    std::vector<int>* outDrawsPerRegion,
    std::vector<int>* outPolysPerDraw)
{
    // P08: collect/cull phase starts here; the sort block below is timed separately.
    const auto collectCullStart = std::chrono::steady_clock::now();

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
        if (pvsCull && camRegion != SIZE_MAX && data.bspTree) {
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

    // P06: area-mode budget decision. Computed as a separate pre-pass sorted
    // strictly by camera distance, independent of visibleRegions' actual
    // iteration/draw order below (which only follows distance when
    // frontToBack||occlusionCull requested a sort) -- the area accumulation's
    // nearest-first invariant must hold regardless of --no-front-to-back.
    // Mirrors IrrlichtRenderer::drawZoneGeometrySorted's area-mode branch.
    std::unordered_map<size_t, bool> areaUseLW;
    if (data.adaptiveLighting && data.zonePPAreaMode) {
        std::vector<const RegionEntry*> distOrder;
        distOrder.reserve(visibleRegions.size());
        for (const auto& re : visibleRegions) distOrder.push_back(&re);
        std::sort(distOrder.begin(), distOrder.end(),
                  [](const RegionEntry* a, const RegionEntry* b) { return a->distSq < b->distSq; });

        irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
        viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);
        irr::core::dimension2du screenSize = data.driver->getScreenSize();
        float screenW = static_cast<float>(screenSize.Width);
        float screenH = static_cast<float>(screenSize.Height);
        float areaBudgetLimit = data.zonePPAreaBudgetFactor * screenW * screenH;
        float cumulativeArea = 0.0f;
        int regionsDrawn = 0;
        for (const auto* rePtr : distOrder) {
            auto& rd = data.regions[rePtr->idx];
            // Add this region's own area FIRST, then decide -- even the
            // nearest region can flip to lightweight if its own footprint
            // alone exceeds the budget (see the matching comment in
            // IrrlichtRenderer::drawZoneGeometrySorted).
            if (rd.mesh) {
                irr::core::aabbox3df worldBox = rd.mesh->getBoundingBox();
                worldBox.MinEdge += rd.position;
                worldBox.MaxEdge += rd.position;
                cumulativeArea += estimateRegionScreenAreaBench(worldBox, viewProj, screenW, screenH);
            }
            // P07: hybrid floor -- first zonePPAreaFloor regions (nearest-first)
            // always keep per-pixel regardless of estimated area; their area
            // still accumulated above so later regions demote as before.
            areaUseLW[rePtr->idx] = (regionsDrawn >= data.zonePPAreaFloor) &&
                                    cumulativeArea > areaBudgetLimit;
            regionsDrawn++;
        }
    }

    // P07d: radius-mode budget decision. Computed as its own pre-pass (order
    // doesn't matter here -- unlike area mode, radius mode's per-region
    // decision is independent across regions, no running total) keyed by
    // region index. Mirrors IrrlichtRenderer::drawZoneGeometrySorted's
    // radius-mode branch: demote iff the closest point of the region's world
    // AABB to the player light position is beyond R.
    std::unordered_map<size_t, bool> radiusUseLW;
    int regionsDemotedRadiusLocal = 0;
    int regionsConsideredRadiusLocal = 0;
    float lightRadiusLocal = 0.0f;
    if (data.adaptiveLighting && data.zonePPRadiusMode && data.zoneShader) {
        const float* pos = data.zoneShader->playerLightPos();
        irr::core::vector3df lightPos(pos[0], pos[1], pos[2]);
        lightRadiusLocal = computeLightEffectiveRadiusBench(
            data.zoneShader->playerLightAtten(), data.zonePPRadiusMargin);
        const float lightRangeSq = (lightRadiusLocal >= std::numeric_limits<float>::max())
                                        ? std::numeric_limits<float>::max()
                                        : lightRadiusLocal * lightRadiusLocal;
        for (const auto& re : visibleRegions) {
            auto& rd = data.regions[re.idx];
            if (!rd.mesh) continue;
            irr::core::aabbox3df worldBox = rd.mesh->getBoundingBox();
            worldBox.MinEdge += rd.position;
            worldBox.MaxEdge += rd.position;
            const float nearX = std::max(worldBox.MinEdge.X, std::min(lightPos.X, worldBox.MaxEdge.X));
            const float nearY = std::max(worldBox.MinEdge.Y, std::min(lightPos.Y, worldBox.MaxEdge.Y));
            const float nearZ = std::max(worldBox.MinEdge.Z, std::min(lightPos.Z, worldBox.MaxEdge.Z));
            const float dx = nearX - lightPos.X;
            const float dy = nearY - lightPos.Y;
            const float dz = nearZ - lightPos.Z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const bool demote = distSq > lightRangeSq;
            radiusUseLW[re.idx] = demote;
            regionsConsideredRadiusLocal++;
            if (demote) regionsDemotedRadiusLocal++;
        }
    }
    if (outLightRadius) *outLightRadius = lightRadiusLocal;
    if (outRegionsDemotedRadius) *outRegionsDemotedRadius = regionsDemotedRadiusLocal;
    (void)regionsConsideredRadiusLocal;

    // Collect mesh buffers from visible regions
    // visibleRegions is sorted front-to-back (nearest first) when
    // frontToBack||occlusionCull; the area-mode decision above does not rely
    // on this loop's order (see pre-pass above).
    int regionIdx = 0;
    for (const auto& re : visibleRegions) {
        auto& rd = data.regions[re.idx];
        if (!rd.mesh) continue;

        // Adaptive zone lighting: nearest N regions get per-pixel, rest get lightweight
        bool usePerPixel;
        if (!data.adaptiveLighting) {
            usePerPixel = true;
        } else if (data.zonePPAreaMode) {
            auto itLW = areaUseLW.find(re.idx);
            usePerPixel = (itLW == areaUseLW.end()) || !itLW->second;
        } else if (data.zonePPRadiusMode) {
            auto itLW = radiusUseLW.find(re.idx);
            usePerPixel = (itLW == radiusUseLW.end()) || !itLW->second;
        } else {
            usePerPixel = regionIdx < data.perPixelZoneBudget;
        }

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(rd.position);

        bool budgetExceeded = false;
        int drawsThisRegion = 0;
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = rd.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            int bufPolys = buf->getIndexCount() / 3;
            if (polysCollected + bufPolys > polyBudget) { budgetExceeded = true; break; }
            if (static_cast<int>(outEntries.size()) >= drawBudget) { budgetExceeded = true; break; }

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)

            // H02d: --shader-debug takes priority over adaptive lighting swaps.
            irr::s32 matOverride = debugMaterialOverride(data, mat.MaterialType);

            // Compute material override for adaptive lighting
            if (matOverride < 0 && data.adaptiveLighting && !usePerPixel && data.materialLWSolid >= 0) {
                irr::s32 origType = mat.MaterialType;
                // Map per-pixel material types to their lightweight equivalents
                if (origType == data.materialPerPixelSolid)
                    matOverride = data.materialLWSolid;
                else if (origType == data.materialPerPixelAlpha)
                    matOverride = data.materialLWAlpha;
            }

            uint32_t effectiveMatType = (matOverride >= 0) ?
                static_cast<uint32_t>(matOverride) : static_cast<uint32_t>(mat.MaterialType);
            uint32_t matKey = (effectiveMatType << 16) | texId;

            outEntries.push_back({matKey, re.distSq, buf, worldMat, matOverride});
            polysCollected += bufPolys;
            drawsThisRegion++;
            if (outPolysPerDraw) outPolysPerDraw->push_back(bufPolys);
        }
        if (outDrawsPerRegion && drawsThisRegion > 0) outDrawsPerRegion->push_back(drawsThisRegion);

        regionIdx++;
        outRegionsDrawn++;
        if (budgetExceeded) break;
    }

    if (outCollectCullMs) {
        *outCollectCullMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - collectCullStart).count();
    }

    // Material sort (primary key: material, secondary: distance)
    const auto sortStart = std::chrono::steady_clock::now();
    if (materialSort) {
        std::sort(outEntries.begin(), outEntries.end(),
                  [](const DrawEntry& a, const DrawEntry& b) {
                      if (a.materialKey != b.materialKey)
                          return a.materialKey < b.materialKey;
                      return a.distanceSq < b.distanceSq;
                  });
    }
    if (outSortMs) {
        *outSortMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - sortStart).count();
    }
}

// P08: computeOcclusionHeadroomUncapped() lives in zone_bench_histogram.cpp/.h (file-size budget).

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

        // Position animated scene node to match (if animation is enabled)
        if (i < static_cast<int>(data.animatedNodes.size()) && data.animatedNodes[i]) {
            data.animatedNodes[i]->setPosition(data.entities[i].position);
            data.animatedNodes[i]->updateAbsolutePosition();
        }

        // Assign priority — simulate realistic mix
        // Entity 0: current target, 1-5: group, 6-10: raid, rest: default/idle
        if (i == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Target;
        else if (i <= 5) data.entities[i].priority = ZoneBenchData::EntityPriority::Group;
        else if (i <= 10) data.entities[i].priority = ZoneBenchData::EntityPriority::Raid;
        else if (i % 5 == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Idle;
        else data.entities[i].priority = ZoneBenchData::EntityPriority::Default;
    }

    printf("  Placed %d entities along camera path (1 target, 5 group, 5 raid, rest default/idle)\n", n);
    for (int i = 0; i < n; ++i) printf("    entity[%d] EQ pos: (%.1f, %.1f, %.1f)\n", i, data.entities[i].eqX, data.entities[i].eqY, data.entities[i].eqZ);
}

void collectObjectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool pvsCull,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outObjectDrawCalls,
    int& outObjectPolys) {

    outObjectDrawCalls = 0;
    outObjectPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    // Find camera's BSP region for PVS
    size_t camRegion = SIZE_MAX;
    if (pvsCull && data.bspTree) {
        camRegion = data.bspTree->findRegionIndexForPoint(cam.eqX, cam.eqY, cam.eqZ);
    }

    for (const auto& obj : data.objects) {
        if (!obj.mesh) continue;

        // Distance cull (nearest AABB edge to camera)
        float dx = std::max(0.0f, std::max(obj.eqMinX - cam.eqX, cam.eqX - obj.eqMaxX));
        float dy = std::max(0.0f, std::max(obj.eqMinY - cam.eqY, cam.eqY - obj.eqMaxY));
        float dz = std::max(0.0f, std::max(obj.eqMinZ - cam.eqZ, cam.eqZ - obj.eqMaxZ));
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        // PVS culling: object visible if ANY of its regions are in camera's PVS
        if (pvsCull && camRegion != SIZE_MAX && data.bspTree &&
            !obj.bspRegions.empty()) {
            auto& camReg = data.bspTree->regions[camRegion];
            bool pvsVisible = false;
            if (camReg) {
                for (size_t regionIdx : obj.bspRegions) {
                    if (regionIdx < camReg->visibleRegions.size() &&
                        camReg->visibleRegions[regionIdx]) {
                        pvsVisible = true;
                        break;
                    }
                }
            }
            if (!pvsVisible) continue;
        }

        // Frustum cull (use object AABB)
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testAABB(
                    obj.eqMinX, obj.eqMinY, obj.eqMinZ,
                    obj.eqMaxX, obj.eqMaxY, obj.eqMaxZ)) {
                continue;
            }
        }

        // Software occlusion cull (test AABB against occlusion buffer from zone pass)
        if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
            if (data.occlusionCuller->testAABB(
                    obj.eqMinX, obj.eqMinY, obj.eqMinZ,
                    obj.eqMaxX, obj.eqMaxY, obj.eqMaxZ)) {
                continue;
            }
        }

        for (irr::u32 i = 0; i < obj.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = obj.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;
            irr::s32 matOverride = debugMaterialOverride(data, mat.MaterialType);  // H02d

            outEntries.push_back({matKey, distSq, buf, obj.worldTransform, matOverride});
            outObjectDrawCalls++;
            outObjectPolys += buf->getIndexCount() / 3;
        }
    }
}

void collectEntityDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outEntityDrawCalls,
    int& outEntityPolys,
    int& outEntitiesVisible) {

    outEntityDrawCalls = 0; outEntityPolys = 0; outEntitiesVisible = 0;
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

        // Software occlusion cull (approximate entity as small AABB around position)
        if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
            float r = 3.0f;  // approximate entity half-size
            if (data.occlusionCuller->testAABB(
                    ent.eqX - r, ent.eqY - r, ent.eqZ,
                    ent.eqX + r, ent.eqY + r, ent.eqZ + 8.0f)) {
                continue;
            }
        }

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(ent.position);

        bool contributedBuffer = false;
        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            outEntries.push_back({matKey, distSq, buf, worldMat});
            outEntityDrawCalls++;
            outEntityPolys += buf->getIndexCount() / 3;
            contributedBuffer = true;
        }
        if (contributedBuffer) outEntitiesVisible++;  // P08b: count once per entity
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
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
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

// Minimum distance from a point to a line segment (3D)
static float pointToSegmentDistSq(float px, float py, float pz,
                                   float ax, float ay, float az,
                                   float bx, float by, float bz) {
    float abx = bx - ax, aby = by - ay, abz = bz - az;
    float apx = px - ax, apy = py - ay, apz = pz - az;
    float t = (apx * abx + apy * aby + apz * abz) /
              (abx * abx + aby * aby + abz * abz + 1e-12f);
    t = std::max(0.0f, std::min(1.0f, t));
    float cx = ax + t * abx - px, cy = ay + t * aby - py, cz = az + t * abz - pz;
    return cx * cx + cy * cy + cz * cz;
}

// Minimum distance between two line segments (approximate: sample endpoints + midpoint)
static float segmentToSegmentDistSq(float a0x, float a0y, float a0z,
                                     float a1x, float a1y, float a1z,
                                     float b0x, float b0y, float b0z,
                                     float b1x, float b1y, float b1z) {
    float best = pointToSegmentDistSq(a0x, a0y, a0z, b0x, b0y, b0z, b1x, b1y, b1z);
    best = std::min(best, pointToSegmentDistSq(a1x, a1y, a1z, b0x, b0y, b0z, b1x, b1y, b1z));
    best = std::min(best, pointToSegmentDistSq(b0x, b0y, b0z, a0x, a0y, a0z, a1x, a1y, a1z));
    best = std::min(best, pointToSegmentDistSq(b1x, b1y, b1z, a0x, a0y, a0z, a1x, a1y, a1z));
    // Midpoints
    float amx = (a0x + a1x) * 0.5f, amy = (a0y + a1y) * 0.5f, amz = (a0z + a1z) * 0.5f;
    float bmx = (b0x + b1x) * 0.5f, bmy = (b0y + b1y) * 0.5f, bmz = (b0z + b1z) * 0.5f;
    best = std::min(best, pointToSegmentDistSq(amx, amy, amz, b0x, b0y, b0z, b1x, b1y, b1z));
    best = std::min(best, pointToSegmentDistSq(bmx, bmy, bmz, a0x, a0y, a0z, a1x, a1y, a1z));
    return best;
}

void analyzePortals(ZoneBenchData& data, const std::string& csvFile) {
    if (!data.portalSystem || !data.portalSystem->hasPortals()) {
        printf("No portals to analyze.\n");
        return;
    }

    auto& portals = data.portalSystem->getData().portals;
    auto wldLoader = data.zone->wldLoader;
    const float contactThreshold = 3.0f;  // EQ units — edge within 3 units of geometry
    const float contactThreshSq = contactThreshold * contactThreshold;

    printf("\n=== Portal Analysis (%zu portals) ===\n", portals.size());

    // Collect triangle edges from each region for contact testing
    // Cache per-region to avoid reloading
    struct RegionEdges {
        struct Edge { float x0, y0, z0, x1, y1, z1; };
        std::vector<Edge> edges;
    };
    std::unordered_map<size_t, RegionEdges> regionEdgeCache;

    auto getRegionEdges = [&](size_t regionIdx) -> const RegionEdges& {
        auto it = regionEdgeCache.find(regionIdx);
        if (it != regionEdgeCache.end()) return it->second;

        RegionEdges& re = regionEdgeCache[regionIdx];
        auto geom = wldLoader->getGeometryForRegion(regionIdx);
        if (!geom || geom->vertices.empty()) return re;

        for (const auto& tri : geom->triangles) {
            if (tri.v1 >= geom->vertices.size() ||
                tri.v2 >= geom->vertices.size() ||
                tri.v3 >= geom->vertices.size()) continue;

            auto mkEdge = [&](uint32_t vi0, uint32_t vi1) {
                const auto& v0 = geom->vertices[vi0];
                const auto& v1 = geom->vertices[vi1];
                re.edges.push_back({
                    geom->centerX + v0.x, geom->centerY + v0.y, geom->centerZ + v0.z,
                    geom->centerX + v1.x, geom->centerY + v1.y, geom->centerZ + v1.z});
            };
            mkEdge(tri.v1, tri.v2);
            mkEdge(tri.v2, tri.v3);
            mkEdge(tri.v3, tri.v1);
        }
        return re;
    };

    // Analyze each portal
    struct PortalStats {
        size_t idx;
        float area;
        int edgeCount;
        int contactEdges;     // Edges that touch geometry
        float contactRatio;   // contactEdges / edgeCount
        float minEdgeDist;    // Closest geometry distance across all edges
        bool hasFloorContact; // At least one edge near ground level (low Z)
    };
    std::vector<PortalStats> stats;
    stats.reserve(portals.size());

    int analyzed = 0;
    for (size_t pi = 0; pi < portals.size(); ++pi) {
        const auto& portal = portals[pi];
        int nVerts = static_cast<int>(portal.vertexCount());
        if (nVerts < 3) continue;

        const auto& edgesA = getRegionEdges(portal.regionA);
        const auto& edgesB = getRegionEdges(portal.regionB);

        PortalStats ps;
        ps.idx = pi;
        ps.area = portal.area;
        ps.edgeCount = nVerts;
        ps.contactEdges = 0;
        ps.minEdgeDist = 1e9f;
        ps.hasFloorContact = false;

        // For each portal edge, find nearest geometry edge from both adjacent regions
        for (int e = 0; e < nVerts; ++e) {
            int e2 = (e + 1) % nVerts;
            float pe0x = portal.vx(e), pe0y = portal.vy(e), pe0z = portal.vz(e);
            float pe1x = portal.vx(e2), pe1y = portal.vy(e2), pe1z = portal.vz(e2);

            float bestDistSq = 1e18f;

            // Search both adjacent regions
            for (const auto* regionEdges : {&edgesA, &edgesB}) {
                for (const auto& ge : regionEdges->edges) {
                    float d = segmentToSegmentDistSq(
                        pe0x, pe0y, pe0z, pe1x, pe1y, pe1z,
                        ge.x0, ge.y0, ge.z0, ge.x1, ge.y1, ge.z1);
                    if (d < bestDistSq) bestDistSq = d;
                    if (bestDistSq < contactThreshSq) break;  // Early out
                }
                if (bestDistSq < contactThreshSq) break;
            }

            float dist = std::sqrt(bestDistSq);
            if (dist < ps.minEdgeDist) ps.minEdgeDist = dist;
            if (bestDistSq < contactThreshSq) {
                ps.contactEdges++;
                // Check if this contact edge is near ground level
                float edgeMidZ = (pe0z + pe1z) * 0.5f;
                float regionMidZ = 0;
                auto itA = data.regions.find(portal.regionA);
                if (itA != data.regions.end())
                    regionMidZ = (itA->second.eqMinZ + itA->second.eqMaxZ) * 0.5f;
                float bottomZ = (itA != data.regions.end()) ? itA->second.eqMinZ : -999;
                if (edgeMidZ < bottomZ + 10.0f)  // Within 10 units of region bottom
                    ps.hasFloorContact = true;
            }
        }

        ps.contactRatio = (ps.edgeCount > 0) ?
            static_cast<float>(ps.contactEdges) / ps.edgeCount : 0;
        stats.push_back(ps);

        analyzed++;
        if (analyzed % 5000 == 0)
            printf("  Analyzed %d/%zu portals...\n", analyzed, portals.size());
    }

    // Distribution summary
    int bucket0 = 0, bucket25 = 0, bucket50 = 0, bucket75 = 0, bucket100 = 0;
    int withFloor = 0;
    float totalArea0 = 0, totalAreaHigh = 0;
    for (const auto& ps : stats) {
        if (ps.contactRatio < 0.01f) { bucket0++; totalArea0 += ps.area; }
        else if (ps.contactRatio < 0.25f) bucket25++;
        else if (ps.contactRatio < 0.50f) bucket50++;
        else if (ps.contactRatio < 0.75f) { bucket75++; totalAreaHigh += ps.area; }
        else { bucket100++; totalAreaHigh += ps.area; }
        if (ps.hasFloorContact) withFloor++;
    }

    printf("\n  Contact ratio distribution:\n");
    printf("    0%%  contact:   %6d portals (avg area %.0f)\n", bucket0,
           bucket0 > 0 ? totalArea0 / bucket0 : 0);
    printf("    1-24%% contact: %6d portals\n", bucket25);
    printf("    25-49%% contact:%6d portals\n", bucket50);
    printf("    50-74%% contact:%6d portals\n", bucket75);
    printf("    75-100%% contact:%5d portals (avg area %.0f)\n", bucket100,
           bucket100 > 0 ? totalAreaHigh / (bucket75 + bucket100) : 0);
    printf("    With floor:    %6d portals\n", withFloor);
    printf("\n");

    // CSV output
    if (!csvFile.empty()) {
        FILE* f = fopen(csvFile.c_str(), "w");
        if (f) {
            fprintf(f, "portal_idx,regionA,regionB,area,edge_count,contact_edges,"
                       "contact_ratio,min_edge_dist,has_floor,center_x,center_y,center_z\n");
            for (const auto& ps : stats) {
                const auto& p = portals[ps.idx];
                fprintf(f, "%zu,%zu,%zu,%.1f,%d,%d,%.3f,%.2f,%d,%.1f,%.1f,%.1f\n",
                        ps.idx, p.regionA, p.regionB, ps.area,
                        ps.edgeCount, ps.contactEdges, ps.contactRatio,
                        ps.minEdgeDist, ps.hasFloorContact ? 1 : 0,
                        p.centerX, p.centerY, p.centerZ);
            }
            fclose(f);
            printf("  Portal analysis CSV written: %s\n", csvFile.c_str());
        }
    }
}

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

int renderDirectGLEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
#ifndef EQT_HAS_GLES2
    // Direct GL mode only works with GLES2 driver
    (void)data; (void)cam; (void)renderDistance; (void)frustumCull;
    return 0;
#else
    float renderDistSq = renderDistance * renderDistance;

    // Get current shader program and uniform locations once
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    GLint uWVP = glGetUniformLocation(currentProgram, "mWorldViewProj");
    GLint uWorld = glGetUniformLocation(currentProgram, "mWorld");

    // Pre-compute view-projection matrix (Irrlicht stores these)
    irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
    viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);

    // Minimal state: enable depth test, disable blend, backface cull off
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Vertex attribute layout: S3DVertex = pos(12) + normal(12) + color(4) + uv(8) = 36
    const GLsizei stride = 36;

    // Save Irrlicht's GL state so we can restore it after direct GL calls
    GLint savedProgram = 0, savedTexture = 0, savedVBO = 0, savedEBO = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedVBO);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &savedEBO);

    GLuint lastTexture = 0;
    int drawCalls = 0;

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

        // Compute WVP for this entity
        irr::core::matrix4 worldMat;
        worldMat.setTranslation(ent.position);
        irr::core::matrix4 wvp = viewProj;
        wvp *= worldMat;

        // Upload matrices once per entity
        if (uWVP >= 0) glUniformMatrix4fv(uWVP, 1, GL_FALSE, wvp.pointer());
        if (uWorld >= 0) glUniformMatrix4fv(uWorld, 1, GL_FALSE, worldMat.pointer());

        // Draw all mesh buffers with minimal state changes
        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Texture bind — only if changed
            const irr::video::SMaterial& mat = buf->getMaterial();
            if (mat.getTexture(0)) {
                GLuint texId = static_cast<GLuint>(
                    mat.getTexture(0)->getDriverTextureHandle());
                if (texId != lastTexture) {
                    glBindTexture(GL_TEXTURE_2D, texId);
                    lastTexture = texId;
                }
            }

            // For HW-buffered meshes, bind VBO/EBO directly
            // We need access to the HW buffer map — use driver's handle
            GLuint vbo = static_cast<GLuint>(buf->getHardwareMappingHint_Vertex());
            GLuint ebo = static_cast<GLuint>(buf->getHardwareMappingHint_Index());

            // Fallback: use client-side arrays if no HW buffer
            // (entities typically use client-side arrays)
            const void* vertexData = buf->getVertices();
            const void* indexData = buf->getIndices();

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, vertexData);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                                  static_cast<const char*>(vertexData) + 12);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                                  static_cast<const char*>(vertexData) + 24);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                                  static_cast<const char*>(vertexData) + 28);
            glEnableVertexAttribArray(3);

            glDrawElements(GL_TRIANGLES, buf->getIndexCount(),
                           GL_UNSIGNED_SHORT, indexData);

            outEntityPolys += buf->getIndexCount() / 3;
            drawCalls++;
        }
    }

    // Restore Irrlicht's GL state so it doesn't crash on next frame
    glUseProgram(savedProgram);
    glBindTexture(GL_TEXTURE_2D, savedTexture);
    glBindBuffer(GL_ARRAY_BUFFER, savedVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, savedEBO);

    return drawCalls;
#endif
}

int renderDirectGLCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
#ifndef EQT_HAS_GLES2
    (void)data; (void)cam; (void)renderDistance; (void)frustumCull;
    return 0;
#else
    float renderDistSq = renderDistance * renderDistance;

    // Save Irrlicht state
    GLint savedProgram = 0, savedTexture = 0, savedVBO = 0, savedEBO = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedVBO);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &savedEBO);

    // Get shader uniforms
    GLint currentProgram = savedProgram;
    GLint uWVP = glGetUniformLocation(currentProgram, "mWorldViewProj");
    GLint uWorld = glGetUniformLocation(currentProgram, "mWorld");

    // Pre-compute VP matrix
    irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
    viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);

    // Set GL state once for all entities
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    const GLsizei stride = 36;
    GLuint lastTexture = 0;
    int drawCalls = 0;

    // Reusable merge buffers
    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;

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

        // Merge all body parts into one buffer with baked position
        mergedVerts.clear();
        mergedIndices.clear();
        GLuint texId = 0;

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            if (texId == 0 && buf->getMaterial().getTexture(0)) {
                texId = static_cast<GLuint>(
                    buf->getMaterial().getTexture(0)->getDriverTextureHandle());
            }

            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());
            if (baseVertex + buf->getVertexCount() > 65535) break;

            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += ent.position;
                mergedVerts.push_back(vert);
            }

            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
        }

        if (mergedIndices.empty()) continue;

        // Upload identity world (positions baked into vertices)
        irr::core::matrix4 identityMat;
        irr::core::matrix4 wvp = viewProj;
        if (uWVP >= 0) glUniformMatrix4fv(uWVP, 1, GL_FALSE, wvp.pointer());
        if (uWorld >= 0) glUniformMatrix4fv(uWorld, 1, GL_FALSE, identityMat.pointer());

        // Bind texture only if changed
        if (texId != lastTexture) {
            glBindTexture(GL_TEXTURE_2D, texId);
            lastTexture = texId;
        }

        // Set vertex attribs and draw
        const void* vdata = mergedVerts.data();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, vdata);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              static_cast<const char*>(vdata) + 12);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              static_cast<const char*>(vdata) + 24);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                              static_cast<const char*>(vdata) + 28);
        glEnableVertexAttribArray(3);

        glDrawElements(GL_TRIANGLES, mergedIndices.size(),
                       GL_UNSIGNED_SHORT, mergedIndices.data());

        outEntityPolys += mergedIndices.size() / 3;
        drawCalls++;
    }

    // Restore Irrlicht state
    glUseProgram(savedProgram);
    glBindTexture(GL_TEXTURE_2D, savedTexture);
    glBindBuffer(GL_ARRAY_BUFFER, savedVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, savedEBO);

    return drawCalls;
#endif
}

int renderCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;

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

        // Merge ALL mesh buffers for this entity into one draw
        mergedVerts.clear();
        mergedIndices.clear();
        irr::video::SMaterial compositeMat;
        bool hasMaterial = false;

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Use first buffer's material as the composite material
            if (!hasMaterial) {
                compositeMat = buf->getMaterial();
                hasMaterial = true;
            }

            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());
            if (baseVertex + buf->getVertexCount() > 65535) break;

            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += ent.position;
                mergedVerts.push_back(vert);
            }

            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx) {
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }
        }

        if (mergedIndices.empty() || !hasMaterial) continue;

        data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
        data.driver->setMaterial(compositeMat);
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

int renderLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    float billboardDist,
    bool frustumCull,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count) {

    outEntityPolys = 0;
    outTier1Count = 0;
    outTier2Count = 0;
    float renderDistSq = renderDistance * renderDistance;
    float bbDistSq = billboardDist * billboardDist;

    // Camera basis vectors for billboard orientation (EQ Z-up)
    float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
    float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen;
    // Right vector (EQ Z-up): forward × (0,0,1)
    float rX = fY, rY = -fX;
    float rLen = std::sqrt(rX * rX + rY * rY);
    if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }

    // Tier 1: composite entities (near)
    std::vector<irr::video::S3DVertex> compositeVerts;
    std::vector<irr::u16> compositeIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;

    // Tier 2: billboard quads (far) — collected then drawn in one batch
    std::vector<irr::video::S3DVertex> bbVerts;
    std::vector<irr::u16> bbIndices;
    irr::video::SMaterial bbMaterial;
    bool hasBbMaterial = false;

    const float bbHalfW = 3.0f;   // Billboard half-width in EQ units
    const float bbHeight = 8.0f;  // Billboard height in EQ units

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        if (distSq < bbDistSq) {
            // Tier 1: composite entity (1 draw per entity)
            compositeVerts.clear();
            compositeIndices.clear();
            irr::video::SMaterial compositeMat;
            bool hasMat = false;

            for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0) continue;

                if (!hasMat) { compositeMat = buf->getMaterial(); hasMat = true; }

                irr::u32 baseVertex = static_cast<irr::u32>(compositeVerts.size());
                if (baseVertex + buf->getVertexCount() > 65535) break;

                const irr::video::S3DVertex* srcVerts =
                    static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                    irr::video::S3DVertex vert = srcVerts[v];
                    vert.Pos += ent.position;
                    compositeVerts.push_back(vert);
                }

                const irr::u16* srcIdx = buf->getIndices();
                for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                    compositeIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }

            if (!compositeIndices.empty() && hasMat) {
                data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
                data.driver->setMaterial(compositeMat);
                data.driver->drawVertexPrimitiveList(
                    compositeVerts.data(), compositeVerts.size(),
                    compositeIndices.data(), compositeIndices.size() / 3,
                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                    irr::video::EIT_16BIT);
                outEntityPolys += compositeIndices.size() / 3;
                drawCalls++;
                outTier1Count++;
            }
        } else {
            // Tier 2: billboard quad
            // Grab material from first buffer if we don't have one yet
            if (!hasBbMaterial && ent.mesh->getMeshBufferCount() > 0) {
                auto* buf = ent.mesh->getMeshBuffer(0);
                if (buf) { bbMaterial = buf->getMaterial(); hasBbMaterial = true; }
            }

            // Check 16-bit index overflow
            if (bbVerts.size() + 4 > 65535) continue;

            irr::u32 base = static_cast<irr::u32>(bbVerts.size());

            // Billboard corners in Irrlicht coords (Y-up)
            // Entity position is already in Irrlicht coords
            // Right vector: EQ (rX, rY, 0) -> Irrlicht (rX, 0, rY)
            float irrRX = rX * bbHalfW, irrRZ = rY * bbHalfW;

            irr::video::SColor white(255, 255, 255, 255);
            // Bottom-left
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 1.0f));
            // Bottom-right
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 1.0f));
            // Top-right
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y + bbHeight,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 0.0f));
            // Top-left
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y + bbHeight,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 0.0f));

            bbIndices.push_back(base + 0);
            bbIndices.push_back(base + 1);
            bbIndices.push_back(base + 2);
            bbIndices.push_back(base + 0);
            bbIndices.push_back(base + 2);
            bbIndices.push_back(base + 3);

            outTier2Count++;
        }
    }

    // Draw all billboards in one batch
    if (!bbIndices.empty() && hasBbMaterial) {
        data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
        data.driver->setMaterial(bbMaterial);
        data.driver->drawVertexPrimitiveList(
            bbVerts.data(), bbVerts.size(),
            bbIndices.data(), bbIndices.size() / 3,
            irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);
        outEntityPolys += bbIndices.size() / 3;
        drawCalls++;  // One draw for ALL billboards
    }

    return drawCalls;
}

void placeEntitiesTrain(ZoneBenchData& data, const glm::vec3& centerPos) {
    if (data.entities.empty()) return;

    int n = static_cast<int>(data.entities.size());
    // Pack entities in a tight grid around the center
    // ~3 units apart (shoulder-to-shoulder mob pack)
    float spacing = 3.0f;
    int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));

    for (int i = 0; i < n; ++i) {
        int gx = i % gridSize;
        int gy = i / gridSize;
        float offsetX = (gx - gridSize / 2) * spacing;
        float offsetY = (gy - gridSize / 2) * spacing;

        data.entities[i].eqX = centerPos.x + offsetX;
        data.entities[i].eqY = centerPos.y + offsetY;
        data.entities[i].eqZ = centerPos.z - 6.0f;  // Remove eye height

        float meshOffsetY = 0;
        if (data.entities[i].mesh) {
            auto bb = data.entities[i].mesh->getBoundingBox();
            meshOffsetY = -bb.MinEdge.Y;
        }

        data.entities[i].position = irr::core::vector3df(
            data.entities[i].eqX,
            data.entities[i].eqZ + meshOffsetY,
            data.entities[i].eqY);

        // Position animated scene node to match (if animation is enabled)
        if (i < static_cast<int>(data.animatedNodes.size()) && data.animatedNodes[i]) {
            data.animatedNodes[i]->setPosition(data.entities[i].position);
            data.animatedNodes[i]->updateAbsolutePosition();
        }

        // Train entities are all hostile mobs — mostly default priority
        if (i == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Target;
        else data.entities[i].priority = ZoneBenchData::EntityPriority::Default;
    }

    float extent = gridSize * spacing;
    printf("  Train: %d entities in %.0fx%.0f grid at (%.0f, %.0f, %.0f)\n",
           n, extent, extent, centerPos.x, centerPos.y, centerPos.z);
}

// Render an entity mesh into a dedicated FBO to create a billboard impostor texture.
// Each entity gets its own RTT that persists as both the render target and the billboard texture.
// Returns a cached texture, or renders one if not yet cached for this entity.
static irr::video::ITexture* getOrCreateImpostor(
    ZoneBenchData& data,
    int entityIdx,
    const ZoneBenchData::PlacedEntity& ent,
    const CameraState& cam) {

    auto& state = data.adaptiveLodState;

    // Check cache first
    auto it = state.impostorTextures.find(entityIdx);
    if (it != state.impostorTextures.end())
        return it->second;

    // Create a dedicated RTT for this entity (serves as both FBO and billboard texture)
    char texName[64];
    snprintf(texName, sizeof(texName), "impostor_%d", entityIdx);
    irr::core::dimension2d<irr::u32> rttSize(state.impostorSize, state.impostorSize);
    irr::video::ITexture* rtt = data.driver->addRenderTargetTexture(rttSize, texName);
    if (!rtt) {
        state.impostorTextures[entityIdx] = nullptr;
        return nullptr;
    }

    // Save current view/projection (drawAll overwrites these)
    irr::core::matrix4 savedView, savedProj;
    savedView = data.driver->getTransform(irr::video::ETS_VIEW);
    savedProj = data.driver->getTransform(irr::video::ETS_PROJECTION);

    // Set entity to idle pose (first frame of o01) for the impostor snapshot.
    // The static T-pose is unnatural — idle is what players actually see in-game.
    irr::scene::IMesh* meshToDraw = ent.mesh;
    if (entityIdx >= 0 && entityIdx < static_cast<int>(data.animatedNodes.size())
        && data.animatedNodes[entityIdx]) {
        auto* node = data.animatedNodes[entityIdx];
        node->playAnimation("o01", true);
        node->forceAnimationUpdate();
        if (node->getInstanceMesh())
            meshToDraw = node->getInstanceMesh();
    }

    // Set render target to this entity's FBO, clear with transparent black
    data.driver->setRenderTarget(rtt, true, true, irr::video::SColor(0, 0, 0, 0));

    // Compute bounding box from idle-pose vertex positions
    irr::core::aabbox3df bbox;
    bool bboxInit = false;
    for (irr::u32 b = 0; b < meshToDraw->getMeshBufferCount(); ++b) {
        auto* buf = meshToDraw->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        const irr::video::S3DVertex* verts =
            static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
            if (!bboxInit) {
                bbox.reset(verts[v].Pos);
                bboxInit = true;
            } else {
                bbox.addInternalPoint(verts[v].Pos);
            }
        }
    }
    if (!bboxInit) {
        data.driver->setRenderTarget(nullptr, false, false);
        data.driver->setTransform(irr::video::ETS_VIEW, savedView);
        data.driver->setTransform(irr::video::ETS_PROJECTION, savedProj);
        state.impostorTextures[entityIdx] = nullptr;
        return nullptr;
    }

    // Mesh coords: X=left/right, Y=height, Z=front/back
    // Camera along +X, Y up (inverted). Width=Z, Height=Y.
    irr::core::vector3df extent = bbox.getExtent();
    float halfW = extent.Z * 0.55f;
    float halfH = extent.Y * 0.55f;
    if (halfW < 1.0f) halfW = 1.0f;
    if (halfH < 1.0f) halfH = 1.0f;

    irr::core::matrix4 projMat;
    projMat.buildProjectionMatrixOrthoLH(halfW * 2.0f, halfH * 2.0f, 0.1f, 500.0f);
    data.driver->setTransform(irr::video::ETS_PROJECTION, projMat);

    irr::core::vector3df center = bbox.getCenter();
    irr::core::vector3df eye = center + irr::core::vector3df(100.0f, 0, 0);
    irr::core::vector3df up(0, -1, 0);

    irr::core::matrix4 viewMat;
    viewMat.buildCameraLookAtMatrixLH(eye, center, up);
    data.driver->setTransform(irr::video::ETS_VIEW, viewMat);

    // World: identity (mesh is in model space at origin)
    irr::core::matrix4 worldMat;
    data.driver->setTransform(irr::video::ETS_WORLD, worldMat);

    // Draw the entity mesh into the FBO.
    // Use the original material (custom zone shader) — it renders correctly in RTT.
    // Only override fog and backface culling.
    for (irr::u32 b = 0; b < meshToDraw->getMeshBufferCount(); ++b) {
        auto* buf = meshToDraw->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        irr::video::SMaterial mat = buf->getMaterial();
        mat.FogEnable = false;
        mat.BackfaceCulling = false;
        data.driver->setMaterial(mat);
        data.driver->drawMeshBuffer(buf);
    }

    // Restore default render target and saved matrices
    data.driver->setRenderTarget(nullptr, false, false);
    data.driver->setTransform(irr::video::ETS_VIEW, savedView);
    data.driver->setTransform(irr::video::ETS_PROJECTION, savedProj);

    state.impostorTextures[entityIdx] = rtt;
    return rtt;
}

int renderAdaptiveLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    float lastFrameMs,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count,
    float& outEntityMs) {

    outEntityPolys = 0;
    outTier1Count = 0;
    outTier2Count = 0;
    outEntityMs = 0;
    float renderDistSq = renderDistance * renderDistance;
    auto& cfg = data.adaptiveLodConfig;
    auto& state = data.adaptiveLodState;

    // Tick hysteresis timers (decrement counters, remove expired)
    state.tickHysteresis();

    // ── Collect visible entities with priority scoring ──
    struct VisibleEntity {
        int entityIdx;
        float distSq;
        float priorityScore;  // Lower = higher priority (render as composite first)
    };
    std::vector<VisibleEntity> visible;

    for (int i = 0; i < static_cast<int>(data.entities.size()); ++i) {
        const auto& ent = data.entities[i];
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Priority score: lower = more important = rendered as composite first.
        // Priority tiers dominate, distance breaks ties within a tier.
        // Tier weight: Target=0, Group=1, ..., Idle=7. Multiply by large constant
        // so tier always dominates over distance.
        float tierWeight;
        switch (ent.priority) {
            case ZoneBenchData::EntityPriority::Target:          tierWeight = 0.0f; break;
            case ZoneBenchData::EntityPriority::Group:           tierWeight = 1.0f; break;
            case ZoneBenchData::EntityPriority::Raid:            tierWeight = 2.0f; break;
            case ZoneBenchData::EntityPriority::Guild:           tierWeight = 3.0f; break;
            case ZoneBenchData::EntityPriority::Friend:          tierWeight = 4.0f; break;
            case ZoneBenchData::EntityPriority::RecentInteract:  tierWeight = 5.0f; break;
            case ZoneBenchData::EntityPriority::Default:         tierWeight = 6.0f; break;
            case ZoneBenchData::EntityPriority::Idle:            tierWeight = 7.0f; break;
            default: tierWeight = 6.0f; break;
        }
        float dist = std::sqrt(distSq);
        // 10000 ensures tier always dominates over distance (max zone dist ~3000)
        float priorityScore = tierWeight * 10000.0f + dist;
        visible.push_back({i, distSq, priorityScore});
    }

    // Sort by priority score ascending — lowest = highest priority = composite first
    std::sort(visible.begin(), visible.end(),
              [](const VisibleEntity& a, const VisibleEntity& b) {
                  return a.priorityScore < b.priorityScore;
              });

    int visibleCount = static_cast<int>(visible.size());

    // ── Proactive prediction: how many composites can we afford? ──
    // Two signals:
    //   1) Frame time: predictedCost = baseZoneMs + N * perEntityMs < budget * safetyMargin
    //   2) Hard cap: never exceed maxEntitiesPerScene
    float effectiveBudgetMs = cfg.frameBudgetMs * cfg.safetyMargin;
    float headroomMs = effectiveBudgetMs - state.avgBaseZoneMs;
    int rawLimit = (state.avgPerEntityMs > 0.001f)
        ? static_cast<int>(headroomMs / state.avgPerEntityMs)
        : cfg.maxEntitiesPerScene;
    rawLimit = std::max(rawLimit, 1);
    rawLimit = std::min(rawLimit, cfg.maxEntitiesPerScene);

    // Rate-limit changes to prevent oscillation (max ±2 per frame)
    if (state.smoothedCompositeLimit < 0) {
        state.smoothedCompositeLimit = rawLimit;  // Initialize on first frame
    } else {
        int delta = rawLimit - state.smoothedCompositeLimit;
        int maxDelta = state.maxLimitChangePerFrame;
        if (delta > maxDelta) delta = maxDelta;
        if (delta < -maxDelta) delta = -maxDelta;
        state.smoothedCompositeLimit += delta;
    }
    int compositeLimit = state.smoothedCompositeLimit;

    // Debug: log prediction state periodically
    static int lodFrameCounter = 0;
    if (++lodFrameCounter % 60 == 1) {
        LOG_INFO(MOD_GRAPHICS, "AdaptiveLOD: visible={} limit={} (raw={} cap={}) "
                 "avgBase={:.1f}ms avgEntity={:.1f}ms headroom={:.1f}ms budget={:.1f}ms lastFrame={:.1f}ms",
                 visibleCount, compositeLimit, rawLimit, cfg.maxEntitiesPerScene,
                 state.avgBaseZoneMs, state.avgPerEntityMs, headroomMs, effectiveBudgetMs, lastFrameMs);
    }

    // ── Walk sorted list: composites up to limit, then billboards ──
    // Camera basis for billboards (XZ plane right vector)
    float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
    float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen;
    float rX = fY, rY = -fX;
    float rLen = std::sqrt(rX * rX + rY * rY);
    if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }

    const float bbHalfW = 3.0f;
    const float bbHeight = 8.0f;

    // Billboard material: textured with impostor RTT, alpha test to discard transparent bg
    irr::video::SMaterial bbMaterial;
    bbMaterial.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
    bbMaterial.MaterialTypeParam = 0.1f;  // Alpha ref threshold
    bbMaterial.Lighting = false;
    bbMaterial.BackfaceCulling = false;

    irr::video::S3DVertex bbQuad[4];
    irr::u16 bbIdx[6] = {0, 1, 2, 0, 2, 3};

    std::vector<irr::video::S3DVertex> compositeVerts;
    std::vector<irr::u16> compositeIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;
    int compositeIdx = 0;

    float entityDrawMs = 0;  // Accumulate only actual draw time (not impostor creation)

    for (int vi = 0; vi < visibleCount; ++vi) {
        const auto& ve = visible[vi];
        const auto& ent = data.entities[ve.entityIdx];

        // Determine if this entity should be a billboard:
        // 1) Over the composite limit (frame-time or hard-cap driven)
        // 2) At the boundary (within 3 slots of limit) AND has active hysteresis timer —
        //    prevents flickering. Entities well within the limit always recover.
        bool overLimit = (compositeIdx >= compositeLimit);
        bool nearBoundary = (compositeIdx >= compositeLimit - 3);
        bool forcedBillboard = nearBoundary && state.isForcedBillboard(ve.entityIdx);
        bool isBillboard = overLimit || forcedBillboard;

        if (isBillboard) {
            // Start or refresh hysteresis timer
            state.markBillboard(ve.entityIdx, cfg.hysteresisFrames);

            // Get or create RTT impostor texture for this entity.
            // This is NOT counted in entity draw time — impostor creation is a one-time
            // cost that would otherwise poison the rolling average and cause oscillation.
            irr::video::ITexture* impostorTex = getOrCreateImpostor(
                data, ve.entityIdx, ent, cam);
            if (!impostorTex) continue;

            float irrRX = rX * bbHalfW, irrRZ = rY * bbHalfW;
            irr::video::SColor white(255, 255, 255, 255);

            // Billboard quad with impostor texture UVs [0,1]
            // FBO RTT content is V-flipped (V=0 at top in GL), so UV Y is 0 at top, 1 at bottom
            bbQuad[0] = irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 1.0f);
            bbQuad[1] = irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 1.0f);
            bbQuad[2] = irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y + bbHeight,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 0.0f);
            bbQuad[3] = irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y + bbHeight,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 0.0f);

            bbMaterial.setTexture(0, impostorTex);
            data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
            data.driver->setMaterial(bbMaterial);
            data.driver->drawVertexPrimitiveList(
                bbQuad, 4, bbIdx, 2,
                irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                irr::video::EIT_16BIT);
            drawCalls++;

            outTier2Count++;
        } else {
            // Composite entity (1 draw call)
            compositeVerts.clear();
            compositeIndices.clear();
            irr::video::SMaterial compositeMat;
            bool hasMat = false;

            for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0) continue;
                if (!hasMat) { compositeMat = buf->getMaterial(); hasMat = true; }

                irr::u32 baseVertex = static_cast<irr::u32>(compositeVerts.size());
                if (baseVertex + buf->getVertexCount() > 65535) break;

                const irr::video::S3DVertex* srcVerts =
                    static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                    irr::video::S3DVertex vert = srcVerts[v];
                    vert.Pos += ent.position;
                    compositeVerts.push_back(vert);
                }

                const irr::u16* srcIdx = buf->getIndices();
                for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                    compositeIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }

            if (!compositeIndices.empty() && hasMat) {
                // Adaptive lighting: nearest composites get per-pixel shader
                if (data.adaptiveLighting && data.materialLWSolid >= 0) {
                    if (compositeIdx < data.perPixelEntityBudget) {
                        compositeMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            data.materialPerPixelSolid);
                    } else {
                        compositeMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            data.materialLWSolid);
                    }
                }
                compositeIdx++;

                auto drawStart = std::chrono::steady_clock::now();
                data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
                data.driver->setMaterial(compositeMat);
                data.driver->drawVertexPrimitiveList(
                    compositeVerts.data(), compositeVerts.size(),
                    compositeIndices.data(), compositeIndices.size() / 3,
                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                    irr::video::EIT_16BIT);
                outEntityPolys += compositeIndices.size() / 3;
                drawCalls++;
                outTier1Count++;
                entityDrawMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - drawStart).count();
            }
        }
    }

    // Billboards are drawn per-entity above (each has its own impostor texture)

    outEntityMs = entityDrawMs;

    // Update rolling averages for next frame's prediction.
    // Skip on first frame (lastFrameMs == 0) to avoid poisoning the average.
    if (lastFrameMs > 0.0f && outTier1Count > 0) {
        state.updateRollingAverage(lastFrameMs, outEntityMs, outTier1Count,
                                   cfg.rollingWindowSize);
    }

    return drawCalls;
}

// ============================================================================
// Occluder-based portal detection
// ============================================================================

// Small 2D rasterizer for projecting triangles onto a plane
struct PlaneGrid {
    int width, height;
    std::vector<uint8_t> pixels;  // 1 = covered by geometry
    float originU, originV;       // World-space origin of grid
    float cellSize;               // World units per pixel
    // Plane basis vectors (in EQ coords)
    float uAxisX, uAxisY, uAxisZ;
    float vAxisX, vAxisY, vAxisZ;
    // Plane normal and point
    float normalX, normalY, normalZ, planeDist;

    void init(int w, int h, float cs,
              float ou, float ov,
              float ux, float uy, float uz,
              float vx, float vy, float vz,
              float nx, float ny, float nz, float pd) {
        width = w; height = h; cellSize = cs;
        originU = ou; originV = ov;
        uAxisX = ux; uAxisY = uy; uAxisZ = uz;
        vAxisX = vx; vAxisY = vy; vAxisZ = vz;
        normalX = nx; normalY = ny; normalZ = nz; planeDist = pd;
        pixels.assign(w * h, 0);
    }

    // Project a world-space point onto grid coordinates
    void project(float wx, float wy, float wz, float& gu, float& gv) const {
        gu = (wx * uAxisX + wy * uAxisY + wz * uAxisZ - originU) / cellSize;
        gv = (wx * vAxisX + wy * vAxisY + wz * vAxisZ - originV) / cellSize;
    }

    // Distance from point to the plane
    float planeDotPoint(float wx, float wy, float wz) const {
        return normalX * wx + normalY * wy + normalZ * wz + planeDist;
    }

    // Rasterize a triangle projected onto the grid (scanline fill)
    void rasterizeTriangle(float u0, float v0, float u1, float v1, float u2, float v2) {
        // Sort by v
        if (v0 > v1) { std::swap(u0, u1); std::swap(v0, v1); }
        if (v0 > v2) { std::swap(u0, u2); std::swap(v0, v2); }
        if (v1 > v2) { std::swap(u1, u2); std::swap(v1, v2); }

        auto scanline = [&](int y, float leftU, float rightU) {
            if (y < 0 || y >= height) return;
            int x0 = std::max(0, static_cast<int>(std::floor(std::min(leftU, rightU))));
            int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max(leftU, rightU))));
            for (int x = x0; x <= x1; ++x)
                pixels[y * width + x] = 1;
        };

        float totalH = v2 - v0;
        if (totalH < 0.001f) return;

        for (int y = std::max(0, static_cast<int>(std::floor(v0)));
             y <= std::min(height - 1, static_cast<int>(std::ceil(v2))); ++y) {
            float fy = static_cast<float>(y) + 0.5f;
            float t = (fy - v0) / totalH;
            float uLong = u0 + t * (u2 - u0);

            float uShort;
            if (fy < v1) {
                float shortH = v1 - v0;
                if (shortH < 0.001f) uShort = u0;
                else uShort = u0 + (fy - v0) / shortH * (u1 - u0);
            } else {
                float shortH = v2 - v1;
                if (shortH < 0.001f) uShort = u1;
                else uShort = u1 + (fy - v1) / shortH * (u2 - u1);
            }

            scanline(y, uLong, uShort);
        }
    }

    int coveredPixels() const {
        int count = 0;
        for (auto p : pixels) count += p;
        return count;
    }
};

// Find all adjacent region pairs by walking BSP tree.
// Returns pairs of (regionA, regionB, splitPlane normal+dist).
struct RegionBoundary {
    size_t regionA, regionB;
    float normalX, normalY, normalZ, splitDist;
};

static void findRegionBoundariesRecursive(
    int nodeIdx, const BspTree& bsp,
    std::vector<std::pair<float, bool>>& ancestorPlanes,  // (dot value, isLeft)
    std::vector<RegionBoundary>& boundaries) {

    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= bsp.nodes.size()) return;
    const auto& node = bsp.nodes[nodeIdx];

    if (node.regionId > 0) return;  // Leaf

    // Collect leaf regions reachable from left and right subtrees
    auto collectLeaves = [&](int subtreeRoot, std::vector<size_t>& leaves) {
        std::vector<int> stack = {subtreeRoot};
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            if (idx < 0 || static_cast<size_t>(idx) >= bsp.nodes.size()) continue;
            const auto& n = bsp.nodes[idx];
            if (n.regionId > 0) {
                size_t regionIdx = static_cast<size_t>(n.regionId - 1);
                if (regionIdx < bsp.regions.size() && bsp.regions[regionIdx] &&
                    bsp.regions[regionIdx]->containsPolygons) {
                    leaves.push_back(regionIdx);
                }
            } else {
                if (n.left >= 0) stack.push_back(n.left);
                if (n.right >= 0) stack.push_back(n.right);
            }
        }
    };

    std::vector<size_t> leftLeaves, rightLeaves;
    if (node.left >= 0) collectLeaves(node.left, leftLeaves);
    if (node.right >= 0) collectLeaves(node.right, rightLeaves);

    // Create boundaries for adjacent leaf pairs.
    // Limit subtree fan-out to avoid O(n^2) explosion on deep BSP nodes.
    if (leftLeaves.size() <= 10 && rightLeaves.size() <= 10) {
        for (size_t a : leftLeaves) {
            for (size_t b : rightLeaves) {
                // Check PVS mutual visibility
                if (a < bsp.regions.size() && b < bsp.regions.size()) {
                    const auto& regA = bsp.regions[a];
                    const auto& regB = bsp.regions[b];
                    bool aSeesB = (b < regA->visibleRegions.size()) && regA->visibleRegions[b];
                    bool bSeesA = (a < regB->visibleRegions.size()) && regB->visibleRegions[a];
                    if (aSeesB && bSeesA) {
                        boundaries.push_back({a, b, node.normalX, node.normalY, node.normalZ, node.splitDistance});
                    }
                }
            }
        }
    }

    // Recurse
    if (node.left >= 0) findRegionBoundariesRecursive(node.left, bsp, ancestorPlanes, boundaries);
    if (node.right >= 0) findRegionBoundariesRecursive(node.right, bsp, ancestorPlanes, boundaries);
}

void detectPortalsFromOccluders(ZoneBenchData& data, const std::string& csvFile) {
    if (!data.portalSystem || !data.portalSystem->hasPortals()) {
        printf("No portal data for detection.\n");
        return;
    }

    auto wldLoader = data.zone->wldLoader;
    auto& portals = data.portalSystem->getData().portals;
    printf("\n=== Occluder-Based Portal Detection ===\n");

    // Use existing portals (region pairs + split planes) as input.
    // Deduplicate region pairs — many BSP portals connect the same pair.
    struct RegionPairInfo {
        size_t regionA, regionB;
        float normalX, normalY, normalZ;
        // Merged portal area for this pair
        float totalPortalArea;
    };

    std::map<std::pair<size_t, size_t>, RegionPairInfo> pairMap;
    for (const auto& p : portals) {
        auto key = std::make_pair(std::min(p.regionA, p.regionB),
                                   std::max(p.regionA, p.regionB));
        auto it = pairMap.find(key);
        if (it == pairMap.end()) {
            pairMap[key] = {key.first, key.second, p.normalX, p.normalY, p.normalZ, p.area};
        } else {
            it->second.totalPortalArea += p.area;
            // Keep the normal from the largest portal for this pair
            if (p.area > it->second.totalPortalArea - p.area) {
                it->second.normalX = p.normalX;
                it->second.normalY = p.normalY;
                it->second.normalZ = p.normalZ;
            }
        }
    }

    std::vector<RegionPairInfo> uniquePairs;
    for (auto& [key, info] : pairMap)
        uniquePairs.push_back(info);

    printf("  %zu portals -> %zu unique region pairs\n", portals.size(), uniquePairs.size());

    // Step 2: For each region pair, rasterize geometry onto the boundary plane
    const float gridCellSize = 2.0f;  // 2 EQ units per pixel
    const int maxGridDim = 64;        // Max 64x64 grid per boundary
    const float nearPlaneThresh = 5.0f;  // Project triangles within 5 units of plane

    struct DetectedPortal {
        size_t regionA, regionB;
        float boundaryArea;    // Total boundary area (sq EQ units)
        float coveredArea;     // Area covered by geometry
        float gapArea;         // Uncovered area (potential portal)
        float coverageRatio;   // covered / total
        float normalX, normalY, normalZ;
        float centerX, centerY, centerZ;
        int gridPixels, coveredPixels;
    };
    std::vector<DetectedPortal> detected;

    int analyzed = 0;
    for (const auto& pair : uniquePairs) {
        // Get region bounding boxes to determine grid extent
        auto itA = data.regions.find(pair.regionA);
        auto itB = data.regions.find(pair.regionB);
        if (itA == data.regions.end() || itB == data.regions.end()) continue;

        // Compute overlap region of the two AABBs (the shared boundary area).
        // Add margin because adjacent BSP regions may touch at edges without overlapping.
        const float margin = 5.0f;
        float overlapMinX = std::max(itA->second.eqMinX, itB->second.eqMinX) - margin;
        float overlapMaxX = std::min(itA->second.eqMaxX, itB->second.eqMaxX) + margin;
        float overlapMinY = std::max(itA->second.eqMinY, itB->second.eqMinY) - margin;
        float overlapMaxY = std::min(itA->second.eqMaxY, itB->second.eqMaxY) + margin;
        float overlapMinZ = std::max(itA->second.eqMinZ, itB->second.eqMinZ) - margin;
        float overlapMaxZ = std::min(itA->second.eqMaxZ, itB->second.eqMaxZ) + margin;

        if (overlapMinX >= overlapMaxX || overlapMinY >= overlapMaxY || overlapMinZ >= overlapMaxZ)
            continue;

        // Build plane basis vectors (same as portal_system.cpp)
        float nx = pair.normalX, ny = pair.normalY, nz = pair.normalZ;
        float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);

        float ux, uy, uz;
        if (ax >= ay && ax >= az) { ux = 0; uy = 1; uz = 0; }
        else if (ay >= ax && ay >= az) { ux = 1; uy = 0; uz = 0; }
        else { ux = 1; uy = 0; uz = 0; }

        // Gram-Schmidt
        float udotn = ux * nx + uy * ny + uz * nz;
        ux -= udotn * nx; uy -= udotn * ny; uz -= udotn * nz;
        float ulen = std::sqrt(ux * ux + uy * uy + uz * uz);
        if (ulen < 1e-8f) continue;
        ux /= ulen; uy /= ulen; uz /= ulen;

        // v = cross(normal, u)
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        // Project overlap AABB corners onto UV plane to get grid extent
        float corners[8][3] = {
            {overlapMinX, overlapMinY, overlapMinZ}, {overlapMaxX, overlapMinY, overlapMinZ},
            {overlapMinX, overlapMaxY, overlapMinZ}, {overlapMaxX, overlapMaxY, overlapMinZ},
            {overlapMinX, overlapMinY, overlapMaxZ}, {overlapMaxX, overlapMinY, overlapMaxZ},
            {overlapMinX, overlapMaxY, overlapMaxZ}, {overlapMaxX, overlapMaxY, overlapMaxZ},
        };
        float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
        for (auto& c : corners) {
            float u = c[0] * ux + c[1] * uy + c[2] * uz;
            float v = c[0] * vx + c[1] * vy + c[2] * vz;
            minU = std::min(minU, u); maxU = std::max(maxU, u);
            minV = std::min(minV, v); maxV = std::max(maxV, v);
        }

        float extentU = maxU - minU, extentV = maxV - minV;
        if (extentU < 1.0f || extentV < 1.0f) continue;

        int gridW = std::min(maxGridDim, std::max(4, static_cast<int>(extentU / gridCellSize)));
        int gridH = std::min(maxGridDim, std::max(4, static_cast<int>(extentV / gridCellSize)));
        float actualCellSize = std::max(extentU / gridW, extentV / gridH);

        PlaneGrid grid;
        // Compute approximate split distance from the plane normal and overlap center
        float overlapCx = (overlapMinX + overlapMaxX) * 0.5f;
        float overlapCy = (overlapMinY + overlapMaxY) * 0.5f;
        float overlapCz = (overlapMinZ + overlapMaxZ) * 0.5f;
        float splitDist = -(nx * overlapCx + ny * overlapCy + nz * overlapCz);

        grid.init(gridW, gridH, actualCellSize, minU, minV,
                  ux, uy, uz, vx, vy, vz, nx, ny, nz, splitDist);

        // Rasterize ALL triangles from both regions that are near the split plane
        for (size_t regionIdx : {pair.regionA, pair.regionB}) {
            auto geom = wldLoader->getGeometryForRegion(regionIdx);
            if (!geom) continue;

            for (const auto& tri : geom->triangles) {
                if (tri.v1 >= geom->vertices.size() ||
                    tri.v2 >= geom->vertices.size() ||
                    tri.v3 >= geom->vertices.size()) continue;

                const auto& va = geom->vertices[tri.v1];
                const auto& vb = geom->vertices[tri.v2];
                const auto& vc = geom->vertices[tri.v3];

                float w0x = geom->centerX + va.x, w0y = geom->centerY + va.y, w0z = geom->centerZ + va.z;
                float w1x = geom->centerX + vb.x, w1y = geom->centerY + vb.y, w1z = geom->centerZ + vb.z;
                float w2x = geom->centerX + vc.x, w2y = geom->centerY + vc.y, w2z = geom->centerZ + vc.z;

                // Check if triangle is near the split plane
                float d0 = grid.planeDotPoint(w0x, w0y, w0z);
                float d1 = grid.planeDotPoint(w1x, w1y, w1z);
                float d2 = grid.planeDotPoint(w2x, w2y, w2z);

                if (std::fabs(d0) > nearPlaneThresh &&
                    std::fabs(d1) > nearPlaneThresh &&
                    std::fabs(d2) > nearPlaneThresh) continue;

                // Project onto grid
                float gu0, gv0, gu1, gv1, gu2, gv2;
                grid.project(w0x, w0y, w0z, gu0, gv0);
                grid.project(w1x, w1y, w1z, gu1, gv1);
                grid.project(w2x, w2y, w2z, gu2, gv2);

                grid.rasterizeTriangle(gu0, gv0, gu1, gv1, gu2, gv2);
            }
        }

        int covered = grid.coveredPixels();
        int total = gridW * gridH;
        float coverageRatio = static_cast<float>(covered) / total;
        float boundaryArea = extentU * extentV;
        float coveredArea = coverageRatio * boundaryArea;
        float gapArea = boundaryArea - coveredArea;

        // Compute boundary center
        float bcx = (overlapMinX + overlapMaxX) * 0.5f;
        float bcy = (overlapMinY + overlapMaxY) * 0.5f;
        float bcz = (overlapMinZ + overlapMaxZ) * 0.5f;

        detected.push_back({pair.regionA, pair.regionB,
                           boundaryArea, coveredArea, gapArea, coverageRatio,
                           nx, ny, nz, bcx, bcy, bcz,
                           total, covered});

        analyzed++;
        if (analyzed % 1000 == 0)
            printf("  Analyzed %d/%zu pairs...\n", analyzed, uniquePairs.size());
    }

    // Distribution summary
    printf("\n  Analyzed %zu boundaries\n", detected.size());

    int highCover = 0, medCover = 0, lowCover = 0, noCover = 0;
    int doorCandidates = 0;

    for (const auto& d : detected) {
        if (d.coverageRatio >= 0.75f) highCover++;
        else if (d.coverageRatio >= 0.50f) medCover++;
        else if (d.coverageRatio >= 0.10f) lowCover++;
        else noCover++;

        // Candidate doorway: high wall coverage with a small gap
        if (d.coverageRatio >= 0.30f && d.gapArea > 10.0f && d.gapArea < 2000.0f) {
            doorCandidates++;
        }
    }

    printf("\n  Coverage distribution:\n");
    printf("    75-100%% covered: %d (mostly wall, small gaps = doorways)\n", highCover);
    printf("    50-74%% covered:  %d (partial wall)\n", medCover);
    printf("    10-49%% covered:  %d (sparse geometry)\n", lowCover);
    printf("    0-9%% covered:    %d (open air)\n", noCover);
    printf("\n  Doorway candidates (30%%+ coverage, 10-2000 sq gap): %d\n", doorCandidates);

    // CSV output
    if (!csvFile.empty()) {
        FILE* f = fopen(csvFile.c_str(), "w");
        if (f) {
            fprintf(f, "regionA,regionB,boundary_area,covered_area,gap_area,"
                       "coverage_ratio,grid_pixels,covered_pixels,"
                       "normal_x,normal_y,normal_z,center_x,center_y,center_z\n");
            for (const auto& d : detected) {
                fprintf(f, "%zu,%zu,%.1f,%.1f,%.1f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n",
                        d.regionA, d.regionB, d.boundaryArea, d.coveredArea, d.gapArea,
                        d.coverageRatio, d.gridPixels, d.coveredPixels,
                        d.normalX, d.normalY, d.normalZ,
                        d.centerX, d.centerY, d.centerZ);
            }
            fclose(f);
            printf("\n  CSV written: %s\n", csvFile.c_str());
        }
    }
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
