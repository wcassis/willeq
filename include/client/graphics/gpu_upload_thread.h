// gpu_upload_thread.h — Dedicated GPU upload thread with shared EGL context
// Processes texture and VBO uploads asynchronously to avoid render thread stalls.
// Uses BackgroundWorkQueue with batch hooks for EGL context bind/unbind.
// Uses EGL_KHR_fence_sync for GPU synchronization between contexts.

#ifndef EQT_GRAPHICS_GPU_UPLOAD_THREAD_H
#define EQT_GRAPHICS_GPU_UPLOAD_THREAD_H

#ifdef EQT_HAS_GLES2

#include "client/graphics/background_work_queue.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

enum class UploadRequestType : uint8_t {
    Texture,            // RGBA8 uncompressed texture
    CompressedTexture,  // ETC1 compressed texture
    VertexBuffer        // VBO + EBO pair
};

struct UploadRequest {
    UploadRequestType type;
    uint32_t requestId;

    // Texture fields (Texture / CompressedTexture)
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixelData;     // RGBA8 or ETC1 compressed bytes
    uint32_t compressedSize = 0;        // For ETC1: actual compressed data size
    std::string textureName;            // For Irrlicht texture registration

    // VBO fields (VertexBuffer)
    std::vector<uint8_t> vertexData;
    std::vector<uint16_t> indexData;    // 16-bit indices (Mali 400 constraint)
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;         // Bytes per vertex (36=Standard, 44=2TCoords)

    // Callback routing key (regionIdx, atlasPageIndex, entity spawn ID, etc.)
    uint64_t callbackKey = 0;
};

struct UploadResult {
    UploadRequestType type;
    uint32_t requestId;

    // Texture result
    GLuint glTextureName = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string textureName;

    // VBO result
    GLuint vbo = 0;
    GLuint ebo = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;

    // GPU fence — render thread waits on this before using the object
    EGLSyncKHR fence = EGL_NO_SYNC_KHR;

    // Size metadata for cache tracking
    size_t gpuBytes = 0;

    // Callback routing key
    uint64_t callbackKey = 0;
};

class GPUUploadThread {
public:
    GPUUploadThread();
    ~GPUUploadThread();

    // Initialize shared EGL context. Returns true on success.
    // mainContext: the render thread's EGL context (share group parent)
    bool init(EGLDisplay display, EGLContext mainContext, EGLConfig config);

    // Start the upload worker thread
    void start();

    // Stop the worker thread and clean up (blocks until thread exits)
    void stop();

    // Submit an upload request (thread-safe, callable from any thread)
    void submit(UploadRequest&& request);

    // Poll for completed uploads (render thread only, non-blocking)
    // Returns completed results and clears them from the internal queue.
    std::vector<UploadResult> pollResults();

    // Check if the upload thread initialized successfully
    bool isAvailable() const { return available_.load(std::memory_order_relaxed); }

    // Number of pending (not yet processed) upload requests
    size_t getPendingCount() const;

    // Number of completed uploads waiting to be polled
    size_t getCompletedCount() const;

    // Statistics for /pmem diagnostics
    uint64_t getTotalUploadsCompleted() const { return totalCompleted_.load(std::memory_order_relaxed); }
    uint64_t getTotalUploadTimeUs() const { return totalUploadTimeUs_.load(std::memory_order_relaxed); }

    // Get EGL display for fence operations on render thread
    EGLDisplay getEGLDisplay() const { return display_; }

private:
    UploadResult processRequest(const UploadRequest& req);
    UploadResult processTextureUpload(const UploadRequest& req);
    UploadResult processCompressedTextureUpload(const UploadRequest& req);
    UploadResult processVertexBufferUpload(const UploadRequest& req);

    // EGL state
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext sharedContext_ = EGL_NO_CONTEXT;
    EGLConfig config_ = nullptr;
    EGLSurface surface_ = EGL_NO_SURFACE;  // EGL_NO_SURFACE if surfaceless, or tiny pbuffer

    // EGL fence sync function pointers (resolved via eglGetProcAddress)
    PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR_ = nullptr;
    PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR_ = nullptr;
    PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR_ = nullptr;

    // Background work queue
    std::unique_ptr<BackgroundWorkQueue<UploadRequest, UploadResult>> queue_;
    std::atomic<bool> available_{false};

    // Request ID generator
    std::atomic<uint32_t> nextRequestId_{1};

    // Statistics
    std::atomic<uint64_t> totalCompleted_{0};
    std::atomic<uint64_t> totalUploadTimeUs_{0};
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
#endif // EQT_GRAPHICS_GPU_UPLOAD_THREAD_H
