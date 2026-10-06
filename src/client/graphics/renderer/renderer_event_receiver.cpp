#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/camera_rig.h"  // T486: kCameraZoomStepUnits
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

// --- RendererEventReceiver Implementation ---

RendererEventReceiver::RendererEventReceiver() {
    // T312: std::atomic<bool> has no aggregate initializer, so the arrays
    // are zeroed here (as they always were) rather than at the declaration.
    for (int i = 0; i < irr::KEY_KEY_CODES_COUNT; ++i) {
        keyIsDown_[i].store(false, std::memory_order_relaxed);
        keyWasPressed_[i].store(false, std::memory_order_relaxed);
    }
}

bool RendererEventReceiver::OnEvent(const irr::SEvent& event) {
    if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        keyIsDown_[event.KeyInput.Key].store(event.KeyInput.PressedDown,
                                            std::memory_order_relaxed);

        if (event.KeyInput.PressedDown) {
            keyWasPressed_[event.KeyInput.Key].store(true, std::memory_order_relaxed);

            // Queue key event for chat input (all printable characters)
            KeyEvent keyEvent;
            keyEvent.key = event.KeyInput.Key;
            keyEvent.character = event.KeyInput.Char;
            keyEvent.shift = event.KeyInput.Shift;
            keyEvent.ctrl = event.KeyInput.Control;
            pendingKeyEvents_.push_back(keyEvent);

            // Chat input shortcuts - these are always tracked for text input
            if (event.KeyInput.Key == irr::KEY_RETURN) {
                enterKeyPressed_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_ESCAPE) {
                escapeKeyPressed_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_OEM_2 || event.KeyInput.Key == irr::KEY_DIVIDE) {
                slashKeyPressed_ = true;
            }

            // When chat input is focused, only allow function keys through to hotkey matching.
            // All other keys are consumed by the chat input handler.
            if (chatInputFocused_ &&
                !(event.KeyInput.Key >= irr::KEY_F1 && event.KeyInput.Key <= irr::KEY_F12)) {
                return true;
            }

            // Look up action from HotkeyManager
            // Check Alt key state (Irrlicht doesn't expose Alt in KeyInput directly)
            bool alt = keyIsDown_[irr::KEY_LMENU].load(std::memory_order_relaxed) ||
                       keyIsDown_[irr::KEY_RMENU].load(std::memory_order_relaxed);
            auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
            auto action = hotkeyMgr.getAction(
                event.KeyInput.Key,
                event.KeyInput.Control,
                event.KeyInput.Shift,
                alt,
                eqt::input::HotkeyMode::Player);  // T319: see the note on the deleted currentMode_

            // Debug: log key press and action lookup
            LOG_DEBUG(MOD_INPUT, "Key pressed: {} (ctrl={}, shift={}, alt={}), mode={}, action={}",
                hotkeyMgr.keyCodeToName(event.KeyInput.Key),
                event.KeyInput.Control, event.KeyInput.Shift, alt,
                hotkeyMgr.modeEnumToName(eqt::input::HotkeyMode::Player),
                action.has_value() ? hotkeyMgr.actionEnumToName(*action) : "none");

            if (action.has_value()) {
                using HA = eqt::input::HotkeyAction;
                using RA = RendererAction;
                switch (*action) {
                    // === Global Actions ===
                    case HA::Quit: quitRequested_ = true; break;
                    case HA::Screenshot: actionQueue_.push_back({RA::Screenshot}); break;
                    case HA::ToggleWireframe: actionQueue_.push_back({RA::ToggleWireframe}); break;
                    case HA::ToggleHUD: actionQueue_.push_back({RA::ToggleHUD}); break;
                    case HA::ToggleNameTags: actionQueue_.push_back({RA::ToggleNameTags}); break;
                    case HA::ToggleZoneLights: actionQueue_.push_back({RA::ToggleZoneLights}); break;
                    case HA::ToggleCameraMode: actionQueue_.push_back({RA::ToggleCameraMode}); break;
                    case HA::ToggleOldModels: actionQueue_.push_back({RA::ToggleOldModels}); break;
                    case HA::ToggleAllUI: actionQueue_.push_back({RA::ToggleAllUI}); break;
                    case HA::ScrollUIUp: actionQueue_.push_back({RA::ScrollUIUp}); break;
                    case HA::ScrollUIDown: actionQueue_.push_back({RA::ScrollUIDown}); break;

                    // === Player Mode Actions ===
                    // Game actions → bridgeQueue (routed through InputActionBridge)
                    // T343 (B002): autorun left the bridge queue. It is not a
                    // game action -- the flag it toggles lives in
                    // PlayerMovementState and is read by the renderer's own
                    // per-frame movement phase, so routing it to the game
                    // thread only reached a stub that could not have written
                    // it (T334's swim finding, same shape). It is now a
                    // renderer action with an arm in processCommonInput().
                    case HA::ToggleAutorun: actionQueue_.push_back({RA::ToggleAutorun}); break;
                    case HA::ToggleAutoAttack: pushBridgeAction({RA::ToggleAutoAttack}); break;
                    // B905 (#225): Ctrl+Q was bound and fell to default.
                    case HA::Attack: pushBridgeAction({RA::Attack}); break;
                    case HA::ToggleInventory: actionQueue_.push_back({RA::ToggleInventory}); break;
                    case HA::ToggleSkills: actionQueue_.push_back({RA::ToggleSkills}); break;
                    case HA::ToggleGroup: actionQueue_.push_back({RA::ToggleGroup}); break;
                    case HA::TogglePetWindow: actionQueue_.push_back({RA::TogglePet}); break;
                    case HA::ToggleSpellbook: actionQueue_.push_back({RA::ToggleSpellbook}); break;
                    case HA::ToggleBuffWindow: actionQueue_.push_back({RA::ToggleBuffWindow}); break;
                    case HA::ToggleCombatAbilities: actionQueue_.push_back({RA::ToggleCombatAbilities}); break;
                    case HA::ToggleOptions: actionQueue_.push_back({RA::ToggleOptions}); break;
                    case HA::ToggleFriends: actionQueue_.push_back({RA::ToggleFriends}); break;  // B897
                    case HA::ToggleVendor: actionQueue_.push_back({RA::ToggleVendor}); break;
                    case HA::ToggleTrainer: actionQueue_.push_back({RA::ToggleTrainer}); break;
                    case HA::ToggleCollision: actionQueue_.push_back({RA::ToggleCollision}); break;
                    case HA::ToggleCollisionDebug: actionQueue_.push_back({RA::ToggleCollisionDebug}); break;
                    case HA::ToggleZoneLineVisualization: actionQueue_.push_back({RA::ToggleZoneLineVisualization}); break;
                    case HA::ToggleMapOverlay: actionQueue_.push_back({RA::ToggleMapOverlay}); break;
                    case HA::RotateMapOverlay: actionQueue_.push_back({RA::RotateMapOverlay}); break;
                    case HA::MirrorMapOverlayX: actionQueue_.push_back({RA::MirrorXMapOverlay}); break;
                    case HA::ToggleNavmeshOverlay: actionQueue_.push_back({RA::ToggleNavmeshOverlay}); break;
                    case HA::RotateNavmeshOverlay: actionQueue_.push_back({RA::RotateNavmeshOverlay}); break;
                    case HA::MirrorNavmeshOverlayX: actionQueue_.push_back({RA::MirrorXNavmeshOverlay}); break;
                    case HA::ToggleFrustumCulling: actionQueue_.push_back({RA::ToggleFrustumCulling}); break;
                    case HA::CycleObjectLights: actionQueue_.push_back({RA::CycleObjectLights}); break;
                    case HA::Interact:  // Generic interact - tries door first, then world object
                        actionQueue_.push_back({RA::DoorInteract});
                        actionQueue_.push_back({RA::WorldObjectInteract});
                        break;
                    case HA::InteractDoor: actionQueue_.push_back({RA::DoorInteract}); break;
                    case HA::InteractWorldObject: actionQueue_.push_back({RA::WorldObjectInteract}); break;
                    case HA::Hail: pushBridgeAction({RA::Hail}); break;
                    // T319: R -> the game thread resolves the last tell
                    // sender and the prefill comes back via
                    // IrrlichtRenderer::handleActionResult().
                    case HA::ReplyToTell: pushBridgeAction({RA::ReplyToTell}); break;
                    case HA::Consider: pushBridgeAction({RA::Consider}); break;
                    case HA::ClearTarget:
                        // B905 (#225): ESCAPE is the window stack's key
                        // (escapeKeyPressed_ above -> FrameComponent's ESC
                        // handler, which closes the newest window and clears
                        // the target only when no window took the press). The
                        // Global CancelDialog binding used to shadow this arm
                        // for Escape; with it deleted, clearing here would
                        // drop the target on every window-closing press.
                        // Any OTHER key bound to ClearTarget clears directly.
                        if (event.KeyInput.Key == irr::KEY_ESCAPE) break;
                        // Both queues: bridge clears combat target, renderer clears display
                        pushBridgeAction({RA::ClearTarget});
                        actionQueue_.push_back({RA::ClearTarget});
                        break;

                    // Targeting → bridgeQueue (routed through InputActionBridge)
                    case HA::TargetSelf: pushBridgeAction({RA::TargetSelf}); break;
                    case HA::TargetGroupMember1: pushBridgeAction({RA::TargetGroupMember1}); break;
                    case HA::TargetGroupMember2: pushBridgeAction({RA::TargetGroupMember2}); break;
                    case HA::TargetGroupMember3: pushBridgeAction({RA::TargetGroupMember3}); break;
                    case HA::TargetGroupMember4: pushBridgeAction({RA::TargetGroupMember4}); break;
                    case HA::TargetGroupMember5: pushBridgeAction({RA::TargetGroupMember5}); break;
                    case HA::TargetNearestPC: pushBridgeAction({RA::TargetNearestPC}); break;
                    case HA::TargetNearestNPC: pushBridgeAction({RA::TargetNearestNPC}); break;
                    case HA::CycleTargets: pushBridgeAction({RA::CycleTargets}); break;
                    case HA::CycleTargetsReverse: pushBridgeAction({RA::CycleTargetsReverse}); break;

                    case HA::OpenChat: enterKeyPressed_ = true; break;
                    case HA::OpenChatSlash: slashKeyPressed_ = true; break;

                    // Spell Gems
                    case HA::SpellGem1: spellGemCastRequest_ = 0; break;
                    case HA::SpellGem2: spellGemCastRequest_ = 1; break;
                    case HA::SpellGem3: spellGemCastRequest_ = 2; break;
                    case HA::SpellGem4: spellGemCastRequest_ = 3; break;
                    case HA::SpellGem5: spellGemCastRequest_ = 4; break;
                    case HA::SpellGem6: spellGemCastRequest_ = 5; break;
                    case HA::SpellGem7: spellGemCastRequest_ = 6; break;
                    case HA::SpellGem8: spellGemCastRequest_ = 7; break;

                    // Hotbar Slots
                    case HA::HotbarSlot1: hotbarActivationRequest_ = 0; break;
                    case HA::HotbarSlot2: hotbarActivationRequest_ = 1; break;
                    case HA::HotbarSlot3: hotbarActivationRequest_ = 2; break;
                    case HA::HotbarSlot4: hotbarActivationRequest_ = 3; break;
                    case HA::HotbarSlot5: hotbarActivationRequest_ = 4; break;
                    case HA::HotbarSlot6: hotbarActivationRequest_ = 5; break;
                    case HA::HotbarSlot7: hotbarActivationRequest_ = 6; break;
                    case HA::HotbarSlot8: hotbarActivationRequest_ = 7; break;
                    case HA::HotbarSlot9: hotbarActivationRequest_ = 8; break;
                    case HA::HotbarSlot10: hotbarActivationRequest_ = 9; break;

                    // Camera Zoom
                    // T486 (item 5): the notch has ONE owner, camera_rig.h's
                    // kCameraZoomStepUnits -- the wheel already used it.
                    case HA::CameraZoomIn: cameraZoomDelta_ = -kCameraZoomStepUnits; break;
                    case HA::CameraZoomOut: cameraZoomDelta_ = kCameraZoomStepUnits; break;
                    // T461/T471: the camera adjust keys (Tether-slide, Pitch,
                    // Raise/Lower); the arms are in
                    // FrameComponent::processCommonInput().
                    case HA::CameraOrbitLeft: actionQueue_.push_back({RA::CameraOrbitLeft}); break;
                    case HA::CameraOrbitRight: actionQueue_.push_back({RA::CameraOrbitRight}); break;
                    case HA::CameraPitchUp: actionQueue_.push_back({RA::CameraPitchUp}); break;
                    case HA::CameraPitchDown: actionQueue_.push_back({RA::CameraPitchDown}); break;
                    case HA::CameraRaise: actionQueue_.push_back({RA::CameraRaise}); break;
                    case HA::CameraLower: actionQueue_.push_back({RA::CameraLower}); break;

                    // Audio Volume
                    // T439 (board B432): 2 steps is the old 0.1f of a 0..1
                    // volume, said on the oracle's 0..20 scale. Same travel.
                    case HA::MusicVolumeUp: musicVolumeStepDelta_ = 2; break;
                    case HA::MusicVolumeDown: musicVolumeStepDelta_ = -2; break;
                    case HA::EffectsVolumeUp: effectsVolumeStepDelta_ = 2; break;
                    case HA::EffectsVolumeDown: effectsVolumeStepDelta_ = -2; break;

                    // Movement keys and Jump are handled separately (continuous state)
                    default:
                        break;
                }
                LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] OnEvent: action {} -> actionQ={}, bridgeQ={}, zoomDelta={}",
                    hotkeyMgr.actionEnumToName(*action),
                    actionQueue_.size(), getBridgeQueueSize(), cameraZoomDelta_);
            }
        }
        return true;
    }

    if (event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
        switch (event.MouseInput.Event) {
            case irr::EMIE_LMOUSE_PRESSED_DOWN:
                leftButtonDown_.store(true, std::memory_order_relaxed);
                // Record click position for targeting
                clickMouseX_.store(event.MouseInput.X, std::memory_order_relaxed);
                clickMouseY_.store(event.MouseInput.Y, std::memory_order_relaxed);
                leftButtonClicked_ = true;
                // Reset mouse delta tracking to prevent camera jump on click
                mouseX_.store(event.MouseInput.X, std::memory_order_relaxed);
                mouseY_.store(event.MouseInput.Y, std::memory_order_relaxed);
                lastMouseX_ = event.MouseInput.X;
                lastMouseY_ = event.MouseInput.Y;
                break;
            case irr::EMIE_LMOUSE_LEFT_UP:
                leftButtonDown_.store(false, std::memory_order_relaxed);
                leftButtonReleased_ = true;
                break;
            case irr::EMIE_RMOUSE_PRESSED_DOWN:
                rightButtonDown_.store(true, std::memory_order_relaxed);
                rightButtonClicked_.store(true, std::memory_order_relaxed);  // B442
                break;
            case irr::EMIE_RMOUSE_LEFT_UP:
                rightButtonDown_.store(false, std::memory_order_relaxed);
                break;
            case irr::EMIE_MOUSE_WHEEL:
                // T313: this case did not exist. The switch fell through to
                // `default: break` and the wheel was discarded by the whole
                // client. T461: a notch the static UI does not consume now
                // zooms the camera (the official MouseWheelZoom option); the
                // one consumer, FrameComponent::processFrameInput(), decides
                // which. Accumulated, not latched, so several notches in one
                // frame all count.
                mouseWheelDelta_ += event.MouseInput.Wheel;
                break;
            case irr::EMIE_MOUSE_MOVED:
                mouseX_.store(event.MouseInput.X, std::memory_order_relaxed);
                mouseY_.store(event.MouseInput.Y, std::memory_order_relaxed);
                if (event.MouseInput.isRightPressed()) {
                    rightButtonDown_.store(true, std::memory_order_relaxed);
                }
                if (event.MouseInput.isLeftPressed()) {
                    leftButtonDown_.store(true, std::memory_order_relaxed);
                }
                break;
            default:
                break;
        }
        return true;
    }

    return false;
}

bool RendererEventReceiver::isKeyDown(irr::EKEY_CODE keyCode) const {
    return keyIsDown_[keyCode].load(std::memory_order_relaxed);
}

// T312: read-and-reset in one step, so a press is observed exactly once
// even though the write is on the render thread and the read on the game
// thread (GraphicsInputHandler's Jump check is the only caller).
bool RendererEventReceiver::wasKeyPressed(irr::EKEY_CODE keyCode) {
    return keyWasPressed_[keyCode].exchange(false, std::memory_order_relaxed);
}

// T312: render thread only (IrrlichtRenderer::updatePlayerMovement's
// mouse-look). One load of the shared position, so the delta and the new
// baseline cannot disagree; lastMouseX_/Y_ are unshared.
int RendererEventReceiver::getMouseDeltaX() {
    const int x = mouseX_.load(std::memory_order_relaxed);
    const int delta = x - lastMouseX_;
    lastMouseX_ = x;
    return delta;
}

int RendererEventReceiver::getMouseDeltaY() {
    const int y = mouseY_.load(std::memory_order_relaxed);
    const int delta = y - lastMouseY_;
    lastMouseY_ = y;
    return delta;
}

// Read-and-reset as one atomic exchange. T315: ONE consumer, on the render
// thread (processFrameInput). The game-thread caller was deleted -- see the
// note at the declaration.
bool RendererEventReceiver::wasLeftButtonClicked() {
    return leftButtonClicked_.exchange(false, std::memory_order_relaxed);
}

bool RendererEventReceiver::wasLeftButtonReleased() {
    return leftButtonReleased_.exchange(false, std::memory_order_relaxed);
}

// B442: read-and-reset, one consumer (renderStaticUI), like the left click.
bool RendererEventReceiver::wasRightButtonClicked() {
    return rightButtonClicked_.exchange(false, std::memory_order_relaxed);
}


} // namespace Graphics
} // namespace EQT
