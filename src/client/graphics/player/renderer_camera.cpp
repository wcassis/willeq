#include "client/graphics/vision_ambient.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/renderer_zone_merge.h"  // ZoneMergeBuffers complete type (unique_ptr member destruction)
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include <filesystem>
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/inventory/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "common/logging.h"
#include <fmt/format.h>
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <cstdio>
#ifdef __linux__
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#endif

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif
#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern void gles2DeleteAllStaticHWBuffers(void* driver);
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetHWBufferCount(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes = 0);
extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
                                           unsigned int vbo, unsigned int ebo,
                                           unsigned int vertexCount, unsigned int indexCount,
                                           int vertexType);
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <cmath>
#include <limits>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Stub definition for WindowManager — class has been deleted but unique_ptr member
// remains in the header (forward-declared). This allows the unique_ptr destructor to compile.
namespace eqt { namespace ui { class WindowManager { public: virtual ~WindowManager() = default; }; } }

namespace EQT {
namespace Graphics {

// T300: the first-person camera's constants and its placement math now live
// in include/client/graphics/first_person_camera.h -- read that header before
// changing any of these numbers, and change them THERE, not here. The three
// tunables it holds are kFirstPersonPitchDownDegrees (3.65, was T75's 12),
// kFirstPersonHorizontalFovDegrees (62.8, was Irrlicht's default 72 vertical
// == ~88 horizontal). The body-hiding lever is no longer a near plane at all:
// Leg A measured the official client clipping nothing, so hiding is a DOWN-
// PITCH CLAMP -- a NAMED MEASURED CONSTANT, kFirstPersonMaxDownPitchDegrees
// in first_person_eye.h. T301 refuted T300's 90 - vFOV/2 derivation and
// removed the FOV parameter so it cannot come back.
//
// T76's kFirstPersonCameraStandoffUnits (0.3 BACKWARD) is gone: it existed to
// pull the eye off in-world arm geometry that is no longer drawn as a
// separate pass, it pointed the wrong way for a real eye, and T298 measured
// the official client's forward offset at 0.00 +/- 0.4 -- which is, since
// T309, the value the eye uses (see kEyeForwardOffsetUnits).
void PlayerCameraComponent::applyCameraProjectionForMode() {
    if (!ctx_.camera) return;
    const bool firstPerson = (cameraMode_ == CameraMode::FirstPerson);
    // T300 (post-Leg-A): the NEAR PLANE IS THE SAME IN EVERY MODE. Leg A
    // measured the official client clipping nothing -- geometry draws at
    // point-blank range -- so first person must clip exactly as third person
    // does. The near-plane hiding that briefly lived here is refuted and gone.
    // T309: neither is the clamp the hiding mechanism -- the measured DRAW
    // RULE is (EntityRenderer::updateFirstPersonBodyDrawRule()).
    // T310 Leg A: the RUNTIME near plane, == kWorldNearPlaneUnits unless
    // /fpcam or --fpcam overrode it. Still applied in EVERY mode.
    ctx_.camera->setNearValue(fpTunable(FpTunable::NearPlaneUnits));
    // T466: THE FOV IS THE SAME IN EVERY MODE. The official client's Chase
    // camera frame matches its own First Person frame at scale 1.00 on the
    // far scene (review/T466/logs/oracle_chase_vs_firstperson_fov_scale.tsv:
    // edge 0.804 at s=1.00, >= 0.91 at s=0.95 and 1.05), so third person uses
    // the measured 62.8 degree horizontal FOV too. Before T466 third person
    // used Irrlicht's default (kWorldVerticalFovRadians, 72 degrees VERTICAL).
    // The visual gate renders through gles2_zone_benchmark's own camera, which
    // never reaches this function.
    ctx_.camera->setFOV(firstPersonVerticalFovRadians(ctx_.camera->getAspectRatio()));
    if (firstPerson) {
        applyFirstPersonPitchClamp();
    } else {
        // Leaving first person must put the up vector back, or third person
        // inherits whatever roll the last first-person pitch left behind --
        // the visual gate's goldens are third-person renders.
        ctx_.camera->setUpVector(irr::core::vector3df(0.0f, 1.0f, 0.0f));
    }
    LOG_DEBUG(MOD_GRAPHICS,
              "[FP-PROJ] firstPerson={} near={:.3f} fovY={:.4f}rad aspect={:.4f}",
              firstPerson, ctx_.camera->getNearValue(), ctx_.camera->getFOV(),
              ctx_.camera->getAspectRatio());
}

// T309: THIS IS NOT THE HIDING SEAM. T300 introduced it as one -- "the body
// hangs directly below the eye, so a down-pitch clamp hides it" -- and that
// is refuted twice over: T301 Leg B showed no clamp value can empty the
// down-frustum band (hiding a Human male's shoulders needs C > 49.1 and
// hiding his feet needs C < 48.4), and the MEASURED clamp is 94.9 degrees,
// about 5 degrees PAST THE NADIR, where the frustum plainly contains the
// body. The body is hidden by the draw rule
// (EntityRenderer::updateFirstPersonBodyDrawRule()).
//
// What survives here is a CONTROL-FEEL LIMIT: how far down the official
// client lets you look. Do not re-derive it from the FOV (90 - vFOV/2 is by
// construction the angle that ADMITS the body's cone, not one that excludes
// it) and do not pull it back under 90 in the belief that it hides anything.
void PlayerCameraComponent::applyFirstPersonPitchClamp() {
    if (!ctx_.camera || cameraMode_ != CameraMode::FirstPerson) return;

    const float minLook = firstPersonMinLookPitchDegrees();
    // T310 Leg A: both limits read the runtime tunables (== the constants
    // unless overridden).
    const float maxUp = fpTunable(FpTunable::MaxUpPitchDegrees);
    if (playerPitch_ >= minLook && playerPitch_ <= maxUp) return;

    const float before = playerPitch_;
    playerPitch_ = std::min(std::max(playerPitch_, minLook), maxUp);
    LOG_DEBUG(MOD_GRAPHICS,
              "[FP-CLAMP] mouse-look pitch {:.2f} -> {:.2f} deg (floor {:.2f}, measured total down-pitch limit {:.2f})",
              before, playerPitch_, minLook,
              firstPersonMaxDownPitchDegrees());
    repositionFirstPersonCamera();
}

// One place that answers "what vertical FOV is the first-person camera using",
// so the clamp derivation and the projection cannot disagree.
float PlayerCameraComponent::getFirstPersonVerticalFov() const {
    return firstPersonVerticalFovRadians(ctx_.camera ? ctx_.camera->getAspectRatio() : 4.0f / 3.0f);
}

// T310 Leg A -- `/fpcam dump`: gather the RESOLVED first-person pose. Only the
// GATHER lives here, because this camera TU is the ONE translation unit
// allowed to read the camera formula
// (tests/test_first_person_camera_single_formula.cpp); the FORMATTING is
// FormatFirstPersonPoseDump() in first_person_tunables.cpp, which is what
// Leg C's sweep driver parses and which is therefore testable on its own.
std::vector<std::string> PlayerCameraComponent::buildFirstPersonCameraDumpLines() const {
    FirstPersonPoseSample sample;

    EntityRenderer::FirstPersonEyeOffset eye;
    if (ctx_.entityRenderer) {
        eye = ctx_.entityRenderer->getPlayerFirstPersonEyeOffset();
        sample.collisionZOffset = ctx_.entityRenderer->getPlayerCollisionZOffset();
    }
    if (eye.irrY <= 0.0f) eye.irrY = 3.65f;

    sample.modeName = getCameraModeString();
    sample.cameraNodePresent = (ctx_.camera != nullptr);
    sample.eyeFromHeadBone = eye.derivedFromHead;
    sample.aspect = ctx_.camera ? ctx_.camera->getAspectRatio() : 4.0f / 3.0f;

    const FirstPersonCameraPlacement p = computeFirstPersonCamera(
        ctx_.playerX, ctx_.playerY, ctx_.playerZ, eye.irrY, playerConfig_.eyeHeight,
        ctx_.playerHeading, playerPitch_, 100.0f, eye.irrX, eye.irrZ);
    sample.eyeIrrX = p.posX;  sample.eyeIrrY = p.posY;  sample.eyeIrrZ = p.posZ;
    sample.targetIrrX = p.targetX; sample.targetIrrY = p.targetY; sample.targetIrrZ = p.targetZ;
    sample.upX = p.upX; sample.upY = p.upY; sample.upZ = p.upZ;

    sample.nodeOriginEqZ = ctx_.playerZ;
    sample.eyeOffsetIrrX = eye.irrX;
    sample.eyeOffsetIrrY = eye.irrY;
    sample.eyeOffsetIrrZ = eye.irrZ;

    sample.headingEqUnits = ctx_.playerHeading;
    sample.lookPitchDeg = playerPitch_;
    sample.fixedPitchDownDeg = fpTunable(FpTunable::PitchDownDegrees);
    sample.maxDownPitchDeg = firstPersonMaxDownPitchDegrees();
    sample.maxUpPitchDeg = fpTunable(FpTunable::MaxUpPitchDegrees);
    sample.totalPitchDeg = sample.lookPitchDeg - sample.fixedPitchDownDeg;
    if (sample.totalPitchDeg < -sample.maxDownPitchDeg) sample.totalPitchDeg = -sample.maxDownPitchDeg;
    if (sample.totalPitchDeg > sample.maxUpPitchDeg) sample.totalPitchDeg = sample.maxUpPitchDeg;

    sample.hfovDeg = fpTunable(FpTunable::HorizontalFovDegrees);
    sample.vfovDeg = firstPersonVerticalFovRadians(sample.aspect) * 180.0f / kFirstPersonPi;
    sample.nearPlane = ctx_.camera ? ctx_.camera->getNearValue() : fpTunable(FpTunable::NearPlaneUnits);

    return FormatFirstPersonPoseDump(sample);
}

void PlayerCameraComponent::repositionFirstPersonCamera() {
    if (!ctx_.camera || cameraMode_ != CameraMode::FirstPerson) {
        return;
    }
    // The projection is part of the first-person camera, not a separate
    // setting: without the near plane the player's own body fills the frame.
    // Applied here as well as on the mode transitions so no entry point can
    // position the eye without it.
    applyCameraProjectionForMode();

    // T301: the eye is the HEAD BONE's, per race, from the mesh -- not the
    // whole-body bbox crown T300 used. getPlayerFirstPersonEyeOffset() falls
    // back to that crown only for a model with no head anchor, and says so.
    EntityRenderer::FirstPersonEyeOffset eye;
    if (ctx_.entityRenderer) {
        eye = ctx_.entityRenderer->getPlayerFirstPersonEyeOffset();
    } else {
        eye.irrY = 3.65f;
    }
    if (eye.irrY <= 0.0f) {
        // Node-origin-relative fallback for a human-sized entity whose model
        // has not loaded yet -- NOT a feet-relative 6.x.
        eye.irrY = 3.65f;
    }

    // T295: the user's mouse-look pitch (playerPitch_, degrees, +up) is
    // applied on top of the fixed down-bias inside the helper. Before T295
    // playerPitch_ existed ONLY in renderer_player_physics.cpp's private copy
    // of this formula, so moving discarded the bias and standing still
    // discarded the mouse.
    const FirstPersonCameraPlacement placement = computeFirstPersonCamera(
        ctx_.playerX, ctx_.playerY, ctx_.playerZ, eye.irrY, playerConfig_.eyeHeight,
        ctx_.playerHeading, playerPitch_, 100.0f, eye.irrX, eye.irrZ);

    ctx_.camera->setPosition(irr::core::vector3df(placement.posX, placement.posY, placement.posZ));
    ctx_.camera->setTarget(irr::core::vector3df(placement.targetX, placement.targetY, placement.targetZ));
    // REQUIRED, not cosmetic: Leg A's measured 94.9-degree clamp reaches past
    // the nadir, where the default (0,1,0) up vector degenerates and flips.
    ctx_.camera->setUpVector(irr::core::vector3df(placement.upX, placement.upY, placement.upZ));

    // T295: DEBUG, not INFO -- this now runs on every frame in which the
    // player moves, turns or looks. The FP-CAMERA tag is the evidence
    // emitter the unit's still-vs-moving proof diffs. T300: playerZ_ is the
    // node origin (== server Z, model CENTRE), not the feet.
    LOG_DEBUG(MOD_GRAPHICS, "[FP-CAMERA] eq=({:.4f},{:.4f},{:.4f}) nodeOriginZ={:.4f} eyeOffset=({:.4f},{:.4f},{:.4f}) fromHead={} adjust={:.2f} heading={:.4f} lookPitch={:.4f}deg fixedPitchDown={:.4f}deg maxDownPitch={:.2f}deg",
              placement.posX, placement.posZ, placement.posY, ctx_.playerZ,
              eye.irrX, eye.irrY, eye.irrZ, eye.derivedFromHead,
              playerConfig_.eyeHeight, ctx_.playerHeading, playerPitch_,
              kFirstPersonPitchDownDegrees,
              firstPersonMaxDownPitchDegrees());
}

void PlayerCameraComponent::setPlayerSpawnId(uint16_t spawnId) {
    if (owner_.isLoading()) return;
    playerSpawnId_ = spawnId;
    LOG_INFO(MOD_GRAPHICS, "[IrrlichtRenderer] Player spawn ID set to: {}", spawnId);

    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->setPlayerSpawnId(spawnId);

        // T300: record the mode (name tag off in first person); the player
        // model itself draws normally either way.
        bool firstPerson = (cameraMode_ == CameraMode::FirstPerson);
        ctx_.entityRenderer->setPlayerFirstPersonMode(firstPerson);

        const EntityRenderer::FirstPersonEyeOffset eye =
            ctx_.entityRenderer->getPlayerFirstPersonEyeOffset();
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerSpawnId: eyeOffset=({:.2f},{:.2f},{:.2f}) fromHead={} firstPerson={}",
                 eye.irrX, eye.irrY, eye.irrZ, eye.derivedFromHead, firstPerson);

        // Fix first-person camera height now that we have the player model
        if (firstPerson && eye.irrY > 0.0f) {
            repositionFirstPersonCamera();
        }
    }
}

// T361 (board B067): the GLOBAL ambient now carries the vision floor
// (IrrlichtRenderer::updateTimeOfDay -> applyVisionAmbientFloor). That
// function only runs when the server sends a time update, which is minutes
// apart, so a vision change has to re-run it or the buff would not show until
// the next tick. The three sites that write ctx_.currentVision all call this.
//
// T423 (board B362): it MARKS instead of recomposing inline. Before this there
// were two paths into the recompute -- the clock packet and this one -- and
// only the vision input had a second one, so weather and the zone-light toggle
// were left waiting on a packet that arrives at zone-in and on `#set time` and
// at no other moment. There is one path now: every non-clock writer marks, and
// FrameComponent consumes the mark once per frame. A vision change still
// reaches the screen on the very next frame (this runs on the render thread,
// from the bridge's VisionChanged arm, before that frame's render phase), so
// nothing about the buff's latency changed.
void PlayerCameraComponent::refreshVisionLighting() {
    if (owner_.isLoading()) return;
    owner_.markSceneAmbientDirty();
}

void PlayerCameraComponent::setPlayerRace(uint16_t raceId) {
    // T361 (board B060): this table is MEASURED against the official Titanium
    // client, not inferred. The old table put Wood Elf (4), High Elf (5),
    // Troll (9) and Iksar (128) on Ultravision; the oracle puts all four on
    // Infravision and gives Ultravision to the DARK ELF ALONE. Vah Shir (130)
    // was on Normal by omission and is Infravision.
    //
    // Method (review/T361/README.md, "the racial sweep"): two logins as
    // Oraclealpha in qeynos2 at forced 22:00, camera never touched, race
    // changed in-game with `#race N`, and the 700x420 world crop read from the
    // session VIDEO -- not from stills, one of which was a bad grab that would
    // have put race 6 in a class of its own. Each session carried its own
    // positive control (a real `#castspell 46` Ultravision landing at the same
    // camera). Three levels, each window flat to +/-0.14 of luma against a
    // 4.8-luma separation:
    //
    //   luma 54.42  Normal        races 1, 2, 3
    //   luma 60.18  Infravision   races 4, 5, 7, 8, 9, 10, 11, 12, 128, 130
    //   luma 65.00  Ultravision   race  6      (= the cast-Ultravision control,
    //                                           65.004 and 65.006, twice)
    //
    // THE LAST ROW IS SETTLED (T428, board B060 CLOSED). Froglok (330)
    // measured 63.49 here -- a distinct FOURTH level the three-valued enum
    // could not express, so T361 left it on the Normal default rather than
    // round it to a neighbour by argument. Will ruled a fourth level; T428
    // measured it per channel on the same official client (x2.6803 R,
    // x1.9478 G, x2.1753 B over its own night base, against Ultravision's
    // x2.7995 / x1.9517 / x2.4946) and `VisionType::FroglokVision` carries
    // it. It is 5.5% under Ultravision on B and 60% over Infravision there.
    baseVision_ = racialBaseVision(raceId);
    ctx_.currentVision = baseVision_;
    LOG_INFO(MOD_GRAPHICS, "Player race {} -> base vision: {}",
             raceId, visionName(ctx_.currentVision));
    // Zone light colors updated by SimulationWorker via visionType in SimulationInput
    refreshVisionLighting();
}

// T334 (board B046): APPLY, not upgrade.
//
// The one production caller is the VisionChanged bridge arm
// (bridge/irrlicht_bridge.cpp), and the one publisher is EverQuest's
// BuffManager fade callback (core/everquest.cpp), which RECOMPUTES the vision
// from every buff still up and publishes the absolute result. Under the old
// "only upgrade, never downgrade" rule that publish could only ever raise
// vision, so a fade -- the very thing the callback exists to report -- was a
// no-op and an Ultravision buff lasted the whole session.
//
// The racial floor is not lost: it lives in baseVision_ (setPlayerRace), and
// resetVisionToBase() restores it here before the buff value is applied on
// top. A published Normal means "no vision BUFFS left", never "no vision".
void PlayerCameraComponent::setVisionType(VisionType vision) {
    if (owner_.isLoading()) return;
    resetVisionToBase();
    // Buffs raise vision above the racial base; they never lower it below.
    // T428: rank, not enumerator -- FroglokVision is 3 by wire value and 2 by
    // brightness, so `vision > ctx_.currentVision` would refuse to raise a
    // Froglok to Ultravision. See visionRank() in vision_type.h.
    if (visionRank(vision) > visionRank(ctx_.currentVision)) {
        ctx_.currentVision = vision;
        LOG_INFO(MOD_GRAPHICS, "Vision upgraded to: {}",
                 visionName(ctx_.currentVision));
        // Zone light colors updated by SimulationWorker via visionType in SimulationInput
        refreshVisionLighting();
    }
}

void PlayerCameraComponent::resetVisionToBase() {
    if (owner_.isLoading()) return;
    if (ctx_.currentVision != baseVision_) {
        ctx_.currentVision = baseVision_;
        LOG_INFO(MOD_GRAPHICS, "Vision reset to base: {}",
                 visionName(ctx_.currentVision));
        // Zone light colors updated by SimulationWorker via visionType in SimulationInput
        refreshVisionLighting();
    }
}

void PlayerCameraComponent::setPlayerPosition(float x, float y, float z, float heading) {
    if (owner_.isLoading()) return;
    ctx_.playerX = x;
    ctx_.playerY = y;
    ctx_.playerZ = z;
    ctx_.playerHeading = heading;
    // T332 (B049): this position was HANDED to us -- it is not a position this
    // renderer derived, and at zone-in it is the raw WIRE z straight out of the
    // zone-load snapshot. Mark it so the echo the movement integrator sends
    // back on the next PlayerPositionChanged intent is recognisable as an echo.
    playerPositionIsSeed_ = true;

    LOG_DEBUG(MOD_GRAPHICS, "[ZONE-IN] setPlayerPosition: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} (EQ units, EQ heading 0-512)",
             x, y, z, heading);

    // Trust the server's Z position - the server places entities at a consistent height
    // that accounts for model placement. Ground-snapping was incorrectly overriding
    // the server Z (e.g., snapping from 3.75 to 0) causing player/NPC Z mismatch.

    // Check if player is within zone geometry bounds before following
    // Uses cached bounds — geometry vectors may have been released after zone load
    bool playerInBounds = true;
    if (ctx_.zoneBoundsValid) {
        float margin = 500.0f;
        if (x < ctx_.zoneBoundsMinX - margin || x > ctx_.zoneBoundsMaxX + margin ||
            y < ctx_.zoneBoundsMinY - margin || y > ctx_.zoneBoundsMaxY + margin) {
            playerInBounds = false;
        }
    }

    if (cameraMode_ == CameraMode::Chase && playerInBounds && ctx_.cameraController) {
        LOG_DEBUG(MOD_GRAPHICS, "[ZONE-IN] Camera mode=Chase, calling setFollowPosition({:.2f},{:.2f},{:.2f},{:.2f})",
                 x, y, z, heading);
        ctx_.cameraController->enableZoneInLogging();  // Enable one-time detailed logging
        ctx_.cameraController->setFollowPosition(x, y, z, heading);
    } else if (cameraMode_ == CameraMode::FirstPerson && ctx_.camera && playerInBounds) {
        // T75: this used to be a third independent copy of the first-person
        // camera placement formula (setCameraMode()/cycleCameraMode() and
        // setPlayerSpawnId() already call the shared repositionFirstPersonCamera()
        // helper T74 extracted). playerX_/playerY_/playerZ_/playerHeading_
        // were already updated above, which is everything the helper reads,
        // so delegating here is safe and removes the last duplicate --
        // otherwise this per-movement path would silently keep the old
        // uncorrected/unpitched formula while every other entry point got
        // T75's fix. T74 deliberately left this copy alone citing session
        // time/hot-path risk, not correctness; de-duplicating it is exactly
        // what removes that risk.
        repositionFirstPersonCamera();
    } else if (isRigCameraMode(cameraMode_) && playerInBounds) {
        // T461: the rig is expressed around the player, so a relocation moves
        // the camera with the player and leaves the rig itself untouched.
        updateRigCamera();
    } else {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera: mode={} playerInBounds={} camera={} (no camera update)",
                 static_cast<int>(cameraMode_), playerInBounds, ctx_.camera != nullptr);
    }

    // T412 (board B294): no flag. The next frame's SimulationWorker pass
    // recomputes visibility from the camera position this call just set.
}

void PlayerCameraComponent::setSwimmingState(bool swimming, float swimSpeed, bool levitating) {
    if (owner_.isLoading()) return;
    if (playerMovement_.isSwimming != swimming) {
        LOG_INFO(MOD_GRAPHICS, "[Swimming] {} water (swimSpeed={}, levitating={})",
                 swimming ? "Entering" : "Exiting", swimSpeed, levitating);

        // When entering water, cancel any jump in progress
        if (swimming) {
            playerMovement_.isJumping = false;
            playerMovement_.verticalVelocity = 0.0f;
        }
    }

    playerMovement_.isSwimming = swimming;
    playerMovement_.swimSpeed = swimSpeed;
    playerMovement_.isLevitating = levitating;
}

void PlayerCameraComponent::setCameraMode(CameraMode mode) {
    if (owner_.isLoading()) return;
    cameraMode_ = mode;

    // T300: the player model draws normally in both modes; this only
    // records the mode and hides the floating name tag in first person.
    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->setPlayerFirstPersonMode(cameraMode_ == CameraMode::FirstPerson);
    }
    // T300: the near plane and FOV are part of the first-person camera and
    // must be restored on the way OUT as well, which repositionFirstPersonCamera()
    // (an early-return no-op outside first person) cannot do.
    applyCameraProjectionForMode();
    // T74: entering first person must move the camera to the player's own
    // eye position immediately -- previously only a later position update
    // did this, so a stationary player toggling modes kept looking through
    // the stale third-person Follow transform (stage-1 diagnostic finding).
    repositionFirstPersonCamera();
    // T461: entering a rig mode places the camera at once, for the same reason
    // (a no-op in first person and Chase).
    updateRigCamera();
}

// T461: the official cycle, eqstr_us.txt 9500..9505 (camera_mode.h). Before
// T461 this rotated Free -> Follow -> FirstPerson and its key, Ctrl+F5, never
// reached it: RendererEventReceiver queued RendererAction::ToggleCameraMode
// and processCommonInput() had no arm for it. It now goes through
// setCameraMode(), so the projection, the name-tag record and the placement
// are the ones every other entry point gets.
void PlayerCameraComponent::cycleCameraMode() {
    setCameraMode(nextCameraMode(cameraMode_));
    LOG_INFO(MOD_GRAPHICS, "Camera mode: {}", getCameraModeString());
}

// T464 (board B532): the zone-in camera step. Both Application loaders used to
// call setCameraMode(Chase) here, on EVERY zone load, while
// FrameComponent::syncUiPreferences() reads the ini only once per
// character/server identity -- so every zone after the first put the player
// back in Chase, and the next save-on-difference wrote View=3 over the saved
// choice. This RE-ENTERS the player's current camera instead: setCameraMode()'s
// side effects are exactly what a new zone needs (the first-person name-tag
// record on the player entity this zone just registered, the projection, the
// immediate placement; T74/T300), and the mode stays the player's.
// setPlayerPosition() then places the camera at the new pose in that mode.
// What a character with NO saved view gets (Chase) is applied where the
// identity is loaded, syncUiPreferences(), not on every zone.
void PlayerCameraComponent::placeCameraForZoneIn(float x, float y, float z, float heading512) {
    setCameraMode(cameraMode_);
    setPlayerPosition(x, y, z, heading512);
}

std::string PlayerCameraComponent::getCameraModeString() const {
    return cameraModeName(cameraMode_);
}



} // namespace Graphics
} // namespace EQT
