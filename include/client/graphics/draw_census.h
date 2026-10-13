#pragma once

// B997 (GitLab #317): per-site 3D-draw census, the card's step-2 evidence.
//
// Step 1 (BASELINE.md) established that Mesa's steady draw-time BO pool (the
// gpu_unnamed_steady guard's dominant row) is sized by the per-draw varying
// BO (docs/reference/lima_cost_sheet.md fact 5) -- and B1013 (fact 13) the
// deeper mechanism: that pool is a Lima BO cache frozen at the zone-in peak,
// so a steady-state lever shows up in gem_pool_growth_mib /
// gem_pool_creates_steady_window, not in the steady draw count -- and that
// the `draw3d:client`
// client-array pool is the dominant 3D pool (q2 1465 BOs / 9.66 MiB, gd 1419 /
// 11.36 MiB) with the zone-merge's own `draw:zone` share a minority (62 / 1.27
// q2, 148 / 4.08 gd). The gem_owners shim already splits the pool by tag
// (draw:zone|draw3d:client, draw3d:client generic, vbo:impostor,
// vbo:particles, vbo:nametags). What it does NOT split is the GENERIC
// `draw3d:client` pool -- the untagged 3D client-array draws -- which is where
// the lever is. THIS header splits that generic pool by site.
//
// The generic `draw3d:client` pool is fed by three kinds of draw:
//   zone_draws  -- the zone draw path (renderMerged pooled draws when the
//                  merge is built, otherwise the unmerged sorted-list draws;
//                  these also carry the draw:zone tag, reported in the
//                  gem_owners draw:zone row -- counted here so scene_graph
//                  excludes them).
//   entity_body -- entity body mesh buffers (EQAnimatedMeshSceneNode::render,
//                  drawMeshBuffer on a buffer NOT in the driver's HWBufferMap
//                  -> the client-array fallback path).
//   scene_graph -- (derived, not a counter) doors + attached equipment +
//                  target overlays + other CMeshSceneNode 3D client draws the
//                  client does not directly instrument:
//                        scene_graph = win_d3client - (zone_draws + entity_body).
//
// Impostors, particles and nametags draw through the VBO path (vbo:impostor /
// vbo:particles / vbo:nametags pools), reported in the driver's d3vbo total +
// gem_owners, so they are NOT counted here and do not enter scene_graph.
//
// WINDOWS: the driver's [drawcensus] line (COpenGLES2Driver.cpp) reports
// window-scoped 3D draw counts and resets every N frames (N is the window:
// 120 by default, EQT_DRAW_CENSUS_WINDOW overrides it -- ONE parse rule,
// drawCensusWindowFromEnv below, so the client's [drawsite] window and the
// driver's [drawcensus] window can never disagree). This header's counters
// are the client's WINDOW-scoped site counts; drawCensusEndFrame() is called
// every frame (from the same endSceneAndSwap point) and, on the driver's
// window boundary, emits [drawsite] and resets the site window. Both streams
// are therefore the same-size windows on the same endScene cadence, so
// post-processing pairs the i-th [drawsite] line with the i-th [drawcensus]
// line for a per-window draws/frame + per-site split of a steady-state window
// (steady_60s in the Pi rig).
//
// Counters are window-scoped on the render thread (no atomics: every increment
// and the emit happen on the render thread). Gated behind EQT_DRAW_CENSUS:
// when off, the add/emit calls are one predicted-false branch (the
// gl_call_counters.h / g_glCounters precedent).

#include <cstdio>
#include <cstdlib>

#ifdef EQT_HAS_GLES2
// B997: the driver-side bridge that exposes the just-closed [drawcensus]
// window to the render thread (defined in COpenGLES2Driver.cpp, the extern
// precedent beside gles2Draw3DLinesBatch -- a plain C++ extern, NOT extern
// "C", matching the bridge's definition). Declared here so
// drawCensusEndFrame() can read the snapshot without the TU re-declaring it.
extern void gles2GetDrawCensusTotals(void* driver, unsigned long* winD3client,
                                     unsigned long* winD3vbo, unsigned long* frames,
                                     bool* newWindow);
// B1013: the matching setter -- the client pushes the window it parsed from
// EQT_DRAW_CENSUS_WINDOW (drawCensusWindowFromEnv, ONE parse rule) into the
// driver's census before the driver's first window close. The driver's
// boundary test and the client's [drawsite] cadence share it, so the two
// streams can never run on different window sizes.
extern void gles2SetDrawCensusWindow(void* driver, unsigned long frames);
#endif

namespace EQT {
namespace Graphics {

// B1013: the ONE parse rule for EQT_DRAW_CENSUS_WINDOW (default 120, the
// pre-B1013 behaviour): null, empty, non-numeric, 0 or negative -> 120;
// otherwise the value clamped to [1, 100000]. The driver's [drawcensus]
// window and the client's [drawsite] window both use this, so a bad value
// can never make one of them degenerate (a 0-frame window divides by zero
// in the post-processing) or silently disagree with the other.
inline unsigned long drawCensusWindowFromEnv(const char* v) {
    static const unsigned long kDefault = 120;
    static const unsigned long kMax = 100000;
    if (!v || !*v) return kDefault;
    char* end = nullptr;
    const long n = std::strtol(v, &end, 10);
    if (end == v || (end && *end) || n <= 0) return kDefault;
    const unsigned long w = (unsigned long)n;
    return w > kMax ? kMax : w;
}

struct DrawCensus {
    bool enabled = false;
    unsigned long window = 120;  // frames per [drawsite]/[drawcensus] window (EQT_DRAW_CENSUS_WINDOW)
    long zoneDraws = 0;   // window-scoped
    long entityBody = 0;  // window-scoped
};

// Render-thread singleton: one process, one render thread, one driver.
inline DrawCensus& drawCensus() {
    static DrawCensus c = [] {
        DrawCensus r;
        const char* env = std::getenv("EQT_DRAW_CENSUS");
        r.enabled = env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y');
        const char* win = std::getenv("EQT_DRAW_CENSUS_WINDOW");
        r.window = drawCensusWindowFromEnv(win);
        return r;
    }();
    return c;
}

inline void drawCensusAddZone(long n) {
    auto& c = drawCensus();
    if (c.enabled) c.zoneDraws += n;
}
inline void drawCensusAddEntity(long n) {
    auto& c = drawCensus();
    if (c.enabled) c.entityBody += n;
}

// Emit the windowed per-site census when the driver just closed a 120-frame
// window (on the same endScene cadence as its own [drawcensus] line).
// `winD3client` / `winD3vbo` / `driverFrames` are the driver's just-closed
// window totals (read by the caller through gles2GetDrawCensusTotals(), which
// also returns the newWindow flag). Called every frame; emits + resets the
// site window only when the driver has just closed a window. `scene_graph` =
// winD3client - (zone_draws + entity_body): the un-instrumented 3D client
// draws (doors, equipment, overlays).
inline bool drawCensusEmit(bool newWindow, long winD3client, long winD3Vbo,
                           long driverFrames) {
    auto& c = drawCensus();
    if (!c.enabled || !newWindow) return false;
    const long counted = c.zoneDraws + c.entityBody;
    const long sceneGraph = winD3client > counted ? winD3client - counted : 0;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[drawsite] frames=%ld zone_draws=%ld entity_body=%ld "
                  "scene_graph=%ld win_3d_client=%ld win_3d_vbo=%ld\n",
                  driverFrames, c.zoneDraws, c.entityBody, sceneGraph,
                  winD3client, winD3Vbo);
    std::fprintf(stderr, "%s", buf);
    c.zoneDraws = 0;
    c.entityBody = 0;
    return true;
}

// G05 (frame_render_order golden) requires the census to touch exactly ONE
// statement in endSceneAndSwap() -- the golden is a position-by-position
// text-parse of renderer_frame_render.cpp, and a multi-statement emit block
// shifts the composed order. drawCensusEndFrame() is that one statement: it
// pushes the client's parsed window into the driver ONCE (function-local
// static -- before the first totals read, so the driver's window boundary is
// set before the first window can close), reads the driver's just-closed
// [drawcensus] window through the gles2 bridge and emits the client's
// [drawsite] split on the same boundary. No-op when the census is off or no
// window just closed. Only the GLES2 build calls it (and the bridge it
// reads), so the body is GLES2-only.
#ifdef EQT_HAS_GLES2
inline void drawCensusEndFrame(void* driver) {
    static bool window_pushed = [] {
        gles2SetDrawCensusWindow(nullptr, drawCensus().window);
        return true;
    }();
    (void)window_pushed;
    unsigned long wc = 0, wv = 0, fr = 0;
    bool newWin = false;
    gles2GetDrawCensusTotals(driver, &wc, &wv, &fr, &newWin);
    drawCensusEmit(newWin, static_cast<long>(wc), static_cast<long>(wv),
                   static_cast<long>(fr));
}
#endif

} // namespace Graphics
} // namespace EQT
