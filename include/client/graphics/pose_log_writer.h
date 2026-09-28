// B874 audit (task/pose-log-e2e-test) -- the render-loop half of --pose-log.
//
// Application::render() used to carry this body inline: open the file on the
// first gameplay frame, count frames, gather one PoseLogRow every Nth frame,
// stamp it with the wall clock, write it, stop at the cap. Inline, nothing
// but a live server run could reach it, so the only writer test called the
// format functions with a hand-built row and a wrong field in the GATHER
// (heading units, camera frame, timestamp clock -- all three happened in
// review) would still pass CI.
//
// It is now this class, and Application::render() makes ONE call per
// rendered frame: onRenderedFrame(*m_renderer). tests/test_pose_log_e2e.cpp
// makes the same call on a real IrrlichtRenderer driven through real
// processFrame() calls on a headless GLES2 device, then parses the file.
//
// The source is a template parameter only so this header needs neither
// Irrlicht nor the renderer header: anything with
//   void getPoseData(PoseLogRow&) const;   int getFrameNumber() const;
// works, and in production that is IrrlichtRenderer.

#pragma once

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "client/graphics/pose_log_format.h"

namespace EQT {
namespace Graphics {

class PoseLogWriter {
public:
    PoseLogWriter() = default;
    ~PoseLogWriter() { poseLogClose(file_); }
    PoseLogWriter(const PoseLogWriter&) = delete;
    PoseLogWriter& operator=(const PoseLogWriter&) = delete;

    // Open `path` (truncating it) and write the header. `every` >= 1 is the
    // row cadence in rendered frames, `maxRows` >= 1 the row cap. On failure
    // returns false and, when `err` is non-null, says why (the strerror of
    // the fopen, or the bad argument) -- the caller decides how loud to be.
    bool open(const std::string& path, int every, int maxRows, std::string* err = nullptr) {
        poseLogClose(file_);
        counter_ = 0;
        rows_ = 0;
        if (path.empty() || every < 1 || maxRows < 1) {
            if (err) {
                *err = "invalid pose-log arguments (path='" + path + "', every=" +
                       std::to_string(every) + ", max=" + std::to_string(maxRows) + ")";
            }
            return false;
        }
        file_ = poseLogOpen(path);
        if (file_ == nullptr) {
            if (err) *err = "cannot open '" + path + "' for writing: " + std::strerror(errno);
            return false;
        }
        every_ = static_cast<uint64_t>(every);
        maxRows_ = static_cast<uint64_t>(maxRows);
        return true;
    }

    // True while rows are still being written (open and under the cap).
    bool active() const { return file_ != nullptr; }
    // True once the cap closed the file (the caller logs it once).
    bool capReached() const { return rows_ > 0 && rows_ >= maxRows_; }

    // Call once per RENDERED frame, after processFrame() returned true, on the
    // render thread. Returns true when this call wrote a row.
    template <class PoseSource>
    bool onRenderedFrame(const PoseSource& source) {
        if (file_ == nullptr) return false;
        if (++counter_ % every_ != 0) return false;
        PoseLogRow row;
        source.getPoseData(row);
        row.frame = source.getFrameNumber();
        poseLogRow(file_, unixMsNow(), row);
        std::fflush(file_);
        if (++rows_ >= maxRows_) poseLogClose(file_);
        return true;
    }

    // The ts column's clock: real wall clock, unix epoch milliseconds, so a
    // row is comparable with host time and with the server-side log
    // (Deliverable A). NOT ITimer::getTime(), which is a 0-based virtual
    // timer (the round-2 review finding).
    static int64_t unixMsNow() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

private:
    std::FILE* file_ = nullptr;
    uint64_t every_ = 1;
    uint64_t maxRows_ = 1;
    uint64_t counter_ = 0;
    uint64_t rows_ = 0;
};

}  // namespace Graphics
}  // namespace EQT
