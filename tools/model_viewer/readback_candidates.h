// tools/model_viewer/readback_candidates.h -- B1198 (#517): the FX harness's
// frame-capture READBACK candidates, measured against the current path.
//
// The B1193 writer thread took the encode + write off the render thread; the
// render thread now pays render + the createScreenShot readback only, and the
// readback is ~57-59 ms/frame at 1280x800 on the ThinkPad (the GBM surface is
// 3840x2160 there -- the readback is the whole surface, 33.2 MB/frame).
//
// This unit is measurement tooling for the diagnose leg: the same capture the
// harness already does (after driver->endScene(), on the render thread), with
// five interchangeable paths, each producing the same A8R8G8B8 IImage the
// FrameWriter consumes:
//
//   driver  -- COpenGLES2Driver::createScreenShot(): glReadPixels of the whole
//              surface + CPU row flip + per-pixel RGBA->BGRA + createImageFromData
//              (the current path; the baseline).
//   glread  -- the harness's own glReadPixels of the whole surface (the GL
//              readback call in isolation), then the same flip + BGRA +
//              createImageFromData OUTSIDE the GL call: the split between "the
//              flush + the bytes" and "the CPU post-work".
//   pbo     -- glReadPixels into a GL_PIXEL_PACK_BUFFER, then glMapBufferRange
//              + copy out, then the same post-work: the driver's PBO readback
//              (its storage is system memory on this GPU; the map copy is the
//              price).
//   fbo     -- glBlitFramebuffer the whole surface into a 1280x800 (the
//              REQUESTED frame size) FBO texture on the GPU, then glReadPixels
//              the FBO (4.1 MB, not 33.2) + the same post-work. The pixels are
//              the GPU's downscale, not the harness's CPU bilinear: they differ
//              from the current path by the filter (refactor/B1198/README.md
//              states exactly which pixels and why, with a frame).
//   dirty   -- every frame a small glReadPixels PROBE (the scene-content
//              rectangle, surface px); when the probe bytes equal the last
//              captured frame's probe the frame is SKIPPED (no capture at
//              all), otherwise the full `driver` readback runs (captured
//              frames are therefore byte-identical to the current path).
//
// Every candidate keeps the per-frame row in frame_cost.tsv (B1193's schema:
// readback_ms = the whole candidate phase on the render thread) and, for the
// non-driver candidates, a sub-phase row in readback_cost.tsv (frame, raw_ms
// = the GL readback call -- the probe itself for dirty -- copyout_ms = the
// PBO map copy, blit_ms = the FBO blit, post_ms = the CPU flip + BGRA +
// IImage, skipped = the dirty frame was not captured).

#ifndef WILLEQ_TOOLS_MODEL_VIEWER_READBACK_CANDIDATES_H
#define WILLEQ_TOOLS_MODEL_VIEWER_READBACK_CANDIDATES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "irrlicht.h"

namespace eqt {
namespace fxharness {
namespace readback {

enum class Mode { Driver, GlRead, Pbo, Fbo, Dirty };

// " " (the default) or "driver" -> Driver; "glread", "pbo", "fbo", "dirty".
// False on anything else.
bool parseMode(const std::string& name, Mode& out);
const char* modeName(Mode m);

// The dirty-gate probe rectangle in SURFACE pixels (the full framebuffer the
// readback reads); (x0, y0) inclusive, (x1, y1) exclusive. !valid = the mode
// needs no probe (every mode but Dirty) or the caller could not compute a
// content rectangle (the Dirty mode then refuses to start).
struct ProbeRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool valid = false;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

// One frame's capture result. image = null when skipped (a clean dirty
// frame); the sub-phase ms fill readback_cost.tsv (driver mode: all zero, the
// split lives inside the driver call).
struct Capture {
    irr::video::IImage* image = nullptr;
    bool skipped = false;
    double rawMs = 0.0;     // the GL readback call (glReadPixels / the probe)
    double copyOutMs = 0.0; // pbo: glMapBufferRange + memcpy + unmap
    double blitMs = 0.0;    // fbo: glBlitFramebuffer
    double postMs = 0.0;    // the CPU flip + BGRA + createImageFromData
    double totalMs() const { return rawMs + copyOutMs + blitMs + postMs; }
};

// Owns the candidate's GL state and buffers (created once before the frame
// loop with the surface size the driver actually renders, destroyed after it).
// The context must be used only on the thread that owns the GL context (the
// harness render thread), between endScene() and the next beginScene().
class ReadbackContext {
public:
    // driver = the harness's IVideoDriver; mode = the candidate; sw/sh = the
    // SURFACE size (the driver's render target, e.g. 3840x2160); tw/th = the
    // REQUESTED frame size (the FBO mode's target); probe = the Dirty mode's
    // content rectangle in surface px (ignored by the other modes); framesDir
    // = where readback_cost.tsv is opened (mode != Driver only). Null with an
    // error on stderr when the GL state cannot be created (the FBO is not
    // complete, a PBO is missing) or when Dirty has no valid probe.
    static std::unique_ptr<ReadbackContext> create(irr::video::IVideoDriver* driver, Mode mode,
                                                   uint32_t sw, uint32_t sh, uint32_t tw, uint32_t th,
                                                   const ProbeRect& probe, const std::string& framesDir);
    ~ReadbackContext();

    ReadbackContext(const ReadbackContext&) = delete;
    ReadbackContext& operator=(const ReadbackContext&) = delete;

    // Capture frame `f` (called after endScene()).
    Capture capture(int f);

    // readback_cost.tsv (opened in framesDir at create() when mode != Driver).
    // Call once per frame with the capture's sub-phases.
    void logCost(int f, const Capture& cap);

private:
    struct Impl;
    explicit ReadbackContext(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace readback
}  // namespace fxharness
}  // namespace eqt

#endif  // WILLEQ_TOOLS_MODEL_VIEWER_READBACK_CANDIDATES_H
