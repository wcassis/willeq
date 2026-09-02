#include "client/graphics/irrlicht_renderer.h"
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
void IrrlichtRenderer::applyCameraProjectionForMode() {
    if (!camera_) return;
    const bool firstPerson = (cameraMode_ == CameraMode::FirstPerson);
    // T300 (post-Leg-A): the NEAR PLANE IS THE SAME IN EVERY MODE. Leg A
    // measured the official client clipping nothing -- geometry draws at
    // point-blank range -- so first person must clip exactly as third person
    // does. The near-plane hiding that briefly lived here is refuted and gone.
    // T309: neither is the clamp the hiding mechanism -- the measured DRAW
    // RULE is (EntityRenderer::updateFirstPersonBodyDrawRule()). Only the FOV
    // is mode-specific here, so third person stays byte-for-byte what it was
    // and the visual gate's goldens hold.
    // T310 Leg A: the RUNTIME near plane, == kWorldNearPlaneUnits unless
    // /fpcam or --fpcam overrode it. Still applied in EVERY mode.
    camera_->setNearValue(fpTunable(FpTunable::NearPlaneUnits));
    camera_->setFOV(firstPerson ? firstPersonVerticalFovRadians(camera_->getAspectRatio())
                                : kWorldVerticalFovRadians);
    if (firstPerson) {
        applyFirstPersonPitchClamp();
    } else {
        // Leaving first person must put the up vector back, or third person
        // inherits whatever roll the last first-person pitch left behind --
        // the visual gate's goldens are third-person renders.
        camera_->setUpVector(irr::core::vector3df(0.0f, 1.0f, 0.0f));
    }
    LOG_DEBUG(MOD_GRAPHICS,
              "[FP-PROJ] firstPerson={} near={:.3f} fovY={:.4f}rad aspect={:.4f}",
              firstPerson, camera_->getNearValue(), camera_->getFOV(),
              camera_->getAspectRatio());
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
void IrrlichtRenderer::applyFirstPersonPitchClamp() {
    if (!camera_ || cameraMode_ != CameraMode::FirstPerson) return;

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
float IrrlichtRenderer::getFirstPersonVerticalFov() const {
    return firstPersonVerticalFovRadians(camera_ ? camera_->getAspectRatio() : 4.0f / 3.0f);
}

// T310 Leg A -- `/fpcam dump`: gather the RESOLVED first-person pose. Only the
// GATHER lives here, because this camera TU is the ONE translation unit
// allowed to read the camera formula
// (tests/test_first_person_camera_single_formula.cpp); the FORMATTING is
// FormatFirstPersonPoseDump() in first_person_tunables.cpp, which is what
// Leg C's sweep driver parses and which is therefore testable on its own.
std::vector<std::string> IrrlichtRenderer::buildFirstPersonCameraDumpLines() const {
    FirstPersonPoseSample sample;

    EntityRenderer::FirstPersonEyeOffset eye;
    if (entityRenderer_) {
        eye = entityRenderer_->getPlayerFirstPersonEyeOffset();
        sample.collisionZOffset = entityRenderer_->getPlayerCollisionZOffset();
    }
    if (eye.irrY <= 0.0f) eye.irrY = 3.65f;

    sample.modeName = getCameraModeString();
    sample.cameraNodePresent = (camera_ != nullptr);
    sample.eyeFromHeadBone = eye.derivedFromHead;
    sample.aspect = camera_ ? camera_->getAspectRatio() : 4.0f / 3.0f;

    const FirstPersonCameraPlacement p = computeFirstPersonCamera(
        playerX_, playerY_, playerZ_, eye.irrY, playerConfig_.eyeHeight,
        playerHeading_, playerPitch_, 100.0f, eye.irrX, eye.irrZ);
    sample.eyeIrrX = p.posX;  sample.eyeIrrY = p.posY;  sample.eyeIrrZ = p.posZ;
    sample.targetIrrX = p.targetX; sample.targetIrrY = p.targetY; sample.targetIrrZ = p.targetZ;
    sample.upX = p.upX; sample.upY = p.upY; sample.upZ = p.upZ;

    sample.nodeOriginEqZ = playerZ_;
    sample.eyeOffsetIrrX = eye.irrX;
    sample.eyeOffsetIrrY = eye.irrY;
    sample.eyeOffsetIrrZ = eye.irrZ;

    sample.headingEqUnits = playerHeading_;
    sample.lookPitchDeg = playerPitch_;
    sample.fixedPitchDownDeg = fpTunable(FpTunable::PitchDownDegrees);
    sample.maxDownPitchDeg = firstPersonMaxDownPitchDegrees();
    sample.maxUpPitchDeg = fpTunable(FpTunable::MaxUpPitchDegrees);
    sample.totalPitchDeg = sample.lookPitchDeg - sample.fixedPitchDownDeg;
    if (sample.totalPitchDeg < -sample.maxDownPitchDeg) sample.totalPitchDeg = -sample.maxDownPitchDeg;
    if (sample.totalPitchDeg > sample.maxUpPitchDeg) sample.totalPitchDeg = sample.maxUpPitchDeg;

    sample.hfovDeg = fpTunable(FpTunable::HorizontalFovDegrees);
    sample.vfovDeg = firstPersonVerticalFovRadians(sample.aspect) * 180.0f / kFirstPersonPi;
    sample.nearPlane = camera_ ? camera_->getNearValue() : fpTunable(FpTunable::NearPlaneUnits);

    return FormatFirstPersonPoseDump(sample);
}

void IrrlichtRenderer::repositionFirstPersonCamera() {
    if (!camera_ || cameraMode_ != CameraMode::FirstPerson) {
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
    if (entityRenderer_) {
        eye = entityRenderer_->getPlayerFirstPersonEyeOffset();
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
        playerX_, playerY_, playerZ_, eye.irrY, playerConfig_.eyeHeight,
        playerHeading_, playerPitch_, 100.0f, eye.irrX, eye.irrZ);

    camera_->setPosition(irr::core::vector3df(placement.posX, placement.posY, placement.posZ));
    camera_->setTarget(irr::core::vector3df(placement.targetX, placement.targetY, placement.targetZ));
    // REQUIRED, not cosmetic: Leg A's measured 94.9-degree clamp reaches past
    // the nadir, where the default (0,1,0) up vector degenerates and flips.
    camera_->setUpVector(irr::core::vector3df(placement.upX, placement.upY, placement.upZ));

    // T295: DEBUG, not INFO -- this now runs on every frame in which the
    // player moves, turns or looks. The FP-CAMERA tag is the evidence
    // emitter the unit's still-vs-moving proof diffs. T300: playerZ_ is the
    // node origin (== server Z, model CENTRE), not the feet.
    LOG_DEBUG(MOD_GRAPHICS, "[FP-CAMERA] eq=({:.4f},{:.4f},{:.4f}) nodeOriginZ={:.4f} eyeOffset=({:.4f},{:.4f},{:.4f}) fromHead={} adjust={:.2f} heading={:.4f} lookPitch={:.4f}deg fixedPitchDown={:.4f}deg maxDownPitch={:.2f}deg",
              placement.posX, placement.posZ, placement.posY, playerZ_,
              eye.irrX, eye.irrY, eye.irrZ, eye.derivedFromHead,
              playerConfig_.eyeHeight, playerHeading_, playerPitch_,
              kFirstPersonPitchDownDegrees,
              firstPersonMaxDownPitchDegrees());
}

void IrrlichtRenderer::setPlayerSpawnId(uint16_t spawnId) {
    if (isLoading()) return;
    playerSpawnId_ = spawnId;
    LOG_INFO(MOD_GRAPHICS, "[IrrlichtRenderer] Player spawn ID set to: {}", spawnId);

    if (entityRenderer_) {
        entityRenderer_->setPlayerSpawnId(spawnId);

        // T300: record the mode (name tag off in first person); the player
        // model itself draws normally either way.
        bool firstPerson = (cameraMode_ == CameraMode::FirstPerson);
        entityRenderer_->setPlayerFirstPersonMode(firstPerson);

        const EntityRenderer::FirstPersonEyeOffset eye =
            entityRenderer_->getPlayerFirstPersonEyeOffset();
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerSpawnId: eyeOffset=({:.2f},{:.2f},{:.2f}) fromHead={} firstPerson={}",
                 eye.irrX, eye.irrY, eye.irrZ, eye.derivedFromHead, firstPerson);

        // Fix first-person camera height now that we have the player model
        if (firstPerson && eye.irrY > 0.0f) {
            repositionFirstPersonCamera();
        }
    }
}

void IrrlichtRenderer::setPlayerRace(uint16_t raceId) {
    // Determine base vision from race
    // Ultravision: Dark Elf (6), High Elf (5), Wood Elf (4), Troll (9), Iksar (128)
    // Infravision: Dwarf (8), Gnome (12), Half Elf (7), Ogre (10), Halfling (11)
    // Normal: Human (1), Barbarian (2), Erudite (3), Vah Shir (130), Froglok (330)
    switch (raceId) {
        case 4:   // Wood Elf
        case 5:   // High Elf
        case 6:   // Dark Elf
        case 9:   // Troll
        case 128: // Iksar
            baseVision_ = VisionType::Ultravision;
            break;
        case 7:   // Half Elf
        case 8:   // Dwarf
        case 10:  // Ogre
        case 11:  // Halfling
        case 12:  // Gnome
            baseVision_ = VisionType::Infravision;
            break;
        default:  // Human (1), Barbarian (2), Erudite (3), Vah Shir (130), Froglok (330), etc.
            baseVision_ = VisionType::Normal;
            break;
    }
    currentVision_ = baseVision_;
    LOG_INFO(MOD_GRAPHICS, "Player race {} -> base vision: {}",
             raceId, currentVision_ == VisionType::Ultravision ? "Ultravision" :
                     currentVision_ == VisionType::Infravision ? "Infravision" : "Normal");
    // Zone light colors updated by SimulationWorker via visionType in SimulationInput
}

void IrrlichtRenderer::setVisionType(VisionType vision) {
    if (isLoading()) return;
    // Only upgrade vision, never downgrade below base
    if (vision > currentVision_) {
        currentVision_ = vision;
        LOG_INFO(MOD_GRAPHICS, "Vision upgraded to: {}",
                 currentVision_ == VisionType::Ultravision ? "Ultravision" :
                 currentVision_ == VisionType::Infravision ? "Infravision" : "Normal");
        // Zone light colors updated by SimulationWorker via visionType in SimulationInput
    }
}

void IrrlichtRenderer::resetVisionToBase() {
    if (isLoading()) return;
    if (currentVision_ != baseVision_) {
        currentVision_ = baseVision_;
        LOG_INFO(MOD_GRAPHICS, "Vision reset to base: {}",
                 currentVision_ == VisionType::Ultravision ? "Ultravision" :
                 currentVision_ == VisionType::Infravision ? "Infravision" : "Normal");
        // Zone light colors updated by SimulationWorker via visionType in SimulationInput
    }
}

// updateZoneLightColors() removed — vision/weather modifiers now applied in SimulationWorker

void IrrlichtRenderer::updateObjectLightColors(float deltaTime) {
    if (objectLights_.empty()) {
        return;
    }

    // Get weather modifier (darker during storms)
    float weatherMod = 1.0f;
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        weatherMod = weatherEffects_->getAmbientLightModifier();
    }

    // Update all object light colors
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            // Apply weather modifier to original color
            float r = objLight.originalColor.r * weatherMod;
            float g = objLight.originalColor.g * weatherMod;
            float b = objLight.originalColor.b * weatherMod;

            // Fire flickering: modulate intensity with layered sine waves
            if (objLight.isFireSource && fireEffectsEnabled_) {
                objLight.flickerPhase += objLight.flickerSpeed * deltaTime;
                float flicker = 0.70f + 0.10f * std::sin(objLight.flickerPhase * 6.7f)
                                      + 0.05f * std::sin(objLight.flickerPhase * 13.1f);
                r *= flicker;
                g *= flicker;
                b *= flicker;
            }

            irr::video::SLight& lightData = objLight.node->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r, g, b, 1.0f);
        }
    }

}

void IrrlichtRenderer::setPlayerPosition(float x, float y, float z, float heading) {
    if (isLoading()) return;
    playerX_ = x;
    playerY_ = y;
    playerZ_ = z;
    playerHeading_ = heading;

    LOG_DEBUG(MOD_GRAPHICS, "[ZONE-IN] setPlayerPosition: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} (EQ units, EQ heading 0-512)",
             x, y, z, heading);

    // Trust the server's Z position - the server places entities at a consistent height
    // that accounts for model placement. Ground-snapping was incorrectly overriding
    // the server Z (e.g., snapping from 3.75 to 0) causing player/NPC Z mismatch.

    // Check if player is within zone geometry bounds before following
    // Uses cached bounds — geometry vectors may have been released after zone load
    bool playerInBounds = true;
    if (zoneBoundsValid_) {
        float margin = 500.0f;
        if (x < zoneBoundsMinX_ - margin || x > zoneBoundsMaxX_ + margin ||
            y < zoneBoundsMinY_ - margin || y > zoneBoundsMaxY_ + margin) {
            playerInBounds = false;
        }
    }

    if (cameraMode_ == CameraMode::Follow && playerInBounds) {
        LOG_DEBUG(MOD_GRAPHICS, "[ZONE-IN] Camera mode=Follow, calling setFollowPosition({:.2f},{:.2f},{:.2f},{:.2f})",
                 x, y, z, heading);
        cameraController_->enableZoneInLogging();  // Enable one-time detailed logging
        cameraController_->setFollowPosition(x, y, z, heading);
    } else if (cameraMode_ == CameraMode::FirstPerson && camera_ && playerInBounds) {
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
    } else {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera: mode={} playerInBounds={} camera={} (no camera update)",
                 static_cast<int>(cameraMode_), playerInBounds, camera_ != nullptr);
    }

    // Force visibility recalculation on the next frame.
    forcePvsUpdate_ = true;
}

void IrrlichtRenderer::setSwimmingState(bool swimming, float swimSpeed, bool levitating) {
    if (isLoading()) return;
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

void IrrlichtRenderer::setCameraMode(CameraMode mode) {
    if (isLoading()) return;
    cameraMode_ = mode;

    // T300: the player model draws normally in both modes; this only
    // records the mode and hides the floating name tag in first person.
    if (entityRenderer_) {
        entityRenderer_->setPlayerFirstPersonMode(cameraMode_ == CameraMode::FirstPerson);
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
}

void IrrlichtRenderer::cycleCameraMode() {
    switch (cameraMode_) {
        case CameraMode::Free:
            cameraMode_ = CameraMode::Follow;
            break;
        case CameraMode::Follow:
            cameraMode_ = CameraMode::FirstPerson;
            break;
        case CameraMode::FirstPerson:
            cameraMode_ = CameraMode::Free;
            break;
    }

    // T300: see setCameraMode() -- mode record + name tag only.
    if (entityRenderer_) {
        entityRenderer_->setPlayerFirstPersonMode(cameraMode_ == CameraMode::FirstPerson);
    }
    applyCameraProjectionForMode();

    // Log eye height when entering first-person mode
    if (cameraMode_ == CameraMode::FirstPerson) {
        LOG_INFO(MOD_GRAPHICS, "First Person mode - Eye height: {:.1f} (Y to raise, Shift+Y to lower)", playerConfig_.eyeHeight);
        // T74: see setCameraMode()'s comment -- same stale-camera gap.
        repositionFirstPersonCamera();
    }
}

std::string IrrlichtRenderer::getCameraModeString() const {
    switch (cameraMode_) {
        case CameraMode::Free: return "Free";
        case CameraMode::Follow: return "Follow";
        case CameraMode::FirstPerson: return "First Person";
        default: return "Unknown";
    }
}

void IrrlichtRenderer::getCameraTransform(float& posX, float& posY, float& posZ,
                                           float& forwardX, float& forwardY, float& forwardZ,
                                           float& upX, float& upY, float& upZ) const {
    if (!camera_) {
        // Default values if camera not available
        posX = posY = posZ = 0.0f;
        forwardX = 0.0f; forwardY = 0.0f; forwardZ = -1.0f;
        upX = 0.0f; upY = 1.0f; upZ = 0.0f;
        return;
    }

    // Get camera position (already in EQ coordinates: x, y, z where z is up)
    irr::core::vector3df pos = camera_->getPosition();
    posX = pos.X;
    posY = pos.Y;
    posZ = pos.Z;

    // Get camera target to compute forward direction
    irr::core::vector3df target = camera_->getTarget();
    irr::core::vector3df forward = target - pos;
    forward.normalize();
    forwardX = forward.X;
    forwardY = forward.Y;
    forwardZ = forward.Z;

    // Get camera up vector
    irr::core::vector3df up = camera_->getUpVector();
    upX = up.X;
    upY = up.Y;
    upZ = up.Z;
}


} // namespace Graphics
} // namespace EQT
