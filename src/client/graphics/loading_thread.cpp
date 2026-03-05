#include "client/graphics/loading_thread.h"

#include <chrono>
#include <irrlicht.h>

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#else
#include <GL/glx.h>
#endif

#include "common/logging.h"

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
            LOG_WARN(MOD_GRAPHICS, "LoadingThread: releaseContext called with no backend");
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
            LOG_WARN(MOD_GRAPHICS, "LoadingThread: acquireContext called with no backend");
            return false;
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
    // Acquire GL context on this thread
    if (!acquireContext(handles)) {
        LOG_ERROR(MOD_GRAPHICS, "LoadingThread: failed to acquire GL context, aborting");
        status.loadingComplete.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_relaxed);
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "LoadingThread: started, entering passive display loop");

    // Passive display loop: render loading screen until graphicsLoadReady or quit
    while (!status.quitRequested.load(std::memory_order_relaxed) &&
           !status.graphicsLoadReady.load(std::memory_order_acquire)) {

        float progress = status.percent.load(std::memory_order_relaxed) / 100.0f;
        std::wstring title = status.getTitle();
        std::string textNarrow = status.getText();
        std::wstring text(textNarrow.begin(), textNarrow.end());

        drawLoadingFrame(driver, font, progress, title, text);

        // ~30 fps for the progress bar
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    if (status.quitRequested.load(std::memory_order_relaxed)) {
        LOG_INFO(MOD_GRAPHICS, "LoadingThread: quit requested during passive phase");
        releaseContext(handles);
        status.loadingComplete.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_relaxed);
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "LoadingThread: graphicsLoadReady, entering active loading phase");

    // Execute the active loading callback (zone loading + renderer setup)
    if (activeCallback) {
        activeCallback(status);
    }

    // Release GL context before signaling completion
    releaseContext(handles);

    LOG_INFO(MOD_GRAPHICS, "LoadingThread: complete, GL context released");
    status.loadingComplete.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_relaxed);
}

void LoadingThread::drawLoadingFrame(irr::video::IVideoDriver* driver,
                                      irr::gui::IGUIFont* font,
                                      float progress,
                                      const std::wstring& title,
                                      const std::wstring& stageText) {
    if (!driver) return;

    driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 40));

    irr::core::dimension2d<irr::u32> screenSize = driver->getScreenSize();

    // Progress bar dimensions
    const int barWidth = 400;
    const int barHeight = 30;
    const int barX = (screenSize.Width - barWidth) / 2;
    const int barY = (screenSize.Height / 2) + 20;

    // Colors
    irr::video::SColor bgColor(255, 40, 40, 60);
    irr::video::SColor borderColor(255, 100, 100, 120);
    irr::video::SColor fillColor(255, 80, 120, 200);

    // Draw border
    driver->draw2DRectangle(borderColor,
        irr::core::recti(barX - 2, barY - 2, barX + barWidth + 2, barY + barHeight + 2));

    // Draw background
    driver->draw2DRectangle(bgColor,
        irr::core::recti(barX, barY, barX + barWidth, barY + barHeight));

    // Draw progress fill
    float clamped = std::max(0.0f, std::min(1.0f, progress));
    int fillWidth = static_cast<int>(barWidth * clamped);
    if (fillWidth > 0) {
        driver->draw2DRectangle(fillColor,
            irr::core::recti(barX, barY, barX + fillWidth, barY + barHeight));
    }

    if (font) {
        // Title above progress bar
        irr::core::dimension2d<irr::u32> titleSize = font->getDimension(title.c_str());
        int titleX = (screenSize.Width - titleSize.Width) / 2;
        int titleY = barY - 40;
        font->draw(title.c_str(),
            irr::core::recti(titleX, titleY, titleX + titleSize.Width, titleY + titleSize.Height),
            irr::video::SColor(255, 255, 255, 255));

        // Stage text below progress bar
        irr::core::dimension2d<irr::u32> stageSize = font->getDimension(stageText.c_str());
        int stageX = (screenSize.Width - stageSize.Width) / 2;
        int stageY = barY + barHeight + 10;
        font->draw(stageText.c_str(),
            irr::core::recti(stageX, stageY, stageX + stageSize.Width, stageY + stageSize.Height),
            irr::video::SColor(255, 200, 200, 200));

        // Percentage centered in progress bar
        std::wstring pctText = std::to_wstring(static_cast<int>(clamped * 100)) + L"%";
        irr::core::dimension2d<irr::u32> pctSize = font->getDimension(pctText.c_str());
        int pctX = (screenSize.Width - pctSize.Width) / 2;
        int pctY = barY + (barHeight - pctSize.Height) / 2;
        font->draw(pctText.c_str(),
            irr::core::recti(pctX, pctY, pctX + pctSize.Width, pctY + pctSize.Height),
            irr::video::SColor(255, 255, 255, 255));
    }

    driver->endScene();
}

} // namespace Graphics
} // namespace EQT
