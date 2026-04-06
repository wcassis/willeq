// gles2_shader_diagnostic — Shader pipeline diagnostic tool
//
// Renders a pluggable test scenario through all shader debug modes (0-11),
// capturing a PNG screenshot for each mode. Produces a set of diagnostic
// images that can be used to visually debug shader issues.
//
// Usage:
//   gles2_shader_diagnostic --drm --zone qeynos2 --eq-path /path/to/EQ \
//       --eqe1-path /path/to/ETC1 --module rtt-impostor --output diag/
//
// Modules:
//   rtt-impostor    Render entity to FBO, draw billboard + composite side-by-side
//   composite       Render composite entity with all shader modes
//   zone            Render zone geometry with all shader modes

#include "zone_bench_loader.h"
#include "zone_bench_camera.h"
#include "common/logging.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/zone_shader.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

// ============================================================================
// Options
// ============================================================================

struct DiagOptions {
    std::string eqPath;
    std::string eqe1Path;
    std::string zone = "qeynos2";
    std::string module = "composite";  // Default module
    std::string outputDir = "diag";
    int resW = 800, resH = 600;
    int testRace = 1;       // Race to test (default: human)
    int testVariant = 0;    // Texture variant
};

static void printUsage(const char* prog) {
    printf("Usage: %s --drm --zone <name> --eq-path <path> --eqe1-path <path> [options]\n\n", prog);
    printf("Options:\n");
    printf("  --zone <name>        Zone short name (default: qeynos2)\n");
    printf("  --eq-path <path>     Path to EverQuest client directory\n");
    printf("  --eqe1-path <path>   Path to EQE1/ETC1 texture directory\n");
    printf("  --module <name>      Test module: rtt-impostor, composite, zone (default: composite)\n");
    printf("  --output <dir>       Output directory for PNGs (default: diag/)\n");
    printf("  --res <W> <H>        Resolution (default: 800 600)\n");
    printf("  --test-race <N>      Race ID (default: 1 = human)\n");
    printf("  --test-variant <N>   Texture variant (default: 0)\n");
    printf("  --drm                Use DRM/KMS display\n");
    printf("\nShader debug modes captured:\n");
    printf("  0=normal 1=texColor 2=vColor 3=LUT 4=pLight 5=worldNormal\n");
    printf("  6=lightDir 7=alpha 8=tex*vColor 9=UV 10=depth 11=rawTexAlpha\n");
}

static DiagOptions parseArgs(int argc, char** argv) {
    DiagOptions opts;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) opts.eqPath = argv[++i];
        else if (strcmp(argv[i], "--eqe1-path") == 0 && i + 1 < argc) opts.eqe1Path = argv[++i];
        else if (strcmp(argv[i], "--zone") == 0 && i + 1 < argc) opts.zone = argv[++i];
        else if (strcmp(argv[i], "--module") == 0 && i + 1 < argc) opts.module = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) opts.outputDir = argv[++i];
        else if (strcmp(argv[i], "--test-race") == 0 && i + 1 < argc) opts.testRace = atoi(argv[++i]);
        else if (strcmp(argv[i], "--test-variant") == 0 && i + 1 < argc) opts.testVariant = atoi(argv[++i]);
        else if (strcmp(argv[i], "--res") == 0 && i + 2 < argc) {
            opts.resW = atoi(argv[++i]);
            opts.resH = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--drm") == 0) { /* handled by device */ }
        else if (strncmp(argv[i], "--log-", 6) == 0) { /* handled by InitLogging */ }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]); exit(0);
        }
    }
    return opts;
}

// ============================================================================
// Screenshot helper — saves current framebuffer as PNG
// ============================================================================

static bool savePNG(irr::video::IVideoDriver* driver, const std::string& path) {
    irr::video::IImage* img = driver->createScreenShot();
    if (!img) {
        printf("  ERROR: createScreenShot() failed for %s\n", path.c_str());
        return false;
    }
    bool ok = driver->writeImageToFile(img, path.c_str());
    img->drop();
    if (!ok) {
        printf("  ERROR: writeImageToFile() failed for %s\n", path.c_str());
    }
    return ok;
}

// ============================================================================
// Module: composite — Render composite entity with each shader debug mode
// ============================================================================

static void runCompositeModule(ZoneBench::ZoneBenchData& data, const DiagOptions& opts) {
    printf("=== Module: composite (race=%d variant=%d) ===\n", opts.testRace, opts.testVariant);

    // Preload entity
    data.raceModelLoader->preloadModelData(opts.testRace, 0, opts.testVariant);
    data.raceModelLoader->promotePreparedModels();
    data.textureCache->uploadAllPending();

    auto* mesh = data.raceModelLoader->getMeshForRace(opts.testRace, 0);
    data.textureCache->uploadAllPending();
    if (!mesh) {
        printf("  ERROR: No mesh for race %d\n", opts.testRace);
        return;
    }

    // Create animated node and apply idle pose (o01)
    auto* animNode = data.raceModelLoader->createAnimatedNode(
        opts.testRace, 0, data.smgr->getRootSceneNode());
    if (animNode) {
        animNode->playAnimation("o01", true);
        animNode->forceAnimationUpdate();
        data.textureCache->uploadAllPending();
        if (animNode->getInstanceMesh())
            mesh = animNode->getInstanceMesh();
        printf("  Mesh: %d buffers (idle pose)\n", mesh->getMeshBufferCount());
    } else {
        printf("  Mesh: %d buffers (static, no animation)\n", mesh->getMeshBufferCount());
    }

    // Compute bounding box from actual vertices
    irr::core::aabbox3df bbox;
    bool bboxInit = false;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        const irr::video::S3DVertex* verts =
            static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
            if (!bboxInit) { bbox.reset(verts[v].Pos); bboxInit = true; }
            else bbox.addInternalPoint(verts[v].Pos);
        }
    }

    // Mesh data: X=left/right, Y=height, Z=depth
    irr::core::vector3df center = bbox.getCenter();
    irr::core::vector3df extent = bbox.getExtent();
    float halfW = extent.X * 0.55f;
    float halfH = extent.Y * 0.55f;
    if (halfW < 1.0f) halfW = 1.0f;
    if (halfH < 1.0f) halfH = 1.0f;
    printf("  BBox: (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
           bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
           bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);

    irr::core::matrix4 viewMat, projMat;
    projMat.buildProjectionMatrixOrthoLH(halfW * 2.0f, halfH * 2.0f, 0.1f, 500.0f);
    irr::core::vector3df eye = center + irr::core::vector3df(0, 0, -100.0f);
    irr::core::vector3df up(0, 1, 0);
    viewMat.buildCameraLookAtMatrixLH(eye, center, up);

    const char* modeNames[] = {"normal", "texColor", "vColor", "LUT", "pLight",
                                "worldNormal", "lightDir", "alpha", "texVColor",
                                "UV", "depth", "rawTexAlpha"};

    for (int mode = 0; mode <= 11; ++mode) {
        // Set debug mode
        if (data.zoneShader) {
            data.zoneShader->setShaderDebug(mode);
        }

        // Begin scene
        data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));

        data.driver->setTransform(irr::video::ETS_PROJECTION, projMat);
        data.driver->setTransform(irr::video::ETS_VIEW, viewMat);
        irr::core::matrix4 worldMat;
        data.driver->setTransform(irr::video::ETS_WORLD, worldMat);

        // Draw entity mesh
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            auto* buf = mesh->getMeshBuffer(b);
            if (!buf || buf->getVertexCount() == 0) continue;
            data.driver->setMaterial(buf->getMaterial());
            data.driver->drawMeshBuffer(buf);
        }

        data.driver->endScene();

        // Screenshot
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/%s_mode%02d_%s.png",
                 opts.outputDir.c_str(), opts.module.c_str(), mode, modeNames[mode]);
        if (savePNG(data.driver, filename)) {
            printf("  mode %2d %-12s -> %s\n", mode, modeNames[mode], filename);
        }
    }

    // Restore normal mode
    if (data.zoneShader) data.zoneShader->setShaderDebug(0);
}

// ============================================================================
// Module: rtt-impostor — Render entity to FBO, then draw FBO on billboard quad
//                        side-by-side with composite entity for comparison
// ============================================================================

static void runRttImpostorModule(ZoneBench::ZoneBenchData& data, const DiagOptions& opts) {
    printf("=== Module: rtt-impostor (race=%d variant=%d) ===\n", opts.testRace, opts.testVariant);

    // Preload entity
    data.raceModelLoader->preloadModelData(opts.testRace, 0, opts.testVariant);
    data.raceModelLoader->promotePreparedModels();
    data.textureCache->uploadAllPending();

    auto* mesh = data.raceModelLoader->getMeshForRace(opts.testRace, 0);
    data.textureCache->uploadAllPending();
    if (!mesh) {
        printf("  ERROR: No mesh for race %d\n", opts.testRace);
        return;
    }

    // Create animated node and apply idle pose (o01) — T-pose is unnatural
    auto* animNode = data.raceModelLoader->createAnimatedNode(
        opts.testRace, 0, data.smgr->getRootSceneNode());
    if (animNode) {
        animNode->playAnimation("o01", true);
        animNode->forceAnimationUpdate();
        data.textureCache->uploadAllPending();
        if (animNode->getInstanceMesh())
            mesh = animNode->getInstanceMesh();
        printf("  Mesh: %d buffers (idle pose)\n", mesh->getMeshBufferCount());
    } else {
        printf("  Mesh: %d buffers (static, no animation)\n", mesh->getMeshBufferCount());
    }

    // Compute mesh bounding box from actual vertices
    irr::core::aabbox3df bbox;
    bool bboxInit = false;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        auto* buf = mesh->getMeshBuffer(b);
        if (!buf || buf->getVertexCount() == 0) continue;
        const irr::video::S3DVertex* verts =
            static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
            if (!bboxInit) { bbox.reset(verts[v].Pos); bboxInit = true; }
            else bbox.addInternalPoint(verts[v].Pos);
        }
    }
    printf("  BBox: (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) extent=(%.1f,%.1f,%.1f)\n",
           bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
           bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z,
           bbox.getExtent().X, bbox.getExtent().Y, bbox.getExtent().Z);

    // Dump first 10 vertices to understand coordinate space
    {
        auto* buf0 = mesh->getMeshBuffer(0);
        if (buf0 && buf0->getVertexCount() > 0) {
            const irr::video::S3DVertex* verts =
                static_cast<const irr::video::S3DVertex*>(buf0->getVertices());
            int n = std::min(10u, buf0->getVertexCount());
            printf("  First %d vertices (buffer 0):\n", n);
            for (int i = 0; i < n; ++i) {
                printf("    v[%d]: pos=(%.2f, %.2f, %.2f) uv=(%.3f, %.3f)\n",
                       i, verts[i].Pos.X, verts[i].Pos.Y, verts[i].Pos.Z,
                       verts[i].TCoords.X, verts[i].TCoords.Y);
            }
        }
    }

    // Write bbox info to a text file in the output directory for remote debugging
    {
        char infoPath[256];
        snprintf(infoPath, sizeof(infoPath), "%s/bbox_info.txt", opts.outputDir.c_str());
        FILE* f = fopen(infoPath, "w");
        if (f) {
            fprintf(f, "Race: %d, Variant: %d\n", opts.testRace, opts.testVariant);
            fprintf(f, "Mesh buffers: %d\n", mesh->getMeshBufferCount());
            fprintf(f, "BBox min: (%.3f, %.3f, %.3f)\n", bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z);
            fprintf(f, "BBox max: (%.3f, %.3f, %.3f)\n", bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
            fprintf(f, "BBox extent: (%.3f, %.3f, %.3f)\n", bbox.getExtent().X, bbox.getExtent().Y, bbox.getExtent().Z);
            auto c = bbox.getCenter();
            fprintf(f, "BBox center: (%.3f, %.3f, %.3f)\n", c.X, c.Y, c.Z);

            auto* buf0 = mesh->getMeshBuffer(0);
            if (buf0) {
                const irr::video::S3DVertex* verts =
                    static_cast<const irr::video::S3DVertex*>(buf0->getVertices());
                int n = std::min(20u, buf0->getVertexCount());
                fprintf(f, "\nFirst %d vertices (buffer 0, %d total):\n", n, buf0->getVertexCount());
                for (int i = 0; i < n; ++i) {
                    fprintf(f, "  v[%d]: pos=(%.3f, %.3f, %.3f) n=(%.3f, %.3f, %.3f) uv=(%.4f, %.4f)\n",
                            i, verts[i].Pos.X, verts[i].Pos.Y, verts[i].Pos.Z,
                            verts[i].Normal.X, verts[i].Normal.Y, verts[i].Normal.Z,
                            verts[i].TCoords.X, verts[i].TCoords.Y);
                }
            }
            fclose(f);
            printf("  Wrote %s\n", infoPath);
        }
    }

    // ── Dump the composite atlas texture via RTT blit + glReadPixels ──
    {
        irr::video::ITexture* atlasTex = nullptr;
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            auto* buf = mesh->getMeshBuffer(b);
            if (buf && buf->getMaterial().getTexture(0)) {
                atlasTex = buf->getMaterial().getTexture(0);
                break;
            }
        }
        if (atlasTex) {
            irr::core::dimension2d<irr::u32> texSize = atlasTex->getOriginalSize();
            printf("  Atlas texture: %dx%d, dumping via RTT blit...\n", texSize.Width, texSize.Height);

            // Create RGBA RTT at atlas dimensions
            irr::video::ITexture* blitRtt = data.driver->addRenderTargetTexture(
                texSize, "atlas_blit_rtt");
            if (blitRtt) {
                // Save state
                irr::core::matrix4 sv = data.driver->getTransform(irr::video::ETS_VIEW);
                irr::core::matrix4 sp = data.driver->getTransform(irr::video::ETS_PROJECTION);

                // Blit atlas texture into RTT using 2D path (UI2D shader)
                data.driver->setRenderTarget(blitRtt, true, true,
                    irr::video::SColor(255, 0, 0, 0));

                irr::core::rect<irr::s32> srcRect(0, 0, texSize.Width, texSize.Height);
                irr::core::rect<irr::s32> destRect(0, 0, texSize.Width, texSize.Height);
                data.driver->draw2DImage(atlasTex, destRect, srcRect,
                    nullptr, nullptr, false);

                // Read back via screenshot (reads from current FBO)
                irr::video::IImage* img = data.driver->createScreenShot();

                // Restore
                data.driver->setRenderTarget(nullptr, false, false);
                data.driver->setTransform(irr::video::ETS_VIEW, sv);
                data.driver->setTransform(irr::video::ETS_PROJECTION, sp);

                if (img) {
                    char filename[256];
                    snprintf(filename, sizeof(filename), "%s/atlas_texture.png",
                             opts.outputDir.c_str());
                    if (data.driver->writeImageToFile(img, filename)) {
                        printf("  Atlas texture: %dx%d -> %s\n",
                               texSize.Width, texSize.Height, filename);
                    } else {
                        printf("  ERROR: writeImageToFile failed for atlas\n");
                    }
                    img->drop();
                } else {
                    printf("  ERROR: createScreenShot failed for atlas RTT\n");
                }
            } else {
                printf("  ERROR: Failed to create RTT for atlas blit\n");
            }
        } else {
            printf("  WARNING: No texture found on mesh buffers\n");
        }
    }

    // ── Step 1: Render entity to FBO ──
    int rttSize = 512;
    irr::core::dimension2d<irr::u32> rttDim(rttSize, rttSize);
    irr::video::ITexture* rtt = data.driver->addRenderTargetTexture(rttDim, "diag_rtt");
    if (!rtt) {
        printf("  ERROR: Failed to create RTT\n");
        return;
    }

    // Save state
    irr::core::matrix4 savedView = data.driver->getTransform(irr::video::ETS_VIEW);
    irr::core::matrix4 savedProj = data.driver->getTransform(irr::video::ETS_PROJECTION);

    // Render to FBO with different material modes to diagnose
    struct RttTest {
        const char* name;
        irr::video::E_MATERIAL_TYPE matType;
        bool fogEnable;
        bool lighting;
    };
    RttTest rttTests[] = {
        {"solid_nofog",    irr::video::EMT_SOLID, false, false},
        {"solid_fog",      irr::video::EMT_SOLID, true, false},
        {"solid_lit",      irr::video::EMT_SOLID, false, true},
        {"original",       irr::video::EMT_SOLID, false, false},  // placeholder, uses original mat
    };

    // Mesh vertex data: X=left/right (~1.5), Y=height (~6.7), Z=depth (~6.0)
    // Front view: camera along -Z looking toward +Z, Y is up
    irr::core::vector3df center = bbox.getCenter();
    irr::core::vector3df extent = bbox.getExtent();
    // Ortho framing: width = Z extent, height = Y extent (vertical), 10% padding
    float halfW = extent.Z * 0.55f;
    float halfH = extent.Y * 0.55f;
    if (halfW < 1.0f) halfW = 1.0f;
    if (halfH < 1.0f) halfH = 1.0f;
    printf("  Camera: center=(%.1f,%.1f,%.1f) halfW=%.1f halfH=%.1f\n",
           center.X, center.Y, center.Z, halfW, halfH);

    for (int t = 0; t < 4; ++t) {
        data.driver->setRenderTarget(rtt, true, true, irr::video::SColor(0, 0, 0, 0));

        // Ortho projection tightly framing the entity with 10% padding
        irr::core::matrix4 orthoProj;
        orthoProj.buildProjectionMatrixOrthoLH(halfW * 2.0f, halfH * 2.0f, 0.1f, 500.0f);
        data.driver->setTransform(irr::video::ETS_PROJECTION, orthoProj);

        // Camera along +X looking toward -X (front of entity), Y is up (inverted)
        irr::core::vector3df eye = center + irr::core::vector3df(100.0f, 0, 0);
        irr::core::vector3df up(0, -1, 0);
        irr::core::matrix4 viewMat;
        viewMat.buildCameraLookAtMatrixLH(eye, center, up);
        data.driver->setTransform(irr::video::ETS_VIEW, viewMat);

        irr::core::matrix4 worldMat;
        data.driver->setTransform(irr::video::ETS_WORLD, worldMat);

        // Draw entity
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            auto* buf = mesh->getMeshBuffer(b);
            if (!buf || buf->getVertexCount() == 0) continue;
            irr::video::SMaterial mat = buf->getMaterial();
            if (t < 3) {
                // Override material for diagnostic
                mat.MaterialType = rttTests[t].matType;
                mat.FogEnable = rttTests[t].fogEnable;
                mat.Lighting = rttTests[t].lighting;
                mat.BackfaceCulling = false;
            }
            // else t==3: use original material as-is
            data.driver->setMaterial(mat);
            data.driver->drawMeshBuffer(buf);
        }

        // Restore and screenshot the FBO content
        data.driver->setRenderTarget(nullptr, false, false);
        data.driver->setTransform(irr::video::ETS_VIEW, savedView);
        data.driver->setTransform(irr::video::ETS_PROJECTION, savedProj);

        // Now render the RTT texture on a fullscreen quad to inspect it
        data.driver->beginScene(true, true, irr::video::SColor(255, 80, 0, 80));

        // Draw the RTT scaled up to fill most of the screen
        int displaySize = std::min(opts.resW, opts.resH) - 40;  // 20px margin
        int dx = (opts.resW - displaySize) / 2;
        int dy = (opts.resH - displaySize) / 2;
        irr::core::rect<irr::s32> destRect(dx, dy, dx + displaySize, dy + displaySize);
        irr::core::rect<irr::s32> srcRect(0, 0, rttSize, rttSize);
        data.driver->draw2DImage(rtt, destRect, srcRect,
            nullptr, nullptr, true);

        data.driver->endScene();

        char filename[256];
        snprintf(filename, sizeof(filename), "%s/rtt_%s.png",
                 opts.outputDir.c_str(), rttTests[t].name);
        if (savePNG(data.driver, filename)) {
            printf("  RTT %-15s -> %s\n", rttTests[t].name, filename);
        }
    }

    // ── Step 2: Side-by-side — composite entity (left) + billboard quad (right) ──
    // Camera along +X, Y up (inverted). Width = Z, height = Y.
    float separation = extent.Z * 2.0f;
    if (separation < 5.0f) separation = 5.0f;
    irr::core::matrix4 sideProj;
    float sideHalfW = (separation + extent.Z) * 0.6f;
    float sideHalfH = extent.Y * 0.6f;
    sideProj.buildProjectionMatrixOrthoLH(sideHalfW * 2.0f, sideHalfH * 2.0f, 0.1f, 500.0f);
    irr::core::vector3df sideEye = center + irr::core::vector3df(100.0f, 0, 0);
    irr::core::vector3df sideUp(0, -1, 0);
    irr::core::matrix4 sideView;
    sideView.buildCameraLookAtMatrixLH(sideEye, center, sideUp);

    data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
    data.driver->setTransform(irr::video::ETS_PROJECTION, sideProj);
    data.driver->setTransform(irr::video::ETS_VIEW, sideView);

    // Left: composite entity offset in -X (EQ east/west)
    {
        irr::core::matrix4 world;
        world.setTranslation(irr::core::vector3df(0, 0, -separation * 0.5f));
        data.driver->setTransform(irr::video::ETS_WORLD, world);
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            auto* buf = mesh->getMeshBuffer(b);
            if (!buf || buf->getVertexCount() == 0) continue;
            irr::video::SMaterial mat = buf->getMaterial();
            mat.FogEnable = false;
            mat.Lighting = false;
            mat.BackfaceCulling = false;
            data.driver->setMaterial(mat);
            data.driver->drawMeshBuffer(buf);
        }
    }

    // Right: billboard quad at +X offset using RTT texture
    // In EQ space: quad spans X (width) and Z (height), facing -Y (toward camera)
    {
        irr::core::matrix4 world;
        data.driver->setTransform(irr::video::ETS_WORLD, world);

        float bbHW = extent.Z * 0.5f;
        float bbH = extent.Y;
        float pz = separation * 0.5f;
        float baseY = bbox.MinEdge.Y;  // Feet level
        irr::video::SColor white(255, 255, 255, 255);
        // Quad: Z=width, Y=height, X=0 (facing +X toward camera)
        irr::video::S3DVertex quad[4] = {
            {0, baseY,       pz - bbHW, 1, 0, 0, white, 0.0f, 1.0f},
            {0, baseY,       pz + bbHW, 1, 0, 0, white, 1.0f, 1.0f},
            {0, baseY + bbH, pz + bbHW, 1, 0, 0, white, 1.0f, 0.0f},
            {0, baseY + bbH, pz - bbHW, 1, 0, 0, white, 0.0f, 0.0f},
        };
        irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};

        irr::video::SMaterial bbMat;
        bbMat.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
        bbMat.MaterialTypeParam = 0.1f;
        bbMat.Lighting = false;
        bbMat.FogEnable = false;
        bbMat.BackfaceCulling = false;
        bbMat.setTexture(0, rtt);
        data.driver->setMaterial(bbMat);
        data.driver->drawVertexPrimitiveList(quad, 4, idx, 2,
            irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);
    }

    data.driver->endScene();

    char filename[256];
    snprintf(filename, sizeof(filename), "%s/rtt_sidebyside.png", opts.outputDir.c_str());
    if (savePNG(data.driver, filename)) {
        printf("  Side-by-side         -> %s\n", filename);
    }

    printf("  Done. %d files written to %s/\n", 5, opts.outputDir.c_str());
}

// ============================================================================
// Module: zone — Render zone geometry with each shader debug mode
// ============================================================================

static void runZoneModule(ZoneBench::ZoneBenchData& data, const DiagOptions& opts) {
    printf("=== Module: zone ===\n");

    // Use a fixed camera position
    ZoneBench::CameraState cam;
    cam.eqX = 0; cam.eqY = 0; cam.eqZ = 50;
    cam.dirX = 1; cam.dirY = 0; cam.dirZ = 0;
    cam.fovRadV = 45.0f * 3.14159f / 180.0f;
    cam.aspect = static_cast<float>(opts.resW) / opts.resH;

    // Collect zone draw entries
    std::vector<ZoneBench::DrawEntry> drawEntries;
    int regVis = 0, regDrawn = 0, regOccluded = 0;
    ZoneBench::collectDrawEntries(data, cam, true, true, true, false, true,
                                   500, 100000, drawEntries, regVis, regDrawn, regOccluded);
    printf("  Zone: %zu draw entries, %d regions visible\n", drawEntries.size(), regVis);

    const char* modeNames[] = {"normal", "texColor", "vColor", "LUT", "pLight",
                                "worldNormal", "lightDir", "alpha", "texVColor",
                                "UV", "depth", "rawTexAlpha"};

    for (int mode = 0; mode <= 11; ++mode) {
        if (data.zoneShader) data.zoneShader->setShaderDebug(mode);

        data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
        data.smgr->drawAll();
        data.driver->endScene();

        char filename[256];
        snprintf(filename, sizeof(filename), "%s/%s_mode%02d_%s.png",
                 opts.outputDir.c_str(), opts.module.c_str(), mode, modeNames[mode]);
        if (savePNG(data.driver, filename)) {
            printf("  mode %2d %-12s -> %s\n", mode, modeNames[mode], filename);
        }
    }

    if (data.zoneShader) data.zoneShader->setShaderDebug(0);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    InitLogging(argc, argv);
    DiagOptions opts = parseArgs(argc, argv);

    if (opts.eqPath.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    printf("Shader Diagnostic Tool\n");
    printf("  Module: %s\n", opts.module.c_str());
    printf("  Zone: %s\n", opts.zone.c_str());
    printf("  Output: %s/\n", opts.outputDir.c_str());
    printf("  Resolution: %dx%d\n", opts.resW, opts.resH);

    // Create output directory
    mkdir(opts.outputDir.c_str(), 0755);

    // Load zone
    ZoneBench::BenchConfig config;
    config.eqPath = opts.eqPath;
    config.eqe1Path = opts.eqe1Path;
    config.zoneName = opts.zone;
    config.width = opts.resW;
    config.height = opts.resH;
    config.compositeEntities = true;
    config.entityCount = 1;  // Need at least 1 to trigger raceModelLoader creation

    auto data = ZoneBench::loadZone(config);
    if (!data) {
        fprintf(stderr, "Failed to load zone %s\n", opts.zone.c_str());
        return 1;
    }

    // Run selected module
    if (opts.module == "rtt-impostor") {
        runRttImpostorModule(*data, opts);
    } else if (opts.module == "composite") {
        runCompositeModule(*data, opts);
    } else if (opts.module == "zone") {
        runZoneModule(*data, opts);
    } else {
        fprintf(stderr, "Unknown module: %s\n", opts.module.c_str());
        return 1;
    }

    printf("\nDone.\n");
    return 0;
}
