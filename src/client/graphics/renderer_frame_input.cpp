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
void IrrlichtRenderer::processFrameInput(float deltaTime) {
    // chatInputFocused_ is managed by processChatInput() — don't reset it here

    // Drain action queue and dispatch
    auto actions = eventReceiver_->drainActions();
    if (!actions.empty()) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processFrameInput: chatFocused={}, drained {} actions from actionQueue, bridgeQueue size={}",
            chatInputFocused_, actions.size(), eventReceiver_->getBridgeQueueSize());
    }
    processCommonInput(actions);
    processPlayerInput(actions);

    // ===== FRAME TIMING: Input Handling =====
    frameTimings_.inputHandling = measureSection();

    processInputDeltas(deltaTime);

    // ===== FRAME TIMING: Input Handling (accumulated) =====
    frameTimings_.inputHandling += measureSection();

    // Handle mouse state BEFORE camera/movement updates
    bool hadClick = eventReceiver_->wasLeftButtonClicked();
    bool hadRelease = eventReceiver_->wasLeftButtonReleased();
    int clickX = eventReceiver_->getClickMouseX();
    int clickY = eventReceiver_->getClickMouseY();
    pendingUIClick_ = hadClick;  // Preserved for UI input processing in render phase

    // Update chat panel mouse hover state
    {
        int mx = eventReceiver_->getMouseX();
        int my = eventReceiver_->getMouseY();
        chatPanelState_.mouseHover = uiLayout_.chatPanel.isPointInside(
            irr::core::position2d<irr::s32>(mx, my));

        // T313: the mouse wheel, routed by cursor position to whichever
        // static-UI panel is under it. Read-and-reset, once per frame, one
        // consumer -- and the ONLY consumer: nothing in the client read the
        // wheel before this unit (camera zoom is the CameraZoomIn/Out
        // hotkeys, not the wheel), so a UI-consumed notch cannot also zoom.
        if (newUIEnabled_ && staticUIInput_) {
            staticUIInput_->processWheel(mx, my, eventReceiver_->getMouseWheelDelta(),
                uiLayout_, chatPanelState_, skillsPopupState_, lootState_);
        } else {
            eventReceiver_->getMouseWheelDelta();  // drain, never accumulate
        }
    }

    // Update camera and player movement
    playerCamera_->updatePlayerMovement(deltaTime);
    frameTimings_.playerMovement = measureSection();
    // Update sky position to follow camera
    if (skyRenderer_ && skyRenderer_->isInitialized() && camera_) {
        skyRenderer_->setCameraPosition(camera_->getPosition());
    }

    // ===== FRAME TIMING: Camera Update =====
    frameTimings_.cameraUpdate = measureSection();

    processChatInput();

    // Handle mouse targeting
    if (hadClick) {
        overlays_->handleMouseTargeting(clickX, clickY);
    }
}

void IrrlichtRenderer::processCommonInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        switch (event.action) {
            case RA::Screenshot: pendingScreenshot_ = true; break;
            case RA::ToggleAllUI: diagnostics_->toggleAllUI(); break;
            // T313: PageUp/PageDown. Same router as the wheel; a whole page
            // per press. Global-mode actions, so they work with a popup up.
            case RA::ScrollUIUp:
            case RA::ScrollUIDown:
                if (newUIEnabled_ && staticUIInput_ && eventReceiver_) {
                    staticUIInput_->processScrollKey(
                        event.action == RA::ScrollUIUp,
                        eventReceiver_->getMouseX(), eventReceiver_->getMouseY(),
                        uiLayout_, chatPanelState_, skillsPopupState_, lootState_);
                }
                break;
            case RA::ToggleZoneLights: diagnostics_->toggleZoneLights(); break;
            case RA::CycleObjectLights: diagnostics_->cycleObjectLights(); break;
            // T330 (B043): these four arms had never existed. The hotkeys were
            // bound (hotkey_manager.cpp) and the receiver queued the actions
            // (renderer_event_receiver.cpp), but they fell through the default
            // below and did nothing. Pinned by tests/test_debug_hotkey_arms.cpp.
            case RA::ToggleWireframe: diagnostics_->toggleWireframe(); break;
            case RA::ToggleHUD: diagnostics_->toggleHUD(); break;
            case RA::ToggleNameTags: diagnostics_->toggleNameTags(); break;
            case RA::ToggleOldModels: diagnostics_->toggleOldModels(); break;
            case RA::ToggleOptions:
                // WindowManager removed — options window not available
                break;
            case RA::ClearTarget:
                if (currentTargetId_ != 0) {
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                    overlays_->clearCurrentTarget();
                    SetTrackedTargetId(0);
                }
                break;
            default: break;
        }
    }
}

void IrrlichtRenderer::processPlayerInput(const std::vector<RendererEvent>& actions) {
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
                if (bridge_) bridge_->pushIntent(eqt::events::VendorToggleIntent{});
                break;
            case RA::ToggleTrainer:
                if (bridge_) bridge_->pushIntent(eqt::events::TrainerToggleIntent{});
                break;
            case RA::DoorInteract:
                if (doorManager_) {
                    uint8_t doorId = doorManager_->getNearestDoor(playerX_, playerY_, playerZ_, playerHeading_);
                    if (doorId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "Door interaction: ID {}", doorId);
                        if (bridge_) bridge_->pushIntent(eqt::events::DoorInteractIntent{doorId});
                    }
                }
                break;
            case RA::WorldObjectInteract: {
                    uint32_t objectId = getNearestWorldObject(playerX_, playerY_, playerZ_);
                    if (objectId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "World object interaction: dropId {}", objectId);
                        if (bridge_) bridge_->pushIntent(eqt::events::WorldObjectInteractIntent{objectId});
                    }
                }
                break;

            // Collision
            case RA::ToggleCollision:
                playerConfig_.collisionEnabled = !playerConfig_.collisionEnabled;
                LOG_INFO(MOD_GRAPHICS, "Collision: {}", (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"));
                break;
            case RA::ToggleCollisionDebug:
                playerConfig_.collisionDebug = !playerConfig_.collisionDebug;
                LOG_INFO(MOD_GRAPHICS, "Collision Debug: {}", (playerConfig_.collisionDebug ? "ON" : "OFF"));
                if (playerConfig_.collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "  Collision Height: {}", playerConfig_.collisionCheckHeight);
                    LOG_INFO(MOD_GRAPHICS, "  Step Height: {}", playerConfig_.collisionStepHeight);
                }
                break;

            // UI Windows
            case RA::ToggleInventory: toggleInventory(); break;
            case RA::ToggleGroup:
                break;  // Group panel visibility is driven by inGroup state
            case RA::ToggleSkills:
                skillsPopupState_.isOpen = !skillsPopupState_.isOpen;
                LOG_INFO(MOD_GRAPHICS, "Skills window {}", skillsPopupState_.isOpen ? "opened" : "closed");
                break;
            case RA::TogglePet:
                break;  // Pet panel visibility is driven by hasPet state
            case RA::ToggleSpellbook:
                spellbookState_.isOpen = !spellbookState_.isOpen;
                LOG_INFO(MOD_GRAPHICS, "Spellbook {}", spellbookState_.isOpen ? "opened" : "closed");
                break;
            case RA::ToggleBuffWindow:
                break;  // Buff bar always visible when buffs exist

            // Debug overlays
            case RA::ToggleZoneLineVisualization: overlays_->toggleZoneLineVisualization(); break;
            case RA::ToggleMapOverlay: overlays_->toggleMapOverlay(); break;
            case RA::RotateMapOverlay:
                mapOverlayRotation_ = (mapOverlayRotation_ + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable rotation: {}° around Y axis (terrain unchanged)", mapOverlayRotation_ * 90);
                break;
            case RA::MirrorXMapOverlay:
                mapOverlayMirrorX_ = !mapOverlayMirrorX_;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable X mirror: {} (terrain unchanged)", mapOverlayMirrorX_ ? "ON" : "OFF");
                break;
            case RA::ToggleNavmeshOverlay: overlays_->toggleNavmeshOverlay(); break;
            case RA::RotateNavmeshOverlay:
                navmeshOverlayRotation_ = (navmeshOverlayRotation_ + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay rotation: {}° around Y axis", navmeshOverlayRotation_ * 90);
                break;
            case RA::MirrorXNavmeshOverlay:
                navmeshOverlayMirrorX_ = !navmeshOverlayMirrorX_;
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay X mirror: {}", navmeshOverlayMirrorX_ ? "ON" : "OFF");
                break;

            case RA::ToggleFrustumCulling:
                if (frustumCuller_) {
                    // Cycle: ON → ON+DEBUG → OFF → ON ...
                    if (frustumCuller_->isEnabled() && !frustumDebugDraw_) {
                        frustumDebugDraw_ = true;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON + DEBUG DRAW (Ctrl+V to cycle)");
                    } else if (frustumCuller_->isEnabled() && frustumDebugDraw_) {
                        frustumCuller_->setEnabled(false);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: OFF (Ctrl+V to cycle)");
                    } else {
                        frustumCuller_->setEnabled(true);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON (Ctrl+V to cycle)");
                    }

                    // Diagnostic: log camera direction and nearby region bboxes
                    if (camera_ && cameraController_) {
                        irr::core::vector3df irrFwd = (camera_->getTarget() - camera_->getPosition());
                        float camX, camY, camZ;
                        cameraController_->getPositionEQ(camX, camY, camZ);
                        float eqFwdX = irrFwd.X, eqFwdY = irrFwd.Z, eqFwdZ = irrFwd.Y;
                        float fwdLen = std::sqrt(eqFwdX*eqFwdX + eqFwdY*eqFwdY + eqFwdZ*eqFwdZ);
                        if (fwdLen > 0.001f) { eqFwdX /= fwdLen; eqFwdY /= fwdLen; eqFwdZ /= fwdLen; }
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: cam EQ=({:.1f},{:.1f},{:.1f}) fwd EQ=({:.3f},{:.3f},{:.3f})",
                            camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ);
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: Irrlicht cam=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f})",
                            camera_->getPosition().X, camera_->getPosition().Y, camera_->getPosition().Z,
                            camera_->getTarget().X, camera_->getTarget().Y, camera_->getTarget().Z);

                        // Log 10 closest region bboxes with sizes and frustum results
                        struct RegionDiag { size_t idx; float dist; float sizeX, sizeY, sizeZ; bool frustum; };
                        std::vector<RegionDiag> diags;
                        for (auto& [regionIdx, bbox] : regionBoundingBoxes_) {
                            float cx = (bbox.MinEdge.X + bbox.MaxEdge.X) * 0.5f;
                            float cy = (bbox.MinEdge.Y + bbox.MaxEdge.Y) * 0.5f;
                            float cz = (bbox.MinEdge.Z + bbox.MaxEdge.Z) * 0.5f;
                            float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            bool inFrustum = !frustumCuller_->isEnabled() || frustumCuller_->testAABB(
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
                            auto& bbox = regionBoundingBoxes_[d.idx];
                            LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: region {} dist={:.0f} size=({:.1f},{:.1f},{:.1f}) "
                                "bbox=({:.1f},{:.1f},{:.1f})->({:.1f},{:.1f},{:.1f}) {}",
                                d.idx, d.dist, d.sizeX, d.sizeY, d.sizeZ,
                                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z,
                                d.frustum ? "VISIBLE" : "CULLED");
                        }
                    }

                    // Log occlusion culler stats if available
                    if (occlusionCuller_ && occlusionCuller_->isEnabled()) {
                        const auto& occStats = occlusionCuller_->getStats();
                        int fillPct = occStats.depthBufferTotalPixels > 0
                            ? (occStats.depthBufferFilledPixels * 100 / occStats.depthBufferTotalPixels) : 0;
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: {} rasterized ({} tris, {} clipped), buf {}/{} px ({}%), "
                            "{} tested, {} culled [reject: {} behind, {} offscreen, {} too-large, {} low-coverage]",
                            occStats.regionsRasterized, occStats.trianglesRasterized, occStats.trianglesClipped,
                            occStats.depthBufferFilledPixels, occStats.depthBufferTotalPixels, fillPct,
                            occStats.regionsTested, occStats.regionsCulled,
                            occStats.rejectedBehindCamera, occStats.rejectedOffScreen,
                            occStats.rejectedScreenTooLarge, occStats.rejectedLowCoverage);
                        occlusionCuller_->dumpDepthBufferPGM("occlusion_depth.pgm");
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: depth buffer dumped to occlusion_depth.pgm");

                        // Detailed per-region occluder diagnostic (use camera position)
                        float occDiagX = playerX_, occDiagY = playerY_, occDiagZ = playerZ_;
                        if (cameraController_) {
                            cameraController_->getPositionEQ(occDiagX, occDiagY, occDiagZ);
                        }
                        struct OccDiag { size_t regionIdx; float dist; size_t triCount; };
                        std::vector<OccDiag> occDiags;
                        for (auto& [regionIdx, node] : regionMeshNodes_) {
                            if (!node || !node->isVisible()) continue;
                            auto bboxIt = regionBoundingBoxes_.find(regionIdx);
                            if (bboxIt == regionBoundingBoxes_.end()) continue;
                            const auto& bbox = bboxIt->second;
                            float closestX = std::max(bbox.MinEdge.X, std::min(occDiagX, bbox.MaxEdge.X));
                            float closestY = std::max(bbox.MinEdge.Y, std::min(occDiagY, bbox.MaxEdge.Y));
                            float closestZ = std::max(bbox.MinEdge.Z, std::min(occDiagZ, bbox.MaxEdge.Z));
                            float ddx = occDiagX - closestX, ddy = occDiagY - closestY, ddz = occDiagZ - closestZ;
                            float dist = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);
                            const auto& occ = occlusionCuller_->getRegionOccluders(regionIdx);
                            occDiags.push_back({regionIdx, dist, occ.size()});
                        }
                        std::sort(occDiags.begin(), occDiags.end(),
                            [](const OccDiag& a, const OccDiag& b) { return a.dist < b.dist; });
                        LOG_INFO(MOD_GRAPHICS, "OCCL REGIONS (closest 20 visible):");
                        for (int i = 0; i < 20 && i < (int)occDiags.size(); ++i) {
                            auto& d = occDiags[i];
                            const auto& occ = occlusionCuller_->getRegionOccluders(d.regionIdx);
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

                    // Force visibility rebuild
                    forcePvsUpdate_ = true;
                }
                break;

            default: break;
        }
    }
}

void IrrlichtRenderer::processInputDeltas(float deltaTime) {
    // Camera zoom with Follow camera
    if (!chatInputFocused_) {
        float zoomDelta = eventReceiver_->getCameraZoomDelta();
        if (zoomDelta != 0.0f) {
            LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processInputDeltas: zoomDelta={}, cameraController={}, cameraMode={}(Follow={})",
                zoomDelta, (cameraController_ != nullptr),
                static_cast<int>(cameraMode_), static_cast<int>(CameraMode::Follow));
        }
        if (zoomDelta != 0.0f && cameraController_ && cameraMode_ == CameraMode::Follow) {
            cameraController_->adjustFollowDistance(zoomDelta);
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_, deltaTime);
            LOG_DEBUG(MOD_GRAPHICS, "Camera zoom distance: {:.1f}", cameraController_->getFollowDistance());
        }
    }

    // D20c2: Push volume hotkey changes as intents via bridge
    if (bridge_) {
        float musicDelta = eventReceiver_->getMusicVolumeDelta();
        if (musicDelta != 0.0f) {
            bridge_->pushIntent(eqt::events::MusicVolumeChangeIntent{musicDelta});
        }
        float effectsDelta = eventReceiver_->getEffectsVolumeDelta();
        if (effectsDelta != 0.0f) {
            bridge_->pushIntent(eqt::events::EffectsVolumeChangeIntent{effectsDelta});
        }
    }
}

void IrrlichtRenderer::processChatInput() {
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
    if (auto prefill = takeChatPrefill()) {
        chatInputFocused_ = true;
        chatPanelState_.inputActive = true;
        chatPanelState_.inputText = *prefill;
        chatPanelState_.cursorPos = static_cast<int>(prefill->size());
        eventReceiver_->setChatInputFocused(true);
        eventReceiver_->clearPendingKeyEvents();
        return;
    }

    // Open chat input on Enter or / key
    if (!chatInputFocused_) {
        if (eventReceiver_->enterKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText.clear();
            chatPanelState_.cursorPos = 0;
            eventReceiver_->setChatInputFocused(true);
            eventReceiver_->clearPendingKeyEvents();
            return;
        }
        if (eventReceiver_->slashKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText = "/";
            chatPanelState_.cursorPos = 1;
            eventReceiver_->setChatInputFocused(true);
            eventReceiver_->clearPendingKeyEvents();
            return;
        }
    }

    // Process typed characters when chat is focused
    if (chatInputFocused_) {
        while (eventReceiver_->hasPendingKeyEvents()) {
            auto ke = eventReceiver_->popKeyEvent();

            if (ke.key == irr::KEY_RETURN) {
                // Submit chat text
                if (!chatPanelState_.inputText.empty()) {
                    LOG_INFO(MOD_GRAPHICS, "Chat submit: '{}'", chatPanelState_.inputText);
                    if (bridge_) {
                        bridge_->pushIntent(eqt::events::ChatSubmitIntent{chatPanelState_.inputText});
                    }
                }
                chatPanelState_.inputText.clear();
                chatPanelState_.cursorPos = 0;
                chatPanelState_.inputActive = false;
                chatInputFocused_ = false;
                eventReceiver_->setChatInputFocused(false);
                return;
            }

            if (ke.key == irr::KEY_ESCAPE) {
                // Cancel chat input
                chatPanelState_.inputText.clear();
                chatPanelState_.cursorPos = 0;
                chatPanelState_.inputActive = false;
                chatInputFocused_ = false;
                eventReceiver_->setChatInputFocused(false);
                return;
            }

            if (ke.key == irr::KEY_BACK) {
                // Backspace
                if (chatPanelState_.cursorPos > 0 && !chatPanelState_.inputText.empty()) {
                    chatPanelState_.inputText.erase(chatPanelState_.cursorPos - 1, 1);
                    chatPanelState_.cursorPos--;
                }
                continue;
            }

            // Insert printable character
            if (ke.character >= 32 && ke.character < 127) {
                chatPanelState_.inputText.insert(chatPanelState_.cursorPos, 1, static_cast<char>(ke.character));
                chatPanelState_.cursorPos++;
            }
        }
        return;
    }

    // Not in chat — clear pending keys and handle escape for target clearing
    eventReceiver_->clearPendingKeyEvents();
    if (eventReceiver_->escapeKeyPressed()) {
        // T290: StaticUIInput::processKey() -- the static UI's ESC handler --
        // had NO production caller at all; it was reached only from
        // tests/test_static_ui_panels.cpp (the T48 implemented-but-unreached
        // class). This is that caller. It runs FIRST so ESC dismisses an open
        // popup before it clears the target, which is what the real client
        // does, and it is the single body for "ESC closes the loot window"
        // (which pushes CloseLootIntent -- the panel closes when the server
        // confirms, never optimistically).
        bool popupConsumedEscape = false;
        if (newUIEnabled_ && staticUIInput_) {
            popupConsumedEscape = staticUIInput_->processKey(
                irr::KEY_ESCAPE, /*pressed=*/true, /*shift=*/false, /*ctrl=*/false,
                inventoryState_, spellbookState_, skillsPopupState_, lootState_);
        }
        if (!popupConsumedEscape && currentTargetId_ != 0) {
            LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
            overlays_->clearCurrentTarget();
            SetTrackedTargetId(0);
            eventReceiver_->pushBridgeAction({RendererAction::ClearTarget});
        }
    }
}

// ===== Phase 2: Visibility =====
void IrrlichtRenderer::processFrameVisibility() {
    // Update frustum planes every frame from actual Irrlicht camera direction.
    // We derive the direction from camera target - position (not CameraController yaw/pitch,
    // which can be stale or represent player facing rather than camera view in follow mode).
    // Update frustum planes every frame (needed by SimulationWorker for next frame's input)
    if (frustumCuller_ && camera_) {
        irr::core::vector3df irrFwd = (camera_->getTarget() - camera_->getPosition());
        float eqFwdX = irrFwd.X;
        float eqFwdY = irrFwd.Z;
        float eqFwdZ = irrFwd.Y;

        float fovV = camera_->getFOV();
        auto screenSize = driver_->getScreenSize();
        float aspect = (float)screenSize.Width / (float)screenSize.Height;
        float camX, camY, camZ;
        cameraController_->getPositionEQ(camX, camY, camZ);

        frustumCuller_->update(camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ,
            fovV, aspect, 1.0f, renderDistance_);
    }

    // Visibility/lighting handled by SimulationWorker — results applied in applySimulationResults()

    // Update camera position for atlas per-pixel lighting
    if (zoneShader_ && zoneShader_->isAtlasAvailable() && camera_) {
        irr::core::vector3df camPos = camera_->getPosition();
        zoneShader_->setCameraPos(camPos.X, camPos.Y, camPos.Z);
    }
}

// ===== Phase 3: Simulation =====
void IrrlichtRenderer::processFrameSimulation(float deltaTime) {
    // WindowManager removed — no old UI update
    sectionStart_ = std::chrono::steady_clock::now();

    // Entity update — worker handles interpolation + culling, main thread handles
    // animation side-effects, corpse fading, equipment transforms, casting bars
    if (entityRenderer_) {
        // Pass occlusion-culled regions to entity renderer for door manager etc.
        entityRenderer_->setOcclusionCulledRegions(
            occlusionCulledRegions_.empty() ? nullptr : &occlusionCulledRegions_);

        // Pass camera BSP region to entity renderer
        if (zoneBspTree_ && currentPvsRegion_ != SIZE_MAX
            && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            entityRenderer_->setCameraRegion(currentPvsRegion_, zoneBspTree_->regions[currentPvsRegion_]);
        } else {
            entityRenderer_->setCameraRegion(SIZE_MAX, nullptr);
        }

        // Flush pending updates for animation processing only
        // (position/velocity math was already drained for the worker in postSimulationInput)
        entityRenderer_->flushPendingUpdatesForAnimations();

        // Main-thread entity state: corpse fading, equipment transforms, animation timing
        entityRenderer_->updateMainThreadEntityState(deltaTime);

        entityRenderer_->updateEntityCastingBars(deltaTime, camera_);
        entityRenderer_->processExpiredCombatBuffers();

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
        if (bridge_) {
            bridge_->pushIntent(eqt::events::PlayerModelGeometryChanged{
                entityRenderer_->getPlayerCollisionZOffset()});
        }
    }
    frameTimings_.entityUpdate = measureSection();

    // Door update
    if (doorManager_) {
        doorManager_->setOcclusionCulledRegions(
            occlusionCulledRegions_.empty() ? nullptr : &occlusionCulledRegions_);
        doorManager_->setPvsRegion(currentPvsRegion_);
        doorManager_->update(deltaTime);
    }
    frameTimings_.doorUpdate = measureSection();

    // Spell VFX update
    if (spellVisualFX_) spellVisualFX_->update(deltaTime);
    frameTimings_.spellVfxUpdate = measureSection();

    // Animated textures
    if (animatedTextureManager_) animatedTextureManager_->update(deltaTime * 1000.0f);
    frameTimings_.animatedTextures = measureSection();

    // Vertex animations and light animations handled by SimulationWorker
    frameTimings_.vertexAnimations = measureSection();

    // Reset timing section so zone load time isn't attributed to tier2Update
    sectionStart_ = std::chrono::steady_clock::now();

    // Rebuild HCMap placeholder as player moves (GREEN-gated)
    // Only active during placeholder phase — before real geometry replaces it
    if (zonePlaceholderNode_ && collisionMap_ && collisionMap_->IsLoaded()) {
        if (!governor_ || governor_->getState() == BudgetState::Green) {
            irr::core::vector3df currentPos(playerX_, playerZ_, playerY_);  // EQ→Irrlicht
            float movedSq = currentPos.getDistanceFromSQ(lastPlaceholderBuildPos_);
            const float rebuildThresholdSq = 100.0f * 100.0f;  // 100 units
            if (movedSq > rebuildThresholdSq) {
                buildZonePlaceholder(playerX_, playerZ_, playerY_);
                setupHCMapCollision();
            }
        }
    }

    // Tier 2: Detail + Tree (~20Hz)
    // Skip while deferred init is still running — subsystems aren't ready yet
    {
    bool runDetailTreeThisFrame = runTier2_;
    if (runDetailTreeThisFrame) {
        if (detailManager_ && detailManager_->isEnabled()) {
            irr::core::vector3df playerPosIrrlicht(playerX_, playerZ_, playerY_);
            static float lastPlayerX = playerX_, lastPlayerY = playerY_;
            irr::core::vector3df playerVelocity(0, 0, 0);
            if (deltaTime > 0.001f) {
                float velX = (playerX_ - lastPlayerX) / deltaTime;
                float velZ = (playerY_ - lastPlayerY) / deltaTime;
                playerVelocity = irr::core::vector3df(velX, 0, velZ);
            }
            lastPlayerX = playerX_;
            lastPlayerY = playerY_;
            bool playerMoving = playerVelocity.getLengthSQ() > 0.1f;
            detailManager_->update(playerPosIrrlicht, deltaTime * 1000.0f,
                                   playerPosIrrlicht, playerVelocity, playerHeading_, playerMoving);
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
        if (treeWindController_.isEnabled()) {
            treeWindController_.update(deltaTime);
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
            if (weatherEffects_) weatherEffects_->updateRenderOnly(accDelta);

            // Sky cloud scrolling computed by SimulationWorker, applied in applySimulationResults()

            if (particleManager_ && particleManager_->isEnabled() && zoneReady_) {
                particleManager_->setPlayerPosition(glm::vec3(playerX_, playerY_, playerZ_), playerHeading_);
                float timeOfDay = currentHour_ + currentMinute_ / 60.0f;
                particleManager_->setTimeOfDay(timeOfDay);
                particleManager_->update(accDelta);
            }

            // Unified particles (fire + weather + spell effects):
            // Physics now runs in SimulationWorker. Weather light collection
            // and command posting happen in postSimulationInput().
            // updateUnified() is a no-op — kept for API compatibility.
            if (particleManager_ && zoneReady_) {
                particleManager_->updateUnified(0.0f);
            }

            // Boids and tumbleweeds now run in SimulationWorker —
            // input posted in postSimulationInput(), results applied in applySimulationResults()
        }
    }
    frameTimings_.tier3Update = measureSection();

    // HUD
    hudAnimTimer_ += deltaTime;
    if (hudAnimTimer_ > 10000.0f) hudAnimTimer_ = 0.0f;
    overlays_->updateHUD();
    frameTimings_.hudUpdate = measureSection();
}

} // namespace Graphics
} // namespace EQT
