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

// Read display settings directly from config/display_settings.json
// This file is optional — missing file uses defaults (everything enabled)
static eqt::ui::DisplaySettings loadDisplaySettingsFromFile() {
    eqt::ui::DisplaySettings settings;  // defaults: everything enabled

    std::ifstream file("config/display_settings.json");
    if (!file.is_open()) {
        LOG_INFO(MOD_GRAPHICS, "No display_settings.json found, using defaults");
        return settings;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse display_settings.json: {}", errors);
        return settings;
    }

    if (root.isMember("environmentEffects")) {
        const Json::Value& env = root["environmentEffects"];
        settings.atmosphericParticles = env.get("atmosphericParticles", true).asBool();
        settings.ambientCreatures = env.get("ambientCreatures", true).asBool();
        settings.rollingObjects = env.get("rollingObjects", true).asBool();
        settings.skyEnabled = env.get("skyEnabled", true).asBool();
        settings.animatedTrees = env.get("animatedTrees", true).asBool();
        settings.fireEffects = env.get("fireEffects", true).asBool();
    }
    if (root.isMember("detailObjects")) {
        const Json::Value& detail = root["detailObjects"];
        settings.detailObjectsEnabled = detail.get("enabled", true).asBool();
    }

    return settings;
}

eqt::ui::DisplaySettings IrrlichtRenderer::getDisplaySettings() {
    return cachedDisplaySettings_;
}

// Log detailed OpenGL/driver information for debugging GPU issues
static void logDriverDetails(irr::video::IVideoDriver* driver, irr::IrrlichtDevice* device,
                             const irr::SIrrlichtCreationParameters& params) {
    if (!driver || !device) return;

    // Driver name and type
    const wchar_t* driverName = driver->getName();
    std::wstring wname(driverName);
    std::string name(wname.begin(), wname.end());
    LOG_INFO(MOD_GRAPHICS, "Video driver: {}", name);

    // Requested vs actual parameters
    auto screenSize = driver->getScreenSize();
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Screen size: {}x{} (requested {}x{})",
              screenSize.Width, screenSize.Height,
              params.WindowSize.Width, params.WindowSize.Height);

    // Current color format
    auto colorFormat = driver->getColorFormat();
    const char* fmtName = "unknown";
    switch (colorFormat) {
        case irr::video::ECF_A1R5G5B5: fmtName = "A1R5G5B5 (16-bit)"; break;
        case irr::video::ECF_R5G6B5:   fmtName = "R5G6B5 (16-bit)"; break;
        case irr::video::ECF_R8G8B8:   fmtName = "R8G8B8 (24-bit)"; break;
        case irr::video::ECF_A8R8G8B8: fmtName = "A8R8G8B8 (32-bit)"; break;
        default: break;
    }
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Color format: {} (enum={})", fmtName, static_cast<int>(colorFormat));

    // Max texture size
    auto maxTexSize = driver->getMaxTextureSize();
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Max texture size: {}x{}", maxTexSize.Width, maxTexSize.Height);

    // Feature support
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Feature support:");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Hardware TL: {}", driver->queryFeature(irr::video::EVDF_HARDWARE_TL) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Multitexture: {}", driver->queryFeature(irr::video::EVDF_MULTITEXTURE) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Bilinear filter: {}", driver->queryFeature(irr::video::EVDF_BILINEAR_FILTER) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   MipMap: {}", driver->queryFeature(irr::video::EVDF_MIP_MAP) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Stencil buffer: {}", driver->queryFeature(irr::video::EVDF_STENCIL_BUFFER) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Vertex shader 1.1: {}", driver->queryFeature(irr::video::EVDF_VERTEX_SHADER_1_1) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Pixel shader 1.1: {}", driver->queryFeature(irr::video::EVDF_PIXEL_SHADER_1_1) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Render to target: {}", driver->queryFeature(irr::video::EVDF_RENDER_TO_TARGET) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Alpha to coverage: {}", driver->queryFeature(irr::video::EVDF_ALPHA_TO_COVERAGE) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Texture NPOT: {}", driver->queryFeature(irr::video::EVDF_TEXTURE_NPOT) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Framebuffer object: {}", driver->queryFeature(irr::video::EVDF_FRAMEBUFFER_OBJECT) ? "yes" : "no");

    // Vendor info from Irrlicht (only for OpenGL driver)
    auto vendorInfo = driver->getVendorInfo();
    if (vendorInfo.size() > 0) {
        std::string vendorStr(vendorInfo.c_str());
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Vendor info: {}", vendorStr);
    }

    // Available video modes
    auto* videoModeList = device->getVideoModeList();
    if (videoModeList) {
        int modeCount = videoModeList->getVideoModeCount();
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Desktop resolution: {}x{} @{}bpp",
                  videoModeList->getDesktopResolution().Width,
                  videoModeList->getDesktopResolution().Height,
                  videoModeList->getDesktopDepth());
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Available video modes: {}", modeCount);
        for (int i = 0; i < modeCount && i < 10; ++i) {
            auto res = videoModeList->getVideoModeResolution(i);
            int depth = videoModeList->getVideoModeDepth(i);
            LOG_TRACE(MOD_GRAPHICS, "[GL]   Mode {}: {}x{} @{}bpp", i, res.Width, res.Height, depth);
        }
    }

    // Primitive counts
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Max primitive count: {}", driver->getMaximalPrimitiveCount());

    // DISPLAY environment variable (important for X11/GLX)
    const char* display = std::getenv("DISPLAY");
    LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY env: {}", display ? display : "(not set)");
}

// --- RendererEventReceiver Implementation ---

RendererEventReceiver::RendererEventReceiver() {
    for (int i = 0; i < irr::KEY_KEY_CODES_COUNT; ++i) {
        keyIsDown_[i] = false;
        keyWasPressed_[i] = false;
    }
}

bool RendererEventReceiver::OnEvent(const irr::SEvent& event) {
    if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        keyIsDown_[event.KeyInput.Key] = event.KeyInput.PressedDown;

        if (event.KeyInput.PressedDown) {
            keyWasPressed_[event.KeyInput.Key] = true;

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
            bool alt = keyIsDown_[irr::KEY_LMENU] || keyIsDown_[irr::KEY_RMENU];
            auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
            auto action = hotkeyMgr.getAction(
                event.KeyInput.Key,
                event.KeyInput.Control,
                event.KeyInput.Shift,
                alt,
                currentMode_);

            // Debug: log key press and action lookup
            LOG_DEBUG(MOD_INPUT, "Key pressed: {} (ctrl={}, shift={}, alt={}), mode={}, action={}",
                hotkeyMgr.keyCodeToName(event.KeyInput.Key),
                event.KeyInput.Control, event.KeyInput.Shift, alt,
                hotkeyMgr.modeEnumToName(currentMode_),
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

                    // === Player Mode Actions ===
                    // Game actions → bridgeQueue (routed through InputActionBridge)
                    case HA::ToggleAutorun: bridgeQueue_.push_back({RA::ToggleAutorun}); break;
                    case HA::ToggleAutoAttack: bridgeQueue_.push_back({RA::ToggleAutoAttack}); break;
                    case HA::ToggleInventory: actionQueue_.push_back({RA::ToggleInventory}); break;
                    case HA::ToggleSkills: actionQueue_.push_back({RA::ToggleSkills}); break;
                    case HA::ToggleGroup: actionQueue_.push_back({RA::ToggleGroup}); break;
                    case HA::TogglePetWindow: actionQueue_.push_back({RA::TogglePet}); break;
                    case HA::ToggleSpellbook: actionQueue_.push_back({RA::ToggleSpellbook}); break;
                    case HA::ToggleBuffWindow: actionQueue_.push_back({RA::ToggleBuffWindow}); break;
                    case HA::ToggleOptions: actionQueue_.push_back({RA::ToggleOptions}); break;
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
                    case HA::Hail: bridgeQueue_.push_back({RA::Hail}); break;
                    case HA::Consider: bridgeQueue_.push_back({RA::Consider}); break;
                    case HA::ClearTarget:
                        // Both queues: bridge clears combat target, renderer clears display
                        bridgeQueue_.push_back({RA::ClearTarget});
                        actionQueue_.push_back({RA::ClearTarget});
                        break;

                    // Targeting → bridgeQueue (routed through InputActionBridge)
                    case HA::TargetSelf: bridgeQueue_.push_back({RA::TargetSelf}); break;
                    case HA::TargetGroupMember1: bridgeQueue_.push_back({RA::TargetGroupMember1}); break;
                    case HA::TargetGroupMember2: bridgeQueue_.push_back({RA::TargetGroupMember2}); break;
                    case HA::TargetGroupMember3: bridgeQueue_.push_back({RA::TargetGroupMember3}); break;
                    case HA::TargetGroupMember4: bridgeQueue_.push_back({RA::TargetGroupMember4}); break;
                    case HA::TargetGroupMember5: bridgeQueue_.push_back({RA::TargetGroupMember5}); break;
                    case HA::TargetNearestPC: bridgeQueue_.push_back({RA::TargetNearestPC}); break;
                    case HA::TargetNearestNPC: bridgeQueue_.push_back({RA::TargetNearestNPC}); break;
                    case HA::CycleTargets: bridgeQueue_.push_back({RA::CycleTargets}); break;
                    case HA::CycleTargetsReverse: bridgeQueue_.push_back({RA::CycleTargetsReverse}); break;

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
                    case HA::CameraZoomIn: cameraZoomDelta_ = -2.0f; break;
                    case HA::CameraZoomOut: cameraZoomDelta_ = 2.0f; break;

                    // Audio Volume
                    case HA::MusicVolumeUp: musicVolumeDelta_ = 0.1f; break;
                    case HA::MusicVolumeDown: musicVolumeDelta_ = -0.1f; break;
                    case HA::EffectsVolumeUp: effectsVolumeDelta_ = 0.1f; break;
                    case HA::EffectsVolumeDown: effectsVolumeDelta_ = -0.1f; break;

                    // Movement keys and Jump are handled separately (continuous state)
                    default:
                        break;
                }
                LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] OnEvent: action {} -> actionQ={}, bridgeQ={}, zoomDelta={}",
                    hotkeyMgr.actionEnumToName(*action),
                    actionQueue_.size(), bridgeQueue_.size(), cameraZoomDelta_);
            }
        }
        return true;
    }

    if (event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
        switch (event.MouseInput.Event) {
            case irr::EMIE_LMOUSE_PRESSED_DOWN:
                leftButtonDown_ = true;
                // Record click position for targeting
                clickMouseX_ = event.MouseInput.X;
                clickMouseY_ = event.MouseInput.Y;
                leftButtonClicked_ = true;
                // Reset mouse delta tracking to prevent camera jump on click
                mouseX_ = event.MouseInput.X;
                mouseY_ = event.MouseInput.Y;
                lastMouseX_ = event.MouseInput.X;
                lastMouseY_ = event.MouseInput.Y;
                break;
            case irr::EMIE_LMOUSE_LEFT_UP:
                leftButtonDown_ = false;
                leftButtonReleased_ = true;
                break;
            case irr::EMIE_RMOUSE_PRESSED_DOWN:
                rightButtonDown_ = true;
                break;
            case irr::EMIE_RMOUSE_LEFT_UP:
                rightButtonDown_ = false;
                break;
            case irr::EMIE_MOUSE_MOVED:
                mouseX_ = event.MouseInput.X;
                mouseY_ = event.MouseInput.Y;
                if (event.MouseInput.isRightPressed()) {
                    rightButtonDown_ = true;
                }
                if (event.MouseInput.isLeftPressed()) {
                    leftButtonDown_ = true;
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
    return keyIsDown_[keyCode];
}

bool RendererEventReceiver::wasKeyPressed(irr::EKEY_CODE keyCode) {
    bool pressed = keyWasPressed_[keyCode];
    keyWasPressed_[keyCode] = false;
    return pressed;
}

int RendererEventReceiver::getMouseDeltaX() {
    int delta = mouseX_ - lastMouseX_;
    lastMouseX_ = mouseX_;
    return delta;
}

int RendererEventReceiver::getMouseDeltaY() {
    int delta = mouseY_ - lastMouseY_;
    lastMouseY_ = mouseY_;
    return delta;
}

// T225: read-and-reset as one atomic exchange -- consumed on both the render
// thread (processFrameInput) and the game thread (GraphicsInputHandler).
bool RendererEventReceiver::wasLeftButtonClicked() {
    return leftButtonClicked_.exchange(false, std::memory_order_relaxed);
}

bool RendererEventReceiver::wasLeftButtonReleased() {
    return leftButtonReleased_.exchange(false, std::memory_order_relaxed);
}

// --- IrrlichtRenderer Implementation ---

IrrlichtRenderer::IrrlichtRenderer() {
}

IrrlichtRenderer::~IrrlichtRenderer() {
    shutdown();
}

bool IrrlichtRenderer::initLoadingScreen(const RendererConfig& config) {
    config_ = config;

    // Apply constrained rendering configuration
    // Constrained mode is always active; Max preset has no practical limits
    // Populate config from preset (unless custom NxNxN config was already set)
    if (config_.constrainedPreset != ConstrainedRenderingPreset::Custom) {
        config_.constrainedConfig = ConstrainedRendererConfig::fromPreset(config_.constrainedPreset);
    }
    config_.constrainedConfig.calculateMaxResolution();
    config_.constrainedConfig.calculateMemoryLimits();
    // In automatic deferred loading mode, mark progressive loading active from the start
    // so the loading screen stays visible until all assets are built.
    if (config_.constrainedConfig.deferredAssetLoading) {
        progressiveLoadingActive_ = true;
    }
    if (config_.constrainedConfig.clampResolution(config_.width, config_.height)) {
        LOG_WARN(MOD_GRAPHICS, "Resolution clamped to {}x{} (framebuffer memory limit: {} bytes)",
                 config_.width, config_.height, config_.constrainedConfig.framebufferMemoryBytes);
    }
    LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode: {} ({}x{}, {}MB texture, {}MB framebuffer, {}MB RAM budget)",
             ConstrainedRendererConfig::presetName(config_.constrainedPreset),
             config_.width, config_.height,
             config_.constrainedConfig.textureMemoryBytes / (1024 * 1024),
             config_.constrainedConfig.framebufferMemoryBytes / (1024 * 1024),
             config_.constrainedConfig.totalMemoryBudgetBytes / (1024 * 1024));
    // Create frame budget governor
    if (!governor_) {
        LOG_TRACE(MOD_GRAPHICS, "Allocating FrameBudgetGovernor...");
        float targetFps = config_.constrainedConfig.targetFps;
        governor_ = std::make_unique<FrameBudgetGovernor>(targetFps);
        LOG_INFO(MOD_GRAPHICS, "Frame budget governor: target {:.0f} FPS ({:.1f}ms budget)",
                 targetFps, governor_->getTargetFrameTimeMs());
    }

    // Create shared background thread pool
    if (!backgroundThreadPool_) {
        LOG_TRACE(MOD_GRAPHICS, "Allocating BackgroundThreadPool ({} threads)...",
                  config_.constrainedConfig.backgroundThreadCount);
        backgroundThreadPool_ = std::make_unique<BackgroundThreadPool>(
            config_.constrainedConfig.backgroundThreadCount);
        LOG_INFO(MOD_GRAPHICS, "Background thread pool: {} thread(s)",
                 config_.constrainedConfig.backgroundThreadCount);
    }

    // One-time file I/O at app startup (before render loop begins).
    // Caches display settings so render-thread code never reads JSON.
    if (!displaySettingsCached_) {
        LOG_TRACE(MOD_GRAPHICS, "Loading display settings from file...");
        cachedDisplaySettings_ = loadDisplaySettingsFromFile();
        displaySettingsCached_ = true;
    }

    // One-time race mappings load (validated at startup by S03)
    if (!areRaceMappingsLoaded()) {
        LOG_TRACE(MOD_GRAPHICS, "Loading race mappings from config/race_models.json...");
        if (!loadRaceMappings("config/race_models.json")) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load race model mappings: config/race_models.json");
        }
    }

    // Load EQE1 archive configuration (which .eqe1 files to open per zone)
    if (eqe1ArchiveEntries_.empty()) {
        loadEQE1ArchiveConfig("config/eqe1_archives.json");
    }

    // Item model mappings handled on-demand via SQLite — no pre-loading needed

    // P32: driver type is always GLES2 now (RenderingBackend::Software/
    // OpenGL removed — docs/decisions.md 2026-07-28 addendum). Collapsed
    // from a switch since RenderingBackend has exactly one value.
    irr::video::E_DRIVER_TYPE driverType = irr::video::EDT_OGLES2;
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL ES 2.0");

    // Create device
    irr::SIrrlichtCreationParameters params;
    params.DriverType = driverType;
    params.WindowSize = irr::core::dimension2d<irr::u32>(config.width, config.height);
    params.Fullscreen = config.fullscreen;
    params.Stencilbuffer = config_.constrainedConfig.enableStencilBuffer;
    params.Vsync = true;
    params.AntiAlias = config_.constrainedConfig.antiAliasLevel;

#ifdef EQT_HAS_DRM
    // DRM/KMS: use framebuffer device type (renders via EGL/GBM, no X11)
    if (config_.constrainedConfig.useDRM) {
        params.DeviceType = irr::EIDT_FRAMEBUFFER;
        LOG_INFO(MOD_GRAPHICS, "[GL] Loading screen: using DRM/KMS framebuffer device");
    }
#endif

#ifdef EQT_HAS_GLES2
    // P25: X11-windowed EGL backend opt-in (x86 GLES2 dev builds only —
    // this is inert on ARM, whose CIrrDeviceFB.cpp is the untouched shared
    // DRM device and never reads this variable). Plumbed via an env var
    // rather than a direct call into CIrrDeviceFB, because that device
    // header is private to the patched Irrlicht build and isn't on this
    // translation unit's include path (only Irrlicht's public API is).
    // Explicit opt-in only: never set based on $DISPLAY.
    if (config_.constrainedConfig.useEglX11) {
        setenv("WILLEQ_EGL_X11", "1", 1);
        LOG_INFO(MOD_GRAPHICS, "[GL] Loading screen: X11-windowed EGL backend requested (--egl-x11)");
    }
#endif

    LOG_DEBUG(MOD_GRAPHICS, "[GL] Creating Irrlicht device: {}x{}, fullscreen={}, vsync={}, stencil={}, AA={}",
              config.width, config.height, config.fullscreen, true,
              config_.constrainedConfig.enableStencilBuffer,
              config_.constrainedConfig.antiAliasLevel);
    if (!config_.constrainedConfig.useDRM) {
        LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY={}", std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "(not set)");
    }

    LOG_TRACE(MOD_GRAPHICS, "Creating Irrlicht device (createDeviceEx)...");
    device_ = irr::createDeviceEx(params);

    if (!device_) {
        // P32: the EDT_SOFTWARE retry fallback was removed along with
        // RenderingBackend::Software (docs/decisions.md 2026-07-28
        // addendum) — GLES2 device creation failure is now fatal, not
        // silently downgraded to the software renderer.
        LOG_ERROR(MOD_GRAPHICS, "Failed to create Irrlicht device with {} driver",
                  backendName(config_.constrainedConfig.renderingBackend));
        return false;
    }

    // Allow Irrlicht logging at DEBUG level, suppress at INFO
    device_->getLogger()->setLogLevel(irr::ELL_WARNING);
    device_->setWindowCaption(irr::core::stringw(config.windowTitle.c_str()).c_str());

    LOG_TRACE(MOD_GRAPHICS, "Acquiring driver, scene manager, GUI environment...");
    driver_ = device_->getVideoDriver();
    smgr_ = device_->getSceneManager();
    guienv_ = device_->getGUIEnvironment();
    if (!driver_ || !smgr_ || !guienv_) {
        LOG_FATAL(MOD_GRAPHICS, "Irrlicht device returned null subsystems: driver={} smgr={} guienv={}",
                  (void*)driver_, (void*)smgr_, (void*)guienv_);
        return false;
    }

    // Wire driver into constrained texture cache immediately (before any texture loading)
    LOG_TRACE(MOD_GRAPHICS, "Wiring driver into constrained texture cache...");
    if (driver_ && constrainedTextureCache_) {
        constrainedTextureCache_->setDriver(driver_);
        constrainedTextureCache_->setSceneManager(smgr_);
        LOG_INFO(MOD_GRAPHICS, "Texture cache wired to driver ({}KB limit, {}x{} max texture, mipmaps={})",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.enableMipmaps ? "yes" : "no");
    }

    // Log comprehensive driver/OpenGL details
    logDriverDetails(driver_, device_, params);

    // In DRM mode, enable and create software cursor (no hardware cursor available)
    if (config_.constrainedConfig.useDRM && device_->getCursorControl()) {
        device_->getCursorControl()->setVisible(true);
    }
    LOG_TRACE(MOD_GRAPHICS, "Creating software cursor...");
    createSoftwareCursor();

    // U01: Initialize batched text renderer
    textBatch_ = std::make_unique<TextBatch>();
    if (textBatch_->init(guienv_)) {
        LOG_INFO(MOD_GRAPHICS, "TextBatch initialized");
    } else {
        LOG_WARN(MOD_GRAPHICS, "TextBatch initialization failed — falling back to font->draw()");
        textBatch_.reset();
    }

    // U02: Load UI atlas from willeq.eqe1 flat pack (ETC1 compressed texture + JSON metadata)
    if (!config_.constrainedConfig.eqe1Path.empty() && constrainedTextureCache_) {
        // Open willeq.eqe1 early (before zone loading) so all willeq-generated assets are available
        getOrOpenEQE1("willeq");

        uiAtlas_ = std::make_unique<UIAtlas>();
        if (!uiAtlas_->load(driver_, constrainedTextureCache_)) {
            LOG_FATAL(MOD_GRAPHICS, "UIAtlas: willeq.eqe1 missing or incomplete in {}. "
                "Run: eqe1_builder --bmp-dir data/atlas --bmp-dir data/textures --output {} --name willeq",
                config_.constrainedConfig.eqe1Path, config_.constrainedConfig.eqe1Path);
            return false;
        }
    }

    // U03: Initialize static layout UI renderer
    LOG_TRACE(MOD_GRAPHICS, "Initializing UI renderer...");
    uiRenderer_ = std::make_unique<UIRenderer>();
    uiRenderer_->init(driver_, uiAtlas_.get(), textBatch_.get());
    uiLayout_.computeLayout(config.width, config.height);
    // U04: Create chat message buffer for new UI
    newUIChatBuffer_ = std::make_unique<eqt::ui::ChatMessageBuffer>(500);
    chatPanelState_.messageBuffer = newUIChatBuffer_.get();
    // U06j: Initialize static UI input handler
    staticUIInput_ = std::make_unique<StaticUIInput>();
    staticUIInput_->init(bridge_);
    LOG_INFO(MOD_GRAPHICS, "UIRenderer initialized (newui=off, toggle with /newui)");

    // Configure mipmap generation based on constrained config
    if (driver_) {
        driver_->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS,
                                         config_.constrainedConfig.enableMipmaps);
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Mipmap generation: {}",
                  config_.constrainedConfig.enableMipmaps ? "enabled" : "disabled");
    }

    // Wire ancillary services into cache (driver/smgr already wired above)
    if (constrainedTextureCache_) {
        if (backgroundThreadPool_)
            constrainedTextureCache_->setBackgroundThreadPool(backgroundThreadPool_.get());
        // Pre-create placeholder texture before render loop starts
        constrainedTextureCache_->getPlaceholderTexture();

        // P10b: register the region-mesh texture guard so evictions can
        // never leave a dangling ITexture* in a region mesh buffer's
        // material, even for nodes detached from the scene graph for
        // front-to-back manual draw (see renderer_zone_merge.h). Bound to
        // regionMeshNodes_ (a persistent member, cleared not recreated
        // between zone visits) once here, for the renderer's lifetime. The
        // placeholder texture is resolved ONCE, right here (before any
        // eviction can occur, safe to lock the cache's mutex_) and handed
        // to the guard as a plain pointer -- the guard must NEVER call back
        // into constrainedTextureCache_ from inside onTextureEvicted() (see
        // RegionMeshTextureGuard's header comment for the self-deadlock
        // this avoids).
        regionMeshTextureGuard_ = std::make_unique<RegionMeshTextureGuard>(
            regionMeshNodes_, constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(regionMeshTextureGuard_.get());

        // P10b: softwareCursorTexture_ was already set by createSoftwareCursor()
        // above (runs earlier in this function) -- guard it against the same
        // dangling-pointer class (see SingleTexturePointerGuard's doc comment
        // in renderer_zone_merge.h; pre-existing, unrelated to mergeZoneDraws,
        // found while verifying this unit's own fix).
        softwareCursorTextureGuard_ = std::make_unique<SingleTexturePointerGuard>(softwareCursorTexture_);
        constrainedTextureCache_->addEvictionListener(softwareCursorTextureGuard_.get());

        // T123: guards detached objectNodes_ entries -- see renderer_zone_merge.h.
        objectNodeTextureGuard_ = std::make_unique<ObjectNodeTextureGuard>(objectNodes_, constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(objectNodeTextureGuard_.get());
    }

    // Create event receiver
    LOG_TRACE(MOD_GRAPHICS, "Creating event receiver...");
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

    // Setup camera (needed for loading screen rendering)
    LOG_TRACE(MOD_GRAPHICS, "Setting up camera...");
    setupCamera();

    // Setup lighting (basic setup)
    LOG_TRACE(MOD_GRAPHICS, "Setting up lighting...");
    setupLighting();

    // Apply initial settings from constrained config (must come BEFORE shader variant sync)
    wireframeMode_ = config_.constrainedConfig.wireframe;
    fogEnabled_ = config_.constrainedConfig.fog;
    debugPlayerLightEnabled_ = config_.constrainedConfig.playerLight;
    lightingEnabled_ = config.lighting;

    // Initialize GLSL shader pipeline (if enabled and driver supports it).
    // P32: the "!= RenderingBackend::Software" guard was removed — GLES2
    // is the only backend now, so it was always true.
    if (config_.constrainedConfig.enableShaders) {
        LOG_TRACE(MOD_GRAPHICS, "Initializing GLSL shader pipeline...");
        auto* gpu = driver_->getGPUProgrammingServices();
        if (gpu) {
            zoneShader_ = std::make_unique<ZoneShaderManager>(driver_, gpu);
            if (!zoneShader_->isAvailable()) {
                LOG_WARN(MOD_GRAPHICS, "GLSL shaders requested but compilation failed; using fixed-function fallback");
                zoneShader_.reset();
            } else if (zoneShader_->isLightweightAvailable()) {
                zoneShader_->setPerPixelPlayerLight(debugPlayerLightEnabled_);
            }
        }
    }

    // Initialize GPU upload thread (shared EGL context for async texture/VBO uploads)
#ifdef EQT_HAS_GLES2
    LOG_TRACE(MOD_GRAPHICS, "Initializing GPU upload thread (shared EGL context)...");
    initGPUUploadThread();
#endif

    // Setup HUD (needed for loading screen text)
    LOG_TRACE(MOD_GRAPHICS, "Setting up HUD...");
    setupHUD();

    // S05: Pre-allocate renderer-lifetime subsystems (survive zone changes)

    // Create entity renderer (needed for entity model loading during zone load)
    LOG_TRACE(MOD_GRAPHICS, "Creating entity renderer...");
    createEntityRenderer();

    // P17: register the entity-mesh texture guard (constructed here, not
    // alongside regionMeshTextureGuard_/softwareCursorTextureGuard_ above,
    // because it needs entityRenderer_ to exist first). Same placeholder-
    // resolved-once-up-front pattern as the other two guards -- see
    // EntityMeshTextureGuard's doc comment in renderer_zone_merge.h.
    if (constrainedTextureCache_ && entityRenderer_) {
        entityMeshTextureGuard_ = std::make_unique<EntityMeshTextureGuard>(
            entityRenderer_->getEntities(), constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(entityMeshTextureGuard_.get());
    }

    // Create sky renderer (skyRendering preset flag controls whether it renders, not whether it exists)
    LOG_TRACE(MOD_GRAPHICS, "Creating sky renderer...");
    skyRenderer_ = std::make_unique<SkyRenderer>(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_) {
        skyRenderer_->setConstrainedTextureCache(constrainedTextureCache_);
    }
    if (!skyRenderer_->initialize(config_.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization failed - sky will not be rendered");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized");
    }

    // Create detail manager (detailObjectsEnabled controls whether it renders, not whether it exists)
    LOG_TRACE(MOD_GRAPHICS, "Creating detail manager...");
    detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
    detailManager_->setSurfaceMapsPath("data/detail/zones");
    if (constrainedTextureCache_) {
        detailManager_->setConstrainedTextureCache(constrainedTextureCache_);
    }
    LOG_INFO(MOD_GRAPHICS, "Detail manager initialized");

    // Create simulation worker (zone data configured later via setZoneData())
    simulationWorker_ = std::make_unique<SimulationWorker>();
    LOG_INFO(MOD_GRAPHICS, "Simulation worker initialized");

    // Create render pass timer (Irrlicht-owned via ref counting)
    renderPassTimer_ = new RenderPassTimer();
    renderPassTimer_->setRenderer(this);
    LOG_INFO(MOD_GRAPHICS, "Render pass timer initialized");

    // P32: AnimatedTreeManager (CPU-instancing wrapper) deleted. Its
    // construction here did three things: setRenderDistance/
    // setConstrainedTextureCache/setShaderMaterialTypes — all three fed
    // ONLY the CPU-instancing path (createAnimatedTree/updateTreeAnimation),
    // which was reachable only via the now-removed EDT_BURNINGSVIDEO branch
    // in loadZoneStep11_Env(). treeWindController_/treeIdentifier_ are
    // plain default-constructed members needing no init here — their
    // per-zone config load happens in loadZoneStep11_Env() same as before.

    // Create weather system (needed before zone loading)
    if (!weatherSystem_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating weather system...");
        weatherSystem_ = std::make_unique<WeatherSystem>();
        // Connect weather system to tree wind controller via callback
        weatherSystem_->addCallback([this](WeatherType weather) {
            treeWindController_.setWeather(weather);
        });
    }

    // Check display settings to determine which environmental managers to create.
    auto displaySettings = getDisplaySettings();

    // Always create particle manager — needed for unified fire system even when
    // atmospheric particles (dust, pollen, fireflies) are disabled
    if (!particleManager_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating particle manager...");
        particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
        particleManager_->setConstrainedTextureCache(constrainedTextureCache_);
        LOG_TRACE(MOD_GRAPHICS, "Initializing particle manager (loading atlas)...");
        if (!particleManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize particle manager");
        }
        // Compile the point sprite shader program now (during init, under the loading
        // screen) rather than during deferred init where the ~260ms Mali 400 shader
        // compilation spikes the governor into Red. The shader has zero zone data
        // dependencies — it only needs the GLES2 context.
        particleManager_->initUnifiedRenderer();
        // Atmospheric particles setting controls billboard emitters (dust, pollen, etc.)
        // Unified fire has its own toggle and works regardless of this setting
        particleManager_->setEnabled(displaySettings.atmosphericParticles);
        LOG_INFO(MOD_GRAPHICS, "Particle manager initialized (atmospheric particles: {})",
                 displaySettings.atmosphericParticles ? "enabled" : "disabled");
    }

    // Create ambient creatures (boids) system (only if ambient creatures enabled)
    if (!boidsManager_ && displaySettings.ambientCreatures) {
        LOG_TRACE(MOD_GRAPHICS, "Creating boids manager (loading atlas)...");
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        boidsManager_->setConstrainedTextureCache(constrainedTextureCache_);
        if (!boidsManager_->init(config_.eqClientPath)) LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
        ensureBoidsTextureGuard();
        LOG_INFO(MOD_GRAPHICS, "Boids manager initialized (ambient creatures enabled in settings)");
    } else if (!boidsManager_) {
        LOG_INFO(MOD_GRAPHICS, "Boids manager skipped (ambient creatures disabled in settings)");
    }

    // Create tumbleweed manager (only if rolling objects enabled)
    if (!tumbleweedManager_ && displaySettings.rollingObjects) {
        LOG_TRACE(MOD_GRAPHICS, "Creating tumbleweed manager (loading texture)...");
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        tumbleweedManager_->setConstrainedTextureCache(constrainedTextureCache_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
        }
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager initialized (rolling objects enabled in settings)");
    } else if (!tumbleweedManager_) {
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager skipped (rolling objects disabled in settings)");
    }

    // Create weather effects controller (screen-space rain/snow overlays, storm clouds, lightning)
    if (!weatherEffects_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating weather effects controller...");
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_TRACE(MOD_GRAPHICS, "Initializing weather effects (loading cloud/rain/snow textures)...");
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller");
        }
        // Connect weather system to weather effects
        if (weatherSystem_) {
            weatherSystem_->addListener(weatherEffects_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects initialized");
    }

    initialized_ = true;
    loadingScreenVisible_ = true;  // Show loading screen by default
    lastFpsTime_ = device_->getTimer()->getTime();

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer loading screen initialized: {}x{}", config.width, config.height);
    return true;
}

bool IrrlichtRenderer::loadGlobalAssets() {
    if (!initialized_) {
        LOG_ERROR(MOD_GRAPHICS, "Cannot load global assets - renderer not initialized");
        return false;
    }

    // S05: entityRenderer_ already created in initLoadingScreen()
    // loadGlobalAssets() is idempotent — use a flag to track
    if (globalAssetsLoaded_) {
        LOG_DEBUG(MOD_GRAPHICS, "Global assets already loaded, skipping");
        return true;
    }
    globalAssetsLoaded_ = true;

    // All S3D archive loading (global characters, numbered globals, equipment)
    // is handled by the background thread via DataReady_ArchiveIndex and
    // DataReady_Equipment phases, or lazily on first use. No eager file I/O here.

    // Create door manager (if not already created)
    if (!doorManager_) {
        doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
        if (constrainedTextureCache_) {
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        // If zone was already loaded before doorManager_ was created, set it now
        if (currentZone_) {
            doorManager_->setZone(currentZone_);
        }
        if (zoneBspTree_) doorManager_->setBspTree(zoneBspTree_.get());
        doorManager_->setPvsRegion(currentPvsRegion_);
        if (frustumCuller_) doorManager_->setFrustumCuller(frustumCuller_.get());
        if (zoneShader_ && zoneShader_->isAvailable()) {
            doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest(),
                                                 zoneShader_->getActiveAlphaTestRGBA());
        }
        ensureDoorTextureGuard();  // T124
    }

    // S05: skyRenderer_ and detailManager_ already created in initLoadingScreen()

    LOG_INFO(MOD_GRAPHICS, "Global assets loaded successfully");
    return true;
}

void IrrlichtRenderer::showLoadingScreen() {
    if (isLoading()) return;
    loadingScreenVisible_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen shown");
}

void IrrlichtRenderer::hideLoadingScreen() {
    if (isLoading()) return;
    loadingScreenVisible_ = false;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen hidden");

    // Force full visibility and lighting recalculation on the first gameplay frame.
    // During loading, Tier2 updates (PVS, object culling, lights) run and cache
    // camera positions. By the time loading completes, these caches are populated
    // at the camera's loading-phase position. Without resetting here, the movement
    // Force PVS recalculation on initial zone-in.
    forcePvsUpdate_ = true;

    // Request deferred governor reset so the 11+ second loading screen frame
    // doesn't poison the rolling average. requestReset() defers the actual reset
    // to the next beginFrame(), discarding this poisoned frame entirely.
    if (governor_) {
        governor_->requestReset();
        LOG_DEBUG(MOD_GRAPHICS, "Governor reset requested — will start GREEN next frame");
    }

    // Start SimulationWorker thread for ongoing runtime updates.
    // Worker was created and zone data set by loadZoneSequential() on the loading thread,
    // which also ran computeOnce() to populate the first frame's scene state.
    if (simulationWorker_ && !simulationWorker_->isRunning()) {
        simulationWorker_->start();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker thread started for runtime updates");
        registerSimulationWorkerVertexAnimData();
    } else if (!simulationWorker_) {
        // Fallback: loading thread didn't create worker (shouldn't happen)
        LOG_WARN(MOD_GRAPHICS, "SimulationWorker not created by loading thread, creating now");
        startSimulationWorkerEarly();
    }

    // Release duplicate character data from zone source. RaceModelLoader independently
    // loads _chr.s3d into its own cache, so currentZone_->characters is an unused copy.
    if (currentZone_) {
        currentZone_->clearCharacterData();
    }
}

namespace {
// T66 -- /camera mode-name parsing. Case-insensitive, spaces/hyphens/
// underscores ignored, so it accepts anything getCameraModeString() already
// produces ("Free", "Follow", "First Person") as a set-then-read round trip,
// plus compact spellings ("firstperson", "fp") for scripts that would
// rather not quote a two-word argument.
std::string NormalizeCameraModeToken(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ParseCameraModeName(const std::string& args, EQT::Graphics::IrrlichtRenderer::CameraMode& out) {
    std::string norm = NormalizeCameraModeToken(args);
    if (norm == "free") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::Free; return true; }
    if (norm == "follow") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::Follow; return true; }
    if (norm == "firstperson" || norm == "fp") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::FirstPerson; return true; }
    return false;
}
} // namespace

// D20d: Handle renderer-specific slash commands forwarded from game thread
void IrrlichtRenderer::processSlashCommand(const std::string& command) {
    // Parse command name and args
    std::string cmd, args;
    size_t spacePos = command.find(' ');
    if (spacePos != std::string::npos) {
        cmd = command.substr(0, spacePos);
        args = command.substr(spacePos + 1);
    } else {
        cmd = command;
    }

    // Helper to push chat feedback via bridge. T233: this is registry 3's
    // counterpart of EverQuest::AddChatSystemMessage() (eq_chat.cpp), which
    // pushes the SystemMessage AND echoes to the console via
    // LOG_INFO(MOD_MAIN, ...). Until T233 only the push happened here, so
    // a --script run / log grep saw nothing for renderer commands (T142).
    // Same module and shape as the game-registry echo so "[MAIN] <reply>"
    // greps work identically for both registries.
    auto chat = [this](const std::string& msg) {
        if (bridge_) {
            eqt::state::ChatMessageData data;
            data.message = msg;
            data.channelType = 0;
            data.channelName = "system";
            bridge_->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SystemMessage, std::move(data)));
        }
        LOG_INFO(MOD_MAIN, "{}", msg);
    };

    // --- Visual rendering ---
    if (cmd == "/sort") {
        toggleManualZoneDraw();
        chat(fmt::format("Front-to-back zone sorting: {}", isManualZoneDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/mergedraws") {
        toggleMergeZoneDraws();
        chat(fmt::format("Zone draw merge: {}", isMergeZoneDrawsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/screenshot") {
        // P10: smallest additive hook for the in-app A/B capture (no other
        // scriptable screenshot command existed — F12 is a raw key event,
        // not reachable from --script's slash-command tour list). Explicit
        // filename arg so a tour script can capture deterministic
        // before/after pairs at a fixed pose.
        std::string path = args.empty() ? "screenshot.png" : args;
        bool ok = saveScreenshot(path);
        chat(fmt::format("Screenshot {}: {}", path, ok ? "saved" : "FAILED"));
    } else if (cmd == "/camera") {
        // T66 -- instance 7 of "reachable only through a UI gesture, invisible
        // to automation" (refactor/deferred.md): cycleCameraMode()/
        // setCameraMode()/getCameraModeString() already existed, but the ONLY
        // producer was HA::ToggleCameraMode, a raw Ctrl+F5 key event -- a
        // scripted/piped session could never enter first person. Modelled on
        // /screenshot immediately above: same registry-3 renderer command,
        // same chat()-reply idiom, no new dispatch path invented. No-arg
        // reports the current mode (so a script can assert rather than
        // assume); a named mode sets it explicitly, round-tripping with
        // getCameraModeString()'s own output -- deliberately NOT just
        // exposing cycleCameraMode(), which would force a script to cycle
        // blind and count (T51 documented exactly that trap for /attack).
        // Cycling is additionally exposed via "cycle" since it was free.
        if (args.empty()) {
            chat(fmt::format("Camera mode: {}", getCameraModeString()));
        } else if (args == "cycle") {
            cycleCameraMode();
            chat(fmt::format("Camera mode: {}", getCameraModeString()));
        } else {
            CameraMode mode;
            if (ParseCameraModeName(args, mode)) {
                setCameraMode(mode);
                chat(fmt::format("Camera mode: {}", getCameraModeString()));
            } else {
                chat(fmt::format(
                    "Unknown camera mode '{}'. Valid: Free, Follow, \"First Person\" "
                    "(or firstperson/fp); no argument reports the current mode; "
                    "'cycle' advances Free->Follow->First Person->Free.", args));
            }
        }
    } else if (cmd == "/upload") {
        toggleGPUUploadThread();
        chat(fmt::format("GPU upload thread: {}", isGPUUploadThreadEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/portal") {
        if (args == "debug") {
            togglePortalDebugDraw();
            chat(fmt::format("Portal debug overlay: {}", isPortalDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
        } else {
            togglePortalOcclusion();
            chat(fmt::format("Portal occlusion: {}", isPortalOcclusionEnabled() ? "ENABLED" : "DISABLED"));
        }
    } else if (cmd == "/stencil") {
        toggleStencilDebugDraw();
        chat(fmt::format("Stencil debug overlay: {}", isStencilDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/plight") {
        togglePlayerLight();
        chat(fmt::format("Per-pixel player light: {}", isPlayerLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/olight") {
        toggleObjectLights();
        chat(fmt::format("Object lights: {}", isObjectLightsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/zlight") {
        toggleDirectionalLight();
        chat(fmt::format("Directional light: {}", isDirectionalLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/lightmode") {
        if (zoneShader_) {
            bool unified = !zoneShader_->unifiedLighting();
            zoneShader_->setUnifiedLighting(unified);
            chat(fmt::format("Lighting mode: {}", unified ? "UNIFIED (pointLight * aColor)" : "ADDITIVE (pointLight + base*aColor)"));
            LOG_INFO(MOD_SHADER, "Lighting mode toggled to {}", unified ? "unified" : "additive");
        }
    } else if (cmd == "/shaderdebug" || cmd == "/sd") {
        if (zoneShader_) {
            int mode = 0;
            if (!args.empty()) {
                if (args == "off") mode = 0;
                else mode = std::atoi(args.c_str());
            }
            int prevMode = zoneShader_->shaderDebug();
            zoneShader_->setShaderDebug(mode);

            // Swap materials to/from debug program
            irr::s32 debugMat = zoneShader_->getDebugMaterialType();
            if (debugMat >= 0) {
                if (mode > 0 && prevMode == 0) {
                    // Entering debug: swap all per-pixel materials to debug program
                    swapToDebugShader(debugMat);
                    LOG_INFO(MOD_SHADER, "Swapped all per-pixel materials to debug program {}", debugMat);
                } else if (mode == 0 && prevMode > 0) {
                    // Leaving debug: restore production materials
                    swapFromDebugShader(debugMat);
                    LOG_INFO(MOD_SHADER, "Restored production materials from debug program");
                }
            }

            const char* modeNames[] = {"normal", "texColor", "vColor", "LUT(r,a)", "pLight",
                                        "worldNormal", "lightDir", "alpha", "tex*vColor",
                                        "UV", "depth", "rawTexAlpha", "companionAlpha"};
            const char* name = (mode >= 0 && mode <= 12) ? modeNames[mode] : "unknown";
            chat(fmt::format("Shader debug mode: {} ({})", mode, name));
            LOG_INFO(MOD_SHADER, "Shader debug mode set to {} ({})", mode, name);
        }
    } else if (cmd == "/fire") {
        if (auto* pm = getParticleManager()) {
            pm->toggleUnifiedFire();
            chat("Fire particles toggled");
        }
    } else if (cmd == "/sky") {
        toggleSky();
        chat(fmt::format("Sky rendering: {}", isSkyEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/skytype") {
        if (!args.empty()) {
            int type = std::atoi(args.c_str());
            forceSkyType(static_cast<uint8_t>(type));
            chat(fmt::format("Sky type forced to {}", type));
        } else {
            chat("Usage: /skytype <0-255>");
        }
    }
    // --- Distance ---
    // T139: /renderdist's clipplane/viewdist and /clipdist's clip/farplane
    // aliases were only ever resolved by the game-registry Command::aliases
    // field (eq_commands_movement.cpp, now deleted) -- registry-3's fallback
    // forwards raw text and only ever matched literal names here. Added so
    // deleting the game-registry copy doesn't silently drop them (T139
    // plan's "THE TRAP"; see refactor/T139_delete_duplicate_commands.md
    // Review for the full alias audit).
    else if (cmd == "/renderdist" || cmd == "/clipplane" || cmd == "/viewdist") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            if (dist >= 50.0f && dist <= 10000.0f) {
                setRenderDistance(dist);
                chat(fmt::format("Render distance: {:.0f}", dist));
            } else {
                chat("Render distance must be between 50 and 10000");
            }
        } else {
            chat(fmt::format("Render distance: {:.0f}", getRenderDistance()));
        }
    } else if (cmd == "/clipdist" || cmd == "/clip" || cmd == "/farplane") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            setClipDistance(dist);
            chat(fmt::format("Clip distance: {:.0f}", dist));
        } else {
            chat(fmt::format("Clip distance: {:.0f}", getClipDistance()));
        }
    }
    // --- Profiling/debug ---
    // T139: frametime/ft, sp/scenebreak, ds, sw were game-registry aliases
    // (eq_commands_movement.cpp, deleted) resolved only there -- added here
    // for the same reason as the renderdist/clipdist aliases above.
    else if (cmd == "/frametiming" || cmd == "/frametime" || cmd == "/ft") {
        bool newState = !isFrameTimingEnabled();
        setFrameTimingEnabled(newState);
        if (newState) {
            chat("Frame timing profiler ENABLED - check console for breakdown every ~2 seconds");
        } else {
            chat("Frame timing profiler DISABLED");
        }
    } else if (cmd == "/sceneprofile" || cmd == "/sp" || cmd == "/scenebreak") {
        runSceneProfile();
        chat("Scene profile running - check console for breakdown");
    } else if (cmd == "/dumpscene" || cmd == "/ds") {
        dumpScene();
        chat("Scene dump written to console log");
    } else if (cmd == "/simworker" || cmd == "/sw") {
        auto info = getSimWorkerDebugInfo();
        for (const auto& line : info) chat(line);
    }
    // T108: "/pmem" moved off this empty-MemoryReportInput{} path (runLoadDiagnostics
    // had zero callers, per T105/T106) — EverQuest::runPmemDiagnostics() is now
    // called directly by the command handler; see irrlicht_bridge.cpp.
    // --- UI ---
    else if (cmd == "/newui") {
        newUIEnabled_ = !newUIEnabled_;
        chat(fmt::format("New static UI: {}", newUIEnabled_ ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/newbook") {
        spellbookState_.isOpen = !spellbookState_.isOpen;
        if (spellbookState_.isOpen) {
            inventoryState_.activePopup = PopupType::None;  // Close other popups
        }
        chat(fmt::format("New UI spellbook: {}", spellbookState_.isOpen ? "OPEN" : "CLOSED"));
    } else if (cmd == "/newbookprev") {
        if (spellbookState_.currentPage > 0) spellbookState_.currentPage--;
    } else if (cmd == "/newbooknext") {
        if (spellbookState_.currentPage < spellbookState_.pageCount() - 1) spellbookState_.currentPage++;
    } else if (cmd == "/newskills") {
        skillsPopupState_.isOpen = !skillsPopupState_.isOpen;
        if (skillsPopupState_.isOpen) {
            inventoryState_.activePopup = PopupType::None;
            spellbookState_.isOpen = false;
        }
        chat(fmt::format("New UI skills: {}", skillsPopupState_.isOpen ? "OPEN" : "CLOSED"));
    } else if (cmd == "/newinv") {
        // T294: ONE body, shared with the `I` keybind — the toggle used to
        // be written out here and IrrlichtRenderer::toggleInventory() (what
        // `I` actually reaches) was an empty stub, so the key did nothing.
        toggleInventory();
        chat(fmt::format("New UI inventory: {}",
            isInventoryOpen() ? "OPEN" : "CLOSED"));
    } else if (cmd == "/timestamp" || cmd == "/timestamps" || cmd == "/ts") {
        // T139: dead stub on both ends (confirmed T138) -- "ts" alias added
        // for consistency with the other aliases in this unit even though
        // the command itself does nothing; see Review for why the stub
        // itself was kept rather than deleted (registry-3's catch-all
        // reports "queued" regardless of whether anything is behind the
        // name, so removing this reply would only lose information, not
        // fix the outcome= mislabeling).
        chat("Timestamps command not available (WindowManager removed)");
    } else if (cmd == "/filter") {
        chat("Filter command not available (WindowManager removed)");
    } else if (cmd == "/reloadeffects" || cmd == "/particlereload" || cmd == "/reloadfx" || cmd == "/reloadparticles") {
        if (auto* pm = getParticleManager()) {
            pm->reloadSettings();
            chat("Environment effects settings reloaded");
        } else {
            chat("Particle system not available");
        }
    } else if (cmd == "/reloadweather" || cmd == "/weatherreload" || cmd == "/weather_reload") {
        if (auto* we = getWeatherEffects()) {
            if (we->reloadConfig()) {
                chat("Weather effects settings reloaded");
            } else {
                chat("Failed to reload weather settings");
            }
        } else {
            chat("Weather effects system not available");
        }
    } else if (cmd == "/spellreload" || cmd == "/reloadspells" || cmd == "/reloadspellfx") {
        // T142: moved from eq_commands_movement.cpp -- was one of the two
        // documented exceptions calling a graphics-only singleton directly
        // from the game thread (T138 found no lock protecting it against
        // this, the render thread). Now the mutation happens here, on the
        // render thread, so the race dissolves by construction.
        EQT::Graphics::Environment::SpellEffectsConfig::instance().reload();
        chat("Spell effect settings reloaded");
    } else if (cmd == "/weatherquality" || cmd == "/wq" || cmd == "/weatherpreset") {
        // T142: moved from eq_commands_movement.cpp -- see /spellreload
        // above for why (same lock-free cross-thread mutation defect,
        // same fix).
        auto& loader = EQT::Graphics::WeatherConfigLoader::instance();

        if (args.empty()) {
            // Show current preset
            auto preset = loader.getQualityPreset();
            std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);

            chat(fmt::format("Weather quality: {}", presetName));
            chat("Available: low, medium, high, ultra, custom");
        } else {
            // Set new preset
            if (loader.setQualityPreset(args)) {
                auto preset = loader.getQualityPreset();
                std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);
                chat(fmt::format("Weather quality set to: {}", presetName));
            } else {
                chat("Invalid preset. Use: low, medium, high, ultra, custom");
            }
        }
    }
    // --- Detail ---
    // T139: detaildensity/grass were game-registry aliases (eq_commands_
    // spells.cpp, deleted) resolved only there.
    else if (cmd == "/detail" || cmd == "/detaildensity" || cmd == "/grass") {
        if (auto* dm = getDetailManager()) {
            if (!args.empty()) {
                float density = std::atof(args.c_str()) / 100.0f;
                dm->setDensity(std::max(0.0f, std::min(1.0f, density)));
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            } else {
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            }
        }
    } else if (cmd == "/togglegrass") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Grass, !dm->isCategoryEnabled(DC::Grass));
            chat(fmt::format("Grass: {}", dm->isCategoryEnabled(DC::Grass) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggleplants") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Plants, !dm->isCategoryEnabled(DC::Plants));
            chat(fmt::format("Plants: {}", dm->isCategoryEnabled(DC::Plants) ? "ON" : "OFF"));
        }
    } else if (cmd == "/togglerocks") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Rocks, !dm->isCategoryEnabled(DC::Rocks));
            chat(fmt::format("Rocks: {}", dm->isCategoryEnabled(DC::Rocks) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggledebris") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Debris, !dm->isCategoryEnabled(DC::Debris));
            chat(fmt::format("Debris: {}", dm->isCategoryEnabled(DC::Debris) ? "ON" : "OFF"));
        }
    } else if (cmd == "/season") {
        if (auto* dm = getDetailManager()) {
            if (!args.empty()) {
                Detail::Season season = Detail::Season::Default;
                if (args == "snow" || args == "winter") season = Detail::Season::Snow;
                else if (args == "autumn" || args == "fall") season = Detail::Season::Autumn;
                else if (args == "desert") season = Detail::Season::Desert;
                else if (args == "swamp") season = Detail::Season::Swamp;
                dm->setSeasonOverride(season);
                chat(fmt::format("Season override: {}", args));
            } else {
                chat("Usage: /season [snow|autumn|desert|swamp|default]");
            }
        }
    } else if (cmd == "/detailinfo") {
        if (auto* dm = getDetailManager()) {
            chat(dm->getDebugInfo());
        }
    }
    // --- Governor ---
    // T139: gov/budget were game-registry aliases (eq_commands_movement.cpp,
    // deleted) resolved only there.
    else if (cmd == "/governor" || cmd == "/gov" || cmd == "/budget") {
        auto* gov = getGovernor();
        if (!gov) { chat("Governor not available"); return; }
        if (args.empty() || args == "status") {
            chat(fmt::format("Governor: {} | avg {:.1f}ms | target {:.1f}ms ({:.0f} fps) | ratio {:.2f}{}",
                gov->getStateName(), gov->getAverageFrameTimeMs(),
                gov->getTargetFrameTimeMs(), gov->getTargetFps(),
                gov->getBudgetRatio(), gov->isForced() ? " (FORCED)" : ""));
        } else if (args.substr(0, 4) == "fps ") {
            try {
                float fps = std::stof(args.substr(4));
                if (fps < 10.0f || fps > 120.0f) { chat("FPS must be between 10 and 120"); return; }
                gov->setTargetFps(fps);
                chat(fmt::format("Governor target FPS set to {:.0f} ({:.1f}ms budget)", fps, gov->getTargetFrameTimeMs()));
            } catch (...) { chat("Usage: /governor fps <10-120>"); }
        } else if (args.substr(0, 6) == "force ") {
            std::string state = args.substr(6);
            if (state == "green") { gov->forceState(EQT::Graphics::BudgetState::Green); chat("Governor forced to GREEN (all loading allowed)"); }
            else if (state == "yellow") { gov->forceState(EQT::Graphics::BudgetState::Yellow); chat("Governor forced to YELLOW (light loading only)"); }
            else if (state == "red") { gov->forceState(EQT::Graphics::BudgetState::Red); chat("Governor forced to RED (critical loading only)"); }
            else { chat("Usage: /governor force green|yellow|red"); }
        } else if (args == "auto") {
            gov->clearForcedState();
            chat("Governor set to AUTO (state determined by frame times)");
        } else {
            chat("Usage: /governor [status|fps <N>|force green|yellow|red|auto]");
        }
    } else if (cmd == "/placeholder") {
        if (getGovernor()) {
            chat("Zone placeholder is managed automatically during progressive loading");
        }
    }
    else {
        LOG_WARN(MOD_GRAPHICS, "Unknown renderer command: {}", command);
    }
}

void IrrlichtRenderer::shutdown() {
    // P10b: unregister before constrainedTextureCache_ (owned by Application,
    // outlives IrrlichtRenderer in the normal shutdown order, but don't rely
    // on that) can call into a soon-to-be-destroyed regionMeshTextureGuard_.
    if (constrainedTextureCache_ && regionMeshTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(regionMeshTextureGuard_.get());
    }
    regionMeshTextureGuard_.reset();
    if (constrainedTextureCache_ && softwareCursorTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(softwareCursorTextureGuard_.get());
    }
    softwareCursorTextureGuard_.reset();
    // P17: same unregister-before-destroy discipline for the entity-mesh guard.
    if (constrainedTextureCache_ && entityMeshTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(entityMeshTextureGuard_.get());
    }
    entityMeshTextureGuard_.reset();
    unregisterRemainingT124Guards();  // T124, see Review

    // Stop GPU upload thread before any other cleanup (must stop before EGL context destroyed)
#ifdef EQT_HAS_GLES2
    if (gpuUploadThread_) {
        gpuUploadThread_->stop();
        gpuUploadThread_.reset();
    }
#endif

    // Stop simulation worker before any other cleanup
    stopSimulationWorker();

    // Stop background entity prep worker before any other cleanup
    if (entityPrepWorker_) {
        entityPrepWorker_->stop();
        entityPrepWorker_.reset();
    }

    unloadZone();

    // Shutdown shared thread pool (all queue users already stopped above)
    if (backgroundThreadPool_) {
        backgroundThreadPool_->shutdown();
        backgroundThreadPool_.reset();
    }

    // Reset all managers that hold Irrlicht resources BEFORE dropping the device
    // Their destructors may try to remove scene nodes which requires a valid device
    entityRenderer_.reset();
    cameraController_.reset();
    doorManager_.reset();
    skyRenderer_.reset();
    animatedTextureManager_.reset();
    eventReceiver_.reset();

    // Weather and environment managers also need the device for cleanup
    weatherEffects_.reset();
    particleManager_.reset();
    boidsManager_.reset();
    tumbleweedManager_.reset();
    weatherSystem_.reset();
    // treeWindController_/treeIdentifier_ are plain members (P32) — no
    // reset needed, they hold no Irrlicht scene-graph resources.
    detailManager_.reset();
    // constrainedTextureCache_ is owned by Application, not reset here
    spellVisualFX_.reset();

    if (device_) {
        device_->drop();
        device_ = nullptr;
    }

    driver_ = nullptr;
    smgr_ = nullptr;
    guienv_ = nullptr;
    camera_ = nullptr;
    hudText_ = nullptr;
    hotkeysText_ = nullptr;
    initialized_ = false;
    loadingScreenVisible_ = true;

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer shutdown");
}

bool IrrlichtRenderer::isRunning() const {
    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking initialized_={}", initialized_);
    if (!initialized_) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking device_={}", (device_ ? "valid" : "null"));
    if (!device_) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: calling device_->run()...");
    bool deviceRunning = device_->run();
    LOG_TRACE(MOD_GRAPHICS, "isRunning: device_->run() returned {}", deviceRunning);
    if (!deviceRunning) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking eventReceiver_...");
    bool quit = eventReceiver_->quitRequested();
    LOG_TRACE(MOD_GRAPHICS, "isRunning: quitRequested={}", quit);

    return !quit;
}

void IrrlichtRenderer::requestQuit() {
    if (eventReceiver_) {
        eventReceiver_->setQuitRequested(true);
    }
}

void IrrlichtRenderer::createSoftwareCursor() {
    if (!driver_ || !config_.constrainedConfig.useDRM) return;

    // Load pre-generated cursor texture via constrained cache
    std::string cursorPath = "data/textures/software_cursor.bmp";
    std::ifstream cursorFile(cursorPath, std::ios::binary | std::ios::ate);
    if (!cursorFile) {
        LOG_FATAL(MOD_GRAPHICS, "Required texture {} not found — run generate_textures first", cursorPath);
        std::exit(1);
    }
    std::streamsize cursorFileSize = cursorFile.tellg();
    cursorFile.seekg(0, std::ios::beg);
    std::vector<char> cursorData(cursorFileSize);
    cursorFile.read(cursorData.data(), cursorFileSize);

    softwareCursorTexture_ = constrainedTextureCache_->getOrLoad("softwareCursor", cursorData);
    if (!softwareCursorTexture_) softwareCursorTexture_ = constrainedTextureCache_->uploadTexture("softwareCursor");
    if (!softwareCursorTexture_) {
        LOG_FATAL(MOD_GRAPHICS, "Failed to load required texture: {}", cursorPath);
        std::exit(1);
    }

    LOG_INFO(MOD_GRAPHICS, "Software cursor texture loaded for DRM mode");
}

void IrrlichtRenderer::setupCamera() {
    camera_ = smgr_->addCameraSceneNode(
        nullptr,
        irr::core::vector3df(0, 100, 0),
        irr::core::vector3df(100, 0, 100),
        -1
    );

    camera_->setFarValue(SKY_FAR_PLANE);
    camera_->setNearValue(1.0f);
    LOG_INFO(MOD_GRAPHICS, "Camera far plane: {}, render distance: {}", SKY_FAR_PLANE, renderDistance_);

    cameraController_ = std::make_unique<CameraController>(camera_);
    cameraController_->setMoveSpeed(500.0f);
    cameraController_->setMouseSensitivity(0.2f);

    frustumCuller_ = std::make_unique<FrustumCuller>();

    // Create software occlusion culler if configured (non-zero buffer dimensions)
    if (config_.constrainedConfig.occlusionBufferWidth > 0 &&
        config_.constrainedConfig.occlusionBufferHeight > 0) {
        OcclusionCullerConfig occConfig;
        occConfig.width = config_.constrainedConfig.occlusionBufferWidth;
        occConfig.height = config_.constrainedConfig.occlusionBufferHeight;
        occConfig.maxOccluderRegions = config_.constrainedConfig.occlusionMaxOccluderRegions;
        occlusionCuller_ = std::make_unique<SoftwareOcclusionCuller>(occConfig);
        LOG_INFO(MOD_GRAPHICS, "Software occlusion culler enabled: {}x{} depth buffer, max {} occluder regions",
            occConfig.width, occConfig.height, occConfig.maxOccluderRegions);
    }
}

void IrrlichtRenderer::setupLighting() {
    // Start in dark mode (lighting ON, zone lights OFF)
    // Only object lights will illuminate the scene
    smgr_->setAmbientLight(irr::video::SColorf(0.005f, 0.005f, 0.008f, 1.0f));

    // Add a directional light (sun) - store reference for time of day updates
    sunLight_ = smgr_->addLightSceneNode(
        nullptr,
        irr::core::vector3df(0, 1000, 0),
        irr::video::SColorf(1.0f, 1.0f, 0.9f, 1.0f),
        10000.0f
    );

    if (sunLight_) {
        irr::video::SLight& lightData = sunLight_->getLightData();
        lightData.Type = irr::video::ELT_DIRECTIONAL;
        lightData.Direction = irr::core::vector3df(0.5f, -1.0f, 0.5f);
        // Start with sun hidden (dark mode - only object lights)
        sunLight_->setVisible(false);
    }
}

void IrrlichtRenderer::updateTimeOfDay(uint8_t hour, uint8_t minute) {
    if (isLoading()) return;
    if (!smgr_) return;

    currentHour_ = hour;
    currentMinute_ = minute;

    // Don't update ambient/sun if we're in dark mode (zone lights OFF)
    // In dark mode, only object lights illuminate the scene
    if (lightingEnabled_ && !zoneLightsEnabled_) {
        return;
    }

    // Calculate ambient light based on hour
    // EQ time: 0-4 night, 5-6 dawn, 7-17 day, 18-19 dusk, 20-23 night
    float r, g, b;
    float sunIntensity = 1.0f;

    if (hour >= 7 && hour <= 17) {
        // Day - bright white ambient
        r = 0.5f; g = 0.5f; b = 0.5f;
        sunIntensity = 1.0f;
    } else if (hour >= 20 || hour <= 4) {
        // Night - dark blue ambient
        r = 0.08f; g = 0.08f; b = 0.15f;
        sunIntensity = 0.1f;
    } else if (hour >= 5 && hour <= 6) {
        // Dawn - transition from night to day (orange tint)
        float t = (hour - 5) + minute / 60.0f;  // 0-2 range
        t /= 2.0f;  // normalize to 0-1
        r = 0.08f + t * (0.5f - 0.08f);
        g = 0.08f + t * (0.4f - 0.08f);
        b = 0.15f + t * (0.35f - 0.15f);
        sunIntensity = 0.1f + t * 0.9f;
    } else {
        // Dusk (18-19) - transition from day to night (orange tint)
        float t = (hour - 18) + minute / 60.0f;  // 0-2 range
        t /= 2.0f;  // normalize to 0-1
        r = 0.5f - t * (0.5f - 0.08f);
        g = 0.4f - t * (0.4f - 0.08f);
        b = 0.35f - t * (0.35f - 0.15f);
        sunIntensity = 1.0f - t * 0.9f;
    }

    // Apply user-adjustable ambient multiplier (Page Up/Down)
    r = std::min(1.0f, r * ambientMultiplier_);
    g = std::min(1.0f, g * ambientMultiplier_);
    b = std::min(1.0f, b * ambientMultiplier_);

    // Apply weather effects (darkening for storms)
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        float weatherMod = weatherEffects_->getAmbientLightModifier();
        static float lastLoggedMod = -1.0f;
        if (std::abs(weatherMod - lastLoggedMod) > 0.01f && weatherMod < 0.99f) {
            LOG_DEBUG(MOD_GRAPHICS, "updateTimeOfDay: weatherMod={:.3f}, applying to ambient r={:.3f} g={:.3f} b={:.3f}",
                      weatherMod, r, g, b);
            lastLoggedMod = weatherMod;
        }
        r *= weatherMod;
        g *= weatherMod;
        b *= weatherMod;
        sunIntensity *= weatherMod;
    }

    smgr_->setAmbientLight(irr::video::SColorf(r, g, b, 1.0f));

    // Update sun light intensity
    if (sunLight_) {
        irr::video::SLight& lightData = sunLight_->getLightData();
        lightData.DiffuseColor = irr::video::SColorf(sunIntensity, sunIntensity, sunIntensity * 0.9f, 1.0f);
    }

    // Update sky celestial body positions and colors
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->updateTimeOfDay(hour, minute);

        // Update fog color to match sky if fog is enabled
        if (fogEnabled_ && driver_ && skyRenderer_->isEnabled()) {
            irr::video::SColor fogColor = skyRenderer_->getRecommendedFogColor();

            // Get current fog settings to preserve distances
            irr::video::SColor currentFogColor;
            irr::video::E_FOG_TYPE fogType;
            irr::f32 fogStart, fogEnd, fogDensity;
            bool pixelFog, rangeFog;
            driver_->getFog(currentFogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);

            // Apply weather effects to fog
            if (weatherEffects_ && weatherEffects_->isEnabled()) {
                // Blend fog color with weather fog color
                irr::video::SColor weatherFog = weatherEffects_->getWeatherFogColor();
                float weatherMod = weatherEffects_->getAmbientLightModifier();
                float blendFactor = 1.0f - weatherMod;  // More blending when darker

                fogColor.setRed(static_cast<uint8_t>(
                    fogColor.getRed() * (1.0f - blendFactor) + weatherFog.getRed() * blendFactor));
                fogColor.setGreen(static_cast<uint8_t>(
                    fogColor.getGreen() * (1.0f - blendFactor) + weatherFog.getGreen() * blendFactor));
                fogColor.setBlue(static_cast<uint8_t>(
                    fogColor.getBlue() * (1.0f - blendFactor) + weatherFog.getBlue() * blendFactor));

                // Apply fog density modifier (brings fog closer during storms)
                float densityMod = weatherEffects_->getFogDensityModifier();
                fogEnd /= densityMod;

                // Apply rain overlay fog reduction (original EQ behavior - heavy rain = short fog)
                float rainFogStart, rainFogEnd;
                if (weatherEffects_->getRainFogSettings(rainFogStart, rainFogEnd)) {
                    // Rain fog completely overrides normal fog distances
                    fogStart = rainFogStart;
                    fogEnd = rainFogEnd;
                }
            }

            // Only update fog color if we have valid fog distances
            if (fogEnd > fogStart && fogEnd > 0) {
                driver_->setFog(fogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);
            }
        }
    }

    // Update zone and object lights with weather modifier (only when weather is active)
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        float weatherMod = weatherEffects_->getAmbientLightModifier();
        static float lastWeatherMod = 1.0f;
        // Only update light colors when weather modifier changes significantly
        if (std::abs(weatherMod - lastWeatherMod) > 0.005f) {
            // Zone light colors now handled by SimulationWorker (vision/weather applied there)
            updateObjectLightColors();
            lastWeatherMod = weatherMod;
        }

        // Update sky brightness based on rain intensity
        // Intensity 1: 50%, Intensity 5: 25%, Intensity 10: ~10% (like night)
        // Formula: brightness = 0.5 * pow(0.5, (intensity - 1) / 4)
        if (skyRenderer_ && weatherEffects_->isRaining()) {
            uint8_t intensity = weatherEffects_->getCurrentIntensity();
            if (intensity > 0) {
                float skyBrightness = 0.5f * std::pow(0.5f, static_cast<float>(intensity - 1) / 4.0f);
                skyRenderer_->setWeatherBrightness(skyBrightness);
            } else {
                skyRenderer_->setWeatherBrightness(1.0f);
            }
        } else if (skyRenderer_) {
            skyRenderer_->setWeatherBrightness(1.0f);
        }
    } else if (skyRenderer_) {
        // No weather active, restore full sky brightness
        skyRenderer_->setWeatherBrightness(1.0f);
    }

    // Update shader uniforms with current lighting state
    if (zoneShader_ && zoneShader_->isAvailable()) {
        if (!debugDirectionalLightEnabled_) {
            // Fullbright: override ambient/sun/tint for debug isolation
            zoneShader_->setAmbientColor(1.0f, 1.0f, 1.0f);
            zoneShader_->setSunColor(0.0f, 0.0f, 0.0f);
            zoneShader_->setTintColor(1.0f, 1.0f, 1.0f);
        } else {
            zoneShader_->setAmbientColor(r, g, b);
            zoneShader_->setSunColor(sunIntensity, sunIntensity, sunIntensity * 0.9f);

            // Sun direction matches setupLighting() directional light
            zoneShader_->setSunDirection(0.5f, -1.0f, 0.5f);

            // Compute day/night tint color from ambient values
            // Dawn/dusk: warm orange; night: cool blue; day: neutral white
            float maxAmb = std::max({r, g, b, 0.01f});
            zoneShader_->setTintColor(r / maxAmb, g / maxAmb, b / maxAmb);
        }

        // Also sync fog with any weather-modified values
        irr::video::SColor currentFogColor;
        irr::video::E_FOG_TYPE fogType;
        irr::f32 fogStart, fogEnd, fogDensity;
        bool pixelFog, rangeFog;
        driver_->getFog(currentFogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);
        zoneShader_->setFog(fogStart, fogEnd,
                            currentFogColor.getRed() / 255.0f,
                            currentFogColor.getGreen() / 255.0f,
                            currentFogColor.getBlue() / 255.0f,
                            currentFogColor.getAlpha() / 255.0f);
    }
}

bool IrrlichtRenderer::isRegionPvsVisible(size_t regionIdx) const {
    if (!usePvsCulling_ || regionIdx == SIZE_MAX || currentPvsRegion_ == SIZE_MAX)
        return true;  // No PVS data — assume visible
    if (!zoneBspTree_ || currentPvsRegion_ >= zoneBspTree_->regions.size())
        return true;
    auto& camRegion = zoneBspTree_->regions[currentPvsRegion_];
    if (!camRegion || camRegion->visibleRegions.empty())
        return true;
    if (regionIdx >= camRegion->visibleRegions.size())
        return true;
    return camRegion->visibleRegions[regionIdx];
}

// Dead code removed: updateObjectVisibility(), updateZoneLightVisibility(), updateObjectLights()
// These are now handled exclusively by SimulationWorker.

void IrrlichtRenderer::setupFog() {
    if (!driver_) {
        return;
    }

    // Unified fog system:
    // - fogEnd = renderDistance_ (absolute visibility limit)
    // - fogStart = renderDistance_ - fogThickness_ (where fade begins)
    // - Everything beyond renderDistance_ is culled, nothing renders there
    // - Fog creates a smooth fade zone at the edge
    float fogEnd = renderDistance_;
    float fogStart = renderDistance_ - fogThickness_;
    fogStart = std::max(0.0f, fogStart);  // Ensure non-negative

    // Get fog color from sky renderer if available, otherwise use default
    irr::video::SColor fogColor(255, 128, 128, 160);  // Default: light gray-blue
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        fogColor = skyRenderer_->getRecommendedFogColor();
    }

    driver_->setFog(
        fogColor,
        irr::video::EFT_FOG_LINEAR,
        fogStart,
        fogEnd,
        0.01f,
        true,   // Pixel fog
        false   // Range fog
    );

    // Update shader fog uniforms
    if (zoneShader_ && zoneShader_->isAvailable()) {
        zoneShader_->setFog(fogStart, fogEnd,
                            fogColor.getRed() / 255.0f,
                            fogColor.getGreen() / 255.0f,
                            fogColor.getBlue() / 255.0f,
                            fogColor.getAlpha() / 255.0f);
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: start={:.0f}, end={:.0f} (renderDistance={:.0f}, fogThickness={:.0f})",
        fogStart, fogEnd, renderDistance_, fogThickness_);
}

void IrrlichtRenderer::drawLoadingScreen(float progress, const std::wstring& stageText) {
    if (!driver_ || !device_) return;

    // Debug log loading progress (when debug level > 0)
    float clampedProgressForLog = std::max(0.0f, std::min(1.0f, progress));
    int percentComplete = static_cast<int>(clampedProgressForLog * 100);

    // Convert wide strings to narrow strings for logging
    std::string stageTextNarrow(stageText.begin(), stageText.end());
    std::string titleNarrow(loadingTitle_.begin(), loadingTitle_.end());

    LOG_DEBUG(MOD_GRAPHICS_LOAD, "[Loading] {} - {} ({}%)", titleNarrow, stageTextNarrow, percentComplete);

    // Immediately render a frame showing loading progress
    driver_->beginScene(true, true, irr::video::SColor(255, 20, 20, 40));

    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

    // Progress bar dimensions
    const int barWidth = 400;
    const int barHeight = 30;
    const int barX = (screenSize.Width - barWidth) / 2;
    const int barY = (screenSize.Height / 2) + 20;

    // Colors
    irr::video::SColor bgColor(255, 40, 40, 60);
    irr::video::SColor borderColor(255, 100, 100, 120);
    irr::video::SColor fillColor(255, 80, 120, 200);

    // Draw border
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(barX - 2, barY - 2, barX + barWidth + 2, barY + barHeight + 2));

    // Draw background
    driver_->draw2DRectangle(bgColor,
        irr::core::recti(barX, barY, barX + barWidth, barY + barHeight));

    // Draw progress fill
    float clampedProgress = std::max(0.0f, std::min(1.0f, progress));
    int fillWidth = static_cast<int>(barWidth * clampedProgress);
    if (fillWidth > 0) {
        driver_->draw2DRectangle(fillColor,
            irr::core::recti(barX, barY, barX + fillWidth, barY + barHeight));
    }

    // Draw text using built-in font
    irr::gui::IGUIFont* font = guienv_ ? guienv_->getBuiltInFont() : nullptr;
    if (font) {
        // Dynamic title (e.g., "EverQuest", "Connecting...", "Loading Zone...")
        irr::core::dimension2d<irr::u32> titleSize = font->getDimension(loadingTitle_.c_str());
        int titleX = (screenSize.Width - titleSize.Width) / 2;
        int titleY = barY - 40;
        font->draw(loadingTitle_.c_str(),
            irr::core::recti(titleX, titleY, titleX + titleSize.Width, titleY + titleSize.Height),
            irr::video::SColor(255, 255, 255, 255));

        // Stage text below progress bar
        irr::core::dimension2d<irr::u32> stageSize = font->getDimension(stageText.c_str());
        int stageX = (screenSize.Width - stageSize.Width) / 2;
        int stageY = barY + barHeight + 10;
        font->draw(stageText.c_str(),
            irr::core::recti(stageX, stageY, stageX + stageSize.Width, stageY + stageSize.Height),
            irr::video::SColor(255, 200, 200, 200));

        // Percentage text centered in progress bar
        std::wstring pctText = std::to_wstring(static_cast<int>(clampedProgress * 100)) + L"%";
        irr::core::dimension2d<irr::u32> pctSize = font->getDimension(pctText.c_str());
        int pctX = (screenSize.Width - pctSize.Width) / 2;
        int pctY = barY + (barHeight - pctSize.Height) / 2;
        font->draw(pctText.c_str(),
            irr::core::recti(pctX, pctY, pctX + pctSize.Width, pctY + pctSize.Height),
            irr::video::SColor(255, 255, 255, 255));
    }

    driver_->endScene();
}

void IrrlichtRenderer::applyEnvironmentalDisplaySettings() {
    const auto& settings = cachedDisplaySettings_;
    bool zoneLoaded = !currentZoneName_.empty();

    // --- Particle Manager: toggle atmospheric billboard particles ---
    // ParticleManager is always created (in loadGlobalAssets) for unified fire support.
    // This toggle controls only the atmospheric billboard emitters (dust, pollen, etc.)
    if (particleManager_) {
        particleManager_->setEnabled(settings.atmosphericParticles);
    }

    // Lazily create weather effects if not yet created and particles are available
    if (!weatherEffects_ && particleManager_) {
        LOG_TRACE(MOD_GRAPHICS, "Lazy-creating weather effects controller...");
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_TRACE(MOD_GRAPHICS, "Lazy-initializing weather effects (loading textures)...");
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller (lazy)");
        }
        if (weatherSystem_) {
            weatherSystem_->addListener(weatherEffects_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects created (toggled on via settings)");
    }

    if (particleManager_) {
        Environment::EffectQuality quality;
        switch (settings.environmentQuality) {
            case eqt::ui::EffectQuality::Off:
                quality = Environment::EffectQuality::Off;
                break;
            case eqt::ui::EffectQuality::Low:
                quality = Environment::EffectQuality::Low;
                break;
            case eqt::ui::EffectQuality::Medium:
                quality = Environment::EffectQuality::Medium;
                break;
            case eqt::ui::EffectQuality::High:
                quality = Environment::EffectQuality::High;
                break;
            default:
                quality = Environment::EffectQuality::Medium;
                break;
        }

        particleManager_->setQuality(quality);
        particleManager_->setEnabled(settings.atmosphericParticles);
        particleManager_->setDensity(settings.environmentDensity);
        particleManager_->setTypeEnabled(Environment::ParticleType::Ember, settings.fireEffects);
        particleManager_->setTypeEnabled(Environment::ParticleType::Smoke, settings.fireEffects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied particle settings: quality={}, enabled={}, density={}, fire={}",
                 static_cast<int>(quality), settings.atmosphericParticles, settings.environmentDensity, settings.fireEffects);
    }

    // Fire effects (light flickering)
    fireEffectsEnabled_ = settings.fireEffects;

    // --- Boids Manager: lazy create if toggled on ---
    if (settings.ambientCreatures && !boidsManager_ && smgr_ && driver_) {
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        if (constrainedTextureCache_) boidsManager_->setConstrainedTextureCache(constrainedTextureCache_);  // T124
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Boids manager created (toggled on via settings)");
        }
        ensureBoidsTextureGuard();
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            if (zoneBoundsValid_) {
                boidsManager_->onZoneEnter(currentZoneName_, biome,
                    glm::vec3(zoneBoundsMinX_, zoneBoundsMinY_, -1000.0f),
                    glm::vec3(zoneBoundsMaxX_, zoneBoundsMaxY_, 1000.0f));
            } else {
                boidsManager_->onZoneEnter(currentZoneName_, biome);
            }
        }
    }

    if (boidsManager_) {
        int quality = static_cast<int>(settings.environmentQuality);
        boidsManager_->setQuality(quality);
        boidsManager_->setEnabled(settings.ambientCreatures);
        boidsManager_->setDensity(settings.environmentDensity);

        LOG_DEBUG(MOD_GRAPHICS, "Applied boids settings: quality={}, enabled={}, density={}",
                 quality, settings.ambientCreatures, settings.environmentDensity);
    }

    // --- Detail Manager: lazy create if toggled on ---
    if (settings.detailObjectsEnabled && !detailManager_ && smgr_ && driver_) {
        detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
        detailManager_->setSurfaceMapsPath("data/detail/zones");
        if (constrainedTextureCache_) {
            detailManager_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_INFO(MOD_GRAPHICS, "Detail manager created (toggled on via settings)");

    }

    if (detailManager_) {
        detailManager_->setEnabled(settings.detailObjectsEnabled);
        detailManager_->setDensity(settings.detailDensity);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Grass, settings.detailGrass);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Plants, settings.detailPlants);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Rocks, settings.detailRocks);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Debris, settings.detailDebris);

        auto foliageConfig = detailManager_->getFoliageDisturbanceConfig();
        if (foliageConfig.enabled != settings.reactiveFoliage) {
            foliageConfig.enabled = settings.reactiveFoliage;
            detailManager_->setFoliageDisturbanceConfig(foliageConfig);
        }

        LOG_DEBUG(MOD_GRAPHICS, "Applied detail settings: enabled={}, density={:.2f}, grass={}, plants={}, rocks={}, debris={}, reactiveFoliage={}",
                 settings.detailObjectsEnabled, settings.detailDensity,
                 settings.detailGrass, settings.detailPlants, settings.detailRocks, settings.detailDebris,
                 settings.reactiveFoliage);
    }

    // --- Tumbleweed Manager: lazy create if toggled on ---
    if (settings.rollingObjects && !tumbleweedManager_ && smgr_ && driver_) {
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager created (toggled on via settings)");
        }
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            tumbleweedManager_->onZoneEnter(currentZoneName_, biome);
        }
    }

    if (tumbleweedManager_) {
        tumbleweedManager_->setEnabled(settings.rollingObjects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied tumbleweed settings: enabled={}",
                 settings.rollingObjects);
    }

    // --- Sky Renderer: toggle based on setting + indoor zone state ---
    if (skyRenderer_) {
        bool skyAllowed = !isIndoorZone_ && settings.skyEnabled;
        skyRenderer_->setEnabled(skyAllowed);
        LOG_DEBUG(MOD_GRAPHICS, "Applied sky settings: enabled={} (setting={}, indoor={})",
                 skyAllowed, settings.skyEnabled, isIndoorZone_);
    }

    // --- Tree Wind Controller: toggle wind animation ---
    treeWindController_.setEnabled(settings.animatedTrees);
    LOG_DEBUG(MOD_GRAPHICS, "Applied tree animation settings: enabled={}", settings.animatedTrees);

    // --- Fire Glow Lighting + Icospheres ---
    // fireEffects is the master gate for all fire-based effects
    fireGlowLightingEnabled_ = settings.fireEffects && settings.enableFireGlowLighting;
    fireGlowIcospheresEnabled_ = settings.fireEffects && settings.enableFireGlowIcospheres;
    maxFireGlowLights_ = settings.maxFireGlowLights;
    if (!fireGlowLightingEnabled_ && zoneShader_) {
        zoneShader_->clearFireGlowLights();
    }
}

void IrrlichtRenderer::setupHUD() {
    if (!guienv_) return;

    // Main HUD in upper left - made taller to show player + target info
    hudText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(10, 10, 450, 500),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (hudText_) {
        hudText_->setOverrideColor(irr::video::SColor(255, 255, 255, 255));
    }

    // Hotkey hints in upper right
    int screenWidth = config_.width;
    hotkeysText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(screenWidth - 400, 10, screenWidth - 10, 80),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (hotkeysText_) {
        hotkeysText_->setOverrideColor(irr::video::SColor(255, 200, 200, 200));  // Slightly dimmer
        hotkeysText_->setTextAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT);
    }

    // Heading debug info centered at top (for Player mode)
    int centerX = screenWidth / 2;
    headingDebugText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(centerX - 175, 10, centerX + 175, 150),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (headingDebugText_) {
        headingDebugText_->setOverrideColor(irr::video::SColor(255, 255, 255, 0));  // Yellow for visibility
        headingDebugText_->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT);
    }
}

void IrrlichtRenderer::updateHUD() {
    if (!hudText_ || !hudEnabled_) return;

    // Performance optimization: check if HUD state has changed
    // Build current state snapshot for comparison
    HudCachedState currentState;
    currentState.fps = currentFps_;
    currentState.playerX = static_cast<int>(playerX_);
    currentState.playerY = static_cast<int>(playerY_);
    currentState.playerZ = static_cast<int>(playerZ_);
    if (entityRenderer_) {
        currentState.entityCount = entityRenderer_->getEntityCount();
        currentState.modeledEntityCount = entityRenderer_->getModeledEntityCount();
    }
    currentState.targetId = currentTargetId_;
    currentState.targetHpPercent = currentTargetHpPercent_;
    currentState.wireframeMode = wireframeMode_;
    currentState.oldModels = isUsingOldModels();
    currentState.cameraMode = getCameraModeString();
    currentState.zoneName = currentZoneName_;

    // Check if state has changed
    bool stateChanged = (currentState.fps != hudCachedState_.fps ||
                         currentState.playerX != hudCachedState_.playerX ||
                         currentState.playerY != hudCachedState_.playerY ||
                         currentState.playerZ != hudCachedState_.playerZ ||
                         currentState.entityCount != hudCachedState_.entityCount ||
                         currentState.modeledEntityCount != hudCachedState_.modeledEntityCount ||
                         currentState.targetId != hudCachedState_.targetId ||
                         currentState.targetHpPercent != hudCachedState_.targetHpPercent ||
                         currentState.wireframeMode != hudCachedState_.wireframeMode ||
                         currentState.oldModels != hudCachedState_.oldModels ||
                         currentState.cameraMode != hudCachedState_.cameraMode ||
                         currentState.zoneName != hudCachedState_.zoneName);

    // Skip rebuild if nothing changed
    if (!stateChanged) {
        return;
    }

    // Update cached state
    hudCachedState_ = currentState;

    std::wstringstream text;
    std::wstringstream hotkeys;

    // Heading debug text (right side, used in Player mode)
    std::wstringstream headingDebug;

    {
        // === PLAYER MODE HUD ===
        // Show heading debug info for current target (on right side)
        if (currentTargetId_ != 0 && entityRenderer_) {
            const auto& entities = entityRenderer_->getEntities();
            auto it = entities.find(currentTargetId_);
            if (it != entities.end()) {
                const EntityVisual& visual = it->second;
                headingDebug << L"--- TARGET HEADING DEBUG ---\n";
                // Entity position (EQ coords: x, y, z)
                headingDebug << L"Pos: (" << static_cast<int>(visual.serverX)
                     << L", " << static_cast<int>(visual.serverY)
                     << L", " << static_cast<int>(visual.serverZ) << L")\n";
                // Server heading (from entity data, degrees 0-360)
                wchar_t hdgBuf[64];
                swprintf(hdgBuf, 64, L"Server Heading: %.1f deg\n", visual.serverHeading);
                headingDebug << hdgBuf;
                // Model rotation (from Irrlicht scene node)
                if (visual.sceneNode) {
                    irr::core::vector3df rot = visual.sceneNode->getRotation();
                    swprintf(hdgBuf, 64, L"Model Rotation: (%.1f, %.1f, %.1f)\n",
                             rot.X, rot.Y, rot.Z);
                    headingDebug << hdgBuf;
                }
                // Interpolated heading (visual.lastHeading)
                swprintf(hdgBuf, 64, L"Interp Heading: %.1f deg\n", visual.lastHeading);
                headingDebug << hdgBuf;
            }
        }
    }

    hudText_->setText(text.str().c_str());
    if (hotkeysText_) {
        hotkeysText_->setText(hotkeys.str().c_str());
    }
    if (headingDebugText_) {
        headingDebugText_->setText(headingDebug.str().c_str());
    }
}


void IrrlichtRenderer::toggleSky() {
    if (isLoading()) return;
    if (skyRenderer_) {
        bool newState = !skyRenderer_->isEnabled();
        skyRenderer_->setEnabled(newState);
        LOG_INFO(MOD_GRAPHICS, "Sky rendering: {}", newState ? "ON" : "OFF");
    }
}

void IrrlichtRenderer::forceSkyType(uint8_t skyTypeId) {
    if (isLoading()) return;
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->setSkyType(skyTypeId, currentZoneName_);
        LOG_INFO(MOD_GRAPHICS, "Forced sky type to {}", skyTypeId);
    }
}

bool IrrlichtRenderer::isSkyEnabled() const {
    return skyRenderer_ && skyRenderer_->isEnabled();
}

std::string IrrlichtRenderer::getSkyDebugInfo() const {
    if (!skyRenderer_ || !skyRenderer_->isInitialized()) {
        return "Sky: Not initialized";
    }

    std::string info = "Sky: ";
    if (!skyRenderer_->isEnabled()) {
        info += "OFF";
    } else {
        info += fmt::format("Type {} ", skyRenderer_->getCurrentSkyType());

        // Get sky color info
        auto colors = skyRenderer_->getCurrentSkyColors();
        info += fmt::format("Bright:{:.0f}% ", colors.cloudBrightness * 100);
    }

    return info;
}


void IrrlichtRenderer::createEntityRenderer() {
    // S05: Called once from initLoadingScreen(). No lazy-init guard needed.
    entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
    entityRenderer_->setClientPath(config_.eqClientPath);
    entityRenderer_->setNameTagsVisible(config_.constrainedConfig.nameTagsEnabled);
    entityRenderer_->setRenderDistance(renderDistance_);
    if (constrainedTextureCache_) {
        entityRenderer_->setConstrainedTextureCache(constrainedTextureCache_);
        // Wire mesh builder into cache for entity texture registration during uploadTexture
        auto* meshBuilder = entityRenderer_->getMeshBuilder();
        if (meshBuilder)
            constrainedTextureCache_->setMeshBuilder(meshBuilder);
    }
    entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
    entityRenderer_->setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload);
    if (config_.constrainedConfig.enableCompositeEntities && !config_.constrainedConfig.compositeEntityPath.empty()) {
        entityRenderer_->setCompositeEntityPath(config_.constrainedConfig.compositeEntityPath);
    }
    if (zoneShader_ && zoneShader_->isAvailable()) {
        entityRenderer_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                zoneShader_->getActiveAlphaTest(),
                                                zoneShader_->getActiveAlphaTestRGBA());
    }
    if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
    }
    entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
        return this->findGroundZ(x, y, currentZ);
    });
    if (zoneBspTree_) {
        entityRenderer_->setBspTree(zoneBspTree_);
    }
    if (frustumCuller_) {
        entityRenderer_->setFrustumCuller(frustumCuller_.get());
    }
}







bool IrrlichtRenderer::createDoor(uint8_t doorId, const std::string& name,
                                   float x, float y, float z, float heading,
                                   uint32_t incline, uint16_t size, uint8_t opentype,
                                   bool initiallyOpen) {
    if (doorManager_) {
        return doorManager_->createDoor(doorId, name, x, y, z, heading, incline, size, opentype, initiallyOpen);
    }
    return false;
}

bool IrrlichtRenderer::registerDoor(uint8_t doorId, const std::string& name,
                                     float x, float y, float z, float heading,
                                     uint32_t incline, uint16_t size, uint8_t opentype,
                                     bool initiallyOpen) {
    if (doorManager_) {
        return doorManager_->registerDoor(doorId, name, x, y, z, heading, incline, size, opentype, initiallyOpen);
    }
    return false;
}

void IrrlichtRenderer::setDoorState(uint8_t doorId, bool open, bool userInitiated) {
    if (isLoading()) return;
    if (doorManager_) {
        doorManager_->setDoorState(doorId, open, userInitiated);
    }
}

void IrrlichtRenderer::clearDoors() {
    if (isLoading()) return;
    if (doorManager_) {
        doorManager_->clearDoors();
    }
}

// ============================================================================
// World Object Management (for tradeskill container click detection)
// ============================================================================

void IrrlichtRenderer::addWorldObject(uint32_t dropId, float x, float y, float z,
                                       uint32_t objectType, const std::string& name) {
    if (isLoading()) return;
    WorldObjectVisual obj;
    obj.dropId = dropId;
    obj.x = x;
    obj.y = y;
    obj.z = z;
    obj.objectType = objectType;
    obj.name = name;

    // Create bounding box for click detection
    // EQ coords: x, y are horizontal, z is vertical
    // Irrlicht coords: x, z are horizontal, y is vertical
    // Transform: EQ (x, y, z) -> Irrlicht (x, z, y)
    float irrX = x;
    float irrY = z;  // EQ z -> Irrlicht y
    float irrZ = y;  // EQ y -> Irrlicht z

    // Default object size - most tradeskill containers are roughly 3x3x3 units
    float halfSize = 3.0f;
    obj.boundingBox = irr::core::aabbox3df(
        irrX - halfSize, irrY - halfSize, irrZ - halfSize,
        irrX + halfSize, irrY + halfSize * 2, irrZ + halfSize  // Extend upward more
    );

    worldObjects_[dropId] = obj;
    LOG_DEBUG(MOD_GRAPHICS, "Added world object: dropId={} type={} name='{}' at ({:.1f}, {:.1f}, {:.1f})",
              dropId, objectType, name, x, y, z);
}

void IrrlichtRenderer::removeWorldObject(uint32_t dropId) {
    auto it = worldObjects_.find(dropId);
    if (it != worldObjects_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "Removed world object: dropId={}", dropId);
        worldObjects_.erase(it);
    }
}

void IrrlichtRenderer::clearWorldObjects() {
    LOG_DEBUG(MOD_GRAPHICS, "Clearing {} world objects", worldObjects_.size());
    worldObjects_.clear();
}

uint32_t IrrlichtRenderer::getWorldObjectAtScreenPos(int screenX, int screenY) const {
    if (!camera_ || !collisionManager_) {
        return 0;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionManager_->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera_);

    uint32_t closestObjectId = 0;
    float closestDist = std::numeric_limits<float>::max();

    for (const auto& [id, obj] : worldObjects_) {
        // Expand box slightly for easier clicking
        irr::core::aabbox3df expandedBox = obj.boundingBox;
        expandedBox.MinEdge -= irr::core::vector3df(1.0f, 1.0f, 1.0f);
        expandedBox.MaxEdge += irr::core::vector3df(1.0f, 1.0f, 1.0f);

        if (expandedBox.intersectsWithLine(ray)) {
            // Calculate distance to object center
            irr::core::vector3df objCenter = expandedBox.getCenter();
            float dist = ray.start.getDistanceFrom(objCenter);

            if (dist < closestDist) {
                closestDist = dist;
                closestObjectId = id;
            }
        }
    }

    return closestObjectId;
}

uint32_t IrrlichtRenderer::getNearestWorldObject(float playerX, float playerY, float playerZ,
                                                   float maxDistance) const {
    uint32_t nearestId = 0;
    float nearestDistSq = maxDistance * maxDistance;

    for (const auto& [id, obj] : worldObjects_) {
        // Calculate 3D distance
        float dx = obj.x - playerX;
        float dy = obj.y - playerY;
        float dz = obj.z - playerZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearestId = id;
        }
    }

    if (nearestId != 0) {
        LOG_DEBUG(MOD_GRAPHICS, "getNearestWorldObject: found dropId={} at distance {:.1f}",
                  nearestId, std::sqrt(nearestDistSq));
    }

    return nearestId;
}


// T300: the first-person camera's constants and its placement math now live
// in include/client/graphics/first_person_camera.h -- read that header before
// changing any of these numbers, and change them THERE, not here. The three
// tunables it holds are kFirstPersonPitchDownDegrees (3.65, was T75's 12),
// kFirstPersonHorizontalFovDegrees (62.8, was Irrlicht's default 72 vertical
// == ~88 horizontal). The body-hiding lever is no longer a near plane at all:
// Leg A measured the official client clipping nothing, so hiding is a DOWN-
// PITCH CLAMP derived as 90 - vFOV/2 (firstPersonMaxDownPitchDegrees()).
//
// T76's kFirstPersonCameraStandoffUnits (0.3 BACKWARD) is gone: it existed to
// pull the eye off in-world arm geometry that is no longer drawn as a
// separate pass, it pointed the wrong way for a real eye, and T298 measured
// the official client's forward offset at 0.00 +/- 0.4.
void IrrlichtRenderer::applyCameraProjectionForMode() {
    if (!camera_) return;
    const bool firstPerson = (cameraMode_ == CameraMode::FirstPerson);
    // T300 (post-Leg-A): the NEAR PLANE IS THE SAME IN EVERY MODE. Leg A
    // measured the official client clipping nothing -- geometry draws at
    // point-blank range -- so first person must clip exactly as third person
    // does. The near-plane hiding that briefly lived here is refuted and gone;
    // the down-pitch clamp (applyFirstPersonPitchClamp()) is what hides the
    // body. Only the FOV is mode-specific, so third person stays byte-for-byte
    // what it was and the visual gate's goldens hold.
    camera_->setNearValue(kWorldNearPlaneUnits);
    camera_->setFOV(firstPerson ? firstPersonVerticalFovRadians(camera_->getAspectRatio())
                                : kWorldVerticalFovRadians);
    if (firstPerson) applyFirstPersonPitchClamp();
    LOG_DEBUG(MOD_GRAPHICS,
              "[FP-PROJ] firstPerson={} near={:.3f} fovY={:.4f}rad aspect={:.4f}",
              firstPerson, camera_->getNearValue(), camera_->getFOV(),
              camera_->getAspectRatio());
}

// T300: THE HIDING SEAM -- see the declaration. The body hangs directly below
// the eye, so clamping the down-pitch so the frustum's lower edge never passes
// the nadir is sufficient to hide it, at any model scale.
void IrrlichtRenderer::applyFirstPersonPitchClamp() {
    if (!camera_ || cameraMode_ != CameraMode::FirstPerson) return;

    const float minLook = firstPersonMinLookPitchDegrees(getFirstPersonVerticalFov());
    if (playerPitch_ >= minLook && playerPitch_ <= kFirstPersonMaxUpPitchDegrees) return;

    const float before = playerPitch_;
    playerPitch_ = std::min(std::max(playerPitch_, minLook), kFirstPersonMaxUpPitchDegrees);
    LOG_DEBUG(MOD_GRAPHICS,
              "[FP-CLAMP] mouse-look pitch {:.2f} -> {:.2f} deg (floor {:.2f}, total down-pitch limit {:.2f} = 90 - vFOV/2)",
              before, playerPitch_, minLook,
              firstPersonMaxDownPitchDegrees(getFirstPersonVerticalFov()));
    repositionFirstPersonCamera();
}

// One place that answers "what vertical FOV is the first-person camera using",
// so the clamp derivation and the projection cannot disagree.
float IrrlichtRenderer::getFirstPersonVerticalFov() const {
    return firstPersonVerticalFovRadians(camera_ ? camera_->getAspectRatio() : 4.0f / 3.0f);
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

    float eyeHeight = entityRenderer_ ? entityRenderer_->getPlayerEyeHeightAboveNodeOrigin() : 0.0f;
    if (eyeHeight <= 0.0f) {
        // Node-origin-relative fallback for a human-sized entity whose model
        // has not loaded yet -- NOT a feet-relative 6.x. See
        // getPlayerEyeHeightAboveNodeOrigin()'s comment for the two frames.
        eyeHeight = 3.65f;
    }

    // T295: the user's mouse-look pitch (playerPitch_, degrees, +up) is
    // applied on top of the fixed down-bias inside the helper. Before T295
    // playerPitch_ existed ONLY in renderer_player_physics.cpp's private copy
    // of this formula, so moving discarded the bias and standing still
    // discarded the mouse.
    const FirstPersonCameraPlacement placement = computeFirstPersonCamera(
        playerX_, playerY_, playerZ_, eyeHeight, playerConfig_.eyeHeight,
        playerHeading_, playerPitch_, getFirstPersonVerticalFov());

    camera_->setPosition(irr::core::vector3df(placement.posX, placement.posY, placement.posZ));
    camera_->setTarget(irr::core::vector3df(placement.targetX, placement.targetY, placement.targetZ));

    // T295: DEBUG, not INFO -- this now runs on every frame in which the
    // player moves, turns or looks. The FP-CAMERA tag is the evidence
    // emitter the unit's still-vs-moving proof diffs. T300: playerZ_ is the
    // node origin (== server Z, model CENTRE), not the feet; the log says so
    // now, because "feet=" is what invited a 3.05-unit "correction".
    LOG_DEBUG(MOD_GRAPHICS, "[FP-CAMERA] eq=({:.4f},{:.4f},{:.4f}) nodeOriginZ={:.4f} eyeAboveOrigin={:.4f} adjust={:.2f} heading={:.4f} lookPitch={:.4f}deg fixedPitchDown={:.4f}deg maxDownPitch={:.2f}deg",
              placement.posX, placement.posZ, placement.posY, playerZ_, eyeHeight,
              playerConfig_.eyeHeight, playerHeading_, playerPitch_,
              kFirstPersonPitchDownDegrees,
              firstPersonMaxDownPitchDegrees(getFirstPersonVerticalFov()));
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

        float eyeHeight = entityRenderer_->getPlayerEyeHeightAboveNodeOrigin();
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerSpawnId: eyeAboveNodeOrigin={:.2f} firstPerson={}", eyeHeight, firstPerson);

        // Fix first-person camera height now that we have the player model
        if (firstPerson && eyeHeight > 0.0f) {
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


void IrrlichtRenderer::run() {
    if (!initialized_) {
        return;
    }

    irr::u32 lastTime = device_->getTimer()->getTime();

    while (isRunning()) {
        irr::u32 currentTime = device_->getTimer()->getTime();
        irr::u32 frameTimeMs = currentTime - lastTime;
        float deltaTime = frameTimeMs / 1000.0f;
        lastTime = currentTime;

        // Record frame time for gameplay statistics
        if (frameTimeMs > 0) {
            EQT::PerformanceMetrics::instance().recordSample("Frame Time", static_cast<int64_t>(frameTimeMs));
        }

        if (!processFrame(deltaTime)) {
            break;
        }
    }
}

bool IrrlichtRenderer::saveScreenshot(const std::string& filename) {
    if (!driver_) {
        return false;
    }

#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
    // DRM/EGL: Irrlicht's createScreenShot() reads GL_FRONT which doesn't exist
    // in EGL (only GLX has separate front/back read targets). Read GL_BACK instead.
    // P10: gated on the GLES2 driver type generally, not just useDRM — this branch
    // is also required for the x86 EGL-surfaceless dev build (H01; useDRM is false
    // there since --drm isn't passed, but it's still the same EQT_HAS_DRM-defined
    // GLES2/EGL device with no real GL_FRONT/window-system default framebuffer).
    // createScreenShot() was found to return garbage (uninitialized-looking noise)
    // under the surfaceless case during P10's in-app A/B screenshot verification —
    // this widened gate is the fix, discovered and applied there.
    // P23: widened again to `EQT_HAS_DRM || EQT_HAS_GLES2`. The x86 GLES2 dev
    // build (build-gles2-x86.sh) configures EQT_DRM=OFF / EQT_GLES2=ON, so
    // EQT_HAS_DRM was never defined there and this whole safe-readback branch
    // was compiled OUT, falling through to driver_->createScreenShot() (which
    // segfaults nondeterministically under llvmpipe — see deferred.md, P20
    // Review deviation 2a). EQT_HAS_GLES2 is defined on both the x86 GLES2
    // build and ARM (which also always defines EQT_HAS_DRM, so ARM's path
    // through this #if is unchanged — this is additive for x86-without-DRM
    // only). The runtime check below (useDRM || EDT_OGLES2) already covers
    // the x86 case correctly once the branch is compiled in at all.
    if (config_.constrainedConfig.useDRM || driver_->getDriverType() == irr::video::EDT_OGLES2) {
        irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();
        uint32_t w = screenSize.Width;
        uint32_t h = screenSize.Height;

        std::vector<uint8_t> pixels(w * h * 4);
#ifndef EQT_HAS_GLES2
        glReadBuffer(GL_BACK);
#endif
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // glReadPixels returns bottom-to-top; flip to top-to-bottom
        size_t rowBytes = w * 4;
        std::vector<uint8_t> rowBuf(rowBytes);
        for (uint32_t y = 0; y < h / 2; ++y) {
            uint8_t* top = pixels.data() + y * rowBytes;
            uint8_t* bot = pixels.data() + (h - 1 - y) * rowBytes;
            std::memcpy(rowBuf.data(), top, rowBytes);
            std::memcpy(top, bot, rowBytes);
            std::memcpy(bot, rowBuf.data(), rowBytes);
        }

        // Convert RGBA to ARGB for Irrlicht (swap R and B)
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);  // R <-> B
        }

        irr::video::IImage* image = driver_->createImageFromData(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2d<irr::u32>(w, h),
            pixels.data(), false);

        if (image) {
            bool result = driver_->writeImageToFile(image, filename.c_str());
            image->drop();
            if (result) {
                LOG_INFO(MOD_GRAPHICS, "Screenshot saved: {}", filename);
            }
            return result;
        }
        return false;
    }
#endif

    irr::video::IImage* screenshot = driver_->createScreenShot();
    if (screenshot) {
        bool result = driver_->writeImageToFile(screenshot, filename.c_str());
        screenshot->drop();
        if (result) {
            LOG_INFO(MOD_GRAPHICS, "Screenshot saved: {}", filename);
        }
        return result;
    }
    return false;
}


void IrrlichtRenderer::setFrameTimingEnabled(bool enabled) {
    if (isLoading()) return;
    frameTimingEnabled_ = enabled;
    if (enabled) {
        // Reset accumulators when starting
        frameTimings_ = FrameTimings();
        frameTimingsAccum_ = FrameTimings();
        frameTimingsSampleCount_ = 0;
        // S05: renderPassTimer_ already created in initLoadingScreen() — just install
        if (smgr_ && renderPassTimer_) {
            smgr_->setLightManager(renderPassTimer_);
        }
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler ENABLED - timing data will be logged every 60 frames");
    } else {
        // Remove render pass timer (but keep it if manual zone draw needs it)
        if (smgr_ && renderPassTimer_ && !manualZoneDrawEnabled_) {
            smgr_->setLightManager(nullptr);
            renderPassTimer_ = nullptr;  // Irrlicht drops the ref
        }
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler DISABLED");
    }
}

// --- Renderer Mode Implementation ---

void IrrlichtRenderer::setClipDistance(float distance) {
    if (isLoading()) return;
    // Clamp to reasonable range
    if (distance < 100.0f) distance = 100.0f;
    if (distance > 50000.0f) distance = 50000.0f;

    // Update render distance for fog/object culling
    renderDistance_ = distance;
    config_.constrainedConfig.clipDistance = distance;

    // In constrained mode, set far plane = render distance for Z-precision
    if (camera_) {
        camera_->setFarValue(std::max(SKY_FAR_PLANE, distance));
    }

    // Update fog to match new render distance
    setupFog();

    LOG_INFO(MOD_GRAPHICS, "Render distance set to {}, camera far plane: {}",
             distance, camera_ ? camera_->getFarValue() : SKY_FAR_PLANE);
}

float IrrlichtRenderer::getClipDistance() const {
    if (camera_) {
        return camera_->getFarValue();
    }
    return config_.constrainedConfig.clipDistance;
}

// ============================================================================
// T60: Inventory paperdoll (CharacterModelView)
// ============================================================================

void IrrlichtRenderer::markPaperdollDirty() {
    if (characterModelView_) characterModelView_->markDirty();
}

void IrrlichtRenderer::ensurePaperdollInitialized() {
    if (characterModelView_ || paperdollInitAttempted_) {
        return;
    }
    paperdollInitAttempted_ = true;

    if (!smgr_ || !driver_) {
        LOG_WARN(MOD_UI, "IrrlichtRenderer: paperdoll init skipped, no scene manager/driver yet");
        return;
    }

    // Fixed size matching InventoryLayout::paperdollRect()'s dimensions
    // (304x256 — constant regardless of screen resolution, since
    // centerPopup is a fixed-size box whose ORIGIN, not size, varies with
    // resolution — see inventory_layout.h).
    constexpr int kPaperdollWidth = 304;
    constexpr int kPaperdollHeight = 256;

    auto doll = std::make_unique<eqt::ui::CharacterModelView>();
    doll->init(smgr_, driver_, kPaperdollWidth, kPaperdollHeight);
    if (entityRenderer_) {
        doll->setRaceModelLoader(entityRenderer_->getRaceModelLoader());
        doll->setEquipmentModelLoader(entityRenderer_->getEquipmentModelLoader());
    }
    // No InventoryManager is wired here (S2 decoupled the renderer from it
    // entirely — IrrlichtRenderer holds no InventoryManager pointer to
    // pass on; inventory UI state arrives only via bridge events). This is
    // fine: the doll's appearance comes from the player's live
    // EntityAppearance (updatePaperdollForFrame(), below), matching Step
    // 4 of the plan, not from inventory item materials.
    characterModelView_ = std::move(doll);
    LOG_INFO(MOD_UI, "IrrlichtRenderer: paperdoll (CharacterModelView) constructed lazily on first popup open");
}

namespace {
bool appearanceEquals(const EQT::Graphics::EntityAppearance& a, const EQT::Graphics::EntityAppearance& b) {
    if (a.face != b.face || a.haircolor != b.haircolor || a.hairstyle != b.hairstyle ||
        a.beardcolor != b.beardcolor || a.beard != b.beard || a.texture != b.texture ||
        a.helm != b.helm) {
        return false;
    }
    for (int i = 0; i < 9; ++i) {
        if (a.equipment[i] != b.equipment[i] || a.equipment_tint[i] != b.equipment_tint[i]) {
            return false;
        }
    }
    return true;
}
} // namespace

void IrrlichtRenderer::updatePaperdollForFrame() {
    ensurePaperdollInitialized();
    if (!characterModelView_ || !entityRenderer_) {
        return;
    }

    // T60 (Step 4): find the player's own in-world entity and feed the
    // doll the SAME EntityAppearance it renders with in 3rd person — not a
    // separately-reconstructed one — so the doll can never visually
    // disagree with what the player actually sees.
    const EQT::Graphics::EntityVisual* playerVisual = nullptr;
    for (const auto& [spawnId, visual] : entityRenderer_->getEntities()) {
        if (visual.isPlayer) {
            playerVisual = &visual;
            break;
        }
    }
    if (!playerVisual) {
        return;  // Not zoned in / player entity not built yet — nothing to show.
    }

    const bool changed = !paperdollHasAppearance_ ||
        playerVisual->raceId != paperdollLastRaceId_ ||
        playerVisual->gender != paperdollLastGender_ ||
        !appearanceEquals(playerVisual->appearance, paperdollLastAppearance_);

    if (changed) {
        paperdollLastRaceId_ = playerVisual->raceId;
        paperdollLastGender_ = playerVisual->gender;
        paperdollLastAppearance_ = playerVisual->appearance;
        paperdollHasAppearance_ = true;
        // setCharacter() rebuilds the model and marks the doll dirty
        // internally — only call it when something actually changed.
        characterModelView_->setCharacter(playerVisual->raceId, playerVisual->gender,
                                           playerVisual->appearance);
    }

    // update()/renderToTexture() are cheap no-ops when not dirty — safe to
    // call every frame the popup is open; the RTT itself only re-renders
    // on markDirty() triggers (appearance change, popup open, drag-rotate).
    characterModelView_->update(16.0f);  // Approximate frame delta for idle-anim throttling
    characterModelView_->renderToTexture();
}

} // namespace Graphics
} // namespace EQT
