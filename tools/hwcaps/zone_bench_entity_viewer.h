#pragma once

// P47 -- magenta entity viewer: renders ONE already-loaded race model LARGE
// against a solid magenta (255,0,255) clear color, at a small fixed set of
// camera angles, with NO zone/object geometry ever drawn. See
// refactor/P47_magenta_entity_viewer.md.
//
// Reading rule: magenta visible INSIDE the model's silhouette bbox = an
// alpha gap (a masked texture's palette-index-0 cutout rendering correctly).
// This instrument exists because P37's masked-alpha fix could not be
// demonstrated by a diff-crop/before-after pair (5 rejected rounds --
// P44/P44b/P46/P46b/P46c/P46d) -- a human can judge "is there magenta
// inside the silhouette" in two seconds at full size, no diff required.
//
// This is substantial new logic split into its own TU per the plan's
// instruction (tools/hwcaps/zone_bench_loader.cpp is already over the
// repo's file-size threshold -- see refactor/F_file_split.md convention).

#include <cstdint>
#include <string>
#include <vector>

#include "zone_bench_loader.h"

namespace ZoneBench {

struct EntityViewerConfig {
    uint16_t raceId = 1;
    // Cosmetic only -- used for PNG filenames and log lines (e.g. "SKE").
    // Falls back to the numeric race ID if left empty.
    std::string raceLabel;
    // Cosmetic + regression-pin bookkeeping: "rgba" or "etc1" (whichever
    // leg the caller configured before loadZone() -- see the CLI dispatch
    // in gles2_zone_benchmark.cpp for how the two legs' BenchConfig flags
    // differ: RGBA leg forces noEntityShaderPromotion=true per P46c/P46d's
    // proven one-variable configuration; ETC1 leg leaves promotion ON,
    // required to bind the companion alpha texture, P46b).
    std::string modeLabel;
    // Deterministic pose: animFrame * 33.3ms, the same animation clock used
    // by every prior unit in this evidence chain (P44b/P46d) -- a pure
    // function of this integer, not wall-clock time.
    int animFrame = 0;
    // Directory to write PNGs into. Must already exist.
    std::string outDir;
};

struct EntityViewerAngleResult {
    std::string angleName;
    std::string pngPath;
    bool ok = false;         // PNG write succeeded
    // Count of magenta (255,0,255) pixels topologically ENCLOSED by
    // non-magenta pixels (a border flood-fill from the frame edge through
    // magenta pixels marks everything reachable as "background"; whatever's
    // left is a true hole -- a rib gap, an eye socket, etc.). This is NOT
    // "all magenta inside bboxX0..Y1" -- a naive whole-rectangle count was
    // tried and rejected (see zone_bench_entity_viewer.cpp): the natural
    // background beside a non-convex silhouette (a diagonal pose, the gap
    // between separated legs) swamps the signal and does not go to exactly
    // zero for an unmasked control. bboxX0..Y1 (below) is still the tight
    // silhouette bbox, kept for documentation/cropping only.
    int magentaInBbox = 0;
    int bboxX0 = 0, bboxY0 = 0, bboxX1 = 0, bboxY1 = 0;
};

// Renders data.entities[0]/data.animatedNodes[0] (must already be set up by
// loadZone() with entityCount=1, testRaceIds={cfg.raceId}, animate=true --
// see the caller in gles2_zone_benchmark.cpp) at a fixed set of camera
// angles (front/right/back/left) around the model, framed so the model
// fills most of the frame height/width. NO zone or object draw entries are
// ever collected or drawn in this mode -- only the entity's own mesh
// buffers, against a solid magenta clear -- so "loads no zone geometry" is
// satisfied visually even though loadZone() still loaded the named zone's
// data for shader/texture-cache/race-loader plumbing (see plan Review for
// why this was the chosen interpretation).
std::vector<EntityViewerAngleResult> runEntityViewer(
    ZoneBenchData& data, const EntityViewerConfig& cfg);

} // namespace ZoneBench
