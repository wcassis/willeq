// nametag_drawcost -- B975 (GitLab #293): where an entity name tag's cost goes,
// and the batched path against the per-node path, on x86 (llvmpipe) and the
// Orange Pi (Lima). No zone, no server: N tags under N empty "entity" nodes at
// the client's offset (5.5 units above the origin), the client's built-in font
// and con colours, the client's camera shape (FOV, near/far).
//
// Usage:
//   nametag_drawcost drawcost <outdir> [--counts 1,50,150,200] [--frames 60]
//                    [--size 800x600] [--dist NEAR,FAR] [--drm]
//     Per N: the per-node path's components (projection, CGUIFont::draw()
//     submit, draw2DImageBatch() alone), then the whole scene through
//     ISceneManager::drawAll() with the per-node CTextSceneNodes and with the
//     batch, glFinish-bracketed. Writes <outdir>/drawcost.csv.
//   nametag_drawcost pixel <outdir> [--size 800x600] [--drm] [--png]
//     Fixed poses (counts x distances x camera yaw): the per-node path and
//     the batch rendered from the same scene, compared per pixel. Writes
//     <outdir>/pixel.csv (+ before/after PNGs of every pose with --png).
//
// No model judges a frame: every column is a count or a time.

#include "client/graphics/name_tag_batch.h"

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
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace EQT::Graphics;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::string mode;
    std::string outDir;
    std::vector<int> counts = {1, 50, 150, 200};
    int frames = 60;
    irr::u32 width = 800, height = 600;
    float nearDist = 115.0f, farDist = 160.0f;  // the B964 crowd's span
    bool drm = false;
    bool png = false;
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
        if (!strcmp(argv[i], "--counts")) o.counts = parseList(next());
        else if (!strcmp(argv[i], "--frames")) o.frames = std::atoi(next());
        else if (!strcmp(argv[i], "--size")) sscanf(next(), "%ux%u", &o.width, &o.height);
        else if (!strcmp(argv[i], "--dist")) sscanf(next(), "%f,%f", &o.nearDist, &o.farDist);
        else if (!strcmp(argv[i], "--drm")) o.drm = true;
        else if (!strcmp(argv[i], "--png")) o.png = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return false; }
    }
    return o.mode == "drawcost" || o.mode == "pixel";
}

void finishGpu() {
#ifdef EQT_HAS_GLES2
    glFinish();
#endif
}

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double p95(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, (v.size() * 95) / 100)];
}

// Names shaped like a populated zone's (display names: underscores and the
// trailing spawn digits stripped, EQT::toDisplayName), with the con colours
// EntityRenderer::getConColor() hands out and the player colour.
const wchar_t* kNames[] = {L"a gnoll pup", L"Guard Hezlan", L"a large rat", L"Fippy Darkpaw",
                           L"a decaying skeleton", L"Crowd Human Female", L"Merchant Tonlar",
                           L"an orc pawn", L"Testchar", L"a fire beetle", L"Captain Tillin",
                           L"Lady Shae", L"a Qeynos guard", L"Wolf"};
const irr::video::SColor kColors[] = {
    {255, 255, 255, 255}, {255, 128, 128, 128}, {255, 0, 200, 0}, {255, 0, 160, 255},
    {255, 255, 255, 0},   {255, 255, 0, 0},     {255, 100, 200, 255}};

struct Scene {
    irr::IrrlichtDevice* device = nullptr;
    irr::video::IVideoDriver* driver = nullptr;
    irr::scene::ISceneManager* smgr = nullptr;
    irr::scene::ICameraSceneNode* camera = nullptr;
    irr::gui::IGUIFont* font = nullptr;
    NameTagBatchNode* batch = nullptr;
    std::vector<irr::scene::ISceneNode*> entities;
    std::vector<irr::scene::ITextSceneNode*> tags;
};

bool openDevice(Scene& s, const Options& o) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(o.width, o.height);
    params.Fullscreen = true;
    params.Vsync = false;          // frame times, not the display rate
    params.Stencilbuffer = true;   // as the client on the orangepi preset
#ifdef EQT_HAS_DRM
    if (o.drm) params.DeviceType = irr::EIDT_FRAMEBUFFER;  // the Pi: DRM/KMS
#endif
    s.device = irr::createDeviceEx(params);
    if (!s.device) return false;
    s.driver = s.device->getVideoDriver();
    s.smgr = s.device->getSceneManager();
    s.font = s.device->getGUIEnvironment()->getBuiltInFont();
    // The client's third-person camera (irrlicht_renderer.cpp): Irrlicht's
    // default FOV, near 1.
    s.camera = s.smgr->addCameraSceneNode(nullptr, irr::core::vector3df(0, 6, 0), irr::core::vector3df(0, 6, 100));
    s.camera->setNearValue(1.0f);
    s.camera->setFarValue(2000.0f);
    s.camera->setAspectRatio(static_cast<float>(o.width) / static_cast<float>(o.height));
    s.batch = new NameTagBatchNode(s.smgr->getRootSceneNode(), s.smgr, s.font);
    return s.font != nullptr;
}

// N entity origins on a grid between `nearD` and `farD` ahead, 3 units apart
// (the B964 tour's crowd grid), deterministic.
void placeEntities(Scene& s, int n, float nearD, float farD) {
    for (auto* e : s.entities) e->remove();
    s.entities.clear();
    s.tags.clear();
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const float depthStep = cols > 1 ? (farD - nearD) / static_cast<float>(cols - 1) : 0.0f;
    for (int i = 0; i < n; ++i) {
        const int row = i / cols, col = i % cols;
        const float x = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * 3.0f;
        const float z = nearD + depthStep * static_cast<float>(row);
        s.entities.push_back(s.smgr->addEmptySceneNode(nullptr));
        s.entities.back()->setPosition(irr::core::vector3df(x, 0.0f, z));
    }
}

// The client's tag: parent = the entity node, offset 5.5 above it.
void makeTags(Scene& s, bool batched) {
    for (auto* t : s.tags) t->remove();
    s.tags.clear();
    for (size_t i = 0; i < s.entities.size(); ++i) {
        const wchar_t* name = kNames[i % (sizeof(kNames) / sizeof(kNames[0]))];
        const irr::video::SColor color = kColors[i % (sizeof(kColors) / sizeof(kColors[0]))];
        const irr::core::vector3df off(0.0f, 5.5f, 0.0f);
        if (batched) {
            auto* node = new NameTagNode(s.entities[i], s.smgr, s.batch, name, color, off);
            node->drop();
            s.tags.push_back(node);
        } else {
            s.tags.push_back(s.smgr->addTextSceneNode(s.font, name, color, s.entities[i], off));
        }
    }
}

double drawScene(Scene& s, bool finish) {
    if (finish) finishGpu();
    const auto t0 = Clock::now();
    s.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
    s.smgr->drawAll();
    if (finish) finishGpu();
    const double ms = msSince(t0);
    s.driver->endScene();
    return ms;
}

void ensureDir(const std::string& d) { mkdir(d.c_str(), 0755); }

// -------------------------------------------------------------- drawcost

int runDrawcost(Scene& s, const Options& o) {
    const std::string csv = o.outDir + "/drawcost.csv";
    FILE* f = fopen(csv.c_str(), "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", csv.c_str()); return 1; }
    fprintf(f, "n,visible_tags,proj_us_per_tag,fontdraw_us_per_tag,imagebatch_us_per_tag,"
               "scene_none_p50_ms,pernode_p50_ms,pernode_p95_ms,batch_p50_ms,batch_p95_ms,"
               "pernode_cpu_p50_ms,batch_cpu_p50_ms,batch_render_us_p50,batch_build_us_p50,batch_submit_us_p50,batch_draws,batch_quads,batch_buffer_bytes\n");
    irr::scene::ISceneCollisionManager* coll = s.smgr->getSceneCollisionManager();
    auto* bitmap = static_cast<irr::gui::IGUIFontBitmap*>(s.font);

    for (int n : o.counts) {
        placeEntities(s, n, o.nearDist, o.farDist);
        // Warm up: two frames, the surfaceless harness's first-frame quirk.
        drawScene(s, true);
        drawScene(s, true);

        // Empty scene (no tags) -- the floor.
        std::vector<double> none;
        for (int k = 0; k < o.frames; ++k) none.push_back(drawScene(s, true));

        // The per-node path, whole scene.
        makeTags(s, false);
        drawScene(s, true);
        std::vector<double> perNode, perNodeCpu;
        for (int k = 0; k < o.frames; ++k) perNode.push_back(drawScene(s, true));
        for (int k = 0; k < o.frames; ++k) perNodeCpu.push_back(drawScene(s, false));

        // Its components, per tag, inside one frame (CPU submission time; the
        // GL work they queue is finished outside the timed loops).
        s.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
        s.smgr->drawAll();  // camera matrices + absolute positions current
        int visible = 0;
        std::vector<irr::core::position2d<irr::s32>> screen(s.tags.size());
        auto t0 = Clock::now();
        for (int rep = 0; rep < 10; ++rep)
            for (size_t i = 0; i < s.tags.size(); ++i)
                screen[i] = coll->getScreenCoordinatesFrom3DPosition(s.tags[i]->getAbsolutePosition(), s.camera);
        const double projUs = msSince(t0) * 1000.0 / (10.0 * static_cast<double>(s.tags.size()));
        for (auto& p : screen)
            if (p.X > -1000 && p.X < static_cast<int>(o.width) && p.Y > -1000 && p.Y < static_cast<int>(o.height)) ++visible;
        // Each loop five times, GPU finished before each; the fastest pass is
        // the submission cost (the slower ones carry the rasteriser's flush of
        // what the previous pass queued).
        double fontUs = 1e30;
        for (int rep = 0; rep < 5; ++rep) {
            finishGpu();
            t0 = Clock::now();
            for (size_t i = 0; i < s.tags.size(); ++i) {
                const irr::core::rect<irr::s32> r(screen[i], irr::core::dimension2d<irr::s32>(1, 1));
                s.font->draw(kNames[i % (sizeof(kNames) / sizeof(kNames[0]))], r,
                             kColors[i % (sizeof(kColors) / sizeof(kColors[0]))], true, true);
            }
            fontUs = std::min(fontUs, msSince(t0) * 1000.0 / static_cast<double>(s.tags.size()));
        }
        finishGpu();
        // draw2DImageBatch alone: the arrays CGUISpriteBank would build, prebuilt.
        std::vector<irr::core::array<irr::core::position2d<irr::s32>>> pos(s.tags.size());
        std::vector<irr::core::array<irr::core::rect<irr::s32>>> rects(s.tags.size());
        irr::video::ITexture* tex = bitmap->getSpriteBank()->getTexture(0);
        for (size_t i = 0; i < s.tags.size(); ++i) {
            const wchar_t* name = kNames[i % (sizeof(kNames) / sizeof(kNames[0]))];
            irr::s32 x = screen[i].X;
            for (const wchar_t* c = name; *c; ++c) {
                const irr::u32 sp = bitmap->getSpriteNoFromChar(c);
                const auto& spr = bitmap->getSpriteBank()->getSprites()[sp];
                const auto& rc = bitmap->getSpriteBank()->getPositions()[spr.Frames[0].rectNumber];
                pos[i].push_back(irr::core::position2d<irr::s32>(x, screen[i].Y));
                rects[i].push_back(rc);
                x += rc.getWidth() + 1;
            }
        }
        double batchCallUs = 1e30;
        for (int rep = 0; rep < 5; ++rep) {
            finishGpu();
            t0 = Clock::now();
            for (size_t i = 0; i < s.tags.size(); ++i)
                s.driver->draw2DImageBatch(tex, pos[i], rects[i], nullptr,
                                           kColors[i % (sizeof(kColors) / sizeof(kColors[0]))], true);
            batchCallUs = std::min(batchCallUs, msSince(t0) * 1000.0 / static_cast<double>(s.tags.size()));
        }
        finishGpu();
        s.driver->endScene();

        // The batch, whole scene.
        makeTags(s, true);
        drawScene(s, true);
        std::vector<double> batch, batchCpu, renderUs, buildUs, submitUs;
        size_t draws = 0, quads = 0;
        for (int k = 0; k < o.frames; ++k) {
            batch.push_back(drawScene(s, true));
            renderUs.push_back(static_cast<double>(s.batch->lastFrame().cpuMicros));
            submitUs.push_back(static_cast<double>(s.batch->lastFrame().submitMicros));
            buildUs.push_back(static_cast<double>(s.batch->lastFrame().cpuMicros - s.batch->lastFrame().submitMicros));
            draws = s.batch->lastFrame().driverDraws;
            quads = s.batch->lastFrame().glyphQuads;
        }
        for (int k = 0; k < o.frames; ++k) batchCpu.push_back(drawScene(s, false));
        for (auto* t : s.tags) t->remove();
        s.tags.clear();

        fprintf(f, "%d,%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%zu,%zu,%zu\n", n,
                visible, projUs, fontUs, batchCallUs, median(none), median(perNode), p95(perNode), median(batch),
                p95(batch), median(perNodeCpu), median(batchCpu), median(renderUs), median(buildUs),
                median(submitUs), draws, quads,
                s.batch->bufferBytes());
        printf("n=%d visible=%d proj=%.2fus font_draw=%.2fus image_batch=%.2fus | scene none %.2f | "
               "per-node %.2f (cpu %.2f) | batch %.2f (cpu %.2f, render %.0fus = build %.0f + submit %.0f, "
               "%zu draws, %zu quads)\n",
               n, visible, projUs, fontUs, batchCallUs, median(none), median(perNode), median(perNodeCpu),
               median(batch), median(batchCpu), median(renderUs), median(buildUs), median(submitUs), draws, quads);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}

// ---------------------------------------------------------------- pixel

void savePng(Scene& s, irr::video::IImage* img, const std::string& path) {
    s.driver->writeImageToFile(img, path.c_str());
}

int runPixel(Scene& s, const Options& o) {
    const std::string csv = o.outDir + "/pixel.csv";
    FILE* f = fopen(csv.c_str(), "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", csv.c_str()); return 1; }
    fprintf(f, "pose,n,near,far,yaw_deg,tag_pixels,differing_pixels,differing_pixels_tie_order,batch_draws,batch_quads\n");
    if (o.png) {
        ensureDir(o.outDir + "/before");
        ensureDir(o.outDir + "/after");
    }
    struct Pose { int n; float nearD, farD, yaw; };
    const Pose poses[] = {
        {1, 30.0f, 30.0f, 0.0f},     {10, 20.0f, 60.0f, 0.0f},   {10, 20.0f, 60.0f, 25.0f},
        {50, 40.0f, 120.0f, 0.0f},   {50, 4.0f, 46.0f, 0.0f},    {150, 115.0f, 160.0f, 0.0f},
        {200, 115.0f, 160.0f, 0.0f}, {200, 115.0f, 160.0f, 20.0f}, {200, 4.0f, 46.0f, 0.0f},
    };
    int failures = 0;
    for (size_t p = 0; p < sizeof(poses) / sizeof(poses[0]); ++p) {
        const Pose& ps = poses[p];
        placeEntities(s, ps.n, ps.nearD, ps.farD);
        const float yaw = ps.yaw * irr::core::DEGTORAD;
        s.camera->setTarget(irr::core::vector3df(100.0f * std::sin(yaw), 6.0f, 100.0f * std::cos(yaw)));

        // 0: per-node, batch node hidden (the scene manager sorts the same
        //    set the batch sorts); 1: the batch; 2: per-node with the (empty)
        //    batch node in the transparent list -- its heapsort then orders
        //    exactly-equidistant tags differently, the one way the two paths
        //    can differ.
        irr::video::IImage* frames[3] = {nullptr, nullptr, nullptr};
        size_t draws = 0, quads = 0;
        for (int variant = 0; variant < 3; ++variant) {
            s.batch->setVisible(variant != 0);
            makeTags(s, variant == 1);
            drawScene(s, true);
            drawScene(s, true);
            s.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            s.smgr->drawAll();
            finishGpu();
            frames[variant] = s.driver->createScreenShot();
            s.driver->endScene();
            if (variant == 1) {
                draws = s.batch->lastFrame().driverDraws;
                quads = s.batch->lastFrame().glyphQuads;
            }
        }
        for (auto* t : s.tags) t->remove();
        s.tags.clear();

        size_t painted = 0, differing = 0, differingTies = 0;
        const irr::video::SColor bg(255, 40, 40, 60);
        const auto d = frames[0]->getDimension();
        for (irr::u32 y = 0; y < d.Height; ++y) {
            for (irr::u32 x = 0; x < d.Width; ++x) {
                const irr::u32 a = frames[0]->getPixel(x, y).color;
                if ((a & 0x00FFFFFF) != (bg.color & 0x00FFFFFF)) ++painted;
                const irr::u32 b = frames[1]->getPixel(x, y).color & 0x00FFFFFF;
                if ((a & 0x00FFFFFF) != b) ++differing;
                if ((frames[2]->getPixel(x, y).color & 0x00FFFFFF) != b) ++differingTies;
            }
        }
        if (differing) ++failures;
        fprintf(f, "%zu,%d,%.0f,%.0f,%.0f,%zu,%zu,%zu,%zu,%zu\n", p, ps.n, ps.nearD, ps.farD, ps.yaw, painted,
                differing, differingTies, draws, quads);
        printf("pose %zu n=%d %.0f-%.0f yaw %.0f: tag pixels %zu, differing %zu (tie-order variant %zu), "
               "batch %zu draws %zu quads\n", p, ps.n, ps.nearD, ps.farD, ps.yaw, painted, differing,
               differingTies, draws, quads);
        if (o.png) {
            char name[96];
            snprintf(name, sizeof(name), "pose%zu_n%d_%.0f-%.0f_yaw%.0f.png", p, ps.n, ps.nearD, ps.farD, ps.yaw);
            savePng(s, frames[0], o.outDir + "/before/" + name);
            savePng(s, frames[1], o.outDir + "/after/" + name);
        }
        if (o.png && differingTies) {
            char name[96];
            snprintf(name, sizeof(name), "pose%zu_tie_order_variant.png", p);
            savePng(s, frames[2], o.outDir + "/before/" + name);
        }
        for (auto* fr : frames) fr->drop();
    }
    s.batch->setVisible(true);
    s.camera->setTarget(irr::core::vector3df(0, 6, 100));
    fclose(f);
    printf("poses with any differing pixel: %d\n", failures);
    return failures ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        fprintf(stderr, "usage: nametag_drawcost drawcost|pixel <outdir> [options] (see the file header)\n");
        return 1;
    }
    ensureDir(o.outDir);
    Scene s;
    if (!openDevice(s, o)) {
        fprintf(stderr, "no EDT_OGLES2 device / built-in font\n");
        return 1;
    }
    printf("nametag_drawcost %s %ux%u driver=%ls\n", o.mode.c_str(), o.width, o.height,
           s.driver->getName());
    const int rc = o.mode == "drawcost" ? runDrawcost(s, o) : runPixel(s, o);
    for (auto* e : s.entities) e->remove();
    s.batch->remove();
    s.batch->drop();
    s.device->drop();
    return rc;
}
