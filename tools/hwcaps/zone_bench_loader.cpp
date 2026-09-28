#include "zone_bench_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <mutex>
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
#include "client/graphics/region_occluders.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/load_mem_audit.h"
#include "client/core/memory_ledger.h"

#ifdef EQT_HAS_GLES2
extern bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer);
#endif

// T284: repo-root fallback for the EQE1 name->pack index, set by CMake for
// every target that compiles this TU (same shape as WILLEQ_DB_REAL_PATH,
// T270). Defined defensively so a new target that forgets it still builds.
#ifndef WILLEQ_EQE1_INDEX_REAL_PATH
#define WILLEQ_EQE1_INDEX_REAL_PATH "data/eqe1_texture_index.json"
#endif

using namespace EQT::Graphics;

namespace ZoneBench {

// P46 Part B (P46b fix): benchmark-only mirror of EntityRenderer's paired
// solid/alpha-test shader promotion (src/client/graphics/entity_renderer.cpp,
// e.g. the loop at ~line 629-648: "if (mat.MaterialType == EMT_SOLID) ...
// else if (mat.MaterialType == EMT_TRANSPARENT_ALPHA_CHANNEL_REF)
// applyAlphaTestMaterialType(mat); mat.Lighting = false;"). This tool does not
// link entity_renderer.cpp (P44b's root-caused finding: `nm` shows no
// applyAlphaTestMaterialType symbol in gles2_zone_benchmark), so
// mesh_building.cpp's P37 EMT_TRANSPARENT_ALPHA_CHANNEL_REF marker on masked
// mesh buffers was never followed up with production's companion-alpha-
// texture binding.
//
// P46b root cause (Will's round-3 rejection: wrong-looking textures on
// skeleton forearms/shins, glowing zombie chest): the ORIGINAL P46 Part B
// port (commit d19e325) implemented ONLY the alpha-test half of this pair --
// it promoted masked buffers to the custom per-pixel-lit GLSL alpha-test
// shader (materialAlphaTest_/materialAlphaTestRGBA_, with real player-light/
// sun/fog math) but left sibling EMT_SOLID buffers on the SAME entity
// untouched, still rendered by Irrlicht's built-in fixed-function EMT_SOLID
// renderer (flat vertex-color modulate, no player light/fog/tint). Every
// masked-classified body part (skeleton forearms/shins, zombie chest/legs --
// exactly P37's ground-truth masked list) rendered through a completely
// different lighting model than its unmasked neighbors on the same mesh,
// independent of any real per-pixel alpha value -- confirmed by the P46b
// bisect matrix: AE vs pre-P37 baseline is BYTE-IDENTICAL whether P46 Part A
// (the decodeBMP alpha fix) is present or reverted, in BOTH ETC1 and RGBA
// modes, proving the visual delta was never about alpha data at all. Fixed
// by promoting ALL of the entity's material buffers together, exactly as
// entity_renderer.cpp does at its 3 call sites: EMT_SOLID buffers get
// shaderMaterialSolid (the same custom lighting shader masked buffers use),
// masked buffers get the alpha-test variant, and Lighting is disabled on
// every promoted buffer (the shader computes lighting itself). This restores
// per-entity shader consistency; production is unaffected (entity_renderer.cpp
// already paired these two promotions from the start, at P21 -- this bug
// existed only in this tool's P46 Part A/B benchmark-only port).
static void applyEntityShaderMaterialPromotion(irr::scene::IMesh* mesh,
                                                ConstrainedTextureCache* textureCache,
                                                irr::s32 shaderMaterialSolid,
                                                irr::s32 shaderMaterialAlphaTest,
                                                irr::s32 shaderMaterialAlphaTestRGBA) {
    if (!mesh) return;

    irr::u32 bufCount = mesh->getMeshBufferCount();
    for (irr::u32 i = 0; i < bufCount; ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf) continue;

        irr::video::SMaterial& mat = buf->getMaterial();

        if (shaderMaterialSolid < 0) {
            // No custom shader available at all -- leave every buffer on
            // fixed-function (matches entity_renderer.cpp's else branch,
            // which also leaves MaterialType untouched in this case).
            continue;
        }

        if (mat.MaterialType == irr::video::EMT_SOLID) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid);
        } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
            // mirrors EntityRenderer::applyAlphaTestMaterialType() -- only
            // fires for buffers mesh_building.cpp already flagged masked (P37).
            if (!textureCache) continue;
            irr::video::ITexture* tex = mat.getTexture(0);
            bool isETC1 = tex && textureCache->isETC1Texture(tex);
            if (isETC1) {
                if (shaderMaterialAlphaTest >= 0) {
                    mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest);
                }
                irr::video::ITexture* alphaTex = textureCache->getAlphaTexture(tex);
                if (alphaTex) {
                    mat.setTexture(1, alphaTex);
                }
            } else if (shaderMaterialAlphaTestRGBA >= 0) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA);
            } else if (shaderMaterialAlphaTest >= 0) {
                // No RGBA variant wired -- fall back to the companion variant,
                // same fallback entity_renderer.cpp's version uses.
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest);
            }
        }
        mat.Lighting = false;  // Shader handles lighting -- mirrors entity_renderer.cpp
    }
}

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
    // T491: the benchmark's lifecycle points mirror the client's
    // (renderer_zone_load.cpp loadZoneSequential) -- same [memledger] line,
    // same schema -- so scripts/ci/memory_cap_hunt.sh and the gate read both.
    eqt::memory::setCurrentZone(config.zoneName);
    // The owner rows (WILLEQ_LEDGER_OWNERS=<category>) are only printed on a
    // diagnostic run: recordPoint() arms them, and the gate never sets the
    // variable, so its invocation is unchanged.
    const bool ownerRows = std::getenv("WILLEQ_LEDGER_OWNERS") != nullptr;
    auto ledgerPoint = [&](const char* point) {
        printf("%s\n", eqt::memory::toLogLine(eqt::memory::snapshot(point)).c_str());
        if (ownerRows) {
            eqt::memory::recordPoint(point);
            printf("%s\n", eqt::memory::startupToLogLines(point).c_str());
        }
        fflush(stdout);
    };
    auto mappingPoint = [&](const char* point) {
        // T491 addendum (B801): the startup baseline is the whole gap to the
        // 64 MB target, and most of it never goes through malloc. Census it
        // by mapping, at the two points that bracket a zone load.
        printf("%s\n",
               eqt::memory::mappingsToLogLines(point, eqt::memory::readMappings(), 16).c_str());
        fflush(stdout);
    };
    ledgerPoint("startup");
    mappingPoint("bench_startup");

    // ── Step 1: Create Irrlicht device ──
    {
        eqt::memory::MemoryScope devScope(eqt::memory::Category::Startup);  // T491
        data->device = createDevice(config);
    }
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

    eqt::memory::MemoryScope parseScope(eqt::memory::Category::ZoneParse);  // T491
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
    ledgerPoint("zone_parse");

    // ── Step 3: Set up constrained renderer config (OrangePi preset) ──
    eqt::memory::MemoryScope texScope(eqt::memory::Category::TexturesDecoded);  // T491
    auto constrainedConfig = ConstrainedRendererConfig::fromPreset(
        ConstrainedRenderingPreset::OrangePi);
    constrainedConfig.eqe1Path = config.eqe1Path;

    // ── Step 4: Set up texture cache ──
    data->textureCache = std::make_unique<ConstrainedTextureCache>(constrainedConfig);
    data->textureCache->setDriver(data->driver);

    // ── Step 4b: Load EQE1 flat packs (zone-specific only) ──
    if (!config.eqe1Path.empty()) {
        eqt::memory::MemoryScope arcScope(eqt::memory::Category::Archives);  // T491
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
    eqt::memory::MemoryScope sceneScope(eqt::memory::Category::IrrlichtScene);  // T491
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
    eqt::memory::MemoryScope meshScope(eqt::memory::Category::ZoneMesh);  // T491
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
    ledgerPoint("mesh_build_textures");

    // ── Step 7b: Load and build zone objects ──
    if (config.loadObjects) {
        eqt::memory::MemoryScope objScope(eqt::memory::Category::Objects);  // T491
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
    eqt::memory::MemoryScope portalScope(eqt::memory::Category::Spatial);  // T491
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

    ledgerPoint("objects");
    // ── Step 9: Set up frustum culler ──
    eqt::memory::MemoryScope spatialScope(eqt::memory::Category::Spatial);  // T491
    // Owner labels for WILLEQ_LEDGER_OWNERS=spatial (no effect otherwise --
    // the rows are only charged for the selected category).
    eqt::memory::StartupScope spatialOwner("sp.frustum");
    auto spatialOwnerLabel = [](const char* name) {
        if (eqt::memory::enabled()) {
            eqt::memory::detail::t_sublabel = eqt::memory::startupSublabelIndex(name);
        }
    };
    data->frustumCuller = std::make_unique<FrustumCuller>();
    data->frustumCuller->setEnabled(true);

    // ── Step 10: Extract occluder triangles for software occlusion culling ──
    {
        spatialOwnerLabel("sp.occ_culler");
        OcclusionCullerConfig occConfig;
        occConfig.width = 64;
        occConfig.height = 32;
        // P42 (P39 finding): the default used to be a hardcoded 16 that never
        // consulted the ConstrainedRendererConfig this loader already builds
        // (step 3, constrainedConfig — fromPreset(OrangePi), P14-blessed 256).
        // That mismatch is why P38's "shipped orangepi preset" Pi measurements
        // landed at ~35.3ms p95 (effectively budget-16 behavior) despite 256
        // being the value the real client ships. Explicit --occluder-budget
        // still wins; the default now resolves from the config the loader
        // already has in scope instead of drifting from it silently.
        occConfig.maxOccluderRegions = (config.occluderBudget > 0)
            ? config.occluderBudget
            : constrainedConfig.occlusionMaxOccluderRegions;
        LOG_INFO(MOD_GRAPHICS, "Occluder budget: {} ({})", occConfig.maxOccluderRegions,
                 (config.occluderBudget > 0) ? "explicit --occluder-budget"
                                              : "resolved from ConstrainedRendererConfig");
        occConfig.minOccluderArea = 20.0f;
        occConfig.maxTrianglesPerRegion = 12;
        data->occlusionCuller = std::make_unique<SoftwareOcclusionCuller>(occConfig);
        spatialOwnerLabel("sp.occluder_sets");

        int totalOccluders = 0;
        int regionsWithOccluders = 0;

        for (size_t i = 0; i < totalRegions; ++i) {
            auto geom = wldLoader->getGeometryForRegion(i);
            if (!geom || geom->vertices.empty() || geom->triangles.empty()) continue;

            // Extract large triangles as occluders -- the client's own
            // selection (region_occluders.h), so the gate measures what ships.
            const std::vector<OccluderTriangle> occluders = SelectRegionOccluders(
                *geom, occConfig.minOccluderArea, occConfig.maxTrianglesPerRegion);
            if (!occluders.empty()) {
                totalOccluders += occluders.size();
                regionsWithOccluders++;
                data->occlusionCuller->setRegionOccluders(i, occluders);
            }
        }
        data->occlusionCuller->finishOccluders();

        printf("  Occlusion culler: %d occluder triangles from %d regions (64x32 depth buffer)\n",
               totalOccluders, regionsWithOccluders);
    }

    ledgerPoint("spatial_bsp");
    // ── Step 11: Load entity models ──
    if (config.entityCount > 0) {
        eqt::memory::MemoryScope entScope(eqt::memory::Category::EntityModels);  // T491
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
        //
        // T284: this used to SCAN config.eqe1Path and open every *_hs_def_* /
        // *_actordef_* pack in it -- 498 packs on the shipped ETC1 tree
        // (production's own scan, in loadZoneStep0_EQE1, was unfiltered and
        // opened all 1506 / 4.44 GiB). Now a sheet name resolves through the
        // shipped name->pack index to exactly ONE pack, opened on demand by
        // the resolver below, which is the same rule production uses
        // (IrrlichtRenderer::openArchiveContaining / EQE1TextureIndex::resolve
        // -- T207's index lookup, T266's rule-R2 late-open). Keeping the
        // benchmark on the same rule is the point: it is the only x86 harness
        // that drives the composite path without a server, so a fork here
        // would measure something production does not do.
        if (config.compositeEntities && !config.eqe1Path.empty()) {
            data->raceModelLoader->setCompositeEntityPath(config.eqe1Path);

            data->eqe1Index = std::make_unique<EQE1TextureIndex>();
            const char* indexEnv = std::getenv("WILLEQ_EQE1_INDEX");
            std::string indexPath = indexEnv && *indexEnv ? indexEnv
                                                          : "data/eqe1_texture_index.json";
            if (!data->eqe1Index->load(indexPath)) {
                // Same fallback shape as WILLEQ_DB_REAL_PATH (T270): a run
                // whose CWD is not the repo root still finds the shipped file.
                if (!data->eqe1Index->load(WILLEQ_EQE1_INDEX_REAL_PATH)) {
                    printf("  Composite EQE1: WARNING -- no name index at '%s' or '%s'; "
                           "composite sheets will not resolve\n",
                           indexPath.c_str(), WILLEQ_EQE1_INDEX_REAL_PATH);
                }
            }

            if (data->eqe1Index->isLoaded()) {
                auto* raw = data.get();
                std::string eqe1Dir = config.eqe1Path;
                data->textureCache->setEQE1ArchiveResolver(
                    [raw, eqe1Dir](const std::string& name) -> const EQE1Loader* {
                        static std::mutex resolverMutex;
                        std::lock_guard<std::mutex> lock(resolverMutex);
                        EQE1TextureIndex::Resolution r;
                        // No zone candidate set in the benchmark -- every
                        // resolve is rule R2 (first owner in index order),
                        // which is the branch a composite sheet takes in
                        // production too (a sheet pack is never a candidate).
                        if (!raw->eqe1Index->resolve(name, {}, r)) return nullptr;
                        for (const auto& l : raw->eqe1Loaders) {
                            if (l && l->getFilePath() == eqe1Dir + "/" + r.archive + ".eqe1")
                                return l.get();
                        }
                        auto loader = std::make_unique<EQE1Loader>();
                        if (!loader->open(eqe1Dir + "/" + r.archive + ".eqe1")) return nullptr;
                        const EQE1Loader* ptr = loader.get();
                        raw->eqe1ResolverOpens += 1;
                        raw->eqe1ResolverBytes += ptr->getMappedBytes();
                        raw->textureCache->addEQE1Loader(ptr);
                        raw->eqe1Loaders.push_back(std::move(loader));
                        return ptr;
                    });
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
        //
        // P46d: production's EntityPrepWorker (entity_prep_worker.cpp) and
        // renderer_zone_load.cpp call RaceModelLoader::preloadModelData()
        // UNCONDITIONALLY for every race the first time it's needed --
        // NEVER gated behind compositeEntities/eqe1Path the way this
        // benchmark's block below is. preloadModelData() is what runs T05
        // (race_model_loader.cpp's per-part decode loop), the ONLY call site
        // that ever passes applyPaletteMask=true for a race's base body
        // textures -- mesh_building.cpp's buildMeshFromGeometry() itself
        // never does (see this unit's Review). Without calling
        // preloadModelData() first, --animate's createAnimatedNode() falls
        // through to buildMeshFromGeometry()'s unmasked loadTextureFromBMP()
        // fallback for EVERY leg (RGBA and ETC1 alike) -- this is why P46c's
        // RGBA-mode discard-fire check found AE=0 even before any cache-key
        // bug was in play: this benchmark's own composite-only gate meant
        // T05 never ran at all for a plain (non---composite-entities)
        // invocation, so the masked decode was never even attempted, let
        // alone poisoned. The unconditional call below (run whenever
        // --animate is set and the composite-specific block above didn't
        // already cover it) closes that instrument gap so this benchmark
        // actually mirrors production's real per-race load order for BOTH
        // legs. Benchmark-only change; production preloadModelData()/T05
        // itself is untouched.
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

            // T284 metric: how many packs the index-driven open actually
            // mapped for this run's composites, against the directory scan it
            // replaced. Machine-readable so tests/measurements can parse it.
            printf("  EQE1-COMPOSITE: %zu pack(s) opened on demand, %zu byte(s) mapped\n",
                   data->eqe1ResolverOpens, data->eqe1ResolverBytes);
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

        // P46d: production-accurate preload for --animate, run AFTER the
        // static getMeshForRace() preload loop above (see the long comment
        // near the composite-entities block for why this ordering matters
        // and why it's benchmark-only). getMeshForRace()'s STATIC mesh build
        // (ZoneMeshBuilder::buildTexturedMesh) unconditionally decodes every
        // per-part texture UNMASKED (zone_geometry.cpp never threads a mask
        // flag through that path, by original P46 design -- masked ZONE
        // textures must never change on it) and registers each into
        // ZoneMeshBuilder's OWN name-keyed fast-path cache
        // (registerUploadedTexture(), zone_geometry.h's textureCache_ --
        // separate from ConstrainedTextureCache's own (name,
        // applyPaletteMask) keying added by this unit). That registration is
        // last-write-wins by plain name. Running the masked T05 preload
        // AFTER this loop means its registration is the one
        // buildMeshFromGeometry()'s getOrLoadTexture() fast-path finds when
        // --animate's createAnimatedNode() runs next -- mirroring
        // production, where EntityPrepWorker's preloadModelData() call
        // always precedes any per-entity mesh build and is never raced by a
        // benchmark-only static preload like the loop just above. Skipped
        // when the composite-entities branch above already ran
        // preloadModelData() for these races (compositeAllowed path takes a
        // different, already-correct route: T05 is explicitly DEFERRED on a
        // composite hit, see race_model_loader.cpp's P13b comment).
        if (config.animate && !(config.compositeEntities && !config.eqe1Path.empty())) {
            for (int r = 0; r < numRaces; ++r) {
                data->raceModelLoader->preloadModelData(raceIds[r], 0);
            }
            data->raceModelLoader->promotePreparedModels();
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
                        // P46 Part B (P46b fix): mirror production's PAIRED solid +
                        // companion-alpha shader promotion (entity_renderer.cpp's
                        // loop at ~line 629-648, not linked into this tool) onto
                        // ALL of this node's material buffers -- not just the
                        // masked-material ones mesh_building.cpp flagged (P37).
                        // P46b root-caused a wrong-texture-looking regression to
                        // exactly this being alpha-only: masked buffers got the
                        // custom per-pixel-lit shader while sibling EMT_SOLID
                        // buffers stayed on fixed-function lighting, producing a
                        // visible cross-body-part lighting mismatch independent of
                        // any real alpha value. Must happen after createAnimatedNode()
                        // builds instanceMesh_'s buffers, before any texture
                        // upload/draw uses this node's material.
                        // P46c: --no-entity-shader-promotion skips this entirely,
                        // leaving entity buffers on the driver's built-in
                        // materials (EMT_SOLID / P37's EMT_TRANSPARENT_ALPHA_CHANNEL_REF)
                        // -- used to capture a lighting-neutral bless pair that
                        // isolates the alpha cutout from P46b's promoted-lighting
                        // delta. Default (flag absent) is unchanged: promotion on.
                        if (!config.noEntityShaderPromotion &&
                            data->zoneShader && data->zoneShader->isAvailable() &&
                            node->getInstanceMesh()) {
                            applyEntityShaderMaterialPromotion(
                                node->getInstanceMesh(), data->textureCache.get(),
                                data->zoneShader->getActiveSolid(),
                                data->zoneShader->getActiveAlphaTest(),
                                data->zoneShader->getActiveAlphaTestRGBA());
                        }
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
    ledgerPoint("entities");
    mappingPoint("bench_entities");

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

// P08: computeOcclusionHeadroomUncapped() lives in zone_bench_histogram.cpp/.h (file-size budget).

} // namespace ZoneBench
