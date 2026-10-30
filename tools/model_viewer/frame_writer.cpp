// tools/model_viewer/frame_writer.cpp -- B1193 (#512). See the header.

#include "frame_writer.h"

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eqt {
namespace fxharness {

// ---------------------------------------------------------------------------
// The encoders
// ---------------------------------------------------------------------------

namespace {

void be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void appendChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
    be32(out, uint32_t(len));
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)type, 4);
    if (len) crc = crc32(crc, data, uInt(len));
    const uint8_t* t = (const uint8_t*)type;
    out.insert(out.end(), t, t + 4);
    out.insert(out.end(), data, data + len);
    be32(out, uint32_t(crc));
}

}  // namespace

// The PNG: 8-bit RGBA (colour type 6), non-interlaced, every scanline with
// filter 0, the zlib stream at compression level 0 (stored blocks -- the
// lowest zlib level; the pixels are the whole payload, nothing is spent on
// compressing them). decode-side compatible with everything the measures use
// (ffmpeg, cv2, Irrlicht's own PNG loader).
bool encodePng(uint32_t w, uint32_t h, const uint8_t* bgra, std::vector<uint8_t>& out) {
    if (w == 0 || h == 0 || !bgra) return false;
    std::vector<uint8_t> scan;
    scan.reserve((size_t(w) * 4 + 1) * h);
    for (uint32_t y = 0; y < h; ++y) {
        scan.push_back(0);  // filter: none
        const uint8_t* row = bgra + (size_t(y) * w) * 4;
        for (uint32_t x = 0; x < w; ++x) {  // A8R8G8B8 (B,G,R,A) -> PNG RGBA
            const uint8_t* p = row + (size_t(x) * 4);
            scan.push_back(p[2]);  // R
            scan.push_back(p[1]);  // G
            scan.push_back(p[0]);  // B
            scan.push_back(p[3]);  // A
        }
    }
    z_stream zs{};
    // windowBits 15 = RAW deflate (no zlib header): PNG's IDAT IS a raw
    // DEFLATE stream. (15 + 32 is rejected by this zlib build AND would put a
    // zlib wrapper into the IDAT, which is not a PNG.)
    if (deflateInit2(&zs, Z_NO_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    std::vector<uint8_t> idat(scan.size() + scan.size() / 100 + 4096);
    zs.next_in = (Bytef*)scan.data();
    zs.avail_in = uInt(scan.size());
    zs.next_out = idat.data();
    zs.avail_out = uInt(idat.size());
    bool ok = deflate(&zs, Z_FINISH) == Z_STREAM_END;  // Z_FINISH finishes the stream
    ok = ok && zs.avail_in == 0 && zs.avail_out > 0;
    idat.resize(zs.total_out);
    deflateEnd(&zs);
    if (!ok) return false;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, w);
    be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // depth 8, RGBA, compression 0, filter 0, no interlace
    out.assign(sig, sig + 8);
    appendChunk(out, "IHDR", ihdr.data(), ihdr.size());
    appendChunk(out, "IDAT", idat.data(), idat.size());
    appendChunk(out, "IEND", nullptr, 0);
    return true;
}

// P6 PPM: RGB24 (the alpha is dropped -- the measures read RGB).
bool encodePpm(uint32_t w, uint32_t h, const uint8_t* bgra, std::vector<uint8_t>& out) {
    if (w == 0 || h == 0 || !bgra) return false;
    const std::string header = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    out.assign(header.begin(), header.end());
    out.reserve(out.size() + (size_t(w) * h) * 3);
    for (size_t i = 0; i < (size_t(w) * h); ++i) {
        out.push_back(bgra[i * 4 + 2]);  // R
        out.push_back(bgra[i * 4 + 1]);  // G
        out.push_back(bgra[i * 4 + 0]);  // B
    }
    return true;
}

// The bilinear scaleImageTo (model_viewer_fx_harness.cpp) computes on
// IImage getPixel/setPixel pairs; the same arithmetic on the raw A8R8G8B8
// bytes (B,G,R,A) gives the same pixels, so the written frames stay
// like-with-like. (int)std::floor and the mix rounding are kept verbatim.
void downscaleBgra(const uint8_t* src, uint32_t sw, uint32_t sh, uint32_t w, uint32_t h,
                   std::vector<uint8_t>& dst) {
    if (sw == w && sh == h) {
        dst.assign(src, src + (size_t(sw) * sh) * 4);
        return;
    }
    dst.resize((size_t(w) * h) * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const float fy = (float)y * (float)sh / (float)h - 0.5f;
        const int y0 = std::max(0, std::min<int>(sh - 1, (int)std::floor(fy)));
        const int y1 = std::max(0, std::min<int>(sh - 1, y0 + 1));
        const float ty = std::max(0.0f, std::min(1.0f, fy - std::floor(fy)));
        for (uint32_t x = 0; x < w; ++x) {
            const float fx = (float)x * (float)sw / (float)w - 0.5f;
            const int x0 = std::max(0, std::min<int>(sw - 1, (int)std::floor(fx)));
            const int x1 = std::max(0, std::min<int>(sw - 1, x0 + 1));
            const float tx = std::max(0.0f, std::min(1.0f, fx - std::floor(fx)));
            const uint8_t* p00 = src + ((size_t(y0) * sw + x0) * 4);
            const uint8_t* p10 = src + ((size_t(y0) * sw + x1) * 4);
            const uint8_t* p01 = src + ((size_t(y1) * sw + x0) * 4);
            const uint8_t* p11 = src + ((size_t(y1) * sw + x1) * 4);
            auto mix = [](int a, int b, float t) { return (int)(a * (1.0f - t) + b * t + 0.5f); };
            uint8_t* o = dst.data() + ((size_t(y) * w + x) * 4);
            for (int c = 0; c < 3; ++c)  // B, G, R (the A slot is set below)
                o[c] = uint8_t(mix(mix(p00[c], p10[c], tx), mix(p01[c], p11[c], tx), ty));
            o[3] = 255;  // SColor(255, ...) -- scaleImageTo's destination alpha
        }
    }
}

// ---------------------------------------------------------------------------
// FrameWriter
// ---------------------------------------------------------------------------

FrameWriter::FrameWriter(std::string dir, bool raw) : dir_(std::move(dir)), raw_(raw) {
    cost_.open(dir_ + "/frame_cost.tsv", std::ios::trunc);
    cost_ << "frame\trender_ms\treadback_ms\tdownscale_ms\tencode_ms\twrite_ms\n";
}

FrameWriter::~FrameWriter() { stopAndJoin(); }

void FrameWriter::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(mux_);
                cvPop_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
                cvPush_.notify_one();
            }
            const auto t0 = std::chrono::steady_clock::now();
            auto msSince = [&t0]() {
                return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count() /
                       1e6;
            };
            const auto dim = job.img->getDimension();
            std::vector<uint8_t> scaled;
            double downMs;
            {
                const auto* src = static_cast<const uint8_t*>(job.img->lock());
                downscaleBgra(src, dim.Width, dim.Height, job.tw, job.th, scaled);
                job.img->unlock();
            }
            downMs = msSince();
            std::vector<uint8_t> enc;
            double encMs;
            {
                const auto t1 = std::chrono::steady_clock::now();
                const bool ok = raw_ ? encodePpm(job.tw, job.th, scaled.data(), enc)
                                     : encodePng(job.tw, job.th, scaled.data(), enc);
                encMs = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - t1)
                            .count() /
                        1e6;
                if (!ok) {
                    stats_.failures++;
                    logRow(job.frame, job.renderMs, job.readbackMs, downMs, encMs, 0.0, false);
                    job.img->drop();
                    continue;
                }
            }
            const auto t2 = std::chrono::steady_clock::now();
            char name[64];
            std::snprintf(name, sizeof(name), "/frame_%05d.%s", job.frame, raw_ ? "ppm" : "png");
            std::ofstream f(dir_ + name, std::ios::binary | std::ios::trunc);
            const bool wrote = f.write((const char*)enc.data(), (std::streamsize)enc.size()).good();
            f.close();
            const double writeMs =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t2)
                    .count() /
                1e6;
            logRow(job.frame, job.renderMs, job.readbackMs, downMs, encMs, writeMs, wrote);
            if (!wrote) stats_.failures++;
            job.img->drop();
        }
    });
}

void FrameWriter::push(int frame, irr::video::IImage* surface, uint32_t tw, uint32_t th,
                       double renderMs, double readbackMs) {
    if (!surface) {
        stats_.failures++;
        logRow(frame, renderMs, readbackMs, 0.0, 0.0, 0.0, false);
        return;
    }
    surface->grab();  // the queue's ref; the caller keeps its own
    {
        std::unique_lock<std::mutex> lk(mux_);
        cvPush_.wait(lk, [this] { return stop_ || queue_.size() < kQueueMax; });
        if (stop_) {
            surface->drop();
            stats_.failures++;
            logRow(frame, renderMs, readbackMs, 0.0, 0.0, 0.0, false);
            return;
        }
        Job job;
        job.frame = frame;
        job.img = surface;
        job.tw = tw;
        job.th = th;
        job.renderMs = renderMs;
        job.readbackMs = readbackMs;
        queue_.push_back(std::move(job));
    }
    cvPop_.notify_one();
}

void FrameWriter::stopAndJoin() {
    {
        std::lock_guard<std::mutex> lk(mux_);
        stop_ = true;
    }
    cvPush_.notify_all();
    cvPop_.notify_all();
    if (thread_.joinable()) thread_.join();
    // The queue holds one ref per image; the writer thread drained what it
    // took, so what is left here is ours to drop.
    for (auto& j : queue_)
        if (j.img) j.img->drop();
    queue_.clear();
}

bool FrameWriter::finish() {
    const bool threaded = running_;
    if (running_) stopAndJoin();
    running_ = false;
    cost_.flush();
    std::printf(
        "[fx-harness] frame cost (%d frames, %s, %s): render thread render %.3f ms/f readback "
        "%.3f ms/f = %.3f ms/f; output thread downscale %.3f + encode %.3f + write %.3f ms/f = "
        "%.3f ms/f%s\n",
        stats_.frames, raw_ ? "raw PPM" : "PNG zlib-0", threaded ? "writer thread" : "render thread (sync)",
        stats_.frames ? stats_.renderMs / stats_.frames : 0.0,
        stats_.frames ? stats_.readbackMs / stats_.frames : 0.0,
        stats_.frames ? stats_.renderThreadMs() / stats_.frames : 0.0,
        stats_.frames ? stats_.downscaleMs / stats_.frames : 0.0,
        stats_.frames ? stats_.encodeMs / stats_.frames : 0.0, stats_.frames ? stats_.writeMs / stats_.frames : 0.0,
        stats_.frames ? stats_.ioThreadMs() / stats_.frames : 0.0,
        stats_.failures ? " (FAILED FRAMES)" : "");
    std::fflush(stdout);
    return stats_.failures == 0;
}

void FrameWriter::logSync(int frame, double renderMs, double readbackMs, double downscaleMs,
                          double encodeMs, double writeMs) {
    logRow(frame, renderMs, readbackMs, downscaleMs, encodeMs, writeMs, encodeMs >= 0);
}

void FrameWriter::logRow(int frame, double renderMs, double readbackMs, double downscaleMs,
                         double encodeMs, double writeMs, bool ok) {
    {
        std::lock_guard<std::mutex> lk(costMux_);
        char row[128];
        std::snprintf(row, sizeof(row), "%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n", frame, renderMs, readbackMs,
                      downscaleMs, encodeMs, writeMs);
        cost_ << row;
    }
    if (ok) {
        stats_.frames++;
        stats_.renderMs += renderMs;
        stats_.readbackMs += readbackMs;
        stats_.downscaleMs += downscaleMs;
        stats_.encodeMs += encodeMs;
        stats_.writeMs += writeMs;
    }
}

}  // namespace fxharness
}  // namespace eqt
