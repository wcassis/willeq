// zone_bench_door_batch_ab.cpp -- B997 (GitLab #317) step 2: the
// deterministic x86 pixel A/B for the per-material scene-graph door batch
// (EQT_SG_BATCH). See zone_bench_door_batch_ab.h for the design and the
// determinism argument. gles2_zone_benchmark only (needs loadZone's
// surfaceless-EGL device + the door production path linked into it).
//
// The frame order inside one leg is production's own:
//   setupCamera -> doors->update(0.0)
//   -> (batch on) doors->gatherSceneGraphBatch()     [SOLID-pre gate]
//   -> driver->beginScene -> zoneShader->beginFrame
//   -> (batch on) doors->renderSceneGraphBatch()     [SOLID-pre point]
//   -> smgr->drawAll()                               [the SOLID pass]
//   -> (batch on) doors->restoreSceneGraphBatch()    [SOLID-post point]
//   -> zoneShader->endFrame -> driver->endScene
// and the capture is the driver's own screen shot right after, so both legs
// rasterise the identical door geometry with the identical transforms; only
// the DRAW PATH differs (one client-array draw per node vs one gathered
// client-array draw per material group).

#include "zone_bench_door_batch_ab.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "client/graphics/door_manager.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/zone_shader.h"  // full ZoneShaderManager (bench loader forward-declares)
#include "zone_bench_frame.h"

namespace fs = std::filesystem;

namespace {

// ---- fixture parsing -------------------------------------------------------

// One `doors` table row (the server's SpawnDoor source; see the header).
struct DoorRow {
    unsigned int doorid = 0;
    std::string name;
    float x = 0, y = 0, z = 0;
    float heading = 0;
    unsigned int incline = 0;
    unsigned int size = 100;
    unsigned int opentype = 0;
    bool doorIsOpen = false;
    unsigned int doorParam = 0;
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool parseDoorFixture(const std::string& path, std::vector<DoorRow>& out,
                      std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot open door fixture " + path;
        return false;
    }
    std::string line;
    bool header = true;
    while (std::getline(f, line)) {
        if (trim(line).empty()) continue;
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(ss, cell, '\t')) cells.push_back(trim(cell));
        if (header) {
            header = false;
            if (cells.empty() || cells[0] != "doorid") continue;
            continue;
        }
        if (cells.size() < 12) {
            err = "door fixture " + path + ": short row: " + line;
            return false;
        }
        try {
            DoorRow r;
            r.doorid = static_cast<unsigned int>(std::stoul(cells[0]));
            r.name = cells[1];
            r.x = std::stof(cells[2]);
            r.y = std::stof(cells[3]);
            r.z = std::stof(cells[4]);
            r.heading = std::stof(cells[5]);
            r.incline = static_cast<unsigned int>(std::stoul(cells[6]));
            r.size = static_cast<unsigned int>(std::stoul(cells[7]));
            r.opentype = static_cast<unsigned int>(std::stoul(cells[8]));
            r.doorIsOpen = (cells[9] == "1");
            r.doorParam = static_cast<unsigned int>(std::stoul(cells[11]));
            out.push_back(r);
        } catch (const std::exception&) {
            err = "door fixture " + path + ": unparsable row: " + line;
            return false;
        }
    }
    if (out.empty()) {
        err = "door fixture " + path + " has no rows";
        return false;
    }
    return true;
}

int countInGraph(const EQT::Graphics::DoorManager& doors,
                 const std::unordered_set<unsigned int>& created) {
    int n = 0;
    for (unsigned id : created) {
        const auto* d = doors.getDoor(static_cast<uint8_t>(id));
        if (d && d->inSceneGraph) ++n;
    }
    return n;
}

} // namespace

namespace ZoneBench {

bool runSgBatchAbLeg(ZoneBenchData& data, bool batchOn,
                     const SgBatchAbConfig& cfg, int minDoorsInGraph,
                     SgBatchAbResult& out) {
    using EQT::Graphics::DoorManager;

    if (!data.bspTree) {
        fprintf(stderr, "[sg-batch-ab] no BSP tree -- cannot gate PVS\n");
        return false;
    }
    if (!data.zoneShader || !data.zoneShader->isAvailable()) {
        fprintf(stderr, "[sg-batch-ab] no zone shader -- the door materials "
                        "would fall back to built-in paths\n");
        return false;
    }

    std::vector<DoorRow> rows;
    std::string err;
    if (!parseDoorFixture(cfg.doorFixturePath, rows, err)) {
        fprintf(stderr, "[sg-batch-ab] %s\n", err.c_str());
        return false;
    }

    // The door meshes resolve through S3DZone::objectGeometries (findDoorMesh
    // in door_manager_mesh.cpp), which loadZone() loads and then discards
    // (the benchmark keeps the placements, not the geometry defs). Re-load
    // the zone's _obj.s3d geometry definitions and merge them into the
    // benchmark's zone object, so the production door path finds them.
    if (cfg.eqPath.empty() || cfg.zoneName.empty()) {
        fprintf(stderr, "[sg-batch-ab] missing eq path / zone name -- cannot "
                        "load the door geometries\n");
        return false;
    }
    {
        EQT::Graphics::S3DLoader objLoader;
        EQT::Graphics::S3DLoadOptions objOpts;
        objOpts.loadObjects = true;
        objOpts.loadCharacters = false;
        objOpts.computeCombinedGeometry = false;
        const std::string archivePath = cfg.eqPath + "/" + cfg.zoneName + ".s3d";
        if (!objLoader.loadZone(archivePath, objOpts)) {
            fprintf(stderr, "[sg-batch-ab] cannot load door geometries from %s: %s\n",
                    archivePath.c_str(), objLoader.getError().c_str());
            return false;
        }
        auto objZone = objLoader.getZone();
        int mergedGeom = 0, mergedTex = 0;
        if (objZone) {
            for (auto& [name, geom] : objZone->objectGeometries) {
                if (data.zone->objectGeometries.find(name) ==
                    data.zone->objectGeometries.end()) {
                    data.zone->objectGeometries[name] = geom;
                    ++mergedGeom;
                }
            }
            for (auto& [name, tex] : objZone->objectTextures) {
                if (data.zone->objectTextures.find(name) ==
                    data.zone->objectTextures.end()) {
                    data.zone->objectTextures[name] = tex;
                    ++mergedTex;
                }
            }
        }
        fprintf(stderr, "[sg-batch-ab] merged %d object geometries, %d textures from %s\n",
                mergedGeom, mergedTex, archivePath.c_str());
        if (mergedGeom == 0) {
            fprintf(stderr, "[sg-batch-ab] no object geometries in %s -- doors "
                            "cannot be built\n", archivePath.c_str());
            return false;
        }
    }

    // The fixed camera, DERIVED from the fixture (no tuning): the doors'
    // centroid, 45 units out in +EQ-X and 25 units up in +EQ-Z. The doors
    // are a wide, flat spread (hundreds of units), so the camera sits CLOSE
    // to the centroid cluster -- not 60 out -- and the far clip is raised
    // (the harness default 300 is shorter than the fixture's own spread,
    // which would clip most doors in both legs at once). A pure function of
    // the fixture rows -- identical both legs. Named in the result line so
    // the ctest can assert the exact pose it got.
    double cx = 0, cy = 0, cz = 0;
    for (const auto& r : rows) { cx += r.x; cy += r.y; cz += r.z; }
    const float n = static_cast<float>(rows.size());
    const float camX = static_cast<float>(cx / n) + 45.0f;
    const float camY = static_cast<float>(cy / n);
    const float camZ = static_cast<float>(cz / n) + 25.0f;
    // Look straight at the centroid (down +inward).
    double dx = -45.0, dy = 0.0, dz = -25.0;
    const double dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float dirX = static_cast<float>(dx / dl);
    const float dirY = static_cast<float>(dy / dl);
    const float dirZ = static_cast<float>(dz / dl);

    // PVS region of the camera position (the production PVS update resolves
    // it the same way -- point lookup; see door_manager.cpp's
    // isRegionPvsVisible: no data means "assume visible", which a camera
    // with no PVS row degenerates to -- fine for the A/B, still reported).
    out.pvsRegion = static_cast<long>(
        data.bspTree->findRegionIndexForPoint(camX, camY, camZ));
    if (out.pvsRegion == static_cast<long>(SIZE_MAX)) out.pvsRegion = -1;

    // The production manager, wired exactly like
    // ZoneLifecycleComponent::ensureDoorManager(): the zone (door meshes
    // resolve through its objectGeometries), the texture cache, the BSP and
    // the PVS region under the camera. No frustum culler, no region
    // neighbors -- the PVS gate with no 1-depth expansion still gates
    // directly-visible regions, which is what a fixed-camera A/B needs.
    auto doors = std::make_unique<DoorManager>(data.smgr, data.driver);
    doors->setZone(data.zone);
    doors->setConstrainedTextureCache(data.textureCache.get());
    doors->setShaderMaterialTypes(data.zoneShader->getActiveSolid(),
                                  data.zoneShader->getActiveAlphaTest(),
                                  data.zoneShader->getActiveAlphaTestRGBA());
    doors->setBspTree(data.bspTree.get());
    doors->setPvsRegion(out.pvsRegion < 0 ? SIZE_MAX
                                          : static_cast<size_t>(out.pvsRegion));
    doors->setSceneGraphBatchEnabled(batchOn);

    out.doorsRequested = static_cast<int>(rows.size());
    std::unordered_set<unsigned int> created;
    for (const auto& r : rows) {
        const bool ok = doors->createDoor(
            static_cast<uint8_t>(r.doorid), r.name, r.x, r.y, r.z,
            r.heading, r.incline, static_cast<uint16_t>(r.size),
            static_cast<uint8_t>(r.opentype), r.doorIsOpen, r.doorParam);
        if (!ok) {
            fprintf(stderr, "[sg-batch-ab] door %u '%s' failed to build\n",
                    r.doorid, r.name.c_str());
            continue;
        }
        if (doors->isDoorMeshBuilt(static_cast<uint8_t>(r.doorid)))
            created.insert(r.doorid);
        // Invisible open-types (50/53/54) are skipped by design -- not a
        // draw, both legs agree.
    }

    out.doorsInGraph = countInGraph(*doors, created);
    if (out.doorsInGraph < minDoorsInGraph) {
        fprintf(stderr, "[sg-batch-ab] only %d doors in the scene graph "
                        "(need >= %d) -- camera/BSP/fixture problem "
                        "(pvsRegion=%ld)\n",
                out.doorsInGraph, minDoorsInGraph, out.pvsRegion);
        return false;
    }

    // The fixed deterministic lighting state, set on the SAME shader
    // instance both legs share. Doors are unlit materials (Lighting=false
    // in door_manager_create.cpp): their pixels are a pure function of
    // texture + transform. The values are setupLighting()'s own "midnight"
    // baseline (zone_bench_frame.cpp) -- a documented, fixed time of day:
    // sun off, player light OFF, no fog, fixed ambient/tint.
    {
        auto& zs = *data.zoneShader;
        zs.setAmbientColor(0.02f, 0.02f, 0.04f);
        zs.setSunDirection(0.0f, -1.0f, 0.0f);
        zs.setSunColor(0.0f, 0.0f, 0.0f);
        zs.setTintColor(0.15f, 0.15f, 0.2f);
        // No fog: the fog factor clamp((end - d)/(end - start)) = 1.0 for
        // any d when start=0, end=1 (d is view-space, >=0) -- the fragment
        // mix(uFogColor, lit, 1.0) = lit, so the fog color never appears.
        zs.setFog(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        // Player light OFF: color (0,0,0) makes the per-pixel light term
        // exactly 0 (setupCamera's Q03 comment); zero the point-light count
        // so NO zone light contributes either.
        data.playerLightColor[0] = data.playerLightColor[1] =
            data.playerLightColor[2] = 0.0f;
        data.playerLightAtten[0] = 1.0f;
        data.playerLightAtten[1] = 0.0f;
        data.playerLightAtten[2] = 0.0f;
        zs.setNumPointLights(0);
        zs.setNumFireGlowLights(0);
    }

    // The fixed camera -- identical both legs.
    setupCamera(data, camX, camY, camZ, dirX, dirY, dirZ);
    out.camX = camX; out.camY = camY; out.camZ = camZ;
    out.dirX = dirX; out.dirY = dirY; out.dirZ = dirZ;

    // A real ACTIVE CAMERA NODE, mirroring the manual matrices above. This
    // is not cosmetic: CSceneManager::drawAll() RESETS every transform to
    // identity and then re-establishes view/projection from the active
    // camera (CSceneManager.cpp drawAll + CCameraSceneNode::render). The
    // production zone path runs with an active camera (renderer_zone_draw
    // uses the same smgr active-camera idiom), so the per-node leg needs it
    // or drawAll() would project with an identity view and draw nothing.
    // Same pose, same fov, same near/far as setupCamera (60 deg / 1 / 300)
    // so both legs' projection matrices are bit-identical.
    irr::scene::ICameraSceneNode* camNode = data.smgr->addCameraSceneNode();
    if (!camNode) {
        fprintf(stderr, "[sg-batch-ab] cannot create the camera node\n");
        return false;
    }
    camNode->setPosition(irr::core::vector3df(camX, camZ, camY));  // irr y-up
    camNode->setTarget(irr::core::vector3df(camX + dirX, camZ + dirZ, camY + dirY));
    camNode->setUpVector(irr::core::vector3df(0, 1, 0));
    camNode->setFOV(60.0f * 3.14159265f / 180.0f);
    camNode->setNearValue(1.0f);
    camNode->setFarValue(300.0f);
    data.smgr->setActiveCamera(camNode);
    camNode->drop();  // the scene manager holds the reference

    // One rendered frame, production order (see the file header). The batch
    // draws at the SOLID-pre point INSIDE drawAll() -- the production
    // renderer_zone_draw.cpp runs renderSceneGraphBatch() from an
    // ILightManager hook (RenderPassTimer::OnRenderPassPreRender) that fires
    // after the active camera has set the 3D P/V and before the SOLID pass,
    // and restoreSceneGraphBatch() from the matching post hook. We install a
    // local hook doing the same, so both legs draw every door at the same
    // pass with the same P/V (the active camera node set above -- drawAll()
    // resets the manual matrices and re-derives P/V from it).
    doors->update(0.0f);  // no clock advances; static doors keep their pose
    out.batchedNodes = 0;
    out.batchGroups = 0;
    out.batchDraws = 0;
    if (batchOn) {
        doors->gatherSceneGraphBatch();  // phase 1 (renderer_frame_input.cpp)
        out.batchedNodes = doors->sgBatchedCount();
        out.batchGroups = doors->sgBatchGroups();
    }

    struct SgBatchAbPassHook : public irr::scene::ILightManager {
        EQT::Graphics::DoorManager* doors;
        int batchDraws = 0;
        void OnPreRender(irr::core::array<irr::scene::ISceneNode*>&) override {}
        void OnPostRender() override {}
        void OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS p) override {
            if (p == irr::scene::ESNRP_SOLID)
                batchDraws = doors->renderSceneGraphBatch();
        }
        void OnRenderPassPostRender(irr::scene::E_SCENE_NODE_RENDER_PASS p) override {
            if (p == irr::scene::ESNRP_SOLID)
                doors->restoreSceneGraphBatch();
        }
        void OnNodePreRender(irr::scene::ISceneNode*) override {}
        void OnNodePostRender(irr::scene::ISceneNode*) override {}
    } passHook;
    passHook.doors = doors.get();

    data.driver->beginScene(true, true, irr::video::SColor(255, 128, 128, 128));
    if (data.zoneShader) data.zoneShader->beginFrame();
    data.smgr->setLightManager(&passHook);  // the benchmark smgr has none
    data.smgr->drawAll();
    out.batchDraws = passHook.batchDraws;
    data.smgr->setLightManager(nullptr);
    data.driver->endScene();

    // Teardown BEFORE the capture: the shot is taken from the framebuffer,
    // not the scene graph, so detaching the door pivots (the production
    // clearDoors()) cannot affect it -- and it leaves the scene clean for
    // the other leg's re-creation in a second process.
    doors->clearDoors();

    out.pngPath = cfg.outDir + "/sgbatch" + (batchOn ? "1" : "0") + ".png";
    irr::video::IImage* shot = data.driver->createScreenShot();
    if (shot) {
        out.pngOk = data.driver->writeImageToFile(shot, out.pngPath.c_str());
        shot->drop();
    }
    if (!out.pngOk) {
        fprintf(stderr, "[sg-batch-ab] PNG write failed: %s\n",
                out.pngPath.c_str());
        return false;
    }

    printf("SG_BATCH_AB_LEG batch_on=%d png=%s doors_requested=%d "
           "doors_in_graph=%d pvs_region=%ld batched_nodes=%d batch_groups=%d "
           "batch_draws=%d "
           "cam=(%.3f,%.3f,%.3f) dir=(%.4f,%.4f,%.4f)\n",
           batchOn ? 1 : 0, out.pngPath.c_str(), out.doorsRequested,
           out.doorsInGraph, out.pvsRegion, out.batchedNodes, out.batchGroups,
           out.batchDraws,
           camX, camY, camZ, dirX, dirY, dirZ);
    return true;
}

} // namespace ZoneBench
