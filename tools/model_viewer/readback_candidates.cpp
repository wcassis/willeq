// tools/model_viewer/readback_candidates.cpp -- B1198 (#517): the readback
// candidates, one TU. The GL calls run on the harness render thread with the
// device's context current (the same thread and window the driver renders in);
// every path reads between endScene() and the next beginScene(), the window
// T61's deferred present keeps the just-rendered buffer current for.
//
// The flip + BGRA + createImageFromData is the driver's own algorithm
// (COpenGLES2Driver::createScreenShot, docker/irrlicht-drm/COpenGLES2Driver.cpp
// 2126-2172) run on the candidate's buffer, so a candidate's IImage is the
// driver path's image for the same readback bytes -- which is what makes
// `glread` and `pbo` provably pixel-identical to `driver` (the committed test
// says so for each frame it renders). `fbo` differs by the downscale filter
// (GPU blit vs the CPU bilinear) and `dirty` captures through the driver path,
// so its captured frames are identical by construction.

#include "readback_candidates.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace eqt {
namespace fxharness {
namespace readback {

// The distro's GLES2 header (libgles-dev) predates the ES 2.0 core promotion
// of OES_framebuffer_object's read/draw names and carries the blit and the
// mapbuffer functions only as extensions (gl2ext.h's glBlitFramebufferANGLE
// / glBlitFramebufferNV / glMapBufferRangeEXT / glUnmapBufferOES): define the
// core names when missing and resolve the blit entry point at context
// creation (Mesa's libGLESv2 exports all three spellings).
#if !defined(GL_READ_FRAMEBUFFER)
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#if !defined(GL_DRAW_FRAMEBUFFER)
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#if !defined(GL_NONE)
#define GL_NONE 0
#endif
#if !defined(GL_PIXEL_PACK_BUFFER)  // OES_mapbuffer; promoted in ES 3.0
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
typedef void (*PFNBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,
                                   GLenum);
typedef void* (*PFNMapBufferRangeEXT)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef void (*PFNUnmapBuffer)(GLenum);

namespace {

double msSince(const std::chrono::steady_clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// The driver's post-work on a bottom-up RGBA buffer of w x h (COpenGLES2Driver
// 2138-2160, line for line): the row flip (one temp row), then the per-pixel
// RGBA -> BGRA (= A8R8G8B8) channel order.
void flipAndConvertBgra(uint8_t* px, uint32_t w, uint32_t h) {
    const uint32_t rowSize = w * 4;
    std::vector<uint8_t> temp(rowSize);
    for (uint32_t y = 0; y < h / 2; ++y) {
        uint8_t* r1 = px + y * rowSize;
        uint8_t* r2 = px + (h - 1 - y) * rowSize;
        std::memcpy(temp.data(), r1, rowSize);
        std::memcpy(r1, r2, rowSize);
        std::memcpy(r2, temp.data(), rowSize);
    }
    for (uint32_t i = 0; i < w * h; ++i) {
        const uint8_t r = px[i * 4 + 0];
        const uint8_t g = px[i * 4 + 1];
        const uint8_t b = px[i * 4 + 2];
        const uint8_t a = px[i * 4 + 3];
        px[i * 4 + 0] = b;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = r;
        px[i * 4 + 3] = a;
    }
}

}  // namespace

bool parseMode(const std::string& name, Mode& out) {
    if (name.empty() || name == "driver") { out = Mode::Driver; return true; }
    if (name == "glread") { out = Mode::GlRead; return true; }
    if (name == "pbo") { out = Mode::Pbo; return true; }
    if (name == "fbo") { out = Mode::Fbo; return true; }
    if (name == "dirty") { out = Mode::Dirty; return true; }
    return false;
}
const char* modeName(Mode m) {
    switch (m) {
        case Mode::Driver: return "driver";
        case Mode::GlRead: return "glread";
        case Mode::Pbo: return "pbo";
        case Mode::Fbo: return "fbo";
        case Mode::Dirty: return "dirty";
    }
    return "?";
}

// ---------------------------------------------------------------------------

struct ReadbackContext::Impl {
    irr::video::IVideoDriver* driver = nullptr;
    Mode mode = Mode::Driver;
    uint32_t sw = 0, sh = 0;  // the surface (render target) size
    uint32_t tw = 0, th = 0;  // the requested frame size

    // glread / pbo: nothing persistent -- createImageFromData TAKES OWNERSHIP
    // of its buffer (B954: ownForeignMemory + deleteMemory; the image frees
    // it with delete[]), so each frame's readback buffer is a fresh new[]
    // (the driver's own pattern, COpenGLES2Driver 2136-2167), never a reused
    // one (a reused vector's storage would be freed twice: by the image and
    // by the vector).
    // pbo: the pack buffer (its storage is system memory on this GPU; the
    // glReadPixels writes into it and the map copy comes out of it) + the
    // resolved map/unmap entry points (Mesa exports the core spellings; the
    // distro header only declares the OES/EXT ones).
    GLuint pbo = 0;
    PFNMapBufferRangeEXT mapRange = nullptr;
    PFNUnmapBuffer unmap = nullptr;
    // fbo: the requested-size target + its RGBA8 texture (LINEAR, the blit
    // filter); blit = the resolved entry point.
    GLuint fbo = 0, fboTex = 0;
    PFNBlitFramebuffer blit = nullptr;
    // dirty: the probe rectangle (surface px) and its last captured bytes.
    int px0 = 0, py0 = 0, pw = 0, ph = 0;
    std::vector<uint8_t> probeBuf;
    std::vector<uint8_t> lastProbe;
    bool hasLast = false;

    std::ofstream cost;  // readback_cost.tsv (mode != driver)
};

ReadbackContext::ReadbackContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ReadbackContext::~ReadbackContext() = default;

std::unique_ptr<ReadbackContext> ReadbackContext::create(irr::video::IVideoDriver* driver, Mode mode,
                                                         uint32_t sw, uint32_t sh, uint32_t tw, uint32_t th,
                                                         const ProbeRect& probe,
                                                         const std::string& framesDir) {
    if (!driver || sw == 0 || sh == 0 || tw == 0 || th == 0) {
        std::fprintf(stderr, "[readback] create: bad sizes (%ux%u surface, %ux%u frame)\n", sw, sh, tw, th);
        return nullptr;
    }
    auto impl = std::unique_ptr<Impl>(new Impl());
    impl->driver = driver;
    impl->mode = mode;
    impl->sw = sw;
    impl->sh = sh;
    impl->tw = tw;
    impl->th = th;

    switch (mode) {
        case Mode::Driver:
            break;  // no state of its own: the driver call is the candidate
        case Mode::GlRead:
            break;  // per-frame new[] buffer (ownership note above)
        case Mode::Pbo:
            impl->mapRange = reinterpret_cast<PFNMapBufferRangeEXT>(eglGetProcAddress("glMapBufferRange"));
            impl->unmap = reinterpret_cast<PFNUnmapBuffer>(eglGetProcAddress("glUnmapBuffer"));
            if (!impl->mapRange || !impl->unmap) {
                std::fprintf(stderr, "[readback] pbo: no glMapBufferRange/glUnmapBuffer entry points\n");
                return nullptr;
            }
            glGenBuffers(1, &impl->pbo);
            if (impl->pbo == 0 || glGetError() != GL_NO_ERROR) {
                std::fprintf(stderr, "[readback] pbo: glGenBuffers failed (glError 0x%x)\n",
                             static_cast<unsigned>(glGetError()));
                return nullptr;
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, impl->pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, size_t(sw) * sh * 4, nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            if (glGetError() != GL_NO_ERROR) {
                std::fprintf(stderr, "[readback] pbo: buffer setup failed (glError 0x%x)\n",
                             static_cast<unsigned>(glGetError()));
                return nullptr;
            }
            break;
        case Mode::Fbo: {
            impl->blit = reinterpret_cast<PFNBlitFramebuffer>(eglGetProcAddress("glBlitFramebuffer"));
            if (!impl->blit)  // the vendor spellings, same entry point on Mesa
                impl->blit = reinterpret_cast<PFNBlitFramebuffer>(eglGetProcAddress("glBlitFramebufferANGLE"));
            if (!impl->blit)
                impl->blit = reinterpret_cast<PFNBlitFramebuffer>(eglGetProcAddress("glBlitFramebufferNV"));
            if (!impl->blit) {
                std::fprintf(stderr, "[readback] fbo: no glBlitFramebuffer entry point\n");
                return nullptr;
            }
            glGenTextures(1, &impl->fboTex);
            glBindTexture(GL_TEXTURE_2D, impl->fboTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &impl->fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl->fboTex, 0);
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr, "[readback] fbo: not complete (0x%x)\n", static_cast<unsigned>(status));
                return nullptr;
            }
            break;
        }
        case Mode::Dirty:
            if (!probe.valid || probe.empty() || probe.x1 > int(sw) || probe.y1 > int(sh)) {
                std::fprintf(stderr,
                             "[readback] dirty: no valid probe rectangle (the mode needs a fixed camera "
                             "and a scene-content box; the run's camera is %s)\n",
                             probe.valid ? "out of range" : "unavailable");
                return nullptr;
            }
            impl->px0 = probe.x0;
            impl->py0 = probe.y0;
            impl->pw = probe.x1 - probe.x0;
            impl->ph = probe.y1 - probe.y0;
            impl->probeBuf.resize(size_t(impl->pw) * impl->ph * 4);
            break;
    }
    if (mode != Mode::Driver && !framesDir.empty()) {
        impl->cost.open(framesDir + "/readback_cost.tsv");
        if (impl->cost.is_open())
            impl->cost << "frame\traw_ms\tcopyout_ms\tblit_ms\tpost_ms\tskipped\n";
    }
    return std::unique_ptr<ReadbackContext>(new ReadbackContext(std::move(impl)));
}

// The shared tail of the GlRead / Pbo / Fbo candidates: `px` is a fresh
// new[] buffer holding a bottom-up RGBA read of w x h; the flip + BGRA run on
// it and createImageFromData then TAKES IT (B954: ownForeignMemory +
// deleteMemory, the driver's own pattern). A failed createImageFromData
// leaves the buffer to the caller.
static irr::video::IImage* makeImage(irr::video::IVideoDriver* driver, uint8_t* px, uint32_t w, uint32_t h,
                                     double& postMs, bool& bufferTaken) {
    const auto t0 = std::chrono::steady_clock::now();
    flipAndConvertBgra(px, w, h);
    irr::video::IImage* image = driver->createImageFromData(
        irr::video::ECF_A8R8G8B8, irr::core::dimension2d<uint32_t>(w, h), px, true, true);
    postMs = msSince(t0);
    bufferTaken = image != nullptr;
    return image;
}

Capture ReadbackContext::capture(int f) {
    Capture cap;
    switch (impl_->mode) {
        case Mode::Driver:
            // The current path itself; the sub-phases live inside the driver
            // call (the harness times the whole thing as readback_ms).
            cap.image = impl_->driver->createScreenShot();
            break;
        case Mode::GlRead: {
            uint8_t* px = new uint8_t[size_t(impl_->sw) * impl_->sh * 4];
            const auto t0 = std::chrono::steady_clock::now();
            glReadPixels(0, 0, impl_->sw, impl_->sh, GL_RGBA, GL_UNSIGNED_BYTE, px);
            cap.rawMs = msSince(t0);
            if (glGetError() != GL_NO_ERROR)
                std::fprintf(stderr, "[readback] glread: frame %d glReadPixels error 0x%x\n", f,
                             static_cast<unsigned>(glGetError()));
            bool taken = false;
            cap.image = makeImage(impl_->driver, px, impl_->sw, impl_->sh, cap.postMs, taken);
            if (!taken) delete[] px;
            break;
        }
        case Mode::Pbo: {
            const size_t bytes = size_t(impl_->sw) * impl_->sh * 4;
            uint8_t* px = new uint8_t[bytes];
            const auto t0 = std::chrono::steady_clock::now();
            glBindBuffer(GL_PIXEL_PACK_BUFFER, impl_->pbo);
            glReadPixels(0, 0, impl_->sw, impl_->sh, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            cap.rawMs = msSince(t0);
            const auto t1 = std::chrono::steady_clock::now();
            void* mapped = impl_->mapRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                                           GL_MAP_READ_BIT);
            if (!mapped) {
                std::fprintf(stderr, "[readback] pbo: frame %d map failed (glError 0x%x)\n", f,
                             static_cast<unsigned>(glGetError()));
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                delete[] px;
                break;
            }
            std::memcpy(px, mapped, bytes);
            impl_->unmap(GL_PIXEL_PACK_BUFFER);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            cap.copyOutMs = msSince(t1);
            bool taken = false;
            cap.image = makeImage(impl_->driver, px, impl_->sw, impl_->sh, cap.postMs, taken);
            if (!taken) delete[] px;
            break;
        }
        case Mode::Fbo: {
            const size_t bytes = size_t(impl_->tw) * impl_->th * 4;
            uint8_t* px = new uint8_t[bytes];
            const auto t0 = std::chrono::steady_clock::now();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, impl_->fbo);
            impl_->blit(0, 0, impl_->sw, impl_->sh, 0, 0, impl_->tw, impl_->th, GL_COLOR_BUFFER_BIT,
                        GL_LINEAR);
            cap.blitMs = msSince(t0);
            const auto t1 = std::chrono::steady_clock::now();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, impl_->fbo);
            glReadPixels(0, 0, impl_->tw, impl_->th, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            cap.rawMs = msSince(t1);
            if (glGetError() != GL_NO_ERROR)
                std::fprintf(stderr, "[readback] fbo: frame %d error 0x%x\n", f,
                             static_cast<unsigned>(glGetError()));
            bool taken = false;
            cap.image = makeImage(impl_->driver, px, impl_->tw, impl_->th, cap.postMs, taken);
            if (!taken) delete[] px;
            break;
        }
        case Mode::Dirty: {
            const auto t0 = std::chrono::steady_clock::now();
            glReadPixels(impl_->px0, impl_->py0, impl_->pw, impl_->ph, GL_RGBA, GL_UNSIGNED_BYTE,
                         impl_->probeBuf.data());
            cap.rawMs = msSince(t0);
            const bool clean = impl_->hasLast &&
                               std::memcmp(impl_->probeBuf.data(), impl_->lastProbe.data(), impl_->probeBuf.size()) == 0;
            if (clean) {
                cap.skipped = true;
            } else {
                // The full current path: captured frames ARE the driver path's
                // frames (byte-identical by construction).
                cap.image = impl_->driver->createScreenShot();
                impl_->lastProbe = impl_->probeBuf;
                impl_->hasLast = true;
            }
            break;
        }
    }
    return cap;
}

void ReadbackContext::logCost(int f, const Capture& cap) {
    if (impl_->mode == Mode::Driver || !impl_->cost.is_open()) return;
    impl_->cost << f << '\t' << cap.rawMs << '\t' << cap.copyOutMs << '\t' << cap.blitMs << '\t'
                << cap.postMs << '\t' << (cap.skipped ? 1 : 0) << '\n';
}

}  // namespace readback
}  // namespace fxharness
}  // namespace eqt
