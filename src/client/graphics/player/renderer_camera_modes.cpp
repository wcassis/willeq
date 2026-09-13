// renderer_camera_modes.cpp -- PlayerCameraComponent's official camera set
// (T461): the rig modes, zoom, keyboard orbit and `/camera`.
//
// The geometry is computeCameraRigPose() (camera_rig.cpp, device-free); this
// TU only owns the rigs, applies them through CameraController (which does the
// collision), and executes `/camera`. It deliberately writes no camera node
// directly and never branches on first person: THE one first-person formula
// stays repositionFirstPersonCamera() in renderer_camera.cpp
// (tests/test_first_person_camera_single_formula.cpp).

#include "client/graphics/player/player_camera_component.h"

#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/slash/camera_command.h"
#include "common/logging.h"

#include <fmt/format.h>

namespace EQT {
namespace Graphics {

namespace {

std::string RigValues(const CameraRig& r) {
    return fmt::format("az={:.3f} pitch={:.3f} dist={:.3f} height={:.3f}",
                       r.azimuthDeg, r.pitchDeg, r.distance, r.height);
}

}  // namespace

const CameraRig* PlayerCameraComponent::cameraRigFor(CameraMode mode) const {
    switch (mode) {
        case CameraMode::Overhead: return &overheadRig_;
        case CameraMode::User1: return &user1Rig_;
        case CameraMode::User2: return &user2Rig_;
        case CameraMode::Tether: return &tetherRig_;
        default: return nullptr;
    }
}

CameraRig* PlayerCameraComponent::rigFor(CameraMode mode) {
    return const_cast<CameraRig*>(
        static_cast<const PlayerCameraComponent*>(this)->cameraRigFor(mode));
}

float PlayerCameraComponent::playerFeetZ() const {
    const float offset =
        ctx_.entityRenderer ? ctx_.entityRenderer->getPlayerCollisionZOffset() : 0.0f;
    return ctx_.playerZ - offset;
}

void PlayerCameraComponent::updateRigCamera() {
    const CameraRig* rig = cameraRigFor(cameraMode_);
    if (!rig || !ctx_.cameraController) return;
    const CameraRigPose pose = computeCameraRigPose(*rig, ctx_.playerX, ctx_.playerY,
                                                    playerFeetZ(), ctx_.playerHeading);
    ctx_.cameraController->placeRigCamera(pose);
}

void PlayerCameraComponent::zoomCamera(float deltaUnits, float deltaTime) {
    if (deltaUnits == 0.0f) return;
    if (cameraMode_ == CameraMode::Chase) {
        // The pre-T461 body of FrameComponent::processInputDeltas(), moved
        // here unchanged so the keys and the wheel share it.
        if (!ctx_.cameraController) return;
        ctx_.cameraController->adjustFollowDistance(deltaUnits);
        ctx_.cameraController->setFollowPosition(ctx_.playerX, ctx_.playerY, ctx_.playerZ,
                                                 ctx_.playerHeading, deltaTime);
        LOG_DEBUG(MOD_GRAPHICS, "Camera zoom distance: {:.1f}",
                  ctx_.cameraController->getFollowDistance());
        return;
    }
    CameraRig* rig = rigFor(cameraMode_);
    if (!rig) return;  // first person does not zoom (it did not before T461 either)
    rig->distance += deltaUnits;
    clampCameraRig(*rig);
    updateRigCamera();
    LOG_DEBUG(MOD_GRAPHICS, "Camera zoom ({}): dist={:.1f}", getCameraModeString(), rig->distance);
}

void PlayerCameraComponent::orbitCamera(float deltaAzimuthDeg, float deltaPitchDeg) {
    if (!isUserCameraMode(cameraMode_)) {
        LOG_DEBUG(MOD_GRAPHICS, "Camera orbit ignored: {} is not a user camera",
                  getCameraModeString());
        return;
    }
    CameraRig* rig = rigFor(cameraMode_);
    rig->azimuthDeg += deltaAzimuthDeg;
    rig->pitchDeg += deltaPitchDeg;
    clampCameraRig(*rig);
    updateRigCamera();
}

void PlayerCameraComponent::setUserCameraRigs(const CameraRig& user1, const CameraRig& user2) {
    user1Rig_ = user1;
    user2Rig_ = user2;
    clampCameraRig(user1Rig_);
    clampCameraRig(user2Rig_);
    updateRigCamera();
}

std::vector<std::string> PlayerCameraComponent::buildCameraGetLines() const {
    std::vector<std::string> lines;
    lines.push_back(fmt::format("[CAMERA-GET] mode=\"{}\" view={}", getCameraModeString(),
                                static_cast<int>(cameraMode_)));
    const CameraRig* rig = cameraRigFor(cameraMode_);
    if (rig) {
        lines.push_back("[CAMERA-GET] " + RigValues(*rig) +
                        " units=deg,deg,eq_units,eq_units");
    } else {
        lines.push_back("[CAMERA-GET] az=n/a pitch=n/a dist=n/a height=n/a "
                        "(not an orbit camera; /camera set switches to User Defined Camera 1)");
    }
    const float feetZ = playerFeetZ();
    lines.push_back(fmt::format(
        "[CAMERA-GET] player=({:.3f},{:.3f},{:.3f}) feetZ={:.3f} heading={:.3f} frame=eq_client",
        ctx_.playerX, ctx_.playerY, ctx_.playerZ, feetZ, ctx_.playerHeading));
    if (rig) {
        const CameraRigPose p = computeCameraRigPose(*rig, ctx_.playerX, ctx_.playerY, feetZ,
                                                     ctx_.playerHeading);
        lines.push_back(fmt::format(
            "[CAMERA-GET] ideal camPos=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) frame=eq_client",
            p.camX, p.camY, p.camZ, p.targetX, p.targetY, p.targetZ));
    }
    if (ctx_.camera) {
        // The node is Irrlicht Y-up; report it in the same EQ client frame.
        const irr::core::vector3df pos = ctx_.camera->getPosition();
        const irr::core::vector3df tgt = ctx_.camera->getTarget();
        lines.push_back(fmt::format(
            "[CAMERA-GET] node camPos=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) frame=eq_client",
            pos.X, pos.Z, pos.Y, tgt.X, tgt.Z, tgt.Y));
    } else {
        lines.push_back("[CAMERA-GET] node=absent (no camera scene node)");
    }
    return lines;
}

std::vector<std::string> PlayerCameraComponent::runCameraCommand(const std::string& args) {
    std::vector<std::string> out;
    const CameraCommand cmd = ParseCameraCommand(args);
    auto modeLine = [this]() { return "Camera mode: " + getCameraModeString(); };

    switch (cmd.kind) {
        case CameraCommand::Kind::Report:
            out.push_back(modeLine());
            break;
        case CameraCommand::Kind::Cycle:
            cycleCameraMode();
            out.push_back(modeLine());
            break;
        case CameraCommand::Kind::SetMode:
            setCameraMode(cmd.mode);
            out.push_back(modeLine());
            break;
        case CameraCommand::Kind::Get:
            out = buildCameraGetLines();
            break;
        case CameraCommand::Kind::Help:
            out = CameraCommandUsageLines();
            break;
        case CameraCommand::Kind::Error:
            out.push_back(cmd.error);
            break;
        case CameraCommand::Kind::Set:
        case CameraCommand::Kind::Nudge: {
            const bool nudge = (cmd.kind == CameraCommand::Kind::Nudge);
            if (!isUserCameraMode(cameraMode_)) {
                setCameraMode(CameraMode::User1);
                if (cameraMode_ != CameraMode::User1) {
                    out.push_back("Camera: cannot change the camera while a zone is loading");
                    break;
                }
                out.push_back(fmt::format("Switched to {} -- /camera {} edits a user camera",
                                          cameraModeName(CameraMode::User1),
                                          nudge ? "nudge" : "set"));
            }
            CameraRig* rig = rigFor(cameraMode_);
            const CameraRigEdit& e = cmd.edit;
            if (e.hasAzimuth) rig->azimuthDeg = (nudge ? rig->azimuthDeg : 0.0f) + e.azimuthDeg;
            if (e.hasPitch) rig->pitchDeg = (nudge ? rig->pitchDeg : 0.0f) + e.pitchDeg;
            if (e.hasDistance) rig->distance = (nudge ? rig->distance : 0.0f) + e.distance;
            if (e.hasHeight) rig->height = (nudge ? rig->height : 0.0f) + e.height;
            const bool clamped = clampCameraRig(*rig);
            updateRigCamera();
            out.push_back(fmt::format("{}: {}{}", getCameraModeString(), RigValues(*rig),
                                      clamped ? " (clamped to range)" : ""));
            break;
        }
    }
    return out;
}

}  // namespace Graphics
}  // namespace EQT
