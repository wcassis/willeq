// impostor_pixel_compare -- B964 (GitLab #282): mechanical pixel and draw-cost
// evidence for the PRODUCTION impostor port. Everything is built through
// EntityRenderer exactly as the client builds it (createEntity +
// updateMainThreadEntityState, the held_item_candidate_search idiom), with the
// zone shader the client's entities use, and the impostor pass forced:
//   forceImpostorTier(2) -> every entity is its model,
//   forceImpostorTier(1) -> every entity is its impostor (captured by the
//                           production ImpostorCache, drawn by the production
//                           ImpostorBatchNode).
// No model judges a frame; the numbers are per-pixel counts (B952's columns).
//
// Usage:
//   impostor_pixel_compare pixel <outdir> [--races 1,2,3,...] [--dists 40,80,160]
//                          [--size 800x600] [--fog START,END] [--variants 0,1,2]
//                          [--eq-path DIR] [--db FILE] [--crops]
//     variants: 0 = shipped billboard program, 1 = built-in alpha test (no
//     un-premultiply, no fog: B952's billboard), 2 = shipped program, fog off.
//     Writes <outdir>/pixel.csv (+ crops/*.png with --crops).
//   impostor_pixel_compare drawcost <outdir> [--counts 1,50,200] [--size 800x600]
//                          [--frames 60] [--aa N] [--drm] [--eq-path DIR] [--db FILE]
//     N entities 100-160 units in front of the camera, full models vs
//     impostors, glFinish-bracketed frame times. Writes <outdir>/drawcost.csv.

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/impostor/impostor_batch_node.h"
#include "client/graphics/impostor/impostor_cache.h"
#include "client/graphics/zone_shader.h"
#include "client/willeq_database.h"
#include "common/logging.h"

#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace EQT::Graphics;

namespace {

struct Options {
    std::string mode;
    std::string outDir;
    std::string eqPath = "/mnt/projects/EverQuestP1999/";
    std::string dbPath = "data/willeq.db";
    std::vector<int> races = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128};
    std::vector<int> dists = {40, 80, 160};
    std::vector<int> counts = {1, 50, 200};
    std::vector<int> variants = {0, 1, 2};
    irr::u32 width = 800, height = 600;
    float fogStart = 180.0f, fogEnd = 300.0f;  // the orangepi client's 300-unit clip
    int frames = 60;
    int antiAlias = 0;
    bool drm = false;
    bool crops = false;
};

std::vector<int> parseList(const char* s) {
    std::vector<int> out;
    std::string str = s;
    size_t pos = 0;
    while (pos < str.size()) {
        size_t c = str.find(',', pos);
        if (c == std::string::npos) c = str.size();
        out.push_back(std::atoi(str.substr(pos, c - pos).c_str()));
        pos = c + 1;
    }
    return out;
}

bool parse(int argc, char** argv, Options& o) {
    if (argc < 3) return false;
    o.mode = argv[1];
    o.outDir = argv[2];
    for (int i = 3; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!strcmp(argv[i], "--races")) o.races = parseList(next());
        else if (!strcmp(argv[i], "--dists")) o.dists = parseList(next());
        else if (!strcmp(argv[i], "--counts")) o.counts = parseList(next());
        else if (!strcmp(argv[i], "--variants")) o.variants = parseList(next());
        else if (!strcmp(argv[i], "--eq-path")) { o.eqPath = next(); if (o.eqPath.back() != '/') o.eqPath += '/'; }
        else if (!strcmp(argv[i], "--db")) o.dbPath = next();
        else if (!strcmp(argv[i], "--frames")) o.frames = std::atoi(next());
        else if (!strcmp(argv[i], "--crops")) o.crops = true;
        else if (!strcmp(argv[i], "--aa")) o.antiAlias = std::atoi(next());
        else if (!strcmp(argv[i], "--drm")) o.drm = true;
        else if (!strcmp(argv[i], "--size")) sscanf(next(), "%ux%u", &o.width, &o.height);
        else if (!strcmp(argv[i], "--fog")) sscanf(next(), "%f,%f", &o.fogStart, &o.fogEnd);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return false; }
    }
    return o.mode == "pixel" || o.mode == "drawcost";
}

void finishGpu() {
#ifdef EQT_HAS_GLES2
    glFinish();
#endif
}

struct Scene {
    irr::IrrlichtDevice* device = nullptr;
    irr::video::IVideoDriver* driver = nullptr;
    irr::scene::ISceneManager* smgr = nullptr;
    irr::scene::ICameraSceneNode* camera = nullptr;
    std::unique_ptr<ZoneShaderManager> shader;
    std::unique_ptr<ConstrainedTextureCache> cache;
    std::unique_ptr<EntityRenderer> ents;
    irr::video::SColor clear{255, 40, 40, 60};
};

void shaderFrame(Scene& s, const Options& o) {
    if (!s.shader) return;
    s.shader->beginFrame();
    s.shader->setAmbientColor(0.55f, 0.55f, 0.55f);
    s.shader->setSunColor(0.6f, 0.6f, 0.54f);
    s.shader->setSunDirection(0.5f, -1.0f, 0.5f);
    s.shader->setTintColor(1.0f, 1.0f, 1.0f);
    s.shader->setFog(o.fogStart, o.fogEnd, 0.5f, 0.5f, 0.63f, 1.0f);
}

// One frame: the entity pass (impostor creation happens here, before
// beginScene, exactly as in the client), then the scene.
double renderFrame(Scene& s, const Options& o, irr::u32 timeMs) {
    shaderFrame(s, o);
    finishGpu();
    const auto t0 = std::chrono::steady_clock::now();
    s.ents->updateMainThreadEntityState(0.016f);
    s.driver->beginScene(true, true, s.clear);
    s.device->getTimer()->setTime(timeMs);
    s.smgr->drawAll();
    s.driver->endScene();
    finishGpu();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

struct Shot {
    std::vector<irr::video::SColor> px;
    irr::u32 w = 0, h = 0;
};

Shot grab(Scene& s) {
    Shot shot;
    irr::video::IImage* img = s.driver->createScreenShot();
    if (!img) return shot;
    shot.w = img->getDimension().Width;
    shot.h = img->getDimension().Height;
    shot.px.resize(static_cast<size_t>(shot.w) * shot.h);
    for (irr::u32 y = 0; y < shot.h; ++y)
        for (irr::u32 x = 0; x < shot.w; ++x) shot.px[y * shot.w + x] = img->getPixel(x, y);
    img->drop();
    return shot;
}

int chanDiff(irr::video::SColor a, irr::video::SColor b) {
    return std::max({std::abs(int(a.getRed()) - int(b.getRed())), std::abs(int(a.getGreen()) - int(b.getGreen())),
                     std::abs(int(a.getBlue()) - int(b.getBlue()))});
}
int luma(irr::video::SColor c) { return (299 * c.getRed() + 587 * c.getGreen() + 114 * c.getBlue()) / 1000; }

struct PixelRow {
    int fullPx = 0, impPx = 0, overlap = 0, silMismatch = 0, diff32 = 0, maxDiff = 0, darkEdge = 0, heightPx = 0;
    int interiorMed = 0, interiorP90 = 0;
};

PixelRow compare(const Shot& bg, const Shot& full, const Shot& imp) {
    PixelRow r;
    std::vector<int> interior;
    int ymin = 1 << 30, ymax = -1;
    for (size_t i = 0; i < bg.px.size() && i < full.px.size() && i < imp.px.size(); ++i) {
        const bool inF = chanDiff(full.px[i], bg.px[i]) > 0;
        const bool inI = chanDiff(imp.px[i], bg.px[i]) > 0;
        if (inF) {
            ++r.fullPx;
            const int y = static_cast<int>(i / bg.w);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
        if (inI) ++r.impPx;
        if (inF && inI) {
            ++r.overlap;
            interior.push_back(chanDiff(full.px[i], imp.px[i]));
        }
        if (inF != inI) ++r.silMismatch;
        const int d = chanDiff(full.px[i], imp.px[i]);
        if (d > 32) ++r.diff32;
        r.maxDiff = std::max(r.maxDiff, d);
        // #274's fringe: an impostor pixel near black where the model is not.
        if (inI && luma(imp.px[i]) < 24 && luma(full.px[i]) >= 24) ++r.darkEdge;
    }
    r.heightPx = ymax >= ymin ? ymax - ymin + 1 : 0;
    if (!interior.empty()) {
        std::sort(interior.begin(), interior.end());
        r.interiorMed = interior[interior.size() / 2];
        r.interiorP90 = interior[std::min(interior.size() - 1, interior.size() * 9 / 10)];
    }
    return r;
}

void writeCrop(Scene& s, const std::string& path, const Shot& full, const Shot& imp) {
    // Bounding box of everything that differs from either frame's corner pixel, padded.
    int x0 = full.w, y0 = full.h, x1 = -1, y1 = -1;
    const irr::video::SColor bg = full.px.empty() ? irr::video::SColor() : full.px[0];
    for (irr::u32 y = 0; y < full.h; ++y)
        for (irr::u32 x = 0; x < full.w; ++x) {
            const size_t i = y * full.w + x;
            if (chanDiff(full.px[i], bg) > 0 || chanDiff(imp.px[i], bg) > 0) {
                x0 = std::min<int>(x0, x); x1 = std::max<int>(x1, x);
                y0 = std::min<int>(y0, y); y1 = std::max<int>(y1, y);
            }
        }
    if (x1 < 0) return;
    x0 = std::max(0, x0 - 4); y0 = std::max(0, y0 - 4);
    x1 = std::min<int>(full.w - 1, x1 + 4); y1 = std::min<int>(full.h - 1, y1 + 4);
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1, scale = 4;
    irr::video::IImage* out = s.driver->createImage(irr::video::ECF_A8R8G8B8,
                                                    irr::core::dimension2d<irr::u32>(cw * scale * 3, ch * scale));
    for (int y = 0; y < ch * scale; ++y)
        for (int x = 0; x < cw * scale; ++x) {
            const size_t i = (y0 + y / scale) * full.w + (x0 + x / scale);
            const int d = std::min(255, chanDiff(full.px[i], imp.px[i]) * 4);
            out->setPixel(x, y, full.px[i]);
            out->setPixel(x + cw * scale, y, imp.px[i]);
            out->setPixel(x + 2 * cw * scale, y, irr::video::SColor(255, d, d, d));
        }
    s.driver->writeImageToFile(out, path.c_str());
    out->drop();
}

bool setup(Scene& s, const Options& o, EQT::WilleqDatabase& db) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(o.width, o.height);
    params.Fullscreen = true;
    params.AntiAlias = static_cast<irr::u8>(o.antiAlias);  // the orangepi preset asks for 4
    params.Vsync = false;                                  // frame times, not the display rate
    params.Stencilbuffer = true;                           // as the client on the orangepi preset
#ifdef EQT_HAS_DRM
    if (o.drm) params.DeviceType = irr::EIDT_FRAMEBUFFER;  // the Pi: DRM/KMS
#else
    (void)o.drm;
#endif
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    s.device = irr::createDeviceEx(params);
    if (!s.device) { fprintf(stderr, "ERROR: no device\n"); return false; }
    s.driver = s.device->getVideoDriver();
    s.smgr = s.device->getSceneManager();
    if (!db.open(o.dbPath)) { fprintf(stderr, "ERROR: cannot open %s\n", o.dbPath.c_str()); return false; }
    EQT::WilleqDatabase::setGlobalInstance(&db);
    if (auto* gpu = s.driver->getGPUProgrammingServices()) {
        s.shader = std::make_unique<ZoneShaderManager>(s.driver, gpu);
        if (!s.shader->isAvailable()) s.shader.reset();
    }
    s.cache = std::make_unique<ConstrainedTextureCache>(ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT));
    s.cache->setDriver(s.driver);
    s.ents = std::make_unique<EntityRenderer>(s.smgr, s.driver, s.device->getFileSystem());
    s.ents->setClientPath(o.eqPath);
    s.ents->setConstrainedTextureCache(s.cache.get());
    if (s.shader)
        s.ents->setShaderMaterialTypes(s.shader->getActiveSolid(), s.shader->getActiveAlphaTest(),
                                       s.shader->getActiveAlphaTestRGBA());
    if (!s.ents->loadGlobalCharacters()) { fprintf(stderr, "ERROR: global_chr.s3d\n"); return false; }
    s.ents->loadEquipmentModels();
    // The blessed budget; tier distance 0 = the policy floor is off (forced tiers decide).
    s.ents->enableImpostorsForTesting(20u * 1024u * 1024u, 0.0f);
    s.ents->setImpostorFrameInputs(nullptr, 0, {}, false);
    s.camera = s.smgr->addCameraSceneNode();
    s.camera->setFOV(irr::core::PI / 3.0f);
    s.camera->setAspectRatio(static_cast<float>(o.width) / static_cast<float>(o.height));
    s.camera->setNearValue(0.5f);
    s.camera->setFarValue(1000.0f);
    s.camera->setPosition(irr::core::vector3df(0.0f, 3.0f, 0.0f));
    s.camera->setTarget(irr::core::vector3df(0.0f, 3.0f, 100.0f));
    s.driver->setFog(irr::video::SColor(255, 128, 128, 160), irr::video::EFT_FOG_LINEAR, o.fogStart, o.fogEnd,
                     0.0f, true, false);
    s.driver->beginScene(true, true, s.clear);
    s.driver->endScene();
    return true;
}

int runPixel(Scene& s, const Options& o) {
    FILE* csv = fopen((o.outDir + "/pixel.csv").c_str(), "w");
    if (!csv) return 1;
    fprintf(csv, "race,gender,dist,variant,full_px,imp_px,overlap_px,sil_mismatch_px,diff32_px,interior_med,"
                 "interior_p90,max_diff,dark_edge_px,model_height_px\n");
    if (o.crops) mkdir((o.outDir + "/crops").c_str(), 0755);
    irr::u32 t = 1000;
    const uint16_t kSpawn = 7;
    for (int race : o.races) {
        for (int gender = 0; gender <= 1; ++gender) {
            for (int dist : o.dists) {
                s.ents->clearEntities();
                // EQ (x, y, z) = Irrlicht (x, z, y): straight ahead of the camera.
                if (!s.ents->createEntity(kSpawn, static_cast<uint16_t>(race), "B964", 0.0f,
                                          static_cast<float>(dist), 0.0f, 0.0f, false,
                                          static_cast<uint8_t>(gender), EntityAppearance(), true)) {
                    fprintf(stderr, "race %d gender %d: no model\n", race, gender);
                    continue;
                }
                s.ents->setEntityAnimation(kSpawn, "o01", true);
                // No name tag: it would sit in both footprints and in the height.
                s.ents->setNameTagsVisible(false);
                // Background: the entity hidden.
                auto& vis = const_cast<EntityVisual&>(s.ents->getEntities().at(kSpawn));
                vis.sceneNode->setVisible(false);
                s.ents->forceImpostorTier(2);
                renderFrame(s, o, t += 16);
                const Shot bg = grab(s);
                vis.sceneNode->setVisible(true);
                // Hold the pose: the impostor captures the pose of its frame.
                auto* anim = vis.animatedNode;
                anim->setAnimationSpeed(0.0f);
                renderFrame(s, o, t += 16);
                renderFrame(s, o, t += 16);
                const Shot full = grab(s);
                for (int variant : o.variants) {
                    const ImpostorCache* ic = s.ents->impostorCache();
                    if (ic && ic->batchNode()) ic->batchNode()->setDiagnosticMode(variant);
                    s.ents->forceImpostorTier(1);
                    renderFrame(s, o, t += 16);  // capture + first billboard
                    renderFrame(s, o, t += 16);
                    if (!s.ents->isEntityDrawnAsImpostor(kSpawn)) {
                        fprintf(stderr, "race %d gender %d dist %d: impostor not drawn\n", race, gender, dist);
                        continue;
                    }
                    const Shot imp = grab(s);
                    const PixelRow r = compare(bg, full, imp);
                    fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", race, gender, dist, variant,
                            r.fullPx, r.impPx, r.overlap, r.silMismatch, r.diff32, r.interiorMed, r.interiorP90,
                            r.maxDiff, r.darkEdge, r.heightPx);
                    if (o.crops) {
                        char name[256];
                        snprintf(name, sizeof(name), "%s/crops/r%03d_g%d_d%03d_v%d.png", o.outDir.c_str(), race,
                                 gender, dist, variant);
                        writeCrop(s, name, full, imp);
                    }
                    s.ents->forceImpostorTier(2);
                    renderFrame(s, o, t += 16);
                }
                fflush(csv);
            }
        }
    }
    fclose(csv);
    printf("[impostor-pixel] wrote %s/pixel.csv\n", o.outDir.c_str());
    return 0;
}

float pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    size_t i = static_cast<size_t>(std::ceil(p * v.size()));
    if (i > 0) --i;
    return static_cast<float>(v[std::min(i, v.size() - 1)]);
}

int runDrawCost(Scene& s, const Options& o) {
    FILE* csv = fopen((o.outDir + "/drawcost.csv").c_str(), "w");
    if (!csv) return 1;
    fprintf(csv, "n,tier,frame_p50_ms,frame_p95_ms,frame_max_ms,drawn_impostor,drawn_full,billboard_draws,pages,"
                 "row_bytes,create_frames,units,unit_ms_avg,unit_ms_max,frame_unit_ms_max\n");
    irr::u32 t = 1000;
    for (int n : o.counts) {
        s.ents->clearEntities();
        int made = 0;
        for (int i = 0; i < n; ++i) {
            const int race = o.races[i % o.races.size()];
            // Rows 100-160 units ahead, spread across +-40 degrees.
            const float d = 100.0f + 60.0f * ((i * 37) % 100) / 100.0f;
            const float a = (-40.0f + 80.0f * ((i * 53) % 100) / 100.0f) * irr::core::DEGTORAD;
            if (s.ents->createEntity(static_cast<uint16_t>(10 + i), static_cast<uint16_t>(race), "B964",
                                     d * std::sin(a), d * std::cos(a), 0.0f, 0.0f, false,
                                     static_cast<uint8_t>(i % 2), EntityAppearance(), true)) {
                s.ents->setEntityAnimation(static_cast<uint16_t>(10 + i), "o01", true);
                ++made;
            }
        }
        for (int tier : {2, 1}) {
            s.ents->forceImpostorTier(tier);
            int createFrames = 0;
            if (tier == 1) {
                // Creation: the scheduled units, a few per frame, until all are drawn.
                for (int f = 0; f < 400 && s.ents->impostorFrameStats().tierImpostor < made; ++f) {
                    renderFrame(s, o, t += 33);
                    ++createFrames;
                }
            }
            for (int f = 0; f < 10; ++f) renderFrame(s, o, t += 33);
            std::vector<double> ms;
            for (int f = 0; f < o.frames; ++f) ms.push_back(renderFrame(s, o, t += 33));
            const auto& fs = s.ents->impostorFrameStats();
            const ImpostorCache* c = s.ents->impostorCache();
            const auto* bn = c ? c->batchNode() : nullptr;
            fprintf(csv, "%d,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%zu,%d,%llu,%.3f,%.3f,%.3f\n", made,
                    tier == 1 ? "impostor" : "full", pct(ms, 0.5), pct(ms, 0.95),
                    *std::max_element(ms.begin(), ms.end()), fs.tierImpostor, fs.tierFull,
                    bn ? bn->drawCallsLastRender() : 0, c ? c->cells().pages() : 0, c ? c->rowBytes() : size_t(0),
                    createFrames, c ? static_cast<unsigned long long>(c->stats().units) : 0ull,
                    c && c->stats().units ? c->stats().unitMsSum / c->stats().units : 0.0,
                    c ? c->stats().unitMsMax : 0.0, c ? c->stats().frameUnitMsMax : 0.0);
            fflush(csv);
        }
    }
    fclose(csv);
    printf("[impostor-drawcost] wrote %s/drawcost.csv\n", o.outDir.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        fprintf(stderr, "usage: impostor_pixel_compare pixel|drawcost <outdir> [options] (see the header)\n");
        return 2;
    }
    mkdir(o.outDir.c_str(), 0755);
    EQT::WilleqDatabase db;
    Scene s;
    int rc = 1;
    if (setup(s, o, db)) rc = o.mode == "pixel" ? runPixel(s, o) : runDrawCost(s, o);
    s.ents.reset();
    s.cache.reset();
    s.shader.reset();
    EQT::WilleqDatabase::setGlobalInstance(nullptr);
    if (s.device) s.device->drop();
    return rc;
}
