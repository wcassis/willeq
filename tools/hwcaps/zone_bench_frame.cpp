// zone_bench_frame.cpp — gles2_zone_benchmark's per-frame machinery.
//
// T387: extracted verbatim from tools/hwcaps/gles2_zone_benchmark.cpp (3,060
// lines, over the 1,500 ceiling; board B167). Camera-path record/replay
// (--path-file, --dump-path), scene setup (lighting, camera), draw
// submission, the frame statistics the pose search scores on, and the
// synthetic draw-call overhead test. Declarations in zone_bench_frame.h;
// `static` dropped where a definition now has an external caller.

#include "zone_bench_frame.h"

#include "zone_bench_camera.h"
#include "zone_bench_merge.h"
#include "zone_bench_metrics.h"
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/zone_bsp.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "gl_call_counters.h"
#include "gpu_sync_probe.h"
#include "gpu_sync_bench_hooks.h"
#endif

// ======== Q01: moving camera-path record/replay ========
//
// A "trace" is one resolved camera pose per rendered frame (warmup +
// measured), independent of whether it came from live navmesh/orbit path
// evaluation (record/normal mode) or was loaded verbatim from a --path-file
// (replay mode). Building this ONE array up front, before entity placement
// and before the render loop, is what makes replay reproduce entity
// placement: entity placement (zone_bench_loader.cpp
// placeEntitiesAlongPath) is fed the trace's MEASURED-frame positions
// directly (see design point A3 in refactor/Q01_path_replay_harness_align.md)
// instead of resampling the raw navmesh polyline by arc length -- a scheme
// that has no equivalent once the navmesh isn't available (replay). The
// render loop then just indexes into the trace instead of re-walking
// pathDist/CameraPath::getPositionAt() itself, so record and replay share
// exactly one code path for "what pose does frame N have".

// Parse a --dump-path-format file: '#'-prefixed / blank lines ignored,
// everything else must be "x,y,z,heading,pitch". Returns false (with a
// stderr ERROR) on any I/O or parse failure.
bool loadPathFile(const std::string& path, std::vector<PathFilePose>& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "ERROR: --path-file: cannot open '%s'\n", path.c_str());
        return false;
    }
    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lineNo;
        // Trim leading whitespace for the comment/blank check.
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0' || *p == '\r') continue;

        PathFilePose pose{};
        int n = sscanf(p, "%f,%f,%f,%f,%f",
                        &pose.x, &pose.y, &pose.z, &pose.heading, &pose.pitch);
        if (n != 5) {
            fprintf(stderr, "ERROR: --path-file: %s:%d: expected \"x,y,z,heading,pitch\", got: %s",
                    path.c_str(), lineNo, line);
            fclose(f);
            return false;
        }
        out.push_back(pose);
    }
    fclose(f);
    if (out.empty()) {
        fprintf(stderr, "ERROR: --path-file: '%s' has no pose lines\n", path.c_str());
        return false;
    }
    return true;
}

// Write the trace's measured-frame poses to a --dump-path file. traceMeasured
// is exactly opts.frames entries (the same poses fed to
// placeEntitiesAlongPath()); CSV frame row i corresponds to path line i.
void writePathFile(const std::string& path, const Options& opts,
                           const std::vector<TracePose>& traceMeasured) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "ERROR: --dump-path: cannot open '%s' for write\n", path.c_str());
        return;
    }
    fprintf(f, "# Q01 camera path dump\n");
    fprintf(f, "# zone: %s\n", opts.zoneName.c_str());
    fprintf(f, "# seed: %u\n", opts.seed);
    fprintf(f, "# entities: %d\n", opts.entityCount);
    fprintf(f, "# frames: %d\n", opts.frames);
    fprintf(f, "# command: %s\n", opts.fullCommandLine.c_str());
    fprintf(f, "# format: x,y,z,heading,pitch (EQ coords + degrees, --camera-pose convention)\n");
    fprintf(f, "# one line per rendered MEASURED (post-warmup) frame; CSV frame row i == path line i\n");
    // %.9g round-trips an IEEE-754 float32 exactly (9 significant digits is
    // the documented minimum) -- required for the replay determinism
    // acceptance test (pose columns byte-identical to this file).
    for (const auto& tp : traceMeasured) {
        fprintf(f, "%.9g,%.9g,%.9g,%.9g,%.9g\n", tp.pos.x, tp.pos.y, tp.pos.z, tp.heading, tp.pitch);
    }
    fclose(f);
    printf("[dump-path] %s: saved (%zu poses)\n", path.c_str(), traceMeasured.size());
}

// ======== Rendering ========

// Set up default lighting uniforms on the shader system.
void setupLighting(ZoneBench::ZoneBenchData& data, const Options& opts) {
    if (!data.zoneShader) return;

    // Midnight lighting — dark ambient, no sun, player light is dominant
    data.zoneShader->setAmbientColor(0.02f, 0.02f, 0.04f);
    data.zoneShader->setSunDirection(0.0f, -1.0f, 0.0f);
    data.zoneShader->setSunColor(0.0f, 0.0f, 0.0f);
    data.zoneShader->setTintColor(0.15f, 0.15f, 0.2f);

    // Fog (based on OrangePi preset: clip 300, fog 60-95%)
    float clipDist = 300.0f;
    data.zoneShader->setFog(clipDist * 0.6f, clipDist * 0.95f,
                            0.6f, 0.65f, 0.7f, 1.0f);

    // Point lights from zone data
    int numLights = 0;
    if (opts.lightCount >= 0) {
        numLights = std::min(opts.lightCount, 8);
    } else if (data.zone) {
        // Use zone lights (up to 7, index 0 reserved for player light)
        auto& lights = data.zone->lights;
        numLights = std::min(static_cast<int>(lights.size()), 7);
        for (int i = 0; i < numLights; ++i) {
            auto& l = lights[i];
            data.zoneShader->setPointLight(i + 1,  // index 0 = player light
                                           l->x, l->z, l->y,  // EQ→Irrlicht
                                           l->r, l->g, l->b,
                                           1.0f, 0.0f, l->radius > 0 ? 1.0f / (l->radius * l->radius) : 0.001f);
        }
    }
    // Slot 0 is the player light (set below), zone lights occupy 1..numLights.
    data.zoneShader->setNumPointLights(std::min(numLights + 1, 8));

    // Q03: player light (per-pixel, index 0) — position is updated per frame
    // at the camera position in setupCamera() (unchanged by this unit); the
    // camera pose is an eye-height point, and production's lightstone sits
    // at player position + 3z (Irrlicht up), i.e. roughly eye height too —
    // see IrrlichtRenderer::setEntityLight, renderer_entities.cpp:98-156 —
    // so the existing camera-collocated placement is treated as equivalent
    // and is NOT changed here.
    //
    // Color/radius/attenuation are mirrored EXACTLY from what the production
    // SHADER receives (verified chain: SLight.Attenuation is set in
    // setEntityLight to (1, 0, 19/radius^2) and flows untouched through
    // SimulationInput::playerLightAtt{Constant,Linear,Quadratic}
    // (renderer_sim_bridge.cpp:301-305) -> SelectedLight::attConstant/
    // attLinear/attQuadratic (sim_visibility.cpp:581-583) ->
    // ZoneShader::setPlayerLightAtten (renderer_sim_bridge.cpp:760)):
    //   intensity = level/10, radius = 20 + intensity*80
    //   color: r = min(1, 0.9 + 0.1*intensity), g = min(1, 0.7 + 0.2*intensity),
    //          b = min(1, 0.4 + 0.2*intensity); each scaled by `intensity`
    //          (the DiffuseColor production stores), then BY 3.0 — the boost
    //          renderer_sim_bridge.cpp:745/754-761 applies to the color
    //          uniforms the shader receives (both setPointLight(0,...) and
    //          setPlayerLightColor). Q03 amendment: the boost IS part of the
    //          production shader input and IS reproduced here (the original
    //          Q03 pass mirrored the SLight color only, which under-lit
    //          every scene 3x vs real gameplay — Will's re-bless review
    //          caught it). Attenuation is never boosted, so the radius-mode
    //          R solve is unaffected.
    //   attenuation = (1, 0, 19/radius^2)
    // The boosted color/atten are also mirrored into point-light slot 0
    // (per-frame, alongside the camera update) because production fills
    // slot 0 too — that is what lights DEMOTED (lightweight, per-vertex)
    // regions; without it the benchmark's lightweight path had NO player
    // light at all, harsher than production ever renders.
    // Level reference: TypeToLevel(LightTypeGreaterLightstone) = 9
    // (light_source.h) — the best practical in-game light. Level 10 is
    // Globe of Stars.
    // Level 0 = no player light: production removes the light scene node
    // entirely (setEntityLight, lightLevel==0 branch); the benchmark has no
    // equivalent "remove the light" step (index 0 is always sent to the
    // shader), so the same zero-contribution result is reproduced instead by
    // zeroing the color uniform -- with color (0,0,0) the per-pixel player
    // light term is exactly 0 regardless of the attenuation triple, which
    // is the observable behavior production's node-removal produces.
    {
        int level = opts.playerLightLevel;
        if (level <= 0) {
            data.playerLightColor[0] = data.playerLightColor[1] = data.playerLightColor[2] = 0.0f;
            data.playerLightAtten[0] = 1.0f; data.playerLightAtten[1] = 0.0f; data.playerLightAtten[2] = 0.0f;
        } else {
            float intensity = level / 10.0f;
            float radius = 20.0f + intensity * 80.0f;
            float r = std::min(1.0f, 0.9f + intensity * 0.1f);
            float g = std::min(1.0f, 0.7f + intensity * 0.2f);
            float b = std::min(1.0f, 0.4f + intensity * 0.2f);
            float quad = 19.0f / (radius * radius);
            const float boost = 3.0f;  // renderer_sim_bridge.cpp:745 — color only, never attenuation
            data.playerLightColor[0] = r * intensity * boost;
            data.playerLightColor[1] = g * intensity * boost;
            data.playerLightColor[2] = b * intensity * boost;
            data.playerLightAtten[0] = 1.0f; data.playerLightAtten[1] = 0.0f; data.playerLightAtten[2] = quad;
        }
        data.zoneShader->setPlayerLightColor(data.playerLightColor[0],
                                             data.playerLightColor[1],
                                             data.playerLightColor[2]);
        data.zoneShader->setPlayerLightAtten(data.playerLightAtten[0],
                                             data.playerLightAtten[1],
                                             data.playerLightAtten[2]);
        // Seed point-light slot 0 (per-vertex path, demoted regions);
        // position is re-set every frame in setupCamera().
        data.zoneShader->setPointLight(0, 0.0f, 0.0f, 0.0f,
                                       data.playerLightColor[0],
                                       data.playerLightColor[1],
                                       data.playerLightColor[2],
                                       data.playerLightAtten[0],
                                       data.playerLightAtten[1],
                                       data.playerLightAtten[2]);
    }
    // Q03: attenuation-mirror proof -- read back what the shader uniforms
    // actually hold (not just what we computed above) via the same
    // accessors the R solve (zone_bench_loader.cpp) and per-frame draw path
    // consume, gated on --dump so it doesn't spam ordinary runs.
    if (opts.dump) {
        const float* c = data.zoneShader->playerLightColor();
        const float* a = data.zoneShader->playerLightAtten();
        printf("[player-light] level=%d color=(%.6f,%.6f,%.6f) atten=(%.6f,%.6f,%.9f)\n",
               opts.playerLightLevel, c[0], c[1], c[2], a[0], a[1], a[2]);
    }
    // Note: setPerPixelPlayerLight(true) is called in loadZone() before mesh building
}

// Set up projection and view matrices for a given camera state (EQ coords).
void setupCamera(ZoneBench::ZoneBenchData& data,
                         float eqX, float eqY, float eqZ,
                         float dirX, float dirY, float dirZ) {
    // Projection matrix
    float fov = 60.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(data.device->getVideoDriver()->getScreenSize().Width) /
                   static_cast<float>(data.device->getVideoDriver()->getScreenSize().Height);
    float nearClip = 1.0f;
    float farClip = 300.0f;  // Match OrangePi preset

    irr::core::matrix4 proj;
    proj.buildProjectionMatrixPerspectiveFovLH(fov, aspect, nearClip, farClip);
    data.driver->setTransform(irr::video::ETS_PROJECTION, proj);

    // View matrix: EQ coords (x, y, z-up) → Irrlicht (x, z, y-up)
    irr::core::vector3df irrPos(eqX, eqZ, eqY);
    irr::core::vector3df irrTarget(eqX + dirX, eqZ + dirZ, eqY + dirY);
    irr::core::vector3df irrUp(0, 1, 0);  // Irrlicht Y-up

    irr::core::matrix4 view;
    view.buildCameraLookAtMatrixLH(irrPos, irrTarget, irrUp);
    data.driver->setTransform(irr::video::ETS_VIEW, view);

    // Update frustum culler (EQ coords)
    if (data.frustumCuller) {
        data.frustumCuller->update(eqX, eqY, eqZ, dirX, dirY, dirZ,
                                   fov, aspect, nearClip, farClip);
    }

    // Update shader camera position (Irrlicht coords)
    if (data.zoneShader) {
        data.zoneShader->setCameraPos(irrPos.X, irrPos.Y, irrPos.Z);
        data.zoneShader->setPlayerLightPos(irrPos.X, irrPos.Y, irrPos.Z);
        // Q03 amendment: keep point-light slot 0 (the per-vertex player
        // light that lights DEMOTED regions, mirroring production's
        // renderer_sim_bridge.cpp:746-752) collocated with the camera.
        data.zoneShader->setPointLight(0, irrPos.X, irrPos.Y, irrPos.Z,
                                       data.playerLightColor[0],
                                       data.playerLightColor[1],
                                       data.playerLightColor[2],
                                       data.playerLightAtten[0],
                                       data.playerLightAtten[1],
                                       data.playerLightAtten[2]);
    }
}

// Draw all entries: setMaterial + drawMeshBuffer for each.
// Returns state change count. P08: outSubmitMs, if non-null, receives the
// wall time (steady_clock, no syscalls) of just the setMaterial+
// drawMeshBuffer loop below -- the "submission phase" for CSV reporting.
// Excludes the depth-test toggle (negligible, one GL call each way).
// P09: when mergeZoneDraws is set, the actual glDrawElements-equivalent
// call count issued (after cross-region same-material coalescing) is
// written to *outActualDraws (left untouched -- not written -- when null or
// when mergeZoneDraws is false, in which case it equals entries.size() at
// the call site as before). stateChanges (the return value) is unaffected
// by merging -- it is still a property of the input entries list (distinct
// materialKey runs), which is exactly the P08 C5 "ceiling" quantity to
// compare the merged draw count against.
// Default arguments live on the zone_bench_frame.h declaration (T387); a
// definition may not repeat them.
int renderDrawEntries(ZoneBench::ZoneBenchData& data,
                        const std::vector<ZoneBench::DrawEntry>& entries,
                        bool depthTest,
                        float* outSubmitMs,
                        bool mergeZoneDraws,
                        int* outActualDraws,
                        bool bakedUnmergedZoneDraws) {
#ifdef EQT_HAS_GLES2
    if (!depthTest) {
        glDisable(GL_DEPTH_TEST);
    }
#else
    (void)depthTest;
#endif

    int stateChanges = 0;
    uint32_t prevMatKey = UINT32_MAX;
    for (const auto& de : entries) {
        if (de.materialKey != prevMatKey) {
            ++stateChanges;
            prevMatKey = de.materialKey;
        }
    }

    const auto submitStart = std::chrono::steady_clock::now();
    if (mergeZoneDraws) {
        int actualDraws = ZoneBench::renderMergedDrawEntries(data, data.driver, entries);
        if (outActualDraws) *outActualDraws = actualDraws;
    } else if (bakedUnmergedZoneDraws) {
        // P09c Phase C: fair baked-vs-baked comparison mode -- see
        // zone_bench_merge.h's renderBakedUnmergedDrawEntries for rationale.
        int actualDraws = ZoneBench::renderBakedUnmergedDrawEntries(data, data.driver, entries);
        if (outActualDraws) *outActualDraws = actualDraws;
    } else {
        for (const auto& de : entries) {
            data.driver->setTransform(irr::video::ETS_WORLD, de.worldMat);

            if (de.materialOverride >= 0) {
                // Apply material with overridden shader type
                irr::video::SMaterial mat = de.buffer->getMaterial();
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
                data.driver->setMaterial(mat);
            } else {
                data.driver->setMaterial(de.buffer->getMaterial());
            }
            data.driver->drawMeshBuffer(de.buffer);
        }
        if (outActualDraws) *outActualDraws = static_cast<int>(entries.size());
    }
    if (outSubmitMs) {
        *outSubmitMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - submitStart).count();
    }

#ifdef EQT_HAS_GLES2
    if (!depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
#endif

    return stateChanges;
}

// Count total polygons in draw entries.
int countPolygons(const std::vector<ZoneBench::DrawEntry>& entries) {
    int total = 0;
    for (const auto& de : entries) {
        total += de.buffer->getIndexCount() / 3;
    }
    return total;
}

// P01 experiment 2: classify each draw entry's effective material type (after
// any materialOverride from adaptive lighting) into "alpha-test family"
// (solid vs alphaTest+alphaTestRGBA+wind, heavy or lightweight variants) vs
// "solid family", using ZoneShaderManager's own type IDs so this never
// hard-codes irrlicht material-type enum values. Returns {alphaTestDraws,
// solidDraws}; entries whose type matches neither known family (e.g.
// fixed-function fallback) are counted under solidDraws as the conservative
// default (matches production's non-alpha-test path).
std::pair<int, int> countMaterialFamilies(
    const std::vector<ZoneBench::DrawEntry>& entries,
    EQT::Graphics::ZoneShaderManager* zoneShader) {
    int alphaTestDraws = 0, solidDraws = 0;
    if (!zoneShader) {
        return {0, static_cast<int>(entries.size())};
    }
    const irr::s32 alphaTypes[] = {
        zoneShader->getMaterialTypeAlphaTest(),
        zoneShader->getMaterialTypeAlphaTestRGBA(),
        zoneShader->getMaterialTypeLWAlphaTest(),
        zoneShader->getMaterialTypeLWAlphaTestRGBA(),
        zoneShader->getMaterialTypeWindAlphaTest(),
        zoneShader->getMaterialTypeLWWindAlphaTest(),
        zoneShader->getMaterialTypeAtlasAlpha(),
        zoneShader->getMaterialTypeLWAtlasAlpha(),
    };
    for (const auto& de : entries) {
        irr::s32 effectiveType = (de.materialOverride >= 0)
            ? de.materialOverride
            : static_cast<irr::s32>(de.buffer->getMaterial().MaterialType);
        bool isAlpha = false;
        for (irr::s32 t : alphaTypes) {
            if (t >= 0 && t == effectiveType) { isAlpha = true; break; }
        }
        if (isAlpha) ++alphaTestDraws; else ++solidDraws;
    }
    return {alphaTestDraws, solidDraws};
}

#ifdef EQT_HAS_GLES2
// P02 experiment C: runtime GL filter-state spot-check. P01 answered H2
// (ETC1-vs-RGBA filter/mip asymmetry) entirely from source inspection of
// constrained_texture_cache.cpp; this confirms the actual driver-reported
// state matches for a handful of real draw entries. Binds unit 0 (main) and
// unit 1 (companion, if the material uses one) via Irrlicht's own
// setMaterial() -- same technique the pre-existing --tree-test diagnostic
// uses -- then reads back GL_TEXTURE_MIN_FILTER/MAG_FILTER directly from the
// driver, no assumptions about what was requested at upload time.
static const char* glFilterName(GLint f) {
    switch (f) {
        case GL_NEAREST: return "GL_NEAREST";
        case GL_LINEAR: return "GL_LINEAR";
        case GL_NEAREST_MIPMAP_NEAREST: return "GL_NEAREST_MIPMAP_NEAREST";
        case GL_LINEAR_MIPMAP_NEAREST: return "GL_LINEAR_MIPMAP_NEAREST";
        case GL_NEAREST_MIPMAP_LINEAR: return "GL_NEAREST_MIPMAP_LINEAR";
        case GL_LINEAR_MIPMAP_LINEAR: return "GL_LINEAR_MIPMAP_LINEAR";
        default: return "?";
    }
}

void dumpFilterState(ZoneBench::ZoneBenchData& data,
                             const std::vector<ZoneBench::DrawEntry>& entries,
                             int count) {
    printf("[dump-filters] sampling first %d of %zu draw entries:\n",
           count, entries.size());
    int n = std::min<int>(count, static_cast<int>(entries.size()));
    for (int i = 0; i < n; ++i) {
        const auto& de = entries[i];
        const irr::video::SMaterial& mat = de.buffer->getMaterial();
        data.driver->setMaterial(mat);

        GLint tex0 = 0, tex1 = 0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);
        glActiveTexture(GL_TEXTURE0);

        auto dumpUnit = [&](int unit, GLint texName, const char* label) {
            if (texName == 0) {
                printf("  entry[%d] unit%d (%s): no texture bound\n", i, unit, label);
                return;
            }
            glActiveTexture(unit == 0 ? GL_TEXTURE0 : GL_TEXTURE1);
            GLint minF = 0, magF = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
            printf("  entry[%d] unit%d (%s) glTex=%d: MIN=%s MAG=%s\n",
                   i, unit, label, texName, glFilterName(minF), glFilterName(magF));
        };
        dumpUnit(0, tex0, "main");
        dumpUnit(1, tex1, "companion");
        glActiveTexture(GL_TEXTURE0);
    }
}
#endif

// H02b: convert a heading/pitch pose (degrees; heading 0=North/+Y, CW; pitch
// positive=up) into an EQ-coordinate direction vector, matching the
// convention the benchmark loop already uses when it derives heading/pitch
// from a direction vector for metrics (see the `heading = atan2f(...)` code
// in the main loop below) -- this is the exact inverse of that computation.
void poseToDirection(float headingDeg, float pitchDeg,
                             float& dirX, float& dirY, float& dirZ) {
    const float d2r = 3.14159265f / 180.0f;
    float headingRad = headingDeg * d2r;
    float pitchRad = pitchDeg * d2r;
    float horiz = cosf(pitchRad);
    dirX = horiz * sinf(headingRad);
    dirY = horiz * cosf(headingRad);
    dirZ = sinf(pitchRad);
}

// H02b: read back the current framebuffer and compute (distinct color count,
// fraction of pixels equal to the clear/background color). Sampled at a
// stride to keep this cheap -- used by --pose-search scoring and available
// for ad-hoc diagnostics. Not used by the CI gate itself (that does its own
// PIL-based comparison in Python on the dumped PNGs).
void computeFrameStats(ZoneBench::ZoneBenchData& data,
                               uint8_t clearR, uint8_t clearG, uint8_t clearB,
                               size_t& distinctColors, double& skyFraction) {
    distinctColors = 0;
    skyFraction = 1.0;
    irr::video::IImage* shot = data.driver->createScreenShot();
    if (!shot) return;

    irr::core::dimension2d<irr::u32> dim = shot->getDimension();
    const int stride = 2;  // sample every 2nd pixel in each axis (4x fewer samples)
    std::unordered_set<uint32_t> seen;
    seen.reserve(4096);
    size_t total = 0, skyCount = 0;
    for (irr::u32 y = 0; y < dim.Height; y += stride) {
        for (irr::u32 x = 0; x < dim.Width; x += stride) {
            irr::video::SColor c = shot->getPixel(x, y);
            uint32_t key = (c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue();
            seen.insert(key);
            if (c.getRed() == clearR && c.getGreen() == clearG && c.getBlue() == clearB) {
                ++skyCount;
            }
            ++total;
        }
    }
    shot->drop();
    distinctColors = seen.size();
    skyFraction = total > 0 ? static_cast<double>(skyCount) / static_cast<double>(total) : 1.0;
}

// ======== Synthetic Draw Call Test (Z5) ========

void runSyntheticDrawTest(ZoneBench::ZoneBenchData& data, int numFrames) {
    printf("\n=== Synthetic Draw Call Overhead Test ===\n");

    // Find a mesh buffer with geometry to use as our test draw
    irr::scene::IMeshBuffer* testBuf = nullptr;
    irr::core::matrix4 identityMat;

    for (auto& [idx, rd] : data.regions) {
        if (rd.mesh && rd.mesh->getMeshBufferCount() > 0) {
            testBuf = rd.mesh->getMeshBuffer(0);
            if (testBuf && testBuf->getVertexCount() > 0) {
                identityMat.setTranslation(rd.position);
                break;
            }
        }
    }

    if (!testBuf) {
        printf("  ERROR: No mesh buffer found for synthetic test\n");
        return;
    }

    int testPolys = testBuf->getIndexCount() / 3;
    printf("  Test buffer: %d vertices, %d polys\n",
           testBuf->getVertexCount(), testPolys);

    // Sweep draw call counts
    int drawCounts[] = {1, 10, 50, 100, 200, 500, 1000};
    int numSweeps = sizeof(drawCounts) / sizeof(drawCounts[0]);

    // Test A: Same material (no state changes)
    printf("\n  --- Same material (no state changes) ---\n");
    printf("  %8s  %10s  %10s  %10s  %10s\n",
           "Draws", "CPU ms", "GPU ms", "Total ms", "us/draw");

    for (int s = 0; s < numSweeps; ++s) {
        int N = drawCounts[s];
        // Warmup
        for (int w = 0; w < 5; ++w) {
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
            data.driver->setMaterial(testBuf->getMaterial());
            for (int d = 0; d < N; ++d)
                data.driver->drawMeshBuffer(testBuf);
#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            data.driver->endScene();
        }

        // Measure
        double cpuSum = 0, gpuSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
            data.driver->setMaterial(testBuf->getMaterial());

            auto drawStart = std::chrono::steady_clock::now();
            for (int d = 0; d < N; ++d)
                data.driver->drawMeshBuffer(testBuf);
            auto drawEnd = std::chrono::steady_clock::now();

#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            auto gpuEnd = std::chrono::steady_clock::now();
            data.driver->endScene();

            cpuSum += std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
            gpuSum += std::chrono::duration<double, std::milli>(gpuEnd - drawEnd).count();
            totalSum += std::chrono::duration<double, std::milli>(gpuEnd - t0).count();
        }

        double avgCpu = cpuSum / numFrames;
        double avgGpu = gpuSum / numFrames;
        double avgTotal = totalSum / numFrames;
        double usPerDraw = (avgCpu * 1000.0) / N;

        printf("  %8d  %10.2f  %10.2f  %10.2f  %10.1f\n",
               N, avgCpu, avgGpu, avgTotal, usPerDraw);
    }

    // Test B: Different material per draw (force state changes)
    printf("\n  --- Material switch per draw ---\n");
    printf("  %8s  %10s  %10s  %10s  %10s\n",
           "Draws", "CPU ms", "GPU ms", "Total ms", "us/draw");

    // Collect distinct mesh buffers with different materials
    std::vector<std::pair<irr::scene::IMeshBuffer*, irr::core::matrix4>> distinctBuffers;
    for (auto& [idx, rd] : data.regions) {
        if (!rd.mesh) continue;
        irr::core::matrix4 mat;
        mat.setTranslation(rd.position);
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            auto* buf = rd.mesh->getMeshBuffer(i);
            if (buf && buf->getVertexCount() > 0) {
                distinctBuffers.push_back({buf, mat});
                if (distinctBuffers.size() >= 1000) break;
            }
        }
        if (distinctBuffers.size() >= 1000) break;
    }

    for (int s = 0; s < numSweeps; ++s) {
        int N = drawCounts[s];
        if (N > static_cast<int>(distinctBuffers.size())) {
            printf("  %8d  (skipped, only %zu distinct buffers)\n", N, distinctBuffers.size());
            continue;
        }

        // Warmup
        for (int w = 0; w < 5; ++w) {
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            for (int d = 0; d < N; ++d) {
                auto& [buf, mat] = distinctBuffers[d];
                data.driver->setTransform(irr::video::ETS_WORLD, mat);
                data.driver->setMaterial(buf->getMaterial());
                data.driver->drawMeshBuffer(buf);
            }
#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            data.driver->endScene();
        }

        // Measure
        double cpuSum = 0, gpuSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));

            auto drawStart = std::chrono::steady_clock::now();
            for (int d = 0; d < N; ++d) {
                auto& [buf, mat] = distinctBuffers[d];
                data.driver->setTransform(irr::video::ETS_WORLD, mat);
                data.driver->setMaterial(buf->getMaterial());
                data.driver->drawMeshBuffer(buf);
            }
            auto drawEnd = std::chrono::steady_clock::now();

#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            auto gpuEnd = std::chrono::steady_clock::now();
            data.driver->endScene();

            cpuSum += std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
            gpuSum += std::chrono::duration<double, std::milli>(gpuEnd - drawEnd).count();
            totalSum += std::chrono::duration<double, std::milli>(gpuEnd - t0).count();
        }

        double avgCpu = cpuSum / numFrames;
        double avgGpu = gpuSum / numFrames;
        double avgTotal = totalSum / numFrames;
        double usPerDraw = (avgCpu * 1000.0) / N;

        printf("  %8d  %10.2f  %10.2f  %10.2f  %10.1f\n",
               N, avgCpu, avgGpu, avgTotal, usPerDraw);
    }
}
