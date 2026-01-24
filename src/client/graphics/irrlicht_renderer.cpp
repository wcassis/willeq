#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
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
#include "client/zone_lines.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <cmath>
#include <limits>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EQT {
namespace Graphics {

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

            // Step sizes for delta-based adjustments
            float uvStep = event.KeyInput.Shift ? 0.01f : 0.1f;
            float scaleStep = event.KeyInput.Shift ? 0.01f : 0.1f;
            float rotStep = event.KeyInput.Shift ? 1.0f : 15.0f;
            float repairRotStep = 15.0f;

            // Look up action from HotkeyManager
            auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
            auto action = hotkeyMgr.getAction(
                event.KeyInput.Key,
                event.KeyInput.Control,
                event.KeyInput.Shift,
                false,  // Alt not directly exposed by Irrlicht
                currentMode_);

            // Debug: log key press and action lookup
            LOG_DEBUG(MOD_INPUT, "Key pressed: {} (ctrl={}, shift={}), mode={}, action={}",
                hotkeyMgr.keyCodeToName(event.KeyInput.Key),
                event.KeyInput.Control, event.KeyInput.Shift,
                hotkeyMgr.modeEnumToName(currentMode_),
                action.has_value() ? hotkeyMgr.actionEnumToName(*action) : "none");

            if (action.has_value()) {
                using HA = eqt::input::HotkeyAction;
                switch (*action) {
                    // === Global Actions ===
                    case HA::Quit: quitRequested_ = true; break;
                    case HA::Screenshot: screenshotRequested_ = true; break;
                    case HA::ToggleWireframe: wireframeToggleRequested_ = true; break;
                    case HA::ToggleHUD: hudToggleRequested_ = true; break;
                    case HA::ToggleNameTags: nameTagToggleRequested_ = true; break;
                    case HA::ToggleZoneLights: zoneLightsToggleRequested_ = true; break;
                    case HA::ToggleCameraMode: cameraModeToggleRequested_ = true; break;
                    case HA::ToggleOldModels: oldModelsToggleRequested_ = true; break;
                    case HA::ToggleRendererMode: rendererModeToggleRequested_ = true; break;

                    // === Player Mode Actions ===
                    case HA::ToggleAutorun: autorunToggleRequested_ = true; break;
                    case HA::ToggleAutoAttack: autoAttackToggleRequested_ = true; break;
                    case HA::ToggleInventory: inventoryToggleRequested_ = true; break;
                    case HA::ToggleSkills: skillsToggleRequested_ = true; break;
                    case HA::ToggleGroup: groupToggleRequested_ = true; break;
                    case HA::TogglePetWindow: petToggleRequested_ = true; break;
                    case HA::ToggleSpellbook: spellbookToggleRequested_ = true; break;
                    case HA::ToggleBuffWindow: buffWindowToggleRequested_ = true; break;
                    case HA::ToggleOptions: optionsToggleRequested_ = true; break;
                    case HA::ToggleVendor: vendorToggleRequested_ = true; break;
                    case HA::ToggleTrainer: trainerToggleRequested_ = true; break;
                    case HA::ToggleCollision: collisionToggleRequested_ = true; break;
                    case HA::ToggleCollisionDebug: collisionDebugToggleRequested_ = true; break;
                    case HA::ToggleZoneLineVisualization: zoneLineVisualizationToggleRequested_ = true; break;
                    case HA::CycleObjectLights: cycleObjectLightsRequested_ = true; break;
                    case HA::Interact:  // Generic interact - tries door first, then world object
                        doorInteractRequested_ = true;
                        worldObjectInteractRequested_ = true;
                        break;
                    case HA::InteractDoor: doorInteractRequested_ = true; break;
                    case HA::InteractWorldObject: worldObjectInteractRequested_ = true; break;
                    case HA::Hail: hailRequested_ = true; break;
                    case HA::Consider: considerRequested_ = true; break;
                    case HA::ClearTarget: clearTargetRequested_ = true; break;

                    // Targeting
                    case HA::TargetSelf:
                        LOG_DEBUG(MOD_INPUT, "Setting targetSelfRequested_ = true");
                        targetSelfRequested_ = true;
                        break;
                    case HA::TargetGroupMember1:
                        LOG_DEBUG(MOD_INPUT, "Setting targetGroupMember1Requested_ = true");
                        targetGroupMember1Requested_ = true;
                        break;
                    case HA::TargetGroupMember2:
                        LOG_DEBUG(MOD_INPUT, "Setting targetGroupMember2Requested_ = true");
                        targetGroupMember2Requested_ = true;
                        break;
                    case HA::TargetGroupMember3:
                        LOG_DEBUG(MOD_INPUT, "Setting targetGroupMember3Requested_ = true");
                        targetGroupMember3Requested_ = true;
                        break;
                    case HA::TargetGroupMember4:
                        LOG_DEBUG(MOD_INPUT, "Setting targetGroupMember4Requested_ = true");
                        targetGroupMember4Requested_ = true;
                        break;
                    case HA::TargetGroupMember5:
                        LOG_DEBUG(MOD_INPUT, "Setting targetGroupMember5Requested_ = true");
                        targetGroupMember5Requested_ = true;
                        break;
                    case HA::TargetNearestPC:
                        LOG_DEBUG(MOD_INPUT, "Setting targetNearestPCRequested_ = true");
                        targetNearestPCRequested_ = true;
                        break;
                    case HA::TargetNearestNPC:
                        LOG_DEBUG(MOD_INPUT, "Setting targetNearestNPCRequested_ = true");
                        targetNearestNPCRequested_ = true;
                        break;
                    case HA::CycleTargets:
                        LOG_DEBUG(MOD_INPUT, "Setting cycleTargetsRequested_ = true");
                        cycleTargetsRequested_ = true;
                        break;
                    case HA::CycleTargetsReverse:
                        LOG_DEBUG(MOD_INPUT, "Setting cycleTargetsReverseRequested_ = true");
                        cycleTargetsReverseRequested_ = true;
                        break;

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

                    // === Admin Mode Actions ===
                    case HA::SaveEntities: saveEntitiesRequested_ = true; break;
                    case HA::ToggleLighting: lightingToggleRequested_ = true; break;
                    case HA::ToggleHelmDebug: helmDebugToggleRequested_ = true; break;
                    case HA::HelmPrintState: helmPrintStateRequested_ = true; break;
                    case HA::AnimSpeedDecrease: animSpeedDelta_ = -0.1f; break;
                    case HA::AnimSpeedIncrease: animSpeedDelta_ = 0.1f; break;
                    case HA::AmbientLightDecrease:
                        ambientLightDelta_ = event.KeyInput.Shift ? -0.01f : -0.05f;
                        break;
                    case HA::AmbientLightIncrease:
                        ambientLightDelta_ = event.KeyInput.Shift ? 0.01f : 0.05f;
                        break;
                    case HA::CorpseZOffsetUp: corpseZOffsetDelta_ = 1.0f; break;
                    case HA::CorpseZOffsetDown: corpseZOffsetDelta_ = -1.0f; break;
                    case HA::EyeHeightUp: eyeHeightDelta_ = 1.0f; break;
                    case HA::EyeHeightDown: eyeHeightDelta_ = -1.0f; break;
                    case HA::ParticleMultiplierDecrease:
                        particleMultiplierDelta_ = event.KeyInput.Shift ? -0.1f : -0.5f;
                        break;
                    case HA::ParticleMultiplierIncrease:
                        particleMultiplierDelta_ = event.KeyInput.Shift ? 0.1f : 0.5f;
                        break;
                    case HA::DetailDensityDecrease:
                        detailDensityDelta_ = event.KeyInput.Shift ? -0.05f : -0.1f;
                        break;
                    case HA::DetailDensityIncrease:
                        detailDensityDelta_ = event.KeyInput.Shift ? 0.05f : 0.1f;
                        break;
                    case HA::HeadVariantPrev: headVariantCycleDelta_ = -1; break;
                    case HA::HeadVariantNext: headVariantCycleDelta_ = 1; break;

                    // Helm UV adjustments
                    case HA::HelmUOffsetLeft: helmUOffsetDelta_ = -uvStep; break;
                    case HA::HelmUOffsetRight: helmUOffsetDelta_ = uvStep; break;
                    case HA::HelmVOffsetUp: helmVOffsetDelta_ = uvStep; break;
                    case HA::HelmVOffsetDown: helmVOffsetDelta_ = -uvStep; break;
                    case HA::HelmUScaleDecrease: helmUScaleDelta_ = -scaleStep; break;
                    case HA::HelmUScaleIncrease: helmUScaleDelta_ = scaleStep; break;
                    case HA::HelmVScaleDecrease: helmVScaleDelta_ = -scaleStep; break;
                    case HA::HelmVScaleIncrease: helmVScaleDelta_ = scaleStep; break;
                    case HA::HelmRotateLeft: helmRotationDelta_ = -rotStep; break;
                    case HA::HelmRotateRight: helmRotationDelta_ = rotStep; break;
                    case HA::HelmReset: helmResetRequested_ = true; break;
                    case HA::HelmUVSwap: helmUVSwapRequested_ = true; break;
                    case HA::HelmVFlip: helmVFlipRequested_ = true; break;
                    case HA::HelmUFlip: helmUFlipRequested_ = true; break;

                    // Collision height adjustments
                    case HA::CollisionHeightUp:
                        collisionHeightDelta_ = event.KeyInput.Shift ? 0.1f : 1.0f;
                        break;
                    case HA::CollisionHeightDown:
                        collisionHeightDelta_ = event.KeyInput.Shift ? -0.1f : -1.0f;
                        break;
                    case HA::StepHeightUp:
                        stepHeightDelta_ = event.KeyInput.Shift ? 0.1f : 1.0f;
                        break;
                    case HA::StepHeightDown:
                        stepHeightDelta_ = event.KeyInput.Shift ? -0.1f : -1.0f;
                        break;

                    // === Repair Mode Actions ===
                    case HA::RepairRotateXPos: repairRotateXDelta_ = repairRotStep; break;
                    case HA::RepairRotateXNeg: repairRotateXDelta_ = -repairRotStep; break;
                    case HA::RepairRotateYPos: repairRotateYDelta_ = repairRotStep; break;
                    case HA::RepairRotateYNeg: repairRotateYDelta_ = -repairRotStep; break;
                    case HA::RepairRotateZPos: repairRotateZDelta_ = repairRotStep; break;
                    case HA::RepairRotateZNeg: repairRotateZDelta_ = -repairRotStep; break;
                    case HA::RepairFlipX: repairFlipXRequested_ = true; break;
                    case HA::RepairFlipY: repairFlipYRequested_ = true; break;
                    case HA::RepairFlipZ: repairFlipZRequested_ = true; break;
                    case HA::RepairReset: repairResetRequested_ = true; break;

                    // Movement keys and Jump are handled separately (continuous state)
                    default:
                        break;
                }
            }

            // Numpad helm controls (not in HotkeyManager, kept as hardcoded fallback)
            if (event.KeyInput.Key == irr::KEY_NUMPAD4) helmUOffsetDelta_ = -uvStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD6) helmUOffsetDelta_ = uvStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD8) helmVOffsetDelta_ = uvStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD2) helmVOffsetDelta_ = -uvStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD7) helmUScaleDelta_ = -scaleStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD9) helmUScaleDelta_ = scaleStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD1) helmVScaleDelta_ = -scaleStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD3) helmVScaleDelta_ = scaleStep;
            if (event.KeyInput.Key == irr::KEY_ADD) helmRotationDelta_ = rotStep;
            if (event.KeyInput.Key == irr::KEY_SUBTRACT) helmRotationDelta_ = -rotStep;
            if (event.KeyInput.Key == irr::KEY_NUMPAD5) helmPrintStateRequested_ = true;
            if (event.KeyInput.Key == irr::KEY_NUMPAD0) helmResetRequested_ = true;
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

    // Apply constrained rendering mode if enabled
    if (config_.constrainedPreset != ConstrainedRenderingPreset::None) {
        // Load preset configuration
        config_.constrainedConfig = ConstrainedRendererConfig::fromPreset(config_.constrainedPreset);

        // Calculate max resolution from framebuffer budget
        config_.constrainedConfig.calculateMaxResolution();

        // Clamp resolution to max allowed by framebuffer memory limit
        if (config_.constrainedConfig.clampResolution(config_.width, config_.height)) {
            LOG_WARN(MOD_GRAPHICS, "Resolution clamped to {}x{} (framebuffer memory limit: {} bytes)",
                     config_.width, config_.height, config_.constrainedConfig.framebufferMemoryBytes);
        }

        LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode: {} ({}x{}, {}MB texture, {}MB framebuffer)",
                 ConstrainedRendererConfig::presetName(config_.constrainedPreset),
                 config_.width, config_.height,
                 config_.constrainedConfig.textureMemoryBytes / (1024 * 1024),
                 config_.constrainedConfig.framebufferMemoryBytes / (1024 * 1024));
    }

    // Choose driver type
    irr::video::E_DRIVER_TYPE driverType;
    if (config.softwareRenderer) {
        driverType = irr::video::EDT_BURNINGSVIDEO;
    } else {
        driverType = irr::video::EDT_OPENGL;
    }

    // Create device
    irr::SIrrlichtCreationParameters params;
    params.DriverType = driverType;
    params.WindowSize = irr::core::dimension2d<irr::u32>(config.width, config.height);
    params.Fullscreen = config.fullscreen;
    params.Stencilbuffer = false;
    params.Vsync = true;
    params.AntiAlias = 0;

    device_ = irr::createDeviceEx(params);

    if (!device_) {
        // Fall back to basic software renderer
        params.DriverType = irr::video::EDT_SOFTWARE;
        device_ = irr::createDeviceEx(params);
    }

    if (!device_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create Irrlicht device");
        return false;
    }

    // Suppress Irrlicht's internal logging (e.g., "Loaded texture:" messages)
    // These are too verbose for normal operation
    device_->getLogger()->setLogLevel(irr::ELL_ERROR);

    device_->setWindowCaption(irr::core::stringw(config.windowTitle.c_str()).c_str());

    driver_ = device_->getVideoDriver();
    smgr_ = device_->getSceneManager();
    guienv_ = device_->getGUIEnvironment();

    // Log driver info
    if (driver_) {
        const wchar_t* driverName = driver_->getName();
        std::wstring wname(driverName);
        std::string name(wname.begin(), wname.end());
        LOG_INFO(MOD_GRAPHICS, "Video driver: {}", name);
    }

    // Create constrained texture cache if in constrained mode
    if (config_.constrainedConfig.enabled && driver_) {
        constrainedTextureCache_ = std::make_unique<ConstrainedTextureCache>(
            config_.constrainedConfig, driver_);
        constrainedTextureCache_->setSceneManager(smgr_);  // Enable safe eviction
        LOG_INFO(MOD_GRAPHICS, "Constrained texture cache created ({}KB limit, {}x{} max texture)",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension);
    }

    // Create event receiver
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());
    eventReceiver_->setCurrentMode(rendererMode_);  // Initialize hotkey mode

    // Setup camera
    setupCamera();

    // Setup lighting
    setupLighting();

    // Setup HUD
    setupHUD();

    // Create entity renderer
    entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
    entityRenderer_->setClientPath(config.eqClientPath);
    entityRenderer_->setNameTagsVisible(config.showNameTags);
    entityRenderer_->setRenderDistance(renderDistance_);

    // Pass constrained config to entity renderer for visibility limits
    if (config_.constrainedConfig.enabled) {
        entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
    }

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

    // Create weather system
    weatherSystem_ = std::make_unique<WeatherSystem>();
    // Connect weather system to tree manager via callback
    weatherSystem_->addCallback([this](WeatherType weather) {
        if (treeManager_) {
            treeManager_->setWeather(weather);
        }
    });

    // Create environmental particle system
    particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
    if (!particleManager_->init(config.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize particle manager");
    }

    // Create ambient creatures (boids) system
    boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
    if (!boidsManager_->init(config.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
    }

    // Create tumbleweed manager (desert/plains rolling objects)
    tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
    if (!tumbleweedManager_->init()) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
    }

    // Create weather effects controller (rain, snow, lightning)
    weatherEffects_ = std::make_unique<WeatherEffectsController>(
        smgr_, driver_, particleManager_.get(), skyRenderer_.get());
    if (!weatherEffects_->initialize(config.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller");
    }
    // Connect weather system to weather effects
    weatherSystem_->addListener(weatherEffects_.get());

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

    // Apply constrained rendering mode if enabled
    if (config_.constrainedPreset != ConstrainedRenderingPreset::None) {
        // Load preset configuration
        config_.constrainedConfig = ConstrainedRendererConfig::fromPreset(config_.constrainedPreset);

        // Calculate max resolution from framebuffer budget
        config_.constrainedConfig.calculateMaxResolution();

        // Clamp resolution to max allowed by framebuffer memory limit
        if (config_.constrainedConfig.clampResolution(config_.width, config_.height)) {
            LOG_WARN(MOD_GRAPHICS, "Resolution clamped to {}x{} (framebuffer memory limit: {} bytes)",
                     config_.width, config_.height, config_.constrainedConfig.framebufferMemoryBytes);
        }

        LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode: {} ({}x{}, {}MB texture, {}MB framebuffer)",
                 ConstrainedRendererConfig::presetName(config_.constrainedPreset),
                 config_.width, config_.height,
                 config_.constrainedConfig.textureMemoryBytes / (1024 * 1024),
                 config_.constrainedConfig.framebufferMemoryBytes / (1024 * 1024));
    }

    // Choose driver type
    irr::video::E_DRIVER_TYPE driverType;
    if (config.softwareRenderer) {
        driverType = irr::video::EDT_BURNINGSVIDEO;
    } else {
        driverType = irr::video::EDT_OPENGL;
    }

    // Create device
    irr::SIrrlichtCreationParameters params;
    params.DriverType = driverType;
    params.WindowSize = irr::core::dimension2d<irr::u32>(config.width, config.height);
    params.Fullscreen = config.fullscreen;
    params.Stencilbuffer = false;
    params.Vsync = true;
    params.AntiAlias = 0;

    device_ = irr::createDeviceEx(params);

    if (!device_) {
        // Fall back to basic software renderer
        params.DriverType = irr::video::EDT_SOFTWARE;
        device_ = irr::createDeviceEx(params);
    }

    if (!device_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create Irrlicht device");
        return false;
    }

    // Suppress Irrlicht's internal logging
    device_->getLogger()->setLogLevel(irr::ELL_ERROR);
    device_->setWindowCaption(irr::core::stringw(config.windowTitle.c_str()).c_str());

    driver_ = device_->getVideoDriver();
    smgr_ = device_->getSceneManager();
    guienv_ = device_->getGUIEnvironment();

    // Log driver info
    if (driver_) {
        const wchar_t* driverName = driver_->getName();
        std::wstring wname(driverName);
        std::string name(wname.begin(), wname.end());
        LOG_INFO(MOD_GRAPHICS, "Video driver: {}", name);
    }

    // Create constrained texture cache if in constrained mode
    if (config_.constrainedConfig.enabled && driver_) {
        constrainedTextureCache_ = std::make_unique<ConstrainedTextureCache>(
            config_.constrainedConfig, driver_);
        constrainedTextureCache_->setSceneManager(smgr_);  // Enable safe eviction
        LOG_INFO(MOD_GRAPHICS, "Constrained texture cache created ({}KB limit, {}x{} max texture)",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension);
    }

    // Create event receiver
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

    // Setup camera (needed for loading screen rendering)
    setupCamera();

    // Setup lighting (basic setup)
    setupLighting();

    // Setup HUD (needed for loading screen text)
    setupHUD();

    // Apply initial settings
    wireframeMode_ = config.wireframe;
    fogEnabled_ = config.fog;
    lightingEnabled_ = config.lighting;

    // NOTE: We do NOT create entity renderer or load models here.
    // That happens in loadGlobalAssets() which is called during graphics loading phase.

    // Create tree wind animation manager (needed before loadZone())
    if (!treeManager_) {
        treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
        treeManager_->setRenderDistance(renderDistance_);
    }

    // Create weather system (needed before loadZone())
    if (!weatherSystem_) {
        weatherSystem_ = std::make_unique<WeatherSystem>();
        // Connect weather system to tree manager via callback
        weatherSystem_->addCallback([this](WeatherType weather) {
            if (treeManager_) {
                treeManager_->setWeather(weather);
            }
        });
    }

    // Create environmental particle system (needed before loadZone())
    if (!particleManager_) {
        particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
        if (!particleManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize particle manager");
        }
    }

    // Create ambient creatures (boids) system (needed before loadZone())
    if (!boidsManager_) {
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
        }
    }

    // Create tumbleweed manager (desert/plains rolling objects)
    if (!tumbleweedManager_) {
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
        }
    }

    // Create weather effects controller (rain, snow, lightning)
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
        if (config_.constrainedConfig.enabled) {
            entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
        }
    }

    // Load main global character models (global_chr.s3d)
    if (entityRenderer_->loadGlobalCharacters()) {
        LOG_DEBUG(MOD_GRAPHICS, "Global character models loaded");
    } else {
        LOG_WARN(MOD_GRAPHICS, "Could not load global character models (will use placeholders)");
    }

    // Preload numbered global character models for better coverage (global2-7_chr.s3d)
    entityRenderer_->loadNumberedGlobals();

    // Load equipment models from gequip.s3d archives
    if (entityRenderer_->loadEquipmentModels()) {
        LOG_INFO(MOD_GRAPHICS, "Equipment models loaded");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Could not load equipment models");
    }

    // Create door manager (if not already created)
    if (!doorManager_) {
        doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
        // If zone was already loaded before doorManager_ was created, set it now
        if (currentZone_) {
            doorManager_->setZone(currentZone_);
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

    // Create detail manager (grass, plants, debris)
    if (!detailManager_) {
        detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
        // Set surface maps path - surface map files are in data/detail/zones/
        detailManager_->setSurfaceMapsPath("data/detail/zones");
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
}

void IrrlichtRenderer::shutdown() {
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

void IrrlichtRenderer::setupCamera() {
    camera_ = smgr_->addCameraSceneNode(
        nullptr,
        irr::core::vector3df(0, 100, 0),
        irr::core::vector3df(100, 0, 100),
        -1
    );

    // Camera far plane must be large enough to include the sky dome (1800 units)
    // Render distance and fog control world object visibility separately
    float farValue = SKY_FAR_PLANE;
    if (config_.constrainedConfig.enabled && config_.constrainedConfig.clipDistance > SKY_FAR_PLANE) {
        farValue = config_.constrainedConfig.clipDistance;
        LOG_INFO(MOD_GRAPHICS, "Constrained mode: clip distance set to {}", farValue);
    }
    camera_->setFarValue(farValue);
    camera_->setNearValue(1.0f);
    LOG_INFO(MOD_GRAPHICS, "Camera far plane: {}, render distance: {}", farValue, renderDistance_);

    cameraController_ = std::make_unique<CameraController>(camera_);
    cameraController_->setMoveSpeed(500.0f);
    cameraController_->setMouseSensitivity(0.2f);
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
}

void IrrlichtRenderer::updateObjectVisibility() {
    if (!camera_ || objectNodes_.empty()) return;

    irr::core::vector3df cameraPos = camera_->getPosition();

    // Only update visibility if camera has moved significantly
    const float updateThreshold = 5.0f;
    float cameraMoved = cameraPos.getDistanceFrom(lastCullingCameraPos_);
    if (cameraMoved < updateThreshold && lastCullingCameraPos_.getLength() > 0.01f) {
        return;  // Camera hasn't moved enough, skip update
    }

    LOG_DEBUG(MOD_GRAPHICS, "=== OBJECT VISIBILITY UPDATE === camPos=({:.1f},{:.1f},{:.1f}) renderDist={}",
        cameraPos.X, cameraPos.Y, cameraPos.Z, renderDistance_);
    lastCullingCameraPos_ = cameraPos;

    // Update scene graph membership based on distance to bounding box edge
    // Objects are kept in scene until their nearest edge exceeds render distance
    // The fog system handles the visual fade - distant parts fade into fog naturally
    size_t inSceneCount = 0;
    size_t removedCount = 0;

    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        if (!objectNodes_[i]) continue;
        if (i >= objectBoundingBoxes_.size()) continue;  // Safety check

        // Calculate distance to the nearest point on the object's bounding box
        // This ensures large objects come into view gradually (edge first) rather than
        // popping in all at once when the center comes within range
        const irr::core::aabbox3df& bbox = objectBoundingBoxes_[i];

        // Check for invalid bounding box (empty or degenerate)
        // If invalid, fall back to using the cached position
        float dist;
        bool validBbox = (bbox.MinEdge.X <= bbox.MaxEdge.X &&
                          bbox.MinEdge.Y <= bbox.MaxEdge.Y &&
                          bbox.MinEdge.Z <= bbox.MaxEdge.Z);

        if (validBbox) {
            // Find the closest point on the bounding box to the camera
            irr::core::vector3df closestPoint;
            closestPoint.X = std::max(bbox.MinEdge.X, std::min(cameraPos.X, bbox.MaxEdge.X));
            closestPoint.Y = std::max(bbox.MinEdge.Y, std::min(cameraPos.Y, bbox.MaxEdge.Y));
            closestPoint.Z = std::max(bbox.MinEdge.Z, std::min(cameraPos.Z, bbox.MaxEdge.Z));
            dist = cameraPos.getDistanceFrom(closestPoint);
        } else {
            // Fall back to center position distance
            dist = cameraPos.getDistanceFrom(objectPositions_[i]);
        }

        // Object should be in scene graph if its nearest edge is within render distance
        bool shouldBeInScene = (dist <= renderDistance_);

        if (shouldBeInScene && !objectInSceneGraph_[i]) {
            // Add back to scene graph
            smgr_->getRootSceneNode()->addChild(objectNodes_[i]);
            objectNodes_[i]->setVisible(true);
            objectInSceneGraph_[i] = true;
        } else if (!shouldBeInScene && objectInSceneGraph_[i]) {
            // Remove from scene graph (but keep the node alive via grab())
            objectNodes_[i]->remove();
            objectInSceneGraph_[i] = false;
        }

        // Log VISIBLE objects with their distance
        if (objectInSceneGraph_[i]) {
            const char* name = objectNodes_[i]->getName();
            LOG_DEBUG(MOD_GRAPHICS, "[OBJ VISIBLE] '{}' dist={:.1f} bbox=({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f})",
                name ? name : "unknown", dist,
                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
            inSceneCount++;
        } else {
            removedCount++;
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "=== OBJECT VISIBILITY RESULT: {} VISIBLE, {} CULLED ===",
        inSceneCount, removedCount);
}

void IrrlichtRenderer::updateZoneLightVisibility() {
    if (!camera_ || zoneLightNodes_.empty()) return;

    irr::core::vector3df cameraPos = camera_->getPosition();

    // Update scene graph membership based on distance
    // This removes far lights entirely from the scene graph to skip traversal overhead
    size_t inSceneCount = 0;
    size_t removedCount = 0;
    const float renderDistSq = renderDistance_ * renderDistance_;

    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (!zoneLightNodes_[i]) continue;

        float distSq = cameraPos.getDistanceFromSQ(zoneLightPositions_[i]);
        bool shouldBeInScene = (distSq <= renderDistSq);

        if (shouldBeInScene && !zoneLightInSceneGraph_[i]) {
            // Add back to scene graph
            smgr_->getRootSceneNode()->addChild(zoneLightNodes_[i]);
            zoneLightInSceneGraph_[i] = true;
        } else if (!shouldBeInScene && zoneLightInSceneGraph_[i]) {
            // Remove from scene graph (but keep the node alive via grab())
            zoneLightNodes_[i]->remove();
            zoneLightInSceneGraph_[i] = false;
        }

        if (zoneLightInSceneGraph_[i]) {
            inSceneCount++;
        } else {
            removedCount++;
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "Zone light scene graph: {} in scene, {} removed (dist={})",
        inSceneCount, removedCount, renderDistance_);
}

void IrrlichtRenderer::updateObjectLights() {
    if (!camera_) return;

    const float maxDistance = 500.0f;  // Maximum distance to consider a light
    const size_t hardwareLightLimit = 8;  // Software renderer limit

    irr::core::vector3df cameraPos = camera_->getPosition();

    // Player position for occlusion checks (EQ coords to Irrlicht: x, z, y)
    // Raise to head height (~5 units) so low geometry doesn't block line of sight
    irr::core::vector3df playerPos(playerX_, playerZ_ + 5.0f, playerY_);

    // Helper to calculate horizontal (XZ plane) distance - ignores vertical (Y) component
    // This ensures lights above/below the player are still considered "close"
    auto horizontalDistance = [](const irr::core::vector3df& a, const irr::core::vector3df& b) -> float {
        float dx = a.X - b.X;
        float dz = a.Z - b.Z;
        return std::sqrt(dx * dx + dz * dz);
    };

    // Helper to check if a light is visible from player position (not occluded by geometry)
    auto isLightVisible = [this, &playerPos](const irr::core::vector3df& lightPos) -> bool {
        if (!collisionManager_ || !zoneTriangleSelector_) {
            return true;  // No collision detection available, assume visible
        }

        // Cast ray from player to light
        irr::core::line3df ray(playerPos, lightPos);
        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTriangle;
        irr::scene::ISceneNode* hitNode = nullptr;

        // Check if ray hits geometry before reaching the light
        hitNode = collisionManager_->getSceneNodeAndCollisionPointFromRay(
            ray, hitPoint, hitTriangle, 0, nullptr);

        if (hitNode) {
            // Calculate distances
            float distToLight = playerPos.getDistanceFrom(lightPos);
            float distToHit = playerPos.getDistanceFrom(hitPoint);

            // Light is occluded if we hit something closer than the light
            // Use a small tolerance to avoid floating point issues
            if (distToHit < distToLight - 5.0f) {
                return false;  // Light is occluded
            }
        }

        return true;  // Light is visible
    };

    // Unified light pool: stores {distance, light_node, is_zone_light, name}
    struct LightCandidate {
        float distance;
        irr::scene::ILightSceneNode* node;
        bool isZoneLight;
        std::string name;
    };
    std::vector<LightCandidate> candidates;
    candidates.reserve(objectLights_.size() + zoneLightNodes_.size());

    // First, disable all lights (including player light)
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            objLight.node->setVisible(false);
        }
    }
    for (auto* node : zoneLightNodes_) {
        if (node) {
            node->setVisible(false);
        }
    }
    if (playerLightNode_) {
        playerLightNode_->setVisible(false);
        // Update player light position to follow player
        playerLightNode_->setPosition(irr::core::vector3df(playerX_, playerZ_ + 3.0f, playerY_));
    }

    // Add player light first with distance 0 (always highest priority)
    if (playerLightNode_ && playerLightLevel_ > 0) {
        candidates.push_back({0.0f, playerLightNode_, false, "player_light"});
    }

    // Add zone lights to the pool if zone lights are enabled
    // Note: Zone lights skip occlusion checks for performance - they are static/decorative
    // and checking 200+ raycasts per frame is too expensive
    if (zoneLightsEnabled_) {
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            auto* node = zoneLightNodes_[i];
            if (node) {
                irr::core::vector3df lightPos = node->getPosition();
                float dist = horizontalDistance(cameraPos, lightPos);
                if (dist <= maxDistance) {
                    candidates.push_back({dist, node, true, "zone_light_" + std::to_string(i)});
                }
            }
        }
    }

    // Add object lights to the pool (up to maxObjectLights_ candidates)
    // Performance: Only do occlusion checks on closest 16 lights, not all in range
    const size_t maxOcclusionChecks = 16;

    // First collect ALL lights in range by distance (no occlusion check yet)
    std::vector<std::pair<float, size_t>> objectDistances;
    objectDistances.reserve(objectLights_.size());
    for (size_t i = 0; i < objectLights_.size(); ++i) {
        float dist = horizontalDistance(cameraPos, objectLights_[i].position);
        if (dist <= maxDistance) {
            objectDistances.push_back({dist, i});
        }
    }
    std::sort(objectDistances.begin(), objectDistances.end());

    // Only do occlusion checks on the closest N lights
    size_t inRangeCount = objectDistances.size();
    size_t occludedCount = 0;
    size_t checksPerformed = std::min(objectDistances.size(), maxOcclusionChecks);

    std::vector<std::pair<float, size_t>> visibleLights;
    visibleLights.reserve(checksPerformed);
    for (size_t i = 0; i < checksPerformed; ++i) {
        size_t idx = objectDistances[i].second;
        if (isLightVisible(objectLights_[idx].position)) {
            visibleLights.push_back(objectDistances[i]);
        } else {
            occludedCount++;
        }
    }

    // Add the closest maxObjectLights_ visible object lights to candidates
    size_t objectLightCount = std::min(visibleLights.size(), static_cast<size_t>(maxObjectLights_));
    for (size_t i = 0; i < objectLightCount; ++i) {
        size_t idx = visibleLights[i].second;
        if (objectLights_[idx].node) {
            candidates.push_back({visibleLights[i].first, objectLights_[idx].node, false, objectLights_[idx].objectName});
        }
    }

    // Sort all candidates by distance (closest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const LightCandidate& a, const LightCandidate& b) {
            return a.distance < b.distance;
        });

    // Enable only the closest lights up to hardware limit
    size_t enabledCount = std::min(candidates.size(), hardwareLightLimit);
    for (size_t i = 0; i < enabledCount; ++i) {
        if (candidates[i].node) {
            candidates[i].node->setVisible(true);
        }
    }

    // Check if active lights changed and log if so (cleared by cycleObjectLights)
    // Use "_none_" sentinel when 0 lights so we can distinguish "0 lights logged" from "needs re-log"
    bool needsLog = previousActiveLights_.empty() ||
                    (enabledCount == 0 && previousActiveLights_[0] != "_none_") ||
                    (enabledCount > 0 && (previousActiveLights_.size() != enabledCount || previousActiveLights_[0] == "_none_"));
    if (needsLog) {
        previousActiveLights_.clear();
        LOG_DEBUG(MOD_GRAPHICS, "Active lights: {} enabled (objLights: {} in range, checked {}, {} visible, {} occluded; maxObj={})",
            enabledCount, inRangeCount, checksPerformed, visibleLights.size(), occludedCount, maxObjectLights_);
        if (enabledCount == 0) {
            previousActiveLights_.push_back("_none_");
        } else {
            previousActiveLights_.reserve(enabledCount);
            for (size_t i = 0; i < enabledCount; ++i) {
                previousActiveLights_.push_back(candidates[i].name);
                if (candidates[i].node) {
                    irr::core::vector3df pos = candidates[i].node->getPosition();
                    LOG_DEBUG(MOD_GRAPHICS, "  #{} '{}' at ({:.1f}, {:.1f}, {:.1f}) dist={:.1f}",
                        i, candidates[i].name, pos.X, pos.Y, pos.Z, candidates[i].distance);
                }
            }
        }
    }

    // Debug: Create/update markers at active light positions
    if (showLightDebugMarkers_ && smgr_) {
        // Remove old markers
        for (auto* marker : lightDebugMarkers_) {
            if (marker) marker->remove();
        }
        lightDebugMarkers_.clear();

        // Create new markers at enabled light positions
        irr::scene::IMesh* cubeMesh = smgr_->getGeometryCreator()->createCubeMesh(irr::core::vector3df(2.0f, 2.0f, 2.0f));
        if (cubeMesh) {
            for (size_t i = 0; i < enabledCount; ++i) {
                if (candidates[i].node) {
                    irr::core::vector3df pos = candidates[i].node->getPosition();
                    irr::scene::IMeshSceneNode* marker = smgr_->addMeshSceneNode(cubeMesh);
                    if (marker) {
                        marker->setPosition(pos);
                        // Color based on light type: yellow for zone lights, orange for object lights
                        irr::video::SColor color = candidates[i].isZoneLight ?
                            irr::video::SColor(255, 255, 255, 0) :  // Yellow for zone
                            irr::video::SColor(255, 255, 128, 0);   // Orange for object
                        marker->getMaterial(0).Lighting = false;
                        marker->getMaterial(0).EmissiveColor = color;
                        marker->getMaterial(0).DiffuseColor = color;
                        lightDebugMarkers_.push_back(marker);
                    }
                }
            }
            cubeMesh->drop();
        }
    }
}

void IrrlichtRenderer::updateVertexAnimations(float deltaMs) {
    if (vertexAnimatedMeshes_.empty()) {
        return;  // Nothing to animate
    }

    for (size_t i = 0; i < vertexAnimatedMeshes_.size(); ++i) {
        auto& vam = vertexAnimatedMeshes_[i];
        if (!vam.mesh || !vam.animData || vam.animData->frames.empty()) {
            continue;
        }

        vam.elapsedMs += deltaMs;

        // Check if we need to advance to the next frame
        if (vam.elapsedMs >= static_cast<float>(vam.animData->delayMs)) {
            vam.elapsedMs = std::fmod(vam.elapsedMs, static_cast<float>(vam.animData->delayMs));
            vam.currentFrame = (vam.currentFrame + 1) % static_cast<int>(vam.animData->frames.size());

            // Update mesh buffer vertices with the new frame's positions
            const VertexAnimFrame& frame = vam.animData->frames[vam.currentFrame];
            size_t expectedVerts = frame.positions.size() / 3;

            // Update each mesh buffer using vertex mapping
            for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = vam.mesh->getMeshBuffer(b);
                irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                irr::u32 vertexCount = buffer->getVertexCount();

                // Check if we have mapping for this buffer
                if (b >= vam.vertexMapping.size() || vam.vertexMapping[b].size() != vertexCount) {
                    continue;
                }

                for (irr::u32 v = 0; v < vertexCount; ++v) {
                    size_t animIdx = vam.vertexMapping[b][v];
                    if (animIdx == SIZE_MAX || animIdx >= expectedVerts) {
                        continue;  // No mapping for this vertex
                    }

                    // Get new position from animation frame (EQ coordinates)
                    // Animation positions are relative to center, add center offset
                    float eqX = frame.positions[animIdx * 3 + 0] + vam.centerOffsetX;
                    float eqY = frame.positions[animIdx * 3 + 1] + vam.centerOffsetY;
                    float eqZ = frame.positions[animIdx * 3 + 2] + vam.centerOffsetZ;

                    // Apply EQ->Irrlicht coordinate transform
                    // EQ (x, y, z) Z-up -> Irrlicht (x, z, y) Y-up
                    vertices[v].Pos.X = eqX;
                    vertices[v].Pos.Y = eqZ;
                    vertices[v].Pos.Z = eqY;
                }

                // Mark buffer as dirty so Irrlicht knows to re-upload it
                buffer->setDirty(irr::scene::EBT_VERTEX);
            }
        }
    }
}

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
    currentState.rendererMode = rendererMode_;
    currentState.fps = currentFps_;
    currentState.playerX = static_cast<int>(playerX_);
    currentState.playerY = static_cast<int>(playerY_);
    currentState.playerZ = static_cast<int>(playerZ_);
    if (entityRenderer_) {
        currentState.entityCount = entityRenderer_->getEntityCount();
        currentState.modeledEntityCount = entityRenderer_->getModeledEntityCount();
        currentState.animSpeed = entityRenderer_->getGlobalAnimationSpeed();
        currentState.corpseZ = entityRenderer_->getCorpseZOffset();
    }
    currentState.targetId = currentTargetId_;
    currentState.targetHpPercent = currentTargetHpPercent_;
    currentState.wireframeMode = wireframeMode_;
    currentState.oldModels = isUsingOldModels();
    currentState.cameraMode = getCameraModeString();
    currentState.zoneName = currentZoneName_;

    // Check if state has changed
    bool stateChanged = (currentState.rendererMode != hudCachedState_.rendererMode ||
                         currentState.fps != hudCachedState_.fps ||
                         currentState.playerX != hudCachedState_.playerX ||
                         currentState.playerY != hudCachedState_.playerY ||
                         currentState.playerZ != hudCachedState_.playerZ ||
                         currentState.entityCount != hudCachedState_.entityCount ||
                         currentState.modeledEntityCount != hudCachedState_.modeledEntityCount ||
                         currentState.targetId != hudCachedState_.targetId ||
                         currentState.targetHpPercent != hudCachedState_.targetHpPercent ||
                         currentState.animSpeed != hudCachedState_.animSpeed ||
                         currentState.corpseZ != hudCachedState_.corpseZ ||
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

    if (rendererMode_ == RendererMode::Player) {
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
    } else if (rendererMode_ == RendererMode::Repair) {
        // === REPAIR MODE HUD ===
        text << L"[REPAIR MODE]\n";

        // Zone info
        if (!currentZoneName_.empty()) {
            std::wstring wzone(currentZoneName_.begin(), currentZoneName_.end());
            text << L"Zone: " << wzone << L"\n";
        }

        // Location
        text << L"Loc: " << (int)playerX_ << L", " << (int)playerY_ << L", " << (int)playerZ_ << L"\n";
        text << L"FPS: " << currentFps_ << L"\n";

        // Repair target info
        if (repairTargetNode_) {
            text << L"\n--- REPAIR TARGET ---\n";
            std::wstring wTargetName = EQT::toDisplayNameW(repairTargetName_);
            text << L"Object: " << wTargetName << L"\n";

            // Position
            irr::core::vector3df pos = repairTargetNode_->getPosition();
            text << L"Pos: (" << (int)pos.X << L", " << (int)pos.Z << L", " << (int)pos.Y << L")\n";

            // Current rotation (original + offset)
            irr::core::vector3df rot = repairTargetNode_->getRotation();
            text << L"Rot: (" << (int)rot.X << L", " << (int)rot.Y << L", " << (int)rot.Z << L")\n";

            // Rotation offset applied
            text << L"Offset: (" << (int)repairRotationOffset_.X << L", "
                 << (int)repairRotationOffset_.Y << L", " << (int)repairRotationOffset_.Z << L")\n";

            // Flip state
            text << L"Flip: ";
            if (repairFlipX_) text << L"X ";
            if (repairFlipY_) text << L"Y ";
            if (repairFlipZ_) text << L"Z ";
            if (!repairFlipX_ && !repairFlipY_ && !repairFlipZ_) text << L"None";
            text << L"\n";
        } else {
            text << L"\nClick on zone object to select\n";
        }

        // Repair mode hotkeys
        hotkeys << L"Click=Select  ESC=Clear\n";
        hotkeys << L"X/Y/Z=Rotate (+Shift=-)\n";
        hotkeys << L"Ctrl+1/2/3=Flip  Ctrl+R=Reset\n";
        hotkeys << L"F9=Admin";
    } else {
        // === ADMIN MODE HUD (full debug info) ===
        text << L"[ADMIN MODE]\n";

        // Zone info
        if (!currentZoneName_.empty()) {
            text << L"Zone: ";
            std::wstring wzone(currentZoneName_.begin(), currentZoneName_.end());
            text << wzone;
            if (currentZone_ && currentZone_->geometry) {
                text << L" (" << currentZone_->geometry->vertices.size() << L" verts)";
            }
            text << L"\n";
        }

        // Camera position (convert Irrlicht coords back to EQ x,y,z format)
        // Irrlicht (X, Y, Z) = EQ (x, z, y), so EQ (x, y, z) = Irrlicht (X, Z, Y)
        if (camera_) {
            irr::core::vector3df pos = camera_->getPosition();
            text << L"Pos: (" << (int)pos.X << L", " << (int)pos.Z << L", " << (int)pos.Y << L")\n";
        }

        // Entity count and animation speed
        if (entityRenderer_) {
            size_t total = entityRenderer_->getEntityCount();
            size_t modeled = entityRenderer_->getModeledEntityCount();
            text << L"Entities: " << total << L" (" << modeled << L" with 3D models)";
            // Animation speed
            float speed = entityRenderer_->getGlobalAnimationSpeed();
            int speedInt = static_cast<int>(speed * 10);
            text << L"  AnimSpeed: " << (speedInt / 10) << L"." << (speedInt % 10) << L"x";
            // Corpse Z offset (if non-zero)
            float corpseZ = entityRenderer_->getCorpseZOffset();
            if (corpseZ != 0.0f) {
                int corpseZInt = static_cast<int>(corpseZ * 10);
                text << L"  CorpseZ: " << (corpseZInt / 10) << L"." << (std::abs(corpseZInt) % 10);
            }
            text << L"\n";
        }

        // Render mode and camera
        text << L"Mode: " << (wireframeMode_ ? L"Wireframe" : L"Solid");
        std::string camMode = getCameraModeString();
        std::wstring wCamMode(camMode.begin(), camMode.end());
        text << L"  Camera: " << wCamMode;
        text << L"  Models: " << (isUsingOldModels() ? L"Classic" : L"Luclin");
        text << L"  FPS: " << currentFps_ << L"\n";

        // Sky info
        std::string skyInfo = getSkyDebugInfo();
        std::wstring wSkyInfo(skyInfo.begin(), skyInfo.end());
        text << wSkyInfo;
        text << L"  Time: " << (int)currentHour_ << L":" << (currentMinute_ < 10 ? L"0" : L"") << (int)currentMinute_ << L"\n";

        // Constrained mode debug info
        if (config_.constrainedPreset != ConstrainedRenderingPreset::None) {
            // Preset name and resolution
            std::string presetName = ConstrainedRendererConfig::presetName(config_.constrainedPreset);
            std::wstring wPresetName(presetName.begin(), presetName.end());
            text << L"\n[" << wPresetName << L"] " << config_.width << L"x" << config_.height;
            text << L" @ " << config_.constrainedConfig.colorDepthBits << L"-bit\n";

            // Framebuffer memory (calculated from current resolution)
            size_t fbiUsed = config_.constrainedConfig.calculateFramebufferUsage(config_.width, config_.height);
            size_t fbiLimit = config_.constrainedConfig.framebufferMemoryBytes;
            float fbiUsedMB = static_cast<float>(fbiUsed) / (1024.0f * 1024.0f);
            float fbiLimitMB = static_cast<float>(fbiLimit) / (1024.0f * 1024.0f);
            wchar_t fbiBuf[64];
            swprintf(fbiBuf, 64, L"FBI: %.1fMB/%.1fMB", fbiUsedMB, fbiLimitMB);
            text << fbiBuf;

            // Texture memory (from cache)
            if (constrainedTextureCache_) {
                size_t tmuUsed = constrainedTextureCache_->getCurrentUsage();
                size_t tmuLimit = constrainedTextureCache_->getMemoryLimit();
                float tmuUsedMB = static_cast<float>(tmuUsed) / (1024.0f * 1024.0f);
                float tmuLimitMB = static_cast<float>(tmuLimit) / (1024.0f * 1024.0f);
                wchar_t tmuBuf[64];
                swprintf(tmuBuf, 64, L" | TMU: %.1fMB/%.1fMB\n", tmuUsedMB, tmuLimitMB);
                text << tmuBuf;

                // Texture count and cache statistics
                size_t texCount = constrainedTextureCache_->getTextureCount();
                float hitRate = constrainedTextureCache_->getHitRate();
                size_t evictions = constrainedTextureCache_->getEvictionCount();
                wchar_t statsBuf[64];
                swprintf(statsBuf, 64, L"Textures: %zu | Hit: %.0f%% | Evict: %zu\n",
                         texCount, hitRate, evictions);
                text << statsBuf;
            } else {
                text << L" | TMU: N/A\n";
            }

            // Geometry stats: polygon count, entity count, clip distance
            wchar_t geomBuf[128];
            int visibleEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
            int totalEntities = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
            swprintf(geomBuf, 128, L"Polys: %u/%d | Entities: %d/%d | Clip: %.0f\n",
                     lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame,
                     visibleEntities, totalEntities,
                     config_.constrainedConfig.clipDistance);
            text << geomBuf;
        }

        // Detail system info (grass, plants, debris)
        if (detailManager_ && detailManager_->isEnabled()) {
            std::string detailInfo = detailManager_->getDebugInfo();
            std::wstring wDetailInfo(detailInfo.begin(), detailInfo.end());
            text << wDetailInfo << L"\n";
        }

        // Current target display
        if (currentTargetId_ != 0) {
            text << L"\n--- TARGET ---\n";
            std::wstring wTargetName = EQT::toDisplayNameW(currentTargetName_);
            text << wTargetName << L" (ID: " << currentTargetId_ << L")";
            if (currentTargetLevel_ > 0) {
                text << L" Lvl " << (int)currentTargetLevel_;
            }
            text << L"\n";
            // HP bar
            text << L"HP: [";
            int barLen = 20;
            int filled = (currentTargetHpPercent_ * barLen) / 100;
            for (int i = 0; i < barLen; ++i) {
                text << (i < filled ? L"|" : L" ");
            }
            text << L"] " << (int)currentTargetHpPercent_ << L"%\n";

            // Extended target info (Admin mode shows all details)
            if (currentTargetInfo_.spawnId != 0) {
                // Race/Gender/Class
                std::string raceName = getRaceName(currentTargetInfo_.raceId);
                std::string genderName = getGenderName(currentTargetInfo_.gender);
                std::string className = getClassName(currentTargetInfo_.classId);
                std::wstring wRace(raceName.begin(), raceName.end());
                std::wstring wGender(genderName.begin(), genderName.end());
                text << L"Race: " << wRace << L" (" << currentTargetInfo_.raceId << L") " << wGender;
                if (!className.empty()) {
                    std::wstring wClass(className.begin(), className.end());
                    text << L" " << wClass;
                }
                text << L"\n";

                // Body/Helm/Texture variants
                text << L"Body: " << (int)currentTargetInfo_.bodyType
                     << L"  Tex: " << (int)currentTargetInfo_.texture
                     << L"  Helm: " << (int)currentTargetInfo_.helm;
                if (currentTargetInfo_.showHelm) {
                    text << L" (shown)";
                }
                text << L"\n";

                // Equipment slots (only show non-zero)
                bool hasEquip = false;
                for (int i = 0; i < 9; ++i) {
                    if (currentTargetInfo_.equipment[i] != 0) {
                        hasEquip = true;
                        break;
                    }
                }
                if (hasEquip) {
                    text << L"Equip: ";
                    static const wchar_t* slotNames[] = {
                        L"Hd", L"Ch", L"Ar", L"Wr", L"Hn", L"Lg", L"Ft", L"Pri", L"Sec"
                    };
                    bool first = true;
                    for (int i = 0; i < 9; ++i) {
                        if (currentTargetInfo_.equipment[i] != 0) {
                            if (!first) text << L" ";
                            text << slotNames[i] << L"=" << currentTargetInfo_.equipment[i];
                            first = false;
                        }
                    }
                    text << L"\n";
                }

                // Tints (only show non-zero, format as RGB hex)
                bool hasTint = false;
                for (int i = 0; i < 9; ++i) {
                    if (currentTargetInfo_.equipmentTint[i] != 0) {
                        hasTint = true;
                        break;
                    }
                }
                if (hasTint) {
                    text << L"Tint: ";
                    static const wchar_t* slotNames[] = {
                        L"Hd", L"Ch", L"Ar", L"Wr", L"Hn", L"Lg", L"Ft", L"Pri", L"Sec"
                    };
                    bool first = true;
                    for (int i = 0; i < 9; ++i) {
                        if (currentTargetInfo_.equipmentTint[i] != 0) {
                            if (!first) text << L" ";
                            uint32_t tint = currentTargetInfo_.equipmentTint[i];
                            // Format as RGB (ignore alpha)
                            wchar_t hexBuf[16];
                            swprintf(hexBuf, 16, L"%s=#%02X%02X%02X", slotNames[i],
                                     (tint >> 16) & 0xFF, (tint >> 8) & 0xFF, tint & 0xFF);
                            text << hexBuf;
                            first = false;
                        }
                    }
                    text << L"\n";
                }

                // Heading debug info - get real-time data from EntityRenderer
                if (entityRenderer_) {
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(currentTargetId_);
                    if (it != entities.end()) {
                        const EntityVisual& visual = it->second;
                        // Entity position (EQ coords: x, y, z)
                        text << L"Pos: (" << static_cast<int>(visual.serverX)
                             << L", " << static_cast<int>(visual.serverY)
                             << L", " << static_cast<int>(visual.serverZ) << L")\n";
                        // Server heading (from entity data, degrees 0-360)
                        wchar_t hdgBuf[64];
                        swprintf(hdgBuf, 64, L"Server Heading: %.1f deg\n", visual.serverHeading);
                        text << hdgBuf;
                        // Model rotation (from Irrlicht scene node)
                        if (visual.sceneNode) {
                            irr::core::vector3df rot = visual.sceneNode->getRotation();
                            swprintf(hdgBuf, 64, L"Model Rotation: (%.1f, %.1f, %.1f)\n",
                                     rot.X, rot.Y, rot.Z);
                            text << hdgBuf;
                        }
                        // Interpolated heading (visual.lastHeading)
                        swprintf(hdgBuf, 64, L"Interp Heading: %.1f deg\n", visual.lastHeading);
                        text << hdgBuf;
                    }
                }
            }
        }

        // External HUD callback
        if (hudCallback_) {
            text << hudCallback_();
        }

        // Admin mode hotkeys in upper right
        hotkeys << L"F1=Wire  F2=HUD  F3=Names\n";
        hotkeys << L"F4=Lights  F5=Cam  F6=Models\n";
        hotkeys << L"F9=Player  F12=Screenshot\n";
        hotkeys << L"[/]=AnimSpd  P=CorpseZ\n";
        hotkeys << L"{/}=Detail  /season";
    }

    hudText_->setText(text.str().c_str());
    if (hotkeysText_) {
        hotkeysText_->setText(hotkeys.str().c_str());
    }
    if (headingDebugText_) {
        headingDebugText_->setText(headingDebug.str().c_str());
    }
}

bool IrrlichtRenderer::loadZone(const std::string& zoneName, float progressStart, float progressEnd) {
    if (!initialized_) {
        LOG_ERROR(MOD_GRAPHICS, "Renderer not initialized");
        return false;
    }

    // Start zone load timing
    EQT::PerformanceMetrics::instance().markZoneLoadStart(zoneName);

    // Helper to scale internal progress (0.0-1.0) to the caller's specified range
    auto scaleProgress = [progressStart, progressEnd](float internalProgress) {
        return progressStart + internalProgress * (progressEnd - progressStart);
    };

    // Show initial loading screen
    drawLoadingScreen(scaleProgress(0.0f), L"Unloading previous zone...");

    unloadZone();

    // Build path to zone S3D
    std::string zonePath = config_.eqClientPath;
    if (!zonePath.empty() && zonePath.back() != '/' && zonePath.back() != '\\') {
        zonePath += '/';
    }
    zonePath += zoneName + ".s3d";

    drawLoadingScreen(scaleProgress(0.05f), L"Loading zone archive...");

    EQT::PerformanceMetrics::instance().startTimer("S3D Archive Load", EQT::MetricCategory::Zoning);
    S3DLoader loader;
    if (!loader.loadZone(zonePath)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to load zone: {}", loader.getError());
        EQT::PerformanceMetrics::instance().stopTimer("S3D Archive Load");
        return false;
    }
    EQT::PerformanceMetrics::instance().stopTimer("S3D Archive Load");

    drawLoadingScreen(scaleProgress(0.30f), L"Processing zone data...");

    currentZone_ = loader.getZone();
    currentZoneName_ = zoneName;

    // Notify entity renderer of zone change for zone-specific model loading
    if (entityRenderer_) {
        entityRenderer_->setCurrentZone(zoneName);
    }

    // Set zone data for door manager (for finding door meshes)
    if (doorManager_) {
        doorManager_->setZone(currentZone_);
    }

    // Sky initialization is deferred to setZoneEnvironment() which is called
    // after loadZone() with actual sky type from server NewZone packet

    drawLoadingScreen(scaleProgress(0.40f), L"Creating zone geometry...");
    EQT::PerformanceMetrics::instance().startTimer("Zone Mesh Creation", EQT::MetricCategory::Zoning);
    // Use PVS-based culling if available (falls back to combined mesh if not)
    createZoneMeshWithPvs();
    EQT::PerformanceMetrics::instance().stopTimer("Zone Mesh Creation");

    drawLoadingScreen(scaleProgress(0.60f), L"Creating object meshes...");
    EQT::PerformanceMetrics::instance().startTimer("Object Mesh Creation", EQT::MetricCategory::Zoning);
    createObjectMeshes();
    EQT::PerformanceMetrics::instance().stopTimer("Object Mesh Creation");

    drawLoadingScreen(scaleProgress(0.85f), L"Setting up zone lights...");
    EQT::PerformanceMetrics::instance().startTimer("Zone Lights Setup", EQT::MetricCategory::Zoning);
    createZoneLights();
    EQT::PerformanceMetrics::instance().stopTimer("Zone Lights Setup");

    drawLoadingScreen(scaleProgress(0.95f), L"Configuring camera...");

    // Position camera at zone center
    if (currentZone_ && currentZone_->geometry) {
        float centerX = (currentZone_->geometry->minX + currentZone_->geometry->maxX) / 2.0f;
        float centerY = (currentZone_->geometry->minY + currentZone_->geometry->maxY) / 2.0f;
        float maxZ = currentZone_->geometry->maxZ;
        float heightRange = currentZone_->geometry->maxZ - currentZone_->geometry->minZ;
        float cameraHeight = maxZ + std::max(200.0f, heightRange * 0.3f);

        camera_->setPosition(irr::core::vector3df(centerX, cameraHeight, centerY));
        camera_->setTarget(irr::core::vector3df(centerX, maxZ, centerY));

        LOG_INFO(MOD_GRAPHICS, "Zone loaded: {}", zoneName);
        LOG_INFO(MOD_GRAPHICS, "Vertices: {}", currentZone_->geometry->vertices.size());
        LOG_INFO(MOD_GRAPHICS, "Triangles: {}", currentZone_->geometry->triangles.size());
        LOG_INFO(MOD_GRAPHICS, "Objects: {}", currentZone_->objects.size());
        LOG_INFO(MOD_GRAPHICS, "Lights: {}", currentZone_->lights.size());
        LOG_DEBUG(MOD_GRAPHICS, "Zone bounds (EQ coords): X[{} to {}] Y[{} to {}] Z[{} to {}]",
                  currentZone_->geometry->minX, currentZone_->geometry->maxX,
                  currentZone_->geometry->minY, currentZone_->geometry->maxY,
                  currentZone_->geometry->minZ, currentZone_->geometry->minZ);
    }

    // Setup fog based on zone size
    setupFog();

    // Setup collision detection (includes zone mesh, objects, doors)
    // This also initializes the detail system
    setupZoneCollision();

    // Initialize tree wind animation system using placeable objects
    LOG_DEBUG(MOD_GRAPHICS, "Tree wind init check: treeManager_={}, currentZone_={}, objects={}",
              (treeManager_ ? "yes" : "no"),
              (currentZone_ ? "yes" : "no"),
              (currentZone_ ? std::to_string(currentZone_->objects.size()) : "n/a"));
    if (treeManager_ && currentZone_ && !currentZone_->objects.empty()) {
        treeManager_->loadConfig("", zoneName);  // Load zone-specific config
        treeManager_->initialize(currentZone_->objects, currentZone_->objectTextures);
        LOG_INFO(MOD_GRAPHICS, "Tree wind system: {} animated trees", treeManager_->getAnimatedTreeCount());
    }

    // Initialize weather system for this zone
    if (weatherSystem_) {
        weatherSystem_->setWeatherFromZone(zoneName);
    }

    // Initialize environmental particle system for this zone
    if (particleManager_) {
        Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(zoneName);
        particleManager_->onZoneEnter(zoneName, biome);
        LOG_INFO(MOD_GRAPHICS, "Environmental particles enabled for zone '{}' (biome: {})",
                 zoneName, static_cast<int>(biome));
    }

    // Initialize ambient creatures (boids) for this zone
    if (boidsManager_) {
        Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(zoneName);
        boidsManager_->setCollisionSelector(zoneTriangleSelector_);
        if (detailManager_) {
            boidsManager_->setSurfaceMap(detailManager_->getSurfaceMap());
        }
        boidsManager_->onZoneEnter(zoneName, biome);
        LOG_INFO(MOD_GRAPHICS, "Ambient creatures enabled for zone '{}' (biome: {})",
                 zoneName, static_cast<int>(biome));
    }

    // Initialize tumbleweeds for this zone (desert/plains only)
    if (tumbleweedManager_) {
        Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(zoneName);
        tumbleweedManager_->setCollisionSelector(zoneTriangleSelector_);
        if (detailManager_) {
            tumbleweedManager_->setSurfaceMap(detailManager_->getSurfaceMap());
        }
        tumbleweedManager_->onZoneEnter(zoneName, biome);
        LOG_INFO(MOD_GRAPHICS, "Tumbleweeds enabled for zone '{}' (biome: {})",
                 zoneName, static_cast<int>(biome));
    }

    drawLoadingScreen(scaleProgress(1.0f), L"Zone loaded!");

    // Log texture cache stats (cache was frozen at start of zone load)
    if (constrainedTextureCache_) {
        LOG_INFO(MOD_GRAPHICS, "Constrained texture cache - {} textures, {} bytes used (limit: {} bytes)",
                 constrainedTextureCache_->getTextureCount(),
                 constrainedTextureCache_->getCurrentUsage(),
                 constrainedTextureCache_->getMemoryLimit());
    }

    EQT::PerformanceMetrics::instance().markZoneLoadEnd();

    return true;
}

void IrrlichtRenderer::unloadZone() {
    // Reset entity loading state - we're starting a new zone
    networkReady_ = false;
    entitiesLoaded_ = false;
    expectedEntityCount_ = 0;
    loadedEntityCount_ = 0;
    zoneReady_ = false;

    // Reset animated texture manager
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
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    // Remove PVS region mesh nodes
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (node) {
            node->remove();
        }
    }
    regionMeshNodes_.clear();
    regionBoundingBoxes_.clear();

    if (fallbackMeshNode_) {
        fallbackMeshNode_->remove();
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

    // Clear BSP tree from entity renderer
    if (entityRenderer_) {
        entityRenderer_->clearBspTree();
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

    // Clear entity renderer
    if (entityRenderer_) {
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

    currentZone_.reset();
    currentZoneName_.clear();
}

void IrrlichtRenderer::setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                          const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                          const float fogMinClip[4], const float fogMaxClip[4]) {
    // Set sky type if sky renderer is available
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->setSkyType(skyType, currentZoneName_);

        // Determine if sky should be shown based on zone type
        // zoneType values from server: 1=Outdoors, 2=Dungeons, 255(0xFF)=Any/default
        // Only disable sky for explicit dungeon zones (type 2)
        bool isDungeon = (zoneType == 2);
        skyRenderer_->setEnabled(!isDungeon);

        LOG_DEBUG(MOD_GRAPHICS, "Zone environment: sky type {}, zone type {} ({}), sky {}",
                  skyType, zoneType, isDungeon ? "dungeon" : "outdoor",
                  isDungeon ? "disabled" : "enabled");
    }

    // Apply fog color from zone data, but keep our controlled distances
    // Zone provides atmosphere color, we control visibility distances
    if (driver_ && fogEnabled_) {
        irr::video::SColor fogColor(255, fogRed[0], fogGreen[0], fogBlue[0]);

        // Use our unified render distance system for fog distances
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

            // NOTE: Collision detection is now set up in setupZoneCollision() which should
            // be called AFTER objects and doors are created to include them in collision.

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

void IrrlichtRenderer::createZoneMeshWithPvs() {
    if (!currentZone_ || !currentZone_->wldLoader) {
        LOG_WARN(MOD_GRAPHICS, "Cannot create PVS mesh - no zone or WLD loader");
        createZoneMesh();  // Fall back to combined mesh
        return;
    }

    auto wldLoader = currentZone_->wldLoader;
    auto bspTree = wldLoader->getBspTree();

    if (!bspTree || bspTree->regions.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Cannot create PVS mesh - no BSP tree or regions");
        createZoneMesh();
        return;
    }

    if (!wldLoader->hasPvsData()) {
        LOG_INFO(MOD_GRAPHICS, "Zone has no PVS data, using combined mesh");
        createZoneMesh();
        return;
    }

    // Clean up existing mesh nodes
    if (zoneMeshNode_) {
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (node) {
            node->remove();
        }
    }
    regionMeshNodes_.clear();
    regionBoundingBoxes_.clear();

    if (fallbackMeshNode_) {
        fallbackMeshNode_->remove();
        fallbackMeshNode_ = nullptr;
    }

    // Store BSP tree for visibility queries
    zoneBspTree_ = bspTree;
    usePvsCulling_ = true;
    currentPvsRegion_ = SIZE_MAX;

    // Pass BSP tree to entity renderer for PVS-based entity culling
    if (entityRenderer_) {
        entityRenderer_->setBspTree(bspTree);
    }

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());

    // Pass constrained texture cache if in constrained mode
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());
    }

    // Count regions with geometry for progress tracking
    size_t regionsWithGeometry = 0;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        if (wldLoader->getGeometryForRegion(i)) {
            regionsWithGeometry++;
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Creating PVS mesh with {} regions ({} with geometry)",
        bspTree->regions.size(), regionsWithGeometry);

    // Create a mesh for each region that has geometry
    size_t createdMeshes = 0;
    for (size_t regionIdx = 0; regionIdx < bspTree->regions.size(); ++regionIdx) {
        auto geom = wldLoader->getGeometryForRegion(regionIdx);
        if (!geom || geom->vertices.empty()) {
            continue;
        }

        irr::scene::IMesh* mesh = nullptr;

        if (!currentZone_->textures.empty() && !geom->textureNames.empty()) {
            mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
        } else {
            mesh = builder.buildColoredMesh(*geom);
        }

        if (mesh) {
            // Use regular mesh scene node (not octree - regions are already spatial partitions)
            auto* node = smgr_->addMeshSceneNode(mesh);
            if (node) {
                // Apply mesh center offset to position the region correctly
                // EQ coords: (x, y, z) -> Irrlicht coords: (x, z, y)
                node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));

                // Log first 10 region mesh positions for debugging
                if (createdMeshes < 10) {
                    LOG_DEBUG(MOD_GRAPHICS, "Region {} mesh: EQ center=({:.1f},{:.1f},{:.1f}) bounds=({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f})",
                        regionIdx, geom->centerX, geom->centerY, geom->centerZ,
                        geom->minX, geom->minY, geom->minZ,
                        geom->maxX, geom->maxY, geom->maxZ);
                }

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

                regionMeshNodes_[regionIdx] = node;

                // Cache world-space bounding box in EQ coordinates for distance culling
                // World bounds = center + relative bounds
                irr::core::aabbox3df worldBounds;
                worldBounds.MinEdge.X = geom->centerX + geom->minX;
                worldBounds.MinEdge.Y = geom->centerY + geom->minY;
                worldBounds.MinEdge.Z = geom->centerZ + geom->minZ;
                worldBounds.MaxEdge.X = geom->centerX + geom->maxX;
                worldBounds.MaxEdge.Y = geom->centerY + geom->maxY;
                worldBounds.MaxEdge.Z = geom->centerZ + geom->maxZ;
                regionBoundingBoxes_[regionIdx] = worldBounds;

                createdMeshes++;
            }
            mesh->drop();
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Created {} region mesh nodes for PVS culling", createdMeshes);

    // Check for geometry not associated with any BSP region (fallback geometry)
    // This geometry should always be visible
    std::set<ZoneGeometry*> referencedGeometries;
    for (size_t regionIdx = 0; regionIdx < bspTree->regions.size(); ++regionIdx) {
        auto geom = wldLoader->getGeometryForRegion(regionIdx);
        if (geom) {
            referencedGeometries.insert(geom.get());
        }
    }

    // Count unreferenced geometries
    const auto& allGeometries = wldLoader->getGeometries();
    size_t unreferencedCount = 0;
    size_t unreferencedVerts = 0;
    for (const auto& geom : allGeometries) {
        if (referencedGeometries.find(geom.get()) == referencedGeometries.end()) {
            unreferencedCount++;
            unreferencedVerts += geom->vertices.size();
        }
    }

    if (unreferencedCount > 0) {
        LOG_WARN(MOD_GRAPHICS, "PVS: {} geometries ({} vertices) not referenced by any BSP region - creating fallback mesh",
            unreferencedCount, unreferencedVerts);

        // Build a combined mesh from unreferenced geometries
        auto fallbackGeom = std::make_shared<ZoneGeometry>();
        uint32_t vertexOffset = 0;

        for (const auto& geom : allGeometries) {
            if (referencedGeometries.find(geom.get()) == referencedGeometries.end()) {
                // Add vertices with center offset applied (world coordinates)
                for (const auto& v : geom->vertices) {
                    Vertex3D worldV = v;
                    worldV.x += geom->centerX;
                    worldV.y += geom->centerY;
                    worldV.z += geom->centerZ;
                    fallbackGeom->vertices.push_back(worldV);
                }

                // Add triangles with adjusted indices
                for (const auto& tri : geom->triangles) {
                    Triangle t = tri;
                    t.v1 += vertexOffset;
                    t.v2 += vertexOffset;
                    t.v3 += vertexOffset;
                    fallbackGeom->triangles.push_back(t);
                }

                // Copy texture info
                for (const auto& texName : geom->textureNames) {
                    fallbackGeom->textureNames.push_back(texName);
                }

                vertexOffset += static_cast<uint32_t>(geom->vertices.size());
            }
        }

        // Create the fallback mesh node (always visible)
        if (!fallbackGeom->vertices.empty()) {
            irr::scene::IMesh* fallbackMesh = nullptr;
            if (!currentZone_->textures.empty() && !fallbackGeom->textureNames.empty()) {
                fallbackMesh = builder.buildTexturedMesh(*fallbackGeom, currentZone_->textures);
            } else {
                fallbackMesh = builder.buildColoredMesh(*fallbackGeom);
            }

            if (fallbackMesh) {
                fallbackMeshNode_ = smgr_->addMeshSceneNode(fallbackMesh);
                if (fallbackMeshNode_) {
                    // Fallback geometry is already in world coords, position at origin
                    fallbackMeshNode_->setPosition(irr::core::vector3df(0, 0, 0));
                    fallbackMeshNode_->setVisible(true);  // Always visible

                    for (irr::u32 i = 0; i < fallbackMeshNode_->getMaterialCount(); ++i) {
                        fallbackMeshNode_->getMaterial(i).Lighting = lightingEnabled_;
                        fallbackMeshNode_->getMaterial(i).BackfaceCulling = false;
                    }

                    LOG_INFO(MOD_GRAPHICS, "Created fallback mesh with {} vertices, {} triangles",
                        fallbackGeom->vertices.size(), fallbackGeom->triangles.size());
                }
                fallbackMesh->drop();
            }
        }
    } else {
        LOG_INFO(MOD_GRAPHICS, "All {} geometries are referenced by BSP regions", allGeometries.size());
    }

    // Initialize animated texture manager for water and other animated textures
    if (currentZone_->geometry) {
        animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());

        // Initialize with zone geometry for WLD-defined animations
        int animCount = animatedTextureManager_->initialize(
            *currentZone_->geometry, currentZone_->textures, nullptr);

        // Auto-detect water texture animations by naming pattern (water01, water02, w1, w2, etc.)
        int waterAnimCount = animatedTextureManager_->detectWaterAnimations(
            currentZone_->textures, nullptr);
        animCount += waterAnimCount;

        if (animCount > 0) {
            LOG_DEBUG(MOD_GRAPHICS, "PVS: Initialized {} animated textures ({} water auto-detected)",
                      animCount, waterAnimCount);

            // Register all region mesh nodes for animated texture updates
            for (auto& [regionIdx, node] : regionMeshNodes_) {
                if (node) {
                    animatedTextureManager_->addSceneNode(node);
                }
            }
            if (fallbackMeshNode_) {
                animatedTextureManager_->addSceneNode(fallbackMeshNode_);
            }
        }
    }
}

void IrrlichtRenderer::updatePvsVisibility() {
    // DEBUG: Set to true to disable PVS culling and show all region meshes
    static bool disablePvsForDebug = false;
    if (disablePvsForDebug) {
        // Show all region meshes for debugging
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node) {
                node->setVisible(true);
            }
        }
        return;
    }

    if (!usePvsCulling_ || !zoneBspTree_ || regionMeshNodes_.empty()) {
        return;
    }

    // Get camera position (use player position for consistency)
    float camX = playerX_;
    float camY = playerY_;
    float camZ = playerZ_;

    // Track position for distance culling updates (separate from BSP region caching)
    static float lastDistCullX = -99999.0f, lastDistCullY = -99999.0f, lastDistCullZ = -99999.0f;

    // Cache BSP lookup - only recompute if position changed significantly (>5 units)
    static float lastBspX = -99999.0f, lastBspY = -99999.0f, lastBspZ = -99999.0f;
    static std::shared_ptr<EQT::Graphics::BspRegion> cachedRegion;

    // Check if we need to force update (e.g., render distance changed)
    if (forcePvsUpdate_) {
        // Reset all static tracking variables to force recalculation
        lastDistCullX = -99999.0f;
        lastDistCullY = -99999.0f;
        lastDistCullZ = -99999.0f;
        lastBspX = -99999.0f;
        lastBspY = -99999.0f;
        lastBspZ = -99999.0f;
        cachedRegion = nullptr;
        forcePvsUpdate_ = false;
        LOG_DEBUG(MOD_GRAPHICS, "Forcing PVS visibility update due to render distance change");
    }

    float dxCull = camX - lastDistCullX;
    float dyCull = camY - lastDistCullY;
    float dzCull = camZ - lastDistCullZ;
    float distCullSq = dxCull*dxCull + dyCull*dyCull + dzCull*dzCull;
    bool needsDistanceCullUpdate = (distCullSq > 25.0f);  // Update distance culling if moved >5 units

    float dx = camX - lastBspX;
    float dy = camY - lastBspY;
    float dz = camZ - lastBspZ;
    float distSq = dx*dx + dy*dy + dz*dz;

    std::shared_ptr<EQT::Graphics::BspRegion> region;
    if (distSq > 25.0f) {  // 5 units squared
        // Position changed enough, do BSP lookup
        region = zoneBspTree_->findRegionForPoint(camX, camY, camZ);
        cachedRegion = region;
        lastBspX = camX;
        lastBspY = camY;
        lastBspZ = camZ;
    } else {
        // Use cached result
        region = cachedRegion;
    }

    // Find region index
    size_t newRegionIdx = SIZE_MAX;
    if (region) {
        for (size_t i = 0; i < zoneBspTree_->regions.size(); ++i) {
            if (zoneBspTree_->regions[i].get() == region.get()) {
                newRegionIdx = i;
                break;
            }
        }
    }

    // Skip update if still in same region AND no distance cull update needed
    bool regionChanged = (newRegionIdx != currentPvsRegion_);
    if (!regionChanged && !needsDistanceCullUpdate) {
        return;
    }

    // Update distance culling position tracker
    if (needsDistanceCullUpdate) {
        lastDistCullX = camX;
        lastDistCullY = camY;
        lastDistCullZ = camZ;
    }

    currentPvsRegion_ = newRegionIdx;

    // If camera is outside all regions or no PVS data, skip PVS but still apply distance culling
    if (newRegionIdx == SIZE_MAX || region->visibleRegions.empty()) {
        size_t visibleCount = 0;
        size_t hiddenByDistCount = 0;
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) continue;

            // Still apply distance culling even without PVS
            bool inRenderDistance = true;
            auto bboxIt = regionBoundingBoxes_.find(regionIdx);
            if (bboxIt != regionBoundingBoxes_.end()) {
                const auto& bbox = bboxIt->second;
                float closestX = std::max(bbox.MinEdge.X, std::min(camX, bbox.MaxEdge.X));
                float closestY = std::max(bbox.MinEdge.Y, std::min(camY, bbox.MaxEdge.Y));
                float closestZ = std::max(bbox.MinEdge.Z, std::min(camZ, bbox.MaxEdge.Z));
                float dx = camX - closestX;
                float dy = camY - closestY;
                float dz = camZ - closestZ;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                inRenderDistance = (dist <= renderDistance_);
            }

            node->setVisible(inRenderDistance);
            if (inRenderDistance) {
                visibleCount++;
            } else {
                hiddenByDistCount++;
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "PVS: outside BSP/no PVS data -> {} visible, {} hidden by distance (renderDist={})",
            visibleCount, hiddenByDistCount, renderDistance_);
        return;
    }

    // Log PVS array details for debugging
    LOG_DEBUG(MOD_GRAPHICS, "PVS debug: region {} has visibleRegions.size()={}, regionMeshNodes_.size()={}",
        newRegionIdx, region->visibleRegions.size(), regionMeshNodes_.size());

    // Count how many regions the PVS says are visible
    size_t pvsVisibleCount = 0;
    for (size_t i = 0; i < region->visibleRegions.size(); ++i) {
        if (region->visibleRegions[i]) pvsVisibleCount++;
    }
    LOG_DEBUG(MOD_GRAPHICS, "PVS debug: region {} PVS marks {} regions as visible out of {}",
        newRegionIdx, pvsVisibleCount, region->visibleRegions.size());

    // Update visibility based on PVS data AND render distance
    // Zone geometry is culled if:
    //   1. PVS says it's not visible from current region (occlusion culling)
    //   2. Nearest edge of region bounding box is beyond render distance
    size_t visibleCount = 0;
    size_t hiddenByPvsCount = 0;
    size_t hiddenByDistCount = 0;
    size_t outOfRangeCount = 0;

    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (!node) continue;

        // Check if this region is visible from current region (PVS)
        bool pvsVisible = false;
        if (regionIdx == newRegionIdx) {
            // Always visible to self
            pvsVisible = true;
        } else if (regionIdx < region->visibleRegions.size()) {
            pvsVisible = region->visibleRegions[regionIdx];
        } else {
            // Region index outside PVS array - count for debugging
            outOfRangeCount++;
        }

        // Check distance to nearest edge of region bounding box (in EQ coords)
        bool inRenderDistance = true;
        auto bboxIt = regionBoundingBoxes_.find(regionIdx);
        if (bboxIt != regionBoundingBoxes_.end()) {
            const auto& bbox = bboxIt->second;
            // Find closest point on bounding box to camera (camX/Y/Z are EQ coords)
            float closestX = std::max(bbox.MinEdge.X, std::min(camX, bbox.MaxEdge.X));
            float closestY = std::max(bbox.MinEdge.Y, std::min(camY, bbox.MaxEdge.Y));
            float closestZ = std::max(bbox.MinEdge.Z, std::min(camZ, bbox.MaxEdge.Z));
            float dx = camX - closestX;
            float dy = camY - closestY;
            float dz = camZ - closestZ;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            inRenderDistance = (dist <= renderDistance_);
        }

        // Combine PVS and distance culling
        bool visible = pvsVisible && inRenderDistance;
        node->setVisible(visible);

        if (visible) {
            visibleCount++;
        } else if (!pvsVisible) {
            hiddenByPvsCount++;
        } else {
            hiddenByDistCount++;
        }
    }

    // Log warning if many regions are outside PVS array
    if (outOfRangeCount > 0) {
        LOG_WARN(MOD_GRAPHICS, "PVS: {} region meshes have index >= visibleRegions.size() ({})",
            outOfRangeCount, region->visibleRegions.size());
    }

    // Check if the current region has its own mesh
    bool currentRegionHasMesh = (regionMeshNodes_.find(newRegionIdx) != regionMeshNodes_.end());

    // Get the mesh position and bounds for the current region if it exists
    float meshX = 0, meshY = 0, meshZ = 0;
    bool camInMeshBounds = false;
    if (currentRegionHasMesh && currentZone_ && currentZone_->wldLoader) {
        auto pos = regionMeshNodes_[newRegionIdx]->getPosition();
        meshX = pos.X;
        meshY = pos.Y;
        meshZ = pos.Z;

        // Check if camera is within mesh bounds (in EQ coordinates)
        auto geom = currentZone_->wldLoader->getGeometryForRegion(newRegionIdx);
        if (geom) {
            // Mesh world bounds = center + relative bounds
            float worldMinX = geom->centerX + geom->minX;
            float worldMaxX = geom->centerX + geom->maxX;
            float worldMinY = geom->centerY + geom->minY;
            float worldMaxY = geom->centerY + geom->maxY;
            float worldMinZ = geom->centerZ + geom->minZ;
            float worldMaxZ = geom->centerZ + geom->maxZ;

            camInMeshBounds = (camX >= worldMinX && camX <= worldMaxX &&
                              camY >= worldMinY && camY <= worldMaxY &&
                              camZ >= worldMinZ && camZ <= worldMaxZ);

            LOG_DEBUG(MOD_GRAPHICS, "PVS region {} mesh bounds: ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}), cam in bounds: {}",
                newRegionIdx, worldMinX, worldMinY, worldMinZ, worldMaxX, worldMaxY, worldMaxZ, camInMeshBounds);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "PVS update: region {} (hasMesh={}) at cam({:.1f},{:.1f},{:.1f}) -> {} visible, {} hidden by PVS, {} hidden by distance, {} outOfRange",
        newRegionIdx, currentRegionHasMesh, camX, camY, camZ, visibleCount, hiddenByPvsCount, hiddenByDistCount, outOfRangeCount);

    // Log the first few visible and hidden region indices to spot patterns
    static size_t logCount = 0;
    if (logCount < 5) {
        std::string visibleStr, hiddenStr;
        size_t visibleLogged = 0, hiddenLogged = 0;
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) continue;
            if (node->isVisible() && visibleLogged < 10) {
                visibleStr += std::to_string(regionIdx) + " ";
                visibleLogged++;
            } else if (!node->isVisible() && hiddenLogged < 10) {
                hiddenStr += std::to_string(regionIdx) + " ";
                hiddenLogged++;
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "PVS sample: visible regions=[{}], hidden regions=[{}]",
            visibleStr, hiddenStr);
        logCount++;
    }

    // If current region has no mesh, log which visible regions are nearby the camera
    if (!currentRegionHasMesh && currentZone_ && currentZone_->wldLoader && visibleCount > 0) {
        size_t nearbyCount = 0;
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node || !node->isVisible()) continue;
            auto geom = currentZone_->wldLoader->getGeometryForRegion(regionIdx);
            if (geom) {
                // Check if this visible region's mesh contains the camera position
                float worldMinX = geom->centerX + geom->minX;
                float worldMaxX = geom->centerX + geom->maxX;
                float worldMinY = geom->centerY + geom->minY;
                float worldMaxY = geom->centerY + geom->maxY;
                float worldMinZ = geom->centerZ + geom->minZ;
                float worldMaxZ = geom->centerZ + geom->maxZ;

                if (camX >= worldMinX && camX <= worldMaxX &&
                    camY >= worldMinY && camY <= worldMaxY &&
                    camZ >= worldMinZ && camZ <= worldMaxZ) {
                    nearbyCount++;
                    if (nearbyCount <= 3) {
                        LOG_DEBUG(MOD_GRAPHICS, "  -> Visible region {} contains camera: bounds ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f})",
                            regionIdx, worldMinX, worldMinY, worldMinZ, worldMaxX, worldMaxY, worldMaxZ);
                    }
                }
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "  -> {} visible regions contain camera position", nearbyCount);
    }
}

void IrrlichtRenderer::createObjectMeshes() {
    if (!currentZone_) {
        return;
    }

    // Clear existing object nodes with proper reference counting
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

    // Clear object lights
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            objLight.node->remove();
        }
    }
    objectLights_.clear();

    // Clear vertex animated meshes
    vertexAnimatedMeshes_.clear();

    if (currentZone_->objects.empty()) {
        return;
    }

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());

    // Pass constrained texture cache if in constrained mode
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_.get());
    }

    std::map<std::string, irr::scene::IMesh*> meshCache;

    for (const auto& objInstance : currentZone_->objects) {
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        const std::string& objName = objInstance.placeable->getName();

        // Skip trees - they will be handled by the animated tree manager
        // Check is done even before tree manager is fully initialized since the
        // tree identifier patterns are available immediately after manager creation
        if (treeManager_) {
            std::string primaryTexture;
            if (!objInstance.geometry->textureNames.empty()) {
                primaryTexture = objInstance.geometry->textureNames[0];
            }
            if (treeManager_->isTreeObject(objName, primaryTexture)) {
                LOG_DEBUG(MOD_GRAPHICS, "[OBJ] Skipping tree '{}' - handled by tree manager", objName);
                continue;
            }
        }

        irr::scene::IMesh* mesh = nullptr;

        auto cacheIt = meshCache.find(objName);
        if (cacheIt != meshCache.end()) {
            mesh = cacheIt->second;
        } else {
            if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames.empty()) {
                mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
            } else {
                mesh = builder.buildColoredMesh(*objInstance.geometry);
            }
            if (mesh) {
                meshCache[objName] = mesh;
            }
        }

        if (!mesh) {
            continue;
        }

        irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
        if (!node) {
            continue;
        }

        // Get scale first (needed for height offset calculation)
        float scaleX = objInstance.placeable->getScaleX();
        float scaleY = objInstance.placeable->getScaleY();
        float scaleZ = objInstance.placeable->getScaleZ();
        node->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

        // With center baked into vertices (matching eqsage), we don't need height offset
        // The mesh origin is now at the bottom of the object
        irr::core::aabbox3df bbox = mesh->getBoundingBox();

        // Debug: log comprehensive object info
        const auto& geom = objInstance.geometry;
        LOG_DEBUG(MOD_GRAPHICS, "[OBJ] {} geomBounds=({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}) center=({:.1f},{:.1f},{:.1f})",
            objName, geom->minX, geom->minY, geom->minZ,
            geom->maxX, geom->maxY, geom->maxZ,
            geom->centerX, geom->centerY, geom->centerZ);
        LOG_DEBUG(MOD_GRAPHICS, "[OBJ] {} meshBbox=({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}) scale=({:.2f},{:.2f},{:.2f})",
            objName, bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
            bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z, scaleX, scaleY, scaleZ);
        LOG_DEBUG(MOD_GRAPHICS, "[OBJ] {} pos=({:.1f},{:.1f},{:.1f}) rot=({:.1f},{:.1f},{:.1f})",
            objName, objInstance.placeable->getX(), objInstance.placeable->getY(), objInstance.placeable->getZ(),
            objInstance.placeable->getRotateX(), objInstance.placeable->getRotateY(), objInstance.placeable->getRotateZ());

        // Apply coordinate transform: EQ (x, y, z) Z-up → Irrlicht (x, z, y) Y-up
        // Objects are placed at their raw ActorInstance positions (eqsage approach)
        float x = objInstance.placeable->getX();
        float y = objInstance.placeable->getY();
        float z = objInstance.placeable->getZ();

        // Position: EQ (x, y, z) → Irrlicht (x, z, y)
        // No height offset needed since center is baked into mesh vertices
        node->setPosition(irr::core::vector3df(x, z, y));

        float rotX = objInstance.placeable->getRotateX();  // Always 0
        float rotY = objInstance.placeable->getRotateY();  // Yaw (matches eqsage Location.rotateY)
        float rotZ = objInstance.placeable->getRotateZ();  // Secondary rotation (matches eqsage Location.rotateZ)
        // Transform internal representation → Irrlicht:
        // Internal (matches eqsage Location): rotateY = yaw around Z-up, rotateZ = secondary
        // Irrlicht: Y rotation = yaw around Y-up
        //
        // eqsage adds +180° for glTF (right-handed), but Irrlicht is left-handed like EQ,
        // so we do NOT add +180°. Just map internal rotateY → Irrlicht Y rotation.
        node->setRotation(irr::core::vector3df(rotX, rotY, rotZ));

        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            node->getMaterial(i).Lighting = lightingEnabled_;
            node->getMaterial(i).BackfaceCulling = false;
            node->getMaterial(i).GouraudShading = true;
            node->getMaterial(i).FogEnable = fogEnabled_;
            node->getMaterial(i).Wireframe = wireframeMode_;
            node->getMaterial(i).NormalizeNormals = true;
            // Set material colors for proper lighting response
            node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
            node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
        }

        // Add object mesh to animated texture manager for flame/water animations
        if (animatedTextureManager_ && objInstance.geometry) {
            animatedTextureManager_->addMesh(*objInstance.geometry, currentZone_->objectTextures, mesh);
            // Register the scene node for texture updates
            animatedTextureManager_->addSceneNode(node);
        }

        // Register vertex animated meshes (flags, banners, etc.)
        if (objInstance.geometry && objInstance.geometry->animatedVertices) {
            VertexAnimatedMesh vam;
            vam.node = node;
            vam.mesh = mesh;
            vam.animData = objInstance.geometry->animatedVertices;
            vam.elapsedMs = 0;
            vam.currentFrame = 0;
            vam.objectName = objName;

            // Build vertex mapping from mesh buffer vertices to animation vertices
            // The mesh has center baked in but animation positions are relative to center
            // Also, buildTexturedMesh() reorders vertices by texture, so we need mapping
            if (!vam.animData->frames.empty() && mesh->getMeshBufferCount() > 0) {
                const auto& frame0 = vam.animData->frames[0];
                size_t animVertCount = frame0.positions.size() / 3;

                // First, calculate center offset from geometry (it was logged but cleared)
                // We can recover it by finding the best match offset
                if (animVertCount > 0) {
                    irr::scene::IMeshBuffer* buffer0 = mesh->getMeshBuffer(0);
                    if (buffer0 && buffer0->getVertexCount() > 0) {
                        irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer0->getVertices());

                        // Find the center offset by matching the first mesh vertex to any anim vertex
                        float meshX = verts[0].Pos.X;
                        float meshY = verts[0].Pos.Y;  // Irrlicht Y = EQ Z
                        float meshZ = verts[0].Pos.Z;  // Irrlicht Z = EQ Y

                        float bestDist = 1e10f;
                        for (size_t av = 0; av < animVertCount; ++av) {
                            float animX = frame0.positions[av * 3 + 0];
                            float animY = frame0.positions[av * 3 + 1];
                            float animZ = frame0.positions[av * 3 + 2];

                            // Try this as the center offset
                            float offsetX = meshX - animX;
                            float offsetY = meshZ - animY;  // Irrlicht Z = EQ Y
                            float offsetZ = meshY - animZ;  // Irrlicht Y = EQ Z

                            // Check if this offset works for vertex 0
                            float dist = offsetX*offsetX + offsetY*offsetY + offsetZ*offsetZ;
                            if (dist < bestDist) {
                                bestDist = dist;
                                vam.centerOffsetX = offsetX;
                                vam.centerOffsetY = offsetY;
                                vam.centerOffsetZ = offsetZ;
                            }
                        }

                        LOG_DEBUG(MOD_GRAPHICS, "Vertex anim '{}' center offset: ({:.2f}, {:.2f}, {:.2f})",
                                  objName, vam.centerOffsetX, vam.centerOffsetY, vam.centerOffsetZ);
                    }
                }

                // Build vertex mapping with center offset applied
                vam.vertexMapping.resize(mesh->getMeshBufferCount());
                size_t totalMapped = 0;
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                    irr::u32 vertexCount = buffer->getVertexCount();

                    vam.vertexMapping[b].resize(vertexCount, SIZE_MAX);

                    for (irr::u32 mv = 0; mv < vertexCount; ++mv) {
                        // Mesh vertex is in Irrlicht coords
                        float meshX = verts[mv].Pos.X;
                        float meshY = verts[mv].Pos.Y;  // Irrlicht Y = EQ Z
                        float meshZ = verts[mv].Pos.Z;  // Irrlicht Z = EQ Y

                        // Find matching animation vertex (with center offset)
                        float bestDist = 1e10f;
                        size_t bestIdx = SIZE_MAX;
                        for (size_t av = 0; av < animVertCount; ++av) {
                            // Animation vertex in EQ coords + center offset
                            float animX = frame0.positions[av * 3 + 0] + vam.centerOffsetX;
                            float animY = frame0.positions[av * 3 + 1] + vam.centerOffsetY;
                            float animZ = frame0.positions[av * 3 + 2] + vam.centerOffsetZ;

                            // Compare: mesh(X,Y,Z) vs anim_centered(X,Z,Y) with coord transform
                            float dx = meshX - animX;
                            float dy = meshY - animZ;  // mesh Y (Irrlicht) = anim Z (EQ)
                            float dz = meshZ - animY;  // mesh Z (Irrlicht) = anim Y (EQ)
                            float dist = dx*dx + dy*dy + dz*dz;

                            if (dist < bestDist) {
                                bestDist = dist;
                                bestIdx = av;
                            }
                        }

                        if (bestDist < 1.0f) {  // Allow some tolerance
                            vam.vertexMapping[b][mv] = bestIdx;
                            totalMapped++;
                        }
                    }
                }

                LOG_DEBUG(MOD_GRAPHICS, "Vertex anim '{}' mapped {}/{} vertices",
                          objName, totalMapped, animVertCount);
            }

            vertexAnimatedMeshes_.push_back(vam);
            LOG_DEBUG(MOD_GRAPHICS, "Registered vertex animated mesh '{}' with {} frames", objName, vam.animData->frames.size());
        }

        // Store the object name in the scene node for later identification
        node->setName(objName.c_str());
        node->grab();  // Keep alive when removed from scene graph
        objectNodes_.push_back(node);
        objectPositions_.push_back(irr::core::vector3df(x, z, y));  // Cache position for distance culling

        // Update absolute transformation before getting bounding box
        node->updateAbsolutePosition();
        irr::core::aabbox3df worldBbox = node->getTransformedBoundingBox();
        objectBoundingBoxes_.push_back(worldBbox);  // Cache world-space bounding box

        // Debug: log bounding box for large objects
        irr::core::vector3df extent = worldBbox.getExtent();
        if (extent.X > 50 || extent.Y > 50 || extent.Z > 50) {
            LOG_DEBUG(MOD_GRAPHICS, "[PLACEABLE] {} bbox: min=({:.1f},{:.1f},{:.1f}) max=({:.1f},{:.1f},{:.1f}) extent=({:.1f},{:.1f},{:.1f})",
                objName, worldBbox.MinEdge.X, worldBbox.MinEdge.Y, worldBbox.MinEdge.Z,
                worldBbox.MaxEdge.X, worldBbox.MaxEdge.Y, worldBbox.MaxEdge.Z,
                extent.X, extent.Y, extent.Z);
        }
        objectInSceneGraph_.push_back(true);  // Initially in scene graph

        // Check if this object is a light source (torch, lantern, etc.)
        std::string upperName = objName;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

        bool isLightSource = false;
        irr::video::SColorf lightColor(1.0f, 0.6f, 0.2f, 1.0f);  // Default: warm orange
        float lightRadius = 100.0f;

        if (upperName.find("TORCH") != std::string::npos ||
            upperName.find("FIRE") != std::string::npos ||
            upperName.find("BRAZIER") != std::string::npos ||
            upperName.find("FLAME") != std::string::npos) {
            // Torches/fire - orange-red
            isLightSource = true;
            lightColor = irr::video::SColorf(1.0f, 0.5f, 0.15f, 1.0f);
            lightRadius = 120.0f;
        } else if (upperName.find("LANTERN") != std::string::npos ||
                   upperName.find("LAMP") != std::string::npos ||
                   upperName.find("LIGHT") != std::string::npos) {
            // Lanterns/lamps/lightpoles - warm yellow, reduced intensity (1/4 strength)
            isLightSource = true;
            lightColor = irr::video::SColorf(0.25f, 0.21f, 0.15f, 1.0f);
            lightRadius = 100.0f;
        } else if (upperName.find("CANDLE") != std::string::npos) {
            // Candles - soft yellow, smaller radius
            isLightSource = true;
            lightColor = irr::video::SColorf(1.0f, 0.9f, 0.7f, 1.0f);
            lightRadius = 50.0f;
        }

        if (isLightSource) {
            // Start with object's base position (EQ coords to Irrlicht: x, z, y)
            irr::core::vector3df lightPos(x, z, y);

            // Try to find a nearby zone light with the correct elevated position
            // Zone lights from WLD data have accurate light source positions (e.g., lantern height)
            if (currentZone_ && !currentZone_->lights.empty()) {
                float bestDist = 50.0f;  // Max horizontal distance to consider a match
                for (const auto& zoneLight : currentZone_->lights) {
                    // Calculate horizontal distance (ignore vertical)
                    float dx = zoneLight->x - x;
                    float dy = zoneLight->y - y;  // EQ Y is horizontal
                    float hDist = std::sqrt(dx * dx + dy * dy);
                    if (hDist < bestDist) {
                        bestDist = hDist;
                        // Use the zone light's position (transform EQ to Irrlicht)
                        lightPos = irr::core::vector3df(zoneLight->x, zoneLight->z, zoneLight->y);
                    }
                }
            }

            irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
                nullptr, lightPos, lightColor, lightRadius * 1.5f);  // Increase effective radius

            if (lightNode) {
                irr::video::SLight& lightData = lightNode->getLightData();
                lightData.Type = irr::video::ELT_POINT;
                // Attenuation: 1/(constant + linear*d + quadratic*d²)
                // constant=1 (full brightness at source), linear for gradual falloff
                lightData.Attenuation = irr::core::vector3df(1.0f, 0.007f, 0.0002f);
                lightNode->setVisible(false);  // Start hidden, updateObjectLights will enable nearby ones

                ObjectLight objLight;
                objLight.node = lightNode;
                objLight.position = lightPos;
                objLight.objectName = objName;
                objLight.originalColor = lightColor;  // Store for weather modification
                objectLights_.push_back(objLight);
            }
        }
    }

    for (auto& [name, mesh] : meshCache) {
        if (mesh) {
            mesh->drop();
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Placed {} object meshes in scene", objectNodes_.size());
    if (!objectLights_.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Created {} object light sources", objectLights_.size());
    }
}

void IrrlichtRenderer::createZoneLights() {
    // Clear existing zone lights
    for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
        if (zoneLightNodes_[i]) {
            // Remove from scene graph if still in it
            if (i < zoneLightInSceneGraph_.size() && zoneLightInSceneGraph_[i]) {
                zoneLightNodes_[i]->remove();
            }
            zoneLightNodes_[i]->drop();  // Release our reference
        }
    }
    zoneLightNodes_.clear();
    zoneLightPositions_.clear();
    zoneLightInSceneGraph_.clear();

    if (!currentZone_ || currentZone_->lights.empty()) {
        return;
    }

    // Create ALL zone lights - unified light management in updateObjectLights()
    // will select the closest ones based on distance and hardware limits
    for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
        const auto& light = currentZone_->lights[i];

        // Transform EQ coordinates (Z-up) to Irrlicht (Y-up)
        // EQ: x, y, z -> Irrlicht: x, z, y
        irr::core::vector3df pos(light->x, light->z, light->y);

        // Create point light at full intensity - updateZoneLightColors() will apply vision-based intensity
        irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
            nullptr,
            pos,
            irr::video::SColorf(light->r, light->g, light->b, 1.0f),
            light->radius
        );

        if (lightNode) {
            irr::video::SLight& lightData = lightNode->getLightData();
            lightData.Type = irr::video::ELT_POINT;
            lightData.Attenuation = irr::core::vector3df(1.0f, 0.0f, 0.00001f);

            // Start hidden - updateObjectLights() manages visibility
            lightNode->setVisible(false);

            lightNode->grab();  // Keep alive when removed from scene graph
            zoneLightNodes_.push_back(lightNode);
            zoneLightPositions_.push_back(lightNode->getPosition());  // Cache position
            zoneLightInSceneGraph_.push_back(true);  // Initially in scene graph
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Created {} zone lights (of {} available)", zoneLightNodes_.size(), currentZone_->lights.size());

    // Enable lighting and zone lights by default so vision system works on initial load
    if (!zoneLightNodes_.empty()) {
        lightingEnabled_ = true;
        zoneLightsEnabled_ = true;

        // Update zone mesh materials to enable lighting
        if (zoneMeshNode_) {
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = true;
            }
        }

        // Update PVS region mesh materials to enable lighting
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

        // Update object mesh materials to enable lighting
        for (auto* node : objectNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                }
            }
        }

        // Update entity materials to enable lighting
        if (entityRenderer_) {
            entityRenderer_->setLightingEnabled(true);
        }
    }

    // Apply vision-based intensity and color adjustments
    updateZoneLightColors();
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

        // In Player mode with FirstPerson camera, hide the player entity
        bool shouldHide = (rendererMode_ == RendererMode::Player && cameraMode_ == CameraMode::FirstPerson);
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
                // Attenuation: 1/(constant + linear*d + quadratic*d²)
                // constant=1 for full brightness at source
                lightData.Attenuation = irr::core::vector3df(1.0f, 0.007f, 0.0002f);
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

        // Apply correct visibility based on current mode
        bool shouldHide = (rendererMode_ == RendererMode::Player && cameraMode_ == CameraMode::FirstPerson);
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

void IrrlichtRenderer::updateObjectLightColors() {
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

            irr::video::SLight& lightData = objLight.node->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r, g, b, 1.0f);
        }
    }

    static float lastLoggedMod = -1.0f;
    if (std::abs(weatherMod - lastLoggedMod) > 0.01f && weatherMod < 0.99f) {
        LOG_DEBUG(MOD_GRAPHICS, "Updated {} object lights: weatherMod={:.2f}",
                  objectLights_.size(), weatherMod);
        lastLoggedMod = weatherMod;
    }
}

void IrrlichtRenderer::setPlayerPosition(float x, float y, float z, float heading) {
    playerX_ = x;
    playerY_ = y;
    playerZ_ = z;
    playerHeading_ = heading;

    LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerPosition: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} (EQ units, EQ heading 0-512)",
             x, y, z, heading);

    // Trust the server's Z position - the server places entities at a consistent height
    // that accounts for model placement. Ground-snapping was incorrectly overriding
    // the server Z (e.g., snapping from 3.75 to 0) causing player/NPC Z mismatch.

    // Check if player is within zone geometry bounds before following
    bool playerInBounds = true;
    if (currentZone_ && currentZone_->geometry) {
        const auto& geom = *currentZone_->geometry;
        // Use a margin around the zone bounds (player can be slightly outside)
        float margin = 500.0f;
        if (x < geom.minX - margin || x > geom.maxX + margin ||
            y < geom.minY - margin || y > geom.maxY + margin) {
            playerInBounds = false;
        }
    }

    if (cameraMode_ == CameraMode::Follow && playerInBounds) {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera mode=Follow, calling setFollowPosition({:.2f},{:.2f},{:.2f},{:.2f})",
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
}

void IrrlichtRenderer::setCameraMode(CameraMode mode) {
    cameraMode_ = mode;

    // In Player mode, show/hide player entity based on camera mode
    if (rendererMode_ == RendererMode::Player && entityRenderer_) {
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

    // In Player mode, show/hide player entity based on camera mode
    if (rendererMode_ == RendererMode::Player && entityRenderer_) {
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
    auto sectionStart = frameStart;
    auto sectionEnd = frameStart;

    // Helper to measure section time in microseconds
    auto measureSection = [&]() -> int64_t {
        sectionEnd = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(sectionEnd - sectionStart).count();
        sectionStart = sectionEnd;
        return us;
    };

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

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checking isRunning...");

    if (!isRunning()) {
        LOG_INFO(MOD_GRAPHICS, "processFrame: isRunning() returned false");
        LOG_INFO(MOD_GRAPHICS, "initialized_={} device_={} device_run={} quitRequested={}",
                  initialized_, (device_ ? "valid" : "null"), (device_ ? device_->run() : false),
                  (eventReceiver_ ? eventReceiver_->quitRequested() : false));
        return false;
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: isRunning check passed");

    LOG_TRACE(MOD_GRAPHICS, "processFrame: getting timer...");

    // Update FPS
    irr::u32 currentTime = device_->getTimer()->getTime();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: timer ok, time={}", currentTime);

    frameCount_++;
    if (currentTime - lastFpsTime_ >= 1000) {
        currentFps_ = frameCount_;
        frameCount_ = 0;
        lastFpsTime_ = currentTime;
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checking input events...");

    // Handle input
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check wireframe...");
    if (eventReceiver_->wireframeToggleRequested()) {
        toggleWireframe();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check hud...");
    if (eventReceiver_->hudToggleRequested()) {
        toggleHUD();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check nametags...");
    if (eventReceiver_->nameTagToggleRequested()) {
        toggleNameTags();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check zonelights...");
    if (eventReceiver_->zoneLightsToggleRequested()) {
        toggleZoneLights();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check cycleObjectLights...");
    if (eventReceiver_->cycleObjectLightsRequested()) {
        cycleObjectLights();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check lighting...");
    if (eventReceiver_->lightingToggleRequested()) {
        toggleLighting();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: input events done");
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check screenshot...");
    if (eventReceiver_->screenshotRequested()) {
        saveScreenshot("screenshot.png");
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check cameraMode...");
    if (eventReceiver_->cameraModeToggleRequested()) {
        cycleCameraMode();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check oldModels...");
    if (eventReceiver_->oldModelsToggleRequested()) {
        toggleOldModels();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check saveEntities...");
    if (eventReceiver_->saveEntitiesRequested() && saveEntitiesCallback_) {
        saveEntitiesCallback_();
    }

    // ===== FRAME TIMING: Input Handling =====
    if (frameTimingEnabled_) frameTimings_.inputHandling = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: check clearTarget...");
    // Handle clear target (Escape key)
    if (eventReceiver_->clearTargetRequested()) {
        // In Repair mode, ESC clears the repair target
        if (rendererMode_ == RendererMode::Repair) {
            if (repairTargetNode_) {
                clearRepairTarget();
            }
        } else {
            // In other modes, ESC clears entity target
            if (currentTargetId_ != 0) {
                LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                clearCurrentTarget();
                SetTrackedTargetId(0);  // Clear tracked target for debug logging
            }
        }
    }

    // Handle targeting hotkeys (F1-F8, Tab) - only in Player mode
    if (rendererMode_ == RendererMode::Player) {
        // F1 - Target Self
        if (eventReceiver_->targetSelfRequested() && targetSelfCallback_) {
            LOG_DEBUG(MOD_INPUT, "F1 pressed - calling targetSelfCallback_");
            targetSelfCallback_();
        }
        // F2-F6 - Target Group Members
        if (eventReceiver_->targetGroupMember1Requested() && targetGroupMemberCallback_) {
            LOG_DEBUG(MOD_INPUT, "F2 pressed - calling targetGroupMemberCallback_(0)");
            targetGroupMemberCallback_(0);
        }
        if (eventReceiver_->targetGroupMember2Requested() && targetGroupMemberCallback_) {
            LOG_DEBUG(MOD_INPUT, "F3 pressed - calling targetGroupMemberCallback_(1)");
            targetGroupMemberCallback_(1);
        }
        if (eventReceiver_->targetGroupMember3Requested() && targetGroupMemberCallback_) {
            LOG_DEBUG(MOD_INPUT, "F4 pressed - calling targetGroupMemberCallback_(2)");
            targetGroupMemberCallback_(2);
        }
        if (eventReceiver_->targetGroupMember4Requested() && targetGroupMemberCallback_) {
            LOG_DEBUG(MOD_INPUT, "F5 pressed - calling targetGroupMemberCallback_(3)");
            targetGroupMemberCallback_(3);
        }
        if (eventReceiver_->targetGroupMember5Requested() && targetGroupMemberCallback_) {
            LOG_DEBUG(MOD_INPUT, "F6 pressed - calling targetGroupMemberCallback_(4)");
            targetGroupMemberCallback_(4);
        }
        // F7 - Target Nearest PC
        if (eventReceiver_->targetNearestPCRequested() && targetNearestPCCallback_) {
            LOG_DEBUG(MOD_INPUT, "F7 pressed - calling targetNearestPCCallback_");
            targetNearestPCCallback_();
        }
        // F8 - Target Nearest NPC
        if (eventReceiver_->targetNearestNPCRequested() && targetNearestNPCCallback_) {
            LOG_DEBUG(MOD_INPUT, "F8 pressed - calling targetNearestNPCCallback_");
            targetNearestNPCCallback_();
        }
        // Tab - Cycle Targets
        if (eventReceiver_->cycleTargetsRequested() && cycleTargetsCallback_) {
            LOG_DEBUG(MOD_INPUT, "Tab pressed - calling cycleTargetsCallback_(false)");
            cycleTargetsCallback_(false);
        }
        // Shift+Tab - Cycle Targets Reverse
        if (eventReceiver_->cycleTargetsReverseRequested() && cycleTargetsCallback_) {
            LOG_DEBUG(MOD_INPUT, "Shift+Tab pressed - calling cycleTargetsCallback_(true)");
            cycleTargetsCallback_(true);
        }
    }

    // Handle Repair mode controls (only when in Repair mode with a target)
    if (rendererMode_ == RendererMode::Repair && repairTargetNode_) {
        // Rotation: X/Y/Z keys (Shift for negative direction)
        float rotX = eventReceiver_->getRepairRotateXDelta();
        float rotY = eventReceiver_->getRepairRotateYDelta();
        float rotZ = eventReceiver_->getRepairRotateZDelta();
        if (rotX != 0.0f || rotY != 0.0f || rotZ != 0.0f) {
            applyRepairRotation(rotX, rotY, rotZ);
        }

        // Flip toggles: Ctrl+1/2/3 keys
        if (eventReceiver_->repairFlipXRequested()) {
            toggleRepairFlip(0);  // X axis
        }
        if (eventReceiver_->repairFlipYRequested()) {
            toggleRepairFlip(1);  // Y axis
        }
        if (eventReceiver_->repairFlipZRequested()) {
            toggleRepairFlip(2);  // Z axis
        }

        // Reset: Ctrl+R key
        if (eventReceiver_->repairResetRequested()) {
            resetRepairAdjustments();
        }
    } else {
        // Consume the events even if not in Repair mode to prevent accumulation
        eventReceiver_->getRepairRotateXDelta();
        eventReceiver_->getRepairRotateYDelta();
        eventReceiver_->getRepairRotateZDelta();
        eventReceiver_->repairFlipXRequested();
        eventReceiver_->repairFlipYRequested();
        eventReceiver_->repairFlipZRequested();
        eventReceiver_->repairResetRequested();
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: check rendererMode...");

    // Handle renderer mode toggle (F9)
    if (eventReceiver_->rendererModeToggleRequested()) {
        toggleRendererMode();
    }
    LOG_TRACE(MOD_GRAPHICS, "processFrame: check autorun...");

    // Check if chat input is focused (skip game hotkeys if so)
    bool chatInputFocused = windowManager_ && windowManager_->isChatInputFocused();

    // Handle autorun toggle (R or Numlock) - only in Player mode, not when chat focused
    if (eventReceiver_->autorunToggleRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        playerMovement_.autorun = !playerMovement_.autorun;
        LOG_INFO(MOD_GRAPHICS, "Autorun: {}", (playerMovement_.autorun ? "ON" : "OFF"));
    }

    // Handle auto attack toggle (` backtick key) - only in Player mode, not when chat focused
    if (eventReceiver_->autoAttackToggleRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (autoAttackCallback_) {
            autoAttackCallback_();
        }
    }

    // Handle hail (H key) - only in Player mode, not when chat focused
    if (eventReceiver_->hailRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (hailCallback_) {
            hailCallback_();
        }
    }

    // Handle consider (C key) - only in Player mode, not when chat focused
    if (eventReceiver_->considerRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (considerCallback_) {
            considerCallback_();
        }
    }

    // Handle vendor toggle (V key) - only in Player mode, not when chat focused
    if (eventReceiver_->vendorToggleRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (vendorToggleCallback_) {
            vendorToggleCallback_();
        }
    }

    // Handle trainer toggle (T key) - only in Player mode, not when chat focused
    if (eventReceiver_->trainerToggleRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (trainerToggleCallback_) {
            trainerToggleCallback_();
        }
    }

    // Handle collision debug controls - only in Player mode, not when chat focused
    if (rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (eventReceiver_->collisionToggleRequested()) {
            playerConfig_.collisionEnabled = !playerConfig_.collisionEnabled;
            LOG_INFO(MOD_GRAPHICS, "Collision: {}", (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"));
        }
        if (eventReceiver_->collisionDebugToggleRequested()) {
            playerConfig_.collisionDebug = !playerConfig_.collisionDebug;
            LOG_INFO(MOD_GRAPHICS, "Collision Debug: {}", (playerConfig_.collisionDebug ? "ON" : "OFF"));
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "  Collision Height: {}", playerConfig_.collisionCheckHeight);
                LOG_INFO(MOD_GRAPHICS, "  Step Height: {}", playerConfig_.collisionStepHeight);
                LOG_INFO(MOD_GRAPHICS, "  Controls: C=toggle collision, Ctrl+C=toggle debug");
                LOG_INFO(MOD_GRAPHICS, "            T/G=collision height +/-, Y/B=step height +/-");
            }
        }
        float collisionHeightDelta = eventReceiver_->getCollisionHeightDelta();
        if (collisionHeightDelta != 0.0f) {
            playerConfig_.collisionCheckHeight += collisionHeightDelta;
            if (playerConfig_.collisionCheckHeight < 0.5f) playerConfig_.collisionCheckHeight = 0.5f;
            LOG_INFO(MOD_GRAPHICS, "Collision Check Height: {}", playerConfig_.collisionCheckHeight);
        }
        float stepHeightDelta = eventReceiver_->getStepHeightDelta();
        if (stepHeightDelta != 0.0f) {
            playerConfig_.collisionStepHeight += stepHeightDelta;
            if (playerConfig_.collisionStepHeight < 0.5f) playerConfig_.collisionStepHeight = 0.5f;
            LOG_INFO(MOD_GRAPHICS, "Step Height: {}", playerConfig_.collisionStepHeight);
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint A (after collision controls)");

    // Handle animation speed adjustments ([ and ] keys) - not when chat focused
    if (!chatInputFocused) {
        float animSpeedDelta = eventReceiver_->getAnimSpeedDelta();
        if (animSpeedDelta != 0.0f && entityRenderer_) {
            entityRenderer_->adjustGlobalAnimationSpeed(animSpeedDelta);
        }
    }

    // Handle particle multiplier adjustments (Ctrl+- and Ctrl+=)
    float particleDelta = eventReceiver_->getParticleMultiplierDelta();
    if (particleDelta != 0.0f && spellVisualFX_) {
        spellVisualFX_->adjustParticleMultiplier(particleDelta);
    }

    // Handle ambient light adjustments (Page Up/Down are OK even when chat focused - they're not text)
    float ambientDelta = eventReceiver_->getAmbientLightDelta();
    if (ambientDelta != 0.0f) {
        ambientMultiplier_ = std::max(0.0f, std::min(3.0f, ambientMultiplier_ + ambientDelta));
        // Force time of day update to apply new multiplier
        updateTimeOfDay(currentHour_, currentMinute_);
        LOG_INFO(MOD_GRAPHICS, "Ambient light multiplier: {}", ambientMultiplier_);
    }

    // Handle detail density adjustments ([ = decrease, ] = increase)
    float detailDelta = eventReceiver_->getDetailDensityDelta();
    if (detailDelta != 0.0f && detailManager_) {
        detailManager_->adjustDensity(detailDelta);
        LOG_INFO(MOD_GRAPHICS, "Detail density: {:.0f}%", detailManager_->getDensity() * 100.0f);
    }

    // Handle corpse Z offset adjustments (P = raise, Shift+P = lower) - not when chat focused
    if (!chatInputFocused) {
        float corpseZDelta = eventReceiver_->getCorpseZOffsetDelta();
        if (corpseZDelta != 0.0f && entityRenderer_) {
            entityRenderer_->adjustCorpseZOffset(corpseZDelta);
        }
    }

    // Handle eye height adjustments (Y = raise, Shift+Y = lower) - not when chat focused
    if (!chatInputFocused) {
        float eyeHeightDelta = eventReceiver_->getEyeHeightDelta();
        if (eyeHeightDelta != 0.0f) {
            playerConfig_.eyeHeight += eyeHeightDelta;
            if (playerConfig_.eyeHeight < 0.0f) playerConfig_.eyeHeight = 0.0f;
            LOG_INFO(MOD_GRAPHICS, "Eye height: {:.1f}", playerConfig_.eyeHeight);
        }
    }

    // Handle camera zoom adjustments (+/- keys) - only in Player/Repair mode with Follow camera
    if (!chatInputFocused && (rendererMode_ == RendererMode::Player || rendererMode_ == RendererMode::Repair)) {
        float zoomDelta = eventReceiver_->getCameraZoomDelta();
        if (zoomDelta != 0.0f && cameraController_ && cameraMode_ == CameraMode::Follow) {
            cameraController_->adjustFollowDistance(zoomDelta);
            // Immediately update the view with the new zoom distance
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_, deltaTime);
            LOG_DEBUG(MOD_GRAPHICS, "Camera zoom distance: {:.1f}", cameraController_->getFollowDistance());
        }
    }

    // Handle helm debug mode toggle (F7 is OK even when chat focused - it's a function key)
    if (eventReceiver_->helmDebugToggleRequested() && entityRenderer_) {
        bool newState = !entityRenderer_->isHelmDebugEnabled();
        entityRenderer_->setHelmDebugEnabled(newState);
        LOG_INFO(MOD_GRAPHICS, "Helm debug mode: {}", (newState ? "ENABLED" : "DISABLED"));
        if (newState) {
            LOG_INFO(MOD_GRAPHICS, "Helm UV Controls (hold Shift for fine adjustment):");
            LOG_INFO(MOD_GRAPHICS, "  I/K: U offset (decrease/increase)");
            LOG_INFO(MOD_GRAPHICS, "  J/L: V offset (decrease/increase)");
            LOG_INFO(MOD_GRAPHICS, "  O/P: U scale (decrease/increase)");
            LOG_INFO(MOD_GRAPHICS, "  ,/.: V scale (decrease/increase)");
            LOG_INFO(MOD_GRAPHICS, "  -/=: Rotation (CCW/CW, 15 deg steps)");
            LOG_INFO(MOD_GRAPHICS, "  F8: Print state");
            LOG_INFO(MOD_GRAPHICS, "  0: Reset all");
            LOG_INFO(MOD_GRAPHICS, "  Ctrl+S: Swap UV");
            LOG_INFO(MOD_GRAPHICS, "  Ctrl+V: Toggle V flip");
            LOG_INFO(MOD_GRAPHICS, "  Ctrl+U: Toggle U flip");
            LOG_INFO(MOD_GRAPHICS, "  H/N: Cycle head variant (prev/next)");
            entityRenderer_->printHelmDebugState();
        }
    }

    // Handle helm debug adjustments - only in Admin mode, not when chat focused (uses letter/number keys)
    if (rendererMode_ == RendererMode::Admin && entityRenderer_ && entityRenderer_->isHelmDebugEnabled() && !chatInputFocused) {
        float uDelta = eventReceiver_->getHelmUOffsetDelta();
        float vDelta = eventReceiver_->getHelmVOffsetDelta();
        float uScaleDelta = eventReceiver_->getHelmUScaleDelta();
        float vScaleDelta = eventReceiver_->getHelmVScaleDelta();
        float rotDelta = eventReceiver_->getHelmRotationDelta();

        if (uDelta != 0.0f) {
            entityRenderer_->adjustHelmUOffset(uDelta);
            entityRenderer_->applyHelmUVTransform();
        }
        if (vDelta != 0.0f) {
            entityRenderer_->adjustHelmVOffset(vDelta);
            entityRenderer_->applyHelmUVTransform();
        }
        if (uScaleDelta != 0.0f) {
            entityRenderer_->adjustHelmUScale(uScaleDelta);
            entityRenderer_->applyHelmUVTransform();
        }
        if (vScaleDelta != 0.0f) {
            entityRenderer_->adjustHelmVScale(vScaleDelta);
            entityRenderer_->applyHelmUVTransform();
        }
        if (rotDelta != 0.0f) {
            entityRenderer_->adjustHelmRotation(rotDelta);
            entityRenderer_->applyHelmUVTransform();
        }
        if (eventReceiver_->helmUVSwapRequested()) {
            entityRenderer_->toggleHelmUVSwap();
            entityRenderer_->applyHelmUVTransform();
        }
        if (eventReceiver_->helmVFlipRequested()) {
            entityRenderer_->toggleHelmVFlip();
            entityRenderer_->applyHelmUVTransform();
        }
        if (eventReceiver_->helmUFlipRequested()) {
            entityRenderer_->toggleHelmUFlip();
            entityRenderer_->applyHelmUVTransform();
        }
        if (eventReceiver_->helmResetRequested()) {
            entityRenderer_->resetHelmUVParams();
            entityRenderer_->applyHelmUVTransform();
        }
        if (eventReceiver_->helmPrintStateRequested()) {
            entityRenderer_->printHelmDebugState();
        }

        int variantDelta = eventReceiver_->getHeadVariantCycleDelta();
        if (variantDelta != 0) {
            entityRenderer_->cycleHeadVariant(variantDelta);
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint B (before camera update)");

    // ===== FRAME TIMING: Input Handling (accumulated from all prior input handling) =====
    if (frameTimingEnabled_) frameTimings_.inputHandling += measureSection();

    // Check if chat input has focus (skip movement keys if so)
    bool chatHasFocus = windowManager_ && windowManager_->isChatInputFocused();

    // Handle window manager mouse capture BEFORE camera/movement updates
    // This ensures UI interactions don't also move the camera/player
    bool hadClick = eventReceiver_->wasLeftButtonClicked();
    bool hadRelease = eventReceiver_->wasLeftButtonReleased();
    int clickX = eventReceiver_->getClickMouseX();
    int clickY = eventReceiver_->getClickMouseY();

    if (windowManager_) {
        // Handle mouse down on click - check if window captures it
        if (hadClick) {
            bool shift = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
            bool ctrl = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
            windowManagerCapture_ = windowManager_->handleMouseDown(clickX, clickY, true, shift, ctrl);
        }

        // Handle mouse up on release
        if (hadRelease) {
            int mouseX = eventReceiver_->getMouseX();
            int mouseY = eventReceiver_->getMouseY();
            windowManager_->handleMouseUp(mouseX, mouseY, true);
            windowManagerCapture_ = false;
        }
    }

    // Update camera based on renderer mode
    if (rendererMode_ == RendererMode::Admin) {
        // Admin mode: Free camera when in Free mode
        if (cameraMode_ == CameraMode::Free && cameraController_) {
            // Mouse look enabled when any of these conditions are met and UI doesn't have capture:
            // 1. Left mouse button held (current WillEQ behavior)
            // 2. Right mouse button held (traditional 3D game control)
            // 3. Ctrl+Left mouse button (single-button mouse workaround)
            bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
            bool mouseEnabled = (eventReceiver_->isLeftButtonDown() ||
                                 eventReceiver_->isRightButtonDown() ||
                                 (ctrlHeld && eventReceiver_->isLeftButtonDown())) &&
                                !windowManagerCapture_;
            int mouseDeltaX = windowManagerCapture_ ? 0 : eventReceiver_->getMouseDeltaX();
            int mouseDeltaY = windowManagerCapture_ ? 0 : eventReceiver_->getMouseDeltaY();

            // Use HotkeyManager for admin camera movement bindings
            auto& hotkeyMgr = eqt::input::HotkeyManager::instance();

            // Helper to check if any binding for an action is currently held
            auto isAdminActionHeld = [&](eqt::input::HotkeyAction action) -> bool {
                if (chatHasFocus) return false;
                bool ctrl = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
                bool shift = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
                bool alt = eventReceiver_->isKeyDown(irr::KEY_LMENU) || eventReceiver_->isKeyDown(irr::KEY_RMENU);

                for (const auto& binding : hotkeyMgr.getBindingsForAction(action)) {
                    if (!eventReceiver_->isKeyDown(binding.keyCode)) continue;

                    bool needsCtrl = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Ctrl);
                    bool needsShift = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Shift);
                    bool needsAlt = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Alt);

                    if (ctrl == needsCtrl && shift == needsShift && alt == needsAlt) {
                        return true;
                    }
                }
                return false;
            };

            bool forward = isAdminActionHeld(eqt::input::HotkeyAction::CameraForward);
            bool backward = isAdminActionHeld(eqt::input::HotkeyAction::CameraBackward);
            bool left = isAdminActionHeld(eqt::input::HotkeyAction::CameraLeft);
            bool right = isAdminActionHeld(eqt::input::HotkeyAction::CameraRight);
            bool up = isAdminActionHeld(eqt::input::HotkeyAction::CameraUp);
            bool down = isAdminActionHeld(eqt::input::HotkeyAction::CameraDown);

            cameraController_->update(deltaTime, forward, backward, left, right,
                                      up, down, mouseDeltaX, mouseDeltaY, mouseEnabled);
        }
        // Admin mode: Update name tags by distance only
        if (entityRenderer_) {
            entityRenderer_->updateNameTags(camera_);
        }
    } else {
        // Player mode: Handle movement with collision
        updatePlayerMovement(deltaTime);
        // Player mode: Update name tags with LOS checking
        updateNameTagsWithLOS(deltaTime);
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint C (after camera update)");

    // Update sky position to follow camera (makes sky appear infinitely far away)
    if (skyRenderer_ && skyRenderer_->isInitialized() && camera_) {
        skyRenderer_->setCameraPosition(camera_->getPosition());
    }

    // ===== FRAME TIMING: Camera Update =====
    if (frameTimingEnabled_) frameTimings_.cameraUpdate = measureSection();

    // Handle inventory toggle (I key) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->inventoryToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            toggleInventory();
        }
    }

    // Handle group window toggle (G key) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->groupToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->toggleGroupWindow();
        }
    }

    // Handle skills window toggle (K key) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->skillsToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->toggleSkillsWindow();
        }
    }

// Handle zone line visualization toggle (Z key) - works in any mode, but only if chat is not focused
    if (eventReceiver_->zoneLineVisualizationToggleRequested()) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            toggleZoneLineVisualization();
        }
    }

    // Handle pet window toggle (P key) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->petToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->togglePetWindow();
        }
    }

    // Handle spellbook toggle (Ctrl+B) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->spellbookToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->toggleSpellbook();
        }
    }

    // Handle buff window toggle (Alt+B) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->buffWindowToggleRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->toggleBuffWindow();
        }
    }

    // Handle options window toggle (O key) - works in ALL modes, and only if chat is not focused
    if (eventReceiver_->optionsToggleRequested()) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            windowManager_->toggleOptionsWindow();
        }
    }

    // Handle door interaction (U key) - only in Player mode, and only if chat is not focused
    if (eventReceiver_->doorInteractRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            if (doorManager_ && doorInteractCallback_) {
                // Use playerHeading_ directly - in first person mode, this is updated by mouse look
                // (cameraController_->getHeadingEQ() only returns the camera's internal yaw which
                // is not updated in first person mode where we control playerHeading_ directly)
                LOG_DEBUG(MOD_GRAPHICS, "U key pressed: pos=({:.1f}, {:.1f}, {:.1f}) heading={:.1f} (512 fmt) = {:.1f} deg",
                    playerX_, playerY_, playerZ_, playerHeading_, playerHeading_ * 360.0f / 512.0f);

                // Find nearest door player is facing
                uint8_t doorId = doorManager_->getNearestDoor(playerX_, playerY_, playerZ_, playerHeading_);
                if (doorId != 0) {
                    LOG_INFO(MOD_GRAPHICS, "Door interaction (U key): ID {}", doorId);
                    doorInteractCallback_(doorId);
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "U key: No door found in range or facing wrong direction");
                }
            }
        }
    }

    // Handle world object (tradeskill container) interaction (O key) - only in Player mode
    if (eventReceiver_->worldObjectInteractRequested() && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            if (worldObjectInteractCallback_) {
                LOG_DEBUG(MOD_GRAPHICS, "O key pressed: pos=({:.1f}, {:.1f}, {:.1f})",
                    playerX_, playerY_, playerZ_);

                // Find nearest world object (tradeskill container)
                uint32_t objectId = getNearestWorldObject(playerX_, playerY_, playerZ_);
                if (objectId != 0) {
                    LOG_INFO(MOD_GRAPHICS, "World object interaction (O key): dropId {}", objectId);
                    worldObjectInteractCallback_(objectId);
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "O key: No world object found in range");
                }
            }
        }
    }

    // Handle spell gem shortcuts (1-8 keys) - only in Player mode, and only if chat is not focused
    int8_t spellGemRequest = eventReceiver_->getSpellGemCastRequest();
    if (spellGemRequest >= 0 && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            if (spellGemCastCallback_) {
                LOG_DEBUG(MOD_GRAPHICS, "Spell gem {} pressed (key {})", spellGemRequest + 1, spellGemRequest + 1);
                spellGemCastCallback_(static_cast<uint8_t>(spellGemRequest));
            }
        }
    }

    // Handle hotbar shortcuts (Ctrl+1-9, Ctrl+0) - only in Player mode, and only if chat is not focused
    int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
    if (hotbarRequest >= 0 && rendererMode_ == RendererMode::Player) {
        if (!windowManager_ || !windowManager_->isChatInputFocused()) {
            if (windowManager_ && windowManager_->getHotbarWindow()) {
                LOG_DEBUG(MOD_GRAPHICS, "Hotbar button {} activated (Ctrl+{})", hotbarRequest + 1,
                         hotbarRequest == 9 ? 0 : hotbarRequest + 1);
                windowManager_->getHotbarWindow()->activateButton(hotbarRequest);
            }
        }
    }

    // Handle chat input (Player mode)
    if (windowManager_ && rendererMode_ == RendererMode::Player) {
        bool chatFocused = windowManager_->isChatInputFocused();

        if (chatFocused) {
            // Route all key events to chat window
            while (eventReceiver_->hasPendingKeyEvents()) {
                auto keyEvent = eventReceiver_->popKeyEvent();
                bool shift = keyEvent.shift;
                bool ctrl = keyEvent.ctrl;
                windowManager_->handleKeyPress(keyEvent.key, shift, ctrl);

                // Also pass character to chat input if it's a printable character
                auto* chatWindow = windowManager_->getChatWindow();
                if (chatWindow && keyEvent.character != 0) {
                    chatWindow->handleKeyPress(keyEvent.key, keyEvent.character, shift, ctrl);
                }
            }

            // Escape unfocuses chat
            if (eventReceiver_->escapeKeyPressed()) {
                windowManager_->unfocusChatInput();
            }
        } else {
            // Chat not focused - handle Ctrl+key shortcuts for UI management
            // Also route keys when money input dialog is shown
            bool moneyDialogShown = windowManager_->isMoneyInputDialogShown();
            while (eventReceiver_->hasPendingKeyEvents()) {
                auto keyEvent = eventReceiver_->popKeyEvent();
                if (keyEvent.ctrl || moneyDialogShown) {
                    // Route Ctrl+key combinations and money dialog input through WindowManager
                    windowManager_->handleKeyPress(keyEvent.key, keyEvent.shift, keyEvent.ctrl);
                }
            }

            // Enter focuses chat (but not if money dialog is handling input)
            if (eventReceiver_->enterKeyPressed() && !moneyDialogShown) {
                windowManager_->focusChatInput();
            }

            // Slash focuses chat and inserts /
            if (eventReceiver_->slashKeyPressed()) {
                windowManager_->focusChatInput();
                auto* chatWindow = windowManager_->getChatWindow();
                if (chatWindow) {
                    chatWindow->insertText("/");
                }
            }

            // Escape: close vendor window if open, otherwise clear target
            // (but not if money dialog is handling ESC)
            if (eventReceiver_->escapeKeyPressed() && !moneyDialogShown) {
                if (windowManager_->isVendorWindowOpen()) {
                    // Close vendor window - the callback will send the close packet
                    if (vendorToggleCallback_) {
                        vendorToggleCallback_();
                    }
                } else if (currentTargetId_ != 0) {
                    // Clear target
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                    clearCurrentTarget();
                    SetTrackedTargetId(0);
                }
            }
        }
    } else {
        // Not in player mode or no window manager - clear pending events
        eventReceiver_->clearPendingKeyEvents();
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint D (before window manager)");

    // Update window manager (for tooltip timing, etc.)
    if (windowManager_) {
        irr::u32 currentTimeMs = device_->getTimer()->getTime();
        windowManager_->update(currentTimeMs);

        // Pass mouse movement to window manager
        int mouseX = eventReceiver_->getMouseX();
        int mouseY = eventReceiver_->getMouseY();
        windowManager_->handleMouseMove(mouseX, mouseY);
    }

    // Handle mouse click targeting (both modes) - but skip if window consumed the click
    // Note: hadClick, hadRelease, clickX, clickY were captured earlier before camera update
    // and windowManagerCapture_ was set there too
    if (!windowManagerCapture_ && hadClick) {
        handleMouseTargeting(clickX, clickY);
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint E (before entity update)");

    // ===== FRAME TIMING: Entity Update =====
    sectionStart = std::chrono::steady_clock::now();
    if (entityRenderer_) {
        entityRenderer_->updateInterpolation(deltaTime);
        // Update entity casting bars (timeout checks, etc.)
        entityRenderer_->updateEntityCastingBars(deltaTime, camera_);
        // Update constrained visibility (limits entity count/distance in constrained mode)
        if (camera_) {
            entityRenderer_->updateConstrainedVisibility(camera_->getAbsolutePosition());
        }
    }
    if (frameTimingEnabled_) frameTimings_.entityUpdate = measureSection();

    // ===== FRAME TIMING: Door Update =====
    if (doorManager_) {
        doorManager_->update(deltaTime);
    }
    if (frameTimingEnabled_) frameTimings_.doorUpdate = measureSection();

    // ===== FRAME TIMING: Spell VFX Update =====
    if (spellVisualFX_) {
        spellVisualFX_->update(deltaTime);
    }
    if (frameTimingEnabled_) frameTimings_.spellVfxUpdate = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint F (before animated textures)");

    // ===== FRAME TIMING: Animated Textures =====
    if (animatedTextureManager_) {
        animatedTextureManager_->update(deltaTime * 1000.0f);  // Convert to milliseconds
    }
    if (frameTimingEnabled_) frameTimings_.animatedTextures = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint G (before vertex animations)");

    // ===== FRAME TIMING: Vertex Animations =====
    updateVertexAnimations(deltaTime * 1000.0f);  // Convert to milliseconds
    if (frameTimingEnabled_) frameTimings_.vertexAnimations = measureSection();

    // ===== Detail System Update (grass, plants, debris) =====
    // Use player position (converted to Irrlicht coords), not camera position
    // Camera can be in free-fly mode, follow mode, etc. - player position is authoritative
    if (detailManager_ && detailManager_->isEnabled()) {
        // Convert EQ coords (X, Y, Z where Z is up) to Irrlicht coords (X, Y, Z where Y is up)
        irr::core::vector3df playerPosIrrlicht(playerX_, playerZ_, playerY_);

        // Track player velocity for foliage disturbance
        static float lastPlayerX = playerX_, lastPlayerY = playerY_;
        irr::core::vector3df playerVelocity(0, 0, 0);
        if (deltaTime > 0.001f) {
            // Calculate velocity in Irrlicht coords (EQ X -> Irr X, EQ Y -> Irr Z)
            float velX = (playerX_ - lastPlayerX) / deltaTime;
            float velZ = (playerY_ - lastPlayerY) / deltaTime;
            playerVelocity = irr::core::vector3df(velX, 0, velZ);
        }
        lastPlayerX = playerX_;
        lastPlayerY = playerY_;

        // Determine if player is moving (velocity above threshold)
        bool playerMoving = playerVelocity.getLengthSQ() > 0.1f;

        detailManager_->update(playerPosIrrlicht, deltaTime * 1000.0f,
                               playerPosIrrlicht, playerVelocity, playerHeading_, playerMoving);
    }

    // ===== Weather System Update =====
    if (weatherSystem_) {
        weatherSystem_->update(deltaTime);
    }

    // ===== Weather Effects Update =====
    if (weatherEffects_) {
        weatherEffects_->update(deltaTime);
    }

    // ===== Environmental Particle System Update =====
    if (particleManager_ && particleManager_->isEnabled()) {
        // Update player position for particle spawning
        particleManager_->setPlayerPosition(
            glm::vec3(playerX_, playerY_, playerZ_), playerHeading_);
        // Update time of day
        float timeOfDay = currentHour_ + currentMinute_ / 60.0f;
        particleManager_->setTimeOfDay(timeOfDay);
        // Update particles
        particleManager_->update(deltaTime);
    }

    // ===== Ambient Creatures (Boids) System Update =====
    if (boidsManager_ && boidsManager_->isEnabled()) {
        // Update player position (for scatter behavior when player approaches)
        boidsManager_->setPlayerPosition(
            glm::vec3(playerX_, playerY_, playerZ_), playerHeading_);
        // Update time of day (affects which creatures appear)
        float timeOfDay = currentHour_ + currentMinute_ / 60.0f;
        boidsManager_->setTimeOfDay(timeOfDay);
        // Update boids
        boidsManager_->update(deltaTime);
    }

    // ===== Tumbleweed System Update =====
    if (tumbleweedManager_ && tumbleweedManager_->isEnabled()) {
        // Set up environment state for tumbleweeds
        Environment::EnvironmentState envState;
        envState.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
        // Get wind intensity from weather system, use fixed direction
        // (EQ doesn't have dynamic wind direction in most zones)
        if (weatherSystem_) {
            envState.windStrength = weatherSystem_->getWindIntensity();
        } else {
            envState.windStrength = 0.5f;
        }
        // Default wind blows from west to east (positive X direction in EQ coords)
        envState.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        tumbleweedManager_->setEnvironmentState(envState);
        tumbleweedManager_->update(deltaTime);
    }

    // ===== Tree Wind Animation Update =====
    if (treeManager_ && treeManager_->isEnabled()) {
        // Use camera position for distance-based animation culling
        irr::core::vector3df cameraPos = camera_ ? camera_->getPosition() : irr::core::vector3df(0, 0, 0);
        treeManager_->update(deltaTime, cameraPos);
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint H (before object visibility)");

    // ===== FRAME TIMING: Object Visibility =====
    updateObjectVisibility();
    updateZoneLightVisibility();
    if (frameTimingEnabled_) frameTimings_.objectVisibility = measureSection();

    // ===== FRAME TIMING: PVS Visibility =====
    updatePvsVisibility();
    if (frameTimingEnabled_) frameTimings_.pvsVisibility = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint H2 (before object lights)");

    // ===== FRAME TIMING: Object Lights =====
    updateObjectLights();
    if (frameTimingEnabled_) frameTimings_.objectLights = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint I (before HUD)");

    // Update HUD animation timer
    hudAnimTimer_ += deltaTime;
    if (hudAnimTimer_ > 10000.0f) {
        hudAnimTimer_ = 0.0f;  // Prevent overflow
    }

    // Update HUD
    updateHUD();

    // ===== FRAME TIMING: HUD Update =====
    if (frameTimingEnabled_) frameTimings_.hudUpdate = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint J (before render)");

    // If loading screen is visible, show it instead of rendering zone
    // Loading screen is shown during initial load, zoning, and until explicitly hidden
    if (loadingScreenVisible_) {
        drawLoadingScreen(loadingProgress_, loadingText_);
        LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint M (done - loading screen)");
        return true;
    }

    // Run scene breakdown profile if scheduled (waits for frame count to reach 0)
    if (sceneProfileEnabled_) {
        if (sceneProfileFrameCount_ < 0) {
            sceneProfileFrameCount_++;
        } else {
            profileSceneBreakdown();
        }
    }

    // Render - use sky renderer's clear color for day/night effect
    irr::video::SColor clearColor(255, 50, 50, 80);  // Default dark blue
    if (skyRenderer_ && skyRenderer_->isEnabled() && skyRenderer_->isInitialized()) {
        clearColor = skyRenderer_->getCurrentClearColor();
    }
    driver_->beginScene(true, true, clearColor);
    sectionStart = std::chrono::steady_clock::now();  // Reset for accurate sceneDrawAll timing
    smgr_->drawAll();
    if (frameTimingEnabled_) {
        frameTimings_.sceneDrawAll = measureSection();
        // Also warn if scene draw is slow (>50ms)
        if (frameTimings_.sceneDrawAll > 50000) {  // 50ms in microseconds
            LOG_WARN(MOD_GRAPHICS, "PERF: smgr->drawAll() took {} ms", frameTimings_.sceneDrawAll / 1000);
        }
    }

    // Track polygon count for constrained mode budget
    lastPolygonCount_ = driver_->getPrimitiveCountDrawn();

    // Render footprints (after terrain, before UI)
    if (detailManager_ && detailManager_->isFootprintEnabled()) {
        detailManager_->renderFootprints();
    }

    if (config_.constrainedConfig.enabled) {
        // Check if we exceeded the polygon budget (soft limit - just warn)
        if (lastPolygonCount_ > static_cast<uint32_t>(config_.constrainedConfig.maxPolygonsPerFrame)) {
            if (++polygonBudgetExceededFrames_ >= 60) {  // Throttle to once per ~60 frames
                LOG_WARN(MOD_GRAPHICS, "Polygon budget exceeded: {} > {} (limit)",
                         lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame);
                polygonBudgetExceededFrames_ = 0;
            }
        } else {
            polygonBudgetExceededFrames_ = 0;
        }

        // Periodic stats logging (every ~5 seconds at 30fps = 150 frames)
        if (++constrainedStatsLogCounter_ >= 150) {
            constrainedStatsLogCounter_ = 0;

            // Get entity stats
            int visibleEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
            int totalEntities = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;

            // Get texture cache stats
            size_t tmuUsed = 0, tmuLimit = 0;
            float hitRate = 0.0f;
            size_t evictions = 0;
            if (constrainedTextureCache_) {
                tmuUsed = constrainedTextureCache_->getCurrentUsage();
                tmuLimit = constrainedTextureCache_->getMemoryLimit();
                hitRate = constrainedTextureCache_->getHitRate();
                evictions = constrainedTextureCache_->getEvictionCount();
            }

            // Calculate framebuffer usage
            size_t fbiUsed = config_.constrainedConfig.calculateFramebufferUsage(config_.width, config_.height);
            size_t fbiLimit = config_.constrainedConfig.framebufferMemoryBytes;

            LOG_INFO(MOD_GRAPHICS, "=== CONSTRAINED MODE STATS [{}] ===",
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
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint K (after drawAll)");

    // Draw selection box around targeted entity
    drawTargetSelectionBox();

    // Draw bounding box around repair target (if in Repair mode with target)
    if (rendererMode_ == RendererMode::Repair && repairTargetNode_) {
        drawRepairTargetBoundingBox();
    }

    // Render environmental particles
    if (particleManager_ && particleManager_->isEnabled()) {
        particleManager_->render();
    }

    // Render ambient creatures (boids)
    if (boidsManager_ && boidsManager_->isEnabled()) {
        boidsManager_->render();
    }

    // Render weather effects (lightning bolts, etc.)
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        weatherEffects_->render();
    }

    // Draw collision debug lines (if debug mode enabled)
    if (playerConfig_.collisionDebug) {
        drawCollisionDebugLines(deltaTime);
    }

    // ===== FRAME TIMING: Target Box =====
    if (frameTimingEnabled_) frameTimings_.targetBox = measureSection();

    // Draw entity casting bars (above other entities' heads)
    if (entityRenderer_) {
        entityRenderer_->renderEntityCastingBars(driver_, guienv_, camera_);
    }

    // ===== FRAME TIMING: Casting Bars =====
    if (frameTimingEnabled_) frameTimings_.castingBars = measureSection();

    guienv_->drawAll();

    // Draw FPS counter at top center
    drawFPSCounter();

    // ===== FRAME TIMING: GUI Draw All =====
    if (frameTimingEnabled_) frameTimings_.guiDrawAll = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint L (after GUI)");

    // Render inventory UI windows (on top of HUD)
    if (windowManager_) {
        windowManager_->render();
    }

    // ===== FRAME TIMING: Window Manager =====
    if (frameTimingEnabled_) frameTimings_.windowManager = measureSection();

    // Draw zone line overlay (pink border when in zone line)
    drawZoneLineOverlay();

    // Draw zone line bounding box labels
    drawZoneLineBoxLabels();

    // ===== FRAME TIMING: Zone Line Overlay =====
    if (frameTimingEnabled_) frameTimings_.zoneLineOverlay = measureSection();

#ifdef WITH_RDP
    // Capture frame for RDP streaming before presenting
    captureFrameForRDP();
#endif

    driver_->endScene();

    // ===== FRAME TIMING: End Scene =====
    if (frameTimingEnabled_) frameTimings_.endScene = measureSection();

    // ===== FRAME TIMING: Total Frame =====
    if (frameTimingEnabled_) {
        auto frameEnd = std::chrono::steady_clock::now();
        frameTimings_.totalFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();

        // Accumulate and periodically log
        frameTimingsAccum_.inputHandling += frameTimings_.inputHandling;
        frameTimingsAccum_.cameraUpdate += frameTimings_.cameraUpdate;
        frameTimingsAccum_.entityUpdate += frameTimings_.entityUpdate;
        frameTimingsAccum_.doorUpdate += frameTimings_.doorUpdate;
        frameTimingsAccum_.spellVfxUpdate += frameTimings_.spellVfxUpdate;
        frameTimingsAccum_.animatedTextures += frameTimings_.animatedTextures;
        frameTimingsAccum_.vertexAnimations += frameTimings_.vertexAnimations;
        frameTimingsAccum_.objectVisibility += frameTimings_.objectVisibility;
        frameTimingsAccum_.pvsVisibility += frameTimings_.pvsVisibility;
        frameTimingsAccum_.objectLights += frameTimings_.objectLights;
        frameTimingsAccum_.hudUpdate += frameTimings_.hudUpdate;
        frameTimingsAccum_.sceneDrawAll += frameTimings_.sceneDrawAll;
        frameTimingsAccum_.targetBox += frameTimings_.targetBox;
        frameTimingsAccum_.castingBars += frameTimings_.castingBars;
        frameTimingsAccum_.guiDrawAll += frameTimings_.guiDrawAll;
        frameTimingsAccum_.windowManager += frameTimings_.windowManager;
        frameTimingsAccum_.zoneLineOverlay += frameTimings_.zoneLineOverlay;
        frameTimingsAccum_.endScene += frameTimings_.endScene;
        frameTimingsAccum_.totalFrame += frameTimings_.totalFrame;
        frameTimingsSampleCount_++;

        // Log every 60 frames (~2 seconds at 30fps)
        if (frameTimingsSampleCount_ >= 60) {
            logFrameTimings();
            // Reset accumulators
            frameTimingsAccum_ = FrameTimings();
            frameTimingsSampleCount_ = 0;
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint M (done)");

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

void IrrlichtRenderer::toggleLighting() {
    lightingEnabled_ = !lightingEnabled_;

    if (zoneMeshNode_) {
        for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
            zoneMeshNode_->getMaterial(i).Lighting = lightingEnabled_;
            zoneMeshNode_->getMaterial(i).NormalizeNormals = true;
            zoneMeshNode_->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
            zoneMeshNode_->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
        }
    }

    // Update PVS region mesh nodes
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).Lighting = lightingEnabled_;
                node->getMaterial(i).NormalizeNormals = true;
                node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
    }

    for (auto* node : objectNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).Lighting = lightingEnabled_;
                node->getMaterial(i).NormalizeNormals = true;
                node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
    }

    // Update entity materials
    if (entityRenderer_) {
        entityRenderer_->setLightingEnabled(lightingEnabled_);
    }

    LOG_INFO(MOD_GRAPHICS, "Lighting: {}", (lightingEnabled_ ? "ON" : "OFF"));
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

void IrrlichtRenderer::setFrameTimingEnabled(bool enabled) {
    frameTimingEnabled_ = enabled;
    if (enabled) {
        // Reset accumulators when starting
        frameTimings_ = FrameTimings();
        frameTimingsAccum_ = FrameTimings();
        frameTimingsSampleCount_ = 0;
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler ENABLED - timing data will be logged every 60 frames");
    } else {
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
    LOG_INFO(MOD_GRAPHICS, "  Camera Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.cameraUpdate), pct(frameTimingsAccum_.cameraUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Entity Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.entityUpdate), pct(frameTimingsAccum_.entityUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Door Update:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.doorUpdate), pct(frameTimingsAccum_.doorUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Spell VFX Update:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.spellVfxUpdate), pct(frameTimingsAccum_.spellVfxUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Animated Textures:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.animatedTextures), pct(frameTimingsAccum_.animatedTextures));
    LOG_INFO(MOD_GRAPHICS, "  Vertex Animations:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.vertexAnimations), pct(frameTimingsAccum_.vertexAnimations));
    LOG_INFO(MOD_GRAPHICS, "  Object Visibility:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectVisibility), pct(frameTimingsAccum_.objectVisibility));
    LOG_INFO(MOD_GRAPHICS, "  PVS Visibility:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.pvsVisibility), pct(frameTimingsAccum_.pvsVisibility));
    LOG_INFO(MOD_GRAPHICS, "  Object Lights:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectLights), pct(frameTimingsAccum_.objectLights));
    LOG_INFO(MOD_GRAPHICS, "  HUD Update:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.hudUpdate), pct(frameTimingsAccum_.hudUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Scene Draw All:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneDrawAll), pct(frameTimingsAccum_.sceneDrawAll));
    LOG_INFO(MOD_GRAPHICS, "  Target Box:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.targetBox), pct(frameTimingsAccum_.targetBox));
    LOG_INFO(MOD_GRAPHICS, "  Casting Bars:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.castingBars), pct(frameTimingsAccum_.castingBars));
    LOG_INFO(MOD_GRAPHICS, "  GUI Draw All:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.guiDrawAll), pct(frameTimingsAccum_.guiDrawAll));
    LOG_INFO(MOD_GRAPHICS, "  Window Manager:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.windowManager), pct(frameTimingsAccum_.windowManager));
    LOG_INFO(MOD_GRAPHICS, "  Zone Line Overlay:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.zoneLineOverlay), pct(frameTimingsAccum_.zoneLineOverlay));
    LOG_INFO(MOD_GRAPHICS, "  End Scene:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.endScene), pct(frameTimingsAccum_.endScene));
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
        // Also hide lights (they may already be out of scene graph)
        for (size_t i = 0; i < zoneLightNodes_.size(); ++i) {
            if (zoneLightNodes_[i]) zoneLightNodes_[i]->setVisible(false);
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
        // Add lights back to scene graph and show
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
    for (auto* node : zoneLightNodes_) if (node) node->setVisible(true);
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
    for (auto* node : zoneLightNodes_) if (node) node->setVisible(false);
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

void IrrlichtRenderer::setRendererMode(RendererMode mode) {
    if (rendererMode_ == mode) {
        return;
    }

    rendererMode_ = mode;

    // Update event receiver's mode for hotkey lookups
    if (eventReceiver_) {
        eventReceiver_->setCurrentMode(mode);
    }

    if (mode == RendererMode::Player) {
        // Switch to Player mode
        // Default to Follow camera (third-person) if coming from Free mode
        if (cameraMode_ == CameraMode::Free) {
            cameraMode_ = CameraMode::Follow;
        }
        // Reset movement state and pitch
        playerMovement_ = PlayerMovementState();
        playerPitch_ = 0;
        // Hide player entity only in first-person view
        if (entityRenderer_) {
            entityRenderer_->setPlayerEntityVisible(cameraMode_ != CameraMode::FirstPerson);
            // Update player entity position to current player position
            entityRenderer_->updatePlayerEntityPosition(playerX_, playerY_, playerZ_, playerHeading_);
        }
        // Log warning if no collision map
        if (!collisionMap_) {
            static bool warned = false;
            if (!warned) {
                LOG_WARN(MOD_GRAPHICS, "Player mode enabled without collision map - movement will not respect geometry");
                warned = true;
            }
        }
        LOG_INFO(MOD_GRAPHICS, "Switched to PLAYER mode (F9 to cycle modes)");
        LOG_INFO(MOD_GRAPHICS, "Controls: WASD=Move, QE=Strafe, R=Autorun, LMB+Mouse=Look");
        LOG_INFO(MOD_GRAPHICS, "Debug: C=Toggle Collision, Ctrl+C=Debug Output, T/G=CollisionHeight");
        LOG_INFO(MOD_GRAPHICS, "Collision: {}, Map: {}", (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"), (collisionMap_ ? "LOADED" : "NONE"));
        if (cameraMode_ == CameraMode::FirstPerson) {
            LOG_INFO(MOD_GRAPHICS, "First Person mode - Eye height: {:.1f} (Y to raise, Shift+Y to lower)", playerConfig_.eyeHeight);
        }
    } else if (mode == RendererMode::Repair) {
        // Switch to Repair mode - similar to Player mode but for object adjustment
        // Default to Follow camera if coming from Free mode
        if (cameraMode_ == CameraMode::Free) {
            cameraMode_ = CameraMode::Follow;
        }
        // Reset movement state and pitch (same as Player mode)
        playerMovement_ = PlayerMovementState();
        playerPitch_ = 0;
        // Show player entity (visible in Repair mode for positioning reference)
        if (entityRenderer_) {
            entityRenderer_->setPlayerEntityVisible(cameraMode_ != CameraMode::FirstPerson);
            // Update player entity position to current player position
            entityRenderer_->updatePlayerEntityPosition(playerX_, playerY_, playerZ_, playerHeading_);
        }
        // Clear any repair target when entering mode
        repairTargetNode_ = nullptr;
        repairTargetName_.clear();

        LOG_INFO(MOD_GRAPHICS, "Switched to REPAIR mode (F9 to cycle modes)");
        LOG_INFO(MOD_GRAPHICS, "Click on zone objects to select. ESC to clear target.");
        LOG_INFO(MOD_GRAPHICS, "X/Y/Z (+Shift): Rotate. 1/2/3: Flip axis. R: Reset.");
    } else {
        // Switch to Admin mode
        playerMovement_.autorun = false;
        // Show player entity again
        if (entityRenderer_) {
            entityRenderer_->setPlayerEntityVisible(true);
        }
        // Clear repair target when leaving repair mode
        repairTargetNode_ = nullptr;
        repairTargetName_.clear();

        LOG_INFO(MOD_GRAPHICS, "Switched to ADMIN mode (F9 to cycle modes)");
    }
}

void IrrlichtRenderer::toggleRendererMode() {
    // Cycle through: Player -> Repair -> Admin -> Player
    switch (rendererMode_) {
        case RendererMode::Player:
            setRendererMode(RendererMode::Repair);
            break;
        case RendererMode::Repair:
            setRendererMode(RendererMode::Admin);
            break;
        case RendererMode::Admin:
            setRendererMode(RendererMode::Player);
            break;
    }
}

std::string IrrlichtRenderer::getRendererModeString() const {
    switch (rendererMode_) {
        case RendererMode::Player: return "Player";
        case RendererMode::Repair: return "Repair";
        case RendererMode::Admin: return "Admin";
        default: return "Unknown";
    }
}

void IrrlichtRenderer::setClipDistance(float distance) {
    // Clamp to reasonable range
    if (distance < 100.0f) distance = 100.0f;
    if (distance > 50000.0f) distance = 50000.0f;

    // Update render distance for fog/object culling
    renderDistance_ = distance;
    config_.constrainedConfig.clipDistance = distance;

    // Camera far plane must stay at least SKY_FAR_PLANE to render sky dome
    // Only increase it if requested distance is larger
    if (camera_) {
        float cameraFar = std::max(SKY_FAR_PLANE, distance);
        camera_->setFarValue(cameraFar);
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
    // Allow movement in both Player and Repair modes
    if (rendererMode_ != RendererMode::Player && rendererMode_ != RendererMode::Repair) {
        return;
    }

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

    // Update movement state
    playerMovement_.moveForward = forward || playerMovement_.autorun;
    playerMovement_.moveBackward = backward;
    playerMovement_.strafeLeft = strafeLeft;
    playerMovement_.strafeRight = strafeRight;
    playerMovement_.turnLeft = turnLeft;
    playerMovement_.turnRight = turnRight;

    // Handle jump initiation (only when on ground)
    if (jumpPressed && !playerMovement_.isJumping) {
        playerMovement_.isJumping = true;
        playerMovement_.verticalVelocity = playerMovement_.jumpVelocity;
        if (playerConfig_.collisionDebug) {
            LOG_INFO(MOD_GRAPHICS, "[Jump] Started jump with velocity {}", playerMovement_.verticalVelocity);
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
    float moveX = 0.0f, moveY = 0.0f;

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

    // Calculate proposed new position
    float newX = playerX_ + moveX * deltaTime;
    float newY = playerY_ + moveY * deltaTime;
    float newZ = playerZ_;

    // Apply jump physics
    if (playerMovement_.isJumping) {
        // Apply gravity to vertical velocity
        playerMovement_.verticalVelocity -= playerMovement_.gravity * deltaTime;

        // Update Z position based on vertical velocity
        newZ += playerMovement_.verticalVelocity * deltaTime;

        LOG_TRACE(MOD_MOVEMENT, "Jump velocity={}, newZ={}", playerMovement_.verticalVelocity, newZ);
    }

    // Apply collision detection if available and we're actually moving
    bool positionChanged = false;
    bool isMoving = (moveX != 0.0f || moveY != 0.0f);
    bool isJumpMoving = playerMovement_.isJumping;

    if (isMoving || isJumpMoving) {
        // Debug: show movement attempt
        LOG_TRACE(MOD_MOVEMENT, "Attempting move: delta=({}, {}) from ({}, {}, {}){}",
                  moveX * deltaTime, moveY * deltaTime, playerX_, playerY_, playerZ_,
                  (isJumpMoving ? " [JUMPING]" : ""));
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
                // Horizontal movement blocked - try wall sliding or continue jump vertically
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
        // Jump animation takes priority (playThrough)
        if (playerMovement_.isJumping && playerMovement_.verticalVelocity > 0) {
            // Only trigger jump animation on the way up (ascending)
            // Use l03 for running jump, l04 for standing jump
            if (hasMovementInput) {
                entityRenderer_->setPlayerEntityAnimation("l03", false, 0.0f, true);  // Running jump
            } else {
                entityRenderer_->setPlayerEntityAnimation("l04", false, 0.0f, true);  // Standing jump
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

void IrrlichtRenderer::setupZoneCollision() {
    // Clean up old selectors
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    // Clean up old collision node (used in PVS mode)
    if (zoneCollisionNode_) {
        zoneCollisionNode_->remove();
        zoneCollisionNode_ = nullptr;
    }

    if (!smgr_) {
        return;
    }

    // Create a meta triangle selector to combine zone, objects, and doors
    irr::scene::IMetaTriangleSelector* metaSelector = smgr_->createMetaTriangleSelector();
    if (!metaSelector) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create meta triangle selector");
        return;
    }

    // Create a separate terrain-only selector for the detail system (excludes placeables)
    irr::scene::IMetaTriangleSelector* terrainMeta = smgr_->createMetaTriangleSelector();
    if (!terrainMeta) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create terrain-only triangle selector");
        metaSelector->drop();
        return;
    }

    int selectorCount = 0;

    // Add zone mesh selector(s)
    // For PVS-based rendering, create a single combined collision mesh from all region geometry
    // (Individual selectors per region would be too slow - 4000+ selectors!)
    if (!regionMeshNodes_.empty() && currentZone_ && currentZone_->geometry) {
        // PVS mode: build a combined collision mesh from the zone geometry
        // Use the original combined geometry which has all triangles
        ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
        irr::scene::IMesh* collisionMesh = builder.buildMesh(*currentZone_->geometry);

        if (collisionMesh) {
            // Create a hidden scene node just for collision
            zoneCollisionNode_ = smgr_->addMeshSceneNode(collisionMesh);
            if (zoneCollisionNode_) {
                zoneCollisionNode_->setVisible(false);  // Don't render, just use for collision
                zoneCollisionNode_->setPosition(irr::core::vector3df(0, 0, 0));

                irr::scene::ITriangleSelector* zoneSelector =
                    smgr_->createOctreeTriangleSelector(collisionMesh, zoneCollisionNode_, 128);
                if (zoneSelector) {
                    metaSelector->addTriangleSelector(zoneSelector);
                    terrainMeta->addTriangleSelector(zoneSelector);  // Also add to terrain-only
                    zoneCollisionNode_->setTriangleSelector(zoneSelector);
                    zoneSelector->drop();
                    selectorCount++;
                    LOG_DEBUG(MOD_GRAPHICS, "Added combined zone collision mesh (octree selector, {} triangles)",
                              currentZone_->geometry->triangles.size());
                }
            }
            collisionMesh->drop();
        }

        // Also add fallback mesh if it exists (geometry not in BSP regions)
        if (fallbackMeshNode_ && fallbackMeshNode_->getMesh()) {
            irr::scene::ITriangleSelector* fallbackSelector =
                smgr_->createTriangleSelector(fallbackMeshNode_->getMesh(), fallbackMeshNode_);
            if (fallbackSelector) {
                metaSelector->addTriangleSelector(fallbackSelector);
                terrainMeta->addTriangleSelector(fallbackSelector);  // Also add to terrain-only
                fallbackMeshNode_->setTriangleSelector(fallbackSelector);
                fallbackSelector->drop();
                selectorCount++;
                LOG_DEBUG(MOD_GRAPHICS, "Added fallback mesh to collision");
            }
        }
    } else if (zoneMeshNode_) {
        // Non-PVS mode: single combined zone mesh
        irr::scene::IMesh* mesh = zoneMeshNode_->getMesh();
        if (mesh) {
            irr::scene::ITriangleSelector* zoneSelector =
                smgr_->createOctreeTriangleSelector(mesh, zoneMeshNode_, 128);
            if (zoneSelector) {
                metaSelector->addTriangleSelector(zoneSelector);
                terrainMeta->addTriangleSelector(zoneSelector);  // Also add to terrain-only
                zoneMeshNode_->setTriangleSelector(zoneSelector);
                zoneSelector->drop();
                selectorCount++;
                LOG_DEBUG(MOD_GRAPHICS, "Added zone mesh to collision (octree selector)");
            }
        }
    }

    // Store terrain-only selector (for detail system ground queries)
    terrainOnlySelector_ = terrainMeta;

    // Add placeable object selectors
    for (auto* objectNode : objectNodes_) {
        if (objectNode && objectNode->getMesh()) {
            irr::scene::ITriangleSelector* objSelector =
                smgr_->createTriangleSelector(objectNode->getMesh(), objectNode);
            if (objSelector) {
                metaSelector->addTriangleSelector(objSelector);
                objectNode->setTriangleSelector(objSelector);
                objSelector->drop();
                selectorCount++;
            }
        }
    }
    if (!objectNodes_.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Added {} placeable objects to collision", objectNodes_.size());
    }

    // Add door selectors
    if (doorManager_) {
        auto doorNodes = doorManager_->getDoorSceneNodes();
        for (auto* doorNode : doorNodes) {
            if (doorNode && doorNode->getMesh()) {
                irr::scene::ITriangleSelector* doorSelector =
                    smgr_->createTriangleSelector(doorNode->getMesh(), doorNode);
                if (doorSelector) {
                    metaSelector->addTriangleSelector(doorSelector);
                    doorNode->setTriangleSelector(doorSelector);
                    doorSelector->drop();
                    selectorCount++;
                }
            }
        }
        if (!doorNodes.empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "Added {} doors to collision", doorNodes.size());
        }
    }

    zoneTriangleSelector_ = metaSelector;
    LOG_DEBUG(MOD_GRAPHICS, "Zone collision setup complete ({} selectors)", selectorCount);

    // Get collision manager
    collisionManager_ = smgr_->getSceneCollisionManager();

    // Set up camera collision detection for follow mode zoom
    if (cameraController_ && collisionManager_ && zoneTriangleSelector_) {
        cameraController_->setCollisionManager(collisionManager_, zoneTriangleSelector_);
    }

    // Initialize detail system with collision selector for ground queries
    // Use terrain-only selector to avoid hitting placeables (trees, buildings, etc.)
    // Also pass WldLoader for BSP-based water/lava/zoneline exclusion
    // and zoneMeshNode for texture lookups
    // Pass the same zone geometry that the mesh was built from to ensure texture indices match
    if (detailManager_ && terrainOnlySelector_) {
        std::shared_ptr<WldLoader> wldLoader = currentZone_ ? currentZone_->wldLoader : nullptr;
        std::shared_ptr<ZoneGeometry> zoneGeom = currentZone_ ? currentZone_->geometry : nullptr;

        // Pass zone mesh node for texture lookups
        // In non-PVS mode, use zoneMeshNode_
        // In PVS mode, zoneMeshNode_ is null - we'll add region nodes below
        // Use terrainOnlySelector_ so ground queries don't hit placeables
        detailManager_->onZoneEnter(currentZoneName_, terrainOnlySelector_, zoneMeshNode_, wldLoader, zoneGeom);

        // In PVS mode, add all region mesh nodes for texture lookup
        if (!regionMeshNodes_.empty()) {
            int addedCount = 0;
            for (auto& [regionIdx, node] : regionMeshNodes_) {
                if (node && node->getMesh()) {
                    detailManager_->addMeshNodeForTextureLookup(node);
                    addedCount++;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "Detail system added {} region meshes for texture lookups", addedCount);
        }

        // Pass surface map to particle manager for shoreline wave detection
        if (particleManager_ && detailManager_->hasSurfaceMap()) {
            particleManager_->setSurfaceMap(detailManager_->getSurfaceMap());
        }

        // Pass surface map to weather effects for water ripples (Phase 7)
        if (weatherEffects_ && detailManager_->hasSurfaceMap()) {
            weatherEffects_->setSurfaceMap(detailManager_->getSurfaceMap());
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

// --- Repair Mode Methods ---

irr::scene::ISceneNode* IrrlichtRenderer::findZoneObjectAtScreenPosition(int screenX, int screenY) {
    if (!collisionManager_ || !camera_ || objectNodes_.empty()) {
        return nullptr;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionManager_->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera_);

    irr::scene::ISceneNode* closestNode = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    for (auto* node : objectNodes_) {
        if (!node || !node->isVisible()) {
            continue;
        }

        // Get the transformed bounding box (world space)
        irr::core::aabbox3df bbox = node->getTransformedBoundingBox();

        // Expand box slightly for easier clicking
        bbox.MinEdge -= irr::core::vector3df(0.5f, 0.5f, 0.5f);
        bbox.MaxEdge += irr::core::vector3df(0.5f, 0.5f, 0.5f);

        if (bbox.intersectsWithLine(ray)) {
            // Calculate distance to object center
            irr::core::vector3df center = bbox.getCenter();
            float dist = ray.start.getDistanceFrom(center);

            if (dist < closestDist) {
                closestDist = dist;
                closestNode = node;
            }
        }
    }

    return closestNode;
}

void IrrlichtRenderer::selectRepairTarget(irr::scene::ISceneNode* node) {
    if (!node) {
        clearRepairTarget();
        return;
    }

    repairTargetNode_ = node;
    repairTargetName_ = node->getName();

    // Store original transform
    repairOriginalRotation_ = node->getRotation();
    repairOriginalScale_ = node->getScale();

    // Reset adjustment state
    repairRotationOffset_ = irr::core::vector3df(0, 0, 0);
    repairFlipX_ = false;
    repairFlipY_ = false;
    repairFlipZ_ = false;

    // Log selection
    irr::core::vector3df pos = node->getPosition();
    // Convert Irrlicht coords (x, y, z) back to EQ coords (x, z, y) for logging
    LOG_INFO(MOD_GRAPHICS, "[REPAIR] Selected object: '{}' at EQ pos ({:.1f}, {:.1f}, {:.1f})",
        repairTargetName_, pos.X, pos.Z, pos.Y);
    LOG_INFO(MOD_GRAPHICS, "[REPAIR]   Original rotation: ({:.1f}, {:.1f}, {:.1f})",
        repairOriginalRotation_.X, repairOriginalRotation_.Y, repairOriginalRotation_.Z);
    LOG_INFO(MOD_GRAPHICS, "[REPAIR]   Original scale: ({:.2f}, {:.2f}, {:.2f})",
        repairOriginalScale_.X, repairOriginalScale_.Y, repairOriginalScale_.Z);
}

void IrrlichtRenderer::clearRepairTarget() {
    if (repairTargetNode_) {
        LOG_INFO(MOD_GRAPHICS, "[REPAIR] Cleared target: '{}'", repairTargetName_);
    }

    repairTargetNode_ = nullptr;
    repairTargetName_.clear();
    repairOriginalRotation_ = irr::core::vector3df(0, 0, 0);
    repairOriginalScale_ = irr::core::vector3df(1, 1, 1);
    repairRotationOffset_ = irr::core::vector3df(0, 0, 0);
    repairFlipX_ = false;
    repairFlipY_ = false;
    repairFlipZ_ = false;
}

void IrrlichtRenderer::drawRepairTargetBoundingBox() {
    if (!repairTargetNode_ || !driver_) {
        return;
    }

    // Get the transformed bounding box (world space)
    irr::core::aabbox3df bbox = repairTargetNode_->getTransformedBoundingBox();

    // Draw white wireframe box
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

    // Draw 12 edges of the box
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

void IrrlichtRenderer::applyRepairRotation(float deltaX, float deltaY, float deltaZ) {
    if (!repairTargetNode_) {
        return;
    }

    // Accumulate rotation offset
    repairRotationOffset_.X += deltaX;
    repairRotationOffset_.Y += deltaY;
    repairRotationOffset_.Z += deltaZ;

    // Normalize to 0-360 range
    while (repairRotationOffset_.X >= 360.0f) repairRotationOffset_.X -= 360.0f;
    while (repairRotationOffset_.X < 0.0f) repairRotationOffset_.X += 360.0f;
    while (repairRotationOffset_.Y >= 360.0f) repairRotationOffset_.Y -= 360.0f;
    while (repairRotationOffset_.Y < 0.0f) repairRotationOffset_.Y += 360.0f;
    while (repairRotationOffset_.Z >= 360.0f) repairRotationOffset_.Z -= 360.0f;
    while (repairRotationOffset_.Z < 0.0f) repairRotationOffset_.Z += 360.0f;

    // Apply combined rotation to node
    irr::core::vector3df newRotation = repairOriginalRotation_ + repairRotationOffset_;
    repairTargetNode_->setRotation(newRotation);

    // Log the adjustment
    logRepairAdjustment();
}

void IrrlichtRenderer::toggleRepairFlip(int axis) {
    if (!repairTargetNode_) {
        return;
    }

    // Toggle the appropriate flip flag
    switch (axis) {
        case 0: repairFlipX_ = !repairFlipX_; break;
        case 1: repairFlipY_ = !repairFlipY_; break;
        case 2: repairFlipZ_ = !repairFlipZ_; break;
        default: return;
    }

    // Apply scale with flips
    irr::core::vector3df newScale = repairOriginalScale_;
    if (repairFlipX_) newScale.X *= -1.0f;
    if (repairFlipY_) newScale.Y *= -1.0f;
    if (repairFlipZ_) newScale.Z *= -1.0f;
    repairTargetNode_->setScale(newScale);

    // Log the adjustment
    logRepairAdjustment();
}

void IrrlichtRenderer::resetRepairAdjustments() {
    if (!repairTargetNode_) {
        return;
    }

    // Reset rotation offset
    repairRotationOffset_ = irr::core::vector3df(0, 0, 0);
    repairTargetNode_->setRotation(repairOriginalRotation_);

    // Reset flip flags and scale
    repairFlipX_ = false;
    repairFlipY_ = false;
    repairFlipZ_ = false;
    repairTargetNode_->setScale(repairOriginalScale_);

    LOG_INFO(MOD_GRAPHICS, "[REPAIR] Reset adjustments for '{}'", repairTargetName_);
}

void IrrlichtRenderer::logRepairAdjustment() {
    if (!repairTargetNode_) {
        return;
    }

    irr::core::vector3df pos = repairTargetNode_->getAbsolutePosition();
    irr::core::vector3df finalRot = repairTargetNode_->getRotation();
    irr::core::vector3df finalScale = repairTargetNode_->getScale();

    // Build flip string
    std::string flipStr;
    if (repairFlipX_ || repairFlipY_ || repairFlipZ_) {
        flipStr = " flip=(";
        if (repairFlipX_) flipStr += "X";
        if (repairFlipY_) flipStr += "Y";
        if (repairFlipZ_) flipStr += "Z";
        flipStr += ")";
    }

    LOG_INFO(MOD_GRAPHICS, "[REPAIR] Object: '{}' at ({:.1f}, {:.1f}, {:.1f})",
             repairTargetName_, pos.X, pos.Y, pos.Z);
    LOG_INFO(MOD_GRAPHICS, "[REPAIR]   Original rotation: ({:.1f}, {:.1f}, {:.1f})",
             repairOriginalRotation_.X, repairOriginalRotation_.Y, repairOriginalRotation_.Z);
    LOG_INFO(MOD_GRAPHICS, "[REPAIR]   Applied offset: rotation=({:.1f}, {:.1f}, {:.1f}){}",
             repairRotationOffset_.X, repairRotationOffset_.Y, repairRotationOffset_.Z, flipStr);
    LOG_INFO(MOD_GRAPHICS, "[REPAIR]   Final: rotation=({:.1f}, {:.1f}, {:.1f}) scale=({:.1f}, {:.1f}, {:.1f})",
             finalRot.X, finalRot.Y, finalRot.Z, finalScale.X, finalScale.Y, finalScale.Z);
}

void IrrlichtRenderer::updateNameTagsWithLOS(float deltaTime) {
    if (!entityRenderer_) {
        return;
    }

    // ALWAYS call entity visibility update first to handle distance-based model culling
    // This culls entity models (not just name tags) based on render distance
    entityRenderer_->updateNameTags(camera_);

    // In admin mode or if no collision map, we're done (updateNameTags handled everything)
    if (rendererMode_ != RendererMode::Player || !collisionMap_) {
        return;
    }

    // Throttle LOS checks for performance (only affects name tag LOS refinement)
    lastLOSCheckTime_ += deltaTime;
    if (lastLOSCheckTime_ < playerConfig_.nameTagLOSCheckInterval) {
        return;  // Skip LOS refinement this frame
    }
    lastLOSCheckTime_ = 0.0f;

    // Player eye position (EQ coordinates)
    glm::vec3 playerEye(playerX_, playerY_, playerZ_ + playerConfig_.eyeHeight);

    // Refine name tag visibility with LOS checks (on top of distance culling)
    const auto& entities = entityRenderer_->getEntities();
    float nameTagDist = entityRenderer_->getNameTagDistance();

    for (const auto& [spawnId, visual] : entities) {
        if (!visual.nameNode) continue;

        // Entity position (approximate chest height)
        glm::vec3 entityPos(visual.lastX, visual.lastY, visual.lastZ + 5.0f);

        // Calculate distance
        glm::vec3 diff = entityPos - playerEye;
        float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        // Check if within name tag distance
        bool visible = (distance <= nameTagDist);

        // If within distance, also check LOS
        if (visible) {
            visible = collisionMap_->CheckLOS(playerEye, entityPos);
        }

        visual.nameNode->setVisible(visible);
    }
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
    // Notify EverQuest class to clear combat target
    if (clearTargetCallback_) {
        clearTargetCallback_();
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

    // In Repair mode, target zone objects instead of entities
    if (rendererMode_ == RendererMode::Repair) {
        irr::scene::ISceneNode* objectNode = findZoneObjectAtScreenPosition(clickX, clickY);
        if (objectNode) {
            selectRepairTarget(objectNode);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "[REPAIR] No zone object at click position ({}, {})", clickX, clickY);
        }
        return;  // Don't fall through to entity targeting in Repair mode
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

        // Set up display settings callback for environmental particles
        windowManager_->setDisplaySettingsChangedCallback([this]() {
            // Update render distance first (affects terrain, objects, entities)
            if (windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();
                setRenderDistance(settings.renderDistance);
                LOG_DEBUG(MOD_GRAPHICS, "Render distance updated to {}", settings.renderDistance);
            }

            if (particleManager_ && windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();

                // Map UI EffectQuality to particle system EffectQuality
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

                LOG_DEBUG(MOD_GRAPHICS, "Particle settings updated: quality={}, enabled={}, density={}",
                         static_cast<int>(quality), settings.atmosphericParticles, settings.environmentDensity);
            }

            // Update boids manager settings
            if (boidsManager_ && windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();

                // Use same quality setting as particles (0=Off, 1=Low, 2=Medium, 3=High)
                int quality = static_cast<int>(settings.environmentQuality);
                boidsManager_->setQuality(quality);
                boidsManager_->setEnabled(settings.ambientCreatures);
                boidsManager_->setDensity(settings.environmentDensity);

                LOG_DEBUG(MOD_GRAPHICS, "Boids settings updated: quality={}, enabled={}, density={}",
                         quality, settings.ambientCreatures, settings.environmentDensity);
            }

            // Update detail manager settings (grass, plants, rocks, debris)
            if (detailManager_ && windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();

                detailManager_->setEnabled(settings.detailObjectsEnabled);
                detailManager_->setDensity(settings.detailDensity);
                detailManager_->setCategoryEnabled(Detail::DetailCategory::Grass, settings.detailGrass);
                detailManager_->setCategoryEnabled(Detail::DetailCategory::Plants, settings.detailPlants);
                detailManager_->setCategoryEnabled(Detail::DetailCategory::Rocks, settings.detailRocks);
                detailManager_->setCategoryEnabled(Detail::DetailCategory::Debris, settings.detailDebris);

                // Update reactive foliage (grass bending when player walks through)
                auto foliageConfig = detailManager_->getFoliageDisturbanceConfig();
                foliageConfig.enabled = settings.reactiveFoliage;
                detailManager_->setFoliageDisturbanceConfig(foliageConfig);

                LOG_DEBUG(MOD_GRAPHICS, "Detail settings updated: enabled={}, density={:.2f}, grass={}, plants={}, rocks={}, debris={}, reactiveFoliage={}",
                         settings.detailObjectsEnabled, settings.detailDensity,
                         settings.detailGrass, settings.detailPlants, settings.detailRocks, settings.detailDebris,
                         settings.reactiveFoliage);
            }

            // Update tumbleweed (rolling objects) settings
            if (tumbleweedManager_ && windowManager_ && windowManager_->getOptionsWindow()) {
                const auto& settings = windowManager_->getOptionsWindow()->getDisplaySettings();
                tumbleweedManager_->setEnabled(settings.rollingObjects);

                LOG_DEBUG(MOD_GRAPHICS, "Tumbleweed settings updated: enabled={}",
                         settings.rollingObjects);
            }
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
}

void IrrlichtRenderer::clearZoneLineBoundingBoxes() {
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->remove();
        }
    }
    zoneLineBoxNodes_.clear();
}

void IrrlichtRenderer::toggleZoneLineVisualization() {
    showZoneLineBoxes_ = !showZoneLineBoxes_;

    // Update visibility of all zone line box nodes
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->setVisible(showZoneLineBoxes_);
        }
    }

    // Notify EverQuest to enable/disable zoning when zone lines are toggled
    if (zoningEnabledCallback_) {
        zoningEnabledCallback_(showZoneLineBoxes_);
    }

    LOG_INFO(MOD_GRAPHICS, "Zone line visualization and zoning {}", showZoneLineBoxes_ ? "enabled" : "disabled");
}

void IrrlichtRenderer::createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox& box) {
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

    // Set up semi-transparent material
    // Use different colors for BSP vs proximity-based boxes
    irr::video::SColor color;
    if (box.isProximityBased) {
        // Cyan for proximity-based zone points
        color = irr::video::SColor(80, 0, 255, 255);
    } else {
        // Magenta/purple for BSP-based zone lines
        color = irr::video::SColor(80, 255, 0, 255);
    }

    // Apply semi-transparent material to all mesh buffers
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        irr::video::SMaterial& mat = node->getMaterial(i);
        mat.MaterialType = irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;
        mat.Lighting = false;
        mat.BackfaceCulling = false;
        mat.ZWriteEnable = false;  // Don't write to depth buffer
        mat.AmbientColor = color;
        mat.DiffuseColor = color;
    }

    // Also modify the mesh vertex colors directly for vertex alpha transparency
    irr::scene::IMeshBuffer* meshBuffer = node->getMesh()->getMeshBuffer(0);
    if (meshBuffer) {
        irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(meshBuffer->getVertices());
        irr::u32 vertexCount = meshBuffer->getVertexCount();
        for (irr::u32 i = 0; i < vertexCount; ++i) {
            vertices[i].Color = color;
        }
    }

    node->setVisible(showZoneLineBoxes_);

    // Store the node
    ZoneLineBoxNode boxNode;
    boxNode.node = node;
    boxNode.targetZoneId = box.targetZoneId;
    boxNode.isProximityBased = box.isProximityBased;
    zoneLineBoxNodes_.push_back(boxNode);

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

void IrrlichtRenderer::triggerFirstPersonAttack() {
    if (entityRenderer_) {
        entityRenderer_->triggerFirstPersonAttack();
    }
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
