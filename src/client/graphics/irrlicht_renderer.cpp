#include "client/graphics/irrlicht_renderer.h"
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include <filesystem>
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_mesh_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
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

bool RendererEventReceiver::wasLeftButtonClicked() {
    bool clicked = leftButtonClicked_;
    leftButtonClicked_ = false;
    return clicked;
}

bool RendererEventReceiver::wasLeftButtonReleased() {
    bool released = leftButtonReleased_;
    leftButtonReleased_ = false;
    return released;
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

    // Choose driver type from constrained config backend
    irr::video::E_DRIVER_TYPE driverType;
    switch (config_.constrainedConfig.renderingBackend) {
#ifdef EQT_HAS_GLES2
        case RenderingBackend::GLES2:
            driverType = irr::video::EDT_OGLES2;
            LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL ES 2.0");
            break;
#endif
        case RenderingBackend::OpenGL:
            driverType = irr::video::EDT_OPENGL;
            LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL");
            break;
        case RenderingBackend::Software:
        default:
            driverType = irr::video::EDT_BURNINGSVIDEO;
            LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: Burnings Software");
            break;
    }

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
        LOG_WARN(MOD_GRAPHICS, "[GL] Failed to create device with {} driver, falling back to software",
                 backendName(config_.constrainedConfig.renderingBackend));
        params.DriverType = irr::video::EDT_SOFTWARE;
        device_ = irr::createDeviceEx(params);
    }

    if (!device_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create Irrlicht device (all drivers failed)");
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

    // Initialize GLSL shader pipeline (if enabled and driver supports it)
    if (config_.constrainedConfig.enableShaders &&
        config_.constrainedConfig.renderingBackend != RenderingBackend::Software) {
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

    // Create tree wind animation manager (needed before zone loading)
    if (!treeManager_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating tree manager...");
        treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
        treeManager_->setRenderDistance(renderDistance_);
        if (constrainedTextureCache_) {
            treeManager_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        if (zoneShader_ && zoneShader_->isAvailable()) {
            treeManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest());
        }
    }

    // Create weather system (needed before zone loading)
    if (!weatherSystem_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating weather system...");
        weatherSystem_ = std::make_unique<WeatherSystem>();
        // Connect weather system to tree manager via callback
        weatherSystem_->addCallback([this](WeatherType weather) {
            if (treeManager_) {
                treeManager_->setWeather(weather);
            }
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
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
        }
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
        if (zoneBspTree_) {
            doorManager_->setBspTree(zoneBspTree_.get());
        }
        doorManager_->setPvsRegion(currentPvsRegion_);
        if (frustumCuller_) {
            doorManager_->setFrustumCuller(frustumCuller_.get());
        }
        if (zoneShader_ && zoneShader_->isAvailable()) {
            doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest());
        }
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

    // Helper to push chat feedback via bridge
    auto chat = [this](const std::string& msg) {
        if (bridge_) {
            eqt::state::ChatMessageData data;
            data.message = msg;
            data.channelType = 0;
            data.channelName = "system";
            bridge_->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SystemMessage, std::move(data)));
        }
    };

    // --- Visual rendering ---
    if (cmd == "/sort") {
        toggleManualZoneDraw();
        chat(fmt::format("Front-to-back zone sorting: {}", isManualZoneDrawEnabled() ? "ENABLED" : "DISABLED"));
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
                                        "UV", "depth", "rawTexAlpha"};
            const char* name = (mode >= 0 && mode <= 11) ? modeNames[mode] : "unknown";
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
    else if (cmd == "/renderdist") {
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
    } else if (cmd == "/clipdist") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            setClipDistance(dist);
            chat(fmt::format("Clip distance: {:.0f}", dist));
        } else {
            chat(fmt::format("Clip distance: {:.0f}", getClipDistance()));
        }
    }
    // --- Profiling/debug ---
    else if (cmd == "/frametiming") {
        bool newState = !isFrameTimingEnabled();
        setFrameTimingEnabled(newState);
        if (newState) {
            chat("Frame timing profiler ENABLED - check console for breakdown every ~2 seconds");
        } else {
            chat("Frame timing profiler DISABLED");
        }
    } else if (cmd == "/sceneprofile") {
        runSceneProfile();
        chat("Scene profile running - check console for breakdown");
    } else if (cmd == "/dumpscene") {
        dumpScene();
        chat("Scene dump written to console log");
    } else if (cmd == "/simworker") {
        auto info = getSimWorkerDebugInfo();
        for (const auto& line : info) chat(line);
    } else if (cmd == "/pmem") {
        MemoryReportInput ext{};  // Empty external data — game thread pushes its own stats
        auto report = getMemoryReport(ext);
        for (const auto& line : report) {
            chat(line);
            LOG_INFO(MOD_GRAPHICS, "{}", line);
        }
    }
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
        if (inventoryState_.activePopup == PopupType::Inventory) {
            inventoryState_.activePopup = PopupType::None;
        } else {
            inventoryState_.activePopup = PopupType::Inventory;
        }
        chat(fmt::format("New UI inventory: {}",
            inventoryState_.activePopup == PopupType::Inventory ? "OPEN" : "CLOSED"));
    } else if (cmd == "/timestamp" || cmd == "/timestamps") {
        chat("Timestamps command not available (WindowManager removed)");
    } else if (cmd == "/filter") {
        chat("Filter command not available (WindowManager removed)");
    } else if (cmd == "/reloadeffects" || cmd == "/particlereload") {
        if (auto* pm = getParticleManager()) {
            pm->reloadSettings();
            chat("Environment effects settings reloaded");
        } else {
            chat("Particle system not available");
        }
    } else if (cmd == "/reloadweather" || cmd == "/weatherreload") {
        if (auto* we = getWeatherEffects()) {
            if (we->reloadConfig()) {
                chat("Weather effects settings reloaded");
            } else {
                chat("Failed to reload weather settings");
            }
        } else {
            chat("Weather effects system not available");
        }
    }
    // --- Detail ---
    else if (cmd == "/detail") {
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
    else if (cmd == "/governor") {
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
    treeManager_.reset();
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
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Boids manager created (toggled on via settings)");
        }
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

    // --- Animated Tree Manager: toggle wind animation ---
    if (treeManager_) {
        treeManager_->setEnabled(settings.animatedTrees);
        LOG_DEBUG(MOD_GRAPHICS, "Applied tree animation settings: enabled={}", settings.animatedTrees);
    }

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

void IrrlichtRenderer::unloadZone() {
    if (isLoading()) return;
    sequentialLoadComplete_ = false;
    storedZoneEnvironment_.pending = false;

    // Stop simulation worker before zone cleanup
    stopSimulationWorker();

    // Stop background entity prep worker before zone cleanup
    if (entityPrepWorker_) {
        entityPrepWorker_->stop();
        entityPrepWorker_.reset();
        if (entityRenderer_) {
            entityRenderer_->setEntityPrepWorker(nullptr);
        }
    }

    // Reset entity loading state - we're starting a new zone
    networkReady_ = false;
    entitiesLoaded_ = false;
    expectedEntityCount_ = 0;
    loadedEntityCount_ = 0;
    zoneReady_ = false;
    entityPrepReady_ = false;
    entityPrepScanCounter_ = 0;

    // Log texture counts before cleanup
    if (constrainedTextureCache_) {
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache has {} textures, {} bytes before cleanup",
                 constrainedTextureCache_->getTextureCount(),
                 constrainedTextureCache_->getCurrentUsage());
    }

    // Remove zone placeholder mesh if still present
    destroyZonePlaceholder();

    // Unload EQE1 flat packs (clear cache refs before destroying loaders)
    if (constrainedTextureCache_) {
        constrainedTextureCache_->clearEQE1Loaders();
    }
    {
        std::lock_guard<std::mutex> lock(eqe1Mutex_);
        eqe1Loaders_.clear();
    }

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
    // Release retained GLES2 textures and EGL images after atlas GL textures are deleted.
    // Lima driver requires source textures/images to stay alive while desktop GL textures
    // are in use, so we release them only after unload().
    if (gles2Helper_) {
        gles2Helper_->releaseSharedResources();
    }
#endif

    // Reset animated texture manager (removes its textures from driver)
    animatedTextureManager_.reset();

    // Clear camera collision FIRST to prevent use-after-free during zone transitions
    if (cameraController_) {
        cameraController_->setBspCollision(nullptr, nullptr);
        cameraController_->setCollisionManager(nullptr, nullptr);
    }

    // Clear detail system BEFORE dropping collision selector
    if (detailManager_) {
        detailManager_->onZoneExit();
    }

    // Clear tree wind animation system
    if (treeManager_) {
        treeManager_->cleanup();
    }

    // Clear environmental particle system
    if (particleManager_) {
        particleManager_->onZoneLeave();
    }

    // Clear ambient creatures (boids) system
    if (boidsManager_) {
        boidsManager_->onZoneLeave();
    }

    // Clear tumbleweed system
    if (tumbleweedManager_) {
        tumbleweedManager_->onZoneLeave();
    }

    // Now safe to remove zone collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    // Remove zone mesh
    if (zoneMeshNode_) {
        deleteMeshHardwareBuffers(zoneMeshNode_);
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    // Remove PVS region mesh nodes (delete VBOs before removing nodes)
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (node) {
            deleteMeshHardwareBuffers(node);
            if (node->getParent()) node->remove(); else node->drop();
        }
    }
    regionMeshNodes_.clear();
    regionBoundingBoxes_.clear();

    if (fallbackMeshNode_) {
        deleteMeshHardwareBuffers(fallbackMeshNode_);
        if (fallbackMeshNode_->getParent()) fallbackMeshNode_->remove(); else fallbackMeshNode_->drop();
        fallbackMeshNode_ = nullptr;
    }

    // Remove collision-only node (used in PVS mode)
    if (zoneCollisionNode_) {
        zoneCollisionNode_->remove();
        zoneCollisionNode_ = nullptr;
    }

    // Reset PVS state
    usePvsCulling_ = false;
    zoneBspTree_.reset();
    currentPvsRegion_ = SIZE_MAX;

    // Reset manual zone draw state
    manualZoneDrawEnabled_ = false;
    sortedZoneDrawList_.clear();
    sortedDrawEntries_.clear();
    portalSystem_.reset();
    portalOcclusionEnabled_ = false;
    portalOcclusionEligible_ = false;
    portalBuildPending_ = false;
    regionNeighbors_.clear();

    // Clear constrained mesh cache
    if (constrainedMeshCache_) {
        LOG_INFO(MOD_GRAPHICS, "unloadZone: mesh cache had {}/{} regions loaded, {} evictions, {} rebuilds",
            constrainedMeshCache_->getLoadedCount(), constrainedMeshCache_->getTotalCount(),
            constrainedMeshCache_->getEvictionCount(), constrainedMeshCache_->getRebuildCount());
        constrainedMeshCache_.reset();
    }
    meshLoadQueue_.clear();
    protectedRegions_.clear();
    portalVisibleRegions_ = nullptr;
    indoorRegions_.clear();
    regionPvsDepthMap_ = nullptr;
    pvsNeighborhood_.clear();
    pvsNeighborhoodAnchor_ = SIZE_MAX;
    pendingTextureRegions_.clear();
    textureRebuildQueue_.clear();
    pendingTextureDoors_.clear();
    doorTextureRebuildQueue_.clear();
    pendingTextureObjects_.clear();
    objectTextureRebuildQueue_.clear();

    // Clean up background mesh build state
    pendingTextureBuffers_.clear();
    pendingMeshBuilds_.clear();
    materialSwapQueue_.clear();
    localMeshResults_.clear();
#ifdef EQT_HAS_GLES2
    pendingGPUResults_.clear();
#endif
    {
        std::lock_guard<std::mutex> lock(meshResultMutex_);
        for (auto& r : meshResultQueue_) {
            if (r.mesh) r.mesh->drop();
        }
        meshResultQueue_.clear();
    }

    // Clear occlusion culler data
    if (occlusionCuller_) {
        occlusionCuller_->clearOccluders();
    }
    occlusionCulledRegions_.clear();

    // Reset zone clip plane so previous zone's cap doesn't linger
    zoneMaxClip_ = 99999.0f;
    setRenderDistance(userRenderDistance_);

    // Clear BSP tree from entity renderer and door manager
    if (entityRenderer_) {
        entityRenderer_->clearBspTree();
    }
    if (doorManager_) {
        doorManager_->setBspTree(nullptr);
        doorManager_->setPvsRegion(SIZE_MAX);
        doorManager_->setOcclusionCulledRegions(nullptr);
        doorManager_->setRegionNeighbors(nullptr);
    }

    // Remove object nodes
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        if (objectNodes_[i]) {
            if (i < objectInSceneGraph_.size() && objectInSceneGraph_[i]) {
                objectNodes_[i]->remove();
            }
            objectNodes_[i]->drop();  // Release our reference
        }
    }
    objectNodes_.clear();
    objectPositions_.clear();
    objectBoundingBoxes_.clear();
    objectInSceneGraph_.clear();

    // Clear zone light data (no scene nodes to remove)
    zoneLightData_.clear();
    zoneLightPositions_.clear();
    // Clear entity renderer
    if (entityRenderer_) {
        // Clear mesh caches first to force fresh mesh/texture rebuild on next zone-in.
        // Keeps model data (geometry, skeletons, S3D archives) cached for performance.
        // Fixes garbled player textures on re-zone caused by stale texture pointers
        // in cached animated meshes.
        if (auto* rml = entityRenderer_->getRaceModelLoader()) {
            rml->clearMeshCaches();
        }
        entityRenderer_->clearEntities();
    }

    // Clear door manager
    if (doorManager_) {
        doorManager_->clearDoors();
        doorManager_->setZone(nullptr);
    }

    // Disable sky when unloading zone (will be re-enabled when new zone loads)
    if (skyRenderer_) {
        skyRenderer_->setEnabled(false);
    }

    // Clear world objects (tradeskill containers)
    clearWorldObjects();

    // Clear object lights (they reference freed zone data)
    for (size_t i = 0; i < objectLights_.size(); ++i) {
        if (objectLights_[i].node) {
            bool isInScene = (i < objectLightInSceneGraph_.size()) ? objectLightInSceneGraph_[i] : false;
            if (isInScene) {
                objectLights_[i].node->remove();
            }
            objectLights_[i].node->drop();
        }
    }
    objectLights_.clear();
    objectLightInSceneGraph_.clear();

    // Clear vertex animated meshes (they reference freed zone mesh buffers)
    vertexAnimatedMeshes_.clear();

    // Clear zone line visualization boxes
    clearZoneLineBoundingBoxes();

    // Clear constrained texture cache to free old zone textures from driver
    if (constrainedTextureCache_) {
        constrainedTextureCache_->unfreeze();
        constrainedTextureCache_->clear();
        constrainedTextureCache_->resetStatistics();
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache cleared");
    }

    currentZone_.reset();
    currentZoneName_.clear();
    isIndoorZone_ = false;
    zoneBoundsValid_ = false;
}

void IrrlichtRenderer::setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                          const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                          const float fogMinClip[4], const float fogMaxClip[4]) {
    // Set sky type if sky renderer is available
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->setSkyType(skyType, currentZoneName_);

        // Determine if sky should be shown based on zone type and user settings
        // zoneType values from server: 1=Outdoors, 2=Dungeons, 255(0xFF)=Any/default
        // Only disable sky for explicit dungeon zones (type 2)
        bool isDungeon = (zoneType == 2);
        isIndoorZone_ = isDungeon;

        // Check user setting and constrained config
        bool skySettingEnabled = getDisplaySettings().skyEnabled;
        bool skyAllowed = !isDungeon && skySettingEnabled && config_.constrainedConfig.skyRendering;
        skyRenderer_->setEnabled(skyAllowed);

        LOG_DEBUG(MOD_GRAPHICS, "Zone environment: sky type {}, zone type {} ({}), sky {} (setting={}, constrained={})",
                  skyType, zoneType, isDungeon ? "dungeon" : "outdoor",
                  skyAllowed ? "enabled" : "disabled",
                  skySettingEnabled ? "on" : "off",
                  config_.constrainedConfig.skyRendering ? "on" : "off");
    }

    // Apply server-provided max clip plane as ceiling on render distance
    // fogMaxClip[0] is the primary clip distance for the zone
    zoneMaxClip_ = (fogMaxClip[0] > 0.0f) ? fogMaxClip[0] : 99999.0f;
    // Re-apply render distance with the new zone cap
    setRenderDistance(userRenderDistance_);

    LOG_INFO(MOD_GRAPHICS, "Zone max clip plane: {:.0f} (effective render distance: {:.0f}, user setting: {:.0f})",
             zoneMaxClip_, renderDistance_, userRenderDistance_);

    // Apply fog color from zone data
    if (driver_ && fogEnabled_) {
        irr::video::SColor fogColor(255, fogRed[0], fogGreen[0], fogBlue[0]);

        float fogEnd = renderDistance_;
        float fogStart = renderDistance_ - fogThickness_;
        fogStart = std::max(0.0f, fogStart);

        driver_->setFog(
            fogColor,
            irr::video::EFT_FOG_LINEAR,
            fogStart,
            fogEnd,
            0.0f,  // Density (unused for linear fog)
            true,  // Pixel fog
            false  // Range fog
        );

        LOG_DEBUG(MOD_GRAPHICS, "Zone fog color: RGB({},{},{}), distances: {:.0f}-{:.0f} (renderDistance={:.0f})",
                  fogRed[0], fogGreen[0], fogBlue[0], fogStart, fogEnd, renderDistance_);
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

void IrrlichtRenderer::buildZonePlaceholder(float playerIrrX, float playerIrrY, float playerIrrZ) {
    if (!collisionMap_ || !collisionMap_->IsLoaded() || !smgr_ || !driver_) return;

    // Destroy existing node for rebuild
    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->remove();
        zonePlaceholderNode_ = nullptr;
    }

    // Cache terrain triangles on first call (GetAllTerrainTriangles is expensive)
    if (cachedPlaceholderTriangles_.empty()) {
        cachedPlaceholderTriangles_ = collisionMap_->GetAllTerrainTriangles();
        if (cachedPlaceholderTriangles_.empty()) return;
    }

    const auto& allTriangles = cachedPlaceholderTriangles_;

    // Filter triangles to render distance from player position (Irrlicht Y-up coords)
    float clipDist = renderDistance_ > 0.0f ? renderDistance_ : 300.0f;
    float clipDistSq = clipDist * clipDist;

    // PVS filtering: if BSP tree is available (from BSP preload), skip triangles
    // in BSP regions not visible from the camera's current region.
    // Centroid coords: Irrlicht Y-up (cx, cy, cz) → EQ Z-up (cx, cz, cy)
    bool usePvsFilter = false;
    std::shared_ptr<BspRegion> camRegion;
    if (zoneBspTree_) {
        // Use currentPvsRegion_ if already computed, otherwise derive from player position
        // (handles the case where BSP preload just completed but PVS update hasn't run yet)
        size_t cameraRegion = currentPvsRegion_;
        if (cameraRegion == SIZE_MAX) {
            // Irrlicht Y-up (x, y, z) → EQ Z-up (x, z, y)
            cameraRegion = zoneBspTree_->findRegionIndexForPoint(playerIrrX, playerIrrZ, playerIrrY);
        }
        if (cameraRegion != SIZE_MAX && cameraRegion < zoneBspTree_->regions.size()) {
            camRegion = zoneBspTree_->regions[cameraRegion];
            if (camRegion && !camRegion->visibleRegions.empty()) {
                usePvsFilter = true;
            }
        }
    }

    std::vector<HCMap::Triangle> triangles;
    triangles.reserve(allTriangles.size() / 2);
    size_t pvsCulledCount = 0;
    for (const auto& tri : allTriangles) {
        // Use triangle centroid for distance check
        float cx = (tri.v1.x + tri.v2.x + tri.v3.x) / 3.0f;
        float cy = (tri.v1.y + tri.v2.y + tri.v3.y) / 3.0f;
        float cz = (tri.v1.z + tri.v2.z + tri.v3.z) / 3.0f;
        float dx = cx - playerIrrX;
        float dy = cy - playerIrrY;
        float dz = cz - playerIrrZ;
        if (dx * dx + dy * dy + dz * dz > clipDistSq) {
            continue;
        }

        // PVS check: convert Irrlicht Y-up centroid to EQ Z-up for BSP lookup
        if (usePvsFilter) {
            size_t triRegion = zoneBspTree_->findRegionIndexForPoint(cx, cz, cy);
            if (triRegion != SIZE_MAX && triRegion < camRegion->visibleRegions.size()
                && !camRegion->visibleRegions[triRegion]) {
                pvsCulledCount++;
                continue;
            }
        }

        triangles.push_back(tri);
    }

    if (triangles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Zone placeholder: 0 triangles within render distance {:.0f} of player ({:.0f},{:.0f},{:.0f})",
                 clipDist, playerIrrX, playerIrrY, playerIrrZ);
        return;
    }

    // Build a single mesh buffer from filtered terrain triangles
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    buf->Vertices.set_used(triangles.size() * 3);
    buf->Indices.set_used(triangles.size() * 3);

    for (size_t i = 0; i < triangles.size(); ++i) {
        const auto& tri = triangles[i];
        const glm::vec3* verts[3] = { &tri.v1, &tri.v2, &tri.v3 };

        // Face normal orientation determines base color:
        // - Floors (normal.y > 0.7): green/brown terrain tones
        // - Walls (|normal.y| < 0.3): gray/blue stone tones
        // - Ceilings (normal.y < -0.7): dark purple
        // - Slopes: blended between categories
        float ny = tri.normal.y;  // Y-up: vertical component
        uint8_t r, g, b;

        if (ny > 0.7f) {
            // Floor: earthy green-brown, brighter = more horizontal
            uint8_t base = static_cast<uint8_t>(100 + ny * 60);
            r = static_cast<uint8_t>(base * 0.7f);
            g = base;
            b = static_cast<uint8_t>(base * 0.5f);
        } else if (ny < -0.7f) {
            // Ceiling: dark muted purple
            uint8_t base = static_cast<uint8_t>(60 + (-ny - 0.7f) * 40);
            r = static_cast<uint8_t>(base * 0.8f);
            g = static_cast<uint8_t>(base * 0.5f);
            b = base;
        } else {
            // Wall: gray-blue, vary by which horizontal direction the wall faces
            float nx = std::abs(tri.normal.x);
            float nz = std::abs(tri.normal.z);
            uint8_t base = static_cast<uint8_t>(90 + (nx + nz) * 50);
            // X-facing walls slightly warmer, Z-facing walls slightly cooler
            r = static_cast<uint8_t>(base * (0.7f + nx * 0.2f));
            g = static_cast<uint8_t>(base * 0.75f);
            b = static_cast<uint8_t>(base * (0.8f + nz * 0.2f));
        }

        // Per-vertex height variation within each triangle for edge visibility
        float minVY = std::min({verts[0]->y, verts[1]->y, verts[2]->y});
        float maxVY = std::max({verts[0]->y, verts[1]->y, verts[2]->y});
        float triYRange = maxVY - minVY;

        for (int j = 0; j < 3; ++j) {
            uint32_t idx = static_cast<uint32_t>(i * 3 + j);
            auto& vert = buf->Vertices[idx];
            vert.Pos.X = verts[j]->x;
            vert.Pos.Y = verts[j]->y;
            vert.Pos.Z = verts[j]->z;
            vert.Normal.X = tri.normal.x;
            vert.Normal.Y = tri.normal.y;
            vert.Normal.Z = tri.normal.z;
            vert.TCoords.X = 0;
            vert.TCoords.Y = 0;

            // Darken lower vertices within each triangle for edge definition
            float heightMod = 1.0f;
            if (triYRange > 0.1f) {
                heightMod = 0.85f + 0.15f * ((verts[j]->y - minVY) / triYRange);
            }

            vert.Color = irr::video::SColor(255,
                static_cast<uint8_t>(std::min(255.0f, r * heightMod)),
                static_cast<uint8_t>(std::min(255.0f, g * heightMod)),
                static_cast<uint8_t>(std::min(255.0f, b * heightMod)));

            buf->Indices[idx] = static_cast<irr::u16>(idx);
        }
    }

    buf->recalculateBoundingBox();
    buf->setHardwareMappingHint(irr::scene::EHM_STATIC);

    // Material: unlit, solid, no backface culling
    buf->Material.Lighting = false;
    buf->Material.MaterialType = irr::video::EMT_SOLID;
    buf->Material.BackfaceCulling = false;
#ifndef EQT_HAS_GLES2
    // Desktop GL: wireframe for classic look
    buf->Material.Wireframe = true;
#endif

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();

    zonePlaceholderNode_ = smgr_->addMeshSceneNode(mesh);
    mesh->drop();

    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->setPosition(irr::core::vector3df(0, 0, 0));
        lastPlaceholderBuildPos_ = irr::core::vector3df(playerIrrX, playerIrrY, playerIrrZ);
        if (usePvsFilter) {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles (PVS culled {}, clip {:.0f}, {} total, {:.1f} KB)",
                     triangles.size(), pvsCulledCount, clipDist, cachedPlaceholderTriangles_.size(),
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        } else {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles within clip {:.0f} ({} total, {:.1f} KB)",
                     triangles.size(), clipDist, cachedPlaceholderTriangles_.size(),
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        }
    }
}

void IrrlichtRenderer::destroyZonePlaceholder() {
    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->remove();
        zonePlaceholderNode_ = nullptr;
        LOG_INFO(MOD_GRAPHICS, "Zone placeholder removed");
    }
    // Free cached triangles — no longer needed once real geometry takes over
    if (!cachedPlaceholderTriangles_.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Releasing {} cached placeholder triangles ({:.1f} KB)",
                  cachedPlaceholderTriangles_.size(),
                  (cachedPlaceholderTriangles_.size() * sizeof(HCMap::Triangle)) / 1024.0f);
        cachedPlaceholderTriangles_.clear();
        cachedPlaceholderTriangles_.shrink_to_fit();
    }
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
                                                zoneShader_->getActiveAlphaTest());
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

void IrrlichtRenderer::setupInstantScene(const std::string& zoneName, float playerX, float playerY, float playerZ) {
    currentZoneName_ = zoneName;
    loadIndoorRegionMap(zoneName);

    // S05: entityRenderer_ already created in initLoadingScreen()

    // Create door manager for placeholder cubes (no model loading yet)
    if (!doorManager_ && smgr_ && driver_) {
        doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
        LOG_INFO(MOD_GRAPHICS, "Door manager created for instant scene (placeholder cubes only)");
    }

    // Notify entity renderer of zone (for zone-specific model search paths)
    if (entityRenderer_) {
        entityRenderer_->setCurrentZone(zoneName);
    }

    // Build HCMap placeholder around player position, clipped to render distance
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        // Convert EQ coords (Z-up) to Irrlicht (Y-up) for the builder
        buildZonePlaceholder(playerX, playerZ, playerY);
    }

    // Minimal collision from HCMap for player movement
    setupHCMapCollision();

    // Set zone ready flags
    zoneReady_ = true;

    // In automatic mode, asset loading is handled by the loading thread
    // via loadZoneSequential() after setupInstantScene() returns.
    // In manual mode, loading is deferred until /loadzone command.
    if (config_.constrainedConfig.deferredAssetLoading && !config_.eqClientPath.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — automatic mode, loading thread handles asset load",
                 zoneName);
    } else {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — HCMap placeholder + collision, awaiting /loadzone",
                 zoneName);
    }
}

void IrrlichtRenderer::setupHCMapCollision() {
    if (!smgr_) return;

    // Clean up all collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    if (zoneTriangleSelector_) { zoneTriangleSelector_->drop(); zoneTriangleSelector_ = nullptr; }
    if (terrainOnlySelector_) { terrainOnlySelector_->drop(); terrainOnlySelector_ = nullptr; }

    irr::scene::IMetaTriangleSelector* metaSelector = smgr_->createMetaTriangleSelector();
    if (!metaSelector) return;

    if (zonePlaceholderNode_ && zonePlaceholderNode_->getMesh()) {
        auto* sel = smgr_->createTriangleSelector(zonePlaceholderNode_->getMesh(), zonePlaceholderNode_);
        if (sel) { metaSelector->addTriangleSelector(sel); sel->drop(); }
    }

    zoneTriangleSelector_ = metaSelector;

    terrainOnlySelector_ = smgr_->createMetaTriangleSelector();
    if (terrainOnlySelector_ && zonePlaceholderNode_ && zonePlaceholderNode_->getMesh()) {
        auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(terrainOnlySelector_);
        auto* sel = smgr_->createTriangleSelector(zonePlaceholderNode_->getMesh(), zonePlaceholderNode_);
        if (sel) { terrainMeta->addTriangleSelector(sel); sel->drop(); }
    }

    collisionManager_ = smgr_->getSceneCollisionManager();
    if (cameraController_ && collisionManager_ && zoneTriangleSelector_) {
        cameraController_->setCollisionManager(collisionManager_, zoneTriangleSelector_);
    }

    LOG_INFO(MOD_GRAPHICS, "HCMap collision setup complete");
}

void IrrlichtRenderer::storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                              const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                              const float fogMinClip[4], const float fogMaxClip[4]) {
    storedZoneEnvironment_.skyType = skyType;
    storedZoneEnvironment_.zoneType = zoneType;
    for (int i = 0; i < 4; ++i) {
        storedZoneEnvironment_.fogR[i] = fogRed[i];
        storedZoneEnvironment_.fogG[i] = fogGreen[i];
        storedZoneEnvironment_.fogB[i] = fogBlue[i];
        storedZoneEnvironment_.fogMinClip[i] = fogMinClip[i];
        storedZoneEnvironment_.fogMaxClip[i] = fogMaxClip[i];
    }
    storedZoneEnvironment_.pending = true;
}

void IrrlichtRenderer::applyStoredZoneEnvironment() {
    if (storedZoneEnvironment_.pending) {
        setZoneEnvironment(storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType,
                           storedZoneEnvironment_.fogR, storedZoneEnvironment_.fogG, storedZoneEnvironment_.fogB,
                           storedZoneEnvironment_.fogMinClip, storedZoneEnvironment_.fogMaxClip);
        storedZoneEnvironment_.pending = false;
        LOG_INFO(MOD_GRAPHICS, "Applied stored zone environment (sky={}, ztype={})",
                 storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType);
    }
}

// Decode BMP data to A8R8G8B8 pixel array (used by sky texture loading)
// T14: decodeBMPtoARGB and bilinearUpscaleARGB removed.
// All texture decoding now goes through ConstrainedTextureCache::getOrLoad().

void IrrlichtRenderer::loadZoneSequential(const std::string& eqClientPath,
                                           LoadingStatus& status) {
    auto totalStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: starting for zone '{}'", currentZoneName_);
    loadFailed_ = false;

    auto updateProgress = [&](int percent, const std::string& text) {
        status.setProgress(percent, text);
        LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: [{}%] {}", percent, text);
        // Render a loading frame so the progress bar updates visually
        std::wstring wtext(text.begin(), text.end());
        drawLoadingScreen(percent / 100.0f, wtext);
    };

    // ── Setup ────────────────────────────────────────────────────────────────
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: zone='{}', eqClientPath='{}'", currentZoneName_, eqClientPath);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: particleManager_={}, zoneShader_={}, "
              "simulationWorker_={}, entityRenderer_={}, doorManager_={}",
              (bool)particleManager_, (bool)zoneShader_,
              (bool)simulationWorker_, (bool)entityRenderer_, (bool)doorManager_);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, "
              "maxFireGlowLights_={}, fireEffectsEnabled_={}, fireGlowLights_.size={}",
              fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
              maxFireGlowLights_, fireEffectsEnabled_, fireGlowLights_.size());
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: constrainedConfig: skipObjectBuild={}, "
              "eqe1Path='{}', deferredAssetLoading={}",
              config_.constrainedConfig.skipObjectBuild,
              config_.constrainedConfig.eqe1Path,
              config_.constrainedConfig.deferredAssetLoading);

    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: stopping simulation worker (running={})",
              simulationWorker_ ? simulationWorker_->isRunning() : false);
    stopSimulationWorker();
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: simulation worker stopped");
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();
    entityPrepReady_ = false;
    entityPrepScanCounter_ = 0;
    pendingZoneComputations_ = std::make_unique<PendingZoneComputations>();

    std::string eqPath = eqClientPath;
    if (!eqPath.empty() && eqPath.back() != '/' && eqPath.back() != '\\')
        eqPath += '/';

    // ── Step 0: EQE1 — open flat pack files (if configured) ────────────────
    if (!config_.constrainedConfig.eqe1Path.empty()) {
        updateProgress(48, "Opening EQE1 textures...");

        auto archiveList = resolveEQE1Archives(currentZoneName_);
        for (const auto& name : archiveList.zoneArchives) {
            getOrOpenEQE1(name);
        }
        for (const auto& name : archiveList.globalArchives) {
            getOrOpenEQE1(name);
        }

        // Load composite entity EQE1 files (from entity_composite_builder --eqe1 output)
        if (config_.constrainedConfig.enableCompositeEntities &&
            !config_.constrainedConfig.compositeEntityPath.empty()) {
            namespace fs = std::filesystem;
            const auto& compositeDir = config_.constrainedConfig.compositeEntityPath;
            if (fs::is_directory(compositeDir)) {
                size_t compositeCount = 0;
                for (const auto& entry : fs::directory_iterator(compositeDir)) {
                    if (entry.path().extension() == ".eqe1") {
                        std::string baseName = entry.path().stem().string();
                        // Skip if already loaded (shouldn't happen, but be safe)
                        std::lock_guard<std::mutex> lock(eqe1Mutex_);
                        if (eqe1Loaders_.find(baseName) != eqe1Loaders_.end()) continue;

                        auto loader = std::make_unique<EQE1Loader>();
                        if (loader->open(entry.path().string())) {
                            const EQE1Loader* ptr = loader.get();
                            eqe1Loaders_[baseName] = std::move(loader);
                            if (constrainedTextureCache_) {
                                constrainedTextureCache_->addEQE1Loader(ptr);
                            }
                            ++compositeCount;
                        }
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "EQE1: loaded {} composite entity archives from '{}'",
                         compositeCount, compositeDir);
            }
        }
    }

    // ── Step 1: S3D — parse zone archive ───────────────────────────────────
    updateProgress(50, "Loading zone data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        std::string zonePath = eqPath + currentZoneName_ + ".s3d";
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loading S3D from '{}'", zonePath);

        S3DLoadOptions loadOptions;
        loadOptions.loadCharacters = false;
        loadOptions.computeCombinedGeometry = true;
        loadOptions.loadObjects = !config_.constrainedConfig.skipObjectBuild;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loadOptions: chars={}, combinedGeom={}, objects={}",
                  loadOptions.loadCharacters, loadOptions.computeCombinedGeometry, loadOptions.loadObjects);

        S3DLoader loader;
        if (!loader.loadZone(zonePath, loadOptions)) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load zone S3D archive: {} ({})", zonePath, loader.getError());
            pendingZoneComputations_.reset();
            loadFailed_ = true;
            return;
        }
        currentZone_ = loader.getZone();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: S3D loaded: objects={}, lights={}, textures={}, "
                  "wldLoader={}, geometry={}",
                  currentZone_ ? currentZone_->objects.size() : 0,
                  currentZone_ ? currentZone_->lights.size() : 0,
                  currentZone_ ? currentZone_->textures.size() : 0,
                  currentZone_ ? (bool)currentZone_->wldLoader : false,
                  currentZone_ ? (bool)currentZone_->geometry : false);
        logAssetBuildTime("seq_s3d_parse", 0, stepStart);
    }
    FlushThreadLog();

    // Install S3D data
    if (!currentZone_) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneSequential: S3D produced no data");
        pendingZoneComputations_.reset();
        loadFailed_ = true;
        return;
    }
    if (entityRenderer_) {
        entityRenderer_->setCurrentZone(currentZoneName_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: entityRenderer_ zone set to '{}'", currentZoneName_);
    }
    if (doorManager_) {
        doorManager_->setZone(currentZone_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ zone set");
        if (constrainedTextureCache_) {
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ constrained texture cache set");
        }
        if (zoneShader_ && zoneShader_->isAvailable()) {
            doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ shader materials set (solid={}, alphaTest={})",
                      zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
        }
    }

    // ── Step 2: BSP — compute spatial data ─────────────────────────────────
    updateProgress(55, "Computing spatial data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        auto zone = currentZone_;
        bool skipObjectBuild = config_.constrainedConfig.skipObjectBuild;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: zone={}, wldLoader={}, skipObjectBuild={}",
                  (bool)zone, zone ? (bool)zone->wldLoader : false, skipObjectBuild);

        if (zone && zone->wldLoader) {
            auto wldLoader = zone->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: bspTree={}, regions={}, hasPvsData={}",
                      (bool)bspTree, bspTree ? bspTree->regions.size() : 0,
                      wldLoader->hasPvsData());
            if (bspTree && !bspTree->regions.empty() && wldLoader->hasPvsData()) {
                // Compute region bounding boxes
                size_t regionsSkipped = 0;
                for (size_t i = 0; i < bspTree->regions.size(); ++i) {
                    auto geom = wldLoader->getGeometryForRegion(i);
                    if (!geom || geom->vertices.empty()) { ++regionsSkipped; continue; }

                    float vMinX = std::numeric_limits<float>::max();
                    float vMinY = vMinX, vMinZ = vMinX;
                    float vMaxX = std::numeric_limits<float>::lowest();
                    float vMaxY = vMaxX, vMaxZ = vMaxX;
                    for (const auto& v : geom->vertices) {
                        float wx = geom->centerX + v.x;
                        float wy = geom->centerY + v.y;
                        float wz = geom->centerZ + v.z;
                        if (wx < vMinX) vMinX = wx; if (wy < vMinY) vMinY = wy; if (wz < vMinZ) vMinZ = wz;
                        if (wx > vMaxX) vMaxX = wx; if (wy > vMaxY) vMaxY = wy; if (wz > vMaxZ) vMaxZ = wz;
                    }
                    irr::core::aabbox3df worldBounds;
                    worldBounds.MinEdge = irr::core::vector3df(vMinX, vMinY, vMinZ);
                    worldBounds.MaxEdge = irr::core::vector3df(vMaxX, vMaxY, vMaxZ);
                    computations->regionBoundingBoxes[i] = worldBounds;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: computed {} region bounding boxes ({} regions skipped, no geometry)",
                         computations->regionBoundingBoxes.size(), regionsSkipped);

                // Compute zone bounds from region bounding boxes (EQ coords)
                float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
                float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;
                for (const auto& [idx, bbox] : computations->regionBoundingBoxes) {
                    zbMinX = std::min(zbMinX, bbox.MinEdge.X); zbMinY = std::min(zbMinY, bbox.MinEdge.Y); zbMinZ = std::min(zbMinZ, bbox.MinEdge.Z);
                    zbMaxX = std::max(zbMaxX, bbox.MaxEdge.X); zbMaxY = std::max(zbMaxY, bbox.MaxEdge.Y); zbMaxZ = std::max(zbMaxZ, bbox.MaxEdge.Z);
                }

                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: zone bounds: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}] Z=[{:.1f},{:.1f}]",
                          zbMinX, zbMaxX, zbMinY, zbMaxY, zbMinZ, zbMaxZ);

                // Build portal system from BSP split planes
                computations->portalSystem = std::make_unique<PortalSystem>();
                computations->portalSystem->buildFromBsp(*bspTree, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
                computations->portalOcclusionEligible = computations->portalSystem->hasPortals() &&
                    (computations->portalSystem->getData().portals.size() > 10);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: portal system: hasPortals={}, portalCount={}, occlusionEligible={}",
                          computations->portalSystem->hasPortals(),
                          computations->portalSystem->getData().portals.size(),
                          computations->portalOcclusionEligible);

                // Cache zone light BSP regions
                for (size_t i = 0; i < zone->lights.size(); ++i) {
                    const auto& light = zone->lights[i];
                    size_t regionIdx = bspTree->findRegionIndexForPoint(light->x, light->y, light->z);
                    computations->zoneLightRegions.push_back(regionIdx);
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: cached {} zone light BSP regions", computations->zoneLightRegions.size());
            }

            // Index objects with BSP regions
            if (!skipObjectBuild) {
                auto bspTree2 = wldLoader->getBspTree();
                if (bspTree2) {
                    for (size_t i = 0; i < zone->objects.size(); ++i) {
                        const auto& objInstance = zone->objects[i];
                        if (!objInstance.geometry || !objInstance.placeable) continue;
                        float x = objInstance.placeable->getX();
                        float y = objInstance.placeable->getY();
                        float z = objInstance.placeable->getZ();
                        size_t bspRegion = bspTree2->findRegionIndexForPoint(x, y, z);
                        computations->deferredObjectEntries.emplace_back(i, bspRegion);
                    }
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: object BSP indexing: {} objects, {} entries (bspTree={})",
                          zone->objects.size(), computations->deferredObjectEntries.size(), (bool)bspTree2);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: object BSP indexing skipped (skipObjectBuild=true)");
            }

            // Pre-build deferred objects with tree filtering + world bounds
            if (!computations->deferredObjectEntries.empty()) {
                for (auto& [objIdx, bspRegion] : computations->deferredObjectEntries) {
                    if (objIdx >= zone->objects.size()) continue;
                    const auto& objInstance = zone->objects[objIdx];
                    if (!objInstance.geometry || !objInstance.placeable) continue;

                    DeferredObject deferred;
                    deferred.objectIndex = objIdx;
                    deferred.bspRegion = bspRegion;
                    deferred.meshBuilt = false;
                    float x = objInstance.placeable->getX();
                    float y = objInstance.placeable->getY();
                    float z = objInstance.placeable->getZ();
                    const auto& geom = objInstance.geometry;
                    float scaleX = objInstance.placeable->getScaleX();
                    float scaleY = objInstance.placeable->getScaleY();
                    float scaleZ = objInstance.placeable->getScaleZ();
                    float halfW = std::max(std::abs(geom->maxX - geom->minX),
                                           std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
                    float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
                    deferred.worldBounds = irr::core::aabbox3df(
                        x - halfW, z - halfH, y - halfW,
                        x + halfW, z + halfH, y + halfW);
                    computations->prebuiltDeferredObjects.push_back(deferred);
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: {} deferred object entries, {} prebuilt",
                          computations->deferredObjectEntries.size(), computations->prebuiltDeferredObjects.size());
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: BSP compute skipped (zone={}, wldLoader={})",
                      (bool)zone, zone ? (bool)zone->wldLoader : false);
        }
        logAssetBuildTime("seq_bsp_compute", 0, stepStart);
    }
    FlushThreadLog();

    // Install BSP data
    {
        auto stepStart = std::chrono::steady_clock::now();
        if (!currentZone_ || !currentZone_->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no wldLoader, fallback to single zone mesh");
            createZoneMesh();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-wldLoader fallback), zoneMeshNode_={}",
                      (bool)zoneMeshNode_);
        } else {
            auto wldLoader = currentZone_->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            if (!bspTree || bspTree->regions.empty() || !wldLoader->hasPvsData()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no BSP/PVS data, fallback to single zone mesh "
                          "(bspTree={}, regions={}, hasPvs={})",
                          (bool)bspTree, bspTree ? bspTree->regions.size() : 0, wldLoader->hasPvsData());
                if (currentZone_ && !currentZone_->geometry && currentZone_->wldLoader)
                    currentZone_->geometry = currentZone_->wldLoader->getCombinedGeometry();
                createZoneMesh();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-BSP/PVS fallback), zoneMeshNode_={}",
                          (bool)zoneMeshNode_);
            } else {
                // Clean up existing mesh nodes
                size_t oldRegionNodes = regionMeshNodes_.size();
                if (zoneMeshNode_) { zoneMeshNode_->remove(); zoneMeshNode_ = nullptr; }
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node) { if (node->getParent()) node->remove(); else node->drop(); }
                }
                regionMeshNodes_.clear();
                regionBoundingBoxes_.clear();
                if (fallbackMeshNode_) {
                    if (fallbackMeshNode_->getParent()) fallbackMeshNode_->remove(); else fallbackMeshNode_->drop();
                    fallbackMeshNode_ = nullptr;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: cleaned up existing mesh nodes "
                          "(zoneMeshNode removed, {} region nodes removed, fallback removed)",
                          oldRegionNodes);

                // Install BSP tree
                if (!zoneBspTree_) {
                    zoneBspTree_ = bspTree;
                    usePvsCulling_ = true;
                    currentPvsRegion_ = SIZE_MAX;
                    // Size protectedRegions bitvector to cover all BSP regions
                    protectedRegions_.resize(bspTree->regions.size(), false);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: BSP tree installed, usePvsCulling=true, "
                              "regions={}, protectedRegions size={}",
                              bspTree->regions.size(), protectedRegions_.size());
                }

                // Install pre-computed bounding boxes
                if (regionBoundingBoxes_.empty() && pendingZoneComputations_ &&
                    !pendingZoneComputations_->regionBoundingBoxes.empty()) {
                    regionBoundingBoxes_ = std::move(pendingZoneComputations_->regionBoundingBoxes);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: installed {} region bounding boxes",
                              regionBoundingBoxes_.size());
                }

                if (entityRenderer_) {
                    entityRenderer_->setBspTree(zoneBspTree_);
                    entityRenderer_->setFrustumCuller(frustumCuller_.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: entityRenderer BSP tree and frustum culler set");
                }
                if (doorManager_) {
                    doorManager_->setBspTree(zoneBspTree_.get());
                    doorManager_->setPvsRegion(currentPvsRegion_);
                    doorManager_->setFrustumCuller(frustumCuller_.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager BSP tree, PVS region, and frustum culler set");
                }
            }
        }

        // Install portal system
        if (!portalSystem_ && pendingZoneComputations_ && pendingZoneComputations_->portalSystem) {
            portalSystem_ = std::move(pendingZoneComputations_->portalSystem);
            portalOcclusionEligible_ = pendingZoneComputations_->portalOcclusionEligible;
            portalBuildPending_ = false;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system installed, occlusionEligible={}",
                      portalOcclusionEligible_);

            // Auto-enable portal occlusion from config.
            // When per-region indoor map is loaded, portal occlusion is dynamically
            // toggled per-frame based on camera region (see simulation output apply).
            // Otherwise, use zone-level portal-to-region ratio heuristic.
            if (portalOcclusionEligible_ && config_.constrainedConfig.portalOcclusion &&
                config_.constrainedConfig.enableStencilBuffer) {
                if (!indoorRegions_.empty()) {
                    // Per-region mode: start disabled, will auto-enable when camera enters indoor region
                    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode ({} indoor regions loaded, will toggle dynamically)",
                             indoorRegions_.size());
                } else {
                    // Fallback: zone-level heuristic. Outdoor zones have ~7+ portals/region
                    // (BSP adjacency artifacts); indoor zones have ~1-2 (actual doorways).
                    size_t portalCount = portalSystem_->getData().portals.size();
                    size_t regionCount = regionBoundingBoxes_.size();
                    float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                    if (portalRatio <= 3.0f) {
                        portalOcclusionEnabled_ = true;
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled: {}/{} portals/regions = {:.1f} ratio (indoor zone)",
                                 portalCount, regionCount, portalRatio);
                    } else {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled: {}/{} portals/regions = {:.1f} ratio (outdoor zone, threshold 3.0)",
                                 portalCount, regionCount, portalRatio);
                    }
                }
            }
        } else if (zoneBspTree_ && !regionBoundingBoxes_.empty() && !portalSystem_) {
            portalBuildPending_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system deferred (portalBuildPending=true)");
        }
        if (regionNeighbors_.empty()) {
            buildRegionNeighborMap();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: built region neighbor map, {} entries", regionNeighbors_.size());
        }
        if (doorManager_) {
            doorManager_->setRegionNeighbors(regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager region neighbors set ({})",
                      regionNeighbors_.empty() ? "null" : std::to_string(regionNeighbors_.size()));
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: zoneBspTree_={}, usePvsCulling_={}, "
                  "regionBoundingBoxes_={}, portalSystem_={}, portalOcclusionEligible_={}, regionNeighbors_={}",
                  (bool)zoneBspTree_, usePvsCulling_, regionBoundingBoxes_.size(),
                  (bool)portalSystem_, portalOcclusionEligible_, regionNeighbors_.size());
        logAssetBuildTime("seq_bsp_install", 0, stepStart);
    }
    FlushThreadLog();

    // Step 3 (zone texture atlas) removed — E10: EQE1 flat packs replace atlas system.
    // Zone textures are loaded as individual ETC1 textures via constrained cache.
    FlushThreadLog();

    // ── Step 4: Regions — build ALL zone meshes eagerly ────────────────────
    updateProgress(63, "Building zone meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: currentZone_={}, wldLoader={}, zoneBspTree_={}",
                  (bool)currentZone_, currentZone_ ? (bool)currentZone_->wldLoader : false, (bool)zoneBspTree_);

        if (currentZone_ && currentZone_->wldLoader && zoneBspTree_) {
            auto wldLoader = currentZone_->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
            if (constrainedTextureCache_)
                builder.setConstrainedTextureCache(constrainedTextureCache_);
            if (zoneShader_ && zoneShader_->isAvailable()) {
                builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                               zoneShader_->getActiveAlphaTest());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: builder shader materials set (solid={}, alphaTest={})",
                          zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: totalRegions={}", bspTree->regions.size());

            size_t totalRegions = bspTree->regions.size();
            size_t regionsBuilt = 0;
            size_t regionsSkipped = 0;
            size_t meshBuildFailed = 0;
            size_t nodeFailed = 0;
            for (size_t i = 0; i < totalRegions; ++i) {
                auto geom = wldLoader->getGeometryForRegion(i);
                if (!geom || geom->vertices.empty()) { ++regionsBuilt; ++regionsSkipped; continue; }

                irr::scene::IMesh* mesh = nullptr;
                if (!currentZone_->textures.empty() && !geom->textureNames().empty()) {
                    mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built textured mesh, texNames={}, verts={}",
                              i, geom->textureNames().size(), geom->vertices.size());
                } else {
                    mesh = builder.buildColoredMesh(*geom);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built colored mesh (no textures), verts={}",
                              i, geom->vertices.size());
                }
                if (!mesh) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step4: region[{}] mesh build returned null — skipping", i);
                    ++meshBuildFailed;
                    continue;
                }

                irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
                if (node) {
                    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
                    // Force AbsoluteTransformation update — same as rebuildRegionMesh().
                    // Without this, triangle selectors in setupMinimalZoneCollision() use
                    // stale identity transforms, placing collision geometry at origin.
                    node->updateAbsolutePosition();
                    for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
                        node->getMaterial(m).Lighting = lightingEnabled_;
                        node->getMaterial(m).BackfaceCulling = false;
                        node->getMaterial(m).GouraudShading = true;
                        node->getMaterial(m).FogEnable = fogEnabled_;
                        node->getMaterial(m).Wireframe = wireframeMode_;
                        node->getMaterial(m).NormalizeNormals = true;
                        node->getMaterial(m).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        node->getMaterial(m).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                    regionMeshNodes_[i] = node;
                    uploadMeshHardwareBuffers(node);
                    if (regionBoundingBoxes_.find(i) == regionBoundingBoxes_.end())
                        regionBoundingBoxes_[i] = node->getTransformedBoundingBox();
                } else {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step4: region[{}] addMeshSceneNode failed — mesh dropped", i);
                    ++nodeFailed;
                }
                mesh->drop();
                ++regionsBuilt;
                // Update progress every 20 regions (63-70%)
                if (regionsBuilt % 20 == 0 && totalRegions > 0) {
                    int pct = 63 + static_cast<int>(7.0f * regionsBuilt / totalRegions);
                    updateProgress(pct, "Building zone meshes [" + std::to_string(regionsBuilt)
                                        + "/" + std::to_string(totalRegions) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: built {} region meshes eagerly ({} skipped, {} mesh failures, {} node failures)",
                     regionMeshNodes_.size(), regionsSkipped, meshBuildFailed, nodeFailed);

            // Extract occluder triangles for software occlusion culling
            if (occlusionCuller_) {
                auto occStart = std::chrono::steady_clock::now();
                int totalOccluders = 0;
                int regionsWithOccluders = 0;
                float minArea = occlusionCuller_->getConfig().minOccluderArea;
                int maxPerRegion = occlusionCuller_->getConfig().maxTrianglesPerRegion;

                for (size_t ri = 0; ri < totalRegions; ++ri) {
                    auto geom = wldLoader->getGeometryForRegion(ri);
                    if (!geom || geom->vertices.empty() || geom->triangles.empty()) continue;

                    std::vector<OccluderTriangle> occluders;
                    for (const auto& tri : geom->triangles) {
                        if (tri.v1 >= geom->vertices.size() ||
                            tri.v2 >= geom->vertices.size() ||
                            tri.v3 >= geom->vertices.size()) continue;

                        const auto& va = geom->vertices[tri.v1];
                        const auto& vb = geom->vertices[tri.v2];
                        const auto& vc = geom->vertices[tri.v3];

                        // World-space positions (add region center offset)
                        float ax = geom->centerX + va.x, ay = geom->centerY + va.y, az = geom->centerZ + va.z;
                        float bx = geom->centerX + vb.x, by = geom->centerY + vb.y, bz = geom->centerZ + vb.z;
                        float cx = geom->centerX + vc.x, cy = geom->centerY + vc.y, cz = geom->centerZ + vc.z;

                        // Cross product for area
                        float ex = bx - ax, ey = by - ay, ez = bz - az;
                        float fx = cx - ax, fy = cy - ay, fz = cz - az;
                        float nx = ey * fz - ez * fy;
                        float ny = ez * fx - ex * fz;
                        float nz = ex * fy - ey * fx;
                        float area = 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz);

                        if (area < minArea) continue;

                        OccluderTriangle ot;
                        ot.v0[0] = ax; ot.v0[1] = ay; ot.v0[2] = az;
                        ot.v1[0] = bx; ot.v1[1] = by; ot.v1[2] = bz;
                        ot.v2[0] = cx; ot.v2[1] = cy; ot.v2[2] = cz;
                        ot.area = area;
                        occluders.push_back(ot);
                    }

                    // Keep only the largest triangles per region
                    if (static_cast<int>(occluders.size()) > maxPerRegion) {
                        std::sort(occluders.begin(), occluders.end(),
                                  [](const OccluderTriangle& a, const OccluderTriangle& b) {
                                      return a.area > b.area;
                                  });
                        occluders.resize(maxPerRegion);
                    }

                    if (!occluders.empty()) {
                        totalOccluders += occluders.size();
                        regionsWithOccluders++;
                        occlusionCuller_->setRegionOccluders(ri, std::move(occluders));
                    }
                }

                auto occEnd = std::chrono::steady_clock::now();
                LOG_INFO(MOD_GRAPHICS, "Sequential: extracted {} occluder triangles from {} regions ({}x{} depth buffer, {:.0f} ms)",
                         totalOccluders, regionsWithOccluders,
                         occlusionCuller_->getConfig().width, occlusionCuller_->getConfig().height,
                         std::chrono::duration<double, std::milli>(occEnd - occStart).count());
            }

            // Initialize animated texture manager for PVS region meshes
            // Discover animations from combined geometry, then register each region mesh's buffers
            if (currentZone_ && currentZone_->geometry && !currentZone_->textures.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: creating AnimatedTextureManager for PVS region meshes "
                          "(geometry texNames={}, texAnimations={}, zoneTextures={})",
                          currentZone_->geometry->textureNames().size(),
                          currentZone_->geometry->textureAnimations().size(),
                          currentZone_->textures.size());

                animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());
                if (constrainedTextureCache_) {
                    animatedTextureManager_->setConstrainedTextureCache(constrainedTextureCache_);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ constrained texture cache set");
                }

                // Discover animated textures from combined geometry (nullptr mesh — buffers added per-region below)
                int animCount = animatedTextureManager_->initialize(
                    *currentZone_->geometry, currentZone_->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ initialize found {} animated textures from geometry",
                          animCount);

                int waterAnimCount = animatedTextureManager_->detectWaterAnimations(
                    currentZone_->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ detectWaterAnimations found {} water textures",
                          waterAnimCount);
                animCount += waterAnimCount;

                // Register each region mesh's buffers with the animated texture manager.
                // PVS region meshes use addMesh (buffer materials) because they're drawn
                // via manual draw path, not through Irrlicht's scene graph.
                size_t registeredMeshes = 0;
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getMesh()) {
                        auto geom = wldLoader->getGeometryForRegion(regionIdx);
                        if (geom) {
                            animatedTextureManager_->addMesh(*geom, currentZone_->textures, node->getMesh());
                            registeredMeshes++;
                            LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] addMesh (buffers={}, texNames={})",
                                      regionIdx, node->getMesh()->getMeshBufferCount(), geom->textureNames().size());
                        }
                    }
                }

                LOG_INFO(MOD_GRAPHICS, "Sequential: initialized {} animated textures ({} water) for {} region meshes "
                         "({} meshes scanned)",
                         animCount, waterAnimCount, regionMeshNodes_.size(), registeredMeshes);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ skipped (currentZone_={}, geometry={}, textures={})",
                          (bool)currentZone_,
                          currentZone_ ? (bool)currentZone_->geometry : false,
                          currentZone_ ? currentZone_->textures.size() : 0);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: region mesh building skipped (currentZone_={}, wldLoader={}, zoneBspTree_={})",
                      (bool)currentZone_, currentZone_ ? (bool)currentZone_->wldLoader : false, (bool)zoneBspTree_);
        }

        // Setup front-to-back sorting
        if (usePvsCulling_ && !regionMeshNodes_.empty() && !config_.constrainedConfig.skipManualZoneDraw) {
#ifdef EQT_HAS_GLES2
            manualZoneDrawEnabled_ = true;
#else
            manualZoneDrawEnabled_ = (driver_->getDriverType() != irr::video::EDT_BURNINGSVIDEO);
#endif
            if (manualZoneDrawEnabled_) {
                // S05: renderPassTimer_ already created in initLoadingScreen()
                if (smgr_ && renderPassTimer_) {
                    renderPassTimer_->setRenderer(this);
                    smgr_->setLightManager(renderPassTimer_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: installed RenderPassTimer as light manager");
                }
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getParent()) { node->grab(); node->remove(); }
                }
                if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) {
                    fallbackMeshNode_->grab(); fallbackMeshNode_->remove();
                }
                LOG_INFO(MOD_GRAPHICS, "Sequential: front-to-back sorting enabled ({} regions)",
                         regionMeshNodes_.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: manual zone draw disabled (software renderer)");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: front-to-back sorting skipped (usePvsCulling_={}, regionMeshNodes_={}, skipManualZoneDraw={})",
                      usePvsCulling_, regionMeshNodes_.size(), config_.constrainedConfig.skipManualZoneDraw);
        }
        logAssetBuildTime("seq_regions", regionMeshNodes_.size(), stepStart);
    }
    FlushThreadLog();

    // ── Step 5: Assets — build indexes, create subsystems ──────────────────
    updateProgress(71, "Building asset indexes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: deferredAssetLoading={}, entityRenderer_={}",
                  config_.constrainedConfig.deferredAssetLoading, (bool)entityRenderer_);

        // Build archive index inline (was background thread)
        if (computations && config_.constrainedConfig.deferredAssetLoading) {
            computations->archiveIndex = std::make_unique<GraphicsArchiveIndex>();
            if (computations->archiveIndex->buildIndex(eqPath, config_.constrainedConfig.lazyPfsLoading)) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: archive index built ({} race entries, {} archives)",
                         computations->archiveIndex->getRaceEntryCount(),
                         computations->archiveIndex->getArchiveCount());
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: archive index build failed, discarded");
                computations->archiveIndex.reset();
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index build skipped (deferredAssetLoading={}, computations={})",
                      config_.constrainedConfig.deferredAssetLoading, (bool)computations);
        }

        // Build equipment index inline (was background thread)
        if (computations) {
            auto eqIdx = std::make_unique<PendingZoneComputations::EquipmentIndexData>();
            if (EquipmentModelLoader::buildEquipmentIndex(eqPath,
                    eqIdx->modelIndex, eqIdx->textureIndex)) {
                eqIdx->loaded = true;
                LOG_INFO(MOD_GRAPHICS, "Sequential: equipment index ({} models, {} textures)",
                         eqIdx->modelIndex.size(), eqIdx->textureIndex.size());
            }
            if (!eqIdx->loaded) {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: equipment index build failed");
            }
            computations->equipmentIndex = std::move(eqIdx);
        }

        // S05: entityRenderer_ already created in initLoadingScreen()
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: entityRenderer_={}", (bool)entityRenderer_);

        // Install archive index
        if (computations && computations->archiveIndex) {
            graphicsArchiveIndex_ = std::move(computations->archiveIndex);
            bool setOnRml = false;
            if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
                entityRenderer_->getRaceModelLoader()->setGraphicsArchiveIndex(graphicsArchiveIndex_.get());
                setOnRml = true;
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index installed, setOnRaceModelLoader={}", setOnRml);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index not installed (computations={}, archiveIndex={})",
                      (bool)computations, computations ? (bool)computations->archiveIndex : false);
        }

        // Install equipment index
        if (computations && computations->equipmentIndex &&
            computations->equipmentIndex->loaded && entityRenderer_) {
            if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                eml->adoptIndex(
                    std::move(computations->equipmentIndex->modelIndex),
                    std::move(computations->equipmentIndex->textureIndex),
                    std::map<uint32_t, int>());  // Item models via on-demand SQLite
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: equipment index installed on EquipmentModelLoader");
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: equipment index built but EquipmentModelLoader is null — cannot install");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: equipment index not installed (computations={}, loaded={}, entityRenderer_={})",
                      (bool)computations,
                      (computations && computations->equipmentIndex) ? computations->equipmentIndex->loaded : false,
                      (bool)entityRenderer_);
        }

        // Set zone on RaceModelLoader
        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setCurrentZone(currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: RaceModelLoader zone set to '{}'", currentZoneName_);
        }

        logAssetBuildTime("seq_assets", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 6: Objects — zone objects ──────────────────────────────────────
    updateProgress(75, "Installing zone objects...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ before={}", objectLights_.size());
        deferredObjects_.clear();

        if (pendingZoneComputations_ && !pendingZoneComputations_->prebuiltDeferredObjects.empty()) {
            deferredObjects_ = std::move(pendingZoneComputations_->prebuiltDeferredObjects);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: using prebuilt deferred objects, count={}", deferredObjects_.size());
        } else if (pendingZoneComputations_ && !pendingZoneComputations_->deferredObjectEntries.empty() && currentZone_) {
            for (auto& [objIdx, bspRegion] : pendingZoneComputations_->deferredObjectEntries) {
                if (objIdx >= currentZone_->objects.size()) continue;
                const auto& objInstance = currentZone_->objects[objIdx];
                if (!objInstance.geometry || !objInstance.placeable) continue;

                DeferredObject deferred;
                deferred.objectIndex = objIdx;
                deferred.bspRegion = bspRegion;
                deferred.meshBuilt = false;
                float x = objInstance.placeable->getX();
                float y = objInstance.placeable->getY();
                float z = objInstance.placeable->getZ();
                const auto& geom = objInstance.geometry;
                float scaleX = objInstance.placeable->getScaleX();
                float scaleY = objInstance.placeable->getScaleY();
                float scaleZ = objInstance.placeable->getScaleZ();
                float halfW = std::max(std::abs(geom->maxX - geom->minX),
                                       std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
                float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
                deferred.worldBounds = irr::core::aabbox3df(
                    x - halfW, z - halfH, y - halfW,
                    x + halfW, z + halfH, y + halfW);
                deferredObjects_.push_back(deferred);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: built deferred objects from entries, count={}", deferredObjects_.size());
        } else {
            indexObjectMeshes();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: used indexObjectMeshes() fallback");
        }

        // Zone bounds
        if (currentZone_ && currentZone_->geometry) {
            zoneBoundsMinX_ = currentZone_->geometry->minX;
            zoneBoundsMaxX_ = currentZone_->geometry->maxX;
            zoneBoundsMinY_ = currentZone_->geometry->minY;
            zoneBoundsMaxY_ = currentZone_->geometry->maxY;
            zoneBoundsValid_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from geometry: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      zoneBoundsMinX_, zoneBoundsMaxX_, zoneBoundsMinY_, zoneBoundsMaxY_);
        } else if (!regionBoundingBoxes_.empty()) {
            float rMinX = std::numeric_limits<float>::max(), rMaxX = std::numeric_limits<float>::lowest();
            float rMinY = std::numeric_limits<float>::max(), rMaxY = std::numeric_limits<float>::lowest();
            for (const auto& [idx, bb] : regionBoundingBoxes_) {
                rMinX = std::min(rMinX, bb.MinEdge.X); rMaxX = std::max(rMaxX, bb.MaxEdge.X);
                rMinY = std::min(rMinY, bb.MinEdge.Y); rMaxY = std::max(rMaxY, bb.MaxEdge.Y);
            }
            zoneBoundsMinX_ = rMinX; zoneBoundsMaxX_ = rMaxX;
            zoneBoundsMinY_ = rMinY; zoneBoundsMaxY_ = rMaxY;
            zoneBoundsValid_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from regionBoundingBoxes: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      zoneBoundsMinX_, zoneBoundsMaxX_, zoneBoundsMinY_, zoneBoundsMaxY_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds not available (no combined geometry and no region bounding boxes)");
        }

        // Build all object meshes eagerly inline (no deferred/PVS-gated build at runtime)
        {
            ZoneMeshBuilder objBuilder(smgr_, driver_, device_->getFileSystem());
            if (constrainedTextureCache_)
                objBuilder.setConstrainedTextureCache(constrainedTextureCache_);
            if (zoneShader_ && zoneShader_->isAvailable()) {
                objBuilder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                   zoneShader_->getActiveAlphaTest());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder shader materials set (solid={}, alphaTest={})",
                          zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
            }
            if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
                objBuilder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                                         zoneShader_->getActiveAtlasAlpha());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder atlas shader materials set (solid={}, alpha={})",
                          zoneShader_->getActiveAtlasSolid(), zoneShader_->getActiveAtlasAlpha());
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: totalObjects={}", deferredObjects_.size());

            size_t totalObjects = deferredObjects_.size();
            size_t objectsBuilt = 0;

            for (size_t oi = 0; oi < totalObjects; ++oi) {
            
                DeferredObject& deferred = deferredObjects_[oi];
                if (deferred.meshBuilt) { ++objectsBuilt; continue; }
                if (deferred.objectIndex >= currentZone_->objects.size()) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const auto& objInstance = currentZone_->objects[deferred.objectIndex];
                if (!objInstance.geometry || !objInstance.placeable) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const std::string& objName = objInstance.placeable->getName();

                // Build mesh
                irr::scene::IMesh* objMesh = nullptr;
                if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                    objMesh = objBuilder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built textured mesh, texNames={}",
                              oi, objName, objInstance.geometry->textureNames().size());
                } else {
                    objMesh = objBuilder.buildColoredMesh(*objInstance.geometry);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built colored mesh (no textures)", oi, objName);
                }

                // Track missing textures for rebuild when they arrive
                const auto& missingObjTex = objBuilder.getMissingTextures();
                if (!missingObjTex.empty()) {
                    for (const auto& texName : missingObjTex)
                        pendingTextureObjects_[texName].insert(oi);
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' has {} missing textures queued for later rebuild",
                             oi, objName, missingObjTex.size());
                }

                if (!objMesh) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' mesh build returned null — skipping", oi, objName);
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                irr::scene::IMeshSceneNode* objNode = smgr_->addMeshSceneNode(objMesh);
                if (!objNode) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' addMeshSceneNode failed — mesh dropped", oi, objName);
                    objMesh->drop(); deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                // Transform
                float scaleX = objInstance.placeable->getScaleX();
                float scaleY = objInstance.placeable->getScaleY();
                float scaleZ = objInstance.placeable->getScaleZ();
                objNode->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

                float ox = objInstance.placeable->getX();
                float oy = objInstance.placeable->getY();
                float oz = objInstance.placeable->getZ();
                objNode->setPosition(irr::core::vector3df(ox, oz, oy));
                objNode->setRotation(irr::core::vector3df(
                    objInstance.placeable->getRotateX(),
                    objInstance.placeable->getRotateY(),
                    objInstance.placeable->getRotateZ()));

                // Materials
                for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                    objNode->getMaterial(mi).Lighting = lightingEnabled_;
                    objNode->getMaterial(mi).BackfaceCulling = false;
                    objNode->getMaterial(mi).GouraudShading = true;
                    objNode->getMaterial(mi).FogEnable = fogEnabled_;
                    objNode->getMaterial(mi).Wireframe = wireframeMode_;
                    objNode->getMaterial(mi).NormalizeNormals = true;
                    objNode->getMaterial(mi).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    objNode->getMaterial(mi).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }

                // Note: treeManager_ wind shaders and animatedTextureManager_ are not yet
                // created (Step 11). A fixup pass after Step 11 applies these to objects.

                // Register vertex-animated objects (flags, banners)
                if (objInstance.geometry && objInstance.geometry->animatedVertices &&
                    objInstance.geometry->animatedVertices->frames.size() >= 2) {
                    auto& animData = objInstance.geometry->animatedVertices;
                    irr::scene::IMesh* objMesh = objNode->getMesh();
                    if (objMesh) {
                        VertexAnimatedMesh vam;
                        vam.node = objNode;
                        vam.mesh = objMesh;
                        vam.animData = animData;
                        vam.objectName = objName;

                        // Compute center offset: geometry vertices have center baked in,
                        // animation frame positions are raw. Offset = geomVertex - animFrame0.
                        const auto& frame0 = animData->frames[0];
                        size_t animVertCount = frame0.positions.size() / 3;
                        if (!objInstance.geometry->vertices.empty() && animVertCount > 0) {
                            const auto& gv = objInstance.geometry->vertices[0];
                            vam.centerOffsetX = gv.x - frame0.positions[0];
                            vam.centerOffsetY = gv.y - frame0.positions[1];
                            vam.centerOffsetZ = gv.z - frame0.positions[2];
                            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj '{}' vertex anim center offset: "
                                      "({:.2f},{:.2f},{:.2f}) from geom({:.2f},{:.2f},{:.2f}) - anim({:.2f},{:.2f},{:.2f})",
                                      objName, vam.centerOffsetX, vam.centerOffsetY, vam.centerOffsetZ,
                                      gv.x, gv.y, gv.z,
                                      frame0.positions[0], frame0.positions[1], frame0.positions[2]);
                        } else {
                            vam.centerOffsetX = 0;
                            vam.centerOffsetY = 0;
                            vam.centerOffsetZ = 0;
                            LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj '{}' vertex anim: cannot compute center offset "
                                     "(verts={}, animVerts={})", objName,
                                     objInstance.geometry->vertices.size(), animVertCount);
                        }

                        // Build vertex mapping: for each mesh buffer vertex, find the
                        // corresponding geometry vertex index (= animation vertex index)
                        // by matching positions (EQ coords → Irrlicht coords: x,z,y)
                        size_t expectedVerts = animData->frames[0].positions.size() / 3;
                        vam.vertexMapping.resize(objMesh->getMeshBufferCount());
                        size_t mappedVerts = 0;
                        for (irr::u32 b = 0; b < objMesh->getMeshBufferCount(); ++b) {
                            irr::scene::IMeshBuffer* buf = objMesh->getMeshBuffer(b);
                            auto* vertices = static_cast<irr::video::S3DVertex*>(buf->getVertices());
                            irr::u32 vertCount = buf->getVertexCount();
                            vam.vertexMapping[b].resize(vertCount, SIZE_MAX);

                            for (irr::u32 v = 0; v < vertCount; ++v) {
                                // Match by position: mesh stores (x, z, y) from geometry (x, y, z)
                                float mx = vertices[v].Pos.X;
                                float my = vertices[v].Pos.Y;  // = geom z
                                float mz = vertices[v].Pos.Z;  // = geom y
                                for (size_t gi = 0; gi < objInstance.geometry->vertices.size(); ++gi) {
                                    const auto& gv = objInstance.geometry->vertices[gi];
                                    if (std::abs(gv.x - mx) < 0.01f &&
                                        std::abs(gv.z - my) < 0.01f &&
                                        std::abs(gv.y - mz) < 0.01f) {
                                        if (gi < expectedVerts) {
                                            vam.vertexMapping[b][v] = gi;
                                            mappedVerts++;
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        vertexAnimatedMeshes_.push_back(std::move(vam));
                        LOG_INFO(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' registered as vertex-animated "
                                 "({} frames, {}ms delay, {} mapped/{} total verts)",
                                 oi, objName, animData->frames.size(), animData->delayMs,
                                 mappedVerts, objInstance.geometry->vertices.size());
                    }
                }

                objNode->setName(objName.c_str());
                objNode->grab();
                size_t newNodeIndex = objectNodes_.size();
                objectNodes_.push_back(objNode);
                objectPositions_.push_back(irr::core::vector3df(ox, oz, oy));
                if (zoneBspTree_)
                    objectRegions_.push_back(zoneBspTree_->findRegionIndexForPoint(ox, oy, oz));
                else
                    objectRegions_.push_back(SIZE_MAX);
                objNode->updateAbsolutePosition();
                objectBoundingBoxes_.push_back(objNode->getTransformedBoundingBox());
                deferred.nodeIndex = newNodeIndex;

                // PVS visibility (no SimWorker yet, so all visible).
                // When manualZoneDrawEnabled_, objects are always out of scene graph
                // and rendered via drawZoneGeometrySorted() instead.
                bool pvsVisible = isRegionPvsVisible(objectRegions_.back());
                if (!pvsVisible || manualZoneDrawEnabled_) {
                    objNode->remove();
                    if (!pvsVisible) {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' removed from scene graph (PVS hidden, region={})",
                                  oi, objName, objectRegions_.back());
                    }
                }
                objectInSceneGraph_.push_back(pvsVisible && !manualZoneDrawEnabled_);

                // Object lights (torch, lantern, fire, candle, etc.)
                std::string upperName = objName;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

                bool hasFireTexture = false;
                bool hasLanternTexture = false;
                if (objInstance.geometry) {
                    for (const auto& texName : objInstance.geometry->textureNames()) {
                        std::string upperTex = texName;
                        std::transform(upperTex.begin(), upperTex.end(), upperTex.begin(), ::toupper);
                        if (upperTex.find("FIRE") != std::string::npos ||
                            upperTex.find("COAL") != std::string::npos ||
                            upperTex.find("TORCH") != std::string::npos)
                            hasFireTexture = true;
                        if (upperTex.find("LANTERN") != std::string::npos ||
                            upperTex.find("LANT") != std::string::npos)
                            hasLanternTexture = true;
                    }
                }

                bool isLightSource = false;
                irr::video::SColorf lightColor(1.0f, 0.6f, 0.2f, 1.0f);
                float lightRadius = 100.0f;

                if (upperName.find("TORCH") != std::string::npos ||
                    upperName.find("FIRE") != std::string::npos ||
                    upperName.find("BRAZIER") != std::string::npos ||
                    upperName.find("FLAME") != std::string::npos ||
                    hasFireTexture) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(1.0f, 0.5f, 0.15f, 1.0f);
                    lightRadius = 120.0f;
                } else if (upperName.find("LANTERN") != std::string::npos ||
                           upperName.find("LANT") != std::string::npos ||
                           upperName.find("LAMP") != std::string::npos ||
                           upperName.find("LIGHT") != std::string::npos ||
                           hasLanternTexture) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(0.25f, 0.21f, 0.15f, 1.0f);
                    lightRadius = 100.0f;
                } else if (upperName.find("CANDLE") != std::string::npos) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(1.0f, 0.9f, 0.7f, 1.0f);
                    lightRadius = 50.0f;
                }

                if (isLightSource) {
                    irr::core::vector3df lightPos(ox, oz, oy);

                    // Find nearby zone light with elevated position
                    if (currentZone_ && !currentZone_->lights.empty()) {
                        float bestDist = 50.0f;
                        for (const auto& zoneLight : currentZone_->lights) {
                            float dx = zoneLight->x - ox;
                            float dy = zoneLight->y - oy;
                            float hDist = std::sqrt(dx * dx + dy * dy);
                            if (hDist < bestDist) {
                                bestDist = hDist;
                                lightPos = irr::core::vector3df(zoneLight->x, zoneLight->z, zoneLight->y);
                            }
                        }
                    }

                    irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
                        nullptr, lightPos, lightColor, lightRadius * 1.5f);

                    if (lightNode) {
                        irr::video::SLight& lightData = lightNode->getLightData();
                        lightData.Type = irr::video::ELT_POINT;
                        lightData.Attenuation = irr::core::vector3df(1.0f, 0.007f, 0.0002f);

                        lightNode->grab();
                        lightNode->remove();

                        ObjectLight objLight;
                        objLight.node = lightNode;
                        objLight.position = lightPos;
                        objLight.objectName = objName;
                        objLight.originalColor = lightColor;
                        objLight.bspRegion = objectRegions_.back();

                        if (upperName.find("TORCH") != std::string::npos ||
                            upperName.find("FIRE") != std::string::npos ||
                            upperName.find("BRAZIER") != std::string::npos ||
                            upperName.find("FLAME") != std::string::npos ||
                            upperName.find("CANDLE") != std::string::npos ||
                            hasFireTexture) {
                            objLight.isFireSource = true;
                            objLight.flickerPhase = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
                            objLight.flickerSpeed = 0.8f + static_cast<float>(rand()) / RAND_MAX * 0.4f;
                        }

                        bool lightPvsVis = isRegionPvsVisible(objLight.bspRegion);
                        if (lightPvsVis)
                            smgr_->getRootSceneNode()->addChild(lightNode);
                        objectLightInSceneGraph_.push_back(lightPvsVis);

                        objectLights_.push_back(objLight);
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' light created: pos=({:.1f},{:.1f},{:.1f}) "
                                  "radius={:.0f} fire={} pvsVisible={} region={}",
                                  oi, objName, lightPos.X, lightPos.Y, lightPos.Z,
                                  lightRadius, objLight.isFireSource, lightPvsVis, objLight.bspRegion);
                    } else {
                        LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' addLightSceneNode failed", oi, objName);
                    }
                }

                objMesh->drop();
                deferred.meshBuilt = true;
                ++objectsBuilt;

                if (objectsBuilt % 20 == 0 && totalObjects > 0) {
                    int pct = 75 + static_cast<int>(3.0f * objectsBuilt / totalObjects);
                    updateProgress(pct, "Building objects [" + std::to_string(objectsBuilt)
                                        + "/" + std::to_string(totalObjects) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: built all {} object meshes eagerly", objectsBuilt);
        }
        {
            int fireSrcCount = 0;
            for (const auto& ol : objectLights_) { if (ol.isFireSource) ++fireSrcCount; }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ after={}, fire sources from objects={}",
                      objectLights_.size(), fireSrcCount);
        }
        logAssetBuildTime("seq_objects", deferredObjects_.size(), stepStart);
    }
    FlushThreadLog();

    // ── Step 7: Doors — door meshes ────────────────────────────────────────
    updateProgress(78, "Building door meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: doorManager_={}", (bool)doorManager_);
        if (!doorManager_) {
            doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
            if (constrainedTextureCache_)
                doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
            if (currentZone_) doorManager_->setZone(currentZone_);
            if (zoneBspTree_) doorManager_->setBspTree(zoneBspTree_.get());
            doorManager_->setPvsRegion(currentPvsRegion_);
            if (frustumCuller_) doorManager_->setFrustumCuller(frustumCuller_.get());
            doorManager_->setRegionNeighbors(regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
            if (zoneShader_ && zoneShader_->isAvailable())
                doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                     zoneShader_->getActiveAlphaTest());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: created DoorManager (zone={}, bsp={}, frustum={}, neighbors={}, shader={})",
                      (bool)currentZone_, (bool)zoneBspTree_, (bool)frustumCuller_,
                      !regionNeighbors_.empty(), zoneShader_ && zoneShader_->isAvailable());
        }

        // Rebuild all doors at once
        if (doorManager_) {
            std::vector<uint8_t> doorList;
            doorManager_->getUnbuiltDoors(doorList);
            size_t unbuiltCount = doorList.size();
            size_t placeholderCount = 0;
            for (uint8_t id = 0; id < 255; ++id) {
                const auto* door = doorManager_->getDoor(id);
                if (door && door->meshBuilt && door->usePlaceholder) {
                    doorList.push_back(id);
                    ++placeholderCount;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: door rebuild list: {} unbuilt + {} placeholder = {} total",
                      unbuiltCount, placeholderCount, doorList.size());
            size_t totalDoors = doorList.size();
            size_t doorsBuilt = 0;
            size_t doorsFailed = 0;
            size_t doorsWithMissingTex = 0;
            for (uint8_t doorId : doorList) {
                bool rebuilt = doorManager_->rebuildSingleDoor(doorId);
                if (!rebuilt) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} rebuildSingleDoor failed", doorId);
                    ++doorsFailed;
                }
                const auto& missing = doorManager_->getLastMissingTextures();
                if (!missing.empty()) {
                    for (const auto& texName : missing)
                        pendingTextureDoors_[texName].insert(doorId);
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} has {} missing textures queued for later rebuild",
                             doorId, missing.size());
                    ++doorsWithMissingTex;
                }
                ++doorsBuilt;
                // Update progress every 10 doors (78-80%)
                if (doorsBuilt % 10 == 0 && totalDoors > 0) {
                    int pct = 78 + static_cast<int>(2.0f * doorsBuilt / totalDoors);
                    updateProgress(pct, "Building doors [" + std::to_string(doorsBuilt)
                                        + "/" + std::to_string(totalDoors) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: rebuilt {} doors ({} failed, {} with missing textures)",
                     totalDoors, doorsFailed, doorsWithMissingTex);
        }
        // Constrained cache was set at creation above — no separate runtime set needed.
        logAssetBuildTime("seq_doors", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 8: Entities — inline prep + build (no background threads) ────
    updateProgress(80, "Preparing entity models...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityRenderer_={}, skipEntityBuild={}",
                  (bool)entityRenderer_, config_.constrainedConfig.skipEntityBuild);

        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()
            && !config_.constrainedConfig.skipEntityBuild) {

            auto* modelLoader = entityRenderer_->getRaceModelLoader();
            auto* equipLoader = entityRenderer_->getEquipmentModelLoader();
            const auto& entities = entityRenderer_->getEntities();
            size_t totalEntities = entities.size();
            size_t prepCount = 0;
            size_t buildCount = 0;
            size_t modelCacheHits = 0;
            size_t modelDiskLoads = 0;

            LOG_INFO(MOD_GRAPHICS, "Sequential: preparing {} entities inline (chrCacheMaxEntries={})",
                     totalEntities, config_.constrainedConfig.chrCacheMaxEntries);

            // Collect spawn IDs (iterate copy since we modify visuals)
            std::vector<uint16_t> spawnIds;
            spawnIds.reserve(totalEntities);
            for (const auto& [spawnId, vis] : entities) {
                spawnIds.push_back(spawnId);
            }

            for (uint16_t spawnId : spawnIds) {
            
                const auto& entities2 = entityRenderer_->getEntities();
                auto it = entities2.find(spawnId);
                if (it == entities2.end()) continue;
                const auto& vis = it->second;

                // Update progress within entity phase (80-88%)
                if (totalEntities > 0) {
                    int pct = 80 + static_cast<int>(8.0f * prepCount / totalEntities);
                    updateProgress(pct, "Preparing entities [" + std::to_string(prepCount + 1)
                                        + "/" + std::to_string(totalEntities) + "]");
                }

                // ── Inline entity prep (EntityPrepWorker::processRequest logic) ──

                // Step 8a: Base race model prep (S3D load, WLD parse, animation merge)
                uint32_t key = (static_cast<uint32_t>(vis.raceId) << 8) | vis.gender;
                bool alreadyCached = modelLoader->isModelDataCached(key);
                bool success = false;
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8a: entity {} ({}) race={} gender={} cacheKey={} cached={}",
                          spawnId, vis.name, vis.raceId, vis.gender, key, alreadyCached);

                if (alreadyCached) {
                    success = true;
                    ++modelCacheHits;
                } else {
                    auto prepStart = std::chrono::steady_clock::now();
                    success = modelLoader->preloadModelData(vis.raceId, vis.gender);
                    auto prepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - prepStart).count();
                    if (success) {
                        ++modelDiskLoads;
                    } else {
                        LOG_FATAL(MOD_GRAPHICS, "Failed to load race model: race={} gender={} (not found in any S3D archive)", vis.raceId, vis.gender);
                        pendingZoneComputations_.reset();
                        loadFailed_ = true;
                        return;
                    }
                    LOG_INFO(MOD_GRAPHICS, "Sequential: preload race={} gender={} took {}ms success={}",
                             vis.raceId, vis.gender, prepMs, success);
                }

                // Step 8b: Variant model prep (zone-specific S3D for head/body variants)
                if (success) {
                    uint8_t headVariant = vis.appearance.helm;
                    uint8_t bodyVariant = 0;
                    uint8_t chestMaterial = static_cast<uint8_t>(
                        vis.appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
                    if (isRobeTexture(vis.appearance.texture) || isRobeTexture(chestMaterial)) {
                        bodyVariant = 1;
                    }
                    if (headVariant != 0 || bodyVariant != 0) {
                        modelLoader->preloadVariantModel(vis.raceId, vis.gender, headVariant, bodyVariant);
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8b: entity {} ({}) preloadVariantModel head={} body={}",
                                  spawnId, vis.name, headVariant, bodyVariant);
                    }
                }

                // Step 8c: Load variant textures via constrained cache
                // T06: Replaced inline DDS/BMP decoder with getOrLoad() calls.
                size_t variantTextureCount = 0;
                {
                    auto modelData = modelLoader->getRaceModelData(vis.raceId, vis.gender);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) appearance.texture={} helm={} modelData={}",
                              spawnId, vis.name, vis.appearance.texture, vis.appearance.helm, (bool)modelData);
                    if (vis.appearance.texture != 0 || vis.appearance.helm != 0) {
                        std::string raceCode = RaceModelLoader::getRaceCode(vis.raceId);
                        std::string lowerRaceCode = raceCode;
                        std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        std::set<std::string> loadedNames;
                        auto loadVariantTexture = [&](const std::string& texName, const std::shared_ptr<TextureInfo>& texInfo) {
                            if (!texInfo || texInfo->data.empty()) return;
                            std::string lowerName = texName;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            if (loadedNames.count(lowerName) > 0) return;
                            constrainedTextureCache_->getOrLoad(lowerName, texInfo->data);
                            loadedNames.insert(lowerName);
                            ++variantTextureCount;
                        };

                        // Build variant prefixes
                        std::vector<std::string> variantPrefixes;
                        if (vis.appearance.texture >= 10) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "clk%02d", vis.appearance.texture - 10);
                            variantPrefixes.push_back(buf);
                        } else if (vis.appearance.texture > 0) {
                            char buf[32];
                            const char* slots[] = {"ch", "ua", "fa", "hn", "lg", "ft"};
                            for (const char* slot : slots) {
                                snprintf(buf, sizeof(buf), "%s%s%02d", lowerRaceCode.c_str(), slot, vis.appearance.texture);
                                variantPrefixes.push_back(buf);
                            }
                        }
                        if (vis.appearance.helm > 0) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%she%02d", lowerRaceCode.c_str(), vis.appearance.helm);
                            variantPrefixes.push_back(buf);
                        }

                        auto searchTextures = [&](const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
                            for (const auto& [texName, texInfo] : textures) {
                                std::string ln = texName;
                                std::transform(ln.begin(), ln.end(), ln.begin(),
                                               [](unsigned char c) { return std::tolower(c); });
                                for (const auto& prefix : variantPrefixes) {
                                    if (ln.find(prefix) != std::string::npos) {
                                        loadVariantTexture(texName, texInfo);
                                        break;
                                    }
                                }
                            }
                        };

                        if (modelData && !modelData->textures.empty())
                            searchTextures(modelData->textures);

                        // Search variant model textures
                        uint8_t hv = vis.appearance.helm;
                        uint8_t bv2 = 0;
                        uint8_t cm = static_cast<uint8_t>(
                            vis.appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
                        if (isRobeTexture(vis.appearance.texture) || isRobeTexture(cm))
                            bv2 = 1;
                        if (hv != 0 || bv2 != 0) {
                            auto variantModelData = modelLoader->getVariantModelData(
                                vis.raceId, vis.gender, hv, bv2);
                            if (variantModelData && !variantModelData->textures.empty())
                                searchTextures(variantModelData->textures);
                        }
                    }
                }

                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) loaded {} variant textures via cache",
                          spawnId, vis.name, variantTextureCount);

                // Step 8d: Equipment model extraction + texture decode via constrained cache
                // T07: Replaced inline DDS decoder with getOrLoad() calls.
                struct EquipPrepData {
                    int modelId = 0;
                    uint32_t equipmentId = 0;
                    bool isPrimary = true;
                    std::shared_ptr<ZoneGeometry> geometry;
                    std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
                };
                std::vector<EquipPrepData> equipmentData;

                bool equipError = false;
                if (equipLoader) {
                    uint32_t primaryId = vis.appearance.equipment[7];
                    uint32_t secondaryId = vis.appearance.equipment[8];

                    auto prepOneEquip = [&](uint32_t equipmentId, bool isPrimary) {
                        if (equipError) return;
                        if (equipmentId == 0) return;
                        int modelId = equipLoader->getModelIdForItem(equipmentId);
                        if (modelId < 0) modelId = static_cast<int>(equipmentId);
                        const EquipmentModelLoader::EquipmentModelRef* modelRef = equipLoader->getModelRef(modelId);
                        if (!modelRef) {
                            LOG_FATAL(MOD_GRAPHICS, "Failed to load equipment model ref: entity {} ({}) equipment {} ({}) modelId={} (not found in item_models.json)",
                                      spawnId, vis.name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                            equipError = true;
                            return;
                        }
                        auto equipData2 = EquipmentModelLoader::extractEquipmentModelOffThread(*modelRef, modelId);
                        if (!equipData2) {
                            LOG_FATAL(MOD_GRAPHICS, "Failed to load equipment model: entity {} ({}) equipment {} ({}) modelId={}", spawnId, vis.name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                            equipError = true;
                            return;
                        }

                        EquipPrepData prepData;
                        prepData.modelId = modelId;
                        prepData.equipmentId = equipmentId;
                        prepData.isPrimary = isPrimary;
                        prepData.geometry = equipData2->geometry;
                        prepData.rawTextures = equipData2->textures;

                        // Load equipment textures into constrained cache
                        for (const auto& texName : equipData2->textureNames) {
                            std::string ln = texName;
                            std::transform(ln.begin(), ln.end(), ln.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            auto texIt = equipData2->textures.find(ln);
                            if (texIt == equipData2->textures.end() || !texIt->second) continue;
                            const auto& texInfo = texIt->second;
                            if (texInfo->data.empty()) continue;

                            constrainedTextureCache_->getOrLoad(ln, texInfo->data);
                        }
                        equipmentData.push_back(std::move(prepData));
                    };

                    prepOneEquip(primaryId, true);
                    prepOneEquip(secondaryId, false);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8d: entity {} ({}) equipment: primary={} secondary={} extracted={}",
                              spawnId, vis.name, vis.appearance.equipment[7], vis.appearance.equipment[8], equipmentData.size());
                }

                if (equipError) {
                    pendingZoneComputations_.reset();
                    loadFailed_ = true;
                    return;
                }

                // Step 8e: Distribute prep results to EntityVisual
                // (equivalent to pollAndDistributePrepResults inline)
                {
                    // We need a mutable reference — use const_cast since we own the renderer
                    auto& mutableEntities = const_cast<std::map<uint16_t, EntityVisual>&>(entityRenderer_->getEntities());
                    auto visIt = mutableEntities.find(spawnId);
                    if (visIt == mutableEntities.end()) {
                        LOG_WARN(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) vanished before distribution — prep work discarded "
                                 "(variantTex={}, equipData={})",
                                 spawnId, vis.name, variantTextureCount, equipmentData.size());
                    } else {
                        auto& mvis = visIt->second;
                        // T06: variantTextures no longer stored on EntityVisual — textures are in constrained cache.

                        for (auto& eq : equipmentData) {
                            EntityVisual::EquipmentStaging staging;
                            staging.modelId = eq.modelId;
                            staging.equipmentId = eq.equipmentId;
                            staging.isPrimary = eq.isPrimary;
                            staging.geometry = eq.geometry;
                            staging.rawTextures = eq.rawTextures;
                            mvis.equipmentStaging.push_back(std::move(staging));
                        }

                        // Textures already in constrained cache from T05 (base) + T06 (variant) + T07 (equipment).
                        mvis.entityPrepComplete = true;
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) distributed: equipStaging={}",
                                  spawnId, vis.name, mvis.equipmentStaging.size());
                    }
                }

                // T04: processUploadQueue removed. Textures uploaded via uploadTexture() in T09.

                // Promote prepared models so buildEntityMesh finds them
                modelLoader->promotePreparedModels();

                // Upload all pending textures (including composite EQE1 entries queued in Step 8a)
                // so they're available as GPU textures when buildEntityMesh builds composite meshes.
                if (constrainedTextureCache_) {
                    constrainedTextureCache_->uploadAllPending();
                }

                // Step 8f: Build entity mesh (GL work — scene node, materials, name tag)
                bool meshBuilt = entityRenderer_->buildEntityMesh(spawnId);
                if (meshBuilt) ++buildCount;
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8f: entity {} ({}) buildEntityMesh result={}", spawnId, vis.name, meshBuilt);

                ++prepCount;
            }

            bool hasBoats = entityRenderer_->hasBoatsInZone();
            LOG_INFO(MOD_GRAPHICS, "Sequential: prepped {} entities, built {} meshes (model cache hits={}, disk loads={}, hasBoats={})",
                     prepCount, buildCount, modelCacheHits, modelDiskLoads, hasBoats);
            if (hasBoats) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: zone has boats — boat collision checks enabled");
            } else {
                LOG_INFO(MOD_GRAPHICS, "Sequential: no boats in zone — boat collision checks disabled (zero per-frame cost)");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entity prep skipped (entityRenderer_={}, raceModelLoader={}, skipEntityBuild={})",
                      (bool)entityRenderer_,
                      entityRenderer_ ? (bool)entityRenderer_->getRaceModelLoader() : false,
                      config_.constrainedConfig.skipEntityBuild);
        }
        // T09: Upload all decoded textures from Steps 8a-8d to GPU.
        // getOrLoad() decoded them; uploadAllPending() promotes DecodedEntry → CachedTexture.
        if (constrainedTextureCache_) {
            int uploaded = constrainedTextureCache_->uploadAllPending();
            LOG_INFO(MOD_GRAPHICS, "SEQ Step8: uploadAllPending uploaded {} entity textures", uploaded);
        }

        entityPrepReady_ = true;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityPrepReady_=true");
        logAssetBuildTime("seq_entities", 0, stepStart);
        LOG_INFO(MOD_GRAPHICS, "SEQ Step8: about to exit scope");
    }
    LOG_INFO(MOD_GRAPHICS, "SEQ Step8: scope exited, flushing log...");
    FlushThreadLog();
    LOG_INFO(MOD_GRAPHICS, "SEQ: Step 8 complete, entering Step 9...");
    FlushThreadLog();

    // ── Step 9: Collision ──────────────────────────────────────────────────
    updateProgress(88, "Setting up collision...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: smgr_={}, regionWorldTriangles_={}, objectNodes_={}, zoneBspTree_={}",
                  (bool)smgr_, regionWorldTriangles_.size(), objectNodes_.size(), (bool)zoneBspTree_);
        setupMinimalZoneCollision();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: setupMinimalZoneCollision complete, regionWorldTriangles_={}",
                  regionWorldTriangles_.size());

        // Add eagerly-built objects as pre-transformed world-space triangles per BSP region
        size_t objectsAdded = 0;
        size_t totalObjectTriangles = 0;
        if (smgr_) {
            for (size_t oi = 0; oi < objectNodes_.size(); ++oi) {
                auto* node = objectNodes_[oi];
                if (node && node->getMesh()) {
                    auto triangles = extractWorldTriangles(smgr_, node);
                    if (!triangles.empty()) {
                        size_t region = (oi < objectRegions_.size()) ? objectRegions_[oi] : SIZE_MAX;
                        auto& regionTris = objectWorldTriangles_[region];
                        totalObjectTriangles += triangles.size();
                        regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
                        objectsAdded++;
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: {} objects -> {} world-space triangles across {} regions",
                     objectsAdded, totalObjectTriangles, objectWorldTriangles_.size());
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: collision setup complete, "
                  "regionWorldTriangles_={}, objectWorldTriangles_={}, doorCollisionData_={}, bspFiltered={}",
                  regionWorldTriangles_.size(), objectWorldTriangles_.size(),
                  doorCollisionData_.size(), (bool)(zoneBspTree_ && !regionWorldTriangles_.empty()));
        logAssetBuildTime("seq_collision", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 10: Sky — all sky, fog, weather ───────────────────────────────
    updateProgress(90, "Loading sky and weather...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        bool skyRendering = config_.constrainedConfig.skyRendering;
        auto skyDomeMode = config_.constrainedConfig.skyDomeMode;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: computations={}, skyRendering={}, skyDomeMode={}, "
                  "storedZoneEnv.pending={}, skyRenderer_={}, weatherSystem_={}",
                  (bool)computations, skyRendering, (int)skyDomeMode,
                  storedZoneEnvironment_.pending, (bool)skyRenderer_, (bool)weatherSystem_);

        // Names of sky textures loaded into constrained cache (used by upload step after skyLoader is moved)
        std::vector<std::string> loadedSkyTextureNames;

        // Load sky data inline (was background thread)
        if (computations) {
            if (skyRendering) {
                auto skyData = std::make_unique<PendingZoneComputations::SkyLoadData>();
                skyData->skyLoader = std::make_unique<SkyLoader>();
                skyData->skyConfig = std::make_unique<SkyConfig>();

                if (skyData->skyLoader->load(eqPath)) {
                    LOG_INFO(MOD_GRAPHICS, "Sequential: sky.s3d loaded ({} textures)",
                             skyData->skyLoader->getSkyData()->textures.size());
                } else {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load sky S3D archive: {}sky.s3d (sky rendering enabled in preset)", eqPath);
                    pendingZoneComputations_.reset();
                    loadFailed_ = true;
                    return;
                }
                std::string skyIniPath = eqPath + "sky.ini";
                bool skyIniLoaded = skyData->skyConfig->loadFromFile(skyIniPath);
                if (!skyIniLoaded) {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load sky config: {} (sky rendering enabled in preset)", skyIniPath);
                    pendingZoneComputations_.reset();
                    loadFailed_ = true;
                    return;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky.ini '{}': loaded={}", skyIniPath, skyIniLoaded);

                // Pre-decode sky textures
                if (skyData->skyLoader && skyData->skyLoader->getSkyData()) {
                    std::set<std::string> neededSet;
                    if (skyData->skyConfig && skyData->skyConfig->isLoaded()) {
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(currentZoneName_);
                        int skyTypeId = skyData->skyConfig->getSkyTypeIdForWeather(weatherType);
                        auto neededTextures = skyData->skyLoader->getTextureNamesForSkyType(skyTypeId);
                        neededSet.insert(neededTextures.begin(), neededTextures.end());
                    }
                    // T08: Load sky textures via constrained cache (replaces inline decodeBMPtoARGB + upscale)
                    const auto& textures = skyData->skyLoader->getSkyData()->textures;
                    size_t skyTexLoaded = 0;
                    size_t skyTexSkipped = 0;
                    for (const auto& [texName, texInfo] : textures) {
                        if (!texInfo || texInfo->data.empty()) continue;
                        if (!neededSet.empty() && neededSet.find(texName) == neededSet.end()) {
                            ++skyTexSkipped;
                            continue;
                        }
                        std::string lowerName = texName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        constrainedTextureCache_->getOrLoad(lowerName, texInfo->data);
                        loadedSkyTextureNames.push_back(texName);  // Save for upload step after skyLoader is moved
                        ++skyTexLoaded;
                    }
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: loaded {} sky textures via cache ({} skipped, {} total in archive)",
                              skyTexLoaded, skyTexSkipped, textures.size());
                }

                // Pre-compute dome mesh
                using SkyDomeMode = ConstrainedRendererConfig::SkyDomeMode;
                bool usedWldDome = false;
                if (skyDomeMode == SkyDomeMode::Original &&
                    skyData->skyLoader && skyData->skyLoader->isLoaded()) {
                    int skyTypeId = 0;
                    if (skyData->skyConfig && skyData->skyConfig->isLoaded()) {
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(currentZoneName_);
                        skyTypeId = skyData->skyConfig->getSkyTypeIdForWeather(weatherType);
                    }
                    auto layers = skyData->skyLoader->getLayersForSkyType(skyTypeId);
                    std::shared_ptr<SkyLayer> bgLayer;
                    for (const auto& layer : layers) {
                        if (layer && layer->type == SkyLayerType::Background && layer->geometry) {
                            bgLayer = layer;
                            break;
                        }
                    }
                    if (bgLayer && bgLayer->geometry && !bgLayer->geometry->vertices.empty()) {
                        const auto& geom = *bgLayer->geometry;
                        float maxExtent = 0.0f;
                        for (const auto& v : geom.vertices) {
                            float ext = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                            if (ext > maxExtent) maxExtent = ext;
                        }
                        float scale = (maxExtent > 0.001f) ? (1800.0f / maxExtent) : 1.0f;

                        auto wldDome = std::make_unique<PendingZoneComputations::SkyLoadData::PrecomputedWldDome>();
                        wldDome->vertices.resize(geom.vertices.size());
                        for (size_t i = 0; i < geom.vertices.size(); ++i) {
                            const auto& v = geom.vertices[i];
                            auto& iv = wldDome->vertices[i];
                            iv.Pos = irr::core::vector3df(v.x * scale, v.z * scale, v.y * scale);
                            iv.Normal = irr::core::vector3df(-v.nx, -v.nz, -v.ny);
                            iv.TCoords = irr::core::vector2df(v.u, v.v);
                            iv.Color = irr::video::SColor(255, 255, 255, 255);
                        }
                        wldDome->indices.reserve(geom.triangles.size() * 3);
                        for (const auto& tri : geom.triangles) {
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v1));
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v2));
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v3));
                        }
                        skyData->precomputedWldDome = std::move(wldDome);
                        usedWldDome = true;
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: precomputed WLD dome mesh: {} verts, {} indices",
                                  skyData->precomputedWldDome->vertices.size(),
                                  skyData->precomputedWldDome->indices.size());
                    } else {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: WLD dome mode requested but no background layer geometry found "
                                  "(bgLayer={}, hasGeom={}) — will use procedural dome",
                                  (bool)bgLayer, bgLayer ? (bool)bgLayer->geometry : false);
                    }
                }
                if (!usedWldDome) {
                    skyData->precomputedDome = std::make_unique<PendingZoneComputations::SkyLoadData::PrecomputedSkyDome>();
                    SkyRenderer::precomputeDomeMesh(skyData->precomputedDome->vertices,
                                                    skyData->precomputedDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: precomputed procedural dome mesh: {} verts, {} indices",
                              skyData->precomputedDome->vertices.size(),
                              skyData->precomputedDome->indices.size());
                }

                computations->skyLoadData = std::move(skyData);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky loading skipped (skyRendering=false)");
            }

            // Load weather config inline
            {
                auto weatherData = std::make_unique<PendingZoneComputations::WeatherConfigData>();
                ZoneWeatherConfig wconfig;
                if (loadZoneWeatherConfig(currentZoneName_, wconfig)) {
                    weatherData->config = wconfig;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: loaded zone weather config for '{}'", currentZoneName_);
                } else {
                    weatherData->config.zoneName = currentZoneName_;
                    weatherData->config.defaultWeather = WeatherType::Normal;
                    weatherData->config.enabled = true;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: using default weather config for '{}'", currentZoneName_);
                }
                computations->weatherConfig = std::move(weatherData);
            }
        }

        // S05: skyRenderer_ already created in initLoadingScreen() — apply zone-specific data
        bool hasBgData = computations && computations->skyLoadData && computations->skyLoadData->skyLoader;
        if (skyRenderer_ && hasBgData) {
            skyRenderer_->initializeFromPreloaded(
                std::move(computations->skyLoadData->skyLoader),
                std::move(computations->skyLoadData->skyConfig));
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applied preloaded sky data");
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: preloaded sky data not applied (skyRenderer_={}, hasBgData={})",
                      (bool)skyRenderer_, hasBgData);
        }

        if (storedZoneEnvironment_.pending) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applying stored zone env: skyType={}, zoneType={}, "
                      "fogR={}, fogG={}, fogB={}, fogMaxClip={}",
                      storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType,
                      storedZoneEnvironment_.fogR[0], storedZoneEnvironment_.fogG[0],
                      storedZoneEnvironment_.fogB[0], storedZoneEnvironment_.fogMaxClip[0]);
            if (skyRenderer_ && skyRenderer_->isInitialized()) {
                skyRenderer_->prepareSkyType(storedZoneEnvironment_.skyType, currentZoneName_);
                bool isDungeon = (storedZoneEnvironment_.zoneType == 2);
                isIndoorZone_ = isDungeon;
                bool skySettingEnabled = getDisplaySettings().skyEnabled;
                skyRenderer_->setEnabled(!isDungeon && skySettingEnabled && config_.constrainedConfig.skyRendering);
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step10: stored zone env pending but skyRenderer not ready "
                         "(skyRenderer_={}, initialized={}) — sky type/dungeon detection skipped",
                         (bool)skyRenderer_, skyRenderer_ ? skyRenderer_->isInitialized() : false);
            }

            zoneMaxClip_ = (storedZoneEnvironment_.fogMaxClip[0] > 0.0f)
                ? storedZoneEnvironment_.fogMaxClip[0] : 99999.0f;
            setRenderDistance(userRenderDistance_);

            if (driver_ && fogEnabled_) {
                irr::video::SColor fogColor(255,
                    storedZoneEnvironment_.fogR[0], storedZoneEnvironment_.fogG[0], storedZoneEnvironment_.fogB[0]);
                float fogEnd = renderDistance_;
                float fogStart = std::max(0.0f, renderDistance_ - fogThickness_);
                driver_->setFog(fogColor, irr::video::EFT_FOG_LINEAR, fogStart, fogEnd, 0.0f, true, false);
            }

            if (weatherSystem_) {
                if (computations && computations->weatherConfig && computations->weatherConfig->loaded) {
                    weatherSystem_->setZoneConfig(computations->weatherConfig->config);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem zone config applied");
                } else {
                    weatherSystem_->setWeatherFromZone(currentZoneName_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem fallback to zone name '{}'", currentZoneName_);
                }
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: stored zone environment not pending — sky type, dungeon detection, fog, and weather skipped");
        }

        // T08: Upload sky textures via constrained cache uploadTexture()
        // Sky textures were decoded by getOrLoad() above; now promote to GPU.
        // Uses loadedSkyTextureNames (collected before skyLoader was moved).
        if (!config_.constrainedConfig.skipSkyTextureUpload && skyRenderer_ &&
            constrainedTextureCache_ && !loadedSkyTextureNames.empty()) {
            size_t skyUploaded = 0;
            for (const auto& texName : loadedSkyTextureNames) {
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                irr::video::ITexture* tex = constrainedTextureCache_->uploadTexture(lowerName);
                if (tex && skyRenderer_) {
                    skyRenderer_->registerUploadedTexture(texName, tex);
                    ++skyUploaded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: uploaded {} sky textures via constrained cache", skyUploaded);
        }
        if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isSkyPrepared()) {
            skyRenderer_->clearSkyForRebuild();
            if (computations && computations->skyLoadData) {
                auto& skyData = *computations->skyLoadData;
                if (skyData.precomputedWldDome) {
                    skyRenderer_->createSkyDomeFromWldGeometry(
                        skyData.precomputedWldDome->vertices, skyData.precomputedWldDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from WLD geometry");
                } else if (skyData.precomputedDome) {
                    skyRenderer_->createSkyDomeFromPrecomputed(
                        skyData.precomputedDome->vertices, skyData.precomputedDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from precomputed mesh");
                } else {
                    skyRenderer_->applySkyType();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no precomputed data)");
                }
            } else {
                skyRenderer_->applySkyType();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no skyLoadData)");
            }
        }
        if (computations && computations->skyLoadData) {
            computations->skyLoadData->precomputedDome.reset();
            computations->skyLoadData->precomputedWldDome.reset();
        }
        if (skyRenderer_ && skyRenderer_->isInitialized()) {
            skyRenderer_->createCelestialBodiesOnly();
            skyRenderer_->applyInitialColors();
            skyRenderer_->consumeSkyPrepared();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky celestial bodies created, initial colors applied, sky consumed");
        }
        if (storedZoneEnvironment_.pending) storedZoneEnvironment_.pending = false;
        if (computations && computations->skyLoadData) computations->skyLoadData.reset();
        setupFog();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: complete, skyRenderer_={}, skyInit={}, skyEnabled={}, "
                  "isIndoorZone_={}, fogEnabled_={}, renderDistance_={:.0f}, weatherSystem_={}",
                  (bool)skyRenderer_,
                  skyRenderer_ ? skyRenderer_->isInitialized() : false,
                  skyRenderer_ ? skyRenderer_->isEnabled() : false,
                  isIndoorZone_, fogEnabled_, renderDistance_, (bool)weatherSystem_);
        logAssetBuildTime("seq_sky", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 11: Env — environment subsystems ──────────────────────────────
    updateProgress(93, "Initializing environment...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: treeManager_={}, detailManager_={}, particleManager_={}, "
                  "boidsManager_={}, tumbleweedManager_={}, weatherEffects_={}",
                  (bool)treeManager_, (bool)detailManager_, (bool)particleManager_,
                  (bool)boidsManager_, (bool)tumbleweedManager_, (bool)weatherEffects_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE fire glow flags: lightingEnabled={}, icospheresEnabled={}, "
                  "maxLights={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);

        // Trees — on software path, tree manager handles CPU wind animation
        // On GPU path, trees are normal objects with wind shaders applied in fixup pass
        if (treeManager_ && currentZone_ && !currentZone_->objects.empty()) {
            treeManager_->loadConfig("", currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: treeManager_ loadConfig for zone '{}'", currentZoneName_);
            if (driver_->getDriverType() == irr::video::EDT_BURNINGSVIDEO) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software renderer tree init path, objects={}", currentZone_->objects.size());
                if (!treeManager_->isInitializing()) {
                    treeManager_->beginInitialize(currentZone_->objects, currentZone_->objectTextures);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: beginInitialize started, objects={}, textures={}",
                              currentZone_->objects.size(), currentZone_->objectTextures.size());
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: treeManager_ already initializing, skipping beginInitialize");
                }
                size_t batchCount = 0;
                while (!treeManager_->initializeNextBatch(100)) { ++batchCount; }
                ++batchCount;
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software tree init batches={}", batchCount);
                if (zoneBspTree_) {
                    treeManager_->assignBspRegions(zoneBspTree_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software tree assignBspRegions done");
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software tree init complete");
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: GPU renderer path, trees handled as normal objects with wind shaders");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree init SKIPPED: treeManager_={}, currentZone_={}, objects={}",
                      (bool)treeManager_, (bool)currentZone_,
                      currentZone_ ? currentZone_->objects.size() : 0);
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: trees done, initialized={}, count={}",
                  treeManager_ ? treeManager_->isInitialized() : false,
                  treeManager_ ? treeManager_->getAnimatedTreeCount() : 0);

        // S05: detailManager_ already created in initLoadingScreen()
        if (detailManager_ && zoneBspTree_ && !regionWorldTriangles_.empty()) {
            // Create BSP-filtered ground raycast callback for detail/footprint systems
            // Uses pre-transformed world-space triangles with direct Möller–Trumbore intersection
            auto groundRaycast = [this](float x, float z, float startY,
                                        float& outY, irr::core::vector3df& outNormal,
                                        irr::core::triangle3df& outTriangle) -> bool {
                // Find BSP region at query point (Irrlicht x,z → EQ x,y)
                // startY is in Irrlicht Y-up coords = EQ Z
                size_t queryRegion = zoneBspTree_->findRegionIndexForPoint(x, z, startY);

                irr::core::vector3df rayStart(x, startY, z);
                irr::core::vector3df rayDir(0.0f, -1.0f, 0.0f);  // Cast down
                float rayLen = 560.0f;

                irr::core::vector3df hitPoint;
                irr::core::triangle3df hitTri;
                bool hit = false;
                float closestDistSq = std::numeric_limits<float>::max();

                // Test query region world triangles (direct, no matrix math)
                if (queryRegion != SIZE_MAX) {
                    auto it = regionWorldTriangles_.find(queryRegion);
                    if (it != regionWorldTriangles_.end()) {
                        float distSq;
                        if (rayIntersectTriangles(rayStart, rayDir, rayLen, it->second, hitPoint, hitTri, distSq)) {
                            if (distSq < closestDistSq) {
                                closestDistSq = distSq;
                                outY = hitPoint.Y;
                                outNormal = hitTri.getNormal();
                                outNormal.normalize();
                                outTriangle = hitTri;
                                hit = true;
                            }
                        }
                    }
                }

                // Test doors and objects in query region (direct Möller–Trumbore)
                if (queryRegion != SIZE_MAX) {
                    irr::core::vector3df hp;
                    irr::core::triangle3df ht;
                    float distSq;
                    if (rayTestDoorsAndObjects(rayStart, rayDir, rayLen, queryRegion, hp, ht, distSq)) {
                        if (distSq < closestDistSq) {
                            outY = hp.Y;
                            outNormal = ht.getNormal();
                            outNormal.normalize();
                            outTriangle = ht;
                            hit = true;
                        }
                    }
                }

                return hit;
            };

            auto wldLoader = currentZone_ ? currentZone_->wldLoader : nullptr;
            auto zoneGeom = currentZone_ ? currentZone_->geometry : nullptr;
            detailManager_->onZoneEnter(currentZoneName_, groundRaycast,
                                        zoneMeshNode_, wldLoader, zoneGeom);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter called for zone '{}'", currentZoneName_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter skipped (detailManager_={}, zoneBspTree_={}, regionWorldTriangles_={})",
                      (bool)detailManager_, (bool)zoneBspTree_, regionWorldTriangles_.size());
        }
        if (detailManager_ && !regionMeshNodes_.empty()) {
            size_t nodesAdded = 0;
            for (auto& [regionIdx, node] : regionMeshNodes_) {
                if (node && node->getMesh()) {
                    detailManager_->addMeshNodeForTextureLookup(node);
                    ++nodesAdded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: added {} mesh nodes for detail texture lookup (of {} total)",
                      nodesAdded, regionMeshNodes_.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_={}, regionWorldTriangles_={}, regionMeshNodes_={}",
                  (bool)detailManager_, regionWorldTriangles_.size(), regionMeshNodes_.size());

        // Particles
        if (particleManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            particleManager_->onZoneEnter(currentZoneName_, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ onZoneEnter done, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));

            // Fire sources
            std::vector<glm::vec3> fireSources;
            std::vector<float> fireRadii;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: objectLights_ count={}", objectLights_.size());
            for (const auto& objLight : objectLights_) {
                if (objLight.isFireSource) {
                    fireSources.emplace_back(objLight.position.X, objLight.position.Z, objLight.position.Y);
                    fireRadii.push_back(objLight.node ? objLight.node->getRadius() : 120.0f);
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fire sources from objectLights_: {} sources, {} radii",
                      fireSources.size(), fireRadii.size());
            if (currentZone_) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: currentZone_->lights count={}", currentZone_->lights.size());
                for (const auto& zl : currentZone_->lights) {
                    std::string upperName = zl->name;
                    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                    bool nameMatch = upperName.find("TORCH") != std::string::npos ||
                                     upperName.find("FIRE") != std::string::npos ||
                                     upperName.find("BRAZIER") != std::string::npos ||
                                     upperName.find("FLAME") != std::string::npos ||
                                     upperName.find("CANDLE") != std::string::npos;
                    bool colorMatch = (zl->r > 0.5f && zl->g > 0.2f && zl->b < 0.3f);
                    if (nameMatch || colorMatch) {
                        fireSources.emplace_back(zl->x, zl->y, zl->z);
                        fireRadii.push_back(zl->radius);
                    }
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: total fireSources={} (objectLights + zone lights)",
                      fireSources.size());
            if (!fireSources.empty()) {
                particleManager_->setFireSources(fireSources);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setFireSources count={}", fireSources.size());
            }
            if (detailManager_ && detailManager_->hasSurfaceMap()) {
                particleManager_->setSurfaceMap(detailManager_->getSurfaceMap());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setSurfaceMap from detailManager_");
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ no surface map (detailManager_={}, hasSurfaceMap={})",
                          (bool)detailManager_, detailManager_ ? detailManager_->hasSurfaceMap() : false);
            }

            particleManager_->initUnifiedRenderer();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ initUnifiedRenderer complete");
            if (!fireSources.empty()) {
                particleManager_->createFireEmitters(fireSources, fireRadii);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ createFireEmitters called, sources={}, radii={}",
                          fireSources.size(), fireRadii.size());
            }

            // Collect fire glow lights (same sources as fire particles)
            // Positions in fireSources are EQ coords (Z-up); convert to Irrlicht (Y-up)
            fireGlowLights_.clear();
            uint32_t rngState = 0x12345678u;
            for (size_t i = 0; i < fireSources.size(); ++i) {
                FireGlowLight e;
                // EQ(x,y,z) -> Irrlicht(x,z,y)
                e.position = irr::core::vector3df(fireSources[i].x, fireSources[i].z, fireSources[i].y);
                e.r = 0.15f; e.g = 0.09f; e.b = 0.02f;  // warm fire color (subtle)
                e.radius = fireRadii[i] * 0.3f;  // compact glow around fire source
                // Random flicker phase/speed per emitter
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerPhase = static_cast<float>(rngState & 0xFFFF) / 65535.0f * 6.28f;
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerSpeed = 0.8f + static_cast<float>(rngState & 0xFFFF) / 65535.0f * 0.4f;
                fireGlowLights_.push_back(e);
            }
            LOG_INFO(MOD_GRAPHICS, "Collected {} fire glow lights for zone", fireGlowLights_.size());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fireGlowLights_ collected={}", fireGlowLights_.size());
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ is NULL — skipping fire source collection entirely");
        }

        // Boids
        if (boidsManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            if (zoneBoundsValid_) {
                boidsManager_->onZoneEnter(currentZoneName_, biome,
                    glm::vec3(zoneBoundsMinX_, zoneBoundsMinY_, -1000.0f),
                    glm::vec3(zoneBoundsMaxX_, zoneBoundsMaxY_, 1000.0f));
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter with bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            } else {
                boidsManager_->onZoneEnter(currentZoneName_, biome);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter without bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            }
        }

        // Tumbleweed
        if (tumbleweedManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            tumbleweedManager_->onZoneEnter(currentZoneName_, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tumbleweedManager_ onZoneEnter, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
        }

        // Weather surface + display settings
        if (weatherEffects_ && detailManager_ && detailManager_->hasSurfaceMap()) {
            weatherEffects_->setSurfaceMap(detailManager_->getSurfaceMap());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: weatherEffects_ setSurfaceMap from detailManager_");
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvironmentalDisplaySettings");
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);
        applyEnvironmentalDisplaySettings();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: POST applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);

        // Build icosphere mesh + shader for fire glow volumes
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere check: fireEffectsEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, fireGlowLights_.size()={}, icosphereVertices_.empty()={}",
                  fireEffectsEnabled_, fireGlowIcospheresEnabled_,
                  fireGlowLights_.size(), icosphereVertices_.empty());
#ifdef EQT_HAS_DRM
        if (fireEffectsEnabled_ && fireGlowIcospheresEnabled_ && !fireGlowLights_.empty() && icosphereVertices_.empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: building icosphere mesh + shader");
            buildIcosphereMesh();
            compileIcosphereShader();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build done: vertices={}, indices={}, "
                      "VBO={}, IBO={}, program={}",
                      icosphereVertices_.size(), icosphereIndices_.size(),
                      icosphereVBO_, icosphereIBO_, icosphereProgram_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build SKIPPED");
        }
#else
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: EQT_HAS_DRM not defined, icosphere build skipped");
#endif

        // Release combined zone geometry (if detail system disabled)
        if (currentZone_ && currentZone_->geometry && !detailManager_) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: releasing combined zone geometry (detailManager_ inactive), "
                      "vertices={}, triangles={}",
                      currentZone_->geometry->vertices.size(), currentZone_->geometry->triangles.size());
            currentZone_->geometry->vertices.clear();
            currentZone_->geometry->vertices.shrink_to_fit();
            currentZone_->geometry->triangles.clear();
            currentZone_->geometry->triangles.shrink_to_fit();
        }

        // Governor reset
        if (governor_) {
            governor_->requestReset();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: governor_ reset requested");
        }

        // Fixup pass: apply tree wind shaders and animated textures to objects
        // built eagerly in Step 6 (before treeManager_ / animatedTextureManager_ existed)
        if (treeManager_ || animatedTextureManager_) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup pass starting, deferredObjects_={}, treeManager_={}, "
                      "animatedTextureManager_={}, zoneShader_={}, windAvailable={}",
                      deferredObjects_.size(), (bool)treeManager_, (bool)animatedTextureManager_,
                      (bool)zoneShader_, zoneShader_ ? zoneShader_->isWindAvailable() : false);
            size_t fixupProcessed = 0, fixupSkipNotBuilt = 0, fixupSkipBadIndex = 0;
            size_t fixupSkipNullNode = 0, fixupSkipNoGeomPlaceable = 0;
            size_t treesFound = 0, treesWindApplied = 0, treesNullMesh = 0;
            size_t animatedApplied = 0;
            bool windCheckEnabled = treeManager_ && zoneShader_ && zoneShader_->isWindAvailable();
            if (!windCheckEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup wind shader check DISABLED: treeManager_={}, "
                          "zoneShader_={}, windAvailable={}",
                          (bool)treeManager_, (bool)zoneShader_,
                          zoneShader_ ? zoneShader_->isWindAvailable() : false);
            }
            for (size_t oi = 0; oi < deferredObjects_.size(); ++oi) {
                const auto& deferred = deferredObjects_[oi];
                if (!deferred.meshBuilt || deferred.nodeIndex == SIZE_MAX) { ++fixupSkipNotBuilt; continue; }
                if (deferred.nodeIndex >= objectNodes_.size()) { ++fixupSkipBadIndex; continue; }
                if (deferred.objectIndex >= currentZone_->objects.size()) { ++fixupSkipBadIndex; continue; }

                auto* objNode = objectNodes_[deferred.nodeIndex];
                if (!objNode) { ++fixupSkipNullNode; continue; }
                const auto& objInstance = currentZone_->objects[deferred.objectIndex];
                if (!objInstance.geometry || !objInstance.placeable) { ++fixupSkipNoGeomPlaceable; continue; }

                ++fixupProcessed;
                const std::string& objName = objInstance.placeable->getName();

                // Wind shader for trees
                if (windCheckEnabled) {
                    std::string primaryTexture;
                    if (!objInstance.geometry->textureNames().empty())
                        primaryTexture = objInstance.geometry->textureNames()[0];
                    bool isTree = treeManager_->isTreeObject(objName, primaryTexture);
                    if (isTree) {
                        ++treesFound;
                        irr::scene::IMesh* treeMesh = objNode->getMesh();
                        if (treeMesh) {
                            irr::core::aabbox3df meshBbox = treeMesh->getBoundingBox();
                            irr::s32 windMatType = zoneShader_->getActiveWindAlphaTest();
                            // Update BOTH node materials AND mesh buffer materials.
                            // Node materials are used by Irrlicht's scene graph rendering.
                            // Mesh buffer materials are used by drawZoneGeometrySorted().
                            for (irr::u32 mi = 0; mi < treeMesh->getMeshBufferCount(); ++mi) {
                                auto& bufMat = treeMesh->getMeshBuffer(mi)->getMaterial();
                                bufMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(windMatType);
                                bufMat.MaterialTypeParam = meshBbox.MinEdge.Y;
                                bufMat.MaterialTypeParam2 = meshBbox.MaxEdge.Y;
                                bufMat.BackfaceCulling = false;
                                if (constrainedTextureCache_ && bufMat.TextureLayer[0].Texture && !bufMat.TextureLayer[1].Texture) {
                                    irr::core::stringc texName = bufMat.TextureLayer[0].Texture->getName().getPath();
                                    irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(
                                        std::string(texName.c_str()));
                                    if (alphaTex) bufMat.setTexture(1, alphaTex);
                                }
                            }
                            for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                                auto& mat = objNode->getMaterial(mi);
                                irr::s32 prevMatType = mat.MaterialType;
                                irr::video::ITexture* tex0 = mat.TextureLayer[0].Texture;
                                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(windMatType);
                                mat.MaterialTypeParam = meshBbox.MinEdge.Y;
                                mat.MaterialTypeParam2 = meshBbox.MaxEdge.Y;
                                mat.BackfaceCulling = false;
                                // Ensure ETC1 alpha companion is on layer 1 (may already be set by mesh builder)
                                if (constrainedTextureCache_ && tex0 && !mat.TextureLayer[1].Texture) {
                                    // Look up alpha companion by texture name
                                    irr::core::stringc texName = tex0->getName().getPath();
                                    irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(
                                        std::string(texName.c_str()));
                                    if (alphaTex) mat.setTexture(1, alphaTex);
                                }
                                LOG_DEBUG(MOD_SHADER, "fixup tree '{}' mat[{}]: prevType={} -> windType={} "
                                          "tex0={} alphaTex1={} bboxY=[{:.2f},{:.2f}]",
                                          objName, mi, prevMatType, windMatType,
                                          (void*)tex0, (void*)mat.TextureLayer[1].Texture,
                                          meshBbox.MinEdge.Y, meshBbox.MaxEdge.Y);
                            }
                            ++treesWindApplied;
                        } else {
                            ++treesNullMesh;
                            LOG_WARN(MOD_GRAPHICS, "SEQ Step11: tree '{}' identified but getMesh() returned null, "
                                     "wind shader NOT applied", objName);
                        }
                    }
                }

                // Animated textures
                if (animatedTextureManager_ && objInstance.geometry) {
                    irr::scene::IMesh* objMesh = objNode->getMesh();
                    if (objMesh) {
                        animatedTextureManager_->addMesh(*objInstance.geometry, currentZone_->objectTextures, objMesh);
                        animatedTextureManager_->addSceneNode(objNode);
                        ++animatedApplied;
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: fixup pass: processed={}/{} objects, trees found={}, "
                     "wind applied={}, null mesh={}, animated={}, skipped: notBuilt={}, badIndex={}, "
                     "nullNode={}, noGeomPlaceable={}",
                     fixupProcessed, deferredObjects_.size(), treesFound, treesWindApplied,
                     treesNullMesh, animatedApplied, fixupSkipNotBuilt, fixupSkipBadIndex,
                     fixupSkipNullNode, fixupSkipNoGeomPlaceable);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup pass SKIPPED: treeManager_={}, animatedTextureManager_={}",
                      (bool)treeManager_, (bool)animatedTextureManager_);
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: complete");
        logAssetBuildTime("seq_env", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 12: Lights — zone lighting ────────────────────────────────────
    updateProgress(96, "Setting up lighting...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: currentZone_={}, lights count={}",
                  (bool)currentZone_,
                  currentZone_ ? currentZone_->lights.size() : 0);
        zoneLightData_.clear();
        zoneLightPositions_.clear();
        zoneLightRegions_.clear();
        if (currentZone_ && !currentZone_->lights.empty()) {
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                irr::core::vector3df pos(light->x, light->z, light->y);
                ZoneLightData zld;
                zld.radius = std::max(light->radius, 1.0f);
                zld.attConstant = 1.0f;
                zld.attLinear = 1.0f / zld.radius;
                zld.attQuadratic = 1.0f / (zld.radius * zld.radius);
                zld.baseDiffuseColor = irr::video::SColorf(light->r, light->g, light->b, 1.0f);
                zoneLightData_.push_back(zld);
                zoneLightPositions_.push_back(pos);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}]: pos=({:.1f},{:.1f},{:.1f}) radius={:.1f} "
                          "color=({:.2f},{:.2f},{:.2f}) attLin={:.4f} attQuad={:.6f}",
                          i, light->x, light->y, light->z, zld.radius,
                          light->r, light->g, light->b, zld.attLinear, zld.attQuadratic);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: no lights to process (currentZone_={}, lights={})",
                      (bool)currentZone_, currentZone_ ? currentZone_->lights.size() : 0);
        }

        // Install light BSP regions
        if (pendingZoneComputations_ && !pendingZoneComputations_->zoneLightRegions.empty()) {
            zoneLightRegions_ = std::move(pendingZoneComputations_->zoneLightRegions);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions from precomputed (count={})", zoneLightRegions_.size());
        } else if (zoneBspTree_ && !zoneBspTree_->regions.empty() && currentZone_) {
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                size_t regionIdx = zoneBspTree_->findRegionIndexForPoint(light->x, light->y, light->z);
                zoneLightRegions_.push_back(regionIdx);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}] BSP region={} pos=({:.1f},{:.1f},{:.1f})",
                          i, regionIdx == SIZE_MAX ? -1 : (int)regionIdx,
                          light->x, light->y, light->z);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions computed via findRegionIndexForPoint (count={})", zoneLightRegions_.size());
        } else {
            zoneLightRegions_.resize(zoneLightData_.size(), SIZE_MAX);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions fallback SIZE_MAX (no BSP), count={}", zoneLightRegions_.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: zoneLightData_={}, zoneLightPositions_={}, zoneLightRegions_={}",
                  zoneLightData_.size(), zoneLightPositions_.size(), zoneLightRegions_.size());
        logAssetBuildTime("seq_lights", zoneLightData_.size(), stepStart);
    }
    FlushThreadLog();

    // ── Step 12b: SimulationWorker first-frame computation ──────────────────
    // Create SimulationWorker, set zone data, and run one synchronous computation
    // so the first rendered frame after loading has full PVS culling, sorted draw
    // list, lighting, weather, particles, etc. — no visual pop-in.
    updateProgress(99, "Computing scene...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: simulationWorker_={}, frustumCuller_={}, camera_={}",
                  (bool)simulationWorker_, (bool)frustumCuller_, (bool)camera_);

        // S05: simulationWorker_ already created in initLoadingScreen()

        // Build and register zone data
        SimulationZoneData zoneData = buildSimulationZoneData();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: buildSimulationZoneData: regions={}, objects={}, zoneLights={}",
                  zoneData.regionBounds.size(), zoneData.objects.size(), zoneData.zoneLights.size());
        simulationWorker_->setZoneData(zoneData);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: setZoneData installed on SimulationWorker");

        // Update frustum culler with current camera so frustum planes are valid
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
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: frustumCuller updated, fov={:.2f}, aspect={:.2f}, renderDist={:.0f}",
                      fovV, aspect, renderDistance_);
        }

        // Build SimulationInput from current state
        SimulationInput input;

        // Camera (Irrlicht Y-up)
        if (camera_) {
            input.cameraPos = camera_->getPosition();
            input.cameraTarget = camera_->getTarget();
        }
        if (cameraController_) {
            cameraController_->getPositionEQ(input.camEqX, input.camEqY, input.camEqZ);
        }

        // Frustum planes (EQ Z-up)
        if (frustumCuller_ && frustumCuller_->isEnabled()) {
            for (int i = 0; i < 6; ++i) {
                const float* plane = frustumCuller_->getPlane(i);
                input.frustumPlanes[i][0] = plane[0];
                input.frustumPlanes[i][1] = plane[1];
                input.frustumPlanes[i][2] = plane[2];
                input.frustumPlanes[i][3] = plane[3];
            }
            input.frustumValid = true;

            // Occlusion camera
            auto& oc = input.occlusionCamera;
            oc.fwdX = frustumCuller_->getFwdX();
            oc.fwdY = frustumCuller_->getFwdY();
            oc.fwdZ = frustumCuller_->getFwdZ();
            oc.rightX = frustumCuller_->getRightX();
            oc.rightY = frustumCuller_->getRightY();
            oc.rightZ = 0;
            oc.upX = frustumCuller_->getUpX();
            oc.upY = frustumCuller_->getUpY();
            oc.upZ = frustumCuller_->getUpZ();
            oc.fovRadV = frustumCuller_->getFov();
            oc.aspect = frustumCuller_->getAspect();
            oc.enabled = true;
        }

        input.renderDistance = renderDistance_;
        input.loadingActive = false;  // Full culling, not loading-mode

        // Player (EQ Z-up)
        input.playerX = playerX_;
        input.playerY = playerY_;
        input.playerZ = playerZ_;
        input.playerHeading = playerHeading_;

        // Timing (first frame)
        input.deltaTime = 1.0f / 30.0f;
        input.frameNumber = frameNumber_;

        // Environment
        input.currentHour = currentHour_;
        input.currentMinute = currentMinute_;
        input.timeOfDay = currentHour_ + currentMinute_ / 60.0f;

        // Player light
        input.playerLightLevel = playerLightLevel_;

        // Vision and weather modifiers
        input.visionType = static_cast<uint8_t>(currentVision_);
        if (weatherEffects_ && weatherEffects_->isEnabled()) {
            input.weatherAmbientModifier = weatherEffects_->getAmbientLightModifier();
        }

        // Entity snapshots
        if (entityRenderer_) {
            const auto& entities = entityRenderer_->getEntities();
            input.entitySnapshots.reserve(entities.size());
            for (const auto& [spawnId, visual] : entities) {
                if (!visual.sceneNode) continue;
                SimulationInput::EntitySnapshot snap;
                snap.spawnId = spawnId;
                snap.lastX = visual.lastX;
                snap.lastY = visual.lastY;
                snap.lastZ = visual.lastZ;
                snap.serverX = visual.serverX;
                snap.serverY = visual.serverY;
                snap.serverZ = visual.serverZ;
                snap.serverHeading = visual.serverHeading;
                snap.timeSinceUpdate = visual.timeSinceUpdate;
                snap.lastUpdateInterval = visual.lastUpdateInterval;
                snap.collisionZOffset = visual.collisionZOffset;
                snap.modelYOffset = visual.modelYOffset;
                snap.serverAnimation = visual.serverAnimation;
                snap.lastNonZeroAnimation = visual.lastNonZeroAnimation;
                snap.cachedBspRegion = visual.cachedBspRegion;
                snap.bspRegionDirty = visual.bspRegionDirty;
                snap.isNPC = visual.isNPC;
                snap.isPlayer = visual.isPlayer;
                snap.isCorpse = visual.isCorpse;
                snap.isFading = visual.isFading;
                snap.inSceneGraph = visual.inSceneGraph;
                snap.hasVelocity = false;
                input.entitySnapshots.push_back(snap);
            }
            input.entityRenderDistance = config_.constrainedConfig.entityRenderDistance;
            input.maxVisibleEntities = config_.constrainedConfig.maxVisibleEntities;
            input.entityCullingEnabled = true;
            input.nameTagDistance = entityRenderer_->getNameTagDistance();
            input.nameTagsVisible = entityRenderer_->areNameTagsVisible();
        }

        // Sky state
        if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
            input.skyEnabled = true;
            input.skyInitialized = true;
            input.skyCloudScrollOffset = skyRenderer_->getCloudScrollOffset();
        }

        // Weather effects state
        if (weatherEffects_ && weatherEffects_->isEnabled()) {
            input.weatherEnabled = true;
            input.weatherTransitionProgress = weatherEffects_->getTransitionProgress();
            input.weatherTransitionDuration = weatherEffects_->getTransitionDuration();
            input.weatherCurrentDarkening = weatherEffects_->getCurrentDarkening();
            input.weatherTargetDarkening = weatherEffects_->getTargetDarkening();
            input.weatherLightningFlashTimer = weatherEffects_->getLightningFlashTimer();
            input.weatherLightningBoltTimer = weatherEffects_->getLightningBoltTimer();
            input.weatherLightningTimer = weatherEffects_->getLightningTimer();
            input.weatherLightningActive = weatherEffects_->isLightningActive();
            input.weatherLightningEnabled = weatherEffects_->isLightningEnabled();
            input.weatherType = weatherEffects_->getCurrentType();
            input.weatherIntensity = weatherEffects_->getCurrentIntensity();
        }

        // Particle system input
        if (particleManager_) {
            auto& pi = input.particleInput;
            pi.deltaTime = input.deltaTime;
            if (camera_) {
                auto cp = camera_->getAbsolutePosition();
                pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
            }
            if (zoneShader_) {
                const float* amb = zoneShader_->ambientColor();
                pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
            }
            pi.windDirection = particleManager_->getWindDirection();
            pi.windStrength = particleManager_->getWindStrength();
            pi.fireEnabled = particleManager_->isUnifiedFireEnabled();
            pi.commands = particleManager_->drainCommands();
            pi.unifiedRendererInitialized = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: particleInput assembled: fireEnabled={}, "
                      "unifiedRendererInitialized={}, commands={}, windStrength={:.2f}",
                      pi.fireEnabled, pi.unifiedRendererInitialized,
                      pi.commands.size(), pi.windStrength);
        }

        // Boids system input
        if (boidsManager_ && boidsManager_->isEnabled()) {
            auto& bi = input.boidsInput;
            bi.deltaTime = input.deltaTime;
            bi.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
            bi.playerHeading = playerHeading_;
            bi.timeOfDay = input.timeOfDay;
            bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            bi.initialized = true;
        }

        // Tumbleweed system input
        if (tumbleweedManager_ && tumbleweedManager_->isEnabled()) {
            auto& ti = input.tumbleweedInput;
            ti.deltaTime = input.deltaTime;
            ti.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
            ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            ti.initialized = true;
        }

        // Weather system input
        if (weatherSystem_) {
            auto& wi = input.weatherInput;
            wi.deltaTime = input.deltaTime;
            wi.initialized = true;
        }

        // Detail wind/disturbance input
        if (detailManager_ && detailManager_->isEnabled()) {
            auto& dti = input.detailInput;
            dti.deltaTime = input.deltaTime;
            const auto& wc = detailManager_->getWindController();
            const auto& wcParams = wc.getParams();
            dti.windStrength = detailManager_->getZoneConfig().windStrength;
            dti.windFrequency = wcParams.frequency;
            dti.gustFrequency = wcParams.gustFrequency;
            dti.gustStrength = wcParams.gustStrength;
            dti.windDirX = wcParams.direction.X;
            dti.windDirY = wcParams.direction.Y;
            dti.playerPosX = playerX_;
            dti.playerPosY = playerZ_;
            dti.playerPosZ = playerY_;
            dti.initialized = true;
        }

        // Run synchronous computation
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationInput: entitySnapshots={}, frustumValid={}, "
                  "particleInput.fireEnabled={}, skyEnabled={}, weatherEnabled={}, renderDist={:.0f}",
                  input.entitySnapshots.size(), input.frustumValid,
                  input.particleInput.fireEnabled, input.skyEnabled,
                  input.weatherEnabled, input.renderDistance);
        SimulationOutput output;
        simulationWorker_->computeOnce(input, output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationOutput: regions={}, objects={}, lights={}, entities={}",
                  output.regionVisible.size(), output.objectVisible.size(),
                  output.lightVisible.size(), output.entityResults.size());

        // Apply results to renderer state
        applySimulationOutput(output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: applySimulationOutput applied, currentPvsRegion_={}", currentPvsRegion_);

        // CRITICAL: applySimulationOutput sets particleRenderBuffer_ to point
        // into output.particleOutput.renderBuffer. But 'output' is a local
        // variable that will be destroyed when this scope ends, leaving a
        // dangling pointer. Null it now — the runtime SimulationWorker will
        // set it correctly from its persistent double-buffered output.
        particleRenderBuffer_ = nullptr;

        // Update door PVS visibility now that currentPvsRegion_ is set
        if (doorManager_) {
            doorManager_->setPvsRegion(currentPvsRegion_);
            doorManager_->update(0.0f);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: doorManager_ PVS region set to {}, initial update done", currentPvsRegion_);
        }

        LOG_INFO(MOD_GRAPHICS, "Sequential: SimulationWorker first-frame computed and applied "
                 "(regions={}, objects={}, lights={}, entities={})",
                 output.regionVisible.size(), output.objectVisible.size(),
                 output.lightVisible.size(), output.entityResults.size());
        logAssetBuildTime("seq_simulation", 0, stepStart);
        FlushThreadLog();

        // Set up BSP-based camera collision now that PVS region is known
        if (cameraController_ && zoneBspTree_) {
            cameraController_->setBspCollision(zoneBspTree_.get(), &regionBoundingBoxes_);
            cameraController_->setBspPlayerRegion(currentPvsRegion_);
            LOG_INFO(MOD_GRAPHICS, "Camera BSP collision initialized for region {}", currentPvsRegion_);
        }
    }

    // ── Step 13: Cleanup ───────────────────────────────────────────────────
    updateProgress(100, "Finalizing...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: releaseTextureDataAfterUpload={}, currentZone_={}, "
                  "pendingZoneComputations_={}",
                  config_.constrainedConfig.releaseTextureDataAfterUpload,
                  (bool)currentZone_, (bool)pendingZoneComputations_);
        if (config_.constrainedConfig.releaseTextureDataAfterUpload && currentZone_) {
            size_t freed = currentZone_->releaseTexturePixelData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released {:.1f}MB texture pixel data", freed / (1024.0f * 1024.0f));
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: texture pixel data retained (releaseTextureDataAfterUpload={}, currentZone_={})",
                      config_.constrainedConfig.releaseTextureDataAfterUpload, (bool)currentZone_);
        }
        if (currentZone_) {
            currentZone_->clearCharacterData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released zone character data");
        }
        if (currentZone_ && currentZone_->wldLoader) {
            currentZone_->wldLoader->releasePostLoadData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released WLD post-load data");
        } else if (currentZone_ && !currentZone_->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: wldLoader is null, skipping releasePostLoadData");
        }
        pendingZoneComputations_.reset();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: pendingZoneComputations_ released");
        logAssetBuildTime("seq_cleanup", 0, stepStart);
    }
    FlushThreadLog();

    sequentialLoadComplete_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ DONE: sequentialLoadComplete_=true, fireGlowLights_={}, "
              "fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}",
              fireGlowLights_.size(), fireGlowLightingEnabled_,
              fireGlowIcospheresEnabled_, maxFireGlowLights_);
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - totalStart).count();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: complete for zone '{}' in {}ms", currentZoneName_, totalMs);
    dumpScene();
    FlushThreadLog();
}





// ============================================================================
// Entity Loading State Management
// ============================================================================

void IrrlichtRenderer::setExpectedEntityCount(size_t count) {
    if (isLoading()) return;
    expectedEntityCount_ = count;
    // Note: Entity count is tracked for informational purposes only.
    // Zone ready is triggered when the player entity is created (isPlayer=true in createEntity).
    LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, already loaded: {}", count, loadedEntityCount_);
}

void IrrlichtRenderer::notifyEntityLoaded() {
    // Note: Entity counting is now done in createEntity() since entities are created
    // synchronously during ZoneSpawns processing. This method is kept for interface
    // compatibility but is not currently used for counting.
    // The expected/loaded entity count comparison happens in setExpectedEntityCount().
}

void IrrlichtRenderer::setNetworkReady(bool ready) {
    if (isLoading()) return;
    networkReady_ = ready;
    LOG_DEBUG(MOD_GRAPHICS, "Network ready: {}", ready);

    if (!ready) {
        // Network not ready means we're zoning, reset entity loading state
        entitiesLoaded_ = false;
        expectedEntityCount_ = 0;
        loadedEntityCount_ = 0;
        zoneReady_ = false;
    }
    // Note: Zone ready is NOT set here. It will be set when the player entity
    // is created in createEntity() with isPlayer=true. This ensures the loading
    // screen stays visible until the player model is fully loaded.
}

void IrrlichtRenderer::setWeather(uint8_t type, uint8_t intensity) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "Weather update: type={}, intensity={}", type, intensity);
    if (weatherEffects_) {
        weatherEffects_->setWeather(type, intensity);
    }
}

void IrrlichtRenderer::checkAndSetZoneReady() {
    // This method is kept for interface compatibility but zone ready
    // is now triggered by player entity creation in createEntity().
    // See createEntity() where isPlayer=true triggers zoneReady_ = true.
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


void IrrlichtRenderer::setPlayerSpawnId(uint16_t spawnId) {
    if (isLoading()) return;
    playerSpawnId_ = spawnId;
    LOG_INFO(MOD_GRAPHICS, "[IrrlichtRenderer] Player spawn ID set to: {}", spawnId);

    if (entityRenderer_) {
        entityRenderer_->setPlayerSpawnId(spawnId);

        // Apply correct visibility based on camera mode
        bool shouldHide = (cameraMode_ == CameraMode::FirstPerson);
        entityRenderer_->setPlayerEntityVisible(!shouldHide);

        // Get player model info now that the model is loaded
        float eyeHeight = entityRenderer_->getPlayerEyeHeightFromFeet();
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerSpawnId: eyeHeightFromFeet={:.2f} visible={}", eyeHeight, !shouldHide);

        // Fix first-person camera height now that we have the player model
        // playerZ_ is feet position, so camera Z = feet + eye height + user adjustment
        if (cameraMode_ == CameraMode::FirstPerson && camera_ && eyeHeight > 0.0f) {
            float camZ = playerZ_ + eyeHeight + playerConfig_.eyeHeight;

            // Update camera position with correct height
            float headingRad = playerHeading_ / 512.0f * 2.0f * irr::core::PI;
            irr::core::vector3df camPos(playerX_, camZ, playerY_);
            irr::core::vector3df target(
                playerX_ + std::sin(headingRad) * 100.0f,
                camZ,
                playerY_ + std::cos(headingRad) * 100.0f
            );

            camera_->setPosition(camPos);
            camera_->setTarget(target);

            LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] First-person camera: playerZ(feet)={:.2f} + eyeHeight={:.2f} + adjust={:.2f} = camZ={:.2f}",
                     playerZ_, eyeHeight, playerConfig_.eyeHeight, camZ);
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
        // Position camera at player location
        // z is feet position, so camera = feet + eye height
        float eyeHeight = 6.0f;  // Default fallback for human-sized entity
        if (entityRenderer_) {
            float modelEyeHeight = entityRenderer_->getPlayerEyeHeightFromFeet();
            if (modelEyeHeight > 0.0f) {
                eyeHeight = modelEyeHeight;
            }
        }
        float camZ = z + eyeHeight + playerConfig_.eyeHeight;

        // Set camera direction based on heading
        // EQ heading: 0=North(+Y), 128=West(-X), 256=South(-Y), 384=East(+X)
        // We convert to radians and use sin/cos for X/Y offset
        float headingRad = heading / 512.0f * 2.0f * irr::core::PI;
        irr::core::vector3df camPos(x, camZ, y);
        irr::core::vector3df target(
            x + std::sin(headingRad) * 100.0f,
            camZ,
            y + std::cos(headingRad) * 100.0f
        );

        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera mode=FirstPerson: z(feet)={:.2f} eyeHeight={:.2f} adjust={:.2f}",
                 z, eyeHeight, playerConfig_.eyeHeight);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera: pos=({:.2f},{:.2f},{:.2f}) -> Irrlicht(x={:.2f},y={:.2f},z={:.2f})",
                 x, y, camZ, camPos.X, camPos.Y, camPos.Z);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera: heading={:.2f} -> radians={:.4f} -> target({:.2f},{:.2f},{:.2f})",
                 heading, headingRad, target.X, target.Y, target.Z);

        camera_->setPosition(camPos);
        camera_->setTarget(target);
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

    // Show/hide player entity based on camera mode
    if (entityRenderer_) {
        entityRenderer_->setPlayerEntityVisible(cameraMode_ != CameraMode::FirstPerson);
    }
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

    // Show/hide player entity based on camera mode
    if (entityRenderer_) {
        entityRenderer_->setPlayerEntityVisible(cameraMode_ != CameraMode::FirstPerson);
    }

    // Log eye height when entering first-person mode
    if (cameraMode_ == CameraMode::FirstPerson) {
        LOG_INFO(MOD_GRAPHICS, "First Person mode - Eye height: {:.1f} (Y to raise, Shift+Y to lower)", playerConfig_.eyeHeight);
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


bool IrrlichtRenderer::processFrame(float deltaTime) {
    auto frameStart = std::chrono::steady_clock::now();
    frameStart_ = frameStart;
    sectionStart_ = frameStart;

    if (governor_) governor_->beginFrame();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: entered");

    // Record frame time for gameplay statistics (deltaTime is in seconds)
    int64_t frameTimeMs = static_cast<int64_t>(deltaTime * 1000.0f);
    if (frameTimeMs > 0) {
        EQT::PerformanceMetrics::instance().recordSample("Frame Time", frameTimeMs);
    }

    // Log warning if previous frame was slow (deltaTime > 100ms = <10 FPS)
    if (deltaTime > 0.1f) {
        LOG_WARN(MOD_GRAPHICS, "PERF: Previous frame took {} ms (slow!)", static_cast<int>(deltaTime * 1000.0f));
    }

    if (!isRunning()) {
        LOG_INFO(MOD_GRAPHICS, "processFrame: isRunning() returned false");
        LOG_INFO(MOD_GRAPHICS, "initialized_={} device_={} device_run={} quitRequested={}",
                  initialized_, (device_ ? "valid" : "null"), (device_ ? device_->run() : false),
                  (eventReceiver_ ? eventReceiver_->quitRequested() : false));
        return false;
    }

    // Update FPS
    irr::u32 currentTime = device_->getTimer()->getTime();
    frameCount_++;
    if (currentTime - lastFpsTime_ >= 1000) {
        currentFps_ = frameCount_;
        frameCount_ = 0;
        lastFpsTime_ = currentTime;
    }

    // Tiered update: increment frame counter and accumulate delta for Tier 3
    frameNumber_++;
    tier3DeltaAccum_ += deltaTime;
    runTier2_ = (frameNumber_ % kTier2Interval == 0);
    runTier3_ = (frameNumber_ % kTier3Interval == 0);

    // Safety: force Tier 2 at least once per second (~30 frames at 30fps)
    if (frameNumber_ % 30 == 0) runTier2_ = true;

    // Phase 1: Input
    processFrameInput(deltaTime);

    // ─── Always: drain queues (cheap data moves, no GL) ───
    sectionStart_ = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
    if (progressiveLoadingActive_) {
        // Loading screen: batch process freely (§4.2: no governor restriction)
        processCompletedUploads();
    } else {
        drainGPUResults();  // move to pendingGPUResults_ (no GL)
    }
#endif

    // T04: processUploadQueue/drainDecodedQueue removed. Textures uploaded via uploadTexture().
    frameTimings_.drainQueues = measureSection();

    // SimulationWorker: apply results from previous frame, then post new input
    applySimulationResults();
    frameTimings_.simWorkerApply = measureSection();

    // Phase 2: Visibility
    sectionStart_ = std::chrono::steady_clock::now();
    processFrameVisibility();
    frameTimings_.visibility = measureSection();

    // SimulationWorker: post new input after frustum planes are updated
    postSimulationInput(deltaTime);
    frameTimings_.postSimInput = measureSection();

    // ─── Progressive loading (loading screen showing, no governor restriction §4.2) ───
    if (progressiveLoadingActive_) {
        processFrameProgressiveLoad();
        frameTimings_.meshLoading = measureSection();
    }

    // ─── Post-loading: all work GREEN-gated, 1 unit per GREEN frame ───
    sectionStart_ = std::chrono::steady_clock::now();
    if (!progressiveLoadingActive_) {
        // Drain bg mesh results (cheap mutex swap, no GL)
        drainMeshResults();

        if (!governor_ || governor_->getState() == BudgetState::Green) {
            bool didWork = false;

            // Priority 1: Complete in-flight GPU work (VBO/texture registration)
#ifdef EQT_HAS_GLES2
            if (!didWork) didWork = processOneGPUResult();
#endif

            // T04: processOneUpload removed. Textures uploaded via uploadTexture().

            // Priority 3: Finalize background mesh build (add to scene graph)
            if (!didWork) didWork = finalizeOneMeshResult();

            // Priority 4: Material swap (placeholder → real texture)
            if (!didWork) didWork = processOneMaterialSwap();

            // Priority 5: Entity prep results
            if (!didWork && entityPrepReady_ && entityRenderer_)
                didWork = entityRenderer_->pollAndDistributePrepResults();

            // Priority 6: Entity mesh building
            if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild)
                didWork = entityRenderer_->processOneEntityBuildStep(currentPvsRegion_);

            // Priority 7: Texture rebuild (full rebuild for regions without buffer map)
            if (!didWork && constrainedMeshCache_ && !textureRebuildQueue_.empty()) {
                auto texEntry = textureRebuildQueue_.front();
                textureRebuildQueue_.erase(textureRebuildQueue_.begin());

                auto existingIt = regionMeshNodes_.find(texEntry.regionIdx);
                if (existingIt != regionMeshNodes_.end() && existingIt->second) {
                    deleteMeshHardwareBuffers(existingIt->second);
                    if (animatedTextureManager_)
                        animatedTextureManager_->removeSceneNode(existingIt->second);
                    if (existingIt->second->getParent()) existingIt->second->remove();
                    else existingIt->second->drop();
                    existingIt->second = nullptr;
                }

                constrainedMeshCache_->markForRebuild(texEntry.regionIdx);
                if (rebuildRegionMesh(texEntry.regionIdx)) {
                    addRegionToCollision(texEntry.regionIdx);
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt region {} with textures (queue remaining: {})",
                             texEntry.regionIdx, textureRebuildQueue_.size());
                }
                didWork = true;
            }

            // Priority 8: Door texture arrival
            if (!didWork && !doorTextureRebuildQueue_.empty() && doorManager_) {
                uint8_t doorId = doorTextureRebuildQueue_.front();
                doorTextureRebuildQueue_.erase(doorTextureRebuildQueue_.begin());
                doorManager_->rebuildSingleDoor(doorId);
                didWork = true;
                LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt door {} with textures (queue: {})",
                         doorId, doorTextureRebuildQueue_.size());
            }

            // Priority 9: Object texture arrival
            if (!didWork && !objectTextureRebuildQueue_.empty()) {
                size_t objIdx = objectTextureRebuildQueue_.front();
                objectTextureRebuildQueue_.erase(objectTextureRebuildQueue_.begin());
                if (objIdx < deferredObjects_.size()) {
                    size_t nodeIdx = deferredObjects_[objIdx].nodeIndex;
                    if (nodeIdx != SIZE_MAX && nodeIdx < objectNodes_.size() && objectNodes_[nodeIdx]) {
                        auto* oldNode = objectNodes_[nodeIdx];
                        if (animatedTextureManager_)
                            animatedTextureManager_->removeSceneNode(oldNode);
                        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
                        objectNodes_[nodeIdx] = nullptr;
                        if (nodeIdx < objectInSceneGraph_.size())
                            objectInSceneGraph_[nodeIdx] = false;
                    }
                    deferredObjects_[objIdx].meshBuilt = false;
                    buildDeferredObject(objIdx);
                    didWork = true;
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt object {} with textures (queue: {})",
                             objIdx, objectTextureRebuildQueue_.size());
                }
            }

            // Priority 10: Submit new background mesh build
            if (!didWork && constrainedMeshCache_) didWork = submitOneBgMeshBuild();

            // Priority 11: Lazy icon loading (disabled — WindowManager removed)

            if (didWork) {
                frameTimings_.meshLoading = measureSection();
            }
        }

        // Lightweight entity housekeeping (always runs, not didWork-gated):
        if (entityPrepReady_ && entityPrepWorker_ && entityRenderer_) {
            entityRenderer_->getRaceModelLoader()->promotePreparedModels();
            if (++entityPrepScanCounter_ % 10 == 0) queueEntityPrepRequests();
            if (entityPrepWorker_->isIdle()) entityPrepWorker_->dispatchOne();
        }
        frameTimings_.entityHousekeeping = measureSection();
    }
    frameTimings_.postLoadWork = measureSection();

    // Phase 3: Simulation
    processFrameSimulation(deltaTime);

    // Phase 4: Render
    if (!processFrameRender(deltaTime)) {
        // Loading screen was shown - skip rest of render
        return true;
    }

    // ===== FRAME TIMING: Total Frame (always-on) =====
    {
        auto frameEnd = std::chrono::steady_clock::now();
        frameTimings_.totalFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();

        // Update render cost EMA (alpha = 0.1 for smooth tracking)
        int64_t renderUs = frameTimings_.sceneDrawAll + frameTimings_.targetBox +
                           frameTimings_.particles + frameTimings_.boids +
                           frameTimings_.weatherRender + frameTimings_.debugOverlays +
                           frameTimings_.castingBars + frameTimings_.guiDrawAll +
                           frameTimings_.staticUI + frameTimings_.zoneLineOverlay +
                           frameTimings_.endScene + frameTimings_.footprintRender +
                           frameTimings_.postRender;
        renderCostAvgUs_ = renderCostAvgUs_ * 0.9f + static_cast<float>(renderUs) * 0.1f;

        // Update essential simulation cost EMA
        int64_t essSimUs = frameTimings_.entityUpdate + frameTimings_.doorUpdate +
                           frameTimings_.spellVfxUpdate + frameTimings_.animatedTextures +
                           frameTimings_.vertexAnimations + frameTimings_.hudUpdate;
        essentialSimCostAvgUs_ = essentialSimCostAvgUs_ * 0.9f + static_cast<float>(essSimUs) * 0.1f;

        // Record frame in governor and log budget violations.
        // endScene is passed for diagnostics but NOT subtracted — full wall-clock
        // frame time is recorded. On shared-bus ARM SoCs, subtracting endScene
        // blinds the governor to GPU pipeline stalls from progressive loading.
        if (governor_) {
            bool hasPendingWork = progressiveLoadingActive_;
            // meshLoadQueue_ is repopulated every frame by SimulationWorker
            // (it's a "regions that should be loaded" list, not a drain queue).
            // Only count as pending work if entries actually need building.
            if (!hasPendingWork && constrainedMeshCache_) {
                for (const auto& entry : meshLoadQueue_) {
                    if (!constrainedMeshCache_->isLoaded(entry.regionIdx)) {
                        hasPendingWork = true;
                        break;
                    }
                }
            }
            governor_->endFrame(static_cast<float>(frameTimings_.endScene), hasPendingWork);
        }

        float totalFrameMs = frameTimings_.totalFrame / 1000.0f;
        float budgetMs = governor_ ? governor_->getTargetFrameTimeMs() : 33.3f;

        // ===== SPIKE DETECTOR: unconditional, fires on any frame >50ms =====
        // Logs every section >1ms sorted by cost. Independent of governor state.
        if (totalFrameMs > 50.0f) {
            struct SectionCost { const char* name; int64_t us; };
            SectionCost spikeSections[] = {
                {"inputHandling",      frameTimings_.inputHandling},
                {"playerMovement",     frameTimings_.playerMovement},
                {"cameraUpdate",       frameTimings_.cameraUpdate},
                {"drainQueues",        frameTimings_.drainQueues},
                {"simWorkerApply",     frameTimings_.simWorkerApply},
                {"visibility",         frameTimings_.visibility},
                {"postSimInput",       frameTimings_.postSimInput},
                {"meshLoading",        frameTimings_.meshLoading},
                {"postLoadWork",       frameTimings_.postLoadWork},
                {"entityHousekeeping", frameTimings_.entityHousekeeping},
                {"entityUpdate",       frameTimings_.entityUpdate},
                {"doorUpdate",         frameTimings_.doorUpdate},
                {"spellVfxUpdate",     frameTimings_.spellVfxUpdate},
                {"animatedTextures",   frameTimings_.animatedTextures},
                {"vertexAnimations",   frameTimings_.vertexAnimations},
                {"tier2Update",        frameTimings_.tier2Update},
                {"tier3Update",        frameTimings_.tier3Update},
                {"hudUpdate",          frameTimings_.hudUpdate},
                {"sceneDrawAll",       frameTimings_.sceneDrawAll},
                {"footprintRender",    frameTimings_.footprintRender},
                {"targetBox",          frameTimings_.targetBox},
                {"particles",          frameTimings_.particles},
                {"boids",              frameTimings_.boids},
                {"weatherRender",      frameTimings_.weatherRender},
                {"debugOverlays",      frameTimings_.debugOverlays},
                {"castingBars",        frameTimings_.castingBars},
                {"guiDrawAll",         frameTimings_.guiDrawAll},
                {"staticUI",           frameTimings_.staticUI},
                {"zoneLineOverlay",    frameTimings_.zoneLineOverlay},
                {"endScene",           frameTimings_.endScene},
                {"postRender",         frameTimings_.postRender},
            };
            std::sort(std::begin(spikeSections), std::end(spikeSections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Sum all measured sections to find unaccounted time
            int64_t accountedUs = 0;
            for (const auto& s : spikeSections) accountedUs += s.us;
            int64_t unaccountedUs = frameTimings_.totalFrame - accountedUs;

            LOG_WARN(MOD_GRAPHICS, "SPIKE: {:.1f}ms frame (unaccounted: {:.1f}ms):",
                     totalFrameMs, unaccountedUs / 1000.0f);
            for (const auto& s : spikeSections) {
                if (s.us < 1000) break;  // Skip <1ms
                LOG_WARN(MOD_GRAPHICS, "  SPIKE: {:>20s} = {:>7.1f}ms",
                         s.name, s.us / 1000.0f);
            }
        }

        // RED STATE: Full diagnostic dump — every section, every frame, so we can
        // trace exactly which subsystem is the budget hog and eliminate it.
        if (governor_ &&
            governor_->getState() == BudgetState::Red) {
            struct SectionCost { const char* name; int64_t us; };
            SectionCost sections[] = {
                {"inputHandling",    frameTimings_.inputHandling},
                {"playerMovement",   frameTimings_.playerMovement},
                {"cameraUpdate",     frameTimings_.cameraUpdate},
                {"pvsVisibility",    frameTimings_.pvsVisibility},
                {"occlusionCulling", frameTimings_.occlusionCulling},
                {"zoneLightVis",     frameTimings_.zoneLightVisibility},
                {"objectVisibility", frameTimings_.objectVisibility},
                {"meshLoading",      frameTimings_.meshLoading},
                {"objectLights",     frameTimings_.objectLights},
                {"entityUpdate",     frameTimings_.entityUpdate},
                {"doorUpdate",       frameTimings_.doorUpdate},
                {"spellVfxUpdate",   frameTimings_.spellVfxUpdate},
                {"animatedTextures", frameTimings_.animatedTextures},
                {"vertexAnimations", frameTimings_.vertexAnimations},
                {"fireFlicker",      frameTimings_.fireFlicker},
                {"tier2Update",      frameTimings_.tier2Update},
                {"tier3Update",      frameTimings_.tier3Update},
                {"hudUpdate",        frameTimings_.hudUpdate},
                {"sceneDrawAll",     frameTimings_.sceneDrawAll},
                {"sceneAnimate",     frameTimings_.sceneAnimate},
                {"sceneSolid",       frameTimings_.sceneSolid},
                {"sceneTransparent", frameTimings_.sceneTransparent},
                {"manualZoneDraw",   frameTimings_.manualZoneDraw},
                {"footprintRender",  frameTimings_.footprintRender},
                {"targetBox",        frameTimings_.targetBox},
                {"particles",        frameTimings_.particles},
                {"weatherRender",    frameTimings_.weatherRender},
                {"castingBars",      frameTimings_.castingBars},
                {"guiDrawAll",       frameTimings_.guiDrawAll},
                {"staticUI",         frameTimings_.staticUI},
                {"glFinish",         frameTimings_.glFinish},
                {"endScene",         frameTimings_.endScene},
                {"postRender",       frameTimings_.postRender},
                {"simWorkerApply",   frameTimings_.simWorkerApply},
            };
            std::sort(std::begin(sections), std::end(sections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Throttle RED warnings to 1 per second
            static auto lastRedWarnTime = std::chrono::steady_clock::now();
            static int redFramesSinceWarn = 0;
            redFramesSinceWarn++;
            auto redNow = std::chrono::steady_clock::now();
            bool emitRedWarn = std::chrono::duration_cast<std::chrono::milliseconds>(redNow - lastRedWarnTime).count() >= 1000;
            if (emitRedWarn) {
                lastRedWarnTime = redNow;
            }

            // Log header with totals + scene composition
            if (emitRedWarn) {
                LOG_WARN(MOD_GRAPHICS, "RED STATE: {:.1f}ms / {:.1f}ms budget ({:.0f}% over) — avg {:.1f}ms, stall {}/{} — {} red frames in last 1s",
                         totalFrameMs, budgetMs,
                         (totalFrameMs / budgetMs - 1.0f) * 100.0f,
                         governor_->getAverageFrameTimeMs(),
                         governor_->getStallCounter(),
                         FrameBudgetGovernor::kStallThreshold,
                         redFramesSinceWarn);
                LOG_WARN(MOD_GRAPHICS, "  RED SCENE: {} sceneNodes, {} polys | manualDraw: {} regions, {} drawCalls | sortedList: {}",
                         frameTimings_.sceneNodeCount, lastPolygonCount_,
                         frameTimings_.manualZoneRegionsDrawn, frameTimings_.manualZoneDrawCalls,
                         sortedZoneDrawList_.size());
                {
                    int totalNodes = 0, visibleNodes = 0;
                    std::function<void(irr::scene::ISceneNode*)> countNodes =
                        [&](irr::scene::ISceneNode* node) {
                            if (!node) return;
                            totalNodes++;
                            if (node->isVisible()) visibleNodes++;
                            for (auto* child : node->getChildren())
                                countNodes(child);
                        };
                    if (smgr_ && smgr_->getRootSceneNode())
                        countNodes(smgr_->getRootSceneNode());
                    LOG_WARN(MOD_GRAPHICS, "  RED GRAPH: {} total scene nodes, {} visible (drawAll traverses all)",
                             totalNodes, visibleNodes);
                }
                for (const auto& s : sections) {
                    if (s.us < 100) continue;
                    LOG_WARN(MOD_GRAPHICS, "  RED: {:>20s} = {:>7.1f}ms ({:>5.1f}%)",
                             s.name, s.us / 1000.0f,
                             totalFrameMs > 0 ? (s.us / 1000.0f / totalFrameMs * 100.0f) : 0.0f);
                }
                redFramesSinceWarn = 0;
            }
        }
        // Yellow/Green budget violation: condensed top-3 warning, throttled to 1/sec
        else if (totalFrameMs > budgetMs * 1.2f) {
            static auto lastBudgetWarnTime = std::chrono::steady_clock::now();
            static int budgetFramesSinceWarn = 0;
            budgetFramesSinceWarn++;
            auto budgetNow = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(budgetNow - lastBudgetWarnTime).count() >= 1000) {
                struct SectionCost { const char* name; int64_t us; };
                SectionCost sections[] = {
                    {"playerMovement", frameTimings_.playerMovement},
                    {"pvsVisibility", frameTimings_.pvsVisibility},
                    {"occlusionCulling", frameTimings_.occlusionCulling},
                    {"meshLoading", frameTimings_.meshLoading},
                    {"entityUpdate", frameTimings_.entityUpdate},
                    {"tier2Update", frameTimings_.tier2Update},
                    {"tier3Update", frameTimings_.tier3Update},
                    {"sceneDrawAll", frameTimings_.sceneDrawAll},
                    {"staticUI", frameTimings_.staticUI},
                    {"endScene", frameTimings_.endScene},
                    {"particles", frameTimings_.particles},
                    {"simWorkerApply", frameTimings_.simWorkerApply},
                    {"targetBox", frameTimings_.targetBox},
                    {"guiDrawAll", frameTimings_.guiDrawAll},
                    {"footprintRender", frameTimings_.footprintRender},
                };
                std::sort(std::begin(sections), std::end(sections),
                          [](const auto& a, const auto& b) { return a.us > b.us; });

                LOG_WARN(MOD_GRAPHICS, "BUDGET: {:.1f}ms (budget {:.1f}ms, gov={}) top: {}={:.1f}ms {}={:.1f}ms {}={:.1f}ms — {} frames in last 1s",
                         totalFrameMs, budgetMs,
                         governor_ ? governor_->getStateName() : "N/A",
                         sections[0].name, sections[0].us / 1000.0f,
                         sections[1].name, sections[1].us / 1000.0f,
                         sections[2].name, sections[2].us / 1000.0f,
                         budgetFramesSinceWarn);
                lastBudgetWarnTime = budgetNow;
                budgetFramesSinceWarn = 0;
            }
        }
    }

    // Accumulate and periodically log (gated behind /frametiming command)
    if (frameTimingEnabled_) {
        frameTimingsAccum_.inputHandling += frameTimings_.inputHandling;
        frameTimingsAccum_.cameraUpdate += frameTimings_.cameraUpdate;
        frameTimingsAccum_.entityUpdate += frameTimings_.entityUpdate;
        frameTimingsAccum_.doorUpdate += frameTimings_.doorUpdate;
        frameTimingsAccum_.spellVfxUpdate += frameTimings_.spellVfxUpdate;
        frameTimingsAccum_.animatedTextures += frameTimings_.animatedTextures;
        frameTimingsAccum_.vertexAnimations += frameTimings_.vertexAnimations;
        frameTimingsAccum_.fireFlicker += frameTimings_.fireFlicker;
        frameTimingsAccum_.objectVisibility += frameTimings_.objectVisibility;
        frameTimingsAccum_.pvsVisibility += frameTimings_.pvsVisibility;
        frameTimingsAccum_.meshLoading += frameTimings_.meshLoading;
        frameTimingsAccum_.objectLights += frameTimings_.objectLights;
        frameTimingsAccum_.tier2Update += frameTimings_.tier2Update;
        frameTimingsAccum_.tier3Update += frameTimings_.tier3Update;
        frameTimingsAccum_.hudUpdate += frameTimings_.hudUpdate;
        frameTimingsAccum_.sceneDrawAll += frameTimings_.sceneDrawAll;
        frameTimingsAccum_.sceneAnimate += frameTimings_.sceneAnimate;
        frameTimingsAccum_.sceneSolid += frameTimings_.sceneSolid;
        frameTimingsAccum_.sceneTransparent += frameTimings_.sceneTransparent;
        frameTimingsAccum_.sceneSkybox += frameTimings_.sceneSkybox;
        frameTimingsAccum_.sceneOther += frameTimings_.sceneOther;
        frameTimingsAccum_.sceneNodeCount += frameTimings_.sceneNodeCount;
        frameTimingsAccum_.manualZoneDraw += frameTimings_.manualZoneDraw;
        frameTimingsAccum_.targetBox += frameTimings_.targetBox;
        frameTimingsAccum_.particles += frameTimings_.particles;
        frameTimingsAccum_.boids += frameTimings_.boids;
        frameTimingsAccum_.weatherRender += frameTimings_.weatherRender;
        frameTimingsAccum_.debugOverlays += frameTimings_.debugOverlays;
        frameTimingsAccum_.castingBars += frameTimings_.castingBars;
        frameTimingsAccum_.guiDrawAll += frameTimings_.guiDrawAll;
        frameTimingsAccum_.staticUI += frameTimings_.staticUI;
        frameTimingsAccum_.zoneLineOverlay += frameTimings_.zoneLineOverlay;
        frameTimingsAccum_.glFinish += frameTimings_.glFinish;
        frameTimingsAccum_.endScene += frameTimings_.endScene;
        frameTimingsAccum_.manualZoneRegionsDrawn += frameTimings_.manualZoneRegionsDrawn;
        frameTimingsAccum_.manualZoneDrawCalls += frameTimings_.manualZoneDrawCalls;
        frameTimingsAccum_.totalFrame += frameTimings_.totalFrame;
        // New fine-grained fields
        frameTimingsAccum_.playerMovement += frameTimings_.playerMovement;
        frameTimingsAccum_.occlusionCulling += frameTimings_.occlusionCulling;
        frameTimingsAccum_.zoneLightVisibility += frameTimings_.zoneLightVisibility;
        frameTimingsAccum_.weatherSystemUpdate += frameTimings_.weatherSystemUpdate;
        frameTimingsAccum_.footprintRender += frameTimings_.footprintRender;
        frameTimingsAccum_.postRender += frameTimings_.postRender;
        frameTimingsAccum_.simWorkerApply += frameTimings_.simWorkerApply;
        frameTimingsAccum_.drainQueues += frameTimings_.drainQueues;
        frameTimingsAccum_.visibility += frameTimings_.visibility;
        frameTimingsAccum_.postSimInput += frameTimings_.postSimInput;
        frameTimingsAccum_.postLoadWork += frameTimings_.postLoadWork;
        frameTimingsAccum_.entityHousekeeping += frameTimings_.entityHousekeeping;
        frameTimingsSampleCount_++;

        // Log every 60 frames (~2 seconds at 30fps)
        if (frameTimingsSampleCount_ >= 60) {
            logFrameTimings();
            // Reset accumulators
            frameTimingsAccum_ = FrameTimings();
            frameTimingsSampleCount_ = 0;
        }
    }

    return true;
}

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
    }

    // Update camera and player movement
    updatePlayerMovement(deltaTime);
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
        handleMouseTargeting(clickX, clickY);
    }
}

void IrrlichtRenderer::processCommonInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        switch (event.action) {
            case RA::Screenshot: pendingScreenshot_ = true; break;
            case RA::ToggleAllUI: toggleAllUI(); break;
            case RA::ToggleZoneLights: toggleZoneLights(); break;
            case RA::CycleObjectLights: cycleObjectLights(); break;
            case RA::ToggleOptions:
                // WindowManager removed — options window not available
                break;
            case RA::ClearTarget:
                if (currentTargetId_ != 0) {
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                    clearCurrentTarget();
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
                if (vendorToggleCallback_) vendorToggleCallback_();
                if (bridge_) bridge_->pushIntent(eqt::events::VendorToggleIntent{});
                break;
            case RA::ToggleTrainer:
                if (trainerToggleCallback_) trainerToggleCallback_();
                if (bridge_) bridge_->pushIntent(eqt::events::TrainerToggleIntent{});
                break;
            case RA::DoorInteract:
                if (doorManager_) {
                    uint8_t doorId = doorManager_->getNearestDoor(playerX_, playerY_, playerZ_, playerHeading_);
                    if (doorId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "Door interaction: ID {}", doorId);
                        if (doorInteractCallback_) doorInteractCallback_(doorId);
                        if (bridge_) bridge_->pushIntent(eqt::events::DoorInteractIntent{doorId});
                    }
                }
                break;
            case RA::WorldObjectInteract: {
                    uint32_t objectId = getNearestWorldObject(playerX_, playerY_, playerZ_);
                    if (objectId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "World object interaction: dropId {}", objectId);
                        if (worldObjectInteractCallback_) worldObjectInteractCallback_(objectId);
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
            case RA::ToggleZoneLineVisualization: toggleZoneLineVisualization(); break;
            case RA::ToggleMapOverlay: toggleMapOverlay(); break;
            case RA::RotateMapOverlay:
                mapOverlayRotation_ = (mapOverlayRotation_ + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable rotation: {}° around Y axis (terrain unchanged)", mapOverlayRotation_ * 90);
                break;
            case RA::MirrorXMapOverlay:
                mapOverlayMirrorX_ = !mapOverlayMirrorX_;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable X mirror: {} (terrain unchanged)", mapOverlayMirrorX_ ? "ON" : "OFF");
                break;
            case RA::ToggleNavmeshOverlay: toggleNavmeshOverlay(); break;
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
    // Handle spell gem shortcuts (1-8 keys)
    int8_t spellGemRequest = eventReceiver_->getSpellGemCastRequest();
    if (spellGemRequest >= 0 && !chatInputFocused_) {
        if (spellGemCastCallback_) {
            LOG_DEBUG(MOD_GRAPHICS, "Spell gem {} pressed", spellGemRequest + 1);
            spellGemCastCallback_(static_cast<uint8_t>(spellGemRequest));
        }
    }

    // Handle hotbar shortcuts (WindowManager removed — hotbar handled by new UI)
    int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
    (void)hotbarRequest;

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
        if (currentTargetId_ != 0) {
            LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
            clearCurrentTarget();
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
        // Worker computes tree vertex positions — just advance wind controller time
        if (treeManager_ && treeManager_->isEnabled()
            && treeManager_->getAnimatedTreeCount() > 0) {
            treeManager_->getWindController().update(deltaTime);
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
    updateHUD();
    frameTimings_.hudUpdate = measureSection();
}

// ===== Phase 4: Render =====
bool IrrlichtRenderer::processFrameRender(float deltaTime) {
    // If loading screen is visible, show it instead of rendering zone
    if (loadingScreenVisible_) {
        drawLoadingScreen(loadingProgress_, loadingText_);
        return false;  // Signal loading screen was shown
    }

    // Run scene breakdown profile if scheduled
    if (sceneProfileEnabled_) {
        if (sceneProfileFrameCount_ < 0) {
            sceneProfileFrameCount_++;
        } else {
            profileSceneBreakdown();
        }
    }

    // Render - use sky renderer's clear color for day/night effect
    irr::video::SColor clearColor(255, 50, 50, 80);
    if (skyRenderer_ && skyRenderer_->isEnabled() && skyRenderer_->isInitialized()) {
        clearColor = skyRenderer_->getCurrentClearColor();
    }
    LOG_TRACE(MOD_GRAPHICS, "PRE-DRAW checkpoint 0: beginScene");
    driver_->beginScene(true, true, clearColor);
    sectionStart_ = std::chrono::steady_clock::now();
    if (renderPassTimer_) renderPassTimer_->reset();
    if (zoneShader_) {
        // Update wind shader uniforms each frame
        windTime_ += deltaTime;
        zoneShader_->setWindTime(windTime_);
        // Wind params from tree wind config (or defaults) with weather multiplier
        float weatherMult = 1.0f;
        if (treeManager_) {
            weatherMult = treeManager_->getWindController().getWeatherMultiplier();
        }
        // Default config values scaled by weather
        zoneShader_->setWindParams(
            0.3f * weatherMult,   // baseStrength
            0.4f,                 // baseFrequency
            0.5f * weatherMult,   // gustStrength
            0.1f);                // gustFrequency

        zoneShader_->beginFrame();
    }
    smgr_->drawAll();
    frameTimings_.sceneDrawAll = measureSection();

    // 3D camera transforms are captured during the SOLID render pass in
    // OnRenderPassPreRender — see captured3DView_/captured3DProj_.
    if (renderPassTimer_) {
        frameTimings_.sceneAnimate = renderPassTimer_->getAnimateTime();
        frameTimings_.sceneSolid = renderPassTimer_->solidTime;
        frameTimings_.sceneTransparent = renderPassTimer_->transparentTime;
        frameTimings_.sceneSkybox = renderPassTimer_->skyboxTime;
        frameTimings_.sceneOther = renderPassTimer_->otherTime;
        frameTimings_.sceneNodeCount = renderPassTimer_->nodeCount;
    }
    if (frameTimings_.sceneDrawAll > 50000) {
        LOG_WARN(MOD_GRAPHICS, "PERF: smgr->drawAll() took {} ms", frameTimings_.sceneDrawAll / 1000);
    }

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 1: getPrimitiveCountDrawn");
    // Track polygon count for constrained mode budget
    lastPolygonCount_ = driver_->getPrimitiveCountDrawn();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 2: footprints (detail={})", detailManager_ != nullptr);
    // Render footprints (after terrain, before UI)
    if (detailManager_ && detailManager_->isFootprintEnabled()) {
        detailManager_->renderFootprints();
    }
    frameTimings_.footprintRender = measureSection();

    if (lastPolygonCount_ > static_cast<uint32_t>(config_.constrainedConfig.maxPolygonsPerFrame)) {
        if (++polygonBudgetExceededFrames_ >= 60) {
            LOG_WARN(MOD_GRAPHICS, "Polygon budget exceeded: {} > {} (limit)",
                     lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame);
            polygonBudgetExceededFrames_ = 0;
        }
    } else {
        polygonBudgetExceededFrames_ = 0;
    }

    if (++constrainedStatsLogCounter_ >= 150) {
        constrainedStatsLogCounter_ = 0;
        int visibleEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
        int totalEntities = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
        size_t tmuUsed = 0, tmuLimit = 0;
        float hitRate = 0.0f;
        size_t evictions = 0;
        if (constrainedTextureCache_) {
            tmuUsed = constrainedTextureCache_->getCurrentUsage();
            tmuLimit = constrainedTextureCache_->getMemoryLimit();
            hitRate = constrainedTextureCache_->getHitRate();
            evictions = constrainedTextureCache_->getEvictionCount();
        }
        size_t fbiUsed = config_.constrainedConfig.calculateFramebufferUsage(config_.width, config_.height);
        size_t fbiLimit = config_.constrainedConfig.framebufferMemoryBytes;
        LOG_INFO(MOD_GRAPHICS, "=== RENDERER STATS [{}] ===",
                 ConstrainedRendererConfig::presetName(config_.constrainedPreset));
        LOG_INFO(MOD_GRAPHICS, "  Resolution: {}x{} @ {}-bit (FBI: {:.1f}MB/{:.1f}MB)",
                 config_.width, config_.height, config_.constrainedConfig.colorDepthBits,
                 fbiUsed / (1024.0f * 1024.0f), fbiLimit / (1024.0f * 1024.0f));
        LOG_INFO(MOD_GRAPHICS, "  Textures: TMU {:.1f}MB/{:.1f}MB | Hit: {:.0f}% | Evictions: {}",
                 tmuUsed / (1024.0f * 1024.0f), tmuLimit / (1024.0f * 1024.0f), hitRate, evictions);
        LOG_INFO(MOD_GRAPHICS, "  Geometry: Polys {}/{} | Entities {}/{} (max {}) | Clip {:.0f}",
                 lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame,
                 visibleEntities, totalEntities, config_.constrainedConfig.maxVisibleEntities,
                 config_.constrainedConfig.clipDistance);
        LOG_INFO(MOD_GRAPHICS, "  FPS: {}", currentFps_);
#ifdef __linux__
        {
            // Governor exception: procfs is memory-mapped kernel data, not disk I/O (~1μs)
            FILE* f = fopen("/proc/self/statm", "r");
            if (f) {
                unsigned long vm = 0, rss = 0;
                if (fscanf(f, "%lu %lu", &vm, &rss) == 2) {
                    long ps = sysconf(_SC_PAGESIZE);
                    LOG_INFO(MOD_GRAPHICS, "  Memory: RSS {:.1f}MB",
                             (rss * ps) / (1024.0f * 1024.0f));
                }
                fclose(f);
            }
        }
#endif
    }

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 3: target outline");
    // Draw selection indicator around targeted entity
#ifdef EQT_HAS_GLES2
    drawTargetOutline();
#else
    drawTargetSelectionBox();
#endif
    frameTimings_.targetBox = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 4: particles (mgr={}, enabled={}, zoneReady={})",
              particleManager_ != nullptr,
              particleManager_ ? particleManager_->isEnabled() : false,
              zoneReady_);
    // Render environmental particles (render every frame, update at Tier 3)
    if (particleManager_ && particleManager_->isEnabled() && zoneReady_) particleManager_->render();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 5: unified particles (have3D={})", have3DTransforms_);
    // Unified particle system (fire point sprites, GLES2 only)
    // Not gated by isEnabled() — fire has its own toggle (unifiedFireEnabled_)
#ifdef EQT_HAS_GLES2
    if (particleManager_ && zoneReady_ && have3DTransforms_) {
        // Pass View and Projection separately — captured during ESNRP_SOLID pass
        // before any 2D drawing overwrites the driver's transform state.
        // The shader multiplies them in GLSL (same as built-in COGLES2 shaders).
        float fogStart = zoneShader_ ? zoneShader_->fogStart() : 999999.0f;
        float fogEnd = zoneShader_ ? zoneShader_->fogEnd() : 999999.0f;
        const float* fogCol = zoneShader_ ? zoneShader_->fogColor() : nullptr;
        float screenH = static_cast<float>(driver_->getScreenSize().Height);
        particleManager_->renderUnified(captured3DView_, captured3DProj_,
                                        camera_ ? camera_->getAbsolutePosition() : irr::core::vector3df(0, 0, 0),
                                        fogStart, fogEnd, fogCol, screenH,
                                        particleRenderBuffer_);
    }
#endif
    frameTimings_.particles = measureSection();

    // Fire glow: update per-vertex lighting uniforms + render icospheres
#ifdef EQT_HAS_DRM
    if (fireEffectsEnabled_ && !fireGlowLights_.empty() && zoneReady_ && have3DTransforms_) {
        fireGlowTime_ += deltaTime;
        if (fireGlowLightingEnabled_) {
            updateFireGlowLighting();
        }
        if (fireGlowIcospheresEnabled_ && icosphereProgram_ != 0) {
            renderFireGlowIcospheres();
        }
    }
#endif

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 6: boids");
    // Render ambient creatures (render every frame, update at Tier 3)
    if (boidsManager_ && boidsManager_->isEnabled() && zoneReady_) boidsManager_->render();
    frameTimings_.boids = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 7: weather");
    // Render weather effects
    if (weatherEffects_ && weatherEffects_->isEnabled()) weatherEffects_->render();
    frameTimings_.weatherRender = measureSection();

    // Draw collision debug lines
    if (playerConfig_.collisionDebug) drawCollisionDebugLines(deltaTime);

    // Culling debug: draw region bounding boxes as wireframe
    // Green = rendered (passes PVS + distance + frustum)
    // Blue = PVS-culled (occluded by walls/geometry)
    // Yellow = distance-culled (beyond render distance)
    // Red = frustum-culled (outside camera view cone)
    if (frustumDebugDraw_ && frustumCuller_) {
        // Disable depth test so debug lines are always visible on top of geometry
        irr::video::SMaterial debugMat;
        debugMat.Lighting = false;
        debugMat.ZBuffer = irr::video::ECFN_ALWAYS;
        debugMat.ZWriteEnable = false;
        debugMat.AntiAliasing = false;
        debugMat.Thickness = 2.0f;
        driver_->setMaterial(debugMat);
        driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

        // Get current PVS region for occlusion check
        std::shared_ptr<EQT::Graphics::BspRegion> pvsRegion;
        if (usePvsCulling_ && zoneBspTree_ && currentPvsRegion_ != SIZE_MAX
            && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            pvsRegion = zoneBspTree_->regions[currentPvsRegion_];
        }

        float camX = playerX_, camY = playerY_, camZ = playerZ_;

        for (auto& [regionIdx, eqBbox] : regionBoundingBoxes_) {
            // Determine culling state using the same logic as the rendering pipeline
            irr::video::SColor color;

            // 1. PVS check
            bool pvsVisible = true;
            if (pvsRegion && !pvsRegion->visibleRegions.empty()) {
                if (regionIdx == currentPvsRegion_) {
                    pvsVisible = true;
                } else if (regionIdx < pvsRegion->visibleRegions.size()) {
                    pvsVisible = pvsRegion->visibleRegions[regionIdx];
                }
            }

            if (!pvsVisible) {
                color = irr::video::SColor(255, 64, 64, 255);  // Blue = PVS-culled
            } else {
                // 2. Distance check
                float closestX = std::max(eqBbox.MinEdge.X, std::min(camX, eqBbox.MaxEdge.X));
                float closestY = std::max(eqBbox.MinEdge.Y, std::min(camY, eqBbox.MaxEdge.Y));
                float closestZ = std::max(eqBbox.MinEdge.Z, std::min(camZ, eqBbox.MaxEdge.Z));
                float ddx = camX - closestX, ddy = camY - closestY, ddz = camZ - closestZ;
                float dist = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);

                if (dist > renderDistance_) {
                    color = irr::video::SColor(255, 255, 255, 0);  // Yellow = distance-culled
                } else {
                    // 3. Frustum check
                    bool inFrustum = frustumCuller_->testAABB(
                        eqBbox.MinEdge.X, eqBbox.MinEdge.Y, eqBbox.MinEdge.Z,
                        eqBbox.MaxEdge.X, eqBbox.MaxEdge.Y, eqBbox.MaxEdge.Z);

                    if (!inFrustum) {
                        color = irr::video::SColor(255, 255, 0, 0);  // Red = frustum-culled
                    } else if (occlusionCulledRegions_.count(regionIdx)) {
                        color = irr::video::SColor(200, 255, 0, 255);  // Magenta = occlusion-culled
                    } else {
                        color = irr::video::SColor(255, 0, 255, 0);  // Green = rendered
                    }
                }
            }

            // Convert EQ Z-up bbox to Irrlicht Y-up corners: EQ(x,y,z) -> Irr(x,z,y)
            irr::core::vector3df corners[8];
            corners[0] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MinEdge.Z, eqBbox.MinEdge.Y);
            corners[1] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MinEdge.Z, eqBbox.MinEdge.Y);
            corners[2] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MaxEdge.Z, eqBbox.MinEdge.Y);
            corners[3] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MaxEdge.Z, eqBbox.MinEdge.Y);
            corners[4] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MinEdge.Z, eqBbox.MaxEdge.Y);
            corners[5] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MinEdge.Z, eqBbox.MaxEdge.Y);
            corners[6] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MaxEdge.Z, eqBbox.MaxEdge.Y);
            corners[7] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MaxEdge.Z, eqBbox.MaxEdge.Y);

            // Bottom face
            driver_->draw3DLine(corners[0], corners[1], color);
            driver_->draw3DLine(corners[1], corners[2], color);
            driver_->draw3DLine(corners[2], corners[3], color);
            driver_->draw3DLine(corners[3], corners[0], color);
            // Top face
            driver_->draw3DLine(corners[4], corners[5], color);
            driver_->draw3DLine(corners[5], corners[6], color);
            driver_->draw3DLine(corners[6], corners[7], color);
            driver_->draw3DLine(corners[7], corners[4], color);
            // Vertical edges
            driver_->draw3DLine(corners[0], corners[4], color);
            driver_->draw3DLine(corners[1], corners[5], color);
            driver_->draw3DLine(corners[2], corners[6], color);
            driver_->draw3DLine(corners[3], corners[7], color);
        }
    }

    // Map overlay
    if (showMapOverlay_) {
        updateMapOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        drawMapOverlay();
    }

    // Navmesh overlay
    if (showNavmeshOverlay_) {
        updateNavmeshOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        drawNavmeshOverlay();
    }

    // Portal wireframe debug overlay — batched into a single GL_LINES draw call.
    // Draws all portals within distance of camera (not limited to current region).
    if (portalDebugDraw_ && portalSystem_ && portalSystem_->hasPortals() && have3DTransforms_) {
#ifdef EQT_HAS_GLES2
        const auto& portalData = portalSystem_->getData();

        // Camera position in EQ coords
        float camEqX = playerX_;
        float camEqY = playerZ_;  // Irrlicht Z -> EQ Y
        float camEqZ = playerY_;  // Irrlicht Y -> EQ Z
        const float maxDistSq = 500.0f * 500.0f;

        // Batch all lines: 2 vertices per line, 7 floats per vertex (xyz + rgba)
        std::vector<float> lineBuf;
        lineBuf.reserve(4096);
        size_t lineCount = 0;

        auto pushLine = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                            float r, float g, float b) {
            // EQ (x,y,z) -> Irrlicht (x,z,y)
            lineBuf.push_back(x1); lineBuf.push_back(z1); lineBuf.push_back(y1);
            lineBuf.push_back(r); lineBuf.push_back(g); lineBuf.push_back(b); lineBuf.push_back(1.0f);
            lineBuf.push_back(x2); lineBuf.push_back(z2); lineBuf.push_back(y2);
            lineBuf.push_back(r); lineBuf.push_back(g); lineBuf.push_back(b); lineBuf.push_back(1.0f);
            lineCount++;
        };

        for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
            const auto& portal = portalData.portals[pi];
            size_t n = portal.vertexCount();
            if (n < 3) continue;

            // Distance cull by portal center
            float dx = portal.centerX - camEqX;
            float dy = portal.centerY - camEqY;
            float dz = portal.centerZ - camEqZ;
            if (dx * dx + dy * dy + dz * dz > maxDistSq) continue;

            bool isAdjacentToCamera = (portal.regionA == currentPvsRegion_ ||
                                       portal.regionB == currentPvsRegion_);
            float r, g, b;
            if (isAdjacentToCamera) { r = 0.0f; g = 1.0f; b = 1.0f; }  // Cyan
            else { r = 1.0f; g = 0.647f; b = 0.0f; }                    // Orange

            for (size_t vi = 0; vi < n; ++vi) {
                size_t vj = (vi + 1) % n;
                pushLine(portal.vx(vi), portal.vy(vi), portal.vz(vi),
                         portal.vx(vj), portal.vy(vj), portal.vz(vj), r, g, b);
            }
        }

        if (lineCount > 0) {
            // Restore 3D camera transforms (2D draws during drawAll() overwrite them)
            driver_->setTransform(irr::video::ETS_VIEW, captured3DView_);
            driver_->setTransform(irr::video::ETS_PROJECTION, captured3DProj_);
            driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

            gles2Draw3DLinesBatch(driver_, lineBuf.data(), static_cast<unsigned int>(lineCount));
        }
#endif
    }

    // Stencil buffer debug overlay (show stencil levels as colored fullscreen rects)
    // Only available on GLES2 with portal occlusion active
#ifdef EQT_HAS_GLES2
    if (stencilDebugDraw_ && portalOcclusionEnabled_) {
        irr::video::SColor levelColors[4] = {
            irr::video::SColor(80, 0, 255, 0),     // Level 1: green
            irr::video::SColor(80, 0, 0, 255),     // Level 2: blue
            irr::video::SColor(80, 255, 255, 0),   // Level 3: yellow
            irr::video::SColor(80, 255, 0, 255)    // Level 4: magenta
        };

        for (int level = 1; level <= 4; ++level) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, level, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

            irr::core::rect<irr::s32> fullScreen(0, 0,
                static_cast<irr::s32>(driver_->getScreenSize().Width),
                static_cast<irr::s32>(driver_->getScreenSize().Height));
            driver_->draw2DRectangle(levelColors[level - 1], fullScreen);
        }
        glDisable(GL_STENCIL_TEST);
    }
#endif

    frameTimings_.debugOverlays = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 8: casting bars (entityRenderer={})", entityRenderer_ != nullptr);
    // Draw entity casting bars
    if (!allUIHidden_ && entityRenderer_) entityRenderer_->renderEntityCastingBars(driver_, guienv_, camera_);
    frameTimings_.castingBars = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 9: GUI drawAll");
    if (!allUIHidden_ && !newUIEnabled_) {
        guienv_->drawAll();
    }
    if (!allUIHidden_) {
        drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();

    // U06j: Process new UI input before rendering
    if (newUIEnabled_ && staticUIInput_ && eventReceiver_) {
        staticUIInput_->processInput(
            eventReceiver_->getMouseX(), eventReceiver_->getMouseY(), pendingUIClick_,
            uiLayout_, hotbarState_, spellGemState_, inventoryState_,
            spellbookState_, skillsPopupState_);
        pendingUIClick_ = false;
    }

    // U03c: Render new static UI when enabled
    if (newUIEnabled_ && uiRenderer_) {
        uiRenderer_->beginFrame();
        renderPlayerStatus(*uiRenderer_, uiLayout_, cachedPlayerStats_);
        renderTargetInfo(*uiRenderer_, uiLayout_, cachedTargetInfo_);
        renderChatPanel(*uiRenderer_, uiLayout_, chatPanelState_);
        renderHotbar(*uiRenderer_, uiLayout_, hotbarState_);
        renderSpellGemPanel(*uiRenderer_, uiLayout_, spellGemState_);
        renderBuffBar(*uiRenderer_, uiLayout_, buffBarState_);
        renderCastingBar(*uiRenderer_, uiLayout_, castingBarState_);
        renderGroupPanel(*uiRenderer_, uiLayout_, groupPanelState_);
        renderPetPanel(*uiRenderer_, uiLayout_, petPanelState_);
        renderInventoryPopup(*uiRenderer_, uiLayout_, inventoryState_);
        renderSpellbookPopup(*uiRenderer_, uiLayout_, spellbookState_);
        renderSkillsPopup(*uiRenderer_, uiLayout_, skillsPopupState_);
        renderXPBar(*uiRenderer_, uiLayout_, xpBarState_);
        uiRenderer_->endFrame();
    }
    frameTimings_.staticUI = measureSection();

    // Draw zone line overlay
    drawZoneLineOverlay();
    drawZoneLineBoxLabels();
    frameTimings_.zoneLineOverlay = measureSection();

#ifdef WITH_RDP
    captureFrameForRDP();
#endif

    // Draw software mouse cursor for DRM/KMS mode (no hardware cursor available)
    if (softwareCursorTexture_) {
        auto* cursor = device_->getCursorControl();
        if (cursor && cursor->isVisible()) {
            auto pos = cursor->getPosition();
            irr::core::position2di destPos(pos.X, pos.Y);
            irr::core::rect<irr::s32> srcRect(0, 0, 8, 10);
            driver_->draw2DImage(softwareCursorTexture_, destPos, srcRect,
                nullptr, irr::video::SColor(255, 255, 255, 255), true);
        }
    }

    // Take deferred screenshot after all rendering, before swap
    if (pendingScreenshot_) {
        pendingScreenshot_ = false;
        saveScreenshot("screenshot.png");
    }
    frameTimings_.postRender = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 11: endScene");
    driver_->endScene();
    frameTimings_.endScene = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 12: frame complete");
    FlushThreadLog();
    return true;
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

#ifdef EQT_HAS_DRM
    // DRM/EGL: Irrlicht's createScreenShot() reads GL_FRONT which doesn't exist
    // in EGL (only GLX has separate front/back read targets).  Read GL_BACK instead.
    if (config_.constrainedConfig.useDRM) {
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



} // namespace Graphics
} // namespace EQT
