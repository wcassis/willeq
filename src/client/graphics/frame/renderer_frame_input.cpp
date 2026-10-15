// renderer_frame_input.cpp - IrrlichtRenderer frame phases 1-3.
//
// Phase 1 input (processFrameInput, processCommonInput, processPlayerInput,
// processInputDeltas, processChatInput), Phase 2 visibility
// (processFrameVisibility), Phase 3 simulation (processFrameSimulation).
// The orchestrator processFrame() stays in renderer_frame.cpp; Phase 4 is in
// renderer_frame_render.cpp.
//
// WARNING: tests/test_static_ui_scroll.cpp reads THIS FILE as text. It counts
// the reads of the event receiver's mouse-wheel accumulator and requires
// exactly two, and it requires that the static UI input object's wheel and
// scroll-key routing calls both appear here. Moving the wheel accumulator or
// either routing call out of this TU fails those assertions with a message
// that wrongly reads as a behaviour change. The literal tokens are spelled
// out in that test, deliberately not here: a comment naming them would
// satisfy the guard on its own.
// Split per refactor/J31_renderer_frame_split.md (move-only).
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/jump_fall_physics.h"  // B893: the shared fall-damage table
#include "client/audio/volume_steps.h"  // T439: the oracle's 0..20 volume step
#include "common/logging.h"
#include "common/performance_metrics.h"
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#ifdef __linux__
#include <unistd.h>  // sysconf, _SC_PAGESIZE (H01: missing on x86 EQT_GLES2 builds)
#endif

#ifdef EQT_HAS_GLES2
// Declared in irrlicht_renderer.cpp (H01: hit when building EQT_GLES2 on x86;
// pre-existing gap from the Batch F file split).
extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
#endif

namespace EQT {
namespace Graphics {
// ===== Phase 1: Input =====
void FrameComponent::processFrameInput(float deltaTime) {
    // chatInputFocused_ is managed by processChatInput() — don't reset it here

    // Drain action queue and dispatch
    auto actions = ctx_.eventReceiver->drainActions();
    if (!actions.empty()) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processFrameInput: chatFocused={}, drained {} actions from actionQueue, bridgeQueue size={}",
            chatInputFocused_, actions.size(), ctx_.eventReceiver->getBridgeQueueSize());
    }
    processCommonInput(actions);
    processPlayerInput(actions);

    // ===== FRAME TIMING: Input Handling =====
    frameTimings_.inputHandling = measureSection();

    processInputDeltas(deltaTime);

    // UIGAP-residue (#151, measured): an unanswered drop / destroy prompt
    // closes itself (20 s / 10 s) as a No -- nothing sent, item kept.
    if (staticUIInput_ &&
        tickConfirmationDialog(staticUIInput_->confirmationDialog(), deltaTime)) {
        staticUIInput_->confirmationDialog().close();
    }

    // ===== FRAME TIMING: Input Handling (accumulated) =====
    frameTimings_.inputHandling += measureSection();

    // Handle mouse state BEFORE camera/movement updates
    bool hadClick = ctx_.eventReceiver->wasLeftButtonClicked();
    bool hadRelease = ctx_.eventReceiver->wasLeftButtonReleased();
    int clickX = ctx_.eventReceiver->getClickMouseX();
    int clickY = ctx_.eventReceiver->getClickMouseY();
    pendingUIClick_ = hadClick;  // Preserved for UI input processing in render phase
    pendingUIRelease_ = hadRelease;  // B442b: the motionless world click's release

    // Update chat panel mouse hover state
    {
        int mx = ctx_.eventReceiver->getMouseX();
        int my = ctx_.eventReceiver->getMouseY();
        chatPanelState_.mouseHover = uiLayout_.chatPanel.isPointInside(
            irr::core::position2d<irr::s32>(mx, my));

        // T313: the mouse wheel, routed by cursor position to whichever
        // static-UI panel is under it. Read-and-reset, once per frame, one
        // consumer. T461: a notch the UI does NOT consume (the cursor is over
        // no scrollable panel) zooms the camera -- the official client's
        // Mouse Wheel Zoom option (eqclient.ini `MouseWheelZoom=1`,
        // EQUI_OptionsWindow.xml `OMP_MouseWheelZoomCheckbox`). It is still
        // one read, so a UI-consumed notch can never also zoom. Irrlicht's
        // Wheel is +1 per notch away from the user: that zooms IN.
        float wheel = 0.0f;
        bool wheelConsumedByUI = false;
        if (newUIEnabled_ && staticUIInput_) {
            wheel = ctx_.eventReceiver->getMouseWheelDelta();
            // B895: the Tracking Window's list is asked first (it draws above).
            wheelConsumedByUI = scrollTipWindow(tipState_, uiLayout_, mx, my, wheel) ||  // B834
                scrollTrackingList(trackingState_, uiLayout_, mx, my, wheel) ||
                staticUIInput_->processWheel(mx, my, wheel,
                uiLayout_, chatPanelState_, skillsPopupState_, lootState_,
                itemDisplayState_);
        } else {
            wheel = ctx_.eventReceiver->getMouseWheelDelta();  // drain, never accumulate
        }
        // B433: OMP_MouseWheelZoomCheckbox gates the zoom half.
        if (wheel != 0.0f && !wheelConsumedByUI && !chatInputFocused_ &&
            ctx_.PlayerCamera().mouseOptions().wheelZoom) {
            ctx_.PlayerCamera().zoomCamera(-wheel * kCameraZoomStepUnits, deltaTime);
        }
    }

    // B1066 (#386): with no static UI to hit-test (the legacy GUI), nothing
    // in renderStaticUI() settles a new press, so every press is the
    // camera's -- the pre-B1066 behaviour on that path.
    if (!(newUIEnabled_ && staticUIInput_)) ctx_.eventReceiver->resolvePendingPresses(false);

    // Update camera and player movement
    ctx_.PlayerCamera().updatePlayerMovement(deltaTime);

    // B893: fall damage, detected HERE, straddling the movement phase. The
    // renderer is the only thing that can see a fall: it is the one that
    // integrates gravity (isJumping, set on Space in readMovementInput) and
    // snaps the player to the ground on landing, and the headless game thread
    // -- the only one allowed to send packets -- has no per-frame integrator,
    // so it cannot. This straddle (unpoliced: the movement-order golden parses
    // only renderer_player_player_physics.cpp, and the static-ui test parses
    // this TU for the mouse-wheel reads and the wheel-routing calls, which
    // this block does not touch) samples isJumping around the one call:
    //   - rising edge: record the takeoff Z (the player became airborne);
    //   - falling edge: the landing. The drop is takeoffZ - playerZ; only a
    //     drop above the table's safe height pushes a FallDamageIntent, which
    //     the game thread turns into OP_EnvDamage (dmgtype 252). A normal
    //     standing jump lands back at its takeoff -> 0 -> nothing is sent.
    // Teleport protection: a horizontal relocation (zoning, a door, /go)
    // between the frames breaks the fall, so it deals nothing instead of
    // "falling" from the last takeoff.
    {
        const bool airborneNow =
            ctx_.PlayerCamera().isJumping() && !ctx_.PlayerCamera().isSwimming();
        if (wasAirborne_) {
            const float dx = ctx_.playerX - lastFallTrackX_;
            const float dy = ctx_.playerY - lastFallTrackY_;
            if (dx * dx + dy * dy < 25.0f * 25.0f) {
                if (!airborneNow) {
                    // The landing edge: report the drop.
                    const float drop = fallTakeoffZ_ - ctx_.playerZ;
                    if (drop > eqt::jumpfall::fall().safeHeight && ctx_.bridge) {
                        LOG_INFO(MOD_MOVEMENT, "[Fall] drop {:.1f} from takeoff {:.1f} "
                            "-> OP_EnvDamage ({} damage for the height above the safe threshold)",
                            drop, fallTakeoffZ_,
                            eqt::jumpfall::damageForFallHeight(drop));
                        ctx_.bridge->pushIntent(eqt::events::FallDamageIntent{drop});
                    }
                    wasAirborne_ = false;
                }
            } else {
                // Relocated, not landed.
                wasAirborne_ = false;
            }
        } else if (airborneNow) {
            wasAirborne_ = true;
            fallTakeoffZ_ = ctx_.playerZ;
        }
        lastFallTrackX_ = ctx_.playerX;
        lastFallTrackY_ = ctx_.playerY;
    }
    // T461: a rig camera (Overhead, Chase since T474, User Defined 1/2, Tether)
    // is re-placed every frame around wherever the frame leaves the player --
    // ONCE, in processFrameSimulation() after the lift carry, the last thing
    // that moves the player (T486, item 6: it was placed here too, so every
    // frame paid two BSP lookups and a PVS ray for one camera). A no-op in
    // first person, which the movement path places itself.
    frameTimings_.playerMovement = measureSection();
    // Update sky position to follow camera
    if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized() && ctx_.camera) {
        ctx_.skyRenderer->setCameraPosition(ctx_.camera->getPosition());
    }

    // ===== FRAME TIMING: Camera Update =====
    frameTimings_.cameraUpdate = measureSection();

    // B834: tips pop on the rising edge of their window (before the ESC
    // stack looks, so a tip opened this frame is the newest window).
    updateTipWindows();
    // B460: the ESC stack learns every window's open ORDER, whoever opened it.
    observeEscWindows();
    processChatInput();

    // Handle mouse targeting
    if (hadClick) {
        ctx_.Overlays().handleMouseTargeting(clickX, clickY);
    }
}

void FrameComponent::processCommonInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        switch (event.action) {
            case RA::Screenshot: pendingScreenshot_ = true; break;
            case RA::ToggleAllUI: ctx_.Diagnostics().toggleAllUI(); break;
            // T313: PageUp/PageDown. Same router as the wheel; a whole page
            // per press. Global-mode actions, so they work with a popup up.
            case RA::ScrollUIUp:
            case RA::ScrollUIDown:
                if (newUIEnabled_ && staticUIInput_ && ctx_.eventReceiver) {
                    staticUIInput_->processScrollKey(
                        event.action == RA::ScrollUIUp,
                        ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
                        uiLayout_, chatPanelState_, skillsPopupState_, lootState_,
                        itemDisplayState_);
                }
                break;
            case RA::ToggleZoneLights: ctx_.Diagnostics().toggleZoneLights(); break;
            case RA::CycleObjectLights: ctx_.Diagnostics().cycleObjectLights(); break;
            // T330 (B043): these four arms had never existed. The hotkeys were
            // bound (hotkey_manager.cpp) and the receiver queued the actions
            // (renderer_event_receiver.cpp), but they fell through the default
            // below and did nothing. Pinned by tests/test_debug_hotkey_arms.cpp.
            case RA::ToggleWireframe: ctx_.Diagnostics().toggleWireframe(); break;
            case RA::ToggleHUD: ctx_.Diagnostics().toggleHUD(); break;
            case RA::ToggleNameTags: ctx_.Diagnostics().toggleNameTags(); break;
            case RA::ToggleOldModels: ctx_.Diagnostics().toggleOldModels(); break;
            // T343 (B002): autorun. The hotkeys were bound and the receiver
            // queued the action, but it went to the game thread and died in a
            // LOG_DEBUG stub; the renderer flag the movement phase reads had
            // no writer at all. Global-mode arm like the T330 four: the
            // receiver already swallows non-function keys while chat is
            // focused, so no extra guard here. Pinned by
            // tests/test_autorun_arm.cpp.
            case RA::ToggleAutorun:
                ctx_.PlayerCamera().toggleAutorun();
                LOG_INFO(MOD_GRAPHICS, "Autorun {}", ctx_.PlayerCamera().autorun() ? "ON" : "OFF");
                break;
            // T461 (F9): the camera-mode key. Until T461 its key (Ctrl+F5)
            // queued this action and there was NO arm, so every press fell
            // through the default below -- the T330 shape again. Pinned by
            // tests/test_camera_mode_wiring.cpp.
            case RA::ToggleCameraMode: ctx_.PlayerCamera().cycleCameraMode(); break;
            // T471: the official camera adjust keys, MEASURED by T466 (board
            // B531). CameraOrbitLeft/Right are the official "Tether-slide
            // camera left/right" and slide the TETHER camera. Pitch up/down
            // pitch the active USER camera; Pitch UP tilts the view up, i.e.
            // lowers the camera (rig pitch is + above the look-at point) --
            // the oracle's User 1 swung up to the sky, and this sign already
            // matched it. Raise/Lower move the Tether camera's look-at height.
            case RA::CameraOrbitLeft:
                ctx_.PlayerCamera().tetherSlideCamera(-kCameraOrbitKeyStepDeg);
                break;
            case RA::CameraOrbitRight:
                ctx_.PlayerCamera().tetherSlideCamera(kCameraOrbitKeyStepDeg);
                break;
            case RA::CameraPitchUp:
                ctx_.PlayerCamera().orbitCamera(0.0f, -kCameraOrbitKeyStepDeg);
                break;
            case RA::CameraPitchDown:
                ctx_.PlayerCamera().orbitCamera(0.0f, kCameraOrbitKeyStepDeg);
                break;
            case RA::CameraRaise:
                ctx_.PlayerCamera().raiseCamera(kCameraRaiseKeyStepUnits);
                break;
            case RA::CameraLower:
                ctx_.PlayerCamera().raiseCamera(-kCameraRaiseKeyStepUnits);
                break;
            // T434: was a bare `break;` with the comment "WindowManager
            // removed -- options window not available" -- the T368 dead-arm
            // shape on the window that is the home of every user-changeable
            // setting. The key is Alt+O, measured at the oracle's
            // Options > Keys > UI page (T377).
            case RA::ToggleOptions: toggleOptionsWindow(); break;
            case RA::ToggleFriends: toggleFriendsWindow(); break;  // B897
            case RA::ClearTarget:
                if (ctx_.Overlays().currentTargetId() != 0) {
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", ctx_.Overlays().currentTargetName());
                    ctx_.Overlays().clearCurrentTarget();
                    SetTrackedTargetId(0);
                }
                break;
            default: break;
        }
    }
}

// T363 (B058): THE skills-window toggle. See the doc comment in
// frame_component.h. The mutual exclusion (one centre popup at a time) is the
// same rule /newbook and the LootWindowOpened bridge arm apply, and it is
// guarded on the OPEN branch only -- closing the skills window must not close
// anything else.
bool FrameComponent::toggleSkillsWindow() {
    skillsPopupState_.isOpen = !skillsPopupState_.isOpen;
    if (skillsPopupState_.isOpen) {
        inventoryState_.activePopup = PopupType::None;
        spellbookState_.isOpen = false;
    }
    LOG_INFO(MOD_GRAPHICS, "Skills window {}", skillsPopupState_.isOpen ? "opened" : "closed");
    return skillsPopupState_.isOpen;
}

// T368 (B101): the three toggles that replace empty arms. Deliberately NOT
// mutually exclusive with the centre popups the way toggleSkillsWindow() is --
// these are HUD panels, not centre popups, and the official client keeps them
// open alongside everything else.
bool FrameComponent::toggleGroupWindow() {
    groupPanelState_.isOpen = !groupPanelState_.isOpen;
    LOG_INFO(MOD_GRAPHICS, "Group window {}", groupPanelState_.isOpen ? "opened" : "closed");
    return groupPanelState_.isOpen;
}

bool FrameComponent::togglePetWindow() {
    petPanelState_.isOpen = !petPanelState_.isOpen;
    LOG_INFO(MOD_GRAPHICS, "Pet window {}", petPanelState_.isOpen ? "opened" : "closed");
    return petPanelState_.isOpen;
}

bool FrameComponent::toggleBuffWindow() {
    buffBarState_.isOpen = !buffBarState_.isOpen;
    LOG_INFO(MOD_GRAPHICS, "Buff window {}", buffBarState_.isOpen ? "opened" : "closed");
    return buffBarState_.isOpen;
}

// T415 (B320): the combat abilities window. A HUD panel like the three
// above, so it is deliberately NOT mutually exclusive with the centre
// popups. Alt+C is the oracle's own key -- MEASURED at Options > Keys > UI
// (review/T377/oracle/T377_oracle_options_keys_UI_page3.png).
bool FrameComponent::toggleCombatAbilityWindow() {
    combatAbilityState_.isOpen = !combatAbilityState_.isOpen;
    LOG_INFO(MOD_GRAPHICS, "Combat abilities window {}",
        combatAbilityState_.isOpen ? "opened" : "closed");
    return combatAbilityState_.isOpen;
}

void FrameComponent::processPlayerInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        // Note: Targeting, combat (autorun, autoattack, hail, consider), and clear target
        // actions are now routed through bridgeQueue_ → GraphicsInputHandler → InputActionBridge.
        // They no longer appear in actionQueue_, so only renderer-local actions remain here.
        if (chatInputFocused_) {
            // No renderer-local actions work when chat is focused
            continue;
        }
        switch (event.action) {
            case RA::ToggleVendor:
                if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::VendorToggleIntent{});
                break;
            case RA::ToggleTrainer:
                if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::TrainerToggleIntent{});
                break;
            case RA::DoorInteract:
                if (ctx_.doorManager) {
                    uint8_t doorId = ctx_.doorManager->getNearestDoor(ctx_.playerX, ctx_.playerY, ctx_.playerZ, ctx_.playerHeading);
                    if (doorId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "Door interaction: ID {}", doorId);
                        if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::DoorInteractIntent{doorId});
                    }
                }
                break;
            case RA::WorldObjectInteract: {
                    uint32_t objectId = owner_.getNearestWorldObject(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
                    if (objectId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "World object interaction: dropId {}", objectId);
                        if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::WorldObjectInteractIntent{objectId});
                    }
                }
                break;

            // Collision
            case RA::ToggleCollision:
                ctx_.PlayerCamera().playerConfig().collisionEnabled = !ctx_.PlayerCamera().playerConfig().collisionEnabled;
                LOG_INFO(MOD_GRAPHICS, "Collision: {}", (ctx_.PlayerCamera().playerConfig().collisionEnabled ? "ENABLED" : "DISABLED"));
                break;
            case RA::ToggleCollisionDebug:
                ctx_.PlayerCamera().playerConfig().collisionDebug = !ctx_.PlayerCamera().playerConfig().collisionDebug;
                LOG_INFO(MOD_GRAPHICS, "Collision Debug: {}", (ctx_.PlayerCamera().playerConfig().collisionDebug ? "ON" : "OFF"));
                if (ctx_.PlayerCamera().playerConfig().collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "  Collision Height: {}", ctx_.PlayerCamera().playerConfig().collisionCheckHeight);
                    LOG_INFO(MOD_GRAPHICS, "  Step Height: {}", ctx_.PlayerCamera().playerConfig().collisionStepHeight);
                }
                break;

            // UI Windows
            case RA::ToggleInventory: owner_.toggleInventory(); break;
            // T368 (B101): real toggles -- these three were empty arms.
            case RA::ToggleGroup: toggleGroupWindow(); break;
            // T363 (B058): one body, shared with /skills.
            case RA::ToggleSkills: toggleSkillsWindow(); break;
            case RA::TogglePet: togglePetWindow(); break;
            case RA::ToggleSpellbook:
                spellbookState_.isOpen = !spellbookState_.isOpen;
                LOG_INFO(MOD_GRAPHICS, "Spellbook {}", spellbookState_.isOpen ? "opened" : "closed");
                break;
            case RA::ToggleBuffWindow: toggleBuffWindow(); break;
            // T415 (B320): Alt+C, the oracle's own key.
            case RA::ToggleCombatAbilities: toggleCombatAbilityWindow(); break;

            // Debug overlays
            case RA::ToggleZoneLineVisualization: ctx_.Overlays().toggleZoneLineVisualization(); break;
            case RA::ToggleMapOverlay: ctx_.Overlays().toggleMapOverlay(); break;
            case RA::RotateMapOverlay:
                ctx_.Overlays().mapOverlayRotation() = (ctx_.Overlays().mapOverlayRotation() + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable rotation: {}° around Y axis (terrain unchanged)", ctx_.Overlays().mapOverlayRotation() * 90);
                break;
            case RA::MirrorXMapOverlay:
                ctx_.Overlays().mapOverlayMirrorX() = !ctx_.Overlays().mapOverlayMirrorX();
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable X mirror: {} (terrain unchanged)", ctx_.Overlays().mapOverlayMirrorX() ? "ON" : "OFF");
                break;
            case RA::ToggleNavmeshOverlay: ctx_.Overlays().toggleNavmeshOverlay(); break;
            case RA::RotateNavmeshOverlay:
                ctx_.Overlays().navmeshOverlayRotation() = (ctx_.Overlays().navmeshOverlayRotation() + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay rotation: {}° around Y axis", ctx_.Overlays().navmeshOverlayRotation() * 90);
                break;
            case RA::MirrorXNavmeshOverlay:
                ctx_.Overlays().navmeshOverlayMirrorX() = !ctx_.Overlays().navmeshOverlayMirrorX();
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay X mirror: {}", ctx_.Overlays().navmeshOverlayMirrorX() ? "ON" : "OFF");
                break;

            case RA::ToggleFrustumCulling:
                if (ctx_.frustumCuller) {
                    // Cycle: ON → ON+DEBUG → OFF → ON ...
                    if (ctx_.frustumCuller->isEnabled() && !frustumDebugDraw_) {
                        frustumDebugDraw_ = true;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON + DEBUG DRAW (Ctrl+V to cycle)");
                    } else if (ctx_.frustumCuller->isEnabled() && frustumDebugDraw_) {
                        ctx_.frustumCuller->setEnabled(false);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: OFF (Ctrl+V to cycle)");
                    } else {
                        ctx_.frustumCuller->setEnabled(true);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON (Ctrl+V to cycle)");
                    }

                    // Diagnostic: log camera direction and nearby region bboxes
                    if (ctx_.camera && ctx_.cameraController) {
                        irr::core::vector3df irrFwd = (ctx_.camera->getTarget() - ctx_.camera->getPosition());
                        float camX, camY, camZ;
                        ctx_.cameraController->getPositionEQ(camX, camY, camZ);
                        float eqFwdX = irrFwd.X, eqFwdY = irrFwd.Z, eqFwdZ = irrFwd.Y;
                        float fwdLen = std::sqrt(eqFwdX*eqFwdX + eqFwdY*eqFwdY + eqFwdZ*eqFwdZ);
                        if (fwdLen > 0.001f) { eqFwdX /= fwdLen; eqFwdY /= fwdLen; eqFwdZ /= fwdLen; }
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: cam EQ=({:.1f},{:.1f},{:.1f}) fwd EQ=({:.3f},{:.3f},{:.3f})",
                            camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ);
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: Irrlicht cam=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f})",
                            ctx_.camera->getPosition().X, ctx_.camera->getPosition().Y, ctx_.camera->getPosition().Z,
                            ctx_.camera->getTarget().X, ctx_.camera->getTarget().Y, ctx_.camera->getTarget().Z);

                        // Log 10 closest region bboxes with sizes and frustum results
                        struct RegionDiag { size_t idx; float dist; float sizeX, sizeY, sizeZ; bool frustum; };
                        std::vector<RegionDiag> diags;
                        for (auto& [regionIdx, bbox] : ctx_.regionBoundingBoxes) {
                            float cx = (bbox.MinEdge.X + bbox.MaxEdge.X) * 0.5f;
                            float cy = (bbox.MinEdge.Y + bbox.MaxEdge.Y) * 0.5f;
                            float cz = (bbox.MinEdge.Z + bbox.MaxEdge.Z) * 0.5f;
                            float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            bool inFrustum = !ctx_.frustumCuller->isEnabled() || ctx_.frustumCuller->testAABB(
                                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
                            diags.push_back({regionIdx, dist,
                                bbox.MaxEdge.X - bbox.MinEdge.X,
                                bbox.MaxEdge.Y - bbox.MinEdge.Y,
                                bbox.MaxEdge.Z - bbox.MinEdge.Z, inFrustum});
                        }
                        std::sort(diags.begin(), diags.end(), [](const RegionDiag& a, const RegionDiag& b) { return a.dist < b.dist; });
                        for (int i = 0; i < 15 && i < (int)diags.size(); ++i) {
                            auto& d = diags[i];
                            auto& bbox = ctx_.regionBoundingBoxes[d.idx];
                            LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: region {} dist={:.0f} size=({:.1f},{:.1f},{:.1f}) "
                                "bbox=({:.1f},{:.1f},{:.1f})->({:.1f},{:.1f},{:.1f}) {}",
                                d.idx, d.dist, d.sizeX, d.sizeY, d.sizeZ,
                                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z,
                                d.frustum ? "VISIBLE" : "CULLED");
                        }
                    }

                    // Log occlusion culler stats if available
                    if (ctx_.occlusionCuller && ctx_.occlusionCuller->isEnabled()) {
                        const auto& occStats = ctx_.occlusionCuller->getStats();
                        int fillPct = occStats.depthBufferTotalPixels > 0
                            ? (occStats.depthBufferFilledPixels * 100 / occStats.depthBufferTotalPixels) : 0;
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: {} rasterized ({} tris, {} clipped), buf {}/{} px ({}%), "
                            "{} tested, {} culled [reject: {} behind, {} offscreen, {} too-large, {} low-coverage]",
                            occStats.regionsRasterized, occStats.trianglesRasterized, occStats.trianglesClipped,
                            occStats.depthBufferFilledPixels, occStats.depthBufferTotalPixels, fillPct,
                            occStats.regionsTested, occStats.regionsCulled,
                            occStats.rejectedBehindCamera, occStats.rejectedOffScreen,
                            occStats.rejectedScreenTooLarge, occStats.rejectedLowCoverage);
                        ctx_.occlusionCuller->dumpDepthBufferPGM("occlusion_depth.pgm");
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: depth buffer dumped to occlusion_depth.pgm");

                        // Detailed per-region occluder diagnostic (use camera position)
                        float occDiagX = ctx_.playerX, occDiagY = ctx_.playerY, occDiagZ = ctx_.playerZ;
                        if (ctx_.cameraController) {
                            ctx_.cameraController->getPositionEQ(occDiagX, occDiagY, occDiagZ);
                        }
                        struct OccDiag { size_t regionIdx; float dist; size_t triCount; };
                        std::vector<OccDiag> occDiags;
                        for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
                            if (!node || !node->isVisible()) continue;
                            auto bboxIt = ctx_.regionBoundingBoxes.find(regionIdx);
                            if (bboxIt == ctx_.regionBoundingBoxes.end()) continue;
                            const auto& bbox = bboxIt->second;
                            float closestX = std::max(bbox.MinEdge.X, std::min(occDiagX, bbox.MaxEdge.X));
                            float closestY = std::max(bbox.MinEdge.Y, std::min(occDiagY, bbox.MaxEdge.Y));
                            float closestZ = std::max(bbox.MinEdge.Z, std::min(occDiagZ, bbox.MaxEdge.Z));
                            float ddx = occDiagX - closestX, ddy = occDiagY - closestY, ddz = occDiagZ - closestZ;
                            float dist = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);
                            const auto& occ = ctx_.occlusionCuller->getRegionOccluders(regionIdx);
                            occDiags.push_back({regionIdx, dist, occ.size()});
                        }
                        std::sort(occDiags.begin(), occDiags.end(),
                            [](const OccDiag& a, const OccDiag& b) { return a.dist < b.dist; });
                        LOG_INFO(MOD_GRAPHICS, "OCCL REGIONS (closest 20 visible):");
                        for (int i = 0; i < 20 && i < (int)occDiags.size(); ++i) {
                            auto& d = occDiags[i];
                            const auto& occ = ctx_.occlusionCuller->getRegionOccluders(d.regionIdx);
                            if (occ.empty()) {
                                LOG_INFO(MOD_GRAPHICS, "  region {} dist={:.1f} NO occluders", d.regionIdx, d.dist);
                            } else {
                                LOG_INFO(MOD_GRAPHICS, "  region {} dist={:.1f} {} occluder tris:", d.regionIdx, d.dist, occ.size());
                                for (size_t t = 0; t < occ.size() && t < 4; ++t) {
                                    LOG_INFO(MOD_GRAPHICS, "    tri{}: ({:.1f},{:.1f},{:.1f}) ({:.1f},{:.1f},{:.1f}) ({:.1f},{:.1f},{:.1f}) area={:.1f}",
                                        t, occ[t].v0[0], occ[t].v0[1], occ[t].v0[2],
                                        occ[t].v1[0], occ[t].v1[1], occ[t].v1[2],
                                        occ[t].v2[0], occ[t].v2[1], occ[t].v2[2], occ[t].area);
                                }
                            }
                        }
                    }

                    // T412 (board B294): no flag -- SimulationWorker rebuilds
                    // visibility on the next frame regardless.
                }
                break;

            default: break;
        }
    }
}

void FrameComponent::processInputDeltas(float deltaTime) {
    // Camera zoom (+/- keys). T461: what one step means is the camera's
    // business -- the Chase follow distance or the active rig's distance --
    // so PlayerCameraComponent::zoomCamera() owns it and the mouse wheel
    // (processFrameInput) reaches the same body.
    if (!chatInputFocused_) {
        float zoomDelta = ctx_.eventReceiver->getCameraZoomDelta();
        if (zoomDelta != 0.0f) {
            LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processInputDeltas: zoomDelta={}, cameraController={}, cameraMode={}",
                zoomDelta, (ctx_.cameraController != nullptr),
                ctx_.PlayerCamera().getCameraModeString());
            ctx_.PlayerCamera().zoomCamera(zoomDelta, deltaTime);
        }
    }

    // D20c2: Push volume hotkey changes as intents via bridge.
    // T439 (board B432): the intents are ABSOLUTE now, so a hotkey adds its
    // step delta to the step the audio manager last PUBLISHED (optionsState_,
    // written by applyVolumesChanged) rather than to a private accumulator.
    // The Options slider and the hotkeys therefore move the same number, and
    // the slider follows a hotkey press without being told separately.
    if (ctx_.bridge) {
        const int musicDelta = ctx_.eventReceiver->getMusicVolumeStepDelta();
        if (musicDelta != 0) {
            const int step = eqt::audio::clampVolumeStep(
                optionsState_.musicVolume + musicDelta);
            optionsState_.musicVolume = step;
            ctx_.bridge->pushIntent(eqt::events::MusicVolumeChangeIntent{step});
        }
        const int effectsDelta = ctx_.eventReceiver->getEffectsVolumeStepDelta();
        if (effectsDelta != 0) {
            const int step = eqt::audio::clampVolumeStep(
                optionsState_.soundVolume + effectsDelta);
            optionsState_.soundVolume = step;
            ctx_.bridge->pushIntent(eqt::events::EffectsVolumeChangeIntent{step});
        }
    }
}

void FrameComponent::processChatInput() {
    // T235: the spell-gem key request (Alt+1..8) is NOT consumed here any
    // more either. This used to call getSpellGemCastRequest() -- an
    // exchange(-1) -- every frame and hand the value to
    // spellGemCastCallback_, which nothing ever set (Application::
    // connectRendererCallbacks() only knows the abstract
    // output::GraphicalRenderer, which IrrlichtRenderer does not derive
    // from; the BEFORE log in review/T235/ has "action=SpellGem1" and
    // nothing after it). Same
    // two-consumer race as the hotbar key below: the render thread polled
    // first and every gem key vanished before the game thread's
    // InputActionBridge::processSpellCasts() could see it. The one
    // consumer is now GraphicsInputHandler::consumeSpellCastRequest()
    // (src/client/input/graphics_input_handler.cpp); the dead callback
    // plumbing is gone. See refactor/T235_spell_gem_key_race.md.

    // T230: the hotbar-key request is NOT consumed here any more. This used
    // to call getHotbarActivationRequest() -- an exchange(-1) -- and discard
    // the result ("WindowManager removed"), and because the render thread
    // reaches this before the game thread's InputActionBridge::update()
    // polls GraphicsInputHandler::consumeHotbarRequest(), every keyboard
    // hotbar press (Spell, Skill AND Item -- the whole T174 route) was
    // swallowed live. Found by T230's live leg: "Key pressed: 1 ...
    // action=HotbarSlot1" logged, nothing ever reached the bridge. The one
    // consumer is now GraphicsInputHandler (src/client/input/
    // graphics_input_handler.cpp). See refactor/T230_target_resolver_and_
    // hotbar_items.md, Live leg.

    // T319: a game-thread action can ask for the chat input to open
    // prefilled. Today the one producer is the R key (ReplyToTell):
    // OnEvent -> bridgeQueue_ -> GraphicsInputHandler -> InputActionBridge
    // -> ActionDispatcher::openReplyToTell() -> the action callback
    // Application installs -> IrrlichtRenderer::handleActionResult() ->
    // requestChatPrefill(). This is the render-thread end of that trip and
    // the only reader of the parked text.
    //
    // It runs before the Enter/slash checks and regardless of current
    // focus: dropping the text when chat happens to be focused would lose
    // it silently, which is the whole failure class this unit is closing.
    if (auto prefill = owner_.takeChatPrefill()) {
        chatInputFocused_ = true;
        chatPanelState_.inputActive = true;
        chatPanelState_.inputText = *prefill;
        chatPanelState_.cursorPos = static_cast<int>(prefill->size());
        ctx_.eventReceiver->setChatInputFocused(true);
        ctx_.eventReceiver->clearPendingKeyEvents();
        return;
    }

    // B433: a Keys-page rebinding owns the keyboard until its key arrives.
    if (processOptionsKeyCapture()) {
        return;
    }

    // T421 (board B365): the tradeskill search boxes get the keys first when
    // one of them has the caret. They cannot take the gate themselves --
    // StaticUIInput has no event receiver -- so the ONE keyboard gate lives
    // here for both entries.
    if (processTradeskillSearchInput()) {
        return;
    }
    // B897: the Friends window's FW_NameInput / FW_IgnoreNameInput.
    if (processFriendsNameInput()) {
        return;
    }
    // B442b (#151 row 5): the QuantityWnd's QTYW_SliderInput.
    if (processQuantityKeyInput()) {
        return;
    }

    // Open chat input on Enter or / key
    if (!chatInputFocused_) {
        if (ctx_.eventReceiver->enterKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText.clear();
            chatPanelState_.cursorPos = 0;
            ctx_.eventReceiver->setChatInputFocused(true);
            ctx_.eventReceiver->clearPendingKeyEvents();
            return;
        }
        if (ctx_.eventReceiver->slashKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText = "/";
            chatPanelState_.cursorPos = 1;
            ctx_.eventReceiver->setChatInputFocused(true);
            ctx_.eventReceiver->clearPendingKeyEvents();
            return;
        }
    }

    // Process typed characters when chat is focused.
    //
    // T421 (board B365): the edit itself is applyTextEntryKey() -- the ONE
    // text-entry body, which the tradeskill search boxes use too. What is
    // left here is only chat's POLICY on the four actions it can return:
    // Submit publishes ChatSubmitIntent and drops focus, Cancel drops focus
    // without publishing, Complete is T357's slash completion, Changed needs
    // nothing.
    if (chatInputFocused_) {
        while (ctx_.eventReceiver->hasPendingKeyEvents()) {
            auto ke = ctx_.eventReceiver->popKeyEvent();
            const TextEntryAction action =
                applyTextEntryKey(chatPanelState_, ke.key, ke.character);

            if (action == TextEntryAction::Submit) {
                if (!chatPanelState_.inputText.empty()) {
                    LOG_INFO(MOD_GRAPHICS, "Chat submit: '{}'", chatPanelState_.inputText);
                    if (ctx_.bridge) {
                        ctx_.bridge->pushIntent(eqt::events::ChatSubmitIntent{chatPanelState_.inputText});
                    }
                }
                clearTextEntry(chatPanelState_);
                chatInputFocused_ = false;
                ctx_.eventReceiver->setChatInputFocused(false);
                return;
            }

            if (action == TextEntryAction::Cancel) {
                clearTextEntry(chatPanelState_);
                chatInputFocused_ = false;
                ctx_.eventReceiver->setChatInputFocused(false);
                return;
            }

            if (action == TextEntryAction::Complete) {
                // T357 (B057): complete the slash command under the caret.
                // Tab is already consumed by RendererEventReceiver::OnEvent()
                // while chat is focused (only F1-F12 escape that gate), so it
                // never reaches HotkeyAction::CycleTargets from here and
                // Irrlicht focus cannot move -- there is no IGUIEditBox to
                // move it to.
                applyCommandCompletion();
                continue;
            }
        }
        return;
    }

    // Not in chat — clear pending keys and handle escape for target clearing
    ctx_.eventReceiver->clearPendingKeyEvents();
    if (ctx_.eventReceiver->escapeKeyPressed()) {
        handleEscapeKey();
    }
}

// B460 (#156): the windows ESC can close, in DRAW order (bottom first), for
// the open-order stack. Containers are listed by their own openSequence so
// two opened in one frame keep the order the game thread gave them.
std::vector<EscWindowKey> FrameComponent::openEscWindows() const {
    std::vector<EscWindowKey> open;
    if (inventoryState_.activePopup != PopupType::None) open.push_back({EscWindow::Inventory, 0});
    if (spellbookState_.isOpen) open.push_back({EscWindow::Spellbook, 0});
    if (skillsPopupState_.isOpen) open.push_back({EscWindow::Skills, 0});
    if (merchantState_.isOpen) open.push_back({EscWindow::Merchant, 0});
    if (lootState_.isOpen) open.push_back({EscWindow::Loot, 0});
    if (tradeskillState_.isOpen) open.push_back({EscWindow::Tradeskill, 0});
    if (bankState_.isOpen) open.push_back({EscWindow::Bank, 0});
    if (trainerState_.isOpen) open.push_back({EscWindow::Trainer, 0});
    if (trackingState_.isOpen) open.push_back({EscWindow::Tracking, 0});
    std::vector<int> bags;
    for (int i = 0; i < ContainerWindowsState::MAX_WINDOWS; ++i) {
        if (containerState_.windows[i].isOpen) bags.push_back(i);
    }
    std::sort(bags.begin(), bags.end(), [&](int a, int b) {
        return containerState_.windows[a].openSequence < containerState_.windows[b].openSequence;
    });
    for (int i : bags) open.push_back({EscWindow::Container, i});
    if (itemDisplayState_.isOpen) open.push_back({EscWindow::ItemDisplay, 0});
    if (friendsState_.isOpen) open.push_back({EscWindow::Friends, 0});
    if (optionsState_.isOpen) open.push_back({EscWindow::Options, 0});
    // B834: drawn above Options (review/UIGAP-ORACLE/156: the tip went first).
    if (tipState_.isOpen) open.push_back({EscWindow::Tip, 0});
    return open;
}

void FrameComponent::observeEscWindows() {
    escWindowStack_.observe(openEscWindows());
}

// B460 (#156): ESC closes ONE window per press, the most recently opened
// (MEASURED, review/UIGAP-ORACLE/156/README.md: tip, then bag, then
// inventory, then nothing). Before this it walked a FIXED list (options ->
// friends -> item display -> containers -> tracking -> trainer -> bank ->
// merchant -> the centre popups). Each window still closes the way it always
// did: the pure client views (options, friends, item display, tracking, the
// spellbook/skills/inventory popups) hide in place; the SERVER-owned ones
// push their close intent and stay up until the server's Closed event comes
// back (T290's rule): containers TradeskillCloseIntent (B899), the trainer
// CloseTrainerIntent (B898), the bank CloseBankIntent and the merchant
// VendorToggleIntent (T436), the tradeskill/loot popups through processKey()
// (T443/T290). With no window open ESC clears the target. Whether the
// official client's first press ALSO clears the target while closing a window
// is NOT separated by the oracle run: fable keeps its behaviour (it does not)
// -- an assumption awaiting the oracle.
bool FrameComponent::handleEscapeKey() {
    observeEscWindows();
    const std::optional<EscWindowKey> top = escWindowStack_.newest();
    bool popupConsumedEscape = false;
    if (top) {
        LOG_DEBUG(MOD_GRAPHICS, "[ESC] closing the newest window: {}", escWindowName(top->kind));
        switch (top->kind) {
            case EscWindow::Options:
                optionsState_.isOpen = false;
                popupConsumedEscape = true;
                break;
            case EscWindow::Tip:
                closeTipWindow(tipState_, tipPrefs_);
                popupConsumedEscape = true;
                break;
            case EscWindow::Friends:
                toggleFriendsWindow();
                popupConsumedEscape = true;
                break;
            case EscWindow::ItemDisplay:
                itemDisplayState_.isOpen = false;
                popupConsumedEscape = true;
                break;
            case EscWindow::Container:
                if (newUIEnabled_ && staticUIInput_) {
                    popupConsumedEscape = staticUIInput_->processContainerEscape(containerState_, top->sub);
                }
                break;
            case EscWindow::Tracking:
                trackingState_.isOpen = false;
                trackingState_.openCombo = TrackingCombo::None;
                popupConsumedEscape = true;
                break;
            case EscWindow::Trainer:
                if (trainerState_.isOpen && ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::CloseTrainerIntent{});
                popupConsumedEscape = true;
                break;
            case EscWindow::Bank:
                if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::CloseBankIntent{});
                popupConsumedEscape = true;
                break;
            case EscWindow::Merchant:
                if (ctx_.bridge) ctx_.bridge->pushIntent(eqt::events::VendorToggleIntent{});
                popupConsumedEscape = true;
                break;
            case EscWindow::Tradeskill:
            case EscWindow::Loot:
            case EscWindow::Spellbook:
            case EscWindow::Skills:
            case EscWindow::Inventory:
                if (newUIEnabled_ && staticUIInput_) {
                    popupConsumedEscape = staticUIInput_->processKey(
                        irr::KEY_ESCAPE, /*pressed=*/true, /*shift=*/false, /*ctrl=*/false,
                        inventoryState_, spellbookState_, skillsPopupState_, lootState_,
                        tradeskillState_, top->kind);
                }
                break;
            case EscWindow::Count:
                break;
        }
        // A pure client view closed in place: forget it now, so a second
        // press in the same frame reaches the next one.
        observeEscWindows();
    }
    if (!popupConsumedEscape && ctx_.Overlays().currentTargetId() != 0) {
        LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", ctx_.Overlays().currentTargetName());
        ctx_.Overlays().clearCurrentTarget();
        SetTrackedTargetId(0);
        if (ctx_.eventReceiver) ctx_.eventReceiver->pushBridgeAction({RendererAction::ClearTarget});
    }
    return popupConsumedEscape;
}

// T357 (B057): the Tab arm of processChatInput()'s key drain.
//
// Before this unit BOTH CommandProcessor::getCompletions() and
// CommandRegistry::getCompletions() were implemented, tested and called by
// nothing in production, while both CLAUDE.md files advertised tab
// completion. This is the caller. The vocabulary comes from the provider
// Application installs (see FrameComponent::setCommandCompletionProvider) --
// the same merged registry-1 + registry-2 set /help prints -- so completion
// and help can never list different commands.
//
// Rules, matching the official client's behaviour closely enough to be
// unsurprising:
//   * only a line that starts with '/' completes; plain chat does not;
//   * only the COMMAND WORD completes -- once the line has a space the
//     argument vocabulary is per-command and we have none, so Tab is a no-op
//     rather than a corruption of what the player typed;
//   * exactly one match replaces the partial and appends a space, so the
//     player can type arguments without reaching for it;
//   * several matches print them on ONE chat line and leave the text alone
//     (the shell convention, and the line the player is reading is the same
//     System channel /help writes to);
//   * no match does nothing at all -- silently, because a beep-equivalent
//     for every stray Tab would be noise.
// T421 (board B365) -- see the declaration. The EDIT is applyTextEntryKey(),
// the same body chat uses; what is here is the tradeskill window's POLICY.
bool FrameComponent::processTradeskillSearchInput() {
    TextEntryState* entry = tradeskillState_.focusedEntry();
    if (entry == nullptr) {
        if (textEntryGateHeld_) {
            textEntryGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
        }
        return false;
    }

    if (!textEntryGateHeld_) {
        textEntryGateHeld_ = true;
        ctx_.eventReceiver->setChatInputFocused(true);
        ctx_.eventReceiver->clearPendingKeyEvents();
        return true;
    }

    while (ctx_.eventReceiver->hasPendingKeyEvents()) {
        auto ke = ctx_.eventReceiver->popKeyEvent();
        const TextEntryAction action = applyTextEntryKey(*entry, ke.key, ke.character);

        if (action == TextEntryAction::Submit) {
            // Enter in ANY of the three runs the search, which is what the
            // official client's Editboxes do -- the Search button and the
            // return key are one request, not two.
            if (ctx_.bridge) {
                ctx_.bridge->pushIntent(eqt::events::RecipeSearchIntent{
                    tradeskillState_.searchEntry.inputText,
                    tradeskillState_.typedMinTrivial(),
                    tradeskillState_.typedMaxTrivial()});
            }
            // The text STAYS -- the box goes on echoing the live query, and
            // applyTradeskillRecipeListCleared() will confirm it from the
            // server's own answer. Only the caret leaves.
            entry->inputActive = false;
            tradeskillState_.focusedField = TradeskillField::None;
            textEntryGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
            return true;
        }

        if (action == TextEntryAction::Cancel) {
            entry->inputActive = false;
            tradeskillState_.focusedField = TradeskillField::None;
            textEntryGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
            return true;
        }
        // Complete (Tab) has no vocabulary here -- a recipe name is the
        // server's, not the client's -- so it is a deliberate no-op rather
        // than a corruption of what the player typed (T357's rule).
    }
    return true;
}

void FrameComponent::applyCommandCompletion() {
    if (!commandCompletionProvider_) {
        return;
    }
    const std::string& text = chatPanelState_.inputText;
    if (text.empty() || text[0] != '/') {
        return;
    }
    const std::string partial = text.substr(1);
    if (partial.find(' ') != std::string::npos) {
        return;
    }

    const std::vector<std::string> matches = commandCompletionProvider_(partial);
    if (matches.empty()) {
        return;
    }

    if (matches.size() == 1) {
        chatPanelState_.inputText = "/" + matches[0] + " ";
        chatPanelState_.cursorPos = static_cast<int>(chatPanelState_.inputText.size());
        return;
    }

    std::string line;
    for (const std::string& m : matches) {
        if (!line.empty()) {
            line += "  ";
        }
        line += "/" + m;
    }
    // Same push the renderer's own slash commands use (registry 3's
    // counterpart of EverQuest::AddChatSystemMessage) -- this runs on the
    // render thread, where the game-thread ChatOutput is not reachable.
    if (ctx_.bridge) {
        eqt::state::ChatMessageData data;
        data.message = line;
        data.channelType = 0;
        data.channelName = "system";
        ctx_.bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SystemMessage, std::move(data)));
    }
    LOG_INFO(MOD_MAIN, "{}", line);
}

// ===== Phase 2: Visibility =====
void FrameComponent::processFrameVisibility() {
    // Update frustum planes every frame from actual Irrlicht camera direction.
    // We derive the direction from camera target - position (not CameraController yaw/pitch,
    // which can be stale or represent player facing rather than camera view in follow mode).
    // Update frustum planes every frame (needed by SimulationWorker for next frame's input)
    if (ctx_.frustumCuller && ctx_.camera) {
        irr::core::vector3df irrFwd = (ctx_.camera->getTarget() - ctx_.camera->getPosition());
        float eqFwdX = irrFwd.X;
        float eqFwdY = irrFwd.Z;
        float eqFwdZ = irrFwd.Y;

        float fovV = ctx_.camera->getFOV();
        auto screenSize = ctx_.driver->getScreenSize();
        float aspect = (float)screenSize.Width / (float)screenSize.Height;
        float camX, camY, camZ;
        ctx_.cameraController->getPositionEQ(camX, camY, camZ);

        ctx_.frustumCuller->update(camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ,
            fovV, aspect, 1.0f, ctx_.renderDistance);
    }

    // Visibility/lighting handled by SimulationWorker — results applied in applySimulationResults()

    // Update camera position for atlas per-pixel lighting
    if (ctx_.zoneShader && ctx_.zoneShader->isAtlasAvailable() && ctx_.camera) {
        irr::core::vector3df camPos = ctx_.camera->getPosition();
        ctx_.zoneShader->setCameraPos(camPos.X, camPos.Y, camPos.Z);
    }
}

// ===== Phase 3: Simulation =====
void FrameComponent::processFrameSimulation(float deltaTime) {
    // WindowManager removed — no old UI update
    sectionStart_ = std::chrono::steady_clock::now();

    // Entity update — worker handles interpolation + culling, main thread handles
    // animation side-effects, corpse fading, equipment transforms, casting bars
    if (ctx_.entityRenderer) {
        // Pass occlusion-culled regions to entity renderer for door manager etc.
        ctx_.entityRenderer->setOcclusionCulledRegions(
            ctx_.occlusionCulledRegions.empty() ? nullptr : &ctx_.occlusionCulledRegions);

        // Pass camera BSP region to entity renderer
        if (ctx_.zoneBspTree && ctx_.currentPvsRegion != SIZE_MAX
            && ctx_.currentPvsRegion < ctx_.zoneBspTree->regions.size()) {
            ctx_.entityRenderer->setCameraRegion(ctx_.currentPvsRegion, ctx_.zoneBspTree->regions[ctx_.currentPvsRegion]);
        } else {
            ctx_.entityRenderer->setCameraRegion(SIZE_MAX, nullptr);
        }

        // Flush pending updates for animation processing only
        // (position/velocity math was already drained for the worker in postSimulationInput)
        ctx_.entityRenderer->flushPendingUpdatesForAnimations();

        // T472 (B587): the player's node wears the renderer's heading, not the
        // self row's raw server heading that a flushed echo may just have set.
        ctx_.entityRenderer->syncLocalPlayerHeading(ctx_.playerHeading);

        // B964: the impostor pass's per-frame inputs -- the governor, and the
        // target and group members it pins to full models. A no-op unless
        // the preset gives impostors a budget (the pass runs at the end of
        // updateMainThreadEntityState, before beginScene()).
        if (ctx_.entityRenderer->impostorsEnabled()) {
            std::vector<std::string> groupNames;
            if (groupPanelState_.inGroup) {
                for (const auto& m : groupPanelState_.members)
                    if (!m.name.empty()) groupNames.push_back(m.name);
            }
            ctx_.entityRenderer->setImpostorFrameInputs(
                ctx_.governor.get(), ctx_.Overlays().currentTargetId(), std::move(groupNames),
                ctx_.loadingScreenVisible);
        }

        // Main-thread entity state: corpse fading, equipment transforms, animation timing
        ctx_.entityRenderer->updateMainThreadEntityState(deltaTime);

        ctx_.entityRenderer->updateEntityCastingBars(deltaTime, ctx_.camera);
        ctx_.entityRenderer->processExpiredCombatBuffers();

        // T311: publish the player model's measured origin-to-feet distance to
        // the game thread. It is a property of the SHIPPED MESH -- only the
        // renderer can measure it -- and the game thread needs it to convert the
        // RENDER-frame z it gets from us into the WIRE-frame z the official
        // client transmits (include/client/position_frames.h). EverQuest holds
        // no renderer pointer by design, so this is a one-way intent, not a
        // synchronous read.
        //
        // Republished every frame rather than on change: caching "last
        // published" would need a new IrrlichtRenderer member, and
        // GameStateBridge::coalesceIntents already collapses these to the latest
        // one per drain, so the queue cannot grow.
        if (ctx_.bridge) {
            ctx_.bridge->pushIntent(eqt::events::PlayerModelGeometryChanged{
                ctx_.entityRenderer->getPlayerCollisionZOffset()});
        }
    }
    frameTimings_.entityUpdate = measureSection();

    // Door update
    if (ctx_.doorManager) {
        ctx_.doorManager->setOcclusionCulledRegions(
            ctx_.occlusionCulledRegions.empty() ? nullptr : &ctx_.occlusionCulledRegions);
        ctx_.doorManager->setPvsRegion(ctx_.currentPvsRegion);
        ctx_.doorManager->update(deltaTime);
        // B997 (board #317): gather this frame's PVS doors into the per-material
        // client-array batch (and detach the ones it fully takes) BEFORE
        // drawAll() -- which runs in the render phase -- builds its SOLID node
        // list. No-op when the gate is off. The batch draws at the SOLID-pre
        // point (renderer_zone_draw.cpp) and restoreSceneGraphBatch() at
        // SOLID-post re-attaches the doors. See scene_graph_batch.h.
        ctx_.doorManager->gatherSceneGraphBatch();
        // T435 (board B425): doors only collide because this runs. Must follow
        // update(), which is what marks a moving door's geometry stale.
        ctx_.PlayerCamera().syncDoorCollision();
        // T438 (board B423): and then the player rides whatever moved. Must
        // follow syncDoorCollision(), which is what puts the platform's
        // CURRENT triangles in the map the ride probe queries.
        ctx_.PlayerCamera().carryPlayerOnLifts(deltaTime);
    }
    // B892 (GitLab #208): and a levitating player hovers. Runs after the lift
    // carry -- which may have moved the floor under the player -- and before
    // the rig camera placement, so the camera frames wherever the hover
    // left the feet. A no-op not levitating, in water, mid-jump, or without
    // collision data (the whole decision is ClampLevitationFeet + a GroundZ
    // query; see applyLevitationClamp()).
    ctx_.PlayerCamera().applyLevitationClamp(deltaTime);
    // T461: a rig camera (Overhead, Chase since T474, User Defined 1/2, Tether)
    // is re-placed around wherever this frame left the player -- after the
    // movement integration AND the lift carry, and whether or not there is a
    // door manager. T486 (item 6): this is the frame's ONE placement; it used
    // to run right after updatePlayerMovement() as well, and only here when
    // doors existed. First person is placed by the movement path and inside
    // carryPlayerOnLifts(); this is a no-op there. Pinned by
    // tests/test_camera_mode_wiring.cpp TheRigCameraIsPlacedOncePerFrameAfterTheLiftCarry.
    ctx_.PlayerCamera().updateRigCamera();
    frameTimings_.doorUpdate = measureSection();

    // Spell VFX update
    if (ctx_.SimBridge().spellVisualFX()) ctx_.SimBridge().spellVisualFX()->update(deltaTime);
    frameTimings_.spellVfxUpdate = measureSection();

    // Animated textures
    if (ctx_.animatedTextureManager) ctx_.animatedTextureManager->update(deltaTime * 1000.0f);
    frameTimings_.animatedTextures = measureSection();

    // Vertex animations and light animations handled by SimulationWorker
    frameTimings_.vertexAnimations = measureSection();

    // Reset timing section so zone load time isn't attributed to tier2Update
    sectionStart_ = std::chrono::steady_clock::now();

    // Rebuild HCMap placeholder as player moves (GREEN-gated)
    // Only active during placeholder phase — before real geometry replaces it
    if (ctx_.zonePlaceholderNode && ctx_.collisionMap && ctx_.collisionMap->IsLoaded()) {
        if (!ctx_.governor || ctx_.governor->getState() == BudgetState::Green) {
            irr::core::vector3df currentPos(ctx_.playerX, ctx_.playerZ, ctx_.playerY);  // EQ→Irrlicht
            float movedSq = currentPos.getDistanceFromSQ(ctx_.ZoneLifecycle().lastPlaceholderBuildPos());
            const float rebuildThresholdSq = 100.0f * 100.0f;  // 100 units
            if (movedSq > rebuildThresholdSq) {
                ctx_.ZoneLifecycle().buildZonePlaceholder(ctx_.playerX, ctx_.playerZ, ctx_.playerY);
                ctx_.ZoneLifecycle().setupHCMapCollision();
            }
        }
    }

    // Tier 2: Detail + Tree (~20Hz)
    // Skip while deferred init is still running — subsystems aren't ready yet
    {
    bool runDetailTreeThisFrame = runTier2_;
    if (runDetailTreeThisFrame) {
        if (ctx_.detailManager && ctx_.detailManager->isEnabled()) {
            irr::core::vector3df playerPosIrrlicht(ctx_.playerX, ctx_.playerZ, ctx_.playerY);
            static float lastPlayerX = ctx_.playerX, lastPlayerY = ctx_.playerY;
            irr::core::vector3df playerVelocity(0, 0, 0);
            if (deltaTime > 0.001f) {
                float velX = (ctx_.playerX - lastPlayerX) / deltaTime;
                float velZ = (ctx_.playerY - lastPlayerY) / deltaTime;
                playerVelocity = irr::core::vector3df(velX, 0, velZ);
            }
            lastPlayerX = ctx_.playerX;
            lastPlayerY = ctx_.playerY;
            bool playerMoving = playerVelocity.getLengthSQ() > 0.1f;
            ctx_.detailManager->update(playerPosIrrlicht, deltaTime * 1000.0f,
                                   playerPosIrrlicht, playerVelocity, ctx_.playerHeading, playerMoving);
        }
        // P33: TreeWindController::update(deltaTime) is now wired live
        // (Will's ruling: trees animate unless the tree-wind-animation
        // setting is disabled — see display_settings.json "animatedTrees",
        // config_.enabled here). P32 found this call dead (gated on
        // getAnimatedTreeCount() > 0, which was always false post
        // CPU-instancing removal); that gate is gone now, replaced with
        // an explicit isEnabled() check so weatherMultiplier_'s transition
        // (setWeather -> targetMultiplier_) actually advances. deltaTime is
        // the real per-frame delta passed in from the caller (never a
        // wall-clock read inside the controller), so this stays
        // deterministic frame-to-frame for the benchmark/gate. The visual
        // gate (gles2_zone_benchmark) never calls into IrrlichtRenderer at
        // all — it drives ZoneShaderManager directly and never calls
        // setWindTime/setWindParams, so windTime_ there is permanently 0
        // regardless of anything in this file; goldens are unaffected by
        // this change (see refactor/P33_tree_wind_enable.md Review).
        if (ctx_.treeWindController.isEnabled()) {
            ctx_.treeWindController.update(deltaTime);
        }
    }
    } // tier2 detail+tree block
    frameTimings_.tier2Update = measureSection();

    // Fire light flickering handled by SimulationWorker
    frameTimings_.fireFlicker = measureSection();

    // Player light position set in applySimulationResults() by SimulationWorker

    // Tier 3: Environmental simulation (~10Hz)
    {
        // Weather state machine runs on SimulationWorker, results applied in applySimulationResults()
        frameTimings_.weatherSystemUpdate = measureSection();

        bool runEnvThisFrame = runTier3_;

        if (runEnvThisFrame) {
            float accDelta = tier3DeltaAccum_;
            tier3DeltaAccum_ = 0.0f;

            // Weather effects: render-only update (rain/snow overlays, cloud layer)
            // Timer/darkening/lightning state computed by SimulationWorker, applied in applySimulationResults()
            if (ctx_.weatherEffects) ctx_.weatherEffects->updateRenderOnly(accDelta);

            // Sky cloud scrolling computed by SimulationWorker, applied in applySimulationResults()

            if (ctx_.particleManager && ctx_.particleManager->isEnabled() && ctx_.zoneReady) {
                ctx_.particleManager->setPlayerPosition(glm::vec3(ctx_.playerX, ctx_.playerY, ctx_.playerZ), ctx_.playerHeading);
                float timeOfDay = ctx_.currentHour + ctx_.currentMinute / 60.0f;
                ctx_.particleManager->setTimeOfDay(timeOfDay);
                ctx_.particleManager->update(accDelta);
            }

            // Unified particles (fire + weather + spell effects):
            // Physics now runs in SimulationWorker. Weather light collection
            // and command posting happen in postSimulationInput().
            // updateUnified() is a no-op — kept for API compatibility.
            if (ctx_.particleManager && ctx_.zoneReady) {
                ctx_.particleManager->updateUnified(0.0f);
            }

            // Boids and tumbleweeds now run in SimulationWorker —
            // input posted in postSimulationInput(), results applied in applySimulationResults()
        }
    }
    frameTimings_.tier3Update = measureSection();

    // HUD
    hudAnimTimer_ += deltaTime;
    if (hudAnimTimer_ > 10000.0f) hudAnimTimer_ = 0.0f;
    ctx_.Overlays().updateHUD();
    frameTimings_.hudUpdate = measureSection();
}

} // namespace Graphics
} // namespace EQT
