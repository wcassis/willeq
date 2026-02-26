#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_prep_worker.h"
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
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "common/logging.h"
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
static eqt::ui::DisplaySettings loadDisplaySettingsFromFile() {
    eqt::ui::DisplaySettings settings;  // defaults: everything enabled

    for (const auto& path : {"config/display_settings.json", "../config/display_settings.json"}) {
        std::ifstream file(path);
        if (!file.is_open()) continue;

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, file, &root, &errors)) break;

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
        break;
    }

    return settings;
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

bool IrrlichtRenderer::init(const RendererConfig& config) {
    config_ = config;

    // Apply constrained rendering configuration
    // Constrained mode is always active; Max preset has no practical limits
    // Populate config from preset (unless custom NxNxN config was already set)
    if (config_.constrainedPreset != ConstrainedRenderingPreset::Custom) {
        config_.constrainedConfig = ConstrainedRendererConfig::fromPreset(config_.constrainedPreset);
    }
    config_.constrainedConfig.calculateMaxResolution();
    config_.constrainedConfig.calculateMemoryLimits();
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
    {
        float targetFps = config_.constrainedConfig.targetFps;
        governor_ = std::make_unique<FrameBudgetGovernor>(targetFps);
        LOG_INFO(MOD_GRAPHICS, "Frame budget governor: target {:.0f} FPS ({:.1f}ms budget)",
                 targetFps, governor_->getTargetFrameTimeMs());
    }

    // Choose driver type
    irr::video::E_DRIVER_TYPE driverType;
    bool useGLES2 = config.useGLES2 || config_.constrainedConfig.useGLES2;
    if (config.softwareRenderer) {
        driverType = irr::video::EDT_BURNINGSVIDEO;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Driver selection: Burnings Software (--opengl not set)");
    }
#ifdef EQT_HAS_GLES2
    else if (useGLES2) {
        driverType = irr::video::EDT_OGLES2;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Driver selection: OpenGL ES 2.0 (GLES2 mode)");
    }
#endif
    else {
        driverType = irr::video::EDT_OPENGL;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Driver selection: OpenGL (--opengl flag set)");
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
    if (config.useDRM) {
        params.DeviceType = irr::EIDT_FRAMEBUFFER;
        LOG_INFO(MOD_GRAPHICS, "[GL] Using DRM/KMS framebuffer device (no X11)");
    }
#endif

    LOG_DEBUG(MOD_GRAPHICS, "[GL] Creating Irrlicht device: {}x{}, fullscreen={}, vsync={}, stencil={}, AA={}",
              config.width, config.height, config.fullscreen, true,
              config_.constrainedConfig.enableStencilBuffer,
              config_.constrainedConfig.antiAliasLevel);
    if (!config.useDRM) {
        LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY={}", std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "(not set)");
    }

    device_ = irr::createDeviceEx(params);

    if (!device_) {
        LOG_WARN(MOD_GRAPHICS, "[GL] Failed to create device with {} driver, falling back to software",
                 config.softwareRenderer ? "Burnings" : "OpenGL");
        // Fall back to basic software renderer
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
    if (config_.useDRM && device_->getCursorControl()) {
        device_->getCursorControl()->setVisible(true);
    }
    createSoftwareCursor();

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
        LOG_INFO(MOD_GRAPHICS, "Texture cache created ({}KB limit, {}x{} max texture, mipmaps={})",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.enableMipmaps ? "yes" : "no");
    }

    // Create event receiver
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

    // Setup camera
    setupCamera();

    // Setup lighting
    setupLighting();

    // Initialize GLSL shader pipeline (if enabled and driver supports it)
    if (config_.constrainedConfig.enableShaders && !config_.softwareRenderer) {
        auto* gpu = driver_->getGPUProgrammingServices();
        if (gpu) {
            zoneShader_ = std::make_unique<ZoneShaderManager>(driver_, gpu);
            if (!zoneShader_->isAvailable()) {
                LOG_WARN(MOD_GRAPHICS, "GLSL shaders requested but compilation failed; using fixed-function fallback");
                zoneShader_.reset();
            }
        }
    }

    // Setup HUD
    setupHUD();

    // Create entity renderer
    entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
    entityRenderer_->setClientPath(config.eqClientPath);
    entityRenderer_->setNameTagsVisible(config.showNameTags);
    entityRenderer_->setRenderDistance(renderDistance_);

    // Pass constrained config to entity renderer for visibility limits
    entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
    // Pass GLSL shader material types if available
    if (zoneShader_ && zoneShader_->isAvailable()) {
        entityRenderer_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                                zoneShader_->getMaterialTypeAlphaTest());
    }
    // Apply chr cache limit to race model loader
    if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
    }

    // Set ground finder callback for NPC terrain snapping during interpolation
    entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
        return this->findGroundZ(x, y, currentZ);
    });

    // Preload numbered global character models for better coverage
    entityRenderer_->loadNumberedGlobals();

    // Load equipment models from gequip.s3d archives
    if (entityRenderer_->loadEquipmentModels()) {
        LOG_INFO(MOD_GRAPHICS, "Equipment models loaded");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Could not load equipment models");
    }

    // Create door manager
    doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);

    // Create tree wind animation manager
    treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
    treeManager_->setRenderDistance(renderDistance_);
    if (zoneShader_ && zoneShader_->isAvailable()) {
        treeManager_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                             zoneShader_->getMaterialTypeAlphaTest());
    }

    // Create weather system
    weatherSystem_ = std::make_unique<WeatherSystem>();
    // Connect weather system to tree manager via callback
    weatherSystem_->addCallback([this](WeatherType weather) {
        if (treeManager_) {
            treeManager_->setWeather(weather);
        }
    });

    // Environmental managers (particleManager_, boidsManager_, tumbleweedManager_,
    // weatherEffects_, detailManager_) are deferred to initializeForZone() / loadGlobalAssets()
    // so that display_settings.json can control whether they are created at all.
    // This prevents crash-causing systems (e.g. boids on ARM) from initializing
    // when the user has disabled them in settings.

    // Apply initial settings
    wireframeMode_ = config.wireframe;
    fogEnabled_ = config.fog;
    lightingEnabled_ = config.lighting;

    initialized_ = true;
    lastFpsTime_ = device_->getTimer()->getTime();

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer initialized: {}x{}", config.width, config.height);
    return true;
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
    // Create frame budget governor (if not already created by init())
    if (!governor_) {
        float targetFps = config_.constrainedConfig.targetFps;
        governor_ = std::make_unique<FrameBudgetGovernor>(targetFps);
        LOG_INFO(MOD_GRAPHICS, "Frame budget governor: target {:.0f} FPS ({:.1f}ms budget)",
                 targetFps, governor_->getTargetFrameTimeMs());
    }

    // Choose driver type
    irr::video::E_DRIVER_TYPE driverType;
    bool loadingUseGLES2 = config.useGLES2 || config_.constrainedConfig.useGLES2;
    if (config.softwareRenderer) {
        driverType = irr::video::EDT_BURNINGSVIDEO;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: Burnings Software");
    }
#ifdef EQT_HAS_GLES2
    else if (loadingUseGLES2) {
        driverType = irr::video::EDT_OGLES2;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL ES 2.0");
    }
#endif
    else {
        driverType = irr::video::EDT_OPENGL;
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL");
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
    if (config.useDRM) {
        params.DeviceType = irr::EIDT_FRAMEBUFFER;
        LOG_INFO(MOD_GRAPHICS, "[GL] Loading screen: using DRM/KMS framebuffer device");
    }
#endif

    LOG_DEBUG(MOD_GRAPHICS, "[GL] Creating Irrlicht device: {}x{}, fullscreen={}, vsync={}, stencil={}, AA={}",
              config.width, config.height, config.fullscreen, true,
              config_.constrainedConfig.enableStencilBuffer,
              config_.constrainedConfig.antiAliasLevel);
    if (!config.useDRM) {
        LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY={}", std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "(not set)");
    }

    device_ = irr::createDeviceEx(params);

    if (!device_) {
        LOG_WARN(MOD_GRAPHICS, "[GL] Failed to create device with {} driver, falling back to software",
                 config.softwareRenderer ? "Burnings" : "OpenGL");
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
    if (config_.useDRM && device_->getCursorControl()) {
        device_->getCursorControl()->setVisible(true);
    }
    createSoftwareCursor();

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

    // Initialize GLSL shader pipeline (if enabled and driver supports it)
    if (config_.constrainedConfig.enableShaders && !config_.softwareRenderer) {
        auto* gpu = driver_->getGPUProgrammingServices();
        if (gpu) {
            zoneShader_ = std::make_unique<ZoneShaderManager>(driver_, gpu);
            if (!zoneShader_->isAvailable()) {
                LOG_WARN(MOD_GRAPHICS, "GLSL shaders requested but compilation failed; using fixed-function fallback");
                zoneShader_.reset();
            }
        }
    }

    // Setup HUD (needed for loading screen text)
    setupHUD();

    // Apply initial settings
    wireframeMode_ = config.wireframe;
    fogEnabled_ = config.fog;
    lightingEnabled_ = config.lighting;

    // NOTE: We do NOT create entity renderer or load models here.
    // That happens in loadGlobalAssets() which is called during graphics loading phase.

    // Create tree wind animation manager (needed before zone loading)
    if (!treeManager_) {
        treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
        treeManager_->setRenderDistance(renderDistance_);
        if (zoneShader_ && zoneShader_->isAvailable()) {
            treeManager_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                                 zoneShader_->getMaterialTypeAlphaTest());
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
    // Use OptionsWindow settings if available, otherwise read from JSON file directly.
    eqt::ui::DisplaySettings displaySettings;
    if (windowManager_ && windowManager_->getOptionsWindow()) {
        displaySettings = windowManager_->getOptionsWindow()->getDisplaySettings();
    } else {
        displaySettings = loadDisplaySettingsFromFile();
    }

    // Always create particle manager — needed for unified fire system even when
    // atmospheric particles (dust, pollen, fireflies) are disabled
    if (!particleManager_) {
        particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
        if (!particleManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize particle manager");
        }
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
        // Connect weather system to weather effects
        if (weatherSystem_) {
            weatherSystem_->addListener(weatherEffects_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects initialized");
    }

    initialized_ = true;
    loadingScreenVisible_ = true;  // Show loading screen by default
    globalAssetsLoaded_ = false;
    lastFpsTime_ = device_->getTimer()->getTime();

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer loading screen initialized: {}x{}", config.width, config.height);
    return true;
}

bool IrrlichtRenderer::loadGlobalAssets() {
    if (!initialized_) {
        LOG_ERROR(MOD_GRAPHICS, "Cannot load global assets - renderer not initialized");
        return false;
    }

    if (globalAssetsLoaded_) {
        LOG_DEBUG(MOD_GRAPHICS, "Global assets already loaded, skipping");
        return true;
    }

    LOG_INFO(MOD_GRAPHICS, "Loading global assets (character models, equipment)...");

    // Create entity renderer (if not already created by init())
    if (!entityRenderer_) {
        entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
        entityRenderer_->setClientPath(config_.eqClientPath);
        entityRenderer_->setNameTagsVisible(config_.showNameTags);
        entityRenderer_->setRenderDistance(renderDistance_);
        // Pass constrained config to entity renderer for visibility limits
        entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
        // Pass GLSL shader material types if available
        if (zoneShader_ && zoneShader_->isAvailable()) {
            entityRenderer_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                                    zoneShader_->getMaterialTypeAlphaTest());
        }
        if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
        }
        // Set ground finder callback for NPC terrain snapping during interpolation
        entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
            return this->findGroundZ(x, y, currentZ);
        });
        // If zone was already loaded before entity renderer was created,
        // pass BSP tree, frustum culler, and occlusion culler now
        if (zoneBspTree_) {
            entityRenderer_->setBspTree(zoneBspTree_);
        }
        if (frustumCuller_) {
            entityRenderer_->setFrustumCuller(frustumCuller_.get());
        }
        // Note: entity occlusion culling is now handled by SimulationWorker
    }

    if (config_.constrainedConfig.deferredAssetLoading) {
        // Deferred mode: build lightweight archive index instead of loading all models
        graphicsArchiveIndex_ = std::make_unique<GraphicsArchiveIndex>();
        bool lazyMode = config_.constrainedConfig.lazyPfsLoading;
        if (graphicsArchiveIndex_->buildIndex(config_.eqClientPath, lazyMode, networkTickCallback_)) {
            LOG_INFO(MOD_GRAPHICS, "Graphics archive index built: {} race entries from {} archives",
                     graphicsArchiveIndex_->getRaceEntryCount(), graphicsArchiveIndex_->getArchiveCount());
            // Pass archive index to RaceModelLoader for on-demand loading
            if (entityRenderer_->getRaceModelLoader()) {
                entityRenderer_->getRaceModelLoader()->setGraphicsArchiveIndex(graphicsArchiveIndex_.get());
            }
        } else {
            LOG_WARN(MOD_GRAPHICS, "Graphics archive index build failed, falling back to eager loading");
            // Fall back to eager loading
            entityRenderer_->loadGlobalCharacters();
            if (networkTickCallback_) networkTickCallback_();
            entityRenderer_->loadNumberedGlobals();
        }
    } else {
        // Eager mode: load all global character models into memory
        if (entityRenderer_->loadGlobalCharacters()) {
            LOG_DEBUG(MOD_GRAPHICS, "Global character models loaded");
        } else {
            LOG_WARN(MOD_GRAPHICS, "Could not load global character models (will use placeholders)");
        }

        // Pump network after global character model load
        if (networkTickCallback_) networkTickCallback_();

        // Preload numbered global character models for better coverage (global2-7_chr.s3d)
        entityRenderer_->loadNumberedGlobals();
    }

    // Pump network after character model loading
    if (networkTickCallback_) networkTickCallback_();

    // Load equipment models from gequip.s3d archives
    if (entityRenderer_->loadEquipmentModels()) {
        LOG_INFO(MOD_GRAPHICS, "Equipment models loaded");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Could not load equipment models");
    }

    // Pump network after equipment model load
    if (networkTickCallback_) networkTickCallback_();

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
    }

    // Create sky renderer (if not already created)
    if (!skyRenderer_) {
        skyRenderer_ = std::make_unique<SkyRenderer>(smgr_, driver_, device_->getFileSystem());
        if (!skyRenderer_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization failed - sky will not be rendered");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized");
        }
    }

    // Create detail manager (grass, plants, debris) - only if enabled in settings
    if (!detailManager_) {
        eqt::ui::DisplaySettings detailSettings;
        if (windowManager_ && windowManager_->getOptionsWindow()) {
            detailSettings = windowManager_->getOptionsWindow()->getDisplaySettings();
        } else {
            detailSettings = loadDisplaySettingsFromFile();
        }

        if (detailSettings.detailObjectsEnabled) {
            detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
            detailManager_->setSurfaceMapsPath("data/detail/zones");
            LOG_INFO(MOD_GRAPHICS, "Detail manager initialized (detail objects enabled in settings)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Detail manager skipped (detail objects disabled in settings)");
        }
    }

    // Initialize inventory window model view now that entity renderer is available
    // This must happen after entityRenderer_ is created since it needs the race model loader
    if (windowManager_ && entityRenderer_) {
        windowManager_->initModelView(smgr_,
                                      entityRenderer_->getRaceModelLoader(),
                                      entityRenderer_->getEquipmentModelLoader());
    }

    globalAssetsLoaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "Global assets loaded successfully");
    return true;
}

void IrrlichtRenderer::showLoadingScreen() {
    loadingScreenVisible_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen shown");
}

void IrrlichtRenderer::hideLoadingScreen() {
    loadingScreenVisible_ = false;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen hidden");

    // Force full visibility and lighting recalculation on the first gameplay frame.
    // During loading, Tier2 updates (PVS, object culling, lights) run and cache
    // camera positions. By the time loading completes, these caches are populated
    // at the camera's loading-phase position. Without resetting here, the movement
    // gates in updateObjectLights() and updateZoneLightVisibility() block
    // recalculation until the player moves 5+ units — leaving lights disabled
    // and PVS stale on initial zone-in.
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
    forcePvsUpdate_ = true;

    // Request deferred governor reset so the 11+ second loading screen frame
    // doesn't poison the rolling average. requestReset() defers the actual reset
    // to the next beginFrame(), discarding this poisoned frame entirely.
    if (governor_) {
        governor_->requestReset();
        LOG_DEBUG(MOD_GRAPHICS, "Governor reset requested — will start GREEN next frame");
    }

    // Release duplicate character data from zone source. RaceModelLoader independently
    // loads _chr.s3d into its own cache, so currentZone_->characters is an unused copy.
    if (currentZone_) {
        currentZone_->clearCharacterData();
    }
}

void IrrlichtRenderer::shutdown() {
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
    globalAssetsLoaded_ = false;

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
    if (!driver_ || !config_.useDRM) return;

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
            updateZoneLightColors();
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

// Debug version that logs why a region is considered visible/hidden
bool IrrlichtRenderer::isRegionPvsVisibleDebug(size_t regionIdx, const char* context, int id) const {
    if (!usePvsCulling_) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (usePvsCulling_=false)",
                  context, id, regionIdx);
        return true;
    }
    if (regionIdx == SIZE_MAX) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region=SIZE_MAX: VISIBLE (no region assigned)",
                  context, id);
        return true;
    }
    if (currentPvsRegion_ == SIZE_MAX) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (currentPvsRegion_=SIZE_MAX)",
                  context, id, regionIdx);
        return true;
    }
    if (!zoneBspTree_) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (no BSP tree)",
                  context, id, regionIdx);
        return true;
    }
    if (currentPvsRegion_ >= zoneBspTree_->regions.size()) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (camRegion {} >= tree size {})",
                  context, id, regionIdx, currentPvsRegion_, zoneBspTree_->regions.size());
        return true;
    }
    auto& camRegion = zoneBspTree_->regions[currentPvsRegion_];
    if (!camRegion) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (camRegion {} is null)",
                  context, id, regionIdx, currentPvsRegion_);
        return true;
    }
    if (camRegion->visibleRegions.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (camRegion {} visibleRegions empty)",
                  context, id, regionIdx, currentPvsRegion_);
        return true;
    }
    if (regionIdx >= camRegion->visibleRegions.size()) {
        LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: VISIBLE (region >= bitvector size {})",
                  context, id, regionIdx, camRegion->visibleRegions.size());
        return true;
    }
    bool visible = camRegion->visibleRegions[regionIdx];
    LOG_DEBUG(MOD_GRAPHICS, "PVS-DBG [{}#{}] region={}: {} (bitvector[{}]={}; camRegion={}, bitvecSize={})",
              context, id, regionIdx, visible ? "VISIBLE" : "HIDDEN",
              regionIdx, visible ? 1 : 0, currentPvsRegion_, camRegion->visibleRegions.size());
    return visible;
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
        LOG_INFO(MOD_GRAPHICS, "Detail manager created (toggled on via settings)");

        // If zone is loaded, schedule deferred init
        if (zoneLoaded && terrainOnlySelector_) {
            environmentInitPending_ = true;
        }
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
    // Wait for background zone load thread if still running
    if (zoneLoadThread_ && zoneLoadThread_->joinable()) {
        zoneLoadThread_->join();
    }
    zoneLoadThread_.reset();
    pendingZoneData_.reset();
    zoneLoadComplete_ = false;
    backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Idle;
    atlasZonePageIndex_ = 0;
    atlasObjPageIndex_ = 0;
    skyTexUploadIndex_ = 0;
    doorRebuildIndex_ = 0;
    doorRebuildList_.clear();
    regionBuildIndex_ = 0;
    regionBuildInitDone_ = false;
    regionMeshCacheInstallStarted_ = false;
    storedZoneEnvironment_.pending = false;

    // Wait for background BSP preload thread if still running
    if (bspPreloadThread_) {
        if (bspPreloadThread_->joinable()) bspPreloadThread_->join();
        bspPreloadThread_.reset();
        pendingBspResult_.reset();
        bspPreloadComplete_ = false;
    }

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
    environmentInitPending_ = false;
    deferredInitActive_ = false;
    entityPrepReady_ = false;

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

    // Clear camera collision selector FIRST to prevent use-after-free during zone transitions
    // This must happen BEFORE dropping zoneTriangleSelector_ to avoid race conditions
    if (cameraController_) {
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

    // Now safe to remove zone collision selectors
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
    portalCacheDirty_ = true;
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

    // Remove zone light nodes
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (zoneLightNodes_[i]) {
            if (i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i]) {
                zoneLightNodes_[i]->remove();
            }
            zoneLightNodes_[i]->drop();  // Release our reference
        }
    }
    zoneLightNodes_.clear();
    zoneLightPositions_.clear();
    zoneLightInSceneGraph_.clear();
    zoneLightNames_.clear();
    zoneLightAnimElapsed_.clear();
    zoneLightAnimFrame_.clear();

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
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            objLight.node->remove();
        }
    }
    objectLights_.clear();

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

        // Check user setting
        bool skySettingEnabled = true;
        if (windowManager_ && windowManager_->getOptionsWindow()) {
            skySettingEnabled = windowManager_->getOptionsWindow()->getDisplaySettings().skyEnabled;
        } else {
            skySettingEnabled = loadDisplaySettingsFromFile().skyEnabled;
        }
        skyRenderer_->setEnabled(!isDungeon && skySettingEnabled);

        LOG_DEBUG(MOD_GRAPHICS, "Zone environment: sky type {}, zone type {} ({}), sky {} (setting={})",
                  skyType, zoneType, isDungeon ? "dungeon" : "outdoor",
                  (!isDungeon && skySettingEnabled) ? "enabled" : "disabled",
                  skySettingEnabled ? "on" : "off");
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
    if (skyRenderer_) {
        bool newState = !skyRenderer_->isEnabled();
        skyRenderer_->setEnabled(newState);
        LOG_INFO(MOD_GRAPHICS, "Sky rendering: {}", newState ? "ON" : "OFF");
    }
}

void IrrlichtRenderer::forceSkyType(uint8_t skyTypeId) {
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

void IrrlichtRenderer::setupInstantScene(const std::string& zoneName, float playerX, float playerY, float playerZ) {
    currentZoneName_ = zoneName;

    // Create entity renderer for placeholder cubes (no model loading yet)
    if (!entityRenderer_ && smgr_ && driver_ && device_) {
        entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
        entityRenderer_->setClientPath(config_.eqClientPath);
        entityRenderer_->setNameTagsVisible(config_.showNameTags);
        entityRenderer_->setRenderDistance(renderDistance_);
        entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
        if (zoneShader_ && zoneShader_->isAvailable()) {
            entityRenderer_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                                    zoneShader_->getMaterialTypeAlphaTest());
        }
        if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
        }
        entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
            return this->findGroundZ(x, y, currentZ);
        });
        // Pass frustum culler for entity visibility culling during placeholder mode
        if (frustumCuller_) {
            entityRenderer_->setFrustumCuller(frustumCuller_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Entity renderer created for instant scene (placeholder cubes only)");
    }

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

    // Start background BSP preload for entity PVS culling during placeholder mode
    if (!config_.eqClientPath.empty()) {
        startBspPreload(zoneName, config_.eqClientPath);
    }

    // Set zone ready flags — but do NOT activate progressive loading
    // Loading is deferred until /loadzone command
    zoneReady_ = true;

    LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — HCMap placeholder + collision, awaiting /loadzone",
             zoneName);
}

void IrrlichtRenderer::beginZoneAssetLoad(const std::string& eqClientPath) {
    if (backgroundZoneLoadPhase_ != BackgroundZoneLoadPhase::Idle) {
        LOG_WARN(MOD_GRAPHICS, "Zone asset load already in progress (phase {})",
                 static_cast<int>(backgroundZoneLoadPhase_));
        return;
    }

    // Stop simulation worker during zone transition
    stopSimulationWorker();

    // Join BSP preload thread if still running (full S3D load will rebuild BSP data)
    if (bspPreloadThread_) {
        if (bspPreloadThread_->joinable()) bspPreloadThread_->join();
        bspPreloadThread_.reset();
        pendingBspResult_.reset();
        bspPreloadComplete_ = false;
    }

    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();
    skyTexUploadIndex_ = 0;
    doorRebuildIndex_ = 0;
    doorRebuildList_.clear();
    regionBuildIndex_ = 0;
    regionBuildInitDone_ = false;
    regionMeshCacheInstallStarted_ = false;
    entityPrepReady_ = false;

    // Create background entity prep worker early so it's ready when archives load.
    if (!entityPrepWorker_ && entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
        entityPrepWorker_ = std::make_unique<EntityPrepWorker>(
            entityRenderer_->getRaceModelLoader(),
            entityRenderer_->getEquipmentModelLoader());
        entityPrepWorker_->start();
        entityRenderer_->setEntityPrepWorker(entityPrepWorker_.get());
        LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker started early for progressive entity loading");
    }

    // Start background icon sheet worker (disk I/O + TGA decode off main thread)
    if (windowManager_) {
        windowManager_->getIconLoader().startWorker();
    }

    // Start the background S3D load thread
    startBackgroundZoneLoad(currentZoneName_, eqClientPath);

    LOG_INFO(MOD_GRAPHICS, "Zone asset load initiated for '{}'", currentZoneName_);
}

void IrrlichtRenderer::startBspPreload(const std::string& zoneName, const std::string& eqClientPath) {
    // Don't start if already running or if full zone load is in progress
    if (bspPreloadThread_ || backgroundZoneLoadPhase_ != BackgroundZoneLoadPhase::Idle) return;

    bspPreloadComplete_ = false;
    pendingBspResult_ = std::make_unique<BspPreloadResult>();

    std::string zonePath = eqClientPath;
    if (!zonePath.empty() && zonePath.back() != '/' && zonePath.back() != '\\')
        zonePath += '/';
    zonePath += zoneName + ".s3d";

    // Capture indoor flag for portal eligibility check (set by setZoneEnvironment before this call)
    bool indoor = isIndoorZone_;

    auto* result = pendingBspResult_.get();
    bspPreloadThread_ = std::make_unique<std::thread>([this, zonePath, indoor, result]() {
        // Set thread to lowest priority so we never steal CPU from render thread
#ifdef __linux__
        struct sched_param param = {};
        if (pthread_setschedparam(pthread_self(), SCHED_IDLE, &param) != 0) {
            nice(19);
        }
#endif

        // 1. Open the S3D archive
        PfsArchive archive;
        if (!archive.open(zonePath)) {
            LOG_ERROR(MOD_GRAPHICS, "BSP preload: failed to open archive: {}", zonePath);
            bspPreloadComplete_ = true;
            return;
        }

        // 2. Find main WLD (same filter as S3DLoader::loadZone)
        std::vector<std::string> wldFiles;
        archive.getFilenames(".wld", wldFiles);
        if (wldFiles.empty()) {
            LOG_ERROR(MOD_GRAPHICS, "BSP preload: no WLD file in archive");
            bspPreloadComplete_ = true;
            return;
        }

        std::string mainWld;
        for (const auto& wld : wldFiles) {
            if (wld.find("_obj") == std::string::npos &&
                wld.find("_chr") == std::string::npos &&
                wld != "objects.wld" &&
                wld != "lights.wld") {
                mainWld = wld;
                break;
            }
        }
        if (mainWld.empty()) mainWld = wldFiles[0];

        // 3. Parse WLD
        auto wldLoader = std::make_shared<WldLoader>();
        if (!wldLoader->parseFromArchive(zonePath, mainWld)) {
            LOG_ERROR(MOD_GRAPHICS, "BSP preload: failed to parse WLD: {}", mainWld);
            bspPreloadComplete_ = true;
            return;
        }

        // 4. Get BSP tree and verify PVS data
        auto bspTree = wldLoader->getBspTree();
        if (!bspTree || !wldLoader->hasPvsData()) {
            LOG_INFO(MOD_GRAPHICS, "BSP preload: zone has no PVS data, skipping");
            bspPreloadComplete_ = true;
            return;
        }

        // 5. Compute region bounding boxes from vertex data
        std::map<size_t, irr::core::aabbox3df> regionBoundingBoxes;
        for (size_t regionIdx = 0; regionIdx < bspTree->regions.size(); ++regionIdx) {
            auto geom = wldLoader->getGeometryForRegion(regionIdx);
            if (!geom || geom->vertices.empty()) continue;

            float vMinX = std::numeric_limits<float>::max();
            float vMinY = vMinX, vMinZ = vMinX;
            float vMaxX = std::numeric_limits<float>::lowest();
            float vMaxY = vMaxX, vMaxZ = vMaxX;
            for (const auto& v : geom->vertices) {
                float wx = geom->centerX + v.x;
                float wy = geom->centerY + v.y;
                float wz = geom->centerZ + v.z;
                if (wx < vMinX) vMinX = wx;
                if (wy < vMinY) vMinY = wy;
                if (wz < vMinZ) vMinZ = wz;
                if (wx > vMaxX) vMaxX = wx;
                if (wy > vMaxY) vMaxY = wy;
                if (wz > vMaxZ) vMaxZ = wz;
            }
            irr::core::aabbox3df worldBounds;
            worldBounds.MinEdge.X = vMinX;
            worldBounds.MinEdge.Y = vMinY;
            worldBounds.MinEdge.Z = vMinZ;
            worldBounds.MaxEdge.X = vMaxX;
            worldBounds.MaxEdge.Y = vMaxY;
            worldBounds.MaxEdge.Z = vMaxZ;
            regionBoundingBoxes[regionIdx] = worldBounds;
        }

        // 6. Build portal system
        auto portalSystem = std::make_unique<PortalSystem>();
        portalSystem->buildFromBsp(*bspTree, regionBoundingBoxes);
        bool portalEligible = portalSystem->hasPortals() &&
                              (portalSystem->getData().portals.size() > 10);

        // 7. Store results
        result->bspTree = bspTree;
        result->regionBoundingBoxes = std::move(regionBoundingBoxes);
        result->portalSystem = std::move(portalSystem);
        result->portalOcclusionEligible = portalEligible;

        LOG_INFO(MOD_GRAPHICS, "BSP preload complete: {} regions, {} portals, PVS eligible={}",
                 bspTree->regions.size(), result->portalSystem->getData().portals.size(),
                 portalEligible ? "yes" : "no");

        // Release heavy WLD/archive data BEFORE signaling completion.
        // BSP tree survives via shared_ptr in result->bspTree.
        // Without this, the destructors run after bspPreloadComplete_ is set,
        // and join() blocks ~150ms on ARM freeing 2000+ regions of parsed geometry.
        wldLoader.reset();
        archive.close();

        bspPreloadComplete_ = true;
    });

    LOG_INFO(MOD_GRAPHICS, "BSP preload started for zone '{}'", zoneName);
}

void IrrlichtRenderer::advanceBspPreload() {
    if (!bspPreloadThread_) return;

    // Join the thread
    if (bspPreloadThread_->joinable())
        bspPreloadThread_->join();
    bspPreloadThread_.reset();

    if (!pendingBspResult_ || !pendingBspResult_->bspTree) {
        // Preload produced no usable data
        pendingBspResult_.reset();
        bspPreloadComplete_ = false;
        return;
    }

    auto& result = *pendingBspResult_;

    // Install BSP tree
    zoneBspTree_ = result.bspTree;
    usePvsCulling_ = true;
    currentPvsRegion_ = SIZE_MAX;

    // Install region bounding boxes
    regionBoundingBoxes_ = std::move(result.regionBoundingBoxes);

    // Wire BSP tree to entity renderer and door manager
    if (entityRenderer_) {
        entityRenderer_->setBspTree(zoneBspTree_);
    }
    if (doorManager_) {
        doorManager_->setBspTree(zoneBspTree_.get());
        doorManager_->setPvsRegion(currentPvsRegion_);
    }

    // Install portal system
    portalSystem_ = std::move(result.portalSystem);
    portalOcclusionEligible_ = result.portalOcclusionEligible;

    // Build region neighbor map for door PVS culling (1-depth expansion)
    buildRegionNeighborMap();
    if (doorManager_) {
        doorManager_->setRegionNeighbors(
            regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
        // Retroactively compute BSP regions for doors registered before BSP arrived.
        // Doors start OUT of scene graph when no BSP; this adds visible ones.
        doorManager_->recomputeAllBspRegions();
    }

    // Signal PVS recalculation on next frame.
    // Do NOT reset lastLightPlayerPos_ — that would force full recalculation.
    forcePvsUpdate_ = true;

    LOG_INFO(MOD_GRAPHICS, "BSP preload installed: {} regions, PVS culling active",
             zoneBspTree_->regions.size());

    // Rebuild placeholder mesh with PVS filtering now that BSP data is available
    if (zonePlaceholderNode_ && collisionMap_ && collisionMap_->IsLoaded()) {
        buildZonePlaceholder(playerX_, playerZ_, playerY_);
        setupHCMapCollision();
    }

    pendingBspResult_.reset();
    bspPreloadComplete_ = false;
}

void IrrlichtRenderer::setupHCMapCollision() {
    if (!smgr_) return;

    // Clean up old selectors
    if (zoneTriangleSelector_) { zoneTriangleSelector_->drop(); zoneTriangleSelector_ = nullptr; }
    if (terrainOnlySelector_) { terrainOnlySelector_->drop(); terrainOnlySelector_ = nullptr; }

    irr::scene::IMetaTriangleSelector* metaSelector = smgr_->createMetaTriangleSelector();
    if (!metaSelector) return;

    // Use HCMap placeholder mesh for collision (already built by buildZonePlaceholder)
    if (zonePlaceholderNode_ && zonePlaceholderNode_->getMesh()) {
        auto* sel = smgr_->createTriangleSelector(zonePlaceholderNode_->getMesh(), zonePlaceholderNode_);
        if (sel) { metaSelector->addTriangleSelector(sel); sel->drop(); }
    }

    zoneTriangleSelector_ = metaSelector;

    // Create terrain-only selector with same data
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

// ── BMP decode + bilinear upscale (background thread, no GL) ─────────────────

// Decode BMP to A8R8G8B8 (Irrlicht's BGRA byte order: B,G,R,A in memory).
// Supports 8-bit indexed, 24-bit, and 32-bit uncompressed BMPs.
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
    // A8R8G8B8: 4 bytes per pixel (B, G, R, A in memory on little-endian)
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
                // A8R8G8B8 in memory (little-endian uint32): B, G, R, A
                outPixels[outIdx + 0] = palette[idx * 4 + 0]; // B
                outPixels[outIdx + 1] = palette[idx * 4 + 1]; // G
                outPixels[outIdx + 2] = palette[idx * 4 + 2]; // R
                outPixels[outIdx + 3] = 255;                   // A
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
                outPixels[outIdx + 0] = pixels[srcIdx + 0]; // B
                outPixels[outIdx + 1] = pixels[srcIdx + 1]; // G
                outPixels[outIdx + 2] = pixels[srcIdx + 2]; // R
                outPixels[outIdx + 3] = 255;                 // A
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
                outPixels[outIdx + 0] = pixels[srcIdx + 0]; // B
                outPixels[outIdx + 1] = pixels[srcIdx + 1]; // G
                outPixels[outIdx + 2] = pixels[srcIdx + 2]; // R
                outPixels[outIdx + 3] = pixels[srcIdx + 3]; // A
            }
        }
        return true;
    }

    return false;
}

// Bilinear upscale A8R8G8B8 pixels from srcW x srcH to dstW x dstH.
// Operates on raw uint32_t* for speed (no IImage::getPixel/setPixel overhead).
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

            // Sample 4 neighbors
            uint32_t c00 = srcPixels[y0 * srcW + x0];
            uint32_t c10 = srcPixels[y0 * srcW + x1];
            uint32_t c01 = srcPixels[y1 * srcW + x0];
            uint32_t c11 = srcPixels[y1 * srcW + x1];

            // Bilinear interpolation per channel
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

void IrrlichtRenderer::startBackgroundZoneLoad(const std::string& zoneName, const std::string& eqClientPath) {
    backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Loading;
    zoneLoadComplete_ = false;
    pendingZoneComputations_ = std::make_unique<PendingZoneComputations>();

    std::string zonePath = eqClientPath;
    if (!zonePath.empty() && zonePath.back() != '/' && zonePath.back() != '\\')
        zonePath += '/';
    zonePath += zoneName + ".s3d";

    auto* computations = pendingZoneComputations_.get();

    // Capture config values by copy for background thread (set once at init, never modified)
    bool deferredAssetLoading = config_.constrainedConfig.deferredAssetLoading;
    bool lazyPfsLoading = config_.constrainedConfig.lazyPfsLoading;
    bool enableAtlas = config_.constrainedConfig.enableTextureAtlas;
    std::string atlasPathCopy = config_.constrainedConfig.atlasPath;
    std::string zoneNameCopy = zoneName;
    std::string eqClientPathCopy = eqClientPath;
    if (!eqClientPathCopy.empty() && eqClientPathCopy.back() != '/' && eqClientPathCopy.back() != '\\')
        eqClientPathCopy += '/';
    size_t meshMemoryBudget = config_.constrainedConfig.meshMemoryBytes;
    // TreeIdentifier::isTreeMesh() is const — pure read-only string matching against patterns
    // set in constructor. loadConfig() that modifies patterns runs during DeferredInitStep::TreeConfig,
    // which is well after the background thread is joined.
    const TreeIdentifier* treeIdentifier = treeManager_ ? &treeManager_->getTreeIdentifier() : nullptr;

    zoneLoadThread_ = std::make_unique<std::thread>([this, zonePath, computations,
                                                      deferredAssetLoading, lazyPfsLoading,
                                                      enableAtlas, atlasPathCopy, zoneNameCopy,
                                                      eqClientPathCopy, meshMemoryBudget,
                                                      treeIdentifier]() {
        // 1. S3D parse (existing)
        S3DLoader loader;
        if (!loader.loadZone(zonePath)) {
            LOG_ERROR(MOD_GRAPHICS, "Background S3D load failed: {}", loader.getError());
            zoneLoadComplete_ = true;
            return;
        }
        pendingZoneData_ = loader.getZone();
        auto zone = pendingZoneData_;

        // 2-5: CPU-only post-processing on background thread
        if (zone->wldLoader) {
            auto bspTree = zone->wldLoader->getBspTree();
            if (bspTree && !bspTree->regions.empty() && zone->wldLoader->hasPvsData()) {

                // 2. Compute region bounding boxes (CPU-only)
                for (size_t i = 0; i < bspTree->regions.size(); ++i) {
                    auto geom = zone->wldLoader->getGeometryForRegion(i);
                    if (!geom || geom->vertices.empty()) continue;

                    float vMinX = std::numeric_limits<float>::max();
                    float vMinY = vMinX, vMinZ = vMinX;
                    float vMaxX = std::numeric_limits<float>::lowest();
                    float vMaxY = vMaxX, vMaxZ = vMaxX;
                    for (const auto& v : geom->vertices) {
                        float wx = geom->centerX + v.x;
                        float wy = geom->centerY + v.y;
                        float wz = geom->centerZ + v.z;
                        if (wx < vMinX) vMinX = wx;
                        if (wy < vMinY) vMinY = wy;
                        if (wz < vMinZ) vMinZ = wz;
                        if (wx > vMaxX) vMaxX = wx;
                        if (wy > vMaxY) vMaxY = wy;
                        if (wz > vMaxZ) vMaxZ = wz;
                    }
                    irr::core::aabbox3df worldBounds;
                    worldBounds.MinEdge.X = vMinX;
                    worldBounds.MinEdge.Y = vMinY;
                    worldBounds.MinEdge.Z = vMinZ;
                    worldBounds.MaxEdge.X = vMaxX;
                    worldBounds.MaxEdge.Y = vMaxY;
                    worldBounds.MaxEdge.Z = vMaxZ;
                    computations->regionBoundingBoxes[i] = worldBounds;
                }
                LOG_INFO(MOD_GRAPHICS, "Background: computed {} region bounding boxes",
                         computations->regionBoundingBoxes.size());

                // 3. Build portal system (CPU-only)
                computations->portalSystem = std::make_unique<PortalSystem>();
                computations->portalSystem->buildFromBsp(*bspTree, computations->regionBoundingBoxes);
                computations->portalOcclusionEligible = computations->portalSystem->hasPortals() &&
                    (computations->portalSystem->getData().portals.size() > 10);
                if (computations->portalOcclusionEligible) {
                    LOG_INFO(MOD_GRAPHICS, "Background: portal system built ({} portals)",
                             computations->portalSystem->getData().portals.size());
                }

                // 4. Cache zone light BSP regions (CPU-only)
                for (size_t i = 0; i < zone->lights.size(); ++i) {
                    const auto& light = zone->lights[i];
                    size_t regionIdx = bspTree->findRegionIndexForPoint(light->x, light->y, light->z);
                    computations->zoneLightRegions.push_back(regionIdx);
                }
                LOG_INFO(MOD_GRAPHICS, "Background: cached BSP regions for {} zone lights",
                         computations->zoneLightRegions.size());
            }

            // 5. Index objects (CPU-only) — build (objectIndex, bspRegion) pairs
            // Note: tree filtering happens on main thread since treeManager_ is not thread-safe
            if (bspTree) {
                for (size_t i = 0; i < zone->objects.size(); ++i) {
                    const auto& objInstance = zone->objects[i];
                    if (!objInstance.geometry || !objInstance.placeable) continue;

                    float x = objInstance.placeable->getX();
                    float y = objInstance.placeable->getY();
                    float z = objInstance.placeable->getZ();
                    size_t bspRegion = bspTree->findRegionIndexForPoint(x, y, z);
                    computations->deferredObjectEntries.emplace_back(i, bspRegion);
                }
                LOG_INFO(MOD_GRAPHICS, "Background: indexed {} objects with BSP regions",
                         computations->deferredObjectEntries.size());
            }

            // 5a. Pre-build ConstrainedMeshCache with all regions registered (CPU-only)
            if (meshMemoryBudget > 0 && !computations->regionBoundingBoxes.empty()) {
                auto prebuilt = std::make_unique<PendingZoneComputations::PrebuiltMeshCacheData>();
                prebuilt->cache = std::make_unique<ConstrainedMeshCache>(meshMemoryBudget);
                for (auto& [regionIdx, bounds] : computations->regionBoundingBoxes) {
                    prebuilt->cache->registerRegion(regionIdx);
                }
                // Count regions with geometry
                if (bspTree) {
                    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
                        if (zone->wldLoader->getGeometryForRegion(i)) prebuilt->regionsWithGeometry++;
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "Background: pre-built mesh cache ({} regions registered, {} with geometry, {} byte budget)",
                         computations->regionBoundingBoxes.size(), prebuilt->regionsWithGeometry, meshMemoryBudget);
                computations->prebuiltMeshCache = std::move(prebuilt);
            }

            // 5b. Pre-build deferred objects with tree filtering + world bounds (CPU-only)
            if (!computations->deferredObjectEntries.empty()) {
                for (auto& [objIdx, bspRegion] : computations->deferredObjectEntries) {
                    if (objIdx >= zone->objects.size()) continue;
                    const auto& objInstance = zone->objects[objIdx];
                    if (!objInstance.geometry || !objInstance.placeable) continue;

                    // Tree filter using captured const TreeIdentifier
                    if (treeIdentifier) {
                        const std::string& objName = objInstance.placeable->getName();
                        std::string primaryTexture;
                        if (!objInstance.geometry->textureNames.empty())
                            primaryTexture = objInstance.geometry->textureNames[0];
                        if (treeIdentifier->isTreeMesh(objName, primaryTexture))
                            continue;
                    }

                    DeferredObject deferred;
                    deferred.objectIndex = objIdx;
                    deferred.bspRegion = bspRegion;
                    deferred.meshBuilt = false;

                    // Estimate world bounds
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
                LOG_INFO(MOD_GRAPHICS, "Background: pre-built {} deferred objects (tree-filtered from {} entries)",
                         computations->prebuiltDeferredObjects.size(), computations->deferredObjectEntries.size());
            }
        }

        // 6. Build graphics archive index (filesystem I/O — no GL)
        if (deferredAssetLoading) {
            computations->archiveIndex = std::make_unique<GraphicsArchiveIndex>();
            if (computations->archiveIndex->buildIndex(eqClientPathCopy, lazyPfsLoading)) {
                LOG_INFO(MOD_GRAPHICS, "Background: graphics archive index built ({} race entries, {} archives)",
                         computations->archiveIndex->getRaceEntryCount(),
                         computations->archiveIndex->getArchiveCount());
            } else {
                LOG_WARN(MOD_GRAPHICS, "Background: graphics archive index build failed");
                computations->archiveIndex.reset();
            }
        }

        // 7. Build equipment model index (PFS + WLD parsing — no GL)
        {
            auto equipIdx = std::make_unique<PendingZoneComputations::EquipmentIndexData>();

            // Load item model mapping
            std::vector<std::string> searchPaths = {
                "data/item_models.json",
                "../data/item_models.json",
                eqClientPathCopy + "../data/item_models.json",
                eqClientPathCopy + "../../eqt-irrlicht/data/item_models.json"
            };
            for (const auto& path : searchPaths) {
                if (EquipmentModelLoader::loadItemModelMappingStatic(path, equipIdx->itemToModelMap) >= 0) {
                    LOG_INFO(MOD_GRAPHICS, "Background: loaded {} item-to-model mappings from {}",
                             equipIdx->itemToModelMap.size(), path);
                    break;
                }
            }

            // Index gequip archives
            if (EquipmentModelLoader::buildEquipmentIndex(eqClientPathCopy,
                    equipIdx->modelIndex, equipIdx->textureIndex)) {
                LOG_INFO(MOD_GRAPHICS, "Background: equipment index built ({} models, {} textures)",
                         equipIdx->modelIndex.size(), equipIdx->textureIndex.size());
                computations->equipmentIndex = std::move(equipIdx);
            } else {
                LOG_WARN(MOD_GRAPHICS, "Background: equipment index build failed");
            }
        }

        // 8. Pre-load global character archives (S3D parsing — no GL)
        {
            auto assets = std::make_unique<PendingZoneComputations::GlobalAssetsData>();

            // 8a. global_chr.s3d
            if (RaceModelLoader::loadGlobalModelsStatic(eqClientPathCopy,
                    assets->globalCharacters, assets->globalTextures)) {
                LOG_INFO(MOD_GRAPHICS, "Background: loaded {} characters from global_chr.s3d",
                         assets->globalCharacters.size());
            }

            // 8b. global2-7_chr.s3d
            if (RaceModelLoader::loadNumberedGlobalModelsStatic(eqClientPathCopy,
                    assets->numberedGlobalCharacters, assets->numberedGlobalTextures)) {
                LOG_INFO(MOD_GRAPHICS, "Background: loaded {} numbered global archives",
                         assets->numberedGlobalCharacters.size());
            }

            // 8c. global17-23_amr.s3d armor texture index
            if (RaceModelLoader::loadArmorTextureIndexStatic(eqClientPathCopy,
                    assets->armorTextureIndex)) {
                LOG_INFO(MOD_GRAPHICS, "Background: indexed {} armor textures",
                         assets->armorTextureIndex.size());
            }

            if (!assets->globalCharacters.empty()) {
                computations->globalAssets = std::move(assets);
            }
        }

        // 9. Pre-load sky data (S3D archive + INI parsing — no GL)
        {
            auto skyData = std::make_unique<PendingZoneComputations::SkyLoadData>();
            skyData->skyLoader = std::make_unique<SkyLoader>();
            skyData->skyConfig = std::make_unique<SkyConfig>();

            if (skyData->skyLoader->load(eqClientPathCopy)) {
                LOG_INFO(MOD_GRAPHICS, "Background: sky.s3d loaded ({} textures)",
                         skyData->skyLoader->getSkyData()->textures.size());
            } else {
                LOG_WARN(MOD_GRAPHICS, "Background: sky.s3d load failed");
            }

            std::string skyIniPath = eqClientPathCopy + "sky.ini";
            if (skyData->skyConfig->loadFromFile(skyIniPath)) {
                LOG_INFO(MOD_GRAPHICS, "Background: sky.ini loaded ({} zone configs)",
                         skyData->skyConfig->getZoneCount());
            } else {
                LOG_WARN(MOD_GRAPHICS, "Background: sky.ini load failed, will use defaults");
            }

            // Pre-decode + upscale BMP sky textures on background thread (no GL)
            if (skyData->skyLoader && skyData->skyLoader->getSkyData()) {
                const auto& textures = skyData->skyLoader->getSkyData()->textures;
                for (const auto& [texName, texInfo] : textures) {
                    if (!texInfo || texInfo->data.size() < 2) continue;
                    // Only handle BMPs (header bytes 'B','M')
                    if (texInfo->data[0] != 'B' || texInfo->data[1] != 'M') continue;

                    PendingZoneComputations::SkyLoadData::PreDecodedTexture preTex;
                    preTex.name = texName;

                    uint32_t decW = 0, decH = 0;
                    std::vector<uint8_t> decoded;
                    if (!decodeBMPtoARGB(texInfo->data, decoded, decW, decH)) {
                        LOG_WARN(MOD_GRAPHICS, "Background: failed to decode BMP sky texture: {}", texName);
                        continue;
                    }

                    // Upscale small textures (<=128x128) to 512x512 with bilinear interpolation
                    if (decW <= 128 && decH <= 128 && decW > 0 && decH > 0) {
                        const uint32_t targetSize = 512;
                        bilinearUpscaleARGB(decoded.data(), decW, decH,
                                            preTex.pixels, targetSize, targetSize);
                        preTex.width = targetSize;
                        preTex.height = targetSize;
                        LOG_INFO(MOD_GRAPHICS, "Background: pre-decoded + upscaled sky texture: {} ({}x{} -> {}x{})",
                                 texName, decW, decH, targetSize, targetSize);
                    } else {
                        // Already large enough, just store decoded pixels
                        preTex.pixels = std::move(decoded);
                        preTex.width = decW;
                        preTex.height = decH;
                        LOG_INFO(MOD_GRAPHICS, "Background: pre-decoded sky texture: {} ({}x{})",
                                 texName, decW, decH);
                    }

                    skyData->preDecodedTextures.push_back(std::move(preTex));
                }
                LOG_INFO(MOD_GRAPHICS, "Background: pre-decoded {} BMP sky textures",
                         skyData->preDecodedTextures.size());
            }

            // Pre-compute dome mesh geometry (pure CPU trig, ~5ms)
            skyData->precomputedDome = std::make_unique<PendingZoneComputations::SkyLoadData::PrecomputedSkyDome>();
            SkyRenderer::precomputeDomeMesh(skyData->precomputedDome->vertices,
                                            skyData->precomputedDome->indices);
            LOG_INFO(MOD_GRAPHICS, "Background: pre-computed sky dome mesh ({} verts, {} indices)",
                     skyData->precomputedDome->vertices.size(),
                     skyData->precomputedDome->indices.size());

            computations->skyLoadData = std::move(skyData);
        }

        // 9b. Pre-load weather config (file I/O + JSON — no GL)
        {
            auto weatherData = std::make_unique<PendingZoneComputations::WeatherConfigData>();
            ZoneWeatherConfig wconfig;
            if (loadZoneWeatherConfig(zoneNameCopy, wconfig)) {
                weatherData->config = wconfig;
                weatherData->loaded = true;
                LOG_INFO(MOD_GRAPHICS, "Background: pre-loaded weather config for '{}'", zoneNameCopy);
            } else {
                weatherData->config.zoneName = zoneNameCopy;
                weatherData->config.defaultWeather = WeatherType::Normal;
                weatherData->config.enabled = true;
                weatherData->loaded = true;
                LOG_DEBUG(MOD_GRAPHICS, "Background: default weather config for '{}'", zoneNameCopy);
            }
            computations->weatherConfig = std::move(weatherData);
        }

        // 9c. Pre-load display settings (file I/O + JSON — no GL)
        {
            auto dispData = std::make_unique<PendingZoneComputations::DisplaySettingsData>();
            dispData->skyEnabled = loadDisplaySettingsFromFile().skyEnabled;
            dispData->loaded = true;
            computations->displaySettings = std::move(dispData);
        }

        // 10. Pre-load atlas files (file I/O + tile lookup — no GL)
        if (enableAtlas && !atlasPathCopy.empty()) {
            std::string atlasDir = atlasPathCopy;
            if (!atlasDir.empty() && atlasDir.back() != '/') atlasDir += '/';

            std::string zoneAtlasFile = atlasDir + zoneNameCopy + ".atlas";
            computations->zoneAtlasPreload = TextureAtlas::preloadFromFile(zoneAtlasFile);

            std::string objAtlasFile = atlasDir + zoneNameCopy + "_obj.atlas";
            computations->objAtlasPreload = TextureAtlas::preloadFromFile(objAtlasFile);
        }

        zoneLoadComplete_ = true;
    });

    LOG_INFO(MOD_GRAPHICS, "Background S3D load started (with CPU post-processing): {}", zonePath);
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

void IrrlichtRenderer::advanceBackgroundZoneLoad() {
    auto stepStart = std::chrono::steady_clock::now();

    switch (backgroundZoneLoadPhase_) {

    // ── Loading: poll background thread (no GREEN gate) ──────────────────
    case BackgroundZoneLoadPhase::Loading:
        if (zoneLoadComplete_) {
            if (zoneLoadThread_ && zoneLoadThread_->joinable())
                zoneLoadThread_->join();
            zoneLoadThread_.reset();

            if (pendingZoneData_) {
                backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Notify;
                LOG_INFO(MOD_GRAPHICS, "S3D data + computations received on main thread for zone '{}'",
                         currentZoneName_);
            } else {
                pendingZoneComputations_.reset();
                backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Complete;
                LOG_ERROR(MOD_GRAPHICS, "S3D background load produced no data");
            }
        }
        break;

    // ── DataReady sub-steps ──────────────────────────────────────────────

    case BackgroundZoneLoadPhase::DataReady_Notify: {
        // Install zone data and notify subsystems
        currentZone_ = pendingZoneData_;
        pendingZoneData_.reset();

        if (entityRenderer_) entityRenderer_->setCurrentZone(currentZoneName_);
        if (doorManager_) {
            doorManager_->setZone(currentZone_);
            if (constrainedTextureCache_)
                doorManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
        }

        // If global assets already loaded, skip to environment step
        if (globalAssetsLoaded_) {
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyPrepare;
            LOG_DEBUG(MOD_GRAPHICS, "Global assets already loaded, skipping to environment");
        } else {
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_EntityRenderer;
        }
        logAssetBuildTime("data_notify", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_EntityRenderer: {
        if (!entityRenderer_) {
            entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
            entityRenderer_->setClientPath(config_.eqClientPath);
            entityRenderer_->setNameTagsVisible(config_.showNameTags);
            entityRenderer_->setRenderDistance(renderDistance_);
            entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
            if (zoneShader_ && zoneShader_->isAvailable()) {
                entityRenderer_->setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                                        zoneShader_->getMaterialTypeAlphaTest());
            }
            if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
                entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
            }
            entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
                return this->findGroundZ(x, y, currentZ);
            });
            if (zoneBspTree_) entityRenderer_->setBspTree(zoneBspTree_);
            if (frustumCuller_) entityRenderer_->setFrustumCuller(frustumCuller_.get());
            // Entity occlusion culling handled by SimulationWorker
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_ArchiveIndex;
        logAssetBuildTime("entity_renderer_init", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_ArchiveIndex: {
        if (pendingZoneComputations_ && pendingZoneComputations_->archiveIndex) {
            // Background thread already built the index — just move it
            graphicsArchiveIndex_ = std::move(pendingZoneComputations_->archiveIndex);
            if (entityRenderer_->getRaceModelLoader()) {
                entityRenderer_->getRaceModelLoader()->setGraphicsArchiveIndex(graphicsArchiveIndex_.get());
            }
            LOG_INFO(MOD_GRAPHICS, "Graphics archive index adopted from background thread ({} race entries, {} archives)",
                     graphicsArchiveIndex_->getRaceEntryCount(), graphicsArchiveIndex_->getArchiveCount());
        } else if (config_.constrainedConfig.deferredAssetLoading) {
            // Fallback: build synchronously (background build failed or wasn't attempted)
            graphicsArchiveIndex_ = std::make_unique<GraphicsArchiveIndex>();
            bool lazyMode = config_.constrainedConfig.lazyPfsLoading;
            if (graphicsArchiveIndex_->buildIndex(config_.eqClientPath, lazyMode, networkTickCallback_)) {
                LOG_INFO(MOD_GRAPHICS, "Graphics archive index built (fallback): {} race entries from {} archives",
                         graphicsArchiveIndex_->getRaceEntryCount(), graphicsArchiveIndex_->getArchiveCount());
                if (entityRenderer_->getRaceModelLoader()) {
                    entityRenderer_->getRaceModelLoader()->setGraphicsArchiveIndex(graphicsArchiveIndex_.get());
                }
            } else {
                LOG_WARN(MOD_GRAPHICS, "Graphics archive index build failed, falling back to eager loading");
                entityRenderer_->loadGlobalCharacters();
                if (networkTickCallback_) networkTickCallback_();
                entityRenderer_->loadNumberedGlobals();
            }
        } else {
            // Non-deferred: eager loading (unchanged)
            if (entityRenderer_->loadGlobalCharacters()) {
                LOG_DEBUG(MOD_GRAPHICS, "Global character models loaded");
            } else {
                LOG_WARN(MOD_GRAPHICS, "Could not load global character models (will use placeholders)");
            }
            if (networkTickCallback_) networkTickCallback_();
            entityRenderer_->loadNumberedGlobals();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_GlobalAssets;
        logAssetBuildTime("archive_index", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_GlobalAssets: {
        // Adopt pre-built global character assets from background thread
        if (pendingZoneComputations_ && pendingZoneComputations_->globalAssets &&
            entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
            auto& ga = *pendingZoneComputations_->globalAssets;
            entityRenderer_->getRaceModelLoader()->adoptGlobalAssets(
                std::move(ga.globalCharacters), std::move(ga.globalTextures),
                std::move(ga.numberedGlobalCharacters), std::move(ga.numberedGlobalTextures),
                std::move(ga.armorTextureIndex));
            LOG_INFO(MOD_GRAPHICS, "Global character assets adopted from background thread");
        }
        // Ensure RaceModelLoader knows the current zone (may be skipped in DataReady_Notify on first load)
        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setCurrentZone(currentZoneName_);
        }
        if (networkTickCallback_) networkTickCallback_();
        entityPrepReady_ = true;
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Equipment;
        logAssetBuildTime("global_assets", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Equipment: {
        if (pendingZoneComputations_ && pendingZoneComputations_->equipmentIndex) {
            // Background thread already built the index — adopt it
            auto& idx = *pendingZoneComputations_->equipmentIndex;
            entityRenderer_->getEquipmentModelLoader()->adoptIndex(
                std::move(idx.modelIndex), std::move(idx.textureIndex), std::move(idx.itemToModelMap));
            LOG_INFO(MOD_GRAPHICS, "Equipment index adopted from background thread");
        } else {
            // Fallback: build synchronously
            if (entityRenderer_->loadEquipmentModels()) {
                LOG_INFO(MOD_GRAPHICS, "Equipment models loaded (fallback)");
            } else {
                LOG_INFO(MOD_GRAPHICS, "Could not load equipment models");
            }
        }
        if (networkTickCallback_) networkTickCallback_();
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_DoorManager;
        logAssetBuildTime("equipment_models", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_DoorManager: {
        if (!doorManager_) {
            doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
            if (constrainedTextureCache_) {
                doorManager_->setConstrainedTextureCache(constrainedTextureCache_.get());
            }
            if (currentZone_) doorManager_->setZone(currentZone_);
            if (zoneBspTree_) doorManager_->setBspTree(zoneBspTree_.get());
            doorManager_->setPvsRegion(currentPvsRegion_);
            if (frustumCuller_) doorManager_->setFrustumCuller(frustumCuller_.get());
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_SkyCreate;
        logAssetBuildTime("door_manager_init", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_SkyCreate: {
        bool hasBgData = pendingZoneComputations_ && pendingZoneComputations_->skyLoadData &&
                         pendingZoneComputations_->skyLoadData->skyLoader;
        if (!skyRenderer_) {
            skyRenderer_ = std::make_unique<SkyRenderer>(smgr_, driver_, device_->getFileSystem());
            if (hasBgData) {
                // Adopt pre-loaded loader + config (lightweight pointer moves)
                // NOTE: skyLoadData is NOT reset here — pre-decoded textures are
                // still needed by the DataReady_Env_SkyTextures step later.
                if (!skyRenderer_->initializeFromPreloaded(
                        std::move(pendingZoneComputations_->skyLoadData->skyLoader),
                        std::move(pendingZoneComputations_->skyLoadData->skyConfig))) {
                    LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization from preloaded data failed");
                } else {
                    LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized from background data");
                }
            } else {
                // Fallback: synchronous load (no background data available)
                if (!skyRenderer_->initialize(config_.eqClientPath)) {
                    LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization failed");
                } else {
                    LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized (synchronous fallback)");
                }
            }
        } else if (hasBgData) {
            // Re-adopt fresh sky data on /loadzone (skyRenderer_ already exists)
            skyRenderer_->initializeFromPreloaded(
                std::move(pendingZoneComputations_->skyLoadData->skyLoader),
                std::move(pendingZoneComputations_->skyLoadData->skyConfig));
            LOG_INFO(MOD_GRAPHICS, "Sky renderer re-initialized from background data (zone reload)");
        }
        if (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData) {
            LOG_DEBUG(MOD_GRAPHICS, "SkyCreate: skyLoadData intact, {} pre-decoded textures, dome={}",
                      pendingZoneComputations_->skyLoadData->preDecodedTextures.size(),
                      pendingZoneComputations_->skyLoadData->precomputedDome ? "yes" : "no");
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_DetailManager;
        logAssetBuildTime("sky_create", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_DetailManager: {
        if (!detailManager_) {
            eqt::ui::DisplaySettings detailSettings;
            if (windowManager_ && windowManager_->getOptionsWindow()) {
                detailSettings = windowManager_->getOptionsWindow()->getDisplaySettings();
            } else {
                detailSettings = loadDisplaySettingsFromFile();
            }
            if (detailSettings.detailObjectsEnabled) {
                detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
                detailManager_->setSurfaceMapsPath("data/detail/zones");
                LOG_INFO(MOD_GRAPHICS, "Detail manager initialized");
            }
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_ModelView;
        logAssetBuildTime("detail_manager_init", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_ModelView: {
        // Model view init deferred to first inventory open (saves ~21ms during zone-in)
        // Just store the loader pointers so WindowManager can lazy-init later
        if (windowManager_ && entityRenderer_) {
            windowManager_->storeModelViewDeps(smgr_,
                                               entityRenderer_->getRaceModelLoader(),
                                               entityRenderer_->getEquipmentModelLoader());
        }
        globalAssetsLoaded_ = true;
        LOG_INFO(MOD_GRAPHICS, "Global assets loaded (model view deferred to first inventory open)");
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyPrepare;
        logAssetBuildTime("model_view_deps", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyPrepare: {
        // Sky type config lookups only (no GL, no scene nodes) — ~2ms
        if (storedZoneEnvironment_.pending) {
            if (skyRenderer_ && skyRenderer_->isInitialized()) {
                skyRenderer_->prepareSkyType(storedZoneEnvironment_.skyType, currentZoneName_);

                bool isDungeon = (storedZoneEnvironment_.zoneType == 2);
                isIndoorZone_ = isDungeon;

                bool skySettingEnabled = true;
                if (windowManager_ && windowManager_->getOptionsWindow()) {
                    skySettingEnabled = windowManager_->getOptionsWindow()->getDisplaySettings().skyEnabled;
                } else if (pendingZoneComputations_ && pendingZoneComputations_->displaySettings &&
                           pendingZoneComputations_->displaySettings->loaded) {
                    skySettingEnabled = pendingZoneComputations_->displaySettings->skyEnabled;
                } else {
                    skySettingEnabled = loadDisplaySettingsFromFile().skyEnabled;
                }
                skyRenderer_->setEnabled(!isDungeon && skySettingEnabled);
            }
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_FogSetup;
        logAssetBuildTime("env_sky_prepare", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_FogSetup: {
        // Fog + clip plane + render distance (has GL call) — ~5-8ms
        if (storedZoneEnvironment_.pending) {
            zoneMaxClip_ = (storedZoneEnvironment_.fogMaxClip[0] > 0.0f)
                ? storedZoneEnvironment_.fogMaxClip[0] : 99999.0f;
            setRenderDistance(userRenderDistance_);

            if (driver_ && fogEnabled_) {
                irr::video::SColor fogColor(255,
                    storedZoneEnvironment_.fogR[0],
                    storedZoneEnvironment_.fogG[0],
                    storedZoneEnvironment_.fogB[0]);
                float fogEnd = renderDistance_;
                float fogStart = std::max(0.0f, renderDistance_ - fogThickness_);
                driver_->setFog(fogColor, irr::video::EFT_FOG_LINEAR,
                                fogStart, fogEnd, 0.0f, true, false);
            }
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_WeatherApply;
        logAssetBuildTime("env_fog_setup", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_WeatherApply: {
        // Weather config apply (no GL, no file I/O — pre-loaded on background thread) — ~1ms
        if (storedZoneEnvironment_.pending && weatherSystem_) {
            if (pendingZoneComputations_ && pendingZoneComputations_->weatherConfig &&
                pendingZoneComputations_->weatherConfig->loaded) {
                weatherSystem_->setZoneConfig(pendingZoneComputations_->weatherConfig->config);
            } else {
                weatherSystem_->setWeatherFromZone(currentZoneName_);
            }
            LOG_INFO(MOD_GRAPHICS, "Applied zone environment config (sky={}, ztype={})",
                     storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType);
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyTextures;
        logAssetBuildTime("env_weather_apply", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyTextures: {
        // Upload pre-decoded sky textures from background thread to GPU.
        // On first entry skyTexUploadIndex_ is 0; repeats until all are uploaded.

        if (skyRenderer_ && pendingZoneComputations_ &&
            pendingZoneComputations_->skyLoadData) {
            auto& preTextures = pendingZoneComputations_->skyLoadData->preDecodedTextures;
            size_t total = preTextures.size();

#ifdef EQT_HAS_GLES2
            // GLES2 strip upload path: upload 1 strip (64 rows) per GREEN frame.
            // Splits each 512x512 A8R8G8B8 texture into ~8 strips of ~2ms each,
            // instead of one 12-21ms glTexImage2D call per texture.
            while (skyTexUploadIndex_ < total) {
                auto& preTex = preTextures[skyTexUploadIndex_];

                if (!skyRenderer_->isStripActive()) {
                    // Not currently uploading — start next texture or skip
                    if (preTex.pixels.empty()) {
                        skyTexUploadIndex_++;
                        continue;  // Empty texture — skip, try next in same frame
                    }
                    if (skyRenderer_->beginStripUpload(
                            preTex.name, preTex.pixels.data(), preTex.width, preTex.height)) {
                        // Already cached — free data and skip
                        preTex.pixels.clear();
                        preTex.pixels.shrink_to_fit();
                        skyTexUploadIndex_++;
                        continue;  // Cache hit is free — try next in same frame
                    }
                    // Strip upload started (uploaded strip 0) — break for this frame
                    break;
                } else {
                    // Continue uploading strips for current texture
                    if (skyRenderer_->continueStripUpload()) {
                        // All strips done — wrap as ITexture and cache
                        skyRenderer_->finalizeStripUpload();
                        preTex.pixels.clear();
                        preTex.pixels.shrink_to_fit();
                        skyTexUploadIndex_++;
                    }
                    break;  // One strip per frame
                }
            }

            if (skyTexUploadIndex_ < total) {
                logAssetBuildTime("env_sky_textures_strip", 1, stepStart);
                break;  // More work — stay in this phase
            }
            logAssetBuildTime("env_sky_textures_strip", 0, stepStart);
#else
            // Desktop GL single-upload path: 1 whole texture per GREEN frame.
            constexpr size_t SKY_TEX_BATCH_SIZE = 1;
            size_t batchEnd = std::min(skyTexUploadIndex_ + SKY_TEX_BATCH_SIZE, total);
            size_t uploaded = 0;

            for (; skyTexUploadIndex_ < batchEnd; ++skyTexUploadIndex_) {
                auto& preTex = preTextures[skyTexUploadIndex_];
                if (!preTex.pixels.empty()) {
                    skyRenderer_->uploadPreDecodedTexture(
                        preTex.name, preTex.pixels.data(),
                        preTex.width, preTex.height);
                    preTex.pixels.clear();
                    preTex.pixels.shrink_to_fit();
                    ++uploaded;
                }
            }

            if (skyTexUploadIndex_ < total) {
                logAssetBuildTime("env_sky_textures_batch", uploaded, stepStart);
                break;
            }
            logAssetBuildTime("env_sky_textures_batch", uploaded, stepStart);
#endif
        } else {
            LOG_WARN(MOD_GRAPHICS, "env_sky_textures: condition failed — skyRenderer_={}, pendingComps={}, skyLoadData={}",
                     skyRenderer_ ? "yes" : "no",
                     pendingZoneComputations_ ? "yes" : "no",
                     (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData) ? "yes" : "no");
            logAssetBuildTime("env_sky_textures", 0, stepStart);
        }
        skyTexUploadIndex_ = 0;
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyRelease;
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyRelease: {
        // Free the pre-decoded sky texture pixel data (already uploaded to GPU).
        // Keep skyLoadData alive — precomputedDome is needed by DataReady_Env_SkyDome.
        if (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData) {
            pendingZoneComputations_->skyLoadData->preDecodedTextures.clear();
            pendingZoneComputations_->skyLoadData->preDecodedTextures.shrink_to_fit();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyDome;
        logAssetBuildTime("env_sky_release", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyDome: {
        // Create dome mesh node from pre-computed vertex/index data + set material.
        // Texture lookups hit cache (uploaded in env_sky_textures phase).
        if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isSkyPrepared()) {
            skyRenderer_->clearSkyForRebuild();
            if (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData &&
                pendingZoneComputations_->skyLoadData->precomputedDome) {
                auto& dome = *pendingZoneComputations_->skyLoadData->precomputedDome;
                skyRenderer_->createSkyDomeFromPrecomputed(dome.vertices, dome.indices);
            } else {
                // Fallback: generate dome on render thread (no pre-computed data)
                LOG_WARN(MOD_GRAPHICS, "No pre-computed dome data, creating dome on render thread");
                skyRenderer_->applySkyType();
            }
        }
        // Free dome data now that it's been consumed
        if (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData) {
            pendingZoneComputations_->skyLoadData->precomputedDome.reset();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyCelestials;
        logAssetBuildTime("env_sky_dome", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyCelestials: {
        // Create sun/moon billboard scene nodes (texture lookups are cache hits).
        if (skyRenderer_ && skyRenderer_->isInitialized()) {
            skyRenderer_->createCelestialBodiesOnly();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::DataReady_Env_SkyColors;
        logAssetBuildTime("env_sky_celestials", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::DataReady_Env_SkyColors: {
        // Calculate sky colors + update vertex alpha + celestial positions.
        if (skyRenderer_ && skyRenderer_->isInitialized()) {
            skyRenderer_->applyInitialColors();
            skyRenderer_->consumeSkyPrepared();
        }
        if (storedZoneEnvironment_.pending) {
            storedZoneEnvironment_.pending = false;
        }
        // Fully release sky load data now
        if (pendingZoneComputations_ && pendingZoneComputations_->skyLoadData) {
            pendingZoneComputations_->skyLoadData.reset();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Zone_Upload;
        logAssetBuildTime("env_sky_colors", 0, stepStart);
        break;
    }

    // ── Atlas sub-steps (preloaded on bg thread, 1 GL page upload per GREEN frame) ──

    case BackgroundZoneLoadPhase::Atlas_Zone_Upload: {
        if (pendingZoneComputations_ && pendingZoneComputations_->zoneAtlasPreload.valid) {
            auto& preload = pendingZoneComputations_->zoneAtlasPreload;
            if (!zoneAtlas_) {
                zoneAtlas_ = std::make_unique<TextureAtlas>();
                atlasZonePageIndex_ = 0;
            }
            bool done = zoneAtlas_->uploadPreloadedPage(preload, atlasZonePageIndex_);
            logAssetBuildTime("atlas_zone_page", atlasZonePageIndex_, stepStart);
            ++atlasZonePageIndex_;
            if (!done) {
                break;  // Stay in this phase — more pages to upload
            }
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Zone_Finalize;
        } else {
            // No zone atlas preloaded — skip
            zoneAtlas_.reset();
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Object_Upload;
            logAssetBuildTime("atlas_zone_skip", 0, stepStart);
        }
        break;
    }

    case BackgroundZoneLoadPhase::Atlas_Zone_Finalize: {
        if (zoneAtlas_ && pendingZoneComputations_) {
            zoneAtlas_->finalizePreload(pendingZoneComputations_->zoneAtlasPreload);
            LOG_INFO(MOD_GRAPHICS, "Zone atlas finalized: {} pages, {} tiles",
                     zoneAtlas_->getPageCount(), zoneAtlas_->getTileCount());
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Object_Upload;
        logAssetBuildTime("atlas_zone_finalize", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Atlas_Object_Upload: {
        if (pendingZoneComputations_ && pendingZoneComputations_->objAtlasPreload.valid) {
            auto& preload = pendingZoneComputations_->objAtlasPreload;
            if (!objAtlas_) {
                objAtlas_ = std::make_unique<TextureAtlas>();
                atlasObjPageIndex_ = 0;
            }
            bool done = objAtlas_->uploadPreloadedPage(preload, atlasObjPageIndex_);
            logAssetBuildTime("atlas_obj_page", atlasObjPageIndex_, stepStart);
            ++atlasObjPageIndex_;
            if (!done) {
                break;  // Stay in this phase — more pages to upload
            }
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Object_Finalize;
        } else {
            // No object atlas preloaded — skip
            objAtlas_.reset();
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Shader;
            logAssetBuildTime("atlas_obj_skip", 0, stepStart);
        }
        break;
    }

    case BackgroundZoneLoadPhase::Atlas_Object_Finalize: {
        if (objAtlas_ && pendingZoneComputations_) {
            objAtlas_->finalizePreload(pendingZoneComputations_->objAtlasPreload);
            LOG_INFO(MOD_GRAPHICS, "Object atlas finalized: {} pages, {} tiles",
                     objAtlas_->getPageCount(), objAtlas_->getTileCount());
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Atlas_Shader;
        logAssetBuildTime("atlas_obj_finalize", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Atlas_Shader: {
        // Set shader page textures for zone atlas
        if (zoneAtlas_ && zoneAtlas_->isLoaded() && zoneShader_ && zoneShader_->isAtlasAvailable()) {
            std::vector<uint32_t> pageTextures;
            for (uint16_t p = 0; p < zoneAtlas_->getPageCount(); ++p) {
                pageTextures.push_back(zoneAtlas_->getPageTexture(p));
            }
            zoneShader_->setAtlasPageTextures(pageTextures);
        }
        // Set shader page textures for object atlas
        if (objAtlas_ && objAtlas_->isLoaded() && zoneShader_ && zoneShader_->isAtlasAvailable()) {
            std::vector<uint32_t> objPageTextures;
            for (uint16_t p = 0; p < objAtlas_->getPageCount(); ++p) {
                objPageTextures.push_back(objAtlas_->getPageTexture(p));
            }
            objAtlasPageOffset_ = zoneShader_->appendAtlasPageTextures(objPageTextures);
            LOG_INFO(MOD_GRAPHICS, "Object atlas shader pages set (page offset {})", objAtlasPageOffset_);
        }

        // Atlas loading uses raw GL calls — reset texture bind state
#ifdef EQT_HAS_GLES2
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
#endif

        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_InstallBsp;
        logAssetBuildTime("atlas_shader", 0, stepStart);
        break;
    }

    // ── Region mesh sub-steps ────────────────────────────────────────────

    case BackgroundZoneLoadPhase::RegionMesh_InstallBsp: {
        if (!currentZone_ || !currentZone_->wldLoader) {
            LOG_WARN(MOD_GRAPHICS, "Cannot create PVS mesh - no zone or WLD loader");
            createZoneMesh();  // Fall back to combined mesh
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Lights_CreateNodes;
            break;
        }

        auto wldLoader = currentZone_->wldLoader;
        auto bspTree = wldLoader->getBspTree();

        if (!bspTree || bspTree->regions.empty() || !wldLoader->hasPvsData()) {
            LOG_INFO(MOD_GRAPHICS, "Zone has no PVS data, using combined mesh");
            createZoneMesh();
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Lights_CreateNodes;
            break;
        }

        // Clean up existing mesh nodes
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

        // Install BSP tree — preserve existing if already installed (e.g. from BSP preload)
        // The BSP tree, PVS visibility sets, and region bounding boxes are derived from
        // zone geometry (WLD data) which doesn't change during /loadzone. Preserving them
        // keeps currentPvsRegion_ valid so PVS culling has no gap during scene rebuild.
        if (!zoneBspTree_) {
            zoneBspTree_ = bspTree;
            usePvsCulling_ = true;
            currentPvsRegion_ = SIZE_MAX;
        } else {
            LOG_INFO(MOD_GRAPHICS, "Preserving BSP tree ({} regions), PVS region {}",
                     zoneBspTree_->regions.size(), currentPvsRegion_);
        }

        // Install pre-computed bounding boxes from background thread (if not already present)
        if (regionBoundingBoxes_.empty() && pendingZoneComputations_ && !pendingZoneComputations_->regionBoundingBoxes.empty()) {
            regionBoundingBoxes_ = std::move(pendingZoneComputations_->regionBoundingBoxes);
            LOG_INFO(MOD_GRAPHICS, "Installed {} pre-computed region bounding boxes",
                     regionBoundingBoxes_.size());
        }

        // Pass BSP tree and frustum culler to entity renderer and door manager
        // (they may have been cleared during earlier phases of zone reload)
        if (entityRenderer_) {
            entityRenderer_->setBspTree(zoneBspTree_);
            entityRenderer_->setFrustumCuller(frustumCuller_.get());
            // Entity occlusion culling handled by SimulationWorker
        }
        if (doorManager_) {
            doorManager_->setBspTree(zoneBspTree_.get());
            doorManager_->setPvsRegion(currentPvsRegion_);
            doorManager_->setFrustumCuller(frustumCuller_.get());
        }

        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_InitCache;
        logAssetBuildTime("region_install_bsp", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::RegionMesh_InitCache: {
        auto wldLoader = currentZone_->wldLoader;
        auto bspTree = wldLoader->getBspTree();

        // Fast path: adopt pre-built mesh cache from background thread
        if (pendingZoneComputations_ && pendingZoneComputations_->prebuiltMeshCache) {
            if (!regionMeshCacheInstallStarted_) {
                // First entry: adopt the cache and start progressive regionMeshNodes_ population
                auto& prebuilt = pendingZoneComputations_->prebuiltMeshCache;
                constrainedMeshCache_ = std::move(prebuilt->cache);
                LOG_INFO(MOD_GRAPHICS, "Creating PVS mesh with {} regions ({} with geometry)",
                         bspTree->regions.size(), prebuilt->regionsWithGeometry);
                LOG_INFO(MOD_GRAPHICS, "Lazy mesh loading enabled: {} byte budget (pre-built)",
                         config_.constrainedConfig.meshMemoryBytes);
                regionMeshCacheInstallStarted_ = true;
                regionMeshCacheInstallCursor_ = regionBoundingBoxes_.begin();
            }

            // Batch: 200 regionMeshNodes_ insertions per GREEN frame
            // Use emplace to avoid overwriting entries that P1 Critical may have
            // already populated via rebuildRegionMesh() on an earlier frame.
            const size_t batchSize = 200;
            for (size_t i = 0; i < batchSize && regionMeshCacheInstallCursor_ != regionBoundingBoxes_.end(); ++i) {
                regionMeshNodes_.emplace(regionMeshCacheInstallCursor_->first, nullptr);
                ++regionMeshCacheInstallCursor_;
            }

            if (regionMeshCacheInstallCursor_ == regionBoundingBoxes_.end()) {
                // Done — all regionMeshNodes_ populated
                LOG_INFO(MOD_GRAPHICS, "Lazy mode: registered {} regions (no meshes built yet, pre-built)",
                         regionBoundingBoxes_.size());
                regionMeshCacheInstallStarted_ = false;
                backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_SortSetup;
                logAssetBuildTime("region_init_cache", regionBoundingBoxes_.size(), stepStart);
            }
            // else: stay in same phase for next GREEN frame
            break;
        }

        // Fallback: original code path (no prebuilt cache)
        size_t regionsWithGeometry = 0;
        for (size_t i = 0; i < bspTree->regions.size(); ++i) {
            if (wldLoader->getGeometryForRegion(i)) regionsWithGeometry++;
        }
        LOG_INFO(MOD_GRAPHICS, "Creating PVS mesh with {} regions ({} with geometry)",
                 bspTree->regions.size(), regionsWithGeometry);

        if (config_.constrainedConfig.meshMemoryBytes > 0) {
            constrainedMeshCache_ = std::make_unique<ConstrainedMeshCache>(
                config_.constrainedConfig.meshMemoryBytes);
            LOG_INFO(MOD_GRAPHICS, "Lazy mesh loading enabled: {} byte budget",
                     config_.constrainedConfig.meshMemoryBytes);
        }

        if (constrainedMeshCache_) {
            size_t registeredRegions = 0;
            if (!regionBoundingBoxes_.empty()) {
                for (auto& [regionIdx, bounds] : regionBoundingBoxes_) {
                    constrainedMeshCache_->registerRegion(regionIdx);
                    regionMeshNodes_[regionIdx] = nullptr;
                    registeredRegions++;
                }
            } else {
                for (size_t regionIdx = 0; regionIdx < bspTree->regions.size(); ++regionIdx) {
                    auto geom = wldLoader->getGeometryForRegion(regionIdx);
                    if (!geom || geom->vertices.empty()) continue;

                    float vMinX = std::numeric_limits<float>::max();
                    float vMinY = vMinX, vMinZ = vMinX;
                    float vMaxX = std::numeric_limits<float>::lowest();
                    float vMaxY = vMaxX, vMaxZ = vMaxX;
                    for (const auto& v : geom->vertices) {
                        float wx = geom->centerX + v.x;
                        float wy = geom->centerY + v.y;
                        float wz = geom->centerZ + v.z;
                        if (wx < vMinX) vMinX = wx;
                        if (wy < vMinY) vMinY = wy;
                        if (wz < vMinZ) vMinZ = wz;
                        if (wx > vMaxX) vMaxX = wx;
                        if (wy > vMaxY) vMaxY = wy;
                        if (wz > vMaxZ) vMaxZ = wz;
                    }
                    irr::core::aabbox3df worldBounds;
                    worldBounds.MinEdge.X = vMinX;
                    worldBounds.MinEdge.Y = vMinY;
                    worldBounds.MinEdge.Z = vMinZ;
                    worldBounds.MaxEdge.X = vMaxX;
                    worldBounds.MaxEdge.Y = vMaxY;
                    worldBounds.MaxEdge.Z = vMaxZ;
                    regionBoundingBoxes_[regionIdx] = worldBounds;
                    constrainedMeshCache_->registerRegion(regionIdx);
                    regionMeshNodes_[regionIdx] = nullptr;
                    registeredRegions++;
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Lazy mode: registered {} regions (no meshes built yet)", registeredRegions);
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_SortSetup;
        } else {
            regionBuildIndex_ = 0;
            regionBuildInitDone_ = true;
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_EagerBatch;
        }
        logAssetBuildTime("region_init_cache", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::RegionMesh_EagerBatch: {
        // Build a batch of region meshes per GREEN frame
        auto wldLoader = currentZone_->wldLoader;
        auto bspTree = wldLoader->getBspTree();
        const size_t BATCH_SIZE = 10;

        ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
        if (constrainedTextureCache_)
            builder.setConstrainedTextureCache(constrainedTextureCache_.get());
        if (zoneShader_ && zoneShader_->isAvailable())
            builder.setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                           zoneShader_->getMaterialTypeAlphaTest());
        if (zoneShader_ && zoneShader_->isAtlasAvailable())
            builder.setAtlasShaderMaterialTypes(zoneShader_->getMaterialTypeAtlasSolid(),
                                                 zoneShader_->getMaterialTypeAtlasAlpha());

        bool useZoneAtlas = zoneAtlas_ && zoneAtlas_->isLoaded() &&
                            zoneShader_ && zoneShader_->isAtlasAvailable();

        size_t batchEnd = std::min(regionBuildIndex_ + BATCH_SIZE, bspTree->regions.size());
        size_t builtInBatch = 0;

        for (; regionBuildIndex_ < batchEnd; ++regionBuildIndex_) {
            auto geom = wldLoader->getGeometryForRegion(regionBuildIndex_);
            if (!geom || geom->vertices.empty()) continue;

            irr::scene::IMesh* mesh = nullptr;
            if (!currentZone_->textures.empty() && !geom->textureNames.empty()) {
                if (useZoneAtlas) {
                    mesh = builder.buildAtlasedMesh(*geom, currentZone_->textures, *zoneAtlas_);
                } else {
                    mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
                }
            } else {
                mesh = builder.buildColoredMesh(*geom);
            }
            if (!mesh) continue;

            irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
            if (node) {
                // EQ coords (x, y, z) → Irrlicht coords (x, z, y)
                node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));

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

                regionMeshNodes_[regionBuildIndex_] = node;
                uploadMeshHardwareBuffers(node);
                builtInBatch++;

                // Bounding box should already be pre-computed on background thread
                if (regionBoundingBoxes_.find(regionBuildIndex_) == regionBoundingBoxes_.end()) {
                    regionBoundingBoxes_[regionBuildIndex_] = node->getTransformedBoundingBox();
                }
            }
            mesh->drop();
        }

        LOG_DEBUG(MOD_GRAPHICS, "Eager batch: built {} meshes (index {}/{})",
                  builtInBatch, regionBuildIndex_, bspTree->regions.size());

        if (regionBuildIndex_ >= bspTree->regions.size()) {
            LOG_INFO(MOD_GRAPHICS, "Eager mode: built {} region meshes", regionMeshNodes_.size());
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_SortSetup;
        }
        // else: stay in RegionMesh_EagerBatch, build next batch on next GREEN frame
        logAssetBuildTime("region_eager_batch", builtInBatch, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::RegionMesh_SortSetup: {
        // Enable front-to-back sorted zone drawing for PVS zones
        if (usePvsCulling_ && !regionMeshNodes_.empty()) {
#ifdef EQT_HAS_GLES2
            manualZoneDrawEnabled_ = true;
#else
            manualZoneDrawEnabled_ = (driver_->getDriverType() != irr::video::EDT_BURNINGSVIDEO);
#endif
            if (manualZoneDrawEnabled_) {
                if (smgr_ && !renderPassTimer_) {
                    renderPassTimer_ = new RenderPassTimer();
                    renderPassTimer_->setRenderer(this);
                    smgr_->setLightManager(renderPassTimer_);
                } else if (renderPassTimer_) {
                    renderPassTimer_->setRenderer(this);
                }
                // Remove zone mesh nodes from scene graph — manual draw path
                // accesses them directly via regionMeshNodes_ map, so they don't
                // need to be in the graph. This eliminates Irrlicht iterating
                // hundreds of invisible nodes during drawAll().
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getParent()) { node->grab(); node->remove(); }
                }
                if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) {
                    fallbackMeshNode_->grab(); fallbackMeshNode_->remove();
                }
                LOG_INFO(MOD_GRAPHICS, "Front-to-back zone sorting ENABLED ({} regions, nodes removed from graph)",
                         regionMeshNodes_.size());
            }
        }

        // Placeholder is redundant now that real zone meshes are rendering
        destroyZonePlaceholder();

        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::RegionMesh_InstallPortal;
        logAssetBuildTime("region_sort_setup", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::RegionMesh_InstallPortal: {
        // Install pre-built portal system from background thread (preserve existing if already installed)
        if (!portalSystem_) {
            if (pendingZoneComputations_ && pendingZoneComputations_->portalSystem) {
                portalSystem_ = std::move(pendingZoneComputations_->portalSystem);
                portalOcclusionEligible_ = pendingZoneComputations_->portalOcclusionEligible;
                portalBuildPending_ = false;
                if (portalOcclusionEligible_) {
                    LOG_INFO(MOD_GRAPHICS, "Portal occlusion installed from background thread ({} portals)",
                             portalSystem_->getData().portals.size());
                }
            } else if (zoneBspTree_ && !regionBoundingBoxes_.empty()) {
                // No pre-built portal — mark as pending for checkProgressiveLoadingComplete()
                portalBuildPending_ = true;
            }
        } else {
            LOG_INFO(MOD_GRAPHICS, "Preserving portal system ({} portals)",
                     portalSystem_->getData().portals.size());
        }

        // Build region neighbor map for door PVS culling (1-depth expansion)
        if (regionNeighbors_.empty()) {
            buildRegionNeighborMap();
        }
        if (doorManager_) {
            doorManager_->setRegionNeighbors(
                regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
        }

        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Lights_CreateNodes;
        logAssetBuildTime("region_install_portal", 0, stepStart);
        break;
    }

    // ── Lights & Objects sub-steps ───────────────────────────────────────

    case BackgroundZoneLoadPhase::Lights_CreateNodes: {
        // Create zone light scene nodes (GPU: scene node creation)
        // Clear existing zone lights
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            if (zoneLightNodes_[i]) {
                if (i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i])
                    zoneLightNodes_[i]->remove();
                zoneLightNodes_[i]->drop();
            }
        }
        zoneLightNodes_.clear();
        zoneLightPositions_.clear();
        zoneLightRegions_.clear();
        zoneLightInSceneGraph_.clear();
        zoneLightNames_.clear();
        zoneLightAnimElapsed_.clear();
        zoneLightAnimFrame_.clear();

        if (currentZone_ && !currentZone_->lights.empty()) {
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                irr::core::vector3df pos(light->x, light->z, light->y);

                irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
                    nullptr, pos,
                    irr::video::SColorf(light->r, light->g, light->b, 1.0f),
                    light->radius);

                if (lightNode) {
                    irr::video::SLight& lightData = lightNode->getLightData();
                    lightData.Type = irr::video::ELT_POINT;
                    float r = std::max(light->radius, 1.0f);
                    lightData.Attenuation = irr::core::vector3df(1.0f, 1.0f / r, 1.0f / (r * r));
                    lightNode->setVisible(false);
                    lightNode->grab();
                    lightNode->remove();  // Remove from scene graph immediately (PVS will add back visible ones)
                    zoneLightNodes_.push_back(lightNode);
                    zoneLightPositions_.push_back(pos);
                    zoneLightInSceneGraph_.push_back(false);
                    zoneLightNames_.push_back(light->name);
                    zoneLightAnimElapsed_.push_back(0.0f);
                    zoneLightAnimFrame_.push_back(light->currentFrame);
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "Created {} zone light scene nodes (all out of scene graph, pending PVS classification)",
                      zoneLightNodes_.size());
            LOG_DEBUG(MOD_GRAPHICS, "  PVS state at Lights_CreateNodes: usePvsCulling_={}, currentPvsRegion_={}, bspTree={}",
                      usePvsCulling_, currentPvsRegion_,
                      zoneBspTree_ ? std::to_string(zoneBspTree_->regions.size()) + " regions" : "null");
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Lights_InstallRegions;
        logAssetBuildTime("lights_create_nodes", zoneLightNodes_.size(), stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Lights_InstallRegions: {
        // Install pre-computed light BSP regions from background thread
        if (pendingZoneComputations_ && !pendingZoneComputations_->zoneLightRegions.empty()) {
            zoneLightRegions_ = std::move(pendingZoneComputations_->zoneLightRegions);
            size_t lightsWithRegion = 0;
            size_t lightsNoRegion = 0;
            for (auto r : zoneLightRegions_) {
                if (r != SIZE_MAX) lightsWithRegion++;
                else lightsNoRegion++;
            }
            LOG_DEBUG(MOD_GRAPHICS, "Installed pre-computed BSP regions for {} of {} zone lights ({} have SIZE_MAX)",
                      lightsWithRegion, zoneLightRegions_.size(), lightsNoRegion);
        } else if (zoneBspTree_ && !zoneBspTree_->regions.empty() && currentZone_) {
            // Fallback: compute on main thread
            size_t lightsWithRegion = 0;
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                size_t regionIdx = zoneBspTree_->findRegionIndexForPoint(light->x, light->y, light->z);
                zoneLightRegions_.push_back(regionIdx);
                if (regionIdx != SIZE_MAX) lightsWithRegion++;
            }
            LOG_DEBUG(MOD_GRAPHICS, "Computed BSP regions for {} of {} zone lights (fallback)",
                      lightsWithRegion, zoneLightRegions_.size());
        } else {
            zoneLightRegions_.resize(zoneLightNodes_.size(), SIZE_MAX);
            LOG_DEBUG(MOD_GRAPHICS, "No BSP/zone data for light regions — all set to SIZE_MAX");
        }

        // Log PVS state before culling
        LOG_DEBUG(MOD_GRAPHICS, "Lights_InstallRegions PVS state: usePvsCulling_={}, currentPvsRegion_={}, "
                  "bspTree={}, zoneLightNodes_={}, zoneLightRegions_={}",
                  usePvsCulling_, currentPvsRegion_,
                  zoneBspTree_ ? std::to_string(zoneBspTree_->regions.size()) : "null",
                  zoneLightNodes_.size(), zoneLightRegions_.size());

        // Log bitvector info for current camera region
        if (usePvsCulling_ && currentPvsRegion_ != SIZE_MAX && zoneBspTree_
            && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            auto& camRegion = zoneBspTree_->regions[currentPvsRegion_];
            if (camRegion) {
                size_t bitvecSize = camRegion->visibleRegions.size();
                size_t visCount = 0;
                for (size_t b = 0; b < bitvecSize; ++b) {
                    if (camRegion->visibleRegions[b]) visCount++;
                }
                LOG_DEBUG(MOD_GRAPHICS, "  Camera region {} bitvector: size={}, {} regions marked visible",
                          currentPvsRegion_, bitvecSize, visCount);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "  Camera region {} is null!", currentPvsRegion_);
            }
        }

        // PVS check — add PVS-visible lights to scene graph (all start out-of-graph)
        {
            size_t pvsCulled = 0;
            size_t pvsVisible = 0;
            size_t alreadyInGraph = 0;
            size_t noNode = 0;

            // Log first 20 lights with full detail
            size_t detailLogged = 0;
            for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
                if (!zoneLightNodes_[i]) { noNode++; continue; }

                size_t regionIdx = (i < zoneLightRegions_.size()) ? zoneLightRegions_[i] : SIZE_MAX;
                bool vis;
                if (detailLogged < 20) {
                    vis = isRegionPvsVisibleDebug(regionIdx, "zlight", static_cast<int>(i));
                    detailLogged++;
                } else {
                    vis = isRegionPvsVisible(regionIdx);
                }

                if (vis) {
                    if (!zoneLightInSceneGraph_[i]) {
                        smgr_->getRootSceneNode()->addChild(zoneLightNodes_[i]);
                        zoneLightInSceneGraph_[i] = true;
                    } else {
                        alreadyInGraph++;
                    }
                    pvsVisible++;
                } else {
                    if (zoneLightInSceneGraph_[i]) {
                        zoneLightNodes_[i]->remove();
                        zoneLightInSceneGraph_[i] = false;
                    }
                    pvsCulled++;
                }
            }

            LOG_DEBUG(MOD_GRAPHICS, "Lights_InstallRegions PVS result: {} PVS-visible (added to graph), {} PVS-culled (out of graph), "
                      "{} already-in-graph, {} no-node (total={})",
                      pvsVisible, pvsCulled, alreadyInGraph, noNode, zoneLightNodes_.size());

            // Log unique regions that lights are in and their PVS status
            {
                std::unordered_map<size_t, size_t> regionLightCount;
                for (size_t i = 0; i < zoneLightRegions_.size(); ++i) {
                    regionLightCount[zoneLightRegions_[i]]++;
                }
                size_t visRegionCount = 0;
                size_t hidRegionCount = 0;
                for (auto& [region, count] : regionLightCount) {
                    bool vis = isRegionPvsVisible(region);
                    if (vis) visRegionCount++;
                    else hidRegionCount++;
                    if (count > 3 || !vis) {  // Log regions with many lights or hidden regions
                        LOG_DEBUG(MOD_GRAPHICS, "  Light region {}: {} lights, PVS={}",
                                  region, count, vis ? "visible" : "HIDDEN");
                    }
                }
                LOG_DEBUG(MOD_GRAPHICS, "  {} unique regions contain lights: {} PVS-visible, {} PVS-hidden",
                          regionLightCount.size(), visRegionCount, hidRegionCount);
            }
        }

        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Objects_Install;
        logAssetBuildTime("lights_install_regions", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Objects_Install: {
        deferredObjects_.clear();

        if (pendingZoneComputations_ && !pendingZoneComputations_->prebuiltDeferredObjects.empty()) {
            // Fast path: adopt pre-built + tree-filtered deferred objects from background thread
            deferredObjects_ = std::move(pendingZoneComputations_->prebuiltDeferredObjects);
            LOG_DEBUG(MOD_GRAPHICS, "Installed {} deferred objects (pre-built on background thread)",
                      deferredObjects_.size());
        } else if (pendingZoneComputations_ && !pendingZoneComputations_->deferredObjectEntries.empty() && currentZone_) {
            // Fallback 1: tree-filter on main thread (prebuilt not available)
            for (auto& [objIdx, bspRegion] : pendingZoneComputations_->deferredObjectEntries) {
                if (objIdx >= currentZone_->objects.size()) continue;
                const auto& objInstance = currentZone_->objects[objIdx];
                if (!objInstance.geometry || !objInstance.placeable) continue;

                if (treeManager_) {
                    const std::string& objName = objInstance.placeable->getName();
                    std::string primaryTexture;
                    if (!objInstance.geometry->textureNames.empty())
                        primaryTexture = objInstance.geometry->textureNames[0];
                    if (treeManager_->isTreeObject(objName, primaryTexture)) {
                        // Only skip trees on software path — GPU path handles them as objects
                        bool hasGpuShaders = (driver_->getDriverType() != irr::video::EDT_BURNINGSVIDEO);
                        if (!hasGpuShaders)
                            continue;
                    }
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

                deferredObjects_.push_back(deferred);
            }
            LOG_DEBUG(MOD_GRAPHICS, "Installed {} deferred objects (tree-filtered on main thread)",
                      deferredObjects_.size());
        } else {
            // Fallback 2: run indexObjectMeshes() on main thread
            indexObjectMeshes();
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_ZoneBounds;
        logAssetBuildTime("objects_install", deferredObjects_.size(), stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Misc_ZoneBounds: {
        if (currentZone_ && currentZone_->geometry) {
            zoneBoundsMinX_ = currentZone_->geometry->minX;
            zoneBoundsMaxX_ = currentZone_->geometry->maxX;
            zoneBoundsMinY_ = currentZone_->geometry->minY;
            zoneBoundsMaxY_ = currentZone_->geometry->maxY;
            zoneBoundsValid_ = true;
        }
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_Fog;
        logAssetBuildTime("zone_bounds", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Misc_Fog: {
        setupFog();
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_DoorSetup;
        logAssetBuildTime("fog_setup", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::Misc_DoorSetup: {
        if (!doorManager_) {
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_ReleaseData;
            break;
        }

        // First entry: collect the list of doors to rebuild
        if (doorRebuildList_.empty() && doorRebuildIndex_ == 0) {
            // Collect all doors that need work: unbuilt doors + placeholder-swap doors
            doorManager_->getUnbuiltDoors(doorRebuildList_);
            // Also add any already-built doors still using placeholders (mesh swap path)
            for (uint8_t id = 0; id < 255; ++id) {
                const auto* door = doorManager_->getDoor(id);
                if (door && door->meshBuilt && door->usePlaceholder) {
                    doorRebuildList_.push_back(id);
                }
            }
            doorRebuildIndex_ = 0;
            if (doorRebuildList_.empty()) {
                // No doors to rebuild — advance immediately
                backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_ReleaseData;
                logAssetBuildTime("door_setup_init", 0, stepStart);
                break;
            }
            LOG_INFO(MOD_GRAPHICS, "Door rebuild: {} doors to process progressively", doorRebuildList_.size());
            logAssetBuildTime("door_setup_init", doorRebuildList_.size(), stepStart);
            break;  // Yield — start rebuilding next frame
        }

        // Progressive rebuild: one door per frame
        if (doorRebuildIndex_ < doorRebuildList_.size()) {
            uint8_t doorId = doorRebuildList_[doorRebuildIndex_];
            bool rebuilt = doorManager_->rebuildSingleDoor(doorId);
            LOG_DEBUG(MOD_GRAPHICS, "door_rebuild_one: door {} {} ({}/{})",
                      doorId, rebuilt ? "rebuilt" : "skipped",
                      doorRebuildIndex_ + 1, doorRebuildList_.size());
            logAssetBuildTime("door_rebuild_one", doorId, stepStart);
            ++doorRebuildIndex_;
            break;  // Stay in Misc_DoorSetup phase, yield to next frame
        }

        // All doors processed — log PVS summary and advance to next phase
        if (doorManager_) {
            size_t totalDoors = doorManager_->getDoorCount();
            size_t visibleDoors = 0;
            size_t hiddenDoors = 0;
            size_t noRegionDoors = 0;
            for (uint8_t id = 0; id < 255; ++id) {
                const auto* door = doorManager_->getDoor(id);
                if (!door) continue;
                if (door->bspRegion == SIZE_MAX) {
                    noRegionDoors++;
                } else if (door->pivotNode && door->pivotNode->isVisible()) {
                    visibleDoors++;
                } else {
                    hiddenDoors++;
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Door rebuild complete: {} doors processed, "
                     "{} total doors ({} visible, {} PVS-hidden, {} no-region)",
                     doorRebuildList_.size(), totalDoors, visibleDoors, hiddenDoors, noRegionDoors);
        } else {
            LOG_INFO(MOD_GRAPHICS, "Door rebuild complete: {} doors processed", doorRebuildList_.size());
        }
        doorRebuildIndex_ = 0;
        doorRebuildList_.clear();
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Misc_ReleaseData;
        break;
    }

    case BackgroundZoneLoadPhase::Misc_ReleaseData: {
        if (config_.constrainedConfig.releaseTextureDataAfterUpload && currentZone_ && !constrainedMeshCache_) {
            size_t freed = currentZone_->releaseTexturePixelData();
            LOG_INFO(MOD_GRAPHICS, "Released {:.1f}MB of texture pixel data (post-upload)",
                     freed / (1024.0f * 1024.0f));
        }
        // Clean up background computations — all data consumed
        pendingZoneComputations_.reset();
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::CollisionRebuild;
        logAssetBuildTime("release_data", 0, stepStart);
        break;
    }

    // ── Collision + Environment + Entity (unchanged) ─────────────────────

    case BackgroundZoneLoadPhase::CollisionRebuild: {
        setupMinimalZoneCollision();
        destroyZonePlaceholder();
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::EnvironmentInit;
        logAssetBuildTime("collision_rebuild", 0, stepStart);
        break;
    }

    case BackgroundZoneLoadPhase::EnvironmentInit:
        if (!deferredInitActive_) {
            deferredInitActive_ = true;
            deferredInitStep_ = DeferredInitStep::TreeConfig;
            startSimulationWorkerEarly();  // Start worker immediately with core data
            LOG_INFO(MOD_GRAPHICS, "Deferred environment init started (via background zone load)");
        }
        advanceDeferredInit();
        if (deferredInitStep_ == DeferredInitStep::Complete) {
            deferredInitActive_ = false;
            backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::EntityLoading;
        }
        break;

    case BackgroundZoneLoadPhase::EntityLoading:
        backgroundZoneLoadPhase_ = BackgroundZoneLoadPhase::Complete;
        LOG_INFO(MOD_GRAPHICS, "Background zone load pipeline complete for zone '{}'", currentZoneName_);
        break;

    default:
        break;
    }
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
        builder.setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                       zoneShader_->getMaterialTypeAlphaTest());
    }

    irr::scene::IMesh* mesh = nullptr;

    if (!currentZone_->textures.empty() && !currentZone_->geometry->textureNames.empty()) {
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

    for (const auto& entry : sortedZoneDrawList_) {
        if (!entry.node) continue;

        auto it = regionMeshNodes_.find(entry.regionIdx);
        if (it == regionMeshNodes_.end() || !it->second) continue;

        collectMeshBuffers(it->second, entry.distanceSq);
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
// Draws a portal quad as 2 triangles (for stencil write/clear).
// Portal vertices are in EQ Z-up coords, converted to Irrlicht Y-up: (x,y,z) -> (x,z,y)
void IrrlichtRenderer::drawPortalQuad(const Portal& portal) {
    irr::video::S3DVertex verts[4];
    for (int i = 0; i < 4; ++i) {
        verts[i].Pos.X = portal.vertices[i][0];
        verts[i].Pos.Y = portal.vertices[i][2];  // EQ Z -> Irrlicht Y
        verts[i].Pos.Z = portal.vertices[i][1];  // EQ Y -> Irrlicht Z
        verts[i].Color = irr::video::SColor(255, 255, 255, 255);
        verts[i].TCoords.X = 0.0f;
        verts[i].TCoords.Y = 0.0f;
        verts[i].Normal.X = portal.normalX;
        verts[i].Normal.Y = portal.normalZ;
        verts[i].Normal.Z = portal.normalY;
    }

    irr::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };

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

    driver_->drawVertexPrimitiveList(verts, 4, indices, 2,
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
    if (!currentZone_ || !currentZone_->wldLoader) return false;

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
    if (!geom || geom->vertices.empty()) return false;

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_)
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());
    if (zoneShader_ && zoneShader_->isAvailable())
        builder.setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                       zoneShader_->getMaterialTypeAlphaTest());
    if (zoneShader_ && zoneShader_->isAtlasAvailable())
        builder.setAtlasShaderMaterialTypes(zoneShader_->getMaterialTypeAtlasSolid(),
                                             zoneShader_->getMaterialTypeAtlasAlpha());

    irr::scene::IMesh* mesh = nullptr;
    if (!currentZone_->textures.empty() && !geom->textureNames.empty()) {
        if (zoneAtlas_ && zoneAtlas_->isLoaded() &&
            zoneShader_ && zoneShader_->isAtlasAvailable()) {
            mesh = builder.buildAtlasedMesh(*geom, currentZone_->textures, *zoneAtlas_);
        } else {
            mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
        }
    } else {
        mesh = builder.buildColoredMesh(*geom);
    }

    if (!mesh) return false;

    auto* node = smgr_->addMeshSceneNode(mesh);
    if (!node) { mesh->drop(); return false; }

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
    uploadMeshHardwareBuffers(node);

    // Register with animated texture manager
    if (animatedTextureManager_)
        animatedTextureManager_->addSceneNode(node);

    // Update cache
    size_t meshSize = ConstrainedMeshCache::estimateMeshSize(node);
    constrainedMeshCache_->onLoaded(regionIdx, node, meshSize);

    LOG_DEBUG(MOD_GRAPHICS, "MeshCache: built region {} ({} bytes)", regionIdx, meshSize);
    return true;
}

void IrrlichtRenderer::processFrameLazyLoad() {
    if (!constrainedMeshCache_ || !currentZone_ || !currentZone_->wldLoader) return;

    // GREEN-only: max 1 region build per frame to stay within budget.
    if (governor_ && governor_->getState() != BudgetState::Green) return;

    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
        if (rebuildRegionMesh(entry.regionIdx)) {
            LOG_DEBUG(MOD_GRAPHICS, "MeshCache: built region {} (usage {}/{} bytes)",
                entry.regionIdx,
                constrainedMeshCache_->getCurrentUsage(),
                constrainedMeshCache_->getMemoryLimit());
        }
        break;  // One region max per frame
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
            evictionsThisFrame++;
        }
    }
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
            if (!objInstance.geometry->textureNames.empty()) {
                primaryTexture = objInstance.geometry->textureNames[0];
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
        builder.setShaderMaterialTypes(zoneShader_->getMaterialTypeSolid(),
                                       zoneShader_->getMaterialTypeAlphaTest());
    }
    if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
        builder.setAtlasShaderMaterialTypes(zoneShader_->getMaterialTypeAtlasSolid(),
                                             zoneShader_->getMaterialTypeAtlasAlpha());
    }

    bool useObjAtlas = objAtlas_ && objAtlas_->isLoaded() &&
                       zoneShader_ && zoneShader_->isAtlasAvailable();

    irr::scene::IMesh* mesh = nullptr;
    if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames.empty()) {
        if (useObjAtlas) {
            mesh = builder.buildAtlasedMesh(*objInstance.geometry, currentZone_->objectTextures,
                                             *objAtlas_, objAtlasPageOffset_);
        } else {
            mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
        }
    } else {
        mesh = builder.buildColoredMesh(*objInstance.geometry);
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
        if (!objInstance.geometry->textureNames.empty()) {
            primaryTexture = objInstance.geometry->textureNames[0];
        }
        if (treeManager_->isTreeObject(objName, primaryTexture)) {
            irr::core::aabbox3df meshBbox = mesh->getBoundingBox();
            float meshMinY = meshBbox.MinEdge.Y;
            float meshMaxY = meshBbox.MaxEdge.Y;
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                    zoneShader_->getMaterialTypeWindAlphaTest());
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
    objectNodes_.push_back(node);
    objectPositions_.push_back(irr::core::vector3df(x, z, y));
    if (zoneBspTree_) {
        objectRegions_.push_back(zoneBspTree_->findRegionIndexForPoint(x, y, z));
    } else {
        objectRegions_.push_back(SIZE_MAX);
    }
    node->updateAbsolutePosition();
    objectBoundingBoxes_.push_back(node->getTransformedBoundingBox());

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
        for (const auto& texName : objInstance.geometry->textureNames) {
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
            lightNode->setVisible(false);

            ObjectLight objLight;
            objLight.node = lightNode;
            objLight.position = lightPos;
            objLight.objectName = objName;
            objLight.originalColor = lightColor;

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

            objectLights_.push_back(objLight);
            LOG_DEBUG(MOD_GRAPHICS, "Deferred object light created: {} at ({:.1f},{:.1f},{:.1f})",
                objName, lightPos.X, lightPos.Y, lightPos.Z);
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
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->registerEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse, serverSize, entityLevel);

    if (result) {
        loadedEntityCount_++;
    }

    return result;
}

void IrrlichtRenderer::updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                                     float dx, float dy, float dz, uint32_t animation) {
    if (entityRenderer_) {
        entityRenderer_->updateEntity(spawnId, x, y, z, heading, dx, dy, dz, animation);
    }
}

void IrrlichtRenderer::removeEntity(uint16_t spawnId) {
    if (entityRenderer_) {
        entityRenderer_->removeEntity(spawnId);
    }
}

void IrrlichtRenderer::startCorpseDecay(uint16_t spawnId) {
    if (entityRenderer_) {
        entityRenderer_->startCorpseDecay(spawnId);
    }
}

void IrrlichtRenderer::setEntityLight(uint16_t spawnId, uint8_t lightLevel) {
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
        // Invalidate light cache to force recalculation with new player light
        lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
        return;
    }

    // Non-player entity lights
    if (entityRenderer_) {
        entityRenderer_->setEntityLight(spawnId, lightLevel);
    }
}

void IrrlichtRenderer::clearEntities() {
    if (entityRenderer_) {
        entityRenderer_->clearEntities();
    }
}

// ============================================================================
// Entity Loading State Management
// ============================================================================

void IrrlichtRenderer::setExpectedEntityCount(size_t count) {
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
    if (doorManager_) {
        doorManager_->setDoorState(doorId, open, userInitiated);
    }
}

void IrrlichtRenderer::clearDoors() {
    if (doorManager_) {
        doorManager_->clearDoors();
    }
}

// ============================================================================
// World Object Management (for tradeskill container click detection)
// ============================================================================

void IrrlichtRenderer::addWorldObject(uint32_t dropId, float x, float y, float z,
                                       uint32_t objectType, const std::string& name) {
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
    if (entityRenderer_) {
        // Mark entity as corpse and play death animation
        entityRenderer_->markEntityAsCorpse(spawnId);
        LOG_DEBUG(MOD_ENTITY, "Entity {} marked as corpse with death animation", spawnId);
    }
}

bool IrrlichtRenderer::setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop, bool playThrough) {
    if (entityRenderer_) {
        return entityRenderer_->setEntityAnimation(spawnId, animCode, loop, playThrough);
    }
    return false;
}

void IrrlichtRenderer::setEntityPoseState(uint16_t spawnId, EntityPoseState pose) {
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
    updateZoneLightColors();
}

void IrrlichtRenderer::setVisionType(VisionType vision) {
    // Only upgrade vision, never downgrade below base
    if (vision > currentVision_) {
        currentVision_ = vision;
        LOG_INFO(MOD_GRAPHICS, "Vision upgraded to: {}",
                 currentVision_ == VisionType::Ultravision ? "Ultravision" :
                 currentVision_ == VisionType::Infravision ? "Infravision" : "Normal");
        updateZoneLightColors();
    }
}

void IrrlichtRenderer::resetVisionToBase() {
    if (currentVision_ != baseVision_) {
        currentVision_ = baseVision_;
        LOG_INFO(MOD_GRAPHICS, "Vision reset to base: {}",
                 currentVision_ == VisionType::Ultravision ? "Ultravision" :
                 currentVision_ == VisionType::Infravision ? "Infravision" : "Normal");
        updateZoneLightColors();
    }
}

void IrrlichtRenderer::updateZoneLightColors() {
    if (!currentZone_ || zoneLightNodes_.empty()) {
        return;
    }

    // Determine intensity and color shift based on vision type
    float intensity = 0.25f;  // Normal (base)
    float redShift = 0.0f;

    switch (currentVision_) {
        case VisionType::Ultravision:
            intensity = 1.0f;  // Full intensity
            redShift = 0.0f;   // Normal colors
            break;
        case VisionType::Infravision:
            intensity = 0.75f; // 75% intensity
            redShift = 0.3f;   // Shift toward red spectrum
            break;
        case VisionType::Normal:
        default:
            intensity = 0.25f; // Base intensity (25%)
            redShift = 0.0f;   // Normal colors
            break;
    }

    // Apply weather effects to zone lights (darker during storms)
    float weatherMod = 1.0f;
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        weatherMod = weatherEffects_->getAmbientLightModifier();
        intensity *= weatherMod;
    }

    // Update all zone light colors
    for (size_t i = 0; i < zoneLightNodes_.size() && i < currentZone_->lights.size(); ++i) {
        auto* node = zoneLightNodes_[i];
        const auto& light = currentZone_->lights[i];
        if (node) {
            // Apply intensity and red shift
            float r = light->r * intensity;
            float g = light->g * intensity * (1.0f - redShift * 0.5f);  // Reduce green for infravision
            float b = light->b * intensity * (1.0f - redShift);         // Reduce blue more for infravision

            // Boost red for infravision
            if (redShift > 0.0f) {
                r = std::min(1.0f, r * (1.0f + redShift));
            }

            irr::video::SLight& lightData = node->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r, g, b, 1.0f);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Updated {} zone lights: intensity={:.0f}%, redShift={:.0f}%, weatherMod={:.2f}",
              zoneLightNodes_.size(), intensity * 100.0f, redShift * 100.0f, weatherMod);
}

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
                float flicker = 0.85f + 0.10f * std::sin(objLight.flickerPhase * 6.7f)
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

void IrrlichtRenderer::refreshShaderLightColors() {
    if (!zoneShader_ || !zoneShader_->isAvailable() || activeLightNodes_.empty()) return;

    for (int i = 0; i < static_cast<int>(activeLightNodes_.size()); ++i) {
        auto* node = activeLightNodes_[i];
        if (!node) continue;
        const irr::video::SLight& ld = node->getLightData();
        if (ld.Type != irr::video::ELT_POINT) continue;
        // Use getPosition() — root-level nodes, AbsoluteTransformation may be stale
        irr::core::vector3df pos = node->getPosition();
        // Boost: player light (index 0) = 3x, zone torches = 1.5x
        float boost = (i == 0) ? 3.0f : 1.5f;
        zoneShader_->setPointLight(i,
            pos.X, pos.Y, pos.Z,
            ld.DiffuseColor.r * boost,
            ld.DiffuseColor.g * boost,
            ld.DiffuseColor.b * boost,
            ld.Attenuation.X, ld.Attenuation.Y, ld.Attenuation.Z);
    }

}

void IrrlichtRenderer::setPlayerPosition(float x, float y, float z, float heading) {
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

    // Force visibility and lighting recalculation on the next frame.
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
    forcePvsUpdate_ = true;
}

void IrrlichtRenderer::setSwimmingState(bool swimming, float swimSpeed, bool levitating) {
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

void IrrlichtRenderer::startSimulationWorkerEarly() {
    if (simulationWorker_ && simulationWorker_->isRunning()) return;  // Already running

    if (!simulationWorker_) {
        simulationWorker_ = std::make_unique<SimulationWorker>();
    }

    // Build core zone data snapshot — BSP, regions, objects, lights only.
    // Tree and vertex anim data will be registered later via update methods.
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
    zoneData.zoneLights.resize(zoneLightNodes_.size());
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        auto& zld = zoneData.zoneLights[i];
        if (i < zoneLightPositions_.size())
            zld.position = zoneLightPositions_[i];
        zld.bspRegion = (i < zoneLightRegions_.size()) ? zoneLightRegions_[i] : SIZE_MAX;
    }

    // Copy zone light node data for light selection
    zoneData.zoneLightNodes.resize(zoneLightNodes_.size());
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        auto& zlnd = zoneData.zoneLightNodes[i];
        if (i < zoneLightPositions_.size())
            zlnd.position = zoneLightPositions_[i];
        if (zoneLightNodes_[i]) {
            const auto& ld = zoneLightNodes_[i]->getLightData();
            zlnd.diffuseColor = ld.DiffuseColor;
            zlnd.radius = ld.Radius;
            zlnd.attConstant = ld.Attenuation.X;
            zlnd.attLinear = ld.Attenuation.Y;
            zlnd.attQuadratic = ld.Attenuation.Z;
        }
    }

    // Copy zone light animation data (animated torches from WLD)
    if (currentZone_) {
        for (size_t i = 0; i < currentZone_->lights.size() && i < zoneLightNodes_.size(); ++i) {
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

    // No trees or vertex anims yet — those get registered later
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

        // Entity culling config
        if (config_.constrainedConfig.enabled) {
            input.entityRenderDistance = config_.constrainedConfig.entityRenderDistance;
            input.maxVisibleEntities = config_.constrainedConfig.maxVisibleEntities;
            input.entityCullingEnabled = true;
        }

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

            for (size_t i = 0; i < zoneLightNodes_.size() && i < zoneLightPositions_.size(); ++i) {
                auto* node = zoneLightNodes_[i];
                if (!node) continue;
                const auto& pos = zoneLightPositions_[i];
                float dx = pos.X - camIrr.x, dy = pos.Y - camIrr.y, dz = pos.Z - camIrr.z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq < maxLightDistSq) {
                    auto& ld = node->getLightData();
                    float radius = ld.Radius > 0 ? ld.Radius : 30.0f;
                    pi.weatherLights.push_back({
                        glm::vec3(pos.X, pos.Y, pos.Z),
                        radius,
                        glm::vec3(
                            std::min(1.0f, ld.DiffuseColor.r * colorBoost),
                            std::min(1.0f, ld.DiffuseColor.g * colorBoost),
                            std::min(1.0f, ld.DiffuseColor.b * colorBoost)
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

        // Always log PVS state from worker (throttled to every 30 frames)
        static int simWorkerPvsLogCounter = 0;
        if (++simWorkerPvsLogCounter % 30 == 1 || currentPvsRegion_ != prevPvsRegion) {
            LOG_DEBUG(MOD_GRAPHICS, "SimWorker apply: PVS={}{} meshLoadQ={} protected={} regionVis={}",
                      currentPvsRegion_,
                      (currentPvsRegion_ != prevPvsRegion ? fmt::format(" (was {})", prevPvsRegion) : ""),
                      results->meshLoadQueue.size(), results->protectedRegions.size(),
                      results->regionVisible.size());
        }

        // Apply portal visible regions from worker
        portalVisibleRegions_ = results->portalVisibleRegions;
        if (entityRenderer_) {
            entityRenderer_->setPortalVisibleRegions(
                portalVisibleRegions_.empty() ? nullptr : &portalVisibleRegions_);
        }

        // Copy mesh load queue and protected regions for constrained mesh cache
        if (constrainedMeshCache_) {
            meshLoadQueue_.clear();
            size_t needBuildCount = 0;
            for (size_t regionIdx : results->meshLoadQueue) {
                meshLoadQueue_.push_back({regionIdx, 0.0f});
                if (!constrainedMeshCache_->isLoaded(regionIdx))
                    needBuildCount++;
            }
            // Always log when there's work to do, throttled otherwise
            static int meshLoadQLogCounter = 0;
            if (needBuildCount > 0 || ++meshLoadQLogCounter % 60 == 1) {
                LOG_DEBUG(MOD_GRAPHICS, "SimWorker meshLoadQueue: {} total, {} need building, {} loaded in cache",
                          meshLoadQueue_.size(), needBuildCount,
                          constrainedMeshCache_->getLoadedCount());
            }
            protectedRegions_.clear();
            protectedRegions_.insert(results->protectedRegions.begin(),
                                     results->protectedRegions.end());
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

    // Apply zone light visibility
    if (!results->lightVisible.empty()) {
        for (size_t i = 0; i < results->lightVisible.size() && i < zoneLightNodes_.size(); ++i) {
            auto* node = zoneLightNodes_[i];
            if (!node) continue;

            bool shouldBeVisible = results->lightVisible[i] != 0;
            bool isInScene = (i < zoneLightInSceneGraph_.size()) ? zoneLightInSceneGraph_[i] : false;

            if (shouldBeVisible && !isInScene) {
                smgr_->getRootSceneNode()->addChild(node);
                node->setVisible(true);
                if (i < zoneLightInSceneGraph_.size())
                    zoneLightInSceneGraph_[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < zoneLightInSceneGraph_.size())
                    zoneLightInSceneGraph_[i] = false;
            }
        }
    }

    // Apply light selection to shader
    if (results->activeLightCount > 0 && zoneShader_ && zoneShader_->isAvailable()) {
        activeLightNodes_.clear();
        for (int i = 0; i < results->activeLightCount; ++i) {
            const auto& sl = results->selectedLights[i];
            if (!sl.valid) continue;

            float boost = (i == 0 && sl.isPlayerLight) ? 3.0f : 1.5f;
            zoneShader_->setPointLight(i,
                sl.position.X, sl.position.Y, sl.position.Z,
                sl.diffuseColor.r * boost,
                sl.diffuseColor.g * boost,
                sl.diffuseColor.b * boost,
                sl.attConstant, sl.attLinear, sl.attQuadratic);
        }
        zoneShader_->setNumPointLights(results->activeLightCount);
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

    // Apply zone light animation colors from worker
    if (!results->zoneLightAnimColors.empty()) {
        for (const auto& alc : results->zoneLightAnimColors) {
            if (!alc.updated) continue;
            if (alc.lightIndex < zoneLightNodes_.size() && zoneLightNodes_[alc.lightIndex]) {
                auto& ld = zoneLightNodes_[alc.lightIndex]->getLightData();
                ld.DiffuseColor = irr::video::SColorf(alc.r, alc.g, alc.b, 1.0f);
            }
        }
    }

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
                // Boat collision fixup (rare)
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
    {
        size_t inGraph = 0, outGraph = 0;
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            if (!zoneLightNodes_[i]) continue;
            bool ig = (i < zoneLightInSceneGraph_.size()) ? zoneLightInSceneGraph_[i] : true;
            if (ig) inGraph++; else outGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ZONE LIGHTS: {} total, {} in-graph, {} out-of-graph",
                 zoneLightNodes_.size(), inGraph, outGraph);
    }

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
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (zoneLightNodes_[i] && i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i]) {
            otherNodes--;
            knownNodes.insert(zoneLightNodes_[i]);
        }
    }
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

    // SimulationWorker: apply results from previous frame, then post new input
    sectionStart_ = std::chrono::steady_clock::now();
    applySimulationResults();
    frameTimings_.simWorkerApply = measureSection();

    // Phase 2: Visibility
    processFrameVisibility();

    // SimulationWorker: post new input after frustum planes are updated
    postSimulationInput(deltaTime);

    // Phase 2.5: Lazy mesh loading (constrained mode) / Progressive asset loading
    if (progressiveLoadingActive_) {
        processFrameProgressiveLoad();
        frameTimings_.meshLoading = measureSection();
    } else if (constrainedMeshCache_) {
        processFrameLazyLoad();
        frameTimings_.meshLoading = measureSection();
    }

    // Poll completed icon sheets even when progressive loading is inactive
    // (spell gem icons are queued on first render and need loading regardless).
    // Background worker does disk I/O; this just moves a pointer (<0.1ms).
    if (!progressiveLoadingActive_ && windowManager_) {
        // Lazy-start worker if icons were requested outside of progressive loading
        windowManager_->getIconLoader().startWorker();
        if (windowManager_->loadOnePendingIconSheet()) {
            frameTimings_.meshLoading = measureSection();
        }
    }

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

        // Record frame in governor and log budget violations
        if (governor_) governor_->endFrame();

        float totalFrameMs = frameTimings_.totalFrame / 1000.0f;
        float budgetMs = governor_ ? governor_->getTargetFrameTimeMs() : 33.3f;

        // RED STATE: Full diagnostic dump — every section, every frame, so we can
        // trace exactly which subsystem is the budget hog and eliminate it.
        if (config_.constrainedConfig.enabled && governor_ &&
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
                {"targetBox",        frameTimings_.targetBox},
                {"particles",        frameTimings_.particles},
                {"weatherRender",    frameTimings_.weatherRender},
                {"guiDrawAll",       frameTimings_.guiDrawAll},
                {"windowManager",    frameTimings_.windowManager},
                {"endScene",         frameTimings_.endScene},
                {"postRender",       frameTimings_.postRender},
                {"simWorkerApply",   frameTimings_.simWorkerApply},
            };
            std::sort(std::begin(sections), std::end(sections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Log header with totals
            LOG_WARN(MOD_GRAPHICS, "RED STATE: {:.1f}ms / {:.1f}ms budget ({:.0f}% over) — avg {:.1f}ms — ALL LOADING HALTED",
                     totalFrameMs, budgetMs,
                     (totalFrameMs / budgetMs - 1.0f) * 100.0f,
                     governor_->getAverageFrameTimeMs());
            // Log every section that consumed >0.1ms, sorted by cost
            for (const auto& s : sections) {
                if (s.us < 100) continue;  // Skip <0.1ms noise
                LOG_WARN(MOD_GRAPHICS, "  RED: {:>20s} = {:>7.1f}ms ({:>5.1f}%)",
                         s.name, s.us / 1000.0f,
                         totalFrameMs > 0 ? (s.us / 1000.0f / totalFrameMs * 100.0f) : 0.0f);
            }
        }
        // Yellow/Green budget violation: condensed top-3 warning
        else if (config_.constrainedConfig.enabled && totalFrameMs > budgetMs * 1.2f) {
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
        frameTimingsAccum_.endScene += frameTimings_.endScene;
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
                break;
            case RA::ToggleTrainer:
                if (trainerToggleCallback_) trainerToggleCallback_();
                break;
            case RA::DoorInteract:
                if (doorManager_ && doorInteractCallback_) {
                    uint8_t doorId = doorManager_->getNearestDoor(playerX_, playerY_, playerZ_, playerHeading_);
                    if (doorId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "Door interaction: ID {}", doorId);
                        doorInteractCallback_(doorId);
                    }
                }
                break;
            case RA::WorldObjectInteract:
                if (worldObjectInteractCallback_) {
                    uint32_t objectId = getNearestWorldObject(playerX_, playerY_, playerZ_);
                    if (objectId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "World object interaction: dropId {}", objectId);
                        worldObjectInteractCallback_(objectId);
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

    // Background BSP preload — install results when ready
    if (bspPreloadThread_ && bspPreloadComplete_) {
        advanceBspPreload();
    }
    // Reset timing section so BSP preload/zone load time isn't attributed to tier2Update
    sectionStart_ = std::chrono::steady_clock::now();

    // Background zone load state machine — one step per GREEN frame
    // This supersedes the old environmentInitPending_ path when active
    if (backgroundZoneLoadPhase_ != BackgroundZoneLoadPhase::Idle &&
        backgroundZoneLoadPhase_ != BackgroundZoneLoadPhase::Complete) {
        // Loading phase polls without GREEN gate (just checking atomic bool)
        // All other phases require GREEN budget
        if (backgroundZoneLoadPhase_ == BackgroundZoneLoadPhase::Loading ||
            !governor_ || governor_->getState() == BudgetState::Green) {
            advanceBackgroundZoneLoad();
        }
    }

    // Deferred environment init — activate state machine once after game becomes playable
    // (Only used when NOT going through the background zone load pipeline)
    if (environmentInitPending_ && zoneReady_ &&
        backgroundZoneLoadPhase_ == BackgroundZoneLoadPhase::Idle) {
        environmentInitPending_ = false;
        deferredInitActive_ = true;
        deferredInitStep_ = DeferredInitStep::TreeConfig;
        startSimulationWorkerEarly();  // Start worker immediately with core data
        LOG_INFO(MOD_GRAPHICS, "Deferred environment init started (multi-frame)");
    }

    // Step through deferred init one step per GREEN frame
    // (Only runs standalone when not driven by advanceBackgroundZoneLoad)
    if (deferredInitActive_ && backgroundZoneLoadPhase_ != BackgroundZoneLoadPhase::EnvironmentInit) {
        if (!governor_ || governor_->getState() == BudgetState::Green) {
            advanceDeferredInit();
        }
    }

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
    bool runDetailTreeThisFrame = !deferredInitActive_ && runTier2_;
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

    // Track polygon count for constrained mode budget
    lastPolygonCount_ = driver_->getPrimitiveCountDrawn();

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

    // Draw selection indicator around targeted entity
#ifdef EQT_HAS_GLES2
    drawTargetOutline();
#else
    drawTargetSelectionBox();
#endif
    frameTimings_.targetBox = measureSection();

    // Render environmental particles (render every frame, update at Tier 3)
    if (particleManager_ && particleManager_->isEnabled() && zoneReady_) particleManager_->render();

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

    // Render ambient creatures (render every frame, update at Tier 3)
    if (boidsManager_ && boidsManager_->isEnabled() && zoneReady_) boidsManager_->render();
    frameTimings_.boids = measureSection();

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

    // Portal wireframe debug overlay
    if (portalDebugDraw_ && portalSystem_ && portalSystem_->hasPortals()) {
        irr::video::SMaterial portalMat;
        portalMat.Lighting = false;
        portalMat.ZBuffer = irr::video::ECFN_ALWAYS;
        portalMat.ZWriteEnable = false;
        portalMat.BackfaceCulling = false;
        portalMat.Thickness = 2.0f;
        driver_->setMaterial(portalMat);
        driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

        const auto& portalData = portalSystem_->getData();
        for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
            const auto& portal = portalData.portals[pi];

            // Cyan for portals adjacent to camera's room, orange for others
            bool isAdjacentToCamera = (portal.regionA == currentPvsRegion_ ||
                                       portal.regionB == currentPvsRegion_);
            irr::video::SColor color = isAdjacentToCamera ?
                irr::video::SColor(255, 0, 255, 255) :    // Cyan
                irr::video::SColor(255, 255, 165, 0);     // Orange

            // Convert portal vertices: EQ (x,y,z) -> Irrlicht (x,z,y)
            irr::core::vector3df v[4];
            for (int i = 0; i < 4; ++i) {
                v[i] = irr::core::vector3df(portal.vertices[i][0],
                                             portal.vertices[i][2],
                                             portal.vertices[i][1]);
            }

            // Draw quad wireframe
            driver_->draw3DLine(v[0], v[1], color);
            driver_->draw3DLine(v[1], v[2], color);
            driver_->draw3DLine(v[2], v[3], color);
            driver_->draw3DLine(v[3], v[0], color);
            // Diagonal for visibility
            driver_->draw3DLine(v[0], v[2], color);
        }
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

    // Draw entity casting bars
    if (!allUIHidden_ && entityRenderer_) entityRenderer_->renderEntityCastingBars(driver_, guienv_, camera_);
    frameTimings_.castingBars = measureSection();

    if (!allUIHidden_) {
        guienv_->drawAll();
        drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();

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

    driver_->endScene();
    frameTimings_.endScene = measureSection();

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
    if (config_.useDRM) {
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
        bool visible = !config_.showNameTags;
        config_.showNameTags = visible;
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
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: ON ({} lights)", zoneLightNodes_.size());
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
    // Note: light visibility is managed by updateObjectLights() unified light management
    // Invalidate light cache to force recalculation
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
}

void IrrlichtRenderer::togglePlayerLight() {
    debugPlayerLightEnabled_ = !debugPlayerLightEnabled_;
    // Force light recalculation
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
    LOG_INFO(MOD_GRAPHICS, "Debug: Player light {}", debugPlayerLightEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleObjectLights() {
    debugObjectLightsEnabled_ = !debugObjectLightsEnabled_;
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);
    LOG_INFO(MOD_GRAPHICS, "Debug: Object lights {}", debugObjectLightsEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleDirectionalLight() {
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

    // Clear previous to force re-logging of active lights on next update
    previousActiveLights_.clear();
    // Invalidate light cache to force recalculation
    lastLightPlayerPos_ = irr::core::vector3df(0, 0, 0);

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
    if (!usePvsCulling_ || regionMeshNodes_.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Manual zone draw not available (no PVS culling)");
        return;
    }
    manualZoneDrawEnabled_ = !manualZoneDrawEnabled_;
    if (manualZoneDrawEnabled_) {
        // Ensure render pass timer is installed
        if (smgr_ && !renderPassTimer_) {
            renderPassTimer_ = new RenderPassTimer();
            renderPassTimer_->setRenderer(this);
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

void IrrlichtRenderer::togglePortalOcclusion() {
    if (!portalOcclusionEligible_) {
        LOG_INFO(MOD_GRAPHICS, "Portal occlusion not available (no portals or too few)");
        return;
    }
    portalOcclusionEnabled_ = !portalOcclusionEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: {}",
             portalOcclusionEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::togglePortalDebugDraw() {
    portalDebugDraw_ = !portalDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Portal debug draw: {}",
             portalDebugDraw_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleStencilDebugDraw() {
    stencilDebugDraw_ = !stencilDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Stencil debug draw: {}",
             stencilDebugDraw_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::setFrameTimingEnabled(bool enabled) {
    frameTimingEnabled_ = enabled;
    if (enabled) {
        // Reset accumulators when starting
        frameTimings_ = FrameTimings();
        frameTimingsAccum_ = FrameTimings();
        frameTimingsSampleCount_ = 0;
        // Install render pass timer for per-pass breakdown of drawAll()
        if (smgr_ && !renderPassTimer_) {
            renderPassTimer_ = new RenderPassTimer();
            renderPassTimer_->setRenderer(this);
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
    if (portalSystem_ && portalSystem_->hasPortals()) {
        LOG_INFO(MOD_GRAPHICS, "    Portal Entity Cull: {} visible regions, {} entities culled",
                 portalVisibleRegions_.size(),
                 entityRenderer_ ? entityRenderer_->getPortalCulledCount() : 0);
    }
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
    LOG_INFO(MOD_GRAPHICS, "  End Scene:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.endScene), pct(frameTimingsAccum_.endScene));
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
        // Remove lights from scene graph
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            if (zoneLightNodes_[i] && i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i]) {
                zoneLightNodes_[i]->remove();
                zoneLightInSceneGraph_[i] = false;
            }
        }
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
        // Add lights back to scene graph
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            if (zoneLightNodes_[i] && i < zoneLightInSceneGraph_.size() && !zoneLightInSceneGraph_[i]) {
                smgr_->getRootSceneNode()->addChild(zoneLightNodes_[i]);
                zoneLightInSceneGraph_[i] = true;
                zoneLightNodes_[i]->setVisible(false);  // updateObjectLights will enable nearby ones
            }
        }
        if (sunLight_) sunLight_->setVisible(true);
        if (playerLightNode_) playerLightNode_->setVisible(true);
    };

    // Count nodes
    breakdown.entityCount = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
    breakdown.objectCount = static_cast<int>(objectNodes_.size());
    breakdown.doorCount = doorManager_ ? static_cast<int>(doorManager_->getDoorCount()) : 0;
    int lightCount = static_cast<int>(zoneLightNodes_.size()) + (sunLight_ ? 1 : 0) + (playerLightNode_ ? 1 : 0);

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

    // 6. Measure lights only
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (zoneLightNodes_[i]) {
            if (i < zoneLightInSceneGraph_.size() && !zoneLightInSceneGraph_[i]) {
                smgr_->getRootSceneNode()->addChild(zoneLightNodes_[i]);
                zoneLightInSceneGraph_[i] = true;
            }
            zoneLightNodes_[i]->setVisible(true);
        }
    }
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
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (zoneLightNodes_[i] && i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i]) {
            zoneLightNodes_[i]->setVisible(false);
        }
    }
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
                  (useIrrlichtCollision_ && zoneTriangleSelector_ ? "YES" : "NO"),
                  (collisionMap_ ? "LOADED" : "NONE"));

        // Determine which collision system to use
        bool useIrrlicht = useIrrlichtCollision_ && zoneTriangleSelector_ && collisionManager_;
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

            // Check horizontal collision first
            // Convert EQ coords to Irrlicht: EQ(x,y,z) -> Irr(x,z,y)
            float checkHeight = playerConfig_.collisionCheckHeight;
            irr::core::vector3df rayStart(playerX_, playerZ_ + checkHeight, playerY_);
            irr::core::vector3df rayEnd(newX, playerZ_ + checkHeight, newY);

            irr::core::vector3df hitPoint;
            irr::core::triangle3df hitTriangle;
            bool blocked = checkCollisionIrrlicht(rayStart, rayEnd, hitPoint, hitTriangle);

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
                float groundZ = findGroundZIrrlicht(newX, newY, newZ, modelYOffset);

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

                        if (checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri)) {
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
                    float groundZ = findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
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

                        if (checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri)) {
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
                    // Note: findGroundZIrrlicht returns feetZ+1000 as a "blocked" sentinel when hitting a ceiling
                    float groundZ = findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
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
                    if (!checkCollisionIrrlicht(rayStart, rayEndX, hitPoint, hitTriangle)) {
                        float groundZ = findGroundZIrrlicht(newX, playerY_, playerZ_, modelYOffset);
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
                        if (!checkCollisionIrrlicht(rayStart, rayEndY, hitPoint, hitTriangle)) {
                            float groundZ = findGroundZIrrlicht(playerX_, newY, playerZ_, modelYOffset);
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

    // Check for boat collision - boats act as elevated platforms
    float boatDeckZ = BEST_Z_INVALID;
    if (entityRenderer_) {
        boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, currentZ);
    }

    // Determine final ground Z
    // If standing on a boat deck, use the higher of boat deck or zone ground
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

    if (groundZ == BEST_Z_INVALID) {
        return currentZ;  // No ground found, keep current Z
    }

    return groundZ;
}

// --- Irrlicht-based Collision Detection (using zone mesh) ---

void IrrlichtRenderer::setupMinimalZoneCollision() {
    // Clean up old selectors
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
        if (playerRegion != SIZE_MAX &&
            (!constrainedMeshCache_ || !constrainedMeshCache_->isLoaded(playerRegion))) {
            rebuildRegionMesh(playerRegion);
            LOG_INFO(MOD_GRAPHICS, "Built player BSP region {} synchronously for deferred loading", playerRegion);
        }
    }

    // Create meta selector with just the player's region (if available)
    irr::scene::IMetaTriangleSelector* metaSelector = smgr_->createMetaTriangleSelector();
    if (!metaSelector) {
        return;
    }

    // Add player's region mesh to collision
    if (playerRegion != SIZE_MAX) {
        auto regionIt = regionMeshNodes_.find(playerRegion);
        if (regionIt != regionMeshNodes_.end() && regionIt->second && regionIt->second->getMesh()) {
            irr::scene::ITriangleSelector* regionSelector =
                smgr_->createTriangleSelector(regionIt->second->getMesh(), regionIt->second);
            if (regionSelector) {
                metaSelector->addTriangleSelector(regionSelector);
                regionSelector->drop();
            }
        }
    }

    // Also add fallback mesh if it exists
    if (fallbackMeshNode_ && fallbackMeshNode_->getMesh()) {
        irr::scene::ITriangleSelector* fallbackSelector =
            smgr_->createTriangleSelector(fallbackMeshNode_->getMesh(), fallbackMeshNode_);
        if (fallbackSelector) {
            metaSelector->addTriangleSelector(fallbackSelector);
            fallbackSelector->drop();
        }
    }

    zoneTriangleSelector_ = metaSelector;

    // Also create a terrain-only selector and populate with zone geometry
    // (DetailManager uses this for ground raycasts when generating detail objects)
    terrainOnlySelector_ = smgr_->createMetaTriangleSelector();
    if (terrainOnlySelector_) {
        auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(terrainOnlySelector_);
        if (playerRegion != SIZE_MAX) {
            auto regionIt = regionMeshNodes_.find(playerRegion);
            if (regionIt != regionMeshNodes_.end() && regionIt->second && regionIt->second->getMesh()) {
                irr::scene::ITriangleSelector* regionSelector =
                    smgr_->createTriangleSelector(regionIt->second->getMesh(), regionIt->second);
                if (regionSelector) {
                    terrainMeta->addTriangleSelector(regionSelector);
                    regionSelector->drop();
                }
            }
        }
        if (fallbackMeshNode_ && fallbackMeshNode_->getMesh()) {
            irr::scene::ITriangleSelector* fallbackSelector =
                smgr_->createTriangleSelector(fallbackMeshNode_->getMesh(), fallbackMeshNode_);
            if (fallbackSelector) {
                terrainMeta->addTriangleSelector(fallbackSelector);
                fallbackSelector->drop();
            }
        }
    }

    collisionManager_ = smgr_->getSceneCollisionManager();

    if (cameraController_ && collisionManager_ && zoneTriangleSelector_) {
        cameraController_->setCollisionManager(collisionManager_, zoneTriangleSelector_);
    }

    // Activate progressive loading
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();

    // Create background entity prep worker if not already started (may have been created
    // early in beginZoneAssetLoad() for the deferred /loadzone path).
    if (!entityPrepWorker_ && entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
        entityPrepWorker_ = std::make_unique<EntityPrepWorker>(
            entityRenderer_->getRaceModelLoader(),
            entityRenderer_->getEquipmentModelLoader());
        entityPrepWorker_->start();
        entityRenderer_->setEntityPrepWorker(entityPrepWorker_.get());
        LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker started for progressive entity loading");
    }

    // Start background icon sheet worker if not already started
    if (windowManager_) {
        windowManager_->getIconLoader().startWorker();
    }

    environmentInitPending_ = true;

    LOG_INFO(MOD_GRAPHICS, "Minimal collision setup complete, progressive loading activated");
}

size_t IrrlichtRenderer::findBspRegionForPoint(float x, float y, float z) {
    if (zoneBspTree_) {
        return zoneBspTree_->findRegionIndexForPoint(x, y, z);
    }
    return SIZE_MAX;
}

void IrrlichtRenderer::addRegionToCollision(size_t regionIdx) {
    if (!smgr_ || !zoneTriangleSelector_) return;

    auto regionIt = regionMeshNodes_.find(regionIdx);
    if (regionIt == regionMeshNodes_.end() || !regionIt->second || !regionIt->second->getMesh()) return;

    auto* metaSelector = static_cast<irr::scene::IMetaTriangleSelector*>(zoneTriangleSelector_);
    irr::scene::ITriangleSelector* selector =
        smgr_->createTriangleSelector(regionIt->second->getMesh(), regionIt->second);
    if (selector) {
        metaSelector->addTriangleSelector(selector);

        // Also add to terrain-only selector (used by DetailManager for ground raycasts)
        if (terrainOnlySelector_) {
            auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(terrainOnlySelector_);
            terrainMeta->addTriangleSelector(selector);
        }

        selector->drop();
    }
}

void IrrlichtRenderer::addDoorToCollision(uint8_t doorId) {
    if (!smgr_ || !zoneTriangleSelector_ || !doorManager_) return;

    const auto* door = doorManager_->getDoor(doorId);
    if (door && door->sceneNode && door->sceneNode->getMesh()) {
        auto* metaSelector = static_cast<irr::scene::IMetaTriangleSelector*>(zoneTriangleSelector_);
        irr::scene::ITriangleSelector* selector =
            smgr_->createTriangleSelector(door->sceneNode->getMesh(), door->sceneNode);
        if (selector) {
            metaSelector->addTriangleSelector(selector);
            selector->drop();
        }
    }
}

void IrrlichtRenderer::addObjectToCollision(size_t objIdx) {
    if (!smgr_ || !zoneTriangleSelector_) return;
    if (objIdx >= objectNodes_.size()) return;

    auto* node = objectNodes_[objIdx];
    if (node && node->getMesh()) {
        auto* metaSelector = static_cast<irr::scene::IMetaTriangleSelector*>(zoneTriangleSelector_);
        irr::scene::ITriangleSelector* selector =
            smgr_->createTriangleSelector(node->getMesh(), node);
        if (selector) {
            metaSelector->addTriangleSelector(selector);
            selector->drop();
        }
    }
}

void IrrlichtRenderer::advanceDeferredInit() {
    auto stepStart = std::chrono::steady_clock::now();
    const char* stepName = "";

    switch (deferredInitStep_) {
        case DeferredInitStep::TreeConfig:
            stepName = "tree_config";
            if (treeManager_ && currentZone_ && !currentZone_->objects.empty()) {
                treeManager_->loadConfig("", currentZoneName_);
            }
            deferredInitStep_ = DeferredInitStep::TreeInit;
            break;

        case DeferredInitStep::TreeInit:
            stepName = "tree_init";
            // On GPU path, trees are rendered as regular objects with wind shader —
            // skip CPU-side AnimatedTreeManager initialization entirely
            if (driver_->getDriverType() != irr::video::EDT_BURNINGSVIDEO) {
                LOG_INFO(MOD_GRAPHICS, "Tree wind system: GPU path (wind shader), skipping CPU tree init");
                deferredInitStep_ = DeferredInitStep::DetailZoneEnter;
            } else if (treeManager_ && currentZone_ && !currentZone_->objects.empty()) {
                if (!treeManager_->isInitializing()) {
                    // First call — start progressive init
                    treeManager_->beginInitialize(currentZone_->objects, currentZone_->objectTextures);
                }
                if (treeManager_->initializeNextBatch(4)) {
                    // All trees done — advance to next step
                    LOG_INFO(MOD_GRAPHICS, "Tree wind system: {} animated trees", treeManager_->getAnimatedTreeCount());
                    // Assign BSP regions for PVS culling
                    if (zoneBspTree_) {
                        treeManager_->assignBspRegions(zoneBspTree_);
                    }
                    registerSimulationWorkerTreeData();
                    deferredInitStep_ = DeferredInitStep::DetailZoneEnter;
                }
                // else: stay on TreeInit, process next batch next GREEN frame
            } else {
                deferredInitStep_ = DeferredInitStep::DetailZoneEnter;
            }
            break;

        case DeferredInitStep::DetailZoneEnter:
            stepName = "detail_zone_enter";
            if (detailManager_ && terrainOnlySelector_) {
                auto wldLoader = currentZone_ ? currentZone_->wldLoader : nullptr;
                auto zoneGeom = currentZone_ ? currentZone_->geometry : nullptr;
                detailManager_->onZoneEnter(currentZoneName_, terrainOnlySelector_,
                                            zoneMeshNode_, wldLoader, zoneGeom);
            }
            deferredInitStep_ = DeferredInitStep::DetailAddMeshNodes;
            break;

        case DeferredInitStep::DetailAddMeshNodes:
            stepName = "detail_mesh_nodes";
            if (detailManager_ && !regionMeshNodes_.empty()) {
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getMesh()) {
                        detailManager_->addMeshNodeForTextureLookup(node);
                    }
                }
            }
            deferredInitStep_ = DeferredInitStep::ParticleZoneEnter;
            break;

        case DeferredInitStep::ParticleZoneEnter:
            stepName = "particle_zone_enter";
            if (particleManager_) {
                auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
                particleManager_->onZoneEnter(currentZoneName_, biome);
            }
            deferredInitStep_ = DeferredInitStep::ParticleFireSetup;
            break;

        case DeferredInitStep::ParticleFireSetup: {
            stepName = "particle_fire_setup";
            if (particleManager_) {
                // Collect fire sources from object lights and zone lights
                std::vector<glm::vec3> fireSources;
                std::vector<float> fireRadii;
                for (const auto& objLight : objectLights_) {
                    if (objLight.isFireSource) {
                        fireSources.emplace_back(objLight.position.X, objLight.position.Z, objLight.position.Y);
                        fireRadii.push_back(objLight.node ? objLight.node->getRadius() : 120.0f);
                    }
                }
                if (currentZone_) {
                    for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                        const auto& zl = currentZone_->lights[i];
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
                LOG_INFO(MOD_GRAPHICS, "Fire sources: {} from objectLights, {} total (zone has {} zone lights)",
                         std::count_if(objectLights_.begin(), objectLights_.end(),
                                       [](const ObjectLight& ol) { return ol.isFireSource; }),
                         fireSources.size(),
                         currentZone_ ? currentZone_->lights.size() : 0);
                if (!fireSources.empty()) {
                    particleManager_->setFireSources(fireSources);
                }
                if (detailManager_ && detailManager_->hasSurfaceMap()) {
                    particleManager_->setSurfaceMap(detailManager_->getSurfaceMap());
                }
            }
            deferredInitStep_ = DeferredInitStep::ParticleUnifiedInit;
            break;
        }

        case DeferredInitStep::ParticleUnifiedInit: {
            stepName = "particle_unified_init";
            if (particleManager_) {
                // initUnifiedRenderer must be called before createFireEmitters
                particleManager_->initUnifiedRenderer();
                // Re-collect fire sources for createFireEmitters (cheap — just iterating vectors)
                std::vector<glm::vec3> fireSources;
                std::vector<float> fireRadii;
                for (const auto& objLight : objectLights_) {
                    if (objLight.isFireSource) {
                        fireSources.emplace_back(objLight.position.X, objLight.position.Z, objLight.position.Y);
                        fireRadii.push_back(objLight.node ? objLight.node->getRadius() : 120.0f);
                    }
                }
                if (currentZone_) {
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
                if (!fireSources.empty()) {
                    particleManager_->createFireEmitters(fireSources, fireRadii);
                }
            }
            deferredInitStep_ = DeferredInitStep::BoidsInit;
            break;
        }

        case DeferredInitStep::BoidsInit:
            stepName = "boids_init";
            if (boidsManager_) {
                auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
                if (zoneBoundsValid_) {
                    boidsManager_->onZoneEnter(currentZoneName_, biome,
                        glm::vec3(zoneBoundsMinX_, zoneBoundsMinY_, -1000.0f),
                        glm::vec3(zoneBoundsMaxX_, zoneBoundsMaxY_, 1000.0f));
                } else {
                    boidsManager_->onZoneEnter(currentZoneName_, biome);
                }
            }
            deferredInitStep_ = DeferredInitStep::TumbleweedInit;
            break;

        case DeferredInitStep::TumbleweedInit:
            stepName = "tumbleweed_init";
            if (tumbleweedManager_) {
                auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
                tumbleweedManager_->onZoneEnter(currentZoneName_, biome);
            }
            deferredInitStep_ = DeferredInitStep::WeatherSurface;
            break;

        case DeferredInitStep::WeatherSurface:
            stepName = "weather_settings";
            if (weatherEffects_ && detailManager_ && detailManager_->hasSurfaceMap())
                weatherEffects_->setSurfaceMap(detailManager_->getSurfaceMap());
            applyEnvironmentalDisplaySettings();
            deferredInitStep_ = DeferredInitStep::ReleaseGeometry;
            break;

        case DeferredInitStep::ReleaseGeometry:
            stepName = "release_geometry";
            // Release combined zone geometry vectors (only when detail system is disabled)
            if (currentZone_ && currentZone_->geometry && !detailManager_) {
                size_t before = currentZone_->geometry->getMemoryUsage();
                currentZone_->geometry->vertices.clear();
                currentZone_->geometry->vertices.shrink_to_fit();
                currentZone_->geometry->triangles.clear();
                currentZone_->geometry->triangles.shrink_to_fit();
                size_t after = currentZone_->geometry->getMemoryUsage();
                LOG_INFO(MOD_GRAPHICS, "Released combined zone geometry vectors: {:.1f} MB freed",
                         (before - after) / (1024.0f * 1024.0f));
            }
            // Release raw texture data from model loaders (only if progressive loading is done)
            if (config_.constrainedConfig.releaseTextureDataAfterUpload && entityRenderer_
                && !progressiveLoadingActive_) {
                size_t totalFreed = 0;
                if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                    totalFreed += eml->releaseRawTextureData();
                }
                if (auto* rml = entityRenderer_->getRaceModelLoader()) {
                    totalFreed += rml->releaseRawTextureData();
                }
                if (totalFreed > 0) {
                    LOG_INFO(MOD_GRAPHICS, "Released {:.1f}MB of equipment/character texture data (post-upload)",
                             totalFreed / (1024.0f * 1024.0f));
                }
            }
            // Final governor reset — clean state for progressive loading
            if (governor_) {
                governor_->requestReset();
                LOG_DEBUG(MOD_GRAPHICS, "Governor reset requested (deferred init complete)");
            }
            // Register vertex anim data with worker (flags, banners — set up by Objects_Install)
            registerSimulationWorkerVertexAnimData();
            deferredInitStep_ = DeferredInitStep::Complete;
            deferredInitActive_ = false;
            LOG_INFO(MOD_GRAPHICS, "Deferred environment init complete for zone '{}'", currentZoneName_);
            break;

        case DeferredInitStep::Complete:
            deferredInitActive_ = false;
            break;
    }

    logAssetBuildTime(stepName, 0, stepStart);
}

void IrrlichtRenderer::processFrameProgressiveLoad() {
    if (!progressiveLoadingActive_) return;

    // Promote background-preloaded model data to main cache.
    // This moves RaceModelData from the staging map to loadedModels_ so that
    // getMeshForRace() can find it. Must happen before any buildEntityMesh() calls.
    if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->promotePreparedModels();
    }

    // --- Critical tasks: always proceed regardless of governor state ---

    // P1: Player's BSP region (must have ground to stand on)
    if (constrainedMeshCache_ && currentPvsRegion_ != SIZE_MAX &&
        !constrainedMeshCache_->isLoaded(currentPvsRegion_)) {
        rebuildRegionMesh(currentPvsRegion_);
        addRegionToCollision(currentPvsRegion_);
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: built player region {} (Critical)",
            currentPvsRegion_);
    }

    // Player entity goes through processOneEntityBuildStep() like other entities
    // (no synchronous build here — avoids 7.6s render thread stall from disk I/O)

    // --- GREEN gate: exactly ONE non-critical step per frame when GREEN ---
    if (governor_ && governor_->getState() != BudgetState::Green) {
        // Queue background prep even when not GREEN (free work)
        queueEntityPrepRequests();
        checkProgressiveLoadingComplete();
        return;
    }

    bool didWork = false;
    auto stepStart = std::chrono::steady_clock::now();

    // Priority 0.5: Load one pending icon sheet (spell/item TGA from disk, ~40-80ms)
    if (!didWork && windowManager_) {
        if (windowManager_->loadOnePendingIconSheet()) {
            didWork = true;
            logAssetBuildTime("icon_sheet", 0, stepStart);
        }
    }

    // Priority 1: Process one entity build step (most visible missing asset)
    if (!didWork && entityRenderer_) {
        // Poll completed prep results and distribute per-entity data
        entityRenderer_->pollAndDistributePrepResults();

        if (entityRenderer_->processOneEntityBuildStep()) {
            didWork = true;
            logAssetBuildTime("entity_step", 0, stepStart);
        }
    }

    // Priority 2: Build one PVS neighbor region
    if (!didWork && constrainedMeshCache_) {
        for (const auto& entry : meshLoadQueue_) {
            if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
            if (rebuildRegionMesh(entry.regionIdx)) {
                addRegionToCollision(entry.regionIdx);
                LOG_DEBUG(MOD_GRAPHICS, "Progressive: built region {} + collision (PVS={}, queue={})",
                          entry.regionIdx, currentPvsRegion_, meshLoadQueue_.size());
                didWork = true;
                logAssetBuildTime("region", entry.regionIdx, stepStart);
            }
            break;  // One attempt max
        }
    }

    // Priority 3: Build one door in current PVS set
    if (!didWork && doorManager_) {
        std::vector<uint8_t> pvsDoors;
        doorManager_->getDoorsInRegions(protectedRegions_, pvsDoors);
        for (auto doorId : pvsDoors) {
            if (doorManager_->isDoorMeshBuilt(doorId)) continue;
            // Use rebuildSingleDoor to properly clean up placeholder nodes
            // before building real mesh (direct buildDoorMesh leaks placeholders)
            doorManager_->rebuildSingleDoor(doorId);
            addDoorToCollision(doorId);
            didWork = true;
            logAssetBuildTime("door", doorId, stepStart);
            break;  // One door max
        }
    }

    // Priority 4: Build one PVS object
    if (!didWork) {
        for (size_t i = 0; i < deferredObjects_.size(); ++i) {
            if (deferredObjects_[i].meshBuilt) continue;
            if (protectedRegions_.count(deferredObjects_[i].bspRegion) == 0) continue;
            buildDeferredObject(i);
            addObjectToCollision(objectNodes_.size() - 1);
            didWork = true;
            logAssetBuildTime("pvs_object", i, stepStart);
            break;  // One object max
        }
    }

    // Priority 5: Build one non-PVS object
    if (!didWork) {
        for (size_t i = 0; i < deferredObjects_.size(); ++i) {
            if (deferredObjects_[i].meshBuilt) continue;
            buildDeferredObject(i);
            addObjectToCollision(objectNodes_.size() - 1);
            didWork = true;
            logAssetBuildTime("object", i, stepStart);
            break;  // One object max
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
            evictionsThisFrame++;
        }
    }

    if (didWork) {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: 1 step this frame ({:.1f}ms remaining, gov=GREEN)",
            governor_ ? governor_->getRemainingBudgetMs() : 0.0f);
    }

    checkProgressiveLoadingComplete();
}

void IrrlichtRenderer::queueEntityPrepRequests() {
    if (!entityPrepWorker_ || !entityRenderer_ || !entityPrepReady_) return;

    std::vector<uint16_t> unbuilt;
    entityRenderer_->getUnbuiltEntities(unbuilt);
    if (unbuilt.empty()) return;

    const auto& entities = entityRenderer_->getEntities();
    for (auto spawnId : unbuilt) {
        if (entityRenderer_->isEntityMeshBuilt(spawnId)) continue;
        auto it = entities.find(spawnId);
        if (it == entities.end()) continue;
        const auto& vis = it->second;
        // Don't re-queue entities whose background prep already completed
        if (vis.entityPrepComplete) continue;
        // Per-entity dedup: each entity gets its own prep job (equipment/variant work differs)
        if (!entityPrepWorker_->isPendingForEntity(spawnId)) {
            entityPrepWorker_->requestPrep({spawnId, vis.raceId, vis.gender, vis.appearance});
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

void IrrlichtRenderer::checkProgressiveLoadingComplete() {
    if (!progressiveLoadingActive_) return;

    // Count unbuilt assets
    size_t unbuiltEntities = 0;
    size_t unbuiltDoors = 0;
    size_t unbuiltObjects = 0;

    if (entityRenderer_) {
        std::vector<uint16_t> unbuilt;
        entityRenderer_->getUnbuiltEntities(unbuilt);
        unbuiltEntities = unbuilt.size();
    }

    if (doorManager_) {
        std::vector<uint8_t> unbuilt;
        doorManager_->getUnbuiltDoors(unbuilt);
        unbuiltDoors = unbuilt.size();
    }

    for (const auto& obj : deferredObjects_) {
        if (!obj.meshBuilt) unbuiltObjects++;
    }

    if (unbuiltEntities == 0 && unbuiltDoors == 0 && unbuiltObjects == 0) {
        progressiveLoadingActive_ = false;

        // Stop background entity prep worker — all entities are built
        if (entityPrepWorker_) {
            entityPrepWorker_->stop();
            entityPrepWorker_.reset();
            if (entityRenderer_) {
                entityRenderer_->setEntityPrepWorker(nullptr);
            }
            LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker stopped — all entities loaded");
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
            portalSystem_ = std::make_unique<PortalSystem>();
            portalSystem_->buildFromBsp(*zoneBspTree_, regionBoundingBoxes_);
            portalOcclusionEligible_ = portalSystem_->hasPortals() &&
                                       (portalSystem_->getData().portals.size() > 10);
            if (portalOcclusionEligible_) {
                LOG_INFO(MOD_GRAPHICS, "Portal occlusion eligible: {} portals (post-load build)",
                         portalSystem_->getData().portals.size());
            }
            portalBuildPending_ = false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - progressiveLoadStartTime_).count();
        LOG_INFO(MOD_GRAPHICS, "Progressive loading complete: {}ms total streaming time", elapsed);

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
    if (!collisionManager_ || !zoneTriangleSelector_) {
        return false;  // No collision system = no collision
    }

    irr::core::line3df ray(start, end);
    irr::core::vector3df outCollisionPoint;
    irr::core::triangle3df outTriangle;
    irr::scene::ISceneNode* outNode = nullptr;

    bool hit = collisionManager_->getCollisionPoint(ray, zoneTriangleSelector_,
                                                      outCollisionPoint, outTriangle, outNode);

    if (hit) {
        hitPoint = outCollisionPoint;
        hitTriangle = outTriangle;
    }

    return hit;
}

float IrrlichtRenderer::findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset) {
    if (!collisionManager_ || !zoneTriangleSelector_) {
        return currentZ - modelYOffset;  // Return current feet position
    }

    // currentZ is the model center (server Z), modelYOffset is the POSITIVE distance from center to feet
    // Feet position = currentZ - modelYOffset (feet are BELOW center)
    // Head position = currentZ + modelYOffset (approximately, assuming symmetric model)
    float feetZ = currentZ - modelYOffset;
    float headZ = currentZ + modelYOffset;  // Approximate head position (mirror of feet offset)
    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // Irrlicht coords: (x, y, z) where Y is up
    // Input is EQ coords where Z is up, so we convert: EQ(x,y,z) -> Irr(x,z,y)

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;

    // PHASE 1: Short raycast to find ground near current level
    // This prevents falling through mesh gaps by looking for nearby ground first
    irr::core::vector3df nearStart(x, feetZ + maxStepUp, y);  // Start at max step-up above feet
    irr::core::vector3df nearEnd(x, feetZ - maxStepDown, y);  // Look down to max step-down below feet

    bool nearHit = checkCollisionIrrlicht(nearStart, nearEnd, hitPoint, hitTriangle);

    if (nearHit) {
        float floorZ = hitPoint.Y;
        // Check if this is valid ground (not a ceiling)
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            // Found nearby ground - use it
            if (playerConfig_.collisionDebug) {
                // Green line showing nearby ground hit (preferred)
                addCollisionDebugLine(nearStart, hitPoint, irr::video::SColor(255, 0, 255, 128), 0.2f);
                float markerSize = 0.5f;
                addCollisionDebugLine(
                    irr::core::vector3df(hitPoint.X - markerSize, hitPoint.Y, hitPoint.Z),
                    irr::core::vector3df(hitPoint.X + markerSize, hitPoint.Y, hitPoint.Z),
                    irr::video::SColor(255, 0, 255, 128), 0.2f);
            }
            return floorZ;
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    // This handles cases like jumping off ledges, falling, etc.
    irr::core::vector3df rayStart(x, headZ + 2.0f, y);  // Start slightly above head
    irr::core::vector3df rayEnd(x, feetZ - 500.0f, y);  // Cast down far below feet

    bool hit = checkCollisionIrrlicht(rayStart, rayEnd, hitPoint, hitTriangle);

    // Debug visualization
    if (playerConfig_.collisionDebug) {
        if (hit) {
            float floorZ = hitPoint.Y;
            // Valid floor: at or below feet + step height, and not above head (ceiling)
            bool validFloor = (floorZ <= feetZ + maxStepUp + 0.1f);
            bool isCeiling = (floorZ > feetZ + maxStepUp + 0.1f);

            if (validFloor && !isCeiling) {
                // Cyan line showing valid ground ray hit
                addCollisionDebugLine(rayStart, hitPoint, irr::video::SColor(255, 0, 255, 255), 0.2f);

                // Small cyan cross at ground point
                float markerSize = 0.5f;
                addCollisionDebugLine(
                    irr::core::vector3df(hitPoint.X - markerSize, hitPoint.Y, hitPoint.Z),
                    irr::core::vector3df(hitPoint.X + markerSize, hitPoint.Y, hitPoint.Z),
                    irr::video::SColor(255, 0, 255, 255), 0.2f);
                addCollisionDebugLine(
                    irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z - markerSize),
                    irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z + markerSize),
                    irr::video::SColor(255, 0, 255, 255), 0.2f);
            } else {
                // Orange line showing ceiling/obstruction hit (player won't fit)
                addCollisionDebugLine(rayStart, hitPoint, irr::video::SColor(255, 255, 165, 0), 0.2f);
                LOG_TRACE(MOD_MOVEMENT, "Ray hit obstruction at {} (head at {}, feet at {})", floorZ, headZ, feetZ);
            }
        } else {
            // Magenta line showing no ground found
            addCollisionDebugLine(rayStart, rayEnd, irr::video::SColor(255, 255, 0, 255), 0.2f);
        }
    }

    float groundZ = feetZ;  // Default to current feet position
    if (hit) {
        float floorZ = hitPoint.Y;

        // Only accept as valid ground if:
        // 1. It's at or below our feet + step height (we can step up to it)
        // 2. It's not a ceiling (above our feet + step tolerance)
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            groundZ = floorZ;
        } else {
            // Hit a ceiling/obstruction - player can't fit, block movement
            // Return an invalid value to signal blocked (return as if ground is way above)
            return feetZ + 1000.0f;
        }
    }

    // Check for boat collision - boats act as elevated platforms
    // This uses feetZ (currentZ - modelYOffset) as the player position
    if (entityRenderer_) {
        float boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, feetZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            // Use the higher of boat deck or zone ground
            if (boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    // Yellow line showing boat deck
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

// updateNameTagsWithLOS removed — name tag visibility now computed by SimulationWorker

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
                } else if (ctrlHeld && visual.isNPC && !visual.isCorpse) {
                    // Ctrl+click on NPC - banker interaction
                    LOG_INFO(MOD_GRAPHICS, "Ctrl+click on NPC: {} (ID: {})", visual.name, targetId);
                    if (bankerInteractCallback_) {
                        bankerInteractCallback_(targetId);
                    }
                } else {
                    // Entity is visible - set as target
                    LOG_INFO(MOD_GRAPHICS, "Target selected: {} (ID: {})", visual.name, targetId);

                    // Invoke callback to notify EverQuest class
                    if (targetCallback_) {
                        targetCallback_(targetId);
                    }
                }
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "Cannot target {} - obstructed", visual.name);
            }
        }
    } else {
        // No entity found - check for door click
        bool handledClick = false;
        if (doorManager_ && doorInteractCallback_) {
            uint8_t doorId = doorManager_->getDoorAtScreenPos(clickX, clickY, camera_, collisionManager_);
            if (doorId != 0) {
                LOG_INFO(MOD_GRAPHICS, "Door clicked: ID {}", doorId);
                doorInteractCallback_(doorId);
                handledClick = true;
            }
        }

        // Check for world object (tradeskill container) click
        if (!handledClick && worldObjectInteractCallback_) {
            uint32_t objectId = getWorldObjectAtScreenPos(clickX, clickY);
            if (objectId != 0) {
                LOG_INFO(MOD_GRAPHICS, "World object clicked: dropId {}", objectId);
                worldObjectInteractCallback_(objectId);
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
    // If we don't have collision detection, assume visible
    if (!collisionManager_ || !zoneTriangleSelector_) {
        return true;
    }

    // Create ray from camera to entity
    irr::core::line3df ray(cameraPos, entityPos);

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    irr::scene::ISceneNode* outNode = nullptr;

    // Check if ray hits zone geometry before reaching entity
    bool hit = collisionManager_->getCollisionPoint(ray, zoneTriangleSelector_,
                                                     hitPoint, hitTriangle, outNode);

    if (!hit) {
        // No obstruction - entity is visible
        return true;
    }

    // Check if hit point is closer than entity
    float hitDist = cameraPos.getDistanceFrom(hitPoint);
    float entityDist = cameraPos.getDistanceFrom(entityPos);

    // Add a small margin to account for bounding box size
    return hitDist > (entityDist - 10.0f);
}

// --- Inventory UI Methods ---

void IrrlichtRenderer::setInventoryManager(eqt::inventory::InventoryManager* manager) {
    inventoryManager_ = manager;

    // Create window manager if not already created
    if (!windowManager_ && inventoryManager_ && driver_ && guienv_) {
        windowManager_ = std::make_unique<eqt::ui::WindowManager>();
        windowManager_->init(driver_, guienv_, inventoryManager_, config_.width, config_.height, config_.eqClientPath);

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
    if (windowManager_) {
        windowManager_->openInventory();
    }
}

void IrrlichtRenderer::closeInventory() {
    if (windowManager_) {
        windowManager_->closeInventory();
    }
}

void IrrlichtRenderer::showNoteWindow(const std::string& text, uint8_t type) {
    if (windowManager_) {
        windowManager_->showNoteWindow(text, type);
    }
}

bool IrrlichtRenderer::isInventoryOpen() const {
    return windowManager_ && windowManager_->isInventoryOpen();
}

void IrrlichtRenderer::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    if (windowManager_) {
        windowManager_->setCharacterInfo(name, level, className);
    }
}

void IrrlichtRenderer::setCharacterDeity(const std::wstring& deity) {
    if (windowManager_) {
        windowManager_->setCharacterDeity(deity);
    }
}

void IrrlichtRenderer::setExpProgress(float progress) {
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
    if (windowManager_) {
        windowManager_->updateCharacterStats(curHp, maxHp, curMana, maxMana, curEnd, maxEnd,
                                             ac, atk, str, sta, agi, dex, wis, intel, cha,
                                             pr, mr, dr, fr, cr, weight, maxWeight,
                                             platinum, gold, silver, copper);
    }
}

void IrrlichtRenderer::updatePlayerAppearance(uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
    LOG_DEBUG(MOD_GRAPHICS, "IrrlichtRenderer::updatePlayerAppearance race={} gender={}", raceId, gender);
    if (windowManager_) {
        windowManager_->setPlayerAppearance(raceId, gender, appearance);
    }
}

void IrrlichtRenderer::updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
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
    if (entityRenderer_) {
        entityRenderer_->queueReceivedDamageAnimation(spawnId);
    }
}

void IrrlichtRenderer::queueSkillAnimation(uint16_t spawnId, const std::string& animCode) {
    if (entityRenderer_) {
        entityRenderer_->queueSkillAnimation(spawnId, animCode);
    }
}

void IrrlichtRenderer::triggerFirstPersonAttack() {
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
        size_t lightCount = zoneLightNodes_.size();
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
        size_t combinedGeomBytes = currentZone_->geometry ? currentZone_->geometry->getMemoryUsage() : 0;
        size_t wldBytes = currentZone_->wldLoader ? currentZone_->wldLoader->getMemoryUsage() : 0;
        size_t objGeomBytes = 0;
        for (const auto& [name, geom] : currentZone_->objectGeometries)
            if (geom) objGeomBytes += geom->getMemoryUsage();

        lines.push_back(fmt::format("[S3D Zone Source Data] {} (combined geom {}, WLD {}, obj geom {})",
            formatBytes(zoneSourceBytes), formatBytes(combinedGeomBytes),
            formatBytes(wldBytes), formatBytes(objGeomBytes)));
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
        int colorBpp = (isConstrainedMode() && config_.constrainedConfig.colorDepthBits == 16) ? 16 : 32;
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

} // namespace Graphics
} // namespace EQT
