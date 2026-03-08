// gpu_upload_thread.cpp — Dedicated GPU upload thread with shared EGL context

#ifdef EQT_HAS_GLES2

#include "client/graphics/gpu_upload_thread.h"
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace EQT {
namespace Graphics {

GPUUploadThread::GPUUploadThread() = default;

GPUUploadThread::~GPUUploadThread() {
    stop();
}

bool GPUUploadThread::init(EGLDisplay display, EGLContext mainContext, EGLConfig config) {
    display_ = display;
    config_ = config;

    if (display_ == EGL_NO_DISPLAY || mainContext == EGL_NO_CONTEXT) {
        LOG_WARN(MOD_GRAPHICS, "GPUUploadThread: invalid EGL display or main context");
        return false;
    }

    // Resolve EGL fence sync function pointers
    eglCreateSyncKHR_ = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(
        eglGetProcAddress("eglCreateSyncKHR"));
    eglDestroySyncKHR_ = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
        eglGetProcAddress("eglDestroySyncKHR"));
    eglClientWaitSyncKHR_ = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
        eglGetProcAddress("eglClientWaitSyncKHR"));

    if (!eglCreateSyncKHR_ || !eglDestroySyncKHR_ || !eglClientWaitSyncKHR_) {
        LOG_WARN(MOD_GRAPHICS, "GPUUploadThread: EGL_KHR_fence_sync not available");
        return false;
    }

    // Create shared context (same share group as main context)
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    sharedContext_ = eglCreateContext(display_, config_, mainContext, contextAttribs);
    if (sharedContext_ == EGL_NO_CONTEXT) {
        EGLint err = eglGetError();
        LOG_WARN(MOD_GRAPHICS, "GPUUploadThread: eglCreateContext failed (error 0x{:04X})", err);
        return false;
    }

    // Save main thread's current EGL state so we can restore it after testing
    EGLContext savedContext = eglGetCurrentContext();
    EGLSurface savedDrawSurface = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface savedReadSurface = eglGetCurrentSurface(EGL_READ);

    // Try surfaceless context first (EGL_KHR_surfaceless_context)
    if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, sharedContext_)) {
        LOG_INFO(MOD_GRAPHICS, "GPUUploadThread: surfaceless context OK");
        surface_ = EGL_NO_SURFACE;
    } else {
        // Surfaceless not supported — create a tiny 1x1 pbuffer
        LOG_INFO(MOD_GRAPHICS, "GPUUploadThread: surfaceless failed, trying 1x1 pbuffer");
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        surface_ = eglCreatePbufferSurface(display_, config_, pbufferAttribs);
        if (surface_ == EGL_NO_SURFACE) {
            EGLint err = eglGetError();
            LOG_WARN(MOD_GRAPHICS, "GPUUploadThread: eglCreatePbufferSurface failed (error 0x{:04X})", err);
            eglDestroyContext(display_, sharedContext_);
            sharedContext_ = EGL_NO_CONTEXT;
            // Restore main context before returning
            eglMakeCurrent(display_, savedDrawSurface, savedReadSurface, savedContext);
            return false;
        }

        if (!eglMakeCurrent(display_, surface_, surface_, sharedContext_)) {
            EGLint err = eglGetError();
            LOG_WARN(MOD_GRAPHICS, "GPUUploadThread: eglMakeCurrent with pbuffer failed (error 0x{:04X})", err);
            eglDestroySurface(display_, surface_);
            eglDestroyContext(display_, sharedContext_);
            surface_ = EGL_NO_SURFACE;
            sharedContext_ = EGL_NO_CONTEXT;
            // Restore main context before returning
            eglMakeCurrent(display_, savedDrawSurface, savedReadSurface, savedContext);
            return false;
        }
    }

    // Restore main thread's EGL context (init() runs on the main/render thread)
    eglMakeCurrent(display_, savedDrawSurface, savedReadSurface, savedContext);

    available_.store(true, std::memory_order_release);
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: initialized (shared EGL context)");
    return true;
}

void GPUUploadThread::start() {
    if (!available_.load(std::memory_order_acquire) || running_.load(std::memory_order_acquire))
        return;

    running_.store(true, std::memory_order_release);
    thread_ = std::make_unique<std::thread>(&GPUUploadThread::workerLoop, this);
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: started");
}

void GPUUploadThread::stop() {
    if (!running_.load(std::memory_order_acquire))
        return;

    running_.store(false, std::memory_order_release);
    requestCv_.notify_all();

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();

    // Clean up EGL resources
    if (sharedContext_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, sharedContext_);
        sharedContext_ = EGL_NO_CONTEXT;
    }
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }

    available_.store(false, std::memory_order_release);
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: stopped");
}

void GPUUploadThread::submit(UploadRequest&& request) {
    request.requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestQueue_.push_back(std::move(request));
    }
    requestCv_.notify_one();
}

std::vector<UploadResult> GPUUploadThread::pollResults() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    std::vector<UploadResult> results;
    results.swap(resultQueue_);
    return results;
}

size_t GPUUploadThread::getPendingCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(requestMutex_));
    return requestQueue_.size();
}

void GPUUploadThread::workerLoop() {
    LOG_DEBUG(MOD_GRAPHICS, "GPUUploadThread worker: started (context unbound)");

    while (running_.load(std::memory_order_acquire)) {
        // Wait for work (context unbound — no cross-context sync overhead)
        UploadRequest request;
        bool hasWork = false;
        {
            std::unique_lock<std::mutex> lock(requestMutex_);
            requestCv_.wait(lock, [this] {
                return !requestQueue_.empty() || !running_.load(std::memory_order_acquire);
            });

            if (!running_.load(std::memory_order_acquire))
                break;

            if (!requestQueue_.empty()) {
                auto minIt = std::min_element(requestQueue_.begin(), requestQueue_.end(),
                    [](const UploadRequest& a, const UploadRequest& b) {
                        return a.priority < b.priority;
                    });
                request = std::move(*minIt);
                requestQueue_.erase(minIt);
                hasWork = true;
            }
        }

        if (!hasWork)
            continue;

        // Bind context before processing
        if (!eglMakeCurrent(display_, surface_, surface_, sharedContext_)) {
            EGLint err = eglGetError();
            LOG_ERROR(MOD_GRAPHICS, "GPUUploadThread: eglMakeCurrent bind failed (0x{:04X})", err);
            running_.store(false, std::memory_order_release);
            return;
        }

        processRequest(request);

        // Drain remaining queued requests while context is bound (min-priority first)
        while (running_.load(std::memory_order_acquire)) {
            UploadRequest nextReq;
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                if (requestQueue_.empty())
                    break;
                auto minIt = std::min_element(requestQueue_.begin(), requestQueue_.end(),
                    [](const UploadRequest& a, const UploadRequest& b) {
                        return a.priority < b.priority;
                    });
                nextReq = std::move(*minIt);
                requestQueue_.erase(minIt);
            }
            processRequest(nextReq);
        }

        // Unbind context before sleeping — critical for avoiding
        // cross-context synchronization overhead in eglSwapBuffers
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        FlushThreadLog();
    }

    // Ensure context is unbound on exit (may already be unbound)
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    LOG_DEBUG(MOD_GRAPHICS, "GPUUploadThread worker: exited");
}

void GPUUploadThread::processRequest(const UploadRequest& req) {
    auto start = std::chrono::steady_clock::now();

    switch (req.type) {
        case UploadRequestType::Texture:
            processTextureUpload(req);
            break;
        case UploadRequestType::CompressedTexture:
            processCompressedTextureUpload(req);
            break;
        case UploadRequestType::VertexBuffer:
            processVertexBufferUpload(req);
            break;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    totalUploadTimeUs_.fetch_add(static_cast<uint64_t>(elapsed), std::memory_order_relaxed);
    totalCompleted_.fetch_add(1, std::memory_order_relaxed);
}

void GPUUploadThread::processTextureUpload(const UploadRequest& req) {
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 req.width, req.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 req.pixelData.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GPUUploadThread: texture upload GL error 0x{:04X} for '{}'",
                  err, req.textureName);
    }

    // Flush and create fence
    glFlush();
    EGLSyncKHR fence = eglCreateSyncKHR_(display_, EGL_SYNC_FENCE_KHR, nullptr);

    UploadResult result;
    result.type = UploadRequestType::Texture;
    result.requestId = req.requestId;
    result.glTextureName = texId;
    result.width = req.width;
    result.height = req.height;
    result.textureName = req.textureName;
    result.fence = fence;
    result.gpuBytes = static_cast<size_t>(req.width) * req.height * 4;
    result.callbackKey = req.callbackKey;

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        resultQueue_.push_back(std::move(result));
    }
}

void GPUUploadThread::processCompressedTextureUpload(const UploadRequest& req) {
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    // Upload all mip levels from contiguous ETC1 data
    {
        uint32_t offset = 0;
        int w = static_cast<int>(req.width);
        int h = static_cast<int>(req.height);
        int levels = std::max(static_cast<int>(req.mipLevels), 1);
        for (int level = 0; level < levels; ++level) {
            uint32_t blocksW = static_cast<uint32_t>(std::max(1, (w + 3) / 4));
            uint32_t blocksH = static_cast<uint32_t>(std::max(1, (h + 3) / 4));
            uint32_t levelSize = blocksW * blocksH * 8;

            if (offset + levelSize > req.compressedSize) {
                LOG_ERROR(MOD_GRAPHICS, "GPUUploadThread: mip level {} overflows data (offset={}, levelSize={}, total={})",
                          level, offset, levelSize, req.compressedSize);
                break;
            }

            glCompressedTexImage2D(GL_TEXTURE_2D, level,
                                   0x8D64,  // GL_ETC1_RGB8_OES
                                   w, h, 0,
                                   static_cast<GLsizei>(levelSize),
                                   req.pixelData.data() + offset);

            offset += levelSize;
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
    }

    if (req.mipLevels > 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GPUUploadThread: compressed texture upload GL error 0x{:04X} ({}x{}, {} bytes, {} levels)",
                  err, req.width, req.height, req.compressedSize, req.mipLevels);
    }

    glFlush();
    EGLSyncKHR fence = eglCreateSyncKHR_(display_, EGL_SYNC_FENCE_KHR, nullptr);

    UploadResult result;
    result.type = UploadRequestType::CompressedTexture;
    result.requestId = req.requestId;
    result.glTextureName = texId;
    result.width = req.width;
    result.height = req.height;
    result.textureName = req.textureName;
    result.fence = fence;
    result.gpuBytes = req.compressedSize;
    result.callbackKey = req.callbackKey;

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        resultQueue_.push_back(std::move(result));
    }
}

void GPUUploadThread::processVertexBufferUpload(const UploadRequest& req) {
    GLuint vbo = 0, ebo = 0;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 req.vertexCount * req.vertexStride,
                 req.vertexData.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 req.indexCount * sizeof(uint16_t),
                 req.indexData.data(),
                 GL_STATIC_DRAW);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GPUUploadThread: VBO upload GL error 0x{:04X} (verts={}, indices={})",
                  err, req.vertexCount, req.indexCount);
    }

    // Unbind to avoid state leaks
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glFlush();
    EGLSyncKHR fence = eglCreateSyncKHR_(display_, EGL_SYNC_FENCE_KHR, nullptr);

    UploadResult result;
    result.type = UploadRequestType::VertexBuffer;
    result.requestId = req.requestId;
    result.vbo = vbo;
    result.ebo = ebo;
    result.vertexCount = req.vertexCount;
    result.indexCount = req.indexCount;
    result.vertexStride = req.vertexStride;
    result.fence = fence;
    result.gpuBytes = static_cast<size_t>(req.vertexCount) * req.vertexStride +
                      static_cast<size_t>(req.indexCount) * sizeof(uint16_t);
    result.callbackKey = req.callbackKey;

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        resultQueue_.push_back(std::move(result));
    }
}

void GPUUploadThread::reprioritize(std::function<uint32_t(const UploadRequest&)> computePriority) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    for (auto& req : requestQueue_) {
        req.priority = computePriority(req);
    }
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
