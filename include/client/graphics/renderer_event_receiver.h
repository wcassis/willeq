// The renderer keyboard/mouse event receiver class.
// Extracted verbatim from the umbrella renderer header by
// refactor/J17_irrlicht_renderer_header_split.md. Move-only: no line of the
// extracted block was edited. The three timer-query free-function
// declarations in that header's preamble deliberately did not travel; see
// that plan for why.

#ifndef EQT_GRAPHICS_RENDERER_EVENT_RECEIVER_H
#define EQT_GRAPHICS_RENDERER_EVENT_RECEIVER_H


#include <irrlicht.h>
#include <atomic>
#include <chrono>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <utility>
#include <optional>
#include <glm/glm.hpp>
#include "client/state/entity_pose_state.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/animated_texture_manager.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/graphics_archive_index.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/tree_wind_controller.h"
#include "client/graphics/tree_identifier.h"
#include "client/graphics/weather_system.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/texture_atlas.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/display_settings.h"  // U07c2: extracted from deleted options_window.h
#include "client/graphics/background_work_queue.h"
#include "client/graphics/background_thread_pool.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/gpu_upload_thread.h"
#include "client/graphics/work_priority.h"
#include "client/input/hotkey_manager.h"

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
#include "client/graphics/gles2_egl_helper.h"
#endif


// Collision map (included for HCMap::Triangle in member variable)
#include "client/hc_map.h"

// Forward declaration for navmesh pathfinder
class PathfinderNavmesh;

// Forward declaration for zone lines
namespace EQT { struct ZoneLineBoundingBox; }

// Forward declarations for inventory UI and bridge
namespace eqt {
// U07c3: WindowManager deleted
namespace inventory { class InventoryManager; }
namespace bridge { class GameStateBridge; }
}

// Forward declaration for spell visual effects
namespace EQ { class SpellVisualFX; }
namespace eqt { namespace ui { class ChatMessageBuffer; class CharacterModelView; } }
namespace EQT { namespace Graphics { class TextBatch; class UIAtlas; class UIRenderer; class StaticUIInput; } }
// U03: included directly for layout struct and panel data structs (small, no deps)
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"

// Forward declaration for constrained texture cache
namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }
// T300: the first-person camera's constants and its pure placement math.
#include "client/graphics/first_person_camera.h"
#include "client/graphics/renderer_types.h"

namespace EQT {
namespace Graphics {
// Event receiver for keyboard/mouse input
class RendererEventReceiver : public irr::IEventReceiver {
public:
    RendererEventReceiver();

    virtual bool OnEvent(const irr::SEvent& event) override;

    bool isKeyDown(irr::EKEY_CODE keyCode) const;
    bool wasKeyPressed(irr::EKEY_CODE keyCode);

    // Mouse state. T312: the position/button scalars are written in
    // OnEvent (render thread) and read from BOTH threads, so they are
    // relaxed atomics -- same discipline as the T225 one-shot flags. The
    // delta accessors below are RENDER-THREAD ONLY (see the note there).
    int getMouseX() const { return mouseX_.load(std::memory_order_relaxed); }
    int getMouseY() const { return mouseY_.load(std::memory_order_relaxed); }
    // T312: destructive read-and-reset, and the ONLY caller is the
    // mouse-look in IrrlichtRenderer::updatePlayerMovement() on the render
    // thread. GraphicsInputHandler used to call these too, from the game
    // thread, storing the result in an InputState field that has no reader
    // anywhere in the tree -- so that call only ever STOLE the delta and
    // froze the camera for a frame. It was deleted; do not add a second
    // consumer without making the accumulator per-consumer.
    int getMouseDeltaX();
    int getMouseDeltaY();
    bool isLeftButtonDown() const { return leftButtonDown_.load(std::memory_order_relaxed); }
    bool isRightButtonDown() const { return rightButtonDown_.load(std::memory_order_relaxed); }
    // T315: destructive read-and-reset (atomic exchange), RENDER THREAD
    // ONLY -- the sole consumer is IrrlichtRenderer::processFrameInput(),
    // which drives pendingUIClick_, the static-UI hit test and
    // handleMouseTargeting(). GraphicsInputHandler polled these too, on the
    // game thread, into a dead InputState field; whenever it won the
    // exchange the click was consumed and no UI ever saw it. Do not add a
    // second caller -- tests/test_read_and_reset_ownership.cpp fails if you
    // do.
    bool wasLeftButtonClicked();  // Returns true once per click (not hold)
    bool wasLeftButtonReleased(); // Returns true once when button is released
    int getClickMouseX() const { return clickMouseX_.load(std::memory_order_relaxed); }
    int getClickMouseY() const { return clickMouseY_.load(std::memory_order_relaxed); }

    // Input state queries (render thread sets, game thread polls -- atomic)
    bool quitRequested() const { return quitRequested_.load(std::memory_order_relaxed); }
    void setQuitRequested(bool quit) { quitRequested_.store(quit, std::memory_order_relaxed); }

    // Action queue for renderer-internal actions (UI toggles, debug, camera, etc.)
    // T312: RENDER THREAD ONLY -- filled in OnEvent and drained by
    // IrrlichtRenderer::processFrame() (renderer_frame.cpp), both on the
    // render thread. Unlike bridgeQueue_ below it needs no lock; if you
    // ever add a game-thread drain, it needs one.
    std::vector<RendererEvent> drainActions() { std::vector<RendererEvent> r; r.swap(actionQueue_); return r; }

    // Bridge queue for game actions routed through InputActionBridge
    // (targeting, combat, movement toggles).
    // T312: CROSS-THREAD -- pushed on the render thread (OnEvent's decoded
    // hotkeys, IrrlichtRenderer::pushBridgeAction) and swap-drained on the
    // game thread (GraphicsInputHandler::updateFromEventReceiver). A
    // reallocating push_back racing the swap is heap corruption, so every
    // access -- size() included -- goes through bridgeMutex_. There is no
    // raw access to bridgeQueue_ anywhere; keep it that way.
    std::vector<RendererEvent> drainBridgeActions() {
        std::vector<RendererEvent> r;
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        r.swap(bridgeQueue_);
        return r;
    }
    size_t getBridgeQueueSize() const {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        return bridgeQueue_.size();
    }
    void pushBridgeAction(RendererEvent event) {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        bridgeQueue_.push_back(std::move(event));
    }

    // Spell gem and hotbar requests (use intData in RendererEvent for new code).
    // Read-and-reset via exchange: see the T225 note on the fields below.
    int8_t getSpellGemCastRequest() { return spellGemCastRequest_.exchange(-1, std::memory_order_relaxed); }
    int8_t getHotbarActivationRequest() { return hotbarActivationRequest_.exchange(-1, std::memory_order_relaxed); }

    // Delta accumulators (accumulate between frames, consumed once per frame)
    float getCameraZoomDelta() { float d = cameraZoomDelta_; cameraZoomDelta_ = 0; return d; }
    // T439 (board B432): STEPS on the oracle's 0..20 scale, not a 0..1
    // fraction. These were `float` +/- 0.1f, which is +/- 2 steps -- the same
    // travel, said in the unit the volume intents and VolumesChanged now
    // speak. The consumer adds them to the step the audio manager last
    // published, so a hotkey and a slider drag can no longer disagree.
    int getMusicVolumeStepDelta() { int d = musicVolumeStepDelta_; musicVolumeStepDelta_ = 0; return d; }
    int getEffectsVolumeStepDelta() { int d = effectsVolumeStepDelta_; effectsVolumeStepDelta_ = 0; return d; }
    // T313: the mouse wheel. Before this unit EMIE_MOUSE_WHEEL had no case
    // in OnEvent() at all -- nothing in the client consumed it, camera zoom
    // included (that is hotkey-only, CameraZoomIn/Out above) -- so the
    // static UI is its sole consumer and there is no zoom to double-fire.
    // Declared here rather than beside the other accumulators below so the
    // whole thing is one hunk (T312 is editing this header concurrently).
    float getMouseWheelDelta() { float d = mouseWheelDelta_; mouseWheelDelta_ = 0; return d; }
private:
    float mouseWheelDelta_ = 0.0f;
public:

    // Chat input key events
    struct KeyEvent {
        irr::EKEY_CODE key;
        wchar_t character;
        bool shift;
        bool ctrl;
    };
    bool hasPendingKeyEvents() const { return !pendingKeyEvents_.empty(); }
    KeyEvent popKeyEvent() {
        if (pendingKeyEvents_.empty()) return {irr::KEY_KEY_CODES_COUNT, 0, false, false};
        KeyEvent e = pendingKeyEvents_.front();
        pendingKeyEvents_.erase(pendingKeyEvents_.begin());
        return e;
    }
    void clearPendingKeyEvents() { pendingKeyEvents_.clear(); }

    // Chat focus shortcuts (read-and-reset). T241: the game-thread readers in
    // GraphicsInputHandler were deleted (their InputAction::OpenChat/
    // OpenChatSlash/CloseChat had no consumer), so these three flags and
    // pendingKeyEvents_ are now set (OnEvent) and consumed
    // (IrrlichtRenderer::processChatInput) on the render thread only.
    bool enterKeyPressed() { bool v = enterKeyPressed_; enterKeyPressed_ = false; return v; }
    bool slashKeyPressed() { bool v = slashKeyPressed_; slashKeyPressed_ = false; return v; }
    bool escapeKeyPressed() { bool v = escapeKeyPressed_; escapeKeyPressed_ = false; return v; }

    // Chat focus state (set by renderer, read by GraphicsInputHandler on the
    // game thread -- atomic, same discipline as the flags)
    void setChatInputFocused(bool focused) { chatInputFocused_.store(focused, std::memory_order_relaxed); }
    bool isChatInputFocused() const { return chatInputFocused_.load(std::memory_order_relaxed); }

private:
    // Continuous key/mouse state.
    // T312: the key arrays are WRITTEN in OnEvent (render thread) and READ
    // on both threads -- isKeyDown() from renderer_player_physics /
    // renderer_overlays / renderer_rdp (render) and from
    // GraphicsInputHandler (game). Every access in the tree is by a single
    // element (constructor loop, OnEvent's indexed writes, the two
    // accessors); nothing copies, memsets, or takes the array as a whole,
    // so a per-element std::atomic<bool> is exactly the granularity of the
    // sharing. Relaxed: each key is independent, with no ordering
    // relationship to any other field (the T225 argument).
    std::atomic<bool> keyIsDown_[irr::KEY_KEY_CODES_COUNT];
    std::atomic<bool> keyWasPressed_[irr::KEY_KEY_CODES_COUNT];
    std::atomic<int> mouseX_{0};
    std::atomic<int> mouseY_{0};
    // Render thread only: touched solely by getMouseDelta{X,Y}(), whose one
    // consumer is the render-thread mouse-look.
    int lastMouseX_ = 0, lastMouseY_ = 0;
    std::atomic<bool> leftButtonDown_{false};
    std::atomic<bool> rightButtonDown_{false};
    // SET on the render thread (OnEvent, from Irrlicht input) and consumed
    // on the render thread (IrrlichtRenderer::processFrameInput()).
    // T315 SUPERSEDES T225 HERE: T225 kept the game-thread consumer on the
    // grounds that "both threads are legitimate consumers of the same
    // flag". They were not -- the game-thread side
    // (GraphicsInputHandler::updateFromEventReceiver) stored the result in
    // InputState, a struct with zero readers, so it could only ever eat the
    // click. It was deleted, along with InputState itself. They stay atomic
    // because OnEvent writes them from the render thread's event pump while
    // the flags are also touched during the frame, and a relaxed exchange()
    // keeps the read-and-reset one indivisible step; there is no ordering
    // relationship to any other field.
    std::atomic<bool> leftButtonClicked_{false};
    std::atomic<bool> leftButtonReleased_{false};
    std::atomic<int> clickMouseX_{0};
    std::atomic<int> clickMouseY_{0};
    std::atomic<bool> quitRequested_{false};
    std::atomic<bool> chatInputFocused_{false};  // Set by renderer, read by GraphicsInputHandler

    // Action queue (replaces ~47 boolean flags) — renderer-internal actions
    std::vector<RendererEvent> actionQueue_;
    // Bridge queue — game actions routed to GraphicsInputHandler → InputActionBridge.
    // T312: guarded by bridgeMutex_ (see the accessors above).
    mutable std::mutex bridgeMutex_;
    std::vector<RendererEvent> bridgeQueue_;

    // Spell gem and hotbar (indexed values, not simple booleans; -1 = none).
    // Same cross-thread read-and-reset as the bools above (T225).
    std::atomic<int8_t> spellGemCastRequest_{-1};
    std::atomic<int8_t> hotbarActivationRequest_{-1};

    // Delta accumulators (accumulate between frames)
    float cameraZoomDelta_ = 0.0f;
    int musicVolumeStepDelta_ = 0;    // T439: oracle steps, was +/- 0.1f
    int effectsVolumeStepDelta_ = 0;

    // Chat input state -- render thread only since T241 (see the accessors)
    std::vector<KeyEvent> pendingKeyEvents_;
    bool enterKeyPressed_ = false;
    bool slashKeyPressed_ = false;
    bool escapeKeyPressed_ = false;

    // T319: currentMode_ and getCurrentMode() are gone. HotkeyMode has
    // exactly two values, Global and Player; getAction() checks Global
    // bindings first in every case and then the mode passed in, so the
    // only mode this receiver could usefully be in is Player. Nothing ever
    // wrote the member and nothing ever called the getter -- it was
    // configuration that could not be configured. OnEvent now passes
    // HotkeyMode::Player literally. Bringing a real mode back (a "cursor
    // mode", say) means adding the HotkeyMode enumerator, its bindings,
    // and a writer; a member with no writer is not a head start on that.
};
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_EVENT_RECEIVER_H
