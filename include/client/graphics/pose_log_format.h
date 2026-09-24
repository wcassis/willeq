// B874 (round 2) -- the --pose-log TSV row format, split from the writer.
//
// The writer (Application::render(), application_frame.cpp) gathers one
// PoseLogRow every Nth rendered frame and hands it to poseLogRow() with a
// wall-clock ts; the file I/O never sees the format. That split is the
// FPCAM-DUMP precedent (GATHER from FORMAT) and it is what makes the row
// testable without a renderer or a GL context: tests/test_pose_log_flags.cpp
// feeds the format the same values a frame would and asserts the TSV parses
// (14 tab-separated columns, the space-carrying camera-mode field, the
// EQ-frame heading).
//
// The columns (Deliverable B spec, card t_c1c2b63b; the row has 14 fields
// -- the B874 cards' "13 columns" is a miscount of this very row.
// Deliverable A's parse_pose_log.sh emits its own 13-column TSV, a
// different shape fed from the server-side MOD_MOVEMENT log):
//
//   ts_unix_ms  frame  cameramode  camX  camY  camZ  targetX  targetY  targetZ
//   playerX  playerY  playerZ  heading0_512  movestate
//
// Every X/Y/Z column is the EQ Z-up frame. The camera position and target
// are converted out of the Irrlicht Y-up node frame with the SAME (x,z,y)
// swap as CameraController::getPositionEQ() (camera_controller.cpp), so one
// row is self-consistent; heading0_512 is ctx_.playerHeading as-is (already
// 0-512 EQ units, renderer_player_physics.cpp).

#pragma once

#include <cstdint>
#include <cstdio>

#include "client/graphics/player/camera_mode.h"

namespace EQT {
namespace Graphics {

// One pose row's data, gathered on the render thread.
struct PoseLogRow {
    int frame = 0;
    // Camera node position and target, already converted to the EQ frame.
    float camX = 0, camY = 0, camZ = 0;
    float targetX = 0, targetY = 0, targetZ = 0;
    float playerX = 0, playerY = 0, playerZ = 0;
    uint16_t heading0_512 = 0;
    CameraMode cameraMode = kDefaultCameraMode;
    int movementState = 0;  // 0=idle, 1=move, 2=jump, 3=swim
};

// The header line (one '\n', no leading '#'): 14 column names, TAB-
// separated. The units are documented here, not in the file: positions are
// EQ Z-up, ts is unix epoch ms, heading is EQ 0-512 units.
inline const char* poseLogHeaderLine() {
    return "ts_unix_ms\tframe\tcameramode\tcamX\tcamY\tcamZ\t"
           "targetX\ttargetY\ttargetZ\tplayerX\tplayerY\tplayerZ\t"
           "heading0_512\tmovestate\n";
}

// One TSV data row (one '\n'). Tab-separated on purpose: the camera-mode
// field carries spaces (cameraModeName(), eqstr_us.txt 9500..9505) and the
// B875/B876 consumers split on '\t' only.
inline void poseLogRow(std::FILE* out, int64_t tsUnixMs, const PoseLogRow& row) {
    std::fprintf(out,
        "%lld\t%d\t%s\t"
        "%.6f\t%.6f\t%.6f\t"
        "%.6f\t%.6f\t%.6f\t"
        "%.6f\t%.6f\t%.6f\t"
        "%d\t%d\n",
        static_cast<long long>(tsUnixMs),
        row.frame,
        cameraModeName(row.cameraMode),
        row.camX, row.camY, row.camZ,
        row.targetX, row.targetY, row.targetZ,
        row.playerX, row.playerY, row.playerZ,
        static_cast<int>(row.heading0_512),
        row.movementState);
}

}  // namespace Graphics
}  // namespace EQT
