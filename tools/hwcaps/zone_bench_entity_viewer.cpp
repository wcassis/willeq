#include "zone_bench_entity_viewer.h"

#include "client/graphics/zone_shader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"

#include <irrlicht.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

namespace ZoneBench {

namespace {

// Count magenta pixels topologically ENCLOSED by non-magenta pixels -- i.e.
// true alpha-test holes (rib gaps, eye sockets, ...) -- as distinct from the
// ordinary magenta background around a non-convex silhouette (the space
// beside/above a diagonal lunging pose, between separated legs, etc., which
// is background that stays connected to the frame border and is NOT a mask
// cutout). Two whole-rectangle/bbox-based approaches were tried first and
// rejected empirically: (1) "all magenta in the model's screen AABB" way
// overcounts (found ~76-95% of the bbox rectangle "magenta" for a lunging
// skeleton pose -- almost all of that is ordinary background beside a
// diagonal thin figure, not holes); (2) "magenta between the leftmost/
// rightmost non-magenta pixel per row" still counts the natural gap between
// separated legs. A border-flood-fill is the standard, unambiguous way to
// separate "connected to the outside" from "enclosed" without needing a
// reference render to diff against: flood-fill from every magenta pixel
// touching the image border through 4-connected magenta neighbors; any
// magenta pixel the flood never reaches is fully surrounded by non-magenta
// pixels on every path to the edge -- a genuine hole. Returns the enclosed
// count and, as a byproduct, the tight bbox of all non-magenta pixels
// (still useful for cropping/documentation, even though it is no longer
// used for the magenta count itself).
struct SilhouetteAnalysis {
    int enclosedMagenta = 0;
    int bboxX0 = 0, bboxY0 = 0, bboxX1 = 0, bboxY1 = 0;  // [x0,x1)x[y0,y1), empty if no pixels found
    bool hasSilhouette = false;
};

SilhouetteAnalysis analyzeSilhouette(irr::video::IImage* shot, int width, int height) {
    SilhouetteAnalysis out;

    std::vector<uint8_t> isMagenta(static_cast<size_t>(width) * height, 0);
    int minX = width, maxX = -1, minY = height, maxY = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            irr::video::SColor c = shot->getPixel(x, y);
            bool magenta = (c.getRed() == 255 && c.getGreen() == 0 && c.getBlue() == 255);
            isMagenta[static_cast<size_t>(y) * width + x] = magenta ? 1 : 0;
            if (!magenta) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        return out;  // entirely magenta -- no silhouette at all
    }
    out.hasSilhouette = true;
    out.bboxX0 = minX; out.bboxY0 = minY; out.bboxX1 = maxX + 1; out.bboxY1 = maxY + 1;

    std::vector<uint8_t> reachedFromBorder(static_cast<size_t>(width) * height, 0);
    std::deque<int> queue;
    auto tryPush = [&](int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        size_t idx = static_cast<size_t>(y) * width + x;
        if (!isMagenta[idx] || reachedFromBorder[idx]) return;
        reachedFromBorder[idx] = 1;
        queue.push_back(static_cast<int>(idx));
    };
    for (int x = 0; x < width; ++x) { tryPush(x, 0); tryPush(x, height - 1); }
    for (int y = 0; y < height; ++y) { tryPush(0, y); tryPush(width - 1, y); }
    while (!queue.empty()) {
        int idx = queue.front(); queue.pop_front();
        int x = idx % width, y = idx / width;
        tryPush(x - 1, y); tryPush(x + 1, y); tryPush(x, y - 1); tryPush(x, y + 1);
    }

    // Connected-component filter: even an UNMASKED, fully opaque model can
    // produce a handful of 1-15px enclosed magenta pixels where two convex
    // body parts nearly touch in 2D screen space without touching in 3D
    // (found empirically: race 1/Human, --entity-viewer-frame 0/5/10/40 all
    // gave identical small enclosures of 2-15px at the "back"/"left"
    // angles, from a hand/forearm silhouette sealing off a sliver of
    // background against the hip -- not a mask cutout, and confirmed
    // frame-independent, i.e. not an animation-pose artifact this unit
    // could dodge by picking a different frame). A speck that small is also
    // not what the mission's own reading rule cares about ("images a human
    // can judge in two seconds" -- a human will never notice a 1-15px
    // fleck at 800x600). Genuine mask cutouts (rib gaps, eye sockets) are
    // one to several orders of magnitude larger (tens to hundreds of
    // pixels, see this unit's Review for the real numbers). Only count
    // enclosed regions at or above this floor -- filters the geometry-seam
    // noise floor without being anywhere near large enough to hide a real
    // cutout.
    constexpr int kMinHoleAreaPx = 20;
    std::vector<uint8_t> visited(isMagenta.size(), 0);
    int enclosed = 0;
    for (size_t start = 0; start < isMagenta.size(); ++start) {
        if (!isMagenta[start] || reachedFromBorder[start] || visited[start]) continue;
        // Flood-fill this enclosed component to find its area.
        std::deque<int> compQueue;
        compQueue.push_back(static_cast<int>(start));
        visited[start] = 1;
        int area = 0;
        while (!compQueue.empty()) {
            int idx = compQueue.front(); compQueue.pop_front();
            ++area;
            int x = idx % width, y = idx / width;
            const int nbrs[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
            for (const auto& n : nbrs) {
                int nx = n[0], ny = n[1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                size_t nidx = static_cast<size_t>(ny) * width + nx;
                if (!isMagenta[nidx] || reachedFromBorder[nidx] || visited[nidx]) continue;
                visited[nidx] = 1;
                compQueue.push_back(static_cast<int>(nidx));
            }
        }
        if (area >= kMinHoleAreaPx) enclosed += area;
    }
    out.enclosedMagenta = enclosed;
    return out;
}

struct ViewAngle {
    const char* name;
    float headingDeg;
};

// Front/back/side minimum per the plan; a 4th ("right") is free and gives a
// more complete part-by-part read. Heading follows the same EQ convention
// used throughout this tool (poseToDirection() in zone_bench_frame.cpp, T387:
// heading 0 -> +Y EQ, clockwise) -- "front" is simply heading 0, not a claim
// about which way the model's texture artist intended it to face.
constexpr ViewAngle kAngles[] = {
    {"front", 0.0f},
    {"right", 90.0f},
    {"back", 180.0f},
    {"left", 270.0f},
};

void poseToDirEQ(float headingDeg, float& dirX, float& dirY) {
    const float d2r = 3.14159265f / 180.0f;
    dirX = sinf(headingDeg * d2r);
    dirY = cosf(headingDeg * d2r);
}

// Mirrors zone_bench_frame.cpp's setupCamera() (T387 moved it there out of
// gles2_zone_benchmark.cpp; it is exported now, but this TU stays
// self-contained per the plan's "put substantial new code in its own TU"
// instruction). Same EQ (x,y,z-up) -> Irrlicht
// (x,z,y-up) convention, same shader camera-pos plumbing.
void setupViewerCamera(ZoneBenchData& data,
                        float eqX, float eqY, float eqZ,
                        float dirX, float dirY, float dirZ,
                        float aspect) {
    const float fov = 60.0f * 3.14159265f / 180.0f;
    const float nearClip = 0.5f;   // closer near plane than the zone default
                                    // (1.0) -- entity-viewer cameras can sit
                                    // quite close to small-bbox races.
    const float farClip = 300.0f;

    irr::core::matrix4 proj;
    proj.buildProjectionMatrixPerspectiveFovLH(fov, aspect, nearClip, farClip);
    data.driver->setTransform(irr::video::ETS_PROJECTION, proj);

    irr::core::vector3df irrPos(eqX, eqZ, eqY);
    irr::core::vector3df irrTarget(eqX + dirX, eqZ + dirZ, eqY + dirY);
    irr::core::vector3df irrUp(0, 1, 0);

    irr::core::matrix4 view;
    view.buildCameraLookAtMatrixLH(irrPos, irrTarget, irrUp);
    data.driver->setTransform(irr::video::ETS_VIEW, view);

    if (data.zoneShader) {
        data.zoneShader->setCameraPos(irrPos.X, irrPos.Y, irrPos.Z);
        data.zoneShader->setPlayerLightPos(irrPos.X, irrPos.Y, irrPos.Z);
        data.zoneShader->setPointLight(0, irrPos.X, irrPos.Y, irrPos.Z,
                                        data.playerLightColor[0],
                                        data.playerLightColor[1],
                                        data.playerLightColor[2],
                                        data.playerLightAtten[0],
                                        data.playerLightAtten[1],
                                        data.playerLightAtten[2]);
    }
}

}  // namespace

std::vector<EntityViewerAngleResult> runEntityViewer(ZoneBenchData& data,
                                                       const EntityViewerConfig& cfg) {
    std::vector<EntityViewerAngleResult> results;

    if (data.animatedNodes.empty() || !data.animatedNodes[0] || data.entities.empty()) {
        fprintf(stderr,
                "ENTITY_VIEWER ERROR: no animated entity loaded for race=%u -- "
                "caller must configure BenchConfig with entityCount=1, "
                "testRaceIds={race}, animate=true before loadZone()\n",
                cfg.raceId);
        return results;
    }

    auto* node = data.animatedNodes[0];

    // Deterministic pose -- see header comment. frame*33.3ms is a pure
    // function of cfg.animFrame, not wall-clock time (P44b's own finding:
    // "the animation clock is frame*33.3ms, deterministic").
    irr::u32 simTimeMs = static_cast<irr::u32>(cfg.animFrame * 33.3f);
    node->OnAnimate(simTimeMs);

    auto* mesh = node->getInstanceMesh();
    if (!mesh || mesh->getMeshBufferCount() == 0) {
        fprintf(stderr, "ENTITY_VIEWER ERROR: race=%u produced no instance mesh after OnAnimate\n",
                cfg.raceId);
        return results;
    }

    irr::core::aabbox3df localBox = mesh->getBoundingBox();
    if (localBox.MinEdge.Y > localBox.MaxEdge.Y) {
        fprintf(stderr, "ENTITY_VIEWER ERROR: race=%u has a degenerate bounding box\n", cfg.raceId);
        return results;
    }

    // Ground the model at EQ Z=0 (Irrlicht Y=0), same convention as
    // zone_bench_modes.cpp's --composite-entity-test mode (T387): offset by
    // -MinEdge.Y so the feet sit at the origin height.
    float meshOffsetY = -localBox.MinEdge.Y;
    irr::core::vector3df pos(0.0f, meshOffsetY, 0.0f);
    node->setPosition(pos);
    node->updateAbsolutePosition();

    data.entities[0].mesh = mesh;
    data.entities[0].position = pos;
    data.entities[0].eqX = 0.0f;
    data.entities[0].eqY = 0.0f;
    data.entities[0].eqZ = 0.0f;

    // worldMat in collectEntityDrawEntries() is a pure translation by
    // ent.position (no rotation) -- so the world AABB is just the local
    // (already-skinned) AABB translated by the same offset.
    irr::core::aabbox3df worldBox = localBox;
    worldBox.MinEdge += pos;
    worldBox.MaxEdge += pos;

    float heightIrr = worldBox.MaxEdge.Y - worldBox.MinEdge.Y;  // EQ Z (vertical)
    float extentX = worldBox.MaxEdge.X - worldBox.MinEdge.X;    // EQ X
    float extentZ = worldBox.MaxEdge.Z - worldBox.MinEdge.Z;    // EQ Y
    float horizExtent = std::max(extentX, extentZ);
    if (heightIrr < 1.0f) heightIrr = 1.0f;
    if (horizExtent < 1.0f) horizExtent = 1.0f;

    irr::core::dimension2d<irr::u32> screenSize = data.driver->getScreenSize();
    const int width = static_cast<int>(screenSize.Width);
    const int height = static_cast<int>(screenSize.Height);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float fovV = 60.0f * 3.14159265f / 180.0f;
    const float fovH = 2.0f * atanf(tanf(fovV * 0.5f) * aspect);
    // The model should fill MOST of the frame -- mission requirement. 0.80
    // leaves a small margin so the silhouette never clips off-screen at the
    // widest limb swing of any tested pose.
    const float margin = 0.80f;

    float distForHeight = (heightIrr * 0.5f / margin) / tanf(fovV * 0.5f);
    float distForWidth = (horizExtent * 0.5f / margin) / tanf(fovH * 0.5f);
    float dist = std::max(distForHeight, distForWidth);
    dist = std::max(dist, 4.0f);  // never push the camera inside the near clip

    irr::core::vector3df worldCenter = (worldBox.MinEdge + worldBox.MaxEdge) * 0.5f;
    float centerEqX = worldCenter.X;
    float centerEqY = worldCenter.Z;
    float centerEqZ = worldCenter.Y;

    const std::string raceLabel = cfg.raceLabel.empty() ? std::to_string(cfg.raceId) : cfg.raceLabel;

    for (const auto& angle : kAngles) {
        float dirEqX, dirEqY;
        poseToDirEQ(angle.headingDeg, dirEqX, dirEqY);

        float camEqX = centerEqX - dist * dirEqX;
        float camEqY = centerEqY - dist * dirEqY;
        float camEqZ = centerEqZ;  // level with the model's vertical midpoint

        setupViewerCamera(data, camEqX, camEqY, camEqZ, dirEqX, dirEqY, 0.0f, aspect);
        if (data.zoneShader) data.zoneShader->beginFrame();

        // Solid magenta clear -- the whole point of this instrument (see
        // header comment / plan). No zone or object draw entries are ever
        // collected below -- only this single entity's own mesh buffers.
        data.driver->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        if (data.zoneShader) data.zoneShader->beginFrame();

        CameraState camState{camEqX, camEqY, camEqZ, dirEqX, dirEqY, 0.0f, fovV, aspect};
        std::vector<DrawEntry> entries;
        int drawCalls = 0, polys = 0, visible = 0;
        // frustumCull/occlusionCull off: this is a single always-in-frame
        // subject, not a scene we're trying to cull correctly.
        collectEntityDrawEntries(data, camState, 1.0e6f, /*frustumCull=*/false,
                                  /*occlusionCull=*/false, entries, drawCalls, polys, visible);

        for (const auto& de : entries) {
            data.driver->setTransform(irr::video::ETS_WORLD, de.worldMat);
            data.driver->setMaterial(de.buffer->getMaterial());
            data.driver->drawMeshBuffer(de.buffer);
        }

        EntityViewerAngleResult result;
        result.angleName = angle.name;

        irr::video::IImage* shot = data.driver->createScreenShot();
        std::string pngPath = cfg.outDir + "/" + cfg.modeLabel + "_" + raceLabel + "_" + angle.name + ".png";
        int magenta = 0;
        bool wrote = false;
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        if (shot) {
            wrote = data.driver->writeImageToFile(shot, pngPath.c_str());

            SilhouetteAnalysis sil = analyzeSilhouette(shot, width, height);
            if (sil.hasSilhouette) {
                magenta = sil.enclosedMagenta;
                bx0 = sil.bboxX0; by0 = sil.bboxY0; bx1 = sil.bboxX1; by1 = sil.bboxY1;
            } else {
                fprintf(stderr,
                        "ENTITY_VIEWER WARNING: race=%u angle=%s rendered NO non-magenta "
                        "pixels -- entity invisible/off-frame at this angle\n",
                        cfg.raceId, angle.name);
            }
            shot->drop();
        }

        data.driver->endScene();

        result.pngPath = pngPath;
        result.ok = wrote;
        result.magentaInBbox = magenta;
        result.bboxX0 = bx0; result.bboxY0 = by0; result.bboxX1 = bx1; result.bboxY1 = by1;
        results.push_back(result);

        printf("[entity-viewer] race=%u mode=%s angle=%-5s dist=%.1f draws=%d polys=%d "
               "bbox=(%d,%d)-(%d,%d) magenta_in_bbox=%d -> %s%s\n",
               cfg.raceId, cfg.modeLabel.c_str(), angle.name, dist, drawCalls, polys,
               bx0, by0, bx1, by1, magenta, pngPath.c_str(), wrote ? "" : " [WRITE FAILED]");
    }

    return results;
}

}  // namespace ZoneBench
