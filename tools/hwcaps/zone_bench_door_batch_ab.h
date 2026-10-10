#pragma once
//
// zone_bench_door_batch_ab.h — B997 (GitLab #317) step 2: the DETERMINISTIC
// x86 pixel A/B for the per-material scene-graph door batch (EQT_SG_BATCH).
//
// The live-client frame diff is NOT a channel (refactor/B997/README.md:
// 0/18 frames identical across two baseline legs — the crowd tour is not
// frame-locked). This instrument IS one: it renders ONLY the zone's doors,
// through the production door path (the same DoorManager + PVS gating the
// client uses, src/client/graphics/door_manager*), against a flat clear
// color at a FIXED camera and a FIXED deterministic lighting state, and
// compares the two legs pixel-for-pixel:
//
//   leg A (EQT_SG_BATCH=0): every PVS door is drawn by smgr->drawAll()
//                           once per node (the baseline).
//   leg B (EQT_SG_BATCH=1): the PVS doors the batch takes are gathered by
//                           the production gatherSceneGraphBatch(), DETACHED
//                           from the scene graph, drawn as one gathered
//                           client-array draw per material group at the
//                           SOLID-pre point (renderSceneGraphBatch()), then
//                           re-attached after the SOLID pass
//                           (restoreSceneGraphBatch()) -- exactly the frame
//                           order production uses (renderer_frame_input.cpp,
//                           renderer_zone_draw.cpp).
//
// Both legs share the SAME loaded scene, the SAME camera matrices, the SAME
// shader uniforms and the SAME textures; only the draw path differs.
// Determinism levers (all fixed, none wall-clock):
//   - no server, no NPCs: door placement comes from a committed fixture
//     (refactor/B997/data/doors_<zone>.txt, the eqemu `doors` table rows the
//     server serialises into SpawnDoor), applied through
//     DoorManager::createDoor() -- the same call the client makes for a
//     door packet (application_zone_loading.cpp).
//   - fixed time of day / lighting: ZoneShaderManager sun OFF, player light
//     OFF, zero fog, fixed ambient/tint; doors are unlit materials
//     (Lighting=false, door_manager_create.cpp) so their pixels are a pure
//     function of texture + transform.
//   - update(0.0): no animation clock advances, so a static door's world
//     transform is a pure function of the fixture row.
//   - one scene, one camera, same driver, same textures, two draw passes.
//
// The verdict is byte-comparison of the two rendered frames (RGB); the
// result struct carries the pixel statistics (max channel diff, differing
// pixel count) and the door counts. The ctest pin
// (tests/test_sg_batch_door_pixel_ab.cpp) shells out twice (one leg per
// process, mirroring how the production env-var gate reads EQT_SG_BATCH)
// and additionally byte-compares the PNGs it writes.
//
// gles2_zone_benchmark only: it needs the loadZone() pipeline and the
// surfaceless-EGL device it creates.

#include <string>

#include "zone_bench_loader.h"

namespace ZoneBench {

struct SgBatchAbConfig {
    // The zone's door fixture (TAB-separated, see
    // refactor/B997/data/doors_<zone>.txt). The rows are the eqemu `doors`
    // table (doorid, name, pos_x, pos_y, pos_z, heading, incline, size,
    // opentype, doorisopen, invert_state, door_param).
    std::string doorFixturePath;
    // Directory to write the leg PNG into (must already exist).
    std::string outDir;
    // The EQ client dir + zone name, used to re-load the zone's _obj.s3d
    // geometry definitions (the door meshes resolve through
    // S3DZone::objectGeometries, which loadZone() does not keep -- the
    // benchmark discards the object placements, only the geometry defs
    // matter, and the doors are drawn through the production path).
    std::string eqPath;
    std::string zoneName;
};

struct SgBatchAbResult {
    std::string pngPath;
    bool pngOk = false;
    // The doors the fixture asked for, and the doors that ended up in the
    // scene graph (the drawn population; identical across legs).
    int doorsRequested = 0;
    int doorsInGraph = 0;
    // The batch's own census for the frame (leg B; all zero on leg A).
    int batchedNodes = 0;
    int batchGroups = 0;
    int batchDraws = 0;
    // The PVS region the fixed camera resolved to (SIZE_MAX printed as -1
    // means "no PVS data -> assume visible", which the test still accepts).
    long pvsRegion = -1;
    // The fixed camera that was actually used (printed verbatim).
    float camX = 0, camY = 0, camZ = 0;
    float dirX = 0, dirY = 0, dirZ = 0;
};

// Renders ONE leg of the A/B. `batchOn` is the leg's EQT_SG_BATCH value;
// the tool process itself is the gate (the ctest pins one leg per process,
// so the env var the production site reads is what the test sets -- this
// parameter exists so the in-process legs stay testable too).
//
// The camera is DERIVED from the fixture (the doors' centroid, at a fixed
// offset and pitch -- a pure function of the fixture rows, no tuning):
// it is named in the SG_BATCH_AB_RESULT line so the test can assert it, and
// minDoorsInGraph fails the leg when fewer than that many doors are in the
// scene graph (the "doors are in frame" proof, alongside the drawn-pixel
// check the ctest does on the PNG).
//
// Returns false (and prints why) on any setup failure: no BSP, no zone
// shader, a fixture that does not parse, fewer than minDoorsInGraph doors
// in the scene graph, or a failed PNG write. A setup failure is a test
// ERROR, never a verdict.
bool runSgBatchAbLeg(ZoneBenchData& data, bool batchOn,
                     const SgBatchAbConfig& cfg, int minDoorsInGraph,
                     SgBatchAbResult& out);

} // namespace ZoneBench
