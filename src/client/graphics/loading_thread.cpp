#include "client/graphics/loading_thread.h"
#include "client/graphics/overlays/loading_frame_layout.h"  // B1088: the shared draw body
#include "common/thread_name.h"  // T504 thread names; B1039: Linux + macOS forms

#include <chrono>
#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#else
#include <GL/glx.h>
#endif

#include "common/logging.h"
#include "client/core/memory_ledger.h"

namespace EQT {
namespace Graphics {

LoadingThread::LoadingThread() = default;

LoadingThread::~LoadingThread() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

GLContextHandles LoadingThread::extractGLHandles(irr::IrrlichtDevice* device,
                                                  irr::video::IVideoDriver* driver) {
    GLContextHandles handles;

    if (!device || !driver) return handles;

#ifdef EQT_HAS_GLES2
    // EGL: query current context (avoids dependency on internal Irrlicht headers)
    handles.backend = GLContextHandles::Backend::EGL;
    handles.eglDisplay = eglGetCurrentDisplay();
    handles.eglContext = eglGetCurrentContext();
    handles.eglSurface = eglGetCurrentSurface(EGL_DRAW);
    LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: extracted EGL handles (display={}, surface={}, context={})",
              (void*)handles.eglDisplay, (void*)handles.eglSurface, (void*)handles.eglContext);
    if (handles.eglDisplay == EGL_NO_DISPLAY || handles.eglContext == EGL_NO_CONTEXT) {
        LOG_WARN(MOD_GRAPHICS, "LoadingThread: no current EGL context");
        handles.backend = GLContextHandles::Backend::Unknown;
    }
#else
    // GLX: query current context and get display/drawable from Irrlicht exposed data
    handles.backend = GLContextHandles::Backend::GLX;
    handles.x11Display = (void*)glXGetCurrentDisplay();
    handles.glxContext = (void*)glXGetCurrentContext();
    handles.x11Window = (unsigned long)glXGetCurrentDrawable();
    LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: extracted GLX handles (display={}, context={}, window={})",
              handles.x11Display, handles.glxContext, handles.x11Window);
    if (!handles.x11Display || !handles.glxContext) {
        LOG_WARN(MOD_GRAPHICS, "LoadingThread: no current GLX context");
        handles.backend = GLContextHandles::Backend::Unknown;
    }
#endif

    return handles;
}

void LoadingThread::releaseContext(const GLContextHandles& handles) {
    switch (handles.backend) {
#ifndef EQT_HAS_GLES2
        case GLContextHandles::Backend::GLX:
            glXMakeCurrent(static_cast<Display*>(handles.x11Display), None, nullptr);
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: released GLX context");
            break;
#else
        case GLContextHandles::Backend::EGL:
            eglMakeCurrent(handles.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: released EGL context");
            break;
#endif
        default:
            // Software renderer — no GL context to release, this is fine
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: no GL context to release (software renderer)");
            break;
    }
}

bool LoadingThread::acquireContext(const GLContextHandles& handles) {
    switch (handles.backend) {
#ifndef EQT_HAS_GLES2
        case GLContextHandles::Backend::GLX: {
            Bool ok = glXMakeCurrent(static_cast<Display*>(handles.x11Display),
                                     handles.x11Window,
                                     static_cast<GLXContext>(handles.glxContext));
            if (!ok) {
                LOG_ERROR(MOD_GRAPHICS, "LoadingThread: glXMakeCurrent failed");
                return false;
            }
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: acquired GLX context");
            return true;
        }
#else
        case GLContextHandles::Backend::EGL: {
            EGLBoolean ok = eglMakeCurrent(handles.eglDisplay, handles.eglSurface,
                                           handles.eglSurface, handles.eglContext);
            if (!ok) {
                LOG_ERROR(MOD_GRAPHICS, "LoadingThread: eglMakeCurrent failed (error 0x{:04X})",
                          eglGetError());
                return false;
            }
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: acquired EGL context");
            return true;
        }
#endif
        default:
            // Software renderer — no GL context needed, proceed normally
            LOG_DEBUG(MOD_GRAPHICS, "LoadingThread: no GL context to acquire (software renderer)");
            return true;
    }
}

void LoadingThread::start(irr::IrrlichtDevice* device,
                           irr::video::IVideoDriver* driver,
                           irr::gui::IGUIFont* font,
                           const GLContextHandles& handles,
                           LoadingStatus& status,
                           ActivePhaseCallback activeCallback) {
    if (running_.load()) {
        LOG_WARN(MOD_GRAPHICS, "LoadingThread: already running");
        return;
    }

    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&LoadingThread::threadFunc, this, device, driver, font, handles,
                           std::ref(status), std::move(activeCallback));
}

void LoadingThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_relaxed);
}

void LoadingThread::threadFunc(irr::IrrlichtDevice* device,
                                irr::video::IVideoDriver* driver,
                                irr::gui::IGUIFont* font,
                                GLContextHandles handles,
                                LoadingStatus& status,
                                ActivePhaseCallback activeCallback) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::ZoneMesh);  // T491
    // B909: the owner row for what the loading thread charges to its DEFAULT
    // category (a step with no scope of its own) under WILLEQ_LEDGER_OWNERS=zone_mesh.
    eqt::memory::StartupScope threadOwner("zm.load_thread_default");
    eqt::setCurrentThreadName("eq-load");  // T504: visible in top -H and the [srcchar] parse lines
    // Acquire GL context on this thread
    if (!acquireContext(handles)) {
        LOG_ERROR(MOD_GRAPHICS, "LoadingThread: failed to acquire GL context, aborting");
        status.loadingComplete.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_relaxed);
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "LoadingThread: started, running sequential loader");

    // Execute the loading callback (zone loading + renderer setup).
    // This MUST succeed — all required assets must load or the client cannot function.
    try {
        if (activeCallback) {
            activeCallback(status);
        }
    } catch (const std::exception& e) {
        LOG_FATAL(MOD_GRAPHICS, "LoadingThread: zone loading failed: {}", e.what());
        releaseContext(handles);
        status.loadingComplete.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_relaxed);
        status.quitRequested.store(true, std::memory_order_release);
        return;
    } catch (...) {
        LOG_FATAL(MOD_GRAPHICS, "LoadingThread: zone loading failed with unknown exception");
        releaseContext(handles);
        status.loadingComplete.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_relaxed);
        status.quitRequested.store(true, std::memory_order_release);
        return;
    }
    FlushThreadLog();

    // Release GL context before signaling completion
    releaseContext(handles);

    LOG_INFO(MOD_GRAPHICS, "LoadingThread: complete, GL context released");
    status.loadingComplete.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_relaxed);
}

// B1088 (#408): the one body for both loading screens. This static is kept
// (the render thread calls it with LoadingStatus's own values, application
// passes the font in), but the LAYOUT lives in
// include/client/graphics/overlays/loading_frame_layout.h, the same function
// OverlaysComponent::drawLoadingScreen goes through -- the two screens cannot
// drift apart.
void LoadingThread::drawLoadingFrame(irr::video::IVideoDriver* driver,
                                      irr::gui::IGUIFont* font,
                                      float progress,
                                      const std::wstring& title,
                                      const std::wstring& entryText,
                                      const std::wstring& stageText) {
    EQT::Graphics::drawLoadingFrame(driver, font, progress, title, entryText, stageText);
}

} // namespace Graphics
} // namespace EQT
