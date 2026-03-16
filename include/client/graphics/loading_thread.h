#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef EQT_HAS_GLES2
#include <EGL/egl.h>
#endif

namespace irr {
    class IrrlichtDevice;
    namespace video { class IVideoDriver; }
    namespace gui { class IGUIEnvironment; class IGUIFont; }
}

namespace EQT {
namespace Graphics {

// Shared state between main thread and loading thread.
// Main thread writes phase/percent/text; loading thread reads them for display.
// Flags coordinate handoff and completion.
struct LoadingStatus {
    // Main -> loading: progress display
    std::atomic<int> percent{0};
    std::mutex textMutex;
    std::string text;
    std::wstring title;

    // Main -> loading: signals
    std::atomic<bool> quitRequested{false};       // "abort"

    // Loading -> main: signals
    std::atomic<bool> loadingComplete{false};      // "done, join me"

    void setProgress(int pct, const std::string& msg) {
        percent.store(pct, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(textMutex);
        text = msg;
    }

    void setTitle(const std::wstring& t) {
        std::lock_guard<std::mutex> lock(textMutex);
        title = t;
    }

    // Read current text (thread-safe copy)
    std::string getText() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(textMutex));
        return text;
    }

    std::wstring getTitle() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(textMutex));
        return title;
    }

    void reset() {
        percent.store(0, std::memory_order_relaxed);
        quitRequested.store(false, std::memory_order_relaxed);
        loadingComplete.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(textMutex);
        text.clear();
        title = L"EverQuest";
    }
};

// GL context handles needed for cross-thread context transfer.
struct GLContextHandles {
    enum class Backend { Unknown, GLX, EGL };
    Backend backend = Backend::Unknown;

    // GLX (desktop X11/OpenGL)
    void* x11Display = nullptr;   // Display*
    void* glxContext = nullptr;    // GLXContext
    unsigned long x11Window = 0;  // Window (XID)

#ifdef EQT_HAS_GLES2
    // EGL (DRM/GLES2)
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;
#endif
};

// Temporary thread that owns the GL context during zone loading.
// Renders the loading screen progress bar, then (when signaled) executes
// zone asset loading sequentially. Joins when loading completes.
class LoadingThread {
public:
    LoadingThread();
    ~LoadingThread();

    // Extract GL context handles from the Irrlicht device/driver.
    // Call before start() while main thread still owns the context.
    static GLContextHandles extractGLHandles(irr::IrrlichtDevice* device,
                                              irr::video::IVideoDriver* driver);

    // Release GL context on the calling thread (main thread).
    static void releaseContext(const GLContextHandles& handles);

    // Acquire GL context on the calling thread.
    static bool acquireContext(const GLContextHandles& handles);

    // Callback for the active loading phase.
    // Called on the loading thread after graphicsLoadReady is signaled.
    // The loading thread owns the GL context when this runs.
    using ActivePhaseCallback = std::function<void(LoadingStatus& status)>;

    // Start the loading thread. Main thread must have already released the GL context.
    // The loading thread will acquire it, render the loading screen, and wait for
    // graphicsLoadReady before calling activeCallback (which does zone loading).
    void start(irr::IrrlichtDevice* device,
               irr::video::IVideoDriver* driver,
               irr::gui::IGUIFont* font,
               const GLContextHandles& handles,
               LoadingStatus& status,
               ActivePhaseCallback activeCallback);

    // Join the loading thread. Blocks until the thread completes.
    // After this returns, the main thread should reacquire the GL context.
    void join();

    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    // Render one frame of the loading screen.
    // Public so the render thread can draw the loading screen during the
    // network handshake phase (0-50%) before the loading thread starts.
    static void drawLoadingFrame(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIFont* font,
                                 float progress,
                                 const std::wstring& title,
                                 const std::wstring& stageText);

private:
    void threadFunc(irr::IrrlichtDevice* device,
                    irr::video::IVideoDriver* driver,
                    irr::gui::IGUIFont* font,
                    GLContextHandles handles,
                    LoadingStatus& status,
                    ActivePhaseCallback activeCallback);

    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace Graphics
} // namespace EQT
