// tools/model_viewer/frame_writer.h -- B1193 (#512): the FX harness's frame
// output, off the render thread. The render thread's per-frame job is the
// readback (createScreenShot) and a push; ONE writer thread then runs the
// (otherwise per-pixel) downscale, the PNG encode (zlib level 0, filter 0 --
// the lowest zlib level) or the P6 PPM dump (--frames-raw) and the file
// write, so the render thread never waits on the disk.
//
// The written pixels are IDENTICAL to the synchronous path (createScreenShot
// -> scaleImageTo -> driver->writeImageToFile, kept behind --frames-sync):
// the downscale is the same bilinear with the same rounding, and both PNG
// writers carry the same RGBA8 pixels (tests/test_fx_harness.cpp,
// FrameWriterOutputIsPixelIdenticalToTheSyncPath, compares the decoded
// pixel arrays). Only the PNG container bytes differ (a different zlib
// level and filter), which is why the identity is proven on pixels, not
// file bytes -- every measure reads pixels, so the gate stays like with
// like.
//
// The writer also owns the per-phase timing table (frame_cost.tsv): one row
// per frame with render_ms (beginScene->endScene, render thread),
// readback_ms (createScreenShot, render thread), downscale_ms, encode_ms and
// write_ms (writer thread in the default path, the render thread with
// --frames-sync). refactor/B1193/scripts/frame_cost_table.py turns the TSVs
// into the README's tables.

#ifndef WILLEQ_TOOLS_MODEL_VIEWER_FRAME_WRITER_H
#define WILLEQ_TOOLS_MODEL_VIEWER_FRAME_WRITER_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "irrlicht.h"

namespace eqt {
namespace fxharness {

struct FrameWriterStats {
    int frames = 0;
    int failures = 0;
    double renderMs = 0;      // render thread: beginScene -> endScene
    double readbackMs = 0;    // render thread: createScreenShot
    double downscaleMs = 0;   // writer thread (or render thread, --frames-sync)
    double encodeMs = 0;
    double writeMs = 0;
    double renderThreadMs() const { return renderMs + readbackMs; }
    double ioThreadMs() const { return downscaleMs + encodeMs + writeMs; }
};

class FrameWriter {
public:
    // dir = the --frames-dir (frame_cost.tsv is opened there at once).
    // raw = --frames-raw: write frame_%05d.ppm (P6, RGB24, no encode step)
    //       instead of frame_%05d.png.
    explicit FrameWriter(std::string dir, bool raw);
    ~FrameWriter();

    FrameWriter(const FrameWriter&) = delete;
    FrameWriter& operator=(const FrameWriter&) = delete;

    // Spawn the writer thread (the default path). Call once before the first
    // push.
    void start();

    // Render thread: enqueue one readback (the image's refcount is taken;
    // the caller keeps its own). tw/th = the REQUESTED frame size; a surface
    // of another size is downscaled on the writer thread. Blocks while the
    // bounded queue is full (back-pressure: the writer is faster than the
    // renderer, so this does not fire in steady state).
    void push(int frame, irr::video::IImage* surface, uint32_t tw, uint32_t th,
              double renderMs, double readbackMs);

    // Stop and join the writer thread, drain the queue (dropping its images)
    // and print the per-phase summary. False when any frame failed to write.
    // Safe to call when the thread was never started (the --frames-sync
    // path logs its rows directly with logSync).
    bool finish();

    // --frames-sync: the render thread did the whole frame itself (the
    // pre-B1193 code path); log its row the same way the writer logs its own.
    void logSync(int frame, double renderMs, double readbackMs, double downscaleMs, double encodeMs,
                 double writeMs);

    const FrameWriterStats& stats() const { return stats_; }

private:
    struct Job {
        int frame = 0;
        irr::video::IImage* img = nullptr;  // one ref held by the queue
        uint32_t tw, th;                    // the requested frame size
        double renderMs = 0, readbackMs = 0;
    };

    void stopAndJoin();
    void logRow(int frame, double renderMs, double readbackMs, double downscaleMs, double encodeMs,
                double writeMs, bool ok);

    std::string dir_;
    bool raw_;
    bool running_ = false;
    std::thread thread_;
    std::mutex mux_;
    std::deque<Job> queue_;
    static constexpr size_t kQueueMax = 8;  // ~33 MB at 1280x800 RGBA
    bool stop_ = false;
    std::condition_variable cvPush_, cvPop_;
    FrameWriterStats stats_;
    std::ofstream cost_;
    std::mutex costMux_;
};

// The PNG encode (level 0, filter 0) and the PPM dump; the pixels both carry
// are the scaled frame's RGBA8 (the PPM drops the alpha). Exposed for the
// offline half of the byte-identity test (tests/test_fx_harness.cpp).
bool encodePng(uint32_t w, uint32_t h, const uint8_t* bgra, std::vector<uint8_t>& out);
bool encodePpm(uint32_t w, uint32_t h, const uint8_t* bgra, std::vector<uint8_t>& out);
// The downscale scaleImageTo (model_viewer_fx_harness.cpp) computes, on raw
// BGRA bytes: same bilinear, same rounding, so the pixels are the driver
// path's pixels. dst is resized to w x h x 4.
void downscaleBgra(const uint8_t* src, uint32_t sw, uint32_t sh, uint32_t w, uint32_t h,
                   std::vector<uint8_t>& dst);

}  // namespace fxharness
}  // namespace eqt

#endif  // WILLEQ_TOOLS_MODEL_VIEWER_FRAME_WRITER_H
