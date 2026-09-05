// gles2_zone_benchmark — Offline zone rendering benchmark
//
// Loads a real EQ zone via the production S3D/WLD pipeline, replays a
// deterministic camera path (from navmesh or orbit fallback), and measures
// per-frame rendering metrics with A/B toggles for each optimization.
//
// Usage:
//   ./gles2_zone_benchmark --zone qeynos2 --eq-path /path/to/EQ \
//       --eqe1-path /path/to/eqe1 --drm --frames 300 --csv results.csv
//
// See --help for full options.

#include "zone_bench_loader.h"
#include "zone_bench_camera.h"
#include "zone_bench_entity_viewer.h"
#include "zone_bench_histogram.h"
#include "zone_bench_merge.h"
#include "tool_race_db.h"
#include "zone_bench_merge_probe.h"
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

// Include full definitions for types used via unique_ptr/shared_ptr in ZoneBenchData
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/zone_bsp.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "gl_call_counters.h"  // P38: driver-side glBindTexture/glTexParameteri counters
#include "gpu_sync_probe.h"    // P52: per-phase GPU-sync characterization instrumentation
#include "gpu_sync_bench_hooks.h"  // P52: call-site wrappers (kept out of this file to limit its growth)

#endif

// T69: full per-subsystem memory-JSON accounting (BenchMemorySnapshot,
// computeMemorySnapshot(), writeMemoryJson()) -- split into its own TU;
// see zone_bench_memory.h/.cpp for the reuse-vs-recompute accounting and
// why (check_file_sizes.sh's FAIL ceiling was already exceeded by this file
// before this unit).
#include "zone_bench_memory.h"

#include "zone_bench_options.h"

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
struct TracePose {
    glm::vec3 pos;
    glm::vec3 dir;
    float heading = 0.0f;
    float pitch = 0.0f;
};

// One line of a loaded --path-file: "x,y,z,heading,pitch" (EQ coords +
// degrees -- exactly the --camera-pose convention, see printUsage()).
struct PathFilePose { float x, y, z, heading, pitch; };

// Parse a --dump-path-format file: '#'-prefixed / blank lines ignored,
// everything else must be "x,y,z,heading,pitch". Returns false (with a
// stderr ERROR) on any I/O or parse failure.
static bool loadPathFile(const std::string& path, std::vector<PathFilePose>& out) {
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
static void writePathFile(const std::string& path, const Options& opts,
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
static void setupLighting(ZoneBench::ZoneBenchData& data, const Options& opts) {
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
static void setupCamera(ZoneBench::ZoneBenchData& data,
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
static int renderDrawEntries(ZoneBench::ZoneBenchData& data,
                        const std::vector<ZoneBench::DrawEntry>& entries,
                        bool depthTest,
                        float* outSubmitMs = nullptr,
                        bool mergeZoneDraws = false,
                        int* outActualDraws = nullptr,
                        bool bakedUnmergedZoneDraws = false) {
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
static int countPolygons(const std::vector<ZoneBench::DrawEntry>& entries) {
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
static std::pair<int, int> countMaterialFamilies(
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

static void dumpFilterState(ZoneBench::ZoneBenchData& data,
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
static void poseToDirection(float headingDeg, float pitchDeg,
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
static void computeFrameStats(ZoneBench::ZoneBenchData& data,
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

static void runSyntheticDrawTest(ZoneBench::ZoneBenchData& data, int numFrames) {
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

// ======== Main ========

int main(int argc, char** argv) {
    InitLogging(argc, argv);
    Options opts = parseArgs(argc, argv);

    // T270: race lookups are db-only now -- see tools/tool_race_db.h.
    openWilleqRaceDatabase();

    // P02: --lighting-model is a thin convenience wrapper over the existing
    // --adaptive-lighting/--zone-pp-budget mechanism (see Options::LightingModel
    // comment above); LightingModel::Default (flag never passed) touches
    // nothing, so default behavior is byte-for-byte unchanged.
    if (opts.lightingModel == Options::LightingModel::PerVertexOrLightweight) {
        opts.adaptiveLighting = true;
        opts.perPixelZoneBudget = 0;
        printf("[lighting-model] pervertex/lightweight requested -- forcing "
               "adaptiveLighting=true, zone-pp-budget=0 (all zone regions "
               "lightweight-shaded).\n");
    } else if (opts.lightingModel == Options::LightingModel::PerPixel) {
        printf("[lighting-model] perpixel requested -- default benchmark "
               "behavior (zone_bench_loader.cpp always calls "
               "setPerPixelPlayerLight(true); adaptiveLighting left as set by "
               "other flags, default off = all regions per-pixel).\n");
    }

    // P47: entity-viewer mode forces the entity-loading options to the
    // proven-correct one-variable configuration for the requested leg,
    // rather than requiring the caller to separately pass
    // --entities/--test-races/--animate/--no-entity-shader-promotion and
    // risk getting the pairing wrong (see zone_bench_loader.cpp's
    // applyEntityShaderMaterialPromotion() Review comment and
    // refactor/P46c_neutral_pair_stop.md / P46d_cache_key_fix.md for why
    // this pairing matters).
    if (opts.entityViewer) {
        if (opts.entityViewerRace <= 0) {
            fprintf(stderr, "ERROR: --entity-viewer requires --entity-viewer-race N\n");
            return 1;
        }
        if (opts.entityViewerOutDir.empty()) {
            fprintf(stderr, "ERROR: --entity-viewer requires --entity-viewer-out-dir DIR\n");
            return 1;
        }
        if (opts.entityViewerComposite && opts.eqe1Path.empty()) {
            fprintf(stderr, "ERROR: --entity-viewer-composite requires --eqe1-path "
                            "(composite sheet alpha only exists in the ETC1/EQE1 format)\n");
            return 1;
        }
        opts.entityCount = 1;
        opts.testRaceIds = { static_cast<uint16_t>(opts.entityViewerRace) };
        opts.animate = true;
        // P48: the composite leg routes the entity through the REAL composite
        // eligibility gate (race_model_loader.cpp's sheet-capability check,
        // zone_bench_loader.cpp's --composite-entities wiring) instead of
        // forcing multi-buffer -- this is what lets the viewer demonstrate a
        // masked race's composite alpha-test rather than only its per-part
        // alpha-test (which the plain ETC1/RGBA legs already covered, P47).
        // An unmasked or stale-sheet (no alpha companion) race composited
        // this way still renders correctly (EMT_SOLID, matches pre-P48) --
        // the eligibility gate and mesh_building.cpp's hasAlpha-gated
        // MaterialType make this automatic, no viewer-side branching needed.
        opts.compositeEntities = opts.entityViewerComposite;
        // RGBA leg (no --eqe1-path): the built-in alpha-test discard shader
        // reads unit-0 alpha directly and needs promotion OFF (P46c/P46d).
        // ETC1 leg (--eqe1-path set, composite or per-part): promotion must
        // stay ON -- it is the only code path that binds the companion alpha
        // texture to unit 1 (P46b/applyEntityShaderMaterialPromotion()), and
        // mesh_building.cpp's buildCompositeMeshFromGeometry() (P48) marks
        // composite buffers with the SAME EMT_TRANSPARENT_ALPHA_CHANNEL_REF
        // marker the per-part path uses, so the SAME promotion call picks it
        // up with no separate composite-specific plumbing.
        opts.noEntityShaderPromotion = opts.eqe1Path.empty();
        opts.entityViewerMode = opts.eqe1Path.empty() ? "rgba"
                               : (opts.entityViewerComposite ? "etc1-composite" : "etc1");
    }

    if (opts.eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path is required\n");
        printUsage(argv[0]);
        return 1;
    }

    printf("=== gles2_zone_benchmark ===\n");
    printf("Zone: %s, Resolution: %dx%d, Frames: %d\n",
           opts.zoneName.c_str(), opts.width, opts.height, opts.frames);
    printf("Toggles: materialSort=%d pvsCull=%d frustumCull=%d occlusionCull=%d "
           "frontToBack=%d depthTest=%d entities=%d animate=%d\n",
           opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
           opts.frontToBack, opts.depthTest, opts.entityCount, opts.animate);
    if (opts.maxDraws > 0) printf("  maxDraws=%d\n", opts.maxDraws);
    if (opts.maxPolys > 0) printf("  maxPolys=%d\n", opts.maxPolys);
    printf("\n");

    // ── Load zone ──
    ZoneBench::BenchConfig benchConfig;
    benchConfig.zoneName = opts.zoneName;
    benchConfig.eqPath = opts.eqPath;
    benchConfig.eqe1Path = opts.eqe1Path;
    benchConfig.width = opts.width;
    benchConfig.height = opts.height;
    benchConfig.useDRM = opts.useDRM && !opts.forceX11;
    benchConfig.entityCount = opts.entityCount;
    benchConfig.testRaceIds = opts.testRaceIds;
    benchConfig.animate = opts.animate;
    benchConfig.compositeEntities = opts.compositeEntities;
    benchConfig.loadObjects = opts.loadObjects;
    benchConfig.shaderDebugMode = opts.shaderDebugMode;
    benchConfig.loadMemAudit = opts.loadMemAudit;
    benchConfig.occluderBudget = opts.occluderBudget;
    benchConfig.noEntityShaderPromotion = opts.noEntityShaderPromotion;

#ifdef EQT_HAS_GLES2
    // P52: must enable before loadZone() creates the driver (capability
    // probe runs once at driver init -- see gpu_sync_probe.h).
    if (opts.gpuSyncProbe) eqtEnableGPUSyncProbe(true);
#endif

    auto loadT0 = std::chrono::steady_clock::now();
    auto data = ZoneBench::loadZone(benchConfig);
    auto loadT1 = std::chrono::steady_clock::now();
    if (!data) {
        fprintf(stderr, "ERROR: Failed to load zone\n");
        return 1;
    }
    if (opts.reportLoadTime) {
        double loadMs = std::chrono::duration<double, std::milli>(loadT1 - loadT0).count();
        printf("LOAD_PHASE_MS=%.2f\n", loadMs);
    }

    // P49 (refactor/P49_drm_fail_loud.md, P43 follow-up): P43 root-caused
    // every historical Pi "400x300" run to a DRM mode fallback -- the
    // requested resolution had no exact match among the connector's
    // advertised modes, so CIrrDeviceFB silently substituted the
    // preferred mode (2560x1440) and overwrote Width/Height, with nobody
    // noticing because nothing compared achieved vs requested. Compare
    // here, right after device creation, before any frame is measured:
    // refuse to run (like a hard assertion failure, not a warning) unless
    // --allow-mode-fallback opts in, and ALWAYS record the achieved
    // resolution into the CSV (setActualResolution() below) so a dataset
    // can never again be silently mislabeled even when the caller does
    // pass --allow-mode-fallback.
    int actualWidth = static_cast<int>(data->driver->getScreenSize().Width);
    int actualHeight = static_cast<int>(data->driver->getScreenSize().Height);
    if (actualWidth != opts.width || actualHeight != opts.height) {
        fprintf(stderr,
                "%s: achieved resolution %dx%d != requested %dx%d -- the display "
                "did not have an exact matching mode and the device silently fell "
                "back (see docker/irrlicht-drm/CIrrDeviceFB.cpp DRM mode-fallback "
                "log line above, and refactor/P43_res_visibility_root_cause.md).\n",
                opts.allowModeFallback ? "WARNING" : "ERROR",
                actualWidth, actualHeight, opts.width, opts.height);
        if (!opts.allowModeFallback) {
            fprintf(stderr,
                    "Refusing to run at a silently-substituted resolution "
                    "(pass --allow-mode-fallback to run anyway; the CSV will "
                    "still record the ACTUAL %dx%d resolution either way).\n",
                    actualWidth, actualHeight);
            return 1;
        }
    }
    // metrics.setActualResolution(actualWidth, actualHeight) is called once
    // `metrics` is declared, below.

#ifdef EQT_HAS_GLES2
    if (opts.glCounters) eqtEnableGLCallCounters(true);
#endif

    // P09 Phase B: build the cross-region same-material merge buffers once,
    // right after zone geometry load, before any draw. No-op cost when both
    // flags are off (function not called at all). P09c Phase C's
    // baked-unmerged mode reuses the exact same baked buffers (see
    // renderBakedUnmergedDrawEntries), so it needs them built too.
    if (opts.mergeZoneDraws || opts.bakedUnmergedZoneDraws) {
        ZoneBench::buildMergedZoneBuffers(*data);
    }

    // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
    // if (opts.analyzePortals) {
    //     ZoneBench::analyzePortals(*data, opts.portalCsvFile);
    //     return 0;
    // }
    // if (opts.detectPortals) {
    //     ZoneBench::detectPortalsFromOccluders(*data, opts.portalCsvFile);
    //     return 0;
    // }

    // ── P47: magenta entity viewer ──
    if (opts.entityViewer) {
        printf("=== Entity Viewer (P47): race=%d mode=%s frame=%d out-dir=%s ===\n",
               opts.entityViewerRace, opts.entityViewerMode.c_str(), opts.entityViewerFrame,
               opts.entityViewerOutDir.c_str());
        ZoneBench::EntityViewerConfig evCfg;
        evCfg.raceId = static_cast<uint16_t>(opts.entityViewerRace);
        evCfg.raceLabel = opts.entityViewerRaceLabel;
        evCfg.modeLabel = opts.entityViewerMode;
        evCfg.animFrame = opts.entityViewerFrame;
        evCfg.outDir = opts.entityViewerOutDir;

        auto results = ZoneBench::runEntityViewer(*data, evCfg);
        if (results.empty()) {
            fprintf(stderr, "Entity viewer produced no results (see ENTITY_VIEWER ERROR above)\n");
            return 1;
        }
        int failures = 0;
        for (const auto& r : results) {
            printf("ENTITY_VIEWER_RESULT race=%d mode=%s angle=%s magenta_in_bbox=%d "
                   "bbox=%d,%d,%d,%d png=%s ok=%d\n",
                   opts.entityViewerRace, evCfg.modeLabel.c_str(), r.angleName.c_str(),
                   r.magentaInBbox, r.bboxX0, r.bboxY0, r.bboxX1, r.bboxY1,
                   r.pngPath.c_str(), r.ok ? 1 : 0);
            if (!r.ok) failures++;
        }
        printf("Entity viewer complete: %zu angle(s), %d failure(s).\n", results.size(), failures);
        return failures > 0 ? 1 : 0;
    }

    // ── Tree test: render one tree, screenshot, exit ──
    if (opts.treeTest) {
        printf("=== Tree Test ===\n");

        // Find TREE6 or TREE1 by name
        ZoneBench::ZoneBenchData::PlacedObject* treeObj = nullptr;
        for (auto& obj : data->objects) {
            if (!obj.mesh) continue;
            if (obj.name == "TREE6" || obj.name == "TREE1") {
                treeObj = &obj;
                break;
            }
        }

        if (!treeObj) {
            printf("No tree object found with alpha material\n");
            return 1;
        }

        printf("Tree '%s' found: %d buffers, pos=(%.1f, %.1f, %.1f)\n",
               treeObj->name.c_str(), treeObj->mesh->getMeshBufferCount(),
               treeObj->eqX, treeObj->eqY, treeObj->eqZ);
        for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
            auto* buf = treeObj->mesh->getMeshBuffer(i);
            const auto& mat = buf->getMaterial();
            printf("  buf[%u]: verts=%u idx=%u matType=%d tex0=%p tex1=%p\n",
                   i, buf->getVertexCount(), buf->getIndexCount(),
                   (int)mat.MaterialType, (void*)mat.getTexture(0), (void*)mat.getTexture(1));
        }

        // Compute bounding box from ALL mesh buffers to frame the whole tree
        irr::core::vector3df bbMin(1e9f, 1e9f, 1e9f), bbMax(-1e9f, -1e9f, -1e9f);
        for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
            auto* buf = treeObj->mesh->getMeshBuffer(bi);
            auto* verts = (irr::video::S3DVertex*)buf->getVertices();
            for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                bbMin.X = std::min(bbMin.X, verts[vi].Pos.X);
                bbMin.Y = std::min(bbMin.Y, verts[vi].Pos.Y);
                bbMin.Z = std::min(bbMin.Z, verts[vi].Pos.Z);
                bbMax.X = std::max(bbMax.X, verts[vi].Pos.X);
                bbMax.Y = std::max(bbMax.Y, verts[vi].Pos.Y);
                bbMax.Z = std::max(bbMax.Z, verts[vi].Pos.Z);
            }
        }
        irr::core::vector3df treeCenter = (bbMin + bbMax) * 0.5f;
        float treeHeight = bbMax.Y - bbMin.Y;
        float treeWidth = std::max(bbMax.X - bbMin.X, bbMax.Z - bbMin.Z);
        float treeDiag = sqrtf(treeHeight * treeHeight + treeWidth * treeWidth);

        printf("[TREE-TEST] Bounding box: (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
               bbMin.X, bbMin.Y, bbMin.Z, bbMax.X, bbMax.Y, bbMax.Z);
        printf("[TREE-TEST] Center: (%.1f,%.1f,%.1f), height=%.1f, width=%.1f\n",
               treeCenter.X, treeCenter.Y, treeCenter.Z, treeHeight, treeWidth);
        fflush(stdout);

        // Camera: back up enough to see the whole tree, look at center
        float camDist = treeDiag * 1.5f;
        irr::core::vector3df camPos = treeCenter + irr::core::vector3df(camDist, 0.0f, 0.0f);
        irr::core::vector3df lookAt = treeCenter;

        printf("[TREE-TEST] Camera at (%.1f,%.1f,%.1f) looking at (%.1f,%.1f,%.1f), dist=%.1f\n",
               camPos.X, camPos.Y, camPos.Z, lookAt.X, lookAt.Y, lookAt.Z, camDist);
        fflush(stdout);

        // Build view and projection matrices manually (don't rely on scene manager)
        irr::core::matrix4 viewMat;
        viewMat.buildCameraLookAtMatrixLH(camPos, lookAt, irr::core::vector3df(0, 1, 0));
        data->driver->setTransform(irr::video::ETS_VIEW, viewMat);

        float aspect = (float)opts.width / (float)opts.height;
        irr::core::matrix4 projMat;
        projMat.buildProjectionMatrixPerspectiveFovLH(0.8f, aspect, 0.1f, 1000.0f);
        data->driver->setTransform(irr::video::ETS_PROJECTION, projMat);

        // Set up lighting — bright, no fog, so tree is clearly visible
        if (data->zoneShader) {
            data->zoneShader->beginFrame();
            data->zoneShader->setAmbientColor(0.8f, 0.8f, 0.8f);
            data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
            data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
            data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
            data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);  // fog far away
            data->zoneShader->setNumPointLights(0);
            data->zoneShader->setPlayerLightPos(camPos.X, camPos.Y, camPos.Z);
            data->zoneShader->setPlayerLightColor(1.0f, 0.95f, 0.8f);
            data->zoneShader->setPlayerLightAtten(1.0f, 0.0f, 0.00001f);  // wide radius
        }

        irr::core::matrix4 identity;

        // Helper: save screenshot to BMP
        auto saveScreenshot = [&](const char* filename) {
#ifdef EQT_HAS_GLES2
            glFinish();
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            int scrW = vp[2], scrH = vp[3];
            std::vector<uint8_t> pixels(scrW * scrH * 4);
            glReadPixels(0, 0, scrW, scrH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            // Count non-clear pixels
            int nonClear = 0;
            for (int p = 0; p < scrW*scrH; p++) {
                uint8_t r = pixels[p*4], g = pixels[p*4+1], b = pixels[p*4+2];
                if (!(r == 100 && g == 150 && b == 255)) nonClear++;
            }
            printf("[TREE-TEST] %s: %d/%d non-clear pixels (%.1f%%)\n",
                   filename, nonClear, scrW*scrH, 100.0*nonClear/(scrW*scrH));

            FILE* f = fopen(filename, "wb");
            if (f) {
                uint32_t rowBytes = scrW * 3;
                uint32_t rowPad = (4 - (rowBytes % 4)) % 4;
                uint32_t dataSize = (rowBytes + rowPad) * scrH;
                uint32_t fileSize = 54 + dataSize;
                uint8_t hdr[54] = {};
                hdr[0] = 'B'; hdr[1] = 'M';
                memcpy(hdr + 2, &fileSize, 4);
                uint32_t off = 54; memcpy(hdr + 10, &off, 4);
                uint32_t dibSize = 40; memcpy(hdr + 14, &dibSize, 4);
                int32_t sw = scrW, sh = scrH;
                memcpy(hdr + 18, &sw, 4); memcpy(hdr + 22, &sh, 4);
                uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
                uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
                fwrite(hdr, 54, 1, f);
                uint8_t pad[3] = {};
                for (int y = 0; y < scrH; y++) {
                    for (int x = 0; x < scrW; x++) {
                        int idx = (y * scrW + x) * 4;
                        uint8_t bgr[3] = { pixels[idx+2], pixels[idx+1], pixels[idx+0] };
                        fwrite(bgr, 3, 1, f);
                    }
                    if (rowPad) fwrite(pad, rowPad, 1, f);
                }
                fclose(f);
                printf("[TREE-TEST] Saved %s (%dx%d)\n", filename, scrW, scrH);
            }
            fflush(stdout);
#endif
        };

        // === Screenshot 1: SOLID shader (no alpha discard) ===
        data->driver->beginScene(true, true, irr::video::SColor(255, 100, 150, 255));
        if (data->zoneShader) data->zoneShader->beginFrame();

        data->driver->setTransform(irr::video::ETS_WORLD, identity);
        for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
            irr::video::SMaterial mat = treeObj->mesh->getMeshBuffer(i)->getMaterial();
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                data->zoneShader->getActiveSolid());
            data->driver->setMaterial(mat);
            data->driver->drawMeshBuffer(treeObj->mesh->getMeshBuffer(i));
        }
        saveScreenshot("tree_test_solid.bmp");
        data->driver->endScene();

        // === Screenshot 2: FULLY RAW GL alpha test (bypass Irrlicht completely) ===
#ifdef EQT_HAS_GLES2
        {
            auto compile = [](GLenum type, const char* src) -> GLuint {
                GLuint s = glCreateShader(type);
                glShaderSource(s, 1, &src, nullptr);
                glCompileShader(s);
                GLint ok = 0;
                glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
                if (!ok) {
                    char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
                    printf("[TREE-TEST] Shader error: %s\n", log);
                }
                return s;
            };

            // Attribute locations matching Irrlicht's S3DVertex layout
            // Position=0, Normal=1, Color=2, TexCoord0=3
            const char* rawVS = R"(
                attribute vec3 aPos;
                attribute vec3 aNormal;
                attribute vec4 aColor;
                attribute vec2 aTexCoord;
                uniform mat4 uWVP;
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = uWVP * vec4(aPos, 1.0);
                    vTexCoord = aTexCoord;
                }
            )";
            const char* rawFS = R"(
                precision mediump float;
                uniform sampler2D uTexture;
                uniform sampler2D uAlphaTexture;
                varying vec2 vTexCoord;
                void main() {
                    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
                    if (alpha < 0.5) discard;
                    gl_FragColor = texture2D(uTexture, vTexCoord);
                }
            )";

            GLuint vs = compile(GL_VERTEX_SHADER, rawVS);
            GLuint fs = compile(GL_FRAGMENT_SHADER, rawFS);
            GLuint rawProg = glCreateProgram();
            glAttachShader(rawProg, vs);
            glAttachShader(rawProg, fs);
            glBindAttribLocation(rawProg, 0, "aPos");
            glBindAttribLocation(rawProg, 1, "aNormal");
            glBindAttribLocation(rawProg, 2, "aColor");
            glBindAttribLocation(rawProg, 3, "aTexCoord");
            glLinkProgram(rawProg);
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint locWVP = glGetUniformLocation(rawProg, "uWVP");
            GLint locTex = glGetUniformLocation(rawProg, "uTexture");
            GLint locAlpha = glGetUniformLocation(rawProg, "uAlphaTexture");
            printf("[TREE-TEST] Raw program: id=%u, uWVP=%d, uTex=%d, uAlpha=%d\n",
                   rawProg, locWVP, locTex, locAlpha);
            fflush(stdout);

            // Clear
            glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);

            glUseProgram(rawProg);
            irr::core::matrix4 wvp = projMat * viewMat;
            glUniformMatrix4fv(locWVP, 1, GL_FALSE, wvp.pointer());
            glUniform1i(locTex, 0);
            glUniform1i(locAlpha, 1);

            // Draw each mesh buffer using raw GL
            for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
                auto* buf = treeObj->mesh->getMeshBuffer(bi);
                const auto& mat = buf->getMaterial();

                // Use Irrlicht to bind textures, then query GL for the bound names
                data->driver->setMaterial(mat);
                GLint glTex0 = 0, glTex1 = 0;
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex0);
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex1);
                glActiveTexture(GL_TEXTURE0);

                // Also check what's ACTUALLY bound after switching program
                glUseProgram(rawProg);
                GLint finalTex0 = 0, finalTex1 = 0;
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex0);
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex1);
                glActiveTexture(GL_TEXTURE0);

                printf("[TREE-TEST] Raw draw buf[%u]: verts=%u, idx=%u, afterSetMat tex0=%d tex1=%d, afterProgSwitch tex0=%d tex1=%d\n",
                       bi, buf->getVertexCount(), buf->getIndexCount(), glTex0, glTex1, finalTex0, finalTex1);
                // Log UV coordinates for foliage buffer
                if (buf->getVertexCount() <= 32) {
                    const irr::video::S3DVertex* v = (const irr::video::S3DVertex*)buf->getVertices();
                    for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                        printf("[TREE-TEST]   v[%u] pos=(%.2f,%.2f,%.2f) uv=(%.4f,%.4f)\n",
                               vi, v[vi].Pos.X, v[vi].Pos.Y, v[vi].Pos.Z,
                               v[vi].TCoords.X, v[vi].TCoords.Y);
                    }
                }
                fflush(stdout);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, glTex0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, glTex1);
                glActiveTexture(GL_TEXTURE0);

                // Bind VBO/EBO from Irrlicht's HW buffer
                // Use client-side arrays from the mesh buffer
                const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
                const irr::u16* indices = buf->getIndices();
                GLsizei stride = sizeof(irr::video::S3DVertex);

                // Unbind VBO to use client pointers
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Normal);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, &verts[0].Color);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
                glEnableVertexAttribArray(3);

                glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
            }

            glFinish();
            saveScreenshot("tree_test_alpha.bmp");

            // === Screenshot 3: Raw GL, NO discard (confirm geometry renders) ===
            const char* noDiscardFS = R"(
                precision mediump float;
                uniform sampler2D uTexture;
                varying vec2 vTexCoord;
                void main() {
                    gl_FragColor = texture2D(uTexture, vTexCoord);
                }
            )";
            GLuint ndFS = compile(GL_FRAGMENT_SHADER, noDiscardFS);
            GLuint ndVS = compile(GL_VERTEX_SHADER, rawVS);
            GLuint ndProg = glCreateProgram();
            glAttachShader(ndProg, ndVS);
            glAttachShader(ndProg, ndFS);
            glBindAttribLocation(ndProg, 0, "aPos");
            glBindAttribLocation(ndProg, 1, "aNormal");
            glBindAttribLocation(ndProg, 2, "aColor");
            glBindAttribLocation(ndProg, 3, "aTexCoord");
            glLinkProgram(ndProg);
            glDeleteShader(ndVS);
            glDeleteShader(ndFS);

            glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(ndProg);
            GLint ndWVP = glGetUniformLocation(ndProg, "uWVP");
            GLint ndTex = glGetUniformLocation(ndProg, "uTexture");
            glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
            glUniform1i(ndTex, 0);

            // Draw only foliage buffer (buf[0]) with tex0 only
            {
                auto* buf = treeObj->mesh->getMeshBuffer(0);
                const auto& mat = buf->getMaterial();
                data->driver->setMaterial(mat);  // binds textures
                glUseProgram(ndProg);  // switch back to our program
                glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
                glUniform1i(ndTex, 0);

                const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
                const irr::u16* indices = buf->getIndices();
                GLsizei stride = sizeof(irr::video::S3DVertex);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
                glEnableVertexAttribArray(3);
                glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
            }
            glFinish();
            saveScreenshot("tree_test_rawsolid.bmp");
            glDeleteProgram(ndProg);

            // Swap to show on screen too
            eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));

            glDeleteProgram(rawProg);
        }
#endif

        printf("Tree test complete.\n");
        return 0;
    }

    // ── Composite entity test mode ──
    if (opts.compositeEntityTest) {
        const char* variantNames[] = {"naked", "cloth", "chain", "plate", "leather"};
        const char* variantName = (opts.testVariant >= 0 && opts.testVariant <= 4)
            ? variantNames[opts.testVariant] : "unknown";
        printf("=== Composite Entity Test (variant %d: %s) ===\n",
               opts.testVariant, variantName);

        // Build entity list: either one specific race (male+female) or all 12
        struct RaceGender { uint16_t raceId; uint8_t gender; const char* label; };
        std::vector<RaceGender> testEntities;

        RaceGender allRaces[] = {
            {1,0,"HUM"},{1,1,"HUF"},{2,0,"BAM"},{2,1,"BAF"},
            {3,0,"ERM"},{3,1,"ERF"},{4,0,"ELM"},{4,1,"ELF"},
            {5,0,"HIM"},{5,1,"HIF"},{6,0,"DAM"},{6,1,"DAF"},
            {7,0,"HAM"},{7,1,"HAF"},{8,0,"DWM"},{8,1,"DWF"},
            {9,0,"TRM"},{9,1,"TRF"},{10,0,"OGM"},{10,1,"OGF"},
            {11,0,"HOM"},{11,1,"HOF"},{12,0,"GNM"},{12,1,"GNF"},
        };

        if (opts.testRace > 0 && opts.testRace <= 12) {
            // Just male + female of the specified race
            testEntities.push_back(allRaces[(opts.testRace - 1) * 2]);
            testEntities.push_back(allRaces[(opts.testRace - 1) * 2 + 1]);
            printf("  Testing race %d: %s / %s\n", opts.testRace,
                   testEntities[0].label, testEntities[1].label);
        } else {
            for (const auto& r : allRaces) testEntities.push_back(r);
            printf("  Testing all %zu race/gender combos\n", testEntities.size());
        }

        int numEntities = static_cast<int>(testEntities.size());

        // Clear any model data from the generic preload (which used variant 0)
        // so we can reload with the requested variant.
        data->raceModelLoader->clearCache();

        // Preload composite data for all genders being tested
        printf("  Preloading composite data (variant %d)...\n", opts.testVariant);
        for (int i = 0; i < numEntities; ++i) {
            data->raceModelLoader->preloadModelData(
                testEntities[i].raceId, testEntities[i].gender, opts.testVariant);
        }
        data->raceModelLoader->promotePreparedModels();
        data->textureCache->uploadAllPending();

        data->entities.resize(numEntities);
        data->animatedNodes.resize(numEntities, nullptr);

        int loadedCount = 0;
        for (int i = 0; i < numEntities; ++i) {
            auto* mesh = data->raceModelLoader->getMeshForRace(
                testEntities[i].raceId, testEntities[i].gender);
            data->textureCache->uploadAllPending();

            if (!mesh) {
                printf("    %s: NO MESH\n", testEntities[i].label);
                continue;
            }
            data->entities[i].mesh = mesh;

            // Create animated node if requested
            if (opts.animate) {
                auto* node = data->raceModelLoader->createAnimatedNode(
                    testEntities[i].raceId, testEntities[i].gender,
                    data->smgr->getRootSceneNode());
                if (node) {
                    data->animatedNodes[i] = node;
                    node->playAnimation("o01", true);
                    data->textureCache->uploadAllPending();
                    if (node->getInstanceMesh())
                        data->entities[i].mesh = node->getInstanceMesh();
                }
            }

            int bufCount = data->entities[i].mesh->getMeshBufferCount();
            const char* texName = "(none)";
            if (bufCount > 0) {
                auto* tex = data->entities[i].mesh->getMeshBuffer(0)->getMaterial().getTexture(0);
                if (tex) texName = tex->getName().getPath().c_str();
            }
            printf("    %s: %d buffers, tex0='%s'\n", testEntities[i].label, bufCount, texName);
            loadedCount++;
        }
        printf("  Loaded %d/%d entities\n", loadedCount, numEntities);

        // Place entities side by side, spaced apart
        float surfX, surfY, surfZ;
        ZoneBench::findSurfacePosition(*data, surfX, surfY, surfZ);

        float spacing = 20.0f;
        float totalWidth = (numEntities - 1) * spacing;
        float startX = surfX - totalWidth * 0.5f;
        float entityY = surfY + 40.0f;  // 40 units in front of camera

        for (int i = 0; i < numEntities; ++i) {
            if (!data->entities[i].mesh) continue;

            float eqX = startX + i * spacing;
            data->entities[i].eqX = eqX;
            data->entities[i].eqY = entityY;
            data->entities[i].eqZ = surfZ - 6.0f;

            float meshOffsetY = -data->entities[i].mesh->getBoundingBox().MinEdge.Y;
            data->entities[i].position = irr::core::vector3df(
                eqX, data->entities[i].eqZ + meshOffsetY, entityY);

            if (data->animatedNodes[i]) {
                data->animatedNodes[i]->setPosition(data->entities[i].position);
                data->animatedNodes[i]->updateAbsolutePosition();
            }
            data->entities[i].priority = ZoneBench::ZoneBenchData::EntityPriority::Target;

            printf("    %s placed at EQ (%.1f, %.1f, %.1f)\n",
                   testEntities[i].label, eqX, entityY, data->entities[i].eqZ);
        }

        // Camera: behind and looking at entities
        float camEqX = surfX;
        float camEqY = surfY;
        float camEqZ = surfZ;
        float camDirX = 0, camDirY = 1, camDirZ = 0;

        printf("  Camera at EQ (%.1f, %.1f, %.1f) looking +Y toward entities at Y=%.1f\n",
               camEqX, camEqY, camEqZ, entityY);

        // Bright daytime lighting
        if (data->zoneShader) {
            data->zoneShader->setAmbientColor(0.6f, 0.6f, 0.6f);
            data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
            data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
            data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
            data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);
            data->zoneShader->setNumPointLights(0);
        }

        // Render loop — runs until --frames count
        int testFrames = opts.frames;
        printf("  Rendering %d frames (%.1f sec at 30fps)...\n",
               testFrames, testFrames / 30.0f);

        irr::core::matrix4 identityMat;
        for (int frame = 0; frame < testFrames; ++frame) {
            setupCamera(*data, camEqX, camEqY, camEqZ, camDirX, camDirY, camDirZ);
            data->driver->beginScene(true, true, irr::video::SColor(255, 80, 80, 120));
            if (data->zoneShader) data->zoneShader->beginFrame();

            // Animate
            if (opts.animate) {
                irr::u32 simTimeMs = static_cast<irr::u32>(frame * 33.3f);
                for (int i = 0; i < numEntities; ++i) {
                    if (!data->animatedNodes[i]) continue;
                    data->animatedNodes[i]->OnAnimate(simTimeMs);
                    auto* instMesh = data->animatedNodes[i]->getInstanceMesh();
                    if (instMesh) data->entities[i].mesh = instMesh;
                }
            }

            // Draw each entity (composite = 1 buffer, multi-buffer = merge with first mat)
            for (int i = 0; i < numEntities; ++i) {
                auto* mesh = data->entities[i].mesh;
                if (!mesh) continue;

                std::vector<irr::video::S3DVertex> verts;
                std::vector<irr::u16> indices;
                irr::video::SMaterial mat;
                bool hasMat = false;

                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    auto* buf = mesh->getMeshBuffer(b);
                    if (!buf || buf->getVertexCount() == 0) continue;
                    if (!hasMat) { mat = buf->getMaterial(); hasMat = true; }

                    irr::u32 base = static_cast<irr::u32>(verts.size());
                    if (base + buf->getVertexCount() > 65535) break;

                    auto* sv = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                    for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                        irr::video::S3DVertex vert = sv[v];
                        vert.Pos += data->entities[i].position;
                        verts.push_back(vert);
                    }
                    auto* si = buf->getIndices();
                    for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                        indices.push_back(static_cast<irr::u16>(si[idx] + base));
                }

                if (!indices.empty() && hasMat) {
                    data->driver->setTransform(irr::video::ETS_WORLD, identityMat);
                    data->driver->setMaterial(mat);
                    data->driver->drawVertexPrimitiveList(
                        verts.data(), verts.size(),
                        indices.data(), indices.size() / 3,
                        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                        irr::video::EIT_16BIT);
                }
            }

            if (frame == testFrames - 1 && !opts.dumpPngPath.empty()) {
                irr::video::IImage* shot = data->driver->createScreenShot();
                if (shot) {
                    data->driver->writeImageToFile(shot, opts.dumpPngPath.c_str());
                    shot->drop();
                }
            }
            data->driver->endScene();
        }

        printf("Composite entity test complete (%d frames rendered).\n", testFrames);
        return 0;
    }

    // ── Find a good surface-level position ──
    // T242: ranked candidates -- the first is the classic "surface position"
    // (largest surface-level region); the rest are navmesh-seed fallbacks.
    auto surfaceCandidates = ZoneBench::findSurfaceCandidates(*data, 16);
    float surfX = surfaceCandidates.front().x;
    float surfY = surfaceCandidates.front().y;
    float surfZ = surfaceCandidates.front().z;
    printf("  Surface position: (%.1f, %.1f, %.1f) [%d polys in region]\n",
           surfX, surfY, surfZ, surfaceCandidates.front().polys);

    // ── Set up camera path ──
    ZoneBench::CameraPath camera;
    std::vector<PathFilePose> loadedPath;   // Q01: populated only in --path-file replay
    bool usingPathFile = !opts.pathFile.empty();

    if (usingPathFile) {
        // Q01: replay -- replaces navmesh/orbit path generation entirely.
        if (!loadPathFile(opts.pathFile, loadedPath)) {
            return 1;
        }
        if (opts.frames > static_cast<int>(loadedPath.size())) {
            fprintf(stderr,
                    "ERROR: --frames %d exceeds --path-file '%s' length (%zu poses)\n",
                    opts.frames, opts.pathFile.c_str(), loadedPath.size());
            return 1;
        }
        if (opts.frames < static_cast<int>(loadedPath.size())) {
            fprintf(stderr,
                    "WARNING: --frames %d < --path-file '%s' length (%zu poses); "
                    "replaying prefix (first %d poses)\n",
                    opts.frames, opts.pathFile.c_str(), loadedPath.size(), opts.frames);
        }
        printf("Replaying camera path from %s (%d of %zu poses)\n",
               opts.pathFile.c_str(), opts.frames, loadedPath.size());
    } else {
        // Try navmesh first
        bool hasNavPath = false;
        if (!opts.navmeshPath.empty()) {
            std::string navFile = opts.navmeshPath;
            if (!navFile.empty() && navFile.back() != '/') navFile += '/';
            navFile += opts.zoneName + ".nav";
            std::vector<glm::vec3> seeds;
            for (const auto& c : surfaceCandidates) seeds.emplace_back(c.x, c.y, c.z);
            hasNavPath = camera.generateFromNavmesh(navFile, opts.navWaypoints, opts.seed, seeds);
        }

        // Fallback to orbit at surface level
        if (!hasNavPath) {
            float cx, cy, cz;
            float extent = std::max(data->boundsMaxX - data->boundsMinX,
                                    data->boundsMaxY - data->boundsMinY);
            float radius = (opts.orbitRadius > 0.0f) ? opts.orbitRadius : extent * 0.3f;  // Orbit at 30% of zone extent

            if (opts.orbitAnchorSurface) {
                // H02: anchor to the densest surface-level region instead of the
                // raw bounding-box center — on tall/uneven zones (caves, tiered
                // outdoor terrain) the bbox-center + fixed-height orbit can put
                // the camera in empty sky or embedded rock for the whole path.
                cx = surfX;
                cy = surfY;
                cz = surfZ + opts.orbitHeightOffset;
            } else {
                cx = (data->boundsMinX + data->boundsMaxX) * 0.5f;
                cy = (data->boundsMinY + data->boundsMaxY) * 0.5f;
                cz = surfZ;
            }
            // Q01 (deferred.md "Benchmark camera path is not reproducible
            // across builds" / P06 review): this fallback used to be silent
            // unless you already knew to look for it. Make it loud -- name
            // the flag that avoids it.
            fprintf(stderr,
                    "WARNING: no navmesh path available (opts.navmeshPath=%s%s); "
                    "falling back to a synthetic ORBIT camera path, not the real "
                    "navmesh-driven path. Pass --navmesh-path /mnt/projects/maps/nav "
                    "(or wherever the zone's .nav file lives) for the real moving path.\n",
                    opts.navmeshPath.empty() ? "<empty>" : opts.navmeshPath.c_str(),
                    opts.navmeshPath.empty() ? "" : ", load failed");
            printf("Using orbit fallback: center=(%.0f,%.0f,%.0f) radius=%.0f%s\n",
                   cx, cy, cz, radius, opts.orbitAnchorSurface ? " [surface-anchored]" : "");
            camera.generateOrbit(cx, cy, cz, radius, 10.0f);
        }

        if (camera.empty()) {
            fprintf(stderr, "ERROR: Failed to generate camera path\n");
            return 1;
        }
    }

    // ── Train mode: override the camera BEFORE the trace is resolved ──
    // (must happen before the Q01 trace build below, since the trace reads
    // whatever `camera` ends up being). Unsupported combined with
    // --path-file: the file already fixed the trajectory, so there is
    // nothing left for trainMode to override -- reject that combination
    // explicitly rather than silently ignoring one of the two flags.
    if (opts.trainMode && !data->entities.empty()) {
        if (usingPathFile) {
            fprintf(stderr, "ERROR: --train is not supported with --path-file\n");
            return 1;
        }
        glm::vec3 trainPos(surfX, surfY, surfZ);
        ZoneBench::placeEntitiesTrain(*data, trainPos);
        camera = ZoneBench::CameraPath();
        camera.generateApproach(surfX, surfY, surfZ, 250.0f, 6.0f);
        printf("Train mode: %d entities at (%.0f,%.0f,%.0f), approach from 250 units\n",
               opts.entityCount, surfX, surfY, surfZ);
    }

    // ── Q01: resolve the per-frame trace (warmup + measured) ──
    // See the "Q01: moving camera-path record/replay" comment block above
    // main() for why this exists: one array, built once, that both entity
    // placement and the render loop read from, so record and replay share
    // exactly one notion of "what pose does frame N have". Not used for
    // --camera-pose/--pose-search/--synthetic-draws/--tree-test, which
    // return earlier or don't touch the moving path.
    int totalFrames = opts.warmup + opts.frames;
    std::vector<TracePose> fullTrace;
    fullTrace.reserve(totalFrames);
    if (usingPathFile) {
        TracePose hold;
        hold.pos = glm::vec3(loadedPath[0].x, loadedPath[0].y, loadedPath[0].z);
        hold.heading = loadedPath[0].heading;
        hold.pitch = loadedPath[0].pitch;
        poseToDirection(hold.heading, hold.pitch, hold.dir.x, hold.dir.y, hold.dir.z);
        // Q01 design point A4: the path file has no warmup poses (it only
        // records rendered MEASURED frames), so warmup holds pose[0]. Warmup
        // frames are never measured/dumped, so this only needs to prime GPU
        // state/caches, not reproduce a specific trajectory.
        for (int i = 0; i < opts.warmup; ++i) fullTrace.push_back(hold);
        for (int i = 0; i < opts.frames; ++i) {
            const auto& lp = loadedPath[i];
            TracePose tp;
            tp.pos = glm::vec3(lp.x, lp.y, lp.z);
            tp.heading = lp.heading;
            tp.pitch = lp.pitch;
            poseToDirection(tp.heading, tp.pitch, tp.dir.x, tp.dir.y, tp.dir.z);
            fullTrace.push_back(tp);
        }
    } else {
        float dt = 1.0f / 30.0f;
        float pathDist = 0.0f;
        for (int f = 0; f < totalFrames; ++f) {
            pathDist += opts.speed * dt;
            TracePose tp;
            tp.pos = camera.getPositionAt(pathDist);
            tp.dir = camera.getDirectionAt(pathDist);
            // Same convention as the CSV columns: heading 0=North/+Y, CW; pitch positive=up.
            tp.heading = atan2f(tp.dir.x, tp.dir.y) * 180.0f / 3.14159265f;
            if (tp.heading < 0) tp.heading += 360.0f;
            float horizLen = sqrtf(tp.dir.x * tp.dir.x + tp.dir.y * tp.dir.y);
            tp.pitch = atan2f(tp.dir.z, horizLen) * 180.0f / 3.14159265f;
            fullTrace.push_back(tp);
        }
    }

    if (!opts.dumpPathFile.empty()) {
        std::vector<TracePose> measured(fullTrace.begin() + opts.warmup, fullTrace.end());
        writePathFile(opts.dumpPathFile, opts, measured);
    }

    // ── Place entities along path ──
    // (trainMode already placed its entities above, before the trace build.)
    if (!data->entities.empty() && !opts.trainMode) {
        // Q01: sample from the resolved MEASURED-frame trace (what will
        // actually be rendered / what a --path-file replay has available),
        // not the raw navmesh polyline's own arc length -- the latter has no
        // equivalent once the navmesh isn't available (replay). This changes
        // exactly where entities land relative to pre-Q01 runs (see design
        // point A3), but is the only way replay can reproduce placement
        // without the navmesh.
        std::vector<glm::vec3> pathPositions;
        pathPositions.reserve(opts.frames);
        for (int i = 0; i < opts.frames; ++i) pathPositions.push_back(fullTrace[opts.warmup + i].pos);
        ZoneBench::placeEntitiesAlongPath(*data, pathPositions);
    }

    // ── Configure adaptive LOD budget ──
    data->adaptiveLodConfig.maxEntitiesPerScene = opts.entityBudget;

    // ── Configure adaptive lighting ──
    data->adaptiveLighting = opts.adaptiveLighting;
    data->perPixelEntityBudget = opts.perPixelBudget;
    data->perPixelZoneBudget = opts.perPixelZoneBudget;
    data->zonePPAreaMode = opts.zonePPAreaMode;
    data->zonePPAreaBudgetFactor = opts.zonePPAreaBudgetFactor;
    data->zonePPAreaFloor = opts.zonePPAreaFloor;
    data->zonePPRadiusMode = opts.zonePPRadiusMode;
    data->zonePPRadiusMargin = opts.zonePPRadiusMargin;
    if (data->zoneShader) {
        data->materialPerPixelSolid = data->zoneShader->getMaterialTypeSolid();
        data->materialPerPixelAlpha = data->zoneShader->getMaterialTypeAlphaTest();
        data->materialLWSolid = data->zoneShader->getMaterialTypeLWSolid();
        data->materialLWAlpha = data->zoneShader->getMaterialTypeLWAlphaTest();
        if (opts.adaptiveLighting) {
            printf("Adaptive lighting: entity PP budget=%d, zone PP budget=%d\n",
                   opts.perPixelBudget, opts.perPixelZoneBudget);
        }
    }

    // ── Set up lighting ──
    setupLighting(*data, opts);

    // ── H02b: interesting-view search (opt-in, bless-time diagnostic only) ──
    // Never used by the CI gate; run manually to find candidate poses, then
    // hard-code the winners as --camera-pose flags in the manifest.
    if (opts.poseSearch > 0) {
        printf("\n=== Pose search: %d anchors x %d headings ===\n",
               opts.poseSearch, opts.poseSearchHeadings);

        // Candidate anchor points: region bounding-box centers/floors, spread
        // evenly across the (already sorted-by-key) region map so candidates
        // are guaranteed to be near real loaded geometry, not empty space.
        std::vector<const ZoneBench::RegionData*> regionList;
        regionList.reserve(data->regions.size());
        for (auto& [idx, rd] : data->regions) regionList.push_back(&rd);

        int n = static_cast<int>(regionList.size());
        int take = std::min(opts.poseSearch, n);
        if (take <= 0) {
            fprintf(stderr, "ERROR: no regions loaded, cannot pose-search\n");
            return 1;
        }

        struct Candidate {
            float x, y, z, heading, pitch;
            int draws = 0;
            size_t distinctColors = 0;
            double skyFraction = 1.0;
            double score = 0.0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(take * opts.poseSearchHeadings);

        std::vector<ZoneBench::DrawEntry> drawEntries;
        drawEntries.reserve(2048);
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

        FILE* csvOut = nullptr;
        if (!opts.poseSearchCsv.empty()) {
            csvOut = fopen(opts.poseSearchCsv.c_str(), "w");
            if (csvOut) fprintf(csvOut, "x,y,z,heading,pitch,draws,distinct_colors,sky_fraction,score\n");
        }

        for (int i = 0; i < take; ++i) {
            int ridx = i * n / take;
            const ZoneBench::RegionData* rd = regionList[ridx];
            float cx = (rd->eqMinX + rd->eqMaxX) * 0.5f;
            float cy = (rd->eqMinY + rd->eqMaxY) * 0.5f;
            // Anchor near the region's floor (bbox min Z) rather than its
            // center -- a good approximation of "standing inside this chunk
            // of geometry" for both open outdoor regions and enclosed rooms.
            float cz = rd->eqMinZ + opts.poseSearchEyeHeight;

            for (int h = 0; h < opts.poseSearchHeadings; ++h) {
                float heading = 360.0f * h / opts.poseSearchHeadings;
                float pitch = opts.poseSearchPitch;

                float dirX, dirY, dirZ;
                poseToDirection(heading, pitch, dirX, dirY, dirZ);
                setupCamera(*data, cx, cy, cz, dirX, dirY, dirZ);
                if (data->zoneShader) data->zoneShader->beginFrame();
                data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
                if (data->zoneShader) data->zoneShader->beginFrame();

                ZoneBench::CameraState camState{cx, cy, cz, dirX, dirY, dirZ, fov, aspect};
                int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
                drawEntries.clear();
                ZoneBench::collectDrawEntries(
                    *data, camState,
                    opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                    opts.frontToBack, opts.maxDraws, opts.maxPolys,
                    drawEntries, regionsVisible, regionsDrawn, regionsOccluded);
                if (!data->objects.empty()) {
                    int objDraws = 0, objPolys = 0;
                    ZoneBench::collectObjectDrawEntries(
                        *data, camState, opts.objectRenderDist,
                        opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                        drawEntries, objDraws, objPolys);
                }
                renderDrawEntries(*data, drawEntries, opts.depthTest);
#ifdef EQT_HAS_GLES2
                glFinish();
#endif
                Candidate c;
                c.x = cx; c.y = cy; c.z = cz; c.heading = heading; c.pitch = pitch;
                c.draws = static_cast<int>(drawEntries.size());
                computeFrameStats(*data, 40, 40, 60, c.distinctColors, c.skyFraction);
                double nonSkyFraction = 1.0 - c.skyFraction;
                c.score = static_cast<double>(c.draws) *
                          static_cast<double>(c.distinctColors) * nonSkyFraction;

                data->driver->endScene();

                if (csvOut) {
                    fprintf(csvOut, "%.1f,%.1f,%.1f,%.1f,%.1f,%d,%zu,%.4f,%.1f\n",
                            c.x, c.y, c.z, c.heading, c.pitch, c.draws,
                            c.distinctColors, c.skyFraction, c.score);
                }
                candidates.push_back(c);
            }
        }
        if (csvOut) fclose(csvOut);

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int topK = std::min<int>(20, static_cast<int>(candidates.size()));
        printf("\nTop %d candidate poses (by draws * distinctColors * nonSkyFraction):\n", topK);
        printf("  %10s %10s %10s %8s %6s %6s %8s %8s %10s\n",
               "x", "y", "z", "heading", "pitch", "draws", "colors", "sky%", "score");
        for (int i = 0; i < topK; ++i) {
            const auto& c = candidates[i];
            printf("  %10.1f %10.1f %10.1f %8.1f %6.1f %6d %8zu %7.1f%% %10.1f  --camera-pose %.1f,%.1f,%.1f,%.1f,%.1f\n",
                   c.x, c.y, c.z, c.heading, c.pitch, c.draws, c.distinctColors,
                   c.skyFraction * 100.0, c.score, c.x, c.y, c.z, c.heading, c.pitch);
        }
        return 0;
    }

    // ── Run synthetic test if requested ──
    if (opts.syntheticDraws) {
        runSyntheticDrawTest(*data, opts.frames);
        return 0;
    }

    // ── P09 A-gate: cross-region merge primitive probe ──
    if (opts.mergeProbe) {
        ZoneBench::runMergeProbeTest(*data, opts.frames, opts.mergeProbeCsv.c_str());
        return 0;
    }

    ZoneBench::MetricsCollector metrics;
    metrics.setActualResolution(actualWidth, actualHeight);

    // ── H02b: static multi-pose capture ──
    // When --camera-pose was given (one or more times), skip the moving
    // navmesh/orbit camera path entirely: render exactly one frame per pose,
    // pose index i -> frame index i, which lines up 1:1 with the existing
    // --dump-frames / manifest "designatedFrames" mechanism below with no
    // changes needed to it or to the CI gate script.
    if (!opts.cameraPoses.empty()) {
        // T291: --warmup applies here too -- it used to be silently ignored
        // on this path (only the moving/--path-file loop below honoured it),
        // so the first measured pose absorbed every one-time cost (shader
        // compile/link, first texture upload, first-touch faults): 20.9-25.5
        // ms vs 2.7-5.1 ms for the other two poses, and ALL of the CI gate's
        // p95 variance lived in that one cold frame. Warmup renders the whole
        // pose list opts.warmup extra times first; those passes are never
        // measured, dumped, or written to the CSV, so the 1:1 pose->frame
        // mapping the gate depends on is untouched (golden PNGs and the
        // draws/state/poly metrics verified byte-identical either way).
        // See refactor/T291_visual_gate_cpu_metric.md.
        const int warmupPasses = (opts.warmup > 0) ? opts.warmup : 0;
        printf("\n=== Static pose capture: %zu poses (%d warmup passes) ===\n",
               opts.cameraPoses.size(), warmupPasses);
        metrics.reserve(static_cast<int>(opts.cameraPoses.size()));
        metrics.setDumpPerFrame(opts.dump);

        std::vector<ZoneBench::DrawEntry> drawEntries;
        drawEntries.reserve(2048);
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

        for (int pass = 0; pass <= warmupPasses; ++pass) {
        const bool measuring = (pass == warmupPasses);
        for (size_t i = 0; i < opts.cameraPoses.size(); ++i) {
            const auto& pose = opts.cameraPoses[i];
            float dirX, dirY, dirZ;
            poseToDirection(pose.heading, pose.pitch, dirX, dirY, dirZ);

            setupCamera(*data, pose.x, pose.y, pose.z, dirX, dirY, dirZ);
            if (data->zoneShader) data->zoneShader->beginFrame();
            data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            if (data->zoneShader) data->zoneShader->beginFrame();

            if (measuring) {
                metrics.beginFrame(static_cast<int>(i), pose.x, pose.y, pose.z,
                                   pose.heading, pose.pitch);
            }

            ZoneBench::CameraState camState{pose.x, pose.y, pose.z, dirX, dirY, dirZ, fov, aspect};
            int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
            float lightRadius = 0.0f;
            int regionsDemotedRadius = 0;
            drawEntries.clear();
            ZoneBench::collectDrawEntries(
                *data, camState,
                opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                opts.frontToBack, opts.maxDraws, opts.maxPolys,
                drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
                &lightRadius, &regionsDemotedRadius);
            if (measuring) metrics.setRadiusStats(lightRadius, regionsDemotedRadius);
            if (!data->objects.empty()) {
                int objDraws = 0, objPolys = 0;
                ZoneBench::collectObjectDrawEntries(
                    *data, camState, opts.objectRenderDist,
                    opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                    drawEntries, objDraws, objPolys);
            }
            int actualDraws = 0;
            int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                                  nullptr, opts.mergeZoneDraws, &actualDraws,
                                                  opts.bakedUnmergedZoneDraws);
            int polyCount = countPolygons(drawEntries);
            if (measuring) {
                metrics.endDraw(actualDraws, stateChanges, polyCount,
                                regionsVisible, regionsDrawn, regionsOccluded);
            }
            if (measuring && opts.materialCounts) {
                auto counts = countMaterialFamilies(drawEntries, data->zoneShader.get());
                metrics.setMaterialCounts(counts.first, counts.second);
            }

#ifdef EQT_HAS_GLES2
            if (measuring && opts.dumpFilters > 0 && i == 0) {
                dumpFilterState(*data, drawEntries, opts.dumpFilters);
            }
            glFinish();
#endif
            if (measuring) {
                metrics.endGpuSync();
                metrics.endFrame();
            }

            if (measuring && !opts.dumpPngPrefix.empty()) {
                irr::video::IImage* shot = data->driver->createScreenShot();
                std::string path = opts.dumpPngPrefix + "_f" + std::to_string(i) + ".png";
                if (shot) {
                    bool ok = data->driver->writeImageToFile(shot, path.c_str());
                    shot->drop();
                    printf("[dump-frames] %s: %s\n", path.c_str(), ok ? "saved" : "FAILED to save");
                } else {
                    printf("[dump-frames] %s: createScreenShot() FAILED\n", path.c_str());
                }
            }

            data->driver->endScene();
        }
        }

        metrics.printSummary();
        if (!opts.csvFile.empty()) metrics.writeCSV(opts.csvFile);

        if (data->textureCache) {
            BenchMemorySnapshot snap = computeMemorySnapshot(*data, opts.width, opts.height);
            printf("Texture cache: %zu / %zu bytes (%zu textures)\n",
                   snap.textureCacheBytesUsed, snap.textureCacheBytesLimit, snap.textureCacheCount);
            printf("Total tracked: %zu bytes | RSS: %zu bytes | untracked: %zu bytes\n",
                   snap.totalEstimate, snap.processRssBytes, snap.untrackedBytes);
            if (!opts.memoryJsonPath.empty()) {
                writeMemoryJson(opts.memoryJsonPath, opts.zoneName, snap);
            }
        }

        printf("Benchmark complete.\n");
        return 0;
    }

    // ── Benchmark loop ──
    printf("\nStarting benchmark: %d warmup + %d measured frames\n",
           opts.warmup, opts.frames);

    metrics.reserve(opts.frames);
    metrics.setDumpPerFrame(opts.dump);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);

    // P08: draw-composition histogram (--draw-histogram + --dump-frames).
    FILE* histFile = nullptr;
    bool histHeaderWritten = false;
    if (!opts.drawHistogramFile.empty()) {
        histFile = fopen(opts.drawHistogramFile.c_str(), "w");
        if (!histFile) {
            fprintf(stderr, "ERROR: cannot open --draw-histogram file: %s\n",
                    opts.drawHistogramFile.c_str());
        }
    }

    // Q01: totalFrames/fullTrace were resolved above (before entity
    // placement) -- both record (navmesh/orbit) and replay (--path-file)
    // populate the same per-frame trace, so the loop below no longer cares
    // which mode it's in.
    for (int frame = 0; frame < totalFrames; ++frame) {
        bool measuring = (frame >= opts.warmup);

        // Camera pose for this frame, resolved up front (Q01).
        const TracePose& tp = fullTrace[frame];
        glm::vec3 camPos = tp.pos;
        glm::vec3 camDir = tp.dir;

        // Set up camera transforms
        setupCamera(*data, camPos.x, camPos.y, camPos.z,
                     camDir.x, camDir.y, camDir.z);

        // Update shader frame
        if (data->zoneShader) {
            data->zoneShader->beginFrame();
        }

        // Begin frame
        data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
        if (data->zoneShader) data->zoneShader->beginFrame();

        if (measuring) {
            metrics.beginFrame(frame - opts.warmup, camPos.x, camPos.y, camPos.z,
                               tp.heading, tp.pitch);
#ifdef EQT_HAS_GLES2
            if (opts.glCounters) eqtResetGLCallCounters();
#endif
        }

        // Tick entity animations (skeletal skinning)
        // Only animate entities within budget, sorted front-to-back,
        // and only if they pass visibility (render distance + frustum).
        if (opts.animate && !data->animatedNodes.empty()) {
            if (measuring) metrics.beginAnimation();

            // Build distance-sorted index list (front to back)
            struct EntityDist {
                int index;
                float distSq;
            };
            static std::vector<EntityDist> sortedEntities;
            sortedEntities.clear();
            sortedEntities.reserve(data->entities.size());

            float renderDistSq = opts.entityRenderDist * opts.entityRenderDist;
            for (int i = 0; i < static_cast<int>(data->entities.size()); ++i) {
                if (!data->animatedNodes[i]) continue;
                auto& ent = data->entities[i];
                float dx = ent.eqX - camPos.x;
                float dy = ent.eqY - camPos.y;
                float dz = ent.eqZ - camPos.z;
                float dSq = dx*dx + dy*dy + dz*dz;

                // Render distance cull
                if (dSq > renderDistSq) continue;

                // Frustum cull
                if (data->frustumCuller && data->frustumCuller->isEnabled()) {
                    if (!data->frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                        continue;
                }

                sortedEntities.push_back({i, dSq});
            }

            // Sort front to back
            std::sort(sortedEntities.begin(), sortedEntities.end(),
                      [](const EntityDist& a, const EntityDist& b) {
                          return a.distSq < b.distSq;
                      });

            // Animate only up to entity budget
            int animBudget = opts.entityBudget;
            int animCount = std::min(static_cast<int>(sortedEntities.size()), animBudget);
            irr::u32 simTimeMs = static_cast<irr::u32>(frame * 33.3f);

            for (int j = 0; j < animCount; ++j) {
                int i = sortedEntities[j].index;
                data->animatedNodes[i]->OnAnimate(simTimeMs);
            }

            if (measuring) metrics.endAnimation();

            // Update entity mesh pointers to use animated instance meshes
            // (only for entities that were actually animated this frame)
            for (int j = 0; j < animCount; ++j) {
                int i = sortedEntities[j].index;
                auto* instMesh = data->animatedNodes[i]->getInstanceMesh();
                if (instMesh) {
                    data->entities[i].mesh = instMesh;
                }
            }
        }

        // Collect draw entries with visibility culling
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);
        ZoneBench::CameraState camState{camPos.x, camPos.y, camPos.z,
                                         camDir.x, camDir.y, camDir.z,
                                         fov, aspect};
        int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
        float lightRadius = 0.0f;
        int regionsDemotedRadius = 0;
        // P08: phaseCollectCullMs accumulates zone-region collection/culling
        // (from collectDrawEntries' own timing) plus object/entity draw-entry
        // collection below (small relative to zone collection); phaseSortMs
        // is the zone material-key sort only; phaseSubmitMs (set per-branch
        // below) is the setMaterial+drawMeshBuffer loop time.
        float phaseCollectCullMs = 0.0f, phaseSortMs = 0.0f, phaseSubmitMs = 0.0f;
        int measuredFrameIdx = frame - opts.warmup;
        bool wantHistogram = measuring && histFile != nullptr && !opts.dumpFrames.empty() &&
            std::find(opts.dumpFrames.begin(), opts.dumpFrames.end(), measuredFrameIdx)
                != opts.dumpFrames.end();
        std::vector<int> histDrawsPerRegion, histPolysPerDraw;
        ZoneBench::collectDrawEntries(
            *data, camState,
            opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
            opts.frontToBack, opts.maxDraws, opts.maxPolys,
            drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
            &lightRadius, &regionsDemotedRadius,
            &phaseCollectCullMs, &phaseSortMs,
            wantHistogram ? &histDrawsPerRegion : nullptr,
            wantHistogram ? &histPolysPerDraw : nullptr);
        if (measuring) metrics.setRadiusStats(lightRadius, regionsDemotedRadius);
        if (wantHistogram) {
            ZoneBench::DrawHistogram hist;
            hist.frame = measuredFrameIdx;
            hist.regionsVisible = regionsVisible;
            hist.regionsDrawn = regionsDrawn;
            hist.regionsOccluded = regionsOccluded;
            hist.stateChanges = 0;  // zone-only state changes not separately tracked at this point
            hist.drawsPerRegion = histDrawsPerRegion;
            hist.polysPerDraw = histPolysPerDraw;
            ZoneBench::writeDrawHistogramCSV(histFile, hist, !histHeaderWritten);
            histHeaderWritten = true;
            if (opts.dump) ZoneBench::printDrawHistogram(hist);

            // P08 design point 6: occlusion headroom ground truth (uncapped
            // occluder budget vs production's capped pass) at the same
            // pinned poses -- one-shot, printed to stdout for log capture.
            ZoneBench::OcclusionHeadroom headroom = ZoneBench::computeOcclusionHeadroomUncapped(
                *data, camState, opts.pvsCull, opts.frustumCull, regionsOccluded);
            printf("[occlusion-headroom] frame=%d regions_pvs=%d occluded_capped=%d "
                   "occluded_uncapped=%d headroom=%d\n",
                   measuredFrameIdx, headroom.regionsVisiblePVS,
                   headroom.regionsOccludedCapped, headroom.regionsOccludedUncapped,
                   headroom.regionsOccludedUncapped - headroom.regionsOccludedCapped);
        }

        // Collect object draw entries (trees, lamps, etc.)
        int objectDrawCalls = 0, objectPolys = 0;
        if (!data->objects.empty()) {
            const auto objCollectStart = std::chrono::steady_clock::now();
            ZoneBench::collectObjectDrawEntries(
                *data, camState, opts.objectRenderDist,
                opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                drawEntries, objectDrawCalls, objectPolys);
            phaseCollectCullMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - objCollectStart).count();
        }

        // P52: proxy-scope GPU query around the combined zone+entity draw
        // submission below -- see gpu_sync_bench_hooks.h for why this tool
        // can't reproduce the production zone-pass/entity-pass split.
#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncBeginSubmissionPhase();
#endif

        // Collect and render entities
        int entityDrawCalls = 0, entityPolys = 0;
        if (!data->entities.empty()) {
            if (opts.directGLEntities && opts.compositeEntities) {
                // Direct GL + composite: 1 draw per entity via raw GL.
                // P08: out of scope (entity draw path) -- phaseSubmitMs here
                // covers only the zone-geometry renderDrawEntries() call;
                // the direct-GL entity submission below falls into otherMs.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderDirectGLCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.directGLEntities) {
                // Direct GL per-body-part: bypass Irrlicht, same draw count as unbatched.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderDirectGLEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.adaptiveLod) {
                // Adaptive LOD: proactive frame-time prediction with billboard fallback.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                int tier1 = 0, tier2 = 0;
                float entityMs = 0;
                float prevFrameMs = metrics.frames().empty() ? 0 :
                    metrics.frames().back().totalMs;
                entityDrawCalls = ZoneBench::renderAdaptiveLodEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    prevFrameMs, entityPolys, tier1, tier2, entityMs);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.lodEntities) {
                // LOD: composite near + batched billboards far.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                int tier1 = 0, tier2 = 0;
                entityDrawCalls = ZoneBench::renderLodEntities(
                    *data, camState, opts.entityRenderDist, opts.lodTier2Dist,
                    opts.frustumCull, entityPolys, tier1, tier2);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.compositeEntities && !opts.compositeSceneGraph) {
                // Composite: 1 draw per entity (simulates pre-baked composite texture).
                // P08: out of scope (entity draw path), same caveat as above.
                // P08D-RETAKE: --composite-scenegraph skips this branch (see opts struct
                // comment) so real composite meshes fall through to the default entity
                // path below instead of this custom stand-in draw.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.batchEntities) {
                // Batched: merge same-material entity parts into single draws.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderBatchedEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else {
                // Unbatched (P08 canonical config -- default, no entity mode
                // flag set): entity draw entries are appended to the SAME
                // drawEntries vector as zone entries (sorted together), so
                // the single renderDrawEntries() call below submits zone +
                // entity draws combined -- phaseSubmitMs is a true combined
                // submission-phase number here, not zone-only.
                const auto entCollectStart = std::chrono::steady_clock::now();
                int entitiesVisible = 0;
                ZoneBench::collectEntityDrawEntries(
                    *data, camState, opts.entityRenderDist,
                    opts.frustumCull, opts.occlusionCull,
                    drawEntries, entityDrawCalls, entityPolys, entitiesVisible);
                phaseCollectCullMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - entCollectStart).count();
                int actualDraws = 0;
                int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                                      &phaseSubmitMs, opts.mergeZoneDraws, &actualDraws,
                                                      opts.bakedUnmergedZoneDraws);
                int polyCount = countPolygons(drawEntries);

                if (measuring) {
                    metrics.endDraw(actualDraws,
                                   stateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.setEntitiesVisible(entitiesVisible);
                    metrics.finalizePhaseTiming();
                }
            }
        } else {
            // No entities — zone only (P08 canonical isolation config, used
            // to quantify entity draw-call contribution separately).
            int actualDraws = 0;
            int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                                  &phaseSubmitMs, opts.mergeZoneDraws, &actualDraws,
                                                  opts.bakedUnmergedZoneDraws);
            int polyCount = countPolygons(drawEntries);

            if (measuring) {
                metrics.endDraw(actualDraws,
                               stateChanges, polyCount,
                               regionsVisible, regionsDrawn);
                metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                metrics.finalizePhaseTiming();
            }
        }

#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncEndSubmissionPhase();
#endif

        // P38: snapshot driver GL call counters before glFinish() -- they
        // count submission-phase glBindTexture/glTexParameteri calls, not
        // GPU-side work, so must be read before the sync point below.
#ifdef EQT_HAS_GLES2
        if (measuring && opts.glCounters) {
            metrics.setGLCounters(eqtGetGLTexBinds(), eqtGetGLTexParams());
        }
#endif

        // GPU sync. P52: glFinish() is exactly the "CPU blocks on GPU sync"
        // point the plan names; timed only when measuring and the probe is on.
#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncTimedFinish(measuring && opts.gpuSyncProbe, glFinish);
#endif

        if (measuring) {
            metrics.endGpuSync();
            metrics.endFrame();  // Measure before endScene (vsync page flip blocks)
        }

        // H01: dump the last frame as a PNG for the x86/Xvfb verification harness.
        // Must read the framebuffer before endScene()'s swap/page-flip.
        if (!opts.dumpPngPath.empty() && frame == totalFrames - 1) {
            irr::video::IImage* shot = data->driver->createScreenShot();
            if (shot) {
                bool ok = data->driver->writeImageToFile(shot, opts.dumpPngPath.c_str());
                shot->drop();
                printf("[dump-png] %s: %s\n", opts.dumpPngPath.c_str(),
                       ok ? "saved" : "FAILED to save");
            } else {
                printf("[dump-png] %s: createScreenShot() FAILED\n", opts.dumpPngPath.c_str());
            }
        }

        // H02: dump designated measured frames (golden-image CI gate) as
        // PREFIX_f<N>.png. Same read-before-swap requirement as --dump-png.
        if (measuring && !opts.dumpPngPrefix.empty() && !opts.dumpFrames.empty()) {
            int measuredFrame = frame - opts.warmup;
            if (std::find(opts.dumpFrames.begin(), opts.dumpFrames.end(), measuredFrame)
                != opts.dumpFrames.end()) {
                irr::video::IImage* shot = data->driver->createScreenShot();
                std::string path = opts.dumpPngPrefix + "_f" + std::to_string(measuredFrame) + ".png";
                if (shot) {
                    bool ok = data->driver->writeImageToFile(shot, path.c_str());
                    shot->drop();
                    printf("[dump-frames] %s: %s\n", path.c_str(), ok ? "saved" : "FAILED to save");
                } else {
                    printf("[dump-frames] %s: createScreenShot() FAILED\n", path.c_str());
                }
            }
        }

        // P08: time endScene() (buffer swap / page flip) separately -- it
        // happens after totalMs is already stamped (comment above), so this
        // is genuinely new information, not a subset of any existing column.
        // Patches the frame record already pushed by endFrame() above.
        const auto swapStart = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
        // P52: GPUSyncPhase::Swap query + CPU-side stats, see gpu_sync_bench_hooks.h.
        ZoneBench::gpuSyncWrapSwap([&]() { data->driver->endScene(); },
                                    metrics, measuring, opts.gpuSyncProbe);
#else
        data->driver->endScene();
#endif
        if (measuring) {
            float swapMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - swapStart).count();
            metrics.setSwapMsForLastFrame(swapMs);
        }

        // Progress
        if (frame > 0 && frame % 100 == 0) {
            printf("  Frame %d/%d...\n", frame, totalFrames);
        }
    }

    // ── Output results ──
    metrics.printSummary();

    if (!opts.csvFile.empty()) {
        metrics.writeCSV(opts.csvFile);
    }

    if (histFile) {
        fclose(histFile);
        printf("Draw histogram written: %s\n", opts.drawHistogramFile.c_str());
    }

    // H02/T69: report/emit the full per-subsystem memory breakdown for the
    // memory-ceiling CI gate (see the block comment above computeMemorySnapshot()).
    if (data->textureCache) {
        BenchMemorySnapshot snap = computeMemorySnapshot(*data, opts.width, opts.height);
        printf("Texture cache: %zu / %zu bytes (%zu textures)\n",
               snap.textureCacheBytesUsed, snap.textureCacheBytesLimit, snap.textureCacheCount);
        printf("Total tracked: %zu bytes | RSS: %zu bytes | untracked: %zu bytes\n",
               snap.totalEstimate, snap.processRssBytes, snap.untrackedBytes);
        if (!opts.memoryJsonPath.empty()) {
            writeMemoryJson(opts.memoryJsonPath, opts.zoneName, snap);
        }
    }

    printf("Benchmark complete.\n");
    return 0;
}
