// zone_bench_impostor_test.cpp -- B952 (GitLab #270): the impostor scenes
// (--impostor-scene) and the pixel evidence for the impostor size
// (--impostor-pixel-test). gles2_zone_benchmark only: it needs setupCamera()
// (zone_bench_frame.cpp), which the other zone_bench_lod.cpp consumers do not
// link. The cache itself is zone_bench_impostor.cpp.
//
// Nothing here judges a picture. The pixel test prints numbers -- per-pixel
// max absolute channel difference, pixels that differ at all, pixels that
// differ by more than 32, and the model's on-screen bounding box against the
// background -- and writes 4x crop sheets for Will to look at.

#include "zone_bench_impostor.h"

#include "zone_bench_frame.h"
#include "zone_bench_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/zone_shader.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

namespace ZoneBench {

namespace {

constexpr float kEyeHeight = 6.0f;  // placeEntitiesAlongPath()'s ground rule

void placeEntityAt(ZoneBenchData& data, int i, float eqX, float eqY, float eqZ) {
    auto& e = data.entities[i];
    e.eqX = eqX;
    e.eqY = eqY;
    e.eqZ = eqZ;
    float meshOffsetY = 0.0f;
    if (e.mesh) meshOffsetY = -e.mesh->getBoundingBox().MinEdge.Y;
    e.position = irr::core::vector3df(eqX, eqZ + meshOffsetY, eqY);
    if (i < static_cast<int>(data.animatedNodes.size()) && data.animatedNodes[i]) {
        data.animatedNodes[i]->setPosition(e.position);
        data.animatedNodes[i]->updateAbsolutePosition();
    }
}

// Fill rows from distance `d0` outwards (row step 5, lateral spacing 4), each
// row no wider than +-30 degrees of the view axis. Returns the last row distance.
float fillRows(ZoneBenchData& data, int first, int count, float eqX, float eqY, float groundZ,
               float fX, float fY, float d0) {
    const float rX = fY, rY = -fX;  // EQ right
    const float spacing = 4.0f, rowStep = 5.0f, tanHalf = 0.577f;
    int placed = 0;
    float d = d0;
    while (placed < count) {
        int perRow = static_cast<int>(2.0f * d * tanHalf / spacing) + 1;
        for (int k = 0; k < perRow && placed < count; ++k) {
            // centre outwards: 0, +1, -1, +2, -2 ...
            int slot = (k + 1) / 2 * ((k % 2) ? 1 : -1);
            float lateral = slot * spacing;
            placeEntityAt(data, first + placed, eqX + fX * d + rX * lateral,
                          eqY + fY * d + rY * lateral, groundZ);
            ++placed;
        }
        d += rowStep;
    }
    return d - rowStep;
}

struct Footprint {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    long pixels = 0;
    bool empty() const { return x1 < 0; }
    void add(int x, int y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
        ++pixels;
    }
};

bool samePixel(const irr::video::SColor& a, const irr::video::SColor& b) {
    return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

int maxChannelDiff(const irr::video::SColor& a, const irr::video::SColor& b) {
    int d = std::abs(static_cast<int>(a.getRed()) - static_cast<int>(b.getRed()));
    d = std::max(d, std::abs(static_cast<int>(a.getGreen()) - static_cast<int>(b.getGreen())));
    d = std::max(d, std::abs(static_cast<int>(a.getBlue()) - static_cast<int>(b.getBlue())));
    return d;
}

Footprint footprintVs(irr::video::IImage* img, irr::video::IImage* bg) {
    Footprint f;
    const auto dim = img->getDimension();
    for (irr::u32 y = 0; y < dim.Height; ++y)
        for (irr::u32 x = 0; x < dim.Width; ++x)
            if (!samePixel(img->getPixel(x, y), bg->getPixel(x, y))) f.add(x, y);
    return f;
}

struct Diff {
    int maxAbs = 0;
    long differ = 0;
    long differ32 = 0;
    // Pixels covered by BOTH images (neither is the background): the shading
    // difference, separate from the silhouette.
    long interior = 0;
    int interiorMed = 0, interiorP90 = 0, interiorMax = 0;
    // Pixels covered by exactly one of the two: the silhouette mismatch.
    long silhouette = 0;
};

Diff diffImages(irr::video::IImage* a, irr::video::IImage* b, irr::video::IImage* bg) {
    Diff d;
    std::vector<int> inner;
    const auto dim = a->getDimension();
    for (irr::u32 y = 0; y < dim.Height; ++y)
        for (irr::u32 x = 0; x < dim.Width; ++x) {
            const auto pa = a->getPixel(x, y), pb = b->getPixel(x, y), pg = bg->getPixel(x, y);
            int m = maxChannelDiff(pa, pb);
            if (m > 0) ++d.differ;
            if (m > 32) ++d.differ32;
            d.maxAbs = std::max(d.maxAbs, m);
            const bool ca = !samePixel(pa, pg), cb = !samePixel(pb, pg);
            if (ca && cb) inner.push_back(m);
            else if (ca != cb) ++d.silhouette;
        }
    if (!inner.empty()) {
        std::sort(inner.begin(), inner.end());
        d.interior = static_cast<long>(inner.size());
        d.interiorMed = inner[inner.size() / 2];
        d.interiorP90 = inner[std::min(inner.size() - 1, inner.size() * 9 / 10)];
        d.interiorMax = inner.back();
    }
    return d;
}

// One frame with only `draw` in it, read back before endScene().
template <typename Fn>
irr::video::IImage* renderFrame(ZoneBenchData& data, const CameraState& cam, Fn&& draw) {
    float dirLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
    setupCamera(data, cam.eqX, cam.eqY, cam.eqZ, cam.dirX / dirLen, cam.dirY / dirLen,
                cam.dirZ / dirLen);
    data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
    if (data.zoneShader) data.zoneShader->beginFrame();
    draw();
    irr::video::IImage* shot = data.driver->createScreenShot();
    data.driver->endScene();
    return shot;
}

void writeSheet(ZoneBenchData& data, const std::vector<irr::video::IImage*>& panels,
                const Footprint& box, const std::string& path) {
    const int pad = 4, scale = 4, gap = 4;
    const auto dim = panels[0]->getDimension();
    int x0 = std::max(0, box.x0 - pad), y0 = std::max(0, box.y0 - pad);
    int x1 = std::min(static_cast<int>(dim.Width) - 1, box.x1 + pad);
    int y1 = std::min(static_cast<int>(dim.Height) - 1, box.y1 + pad);
    int cw = (x1 - x0 + 1) * scale, ch = (y1 - y0 + 1) * scale;
    int W = static_cast<int>(panels.size()) * cw + (static_cast<int>(panels.size()) - 1) * gap;
    irr::video::IImage* sheet = data.driver->createImage(
        irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(W, ch));
    if (!sheet) return;
    sheet->fill(irr::video::SColor(255, 255, 255, 255));
    for (size_t p = 0; p < panels.size(); ++p) {
        int ox = static_cast<int>(p) * (cw + gap);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                sheet->setPixel(ox + x, y, panels[p]->getPixel(x0 + x / scale, y0 + y / scale));
    }
    data.driver->writeImageToFile(sheet, path.c_str());
    sheet->drop();
}

}  // namespace

void placeImpostorScene(ZoneBenchData& data, float eqX, float eqY, float eqZ,
                        float dirX, float dirY, bool mixed, float tierDist) {
    const int n = static_cast<int>(data.entities.size());
    if (n == 0) return;
    float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 1e-4f) { dirX = 0.0f; dirY = 1.0f; len = 1.0f; }
    const float fX = dirX / len, fY = dirY / len;
    const float groundZ = eqZ - kEyeHeight;
    const float farStart = tierDist * 1.15f;
    if (mixed) {
        const int nearCount = n / 2;
        float nearEnd = fillRows(data, 0, nearCount, eqX, eqY, groundZ, fX, fY, 10.0f);
        float farEnd = fillRows(data, nearCount, n - nearCount, eqX, eqY, groundZ, fX, fY, farStart);
        printf("[impostor-scene] mixed: %d near (rows 10..%.0f), %d far (rows %.0f..%.0f), tier=%.0f\n",
               nearCount, nearEnd, n - nearCount, farStart, farEnd, tierDist);
        if (nearEnd >= tierDist)
            printf("[impostor-scene] WARNING: the near half reaches %.0f >= tier %.0f\n", nearEnd, tierDist);
    } else {
        float farEnd = fillRows(data, 0, n, eqX, eqY, groundZ, fX, fY, farStart);
        printf("[impostor-scene] far: %d entities (rows %.0f..%.0f), tier=%.0f\n", n, farStart,
               farEnd, tierDist);
    }
}

int runImpostorPixelTest(ZoneBenchData& data, float eqX, float eqY, float eqZ, float heading,
                         const std::string& outDir) {
    if (data.entities.empty()) {
        fprintf(stderr, "ERROR: --impostor-pixel-test needs --entities N (one per variant)\n");
        return 1;
    }
    const float h = heading * 3.14159265f / 180.0f;
    const float fX = std::sin(h), fY = std::cos(h);  // heading 0 = +Y, clockwise
    const auto screen = data.driver->getScreenSize();
    const int W = static_cast<int>(screen.Width), H = static_cast<int>(screen.Height);
    const float groundZ = eqZ - kEyeHeight;
    const float distances[] = {40.0f, 80.0f, 160.0f};
    const int sizes[] = {64, 128, 256};

    // One entity per distinct race/gender variant.
    std::vector<int> picks;
    std::set<std::pair<int, int>> seen;
    for (int i = 0; i < static_cast<int>(data.entities.size()); ++i) {
        const auto& e = data.entities[i];
        if (e.mesh && seen.insert({e.raceId, e.gender}).second) picks.push_back(i);
    }
    printf("[impostor-pixel] res=%dx%d variants=%zu\n", W, H, picks.size());

    std::string csvPath = outDir + "/impostor_pixel_" + std::to_string(W) + "x" + std::to_string(H) + ".csv";
    FILE* csv = fopen(csvPath.c_str(), "w");
    if (csv)
        fprintf(csv, "res,race,gender,dist,size,model_h_px,model_w_px,model_h_px_pred,full_px,imp_px,"
                     "max_abs,diff_px,diff32_px,interior_px,interior_med,interior_p90,interior_max,silhouette_px\n");

    ImpostorCache& cache = impostorCache(data);
    const ImpostorConfig saved = cache.cfg;
    // Diagnostics for decomposing a systematic difference (README "pixel
    // evidence"): B952_FULL_NOFOG=1 draws the full model without fog, the way
    // the impostor capture does; B952_BB_FOG=1 fogs the impostor quad.
    const bool fullNoFog = std::getenv("B952_FULL_NOFOG") && *std::getenv("B952_FULL_NOFOG") == '1';
    printf("[impostor-pixel] full_model_fog=%s billboard_fog=%s\n", fullNoFog ? "off" : "material",
           (std::getenv("B952_BB_FOG") && *std::getenv("B952_BB_FOG") == '1') ? "on" : "off");

    for (float D : distances) {
        for (int vi : picks) {
            placeEntityAt(data, vi, eqX + fX * D, eqY + fY * D, groundZ);
            auto& ent = data.entities[vi];
            auto* node = vi < static_cast<int>(data.animatedNodes.size()) ? data.animatedNodes[vi] : nullptr;
            irr::scene::IMesh* mesh = ent.mesh;
            if (node) {
                node->playAnimation("o01", true);
                node->forceAnimationUpdate();
                if (node->getInstanceMesh()) mesh = node->getInstanceMesh();
            }
            CameraState cam{eqX, eqY, eqZ, fX, fY, 0.0f, 60.0f * 3.14159265f / 180.0f,
                            static_cast<float>(W) / H};

            irr::video::IImage* bg = renderFrame(data, cam, [] {});
            irr::video::IImage* full = renderFrame(data, cam, [&] {
                irr::core::matrix4 world;
                world.setTranslation(ent.position);
                data.driver->setTransform(irr::video::ETS_WORLD, world);
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    auto* buf = mesh->getMeshBuffer(b);
                    if (!buf || buf->getVertexCount() == 0) continue;
                    irr::video::SMaterial mat = buf->getMaterial();
                    if (fullNoFog) mat.FogEnable = false;  // diagnostic decomposition
                    data.driver->setMaterial(mat);
                    data.driver->drawMeshBuffer(buf);
                }
            });
            if (!bg || !full) {
                fprintf(stderr, "ERROR: createScreenShot() failed\n");
                return 1;
            }
            Footprint fpFull = footprintVs(full, bg);
            Footprint box = fpFull;

            std::vector<irr::video::IImage*> panels{full};
            for (int s : sizes) {
                cache.clear(data);
                ImpostorConfig c = saved;  // keeps --impostor-fbo
                c.size = s;
                c.shared = false;
                c.refreshEvery = 0;
                cache.cfg = c;
                setupCamera(data, cam.eqX, cam.eqY, cam.eqZ, cam.dirX, cam.dirY, cam.dirZ);
                ImpostorEntry* e = cache.acquire(data, vi, cam);
                if (!e) {
                    fprintf(stderr, "ERROR: impostor creation failed (size %d)\n", s);
                    return 1;
                }
                // The impostor texture itself (RGBA, alpha kept), for the texel census.
                if (D == 80.0f) {
#ifdef EQT_HAS_GLES2
                    GLint prevTex = 0;  // unlock() binds raw; keep the driver's cache true
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
#endif
                    void* px = e->tex->lock(irr::video::ETLM_READ_ONLY);
                    if (px) {
                        irr::video::IImage* ti = data.driver->createImageFromData(
                            irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(s, s), px, false, false);
                        char tn[160];
                        snprintf(tn, sizeof(tn), "/texture_%d_r%03dg%d.png", s, ent.raceId, ent.gender);
                        if (ti) { data.driver->writeImageToFile(ti, (outDir + tn).c_str()); ti->drop(); }
                    }
                    e->tex->unlock();
#ifdef EQT_HAS_GLES2
                    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
#endif
                }
                const float modelH = 2.0f * e->halfH / 1.04f;
                const float predH = H * modelH / (2.0f * D * std::tan(30.0f * 3.14159265f / 180.0f));
                irr::video::IImage* imp = renderFrame(data, cam, [&] {
                    drawImpostorBillboard(data, *e, ent.position, cam);
                });
                if (!imp) return 1;
                Footprint fpImp = footprintVs(imp, bg);
                if (!fpImp.empty()) {
                    box.add(fpImp.x0, fpImp.y0);
                    box.add(fpImp.x1, fpImp.y1);
                }
                Diff d = diffImages(imp, full, bg);
                const int hpx = fpFull.empty() ? 0 : fpFull.y1 - fpFull.y0 + 1;
                const int wpx = fpFull.empty() ? 0 : fpFull.x1 - fpFull.x0 + 1;
                printf("[impostor-pixel] res=%dx%d race=%d gender=%d dist=%.0f size=%d model_h_px=%d "
                       "model_w_px=%d model_h_px_pred=%.1f full_px=%ld imp_px=%ld max_abs=%d "
                       "diff_px=%ld diff32_px=%ld interior_px=%ld interior_med=%d interior_p90=%d "
                       "interior_max=%d silhouette_px=%ld\n",
                       W, H, ent.raceId, ent.gender, D, s, hpx, wpx, predH, fpFull.pixels,
                       fpImp.pixels, d.maxAbs, d.differ, d.differ32, d.interior, d.interiorMed,
                       d.interiorP90, d.interiorMax, d.silhouette);
                if (csv)
                    fprintf(csv, "%dx%d,%d,%d,%.0f,%d,%d,%d,%.1f,%ld,%ld,%d,%ld,%ld,%ld,%d,%d,%d,%ld\n",
                            W, H, ent.raceId, ent.gender, D, s, hpx, wpx, predH, fpFull.pixels,
                            fpImp.pixels, d.maxAbs, d.differ, d.differ32, d.interior,
                            d.interiorMed, d.interiorP90, d.interiorMax, d.silhouette);
                panels.push_back(imp);
            }
            if (!box.empty()) {
                char name[128];
                snprintf(name, sizeof(name), "/sheet_%dx%d_d%03.0f_r%03dg%d.png", W, H, D,
                         ent.raceId, ent.gender);
                writeSheet(data, panels, box, outDir + name);
            }
            for (auto* p : panels) p->drop();
            bg->drop();
        }
    }
    cache.clear(data);
    cache.cfg = saved;
    if (csv) fclose(csv);
    printf("[impostor-pixel] csv=%s\n", csvPath.c_str());
    return 0;
}

}  // namespace ZoneBench
