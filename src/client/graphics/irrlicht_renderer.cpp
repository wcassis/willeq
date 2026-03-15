#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
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
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/chat_message_buffer.h"
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
extern bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer);
extern void gles2DeleteStaticHWBuffer(void* driver, const void* meshBuffer);
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

namespace EQT {
namespace Graphics {

// Read display settings directly from config/display_settings.json
// Used during early initialization before WindowManager/OptionsWindow exist
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
    if (windowManager_ && windowManager_->getOptionsWindow()) {
        return windowManager_->getOptionsWindow()->getDisplaySettings();
    }
    return cachedDisplaySettings_;  // loaded once at startup
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
        float targetFps = config_.constrainedConfig.targetFps;
        governor_ = std::make_unique<FrameBudgetGovernor>(targetFps);
        LOG_INFO(MOD_GRAPHICS, "Frame budget governor: target {:.0f} FPS ({:.1f}ms budget)",
                 targetFps, governor_->getTargetFrameTimeMs());
    }

    // Create shared background thread pool
    if (!backgroundThreadPool_) {
        backgroundThreadPool_ = std::make_unique<BackgroundThreadPool>(
            config_.constrainedConfig.backgroundThreadCount);
        LOG_INFO(MOD_GRAPHICS, "Background thread pool: {} thread(s)",
                 config_.constrainedConfig.backgroundThreadCount);
    }

    // One-time file I/O at app startup (before render loop begins).
    // Caches display settings so render-thread code never reads JSON.
    if (!displaySettingsCached_) {
        cachedDisplaySettings_ = loadDisplaySettingsFromFile();
        displaySettingsCached_ = true;
    }

    // One-time race mappings load (validated at startup by S03)
    if (!areRaceMappingsLoaded()) {
        if (!loadRaceMappings("config/race_models.json")) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load race model mappings: config/race_models.json");
        }
    }

    // Pre-load item-to-model mapping (validated at startup by S03)
    if (itemToModelMap_.empty()) {
        if (EquipmentModelLoader::loadItemModelMappingStatic("data/item_models.json", itemToModelMap_) < 0) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load item model mappings: data/item_models.json");
        }
    }

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

    driver_ = device_->getVideoDriver();
    smgr_ = device_->getSceneManager();
    guienv_ = device_->getGUIEnvironment();

    // Log comprehensive driver/OpenGL details
    logDriverDetails(driver_, device_, params);

    // In DRM mode, enable and create software cursor (no hardware cursor available)
    if (config_.constrainedConfig.useDRM && device_->getCursorControl()) {
        device_->getCursorControl()->setVisible(true);
    }
    createSoftwareCursor();

    // U01: Initialize batched text renderer
    textBatch_ = std::make_unique<TextBatch>();
    if (textBatch_->init(guienv_)) {
        LOG_INFO(MOD_GRAPHICS, "TextBatch initialized");
    } else {
        LOG_WARN(MOD_GRAPHICS, "TextBatch initialization failed — falling back to font->draw()");
        textBatch_.reset();
    }

    // U02: Load UI atlas (required when enableTextureAtlas is true)
    if (config_.constrainedConfig.enableTextureAtlas && !config_.constrainedConfig.atlasPath.empty()) {
        uiAtlas_ = std::make_unique<UIAtlas>();
        if (!uiAtlas_->load(driver_, config_.constrainedConfig.atlasPath)) {
            LOG_FATAL(MOD_GRAPHICS, "UIAtlas: required atlas files missing from {}. "
                "Run ui_atlas_builder --output {} to generate them.",
                config_.constrainedConfig.atlasPath, config_.constrainedConfig.atlasPath);
            return false;
        }
    }

    // U03: Initialize static layout UI renderer
    uiRenderer_ = std::make_unique<UIRenderer>();
    uiRenderer_->init(driver_, uiAtlas_.get(), textBatch_.get());
    uiLayout_.computeLayout(config.width, config.height);
    // U04: Create chat message buffer for new UI
    newUIChatBuffer_ = std::make_unique<eqt::ui::ChatMessageBuffer>(500);
    chatPanelState_.messageBuffer = newUIChatBuffer_.get();
    LOG_INFO(MOD_GRAPHICS, "UIRenderer initialized (newui=off, toggle with /newui)");

    // Configure mipmap generation based on constrained config
    if (driver_) {
        driver_->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS,
                                         config_.constrainedConfig.enableMipmaps);
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Mipmap generation: {}",
                  config_.constrainedConfig.enableMipmaps ? "enabled" : "disabled");
    }

    // Create constrained texture cache (always active; Max preset has generous limits)
    if (driver_) {
        constrainedTextureCache_ = std::make_unique<ConstrainedTextureCache>(
            config_.constrainedConfig, driver_);
        constrainedTextureCache_->setSceneManager(smgr_);  // Enable safe eviction
        if (backgroundThreadPool_)
            constrainedTextureCache_->setBackgroundThreadPool(backgroundThreadPool_.get());
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_)
            constrainedTextureCache_->setGPUUploadThread(gpuUploadThread_.get());
#endif
        // Pre-create placeholder texture before render loop starts
        constrainedTextureCache_->getPlaceholderTexture();
        LOG_INFO(MOD_GRAPHICS, "Texture cache created ({}KB limit, {}x{} max texture, mipmaps={})",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.enableMipmaps ? "yes" : "no");
    }

    // Create event receiver
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

    // Setup camera (needed for loading screen rendering)
    setupCamera();

    // Setup lighting (basic setup)
    setupLighting();

    // Apply initial settings from constrained config (must come BEFORE shader variant sync)
    wireframeMode_ = config_.constrainedConfig.wireframe;
    fogEnabled_ = config_.constrainedConfig.fog;
    debugPlayerLightEnabled_ = config_.constrainedConfig.playerLight;
    lightingEnabled_ = config.lighting;

    // Initialize GLSL shader pipeline (if enabled and driver supports it)
    if (config_.constrainedConfig.enableShaders &&
        config_.constrainedConfig.renderingBackend != RenderingBackend::Software) {
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
    initGPUUploadThread();
#endif

    // Setup HUD (needed for loading screen text)
    setupHUD();

    // S05: Pre-allocate renderer-lifetime subsystems (survive zone changes)

    // Create entity renderer (needed for entity model loading during zone load)
    createEntityRenderer();

    // Create sky renderer (skyRendering preset flag controls whether it renders, not whether it exists)
    skyRenderer_ = std::make_unique<SkyRenderer>(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_) {
        skyRenderer_->setConstrainedTextureCache(constrainedTextureCache_.get());
    }
    if (!skyRenderer_->initialize(config_.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization failed - sky will not be rendered");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized");
    }

    // Create detail manager (detailObjectsEnabled controls whether it renders, not whether it exists)
    detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
    detailManager_->setSurfaceMapsPath("data/detail/zones");
    if (constrainedTextureCache_) {
        detailManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
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
        treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
        treeManager_->setRenderDistance(renderDistance_);
        if (constrainedTextureCache_) {
            treeManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
        }
        if (zoneShader_ && zoneShader_->isAvailable()) {
            treeManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest());
        }
    }

    // Create weather system (needed before zone loading)
    if (!weatherSystem_) {
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
        particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
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
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
        }
        LOG_INFO(MOD_GRAPHICS, "Boids manager initialized (ambient creatures enabled in settings)");
    } else if (!boidsManager_) {
        LOG_INFO(MOD_GRAPHICS, "Boids manager skipped (ambient creatures disabled in settings)");
    }

    // Create tumbleweed manager (only if rolling objects enabled)
    if (!tumbleweedManager_ && displaySettings.rollingObjects) {
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
        }
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager initialized (rolling objects enabled in settings)");
    } else if (!tumbleweedManager_) {
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager skipped (rolling objects disabled in settings)");
    }

    // Create weather effects controller (screen-space rain/snow overlays, storm clouds, lightning)
    if (!weatherEffects_) {
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller");
        }
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_.get());
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
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
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

    // Initialize inventory window model view now that entity renderer is available
    // This must happen after entityRenderer_ is created since it needs the race model loader
    if (windowManager_ && entityRenderer_) {
        windowManager_->initModelView(smgr_,
                                      entityRenderer_->getRaceModelLoader(),
                                      entityRenderer_->getEquipmentModelLoader());
    }

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
        for (const auto& line : report) chat(line);
    }
    // --- UI ---
    else if (cmd == "/newui") {
        newUIEnabled_ = !newUIEnabled_;
        chat(fmt::format("New static UI: {}", newUIEnabled_ ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/timestamp" || cmd == "/timestamps") {
        if (auto* wm = getWindowManager()) {
            if (auto* cw = wm->getChatWindow()) {
                cw->toggleTimestamps();
                cw->saveSettings();
                chat(fmt::format("Timestamps {}", cw->getShowTimestamps() ? "enabled" : "disabled"));
            }
        }
    } else if (cmd == "/filter") {
        if (auto* wm = getWindowManager()) {
            if (auto* cw = wm->getChatWindow()) {
                if (args.empty()) {
                    chat("=== Chat Filter Status ===");
                    chat(fmt::format("Say: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Say) ? "ON" : "OFF"));
                    chat(fmt::format("Tell: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Tell) ? "ON" : "OFF"));
                    chat(fmt::format("Group: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Group) ? "ON" : "OFF"));
                    chat(fmt::format("Guild: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Guild) ? "ON" : "OFF"));
                    chat(fmt::format("Shout: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Shout) ? "ON" : "OFF"));
                    chat(fmt::format("Auction: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Auction) ? "ON" : "OFF"));
                    chat(fmt::format("OOC: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::OOC) ? "ON" : "OFF"));
                    chat(fmt::format("Emote: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Emote) ? "ON" : "OFF"));
                    chat(fmt::format("Combat: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Combat) ? "ON" : "OFF"));
                    chat(fmt::format("Miss: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::CombatMiss) ? "ON" : "OFF"));
                    chat(fmt::format("Exp: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Experience) ? "ON" : "OFF"));
                    chat(fmt::format("Loot: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::Loot) ? "ON" : "OFF"));
                    chat(fmt::format("NPC: {}", cw->isChannelEnabled(eqt::ui::ChatChannel::NPCDialogue) ? "ON" : "OFF"));
                    chat("Type /filter <channel> to toggle.");
                } else {
                    std::string channel = args;
                    std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);
                    eqt::ui::ChatChannel ch;
                    bool found = true;
                    if (channel == "say" || channel == "s") ch = eqt::ui::ChatChannel::Say;
                    else if (channel == "tell" || channel == "t") ch = eqt::ui::ChatChannel::Tell;
                    else if (channel == "group" || channel == "g") ch = eqt::ui::ChatChannel::Group;
                    else if (channel == "guild" || channel == "gu") ch = eqt::ui::ChatChannel::Guild;
                    else if (channel == "shout" || channel == "sho") ch = eqt::ui::ChatChannel::Shout;
                    else if (channel == "auction" || channel == "auc") ch = eqt::ui::ChatChannel::Auction;
                    else if (channel == "ooc" || channel == "o") ch = eqt::ui::ChatChannel::OOC;
                    else if (channel == "emote" || channel == "em") ch = eqt::ui::ChatChannel::Emote;
                    else if (channel == "combat") ch = eqt::ui::ChatChannel::Combat;
                    else if (channel == "miss" || channel == "misses") ch = eqt::ui::ChatChannel::CombatMiss;
                    else if (channel == "exp" || channel == "experience") ch = eqt::ui::ChatChannel::Experience;
                    else if (channel == "loot") ch = eqt::ui::ChatChannel::Loot;
                    else if (channel == "npc") ch = eqt::ui::ChatChannel::NPCDialogue;
                    else if (channel == "all") {
                        cw->enableAllChannels(); cw->saveSettings();
                        chat("All channels enabled"); found = false;
                    } else if (channel == "none") {
                        cw->disableAllChannels(); cw->saveSettings();
                        chat("All channels disabled (except system)"); found = false;
                    } else {
                        chat(fmt::format("Unknown channel: {}. Use /filter for list.", args));
                        found = false;
                    }
                    if (found) {
                        cw->toggleChannel(ch); cw->saveSettings();
                        const char* chName = eqt::ui::getChannelName(ch);
                        chat(fmt::format("{} filter: {}", chName, cw->isChannelEnabled(ch) ? "ON" : "OFF"));
                    }
                }
            }
        }
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

    // Stop background icon sheet worker before any other cleanup
    if (windowManager_) {
        windowManager_->getIconLoader().stopWorker();
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
    windowManager_.reset();
    eventReceiver_.reset();

    // Weather and environment managers also need the device for cleanup
    weatherEffects_.reset();
    particleManager_.reset();
    boidsManager_.reset();
    tumbleweedManager_.reset();
    weatherSystem_.reset();
    treeManager_.reset();
    detailManager_.reset();
    constrainedTextureCache_.reset();
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

#ifdef EQT_HAS_GLES2
void IrrlichtRenderer::initGPUUploadThread() {
    if (!device_) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no device, skipping");
        return;
    }
    if (!driver_) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no driver, skipping");
        return;
    }
    if (driver_->getDriverType() != irr::video::EDT_OGLES2) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: driver type {} != EDT_OGLES2 ({}), skipping",
                 static_cast<int>(driver_->getDriverType()), static_cast<int>(irr::video::EDT_OGLES2));
        return;
    }

#ifdef EQT_HAS_DRM
    // Get EGL handles from current context (avoids dependency on internal Irrlicht headers)
    EGLDisplay eglDisplay = eglGetCurrentDisplay();
    EGLContext eglContext = eglGetCurrentContext();
    if (eglDisplay == EGL_NO_DISPLAY || eglContext == EGL_NO_CONTEXT) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no current EGL context, skipping");
        return;
    }

    // Retrieve the EGLConfig associated with the current context
    EGLint configId = 0;
    eglQueryContext(eglDisplay, eglContext, EGL_CONFIG_ID, &configId);
    EGLConfig eglConfig = nullptr;
    EGLint numConfigs = 0;
    EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs);
    if (numConfigs == 0 || !eglConfig) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: failed to resolve EGLConfig, skipping");
        return;
    }

    gpuUploadThread_ = std::make_unique<GPUUploadThread>();
    if (gpuUploadThread_->init(eglDisplay, eglContext, eglConfig)) {
        gpuUploadThread_->start();
        // Wire to subsystems already created
        if (constrainedTextureCache_)
            constrainedTextureCache_->setGPUUploadThread(gpuUploadThread_.get());
    } else {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: init failed, falling back to render-thread uploads");
        gpuUploadThread_.reset();
    }
#else
    LOG_DEBUG(MOD_GRAPHICS, "GPU upload thread: DRM not available, skipping");
#endif
}

void IrrlichtRenderer::processCompletedUploads() {
    if (!gpuUploadThread_ || !gpuUploadThread_->isAvailable())
        return;

    auto results = gpuUploadThread_->pollResults();
    if (results.empty())
        return;

    // Resolve EGL fence wait function pointer (cached, one-time)
    static auto eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
        eglGetProcAddress("eglClientWaitSyncKHR"));
    static auto eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
        eglGetProcAddress("eglDestroySyncKHR"));

    EGLDisplay display = gpuUploadThread_->getEGLDisplay();

    for (auto& result : results) {
        // Wait on GPU fence (should be near-instant if upload already finished)
        if (result.fence != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR && eglDestroySyncKHR) {
            EGLint waitResult = eglClientWaitSyncKHR(display, result.fence,
                                                      EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                                      EGL_FOREVER_KHR);
            if (waitResult == EGL_FALSE) {
                LOG_WARN(MOD_GRAPHICS, "GPUUpload: fence wait failed for request {}", result.requestId);
            }
            eglDestroySyncKHR(display, result.fence);
        }

        switch (result.type) {
            case UploadRequestType::Texture:
            case UploadRequestType::CompressedTexture: {
                // Wrap the externally-uploaded GL texture as an Irrlicht ITexture
                irr::video::ITexture* wrappedTex = nullptr;
                if (result.glTextureName != 0 && !result.textureName.empty()) {
                    wrappedTex = static_cast<irr::video::ITexture*>(
                        gles2WrapTexture(driver_, result.textureName.c_str(),
                                         result.glTextureName,
                                         result.width, result.height,
                                         result.gpuBytes));
                    LOG_DEBUG(MOD_GRAPHICS, "GPUUpload: texture '{}' ready ({}x{}, {} bytes)",
                              result.textureName, result.width, result.height, result.gpuBytes);
                }

                // Route based on high byte of callbackKey
                uint8_t sourceType = static_cast<uint8_t>(result.callbackKey >> 56);

                if (sourceType == 3) {
                    // Unified constrained cache texture (zone + entity textures)
                    // Register in LRU cache and mesh builder for entity scene node creation
                    if (wrappedTex && constrainedTextureCache_) {
                        size_t texSize = static_cast<size_t>(result.width) * result.height * 4;
                        constrainedTextureCache_->registerTexture(result.textureName, wrappedTex, texSize, false);
                        constrainedTextureCache_->clearPendingAsync(result.textureName);

                        // Also register in mesh builder so entity SceneNodeCreation finds it
                        if (entityRenderer_) {
                            auto* meshBuilder = entityRenderer_->getMeshBuilder();
                            if (meshBuilder) {
                                meshBuilder->registerUploadedTexture(result.textureName, wrappedTex, false);
                            }
                        }

                        // Check if any regions were waiting for this texture
                        auto pendIt = pendingTextureRegions_.find(result.textureName);
                        if (pendIt != pendingTextureRegions_.end()) {
                            for (size_t regionIdx : pendIt->second) {
                                // Deduplicate: only add if not already queued
                                bool alreadyQueued = false;
                                for (const auto& entry : textureRebuildQueue_) {
                                    if (entry.regionIdx == regionIdx) {
                                        alreadyQueued = true;
                                        break;
                                    }
                                }
                                if (!alreadyQueued) {
                                    float distSq = 0.0f;
                                    auto bbIt = regionBoundingBoxes_.find(regionIdx);
                                    if (bbIt != regionBoundingBoxes_.end()) {
                                        auto center = bbIt->second.getCenter();
                                        float dx = center.X - playerX_;
                                        float dy = center.Y - playerY_;
                                        distSq = dx * dx + dy * dy;
                                    }
                                    // Player's region gets priority (distSq = -1)
                                    if (regionIdx == currentPvsRegion_) distSq = -1.0f;
                                    textureRebuildQueue_.push_back({regionIdx, distSq});
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} regions for rebuild",
                                     result.textureName, pendIt->second.size());
                            pendingTextureRegions_.erase(pendIt);
                        }

                        // Doors waiting for this texture
                        auto doorPendIt = pendingTextureDoors_.find(result.textureName);
                        if (doorPendIt != pendingTextureDoors_.end()) {
                            for (uint8_t doorId : doorPendIt->second) {
                                if (doorManager_) {
                                    const auto* dv = doorManager_->getDoor(doorId);
                                    if (dv) doorManager_->invalidateMeshCache(dv->modelName);
                                }
                                if (std::find(doorTextureRebuildQueue_.begin(),
                                              doorTextureRebuildQueue_.end(), doorId) == doorTextureRebuildQueue_.end()) {
                                    doorTextureRebuildQueue_.push_back(doorId);
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} doors for rebuild",
                                     result.textureName, doorPendIt->second.size());
                            pendingTextureDoors_.erase(doorPendIt);
                        }

                        // PVS objects waiting for this texture
                        auto objPendIt = pendingTextureObjects_.find(result.textureName);
                        if (objPendIt != pendingTextureObjects_.end()) {
                            for (size_t objIdx : objPendIt->second) {
                                if (std::find(objectTextureRebuildQueue_.begin(),
                                              objectTextureRebuildQueue_.end(), objIdx) == objectTextureRebuildQueue_.end()) {
                                    objectTextureRebuildQueue_.push_back(objIdx);
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} objects for rebuild",
                                     result.textureName, objPendIt->second.size());
                            pendingTextureObjects_.erase(objPendIt);
                        }
                    }
                } else if (sourceType == 4) {
                    // Icon texture — register in icon loader cache
                    uint32_t iconId = static_cast<uint32_t>(result.callbackKey & 0xFFFFFFFF);
                    if (wrappedTex && windowManager_) {
                        auto& iconLoader = windowManager_->getIconLoader();
                        iconLoader.registerAsyncIcon(iconId, wrappedTex);
                        iconLoader.clearPendingAsyncIcon(iconId);
                        // Also register in constrained cache for LRU tracking
                        if (constrainedTextureCache_) {
                            std::string texName = "icon_" + std::to_string(iconId);
                            size_t iconBytes = 40 * 40 * 4;  // 40x40 ARGB
                            constrainedTextureCache_->registerTexture(texName, wrappedTex, iconBytes, true);
                        }
                    }
                } else {
                    // Atlas texture (sourceType 0 or 1, legacy encoding)
                    // callbackKey encoding: high 32 bits = atlas type (0=zone, 1=obj), low 32 bits = page index
                    uint32_t atlasType = static_cast<uint32_t>(result.callbackKey >> 32);
                    uint32_t pageIndex = static_cast<uint32_t>(result.callbackKey & 0xFFFFFFFF);
                    TextureAtlas* atlas = (atlasType == 0) ? zoneAtlas_.get() : objAtlas_.get();
                    if (atlas && result.glTextureName != 0) {
                        atlas->setPageTexture(static_cast<int>(pageIndex),
                                               result.glTextureName, result.gpuBytes);
                    }
                }
                break;
            }

            case UploadRequestType::VertexBuffer: {
                // Decode callbackKey: high 16 bits = buffer index, low 48 bits = region index
                uint32_t bufferIdx = static_cast<uint32_t>(result.callbackKey >> 48);
                size_t regionIdx = static_cast<size_t>(result.callbackKey & 0xFFFFFFFFFFFFULL);
                auto it = regionMeshNodes_.find(regionIdx);
                if (it != regionMeshNodes_.end() && it->second) {
                    irr::scene::IMesh* mesh = it->second->getMesh();
                    if (mesh && bufferIdx < mesh->getMeshBufferCount()) {
                        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bufferIdx);
                        if (buf) {
                            gles2RegisterExternalHWBuffer(
                                driver_, buf,
                                result.vbo, result.ebo,
                                result.vertexCount, result.indexCount,
                                static_cast<int>(buf->getVertexType()));
                            LOG_DEBUG(MOD_GRAPHICS, "GPUUpload: VBO for region {} buf {} ready (verts={}, indices={})",
                                      regionIdx, bufferIdx, result.vertexCount, result.indexCount);
                        }
                    }
                }
                pendingVBOUploads_.erase(regionIdx);
                break;
            }
        }
    }

    // Sort texture rebuild queue: player's region first (distSq=-1), then nearest
    if (!textureRebuildQueue_.empty()) {
        std::sort(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                  [](const TextureRebuildEntry& a, const TextureRebuildEntry& b) {
                      return a.distanceSq < b.distanceSq;
                  });
    }
}

void IrrlichtRenderer::drainGPUResults() {
    if (!gpuUploadThread_ || !gpuUploadThread_->isAvailable())
        return;

    auto results = gpuUploadThread_->pollResults();
    for (auto& r : results)
        pendingGPUResults_.push_back(std::move(r));
}

bool IrrlichtRenderer::processOneGPUResult() {
    if (pendingGPUResults_.empty()) return false;

    auto result = std::move(pendingGPUResults_.front());
    pendingGPUResults_.erase(pendingGPUResults_.begin());

    // Resolve EGL fence wait function pointer (cached, one-time)
    static auto eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
        eglGetProcAddress("eglClientWaitSyncKHR"));
    static auto eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
        eglGetProcAddress("eglDestroySyncKHR"));

    EGLDisplay display = gpuUploadThread_->getEGLDisplay();

    // Wait on GPU fence (should be near-instant if upload already finished)
    if (result.fence != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR && eglDestroySyncKHR) {
        EGLint waitResult = eglClientWaitSyncKHR(display, result.fence,
                                                  EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                                  EGL_FOREVER_KHR);
        if (waitResult == EGL_FALSE) {
            LOG_WARN(MOD_GRAPHICS, "GPUUpload processOne: fence wait failed for request {}", result.requestId);
        }
        eglDestroySyncKHR(display, result.fence);
    }

    switch (result.type) {
        case UploadRequestType::Texture:
        case UploadRequestType::CompressedTexture: {
            // Wrap the externally-uploaded GL texture as an Irrlicht ITexture
            irr::video::ITexture* wrappedTex = nullptr;
            if (result.glTextureName != 0 && !result.textureName.empty()) {
                wrappedTex = static_cast<irr::video::ITexture*>(
                    gles2WrapTexture(driver_, result.textureName.c_str(),
                                     result.glTextureName,
                                     result.width, result.height,
                                     result.gpuBytes));
                LOG_DEBUG(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' ready ({}x{}, {} bytes)",
                          result.textureName, result.width, result.height, result.gpuBytes);
            }

            // Route based on high byte of callbackKey
            uint8_t sourceType = static_cast<uint8_t>(result.callbackKey >> 56);

            if (sourceType == 3) {
                // Unified constrained cache texture (zone + entity textures)
                if (wrappedTex && constrainedTextureCache_) {
                    size_t texSize = static_cast<size_t>(result.width) * result.height * 4;
                    constrainedTextureCache_->registerTexture(result.textureName, wrappedTex, texSize, false);
                    constrainedTextureCache_->clearPendingAsync(result.textureName);

                    // Also register in mesh builder so entity SceneNodeCreation finds it
                    if (entityRenderer_) {
                        auto* meshBuilder = entityRenderer_->getMeshBuilder();
                        if (meshBuilder) {
                            meshBuilder->registerUploadedTexture(result.textureName, wrappedTex, false);
                        }
                    }

                    // Check if any regions were waiting for this texture → material swap queue
                    auto pendIt = pendingTextureRegions_.find(result.textureName);
                    if (pendIt != pendingTextureRegions_.end()) {
                        // Check if we have pendingTextureBuffers_ entries for material swap
                        bool usedMaterialSwap = false;
                        for (size_t regionIdx : pendIt->second) {
                            auto ptbIt = pendingTextureBuffers_.find(regionIdx);
                            if (ptbIt != pendingTextureBuffers_.end()) {
                                auto bufIt = ptbIt->second.find(result.textureName);
                                if (bufIt != ptbIt->second.end() && !bufIt->second.empty()) {
                                    bool texHasAlpha = constrainedTextureCache_->hasAlpha(result.textureName);
                                    materialSwapQueue_.push_back({regionIdx, result.textureName, wrappedTex, texHasAlpha});
                                    usedMaterialSwap = true;
                                    continue;
                                }
                            }
                            // No buffer map → fall back to full rebuild (textureRebuildQueue_)
                            bool alreadyQueued = false;
                            for (const auto& entry : textureRebuildQueue_) {
                                if (entry.regionIdx == regionIdx) {
                                    alreadyQueued = true;
                                    break;
                                }
                            }
                            if (!alreadyQueued) {
                                float distSq = 0.0f;
                                auto bbIt = regionBoundingBoxes_.find(regionIdx);
                                if (bbIt != regionBoundingBoxes_.end()) {
                                    auto center = bbIt->second.getCenter();
                                    float dx = center.X - playerX_;
                                    float dy = center.Y - playerY_;
                                    distSq = dx * dx + dy * dy;
                                }
                                if (regionIdx == currentPvsRegion_) distSq = -1.0f;
                                textureRebuildQueue_.push_back({regionIdx, distSq});
                            }
                        }
                        if (usedMaterialSwap) {
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued material swaps for regions",
                                     result.textureName);
                        } else {
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} regions for rebuild",
                                     result.textureName, pendIt->second.size());
                        }
                        pendingTextureRegions_.erase(pendIt);
                    }

                    // Doors waiting for this texture
                    auto doorPendIt = pendingTextureDoors_.find(result.textureName);
                    if (doorPendIt != pendingTextureDoors_.end()) {
                        for (uint8_t doorId : doorPendIt->second) {
                            if (doorManager_) {
                                const auto* dv = doorManager_->getDoor(doorId);
                                if (dv) doorManager_->invalidateMeshCache(dv->modelName);
                            }
                            if (std::find(doorTextureRebuildQueue_.begin(),
                                          doorTextureRebuildQueue_.end(), doorId) == doorTextureRebuildQueue_.end()) {
                                doorTextureRebuildQueue_.push_back(doorId);
                            }
                        }
                        LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} doors for rebuild",
                                 result.textureName, doorPendIt->second.size());
                        pendingTextureDoors_.erase(doorPendIt);
                    }

                    // PVS objects waiting for this texture
                    auto objPendIt = pendingTextureObjects_.find(result.textureName);
                    if (objPendIt != pendingTextureObjects_.end()) {
                        for (size_t objIdx : objPendIt->second) {
                            if (std::find(objectTextureRebuildQueue_.begin(),
                                          objectTextureRebuildQueue_.end(), objIdx) == objectTextureRebuildQueue_.end()) {
                                objectTextureRebuildQueue_.push_back(objIdx);
                            }
                        }
                        LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} objects for rebuild",
                                 result.textureName, objPendIt->second.size());
                        pendingTextureObjects_.erase(objPendIt);
                    }
                }
            } else if (sourceType == 4) {
                // Icon texture — register in icon loader cache
                uint32_t iconId = static_cast<uint32_t>(result.callbackKey & 0xFFFFFFFF);
                if (wrappedTex && windowManager_) {
                    auto& iconLoader = windowManager_->getIconLoader();
                    iconLoader.registerAsyncIcon(iconId, wrappedTex);
                    iconLoader.clearPendingAsyncIcon(iconId);
                    if (constrainedTextureCache_) {
                        std::string texName = "icon_" + std::to_string(iconId);
                        size_t iconBytes = 40 * 40 * 4;
                        constrainedTextureCache_->registerTexture(texName, wrappedTex, iconBytes, true);
                    }
                }
            } else {
                // Atlas texture (sourceType 0 or 1, legacy encoding)
                uint32_t atlasType = static_cast<uint32_t>(result.callbackKey >> 32);
                uint32_t pageIndex = static_cast<uint32_t>(result.callbackKey & 0xFFFFFFFF);
                TextureAtlas* atlas = (atlasType == 0) ? zoneAtlas_.get() : objAtlas_.get();
                if (atlas && result.glTextureName != 0) {
                    atlas->setPageTexture(static_cast<int>(pageIndex),
                                           result.glTextureName, result.gpuBytes);
                }
            }
            break;
        }

        case UploadRequestType::VertexBuffer: {
            uint32_t bufferIdx = static_cast<uint32_t>(result.callbackKey >> 48);
            size_t regionIdx = static_cast<size_t>(result.callbackKey & 0xFFFFFFFFFFFFULL);
            auto it = regionMeshNodes_.find(regionIdx);
            if (it != regionMeshNodes_.end() && it->second) {
                irr::scene::IMesh* mesh = it->second->getMesh();
                if (mesh && bufferIdx < mesh->getMeshBufferCount()) {
                    irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bufferIdx);
                    if (buf) {
                        gles2RegisterExternalHWBuffer(
                            driver_, buf,
                            result.vbo, result.ebo,
                            result.vertexCount, result.indexCount,
                            static_cast<int>(buf->getVertexType()));
                        LOG_DEBUG(MOD_GRAPHICS, "GPUUpload processOne: VBO for region {} buf {} ready (verts={}, indices={})",
                                  regionIdx, bufferIdx, result.vertexCount, result.indexCount);
                    }
                }
            }
            pendingVBOUploads_.erase(regionIdx);
            break;
        }
    }

    // Sort texture rebuild queue: player's region first (distSq=-1), then nearest
    if (!textureRebuildQueue_.empty()) {
        std::sort(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                  [](const TextureRebuildEntry& a, const TextureRebuildEntry& b) {
                      return a.distanceSq < b.distanceSq;
                  });
    }

    return true;
}
#endif // EQT_HAS_GLES2

void IrrlichtRenderer::toggleGPUUploadThread() {
    if (isLoading()) return;
#ifdef EQT_HAS_GLES2
    gpuUploadEnabled_ = !gpuUploadEnabled_;
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: {}", gpuUploadEnabled_ ? "ENABLED" : "DISABLED");

    // Propagate to subsystems: set or clear their gpuUploadThread_ pointers
    GPUUploadThread* ptr = gpuUploadEnabled_ ? gpuUploadThread_.get() : nullptr;
    if (constrainedTextureCache_)
        constrainedTextureCache_->setGPUUploadThread(ptr);
    if (windowManager_)
        windowManager_->getIconLoader().setGPUUploadThread(ptr);
#else
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: not available (non-GLES2 build)");
#endif
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

    // Create an 8x10 arrow cursor texture with alpha transparency
    const int W = 8, H = 10;
    irr::video::IImage* img = driver_->createImage(irr::video::ECF_A8R8G8B8,
        irr::core::dimension2du(W, H));
    if (!img) return;

    const irr::video::SColor transparent(0, 0, 0, 0);
    const irr::video::SColor black(255, 0, 0, 0);
    const irr::video::SColor white(255, 255, 255, 255);

    // Clear to transparent
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img->setPixel(x, y, transparent);

    // Classic arrow cursor shape (1=black outline, 2=white fill)
    static const char* shape[10] = {
        "11      ",
        "121     ",
        "1221    ",
        "12221   ",
        "122221  ",
        "1222211 ",
        "111111   ",
        "   121  ",
        "    121 ",
        "     11 ",
    };

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            char c = shape[y][x];
            if (c == '1') img->setPixel(x, y, black);
            else if (c == '2') img->setPixel(x, y, white);
        }
    }

    softwareCursorTexture_ = driver_->addTexture("softwareCursor", img);
    img->drop();

    if (softwareCursorTexture_) {
        LOG_INFO(MOD_GRAPHICS, "Software cursor texture created for DRM mode");
    }
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
    if (!windowManager_ || !windowManager_->getOptionsWindow()) {
        return;
    }

    const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();
    bool zoneLoaded = !currentZoneName_.empty();

    // --- Particle Manager: toggle atmospheric billboard particles ---
    // ParticleManager is always created (in loadGlobalAssets) for unified fire support.
    // This toggle controls only the atmospheric billboard emitters (dust, pollen, etc.)
    if (particleManager_) {
        particleManager_->setEnabled(settings.atmosphericParticles);
    }

    // Lazily create weather effects if not yet created and particles are available
    if (!weatherEffects_ && particleManager_) {
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller (lazy)");
        }
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_.get());
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
            detailManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
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

    // Stop background icon sheet worker before zone cleanup
    if (windowManager_) {
        windowManager_->getIconLoader().stopWorker();
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

    // Unload texture atlases
    if (zoneAtlas_) {
        zoneAtlas_->unload();
        zoneAtlas_.reset();
    }
    if (objAtlas_) {
        objAtlas_->unload();
        objAtlas_.reset();
    }
    objAtlasPageOffset_ = 0;
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
        entityRenderer_->setConstrainedTextureCache(constrainedTextureCache_.get());
        // Wire mesh builder into cache for entity texture registration during processUploadQueue
        auto* meshBuilder = entityRenderer_->getMeshBuilder();
        if (meshBuilder)
            constrainedTextureCache_->setMeshBuilder(meshBuilder);
    }
    entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
    entityRenderer_->setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload);
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
static bool decodeBMPtoARGB(const std::vector<char>& data,
                            std::vector<uint8_t>& outPixels,
                            uint32_t& outWidth, uint32_t& outHeight) {
    if (data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());

    if (d[0] != 'B' || d[1] != 'M') return false;

    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(d + 10);
    int32_t width  = *reinterpret_cast<const int32_t*>(d + 18);
    int32_t height = *reinterpret_cast<const int32_t*>(d + 22);
    uint16_t bpp   = *reinterpret_cast<const uint16_t*>(d + 28);
    uint32_t compression = *reinterpret_cast<const uint32_t*>(d + 30);

    if (width <= 0 || width > 4096) return false;
    bool bottomUp = height > 0;
    if (height < 0) height = -height;
    if (height <= 0 || height > 4096) return false;

    outWidth = static_cast<uint32_t>(width);
    outHeight = static_cast<uint32_t>(height);
    outPixels.resize(width * height * 4);

    if (bpp == 8 && compression == 0) {
        const uint8_t* palette = d + 54;
        if (54 + 256 * 4 > data.size()) return false;
        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width + 3) & ~3;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = pixels[srcY * rowStride + x];
                int outIdx = (y * width + x) * 4;
                outPixels[outIdx + 0] = palette[idx * 4 + 0];
                outPixels[outIdx + 1] = palette[idx * 4 + 1];
                outPixels[outIdx + 2] = palette[idx * 4 + 2];
                outPixels[outIdx + 3] = 255;
            }
        }
        return true;
    } else if (bpp == 24 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width * 3 + 3) & ~3;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 3;
                int outIdx = (y * width + x) * 4;
                outPixels[outIdx + 0] = pixels[srcIdx + 0];
                outPixels[outIdx + 1] = pixels[srcIdx + 1];
                outPixels[outIdx + 2] = pixels[srcIdx + 2];
                outPixels[outIdx + 3] = 255;
            }
        }
        return true;
    } else if (bpp == 32 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = width * 4;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 4;
                int outIdx = (y * width + x) * 4;
                outPixels[outIdx + 0] = pixels[srcIdx + 0];
                outPixels[outIdx + 1] = pixels[srcIdx + 1];
                outPixels[outIdx + 2] = pixels[srcIdx + 2];
                outPixels[outIdx + 3] = pixels[srcIdx + 3];
            }
        }
        return true;
    }

    return false;
}

// Bilinear upscale A8R8G8B8 texture (used by sky texture loading)
static void bilinearUpscaleARGB(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                                std::vector<uint8_t>& dst, uint32_t dstW, uint32_t dstH) {
    dst.resize(dstW * dstH * 4);
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(src);
    uint32_t* dstPixels = reinterpret_cast<uint32_t*>(dst.data());

    float scaleX = static_cast<float>(srcW) / dstW;
    float scaleY = static_cast<float>(srcH) / dstH;

    for (uint32_t y = 0; y < dstH; ++y) {
        float srcYf = y * scaleY;
        uint32_t y0 = static_cast<uint32_t>(srcYf);
        uint32_t y1 = (y0 + 1 < srcH) ? y0 + 1 : y0;
        float fy = srcYf - y0;

        for (uint32_t x = 0; x < dstW; ++x) {
            float srcXf = x * scaleX;
            uint32_t x0 = static_cast<uint32_t>(srcXf);
            uint32_t x1 = (x0 + 1 < srcW) ? x0 + 1 : x0;
            float fx = srcXf - x0;

            uint32_t c00 = srcPixels[y0 * srcW + x0];
            uint32_t c10 = srcPixels[y0 * srcW + x1];
            uint32_t c01 = srcPixels[y1 * srcW + x0];
            uint32_t c11 = srcPixels[y1 * srcW + x1];

            auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
            auto ch = [](uint32_t pixel, int shift) -> float {
                return static_cast<float>((pixel >> shift) & 0xFF);
            };

            uint32_t result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                float v = lerp(lerp(ch(c00, shift), ch(c10, shift), fx),
                               lerp(ch(c01, shift), ch(c11, shift), fx), fy);
                result |= (static_cast<uint32_t>(v) & 0xFF) << shift;
            }
            dstPixels[y * dstW + x] = result;
        }
    }
}

void IrrlichtRenderer::loadZoneSequential(const std::string& eqClientPath,
                                           LoadingStatus& status) {
    auto totalStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: starting for zone '{}'", currentZoneName_);

    auto updateProgress = [&](int percent, const std::string& text) {
        status.setProgress(percent, text);
        LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: [{}%] {}", percent, text);
        // Render a loading frame so the progress bar updates visually
        std::wstring wtext(text.begin(), text.end());
        drawLoadingScreen(percent / 100.0f, wtext);
    };
    auto checkQuit = [&]() -> bool {
        return status.quitRequested.load(std::memory_order_relaxed);
    };

    // ── Setup ────────────────────────────────────────────────────────────────
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: zone='{}', eqClientPath='{}'", currentZoneName_, eqClientPath);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: particleManager_={}, windowManager_={}, zoneShader_={}, "
              "simulationWorker_={}, entityRenderer_={}, doorManager_={}",
              (bool)particleManager_, (bool)windowManager_, (bool)zoneShader_,
              (bool)simulationWorker_, (bool)entityRenderer_, (bool)doorManager_);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, "
              "maxFireGlowLights_={}, fireEffectsEnabled_={}, fireGlowLights_.size={}",
              fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
              maxFireGlowLights_, fireEffectsEnabled_, fireGlowLights_.size());
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: constrainedConfig: skipObjectBuild={}, enableTextureAtlas={}, "
              "atlasPath='{}', deferredAssetLoading={}",
              config_.constrainedConfig.skipObjectBuild, config_.constrainedConfig.enableTextureAtlas,
              config_.constrainedConfig.atlasPath, config_.constrainedConfig.deferredAssetLoading);

    stopSimulationWorker();
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();
    entityPrepReady_ = false;
    entityPrepScanCounter_ = 0;
    pendingZoneComputations_ = std::make_unique<PendingZoneComputations>();

    std::string eqPath = eqClientPath;
    if (!eqPath.empty() && eqPath.back() != '/' && eqPath.back() != '\\')
        eqPath += '/';

    // ── Step 1: S3D — parse zone archive ───────────────────────────────────
    updateProgress(50, "Loading zone data...");
    if (checkQuit()) return;
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
        LOG_ERROR(MOD_GRAPHICS, "loadZoneSequential: S3D produced no data");
        pendingZoneComputations_.reset();
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
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
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
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        auto zone = currentZone_;
        bool skipObjectBuild = config_.constrainedConfig.skipObjectBuild;
        const TreeIdentifier* treeIdentifier = treeManager_ ? &treeManager_->getTreeIdentifier() : nullptr;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: zone={}, wldLoader={}, skipObjectBuild={}, treeIdentifier={}",
                  (bool)zone, zone ? (bool)zone->wldLoader : false, skipObjectBuild, (bool)treeIdentifier);

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

                    if (treeIdentifier) {
                        const std::string& objName = objInstance.placeable->getName();
                        std::string primaryTexture;
                        if (!objInstance.geometry->textureNames().empty())
                            primaryTexture = objInstance.geometry->textureNames()[0];
                        if (treeIdentifier->isTreeMesh(objName, primaryTexture))
                            continue;
                    }

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
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: {} deferred object entries, {} after tree filtering",
                          computations->deferredObjectEntries.size(), computations->prebuiltDeferredObjects.size());
            }
        }
        logAssetBuildTime("seq_bsp_compute", 0, stepStart);
    }
    FlushThreadLog();

    // Install BSP data
    {
        auto stepStart = std::chrono::steady_clock::now();
        if (!currentZone_ || !currentZone_->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no wldLoader, fallback to single zone mesh");
            if (currentZone_ && !currentZone_->geometry && currentZone_->wldLoader)
                currentZone_->geometry = currentZone_->wldLoader->getCombinedGeometry();
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

    // ── Step 3: Atlas — texture atlas ──────────────────────────────────────
    updateProgress(60, "Building texture atlases...");
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        bool enableAtlas = config_.constrainedConfig.enableTextureAtlas;
        std::string atlasPathCopy = config_.constrainedConfig.atlasPath;
        bool skipObjectBuild = config_.constrainedConfig.skipObjectBuild;
        auto* computations = pendingZoneComputations_.get();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: enableAtlas={}, atlasPath='{}', skipObjectBuild={}",
                  enableAtlas, atlasPathCopy, skipObjectBuild);

        // Preload atlas data inline (was background thread)
        if (enableAtlas && !atlasPathCopy.empty() && computations) {
            std::string atlasDir = atlasPathCopy;
            if (!atlasDir.empty() && atlasDir.back() != '/') atlasDir += '/';

            std::string zoneAtlasFile = atlasDir + currentZoneName_ + ".atlas";
            computations->zoneAtlasPreload = TextureAtlas::preloadFromFile(zoneAtlasFile);
            if (!computations->zoneAtlasPreload.valid) {
                LOG_FATAL(MOD_GRAPHICS, "Failed to load texture atlas: {} (atlas enabled in preset but file missing or invalid)", zoneAtlasFile);
                pendingZoneComputations_.reset();
                return;
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: zone atlas preload '{}': valid={}, pages={}",
                      zoneAtlasFile, computations->zoneAtlasPreload.valid,
                      computations->zoneAtlasPreload.numPages);

            if (!skipObjectBuild) {
                std::string objAtlasFile = atlasDir + currentZoneName_ + "_obj.atlas";
                computations->objAtlasPreload = TextureAtlas::preloadFromFile(objAtlasFile);
                if (!computations->objAtlasPreload.valid) {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load object texture atlas: {} (atlas enabled in preset but file missing or invalid)", objAtlasFile);
                    pendingZoneComputations_.reset();
                    return;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: obj atlas preload '{}': valid={}, pages={}",
                          objAtlasFile, computations->objAtlasPreload.valid,
                          computations->objAtlasPreload.numPages);
            }
        }

        // Upload zone atlas pages with per-page progress (60-62%)
        int totalAtlasPages = (computations ? computations->zoneAtlasPreload.numPages : 0)
                            + (computations ? computations->objAtlasPreload.numPages : 0);
        int atlasPagesDone = 0;

        if (computations && computations->zoneAtlasPreload.valid) {
            zoneAtlas_ = std::make_shared<TextureAtlas>();
            int pageIdx = 0;
            bool done = false;
            while (!done) {
                // Synchronous GL upload — loading thread owns the GL context,
                // so upload directly instead of deferring to GPU upload thread.
                done = zoneAtlas_->uploadPreloadedPage(computations->zoneAtlasPreload, pageIdx);
                ++pageIdx;
                ++atlasPagesDone;
                if (totalAtlasPages > 0) {
                    int pct = 60 + static_cast<int>(2.0f * atlasPagesDone / totalAtlasPages);
                    updateProgress(pct, "Uploading atlas pages [" + std::to_string(atlasPagesDone)
                                        + "/" + std::to_string(totalAtlasPages) + "]");
                }
            }
            zoneAtlas_->finalizePreload(computations->zoneAtlasPreload);
            LOG_INFO(MOD_GRAPHICS, "Sequential: zone atlas uploaded: {} pages, {} tiles",
                     zoneAtlas_->getPageCount(), zoneAtlas_->getTileCount());
        } else {
            zoneAtlas_.reset();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: no zone atlas (preload invalid or atlas disabled)");
        }

        // Upload object atlas pages with per-page progress
        if (computations && computations->objAtlasPreload.valid) {
            objAtlas_ = std::make_unique<TextureAtlas>();
            int pageIdx = 0;
            bool done = false;
            while (!done) {
                done = objAtlas_->uploadPreloadedPage(computations->objAtlasPreload, pageIdx);
                ++pageIdx;
                ++atlasPagesDone;
                if (totalAtlasPages > 0) {
                    int pct = 60 + static_cast<int>(2.0f * atlasPagesDone / totalAtlasPages);
                    updateProgress(pct, "Uploading atlas pages [" + std::to_string(atlasPagesDone)
                                        + "/" + std::to_string(totalAtlasPages) + "]");
                }
            }
            objAtlas_->finalizePreload(computations->objAtlasPreload);
            LOG_INFO(MOD_GRAPHICS, "Sequential: obj atlas uploaded: {} pages, {} tiles",
                     objAtlas_->getPageCount(), objAtlas_->getTileCount());
        } else {
            objAtlas_.reset();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: no obj atlas (preload invalid or atlas disabled)");
        }

        // Set shader page textures
        if (zoneAtlas_ && zoneAtlas_->isLoaded() && zoneShader_ && zoneShader_->isAtlasAvailable()) {
            std::vector<uint32_t> pageTextures;
            for (uint16_t p = 0; p < zoneAtlas_->getPageCount(); ++p)
                pageTextures.push_back(zoneAtlas_->getPageTexture(p));
            zoneShader_->setAtlasPageTextures(pageTextures);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: zone shader atlas page textures set, {} pages", pageTextures.size());
        }
        if (objAtlas_ && objAtlas_->isLoaded() && zoneShader_ && zoneShader_->isAtlasAvailable()) {
            std::vector<uint32_t> objPageTextures;
            for (uint16_t p = 0; p < objAtlas_->getPageCount(); ++p)
                objPageTextures.push_back(objAtlas_->getPageTexture(p));
            objAtlasPageOffset_ = zoneShader_->appendAtlasPageTextures(objPageTextures);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step3: obj shader atlas page textures appended, {} pages, offset={}",
                      objPageTextures.size(), objAtlasPageOffset_);
        }
#ifdef EQT_HAS_GLES2
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
#endif
        logAssetBuildTime("seq_atlas", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 4: Regions — build ALL zone meshes eagerly ────────────────────
    updateProgress(63, "Building zone meshes...");
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: currentZone_={}, wldLoader={}, zoneBspTree_={}",
                  (bool)currentZone_, currentZone_ ? (bool)currentZone_->wldLoader : false, (bool)zoneBspTree_);

        if (currentZone_ && currentZone_->wldLoader && zoneBspTree_) {
            auto wldLoader = currentZone_->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
            // Do NOT set constrained texture cache during sequential loading.
            // The constrained cache defers to background threads and returns nullptr
            // on first call, which breaks single-pass mesh building. The unconstrained
            // path does synchronous DDS decode + driver_->addTexture() inline.
            if (zoneShader_ && zoneShader_->isAvailable()) {
                builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                               zoneShader_->getActiveAlphaTest());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: builder shader materials set (solid={}, alphaTest={})",
                          zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
            }
            if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
                builder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                                     zoneShader_->getActiveAtlasAlpha());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: builder atlas shader materials set (solid={}, alpha={})",
                          zoneShader_->getActiveAtlasSolid(), zoneShader_->getActiveAtlasAlpha());
            }
            bool useZoneAtlas = zoneAtlas_ && zoneAtlas_->isLoaded() &&
                                zoneShader_ && zoneShader_->isAtlasAvailable();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: useZoneAtlas={}, totalRegions={}",
                      useZoneAtlas, bspTree->regions.size());

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
                    if (useZoneAtlas) {
                        mesh = builder.buildAtlasedMesh(*geom, currentZone_->textures, *zoneAtlas_);
                        LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built atlased mesh, texNames={}, verts={}",
                                  i, geom->textureNames().size(), geom->vertices.size());
                    } else {
                        mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
                        LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built textured mesh, texNames={}, verts={}",
                                  i, geom->textureNames().size(), geom->vertices.size());
                    }
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
        }
        logAssetBuildTime("seq_regions", regionMeshNodes_.size(), stepStart);
    }
    FlushThreadLog();

    // ── Step 5: Assets — build indexes, create subsystems ──────────────────
    updateProgress(71, "Building asset indexes...");
    if (checkQuit()) return;
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
        }

        // Install equipment index
        if (computations && computations->equipmentIndex &&
            computations->equipmentIndex->loaded && entityRenderer_) {
            if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                eml->adoptIndex(
                    std::move(computations->equipmentIndex->modelIndex),
                    std::move(computations->equipmentIndex->textureIndex),
                    std::map<uint32_t, int>(itemToModelMap_));
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: equipment index installed on EquipmentModelLoader");
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: equipment index built but EquipmentModelLoader is null — cannot install");
            }
        }

        // Set zone on RaceModelLoader
        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setCurrentZone(currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: RaceModelLoader zone set to '{}'", currentZoneName_);
        }

        // Store model view deps
        if (windowManager_ && entityRenderer_) {
            windowManager_->storeModelViewDeps(smgr_,
                                               entityRenderer_->getRaceModelLoader(),
                                               entityRenderer_->getEquipmentModelLoader());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: window manager model view deps stored");
        }
        logAssetBuildTime("seq_assets", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 6: Objects — zone objects ──────────────────────────────────────
    updateProgress(75, "Installing zone objects...");
    if (checkQuit()) return;
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
        }

        // Build all object meshes eagerly inline (no deferred/PVS-gated build at runtime)
        {
            ZoneMeshBuilder objBuilder(smgr_, driver_, device_->getFileSystem());
            // Do NOT set constrained texture cache — use synchronous unconstrained path
            // (same rationale as region mesh building above).
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
            bool useObjAtlas = objAtlas_ && objAtlas_->isLoaded() &&
                               zoneShader_ && zoneShader_->isAtlasAvailable();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: useObjAtlas={}, totalObjects={}", useObjAtlas, deferredObjects_.size());

            size_t totalObjects = deferredObjects_.size();
            size_t objectsBuilt = 0;

            for (size_t oi = 0; oi < totalObjects; ++oi) {
                if (checkQuit()) return;

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
                    if (useObjAtlas) {
                        objMesh = objBuilder.buildAtlasedMesh(*objInstance.geometry, currentZone_->objectTextures,
                                                               *objAtlas_, objAtlasPageOffset_);
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built atlased mesh, texNames={}",
                                  oi, objName, objInstance.geometry->textureNames().size());
                    } else {
                        objMesh = objBuilder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built textured mesh, texNames={}",
                                  oi, objName, objInstance.geometry->textureNames().size());
                    }
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

                // PVS visibility (no SimWorker yet, so all visible)
                bool pvsVisible = isRegionPvsVisible(objectRegions_.back());
                if (!pvsVisible) {
                    objNode->remove();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' removed from scene graph (PVS hidden, region={})",
                              oi, objName, objectRegions_.back());
                }
                objectInSceneGraph_.push_back(pvsVisible);

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
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: doorManager_={}", (bool)doorManager_);
        if (!doorManager_) {
            doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
            // Do NOT set constrained texture cache yet — sequential builds use
            // synchronous unconstrained path. Set it after building for runtime use.
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
        // Now set constrained cache for runtime door rebuilds (texture arrivals, etc.)
        if (doorManager_ && constrainedTextureCache_) {
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: doorManager constrained texture cache set for runtime");
        }
        logAssetBuildTime("seq_doors", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 8: Entities — inline prep + build (no background threads) ────
    updateProgress(80, "Preparing entity models...");
    if (checkQuit()) return;
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
                if (checkQuit()) return;

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

                if (!success) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step8a: entity {} ({}) race={} gender={} preload FAILED — skipping",
                             spawnId, vis.name, vis.raceId, vis.gender);
                    ++prepCount;
                    continue;
                }

                // Step 8c: Decode variant textures (body-part overrides based on appearance)
                std::vector<DecodedTexture> variantTextures;
                {
                    auto modelData = modelLoader->getRaceModelData(vis.raceId, vis.gender);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) appearance.texture={} helm={} modelData={}",
                              spawnId, vis.name, vis.appearance.texture, vis.appearance.helm, (bool)modelData);
                    if (vis.appearance.texture != 0 || vis.appearance.helm != 0) {
                        std::string raceCode = RaceModelLoader::getRaceCode(vis.raceId);
                        std::string lowerRaceCode = raceCode;
                        std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        std::set<std::string> decodedNames;

                        auto decodeAndAdd = [&](const std::string& texName, const std::shared_ptr<TextureInfo>& texInfo) {
                            if (!texInfo || texInfo->data.empty()) return;
                            std::string lowerName = texName;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            if (decodedNames.count(lowerName) > 0) return;

                            if (DDSDecoder::isDDS(texInfo->data)) {
                                DecodedTexture decoded;
                                decoded.name = texName;
                                DecodedImage img = DDSDecoder::decode(texInfo->data);
                                if (!img.isValid()) {
                                    LOG_FATAL(MOD_GRAPHICS, "Failed to decode variant texture: '{}' for entity {} ({}) (DDS decode failed, size={})",
                                              texName, spawnId, vis.name, texInfo->data.size());
                                    return;
                                }
                                decoded.width = img.width;
                                decoded.height = img.height;
                                decoded.argbPixels.resize(img.width * img.height);
                                for (uint32_t pi = 0; pi < img.width * img.height; ++pi) {
                                    uint8_t r = img.pixels[pi * 4 + 0];
                                    uint8_t g = img.pixels[pi * 4 + 1];
                                    uint8_t b = img.pixels[pi * 4 + 2];
                                    uint8_t a = img.pixels[pi * 4 + 3];
                                    decoded.argbPixels[pi] = (static_cast<uint32_t>(a) << 24) |
                                                             (static_cast<uint32_t>(r) << 16) |
                                                             (static_cast<uint32_t>(g) << 8) |
                                                             static_cast<uint32_t>(b);
                                    if (a < 255) decoded.hasAlpha = true;
                                }
                                decodedNames.insert(lowerName);
                                variantTextures.push_back(std::move(decoded));
                            } else if (texInfo->data.size() >= 2 && texInfo->data[0] == 'B' && texInfo->data[1] == 'M') {
                                // BMP decode inline
                                const uint8_t* d = reinterpret_cast<const uint8_t*>(texInfo->data.data());
                                if (texInfo->data.size() < 54) return;
                                uint32_t dataOffset = d[10] | (d[11] << 8) | (d[12] << 16) | (d[13] << 24);
                                int32_t width = d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24);
                                int32_t height = d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24);
                                uint16_t bpp = d[28] | (d[29] << 8);
                                bool bottomUp = (height > 0);
                                if (height < 0) height = -height;
                                if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
                                if (bpp != 24 && bpp != 32) return;
                                if (dataOffset >= texInfo->data.size()) return;
                                uint32_t w = static_cast<uint32_t>(width);
                                uint32_t h = static_cast<uint32_t>(height);
                                uint32_t rowStride = ((w * (bpp / 8) + 3) & ~3);
                                DecodedTexture decoded;
                                decoded.name = texName;
                                decoded.width = w;
                                decoded.height = h;
                                decoded.argbPixels.resize(w * h);
                                decoded.hasAlpha = (bpp == 32);
                                for (uint32_t y = 0; y < h; ++y) {
                                    uint32_t srcRow = bottomUp ? (h - 1 - y) : y;
                                    const uint8_t* row = d + dataOffset + srcRow * rowStride;
                                    if (dataOffset + srcRow * rowStride + w * (bpp / 8) > texInfo->data.size()) return;
                                    for (uint32_t x = 0; x < w; ++x) {
                                        uint8_t bv = row[x * (bpp / 8) + 0];
                                        uint8_t gv = row[x * (bpp / 8) + 1];
                                        uint8_t rv = row[x * (bpp / 8) + 2];
                                        uint8_t av = (bpp == 32) ? row[x * 4 + 3] : 255;
                                        decoded.argbPixels[y * w + x] = (static_cast<uint32_t>(av) << 24) |
                                                                         (static_cast<uint32_t>(rv) << 16) |
                                                                         (static_cast<uint32_t>(gv) << 8) |
                                                                         static_cast<uint32_t>(bv);
                                        if (av < 255) decoded.hasAlpha = true;
                                    }
                                }
                                decodedNames.insert(lowerName);
                                variantTextures.push_back(std::move(decoded));
                            }
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
                                        decodeAndAdd(texName, texInfo);
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

                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) decoded {} variant textures",
                          spawnId, vis.name, variantTextures.size());

                // Step 8d: Equipment model extraction + texture decode
                struct EquipPrepData {
                    int modelId = 0;
                    uint32_t equipmentId = 0;
                    bool isPrimary = true;
                    std::shared_ptr<ZoneGeometry> geometry;
                    std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
                    std::vector<DecodedTexture> decodedTextures;
                };
                std::vector<EquipPrepData> equipmentData;

                if (equipLoader) {
                    uint32_t primaryId = vis.appearance.equipment[7];
                    uint32_t secondaryId = vis.appearance.equipment[8];

                    auto prepOneEquip = [&](uint32_t equipmentId, bool isPrimary) {
                        if (equipmentId == 0) return;
                        int modelId = equipLoader->getModelIdForItem(equipmentId);
                        if (modelId < 0) modelId = static_cast<int>(equipmentId);
                        const EquipmentModelLoader::EquipmentModelRef* modelRef = equipLoader->getModelRef(modelId);
                        if (!modelRef) {
                            LOG_FATAL(MOD_GRAPHICS, "Failed to load equipment model ref: entity {} ({}) equipment {} ({}) modelId={} (not found in item_models.json)",
                                      spawnId, vis.name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                            return;
                        }
                        auto equipData2 = EquipmentModelLoader::extractEquipmentModelOffThread(*modelRef, modelId);
                        if (!equipData2) {
                            LOG_FATAL(MOD_GRAPHICS, "Failed to load equipment model: entity {} ({}) equipment {} ({}) modelId={}", spawnId, vis.name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                            return;
                        }

                        EquipPrepData prepData;
                        prepData.modelId = modelId;
                        prepData.equipmentId = equipmentId;
                        prepData.isPrimary = isPrimary;
                        prepData.geometry = equipData2->geometry;
                        prepData.rawTextures = equipData2->textures;

                        for (const auto& texName : equipData2->textureNames) {
                            std::string ln = texName;
                            std::transform(ln.begin(), ln.end(), ln.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            auto texIt = equipData2->textures.find(ln);
                            if (texIt == equipData2->textures.end() || !texIt->second) continue;
                            const auto& texInfo = texIt->second;
                            if (texInfo->data.empty()) continue;

                            if (DDSDecoder::isDDS(texInfo->data)) {
                                DecodedTexture decoded;
                                decoded.name = texName;
                                DecodedImage img = DDSDecoder::decode(texInfo->data);
                                if (img.isValid()) {
                                    decoded.width = img.width;
                                    decoded.height = img.height;
                                    decoded.argbPixels.resize(img.width * img.height);
                                    for (uint32_t pi = 0; pi < img.width * img.height; ++pi) {
                                        uint8_t r = img.pixels[pi * 4 + 0];
                                        uint8_t g = img.pixels[pi * 4 + 1];
                                        uint8_t b = img.pixels[pi * 4 + 2];
                                        uint8_t a = img.pixels[pi * 4 + 3];
                                        decoded.argbPixels[pi] = (static_cast<uint32_t>(a) << 24) |
                                                                 (static_cast<uint32_t>(r) << 16) |
                                                                 (static_cast<uint32_t>(g) << 8) |
                                                                 static_cast<uint32_t>(b);
                                        if (a < 255) decoded.hasAlpha = true;
                                    }
                                    prepData.decodedTextures.push_back(std::move(decoded));
                                } else {
                                    LOG_FATAL(MOD_GRAPHICS, "Failed to decode equipment texture: '{}' for entity {} ({}) equipment {} (DDS decode failed, size={})",
                                              texName, spawnId, vis.name, prepData.equipmentId, texInfo->data.size());
                                }
                            }
                        }
                        equipmentData.push_back(std::move(prepData));
                    };

                    prepOneEquip(primaryId, true);
                    prepOneEquip(secondaryId, false);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8d: entity {} ({}) equipment: primary={} secondary={} extracted={}",
                              spawnId, vis.name, vis.appearance.equipment[7], vis.appearance.equipment[8], equipmentData.size());
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
                                 spawnId, vis.name, variantTextures.size(), equipmentData.size());
                    } else {
                        auto& mvis = visIt->second;
                        mvis.variantTextures = std::move(variantTextures);

                        for (auto& eq : equipmentData) {
                            for (auto& tex : eq.decodedTextures) {
                                mvis.equipmentTextures.push_back(std::move(tex));
                            }
                            EntityVisual::EquipmentStaging staging;
                            staging.modelId = eq.modelId;
                            staging.equipmentId = eq.equipmentId;
                            staging.isPrimary = eq.isPrimary;
                            staging.geometry = eq.geometry;
                            staging.rawTextures = eq.rawTextures;
                            mvis.equipmentStaging.push_back(std::move(staging));
                        }

                        // Submit decoded textures to constrained cache
                        if (constrainedTextureCache_) {
                            auto modelData = modelLoader->getRaceModelData(mvis.raceId, mvis.gender);
                            if (modelData) {
                                for (const auto& decoded : modelData->decodedTextures) {
                                    if (!decoded.argbPixels.empty()) {
                                        std::string ln = decoded.name;
                                        std::transform(ln.begin(), ln.end(), ln.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });
                                        constrainedTextureCache_->queueDecodedARGB(
                                            ln, decoded.argbPixels, decoded.width, decoded.height, decoded.hasAlpha);
                                    }
                                }
                            }
                            for (const auto& decoded : mvis.variantTextures) {
                                if (!decoded.argbPixels.empty()) {
                                    std::string ln = decoded.name;
                                    std::transform(ln.begin(), ln.end(), ln.begin(),
                                                   [](unsigned char c) { return std::tolower(c); });
                                    constrainedTextureCache_->queueDecodedARGB(
                                        ln, decoded.argbPixels, decoded.width, decoded.height, decoded.hasAlpha);
                                }
                            }
                            for (const auto& decoded : mvis.equipmentTextures) {
                                if (!decoded.argbPixels.empty()) {
                                    std::string ln = decoded.name;
                                    std::transform(ln.begin(), ln.end(), ln.begin(),
                                                   [](unsigned char c) { return std::tolower(c); });
                                    constrainedTextureCache_->queueDecodedARGB(
                                        ln, decoded.argbPixels, decoded.width, decoded.height, decoded.hasAlpha);
                                }
                            }
                            mvis.texturesSubmittedToGpu = true;
                        }
                        mvis.entityPrepComplete = true;
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) distributed: variantTex={} equipTex={} equipStaging={} "
                                  "baseTex={} submittedToGpu={}",
                                  spawnId, vis.name, mvis.variantTextures.size(), mvis.equipmentTextures.size(),
                                  mvis.equipmentStaging.size(),
                                  modelLoader->getRaceModelData(mvis.raceId, mvis.gender)
                                      ? modelLoader->getRaceModelData(mvis.raceId, mvis.gender)->decodedTextures.size() : 0,
                                  mvis.texturesSubmittedToGpu);
                    }
                }

                // Flush constrained texture cache uploads (we have GL context)
                if (constrainedTextureCache_) {
                    constrainedTextureCache_->processUploadQueue();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) flushed texture cache uploads", spawnId, vis.name);
                }

                // Promote prepared models so buildEntityMesh finds them
                modelLoader->promotePreparedModels();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8f: entity {} ({}) promoted prepared models", spawnId, vis.name);

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
        }
        entityPrepReady_ = true;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityPrepReady_=true");
        logAssetBuildTime("seq_entities", 0, stepStart);
    }
    FlushThreadLog();

    // ── Step 9: Collision ──────────────────────────────────────────────────
    updateProgress(88, "Setting up collision...");
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: smgr_={}, regionWorldTriangles_={}, objectNodes_={}, zoneBspTree_={}",
                  (bool)smgr_, regionWorldTriangles_.size(), objectNodes_.size(), (bool)zoneBspTree_);
        setupMinimalZoneCollision();

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
    if (checkQuit()) return;
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        bool skyRendering = config_.constrainedConfig.skyRendering;
        auto skyDomeMode = config_.constrainedConfig.skyDomeMode;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: computations={}, skyRendering={}, skyDomeMode={}, "
                  "storedZoneEnv.pending={}, skyRenderer_={}, weatherSystem_={}",
                  (bool)computations, skyRendering, (int)skyDomeMode,
                  storedZoneEnvironment_.pending, (bool)skyRenderer_, (bool)weatherSystem_);

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
                    return;
                }
                std::string skyIniPath = eqPath + "sky.ini";
                bool skyIniLoaded = skyData->skyConfig->loadFromFile(skyIniPath);
                if (!skyIniLoaded) {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load sky config: {} (sky rendering enabled in preset)", skyIniPath);
                    pendingZoneComputations_.reset();
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
                    const auto& textures = skyData->skyLoader->getSkyData()->textures;
                    for (const auto& [texName, texInfo] : textures) {
                        if (!texInfo || texInfo->data.size() < 2) continue;
                        if (texInfo->data[0] != 'B' || texInfo->data[1] != 'M') continue;
                        if (!neededSet.empty() && neededSet.find(texName) == neededSet.end()) continue;

                        PendingZoneComputations::SkyLoadData::PreDecodedTexture preTex;
                        preTex.name = texName;
                        uint32_t decW = 0, decH = 0;
                        std::vector<uint8_t> decoded;
                        if (!decodeBMPtoARGB(texInfo->data, decoded, decW, decH)) {
                            LOG_FATAL(MOD_GRAPHICS, "Failed to decode sky texture: '{}' (BMP decode failed, size={})",
                                      texName, texInfo->data.size());
                            continue;
                        }
                        if (decW <= 128 && decH <= 128 && decW > 0 && decH > 0) {
                            const uint32_t targetSize = 512;
                            bilinearUpscaleARGB(decoded.data(), decW, decH,
                                                preTex.pixels, targetSize, targetSize);
                            preTex.width = targetSize;
                            preTex.height = targetSize;
                        } else {
                            preTex.pixels = std::move(decoded);
                            preTex.width = decW;
                            preTex.height = decH;
                        }
                        skyData->preDecodedTextures.push_back(std::move(preTex));
                    }
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: pre-decoded {} sky textures", skyData->preDecodedTextures.size());
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
        }

        // Upload sky textures (all at once)
        if (!config_.constrainedConfig.skipSkyTextureUpload && skyRenderer_ &&
            computations && computations->skyLoadData) {
            auto& preTextures = computations->skyLoadData->preDecodedTextures;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: uploading {} sky textures", preTextures.size());
#ifdef EQT_HAS_GLES2
            for (size_t i = 0; i < preTextures.size(); ++i) {
                auto& preTex = preTextures[i];
                if (preTex.pixels.empty()) continue;
                if (skyRenderer_->beginStripUpload(preTex.name, preTex.pixels.data(),
                                                    preTex.width, preTex.height)) {
                    preTex.pixels.clear();
                    preTex.pixels.shrink_to_fit();
                    continue;
                }
                while (skyRenderer_->isStripActive()) {
                    if (skyRenderer_->continueStripUpload()) {
                        skyRenderer_->finalizeStripUpload();
                        break;
                    }
                }
                preTex.pixels.clear();
                preTex.pixels.shrink_to_fit();
            }
#else
            for (auto& preTex : preTextures) {
                if (!preTex.pixels.empty()) {
                    skyRenderer_->uploadPreDecodedTexture(preTex.name, preTex.pixels.data(),
                                                          preTex.width, preTex.height);
                    preTex.pixels.clear();
                    preTex.pixels.shrink_to_fit();
                }
            }
#endif
        }

        // Finalize sky (P10_Sky_Finalize)
        if (computations && computations->skyLoadData) {
            computations->skyLoadData->preDecodedTextures.clear();
            computations->skyLoadData->preDecodedTextures.shrink_to_fit();
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
    if (checkQuit()) return;
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

        // Trees
        if (treeManager_ && currentZone_ && !currentZone_->objects.empty()) {
            treeManager_->loadConfig("", currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: treeManager_ loadConfig for zone '{}'", currentZoneName_);
            if (driver_->getDriverType() == irr::video::EDT_BURNINGSVIDEO) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software renderer tree init path, objects={}", currentZone_->objects.size());
                // Software path: initialize CPU tree wind
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
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: no zoneBspTree_, skipping assignBspRegions");
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: software tree init complete");
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: GPU renderer path, tree wind via shader fixup pass (driver={})",
                          (int)driver_->getDriverType());
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree init SKIPPED: treeManager_={}, currentZone_={}, objects={}",
                      (bool)treeManager_, (bool)currentZone_,
                      currentZone_ ? currentZone_->objects.size() : 0);
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: trees done, treeManager_ initialized={}",
                  treeManager_ ? treeManager_->isInitializing() : false);

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
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvironmentalDisplaySettings: windowManager_={}, "
                  "optionsWindow={}",
                  (bool)windowManager_,
                  windowManager_ ? (bool)windowManager_->getOptionsWindow() : false);
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
                            for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                                objNode->getMaterial(mi).MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                                    zoneShader_->getActiveWindAlphaTest());
                                objNode->getMaterial(mi).MaterialTypeParam = meshBbox.MinEdge.Y;
                                objNode->getMaterial(mi).MaterialTypeParam2 = meshBbox.MaxEdge.Y;
                                objNode->getMaterial(mi).BackfaceCulling = false;
                            }
                            ++treesWindApplied;
                            LOG_TRACE(MOD_GRAPHICS, "SEQ Step11: fixup tree wind applied: '{}' tex='{}' "
                                      "materials={} bboxY=[{:.1f},{:.1f}] materialType={}",
                                      objName, primaryTexture, objNode->getMaterialCount(),
                                      meshBbox.MinEdge.Y, meshBbox.MaxEdge.Y,
                                      zoneShader_->getActiveWindAlphaTest());
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
    if (checkQuit()) return;
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

void IrrlichtRenderer::createZoneMesh() {
    if (!currentZone_ || !currentZone_->geometry) {
        return;
    }

    if (zoneMeshNode_) {
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());

    // Pass constrained texture cache if in constrained mode
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());
    }
    // Pass GLSL shader material types if available
    if (zoneShader_ && zoneShader_->isAvailable()) {
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest());
    }

    irr::scene::IMesh* mesh = nullptr;

    if (!currentZone_->textures.empty() && !currentZone_->geometry->textureNames().empty()) {
        mesh = builder.buildTexturedMesh(*currentZone_->geometry, currentZone_->textures);
    } else {
        mesh = builder.buildColoredMesh(*currentZone_->geometry);
    }

    if (mesh) {
        // Use octree scene node for automatic frustum culling of zone geometry
        // minimalPolysPerNode=256 controls octree subdivision granularity
        // This helps with OpenGL/llvmpipe but not with Irrlicht's software renderer
        zoneMeshNode_ = smgr_->addOctreeSceneNode(mesh, nullptr, -1, 256);
        if (zoneMeshNode_) {
            LOG_INFO(MOD_GRAPHICS, "Zone mesh created as octree node (polys per node: 256)");
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = lightingEnabled_;
                zoneMeshNode_->getMaterial(i).BackfaceCulling = false;
                zoneMeshNode_->getMaterial(i).GouraudShading = true;
                zoneMeshNode_->getMaterial(i).FogEnable = fogEnabled_;
                zoneMeshNode_->getMaterial(i).Wireframe = wireframeMode_;
                zoneMeshNode_->getMaterial(i).NormalizeNormals = true;
                // Set material colors for proper lighting response
                zoneMeshNode_->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                zoneMeshNode_->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }

            // NOTE: Collision detection is set up progressively via setupMinimalZoneCollision()
            // and addRegionToCollision() during the background zone loading phases.

            // Initialize animated texture manager for zone textures
            animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());
            if (constrainedTextureCache_) {
                animatedTextureManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
            }
            int animCount = animatedTextureManager_->initialize(
                *currentZone_->geometry, currentZone_->textures, mesh);

            // Auto-detect water texture animations by naming pattern (water01, water02, etc.)
            int waterAnimCount = animatedTextureManager_->detectWaterAnimations(
                currentZone_->textures, mesh);
            animCount += waterAnimCount;

            if (animCount > 0) {
                LOG_DEBUG(MOD_GRAPHICS, "Initialized {} animated zone textures ({} water auto-detected)",
                          animCount, waterAnimCount);
                // Register the zone scene node for animated texture updates
                animatedTextureManager_->addSceneNode(zoneMeshNode_);
            }
        }
        mesh->drop();
    }
}

// ======== OnRenderPassPreRender (ILightManager hook) ========
// Fires after CAMERA pass sets up view/projection, before SOLID pass renders nodes.
// We intercept ESNRP_SOLID to draw zone geometry manually in sorted order.
void IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) {
    passStart_ = std::chrono::steady_clock::now();
    if (!firstPassSeen_) {
        firstPassStart_ = passStart_;
        firstPassSeen_ = true;
    }
    currentPass_ = renderPass;

    // Capture 3D camera transforms during the SOLID pass — this is the only
    // reliable point where ETS_VIEW/ETS_PROJECTION contain the 3D perspective
    // camera matrices. By the time drawAll() returns, they may be overwritten
    // by 2D rendering (billboards, GUI nodes, overlays).
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_) {
        renderer_->captured3DView_ = renderer_->driver_->getTransform(irr::video::ETS_VIEW);
        renderer_->captured3DProj_ = renderer_->driver_->getTransform(irr::video::ETS_PROJECTION);
        renderer_->have3DTransforms_ = true;
    }

    // Hook: draw zone geometry manually before Irrlicht's SOLID pass
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_ && renderer_->manualZoneDrawEnabled_) {
        auto drawStart = std::chrono::steady_clock::now();
        if (renderer_->portalOcclusionEnabled_ && renderer_->portalOcclusionEligible_) {
            renderer_->drawZoneGeometryWithPortals();
        } else {
            renderer_->drawZoneGeometrySorted();
        }
        renderer_->frameTimings_.manualZoneDraw = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - drawStart).count();
    }
}

// ======== uploadMeshHardwareBuffers ========
// Uploads VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void IrrlichtRenderer::uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !driver_ || driver_->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf && buf->getVertexCount() > 0)
            gles2CreateStaticHWBuffer(driver_, buf);
    }
#endif
}

// ======== deleteMeshHardwareBuffers ========
// Deletes VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void IrrlichtRenderer::deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !driver_ || driver_->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf)
            gles2DeleteStaticHWBuffer(driver_, buf);
    }
#endif
}

// ======== drawZoneGeometrySorted ========
// Draws zone geometry sorted by material (primary) and distance (secondary).
// Material sorting minimizes expensive setMaterial() state changes (glUseProgram,
// glBindTexture, glUniform*). Distance sorting within each material group
// preserves front-to-back order for early-Z rejection on tile-based GPUs.
void IrrlichtRenderer::drawZoneGeometrySorted() {
    if (sortedZoneDrawList_.empty() && !fallbackMeshNode_) return;

    // Collect all visible mesh buffers into a flat draw list
    sortedDrawEntries_.clear();

    auto collectMeshBuffers = [this](irr::scene::IMeshSceneNode* node, float distSq) {
        irr::scene::IMesh* mesh = node->getMesh();
        if (!mesh) return;

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(node->getPosition());

        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();

            // Material key: (materialType << 16) | textureID
            // Groups all same-shader, same-texture draws together
            uint32_t texId = 0;
            if (mat.getTexture(0)) {
                // Use low 16 bits of texture pointer as a hash
                texId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
            }
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            sortedDrawEntries_.push_back({matKey, distSq, buf, worldMat});
        }
    };

    // When portal vis is enabled via BFS (no stencil), filter to portal-visible regions
    const bool useBfsPortalCulling = portalVisibleRegions_ && !portalVisibleRegions_->empty() &&
                                     config_.constrainedConfig.portalOcclusion &&
                                     !config_.constrainedConfig.enableStencilBuffer;

    int regionsDrawn = 0;
    int portalCulled = 0;
    for (const auto& entry : sortedZoneDrawList_) {
        if (!entry.node) continue;

        // BFS portal culling: skip regions not reachable through portal walk
        if (useBfsPortalCulling && portalVisibleRegions_->count(entry.regionIdx) == 0) {
            portalCulled++;
            continue;
        }

        auto it = regionMeshNodes_.find(entry.regionIdx);
        if (it == regionMeshNodes_.end() || !it->second) continue;

        collectMeshBuffers(it->second, entry.distanceSq);
        regionsDrawn++;
    }

    // Add fallback mesh buffers (always drawn last — use max distance)
    if (fallbackMeshNode_) {
        collectMeshBuffers(fallbackMeshNode_, 1e18f);
    }

    // Sort by material key (primary), distance (secondary)
    std::sort(sortedDrawEntries_.begin(), sortedDrawEntries_.end(),
        [](const ZoneDrawEntry& a, const ZoneDrawEntry& b) {
            if (a.materialKey != b.materialKey)
                return a.materialKey < b.materialKey;
            return a.distanceSq < b.distanceSq;
        });

    // Draw — setMaterial() is called every time because custom shader callbacks
    // need the current world matrix. However, sorting by material ensures the
    // driver's state tracking (SOGLES2State) skips redundant GL calls for
    // blend/depth/cull/texture/program when consecutive draws share the same material.
    for (const auto& de : sortedDrawEntries_) {
        driver_->setTransform(irr::video::ETS_WORLD, de.worldMat);
        driver_->setMaterial(de.buffer->getMaterial());
        driver_->drawMeshBuffer(de.buffer);
    }

    frameTimings_.manualZoneRegionsDrawn = regionsDrawn;
    frameTimings_.manualZoneDrawCalls = static_cast<int>(sortedDrawEntries_.size());
}

// ======== drawRegionMesh ========
// Draws a single region's mesh buffers with proper world transform.
void IrrlichtRenderer::drawRegionMesh(size_t regionIdx) {
    irr::scene::IMeshSceneNode* node = nullptr;

    if (regionIdx == SIZE_MAX) {
        node = fallbackMeshNode_;
    } else {
        auto it = regionMeshNodes_.find(regionIdx);
        if (it == regionMeshNodes_.end() || !it->second) return;
        node = it->second;
    }

    if (!node) return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    // Set world transform — region nodes only have translation (position), no rotation/scale.
    // Use getPosition() (relative) not getAbsolutePosition() because Irrlicht's OnAnimate()
    // skips invisible nodes, so AbsoluteTransformation is never computed for hidden nodes.
    // Zone mesh nodes are direct children of the root scene node, so relative == absolute.
    irr::core::matrix4 worldMat;
    worldMat.setTranslation(node->getPosition());
    driver_->setTransform(irr::video::ETS_WORLD, worldMat);

    // Draw each mesh buffer
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf || buf->getVertexCount() == 0) continue;
        driver_->setMaterial(buf->getMaterial());
        driver_->drawMeshBuffer(buf);
    }
}

// ======== drawPortalQuad ========
// Draws a portal polygon as a triangle fan (for stencil write/clear).
// Portal vertices are in EQ Z-up coords, converted to Irrlicht Y-up: (x,y,z) -> (x,z,y)
void IrrlichtRenderer::drawPortalQuad(const Portal& portal) {
    size_t n = portal.vertexCount();
    if (n < 3) return;

    std::vector<irr::video::S3DVertex> verts(n);
    for (size_t i = 0; i < n; ++i) {
        verts[i].Pos.X = portal.vx(i);
        verts[i].Pos.Y = portal.vz(i);  // EQ Z -> Irrlicht Y
        verts[i].Pos.Z = portal.vy(i);  // EQ Y -> Irrlicht Z
        verts[i].Color = irr::video::SColor(255, 255, 255, 255);
        verts[i].TCoords.X = 0.0f;
        verts[i].TCoords.Y = 0.0f;
        verts[i].Normal.X = portal.normalX;
        verts[i].Normal.Y = portal.normalZ;
        verts[i].Normal.Z = portal.normalY;
    }

    // Fan triangulation: (0,1,2), (0,2,3), (0,3,4), ...
    std::vector<irr::u16> indices;
    indices.reserve((n - 2) * 3);
    for (size_t i = 1; i + 1 < n; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<irr::u16>(i));
        indices.push_back(static_cast<irr::u16>(i + 1));
    }

    // Identity world transform
    irr::core::matrix4 identity;
    driver_->setTransform(irr::video::ETS_WORLD, identity);

    // Material: no texture, no lighting, depth test but no depth write
    irr::video::SMaterial mat;
    mat.Lighting = false;
    mat.ZBuffer = irr::video::ECFN_LESSEQUAL;
    mat.ZWriteEnable = false;
    mat.BackfaceCulling = false;
    mat.MaterialType = irr::video::EMT_SOLID;
    driver_->setMaterial(mat);

    driver_->drawVertexPrimitiveList(verts.data(), static_cast<irr::u32>(n),
        indices.data(), static_cast<irr::u32>(n - 2),
        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}

// ======== computePortalVisibleRegions ========
// Walk the portal graph from the camera's BSP region via BFS.
// Only regions reachable through portal openings that face the camera
// and intersect the view frustum are added.  Depth limit 3.
void IrrlichtRenderer::buildRegionNeighborMap() {
    regionNeighbors_.clear();
    if (!portalSystem_ || !portalSystem_->hasPortals()) return;

    const auto& portalData = portalSystem_->getData();
    for (const auto& [regionIdx, portalIndices] : portalData.regionPortals) {
        auto& neighbors = regionNeighbors_[regionIdx];
        for (size_t pi : portalIndices) {
            size_t other = portalSystem_->getOtherRegion(pi, regionIdx);
            if (other != SIZE_MAX) neighbors.push_back(other);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "Built region neighbor map: {} regions with portal neighbors",
              regionNeighbors_.size());
}

// ======== drawZoneGeometryWithPortals ========
// Uses stencil-based portal occlusion to render only visible rooms.
void IrrlichtRenderer::drawZoneGeometryWithPortals() {
#ifdef EQT_HAS_GLES2
    if (!portalSystem_ || currentPvsRegion_ == SIZE_MAX) {
        // Fallback: just do front-to-back sorted draw
        drawZoneGeometrySorted();
        return;
    }

    // Draw current room with no stencil masking
    std::unordered_set<size_t> drawnRegions;
    drawRegionMesh(currentPvsRegion_);
    drawnRegions.insert(currentPvsRegion_);

    // Recursively draw visible rooms through portals using stencil
    drawPortalRecursive(currentPvsRegion_, 0, 3, drawnRegions);

    // Disable stencil, restore color mask and depth write to defaults
    // (matches SOGLES2State::reset() defaults so driver cache stays in sync)
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    // Draw fallback mesh (geometry not in any BSP region)
    if (fallbackMeshNode_) {
        drawRegionMesh(SIZE_MAX);
    }
#else
    drawZoneGeometrySorted();
#endif
}

// ======== drawPortalRecursive ========
// Recursively draws rooms visible through portals using nested stencil masking.
void IrrlichtRenderer::drawPortalRecursive(size_t fromRegion, int stencilLevel,
                                            int maxDepth,
                                            std::unordered_set<size_t>& drawn) {
#ifdef EQT_HAS_GLES2
    if (!portalSystem_ || maxDepth <= 0) return;

    const auto& portalIndices = portalSystem_->getPortalsForRegion(fromRegion);

    for (size_t portalIdx : portalIndices) {
        size_t otherRegion = portalSystem_->getOtherRegion(portalIdx, fromRegion);
        if (otherRegion == SIZE_MAX) continue;
        if (drawn.count(otherRegion)) continue;

        // Check PVS: is otherRegion visible from current region?
        if (currentPvsRegion_ != SIZE_MAX && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            const auto& currentRegionData = zoneBspTree_->regions[currentPvsRegion_];
            if (currentRegionData && otherRegion < currentRegionData->visibleRegions.size()) {
                if (!currentRegionData->visibleRegions[otherRegion]) continue;
            }
        }

        // Check if the other region has a mesh
        auto nodeIt = regionMeshNodes_.find(otherRegion);
        if (nodeIt == regionMeshNodes_.end() || !nodeIt->second) continue;

        // Frustum cull the portal center (quick reject)
        if (frustumCuller_ && frustumCuller_->isEnabled()) {
            const Portal& portal = portalSystem_->getData().portals[portalIdx];
            auto bboxIt = regionBoundingBoxes_.find(otherRegion);
            if (bboxIt != regionBoundingBoxes_.end()) {
                const auto& bbox = bboxIt->second;
                if (!frustumCuller_->testAABB(
                        bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                        bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z)) {
                    continue;
                }
            }
        }

        const Portal& portal = portalSystem_->getData().portals[portalIdx];

        // Step 1: Stencil write — draw portal quad, increment stencil where depth passes
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, stencilLevel, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);

        // Step 2: Draw room mesh where stencil == stencilLevel+1
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        drawRegionMesh(otherRegion);
        drawn.insert(otherRegion);

        // Step 3: Recurse into deeper rooms
        drawPortalRecursive(otherRegion, stencilLevel + 1, maxDepth - 1, drawn);

        // Step 4: Stencil pop — decrement stencil back to previous level
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);
    }

    // Restore color/depth after portal loop
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
#endif
}

bool IrrlichtRenderer::rebuildRegionMesh(size_t regionIdx) {
    auto rebuildStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: BEGIN region {} | currentZone_={} wldLoader={} "
             "constrainedMeshCache_={} meshCacheLoaded={} regionMeshNodes_.count={}",
             regionIdx,
             currentZone_ != nullptr,
             (currentZone_ && currentZone_->wldLoader) ? true : false,
             constrainedMeshCache_ != nullptr,
             (constrainedMeshCache_ ? constrainedMeshCache_->isLoaded(regionIdx) : false),
             regionMeshNodes_.count(regionIdx));

    if (!currentZone_ || !currentZone_->wldLoader) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — currentZone_={} wldLoader={}",
                 regionIdx, currentZone_ != nullptr,
                 (currentZone_ ? (currentZone_->wldLoader != nullptr) : false));
        return false;
    }

    // Clean up existing node if rebuilding (prevents scene node leak)
    auto existingIt = regionMeshNodes_.find(regionIdx);
    if (existingIt != regionMeshNodes_.end() && existingIt->second) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: region {} already has node (parent={}), cleaning up",
                 regionIdx, existingIt->second->getParent() != nullptr);
        auto* oldNode = existingIt->second;
        deleteMeshHardwareBuffers(oldNode);
        if (animatedTextureManager_)
            animatedTextureManager_->removeSceneNode(oldNode);
        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
        existingIt->second = nullptr;
    }

    auto geom = currentZone_->wldLoader->getGeometryForRegion(regionIdx);
    if (!geom || geom->vertices.empty()) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — getGeometryForRegion returned {} "
                 "(vertices={})",
                 regionIdx, geom ? "geom" : "nullptr",
                 geom ? geom->vertices.size() : 0);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} geom OK — verts={} tris={} texNames={} "
             "center=({:.1f},{:.1f},{:.1f})",
             regionIdx, geom->vertices.size(), geom->triangles.size(),
             geom->textureNames().size(),
             geom->centerX, geom->centerY, geom->centerZ);

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
    // Only use constrained cache at runtime — during sequential loading, the cache's
    // async background decode submits to bgThreadPool_ which may not be initialized,
    // causing pthread_mutex_lock(NULL) crashes.
    if (constrainedTextureCache_ && !isLoading())
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());

    bool shaderAvail = zoneShader_ && zoneShader_->isAvailable();
    bool atlasAvail = zoneShader_ && zoneShader_->isAtlasAvailable();
    if (shaderAvail)
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest());
    if (atlasAvail)
        builder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                             zoneShader_->getActiveAtlasAlpha());

    bool hasTextures = !currentZone_->textures.empty();
    bool hasTexNames = !geom->textureNames().empty();
    bool hasAtlas = zoneAtlas_ && zoneAtlas_->isLoaded();

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} build config — shader={} atlas={} "
             "hasTextures={} (count={}) hasTexNames={} hasAtlas={} activeSolid={} activeAlpha={}",
             regionIdx, shaderAvail, atlasAvail,
             hasTextures, currentZone_->textures.size(),
             hasTexNames, hasAtlas,
             shaderAvail ? zoneShader_->getActiveSolid() : -1,
             shaderAvail ? zoneShader_->getActiveAlphaTest() : -1);

    irr::scene::IMesh* mesh = nullptr;
    const char* buildPath = "none";
    auto meshBuildStart = std::chrono::steady_clock::now();

    if (hasTextures && hasTexNames) {
        if (hasAtlas && atlasAvail) {
            buildPath = "buildAtlasedMesh";
            mesh = builder.buildAtlasedMesh(*geom, currentZone_->textures, *zoneAtlas_);
        } else {
            buildPath = "buildTexturedMesh";
            mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
        }
    } else {
        buildPath = "buildColoredMesh";
        mesh = builder.buildColoredMesh(*geom);
    }

    auto meshBuildEnd = std::chrono::steady_clock::now();
    auto meshBuildMs = std::chrono::duration_cast<std::chrono::microseconds>(
        meshBuildEnd - meshBuildStart).count() / 1000.0f;

    if (!mesh) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — {} returned null "
                  "(took {:.1f}ms) | textures.size={} texNames.size={}",
                  regionIdx, buildPath, meshBuildMs,
                  currentZone_->textures.size(), geom->textureNames().size());
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} mesh built via {} in {:.1f}ms — "
             "buffers={}", regionIdx, buildPath, meshBuildMs, mesh->getMeshBufferCount());

    // Track textures that were null during build (async GPU upload still pending).
    // When these textures complete upload, this region will be queued for rebuild.
    const auto& missing = builder.getMissingTextures();
    if (!missing.empty()) {
        for (const auto& texName : missing) {
            pendingTextureRegions_[texName].insert(regionIdx);
        }
        LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} has {} missing textures (async pending), "
                 "will rebuild when uploads complete", regionIdx, missing.size());
    }

    auto* node = smgr_->addMeshSceneNode(mesh);
    if (!node) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — addMeshSceneNode returned null",
                  regionIdx);
        mesh->drop();
        return false;
    }

    // Apply standard zone region materials
    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = lightingEnabled_;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).GouraudShading = true;
        node->getMaterial(i).FogEnable = fogEnabled_;
        node->getMaterial(i).Wireframe = wireframeMode_;
        node->getMaterial(i).NormalizeNormals = true;
        node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
        node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
    }
    mesh->drop();

    // Force AbsoluteTransformation update after setPosition — Irrlicht only updates
    // this in OnAnimate() for visible nodes. Since we immediately set the node invisible
    // (below), OnAnimate() will skip it and the AbsoluteTransformation would remain at
    // identity (from the constructor). Triangle selectors created by addRegionToCollision()
    // use getAbsoluteTransformation() to transform collision triangles, so a stale identity
    // transform would place them at center-relative positions instead of world-space.
    node->updateAbsolutePosition();

    // Start invisible — PVS will make it visible on the next Tier2 frame if appropriate.
    // Without this, newly-built nodes are visible by default in Irrlicht, and if PVS
    // doesn't run before the next render, they add to the polygon count unchecked.
    node->setVisible(false);

    // When manual draw is active, remove from scene graph entirely — the manual draw
    // path accesses nodes directly via regionMeshNodes_ map using getPosition()/getMesh().
    if (manualZoneDrawEnabled_) {
        node->grab();
        node->remove();
    }

    // Update renderer state
    regionMeshNodes_[regionIdx] = node;

    // Upload static VBOs for zone geometry (GLES2 only)
    if (!config_.constrainedConfig.skipVBOUpload) {
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_ && gpuUploadEnabled_ && gpuUploadThread_->isAvailable() &&
            node->getMesh() && node->getMesh()->getMeshBufferCount() > 0) {
            // Submit VBO uploads asynchronously — mesh renders via software vertex
            // path (client-side arrays) until each buffer's upload completes.
            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0)
                    continue;

                UploadRequest req;
                req.type = UploadRequestType::VertexBuffer;
                req.vertexCount = buf->getVertexCount();
                req.indexCount = buf->getIndexCount();

                switch (buf->getVertexType()) {
                    case irr::video::EVT_STANDARD:  req.vertexStride = 36; break;
                    case irr::video::EVT_2TCOORDS:  req.vertexStride = 44; break;
                    case irr::video::EVT_TANGENTS:  req.vertexStride = 60; break;
                    default: req.vertexStride = 36; break;
                }

                size_t vboSize = req.vertexCount * req.vertexStride;
                req.vertexData.resize(vboSize);
                std::memcpy(req.vertexData.data(), buf->getVertices(), vboSize);

                req.indexData.resize(req.indexCount);
                std::memcpy(req.indexData.data(), buf->getIndices(),
                            req.indexCount * sizeof(uint16_t));

                // Encode regionIdx (low 48 bits) + buffer index (high 16 bits)
                req.callbackKey = (static_cast<uint64_t>(i) << 48) |
                                  (static_cast<uint64_t>(regionIdx) & 0xFFFFFFFFFFFFULL);
                req.priority = WorkPriorityKey::make(getRegionPvsDepth(regionIdx), AssetType::ZoneMesh).value;

                gpuUploadThread_->submit(std::move(req));
            }
            pendingVBOUploads_.insert(regionIdx);
        } else {
            uploadMeshHardwareBuffers(node);
        }
#else
        uploadMeshHardwareBuffers(node);
#endif
    }

    // Register with animated texture manager
    if (animatedTextureManager_)
        animatedTextureManager_->addSceneNode(node);

    // Pre-evict to make room, then update cache
    size_t meshSize = ConstrainedMeshCache::estimateMeshSize(node);
    auto evicted = constrainedMeshCache_->evictUntilAvailable(meshSize, protectedRegions_);
    for (size_t idx : evicted) {
        auto it = regionMeshNodes_.find(idx);
        if (it != regionMeshNodes_.end() && it->second) {
            deleteMeshHardwareBuffers(it->second);
            if (animatedTextureManager_)
                animatedTextureManager_->removeSceneNode(it->second);
            if (it->second->getParent()) it->second->remove(); else it->second->drop();
            it->second = nullptr;
        }
        for (auto& [texName, regions] : pendingTextureRegions_) {
            regions.erase(idx);
        }
    }
    constrainedMeshCache_->onLoaded(regionIdx, node, meshSize);

    auto rebuildEnd = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::microseconds>(
        rebuildEnd - rebuildStart).count() / 1000.0f;

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: SUCCESS region {} — {} bytes, {:.1f}ms total "
             "(mesh build {:.1f}ms) | cache usage {}/{} bytes, {} loaded",
             regionIdx, meshSize, totalMs, meshBuildMs,
             constrainedMeshCache_->getCurrentUsage(),
             constrainedMeshCache_->getMemoryLimit(),
             constrainedMeshCache_->getLoadedCount());
    return true;
}

void IrrlichtRenderer::processFrameLazyLoad() {
    if (!constrainedMeshCache_ || !currentZone_ || !currentZone_->wldLoader) {
        static int lazyLoadSkipLog = 0;
        if (++lazyLoadSkipLog % 300 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: SKIP — cache={} zone={} wld={}",
                      constrainedMeshCache_ != nullptr, currentZone_ != nullptr,
                      (currentZone_ ? (currentZone_->wldLoader != nullptr) : false));
        }
        return;
    }

    // GREEN-only: max 1 region build per frame to stay within budget.
    if (governor_ && governor_->getState() != BudgetState::Green) {
        static int lazyLoadGovLog = 0;
        if (++lazyLoadGovLog % 300 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: SKIP — governor state={} (not GREEN) | "
                      "meshLoadQueue_.size={}", governor_->getStateName(),
                      meshLoadQueue_.size());
        }
        return;
    }

    // Log queue state before scanning
    size_t queueSize = meshLoadQueue_.size();
    size_t alreadyLoaded = 0;
    size_t needBuild = 0;
    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx))
            alreadyLoaded++;
        else
            needBuild++;
    }

    static int lazyLoadQueueLog = 0;
    if (needBuild > 0 || ++lazyLoadQueueLog % 300 == 1) {
        LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: queue={} (loaded={} needBuild={}) | "
                  "cache usage {}/{} bytes, {} loaded total",
                  queueSize, alreadyLoaded, needBuild,
                  constrainedMeshCache_->getCurrentUsage(),
                  constrainedMeshCache_->getMemoryLimit(),
                  constrainedMeshCache_->getLoadedCount());
    }

    bool builtRegion = false;
    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
        LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: attempting region {} (first unloaded in queue)",
                 entry.regionIdx);
        if (rebuildRegionMesh(entry.regionIdx)) {
            LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: SUCCESS region {} | cache usage {}/{} bytes",
                entry.regionIdx,
                constrainedMeshCache_->getCurrentUsage(),
                constrainedMeshCache_->getMemoryLimit());
            sendLoadProgress(fmt::format("[LazyLoad] Region {} [{}/{}]",
                entry.regionIdx, alreadyLoaded + 1, queueSize));
            builtRegion = true;
        } else {
            LOG_ERROR(MOD_GRAPHICS, "processFrameLazyLoad: FAILED region {} — rebuildRegionMesh returned false",
                      entry.regionIdx);
        }
        break;  // One region max per frame
    }

    // Rebuild one region whose async fallback textures have arrived (if no region was built this frame)
    if (!builtRegion && !textureRebuildQueue_.empty()) {
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
            LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: rebuilt region {} with textures (queue remaining: {})",
                     texEntry.regionIdx, textureRebuildQueue_.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "processFrameLazyLoad: FAILED to rebuild region {} with textures",
                     texEntry.regionIdx);
        }
    }

    // Evict with per-frame cap
    if (constrainedMeshCache_->getCurrentUsage() > constrainedMeshCache_->getMemoryLimit()) {
        constexpr int MAX_EVICTIONS_PER_FRAME = 10;
        int evictionsThisFrame = 0;
        auto evicted = constrainedMeshCache_->evictUntilAvailable(0, protectedRegions_);
        for (size_t idx : evicted) {
            if (evictionsThisFrame >= MAX_EVICTIONS_PER_FRAME) break;
            auto it = regionMeshNodes_.find(idx);
            if (it != regionMeshNodes_.end() && it->second) {
                deleteMeshHardwareBuffers(it->second);
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(it->second);
                if (it->second->getParent()) it->second->remove(); else it->second->drop();
                it->second = nullptr;
            }
            // Remove evicted region from texture rebuild tracking
            for (auto& [texName, regions] : pendingTextureRegions_) {
                regions.erase(idx);
            }
            textureRebuildQueue_.erase(
                std::remove_if(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                    [idx](const TextureRebuildEntry& e) { return e.regionIdx == idx; }),
                textureRebuildQueue_.end());
            evictionsThisFrame++;
        }
    }
}

// ---------------------------------------------------------------------------
// Background mesh build: drain, finalize, material swap, submit
// ---------------------------------------------------------------------------

void IrrlichtRenderer::drainMeshResults() {
    std::lock_guard<std::mutex> lock(meshResultMutex_);
    if (meshResultQueue_.empty()) return;
    for (auto& r : meshResultQueue_)
        localMeshResults_.push_back(std::move(r));
    meshResultQueue_.clear();
}

bool IrrlichtRenderer::finalizeOneMeshResult() {
    if (localMeshResults_.empty()) return false;

    auto result = std::move(localMeshResults_.front());
    localMeshResults_.erase(localMeshResults_.begin());
    pendingMeshBuilds_.erase(result.regionIdx);

    // Discard stale results from a different zone
    if (result.zoneName != currentZoneName_) {
        if (result.mesh) result.mesh->drop();
        return true;  // consumed work (discarded)
    }

    // Clean up existing node if present
    auto existingIt = regionMeshNodes_.find(result.regionIdx);
    if (existingIt != regionMeshNodes_.end() && existingIt->second) {
        auto* oldNode = existingIt->second;
        deleteMeshHardwareBuffers(oldNode);
        if (animatedTextureManager_)
            animatedTextureManager_->removeSceneNode(oldNode);
        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
        existingIt->second = nullptr;
    }

    if (!result.mesh) return true;

    auto* node = smgr_->addMeshSceneNode(result.mesh);
    if (!node) {
        result.mesh->drop();
        return true;
    }

    // Apply standard zone region materials
    node->setPosition(irr::core::vector3df(result.centerX, result.centerZ, result.centerY));
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = lightingEnabled_;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).GouraudShading = true;
        node->getMaterial(i).FogEnable = fogEnabled_;
        node->getMaterial(i).Wireframe = wireframeMode_;
        node->getMaterial(i).NormalizeNormals = true;
        node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
        node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
    }
    result.mesh->drop();

    node->updateAbsolutePosition();
    node->setVisible(false);

    if (manualZoneDrawEnabled_) {
        node->grab();
        node->remove();
    }

    regionMeshNodes_[result.regionIdx] = node;

    // Assign textures to fallback buffers (placeholder or real if already in cache)
    auto* placeholder = constrainedTextureCache_ ? constrainedTextureCache_->getPlaceholderTexture() : nullptr;
    for (const auto& fb : result.fallbackBuffers) {
        if (fb.bufferIndex >= node->getMesh()->getMeshBufferCount()) continue;
        auto* buf = node->getMesh()->getMeshBuffer(fb.bufferIndex);
        if (!buf) continue;

        auto* tex = constrainedTextureCache_ ? constrainedTextureCache_->getTexture(fb.textureName) : nullptr;
        if (tex) {
            buf->getMaterial().setTexture(0, tex);
            bool hasAlpha = constrainedTextureCache_->hasAlpha(fb.textureName);
            if (hasAlpha && zoneShader_ && zoneShader_->getActiveAlphaTest() >= 0) {
                buf->getMaterial().MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                    zoneShader_->getActiveAlphaTest());
            }
        } else {
            if (placeholder) {
                buf->getMaterial().setTexture(0, placeholder);
            }
            pendingTextureBuffers_[result.regionIdx][fb.textureName].push_back(fb.bufferIndex);
        }
    }

    // Track missing textures for arrival notification
    for (const auto& texName : result.missingTextures) {
        pendingTextureRegions_[texName].insert(result.regionIdx);
    }

    // Upload static VBOs (GLES2 only)
    if (!config_.constrainedConfig.skipVBOUpload) {
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_ && gpuUploadEnabled_ && gpuUploadThread_->isAvailable() &&
            node->getMesh() && node->getMesh()->getMeshBufferCount() > 0) {
            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0) continue;

                UploadRequest req;
                req.type = UploadRequestType::VertexBuffer;
                req.vertexCount = buf->getVertexCount();
                req.indexCount = buf->getIndexCount();

                switch (buf->getVertexType()) {
                    case irr::video::EVT_STANDARD:  req.vertexStride = 36; break;
                    case irr::video::EVT_2TCOORDS:  req.vertexStride = 44; break;
                    case irr::video::EVT_TANGENTS:  req.vertexStride = 60; break;
                    default: req.vertexStride = 36; break;
                }

                size_t vboSize = req.vertexCount * req.vertexStride;
                req.vertexData.resize(vboSize);
                std::memcpy(req.vertexData.data(), buf->getVertices(), vboSize);
                req.indexData.resize(req.indexCount);
                std::memcpy(req.indexData.data(), buf->getIndices(), req.indexCount * sizeof(uint16_t));

                req.callbackKey = (static_cast<uint64_t>(i) << 48) |
                                  (static_cast<uint64_t>(result.regionIdx) & 0xFFFFFFFFFFFFULL);
                req.priority = WorkPriorityKey::make(getRegionPvsDepth(result.regionIdx), AssetType::ZoneMesh).value;
                gpuUploadThread_->submit(std::move(req));
            }
            pendingVBOUploads_.insert(result.regionIdx);
        } else {
            uploadMeshHardwareBuffers(node);
        }
#else
        uploadMeshHardwareBuffers(node);
#endif
    }

    // Register with animated texture manager
    if (animatedTextureManager_)
        animatedTextureManager_->addSceneNode(node);

    // Mesh cache bookkeeping (evict + track)
    if (constrainedMeshCache_) {
        size_t meshSize = ConstrainedMeshCache::estimateMeshSize(node);
        auto evicted = constrainedMeshCache_->evictUntilAvailable(meshSize, protectedRegions_);
        for (size_t idx : evicted) {
            auto it = regionMeshNodes_.find(idx);
            if (it != regionMeshNodes_.end() && it->second) {
                deleteMeshHardwareBuffers(it->second);
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(it->second);
                if (it->second->getParent()) it->second->remove(); else it->second->drop();
                it->second = nullptr;
            }
            pendingTextureBuffers_.erase(idx);
            pendingMeshBuilds_.erase(idx);
            for (auto& [texName, regions] : pendingTextureRegions_)
                regions.erase(idx);
        }
        constrainedMeshCache_->onLoaded(result.regionIdx, node, meshSize);
    }

    LOG_INFO(MOD_GRAPHICS, "finalizeOneMeshResult: region {} finalized ({:.1f}ms bg build)",
             result.regionIdx, result.buildTimeMs);
    return true;
}

void IrrlichtRenderer::updateRegionMaterials(size_t regionIdx, const std::string& textureName,
                                              irr::video::ITexture* texture, bool hasAlpha) {
    auto ptbIt = pendingTextureBuffers_.find(regionIdx);
    if (ptbIt == pendingTextureBuffers_.end()) return;

    auto bufIt = ptbIt->second.find(textureName);
    if (bufIt == ptbIt->second.end()) return;

    auto nodeIt = regionMeshNodes_.find(regionIdx);
    if (nodeIt == regionMeshNodes_.end() || !nodeIt->second) {
        ptbIt->second.erase(bufIt);
        if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
        return;
    }

    auto* mesh = nodeIt->second->getMesh();
    if (!mesh) {
        ptbIt->second.erase(bufIt);
        if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
        return;
    }

    for (irr::u32 bufferIdx : bufIt->second) {
        if (bufferIdx >= mesh->getMeshBufferCount()) continue;
        auto* buf = mesh->getMeshBuffer(bufferIdx);
        if (!buf) continue;

        buf->getMaterial().setTexture(0, texture);
        if (hasAlpha && zoneShader_ && zoneShader_->getActiveAlphaTest() >= 0) {
            buf->getMaterial().MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                zoneShader_->getActiveAlphaTest());
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "updateRegionMaterials: region {} texture '{}' → {} buffers",
              regionIdx, textureName, bufIt->second.size());

    ptbIt->second.erase(bufIt);
    if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
}

bool IrrlichtRenderer::processOneMaterialSwap() {
    if (materialSwapQueue_.empty()) return false;

    auto entry = std::move(materialSwapQueue_.front());
    materialSwapQueue_.erase(materialSwapQueue_.begin());
    updateRegionMaterials(entry.regionIdx, entry.textureName, entry.texture, entry.hasAlpha);
    return true;
}

bool IrrlichtRenderer::submitOneBgMeshBuild() {
    if (!constrainedMeshCache_ || !backgroundThreadPool_ || !currentZone_) return false;
    if (!zoneAtlas_ || !zoneAtlas_->isLoaded()) return false;

    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
        if (pendingMeshBuilds_.count(entry.regionIdx)) continue;

        auto zone = currentZone_;
        auto atlas = zoneAtlas_;
        auto zoneName = currentZoneName_;
        size_t regionIdx = entry.regionIdx;
        int shaderSolid = zoneShader_ ? zoneShader_->getActiveSolid() : -1;
        int shaderAlpha = zoneShader_ ? zoneShader_->getActiveAlphaTest() : -1;
        int shaderAtlasSolid = zoneShader_ ? zoneShader_->getActiveAtlasSolid() : -1;
        int shaderAtlasAlpha = zoneShader_ ? zoneShader_->getActiveAtlasAlpha() : -1;

        pendingMeshBuilds_.insert(regionIdx);
        uint32_t priority = WorkPriorityKey::make(
            getRegionPvsDepth(regionIdx), AssetType::ZoneMesh).value;

        backgroundThreadPool_->submit(priority, [=, this]() {
            auto geom = zone->wldLoader->getGeometryForRegion(regionIdx);
            if (!geom || geom->vertices.empty()) {
                std::lock_guard<std::mutex> lock(meshResultMutex_);
                meshResultQueue_.push_back({regionIdx, zoneName, nullptr, {}, {}, 0, 0, 0, 0});
                return;
            }

            ZoneMeshBuilder builder(nullptr, nullptr, nullptr);
            builder.setShaderMaterialTypes(shaderSolid, shaderAlpha);
            builder.setAtlasShaderMaterialTypes(shaderAtlasSolid, shaderAtlasAlpha);

            auto start = std::chrono::steady_clock::now();
            auto* mesh = builder.buildAtlasedMesh(
                *geom, zone->textures, *atlas, 0, /*deferTextures=*/true);
            float elapsed = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            BackgroundMeshResult r;
            r.regionIdx = regionIdx;
            r.zoneName = zoneName;
            r.mesh = static_cast<irr::scene::SMesh*>(mesh);
            r.fallbackBuffers = builder.getFallbackBufferMap();
            r.missingTextures = {builder.getMissingTextures().begin(), builder.getMissingTextures().end()};
            r.centerX = geom->centerX;
            r.centerY = geom->centerY;
            r.centerZ = geom->centerZ;
            r.buildTimeMs = elapsed;

            std::lock_guard<std::mutex> lock(meshResultMutex_);
            meshResultQueue_.push_back(std::move(r));
        });

        return true;  // consumed 1 unit of work
    }
    return false;
}

void IrrlichtRenderer::indexObjectMeshes() {
    if (!currentZone_) {
        return;
    }

    deferredObjects_.clear();

    for (size_t i = 0; i < currentZone_->objects.size(); ++i) {
        const auto& objInstance = currentZone_->objects[i];
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        // Skip trees on software path (handled by tree manager)
        // On GPU path, trees are indexed normally for PVS/distance culling
        if (treeManager_) {
            const std::string& objName = objInstance.placeable->getName();
            std::string primaryTexture;
            if (!objInstance.geometry->textureNames().empty()) {
                primaryTexture = objInstance.geometry->textureNames()[0];
            }
            if (treeManager_->isTreeObject(objName, primaryTexture)) {
                bool hasGpuShaders = (driver_->getDriverType() != irr::video::EDT_BURNINGSVIDEO);
                if (!hasGpuShaders) {
                    continue;
                }
            }
        }

        DeferredObject deferred;
        deferred.objectIndex = i;
        deferred.meshBuilt = false;

        float x = objInstance.placeable->getX();
        float y = objInstance.placeable->getY();
        float z = objInstance.placeable->getZ();

        // Compute BSP region
        if (zoneBspTree_) {
            deferred.bspRegion = zoneBspTree_->findRegionIndexForPoint(x, y, z);
        }

        // Estimate world bounds from geometry extents
        const auto& geom = objInstance.geometry;
        float scaleX = objInstance.placeable->getScaleX();
        float scaleY = objInstance.placeable->getScaleY();
        float scaleZ = objInstance.placeable->getScaleZ();
        // Approximate bounding box in Irrlicht coords (x, z, y)
        float halfW = std::max(std::abs(geom->maxX - geom->minX), std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
        float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
        deferred.worldBounds = irr::core::aabbox3df(
            x - halfW, z - halfH, y - halfW,
            x + halfW, z + halfH, y + halfW);

        deferredObjects_.push_back(deferred);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Indexed {} deferred objects", deferredObjects_.size());
}

void IrrlichtRenderer::buildDeferredObject(size_t idx) {
    if (idx >= deferredObjects_.size() || !currentZone_ || !smgr_) {
        return;
    }

    DeferredObject& deferred = deferredObjects_[idx];
    if (deferred.meshBuilt) {
        return;
    }

    if (deferred.objectIndex >= currentZone_->objects.size()) {
        return;
    }

    const auto& objInstance = currentZone_->objects[deferred.objectIndex];
    if (!objInstance.geometry || !objInstance.placeable) {
        deferred.meshBuilt = true;
        return;
    }

    const std::string& objName = objInstance.placeable->getName();

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());
    }
    if (zoneShader_ && zoneShader_->isAvailable()) {
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest());
    }
    if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
        builder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                             zoneShader_->getActiveAtlasAlpha());
    }

    bool useObjAtlas = objAtlas_ && objAtlas_->isLoaded() &&
                       zoneShader_ && zoneShader_->isAtlasAvailable();

    irr::scene::IMesh* mesh = nullptr;
    if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
        if (useObjAtlas) {
            mesh = builder.buildAtlasedMesh(*objInstance.geometry, currentZone_->objectTextures,
                                             *objAtlas_, objAtlasPageOffset_);
        } else {
            mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
        }
    } else {
        mesh = builder.buildColoredMesh(*objInstance.geometry);
    }

    // Track textures that were missing (async pending) for rebuild when they arrive
    const auto& missingObjTex = builder.getMissingTextures();
    if (!missingObjTex.empty()) {
        for (const auto& texName : missingObjTex) {
            pendingTextureObjects_[texName].insert(idx);
        }
    }

    if (!mesh) {
        deferred.meshBuilt = true;
        return;
    }

    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    if (!node) {
        mesh->drop();
        deferred.meshBuilt = true;
        return;
    }

    float scaleX = objInstance.placeable->getScaleX();
    float scaleY = objInstance.placeable->getScaleY();
    float scaleZ = objInstance.placeable->getScaleZ();
    node->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

    float x = objInstance.placeable->getX();
    float y = objInstance.placeable->getY();
    float z = objInstance.placeable->getZ();
    node->setPosition(irr::core::vector3df(x, z, y));

    float rotX = objInstance.placeable->getRotateX();
    float rotY = objInstance.placeable->getRotateY();
    float rotZ = objInstance.placeable->getRotateZ();
    node->setRotation(irr::core::vector3df(rotX, rotY, rotZ));

    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = lightingEnabled_;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).GouraudShading = true;
        node->getMaterial(i).FogEnable = fogEnabled_;
        node->getMaterial(i).Wireframe = wireframeMode_;
        node->getMaterial(i).NormalizeNormals = true;
        node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
        node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
    }

    // Apply wind shader to tree meshes on GPU path (deferred build)
    if (treeManager_ && zoneShader_ && zoneShader_->isWindAvailable()) {
        std::string primaryTexture;
        if (!objInstance.geometry->textureNames().empty()) {
            primaryTexture = objInstance.geometry->textureNames()[0];
        }
        if (treeManager_->isTreeObject(objName, primaryTexture)) {
            irr::core::aabbox3df meshBbox = mesh->getBoundingBox();
            float meshMinY = meshBbox.MinEdge.Y;
            float meshMaxY = meshBbox.MaxEdge.Y;
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                    zoneShader_->getActiveWindAlphaTest());
                node->getMaterial(i).MaterialTypeParam = meshMinY;
                node->getMaterial(i).MaterialTypeParam2 = meshMaxY;
                node->getMaterial(i).BackfaceCulling = false;
            }
        }
    }

    if (animatedTextureManager_ && objInstance.geometry) {
        animatedTextureManager_->addMesh(*objInstance.geometry, currentZone_->objectTextures, mesh);
        animatedTextureManager_->addSceneNode(node);
    }

    node->setName(objName.c_str());
    node->grab();
    size_t newNodeIndex = objectNodes_.size();
    objectNodes_.push_back(node);
    objectPositions_.push_back(irr::core::vector3df(x, z, y));
    if (zoneBspTree_) {
        objectRegions_.push_back(zoneBspTree_->findRegionIndexForPoint(x, y, z));
    } else {
        objectRegions_.push_back(SIZE_MAX);
    }
    node->updateAbsolutePosition();
    objectBoundingBoxes_.push_back(node->getTransformedBoundingBox());

    // Track deferred→node mapping for texture rebuild
    deferred.nodeIndex = newNodeIndex;

    // PVS check at insertion — start hidden if not in visible region
    bool pvsVisible = isRegionPvsVisible(objectRegions_.back());
    if (!pvsVisible) {
        node->remove();  // Remove from scene graph (grab() keeps it alive)
    }
    objectInSceneGraph_.push_back(pvsVisible);

    // Create object light if this is a light source (torch, lantern, etc.)
    // Create object light if this is a light source (torch, lantern, etc.)
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
                upperTex.find("TORCH") != std::string::npos) {
                hasFireTexture = true;
            }
            if (upperTex.find("LANTERN") != std::string::npos ||
                upperTex.find("LANT") != std::string::npos) {
                hasLanternTexture = true;
            }
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
        irr::core::vector3df lightPos(x, z, y);

        // Try to find a nearby zone light with the correct elevated position
        if (currentZone_ && !currentZone_->lights.empty()) {
            float bestDist = 50.0f;
            for (const auto& zoneLight : currentZone_->lights) {
                float dx = zoneLight->x - x;
                float dy = zoneLight->y - y;
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

            // grab() + remove() pattern: keep node alive but out of scene graph
            lightNode->grab();
            lightNode->remove();

            ObjectLight objLight;
            objLight.node = lightNode;
            objLight.position = lightPos;
            objLight.objectName = objName;
            objLight.originalColor = lightColor;
            objLight.bspRegion = objectRegions_.back();  // Same BSP region as parent object

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

            // Initial PVS check — only add to scene if region is visible
            bool pvsVis = isRegionPvsVisible(objLight.bspRegion);
            if (pvsVis) {
                smgr_->getRootSceneNode()->addChild(lightNode);
            }
            objectLightInSceneGraph_.push_back(pvsVis);

            objectLights_.push_back(objLight);
            LOG_DEBUG(MOD_GRAPHICS, "Deferred object light created: {} at ({:.1f},{:.1f},{:.1f}) region={}",
                objName, lightPos.X, lightPos.Y, lightPos.Z, objLight.bspRegion);
        }
    }

    mesh->drop();
    deferred.meshBuilt = true;
}

bool IrrlichtRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                     float x, float y, float z, float heading, bool isPlayer,
                                     uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                     bool isCorpse, float serverSize) {
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->createEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse, serverSize);

    // If this is the player entity, handle visibility based on current mode
    if (result && isPlayer) {
        // Set player race for vision-based lighting
        setPlayerRace(raceId);

        // In FirstPerson camera, hide the player entity
        bool shouldHide = (cameraMode_ == CameraMode::FirstPerson);
        entityRenderer_->setPlayerEntityVisible(!shouldHide);

        // Player entity creation is the final step - mark zone as ready
        // This happens after all network packets are processed and other entities are loaded
        if (networkReady_) {
            setLoadingProgress(1.0f, L"Zone ready!");
            zoneReady_ = true;
            LOG_INFO(MOD_GRAPHICS, "Zone ready - player entity created and camera initialized");
        }
    }

    // Track entity loading for loading screen progress
    if (result) {
        loadedEntityCount_++;
        LOG_TRACE(MOD_GRAPHICS, "Entity created: {} (ID: {}), loaded count: {}", name, spawnId, loadedEntityCount_);
    }

    return result;
}

bool IrrlichtRenderer::registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                       float x, float y, float z, float heading, bool isPlayer,
                                       uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                       bool isCorpse, float serverSize, uint8_t entityLevel) {
    if (isLoading()) return false;
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->registerEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse, serverSize, entityLevel);

    if (result) {
        loadedEntityCount_++;
        // Re-enable progressive loading so the multi-frame pipeline picks up
        // this entity (progressiveLoadingActive_ may have been set to false
        // after initial zone load completed all entity builds)
        if (!progressiveLoadingActive_ && entityPrepReady_) {
            progressiveLoadingActive_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "Progressive loading re-enabled for post-load entity: {} ({})",
                      name, spawnId);
        }
    }

    return result;
}

void IrrlichtRenderer::updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                                     float dx, float dy, float dz, uint32_t animation) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->updateEntity(spawnId, x, y, z, heading, dx, dy, dz, animation);
    }
}

void IrrlichtRenderer::removeEntity(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->removeEntity(spawnId);
    }
}

void IrrlichtRenderer::startCorpseDecay(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->startCorpseDecay(spawnId);
    }
}

void IrrlichtRenderer::setEntityLight(uint16_t spawnId, uint8_t lightLevel) {
    if (isLoading()) return;
    // Handle player light specially - it's always highest priority in light pool
    if (spawnId == playerSpawnId_ && playerSpawnId_ != 0) {
        playerLightLevel_ = lightLevel;

        if (lightLevel == 0) {
            // Remove player light
            if (playerLightNode_) {
                playerLightNode_->remove();
                playerLightNode_ = nullptr;
                LOG_DEBUG(MOD_GRAPHICS, "Removed player light");
            }
            return;
        }

        // Calculate light properties based on level
        // Server sends light TYPE (0-15), convert to level (0-10) for intensity
        uint8_t level = lightsource::TypeToLevel(lightLevel);
        float intensity = level / 10.0f;  // 0-10 scale to 0.0-1.0
        float radius = 20.0f + (level / 10.0f) * 80.0f;  // 20-100 range

        // Warm light color (slightly yellow/orange like torchlight)
        float r = std::min(1.0f, 0.9f + intensity * 0.1f);
        float g = std::min(1.0f, 0.7f + intensity * 0.2f);
        float b = std::min(1.0f, 0.4f + intensity * 0.2f);

        // Position at player location (Irrlicht Y-up coordinates)
        irr::core::vector3df lightPos(playerX_, playerZ_ + 3.0f, playerY_);

        if (playerLightNode_) {
            // Update existing light
            irr::video::SLight& lightData = playerLightNode_->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f);
            lightData.Radius = radius;
            float quad = 19.0f / (radius * radius);
            lightData.Attenuation = irr::core::vector3df(1.0f, 0.0f, quad);
            playerLightNode_->setPosition(lightPos);
        } else if (smgr_) {
            // Create new light
            playerLightNode_ = smgr_->addLightSceneNode(
                nullptr,
                lightPos,
                irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f),
                radius
            );

            if (playerLightNode_) {
                irr::video::SLight& lightData = playerLightNode_->getLightData();
                lightData.Type = irr::video::ELT_POINT;
                // Attenuation: 1/(constant + quadratic*d²)
                // Tuned so attenuation ≈ 5% at the light's radius, enabling an
                // efficient distance-based early-out in the per-pixel FS.
                // quad = 19/(R²) gives 1/(1+19)=5% at d=R, 1/(1+76)=1.3% at d=2R.
                float quad = 19.0f / (radius * radius);
                lightData.Attenuation = irr::core::vector3df(1.0f, 0.0f, quad);
                // Start hidden - updateObjectLights will enable it
                playerLightNode_->setVisible(false);
                LOG_INFO(MOD_GRAPHICS, "Created player light: level={}, radius={:.1f}", lightLevel, radius);
            }
        }
        return;
    }

    // Non-player entity lights
    if (entityRenderer_) {
        entityRenderer_->setEntityLight(spawnId, lightLevel);
    }
}

void IrrlichtRenderer::clearEntities() {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->clearEntities();
    }
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

void IrrlichtRenderer::playEntityDeathAnimation(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        // Mark entity as corpse and play death animation
        entityRenderer_->markEntityAsCorpse(spawnId);
        LOG_DEBUG(MOD_ENTITY, "Entity {} marked as corpse with death animation", spawnId);
    }
}

bool IrrlichtRenderer::setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop, bool playThrough) {
    if (isLoading()) return false;
    if (entityRenderer_) {
        return entityRenderer_->setEntityAnimation(spawnId, animCode, loop, playThrough);
    }
    return false;
}

void IrrlichtRenderer::setEntityPoseState(uint16_t spawnId, EntityPoseState pose) {
    if (isLoading()) return;
    if (entityRenderer_) {
        // Convert from IrrlichtRenderer::EntityPoseState to EntityVisual::PoseState
        EntityVisual::PoseState internalPose;
        switch (pose) {
            case EntityPoseState::Sitting:   internalPose = EntityVisual::PoseState::Sitting; break;
            case EntityPoseState::Crouching: internalPose = EntityVisual::PoseState::Crouching; break;
            case EntityPoseState::Lying:     internalPose = EntityVisual::PoseState::Lying; break;
            default:                         internalPose = EntityVisual::PoseState::Standing; break;
        }
        entityRenderer_->setEntityPoseState(spawnId, internalPose);
    }
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

// ============================================================================
// SimulationWorker Integration
// ============================================================================

SimulationZoneData IrrlichtRenderer::buildSimulationZoneData() {
    SimulationZoneData zoneData;
    zoneData.bspTree = zoneBspTree_;
    zoneData.usePvsCulling = usePvsCulling_;

    // Copy region bounding boxes (EQ Z-up coordinates)
    for (const auto& [regionIdx, bbox] : regionBoundingBoxes_) {
        SimulationZoneData::RegionBounds rb;
        rb.regionIdx = regionIdx;
        rb.minX = bbox.MinEdge.X;
        rb.minY = bbox.MinEdge.Y;
        rb.minZ = bbox.MinEdge.Z;
        rb.maxX = bbox.MaxEdge.X;
        rb.maxY = bbox.MaxEdge.Y;
        rb.maxZ = bbox.MaxEdge.Z;
        zoneData.regionBounds.push_back(rb);
    }

    // Build reverse lookup: regionIdx → index in regionBounds vector
    for (size_t i = 0; i < zoneData.regionBounds.size(); ++i) {
        zoneData.regionBoundsIndex[zoneData.regionBounds[i].regionIdx] = i;
    }

    // Build fallback list (all indices, for no-PVS zones)
    zoneData.allBoundsIndices.resize(zoneData.regionBounds.size());
    for (size_t i = 0; i < zoneData.regionBounds.size(); ++i) {
        zoneData.allBoundsIndices[i] = i;
    }

    // Pre-compute PVS visible region lists per camera region.
    // For each BspRegion with PVS data, extract its visibleRegions bitvector
    // into a compact list of regionBounds indices.
    if (zoneData.usePvsCulling && zoneData.bspTree) {
        const auto& bsp = *zoneData.bspTree;
        for (size_t camRegion = 0; camRegion < bsp.regions.size(); ++camRegion) {
            if (!bsp.regions[camRegion]) continue;
            const auto& pvs = bsp.regions[camRegion]->visibleRegions;
            if (pvs.empty()) continue;

            std::vector<size_t> indices;
            // Walk the bitvector, collect regionBounds indices for visible regions
            for (size_t regionIdx = 0; regionIdx < pvs.size(); ++regionIdx) {
                if (!pvs[regionIdx]) continue;
                auto it = zoneData.regionBoundsIndex.find(regionIdx);
                if (it != zoneData.regionBoundsIndex.end()) {
                    indices.push_back(it->second);
                }
            }
            if (!indices.empty()) {
                zoneData.pvsVisibleBoundsIndices[camRegion] = std::move(indices);
            }
        }
        LOG_INFO(MOD_GRAPHICS, "PVS lookup tables built: {} camera regions with PVS data, {} total region bounds",
                 zoneData.pvsVisibleBoundsIndices.size(), zoneData.regionBounds.size());
    }

    // Copy object data
    zoneData.objects.resize(objectNodes_.size());
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        auto& od = zoneData.objects[i];
        od.hasNode = (objectNodes_[i] != nullptr);
        if (i < objectBoundingBoxes_.size())
            od.boundingBox = objectBoundingBoxes_[i];
        if (i < objectPositions_.size())
            od.position = objectPositions_[i];
        od.bspRegion = (i < objectRegions_.size()) ? objectRegions_[i] : SIZE_MAX;
    }

    // Copy zone light data
    zoneData.zoneLights.resize(zoneLightData_.size());
    for (size_t i = 0; i < zoneLightData_.size(); ++i) {
        auto& zld = zoneData.zoneLights[i];
        if (i < zoneLightPositions_.size())
            zld.position = zoneLightPositions_[i];
        zld.bspRegion = (i < zoneLightRegions_.size()) ? zoneLightRegions_[i] : SIZE_MAX;
    }

    // Copy zone light node data for light selection (from zoneLightData_, no scene nodes)
    zoneData.zoneLightNodes.resize(zoneLightData_.size());
    for (size_t i = 0; i < zoneLightData_.size(); ++i) {
        auto& zlnd = zoneData.zoneLightNodes[i];
        if (i < zoneLightPositions_.size())
            zlnd.position = zoneLightPositions_[i];
        zlnd.diffuseColor = zoneLightData_[i].baseDiffuseColor;
        zlnd.radius = zoneLightData_[i].radius;
        zlnd.attConstant = zoneLightData_[i].attConstant;
        zlnd.attLinear = zoneLightData_[i].attLinear;
        zlnd.attQuadratic = zoneLightData_[i].attQuadratic;
    }

    // Copy zone light animation data (animated torches from WLD)
    if (currentZone_) {
        for (size_t i = 0; i < currentZone_->lights.size() && i < zoneLightData_.size(); ++i) {
            const auto& light = currentZone_->lights[i];
            if (!light->isAnimated()) continue;
            SimulationZoneData::ZoneLightAnimData anim;
            anim.lightIndex = i;
            anim.frameCount = light->frameCount;
            anim.sleepMs = light->sleepMs;
            anim.baseR = light->r;
            anim.baseG = light->g;
            anim.baseB = light->b;
            // Copy per-frame colors
            for (const auto& c : light->colors) {
                anim.frameColors.push_back(c);
            }
            // Copy light levels
            anim.lightLevels = light->lightLevels;
            zoneData.zoneLightAnims.push_back(std::move(anim));
        }
    }

    // Copy object light data
    zoneData.objectLights.resize(objectLights_.size());
    for (size_t i = 0; i < objectLights_.size(); ++i) {
        auto& old = zoneData.objectLights[i];
        old.position = objectLights_[i].position;
        old.originalColor = objectLights_[i].originalColor;
        old.isFireSource = objectLights_[i].isFireSource;
        old.flickerSpeed = objectLights_[i].flickerSpeed;
        old.objectName = objectLights_[i].objectName;
        old.bspRegion = objectLights_[i].bspRegion;
        if (objectLights_[i].node) {
            const auto& ld = objectLights_[i].node->getLightData();
            old.radius = ld.Radius;
            old.attConstant = ld.Attenuation.X;
            old.attLinear = ld.Attenuation.Y;
            old.attQuadratic = ld.Attenuation.Z;
        }
    }

    // Portal system (non-owning const pointer, immutable after zone load)
    if (portalSystem_) {
        zoneData.portalSystem = portalSystem_.get();
    }

    // Portal visibility config
    zoneData.enablePortalVis = config_.constrainedConfig.portalOcclusion;
    zoneData.enableStencilBuffer = config_.constrainedConfig.enableStencilBuffer;
    zoneData.indoorRegions = indoorRegions_;

    // Collision map for entity ground snapping (immutable after zone load)
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        zoneData.hcMap = collisionMap_;
    }

    // Copy occluder data from renderer's occlusion culler (if it has occluders)
    if (occlusionCuller_ && occlusionCuller_->hasOccluders()) {
        zoneData.regionOccluders = occlusionCuller_->getRegionOccludersMap();
        zoneData.occlusionConfig = occlusionCuller_->getConfig();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: copying {} region occluder sets to worker",
                 zoneData.regionOccluders.size());
    }

    return zoneData;
}

void IrrlichtRenderer::startSimulationWorkerEarly() {
    // S05: simulationWorker_ already created in initLoadingScreen()
    if (!simulationWorker_ || simulationWorker_->isRunning()) return;

    SimulationZoneData zoneData = buildSimulationZoneData();
    simulationWorker_->setZoneData(zoneData);
    simulationWorker_->start();

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker started early for zone '{}' (BSP/regions/objects/lights)",
             currentZoneName_);
}

void IrrlichtRenderer::registerSimulationWorkerTreeData() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;
    if (!treeManager_ || treeManager_->getAnimatedTreeCount() == 0) return;

    const auto& trees = treeManager_->getAnimatedTrees();
    std::vector<SimulationZoneData::AnimatedTreeData> treeData(trees.size());
    for (size_t i = 0; i < trees.size(); ++i) {
        auto& td = treeData[i];
        td.worldPosition = trees[i].worldPosition;
        td.meshSeed = trees[i].meshSeed;
        td.buffers.resize(trees[i].buffers.size());
        for (size_t b = 0; b < trees[i].buffers.size(); ++b) {
            td.buffers[b].basePositions = trees[i].buffers[b].basePositions;
            td.buffers[b].vertexHeights = trees[i].buffers[b].vertexHeights;
        }
    }

    simulationWorker_->updateTreeData(std::move(treeData));
    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: registered {} animated trees", trees.size());
}

void IrrlichtRenderer::registerSimulationWorkerVertexAnimData() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;
    if (vertexAnimatedMeshes_.empty()) return;

    std::vector<SimulationZoneData::VertexAnimData> vertAnims(vertexAnimatedMeshes_.size());
    for (size_t i = 0; i < vertexAnimatedMeshes_.size(); ++i) {
        const auto& vam = vertexAnimatedMeshes_[i];
        auto& vad = vertAnims[i];
        if (vam.animData && !vam.animData->frames.empty()) {
            vad.delayMs = vam.animData->delayMs;
            vad.frameCount = vam.animData->frames.size();
            vad.framePositions.resize(vam.animData->frames.size());
            for (size_t f = 0; f < vam.animData->frames.size(); ++f) {
                vad.framePositions[f] = vam.animData->frames[f].positions;
            }
            vad.vertexMapping = vam.vertexMapping;
            vad.centerOffsetX = vam.centerOffsetX;
            vad.centerOffsetY = vam.centerOffsetY;
            vad.centerOffsetZ = vam.centerOffsetZ;
            if (vam.mesh) {
                vad.bufferVertexCounts.resize(vam.mesh->getMeshBufferCount());
                for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                    vad.bufferVertexCounts[b] = vam.mesh->getMeshBuffer(b)->getVertexCount();
                }
            }
        }
    }

    simulationWorker_->updateVertexAnimData(std::move(vertAnims));
    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: registered {} vertex animated meshes", vertexAnimatedMeshes_.size());
}

void IrrlichtRenderer::stopSimulationWorker() {
    if (simulationWorker_ && simulationWorker_->isRunning()) {
        simulationWorker_->stop();
        simulationWorker_->clearZoneData();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker stopped for zone transition");
    }
}

void IrrlichtRenderer::postSimulationInput(float deltaTime) {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;

    SimulationInput input;

    // Camera (Irrlicht Y-up)
    if (camera_) {
        input.cameraPos = camera_->getPosition();
        input.cameraTarget = camera_->getTarget();
    }

    // Camera position in EQ Z-up
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
    } else {
        input.frustumValid = false;
    }

    input.renderDistance = renderDistance_;
    input.loadingActive = loadingScreenVisible_ || loading_.load(std::memory_order_relaxed);

    // Player (EQ Z-up)
    input.playerX = playerX_;
    input.playerY = playerY_;
    input.playerZ = playerZ_;
    input.playerHeading = playerHeading_;

    // Timing
    input.deltaTime = deltaTime;
    input.frameNumber = frameNumber_;

    // Environment
    input.currentHour = currentHour_;
    input.currentMinute = currentMinute_;
    input.timeOfDay = currentHour_ + currentMinute_ / 60.0f;

    // Player light
    input.playerLightLevel = playerLightLevel_;
    if (playerLightNode_ && playerLightLevel_ > 0) {
        const auto& ld = playerLightNode_->getLightData();
        input.playerLightColor = ld.DiffuseColor;
        input.playerLightRadius = ld.Radius;
        input.playerLightAttConstant = ld.Attenuation.X;
        input.playerLightAttLinear = ld.Attenuation.Y;
        input.playerLightAttQuadratic = ld.Attenuation.Z;
    }

    // Vision and weather modifiers (for zone light animation colors)
    input.visionType = static_cast<uint8_t>(currentVision_);
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        input.weatherAmbientModifier = weatherEffects_->getAmbientLightModifier();
    }

    // Tree wind state snapshot
    if (treeManager_ && treeManager_->isEnabled()) {
        const auto& wc = treeManager_->getWindController();
        const auto& cfg = wc.getConfig();
        input.treeWind.enabled = cfg.enabled;
        input.treeWind.time = wc.getTime();
        input.treeWind.baseFrequency = cfg.baseFrequency;
        input.treeWind.baseStrength = cfg.baseStrength;
        input.treeWind.gustFrequency = cfg.gustFrequency;
        input.treeWind.gustStrength = cfg.gustStrength;
        input.treeWind.turbulence = cfg.turbulence;
        input.treeWind.influenceStartHeight = cfg.influenceStartHeight;
        input.treeWind.influenceExponent = cfg.influenceExponent;
        input.treeWind.weatherMultiplier = wc.getWeatherMultiplier();
        auto dir = wc.getWindDirection();
        input.treeWind.windDirX = dir.X;
        input.treeWind.windDirY = dir.Y;
    }

    // Sky state snapshot
    if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
        input.skyEnabled = true;
        input.skyInitialized = true;
        input.skyCloudScrollOffset = skyRenderer_->getCloudScrollOffset();
    }

    // Weather effects state snapshot
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

    // Occlusion camera state
    if (frustumCuller_ && frustumCuller_->isEnabled()) {
        auto& oc = input.occlusionCamera;
        oc.fwdX = frustumCuller_->getFwdX();
        oc.fwdY = frustumCuller_->getFwdY();
        oc.fwdZ = frustumCuller_->getFwdZ();
        oc.rightX = frustumCuller_->getRightX();
        oc.rightY = frustumCuller_->getRightY();
        oc.rightZ = 0;  // Right vector is horizontal in EQ Z-up
        oc.upX = frustumCuller_->getUpX();
        oc.upY = frustumCuller_->getUpY();
        oc.upZ = frustumCuller_->getUpZ();
        oc.fovRadV = frustumCuller_->getFov();
        oc.aspect = frustumCuller_->getAspect();
        oc.enabled = true;
    }

    // Entity snapshots for worker interpolation
    if (entityRenderer_) {
        // Drain pending updates for worker
        auto drained = entityRenderer_->drainPendingUpdates();
        input.entityPendingUpdates.reserve(drained.size());
        for (const auto& d : drained) {
            SimulationInput::EntityPendingUpdate epu;
            epu.spawnId = d.spawnId;
            epu.x = d.x; epu.y = d.y; epu.z = d.z;
            epu.heading = d.heading;
            epu.dx = d.dx; epu.dy = d.dy; epu.dz = d.dz;
            epu.animation = d.animation;
            input.entityPendingUpdates.push_back(epu);
        }

        // Build entity snapshots
        const auto& entities = entityRenderer_->getEntities();
        input.entitySnapshots.reserve(entities.size());
        for (const auto& [spawnId, visual] : entities) {
            if (!visual.sceneNode) continue;
            SimulationInput::EntitySnapshot snap;
            snap.spawnId = spawnId;
            snap.lastX = visual.lastX;
            snap.lastY = visual.lastY;
            snap.lastZ = visual.lastZ;
            snap.velocityX = visual.velocityX;
            snap.velocityY = visual.velocityY;
            snap.velocityZ = visual.velocityZ;
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
            snap.hasVelocity = (std::abs(visual.velocityX) > 0.01f ||
                                std::abs(visual.velocityY) > 0.01f ||
                                std::abs(visual.velocityZ) > 0.01f);
            input.entitySnapshots.push_back(snap);
        }

        // Entity culling config (always active)
        input.entityRenderDistance = config_.constrainedConfig.entityRenderDistance;
        input.maxVisibleEntities = config_.constrainedConfig.maxVisibleEntities;
        input.entityCullingEnabled = true;

        // Name tag visibility config
        input.nameTagDistance = entityRenderer_->getNameTagDistance();
        input.nameTagsVisible = entityRenderer_->areNameTagsVisible();
    }

    // Vertex animation timing
    input.vertAnimDeltaMs = deltaTime * 1000.0f;

    // Particle system input
    if (particleManager_ && zoneReady_) {
        auto& pi = input.particleInput;
        pi.deltaTime = deltaTime;
        if (camera_) {
            auto cp = camera_->getAbsolutePosition();
            pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
        }

        // Ambient color from zone shader
        if (zoneShader_) {
            const float* amb = zoneShader_->ambientColor();
            pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
        }

        // Wind from particle manager's env state
        pi.windDirection = particleManager_->getWindDirection();
        pi.windStrength = particleManager_->getWindStrength();

        // Entity positions/directions resolved last frame
        pi.entityPositions = std::move(pendingParticleEntityPositions_);
        pi.entityDirections = std::move(pendingParticleEntityDirections_);

        // Commands from particle manager
        pi.commands = particleManager_->drainCommands();

        // State flags
        pi.fireEnabled = particleManager_->isUnifiedFireEnabled();
        pi.unifiedRendererInitialized = true;  // If we got here, it's initialized
        pi.poolSize = 0;  // Worker determines pool size from config

        // Weather lights: collect nearby lights for per-particle illumination
        if (particleManager_->isWeatherParticlesActive()) {
            float maxLightDist = 150.0f;
            float maxLightDistSq = maxLightDist * maxLightDist;
            float colorBoost = 2.5f;
            glm::vec3 camIrr = pi.cameraPos;

            for (size_t i = 0; i < zoneLightData_.size() && i < zoneLightPositions_.size(); ++i) {
                const auto& pos = zoneLightPositions_[i];
                float dx = pos.X - camIrr.x, dy = pos.Y - camIrr.y, dz = pos.Z - camIrr.z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq < maxLightDistSq) {
                    const auto& zld = zoneLightData_[i];
                    float radius = zld.radius > 0 ? zld.radius : 30.0f;
                    pi.weatherLights.push_back({
                        glm::vec3(pos.X, pos.Y, pos.Z),
                        radius,
                        glm::vec3(
                            std::min(1.0f, zld.baseDiffuseColor.r * colorBoost),
                            std::min(1.0f, zld.baseDiffuseColor.g * colorBoost),
                            std::min(1.0f, zld.baseDiffuseColor.b * colorBoost)
                        )
                    });
                }
            }

            // Player light
            if (playerLightNode_ && playerLightLevel_ > 0) {
                auto pos = playerLightNode_->getPosition();
                auto& ld = playerLightNode_->getLightData();
                pi.weatherLights.push_back({
                    glm::vec3(pos.X, pos.Y, pos.Z),
                    ld.Radius,
                    glm::vec3(
                        std::min(1.0f, ld.DiffuseColor.r * colorBoost),
                        std::min(1.0f, ld.DiffuseColor.g * colorBoost),
                        std::min(1.0f, ld.DiffuseColor.b * colorBoost)
                    )
                });
            }
        }
    }

    // Boids system input
    if (boidsManager_ && boidsManager_->isEnabled() && zoneReady_) {
        auto& bi = input.boidsInput;
        bi.deltaTime = deltaTime;
        bi.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
        bi.playerHeading = playerHeading_;
        bi.timeOfDay = currentHour_ + currentMinute_ / 60.0f;
        bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        bi.commands = boidsManager_->drainCommands();
        bi.initialized = true;
    }

    // Tumbleweed system input
    if (tumbleweedManager_ && tumbleweedManager_->isEnabled() && zoneReady_) {
        auto& ti = input.tumbleweedInput;
        ti.deltaTime = deltaTime;
        ti.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
        ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        ti.commands = tumbleweedManager_->drainCommands();
        ti.initialized = true;
    }

    // Weather system input
    if (weatherSystem_) {
        auto& wi = input.weatherInput;
        wi.deltaTime = deltaTime;
        wi.commands = weatherSystem_->drainCommands();
        wi.initialized = true;
    }

    // Spell VFX input (desktop GL worker-driven path)
    if (spellVisualFX_ && spellVisualFX_->isWorkerDriven()) {
        auto& svi = input.spellVfxInput;
        svi.deltaTime = deltaTime;
        svi.commands = spellVisualFX_->drainCommands();
        svi.initialized = true;
    }

    // Detail wind/disturbance input
    if (detailManager_ && detailManager_->isEnabled() && zoneReady_) {
        auto& dti = input.detailInput;
        dti.deltaTime = deltaTime;
        // Wind params from detail manager's wind controller and zone config
        const auto& wc = detailManager_->getWindController();
        const auto& wcParams = wc.getParams();
        dti.windStrength = detailManager_->getZoneConfig().windStrength;
        dti.windFrequency = wcParams.frequency;
        dti.gustFrequency = wcParams.gustFrequency;
        dti.gustStrength = wcParams.gustStrength;
        dti.windDirX = wcParams.direction.X;
        dti.windDirY = wcParams.direction.Y;
        // Disturbance params
        const auto& dc = detailManager_->getFoliageDisturbanceConfig();
        dti.disturbanceEnabled = dc.enabled;
        dti.playerRadius = dc.playerRadius;
        dti.playerStrength = dc.playerStrength;
        dti.maxDisplacement = dc.maxDisplacement;
        dti.verticalDipFactor = dc.verticalDipFactor;
        dti.velocityInfluence = dc.velocityInfluence;
        dti.heightExponent = dc.heightExponent;
        dti.recoveryRate = dc.recoveryRate;
        // Player state (Irrlicht Y-up coords)
        dti.playerPosX = playerX_;
        dti.playerPosY = playerZ_;  // EQ Z → Irrlicht Y
        dti.playerPosZ = playerY_;  // EQ Y → Irrlicht Z
        // Compute velocity for disturbance
        static float lastDetailPX = playerX_, lastDetailPY = playerY_;
        if (deltaTime > 0.001f) {
            dti.playerVelX = (playerX_ - lastDetailPX) / deltaTime;
            dti.playerVelZ = (playerY_ - lastDetailPY) / deltaTime;
        }
        lastDetailPX = playerX_;
        lastDetailPY = playerY_;
        dti.playerMoving = (dti.playerVelX * dti.playerVelX + dti.playerVelZ * dti.playerVelZ) > 0.1f;
        dti.commands = detailManager_->drainCommands();
        dti.initialized = true;
    }

    simulationWorker_->postInput(input);
}

void IrrlichtRenderer::applySimulationResults() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;

    const SimulationOutput* results = simulationWorker_->swapAndGetResults();
    if (!results) return;

    applySimulationOutput(*results);
}

void IrrlichtRenderer::applySimulationOutput(const SimulationOutput& resultsRef) {
    const SimulationOutput* results = &resultsRef;

    // Apply PVS region visibility
    if (usePvsCulling_ && !results->regionVisible.empty()) {
        // Build a lookup: index in regionBounds → regionIdx
        // We need to map worker output indices back to actual regionMeshNodes_
        size_t idx = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) { idx++; continue; }
            // Find matching index in worker output
            // Worker output is indexed by position in zoneData_.regionBounds vector
            // which was built from regionBoundingBoxes_ in the same order
            bool visible = false;
            if (idx < results->regionVisible.size()) {
                visible = results->regionVisible[idx] != 0;
            }
            node->setVisible(visible);
            idx++;
        }

        size_t prevPvsRegion = currentPvsRegion_;
        currentPvsRegion_ = results->currentPvsRegion;

        // Point to PVS depth map in front buffer (no copy — stable until next swap)
        regionPvsDepthMap_ = &results->regionPvsDepth;

        // Re-prioritize pending queues only when leaving the PVS neighborhood.
        // Small region crossings within the neighborhood are suppressed to avoid
        // expensive re-prioritization cascades on every boundary crossing.
        if (currentPvsRegion_ != prevPvsRegion && prevPvsRegion != SIZE_MAX) {
            // Camera collision update is cheap and always needed
            if (cameraController_) {
                cameraController_->setBspPlayerRegion(currentPvsRegion_);
            }
            // Full re-prioritization only when leaving neighborhood
            if (!isInPvsNeighborhood(currentPvsRegion_)) {
                rebuildPvsNeighborhood(currentPvsRegion_);
                onPvsRegionChanged();
            }
        }

        // Always log PVS state from worker (throttled to every 30 frames)
        static int simWorkerPvsLogCounter = 0;
        if (++simWorkerPvsLogCounter % 30 == 1 || currentPvsRegion_ != prevPvsRegion) {
            LOG_DEBUG(MOD_GRAPHICS, "SimWorker apply: PVS={}{} meshLoadQ={} protected={} regionVis={}",
                      currentPvsRegion_,
                      (currentPvsRegion_ != prevPvsRegion ? fmt::format(" (was {})", prevPvsRegion) : ""),
                      results->meshLoadQueue.size(), results->protectedRegions.size(),
                      results->regionVisible.size());
        }

        // Point to portal visible regions in front buffer (no copy — stable until next swap)
        // Used for geometry stencil masking only (not entity/light culling)
        portalVisibleRegions_ = &results->portalVisibleRegions;

        // Per-region portal occlusion: enable/disable based on camera's indoor status
        // When indoor region map is loaded, dynamically toggle portal occlusion as
        // the player moves between indoor and outdoor regions within the same zone.
        if (portalOcclusionEligible_ && config_.constrainedConfig.portalOcclusion &&
            config_.constrainedConfig.enableStencilBuffer && !indoorRegions_.empty()) {
            bool wasEnabled = portalOcclusionEnabled_;
            portalOcclusionEnabled_ = results->cameraInIndoorRegion;
            if (portalOcclusionEnabled_ != wasEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "Portal occlusion {} (camera region {} is {})",
                          portalOcclusionEnabled_ ? "ON" : "OFF",
                          currentPvsRegion_,
                          results->cameraInIndoorRegion ? "indoor" : "outdoor");
            }
        }

        // Copy mesh load queue and protected regions for constrained mesh cache
        if (constrainedMeshCache_) {
            meshLoadQueue_.clear();
            size_t needBuildCount = 0;
            for (const auto& sr : results->meshLoadQueue) {
                meshLoadQueue_.push_back({sr.regionIdx, sr.distanceSq});
                if (!constrainedMeshCache_->isLoaded(sr.regionIdx))
                    needBuildCount++;
            }
            // Always log when there's work to do, throttled otherwise
            static int meshLoadQLogCounter = 0;
            if (needBuildCount > 0 || ++meshLoadQLogCounter % 60 == 1) {
                LOG_DEBUG(MOD_GRAPHICS, "SimWorker meshLoadQueue: {} total, {} need building, {} loaded in cache",
                          meshLoadQueue_.size(), needBuildCount,
                          constrainedMeshCache_->getLoadedCount());
            }
            // Rebuild bitvector: clear all bits, then set protected ones
            std::fill(protectedRegions_.begin(), protectedRegions_.end(), false);
            for (size_t regionIdx : results->protectedRegions) {
                if (regionIdx < protectedRegions_.size()) {
                    protectedRegions_[regionIdx] = true;
                }
            }
        }

        // Apply sorted region draw list for front-to-back zone rendering
        if (manualZoneDrawEnabled_ && !results->sortedRegions.empty()) {
            sortedZoneDrawList_.clear();
            sortedZoneDrawList_.reserve(results->sortedRegions.size());
            for (const auto& sr : results->sortedRegions) {
                auto it = regionMeshNodes_.find(sr.regionIdx);
                irr::scene::IMeshSceneNode* node = (it != regionMeshNodes_.end()) ? it->second : nullptr;
                sortedZoneDrawList_.push_back({sr.regionIdx, sr.distanceSq, node});
            }
        }
    }

    // Apply object visibility
    if (!results->objectVisible.empty()) {
        for (size_t i = 0; i < results->objectVisible.size() && i < objectNodes_.size(); ++i) {
            auto* node = objectNodes_[i];
            if (!node) continue;

            bool shouldBeVisible = results->objectVisible[i] != 0;
            bool isInScene = (i < objectInSceneGraph_.size()) ? objectInSceneGraph_[i] : false;

            if (shouldBeVisible && !isInScene) {
                smgr_->getRootSceneNode()->addChild(node);
                node->setVisible(true);
                if (i < objectInSceneGraph_.size())
                    objectInSceneGraph_[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < objectInSceneGraph_.size())
                    objectInSceneGraph_[i] = false;
            }
        }
    }

    // Zone light visibility: no scene nodes to add/remove.
    // Worker uses lightVisible output internally for light selection filtering.

    // Apply light selection to shader
    if (results->activeLightCount > 0 && zoneShader_ && zoneShader_->isAvailable()) {
        bool playerLightSet = false;
        for (int i = 0; i < results->activeLightCount; ++i) {
            const auto& sl = results->selectedLights[i];
            if (!sl.valid) continue;

            float boost = (i == 0 && sl.isPlayerLight) ? 3.0f : 1.0f;
            zoneShader_->setPointLight(i,
                sl.position.X, sl.position.Y, sl.position.Z,
                sl.diffuseColor.r * boost,
                sl.diffuseColor.g * boost,
                sl.diffuseColor.b * boost,
                sl.attConstant, sl.attLinear, sl.attQuadratic);

            // Set per-pixel player light FS uniforms (GLES2: computed in fragment shader)
            if (i == 0 && sl.isPlayerLight) {
                zoneShader_->setPlayerLightPos(sl.position.X, sl.position.Y, sl.position.Z);
                zoneShader_->setPlayerLightColor(
                    sl.diffuseColor.r * boost,
                    sl.diffuseColor.g * boost,
                    sl.diffuseColor.b * boost);
                zoneShader_->setPlayerLightAtten(sl.attConstant, sl.attLinear, sl.attQuadratic);
                playerLightSet = true;
            }
        }
        // Clear player light FS uniforms if no player light active
        if (!playerLightSet) {
            zoneShader_->setPlayerLightColor(0.0f, 0.0f, 0.0f);
        }

        // Apply debug toggles (/plight, /olight, L key)
        int effectiveLightCount = results->activeLightCount;

        // /plight off: zero player light in both VS (light[0]) and FS
        if (!debugPlayerLightEnabled_) {
            zoneShader_->setPointLight(0, 0,0,0, 0,0,0, 1,0,0);
            zoneShader_->setPlayerLightColor(0.0f, 0.0f, 0.0f);
            if (playerLightSet) effectiveLightCount--;
            playerLightSet = false;
        }

        // /olight off: zero all zone lights (indices 1-7) in VS
        if (!debugObjectLightsEnabled_) {
            for (int i = 1; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                zoneShader_->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = playerLightSet ? 1 : 0;
        } else if (maxObjectLights_ < effectiveLightCount - (playerLightSet ? 1 : 0)) {
            // L key: limit zone light count, zero excess slots
            int maxZone = maxObjectLights_;
            int startClear = (playerLightSet ? 1 : 0) + maxZone;
            for (int i = startClear; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                zoneShader_->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = (playerLightSet ? 1 : 0) + maxZone;
        }

        zoneShader_->setNumPointLights(effectiveLightCount);
        activeLightCount_ = effectiveLightCount;
    }

    // Apply object light PVS visibility (scene graph add/remove)
    if (!results->objectLightVisible.empty()) {
        for (size_t i = 0; i < results->objectLightVisible.size() && i < objectLights_.size(); ++i) {
            auto* node = objectLights_[i].node;
            if (!node) continue;
            bool shouldBeVisible = results->objectLightVisible[i] != 0;
            bool isInScene = (i < objectLightInSceneGraph_.size()) ? objectLightInSceneGraph_[i] : false;
            if (shouldBeVisible && !isInScene) {
                smgr_->getRootSceneNode()->addChild(node);
                if (i < objectLightInSceneGraph_.size())
                    objectLightInSceneGraph_[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < objectLightInSceneGraph_.size())
                    objectLightInSceneGraph_[i] = false;
            }
        }
    }

    // Apply fire flicker colors to object lights (for other systems that read them)
    if (!results->objectLightColors.empty()) {
        for (size_t i = 0; i < results->objectLightColors.size() && i < objectLights_.size(); ++i) {
            auto* node = objectLights_[i].node;
            if (!node) continue;
            auto& ld = node->getLightData();
            ld.DiffuseColor.r = results->objectLightColors[i].r;
            ld.DiffuseColor.g = results->objectLightColors[i].g;
            ld.DiffuseColor.b = results->objectLightColors[i].b;
        }
    }

    // Zone light animation colors: worker applies them to its own zoneData_ internally.
    // No scene nodes to update on main thread.

    // Apply sky state from worker
    if (results->skyState.valid && skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
        SkyRenderer::SkyState skyState;
        skyState.cloudScrollOffset = results->skyState.cloudScrollOffset;
        skyRenderer_->applyState(skyState);
    }

    // Apply weather system state from worker (must fire listeners before weatherEffects apply)
    if (results->weatherOutput.valid && weatherSystem_) {
        const auto& wo = results->weatherOutput;
        weatherSystem_->applyWorkerResults(wo.currentWeather, wo.targetWeather,
                                           wo.transitionProgress, wo.windIntensity,
                                           wo.weatherChanged, wo.newWeatherType);
    }

    // Apply weather effects state from worker
    if (results->weatherEffectsState.valid && weatherEffects_ && weatherEffects_->isEnabled()) {
        WeatherEffectsController::WeatherEffectsState wes;
        wes.transitionProgress = results->weatherEffectsState.transitionProgress;
        wes.currentDarkening = results->weatherEffectsState.currentDarkening;
        wes.lightningFlashTimer = results->weatherEffectsState.lightningFlashTimer;
        wes.lightningBoltTimer = results->weatherEffectsState.lightningBoltTimer;
        wes.lightningActive = results->weatherEffectsState.lightningActive;
        wes.triggerLightningFlash = results->weatherEffectsState.triggerLightningFlash;
        weatherEffects_->applyState(wes);
    }

    // Update player light position (every frame, not gated)
    if (playerLightNode_ && playerLightLevel_ > 0) {
        playerLightNode_->setPosition(irr::core::vector3df(playerX_, playerZ_ + 3.0f, playerY_));
    }

    // Apply tree wind shadow vertices
    if (treeManager_ && !results->treeShadows.empty()) {
        const auto& trees = treeManager_->getAnimatedTrees();
        for (size_t t = 0; t < results->treeShadows.size() && t < trees.size(); ++t) {
            const auto& tree = trees[t];
            for (size_t b = 0; b < results->treeShadows[t].size() && b < tree.buffers.size(); ++b) {
                const auto& shadow = results->treeShadows[t][b];
                if (!shadow.dirty) continue;
                auto* buffer = tree.buffers[b].buffer;
                if (!buffer) continue;
                auto* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                irr::u32 vertexCount = buffer->getVertexCount();
                for (irr::u32 v = 0; v < vertexCount && v < shadow.positions.size(); ++v) {
                    vertices[v].Pos = shadow.positions[v];
                }
                buffer->setDirty(irr::scene::EBT_VERTEX);
            }
        }
    }

    // Apply spell VFX results from worker (desktop GL path)
    if (results->spellVfxOutput.valid && spellVisualFX_ && spellVisualFX_->isWorkerDriven()) {
        spellVisualFX_->applyWorkerResults(*results);
    }

    // Apply detail wind/disturbance shadow vertices
    if (detailManager_ && detailManager_->isEnabled() && results->detailOutput.valid) {
        detailManager_->applyWorkerResults(results->detailOutput);
    }

    // Apply vertex animation results (frame index computed by worker)
    if (!results->vertexAnims.empty() && !vertexAnimatedMeshes_.empty()) {
        for (size_t i = 0; i < results->vertexAnims.size() && i < vertexAnimatedMeshes_.size(); ++i) {
            const auto& vr = results->vertexAnims[i];
            auto& vam = vertexAnimatedMeshes_[i];
            if (!vr.frameChanged) continue;
            if (!vam.mesh || !vam.animData || vam.animData->frames.empty()) continue;

            vam.currentFrame = vr.currentFrame;

            // Skip expensive per-vertex work for non-visible meshes
            if (vam.node && !vam.node->isVisible()) continue;

            const VertexAnimFrame& frame = vam.animData->frames[vam.currentFrame];
            size_t expectedVerts = frame.positions.size() / 3;

            for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = vam.mesh->getMeshBuffer(b);
                irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                irr::u32 vertexCount = buffer->getVertexCount();
                if (b >= vam.vertexMapping.size() || vam.vertexMapping[b].size() != vertexCount) continue;
                for (irr::u32 v = 0; v < vertexCount; ++v) {
                    size_t animIdx = vam.vertexMapping[b][v];
                    if (animIdx == SIZE_MAX || animIdx >= expectedVerts) continue;
                    float eqX = frame.positions[animIdx * 3 + 0] + vam.centerOffsetX;
                    float eqY = frame.positions[animIdx * 3 + 1] + vam.centerOffsetY;
                    float eqZ = frame.positions[animIdx * 3 + 2] + vam.centerOffsetZ;
                    vertices[v].Pos.X = eqX;
                    vertices[v].Pos.Y = eqZ;
                    vertices[v].Pos.Z = eqY;
                }
                buffer->setDirty(irr::scene::EBT_VERTEX);
            }
        }
    }

    // Apply particle output from worker
    if (results->particleOutput.valid && particleManager_) {
        // Store render buffer pointer for use during rendering
        particleRenderBuffer_ = &results->particleOutput.renderBuffer;

        // Update particle manager's active count from worker
        particleManager_->setWorkerActiveCount(results->particleOutput.activeCount);

        // Resolve entity positions for the next frame's particle input
        pendingParticleEntityPositions_.clear();
        pendingParticleEntityDirections_.clear();

        for (uint16_t entityID : results->particleOutput.positionRequestEntities) {
            glm::vec3 pos;
            if (particleManager_->resolveEntityPosition(entityID, pos)) {
                pendingParticleEntityPositions_[entityID] = pos;
            }
        }
    } else {
        particleRenderBuffer_ = nullptr;
    }

    // Apply boids output from worker
    if (results->boidsOutput.valid && boidsManager_) {
        boidsManager_->applyWorkerResults(results->boidsOutput);
    }

    // Apply tumbleweed output from worker
    if (results->tumbleweedOutput.valid && tumbleweedManager_) {
        tumbleweedManager_->applyWorkerResults(results->tumbleweedOutput);
    }

    // Apply occlusion-culled regions from worker
    occlusionCulledRegions_ = results->occlusionCulledRegions;

    // Apply entity interpolation and visibility results
    if (entityRenderer_ && !results->entityResults.empty()) {
        auto& entities = const_cast<std::map<uint16_t, EntityVisual>&>(entityRenderer_->getEntities());

        for (const auto& er : results->entityResults) {
            auto it = entities.find(er.spawnId);
            if (it == entities.end()) continue;
            EntityVisual& visual = it->second;

            // Apply interpolated position (skip player — camera controls it)
            if (er.wasInterpolated && !visual.isPlayer) {
                visual.lastX = er.posX;
                visual.lastY = er.posY;
                visual.lastZ = er.posZ;
                if (visual.sceneNode) {
                    visual.sceneNode->setPosition(irr::core::vector3df(
                        er.posX, er.posZ + visual.modelYOffset, er.posY));
                }
                if (visual.lightNode) {
                    visual.lightNode->setPosition(irr::core::vector3df(
                        er.posX, er.posZ + visual.modelYOffset + 3.0f, er.posY));
                }
                // Boat collision fixup — only checked when boats exist in zone
                if (entityRenderer_->hasBoatsInZone()) {
                    float boatZ = entityRenderer_->findBoatDeckZ(er.posX, er.posY, er.posZ);
                    if (boatZ > -999000.0f) {
                        float targetZ = boatZ;
                        if (std::abs(targetZ - er.posZ) < 20.0f) {
                            visual.lastZ = targetZ;
                            if (visual.sceneNode) {
                                visual.sceneNode->setPosition(irr::core::vector3df(
                                    er.posX, targetZ + visual.modelYOffset, er.posY));
                            }
                        }
                    }
                }
            }

            // Apply BSP region cache
            visual.cachedBspRegion = er.cachedBspRegion;
            visual.bspRegionDirty = er.bspRegionDirty;

            // Apply entity visibility (culling results)
            if (visual.sceneNode) {
                if (er.shouldBeVisible && !visual.inSceneGraph) {
                    smgr_->getRootSceneNode()->addChild(visual.sceneNode);
                    visual.sceneNode->setVisible(true);
                    visual.inSceneGraph = true;
                    if (visual.lightNode) {
                        smgr_->getRootSceneNode()->addChild(visual.lightNode);
                    }
                } else if (!er.shouldBeVisible && visual.inSceneGraph) {
                    visual.sceneNode->remove();
                    visual.inSceneGraph = false;
                    if (visual.lightNode) {
                        visual.lightNode->remove();
                    }
                }
            }
            if (visual.nameNode) {
                visual.nameNode->setVisible(er.nameTagVisible);
            }
        }
        entityRenderer_->setVisibleEntityCount(results->entityVisibleCount);
    }
}

std::vector<std::string> IrrlichtRenderer::getSimWorkerDebugInfo() const {
    std::vector<std::string> lines;

    if (!simulationWorker_) {
        lines.push_back("SimulationWorker: not created");
        return lines;
    }

    auto info = simulationWorker_->getDebugInfo();

    lines.push_back(fmt::format("SimulationWorker: {}",
        simulationWorker_->isRunning() ? "RUNNING" : "STOPPED"));
    lines.push_back(fmt::format("  Frames computed: {}", info.framesComputed));
    lines.push_back(fmt::format("  Frames skipped: {} ({:.1f}%)",
        info.framesSkipped,
        (info.framesComputed + info.framesSkipped) > 0
            ? 100.0f * info.framesSkipped / (info.framesComputed + info.framesSkipped)
            : 0.0f));
    lines.push_back(fmt::format("  Last compute: {:.2f}ms", info.lastComputeTimeMs));
    lines.push_back(fmt::format("  Avg compute: {:.2f}ms", info.avgComputeTimeMs));
    lines.push_back(fmt::format("  Worker busy: {}", info.workerBusy ? "YES" : "no"));
    lines.push_back(fmt::format("  Trees: {}, VertAnims: {}",
        treeManager_ ? treeManager_->getAnimatedTreeCount() : 0,
        vertexAnimatedMeshes_.size()));
    lines.push_back("  Offloaded: visibility, lighting, trees, vertex anims, fire flicker, boids, tumbleweeds");
    lines.push_back("  Ungated: detail wind, particles, sky, weather (30Hz)");

    return lines;
}

void IrrlichtRenderer::dumpScene() const {
    LOG_INFO(MOD_GRAPHICS, "=== SCENE DUMP (dumpscene) ===");
    LOG_INFO(MOD_GRAPHICS, "  currentPvsRegion_={}, usePvsCulling_={}", currentPvsRegion_, usePvsCulling_);

    // Count Irrlicht scene graph nodes recursively
    std::function<int(irr::scene::ISceneNode*)> countNodes = [&](irr::scene::ISceneNode* node) -> int {
        if (!node) return 0;
        int count = 1;
        for (auto* child : node->getChildren()) count += countNodes(child);
        return count;
    };
    int totalSceneNodes = smgr_ ? countNodes(smgr_->getRootSceneNode()) - 1 : 0;  // -1 for root itself
    LOG_INFO(MOD_GRAPHICS, "  Total Irrlicht scene graph nodes: {}", totalSceneNodes);

    // --- Entities ---
    if (entityRenderer_) {
        const auto& entities = entityRenderer_->getEntities();
        size_t inGraph = 0, outGraph = 0;
        for (const auto& [id, ent] : entities) {
            if (ent.inSceneGraph) inGraph++; else outGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ENTITIES: {} total, {} in-graph, {} out-of-graph",
                 entities.size(), inGraph, outGraph);
        for (const auto& [id, ent] : entities) {
            const char* type = ent.isPlayer ? "PLAYER" : (ent.isNPC ? "NPC" : "PC");
            LOG_INFO(MOD_GRAPHICS, "    [{}] id={} '{}' pos=({:.1f},{:.1f},{:.1f}) bspRegion={} inGraph={} placeholder={}",
                     type, id, ent.name, ent.lastX, ent.lastY, ent.lastZ,
                     ent.cachedBspRegion == SIZE_MAX ? static_cast<int64_t>(-1) : static_cast<int64_t>(ent.cachedBspRegion),
                     ent.inSceneGraph, ent.usesPlaceholder);
        }
    } else {
        LOG_INFO(MOD_GRAPHICS, "  ENTITIES: no EntityRenderer");
    }

    // --- Doors ---
    if (doorManager_) {
        size_t inGraph = 0, outGraph = 0, total = doorManager_->getDoorCount();
        // Iterate door IDs 0-255
        for (uint16_t i = 0; i < 256; ++i) {
            const auto* door = doorManager_->getDoor(static_cast<uint8_t>(i));
            if (!door) continue;
            if (door->inSceneGraph) inGraph++; else outGraph++;
            LOG_INFO(MOD_GRAPHICS, "    [DOOR] id={} '{}' pos=({:.1f},{:.1f},{:.1f}) bspRegion={} inGraph={} meshBuilt={}",
                     door->doorId, door->modelName, door->x, door->y, door->z,
                     door->bspRegion == SIZE_MAX ? static_cast<int64_t>(-1) : static_cast<int64_t>(door->bspRegion),
                     door->inSceneGraph, door->meshBuilt);
        }
        LOG_INFO(MOD_GRAPHICS, "  DOORS: {} total, {} in-graph, {} out-of-graph", total, inGraph, outGraph);
    }

    // --- Objects ---
    {
        size_t inGraph = 0, outGraph = 0;
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (!objectNodes_[i]) continue;
            bool ig = (i < objectInSceneGraph_.size()) ? objectInSceneGraph_[i] : true;
            if (ig) inGraph++; else outGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  OBJECTS: {} total, {} in-graph, {} out-of-graph",
                 objectNodes_.size(), inGraph, outGraph);
    }

    // --- Zone Lights ---
    LOG_INFO(MOD_GRAPHICS, "  ZONE LIGHTS: {} total (no scene nodes, data-only)",
             zoneLightData_.size());

    // --- Zone Meshes ---
    {
        size_t zmInGraph = 0, zmOutGraph = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) continue;
            if (node->getParent()) zmInGraph++; else zmOutGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ZONE MESHES: {} total, {} in-graph, {} out-of-graph | manualDraw={}",
                 regionMeshNodes_.size(), zmInGraph, zmOutGraph, manualZoneDrawEnabled_);
    }

    // --- Animated Trees ---
    size_t treeInGraph = 0, treeTotal = 0;
    if (treeManager_) {
        const auto& trees = treeManager_->getAnimatedTrees();
        treeTotal = trees.size();
        for (const auto& tree : trees) {
            if (tree.node && tree.node->getParent()) treeInGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ANIMATED TREES: {} total, {} in-graph, {} out-of-graph",
                 treeTotal, treeInGraph, treeTotal - treeInGraph);
    } else {
        LOG_INFO(MOD_GRAPHICS, "  ANIMATED TREES: no TreeManager (GPU wind path or disabled)");
    }

    // --- Object Lights ---
    {
        int olInGraph = 0;
        for (const auto& ol : objectLights_) {
            if (ol.node && ol.node->getParent()) olInGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  OBJECT LIGHTS: {} total, {} in-graph", objectLights_.size(), olInGraph);
    }

    // --- Sky Renderer ---
    int skyNodeCount = 0;
    if (skyRenderer_) {
        skyNodeCount = skyRenderer_->getSceneNodeCount();
        LOG_INFO(MOD_GRAPHICS, "  SKY NODES: {}", skyNodeCount);
    }

    // --- Zone Collision Node ---
    bool collisionInGraph = zoneCollisionNode_ && zoneCollisionNode_->getParent();
    if (zoneCollisionNode_) {
        LOG_INFO(MOD_GRAPHICS, "  ZONE COLLISION NODE: in-graph={}", collisionInGraph);
    }

    // --- Weather Nodes ---
    int weatherNodeCount = 0;
    if (weatherEffects_) {
        weatherNodeCount = weatherEffects_->getSceneNodeCount();
        if (weatherNodeCount > 0) {
            LOG_INFO(MOD_GRAPHICS, "  WEATHER NODES: {}", weatherNodeCount);
        }
    }

    // --- Entity Lights ---
    int entityLightsInGraph = 0;
    if (entityRenderer_) {
        for (const auto& [id, ent] : entityRenderer_->getEntities()) {
            if (ent.lightNode && ent.lightNode->getParent()) entityLightsInGraph++;
        }
        if (entityLightsInGraph > 0) {
            LOG_INFO(MOD_GRAPHICS, "  ENTITY LIGHTS: {} in-graph", entityLightsInGraph);
        }
    }

    // --- Other nodes ---
    // Build a set of all known/tracked scene node pointers for diagnostic enumeration
    std::set<irr::scene::ISceneNode*> knownNodes;
    int otherNodes = totalSceneNodes;
    // Subtract known categories
    if (entityRenderer_) {
        for (const auto& [id, ent] : entityRenderer_->getEntities()) {
            if (ent.inSceneGraph && ent.sceneNode) {
                otherNodes -= countNodes(ent.sceneNode);  // entity + all descendants (children, grandchildren, etc.)
                knownNodes.insert(ent.sceneNode);
            }
            // Subtract entity light nodes (parented to ROOT, not to entity)
            if (ent.lightNode && ent.lightNode->getParent()) {
                otherNodes--;
                knownNodes.insert(ent.lightNode);
            }
        }
    }
    if (doorManager_) {
        for (uint16_t i = 0; i < 256; ++i) {
            const auto* door = doorManager_->getDoor(static_cast<uint8_t>(i));
            if (door && door->inSceneGraph) {
                otherNodes -= 2;  // pivot + mesh child
                if (door->pivotNode) knownNodes.insert(door->pivotNode);
            }
        }
    }
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        if (objectNodes_[i] && i < objectInSceneGraph_.size() && objectInSceneGraph_[i]) {
            otherNodes--;
            knownNodes.insert(objectNodes_[i]);
        }
    }
    // Zone lights have no scene nodes — nothing to subtract
    // Subtract zone mesh nodes that are in the scene graph
    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (node && node->getParent()) {
            otherNodes--;
            knownNodes.insert(node);
        }
    }
    // Subtract object light nodes
    for (const auto& ol : objectLights_) {
        if (ol.node && ol.node->getParent()) {
            otherNodes--;
            knownNodes.insert(ol.node);
        }
    }
    // Subtract sky renderer nodes and collect their pointers
    otherNodes -= skyNodeCount;
    if (skyRenderer_) skyRenderer_->collectSceneNodes(knownNodes);
    // Subtract weather nodes and collect their pointers
    otherNodes -= weatherNodeCount;
    if (weatherEffects_) weatherEffects_->collectSceneNodes(knownNodes);
    // Subtract zone collision node
    if (collisionInGraph) { otherNodes--; knownNodes.insert(zoneCollisionNode_); }
    if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) { otherNodes--; knownNodes.insert(fallbackMeshNode_); }
    if (zonePlaceholderNode_) { otherNodes--; knownNodes.insert(zonePlaceholderNode_); }
    if (camera_) { otherNodes--; knownNodes.insert(camera_); }
    if (sunLight_) { otherNodes--; knownNodes.insert(sunLight_); }
    if (playerLightNode_) { otherNodes--; knownNodes.insert(playerLightNode_); }
    // Subtract animated tree nodes that are in the scene graph
    if (treeManager_) {
        for (const auto& tree : treeManager_->getAnimatedTrees()) {
            if (tree.node && tree.node->getParent()) knownNodes.insert(tree.node);
        }
    }
    otherNodes -= static_cast<int>(treeInGraph);
    LOG_INFO(MOD_GRAPHICS, "  OTHER: camera={}, sunLight={}, playerLight={}, unaccounted={}",
             camera_ != nullptr, sunLight_ != nullptr, playerLightNode_ != nullptr, otherNodes);

    // Enumerate unaccounted nodes for diagnostics
    if (otherNodes != 0 && smgr_) {
        auto* root = smgr_->getRootSceneNode();
        int idx = 0;
        for (auto* child : root->getChildren()) {
            if (knownNodes.count(child) == 0) {
                // Not in any tracked category — identify by type
                const char* typeName = "unknown";
                switch (child->getType()) {
                    case irr::scene::ESNT_MESH: typeName = "mesh"; break;
                    case irr::scene::ESNT_LIGHT: typeName = "light"; break;
                    case irr::scene::ESNT_CAMERA: typeName = "camera"; break;
                    case irr::scene::ESNT_BILLBOARD: typeName = "billboard"; break;
                    case irr::scene::ESNT_ANIMATED_MESH: typeName = "animMesh"; break;
                    case irr::scene::ESNT_EMPTY: typeName = "empty"; break;
                    case irr::scene::ESNT_PARTICLE_SYSTEM: typeName = "particle"; break;
                    case irr::scene::ESNT_TEXT: typeName = "text"; break;
                    case irr::scene::ESNT_OCTREE: typeName = "octree"; break;
                    default: break;
                }
                auto pos = child->getPosition();
                const char* nodeName = child->getName() ? child->getName() : "";
                // For mesh nodes, log material/texture info to help identify
                std::string extraInfo;
                if (child->getType() == irr::scene::ESNT_MESH) {
                    auto* meshNode = static_cast<irr::scene::IMeshSceneNode*>(child);
                    auto matCount = meshNode->getMaterialCount();
                    extraInfo = fmt::format(" mats={}", matCount);
                    if (matCount > 0) {
                        auto& mat = meshNode->getMaterial(0);
                        if (mat.getTexture(0)) {
                            auto texName = std::string(mat.getTexture(0)->getName().getPath().c_str());
                            extraInfo += fmt::format(" tex='{}'", texName);
                        }
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "    UNACCOUNTED[{}]: type={} name='{}' visible={} pos=({:.1f},{:.1f},{:.1f}) children={}{}",
                         idx, typeName, nodeName, child->isVisible(), pos.X, pos.Y, pos.Z,
                         static_cast<int>(child->getChildren().size()), extraInfo);
                idx++;
            }
        }
    }

    // --- Fire Glow ---
    {
        unsigned int icoProgram = 0;
#ifdef EQT_HAS_DRM
        icoProgram = icosphereProgram_;
#endif
        LOG_INFO(MOD_GRAPHICS, "  FIRE GLOW: lights={}, lightingEnabled={}, icospheresEnabled={}, "
                 "maxLights={}, fireEffectsEnabled={}, icosphereProgram={}",
                 fireGlowLights_.size(), fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                 maxFireGlowLights_, fireEffectsEnabled_, icoProgram);
    }

    // --- Visibility counts (from SimulationWorker output applied to scene) ---
    {
        size_t visRegions = 0, visObjects = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && node->isVisible()) visRegions++;
        }
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i] && i < objectInSceneGraph_.size() && objectInSceneGraph_[i])
                visObjects++;
        }
        size_t visEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
        LOG_INFO(MOD_GRAPHICS, "  VISIBLE: regions={}/{}, objects={}/{}, lights={}, entities={}",
                 visRegions, regionMeshNodes_.size(),
                 visObjects, objectNodes_.size(),
                 activeLightCount_, visEntities);
    }

    LOG_INFO(MOD_GRAPHICS, "=== END SCENE DUMP ===");
}

int64_t IrrlichtRenderer::measureSection() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
    sectionStart_ = now;
    return us;
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

    if (constrainedTextureCache_) {
        if (progressiveLoadingActive_) {
            constrainedTextureCache_->processUploadQueue();  // batch during loading
        } else {
            constrainedTextureCache_->drainDecodedQueue();   // drain only (no GL)
        }
    }
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

            // Priority 2: Progress texture decode pipeline (decoded → GPU submit)
            if (!didWork && constrainedTextureCache_) didWork = constrainedTextureCache_->processOneUpload();

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

            // Priority 11: Lazy icon loading
            if (!didWork && windowManager_ && config_.constrainedConfig.enableItemIcons) {
                windowManager_->getIconLoader().setBackgroundThreadPool(backgroundThreadPool_.get());
                windowManager_->getIconLoader().startWorker();
                didWork = windowManager_->processOneLazyIcon();
            }

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
                           frameTimings_.windowManager + frameTimings_.zoneLineOverlay +
                           frameTimings_.endScene + frameTimings_.footprintRender +
                           frameTimings_.postRender;
        renderCostAvgUs_ = renderCostAvgUs_ * 0.9f + static_cast<float>(renderUs) * 0.1f;

        // Update essential simulation cost EMA
        int64_t essSimUs = frameTimings_.entityUpdate + frameTimings_.doorUpdate +
                           frameTimings_.spellVfxUpdate + frameTimings_.animatedTextures +
                           frameTimings_.vertexAnimations + frameTimings_.hudUpdate +
                           frameTimings_.windowManagerUpdate;
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
                {"wmUpdate",           frameTimings_.windowManagerUpdate},
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
                {"windowManager",      frameTimings_.windowManager},
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
                {"wmUpdate",         frameTimings_.windowManagerUpdate},
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
                {"windowManager",    frameTimings_.windowManager},
                {"glFinish",         frameTimings_.glFinish},
                {"endScene",         frameTimings_.endScene},
                {"postRender",       frameTimings_.postRender},
                {"simWorkerApply",   frameTimings_.simWorkerApply},
            };
            std::sort(std::begin(sections), std::end(sections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Log header with totals + scene composition
            LOG_WARN(MOD_GRAPHICS, "RED STATE: {:.1f}ms / {:.1f}ms budget ({:.0f}% over) — avg {:.1f}ms, stall {}/{} — ALL LOADING HALTED",
                     totalFrameMs, budgetMs,
                     (totalFrameMs / budgetMs - 1.0f) * 100.0f,
                     governor_->getAverageFrameTimeMs(),
                     governor_->getStallCounter(),
                     FrameBudgetGovernor::kStallThreshold);
            // Scene composition: nodes, polys, manual draw stats
            LOG_WARN(MOD_GRAPHICS, "  RED SCENE: {} sceneNodes, {} polys | manualDraw: {} regions, {} drawCalls | sortedList: {}",
                     frameTimings_.sceneNodeCount, lastPolygonCount_,
                     frameTimings_.manualZoneRegionsDrawn, frameTimings_.manualZoneDrawCalls,
                     sortedZoneDrawList_.size());

            // Scene graph node census
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

            // Log every section that consumed >0.1ms, sorted by cost
            for (const auto& s : sections) {
                if (s.us < 100) continue;  // Skip <0.1ms noise
                LOG_WARN(MOD_GRAPHICS, "  RED: {:>20s} = {:>7.1f}ms ({:>5.1f}%)",
                         s.name, s.us / 1000.0f,
                         totalFrameMs > 0 ? (s.us / 1000.0f / totalFrameMs * 100.0f) : 0.0f);
            }
        }
        // Yellow/Green budget violation: condensed top-3 warning
        else if (totalFrameMs > budgetMs * 1.2f) {
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
                {"windowManager", frameTimings_.windowManager},
                {"endScene", frameTimings_.endScene},
                {"particles", frameTimings_.particles},
                {"simWorkerApply", frameTimings_.simWorkerApply},
                {"targetBox", frameTimings_.targetBox},
                {"guiDrawAll", frameTimings_.guiDrawAll},
                {"footprintRender", frameTimings_.footprintRender},
            };
            std::sort(std::begin(sections), std::end(sections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            LOG_WARN(MOD_GRAPHICS, "BUDGET: {:.1f}ms (budget {:.1f}ms, gov={}) top: {}={:.1f}ms {}={:.1f}ms {}={:.1f}ms",
                     totalFrameMs, budgetMs,
                     governor_ ? governor_->getStateName() : "N/A",
                     sections[0].name, sections[0].us / 1000.0f,
                     sections[1].name, sections[1].us / 1000.0f,
                     sections[2].name, sections[2].us / 1000.0f);
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
        frameTimingsAccum_.windowManager += frameTimings_.windowManager;
        frameTimingsAccum_.wmChat += frameTimings_.wmChat;
        frameTimingsAccum_.wmInventory += frameTimings_.wmInventory;
        frameTimingsAccum_.wmSpellGems += frameTimings_.wmSpellGems;
        frameTimingsAccum_.wmHotbar += frameTimings_.wmHotbar;
        frameTimingsAccum_.wmPlayerStatus += frameTimings_.wmPlayerStatus;
        frameTimingsAccum_.wmBuffs += frameTimings_.wmBuffs;
        frameTimingsAccum_.wmGroup += frameTimings_.wmGroup;
        frameTimingsAccum_.wmSpellbook += frameTimings_.wmSpellbook;
        frameTimingsAccum_.wmCastingBars += frameTimings_.wmCastingBars;
        frameTimingsAccum_.wmPet += frameTimings_.wmPet;
        frameTimingsAccum_.wmSkills += frameTimings_.wmSkills;
        frameTimingsAccum_.wmLoot += frameTimings_.wmLoot;
        frameTimingsAccum_.wmVendor += frameTimings_.wmVendor;
        frameTimingsAccum_.wmBags += frameTimings_.wmBags;
        frameTimingsAccum_.wmTooltips += frameTimings_.wmTooltips;
        frameTimingsAccum_.wmOverlays += frameTimings_.wmOverlays;
        frameTimingsAccum_.wmOther += frameTimings_.wmOther;
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
        frameTimingsAccum_.windowManagerUpdate += frameTimings_.windowManagerUpdate;
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
    chatInputFocused_ = windowManager_ && windowManager_->isChatInputFocused();
    if (eventReceiver_) eventReceiver_->setChatInputFocused(chatInputFocused_);

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

    // Handle window manager mouse capture BEFORE camera/movement updates
    bool hadClick = eventReceiver_->wasLeftButtonClicked();
    bool hadRelease = eventReceiver_->wasLeftButtonReleased();
    int clickX = eventReceiver_->getClickMouseX();
    int clickY = eventReceiver_->getClickMouseY();

    if (windowManager_) {
        if (hadClick) {
            bool shift = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
            bool ctrl = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
            windowManagerCapture_ = windowManager_->handleMouseDown(clickX, clickY, true, shift, ctrl);
        }
        if (hadRelease) {
            int mouseX = eventReceiver_->getMouseX();
            int mouseY = eventReceiver_->getMouseY();
            windowManager_->handleMouseUp(mouseX, mouseY, true);
            windowManagerCapture_ = false;
        }
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

    // Handle mouse targeting - skip if window consumed the click
    if (!windowManagerCapture_ && hadClick) {
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
                if (!chatInputFocused_ && windowManager_) windowManager_->toggleOptionsWindow();
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
                if (windowManager_) windowManager_->toggleGroupWindow();
                break;
            case RA::ToggleSkills:
                if (windowManager_) windowManager_->toggleSkillsWindow();
                break;
            case RA::TogglePet:
                if (windowManager_) windowManager_->togglePetWindow();
                break;
            case RA::ToggleSpellbook:
                if (windowManager_) windowManager_->toggleSpellbook();
                break;
            case RA::ToggleBuffWindow:
                if (windowManager_) windowManager_->toggleBuffWindow();
                break;

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

    // Handle hotbar shortcuts
    int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
    if (hotbarRequest >= 0 && !chatInputFocused_) {
        if (windowManager_ && windowManager_->getHotbarWindow()) {
            LOG_DEBUG(MOD_GRAPHICS, "Hotbar button {} activated", hotbarRequest + 1);
            windowManager_->getHotbarWindow()->activateButton(hotbarRequest);
        }
    }

    // Handle chat input (Player mode)
    if (windowManager_) {
        bool chatFocused = windowManager_->isChatInputFocused();
        if (chatFocused) {
            auto* chatWindow = windowManager_->getChatWindow();
            while (eventReceiver_->hasPendingKeyEvents()) {
                auto keyEvent = eventReceiver_->popKeyEvent();
                if (keyEvent.key == irr::KEY_ESCAPE) { windowManager_->unfocusChatInput(); continue; }
                if (chatWindow) chatWindow->handleKeyPress(keyEvent.key, keyEvent.character, keyEvent.shift, keyEvent.ctrl);
            }
            eventReceiver_->escapeKeyPressed();
            eventReceiver_->enterKeyPressed();
        } else {
            bool moneyDialogShown = windowManager_->isMoneyInputDialogShown();
            while (eventReceiver_->hasPendingKeyEvents()) {
                auto keyEvent = eventReceiver_->popKeyEvent();
                if (keyEvent.ctrl || moneyDialogShown) {
                    windowManager_->handleKeyPress(keyEvent.key, keyEvent.shift, keyEvent.ctrl);
                }
            }
            if (eventReceiver_->enterKeyPressed() && !moneyDialogShown) windowManager_->focusChatInput();
            if (eventReceiver_->slashKeyPressed()) {
                windowManager_->focusChatInput();
                auto* chatWindow = windowManager_->getChatWindow();
                if (chatWindow) chatWindow->insertText("/");
            }
            if (eventReceiver_->escapeKeyPressed() && !moneyDialogShown) {
                if (windowManager_->isVendorWindowOpen()) {
                    if (vendorToggleCallback_) vendorToggleCallback_();
                    if (bridge_) bridge_->pushIntent(eqt::events::VendorToggleIntent{});
                } else if (currentTargetId_ != 0) {
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                    clearCurrentTarget();
                    SetTrackedTargetId(0);
                    // Also notify CombatManager via bridge queue so player status window updates
                    eventReceiver_->pushBridgeAction({RendererAction::ClearTarget});
                }
            }
        }
    } else {
        eventReceiver_->clearPendingKeyEvents();
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
    // Update window manager (for tooltip timing, etc.)
    sectionStart_ = std::chrono::steady_clock::now();
    if (windowManager_) {
        irr::u32 currentTimeMs = device_->getTimer()->getTime();
        windowManager_->update(currentTimeMs);
        int mouseX = eventReceiver_->getMouseX();
        int mouseY = eventReceiver_->getMouseY();
        windowManager_->handleMouseMove(mouseX, mouseY);
    }
    frameTimings_.windowManagerUpdate = measureSection();

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
    if (!allUIHidden_) {
        guienv_->drawAll();
        drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 10: window manager");
    // Render inventory UI windows (on top of HUD)
    if (!allUIHidden_ && windowManager_) windowManager_->render();
    frameTimings_.windowManager = measureSection();
    if (windowManager_) {
        const auto& wt = windowManager_->renderTimings_;
        frameTimings_.wmChat = wt.chat;
        frameTimings_.wmInventory = wt.inventory;
        frameTimings_.wmSpellGems = wt.spellGems;
        frameTimings_.wmHotbar = wt.hotbar;
        frameTimings_.wmPlayerStatus = wt.playerStatus;
        frameTimings_.wmBuffs = wt.buffs;
        frameTimings_.wmGroup = wt.group;
        frameTimings_.wmSpellbook = wt.spellbook;
        frameTimings_.wmCastingBars = wt.castingBars;
        frameTimings_.wmPet = wt.pet;
        frameTimings_.wmSkills = wt.skills;
        frameTimings_.wmLoot = wt.loot;
        frameTimings_.wmVendor = wt.vendor;
        frameTimings_.wmBags = wt.bags;
        frameTimings_.wmTooltips = wt.tooltips;
        frameTimings_.wmOverlays = wt.overlays;
        frameTimings_.wmOther = wt.other;
    }

    // U03c: Render new static UI when enabled
    if (newUIEnabled_ && uiRenderer_) {
        uiRenderer_->beginFrame();
        renderPlayerStatus(*uiRenderer_, uiLayout_, cachedPlayerStats_);
        renderTargetInfo(*uiRenderer_, uiLayout_, cachedTargetInfo_);
        renderChatPanel(*uiRenderer_, uiLayout_, chatPanelState_);
        uiRenderer_->endFrame();
    }

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

void IrrlichtRenderer::toggleWireframe() {
    wireframeMode_ = !wireframeMode_;

    if (zoneMeshNode_) {
        for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
            zoneMeshNode_->getMaterial(i).Wireframe = wireframeMode_;
        }
    }

    for (auto* node : objectNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).Wireframe = wireframeMode_;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Wireframe mode: {}", (wireframeMode_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleHUD() {
    hudEnabled_ = !hudEnabled_;
    if (hudText_) {
        hudText_->setVisible(hudEnabled_);
    }
    if (hotkeysText_) {
        hotkeysText_->setVisible(hudEnabled_);
    }
    LOG_INFO(MOD_GRAPHICS, "HUD: {}", (hudEnabled_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleAllUI() {
    allUIHidden_ = !allUIHidden_;
    LOG_INFO(MOD_GRAPHICS, "All UI: {}", (allUIHidden_ ? "HIDDEN" : "VISIBLE"));
}

void IrrlichtRenderer::toggleNameTags() {
    if (entityRenderer_) {
        bool visible = !config_.constrainedConfig.nameTagsEnabled;
        config_.constrainedConfig.nameTagsEnabled = visible;
        entityRenderer_->setNameTagsVisible(visible);
        LOG_INFO(MOD_GRAPHICS, "Name tags: {}", (visible ? "ON" : "OFF"));
    }
}

void IrrlichtRenderer::toggleFog() {
    fogEnabled_ = !fogEnabled_;

    if (zoneMeshNode_) {
        for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
            zoneMeshNode_->getMaterial(i).FogEnable = fogEnabled_;
        }
    }

    for (auto* node : objectNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).FogEnable = fogEnabled_;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: {}", (fogEnabled_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleZoneLights() {
    // 3-state cycle:
    // State 1: Lighting ON, Zone lights OFF (default) - dark scene, only object lights
    // State 2: Lighting ON, Zone lights ON - normal ambient/sun + zone lights
    // State 3: Lighting OFF, Zone lights OFF - no lighting effects
    // Then back to State 1

    if (lightingEnabled_ && !zoneLightsEnabled_) {
        // State 1 -> State 2: Turn zone lights ON, restore normal ambient/sun
        zoneLightsEnabled_ = true;
        // Restore normal ambient and sun based on time of day
        updateTimeOfDay(currentHour_, currentMinute_);
        if (sunLight_) {
            sunLight_->setVisible(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: ON ({} lights)", zoneLightData_.size());
    } else if (lightingEnabled_ && zoneLightsEnabled_) {
        // State 2 -> State 3: Turn both OFF
        zoneLightsEnabled_ = false;
        lightingEnabled_ = false;
        // Update materials to disable lighting
        if (zoneMeshNode_) {
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = false;
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        for (auto* node : objectNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        // Update entity materials
        if (entityRenderer_) {
            entityRenderer_->setLightingEnabled(false);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: OFF, Zone lights: OFF");
    } else {
        // State 3 -> State 1: Turn lighting ON, keep zone lights OFF
        // Dark scene - only object lights provide illumination
        lightingEnabled_ = true;
        zoneLightsEnabled_ = false;
        // Update materials to enable lighting
        if (zoneMeshNode_) {
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = true;
                zoneMeshNode_->getMaterial(i).NormalizeNormals = true;
                zoneMeshNode_->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                zoneMeshNode_->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                    node->getMaterial(i).NormalizeNormals = true;
                    node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        for (auto* node : objectNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                    node->getMaterial(i).NormalizeNormals = true;
                    node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        // Set dark ambient and disable sun - only object lights illuminate
        smgr_->setAmbientLight(irr::video::SColorf(0.005f, 0.005f, 0.008f, 1.0f));
        if (sunLight_) {
            sunLight_->setVisible(false);
        }
        // Update entity materials
        if (entityRenderer_) {
            entityRenderer_->setLightingEnabled(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: OFF (dark mode)");
    }
}

void IrrlichtRenderer::togglePlayerLight() {
    if (isLoading()) return;
    debugPlayerLightEnabled_ = !debugPlayerLightEnabled_;
    // Toggle shader variant (GLES2 only — lightweight vs per-pixel player light)
    if (zoneShader_ && zoneShader_->isLightweightAvailable()) {
        zoneShader_->setPerPixelPlayerLight(debugPlayerLightEnabled_);
        LOG_INFO(MOD_GRAPHICS, "plight toggle: pp solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getMaterialTypeSolid(), zoneShader_->getMaterialTypeAlphaTest(),
            zoneShader_->getMaterialTypeAtlasSolid(), zoneShader_->getMaterialTypeAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: lw solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getMaterialTypeLWSolid(), zoneShader_->getMaterialTypeLWAlphaTest(),
            zoneShader_->getMaterialTypeLWAtlasSolid(), zoneShader_->getMaterialTypeLWAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: active solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest(),
            zoneShader_->getActiveAtlasSolid(), zoneShader_->getActiveAtlasAlpha());
        swapZoneMeshMaterials();
    }
    LOG_INFO(MOD_GRAPHICS, "Debug: Player light {}", debugPlayerLightEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::swapZoneMeshMaterials() {
    if (!zoneShader_) return;

    // Build mapping from old material type IDs to new active variant IDs.
    // After setPerPixelPlayerLight(), the getActive*() and getMaterialType*() methods
    // return the new and old types respectively (or vice versa).
    // We need to map both directions: per-pixel ↔ lightweight.
    struct MaterialMap {
        irr::s32 ppSolid, ppAlpha, ppAtlasSolid, ppAtlasAlpha, ppWind;
        irr::s32 lwSolid, lwAlpha, lwAtlasSolid, lwAtlasAlpha, lwWind;
    };
    MaterialMap m;
    m.ppSolid = zoneShader_->getMaterialTypeSolid();
    m.ppAlpha = zoneShader_->getMaterialTypeAlphaTest();
    m.ppAtlasSolid = zoneShader_->getMaterialTypeAtlasSolid();
    m.ppAtlasAlpha = zoneShader_->getMaterialTypeAtlasAlpha();
    m.ppWind = zoneShader_->getMaterialTypeWindAlphaTest();
    m.lwSolid = zoneShader_->getMaterialTypeLWSolid();
    m.lwAlpha = zoneShader_->getMaterialTypeLWAlphaTest();
    m.lwAtlasSolid = zoneShader_->getMaterialTypeLWAtlasSolid();
    m.lwAtlasAlpha = zoneShader_->getMaterialTypeLWAtlasAlpha();
    m.lwWind = zoneShader_->getMaterialTypeLWWindAlphaTest();

    // Determine target for each known custom material type
    auto mapMaterial = [&](irr::s32 cur) -> irr::s32 {
        // Per-pixel → lightweight
        if (cur == m.ppSolid && m.lwSolid >= 0) return zoneShader_->getActiveSolid();
        if (cur == m.ppAlpha && m.lwAlpha >= 0) return zoneShader_->getActiveAlphaTest();
        if (cur == m.ppAtlasSolid && m.lwAtlasSolid >= 0) return zoneShader_->getActiveAtlasSolid();
        if (cur == m.ppAtlasAlpha && m.lwAtlasAlpha >= 0) return zoneShader_->getActiveAtlasAlpha();
        if (cur == m.ppWind && m.lwWind >= 0) return zoneShader_->getActiveWindAlphaTest();
        // Lightweight → per-pixel
        if (cur == m.lwSolid) return zoneShader_->getActiveSolid();
        if (cur == m.lwAlpha) return zoneShader_->getActiveAlphaTest();
        if (cur == m.lwAtlasSolid) return zoneShader_->getActiveAtlasSolid();
        if (cur == m.lwAtlasAlpha) return zoneShader_->getActiveAtlasAlpha();
        if (cur == m.lwWind) return zoneShader_->getActiveWindAlphaTest();
        return cur;  // Not a custom shader material — leave unchanged
    };

    int swapTotal = 0, swapChanged = 0;
    auto swapNode = [&](irr::scene::IMeshSceneNode* node) {
        if (!node) return;
        auto* mesh = node->getMesh();
        if (!mesh) return;
        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            auto& mat = mesh->getMeshBuffer(i)->getMaterial();
            irr::s32 cur = mat.MaterialType;
            irr::s32 mapped = mapMaterial(cur);
            ++swapTotal;
            if (mapped != cur) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(mapped);
                ++swapChanged;
            }
        }
    };

    // Swap zone mesh node (non-PVS single mesh)
    swapNode(zoneMeshNode_);

    // Swap PVS region mesh nodes
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        swapNode(node);
    }

    // Swap fallback mesh node
    swapNode(fallbackMeshNode_);

    // Swap object nodes (trees use wind material)
    for (auto* node : objectNodes_) {
        swapNode(node);
    }

    // Swap door mesh materials and update stored types for future door builds
    int doorSwapped = 0;
    if (doorManager_) {
        doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                             zoneShader_->getActiveAlphaTest());
        // Swap materials on existing door scene nodes
        for (auto* node : doorManager_->getDoorSceneNodes()) {
            swapNode(node);
            ++doorSwapped;
        }
        // Invalidate mesh cache so future rebuilds use the new material types
        doorManager_->invalidateMeshCache("");
    }

    LOG_INFO(MOD_GRAPHICS, "swapZoneMeshMaterials: swapped {}/{} mesh buffers (zone={} regions={} fallback={} objects={} doors={})",
        swapChanged, swapTotal,
        zoneMeshNode_ != nullptr, regionMeshNodes_.size(),
        fallbackMeshNode_ != nullptr, objectNodes_.size(), doorSwapped);

    // Rebuild sorted draw list if manual zone draw is active (material keys changed)
    if (manualZoneDrawEnabled_) {
        sortedDrawEntries_.clear();
    }

    // Swap existing entity mesh materials and update future entity material types
    if (entityRenderer_ && zoneShader_->isAvailable()) {
        entityRenderer_->swapShaderMaterials(
            m.ppSolid, zoneShader_->getActiveSolid(),
            m.ppAlpha, zoneShader_->getActiveAlphaTest());
        // Also swap lw→active in case entities had lightweight materials
        entityRenderer_->swapShaderMaterials(
            m.lwSolid, zoneShader_->getActiveSolid(),
            m.lwAlpha, zoneShader_->getActiveAlphaTest());
        entityRenderer_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                zoneShader_->getActiveAlphaTest());
    }
    if (treeManager_ && zoneShader_->isAvailable()) {
        treeManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                             zoneShader_->getActiveAlphaTest());
    }
}

void IrrlichtRenderer::toggleObjectLights() {
    if (isLoading()) return;
    debugObjectLightsEnabled_ = !debugObjectLightsEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Debug: Object lights {}", debugObjectLightsEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleDirectionalLight() {
    if (isLoading()) return;
    debugDirectionalLightEnabled_ = !debugDirectionalLightEnabled_;
    if (zoneShader_ && zoneShader_->isAvailable()) {
        if (!debugDirectionalLightEnabled_) {
            // Fullbright: ambient=1, sun=0, tint=1
            zoneShader_->setAmbientColor(1.0f, 1.0f, 1.0f);
            zoneShader_->setSunColor(0.0f, 0.0f, 0.0f);
            zoneShader_->setTintColor(1.0f, 1.0f, 1.0f);
        } else {
            // Restore time-of-day lighting
            updateTimeOfDay(currentHour_, currentMinute_);
        }
    }
    LOG_INFO(MOD_GRAPHICS, "Debug: Directional/ambient light {}", debugDirectionalLightEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::cycleObjectLights() {
    // Cycle: 0 -> 1 -> 2 -> 3 -> ... -> 8 -> 0 -> ...
    if (maxObjectLights_ >= 8) {
        maxObjectLights_ = 0;
    } else {
        maxObjectLights_++;
    }

    LOG_INFO(MOD_GRAPHICS, "Object lights: {} max", maxObjectLights_);
}

void IrrlichtRenderer::toggleOldModels() {
    if (!entityRenderer_) {
        return;
    }

    auto* loader = entityRenderer_->getRaceModelLoader();
    if (!loader) {
        return;
    }

    bool newState = !loader->isUsingOldModels();
    loader->setUseOldModels(newState);
    loader->clearCache();

    LOG_INFO(MOD_GRAPHICS, "Model mode: {}", (newState ? "Old (Classic)" : "New (Luclin+)"));
}

bool IrrlichtRenderer::isUsingOldModels() const {
    if (!entityRenderer_) {
        return true;  // Default to old models
    }

    auto* loader = entityRenderer_->getRaceModelLoader();
    if (!loader) {
        return true;
    }

    return loader->isUsingOldModels();
}

void IrrlichtRenderer::toggleManualZoneDraw() {
    if (isLoading()) return;
    if (!usePvsCulling_ || regionMeshNodes_.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Manual zone draw not available (no PVS culling)");
        return;
    }
    manualZoneDrawEnabled_ = !manualZoneDrawEnabled_;
    if (manualZoneDrawEnabled_) {
        // S05: renderPassTimer_ already created in initLoadingScreen() — just install
        if (smgr_ && renderPassTimer_) {
            smgr_->setLightManager(renderPassTimer_);
        }
        // Remove zone mesh nodes from scene graph — manual draw accesses them directly
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && node->getParent()) { node->grab(); node->remove(); }
        }
        if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) {
            fallbackMeshNode_->grab(); fallbackMeshNode_->remove();
        }
    } else {
        // Re-add zone mesh nodes to scene graph for Irrlicht-managed rendering
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && !node->getParent()) {
                smgr_->getRootSceneNode()->addChild(node);
                node->drop();
            }
        }
        if (fallbackMeshNode_ && !fallbackMeshNode_->getParent()) {
            smgr_->getRootSceneNode()->addChild(fallbackMeshNode_);
            fallbackMeshNode_->drop();
        }
    }
    LOG_INFO(MOD_GRAPHICS, "Manual zone draw (front-to-back sorting): {}",
             manualZoneDrawEnabled_ ? "ENABLED" : "DISABLED");
}

bool IrrlichtRenderer::loadIndoorRegionMap(const std::string& zoneName) {
    indoorRegions_.clear();

    std::string regionMapsPath = config_.regionMapsPath;
    if (regionMapsPath.empty()) regionMapsPath = "data/region_maps";

    std::string mapFile = regionMapsPath + "/" + zoneName + ".json";
    std::ifstream ifs(mapFile);
    if (!ifs.is_open()) {
        LOG_DEBUG(MOD_GRAPHICS, "No region map file: {} (portal vis will use zone-level heuristic)", mapFile);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, ifs, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse region map {}: {}", mapFile, errors);
        return false;
    }

    const auto& regions = root["regions"];
    if (!regions.isArray()) {
        LOG_WARN(MOD_GRAPHICS, "Region map {} has no 'regions' array", mapFile);
        return false;
    }

    for (const auto& r : regions) {
        if (r.isMember("indoor") && r["indoor"].asBool()) {
            indoorRegions_.insert(static_cast<size_t>(r["region"].asUInt()));
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Loaded region map: {} ({} indoor regions out of {} total)",
             mapFile, indoorRegions_.size(), regions.size());
    return true;
}

void IrrlichtRenderer::togglePortalOcclusion() {
    if (isLoading()) return;
    if (!portalOcclusionEligible_) {
        LOG_INFO(MOD_GRAPHICS, "Portal occlusion not available (no portals or too few)");
        return;
    }
    portalOcclusionEnabled_ = !portalOcclusionEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: {}",
             portalOcclusionEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::togglePortalDebugDraw() {
    if (isLoading()) return;
    portalDebugDraw_ = !portalDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Portal debug draw: {}",
             portalDebugDraw_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleStencilDebugDraw() {
    if (isLoading()) return;
    stencilDebugDraw_ = !stencilDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Stencil debug draw: {}",
             stencilDebugDraw_ ? "ENABLED" : "DISABLED");
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
        // Enable per-window timing in WindowManager
        if (windowManager_) windowManager_->setRenderTimingEnabled(true);
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler ENABLED - timing data will be logged every 60 frames");
    } else {
        // Remove render pass timer (but keep it if manual zone draw needs it)
        if (smgr_ && renderPassTimer_ && !manualZoneDrawEnabled_) {
            smgr_->setLightManager(nullptr);
            renderPassTimer_ = nullptr;  // Irrlicht drops the ref
        }
        if (windowManager_) windowManager_->setRenderTimingEnabled(false);
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler DISABLED");
    }
}

void IrrlichtRenderer::logFrameTimings() {
    if (frameTimingsSampleCount_ == 0) return;

    float avgTotal = static_cast<float>(frameTimingsAccum_.totalFrame) / frameTimingsSampleCount_;
    float fpsEstimate = avgTotal > 0 ? 1000000.0f / avgTotal : 0;

    // Calculate percentages
    auto pct = [&](int64_t val) -> float {
        return frameTimingsAccum_.totalFrame > 0 ?
            100.0f * static_cast<float>(val) / frameTimingsAccum_.totalFrame : 0;
    };

    // Calculate averages in microseconds
    auto avg = [&](int64_t val) -> float {
        return static_cast<float>(val) / frameTimingsSampleCount_;
    };

    LOG_INFO(MOD_GRAPHICS, "=== FRAME TIMING BREAKDOWN ({} frames, {:.1f} fps estimate) ===",
             frameTimingsSampleCount_, fpsEstimate);
    LOG_INFO(MOD_GRAPHICS, "  Total Frame:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.totalFrame), 100.0f);
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Input Handling:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.inputHandling), pct(frameTimingsAccum_.inputHandling));
    LOG_INFO(MOD_GRAPHICS, "    Player Movement:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.playerMovement), pct(frameTimingsAccum_.playerMovement));
    LOG_INFO(MOD_GRAPHICS, "  Camera Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.cameraUpdate), pct(frameTimingsAccum_.cameraUpdate));
    LOG_INFO(MOD_GRAPHICS, "  WM Update (sim):    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.windowManagerUpdate), pct(frameTimingsAccum_.windowManagerUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Entity Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.entityUpdate), pct(frameTimingsAccum_.entityUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Door Update:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.doorUpdate), pct(frameTimingsAccum_.doorUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Spell VFX Update:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.spellVfxUpdate), pct(frameTimingsAccum_.spellVfxUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Animated Textures:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.animatedTextures), pct(frameTimingsAccum_.animatedTextures));
    LOG_INFO(MOD_GRAPHICS, "  Vertex Animations:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.vertexAnimations), pct(frameTimingsAccum_.vertexAnimations));
    LOG_INFO(MOD_GRAPHICS, "  Fire Flicker:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.fireFlicker), pct(frameTimingsAccum_.fireFlicker));
    LOG_INFO(MOD_GRAPHICS, "  Object Visibility:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectVisibility), pct(frameTimingsAccum_.objectVisibility));
    LOG_INFO(MOD_GRAPHICS, "  Zone Light Vis:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.zoneLightVisibility), pct(frameTimingsAccum_.zoneLightVisibility));
    LOG_INFO(MOD_GRAPHICS, "  PVS Visibility:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.pvsVisibility), pct(frameTimingsAccum_.pvsVisibility));
    LOG_INFO(MOD_GRAPHICS, "    Occlusion Cull:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.occlusionCulling), pct(frameTimingsAccum_.occlusionCulling));
    if (constrainedMeshCache_)
    LOG_INFO(MOD_GRAPHICS, "  Mesh Loading:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.meshLoading), pct(frameTimingsAccum_.meshLoading));
    LOG_INFO(MOD_GRAPHICS, "  Object Lights:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectLights), pct(frameTimingsAccum_.objectLights));
    LOG_INFO(MOD_GRAPHICS, "  Tier2 (Detail/Tree):{:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.tier2Update), pct(frameTimingsAccum_.tier2Update));
    LOG_INFO(MOD_GRAPHICS, "  Weather System:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.weatherSystemUpdate), pct(frameTimingsAccum_.weatherSystemUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Tier3 (Env/Sky):    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.tier3Update), pct(frameTimingsAccum_.tier3Update));
    LOG_INFO(MOD_GRAPHICS, "  HUD Update:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.hudUpdate), pct(frameTimingsAccum_.hudUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Scene Draw All:     {:>8.0f} us ({:>5.1f}%)  [{} nodes, {} polys]",
             avg(frameTimingsAccum_.sceneDrawAll), pct(frameTimingsAccum_.sceneDrawAll),
             frameTimingsSampleCount_ > 0 ? frameTimingsAccum_.sceneNodeCount / frameTimingsSampleCount_ : 0,
             lastPolygonCount_);
    LOG_INFO(MOD_GRAPHICS, "    Animate+Register: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneAnimate), pct(frameTimingsAccum_.sceneAnimate));
    LOG_INFO(MOD_GRAPHICS, "    Solid Pass:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneSolid), pct(frameTimingsAccum_.sceneSolid));
    if (manualZoneDrawEnabled_) {
        LOG_INFO(MOD_GRAPHICS, "    Manual Zone Draw: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.manualZoneDraw), pct(frameTimingsAccum_.manualZoneDraw));
    }
    LOG_INFO(MOD_GRAPHICS, "    Transparent Pass: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneTransparent), pct(frameTimingsAccum_.sceneTransparent));
    LOG_INFO(MOD_GRAPHICS, "    Skybox Pass:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneSkybox), pct(frameTimingsAccum_.sceneSkybox));
    LOG_INFO(MOD_GRAPHICS, "    Other Passes:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneOther), pct(frameTimingsAccum_.sceneOther));
    LOG_INFO(MOD_GRAPHICS, "  Target Box:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.targetBox), pct(frameTimingsAccum_.targetBox));
    LOG_INFO(MOD_GRAPHICS, "  Particles:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.particles), pct(frameTimingsAccum_.particles));
    LOG_INFO(MOD_GRAPHICS, "  Boids:              {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.boids), pct(frameTimingsAccum_.boids));
    LOG_INFO(MOD_GRAPHICS, "  Weather Render:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.weatherRender), pct(frameTimingsAccum_.weatherRender));
    LOG_INFO(MOD_GRAPHICS, "  Debug Overlays:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.debugOverlays), pct(frameTimingsAccum_.debugOverlays));
    LOG_INFO(MOD_GRAPHICS, "  Casting Bars:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.castingBars), pct(frameTimingsAccum_.castingBars));
    LOG_INFO(MOD_GRAPHICS, "  GUI Draw All:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.guiDrawAll), pct(frameTimingsAccum_.guiDrawAll));
    LOG_INFO(MOD_GRAPHICS, "  Window Manager:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.windowManager), pct(frameTimingsAccum_.windowManager));
    LOG_INFO(MOD_GRAPHICS, "    Chat:             {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmChat), pct(frameTimingsAccum_.wmChat));
    LOG_INFO(MOD_GRAPHICS, "    Inventory:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmInventory), pct(frameTimingsAccum_.wmInventory));
    LOG_INFO(MOD_GRAPHICS, "    Spell Gems:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmSpellGems), pct(frameTimingsAccum_.wmSpellGems));
    LOG_INFO(MOD_GRAPHICS, "    Hotbar:           {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmHotbar), pct(frameTimingsAccum_.wmHotbar));
    LOG_INFO(MOD_GRAPHICS, "    Player Status:    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmPlayerStatus), pct(frameTimingsAccum_.wmPlayerStatus));
    LOG_INFO(MOD_GRAPHICS, "    Buffs:            {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmBuffs), pct(frameTimingsAccum_.wmBuffs));
    LOG_INFO(MOD_GRAPHICS, "    Group:            {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmGroup), pct(frameTimingsAccum_.wmGroup));
    LOG_INFO(MOD_GRAPHICS, "    Spellbook:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmSpellbook), pct(frameTimingsAccum_.wmSpellbook));
    LOG_INFO(MOD_GRAPHICS, "    Casting Bars:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmCastingBars), pct(frameTimingsAccum_.wmCastingBars));
    LOG_INFO(MOD_GRAPHICS, "    Pet:              {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmPet), pct(frameTimingsAccum_.wmPet));
    LOG_INFO(MOD_GRAPHICS, "    Skills:           {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmSkills), pct(frameTimingsAccum_.wmSkills));
    LOG_INFO(MOD_GRAPHICS, "    Loot:             {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmLoot), pct(frameTimingsAccum_.wmLoot));
    LOG_INFO(MOD_GRAPHICS, "    Vendor:           {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmVendor), pct(frameTimingsAccum_.wmVendor));
    LOG_INFO(MOD_GRAPHICS, "    Bags:             {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmBags), pct(frameTimingsAccum_.wmBags));
    LOG_INFO(MOD_GRAPHICS, "    Tooltips:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmTooltips), pct(frameTimingsAccum_.wmTooltips));
    LOG_INFO(MOD_GRAPHICS, "    Overlays:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmOverlays), pct(frameTimingsAccum_.wmOverlays));
    LOG_INFO(MOD_GRAPHICS, "    Other:            {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.wmOther), pct(frameTimingsAccum_.wmOther));
    LOG_INFO(MOD_GRAPHICS, "  Zone Line Overlay:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.zoneLineOverlay), pct(frameTimingsAccum_.zoneLineOverlay));
    LOG_INFO(MOD_GRAPHICS, "  Footprint Render:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.footprintRender), pct(frameTimingsAccum_.footprintRender));
    LOG_INFO(MOD_GRAPHICS, "  Post Render:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.postRender), pct(frameTimingsAccum_.postRender));
    LOG_INFO(MOD_GRAPHICS, "  SimWorker Apply:    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.simWorkerApply), pct(frameTimingsAccum_.simWorkerApply));
    LOG_INFO(MOD_GRAPHICS, "  glFinish:           {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.glFinish), pct(frameTimingsAccum_.glFinish));
    LOG_INFO(MOD_GRAPHICS, "  End Scene:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.endScene), pct(frameTimingsAccum_.endScene));
    LOG_INFO(MOD_GRAPHICS, "  Manual Zone Draw:   {} regions avg, {} drawCalls avg",
             frameTimingsAccum_.manualZoneRegionsDrawn / std::max(1, frameTimingsSampleCount_),
             frameTimingsAccum_.manualZoneDrawCalls / std::max(1, frameTimingsSampleCount_));
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Render EMA:         {:>8.0f} us | EssSim EMA: {:>6.0f} us",
             renderCostAvgUs_, essentialSimCostAvgUs_);
    if (governor_) {
        LOG_INFO(MOD_GRAPHICS, "  Governor: {} | avg {:.1f}ms | target {:.1f}ms | ratio {:.2f}{}",
                 governor_->getStateName(),
                 governor_->getAverageFrameTimeMs(),
                 governor_->getTargetFrameTimeMs(),
                 governor_->getBudgetRatio(),
                 governor_->isForced() ? " (FORCED)" : "");
    }
}

void IrrlichtRenderer::runSceneProfile() {
    sceneProfileEnabled_ = true;
    // Wait 60 frames before profiling to allow constrained visibility to run
    sceneProfileFrameCount_ = -60;
    LOG_INFO(MOD_GRAPHICS, "Scene profile scheduled - will run after 60 frames");
}

void IrrlichtRenderer::profileSceneBreakdown() {
    if (!driver_ || !smgr_) return;

    SceneBreakdown breakdown;
    const int numSamples = 10;  // More samples for stability

    // Helper to draw scene and return (time_us, poly_count)
    auto timeAndPolyDrawAll = [this]() -> std::pair<int64_t, uint32_t> {
        if (zoneShader_) zoneShader_->beginFrame();
        auto start = std::chrono::steady_clock::now();
        smgr_->drawAll();
        auto end = std::chrono::steady_clock::now();
        int64_t time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        uint32_t polys = driver_->getPrimitiveCountDrawn();
        return {time_us, polys};
    };

    // Helper to hide all scene content including lights
    auto hideAll = [this]() {
        if (zoneMeshNode_) zoneMeshNode_->setVisible(false);
        if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(false);
        // Hide objects (they may already be out of scene graph)
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i]) objectNodes_[i]->setVisible(false);
        }
        if (doorManager_) doorManager_->setAllDoorsVisible(false);
        // Zone lights have no scene nodes — nothing to hide
        if (sunLight_) sunLight_->setVisible(false);
        if (playerLightNode_) playerLightNode_->setVisible(false);
    };

    auto showAll = [this]() {
        if (zoneMeshNode_) zoneMeshNode_->setVisible(true);
        if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(true);
        // Add objects back to scene graph and show
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i]) {
                if (i < objectInSceneGraph_.size() && !objectInSceneGraph_[i]) {
                    smgr_->getRootSceneNode()->addChild(objectNodes_[i]);
                    objectInSceneGraph_[i] = true;
                }
                objectNodes_[i]->setVisible(true);
            }
        }
        if (doorManager_) doorManager_->setAllDoorsVisible(true);
        // Zone lights have no scene nodes — nothing to show
        if (sunLight_) sunLight_->setVisible(true);
        if (playerLightNode_) playerLightNode_->setVisible(true);
    };

    // Count nodes
    breakdown.entityCount = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
    breakdown.objectCount = static_cast<int>(objectNodes_.size());
    breakdown.doorCount = doorManager_ ? static_cast<int>(doorManager_->getDoorCount()) : 0;
    int lightCount = static_cast<int>(zoneLightData_.size()) + (sunLight_ ? 1 : 0) + (playerLightNode_ ? 1 : 0);

    // Count total scene nodes recursively
    std::function<int(irr::scene::ISceneNode*)> countNodes = [&](irr::scene::ISceneNode* node) -> int {
        if (!node) return 0;
        int count = 1;
        const auto& children = node->getChildren();
        for (auto* child : children) {
            count += countNodes(child);
        }
        return count;
    };
    int totalSceneNodes = countNodes(smgr_->getRootSceneNode());

    LOG_INFO(MOD_GRAPHICS, "=== SCENE BREAKDOWN PROFILE ===");
    LOG_INFO(MOD_GRAPHICS, "Zone mesh node: {}", zoneMeshNode_ ? "valid" : "NULL");
    LOG_INFO(MOD_GRAPHICS, "Total scene nodes: {} (lights: {})", totalSceneNodes, lightCount);
    LOG_INFO(MOD_GRAPHICS, "Measuring each category in isolation ({} samples each)...", numSamples);

    // Hide everything first
    hideAll();

    // 1. Measure baseline (nothing visible - just scene graph overhead)
    int64_t baselineSum = 0;
    uint32_t baselinePolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        baselineSum += time;
        baselinePolys = polys;  // Should be 0 or near 0
        driver_->endScene();
    }
    int64_t baseline = baselineSum / numSamples;

    // 2. Measure zone mesh only
    if (zoneMeshNode_) zoneMeshNode_->setVisible(true);
    int64_t zoneSum = 0;
    uint32_t zonePolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        zoneSum += time;
        zonePolys = polys;  // Keep last sample's poly count
        driver_->endScene();
    }
    breakdown.zoneTime = (zoneSum / numSamples) - baseline;
    breakdown.zonePolys = static_cast<int>(zonePolys - baselinePolys);
    if (zoneMeshNode_) zoneMeshNode_->setVisible(false);

    // 3. Measure entities only
    if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(true);
    int64_t entitySum = 0;
    uint32_t entityPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        entitySum += time;
        entityPolys = polys;
        driver_->endScene();
    }
    breakdown.entityTime = (entitySum / numSamples) - baseline;
    int entityPolyCount = static_cast<int>(entityPolys - baselinePolys);
    if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(false);

    // 4. Measure objects only
    for (auto* node : objectNodes_) if (node) node->setVisible(true);
    int64_t objectSum = 0;
    uint32_t objectPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        objectSum += time;
        objectPolys = polys;
        driver_->endScene();
    }
    breakdown.objectTime = (objectSum / numSamples) - baseline;
    int objectPolyCount = static_cast<int>(objectPolys - baselinePolys);
    for (auto* node : objectNodes_) if (node) node->setVisible(false);

    // 5. Measure doors only
    if (doorManager_) doorManager_->setAllDoorsVisible(true);
    int64_t doorSum = 0;
    uint32_t doorPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        doorSum += time;
        doorPolys = polys;
        driver_->endScene();
    }
    breakdown.doorTime = (doorSum / numSamples) - baseline;
    int doorPolyCount = static_cast<int>(doorPolys - baselinePolys);
    if (doorManager_) doorManager_->setAllDoorsVisible(false);

    // 6. Measure lights only (zone lights have no scene nodes, just sun + player light)
    if (sunLight_) sunLight_->setVisible(true);
    if (playerLightNode_) playerLightNode_->setVisible(true);
    int64_t lightSum = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        lightSum += time;
        driver_->endScene();
    }
    int64_t lightTime = (lightSum / numSamples) - baseline;
    if (sunLight_) sunLight_->setVisible(false);
    if (playerLightNode_) playerLightNode_->setVisible(false);

    // 7. Measure full scene
    showAll();
    int64_t totalSum = 0;
    uint32_t totalPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        totalSum += time;
        totalPolys = polys;
        driver_->endScene();
    }
    breakdown.totalDrawAll = totalSum / numSamples;

    // Calculate "other" time (scene overhead not captured by individual categories)
    int64_t measuredTotal = breakdown.zoneTime + breakdown.entityTime +
                            breakdown.objectTime + breakdown.doorTime + baseline;
    breakdown.otherTime = breakdown.totalDrawAll - measuredTotal;
    if (breakdown.otherTime < 0) breakdown.otherTime = 0;

    // Calculate percentages
    auto pct = [&](int64_t val) -> float {
        return breakdown.totalDrawAll > 0 ? 100.0f * val / breakdown.totalDrawAll : 0;
    };

    LOG_INFO(MOD_GRAPHICS, "");
    LOG_INFO(MOD_GRAPHICS, "Scene contents (from driver polygon count):");
    LOG_INFO(MOD_GRAPHICS, "  Zone mesh:    {:>6} polys", breakdown.zonePolys);
    LOG_INFO(MOD_GRAPHICS, "  Entities:     {:>6} polys ({} nodes)", entityPolyCount, breakdown.entityCount);
    LOG_INFO(MOD_GRAPHICS, "  Objects:      {:>6} polys ({} nodes)", objectPolyCount, breakdown.objectCount);
    LOG_INFO(MOD_GRAPHICS, "  Doors:        {:>6} polys ({} nodes)", doorPolyCount, breakdown.doorCount);
    LOG_INFO(MOD_GRAPHICS, "  Lights:       {:>6} nodes", lightCount);
    LOG_INFO(MOD_GRAPHICS, "  Total:        {:>6} polys ({} scene nodes)", totalPolys, totalSceneNodes);
    LOG_INFO(MOD_GRAPHICS, "");
    LOG_INFO(MOD_GRAPHICS, "Render time breakdown (avg of {} samples):", numSamples);
    LOG_INFO(MOD_GRAPHICS, "  Total drawAll:  {:>8} us (100.0%)", breakdown.totalDrawAll);
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Zone mesh:      {:>8} us ({:>5.1f}%)", breakdown.zoneTime, pct(breakdown.zoneTime));
    LOG_INFO(MOD_GRAPHICS, "  Entities:       {:>8} us ({:>5.1f}%)", breakdown.entityTime, pct(breakdown.entityTime));
    LOG_INFO(MOD_GRAPHICS, "  Objects:        {:>8} us ({:>5.1f}%)", breakdown.objectTime, pct(breakdown.objectTime));
    LOG_INFO(MOD_GRAPHICS, "  Doors:          {:>8} us ({:>5.1f}%)", breakdown.doorTime, pct(breakdown.doorTime));
    LOG_INFO(MOD_GRAPHICS, "  Lights:         {:>8} us ({:>5.1f}%)", lightTime, pct(lightTime));
    LOG_INFO(MOD_GRAPHICS, "  Baseline:       {:>8} us ({:>5.1f}%)", baseline, pct(baseline));
    LOG_INFO(MOD_GRAPHICS, "  Interaction:    {:>8} us ({:>5.1f}%)", breakdown.otherTime, pct(breakdown.otherTime));
    LOG_INFO(MOD_GRAPHICS, "=== END SCENE BREAKDOWN ===");

    sceneProfileEnabled_ = false;
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

// --- Player Mode Movement Implementation ---

void IrrlichtRenderer::updatePlayerMovement(float deltaTime) {
    // Check if chat has focus - skip movement keys if so
    bool chatHasFocus = windowManager_ && windowManager_->isChatInputFocused();

    // Read keyboard state for EQ-style movement (disabled when chat has focus)
    // Use HotkeyManager to check configured movement bindings
    auto& hotkeyMgr = eqt::input::HotkeyManager::instance();

    // Helper to check if any binding for an action is currently held
    auto isActionHeld = [&](eqt::input::HotkeyAction action) -> bool {
        if (chatHasFocus) return false;
        bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
        bool shiftHeld = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
        bool altHeld = eventReceiver_->isKeyDown(irr::KEY_LMENU) || eventReceiver_->isKeyDown(irr::KEY_RMENU);

        for (const auto& binding : hotkeyMgr.getBindingsForAction(action)) {
            if (!eventReceiver_->isKeyDown(binding.keyCode)) continue;

            bool needsCtrl = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Ctrl);
            bool needsShift = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Shift);
            bool needsAlt = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Alt);

            if (ctrlHeld == needsCtrl && shiftHeld == needsShift && altHeld == needsAlt) {
                return true;
            }
        }
        return false;
    };

    bool forward = isActionHeld(eqt::input::HotkeyAction::MoveForward);
    bool backward = isActionHeld(eqt::input::HotkeyAction::MoveBackward);
    bool turnLeft = isActionHeld(eqt::input::HotkeyAction::TurnLeft);
    bool turnRight = isActionHeld(eqt::input::HotkeyAction::TurnRight);
    bool strafeLeft = isActionHeld(eqt::input::HotkeyAction::StrafeLeft);
    bool strafeRight = isActionHeld(eqt::input::HotkeyAction::StrafeRight);
    bool jumpPressed = isActionHeld(eqt::input::HotkeyAction::Jump);
    bool swimUpPressed = isActionHeld(eqt::input::HotkeyAction::SwimUp);
    bool swimDownPressed = isActionHeld(eqt::input::HotkeyAction::SwimDown);

    // Update swimming input state
    playerMovement_.swimUp = swimUpPressed;
    playerMovement_.swimDown = swimDownPressed;

    // Update movement state
    playerMovement_.moveForward = forward || playerMovement_.autorun;
    playerMovement_.moveBackward = backward;
    // Strafing is disabled when swimming (EQ behavior)
    playerMovement_.strafeLeft = playerMovement_.isSwimming ? false : strafeLeft;
    playerMovement_.strafeRight = playerMovement_.isSwimming ? false : strafeRight;
    playerMovement_.turnLeft = turnLeft;
    playerMovement_.turnRight = turnRight;

    // Handle jump/swim up initiation
    if (playerMovement_.isSwimming) {
        // In water: Space = swim up, no jump
        // Vertical movement is handled in the swimming movement calculation below
    } else {
        // On land: Space = jump (only when on ground)
        if (jumpPressed && !playerMovement_.isJumping) {
            playerMovement_.isJumping = true;
            playerMovement_.verticalVelocity = playerMovement_.jumpVelocity;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Jump] Started jump with velocity {}", playerMovement_.verticalVelocity);
            }
        }
    }

    // Get current heading (EQ format: 0-512)
    float heading = playerHeading_;
    float oldHeading = heading;

    // Handle turning (smooth)
    if (turnLeft && !turnRight) {
        heading -= playerMovement_.turnSpeed * deltaTime * (512.0f / 360.0f);
    } else if (turnRight && !turnLeft) {
        heading += playerMovement_.turnSpeed * deltaTime * (512.0f / 360.0f);
    }

    // Mouse look - enabled when any of these conditions are met and UI doesn't have capture:
    // 1. Left mouse button held (current WillEQ behavior)
    // 2. Right mouse button held (traditional 3D game control)
    // 3. Ctrl+Left mouse button (single-button mouse workaround - Ctrl+Click = Right Click on Mac)
    bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
    bool mouseLookActive = (eventReceiver_->isLeftButtonDown() ||
                            eventReceiver_->isRightButtonDown() ||
                            (ctrlHeld && eventReceiver_->isLeftButtonDown())) &&
                           !windowManagerCapture_;

    if (mouseLookActive) {
        int mouseDeltaX = eventReceiver_->getMouseDeltaX();
        int mouseDeltaY = eventReceiver_->getMouseDeltaY();
        // Convert mouse delta X to heading change (sensitivity adjusted)
        heading += mouseDeltaX * 0.5f;
        // Mouse delta Y controls pitch (vertical look) - inverted so dragging up looks up
        playerPitch_ -= mouseDeltaY * 0.3f;
        // Clamp pitch to prevent camera flipping
        if (playerPitch_ > 89.0f) playerPitch_ = 89.0f;
        if (playerPitch_ < -89.0f) playerPitch_ = -89.0f;
    }

    // Normalize heading to 0-512
    while (heading < 0.0f) heading += 512.0f;
    while (heading >= 512.0f) heading -= 512.0f;

    // Convert heading to radians for movement calculation
    float headingRad = heading / 512.0f * 2.0f * static_cast<float>(M_PI);

    // Calculate movement vector
    float moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;

    if (playerMovement_.isSwimming) {
        // Swimming movement - use swim speeds, no strafing
        if (playerMovement_.moveForward) {
            float speed = playerMovement_.swimSpeed;
            moveX += std::sin(headingRad) * speed;
            moveY += std::cos(headingRad) * speed;
        }
        if (playerMovement_.moveBackward) {
            float speed = playerMovement_.swimBackwardSpeed;
            moveX -= std::sin(headingRad) * speed;
            moveY -= std::cos(headingRad) * speed;
        }
        // Strafing disabled in water (already handled above but being explicit)

        // Vertical movement in water
        if (playerMovement_.swimUp) {
            moveZ += playerMovement_.swimVerticalSpeed;
        }
        if (playerMovement_.swimDown) {
            moveZ -= playerMovement_.swimVerticalSpeed;
        }

        // Apply sinking when idle (unless swimming up or levitating)
        bool hasHorizontalMovement = playerMovement_.moveForward || playerMovement_.moveBackward;
        bool hasVerticalInput = playerMovement_.swimUp || playerMovement_.swimDown;
        if (!hasHorizontalMovement && !hasVerticalInput && !playerMovement_.isLevitating) {
            // Sink slowly when idle in water (unless levitating)
            moveZ -= playerMovement_.sinkRate;
        }
    } else {
        // Normal ground movement
        if (playerMovement_.moveForward) {
            float speed = playerMovement_.isRunning ? playerMovement_.runSpeed : playerMovement_.walkSpeed;
            moveX += std::sin(headingRad) * speed;
            moveY += std::cos(headingRad) * speed;
        }
        if (playerMovement_.moveBackward) {
            float speed = playerMovement_.backwardSpeed;
            moveX -= std::sin(headingRad) * speed;
            moveY -= std::cos(headingRad) * speed;
        }
        if (playerMovement_.strafeLeft) {
            float strafeRad = headingRad - static_cast<float>(M_PI) / 2.0f;
            moveX += std::sin(strafeRad) * playerMovement_.strafeSpeed;
            moveY += std::cos(strafeRad) * playerMovement_.strafeSpeed;
        }
        if (playerMovement_.strafeRight) {
            float strafeRad = headingRad + static_cast<float>(M_PI) / 2.0f;
            moveX += std::sin(strafeRad) * playerMovement_.strafeSpeed;
            moveY += std::cos(strafeRad) * playerMovement_.strafeSpeed;
        }
    }

    // Calculate proposed new position
    float newX = playerX_ + moveX * deltaTime;
    float newY = playerY_ + moveY * deltaTime;
    float newZ = playerZ_ + moveZ * deltaTime;

    // Apply jump/fall physics (only when not swimming)
    if (!playerMovement_.isSwimming && playerMovement_.isJumping) {
        // Apply gravity to vertical velocity
        playerMovement_.verticalVelocity -= playerMovement_.gravity * deltaTime;

        // Update Z position based on vertical velocity
        newZ += playerMovement_.verticalVelocity * deltaTime;

        LOG_TRACE(MOD_MOVEMENT, "Jump velocity={}, newZ={}", playerMovement_.verticalVelocity, newZ);
    }

    // Apply collision detection if available and we're actually moving
    bool positionChanged = false;
    bool isMoving = (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);
    bool isJumpMoving = playerMovement_.isJumping && !playerMovement_.isSwimming;
    bool isSwimMoving = playerMovement_.isSwimming && (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);

    if (isMoving || isJumpMoving || isSwimMoving) {
        // Debug: show movement attempt
        LOG_TRACE(MOD_MOVEMENT, "Attempting move: delta=({}, {}, {}) from ({}, {}, {}){}{}",
                  moveX * deltaTime, moveY * deltaTime, moveZ * deltaTime, playerX_, playerY_, playerZ_,
                  (isJumpMoving ? " [JUMPING]" : ""),
                  (isSwimMoving ? " [SWIMMING]" : ""));
        LOG_TRACE(MOD_MOVEMENT, "Collision: {}, Irrlicht: {}, Map: {}",
                  (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"),
                  (useIrrlichtCollision_ && !regionWorldTriangles_.empty() ? "YES" : "NO"),
                  (collisionMap_ ? "LOADED" : "NONE"));

        // Determine which collision system to use
        // BSP-filtered uses regionWorldTriangles_ directly, no meta selector needed
        bool useIrrlicht = useIrrlichtCollision_ && !regionWorldTriangles_.empty();
        bool useHCMap = collisionMap_ != nullptr;
        bool hasCollision = playerConfig_.collisionEnabled && (useIrrlicht || useHCMap);

        // Get the player's collision Z offset for ground snapping
        // Server Z represents the CENTER of the model, so when snapping to ground,
        // we need to offset by -collisionZOffset to place feet at ground level
        float modelYOffset = 0.0f;
        if (entityRenderer_) {
            modelYOffset = entityRenderer_->getPlayerCollisionZOffset();
        }

        if (hasCollision && useIrrlicht) {
            // --- Irrlicht-based collision (using actual zone geometry) ---
            // When BSP tree is available, use BSP-filtered collision (tests only source+dest
            // region selectors + doors/objects) instead of the full ~1900-region meta selector.
            bool useBspFiltered = zoneBspTree_ && !regionWorldTriangles_.empty();

            // Check horizontal collision first
            // Convert EQ coords to Irrlicht: EQ(x,y,z) -> Irr(x,z,y)
            float checkHeight = playerConfig_.collisionCheckHeight;
            irr::core::vector3df rayStart(playerX_, playerZ_ + checkHeight, playerY_);
            irr::core::vector3df rayEnd(newX, playerZ_ + checkHeight, newY);

            irr::core::vector3df hitPoint;
            irr::core::triangle3df hitTriangle;
            bool blocked = useBspFiltered
                ? checkCollisionBspFiltered(rayStart, rayEnd, hitPoint, hitTriangle)
                : checkCollisionIrrlicht(rayStart, rayEnd, hitPoint, hitTriangle);

            if (playerConfig_.collisionDebug) {
                // Extend the ray visually so it's easier to see (movement per frame is tiny)
                irr::core::vector3df direction = rayEnd - rayStart;
                float len = direction.getLength();
                if (len > 0.001f) {
                    direction.normalize();
                    // Extend to at least 10 units for visibility
                    irr::core::vector3df extendedEnd = rayStart + direction * std::max(len, 10.0f);

                    if (blocked) {
                        // Red line to hit point
                        addCollisionDebugLine(rayStart, hitPoint, irr::video::SColor(255, 255, 0, 0), 0.5f);
                        // Yellow cross at hit
                        float ms = 2.0f;
                        addCollisionDebugLine(irr::core::vector3df(hitPoint.X-ms, hitPoint.Y, hitPoint.Z),
                                              irr::core::vector3df(hitPoint.X+ms, hitPoint.Y, hitPoint.Z),
                                              irr::video::SColor(255, 255, 255, 0), 0.5f);
                        addCollisionDebugLine(irr::core::vector3df(hitPoint.X, hitPoint.Y-ms, hitPoint.Z),
                                              irr::core::vector3df(hitPoint.X, hitPoint.Y+ms, hitPoint.Z),
                                              irr::video::SColor(255, 255, 255, 0), 0.5f);
                        addCollisionDebugLine(irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z-ms),
                                              irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z+ms),
                                              irr::video::SColor(255, 255, 255, 0), 0.5f);
                        LOG_TRACE(MOD_MOVEMENT, "Horizontal BLOCKED at ({}, {}, {})", hitPoint.X, hitPoint.Y, hitPoint.Z);
                    } else {
                        // Green line for clear path (extended for visibility)
                        addCollisionDebugLine(rayStart, extendedEnd, irr::video::SColor(255, 0, 255, 0), 0.3f);
                    }
                }
            }

            if (!blocked) {
                // Path is clear, now find ground at new position
                float groundZ = useBspFiltered
                    ? findGroundZBspFiltered(newX, newY, newZ, modelYOffset)
                    : findGroundZIrrlicht(newX, newY, newZ, modelYOffset);

                LOG_TRACE(MOD_MOVEMENT, "Ground at target: {}, playerZ: {}, newZ: {}", groundZ, playerZ_, newZ);

                if (playerMovement_.isJumping) {
                    // While jumping, check if we've landed
                    // Compare feet position (centerZ - modelYOffset) with ground
                    float feetZ = newZ - modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    // Allow horizontal movement while in air
                    positionChanged = true;
                } else if (playerMovement_.isSwimming) {
                    // Swimming - no ground snap, but stop at floor/ceiling
                    // Player sinks slowly (handled by sinkRate in movement calc)

                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_) {
                        // Cast ray from current head to target head position
                        // Head is approximately modelYOffset above center (same as feet are below)
                        float headHeight = modelYOffset;
                        // Irrlicht coords: (x, z, y) where Irrlicht Y = EQ Z
                        irr::core::vector3df headStart(playerX_, playerZ_ + headHeight, playerY_);
                        irr::core::vector3df headEnd(newX, newZ + headHeight, newY);
                        irr::core::vector3df ceilingHit;
                        irr::core::triangle3df ceilingTri;

                        float deltaZ = newZ - playerZ_;
                        LOG_DEBUG(MOD_MOVEMENT, "[Swim] Ceiling check: from Z={} to Z={} (delta={}), ray ({},{},{}) -> ({},{},{})",
                            playerZ_, newZ, deltaZ, headStart.X, headStart.Y, headStart.Z, headEnd.X, headEnd.Y, headEnd.Z);

                        if ((useBspFiltered ? checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)
                                             : checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri))) {
                            // Hit ceiling - clamp newZ to just below the hit point
                            // ceilingHit.Y is in Irrlicht coords (EQ Z)
                            // Player center should be headHeight below the ceiling hit
                            float maxZ = ceilingHit.Y - headHeight - 0.1f;  // Small buffer
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling at {}, clamping newZ from {} to {}", ceilingHit.Y, newZ, maxZ);
                            if (newZ > maxZ) {
                                newZ = maxZ;
                            }
                        } else {
                            LOG_DEBUG(MOD_MOVEMENT, "[Swim] Ceiling check: no collision detected");
                        }
                    }

                    // Check if we'd go below ground and clamp if so
                    // Note: findGroundZIrrlicht returns feetZ+1000 as a "blocked" sentinel when hitting a ceiling
                    // We need to ignore that sentinel value for floor clamping
                    float feetZ = newZ - modelYOffset;
                    float maxReasonableGround = playerZ_ + 100.0f;  // Ground can't reasonably be 100+ units above current position
                    if (groundZ < maxReasonableGround && feetZ < groundZ) {
                        // Would sink below ground - stop at ground level
                        newZ = groundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Normal ground movement - check step height
                    // Compare feet positions: current feet vs target ground
                    float currentFeetZ = playerZ_ - modelYOffset;
                    float stepHeight = groundZ - currentFeetZ;

                    // Limit both stepping UP and stepping DOWN
                    // Small steps down (e.g., stairs) are OK, but large drops should trigger falling
                    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;  // Allow stepping down ~2x step height
                    if (stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
                        // Snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        positionChanged = true;
                    } else if (stepHeight < -maxStepDown) {
                        // Large drop detected - start falling instead of snapping
                        playerMovement_.isJumping = true;
                        playerMovement_.verticalVelocity = 0.0f;  // Start with 0 velocity (just walked off edge)
                        positionChanged = true;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Irrlicht] Walked off edge, drop={}, starting fall", -stepHeight);
                        }
                    } else if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[Irrlicht] Step up too high: {}", stepHeight);
                    }
                }
            } else {
                // Horizontal movement blocked - try wall sliding or continue jump/swim vertically
                if (playerMovement_.isJumping) {
                    // Even if horizontal is blocked, continue vertical jump movement
                    float groundZ = useBspFiltered
                        ? findGroundZBspFiltered(playerX_, playerY_, newZ, modelYOffset)
                        : findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
                    float feetZ = newZ - modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (blocked horizontal) at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    // Keep X/Y at current position but update Z
                    newX = playerX_;
                    newY = playerY_;
                    positionChanged = true;
                } else if (playerMovement_.isSwimming) {
                    // Horizontal blocked while swimming - allow vertical movement at current X/Y
                    newX = playerX_;
                    newY = playerY_;

                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_) {
                        float headHeight = modelYOffset;
                        irr::core::vector3df headStart(playerX_, playerZ_ + headHeight, playerY_);
                        irr::core::vector3df headEnd(playerX_, newZ + headHeight, playerY_);
                        irr::core::vector3df ceilingHit;
                        irr::core::triangle3df ceilingTri;

                        if ((useBspFiltered ? checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)
                                             : checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri))) {
                            float maxZ = ceilingHit.Y - headHeight - 0.1f;
                            if (newZ > maxZ) {
                                newZ = maxZ;
                                if (playerConfig_.collisionDebug) {
                                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (blocked horiz) at {}, clamped to serverZ={}", ceilingHit.Y, newZ);
                                }
                            }
                        }
                    }

                    // Check floor collision
                    // Note: findGroundZ returns feetZ+1000 as a "blocked" sentinel when hitting a ceiling
                    float groundZ = useBspFiltered
                        ? findGroundZBspFiltered(playerX_, playerY_, newZ, modelYOffset)
                        : findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
                    float feetZ = newZ - modelYOffset;
                    float maxReasonableGround = playerZ_ + 100.0f;
                    if (groundZ < maxReasonableGround && feetZ < groundZ) {
                        newZ = groundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (blocked horiz) at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Try wall sliding - X only
                    irr::core::vector3df rayEndX(newX, playerZ_ + checkHeight, playerY_);
                    if (!(useBspFiltered ? checkCollisionBspFiltered(rayStart, rayEndX, hitPoint, hitTriangle)
                                         : checkCollisionIrrlicht(rayStart, rayEndX, hitPoint, hitTriangle))) {
                        float groundZ = useBspFiltered
                            ? findGroundZBspFiltered(newX, playerY_, playerZ_, modelYOffset)
                            : findGroundZIrrlicht(newX, playerY_, playerZ_, modelYOffset);
                        float currentFeetZ = playerZ_ - modelYOffset;
                        float stepHeight = groundZ - currentFeetZ;
                        // Only limit stepping UP - can always step down
                        if (stepHeight <= playerConfig_.collisionStepHeight) {
                            newY = playerY_;
                            newZ = groundZ + modelYOffset;
                            positionChanged = true;
                            LOG_TRACE(MOD_MOVEMENT, "Wall slide X");
                        }
                    }
                    // Try wall sliding - Y only
                    if (!positionChanged) {
                        irr::core::vector3df rayEndY(playerX_, playerZ_ + checkHeight, newY);
                        if (!(useBspFiltered ? checkCollisionBspFiltered(rayStart, rayEndY, hitPoint, hitTriangle)
                                              : checkCollisionIrrlicht(rayStart, rayEndY, hitPoint, hitTriangle))) {
                            float groundZ = useBspFiltered
                                ? findGroundZBspFiltered(playerX_, newY, playerZ_, modelYOffset)
                                : findGroundZIrrlicht(playerX_, newY, playerZ_, modelYOffset);
                            float currentFeetZ = playerZ_ - modelYOffset;
                            float stepHeight = groundZ - currentFeetZ;
                            // Only limit stepping UP - can always step down
                            if (stepHeight <= playerConfig_.collisionStepHeight) {
                                newX = playerX_;
                                newZ = groundZ + modelYOffset;
                                positionChanged = true;
                                LOG_TRACE(MOD_MOVEMENT, "Wall slide Y");
                            }
                        }
                    }
                }
                if (!positionChanged && playerConfig_.collisionDebug) {
                    LOG_TRACE(MOD_MOVEMENT, "BLOCKED - no movement");
                }
            }
        } else if (hasCollision && useHCMap) {
            // --- HCMap-based collision (server map file) ---
            float targetGroundZ = findGroundZ(newX, newY, newZ);

            LOG_TRACE(MOD_MOVEMENT, "HCMap Move from ({}, {}, {}) to ({}, {}), groundZ={}", playerX_, playerY_, playerZ_, newX, newY, targetGroundZ);

            if (playerMovement_.isJumping) {
                // Handle jumping with HCMap
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, newZ);
                if (losCheck) {
                    // Check if we've landed (compare feet position with ground)
                    float feetZ = newZ - modelYOffset;
                    if (targetGroundZ != BEST_Z_INVALID && feetZ <= targetGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = targetGroundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap) at groundZ={}, serverZ={}", targetGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Horizontal blocked but continue vertical movement
                    newX = playerX_;
                    newY = playerY_;
                    float currentGroundZ = findGroundZ(playerX_, playerY_, newZ);
                    float feetZ = newZ - modelYOffset;
                    if (currentGroundZ != BEST_Z_INVALID && feetZ <= currentGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = currentGroundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap, blocked) at groundZ={}, serverZ={}", currentGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                }
            } else if (playerMovement_.isSwimming) {
                // Swimming with HCMap - no ground snap, but stop at floor/ceiling
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, newZ);
                if (losCheck) {
                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_ && collisionMap_) {
                        // Cast ray from current head to target head position
                        float headHeight = modelYOffset;
                        glm::vec3 headStart(playerX_, playerY_, playerZ_ + headHeight);
                        glm::vec3 headEnd(newX, newY, newZ + headHeight);
                        glm::vec3 ceilingHit;

                        if (!collisionMap_->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
                            // Hit ceiling - clamp newZ to just below the hit point
                            float maxZ = ceilingHit.z - headHeight - 0.1f;  // Small buffer
                            if (newZ > maxZ) {
                                newZ = maxZ;
                                if (playerConfig_.collisionDebug) {
                                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (HCMap) at {}, clamped to serverZ={}", ceilingHit.z, newZ);
                                }
                            }
                        }
                    }

                    float feetZ = newZ - modelYOffset;
                    if (targetGroundZ != BEST_Z_INVALID && feetZ < targetGroundZ) {
                        // Would sink below ground - stop at ground level
                        newZ = targetGroundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (HCMap) at groundZ={}, serverZ={}", targetGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Horizontal blocked, allow vertical movement at current X/Y
                    newX = playerX_;
                    newY = playerY_;

                    // Check ceiling collision for vertical-only movement when swimming up
                    if (newZ > playerZ_ && collisionMap_) {
                        float headHeight = modelYOffset;
                        glm::vec3 headStart(playerX_, playerY_, playerZ_ + headHeight);
                        glm::vec3 headEnd(playerX_, playerY_, newZ + headHeight);
                        glm::vec3 ceilingHit;

                        if (!collisionMap_->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
                            float maxZ = ceilingHit.z - headHeight - 0.1f;
                            if (newZ > maxZ) {
                                newZ = maxZ;
                                if (playerConfig_.collisionDebug) {
                                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (HCMap, blocked horiz) at {}, clamped to serverZ={}", ceilingHit.z, newZ);
                                }
                            }
                        }
                    }

                    float currentGroundZ = findGroundZ(playerX_, playerY_, newZ);
                    float feetZ = newZ - modelYOffset;
                    if (currentGroundZ != BEST_Z_INVALID && feetZ < currentGroundZ) {
                        newZ = currentGroundZ + modelYOffset;
                    }
                    positionChanged = true;
                }
            } else if (targetGroundZ != BEST_Z_INVALID) {
                // Compare feet positions for step height
                float currentFeetZ = playerZ_ - modelYOffset;
                float stepHeight = targetGroundZ - currentFeetZ;

                LOG_TRACE(MOD_MOVEMENT, "HCMap Step height: {} (max up: {})", stepHeight, playerConfig_.collisionStepHeight);

                // Limit both stepping UP and stepping DOWN
                float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;
                if (stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
                    bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, targetGroundZ);

                    LOG_TRACE(MOD_MOVEMENT, "HCMap LOS check: {}", (losCheck ? "CLEAR" : "BLOCKED"));

                    if (losCheck) {
                        newZ = targetGroundZ + modelYOffset;
                        positionChanged = true;
                    } else {
                        // Wall sliding for HCMap
                        float xGroundZ = findGroundZ(newX, playerY_, playerZ_);
                        float xStepHeight = xGroundZ - currentFeetZ;
                        if (xGroundZ != BEST_Z_INVALID &&
                            xStepHeight <= playerConfig_.collisionStepHeight && xStepHeight >= -maxStepDown &&
                            checkMovementCollision(playerX_, playerY_, playerZ_, newX, playerY_, xGroundZ)) {
                            newY = playerY_;
                            newZ = xGroundZ + modelYOffset;
                            positionChanged = true;
                        } else {
                            float yGroundZ = findGroundZ(playerX_, newY, playerZ_);
                            float yStepHeight = yGroundZ - currentFeetZ;
                            if (yGroundZ != BEST_Z_INVALID &&
                                yStepHeight <= playerConfig_.collisionStepHeight && yStepHeight >= -maxStepDown &&
                                checkMovementCollision(playerX_, playerY_, playerZ_, playerX_, newY, yGroundZ)) {
                                newX = playerX_;
                                newZ = yGroundZ + modelYOffset;
                                positionChanged = true;
                            }
                        }
                    }
                } else if (stepHeight < -maxStepDown) {
                    // Large drop detected - start falling instead of snapping
                    playerMovement_.isJumping = true;
                    playerMovement_.verticalVelocity = 0.0f;
                    positionChanged = true;
                    if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[HCMap] Walked off edge, drop={}, starting fall", -stepHeight);
                    }
                } else {
                    LOG_TRACE(MOD_MOVEMENT, "HCMap Step up too high ({}) - blocked", stepHeight);
                }
            } else {
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, playerZ_);
                if (losCheck) {
                    positionChanged = true;
                }
            }
        } else {
            // No collision map or collision disabled - allow all movement
            positionChanged = true;

            // Handle jump landing without collision detection
            // Use a simple ground plane at the starting Z or a minimum height
            if (playerMovement_.isJumping && playerMovement_.verticalVelocity <= 0) {
                // Without collision, assume flat ground - land when we return to starting height or below
                // Use a minimum ground level to prevent falling forever
                float minGroundZ = -1000.0f;  // Reasonable minimum
                if (newZ <= playerZ_ || newZ <= minGroundZ) {
                    // Keep the current ground level or use minimum
                    newZ = std::max(playerZ_, minGroundZ);
                    playerMovement_.isJumping = false;
                    playerMovement_.verticalVelocity = 0.0f;
                    if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (no collision) at Z={}", newZ);
                    }
                }
            }

            if (playerConfig_.collisionDebug) {
                if (!collisionMap_) {
                    LOG_TRACE(MOD_MOVEMENT, "No collision map - movement allowed");
                } else {
                    LOG_TRACE(MOD_MOVEMENT, "Collision DISABLED - movement allowed");
                }
                // Draw a white line showing the intended movement path (no collision check)
                irr::core::vector3df irrFrom(playerX_, playerZ_ + 3.0f, playerY_);
                irr::core::vector3df irrTo(newX, newZ + 3.0f, newY);
                addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 255), 0.2f);
            }
        }
    }

    // Check if heading changed (with small epsilon for float comparison)
    bool headingChanged = (std::abs(heading - oldHeading) > 0.001f);

    // Update position if movement occurred
    if (positionChanged) {
        playerX_ = newX;
        playerY_ = newY;
        playerZ_ = newZ;
    }

    // Always update heading if it changed (separate from position)
    if (headingChanged) {
        playerHeading_ = heading;
    }

    // Update camera and entity position if anything changed
    if (positionChanged || headingChanged) {
        LOG_TRACE(MOD_MOVEMENT, "Position updated: ({}, {}, {}) heading={}", playerX_, playerY_, playerZ_, playerHeading_);

        // Update player entity position (for third-person view)
        if (entityRenderer_) {
            entityRenderer_->updatePlayerEntityPosition(playerX_, playerY_, playerZ_, playerHeading_);
        }

        // Update camera based on camera mode
        if (camera_ && cameraMode_ == CameraMode::FirstPerson) {
            // First-person: camera at head bone position to track model animation
            float camX = playerX_;
            float camY = playerY_;
            float camZ = playerZ_ + 5.0f;  // Default fallback if no head bone

            // Try to get head bone position from the animated player model
            // This allows the camera to track head movement during animations
            bool gotHeadBone = false;
            if (entityRenderer_) {
                float headX, headY, headZ;
                if (entityRenderer_->getPlayerHeadBonePosition(headX, headY, headZ)) {
                    // Use head bone position for camera (adds head bob/sway from animation)
                    camX = headX;
                    camY = headY;
                    camZ = headZ;
                    gotHeadBone = true;
                }
            }

            // If no head bone, compute eye height from model bounding box
            // playerZ_ is feet position, so camera is at feet + eye height
            if (!gotHeadBone && entityRenderer_) {
                float eyeHeightFromFeet = entityRenderer_->getPlayerEyeHeightFromFeet();
                camZ = playerZ_ + eyeHeightFromFeet;

                // Debug logging
                static int fallbackLogCount = 0;
                if (fallbackLogCount++ % 500 == 0) {
                    LOG_DEBUG(MOD_GRAPHICS, "Camera fallback: playerZ(feet)={:.2f} + eyeHeight={:.2f} => camZ={:.2f} (before adjust)",
                              playerZ_, eyeHeightFromFeet, camZ);
                }
            }

            // Apply eye height offset (user-adjustable with Y/Shift+Y)
            camZ += playerConfig_.eyeHeight;

            // EQ coords: (x, y, z) -> Irrlicht coords: (x, z, y)
            camera_->setPosition(irr::core::vector3df(camX, camZ, camY));

            // Calculate look direction from playerHeading_ (yaw) and playerPitch_ (pitch)
            float lookRad = playerHeading_ / 512.0f * 2.0f * static_cast<float>(M_PI);
            float pitchRad = playerPitch_ * static_cast<float>(M_PI) / 180.0f;
            float cosPitch = std::cos(pitchRad);
            float sinPitch = std::sin(pitchRad);
            irr::core::vector3df target(
                camX + std::sin(lookRad) * cosPitch * 100.0f,
                camZ + sinPitch * 100.0f,
                camY + std::cos(lookRad) * cosPitch * 100.0f
            );
            camera_->setTarget(target);
        } else if (cameraMode_ == CameraMode::Follow && cameraController_) {
            // Follow mode: third-person camera behind player
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_, deltaTime);
        }
    }

    // Movement state tracking and server sync - runs every frame to detect stops
    // This must be OUTSIDE the position/heading changed block to detect when player stops
    bool hasMovementInput = playerMovement_.moveForward || playerMovement_.moveBackward ||
                            playerMovement_.strafeLeft || playerMovement_.strafeRight;

    // Track movement state transitions to detect when player stops
    static bool hadMovementInput = false;
    bool stoppedMoving = hadMovementInput && !hasMovementInput;
    hadMovementInput = hasMovementInput;

    // Track previous position for velocity calculation
    static float prevX = playerX_, prevY = playerY_, prevZ = playerZ_;

    // Throttle callback invocations to ~250ms to match working client behavior
    // This prevents jerky motion when viewed by other players
    static auto lastCallbackTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCallbackTime);

    // Notify EverQuest class of position/heading change (for server sync)
    // Throttle to 250ms minimum between callbacks (working client averages ~275ms)
    // Exception: allow immediate callback when player stops (so others see us stop promptly)
    bool shouldCallback = movementCallback_ &&
                          (positionChanged || headingChanged || stoppedMoving) &&
                          (stoppedMoving || elapsed.count() >= 250);

    if (shouldCallback) {
        PlayerPositionUpdate update;
        update.x = playerX_;
        update.y = playerY_;
        update.z = playerZ_;
        update.heading = heading;  // EQ format (0-512)

        // When player stops, send zero deltas to ensure server knows we've stopped
        // This triggers anim=0 in OnGraphicsMovement
        if (stoppedMoving) {
            update.dx = 0.0f;
            update.dy = 0.0f;
            update.dz = 0.0f;
        } else {
            update.dx = playerX_ - prevX;
            update.dy = playerY_ - prevY;
            update.dz = playerZ_ - prevZ;
        }
        movementCallback_(update);

        // D14: Also push movement intent to bridge (dual path with callback)
        if (bridge_) {
            eqt::events::PlayerPositionChanged intent;
            intent.x = update.x;
            intent.y = update.y;
            intent.z = update.z;
            intent.heading = update.heading;
            intent.dx = update.dx;
            intent.dy = update.dy;
            intent.dz = update.dz;
            bridge_->pushIntent(std::move(intent));
        }

        lastCallbackTime = now;

        // Update previous position only when we actually send an update
        prevX = playerX_;
        prevY = playerY_;
        prevZ = playerZ_;
    }

    // Update player entity animation based on movement state (runs every frame)
    if (entityRenderer_) {
        // Jump animation takes priority (playThrough) - but not when swimming
        if (playerMovement_.isJumping && playerMovement_.verticalVelocity > 0 && !playerMovement_.isSwimming) {
            // Only trigger jump animation on the way up (ascending)
            // Use l03 for running jump, l04 for standing jump
            if (hasMovementInput) {
                entityRenderer_->setPlayerEntityAnimation("l03", false, 0.0f, true);  // Running jump
            } else {
                entityRenderer_->setPlayerEntityAnimation("l04", false, 0.0f, true);  // Standing jump
            }
        } else if (playerMovement_.isSwimming) {
            // Swimming animations
            bool hasSwimMovement = hasMovementInput || playerMovement_.swimUp || playerMovement_.swimDown;
            if (hasSwimMovement) {
                entityRenderer_->setPlayerEntityAnimation("l09", true, playerMovement_.swimSpeed);  // Swim treading/moving
            } else {
                entityRenderer_->setPlayerEntityAnimation("p06", true);  // Swim idle
            }
        } else if (hasMovementInput) {
            // Use run animation for forward movement when running, walk for everything else
            // Pass movement speed to match animation speed to actual movement
            float speed = playerMovement_.isRunning ? playerMovement_.runSpeed : playerMovement_.walkSpeed;
            if (playerMovement_.moveForward && playerMovement_.isRunning) {
                entityRenderer_->setPlayerEntityAnimation("l02", true, speed);  // Run
            } else {
                entityRenderer_->setPlayerEntityAnimation("l01", true, speed);  // Walk
            }
        } else {
            entityRenderer_->setPlayerEntityAnimation("p01", true);  // Idle
        }
    }
}

bool IrrlichtRenderer::checkMovementCollision(float fromX, float fromY, float fromZ,
                                               float toX, float toY, float toZ) {
    if (!collisionMap_) {
        return true;  // No collision map = allow movement
    }

    // Check line of sight from current position to new position
    // Use configurable check height for collision detection
    float checkHeight = playerConfig_.collisionCheckHeight;
    glm::vec3 from(fromX, fromY, fromZ + checkHeight);
    glm::vec3 to(toX, toY, toZ + checkHeight);

    glm::vec3 hitLocation;
    bool clear = collisionMap_->CheckLOSWithHit(from, to, &hitLocation);

    // Add debug visualization if enabled
    if (playerConfig_.collisionDebug) {
        // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
        irr::core::vector3df irrFrom(from.x, from.z, from.y);
        irr::core::vector3df irrTo(to.x, to.z, to.y);

        if (clear) {
            // Green line for clear path
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 0), 0.2f);
        } else {
            // Red line from start to hit point
            irr::core::vector3df irrHit(hitLocation.x, hitLocation.z, hitLocation.y);
            addCollisionDebugLine(irrFrom, irrHit, irr::video::SColor(255, 255, 0, 0), 0.5f);

            // Yellow sphere marker at hit point (draw as short lines forming a cross)
            float markerSize = 1.0f;
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X - markerSize, irrHit.Y, irrHit.Z),
                irr::core::vector3df(irrHit.X + markerSize, irrHit.Y, irrHit.Z),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X, irrHit.Y - markerSize, irrHit.Z),
                irr::core::vector3df(irrHit.X, irrHit.Y + markerSize, irrHit.Z),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X, irrHit.Y, irrHit.Z - markerSize),
                irr::core::vector3df(irrHit.X, irrHit.Y, irrHit.Z + markerSize),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
        }
    }

    return clear;
}

float IrrlichtRenderer::findGroundZ(float x, float y, float currentZ) {
    if (!collisionMap_) {
        return currentZ;
    }

    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // PHASE 1: Short raycast to find ground near current level
    // This prevents falling through mesh gaps by looking for nearby ground first
    // Start from max step-up above current position
    glm::vec3 nearPos(x, y, currentZ + maxStepUp);
    glm::vec3 nearResult;
    float nearGroundZ = collisionMap_->FindBestZ(nearPos, &nearResult);

    // Check if we found ground within the nearby range (max step-up to max step-down)
    if (nearGroundZ != BEST_Z_INVALID) {
        float heightDiff = nearGroundZ - currentZ;
        if (heightDiff >= -maxStepDown && heightDiff <= maxStepUp) {
            // Found nearby ground - use it
            if (playerConfig_.collisionDebug) {
                // Green line showing nearby ground hit (preferred)
                irr::core::vector3df irrFrom(x, currentZ + maxStepUp, y);
                irr::core::vector3df irrTo(x, nearGroundZ, y);
                addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 128), 0.2f);
                float markerSize = 0.5f;
                addCollisionDebugLine(
                    irr::core::vector3df(irrTo.X - markerSize, irrTo.Y, irrTo.Z),
                    irr::core::vector3df(irrTo.X + markerSize, irrTo.Y, irrTo.Z),
                    irr::video::SColor(255, 0, 255, 128), 0.2f);
            }
            return nearGroundZ;
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    // This handles cases like jumping off ledges, falling, etc.
    glm::vec3 pos(x, y, currentZ + 10.0f);  // Start slightly above
    glm::vec3 result;
    float groundZ = collisionMap_->FindBestZ(pos, &result);

    // Add debug visualization if enabled
    if (playerConfig_.collisionDebug) {
        // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
        irr::core::vector3df irrFrom(x, currentZ + 10.0f, y);

        if (groundZ != BEST_Z_INVALID) {
            // Cyan line showing ground ray hit
            irr::core::vector3df irrTo(x, groundZ, y);
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 255), 0.2f);

            // Small cyan cross at ground point
            float markerSize = 0.5f;
            addCollisionDebugLine(
                irr::core::vector3df(irrTo.X - markerSize, irrTo.Y, irrTo.Z),
                irr::core::vector3df(irrTo.X + markerSize, irrTo.Y, irrTo.Z),
                irr::video::SColor(255, 0, 255, 255), 0.2f);
            addCollisionDebugLine(
                irr::core::vector3df(irrTo.X, irrTo.Y, irrTo.Z - markerSize),
                irr::core::vector3df(irrTo.X, irrTo.Y, irrTo.Z + markerSize),
                irr::video::SColor(255, 0, 255, 255), 0.2f);
        } else {
            // Magenta line showing no ground found (ray going down)
            irr::core::vector3df irrTo(x, currentZ - 50.0f, y);
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 0, 255), 0.2f);
        }
    }

    // Check for boat collision — only when boats exist in zone
    if (entityRenderer_ && entityRenderer_->hasBoatsInZone()) {
        float boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, currentZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (groundZ == BEST_Z_INVALID || boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    // Yellow line showing boat deck hit
                    irr::core::vector3df irrFrom(x, currentZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return boatDeckZ;
            }
        }
    }

    if (groundZ == BEST_Z_INVALID) {
        return currentZ;  // No ground found, keep current Z
    }

    return groundZ;
}

// --- Irrlicht-based Collision Detection (using zone mesh) ---

void IrrlichtRenderer::setupMinimalZoneCollision() {
    // Clean up old collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    // Legacy meta selectors (only used by HCMap fallback path now)
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    if (!smgr_) {
        return;
    }

    // Build player's BSP region mesh synchronously (only if not already loaded)
    size_t playerRegion = SIZE_MAX;
    if (zoneBspTree_) {
        playerRegion = zoneBspTree_->findRegionIndexForPoint(playerX_, playerY_, playerZ_);
        bool alreadyBuilt = regionMeshNodes_.count(playerRegion) && regionMeshNodes_[playerRegion];
        if (playerRegion != SIZE_MAX && !alreadyBuilt &&
            (!constrainedMeshCache_ || !constrainedMeshCache_->isLoaded(playerRegion))) {
            rebuildRegionMesh(playerRegion);
            LOG_INFO(MOD_GRAPHICS, "Built player BSP region {} synchronously for deferred loading", playerRegion);
        }
    }

    // Build pre-transformed world-space triangles per region for BSP-filtered collision
    // All collision paths (movement, detail, footprints, LOS) use these via BSP region lookup
    // Direct Möller–Trumbore ray-triangle intersection — no matrix math per query
    size_t regionsAdded = 0;
    size_t totalRegionTriangles = 0;
    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (!node || !node->getMesh()) continue;
        auto triangles = extractWorldTriangles(smgr_, node);
        if (!triangles.empty()) {
            totalRegionTriangles += triangles.size();
            regionsAdded++;
            regionWorldTriangles_[regionIdx] = std::move(triangles);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Collision setup: {} regions (total {} world-space triangles)",
             regionsAdded, totalRegionTriangles);

    collisionManager_ = smgr_->getSceneCollisionManager();

    // Camera collision uses BSP directly (no triangle selectors)

    // Activate progressive loading
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();

    // NOTE: EntityPrepWorker is NOT created here — it is created during the
    // P08_Entities phase (beginZoneAssetLoad) which runs after P09_Collision.
    // Creating it here would bypass the manual load gate.

    // Start background icon sheet worker if not already started
    if (windowManager_ && config_.constrainedConfig.enableItemIcons) {
        windowManager_->getIconLoader().setBackgroundThreadPool(backgroundThreadPool_.get());
        windowManager_->getIconLoader().startWorker();
    }

    LOG_INFO(MOD_GRAPHICS, "Minimal collision setup complete, progressive loading activated");
}

size_t IrrlichtRenderer::findBspRegionForPoint(float x, float y, float z) {
    if (zoneBspTree_) {
        return zoneBspTree_->findRegionIndexForPoint(x, y, z);
    }
    return SIZE_MAX;
}

void IrrlichtRenderer::addRegionToCollision(size_t regionIdx) {
    if (!smgr_) return;

    auto regionIt = regionMeshNodes_.find(regionIdx);
    if (regionIt == regionMeshNodes_.end() || !regionIt->second || !regionIt->second->getMesh()) return;

    auto triangles = extractWorldTriangles(smgr_, regionIt->second);
    if (!triangles.empty()) {
        regionWorldTriangles_[regionIdx] = std::move(triangles);
    }
}

void IrrlichtRenderer::addDoorToCollision(uint8_t doorId) {
    if (!smgr_ || !doorManager_) return;

    const auto* door = doorManager_->getDoor(doorId);
    if (!door || !door->sceneNode || !door->sceneNode->getMesh() || !door->pivotNode) return;

    DoorCollisionData data;
    data.bspRegion = door->bspRegion;

    // Classify door type — skip collision for spinning, lifts, hazardous, invisible, teleporters
    uint8_t ot = door->opentype;
    bool isSpinning = (ot >= 30 && ot <= 44) || (ot >= 100 && ot <= 107);
    bool isLift = (ot == 59 || ot == 60);
    bool isHazardous = (ot >= 115 && ot <= 152);
    bool isInvisible = (ot == 50 || ot == 53 || ot == 54 || ot == 57);
    if (isSpinning || isLift || isHazardous || isInvisible) {
        data.hasCollision = false;
        doorCollisionData_[doorId] = std::move(data);
        LOG_DEBUG(MOD_GRAPHICS, "Door {} opentype={}: no collision ({})",
                  doorId, ot, isSpinning ? "spinning" : isLift ? "lift" : isHazardous ? "hazardous" : "invisible");
        return;
    }

    // Save current pivot rotation
    irr::core::vector3df savedRotation = door->pivotNode->getRotation();

    // Extract CLOSED state triangles
    float closedIrrRot = -door->openHeading * 360.0f / 512.0f + 90.0f;
    door->pivotNode->setRotation(irr::core::vector3df(0, closedIrrRot, 0));
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();
    data.closedTriangles = extractWorldTriangles(smgr_, door->sceneNode);

    // Extract OPEN state triangles
    float openIrrRot = -door->closedHeading * 360.0f / 512.0f + 90.0f;
    door->pivotNode->setRotation(irr::core::vector3df(0, openIrrRot, 0));
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();
    data.openTriangles = extractWorldTriangles(smgr_, door->sceneNode);

    // Restore original rotation
    door->pivotNode->setRotation(savedRotation);
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();

    LOG_DEBUG(MOD_GRAPHICS, "Door {} opentype={} region={}: {} closed tris, {} open tris",
              doorId, ot, data.bspRegion, data.closedTriangles.size(), data.openTriangles.size());

    doorCollisionData_[doorId] = std::move(data);
}

void IrrlichtRenderer::addObjectToCollision(size_t objIdx) {
    if (!smgr_) return;
    if (objIdx >= objectNodes_.size()) return;

    auto* node = objectNodes_[objIdx];
    if (!node || !node->getMesh()) return;

    auto triangles = extractWorldTriangles(smgr_, node);
    if (triangles.empty()) return;

    // Determine BSP region for this object
    size_t region = SIZE_MAX;
    if (objIdx < objectRegions_.size()) {
        region = objectRegions_[objIdx];
    }

    // Append to per-region object triangles
    auto& regionTris = objectWorldTriangles_[region];
    regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
}

void IrrlichtRenderer::processFrameProgressiveLoad() {
    if (!progressiveLoadingActive_) return;

    // Promote background-preloaded model data to main cache.
    // This moves RaceModelData from the staging map to loadedModels_ so that
    // getMeshForRace() can find it. Must happen before any buildEntityMesh() calls.
    if (entityPrepReady_ && entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->promotePreparedModels();
    }

    // --- Critical tasks: always proceed regardless of governor state ---

    // P1: Player's BSP region (must have ground to stand on)
    if (constrainedMeshCache_ && currentPvsRegion_ != SIZE_MAX &&
        !constrainedMeshCache_->isLoaded(currentPvsRegion_)) {
        LOG_INFO(MOD_GRAPHICS, "Progressive P1: building CRITICAL player region {} (not loaded in cache)",
                 currentPvsRegion_);
        bool ok = rebuildRegionMesh(currentPvsRegion_);
        if (ok) {
            addRegionToCollision(currentPvsRegion_);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1: SUCCESS built player region {} + collision",
                     currentPvsRegion_);
        } else {
            LOG_ERROR(MOD_GRAPHICS, "Progressive P1: FAILED to build player region {}!",
                      currentPvsRegion_);
        }
    }

    // Player entity goes through processOneEntityBuildStep() like other entities
    // (no synchronous build here — avoids 7.6s render thread stall from disk I/O)

    // Update loading screen progress during asset loading.
    // --- GREEN gate: exactly ONE non-critical step per frame when GREEN ---
    // Bypass when loading screen is visible (no scene to compete with).
    if (governor_ && !loadingScreenVisible_ && governor_->getState() != BudgetState::Green) {
        static int progGovLog = 0;
        if (++progGovLog % 300 == 1) {
            size_t qNeedBuild = 0;
            for (const auto& e : meshLoadQueue_)
                if (constrainedMeshCache_ && !constrainedMeshCache_->isLoaded(e.regionIdx))
                    qNeedBuild++;
            LOG_DEBUG(MOD_GRAPHICS, "Progressive: GREEN gate blocked — governor={} | "
                      "meshLoadQueue_.size={} needBuild={}",
                      governor_->getStateName(), meshLoadQueue_.size(), qNeedBuild);
        }
        // Populate pending queue even when not GREEN (zero-cost, main-thread-only).
        // Do NOT dispatch — worker stays idle during non-GREEN frames.
        if (++entityPrepScanCounter_ % 10 == 0) {
            queueEntityPrepRequests();
        }
        checkProgressiveLoadingComplete();
        return;
    }

    bool didWork = false;
    // During loading screen, never mark didWork so all priorities run each frame
    // (no scene rendering to compete with).
    const bool canMarkWork = !loadingScreenVisible_;
    auto stepStart = std::chrono::steady_clock::now();

    // Lightweight entity ops (always run under GREEN, no didWork cost):
    if (entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        if (++entityPrepScanCounter_ % 10 == 0) {
            queueEntityPrepRequests();
        }
        if (entityPrepWorker_ && entityPrepWorker_->isIdle()) {
            entityPrepWorker_->dispatchOne();
        }
    }

    // Priority 1: Heavy entity ops (cost didWork budget)
    if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        // Poll completed prep results — distributing a result consumes the budget
        if (entityRenderer_->pollAndDistributePrepResults()) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_poll", 0, stepStart);
        }
    }
    if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        if (entityRenderer_->processOneEntityBuildStep(currentPvsRegion_)) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_step", 0, stepStart);
            // Report entity progress to chat
            size_t totalEntities = 0, builtEntities = 0;
            for (const auto& [id, vis] : entityRenderer_->getEntities()) {
                if (vis.inSceneGraph) {
                    totalEntities++;
                    if (vis.meshBuilt) builtEntities++;
                }
            }
            sendLoadProgress(fmt::format("[Load] Entity {}/{}", builtEntities, totalEntities));
        }
    }

    // Priority 1.5: Rebuild one region whose async fallback textures have arrived
    if (!didWork && constrainedMeshCache_ && !textureRebuildQueue_.empty()) {
        auto entry = textureRebuildQueue_.front();
        textureRebuildQueue_.erase(textureRebuildQueue_.begin());

        // Clean up existing node before rebuild
        auto existingIt = regionMeshNodes_.find(entry.regionIdx);
        if (existingIt != regionMeshNodes_.end() && existingIt->second) {
            deleteMeshHardwareBuffers(existingIt->second);
            if (animatedTextureManager_)
                animatedTextureManager_->removeSceneNode(existingIt->second);
            if (existingIt->second->getParent()) existingIt->second->remove();
            else existingIt->second->drop();
            existingIt->second = nullptr;
        }

        constrainedMeshCache_->markForRebuild(entry.regionIdx);
        if (rebuildRegionMesh(entry.regionIdx)) {
            addRegionToCollision(entry.regionIdx);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1.5: rebuilt region {} with textures (queue remaining: {})",
                     entry.regionIdx, textureRebuildQueue_.size());
            didWork = canMarkWork;
            logAssetBuildTime("tex_rebuild", entry.regionIdx, stepStart);
        } else {
            LOG_WARN(MOD_GRAPHICS, "Progressive P1.5: FAILED to rebuild region {} with textures",
                     entry.regionIdx);
        }
    }

    // Priority 1.6: Rebuild one door whose async textures have arrived
    if (!didWork && !doorTextureRebuildQueue_.empty() && doorManager_) {
        uint8_t doorId = doorTextureRebuildQueue_.front();
        doorTextureRebuildQueue_.erase(doorTextureRebuildQueue_.begin());
        doorManager_->rebuildSingleDoor(doorId);
        didWork = canMarkWork;
        logAssetBuildTime("door_tex_rebuild", doorId, stepStart);
        LOG_INFO(MOD_GRAPHICS, "Progressive P1.6: rebuilt door {} with textures (queue: {})",
                 doorId, doorTextureRebuildQueue_.size());
    }

    // Priority 1.7: Rebuild one PVS object whose async textures have arrived
    if (!didWork && !objectTextureRebuildQueue_.empty()) {
        size_t objIdx = objectTextureRebuildQueue_.front();
        objectTextureRebuildQueue_.erase(objectTextureRebuildQueue_.begin());
        if (objIdx < deferredObjects_.size()) {
            // Find and remove the existing scene node for this deferred object
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
            didWork = canMarkWork;
            logAssetBuildTime("obj_tex_rebuild", objIdx, stepStart);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1.7: rebuilt object {} with textures (queue: {})",
                     objIdx, objectTextureRebuildQueue_.size());
        }
    }

    // Priority 2: Build one PVS neighbor region
    if (!didWork && constrainedMeshCache_) {
        size_t qNeedBuild = 0;
        size_t firstUnloaded = SIZE_MAX;
        for (const auto& entry : meshLoadQueue_) {
            if (!constrainedMeshCache_->isLoaded(entry.regionIdx)) {
                if (getRegionPvsDepth(entry.regionIdx) > static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth)) continue;
                qNeedBuild++;
                if (firstUnloaded == SIZE_MAX) firstUnloaded = entry.regionIdx;
            }
        }
        if (qNeedBuild > 0) {
            LOG_INFO(MOD_GRAPHICS, "Progressive P2: attempting region {} ({} need building in queue of {})",
                     firstUnloaded, qNeedBuild, meshLoadQueue_.size());
        }

        for (const auto& entry : meshLoadQueue_) {
            if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
            // PVS depth gate: skip regions beyond configured depth
            if (getRegionPvsDepth(entry.regionIdx) > static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth)) continue;
            if (rebuildRegionMesh(entry.regionIdx)) {
                addRegionToCollision(entry.regionIdx);
                LOG_INFO(MOD_GRAPHICS, "Progressive P2: SUCCESS region {} + collision (PVS={}, queue={}, "
                          "needBuild={})",
                          entry.regionIdx, currentPvsRegion_, meshLoadQueue_.size(), qNeedBuild - 1);
                didWork = canMarkWork;
                logAssetBuildTime("region", entry.regionIdx, stepStart);
                sendLoadProgress(fmt::format("[Load] Region {} [{}/{}]",
                    entry.regionIdx, meshLoadQueue_.size() - (qNeedBuild - 1), meshLoadQueue_.size()));
            } else {
                LOG_ERROR(MOD_GRAPHICS, "Progressive P2: FAILED region {} — rebuildRegionMesh returned false "
                          "(PVS={}, queue={})",
                          entry.regionIdx, currentPvsRegion_, meshLoadQueue_.size());
            }
            break;  // One attempt max
        }
    }

    // Priority 3: Build one door in current PVS set (nearest first)
    if (!didWork && doorManager_) {
        std::vector<uint8_t> pvsDoors;
        doorManager_->getDoorsInRegions(protectedRegions_, pvsDoors);
        // PVS depth gate: remove doors beyond configured depth
        pvsDoors.erase(std::remove_if(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t doorId) {
            const auto* dv = doorManager_->getDoor(doorId);
            if (!dv) return true;
            return getRegionPvsDepth(dv->bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth);
        }), pvsDoors.end());
        std::sort(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t a, uint8_t b) {
            const auto* da = doorManager_->getDoor(a);
            const auto* db = doorManager_->getDoor(b);
            if (!da || !db) return false;
            float distA = (da->x - playerX_) * (da->x - playerX_) + (da->y - playerY_) * (da->y - playerY_);
            float distB = (db->x - playerX_) * (db->x - playerX_) + (db->y - playerY_) * (db->y - playerY_);
            return distA < distB;
        });
        size_t totalPvsDoors = pvsDoors.size();
        size_t doorIndex = 0;
        for (auto doorId : pvsDoors) {
            if (doorManager_->isDoorMeshBuilt(doorId)) continue;
            // Use rebuildSingleDoor to properly clean up placeholder nodes
            // before building real mesh (direct buildDoorMesh leaks placeholders)
            doorManager_->rebuildSingleDoor(doorId);
            // Track any textures that were pending async upload
            const auto& missingDoorTex = doorManager_->getLastMissingTextures();
            if (!missingDoorTex.empty()) {
                for (const auto& texName : missingDoorTex) {
                    pendingTextureDoors_[texName].insert(doorId);
                }
            }
            addDoorToCollision(doorId);
            didWork = canMarkWork;
            logAssetBuildTime("door", doorId, stepStart);
            const auto* dv = doorManager_->getDoor(doorId);
            sendLoadProgress(fmt::format("[Load] Door {} '{}' [{}/{}]",
                doorId, dv ? dv->modelName : "?", doorIndex + 1, totalPvsDoors));
            break;  // One door max
        }
    }

    // Priority 4: Build one PVS object
    if (!didWork) {
        size_t totalPvsObjects = 0, builtPvsObjects = 0;
        for (const auto& obj : deferredObjects_) {
            if (obj.bspRegion < protectedRegions_.size() && protectedRegions_[obj.bspRegion] &&
                getRegionPvsDepth(obj.bspRegion) <= static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth)) {
                totalPvsObjects++;
                if (obj.meshBuilt) builtPvsObjects++;
            }
        }
        // Collect unbuilt PVS objects with distance, sort nearest-first
        std::vector<std::pair<size_t, float>> objCandidates;
        for (size_t i = 0; i < deferredObjects_.size(); ++i) {
            if (deferredObjects_[i].meshBuilt) continue;
            if (deferredObjects_[i].bspRegion >= protectedRegions_.size() || !protectedRegions_[deferredObjects_[i].bspRegion]) continue;
            // PVS depth gate
            if (getRegionPvsDepth(deferredObjects_[i].bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth)) continue;
            auto center = deferredObjects_[i].worldBounds.getCenter();
            // worldBounds is Irrlicht Y-up: (x, z_eq, y_eq)
            float dx = center.X - playerX_;
            float dy = center.Z - playerY_;  // Irr Z = EQ Y
            objCandidates.push_back({i, dx*dx + dy*dy});
        }
        std::sort(objCandidates.begin(), objCandidates.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        if (!objCandidates.empty()) {
            size_t i = objCandidates[0].first;
            buildDeferredObject(i);
            addObjectToCollision(objectNodes_.size() - 1);
            didWork = canMarkWork;
            logAssetBuildTime("pvs_object", i, stepStart);
            std::string objName = "?";
            if (currentZone_ && deferredObjects_[i].objectIndex < currentZone_->objects.size()) {
                const auto& objInstance = currentZone_->objects[deferredObjects_[i].objectIndex];
                if (objInstance.placeable) objName = objInstance.placeable->getName();
            }
            sendLoadProgress(fmt::format("[Load] Object '{}' [{}/{}]",
                objName, builtPvsObjects + 1, totalPvsObjects));
        }
    }

    // Priority 5: Lazy icon extraction (lowest priority, one icon per GREEN frame)
    // Sort by sheet key before processing so all icons from the same resident sheet
    // are extracted consecutively before triggering a new sheet load.
    if (!didWork && windowManager_ && config_.constrainedConfig.enableItemIcons) {
        windowManager_->getIconLoader().sortPendingBySheet();
        if (windowManager_->processOneLazyIcon()) {
            didWork = canMarkWork;
            logAssetBuildTime("icon", 0, stepStart);
        }
    }

    // Always queue background prep requests (free work on background thread)
    queueEntityPrepRequests();

    // Mesh cache eviction (lightweight, cap per frame)
    if (constrainedMeshCache_ &&
        constrainedMeshCache_->getCurrentUsage() > constrainedMeshCache_->getMemoryLimit()) {
        constexpr int MAX_EVICTIONS_PER_FRAME = 10;
        int evictionsThisFrame = 0;
        auto evicted = constrainedMeshCache_->evictUntilAvailable(0, protectedRegions_);
        for (size_t idx : evicted) {
            if (evictionsThisFrame >= MAX_EVICTIONS_PER_FRAME) break;
            auto it = regionMeshNodes_.find(idx);
            if (it != regionMeshNodes_.end() && it->second) {
                deleteMeshHardwareBuffers(it->second);
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(it->second);
                if (it->second->getParent()) it->second->remove(); else it->second->drop();
                it->second = nullptr;
            }
            // Remove evicted region from texture rebuild tracking
            for (auto& [texName, regions] : pendingTextureRegions_) {
                regions.erase(idx);
            }
            textureRebuildQueue_.erase(
                std::remove_if(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                    [idx](const TextureRebuildEntry& e) { return e.regionIdx == idx; }),
                textureRebuildQueue_.end());
            evictionsThisFrame++;
        }
    }

    if (didWork) {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: 1 step this frame ({:.1f}ms remaining, gov=GREEN)",
            governor_ ? governor_->getRemainingBudgetMs() : 0.0f);
    }

    checkProgressiveLoadingComplete();
}

uint8_t IrrlichtRenderer::getRegionPvsDepth(size_t regionIdx) const {
    if (!regionPvsDepthMap_) return 255;
    auto it = regionPvsDepthMap_->find(regionIdx);
    return (it != regionPvsDepthMap_->end()) ? it->second : 255;
}

void IrrlichtRenderer::rebuildPvsNeighborhood(size_t anchorRegion) {
    pvsNeighborhood_.clear();
    pvsNeighborhoodAnchor_ = anchorRegion;
    if (anchorRegion == SIZE_MAX || regionNeighbors_.empty()) return;

    const int K = config_.constrainedConfig.pvsNeighborhoodHops;
    if (K <= 0) return;  // Disabled: fire onPvsRegionChanged on every crossing

    struct Entry { size_t region; int depth; };
    std::vector<Entry> queue;
    queue.push_back({anchorRegion, 0});
    pvsNeighborhood_.insert(anchorRegion);

    size_t head = 0;
    while (head < queue.size()) {
        auto [fromRegion, depth] = queue[head++];
        if (depth >= K) continue;
        auto it = regionNeighbors_.find(fromRegion);
        if (it == regionNeighbors_.end()) continue;
        for (size_t neighbor : it->second) {
            if (pvsNeighborhood_.insert(neighbor).second) {
                queue.push_back({neighbor, depth + 1});
            }
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "PVS neighborhood rebuilt: anchor={}, {} regions (K={})",
              anchorRegion, pvsNeighborhood_.size(), K);
}

bool IrrlichtRenderer::isInPvsNeighborhood(size_t regionIdx) const {
    return pvsNeighborhoodAnchor_ != SIZE_MAX &&
           pvsNeighborhood_.count(regionIdx) > 0;
}

void IrrlichtRenderer::onPvsRegionChanged() {
    // Re-sort entity prep pending queue with updated PVS depths
    if (entityPrepWorker_) {
        entityPrepWorker_->updateDepths([this](size_t bspRegion) {
            return getRegionPvsDepth(bspRegion);
        });
    }

#ifdef EQT_HAS_GLES2
    // Re-prioritize GPU upload queue
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        gpuUploadThread_->reprioritize([this](const UploadRequest& req) -> uint32_t {
            uint8_t sourceType = static_cast<uint8_t>(req.callbackKey >> 56);
            if (sourceType == 0 || sourceType == 1) {
                // VBO upload: low 48 bits = region index
                size_t regionIdx = req.callbackKey & 0xFFFFFFFFFFFFULL;
                uint8_t depth = getRegionPvsDepth(regionIdx);
                return WorkPriorityKey::make(depth, AssetType::ZoneMesh).value;
            } else if (sourceType == 3) {
                // Constrained cache texture (unified: zone + entity textures)
                return WorkPriorityKey::make(0, AssetType::ZoneTexture).value;
            }
            // Icons and unknown: lowest priority
            return WorkPriorityKey::makeNonSpatial(AssetType::Icon).value;
        });
    }
#endif
    // meshLoadQueue_ and textureRebuildQueue_ are repopulated each frame — no action needed
    // Note: camera BSP collision (setBspPlayerRegion) is updated inline in applySimulationOutput
    // on every region change, not gated by the neighborhood.
}

void IrrlichtRenderer::queueEntityPrepRequests() {
    if (!entityPrepWorker_ || !entityRenderer_ || !entityPrepReady_) return;

    std::vector<uint16_t> unbuilt;
    entityRenderer_->getUnbuiltEntities(unbuilt);
    if (unbuilt.empty()) return;

    const bool pvsValid = (currentPvsRegion_ != SIZE_MAX);

    const auto& entities = entityRenderer_->getEntities();
    for (auto spawnId : unbuilt) {
        if (entityRenderer_->isEntityMeshBuilt(spawnId)) continue;
        auto it = entities.find(spawnId);
        if (it == entities.end()) continue;
        const auto& vis = it->second;
        // Don't prep invisible entities — matches build step visibility filter
        if (!vis.inSceneGraph) continue;
        // Don't re-queue entities whose background prep already completed
        if (vis.entityPrepComplete) continue;

        // When player is outside all BSP regions (SIZE_MAX), only build the player entity
        if (!pvsValid && spawnId != playerSpawnId_) continue;

        // PVS depth gate: queue entities within configured portal hop distance
        uint8_t depth = 255;
        if (spawnId == playerSpawnId_) {
            depth = 0;
        } else {
            depth = getRegionPvsDepth(vis.cachedBspRegion);
        }
        if (depth > static_cast<uint8_t>(config_.constrainedConfig.entityPrepMaxPvsDepth)) continue;

        // Per-entity dedup: each entity gets its own prep job (equipment/variant work differs)
        if (!entityPrepWorker_->isPendingForEntity(spawnId)) {
            EntityPrepWorker::PrepRequest req;
            req.spawnId = spawnId;
            req.raceId = vis.raceId;
            req.gender = vis.gender;
            req.appearance = vis.appearance;
            req.pvsDepth = depth;
            req.bspRegion = vis.cachedBspRegion;
            entityPrepWorker_->requestPrep(req);
        }
    }
}

void IrrlichtRenderer::logAssetBuildTime(const char* type, size_t id,
    std::chrono::steady_clock::time_point start) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() / 1000.0f;
    if (elapsed > 10.0f) {
        LOG_WARN(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms (budget warning)", type, id, elapsed);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms", type, id, elapsed);
    }
}

void IrrlichtRenderer::sendLoadProgress(const std::string& msg) {
    if (windowManager_) {
        auto* chatWindow = windowManager_->getChatWindow();
        if (chatWindow) {
            chatWindow->addSystemMessage(msg);
        }
    }
}

void IrrlichtRenderer::checkProgressiveLoadingComplete() {
    if (!progressiveLoadingActive_) return;

    // Phase pipeline must reach Complete before we can declare progressive loading done.
    // Otherwise zero counts just mean "nothing queued yet" (pre-zone-load).
    if (!sequentialLoadComplete_) return;

    // Count unbuilt assets
    size_t unbuiltEntities = 0;
    size_t unbuiltDoors = 0;
    size_t unbuiltObjects = 0;

    if (entityRenderer_) {
        for (const auto& [id, vis] : entityRenderer_->getEntities()) {
            if (!vis.meshBuilt && vis.inSceneGraph) {
                // Only count entities in the player's PVS region as blocking.
                // Out-of-region entities will be built lazily when the player moves.
                // When player is outside all BSP regions (SIZE_MAX), only the player
                // entity counts as blocking — other entities can't be built anyway.
                if (currentPvsRegion_ == SIZE_MAX && id != playerSpawnId_)
                    continue;
                if (currentPvsRegion_ != SIZE_MAX &&
                    id != playerSpawnId_ && vis.cachedBspRegion != currentPvsRegion_)
                    continue;
                unbuiltEntities++;
            }
        }
    }

    if (doorManager_) {
        std::vector<uint8_t> pvsDoors;
        doorManager_->getDoorsInRegions(protectedRegions_, pvsDoors);
        // PVS depth gate: only count doors within configured depth
        pvsDoors.erase(std::remove_if(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t doorId) {
            const auto* dv = doorManager_->getDoor(doorId);
            return dv && getRegionPvsDepth(dv->bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth);
        }), pvsDoors.end());
        unbuiltDoors = pvsDoors.size();
    }

    for (const auto& obj : deferredObjects_) {
        if (!obj.meshBuilt && obj.bspRegion < protectedRegions_.size() && protectedRegions_[obj.bspRegion] &&
            getRegionPvsDepth(obj.bspRegion) <= static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth))
            unbuiltObjects++;
    }

    // Periodic status log for progressive loading (every 5 seconds)
    static int progCheckLog = 0;
    if (++progCheckLog % 150 == 1) {
        size_t unbuiltRegions = 0;
        if (constrainedMeshCache_) {
            for (const auto& entry : meshLoadQueue_) {
                if (!constrainedMeshCache_->isLoaded(entry.regionIdx) &&
                    getRegionPvsDepth(entry.regionIdx) <= static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth))
                    unbuiltRegions++;
            }
        }
        bool pendingTextures = constrainedTextureCache_ && constrainedTextureCache_->hasPendingWork();
        size_t pendingVBOs = 0;
#ifdef EQT_HAS_GLES2
        pendingVBOs = pendingVBOUploads_.size();
#endif
        LOG_INFO(MOD_GRAPHICS, "Progressive status: entities={} doors={} objects={} "
                 "queuedRegions={} pendingVBOs={} pendingTextures={} texRebuild={} objRebuild={} "
                 "| governor={} | progressive={}",
                 unbuiltEntities, unbuiltDoors, unbuiltObjects, unbuiltRegions,
                 pendingVBOs, pendingTextures,
                 textureRebuildQueue_.size(), objectTextureRebuildQueue_.size(),
                 governor_ ? governor_->getStateName() : "N/A",
                 progressiveLoadingActive_);
    }

    if (unbuiltEntities == 0 && unbuiltDoors == 0 && unbuiltObjects == 0) {
        // Also wait for async GPU work (texture decodes/uploads, VBOs, rebuild queues)
        // before declaring progressive loading complete. Without this, the loading
        // screen hides while textures are still in flight → garbled first frame.
        bool gpuWorkPending = false;
#ifdef EQT_HAS_GLES2
        if (!pendingVBOUploads_.empty())
            gpuWorkPending = true;
#endif
        if (constrainedTextureCache_ && constrainedTextureCache_->hasPendingWork())
            gpuWorkPending = true;
        if (!objectTextureRebuildQueue_.empty() || !textureRebuildQueue_.empty())
            gpuWorkPending = true;
        if (gpuWorkPending) return;

        progressiveLoadingActive_ = false;

        // EntityPrepWorker stays alive for the entire session — entities move
        // in and out of the scene graph during gameplay and need lazy prep.

        // Release object texture pixel data — all deferred objects and doors are built.
        // Safe even with constrainedMeshCache_ active (only zone textures needed for region rebuilds).
        if (currentZone_ && !currentZone_->objectTextures.empty()) {
            size_t freed = 0;
            for (auto& [name, texInfo] : currentZone_->objectTextures) {
                if (!texInfo) continue;
                freed += texInfo->data.capacity();
                texInfo->data.clear();
                texInfo->data.shrink_to_fit();
                for (auto& frame : texInfo->frames) {
                    freed += frame.data.capacity();
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
            if (freed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f}KB object texture pixel data (progressive complete)",
                         freed / 1024.0f);
            }
        }

        // Stop background icon sheet worker — progressive loading complete
        if (windowManager_) {
            windowManager_->getIconLoader().stopWorker();
        }

        // Remove zone placeholder mesh — real geometry is fully loaded
        destroyZonePlaceholder();

        // Build portal system now that all geometry is loaded. Portal occlusion
        // is an optimization (~472ms build cost) — deferred from loadZone to avoid
        // poisoning the governor during progressive loading.
        if (portalBuildPending_ && zoneBspTree_ && !regionBoundingBoxes_.empty()) {
            // Compute zone bounds from region bounding boxes (EQ coords)
            float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
            float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;
            for (const auto& [idx, bbox] : regionBoundingBoxes_) {
                zbMinX = std::min(zbMinX, bbox.MinEdge.X); zbMinY = std::min(zbMinY, bbox.MinEdge.Y); zbMinZ = std::min(zbMinZ, bbox.MinEdge.Z);
                zbMaxX = std::max(zbMaxX, bbox.MaxEdge.X); zbMaxY = std::max(zbMaxY, bbox.MaxEdge.Y); zbMaxZ = std::max(zbMaxZ, bbox.MaxEdge.Z);
            }
            portalSystem_ = std::make_unique<PortalSystem>();
            portalSystem_->buildFromBsp(*zoneBspTree_, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
            portalOcclusionEligible_ = portalSystem_->hasPortals() &&
                                       (portalSystem_->getData().portals.size() > 10);
            if (portalOcclusionEligible_) {
                LOG_INFO(MOD_GRAPHICS, "Portal occlusion eligible: {} portals (post-load build)",
                         portalSystem_->getData().portals.size());
                // Auto-enable portal occlusion (post-load path)
                if (config_.constrainedConfig.portalOcclusion &&
                    config_.constrainedConfig.enableStencilBuffer) {
                    if (!indoorRegions_.empty()) {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode (post-load, {} indoor regions)",
                                 indoorRegions_.size());
                    } else {
                        size_t portalCount = portalSystem_->getData().portals.size();
                        size_t regionCount = regionBoundingBoxes_.size();
                        float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                        if (portalRatio <= 3.0f) {
                            portalOcclusionEnabled_ = true;
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled (post-load): {:.1f} ratio (indoor)", portalRatio);
                        } else {
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled (post-load): {:.1f} ratio (outdoor)", portalRatio);
                        }
                    }
                }
            }
            portalBuildPending_ = false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - progressiveLoadStartTime_).count();
        LOG_INFO(MOD_GRAPHICS, "Progressive loading complete: {}ms total streaming time", elapsed);
        sendLoadProgress(fmt::format("[Load] Complete ({:.1f}s)", elapsed / 1000.0f));

        // Regenerate detail chunks now that all terrain is in the selector
        // (chunks generated during progressive loading had incomplete terrain raycasts)
        if (detailManager_ && detailManager_->isEnabled()) {
            detailManager_->setEnabled(false);
            detailManager_->setEnabled(true);
            LOG_INFO(MOD_GRAPHICS, "Regenerating detail chunks with complete terrain");
        }

        // Release raw texture data now that all models are built
        if (config_.constrainedConfig.releaseTextureDataAfterUpload && entityRenderer_) {
            size_t totalFreed = 0;
            if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                totalFreed += eml->releaseRawTextureData();
            }
            if (auto* rml = entityRenderer_->getRaceModelLoader()) {
                totalFreed += rml->releaseRawTextureData();
            }
            if (totalFreed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f} KB of raw texture data after progressive load",
                    totalFreed / 1024.0f);
            }
        }
    }
}

bool IrrlichtRenderer::checkCollisionIrrlicht(const irr::core::vector3df& start,
                                               const irr::core::vector3df& end,
                                               irr::core::vector3df& hitPoint,
                                               irr::core::triangle3df& hitTriangle) {
    // DIAGNOSTIC: This should NOT be called on the hot path anymore.
    // BSP-filtered collision should be used instead for movement.
    LOG_WARN(MOD_GRAPHICS, "checkCollisionIrrlicht CALLED — old meta selector path! "
             "ray ({},{},{}) -> ({},{},{})",
             start.X, start.Y, start.Z, end.X, end.Y, end.Z);
    return false;
}

float IrrlichtRenderer::findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset) {
    // DIAGNOSTIC: This should NOT be called on the hot path anymore.
    // BSP-filtered collision should be used instead for movement.
    LOG_WARN(MOD_GRAPHICS, "findGroundZIrrlicht CALLED — old meta selector path! "
             "pos ({},{},{})", x, y, currentZ);
    return currentZ - modelYOffset;
}

// updateNameTagsWithLOS removed — name tag visibility now computed by SimulationWorker

// --- BSP-Filtered Collision (hot path: movement) ---
// Uses pre-transformed world-space triangles per region — no matrix math.
// Door/object selector still uses Irrlicht (few triangles, dynamic transforms).

std::vector<irr::core::triangle3df> IrrlichtRenderer::extractWorldTriangles(
    irr::scene::ISceneManager* smgr, irr::scene::IMeshSceneNode* node) {
    std::vector<irr::core::triangle3df> result;
    if (!smgr || !node || !node->getMesh()) return result;

    // Create a temporary selector to extract world-space triangles
    irr::scene::ITriangleSelector* sel = smgr->createTriangleSelector(node->getMesh(), node);
    if (!sel) return result;

    int count = sel->getTriangleCount();
    if (count > 0) {
        result.resize(count);
        irr::s32 outCount = 0;
        sel->getTriangles(result.data(), count, outCount);
        result.resize(outCount);
    }
    sel->drop();
    return result;
}

bool IrrlichtRenderer::rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                                              const irr::core::vector3df& rayDir, float rayLen,
                                              const std::vector<irr::core::triangle3df>& triangles,
                                              irr::core::vector3df& hitPoint,
                                              irr::core::triangle3df& hitTriangle,
                                              float& hitDistSq) {
    // Möller–Trumbore ray-triangle intersection — no matrix math
    constexpr float EPSILON = 0.000001f;
    bool anyHit = false;
    float closestT = rayLen + 1.0f;

    for (const auto& tri : triangles) {
        const irr::core::vector3df& v0 = tri.pointA;
        const irr::core::vector3df edge1 = tri.pointB - v0;
        const irr::core::vector3df edge2 = tri.pointC - v0;

        const irr::core::vector3df h = rayDir.crossProduct(edge2);
        float a = edge1.dotProduct(h);
        if (a > -EPSILON && a < EPSILON) continue;  // Parallel

        float f = 1.0f / a;
        const irr::core::vector3df s = rayOrigin - v0;
        float u = f * s.dotProduct(h);
        if (u < 0.0f || u > 1.0f) continue;

        const irr::core::vector3df q = s.crossProduct(edge1);
        float v = f * rayDir.dotProduct(q);
        if (v < 0.0f || u + v > 1.0f) continue;

        float t = f * edge2.dotProduct(q);
        if (t > EPSILON && t < closestT) {
            closestT = t;
            hitPoint = rayOrigin + rayDir * t;
            hitTriangle = tri;
            anyHit = true;
        }
    }

    if (anyHit) {
        hitDistSq = hitPoint.getDistanceFromSQ(rayOrigin);
    }
    return anyHit;
}

bool IrrlichtRenderer::rayTestDoorsAndObjects(const irr::core::vector3df& rayOrigin,
                                                const irr::core::vector3df& rayDir, float rayLen,
                                                size_t bspRegion,
                                                irr::core::vector3df& hitPoint,
                                                irr::core::triangle3df& hitTriangle,
                                                float& hitDistSq) {
    bool anyHit = false;

    // Test objects in this BSP region
    auto objIt = objectWorldTriangles_.find(bspRegion);
    if (objIt != objectWorldTriangles_.end()) {
        if (rayIntersectTriangles(rayOrigin, rayDir, rayLen, objIt->second, hitPoint, hitTriangle, hitDistSq)) {
            anyHit = true;
        }
    }

    // Test doors in this BSP region (skip animating doors — walk-through during animation)
    for (const auto& [doorId, doorData] : doorCollisionData_) {
        if (!doorData.hasCollision) continue;
        if (doorData.bspRegion != bspRegion) continue;

        // Get current door state from door manager
        const auto* door = doorManager_ ? doorManager_->getDoor(doorId) : nullptr;
        if (!door) continue;
        if (door->isAnimating) continue;  // Walk-through during animation (original EQ behavior)

        const auto& tris = door->isOpen ? doorData.openTriangles : doorData.closedTriangles;
        if (tris.empty()) continue;

        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayIntersectTriangles(rayOrigin, rayDir, rayLen, tris, hp, ht, distSq)) {
            if (!anyHit || distSq < hitDistSq) {
                hitPoint = hp;
                hitTriangle = ht;
                hitDistSq = distSq;
                anyHit = true;
            }
        }
    }

    return anyHit;
}

bool IrrlichtRenderer::checkCollisionBspFiltered(const irr::core::vector3df& start,
                                                  const irr::core::vector3df& end,
                                                  irr::core::vector3df& hitPoint,
                                                  irr::core::triangle3df& hitTriangle) {
    if (!zoneBspTree_) return false;

    irr::core::vector3df rayDir = end - start;
    float rayLen = rayDir.getLength();
    if (rayLen < 0.0001f) return false;
    rayDir /= rayLen;  // Normalize

    bool anyHit = false;
    float closestDistSq = std::numeric_limits<float>::max();
    irr::core::vector3df bestHit;
    irr::core::triangle3df bestTri;

    // Find source and destination BSP regions
    // Irrlicht coords (x, z, y) -> EQ coords (x, y, z): eqX=irrX, eqY=irrZ, eqZ=irrY
    size_t srcRegion = zoneBspTree_->findRegionIndexForPoint(start.X, start.Z, start.Y);
    size_t dstRegion = zoneBspTree_->findRegionIndexForPoint(end.X, end.Z, end.Y);

    // Test source region triangles (direct, no matrix math)
    if (srcRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(srcRegion);
        if (it != regionWorldTriangles_.end()) {
            irr::core::vector3df hp;
            irr::core::triangle3df ht;
            float distSq;
            if (rayIntersectTriangles(start, rayDir, rayLen, it->second, hp, ht, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hp;
                    bestTri = ht;
                    anyHit = true;
                }
            }
        }
    }

    // Test destination region triangles (if different from source)
    if (dstRegion != SIZE_MAX && dstRegion != srcRegion) {
        auto it = regionWorldTriangles_.find(dstRegion);
        if (it != regionWorldTriangles_.end()) {
            irr::core::vector3df hp;
            irr::core::triangle3df ht;
            float distSq;
            if (rayIntersectTriangles(start, rayDir, rayLen, it->second, hp, ht, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hp;
                    bestTri = ht;
                    anyHit = true;
                }
            }
        }
    }

    // Test doors and objects in source region (direct Möller–Trumbore, no matrix math)
    if (srcRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(start, rayDir, rayLen, srcRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                bestTri = ht;
                anyHit = true;
            }
        }
    }

    // Test doors and objects in destination region (if different)
    if (dstRegion != SIZE_MAX && dstRegion != srcRegion) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(start, rayDir, rayLen, dstRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                bestTri = ht;
                anyHit = true;
            }
        }
    }

    if (anyHit) {
        hitPoint = bestHit;
        hitTriangle = bestTri;
    }
    return anyHit;
}

float IrrlichtRenderer::findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset) {
    if (!zoneBspTree_) {
        return currentZ - modelYOffset;  // Return current feet position
    }

    float feetZ = currentZ - modelYOffset;
    float headZ = currentZ + modelYOffset;
    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // Find BSP region at query point
    size_t queryRegion = zoneBspTree_->findRegionIndexForPoint(x, y, currentZ);

    // PHASE 1: Short raycast near current level
    irr::core::vector3df nearStart(x, feetZ + maxStepUp, y);
    irr::core::vector3df rayDir(0.0f, -1.0f, 0.0f);  // Cast down
    float nearRayLen = maxStepUp + maxStepDown;

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    bool nearHit = false;
    float closestDistSq = std::numeric_limits<float>::max();
    irr::core::vector3df bestHit;

    // Test query region world triangles (direct, no matrix math)
    if (queryRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(queryRegion);
        if (it != regionWorldTriangles_.end()) {
            float distSq;
            if (rayIntersectTriangles(nearStart, rayDir, nearRayLen, it->second, hitPoint, hitTriangle, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hitPoint;
                    nearHit = true;
                }
            }
        }
    }

    // Test doors and objects in query region (direct Möller–Trumbore)
    if (queryRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(nearStart, rayDir, nearRayLen, queryRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                nearHit = true;
            }
        }
    }

    if (nearHit) {
        float floorZ = bestHit.Y;
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            if (playerConfig_.collisionDebug) {
                addCollisionDebugLine(nearStart, bestHit, irr::video::SColor(255, 0, 255, 128), 0.2f);
            }
            return floorZ;
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    irr::core::vector3df rayStart(x, headZ + 2.0f, y);
    irr::core::vector3df rayEnd(x, feetZ - 500.0f, y);
    float fullRayLen = (headZ + 2.0f) - (feetZ - 500.0f);

    bool hit = false;
    closestDistSq = std::numeric_limits<float>::max();
    bestHit = irr::core::vector3df();

    if (queryRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(queryRegion);
        if (it != regionWorldTriangles_.end()) {
            float distSq;
            if (rayIntersectTriangles(rayStart, rayDir, fullRayLen, it->second, hitPoint, hitTriangle, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hitPoint;
                    hit = true;
                }
            }
        }
    }

    if (queryRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(rayStart, rayDir, fullRayLen, queryRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                hit = true;
            }
        }
    }

    // Debug visualization
    if (playerConfig_.collisionDebug) {
        if (hit) {
            float floorZ = bestHit.Y;
            bool validFloor = (floorZ <= feetZ + maxStepUp + 0.1f);
            if (validFloor) {
                addCollisionDebugLine(rayStart, bestHit, irr::video::SColor(255, 0, 255, 255), 0.2f);
            } else {
                addCollisionDebugLine(rayStart, bestHit, irr::video::SColor(255, 255, 165, 0), 0.2f);
                LOG_TRACE(MOD_MOVEMENT, "BSP ray hit obstruction at {} (head at {}, feet at {})", bestHit.Y, headZ, feetZ);
            }
        } else {
            addCollisionDebugLine(rayStart, rayEnd, irr::video::SColor(255, 255, 0, 255), 0.2f);
        }
    }

    float groundZ = feetZ;  // Default to current feet position
    if (hit) {
        float floorZ = bestHit.Y;
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            groundZ = floorZ;
        } else {
            return feetZ + 1000.0f;  // Ceiling sentinel
        }
    }

    // Check for boat collision — only when boats exist in zone
    if (entityRenderer_ && entityRenderer_->hasBoatsInZone()) {
        float boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, feetZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    irr::core::vector3df irrFrom(x, feetZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return boatDeckZ;
            }
        }
    }

    return groundZ;
}

// --- Collision Debug Visualization ---

void IrrlichtRenderer::addCollisionDebugLine(const irr::core::vector3df& start,
                                              const irr::core::vector3df& end,
                                              const irr::video::SColor& color,
                                              float duration) {
    CollisionDebugLine line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.timeRemaining = duration;
    collisionDebugLines_.push_back(line);
}

void IrrlichtRenderer::drawCollisionDebugLines(float deltaTime) {
    if (!driver_ || collisionDebugLines_.empty()) {
        return;
    }

    // Set up material for line drawing (no lighting, no textures)
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 3.0f;  // Thicker lines for visibility
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_ALWAYS;  // Always draw on top
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Draw all lines and update their timers
    auto it = collisionDebugLines_.begin();
    while (it != collisionDebugLines_.end()) {
        driver_->draw3DLine(it->start, it->end, it->color);

        // Update timer
        it->timeRemaining -= deltaTime;
        if (it->timeRemaining <= 0) {
            it = collisionDebugLines_.erase(it);
        } else {
            ++it;
        }
    }
}

void IrrlichtRenderer::clearCollisionDebugLines() {
    collisionDebugLines_.clear();
}

void IrrlichtRenderer::toggleMapOverlay() {
    showMapOverlay_ = !showMapOverlay_;
    LOG_INFO(MOD_GRAPHICS, "Map overlay: {}", showMapOverlay_ ? "ON" : "OFF");

    if (!showMapOverlay_) {
        mapOverlayTriangles_.clear();
    } else {
        // Force update on next frame
        lastMapOverlayUpdatePos_ = glm::vec3(std::numeric_limits<float>::max());
    }
}

void IrrlichtRenderer::updateMapOverlay(const glm::vec3& playerPos) {
    if (!showMapOverlay_ || !collisionMap_) {
        return;
    }

    // Only update if player moved more than 10 units horizontally (X and Z in Y-up coords)
    float dx = playerPos.x - lastMapOverlayUpdatePos_.x;
    float dz = playerPos.z - lastMapOverlayUpdatePos_.z;
    float distSq = dx * dx + dz * dz;
    if (distSq < 100.0f) {  // 10^2
        return;
    }

    lastMapOverlayUpdatePos_ = playerPos;
    mapOverlayTriangles_.clear();

    // Get triangles within radius from HCMap
    auto triangles = collisionMap_->GetTrianglesInRadius(playerPos, mapOverlayRadius_);

    if (triangles.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Map overlay: No triangles found within radius {} at ({}, {}, {})",
                  mapOverlayRadius_, playerPos.x, playerPos.y, playerPos.z);
        return;
    }

    // Get Z range for color gradient
    float minZ, maxZ;
    collisionMap_->GetZRange(minZ, maxZ);

    // Triangles are already in Irrlicht Y-up format from HCMap
    for (const auto& tri : triangles) {
        MapOverlayTriangle overlayTri;

        // Vertices already in Irrlicht format (Y-up), use directly
        overlayTri.v1 = irr::core::vector3df(tri.v1.x, tri.v1.y, tri.v1.z);
        overlayTri.v2 = irr::core::vector3df(tri.v2.x, tri.v2.y, tri.v2.z);
        overlayTri.v3 = irr::core::vector3df(tri.v3.x, tri.v3.y, tri.v3.z);

        // Calculate average Y for color (Y is vertical in Irrlicht coords)
        float avgY = (tri.v1.y + tri.v2.y + tri.v3.y) / 3.0f;
        overlayTri.color = getMapOverlayColor(avgY, minZ, maxZ, tri.normal);

        // Copy placeable flag for rotation
        overlayTri.isPlaceable = tri.isPlaceable;

        mapOverlayTriangles_.push_back(overlayTri);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Map overlay: Updated with {} triangles", mapOverlayTriangles_.size());
}

irr::video::SColor IrrlichtRenderer::getMapOverlayColor(float y, float minY, float maxY, const glm::vec3& normal) const {
    // Normalize Y (height) to 0-1 range
    float range = maxY - minY;
    float t = (range > 0.001f) ? (y - minY) / range : 0.5f;
    t = std::max(0.0f, std::min(1.0f, t));  // Clamp to [0,1]

    // Determine if it's a floor (normal pointing up in Y-up coords, Y > 0.7)
    // or a wall (normal mostly horizontal)
    bool isFloor = std::abs(normal.y) > 0.7f;

    irr::u8 r, g, b;

    if (isFloor) {
        // Floor: Blue (low) -> Green (mid) -> Red (high)
        if (t < 0.5f) {
            // Blue to Green
            float localT = t * 2.0f;
            r = 0;
            g = static_cast<irr::u8>(localT * 255);
            b = static_cast<irr::u8>((1.0f - localT) * 255);
        } else {
            // Green to Red
            float localT = (t - 0.5f) * 2.0f;
            r = static_cast<irr::u8>(localT * 255);
            g = static_cast<irr::u8>((1.0f - localT) * 255);
            b = 0;
        }
    } else {
        // Wall: Use purple/magenta tones to distinguish from floors
        // Darker purple (low) to brighter magenta (high)
        r = static_cast<irr::u8>(128 + t * 127);  // 128-255
        g = static_cast<irr::u8>(t * 100);        // 0-100
        b = static_cast<irr::u8>(180 + t * 75);   // 180-255
    }

    return irr::video::SColor(200, r, g, b);  // Slightly transparent
}

void IrrlichtRenderer::drawMapOverlay() {
    if (!showMapOverlay_ || !driver_ || mapOverlayTriangles_.empty()) {
        return;
    }

    // Set up material for wireframe line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;  // Respect depth
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);

    // Build transform for placeables (rotation and/or mirroring)
    // These transforms are around the world origin to test coordinate alignment
    irr::core::matrix4 placeableTransform;
    placeableTransform.makeIdentity();

    // Apply X-axis mirror if enabled (scale X by -1)
    if (mapOverlayMirrorX_) {
        irr::core::matrix4 mirrorMatrix;
        mirrorMatrix.makeIdentity();
        mirrorMatrix.setScale(irr::core::vector3df(-1.0f, 1.0f, 1.0f));
        placeableTransform *= mirrorMatrix;
    }

    // Apply rotation if set
    if (mapOverlayRotation_ != 0) {
        irr::core::matrix4 rotMatrix;
        rotMatrix.setRotationDegrees(irr::core::vector3df(0, mapOverlayRotation_ * 90.0f, 0));
        placeableTransform *= rotMatrix;
    }

    // Draw terrain triangles first (no transform)
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    for (const auto& tri : mapOverlayTriangles_) {
        if (!tri.isPlaceable) {
            driver_->draw3DLine(tri.v1, tri.v2, tri.color);
            driver_->draw3DLine(tri.v2, tri.v3, tri.color);
            driver_->draw3DLine(tri.v3, tri.v1, tri.color);
        }
    }

    // Draw placeable triangles (with transform if set)
    driver_->setTransform(irr::video::ETS_WORLD, placeableTransform);
    for (const auto& tri : mapOverlayTriangles_) {
        if (tri.isPlaceable) {
            driver_->draw3DLine(tri.v1, tri.v2, tri.color);
            driver_->draw3DLine(tri.v2, tri.v3, tri.color);
            driver_->draw3DLine(tri.v3, tri.v1, tri.color);
        }
    }
}

void IrrlichtRenderer::toggleNavmeshOverlay() {
    showNavmeshOverlay_ = !showNavmeshOverlay_;
    LOG_INFO(MOD_GRAPHICS, "Navmesh overlay: {}", showNavmeshOverlay_ ? "ON" : "OFF");

    if (!showNavmeshOverlay_) {
        navmeshOverlayTriangles_.clear();
    } else {
        // Force update on next frame
        lastNavmeshOverlayUpdatePos_ = glm::vec3(std::numeric_limits<float>::max());
    }
}

void IrrlichtRenderer::updateNavmeshOverlay(const glm::vec3& playerPos) {
    if (!showNavmeshOverlay_ || !navmesh_) {
        return;
    }

    // Only update if player moved more than 10 units horizontally (X and Z in Y-up coords)
    float dx = playerPos.x - lastNavmeshOverlayUpdatePos_.x;
    float dz = playerPos.z - lastNavmeshOverlayUpdatePos_.z;
    float distSq = dx * dx + dz * dz;
    if (distSq < 100.0f) {  // 10^2
        return;
    }

    lastNavmeshOverlayUpdatePos_ = playerPos;
    navmeshOverlayTriangles_.clear();

#ifdef EQT_HAS_NAVMESH
    // Get triangles within radius from navmesh (already in Irrlicht Y-up coords)
    auto triangles = navmesh_->GetTrianglesInRadius(playerPos, navmeshOverlayRadius_);

    if (triangles.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Navmesh overlay: No triangles found within radius {} at ({}, {}, {})",
                  navmeshOverlayRadius_, playerPos.x, playerPos.y, playerPos.z);
        return;
    }

    // Triangles are already in Irrlicht Y-up format, use directly
    for (const auto& tri : triangles) {
        NavmeshOverlayTriangle overlayTri;

        // Vertices already in Irrlicht format (Y-up), use directly
        overlayTri.v1 = irr::core::vector3df(tri.v1.x, tri.v1.y, tri.v1.z);
        overlayTri.v2 = irr::core::vector3df(tri.v2.x, tri.v2.y, tri.v2.z);
        overlayTri.v3 = irr::core::vector3df(tri.v3.x, tri.v3.y, tri.v3.z);

        // Color based on area type
        overlayTri.color = getNavmeshAreaColor(tri.areaType);

        navmeshOverlayTriangles_.push_back(overlayTri);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Navmesh overlay: Updated with {} triangles", navmeshOverlayTriangles_.size());
#endif
}

irr::video::SColor IrrlichtRenderer::getNavmeshAreaColor(uint8_t areaType) const {
    // Area types from PathfinderNavmesh (see pathfinder_interface.h):
    // 0 = Normal (walkable)
    // 1 = Water
    // 2 = Lava
    // 4 = PvP
    // 5 = Slime
    // 6 = Ice
    // 7 = V Water (Frigid Water)
    // 8 = General Area
    // 9 = Portal
    // 10 = Prefer

    switch (areaType) {
        case 0:  // Normal - Green
            return irr::video::SColor(180, 0, 200, 0);
        case 1:  // Water - Blue
            return irr::video::SColor(180, 0, 100, 255);
        case 2:  // Lava - Red/Orange
            return irr::video::SColor(180, 255, 80, 0);
        case 4:  // PvP - Purple
            return irr::video::SColor(180, 180, 0, 200);
        case 5:  // Slime - Yellow-Green
            return irr::video::SColor(180, 180, 200, 0);
        case 6:  // Ice - Cyan
            return irr::video::SColor(180, 0, 220, 220);
        case 7:  // V Water (Frigid) - Dark Blue
            return irr::video::SColor(180, 0, 50, 180);
        case 8:  // General Area - Gray
            return irr::video::SColor(180, 150, 150, 150);
        case 9:  // Portal - Magenta
            return irr::video::SColor(180, 255, 0, 255);
        case 10: // Prefer - Yellow
            return irr::video::SColor(180, 255, 255, 0);
        default: // Unknown - White
            return irr::video::SColor(180, 255, 255, 255);
    }
}

void IrrlichtRenderer::drawNavmeshOverlay() {
    if (!showNavmeshOverlay_ || !driver_ || navmeshOverlayTriangles_.empty()) {
        return;
    }

    // Set up material for wireframe line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;  // Respect depth
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);

    // Build transform matrix for rotation and/or mirroring
    // These transforms are around the world origin to test coordinate alignment
    irr::core::matrix4 navmeshTransform;
    navmeshTransform.makeIdentity();

    // Apply X-axis mirror if enabled (scale X by -1)
    if (navmeshOverlayMirrorX_) {
        irr::core::matrix4 mirrorMatrix;
        mirrorMatrix.makeIdentity();
        mirrorMatrix.setScale(irr::core::vector3df(-1.0f, 1.0f, 1.0f));
        navmeshTransform *= mirrorMatrix;
    }

    // Apply rotation if set
    if (navmeshOverlayRotation_ != 0) {
        irr::core::matrix4 rotMatrix;
        rotMatrix.setRotationDegrees(irr::core::vector3df(0, navmeshOverlayRotation_ * 90.0f, 0));
        navmeshTransform *= rotMatrix;
    }

    // Set transform and draw all triangles as wireframe
    driver_->setTransform(irr::video::ETS_WORLD, navmeshTransform);

    for (const auto& tri : navmeshOverlayTriangles_) {
        driver_->draw3DLine(tri.v1, tri.v2, tri.color);
        driver_->draw3DLine(tri.v2, tri.v3, tri.color);
        driver_->draw3DLine(tri.v3, tri.v1, tri.color);
    }
}

#ifdef EQT_HAS_GLES2
void IrrlichtRenderer::drawTargetOutline() {
    if (currentTargetId_ == 0 || !entityRenderer_ || !driver_) return;
    if (!have3DTransforms_) return;  // Need captured 3D matrices

    const auto& entities = entityRenderer_->getEntities();
    auto it = entities.find(currentTargetId_);
    if (it == entities.end()) return;

    const EntityVisual& visual = it->second;
    if (!visual.animatedNode || !visual.meshBuilt) return;

    irr::scene::ISceneNode* node = visual.sceneNode;
    if (!node || !node->isVisible()) return;

    irr::scene::SMesh* mesh = visual.animatedNode->getInstanceMesh();
    if (!mesh || mesh->getMeshBufferCount() == 0) return;

    // Restore 3D camera matrices — after drawAll(), ETS_VIEW and ETS_PROJECTION
    // may have been overwritten by 2D rendering (billboards, GUI nodes, overlays).
    // The correct 3D matrices were captured during the ESNRP_SOLID render pass.
    driver_->setTransform(irr::video::ETS_VIEW, captured3DView_);
    driver_->setTransform(irr::video::ETS_PROJECTION, captured3DProj_);

    // Stencil reference value above portal range (portals use 1-4)
    const GLint STENCIL_REF = 128;

    // Compute normal transform (mirrors EQAnimatedMeshSceneNode::render() logic)
    irr::core::matrix4 normalTransform;
    if (visual.animatedNode->isPlayerNode()) {
        normalTransform.setTranslation(node->getAbsoluteTransformation().getTranslation());
    } else {
        normalTransform = node->getAbsoluteTransformation();
    }

    // Scaled transform for outline pass (scale from entity center)
    irr::core::vector3df pos = normalTransform.getTranslation();
    const float scaleFactor = 1.10f;
    irr::core::matrix4 toOrigin, fromOrigin, scaleMat;
    toOrigin.setTranslation(-pos);
    fromOrigin.setTranslation(pos);
    scaleMat.setScale(irr::core::vector3df(scaleFactor, scaleFactor, scaleFactor));
    irr::core::matrix4 outlineTransform = fromOrigin * scaleMat * toOrigin * normalTransform;

    // Material: no texture → driver selects Color2D shader (renders vertex colors).
    // EMT_SOLID forces depth write ON in the driver, so we override with raw GL
    // after setMaterial() calls.
    irr::video::SMaterial outlineMat;
    outlineMat.Lighting = false;
    outlineMat.ZBuffer = irr::video::ECFN_LESSEQUAL;
    outlineMat.BackfaceCulling = false;
    outlineMat.MaterialType = irr::video::EMT_SOLID;

    // --- Pass 1: Write stencil at normal scale (no color output) ---
    // Depth test ON (only mark visible pixels), depth write OFF, color OFF.
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, STENCIL_REF, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    driver_->setTransform(irr::video::ETS_WORLD, normalTransform);
    driver_->setMaterial(outlineMat);
    // Override depth write after setMaterial (EMT_SOLID forces it ON)
    glDepthMask(GL_FALSE);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (mb) driver_->drawMeshBuffer(mb);
    }

    // --- Pass 2: Draw outline at scaled-up size (only where stencil != ref) ---
    // Depth test ON with LESSEQUAL so outline is occluded by closer geometry (pet, etc.)
    // but draws over geometry behind/at the target.
    glStencilFunc(GL_NOTEQUAL, STENCIL_REF, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Rotating outline color: cycle hue over time (full rotation every 3 seconds)
    static float outlineHue = 0.0f;
    outlineHue += 360.0f / 180.0f;  // ~2 deg/frame at 60fps → full cycle in 3s
    if (outlineHue >= 360.0f) outlineHue -= 360.0f;
    // HSV to RGB (S=0.8, V=1.0 for vivid bright colors)
    float h = outlineHue / 60.0f;
    int hi = static_cast<int>(h) % 6;
    float f = h - static_cast<int>(h);
    float v = 1.0f, s = 0.8f;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float r, g, b;
    switch (hi) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    const irr::video::SColor outlineColor(255,
        static_cast<irr::u32>(r * 255.0f),
        static_cast<irr::u32>(g * 255.0f),
        static_cast<irr::u32>(b * 255.0f));
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexType() != irr::video::EVT_STANDARD) continue;
        auto* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
        for (irr::u32 v = 0; v < mb->getVertexCount(); ++v)
            verts[v].Color = outlineColor;
    }

    driver_->setTransform(irr::video::ETS_WORLD, outlineTransform);
    driver_->setMaterial(outlineMat);
    // Override depth write after setMaterial (don't contaminate depth buffer)
    glDepthMask(GL_FALSE);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (mb) driver_->drawMeshBuffer(mb);
    }

    // Restore white vertex colors (entity's normal rendering expects white)
    const irr::video::SColor white(255, 255, 255, 255);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexType() != irr::video::EVT_STANDARD) continue;
        auto* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
        for (irr::u32 v = 0; v < mb->getVertexCount(); ++v)
            verts[v].Color = white;
    }

    // --- Cleanup: restore to defaults matching SOGLES2State::reset() ---
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
#else
void IrrlichtRenderer::drawTargetSelectionBox() {
    // Check if we have a target
    if (currentTargetId_ == 0 || !entityRenderer_ || !driver_) {
        return;
    }

    // Find the targeted entity
    const auto& entities = entityRenderer_->getEntities();
    auto it = entities.find(currentTargetId_);
    if (it == entities.end()) {
        return;
    }

    const EntityVisual& visual = it->second;
    irr::scene::ISceneNode* node = visual.sceneNode;
    if (!node || !node->isVisible()) {
        return;
    }

    // Get bounding box in world space
    irr::core::aabbox3df bbox = node->getTransformedBoundingBox();

    // Set up material for line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // White color for selection box
    irr::video::SColor white(255, 255, 255, 255);

    // Get the 8 corners of the bounding box
    irr::core::vector3df corners[8];
    corners[0] = irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z);
    corners[1] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z);
    corners[2] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MinEdge.Z);
    corners[3] = irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y, bbox.MinEdge.Z);
    corners[4] = irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MaxEdge.Z);
    corners[5] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y, bbox.MaxEdge.Z);
    corners[6] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
    corners[7] = irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);

    // Draw the 12 edges of the bounding box
    // Bottom face
    driver_->draw3DLine(corners[0], corners[1], white);
    driver_->draw3DLine(corners[1], corners[2], white);
    driver_->draw3DLine(corners[2], corners[3], white);
    driver_->draw3DLine(corners[3], corners[0], white);

    // Top face
    driver_->draw3DLine(corners[4], corners[5], white);
    driver_->draw3DLine(corners[5], corners[6], white);
    driver_->draw3DLine(corners[6], corners[7], white);
    driver_->draw3DLine(corners[7], corners[4], white);

    // Vertical edges
    driver_->draw3DLine(corners[0], corners[4], white);
    driver_->draw3DLine(corners[1], corners[5], white);
    driver_->draw3DLine(corners[2], corners[6], white);
    driver_->draw3DLine(corners[3], corners[7], white);
}
#endif

// --- Mouse Targeting Implementation ---

void IrrlichtRenderer::setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent, uint8_t level) {
    currentTargetId_ = spawnId;
    currentTargetName_ = name;
    currentTargetHpPercent_ = hpPercent;
    currentTargetLevel_ = level;
    // Enable animation debugging for targeted entity
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(spawnId);
    }
}

void IrrlichtRenderer::clearCurrentTarget() {
    if (isLoading()) return;
    currentTargetId_ = 0;
    currentTargetName_.clear();
    currentTargetHpPercent_ = 100;
    currentTargetLevel_ = 0;
    currentTargetInfo_ = TargetInfo();  // Reset to defaults
    // Disable animation debugging
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(0);
    }
}

void IrrlichtRenderer::setCurrentTargetInfo(const TargetInfo& info) {
    if (isLoading()) return;
    currentTargetInfo_ = info;
    // Also update the legacy fields for backward compatibility
    currentTargetId_ = info.spawnId;
    currentTargetName_ = info.name;
    currentTargetHpPercent_ = info.hpPercent;
    currentTargetLevel_ = info.level;
    // Enable animation debugging for targeted entity
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(info.spawnId);
    }
}

void IrrlichtRenderer::updateCurrentTargetHP(uint8_t hpPercent) {
    if (isLoading()) return;
    currentTargetHpPercent_ = hpPercent;
    currentTargetInfo_.hpPercent = hpPercent;
}

void IrrlichtRenderer::handleMouseTargeting(int clickX, int clickY) {
    if (!eventReceiver_ || !camera_ || !entityRenderer_) {
        return;
    }

    bool shiftHeld = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
    bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);

    // Get entity at click position
    uint16_t targetId = getEntityAtScreenPos(clickX, clickY);

    if (targetId != 0) {
        // Found an entity - verify it's visible (LOS from camera)
        const auto& entities = entityRenderer_->getEntities();
        auto it = entities.find(targetId);
        if (it != entities.end()) {
            const auto& visual = it->second;

            // Convert entity position to Irrlicht coords for LOS check
            // EQ (x, y, z) -> Irrlicht (x, z, y)
            irr::core::vector3df entityPos(visual.lastX, visual.lastZ + 5.0f, visual.lastY);
            irr::core::vector3df cameraPos = camera_->getPosition();

            if (checkEntityLOS(cameraPos, entityPos)) {
                // Check for shift+click on corpse to loot
                if (shiftHeld && visual.isCorpse) {
                    if (lootCorpseCallback_) {
                        lootCorpseCallback_(targetId);
                    }
                    if (bridge_) bridge_->pushIntent(eqt::events::LootCorpseIntent{targetId});
                } else if (ctrlHeld && visual.isNPC && !visual.isCorpse) {
                    // Ctrl+click on NPC - banker interaction
                    LOG_INFO(MOD_GRAPHICS, "Ctrl+click on NPC: {} (ID: {})", visual.name, targetId);
                    if (bankerInteractCallback_) {
                        bankerInteractCallback_(targetId);
                    }
                    if (bridge_) bridge_->pushIntent(eqt::events::BankerInteractIntent{targetId});
                } else {
                    // Entity is visible - set as target
                    LOG_INFO(MOD_GRAPHICS, "Target selected: {} (ID: {})", visual.name, targetId);

                    // Invoke callback to notify EverQuest class
                    if (targetCallback_) {
                        targetCallback_(targetId);
                    }
                    if (bridge_) bridge_->pushIntent(eqt::events::TargetIntent{targetId});
                }
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "Cannot target {} - obstructed", visual.name);
            }
        }
    } else {
        // No entity found - check for door click
        bool handledClick = false;
        if (doorManager_) {
            uint8_t doorId = doorManager_->getDoorAtScreenPos(clickX, clickY, camera_, collisionManager_);
            if (doorId != 0) {
                LOG_INFO(MOD_GRAPHICS, "Door clicked: ID {}", doorId);
                if (doorInteractCallback_) doorInteractCallback_(doorId);
                if (bridge_) bridge_->pushIntent(eqt::events::DoorInteractIntent{doorId});
                handledClick = true;
            }
        }

        // Check for world object (tradeskill container) click
        if (!handledClick) {
            uint32_t objectId = getWorldObjectAtScreenPos(clickX, clickY);
            if (objectId != 0) {
                LOG_INFO(MOD_GRAPHICS, "World object clicked: dropId {}", objectId);
                if (worldObjectInteractCallback_) worldObjectInteractCallback_(objectId);
                if (bridge_) bridge_->pushIntent(eqt::events::WorldObjectInteractIntent{objectId});
            }
        }
    }
}

uint16_t IrrlichtRenderer::getEntityAtScreenPos(int screenX, int screenY) {
    if (!collisionManager_ || !camera_ || !entityRenderer_ || !driver_) {
        return 0;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionManager_->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera_);

    // Check each entity for intersection
    const auto& entities = entityRenderer_->getEntities();
    float closestDist = std::numeric_limits<float>::max();
    uint16_t closestEntity = 0;

    for (const auto& [spawnId, visual] : entities) {
        // Skip player entity
        if (visual.isPlayer) {
            continue;
        }

        irr::scene::ISceneNode* node = visual.sceneNode;
        if (!node || !node->isVisible()) {
            continue;
        }

        // Get bounding box in world space
        irr::core::aabbox3df bbox = node->getTransformedBoundingBox();

        // Expand bounding box slightly for easier targeting
        irr::core::vector3df extent = bbox.getExtent();
        float minSize = 5.0f;  // Minimum clickable size
        if (extent.X < minSize) {
            float expand = (minSize - extent.X) / 2.0f;
            bbox.MinEdge.X -= expand;
            bbox.MaxEdge.X += expand;
        }
        if (extent.Y < minSize) {
            float expand = (minSize - extent.Y) / 2.0f;
            bbox.MinEdge.Y -= expand;
            bbox.MaxEdge.Y += expand;
        }
        if (extent.Z < minSize) {
            float expand = (minSize - extent.Z) / 2.0f;
            bbox.MinEdge.Z -= expand;
            bbox.MaxEdge.Z += expand;
        }

        // Check ray intersection with bounding box
        if (bbox.intersectsWithLine(ray)) {
            // Calculate distance to entity center
            irr::core::vector3df center = bbox.getCenter();
            float dist = ray.start.getDistanceFrom(center);

            if (dist < closestDist) {
                closestDist = dist;
                closestEntity = spawnId;
            }
        }
    }

    return closestEntity;
}

bool IrrlichtRenderer::checkEntityLOS(const irr::core::vector3df& cameraPos, const irr::core::vector3df& entityPos) {
    if (!zoneBspTree_) {
        return true;  // No BSP data — can't do collision, assume visible
    }

    // BSP PVS check first — if target region isn't visible from camera region, blocked by zone geometry
    if (zoneBspTree_) {
        // Irrlicht (x, y, z) -> EQ (x, z, y)
        size_t camRegion = zoneBspTree_->findRegionIndexForPoint(cameraPos.X, cameraPos.Z, cameraPos.Y);
        size_t entRegion = zoneBspTree_->findRegionIndexForPoint(entityPos.X, entityPos.Z, entityPos.Y);

        if (camRegion != SIZE_MAX && entRegion != SIZE_MAX && camRegion != entRegion) {
            if (camRegion < zoneBspTree_->regions.size()) {
                const auto& pvs = zoneBspTree_->regions[camRegion]->visibleRegions;
                if (entRegion < pvs.size() && !pvs[entRegion]) {
                    return false;  // Not PVS-visible — wall between camera and entity
                }
            }
        }
    }

    // PVS says visible (or no BSP) — raycast against BSP-filtered region geometry + doors/objects
    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;

    bool hit = checkCollisionBspFiltered(cameraPos, entityPos, hitPoint, hitTriangle);

    if (!hit) {
        return true;  // No obstruction
    }

    // Check if hit point is closer than entity
    float hitDist = cameraPos.getDistanceFrom(hitPoint);
    float entityDist = cameraPos.getDistanceFrom(entityPos);

    // Add a small margin to account for bounding box size
    return hitDist > (entityDist - 10.0f);
}

// --- Inventory UI Methods ---

void IrrlichtRenderer::setInventoryManager(eqt::inventory::InventoryManager* manager) {
    if (isLoading()) return;
    inventoryManager_ = manager;

    // Create window manager if not already created
    if (!windowManager_ && inventoryManager_ && driver_ && guienv_) {
        windowManager_ = std::make_unique<eqt::ui::WindowManager>();
        windowManager_->init(driver_, guienv_, inventoryManager_, config_.width, config_.height, config_.eqClientPath);

        // Wire up constrained icon loading config
        windowManager_->setIconLoadingEnabled(config_.constrainedConfig.enableItemIcons);
        if (constrainedTextureCache_) {
            windowManager_->setIconConstrainedTextureCache(constrainedTextureCache_.get());
        }
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_)
            windowManager_->getIconLoader().setGPUUploadThread(gpuUploadThread_.get());
#endif

        // Apply UI settings from config (UISettings was loaded in main.cpp)
        windowManager_->applyUISettings();

        // Initialize model view if entity renderer is available
        if (entityRenderer_ && smgr_) {
            windowManager_->initModelView(smgr_,
                                          entityRenderer_->getRaceModelLoader(),
                                          entityRenderer_->getEquipmentModelLoader());
        }

        // Set up display settings callback for environmental systems
        windowManager_->setDisplaySettingsChangedCallback([this]() {
            // Update render distance first (affects terrain, objects, entities)
            if (windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();
                setRenderDistance(settings.renderDistance);
                LOG_DEBUG(MOD_GRAPHICS, "Render distance updated to {}", settings.renderDistance);
            }

            // Apply environmental display settings (particles, boids, detail, tumbleweeds)
            applyEnvironmentalDisplaySettings();
        });

        // Apply initial render distance from saved settings
        if (windowManager_->getOptionsWindow()) {
            const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();
            setRenderDistance(settings.renderDistance);
            LOG_INFO(MOD_GRAPHICS, "Initial render distance set to {} from saved settings", settings.renderDistance);
        }

        // Set up chat callback if already registered
        if (chatSubmitCallback_) {
            windowManager_->setChatSubmitCallback(chatSubmitCallback_);
        }
    }

    // Create spell visual effects if not already created
    if (!spellVisualFX_ && smgr_ && driver_) {
        spellVisualFX_ = std::make_unique<EQ::SpellVisualFX>(smgr_, driver_, config_.eqClientPath);

        // Set up entity position callback for spell effects
        spellVisualFX_->setEntityPositionCallback(
            [this](uint16_t entity_id, irr::core::vector3df& out_pos) -> bool {
                // Hand bone pseudo-entity IDs (bit 15 set)
                if (EQ::SpellVisualFX::isHandBoneId(entity_id)) {
                    uint16_t realId = EQ::SpellVisualFX::realEntityId(entity_id);
                    bool rightHand = EQ::SpellVisualFX::isRightHand(entity_id);
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(realId);
                    if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                    int boneIdx = rightHand
                        ? it->second.animatedNode->findRightHandBoneIndex()
                        : it->second.animatedNode->findLeftHandBoneIndex();
                    if (boneIdx < 0) return false;
                    return it->second.animatedNode->getBoneWorldPosition(boneIdx, out_pos);
                }
                // Normal entity lookup
                if (!entityRenderer_) return false;
                const auto& entities = entityRenderer_->getEntities();
                auto it = entities.find(entity_id);
                if (it == entities.end()) return false;

                const auto& visual = it->second;
                // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
                out_pos.X = visual.lastX;
                out_pos.Y = visual.lastZ + visual.modelYOffset;
                out_pos.Z = visual.lastY;
                return true;
            }
        );

        LOG_DEBUG(MOD_GRAPHICS, "Spell visual effects initialized");

#ifdef EQT_HAS_GLES2
        if (particleManager_) {
            spellVisualFX_->setParticleManager(particleManager_.get());
            particleManager_->setEntityPositionCallback(
                [this](uint16_t entity_id, glm::vec3& out_pos) -> bool {
                    // Hand bone pseudo-entity IDs (bit 15 set)
                    if (EQ::SpellVisualFX::isHandBoneId(entity_id)) {
                        uint16_t realId = EQ::SpellVisualFX::realEntityId(entity_id);
                        bool rightHand = EQ::SpellVisualFX::isRightHand(entity_id);
                        if (!entityRenderer_) return false;
                        const auto& entities = entityRenderer_->getEntities();
                        auto it = entities.find(realId);
                        if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                        int boneIdx = rightHand
                            ? it->second.animatedNode->findRightHandBoneIndex()
                            : it->second.animatedNode->findLeftHandBoneIndex();
                        if (boneIdx < 0) return false;
                        irr::core::vector3df bonePos;
                        if (it->second.animatedNode->getBoneWorldPosition(boneIdx, bonePos)) {
                            out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
                            return true;
                        }
                        return false;
                    }
                    // Normal entity lookup
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(entity_id);
                    if (it == entities.end()) return false;
                    const auto& visual = it->second;
                    out_pos.x = visual.lastX;
                    out_pos.y = visual.lastZ + visual.modelYOffset;
                    out_pos.z = visual.lastY;
                    return true;
                }
            );
            particleManager_->setEntityDirectionCallback(
                [this](uint16_t entity_id, glm::vec3& out_dir) -> bool {
                    if (!EQ::SpellVisualFX::isHandBoneId(entity_id)) return false;
                    uint16_t realId = EQ::SpellVisualFX::realEntityId(entity_id);
                    bool rightHand = EQ::SpellVisualFX::isRightHand(entity_id);
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(realId);
                    if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                    int boneIdx = rightHand
                        ? it->second.animatedNode->findRightHandBoneIndex()
                        : it->second.animatedNode->findLeftHandBoneIndex();
                    if (boneIdx < 0) return false;
                    irr::core::vector3df dir;
                    if (it->second.animatedNode->getBoneWorldDirection(boneIdx, dir)) {
                        out_dir = glm::vec3(dir.X, dir.Y, dir.Z);
                        return true;
                    }
                    return false;
                }
            );
            LOG_DEBUG(MOD_GRAPHICS, "SpellVisualFX: GLES2 particle delegation enabled");
        }
#endif

        // Enable worker-driven mode for desktop GL path when SimulationWorker is running
        if (simulationWorker_ && simulationWorker_->isRunning()) {
            spellVisualFX_->setWorkerDriven(true);
            LOG_DEBUG(MOD_GRAPHICS, "SpellVisualFX: worker-driven mode enabled");
        }
    }
}

void IrrlichtRenderer::toggleInventory() {
    if (windowManager_) {
        windowManager_->toggleInventory();
    }
}

void IrrlichtRenderer::openInventory() {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->openInventory();
    }
}

void IrrlichtRenderer::closeInventory() {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->closeInventory();
    }
}

void IrrlichtRenderer::showNoteWindow(const std::string& text, uint8_t type) {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->showNoteWindow(text, type);
    }
}

bool IrrlichtRenderer::isInventoryOpen() const {
    return windowManager_ && windowManager_->isInventoryOpen();
}

void IrrlichtRenderer::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->setCharacterInfo(name, level, className);
    }
}

void IrrlichtRenderer::setCharacterDeity(const std::wstring& deity) {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->setCharacterDeity(deity);
    }
}

void IrrlichtRenderer::setExpProgress(float progress) {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->setExpProgress(progress);
    }
}

void IrrlichtRenderer::updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                             uint32_t curMana, uint32_t maxMana,
                                             uint32_t curEnd, uint32_t maxEnd,
                                             int ac, int atk,
                                             int str, int sta, int agi, int dex, int wis, int intel, int cha,
                                             int pr, int mr, int dr, int fr, int cr,
                                             float weight, float maxWeight,
                                             uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    if (isLoading()) return;
    if (windowManager_) {
        windowManager_->updateCharacterStats(curHp, maxHp, curMana, maxMana, curEnd, maxEnd,
                                             ac, atk, str, sta, agi, dex, wis, intel, cha,
                                             pr, mr, dr, fr, cr, weight, maxWeight,
                                             platinum, gold, silver, copper);
    }
}

void IrrlichtRenderer::updatePlayerAppearance(uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "IrrlichtRenderer::updatePlayerAppearance race={} gender={}", raceId, gender);
    if (windowManager_) {
        windowManager_->setPlayerAppearance(raceId, gender, appearance);
    }
}

void IrrlichtRenderer::updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "IrrlichtRenderer::updateEntityAppearance spawn={} race={} gender={}",
              spawnId, raceId, gender);
    if (entityRenderer_) {
        entityRenderer_->updateEntityAppearance(spawnId, raceId, gender, appearance);
    }
}

void IrrlichtRenderer::setChatSubmitCallback(ChatSubmitCallback callback) {
    chatSubmitCallback_ = callback;
    if (windowManager_) {
        windowManager_->setChatSubmitCallback(callback);
    }
}

void IrrlichtRenderer::setReadItemCallback(ReadItemCallback callback) {
    if (windowManager_) {
        windowManager_->setOnReadItem(callback);
    }
}

void IrrlichtRenderer::setZoneLineDebug(bool inZoneLine, uint16_t targetZoneId, const std::string& debugText) {
    if (isLoading()) return;
    inZoneLine_ = inZoneLine;
    zoneLineTargetZoneId_ = targetZoneId;
    zoneLineDebugText_ = debugText;
}

void IrrlichtRenderer::drawZoneLineOverlay() {
    if (!inZoneLine_ || !driver_) {
        return;
    }

    // Draw a semi-transparent pink overlay on the screen edges
    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

    // Pink color with alpha
    irr::video::SColor pink(100, 255, 50, 150);  // Semi-transparent pink

    // Draw border rectangles (top, bottom, left, right)
    int borderWidth = 15;

    // Top border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, 0, screenSize.Width, borderWidth));
    // Bottom border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, screenSize.Height - borderWidth, screenSize.Width, screenSize.Height));
    // Left border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, borderWidth, borderWidth, screenSize.Height - borderWidth));
    // Right border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(screenSize.Width - borderWidth, borderWidth, screenSize.Width, screenSize.Height - borderWidth));

    // Draw zone line text using GUI environment's built-in font
    if (guienv_) {
        irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
        if (font) {
            std::wstring text = L"[ZONE LINE] Target Zone: " + std::to_wstring(zoneLineTargetZoneId_);
            irr::core::dimension2d<irr::u32> textSize = font->getDimension(text.c_str());
            int textX = (screenSize.Width - textSize.Width) / 2;
            int textY = borderWidth + 5;
            font->draw(text.c_str(), irr::core::rect<irr::s32>(textX, textY, textX + textSize.Width, textY + textSize.Height),
                       irr::video::SColor(255, 255, 100, 200));  // Bright pink text

            // Draw debug text if available
            if (!zoneLineDebugText_.empty()) {
                std::wstring debugWStr(zoneLineDebugText_.begin(), zoneLineDebugText_.end());
                irr::core::dimension2d<irr::u32> debugSize = font->getDimension(debugWStr.c_str());
                int debugX = (screenSize.Width - debugSize.Width) / 2;
                int debugY = textY + textSize.Height + 5;
                font->draw(debugWStr.c_str(), irr::core::rect<irr::s32>(debugX, debugY, debugX + debugSize.Width, debugY + debugSize.Height),
                           irr::video::SColor(255, 255, 200, 255));  // Light pink text
            }
        }
    }
}

void IrrlichtRenderer::setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>& boxes) {
    if (isLoading()) return;
    // Clear existing boxes first
    clearZoneLineBoundingBoxes();

    if (!smgr_ || !driver_) {
        LOG_WARN(MOD_GRAPHICS, "Cannot create zone line boxes - renderer not initialized");
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "Creating {} zone line visualization boxes", boxes.size());

    for (const auto& box : boxes) {
        createZoneLineBoxMesh(box);
    }
    zoneLineBoxesCreated_ = showZoneLineBoxes_;
}

void IrrlichtRenderer::clearZoneLineBoundingBoxes() {
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->remove();
            boxNode.node = nullptr;
        }
    }
    zoneLineBoxNodes_.clear();
    zoneLineBoxesCreated_ = false;
}

void IrrlichtRenderer::toggleZoneLineVisualization() {
    showZoneLineBoxes_ = !showZoneLineBoxes_;

    // Remove/recreate scene nodes instead of toggling visibility.
    // Calling setVisible(false) on nodes with EMT_TRANSPARENT_VERTEX_ALPHA
    // triggers a GPU hang on Lima (Mali 400) driver.
    if (showZoneLineBoxes_) {
        recreateZoneLineBoxSceneNodes();
    } else {
        removeZoneLineBoxSceneNodes();
    }

    // Notify EverQuest to enable/disable zoning when zone lines are toggled
    if (zoningEnabledCallback_) {
        zoningEnabledCallback_(showZoneLineBoxes_);
    }
    if (bridge_) bridge_->pushIntent(eqt::events::ZoningEnabledIntent{showZoneLineBoxes_});

    LOG_INFO(MOD_GRAPHICS, "Zone line visualization and zoning {}", showZoneLineBoxes_ ? "enabled" : "disabled");
}

void IrrlichtRenderer::removeZoneLineBoxSceneNodes() {
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->remove();
            boxNode.node = nullptr;
        }
    }
    zoneLineBoxesCreated_ = false;
}

void IrrlichtRenderer::recreateZoneLineBoxSceneNodes() {
    if (zoneLineBoxesCreated_ || !smgr_ || !driver_) return;

    // Recreate scene nodes from stored bounding box data
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) continue;  // Already has a node

        // Reconstruct bounding box from stored data
        EQT::ZoneLineBoundingBox box;
        box.targetZoneId = boxNode.targetZoneId;
        box.isProximityBased = boxNode.isProximityBased;
        box.minX = boxNode.minX; box.minY = boxNode.minY; box.minZ = boxNode.minZ;
        box.maxX = boxNode.maxX; box.maxY = boxNode.maxY; box.maxZ = boxNode.maxZ;
        createZoneLineBoxMeshForNode(box, boxNode);
    }
    zoneLineBoxesCreated_ = true;
}

void IrrlichtRenderer::createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox& box) {
    // Store metadata and coordinates for later recreation
    ZoneLineBoxNode boxNode;
    boxNode.node = nullptr;
    boxNode.targetZoneId = box.targetZoneId;
    boxNode.isProximityBased = box.isProximityBased;
    boxNode.minX = box.minX; boxNode.minY = box.minY; boxNode.minZ = box.minZ;
    boxNode.maxX = box.maxX; boxNode.maxY = box.maxY; boxNode.maxZ = box.maxZ;

    // Only create scene nodes if visualization is currently enabled
    if (showZoneLineBoxes_ && smgr_ && driver_) {
        createZoneLineBoxMeshForNode(box, boxNode);
    }

    zoneLineBoxNodes_.push_back(boxNode);
}

void IrrlichtRenderer::createZoneLineBoxMeshForNode(const EQT::ZoneLineBoundingBox& box, ZoneLineBoxNode& boxNode) {
    if (!smgr_ || !driver_) return;

    // Convert EQ coordinates to Irrlicht coordinates
    // EQ uses Z-up: (x, y, z) -> Irrlicht Y-up: (x, z, y)
    float irrMinX = box.minX;
    float irrMinY = box.minZ;  // EQ Z -> Irrlicht Y
    float irrMinZ = box.minY;  // EQ Y -> Irrlicht Z
    float irrMaxX = box.maxX;
    float irrMaxY = box.maxZ;
    float irrMaxZ = box.maxY;

    // Calculate box dimensions and center
    float width = irrMaxX - irrMinX;
    float height = irrMaxY - irrMinY;
    float depth = irrMaxZ - irrMinZ;
    float centerX = (irrMinX + irrMaxX) / 2.0f;
    float centerY = (irrMinY + irrMaxY) / 2.0f;
    float centerZ = (irrMinZ + irrMaxZ) / 2.0f;

    LOG_INFO(MOD_GRAPHICS, "Zone line box -> zone {}: EQ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}) => Irr center({:.1f},{:.1f},{:.1f}) size({:.1f},{:.1f},{:.1f})",
        box.targetZoneId, box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ,
        centerX, centerY, centerZ, width, height, depth);

    // Create a cube mesh using geometry creator
    const irr::scene::IGeometryCreator* geomCreator = smgr_->getGeometryCreator();
    if (!geomCreator) {
        LOG_WARN(MOD_GRAPHICS, "No geometry creator available");
        return;
    }

    // Create a unit cube mesh and scale it
    irr::scene::IMesh* cubeMesh = geomCreator->createCubeMesh(irr::core::vector3df(width, height, depth));
    if (!cubeMesh) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create cube mesh");
        return;
    }

    // Create scene node for the box
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(cubeMesh);
    cubeMesh->drop();  // Scene manager now owns it

    if (!node) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create mesh scene node");
        return;
    }

    // Position the box
    node->setPosition(irr::core::vector3df(centerX, centerY, centerZ));

    // Use wireframe rendering - avoids EMT_TRANSPARENT_VERTEX_ALPHA which
    // causes GPU hangs on Lima (Mali 400) driver when toggling visibility
    irr::video::SColor color;
    if (box.isProximityBased) {
        color = irr::video::SColor(255, 0, 255, 255);  // Cyan
    } else {
        color = irr::video::SColor(255, 255, 0, 255);  // Magenta
    }

    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        irr::video::SMaterial& mat = node->getMaterial(i);
        mat.MaterialType = irr::video::EMT_SOLID;
        mat.Wireframe = true;
        mat.Lighting = false;
        mat.BackfaceCulling = false;
        mat.Thickness = 2.0f;
        mat.AmbientColor = color;
        mat.DiffuseColor = color;
    }

    // Set vertex colors for wireframe
    irr::scene::IMeshBuffer* meshBuffer = node->getMesh()->getMeshBuffer(0);
    if (meshBuffer) {
        irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(meshBuffer->getVertices());
        irr::u32 vertexCount = meshBuffer->getVertexCount();
        for (irr::u32 i = 0; i < vertexCount; ++i) {
            vertices[i].Color = color;
        }
    }

    node->setVisible(true);
    boxNode.node = node;

    LOG_TRACE(MOD_GRAPHICS, "Created zone line box for zone {} at ({},{},{}) size ({},{},{})",
        box.targetZoneId, centerX, centerY, centerZ, width, height, depth);
}

void IrrlichtRenderer::drawZoneLineBoxLabels() {
    if (!showZoneLineBoxes_ || !driver_ || !guienv_ || !camera_) {
        return;
    }

    irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
    if (!font) return;

    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

    for (const auto& boxNode : zoneLineBoxNodes_) {
        if (!boxNode.node || !boxNode.node->isVisible()) continue;

        // Get 3D position of box center
        irr::core::vector3df boxPos = boxNode.node->getAbsolutePosition();

        // Convert 3D position to 2D screen position
        irr::core::position2d<irr::s32> screenPos = smgr_->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(
            boxPos, camera_);

        // Check if on screen
        if (screenPos.X < 0 || screenPos.X >= (irr::s32)screenSize.Width ||
            screenPos.Y < 0 || screenPos.Y >= (irr::s32)screenSize.Height) {
            continue;
        }

        // Create label text
        std::wstring label = L"Zone " + std::to_wstring(boxNode.targetZoneId);
        if (boxNode.isProximityBased) {
            label += L" (prox)";
        }

        // Draw label
        irr::core::dimension2d<irr::u32> textSize = font->getDimension(label.c_str());
        int textX = screenPos.X - textSize.Width / 2;
        int textY = screenPos.Y - textSize.Height / 2;

        // Background rectangle
        irr::video::SColor bgColor(150, 0, 0, 0);
        driver_->draw2DRectangle(bgColor, irr::core::rect<irr::s32>(
            textX - 2, textY - 2, textX + textSize.Width + 2, textY + textSize.Height + 2));

        // Text color based on type
        irr::video::SColor textColor = boxNode.isProximityBased ?
            irr::video::SColor(255, 0, 255, 255) :  // Cyan
            irr::video::SColor(255, 255, 0, 255);   // Magenta

        font->draw(label.c_str(), irr::core::rect<irr::s32>(
            textX, textY, textX + textSize.Width, textY + textSize.Height), textColor);
    }
}

void IrrlichtRenderer::drawFPSCounter() {
    if (!driver_ || !guienv_) {
        return;
    }

    irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
    if (!font) {
        return;
    }

    // Build FPS text
    std::wstring fpsText = L"FPS: " + std::to_wstring(currentFps_);

    // Get screen dimensions and text size for centering
    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();
    irr::core::dimension2d<irr::u32> textSize = font->getDimension(fpsText.c_str());

    // Position at top center with small padding from top edge
    int textX = (screenSize.Width - textSize.Width) / 2;
    int textY = 5;

    // Draw with a semi-transparent black background for readability
    irr::core::rect<irr::s32> bgRect(textX - 4, textY - 2,
                                      textX + textSize.Width + 4,
                                      textY + textSize.Height + 2);
    driver_->draw2DRectangle(irr::video::SColor(128, 0, 0, 0), bgRect);

    // Draw FPS text in white
    font->draw(fpsText.c_str(),
               irr::core::rect<irr::s32>(textX, textY, textX + textSize.Width, textY + textSize.Height),
               irr::video::SColor(255, 255, 255, 255));
}

void IrrlichtRenderer::setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->setEntityWeaponSkills(spawnId, primaryWeaponSkill, secondaryWeaponSkill);
    }
}

uint8_t IrrlichtRenderer::getEntityPrimaryWeaponSkill(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->getEntityPrimaryWeaponSkill(spawnId);
    }
    return 0;
}

uint8_t IrrlichtRenderer::getEntitySecondaryWeaponSkill(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->getEntitySecondaryWeaponSkill(spawnId);
    }
    return 0;
}

void IrrlichtRenderer::queueCombatAnimation(uint16_t sourceId, uint16_t targetId,
                                             uint8_t weaponSkill, int32_t damage, float damagePercent) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueCombatAnimation(sourceId, targetId, weaponSkill, damage, damagePercent);
    }
}

bool IrrlichtRenderer::hasEntityPendingCombatAnims(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->hasEntityPendingCombatAnims(spawnId);
    }
    return false;
}

void IrrlichtRenderer::queueReceivedDamageAnimation(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueReceivedDamageAnimation(spawnId);
    }
}

void IrrlichtRenderer::queueSkillAnimation(uint16_t spawnId, const std::string& animCode) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueSkillAnimation(spawnId, animCode);
    }
}

void IrrlichtRenderer::triggerFirstPersonAttack() {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->triggerFirstPersonAttack();
    }
}

// ============================================================================
// Memory Report
// ============================================================================

static std::string formatBytes(size_t bytes) {
    if (bytes >= 1024 * 1024)
        return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return fmt::format("{:.1f} KB", bytes / 1024.0);
    return fmt::format("{} B", bytes);
}

std::vector<std::string> IrrlichtRenderer::getMemoryReport(const MemoryReportInput& ext) const {
    std::vector<std::string> lines;
    size_t totalEstimate = 0;

    lines.push_back("=== Memory Usage Report ===");
    lines.push_back("");
    lines.push_back("--- Graphics ---");

    // --- Texture Cache ---
    // Note: texture cache textures are created via driver->addTexture() and already
    // counted in GPU Textures below — do NOT add to totalEstimate here.
    if (constrainedTextureCache_) {
        size_t used = constrainedTextureCache_->getCurrentUsage();
        size_t limit = constrainedTextureCache_->getMemoryLimit();
        size_t count = constrainedTextureCache_->getTextureCount();
        lines.push_back(fmt::format("[Texture Cache] {} / {} ({} textures, GPU cost in GPU Textures)",
            formatBytes(used), formatBytes(limit), count));
        lines.push_back(fmt::format("  Hits: {}  Misses: {}  Hit rate: {:.1f}%  Evictions: {}",
            constrainedTextureCache_->getCacheHits(),
            constrainedTextureCache_->getCacheMisses(),
            constrainedTextureCache_->getHitRate(),
            constrainedTextureCache_->getEvictionCount()));
    } else {
        // Non-constrained mode: count Irrlicht driver textures
        if (driver_) {
            int texCount = driver_->getTextureCount();
            lines.push_back(fmt::format("[Textures] {} loaded (driver-managed, no budget)", texCount));
        }
    }

    // --- Mesh Cache ---
    if (constrainedMeshCache_) {
        size_t used = constrainedMeshCache_->getCurrentUsage();
        size_t limit = constrainedMeshCache_->getMemoryLimit();
        size_t loaded = constrainedMeshCache_->getLoadedCount();
        size_t total = constrainedMeshCache_->getTotalCount();
        size_t evicted = constrainedMeshCache_->getEvictedCount();
        totalEstimate += used;
        lines.push_back(fmt::format("[Mesh Cache] {} / {} ({}/{} regions loaded, {} evicted)",
            formatBytes(used), formatBytes(limit), loaded, total, evicted));
        lines.push_back(fmt::format("  Evictions: {}  Rebuilds: {}  Hit rate: {:.1f}%",
            constrainedMeshCache_->getEvictionCount(),
            constrainedMeshCache_->getRebuildCount(),
            constrainedMeshCache_->getHitRate()));
    }

    // --- Zone Geometry ---
    {
        size_t regionCount = regionMeshNodes_.size();
        size_t objectCount = objectNodes_.size();
        size_t lightCount = zoneLightData_.size();
        size_t objectLightCount = objectLights_.size();

        // Estimate zone mesh memory from Irrlicht mesh buffers
        size_t zoneMeshBytes = 0;
        if (zoneMeshNode_ && zoneMeshNode_->getMesh()) {
            auto* mesh = zoneMeshNode_->getMesh();
            for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                auto* buf = mesh->getMeshBuffer(i);
                zoneMeshBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                zoneMeshBytes += buf->getIndexCount() * sizeof(uint16_t);
            }
        }
        // PVS region meshes
        size_t pvsBytes = 0;
        uint32_t pvsVerts = 0, pvsIndices = 0;
        for (const auto& [id, node] : regionMeshNodes_) {
            if (node && node->getMesh()) {
                auto* mesh = node->getMesh();
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    auto* buf = mesh->getMeshBuffer(i);
                    pvsVerts += buf->getVertexCount();
                    pvsIndices += buf->getIndexCount();
                    pvsBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                    pvsBytes += buf->getIndexCount() * sizeof(uint16_t);
                }
            }
        }

        size_t zoneTotal = zoneMeshBytes + pvsBytes;
        totalEstimate += zoneTotal;
        if (usePvsCulling_) {
            lines.push_back(fmt::format("[Zone Geometry] {} ({} regions, {}V/{}I)",
                formatBytes(pvsBytes), regionCount, pvsVerts, pvsIndices));
        } else if (zoneMeshNode_) {
            auto* mesh = zoneMeshNode_->getMesh();
            uint32_t verts = 0, indices = 0;
            if (mesh) {
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    verts += mesh->getMeshBuffer(i)->getVertexCount();
                    indices += mesh->getMeshBuffer(i)->getIndexCount();
                }
            }
            lines.push_back(fmt::format("[Zone Geometry] {} (single mesh, {}V/{}I)",
                formatBytes(zoneMeshBytes), verts, indices));
        } else {
            lines.push_back("[Zone Geometry] Not loaded");
        }

        // Placeable objects
        size_t objBytes = 0;
        for (auto* node : objectNodes_) {
            if (node && node->getMesh()) {
                auto* mesh = node->getMesh();
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    auto* buf = mesh->getMeshBuffer(i);
                    objBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                    objBytes += buf->getIndexCount() * sizeof(uint16_t);
                }
            }
        }
        totalEstimate += objBytes;
        lines.push_back(fmt::format("[Placeable Objects] {} ({} objects)",
            formatBytes(objBytes), objectCount));

        // Texture Atlases
        size_t atlasBytes = 0;
        if (zoneAtlas_) atlasBytes += zoneAtlas_->getGPUMemoryUsage();
        if (objAtlas_) atlasBytes += objAtlas_->getGPUMemoryUsage();
        if (atlasBytes > 0) {
            size_t tileCount = 0;
            size_t pageCount = 0;
            if (zoneAtlas_) { tileCount += zoneAtlas_->getTileCount(); pageCount += zoneAtlas_->getPageCount(); }
            if (objAtlas_) { tileCount += objAtlas_->getTileCount(); pageCount += objAtlas_->getPageCount(); }
            totalEstimate += atlasBytes;
            lines.push_back(fmt::format("[Texture Atlases] {} ({} pages, {} tiles)",
                formatBytes(atlasBytes), pageCount, tileCount));
        }

        lines.push_back(fmt::format("[Zone Lights] {} zone + {} object",
            lightCount, objectLightCount));
    }

    // --- Entity Renderer ---
    if (entityRenderer_) {
        size_t entityCount = entityRenderer_->getEntityCount();
        size_t modeledCount = entityRenderer_->getModeledEntityCount();
        size_t visibleCount = entityRenderer_->getVisibleEntityCount();
        auto* rml = entityRenderer_->getRaceModelLoader();
        size_t loadedModels = rml ? rml->getLoadedModelCount() : 0;
        size_t perInstanceBytes = entityRenderer_->getPerInstanceMemoryBytes();
        totalEstimate += perInstanceBytes;
        lines.push_back(fmt::format("[Entities] {} total, {} modeled, {} visible, {} race models, {} instance data",
            entityCount, modeledCount, visibleCount, loadedModels, formatBytes(perInstanceBytes)));
    }

    // --- Character Models ---
    if (entityRenderer_) {
        if (auto* rml = entityRenderer_->getRaceModelLoader()) {
            auto stats = rml->getMemoryStats();
            size_t rmlTexTotal = stats.globalTextureBytes + stats.numberedTextureBytes
                            + stats.armorTextureBytes + stats.zoneTextureBytes
                            + stats.otherChrTextureBytes;
            size_t rmlTotal = rmlTexTotal + stats.modelGeometryBytes + stats.modelSkeletonBytes
                            + stats.characterModelBytes + stats.animatedMeshBytes
                            + stats.irrlichtMeshBytes;
            totalEstimate += rmlTotal;
            lines.push_back(fmt::format("[Character Models] {} (tex {}, geom {}, skel {}, char {}, anim mesh {}, irr mesh {})",
                formatBytes(rmlTotal),
                formatBytes(rmlTexTotal),
                formatBytes(stats.modelGeometryBytes),
                formatBytes(stats.modelSkeletonBytes),
                formatBytes(stats.characterModelBytes),
                formatBytes(stats.animatedMeshBytes),
                formatBytes(stats.irrlichtMeshBytes)));
        }
    }

    // --- Equipment Models ---
    if (entityRenderer_) {
        if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
            auto stats = eml->getMemoryStats();
            size_t emlTotal = stats.rawTextureBytes + stats.geometryBytes
                            + stats.irrlichtMeshBytes + stats.indexBytes;
            totalEstimate += emlTotal;
            lines.push_back(fmt::format("[Equipment Models] {} (tex {}, geom {}, mesh {}, idx {}; {}/{} loaded, {} mappings)",
                formatBytes(emlTotal),
                formatBytes(stats.rawTextureBytes),
                formatBytes(stats.geometryBytes),
                formatBytes(stats.irrlichtMeshBytes),
                formatBytes(stats.indexBytes),
                stats.loadedGeometryCount, stats.indexedModelCount, stats.mappingCount));
        }
    }

    // --- S3D Zone Source Data (CPU-side geometry, BSP, animations kept for mesh rebuilds) ---
    if (currentZone_) {
        size_t zoneSourceBytes = currentZone_->getMemoryUsage();
        totalEstimate += zoneSourceBytes;

        // Break down major components
        size_t combinedGeomBytes = currentZone_->geometry ? currentZone_->geometry->getFullMemoryUsage() : 0;
        size_t wldBytes = currentZone_->wldLoader ? currentZone_->wldLoader->getMemoryUsage() : 0;
        size_t objGeomBytes = 0;
        for (const auto& [name, geom] : currentZone_->objectGeometries)
            if (geom) objGeomBytes += geom->getFullMemoryUsage();

        lines.push_back(fmt::format("[S3D Zone Source Data] {} (combined geom {}, WLD {}, obj geom {})",
            formatBytes(zoneSourceBytes), formatBytes(combinedGeomBytes),
            formatBytes(wldBytes), formatBytes(objGeomBytes)));

        // Log detailed WLD memory breakdown
        if (currentZone_->wldLoader) {
            currentZone_->wldLoader->logMemoryBreakdown();
        }
    }

    // --- GPU Texture Memory (actual GPU-side allocations in shared RAM) ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        size_t gpuTexBytes = gles2GetGpuTextureMemoryUsage(driver_);
        int texCount = driver_->getTextureCount();
        totalEstimate += gpuTexBytes;
        lines.push_back(fmt::format("[GPU Textures] {} ({} textures in shared RAM)",
            formatBytes(gpuTexBytes), texCount));
    }
#else
    if (driver_) {
        int texCount = driver_->getTextureCount();
        lines.push_back(fmt::format("[Irrlicht Driver] {} textures (GPU-side, not tracked)", texCount));
    }
#endif

    // --- HCMap / Collision ---
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        auto mapStats = collisionMap_->GetMemoryStats();
        totalEstimate += mapStats.totalBytes;
        lines.push_back(fmt::format("[HCMap/Collision] {} ({} verts, {} faces)",
            formatBytes(mapStats.totalBytes), mapStats.vertexCount, mapStats.faceCount));
    }

    // --- Framebuffer ---
    if (driver_) {
        auto screenSize = driver_->getScreenSize();
        size_t totalFb = config_.constrainedConfig.calculateFramebufferUsage(
            static_cast<int>(screenSize.Width), static_cast<int>(screenSize.Height));
        totalEstimate += totalFb;
        int colorBpp = (config_.constrainedConfig.colorDepthBits == 16) ? 16 : 32;
        lines.push_back(fmt::format("[Framebuffer] {} ({}x{}, {}bpp color, D24S8)",
            formatBytes(totalFb), screenSize.Width, screenSize.Height, colorBpp));
    }

    // --- VBO/EBO GPU Buffers ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        size_t hwbBytes = gles2GetHWBufferMemoryUsage(driver_);
        size_t hwbCount = gles2GetHWBufferCount(driver_);
        if (hwbCount > 0) {
            totalEstimate += hwbBytes;
            lines.push_back(fmt::format("[VBO/EBO GPU Buffers] {} ({} buffers)",
                formatBytes(hwbBytes), hwbCount));
        }
    }

    // --- GPU Upload Thread ---
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        size_t pending = gpuUploadThread_->getPendingCount();
        uint64_t completed = gpuUploadThread_->getTotalUploadsCompleted();
        uint64_t totalUs = gpuUploadThread_->getTotalUploadTimeUs();
        float avgUs = completed > 0 ? static_cast<float>(totalUs) / completed : 0.0f;
        lines.push_back(fmt::format("[GPU Upload Thread] {} pending, {} completed, avg {:.0f}us/upload",
            pending, completed, avgUs));
    }
#endif

    // ============================================================
    // Audio section (from external input)
    // ============================================================
    if (ext.audioAvailable) {
        lines.push_back("");
        lines.push_back("--- Audio ---");

        totalEstimate += ext.sfxCacheBytes;
        lines.push_back(fmt::format("[SFX Cache] {}", formatBytes(ext.sfxCacheBytes)));

        totalEstimate += ext.soundBufferCacheBytes;
        if (ext.soundBufferCacheMaxBytes > 0) {
            lines.push_back(fmt::format("[Sound Buffer Cache] {} / {}",
                formatBytes(ext.soundBufferCacheBytes), formatBytes(ext.soundBufferCacheMaxBytes)));
        } else {
            lines.push_back(fmt::format("[Sound Buffer Cache] {} (no limit)",
                formatBytes(ext.soundBufferCacheBytes)));
        }

        if (ext.soundFontEstimateBytes > 0) {
            totalEstimate += ext.soundFontEstimateBytes;
            lines.push_back(fmt::format("[SoundFont] ~{}", formatBytes(ext.soundFontEstimateBytes)));
        }

        if (ext.musicDecodedBytes > 0) {
            totalEstimate += ext.musicDecodedBytes;
            lines.push_back(fmt::format("[Music Decoded Data] {}", formatBytes(ext.musicDecodedBytes)));
        }

        if (ext.audioPfsArchiveBytes > 0) {
            totalEstimate += ext.audioPfsArchiveBytes;
            lines.push_back(fmt::format("[Audio PFS Archives] {}", formatBytes(ext.audioPfsArchiveBytes)));
        }

        lines.push_back(fmt::format("[Zone Emitters] {} total, {} active",
            ext.zoneEmitterCount, ext.activeEmitterCount));
    }

    // ============================================================
    // Network section (from external input)
    // ============================================================
    if (!ext.connections.empty()) {
        lines.push_back("");
        lines.push_back("--- Network ---");
        for (const auto& conn : ext.connections) {
            lines.push_back(fmt::format("[{}] RX {} TX {} | Ping {}ms",
                conn.name, formatBytes(conn.recvBytes), formatBytes(conn.sentBytes), conn.avgPing));
        }
    }

    // ============================================================
    // Game Data section (from external input)
    // ============================================================
    if (ext.entityCount > 0 || ext.doorCount > 0 || ext.spellDbCount > 0) {
        lines.push_back("");
        lines.push_back("--- Game Data ---");

        if (ext.entityCount > 0) {
            totalEstimate += ext.entityEstimateBytes;
            lines.push_back(fmt::format("[Entities] {} (~{})",
                ext.entityCount, formatBytes(ext.entityEstimateBytes)));
        }
        if (ext.doorCount > 0) {
            totalEstimate += ext.doorEstimateBytes;
            lines.push_back(fmt::format("[Doors] {} (~{})",
                ext.doorCount, formatBytes(ext.doorEstimateBytes)));
        }
        if (ext.spellDbCount > 0) {
            totalEstimate += ext.spellDbEstimateBytes;
            lines.push_back(fmt::format("[Spell Database] {} spells (~{})",
                ext.spellDbCount, formatBytes(ext.spellDbEstimateBytes)));
        }
    }

    // ============================================================
    // Process section (from external input)
    // ============================================================
    if (ext.processRssBytes > 0) {
        lines.push_back("");
        lines.push_back("--- Process ---");
        lines.push_back(fmt::format("[RSS] {}", formatBytes(ext.processRssBytes)));
        lines.push_back(fmt::format("[Virtual] {}", formatBytes(ext.processVmBytes)));
        if (ext.sharedLibBytes > 0 || ext.anonBytes > 0 || ext.stackBytes > 0) {
            lines.push_back(fmt::format("[Shared Libs] {}", formatBytes(ext.sharedLibBytes)));
            lines.push_back(fmt::format("[Anonymous (heap+mmap)] {}", formatBytes(ext.anonBytes)));
            if (ext.stackBytes > 0)
                lines.push_back(fmt::format("[Thread Stacks] {}", formatBytes(ext.stackBytes)));
        }
    }

    // --- Total ---
    lines.push_back("");
    if (ext.processRssBytes > 0) {
        size_t gap = ext.processRssBytes > totalEstimate ? ext.processRssBytes - totalEstimate : 0;
        float pct = ext.processRssBytes > 0 ? (totalEstimate * 100.0f / ext.processRssBytes) : 0.0f;
        lines.push_back(fmt::format("--- Tracked: {} / RSS: {} ({:.0f}%, gap: {}) ---",
            formatBytes(totalEstimate), formatBytes(ext.processRssBytes), pct, formatBytes(gap)));
    } else {
        lines.push_back(fmt::format("--- Tracked total: {} ---", formatBytes(totalEstimate)));
    }

    return lines;
}

// ============================================================================
// RDP Server Integration
// ============================================================================

#ifdef WITH_RDP

bool IrrlichtRenderer::initRDP(uint16_t port) {
    if (rdpServer_) {
        LOG_WARN(MOD_GRAPHICS, "RDP server already initialized");
        return true;
    }

    rdpServer_ = std::make_unique<RDPServer>();

    if (!rdpServer_->initialize(port)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to initialize RDP server on port {}", port);
        rdpServer_.reset();
        return false;
    }

    // Set resolution to match window size
    if (driver_) {
        auto screenSize = driver_->getScreenSize();
        rdpServer_->setResolution(screenSize.Width, screenSize.Height);
    } else {
        rdpServer_->setResolution(config_.width, config_.height);
    }

    // Set up input callbacks to route RDP input to the renderer
    rdpServer_->setKeyboardCallback([this](uint16_t flags, uint8_t scancode) {
        handleRDPKeyboard(flags, scancode);
    });

    rdpServer_->setMouseCallback([this](uint16_t flags, uint16_t x, uint16_t y) {
        handleRDPMouse(flags, x, y);
    });

    LOG_INFO(MOD_GRAPHICS, "RDP server initialized on port {}", port);
    return true;
}

bool IrrlichtRenderer::startRDPServer() {
    if (!rdpServer_) {
        LOG_ERROR(MOD_GRAPHICS, "RDP server not initialized");
        return false;
    }

    if (!rdpServer_->start()) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to start RDP server");
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "RDP server started");
    return true;
}

void IrrlichtRenderer::stopRDPServer() {
    if (rdpServer_) {
        rdpServer_->stop();
        LOG_INFO(MOD_GRAPHICS, "RDP server stopped");
    }
}

bool IrrlichtRenderer::isRDPRunning() const {
    return rdpServer_ && rdpServer_->isRunning();
}

size_t IrrlichtRenderer::getRDPClientCount() const {
    return rdpServer_ ? rdpServer_->getClientCount() : 0;
}

void IrrlichtRenderer::captureFrameForRDP() {
    if (!rdpServer_ || !rdpServer_->isRunning() || rdpServer_->getClientCount() == 0) {
        return;
    }

    if (!driver_) {
        return;
    }

    // Capture the current framebuffer
    irr::video::IImage* screenshot = driver_->createScreenShot();
    if (!screenshot) {
        return;
    }

    // Get image dimensions and data
    irr::core::dimension2d<irr::u32> size = screenshot->getDimension();
    uint32_t width = size.Width;
    uint32_t height = size.Height;

    // Convert to BGRA format if needed
    // Irrlicht's software renderer typically uses A8R8G8B8 (which is BGRA in memory)
    irr::video::ECOLOR_FORMAT format = screenshot->getColorFormat();

    if (format == irr::video::ECF_A8R8G8B8) {
        // Direct copy - format matches
        // Use lock() to get raw pixel data pointer
        const uint8_t* data = static_cast<const uint8_t*>(screenshot->lock());
        if (data) {
            uint32_t pitch = width * 4;  // 4 bytes per pixel for BGRA
            rdpServer_->updateFrame(data, width, height, pitch);
            screenshot->unlock();
        }
    } else {
        // Need to convert - create a temporary buffer
        std::vector<uint8_t> bgraData(width * height * 4);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                irr::video::SColor color = screenshot->getPixel(x, y);
                size_t offset = (y * width + x) * 4;
                bgraData[offset + 0] = color.getBlue();
                bgraData[offset + 1] = color.getGreen();
                bgraData[offset + 2] = color.getRed();
                bgraData[offset + 3] = color.getAlpha();
            }
        }

        rdpServer_->updateFrame(bgraData.data(), width, height, width * 4);
    }

    screenshot->drop();
}

void IrrlichtRenderer::handleRDPKeyboard(uint16_t flags, uint8_t scancode) {
    if (!device_ || !eventReceiver_) {
        return;
    }

    // Translate RDP scancode to Irrlicht key code
    bool extended = (flags & 0x0100) != 0;  // KBD_FLAGS_EXTENDED
    bool released = (flags & 0x8000) != 0;  // KBD_FLAGS_RELEASE

    irr::EKEY_CODE keyCode = rdpScancodeToIrrlicht(scancode, extended);
    if (keyCode == irr::KEY_KEY_CODES_COUNT) {
        // Unknown key
        return;
    }

    // Get character for text input
    bool shift = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) ||
                 eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
    bool capsLock = false;  // TODO: track caps lock state
    wchar_t character = rdpScancodeToChar(scancode, shift, capsLock);

    // Create Irrlicht key event
    irr::SEvent event;
    event.EventType = irr::EET_KEY_INPUT_EVENT;
    event.KeyInput.Key = keyCode;
    event.KeyInput.Char = character;
    event.KeyInput.PressedDown = !released;
    event.KeyInput.Shift = shift;
    event.KeyInput.Control = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) ||
                             eventReceiver_->isKeyDown(irr::KEY_RCONTROL);

    // Post event to Irrlicht device
    device_->postEventFromUser(event);
}

void IrrlichtRenderer::handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y) {
    if (!device_ || !eventReceiver_) {
        return;
    }

    // Determine mouse event type
    irr::EMOUSE_INPUT_EVENT eventType = rdpMouseFlagsToIrrlicht(flags);

    // Create Irrlicht mouse event
    irr::SEvent event;
    event.EventType = irr::EET_MOUSE_INPUT_EVENT;
    event.MouseInput.X = x;
    event.MouseInput.Y = y;
    event.MouseInput.Event = eventType;

    // Handle wheel delta
    if (eventType == irr::EMIE_MOUSE_WHEEL) {
        event.MouseInput.Wheel = rdpGetWheelDelta(flags);
    } else {
        event.MouseInput.Wheel = 0.0f;
    }

    // Set button states
    event.MouseInput.ButtonStates = 0;
    if (eventReceiver_->isLeftButtonDown()) {
        event.MouseInput.ButtonStates |= irr::EMBSM_LEFT;
    }
    if (eventReceiver_->isRightButtonDown()) {
        event.MouseInput.ButtonStates |= irr::EMBSM_RIGHT;
    }

    // Post event to Irrlicht device
    device_->postEventFromUser(event);
}

#endif // WITH_RDP

// ============================================================================
// Fire Glow: icosphere mesh + shader + rendering
// ============================================================================

#ifdef EQT_HAS_DRM

// GLES2 spec constants — fallback defines for older headers
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_DEPTH_WRITEMASK
#define GL_DEPTH_WRITEMASK 0x0B72
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif

void IrrlichtRenderer::buildIcosphereMesh() {
    // Build subdivision 2 icosphere (312 verts, 320 tris) on unit sphere
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    float icoV[][3] = {
        {-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
        {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
        {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1},
    };
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(icoV[i][0]*icoV[i][0]+icoV[i][1]*icoV[i][1]+icoV[i][2]*icoV[i][2]);
        icoV[i][0]/=l; icoV[i][1]/=l; icoV[i][2]/=l;
    }
    int icoI[] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    struct Tri { int i0, i1, i2; };
    std::vector<float> pts;
    for (int i = 0; i < 12; i++) { pts.push_back(icoV[i][0]); pts.push_back(icoV[i][1]); pts.push_back(icoV[i][2]); }
    std::vector<Tri> tris;
    for (int i = 0; i < 20; i++) tris.push_back({icoI[i*3], icoI[i*3+1], icoI[i*3+2]});

    // Subdivide 2 times
    for (int s = 0; s < 2; s++) {
        std::vector<Tri> newTris;
        for (const auto& tri : tris) {
            auto mid = [&](int a, int b) -> int {
                int idx = static_cast<int>(pts.size()) / 3;
                float mx=(pts[a*3]+pts[b*3])*0.5f, my=(pts[a*3+1]+pts[b*3+1])*0.5f, mz=(pts[a*3+2]+pts[b*3+2])*0.5f;
                float l=sqrtf(mx*mx+my*my+mz*mz);
                pts.push_back(mx/l); pts.push_back(my/l); pts.push_back(mz/l);
                return idx;
            };
            int m01=mid(tri.i0,tri.i1), m12=mid(tri.i1,tri.i2), m20=mid(tri.i2,tri.i0);
            newTris.push_back({tri.i0,m01,m20}); newTris.push_back({tri.i1,m12,m01});
            newTris.push_back({tri.i2,m20,m12}); newTris.push_back({m01,m12,m20});
        }
        tris = newTris;
    }

    // Store vertices (position only — normal == position for unit sphere)
    icosphereVertices_.clear();
    icosphereVertices_ = std::move(pts);

    icosphereIndices_.clear();
    for (const auto& tri : tris) {
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i0));
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i1));
        icosphereIndices_.push_back(static_cast<uint16_t>(tri.i2));
    }

    // Upload to GPU
    glGenBuffers(1, &icosphereVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, icosphereVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 icosphereVertices_.size() * sizeof(float),
                 icosphereVertices_.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &icosphereIBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icosphereIBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 icosphereIndices_.size() * sizeof(uint16_t),
                 icosphereIndices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    LOG_INFO(MOD_GRAPHICS, "Built icosphere mesh: {} verts, {} tris, VBO={}, IBO={}",
             static_cast<int>(icosphereVertices_.size()) / 3,
             static_cast<int>(icosphereIndices_.size()) / 3,
             icosphereVBO_, icosphereIBO_);
}

void IrrlichtRenderer::compileIcosphereShader() {
    // Vertex shader: scale pulse + vertex jitter, fog distance
    static const char* VS_SRC = R"(
precision highp float;
attribute vec3 aPosition;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uTime;
uniform float uJitterAmount;
uniform vec4 uColor;
uniform float uFogStart;
uniform float uFogEnd;

varying vec4 vColor;
varying float vFogFactor;

void main() {
    // Per-vertex jitter using position components as pseudo-random phase
    float phase = aPosition.x * 7.13 + aPosition.y * 11.37 + aPosition.z * 5.79;
    float jitter = sin(uTime * 4.0 + phase) * uJitterAmount;

    vec3 worldPos = uCenter + aPosition * uRadius * (1.0 + jitter);
    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    gl_Position = clipPos;
    vColor = uColor;

    float fogDist = length(clipPos.xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

    // Fragment shader: trivial output with fog
    static const char* FS_SRC = R"(
precision mediump float;
uniform vec4 uFogColor;
varying vec4 vColor;
varying float vFogFactor;
void main() {
    gl_FragColor = vColor * vFogFactor;
}
)";

    // Compile VS
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &VS_SRC, nullptr);
    glCompileShader(vs);
    GLint ok = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere VS compile error: {}", log);
        glDeleteShader(vs);
        return;
    }

    // Compile FS
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &FS_SRC, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere FS compile error: {}", log);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    // Link
    icosphereProgram_ = glCreateProgram();
    glAttachShader(icosphereProgram_, vs);
    glAttachShader(icosphereProgram_, fs);
    glBindAttribLocation(icosphereProgram_, 0, "aPosition");
    glLinkProgram(icosphereProgram_);
    glGetProgramiv(icosphereProgram_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(icosphereProgram_, sizeof(log), nullptr, log);
        LOG_ERROR(MOD_GRAPHICS, "Icosphere program link error: {}", log);
        glDeleteProgram(icosphereProgram_);
        icosphereProgram_ = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }
    glDetachShader(icosphereProgram_, vs);
    glDetachShader(icosphereProgram_, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Cache uniform locations
    icoLocViewProj_ = glGetUniformLocation(icosphereProgram_, "uViewProj");
    icoLocCenter_ = glGetUniformLocation(icosphereProgram_, "uCenter");
    icoLocRadius_ = glGetUniformLocation(icosphereProgram_, "uRadius");
    icoLocTime_ = glGetUniformLocation(icosphereProgram_, "uTime");
    icoLocJitterAmt_ = glGetUniformLocation(icosphereProgram_, "uJitterAmount");
    icoLocColor_ = glGetUniformLocation(icosphereProgram_, "uColor");
    icoLocFogStart_ = glGetUniformLocation(icosphereProgram_, "uFogStart");
    icoLocFogEnd_ = glGetUniformLocation(icosphereProgram_, "uFogEnd");
    icoLocFogColor_ = glGetUniformLocation(icosphereProgram_, "uFogColor");

    LOG_INFO(MOD_GRAPHICS, "Compiled icosphere shader program={}", icosphereProgram_);
}

void IrrlichtRenderer::updateFireGlowLighting() {
    if (!zoneShader_ || fireGlowLights_.empty()) return;

    // Get camera position in Irrlicht coords (Y-up)
    irr::core::vector3df camPos = camera_ ? camera_->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    // Sort emitters by distance to camera, select nearest N
    int maxN = std::min(maxFireGlowLights_,
                        static_cast<int>(fireGlowLights_.size()));
    maxN = std::min(maxN, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);

    // Build index + distance array for partial sort
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(fireGlowLights_.size());
    for (size_t i = 0; i < fireGlowLights_.size(); ++i) {
        float dx = fireGlowLights_[i].position.X - camPos.X;
        float dy = fireGlowLights_[i].position.Y - camPos.Y;
        float dz = fireGlowLights_[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    zoneShader_->clearFireGlowLights();
    zoneShader_->setNumFireGlowLights(maxN);

    for (int i = 0; i < maxN; ++i) {
        const auto& e = fireGlowLights_[dists[i].index];
        // Fire glow light uniforms are in Irrlicht coords (Y-up) — matches zone VS worldPos
        zoneShader_->setFireGlowLight(i,
            e.position.X, e.position.Y, e.position.Z,
            e.r, e.g, e.b,
            e.radius, 1.0f);
    }
}

void IrrlichtRenderer::renderFireGlowIcospheres() {
    if (icosphereVBO_ == 0 || icosphereProgram_ == 0 || fireGlowLights_.empty()) return;

    // Get camera position for nearest-N selection (reuse fire glow light selection)
    irr::core::vector3df camPos = camera_ ? camera_->getAbsolutePosition()
                                          : irr::core::vector3df(0, 0, 0);

    int maxN = std::min(maxFireGlowLights_,
                        static_cast<int>(fireGlowLights_.size()));

    // Build distance-sorted indices
    struct EmitterDist { int index; float distSq; };
    std::vector<EmitterDist> dists(fireGlowLights_.size());
    for (size_t i = 0; i < fireGlowLights_.size(); ++i) {
        float dx = fireGlowLights_[i].position.X - camPos.X;
        float dy = fireGlowLights_[i].position.Y - camPos.Y;
        float dz = fireGlowLights_[i].position.Z - camPos.Z;
        dists[i] = { static_cast<int>(i), dx*dx + dy*dy + dz*dz };
    }
    std::partial_sort(dists.begin(), dists.begin() + maxN, dists.end(),
                      [](const EmitterDist& a, const EmitterDist& b) { return a.distSq < b.distSq; });

    // Build ViewProj matrix from captured 3D transforms
    irr::core::matrix4 viewProj = captured3DProj_ * captured3DView_;
    float vpMat[16];
    for (int i = 0; i < 16; i++) vpMat[i] = viewProj[i];

    // Save GL state
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    GLboolean prevDepthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthWrite);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    // Set render state: additive blend, depth test ON, depth write OFF, no cull
    glUseProgram(icosphereProgram_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // Set shared uniforms
    glUniformMatrix4fv(icoLocViewProj_, 1, GL_FALSE, vpMat);
    glUniform1f(icoLocTime_, fireGlowTime_);
    glUniform1f(icoLocJitterAmt_, 0.08f);

    float fogStart = zoneShader_ ? zoneShader_->fogStart() : 999999.0f;
    float fogEnd = zoneShader_ ? zoneShader_->fogEnd() : 999999.0f;
    const float* fogCol = zoneShader_ ? zoneShader_->fogColor() : nullptr;
    glUniform1f(icoLocFogStart_, fogStart);
    glUniform1f(icoLocFogEnd_, fogEnd);
    if (fogCol) glUniform4f(icoLocFogColor_, fogCol[0], fogCol[1], fogCol[2], fogCol[3]);
    else glUniform4f(icoLocFogColor_, 0.0f, 0.0f, 0.0f, 0.0f);

    // Bind icosphere VBO/IBO
    glBindBuffer(GL_ARRAY_BUFFER, icosphereVBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icosphereIBO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    // Disable other attribs that zone shaders may have left enabled
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    int indexCount = static_cast<int>(icosphereIndices_.size());

    // xorshift RNG for per-emitter color flicker (deterministic per frame from time)
    uint32_t flickerRng = static_cast<uint32_t>(fireGlowTime_ * 1000.0f) ^ 0xDEADBEEF;

    for (int i = 0; i < maxN; ++i) {
        const auto& e = fireGlowLights_[dists[i].index];

        // Color flicker: random intensity variation per light per frame
        flickerRng ^= flickerRng << 13; flickerRng ^= flickerRng >> 17; flickerRng ^= flickerRng << 5;
        float flickerIntensity = 0.6f + 0.4f * static_cast<float>(flickerRng & 0xFFFF) / 65535.0f;

        float alpha = 0.25f * flickerIntensity;  // subtle additive glow

        glUniform3f(icoLocCenter_, e.position.X, e.position.Y, e.position.Z);
        glUniform1f(icoLocRadius_, e.radius);
        glUniform4f(icoLocColor_, e.r * alpha, e.g * alpha, e.b * alpha, alpha);

        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    }

    // Restore GL state
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDepthMask(prevDepthWrite);
    if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);
    glUseProgram(prevProgram);
}

#endif // EQT_HAS_DRM

} // namespace Graphics
} // namespace EQT
