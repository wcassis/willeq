#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <cmath>
#include <limits>

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

            // Wireframe toggle on F1
            if (event.KeyInput.Key == irr::KEY_F1) {
                wireframeToggleRequested_ = true;
            }

            // HUD toggle on F2
            if (event.KeyInput.Key == irr::KEY_F2) {
                hudToggleRequested_ = true;
            }

            // Name tags toggle on F3
            if (event.KeyInput.Key == irr::KEY_F3) {
                nameTagToggleRequested_ = true;
            }

            // Zone lights toggle on F4
            if (event.KeyInput.Key == irr::KEY_F4) {
                zoneLightsToggleRequested_ = true;
            }

            // Camera mode toggle on F5
            if (event.KeyInput.Key == irr::KEY_F5) {
                cameraModeToggleRequested_ = true;
            }

            // Old/New models toggle on F6
            if (event.KeyInput.Key == irr::KEY_F6) {
                oldModelsToggleRequested_ = true;
            }

            // Renderer mode toggle on F9 (Admin/Player mode)
            if (event.KeyInput.Key == irr::KEY_F9) {
                rendererModeToggleRequested_ = true;
            }

            // Autorun toggle on R or Numlock (Player mode)
            if (event.KeyInput.Key == irr::KEY_KEY_R && !event.KeyInput.Control) {
                autorunToggleRequested_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_NUMLOCK) {
                autorunToggleRequested_ = true;
            }

            // Auto attack toggle on ` (backtick) key (Player mode)
            if (event.KeyInput.Key == irr::KEY_OEM_3) {  // Backtick/tilde key
                autoAttackToggleRequested_ = true;
            }

            // Inventory toggle on I key (Player mode)
            if (event.KeyInput.Key == irr::KEY_KEY_I) {
                inventoryToggleRequested_ = true;
            }

            // Group window toggle on G key (Player mode)
            if (event.KeyInput.Key == irr::KEY_KEY_G) {
                groupToggleRequested_ = true;
            }

            // Door interaction on U key (Player mode - use nearest door)
            if (event.KeyInput.Key == irr::KEY_KEY_U) {
                doorInteractRequested_ = true;
            }

            // Hail on H key (Player mode)
            if (event.KeyInput.Key == irr::KEY_KEY_H) {
                hailRequested_ = true;
            }

            // Vendor window toggle on V key (Player mode)
            if (event.KeyInput.Key == irr::KEY_KEY_V) {
                vendorToggleRequested_ = true;
            }

            // Spell gem shortcuts (1-8 keys, not numpad)
            if (event.KeyInput.Key >= irr::KEY_KEY_1 && event.KeyInput.Key <= irr::KEY_KEY_8) {
                spellGemCastRequest_ = static_cast<int8_t>(event.KeyInput.Key - irr::KEY_KEY_1);
            }

            // Chat input shortcuts
            if (event.KeyInput.Key == irr::KEY_RETURN) {
                enterKeyPressed_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_ESCAPE) {
                escapeKeyPressed_ = true;
            }
            // Slash key (forward slash or numpad divide)
            if (event.KeyInput.Key == irr::KEY_OEM_2 || event.KeyInput.Key == irr::KEY_DIVIDE) {
                slashKeyPressed_ = true;
            }

            // Queue key event for chat input (all printable characters)
            KeyEvent keyEvent;
            keyEvent.key = event.KeyInput.Key;
            keyEvent.character = event.KeyInput.Char;
            keyEvent.shift = event.KeyInput.Shift;
            keyEvent.ctrl = event.KeyInput.Control;
            pendingKeyEvents_.push_back(keyEvent);

            // Collision debug controls (Player mode)
            // C: Toggle collision on/off
            if (event.KeyInput.Key == irr::KEY_KEY_C && !event.KeyInput.Control) {
                collisionToggleRequested_ = true;
            }
            // Ctrl+C: Toggle collision debug output
            if (event.KeyInput.Key == irr::KEY_KEY_C && event.KeyInput.Control) {
                collisionDebugToggleRequested_ = true;
            }
            // T: Adjust collision check height (T=up)
            if (event.KeyInput.Key == irr::KEY_KEY_T) {
                collisionHeightDelta_ = event.KeyInput.Shift ? 0.1f : 1.0f;
            }
            // Y/B: Adjust step height (Y=up, B=down)
            if (event.KeyInput.Key == irr::KEY_KEY_Y) {
                stepHeightDelta_ = event.KeyInput.Shift ? 0.1f : 1.0f;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_B && !event.KeyInput.Control) {
                stepHeightDelta_ = event.KeyInput.Shift ? -0.1f : -1.0f;
            }

            // Save entities on F10
            if (event.KeyInput.Key == irr::KEY_F10) {
                saveEntitiesRequested_ = true;
            }

            // Lighting toggle on F11
            if (event.KeyInput.Key == irr::KEY_F11) {
                lightingToggleRequested_ = true;
            }

            // Screenshot on F12
            if (event.KeyInput.Key == irr::KEY_F12) {
                screenshotRequested_ = true;
            }

            // Escape: Clear target, Shift+Escape: Quit
            if (event.KeyInput.Key == irr::KEY_ESCAPE) {
                if (event.KeyInput.Shift) {
                    quitRequested_ = true;
                } else {
                    clearTargetRequested_ = true;
                }
            }

            // Animation speed control: [ to decrease, ] to increase
            if (event.KeyInput.Key == irr::KEY_OEM_4) {  // '[' key
                animSpeedDelta_ = -0.1f;
            }
            if (event.KeyInput.Key == irr::KEY_OEM_6) {  // ']' key
                animSpeedDelta_ = 0.1f;
            }

            // Ambient light control: Page Up to increase, Page Down to decrease
            if (event.KeyInput.Key == irr::KEY_PRIOR) {  // Page Up
                ambientLightDelta_ = event.KeyInput.Shift ? 0.01f : 0.05f;
            }
            if (event.KeyInput.Key == irr::KEY_NEXT) {  // Page Down
                ambientLightDelta_ = event.KeyInput.Shift ? -0.01f : -0.05f;
            }

            // Corpse Z offset control: P to raise, Shift+P to lower
            if (event.KeyInput.Key == irr::KEY_KEY_P) {
                corpseZOffsetDelta_ = event.KeyInput.Shift ? -1.0f : 1.0f;
            }

            // Eye height control: Y to raise, Shift+Y to lower
            if (event.KeyInput.Key == irr::KEY_KEY_Y) {
                eyeHeightDelta_ = event.KeyInput.Shift ? -1.0f : 1.0f;
            }

            // Spell particle control: Ctrl+- to decrease, Ctrl+= to increase
            // Shift for fine control (0.1 step), normal is 0.5 step
            if (event.KeyInput.Control) {
                float particleStep = event.KeyInput.Shift ? 0.1f : 0.5f;
                if (event.KeyInput.Key == irr::KEY_MINUS) {
                    particleMultiplierDelta_ = -particleStep;
                }
                if (event.KeyInput.Key == irr::KEY_PLUS) {
                    particleMultiplierDelta_ = particleStep;
                }
            }

            // F7: Toggle helm debug mode
            if (event.KeyInput.Key == irr::KEY_F7) {
                helmDebugToggleRequested_ = true;
            }

            // Helm UV adjustment keys (active anytime, not just in helm debug mode):
            // Arrow keys: U/V offset
            // I/K: U offset, J/L: V offset (alternative)
            // O/P: U scale, comma/period: V scale
            // -/=: Rotation
            // 0: Reset all
            // Ctrl+S: Swap UV
            // Ctrl+V: Toggle V flip
            // Ctrl+U: Toggle U flip
            // F8: Print helm state
            float uvStep = event.KeyInput.Shift ? 0.01f : 0.1f;
            float scaleStep = event.KeyInput.Shift ? 0.01f : 0.1f;
            float rotStep = event.KeyInput.Shift ? 1.0f : 15.0f;

            // U offset: I/K keys or Left/Right arrows with Alt
            if (event.KeyInput.Key == irr::KEY_KEY_I) {
                helmUOffsetDelta_ = -uvStep;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_K) {
                helmUOffsetDelta_ = uvStep;
            }
            // V offset: J/L keys
            if (event.KeyInput.Key == irr::KEY_KEY_J) {
                helmVOffsetDelta_ = -uvStep;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_L) {
                helmVOffsetDelta_ = uvStep;
            }
            // U scale: O/P keys
            if (event.KeyInput.Key == irr::KEY_KEY_O) {
                helmUScaleDelta_ = -scaleStep;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_P) {
                helmUScaleDelta_ = scaleStep;
            }
            // V scale: comma/period keys
            if (event.KeyInput.Key == irr::KEY_COMMA) {
                helmVScaleDelta_ = -scaleStep;
            }
            if (event.KeyInput.Key == irr::KEY_PERIOD) {
                helmVScaleDelta_ = scaleStep;
            }
            // Rotation: -/= keys (minus/equals)
            if (event.KeyInput.Key == irr::KEY_MINUS) {
                helmRotationDelta_ = -rotStep;
            }
            if (event.KeyInput.Key == irr::KEY_PLUS) {
                helmRotationDelta_ = rotStep;
            }
            // F8: Print state
            if (event.KeyInput.Key == irr::KEY_F8) {
                helmPrintStateRequested_ = true;
            }
            // 0 key: Reset (when not in camera movement)
            if (event.KeyInput.Key == irr::KEY_KEY_0) {
                helmResetRequested_ = true;
            }
            // Ctrl+S: Swap UV
            if (event.KeyInput.Key == irr::KEY_KEY_S && event.KeyInput.Control) {
                helmUVSwapRequested_ = true;
            }
            // Ctrl+V: Toggle V flip
            if (event.KeyInput.Key == irr::KEY_KEY_V && event.KeyInput.Control) {
                helmVFlipRequested_ = true;
            }
            // Ctrl+U: Toggle U flip
            if (event.KeyInput.Key == irr::KEY_KEY_U && event.KeyInput.Control) {
                helmUFlipRequested_ = true;
            }

            // Also support numpad if available
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

            // H/N: Cycle head variant (prev/next)
            if (event.KeyInput.Key == irr::KEY_KEY_H && !event.KeyInput.Control) {
                headVariantCycleDelta_ = -1;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_N) {
                headVariantCycleDelta_ = 1;
            }

            // Repair mode controls: X/Y/Z = rotate, 1/2/3 = flip, R = reset
            // Note: These are captured always but only processed when in Repair mode
            float repairRotStep = 15.0f;  // Fixed 15-degree rotation increment
            if (event.KeyInput.Key == irr::KEY_KEY_X && !event.KeyInput.Control) {
                repairRotateXDelta_ = event.KeyInput.Shift ? -repairRotStep : repairRotStep;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_Y && !event.KeyInput.Control) {
                repairRotateYDelta_ = event.KeyInput.Shift ? -repairRotStep : repairRotStep;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_Z && !event.KeyInput.Control) {
                repairRotateZDelta_ = event.KeyInput.Shift ? -repairRotStep : repairRotStep;
            }
            // 1/2/3 keys for flip toggles (repair mode only - conflicts with spell gems)
            if (event.KeyInput.Key == irr::KEY_KEY_1 && event.KeyInput.Control) {
                repairFlipXRequested_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_2 && event.KeyInput.Control) {
                repairFlipYRequested_ = true;
            }
            if (event.KeyInput.Key == irr::KEY_KEY_3 && event.KeyInput.Control) {
                repairFlipZRequested_ = true;
            }
            // R key for reset (Ctrl+R to avoid conflict with autorun)
            if (event.KeyInput.Key == irr::KEY_KEY_R && event.KeyInput.Control) {
                repairResetRequested_ = true;
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

    // Create event receiver
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

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

    // Apply initial settings
    wireframeMode_ = config.wireframe;
    fogEnabled_ = config.fog;
    lightingEnabled_ = config.lighting;

    initialized_ = true;
    lastFpsTime_ = device_->getTimer()->getTime();

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer initialized: {}x{}", config.width, config.height);
    return true;
}

void IrrlichtRenderer::shutdown() {
    unloadZone();

    entityRenderer_.reset();
    cameraController_.reset();
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

    camera_->setFarValue(50000.0f);
    camera_->setNearValue(1.0f);

    cameraController_ = std::make_unique<CameraController>(camera_);
    cameraController_->setMoveSpeed(500.0f);
    cameraController_->setMouseSensitivity(0.2f);
}

void IrrlichtRenderer::setupLighting() {
    // Add ambient light - will be updated by time of day
    smgr_->setAmbientLight(irr::video::SColorf(config_.ambientIntensity,
                                                config_.ambientIntensity,
                                                config_.ambientIntensity, 1.0f));

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
    }
}

void IrrlichtRenderer::updateTimeOfDay(uint8_t hour, uint8_t minute) {
    if (!smgr_) return;

    currentHour_ = hour;
    currentMinute_ = minute;

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

    smgr_->setAmbientLight(irr::video::SColorf(r, g, b, 1.0f));

    // Update sun light intensity
    if (sunLight_) {
        irr::video::SLight& lightData = sunLight_->getLightData();
        lightData.DiffuseColor = irr::video::SColorf(sunIntensity, sunIntensity, sunIntensity * 0.9f, 1.0f);
    }
}

void IrrlichtRenderer::updateObjectLights() {
    if (objectLights_.empty() || !camera_) return;

    const float maxDistance = 500.0f;  // Maximum distance to enable a light
    const size_t maxActiveLights = 12;  // Maximum number of active object lights

    irr::core::vector3df cameraPos = camera_->getPosition();

    // Calculate distances and sort by distance
    std::vector<std::pair<float, size_t>> distances;
    distances.reserve(objectLights_.size());

    for (size_t i = 0; i < objectLights_.size(); ++i) {
        float dist = cameraPos.getDistanceFrom(objectLights_[i].position);
        if (dist <= maxDistance) {
            distances.push_back({dist, i});
        }
    }

    // Sort by distance (closest first)
    std::sort(distances.begin(), distances.end());

    // First, disable all lights
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            objLight.node->setVisible(false);
        }
    }

    // Enable the closest N lights
    size_t enabledCount = std::min(distances.size(), maxActiveLights);
    for (size_t i = 0; i < enabledCount; ++i) {
        size_t idx = distances[i].second;
        if (objectLights_[idx].node) {
            objectLights_[idx].node->setVisible(true);
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

            // Update each mesh buffer
            // Track cumulative vertex offset across buffers
            size_t vertexOffset = 0;
            for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = vam.mesh->getMeshBuffer(b);
                irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                irr::u32 vertexCount = buffer->getVertexCount();

                // Check if we have enough animation data for this buffer
                if (vertexOffset + vertexCount > expectedVerts) {
                    vertexOffset += vertexCount;
                    continue;
                }

                // Use vertex mapping if available, otherwise fall back to sequential
                bool useMapping = (vam.vertexMapping.size() > b &&
                                   vam.vertexMapping[b].size() == vertexCount);

                for (irr::u32 v = 0; v < vertexCount; ++v) {
                    size_t animIdx;
                    if (useMapping) {
                        animIdx = vam.vertexMapping[b][v];
                        if (animIdx == SIZE_MAX) {
                            // No match found for this vertex, skip it
                            continue;
                        }
                    } else {
                        animIdx = vertexOffset + v;
                    }

                    // Bounds check
                    if (animIdx >= expectedVerts) {
                        continue;
                    }

                    // Get new position from animation frame (EQ coordinates)
                    float eqX = frame.positions[animIdx * 3 + 0];
                    float eqY = frame.positions[animIdx * 3 + 1];
                    float eqZ = frame.positions[animIdx * 3 + 2];

                    // Apply EQ->Irrlicht coordinate transform (same as zone_geometry.cpp)
                    // EQ Z-up -> Irrlicht Y-up
                    vertices[v].Pos.X = eqX;
                    vertices[v].Pos.Y = eqZ;
                    vertices[v].Pos.Z = eqY;
                }

                // Mark buffer as dirty so Irrlicht knows to re-upload it
                buffer->setDirty(irr::scene::EBT_VERTEX);
                vertexOffset += vertexCount;
            }
        }
    }
}

void IrrlichtRenderer::setupFog() {
    if (!driver_ || !currentZone_ || !currentZone_->geometry) {
        return;
    }

    float zoneWidth = currentZone_->geometry->maxX - currentZone_->geometry->minX;
    float zoneDepth = currentZone_->geometry->maxY - currentZone_->geometry->minY;
    float zoneSize = std::max(zoneWidth, zoneDepth);

    float fogStart = zoneSize * 0.2f;
    float fogEnd = zoneSize * 0.8f;

    fogStart = std::max(100.0f, std::min(fogStart, 5000.0f));
    fogEnd = std::max(fogStart + 500.0f, std::min(fogEnd, 20000.0f));

    irr::video::SColor fogColor(255, 50, 50, 80);

    driver_->setFog(
        fogColor,
        irr::video::EFT_FOG_LINEAR,
        fogStart,
        fogEnd,
        0.01f,
        true,
        false
    );
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
}

void IrrlichtRenderer::updateHUD() {
    if (!hudText_ || !hudEnabled_) return;

    std::wstringstream text;
    std::wstringstream hotkeys;

    if (rendererMode_ == RendererMode::Player) {
        // === PLAYER MODE HUD ===
        // No HUD text in player mode - UI windows handle player/target info
        // F2 toggles HUD which would show debug info in admin mode
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
        hotkeys << L"[/]=AnimSpd  P=CorpseZ";
    }

    hudText_->setText(text.str().c_str());
    if (hotkeysText_) {
        hotkeysText_->setText(hotkeys.str().c_str());
    }
}

bool IrrlichtRenderer::loadZone(const std::string& zoneName, float progressStart, float progressEnd) {
    if (!initialized_) {
        LOG_ERROR(MOD_GRAPHICS, "Renderer not initialized");
        return false;
    }

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

    S3DLoader loader;
    if (!loader.loadZone(zonePath)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to load zone: {}", loader.getError());
        return false;
    }

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

    drawLoadingScreen(scaleProgress(0.40f), L"Creating zone geometry...");
    createZoneMesh();

    drawLoadingScreen(scaleProgress(0.60f), L"Creating object meshes...");
    createObjectMeshes();

    drawLoadingScreen(scaleProgress(0.85f), L"Setting up zone lights...");
    createZoneLights();

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

    drawLoadingScreen(scaleProgress(1.0f), L"Zone loaded!");

    return true;
}

void IrrlichtRenderer::unloadZone() {
    // Note: zoneReady_ is controlled by EverQuest based on network state,
    // not by graphics loading state. Don't reset it here.

    // Reset animated texture manager
    animatedTextureManager_.reset();

    // Remove zone collision selector
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }

    // Remove zone mesh
    if (zoneMeshNode_) {
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    // Remove object nodes
    for (auto* node : objectNodes_) {
        if (node) {
            node->remove();
        }
    }
    objectNodes_.clear();

    // Remove zone light nodes
    for (auto* node : zoneLightNodes_) {
        if (node) {
            node->remove();
        }
    }
    zoneLightNodes_.clear();

    // Clear entity renderer
    if (entityRenderer_) {
        entityRenderer_->clearEntities();
    }

    // Clear door manager
    if (doorManager_) {
        doorManager_->clearDoors();
        doorManager_->setZone(nullptr);
    }

    // Clear object lights (they reference freed zone data)
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            objLight.node->remove();
        }
    }
    objectLights_.clear();

    // Clear vertex animated meshes (they reference freed zone mesh buffers)
    vertexAnimatedMeshes_.clear();

    currentZone_.reset();
    currentZoneName_.clear();
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
    irr::scene::IMesh* mesh = nullptr;

    if (!currentZone_->textures.empty() && !currentZone_->geometry->textureNames.empty()) {
        mesh = builder.buildTexturedMesh(*currentZone_->geometry, currentZone_->textures);
    } else {
        mesh = builder.buildColoredMesh(*currentZone_->geometry);
    }

    if (mesh) {
        zoneMeshNode_ = smgr_->addMeshSceneNode(mesh);
        if (zoneMeshNode_) {
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

            // Set up collision detection using the zone mesh
            setupZoneCollision();

            // Initialize animated texture manager for zone textures
            animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());
            int animCount = animatedTextureManager_->initialize(
                *currentZone_->geometry, currentZone_->textures, mesh);
            if (animCount > 0) {
                LOG_DEBUG(MOD_GRAPHICS, "Initialized {} animated zone textures", animCount);
                // Register the zone scene node for animated texture updates
                animatedTextureManager_->addSceneNode(zoneMeshNode_);
            }
        }
        mesh->drop();
    }
}

void IrrlichtRenderer::createObjectMeshes() {
    if (!currentZone_) {
        return;
    }

    for (auto* node : objectNodes_) {
        if (node) {
            node->remove();
        }
    }
    objectNodes_.clear();

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
    std::map<std::string, irr::scene::IMesh*> meshCache;

    for (const auto& objInstance : currentZone_->objects) {
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        const std::string& objName = objInstance.placeable->getName();
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

        // Calculate height offset for models with non-base origins
        // If the bounding box MinEdge.Y is negative, the model origin is above its base
        float heightOffset = 0;
        irr::core::aabbox3df bbox = mesh->getBoundingBox();
        heightOffset = -bbox.MinEdge.Y * scaleZ;  // scaleZ is EQ's Z (height) -> Irrlicht Y

        // Apply transforms (EQ Z-up to Irrlicht Y-up)
        float x = objInstance.placeable->getX();
        float y = objInstance.placeable->getY();
        float z = objInstance.placeable->getZ();
        node->setPosition(irr::core::vector3df(x, z + heightOffset, y));

        float rotX = objInstance.placeable->getRotateX();
        float rotY = objInstance.placeable->getRotateY();
        float rotZ = objInstance.placeable->getRotateZ();
        node->setRotation(irr::core::vector3df(rotX, rotZ, rotY));

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
            // by matching frame 0 positions (with coordinate transform)
            if (!vam.animData->frames.empty()) {
                const auto& frame0 = vam.animData->frames[0];
                size_t animVertCount = frame0.positions.size() / 3;

                vam.vertexMapping.resize(mesh->getMeshBufferCount());
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                    irr::u32 vertexCount = buffer->getVertexCount();

                    vam.vertexMapping[b].resize(vertexCount, SIZE_MAX);

                    for (irr::u32 mv = 0; mv < vertexCount; ++mv) {
                        // Mesh vertex is in Irrlicht coords (x, z, y from EQ)
                        float meshX = verts[mv].Pos.X;
                        float meshY = verts[mv].Pos.Y;  // This is EQ Z
                        float meshZ = verts[mv].Pos.Z;  // This is EQ Y

                        // Find matching animation vertex
                        float bestDist = 1e10f;
                        size_t bestIdx = SIZE_MAX;
                        for (size_t av = 0; av < animVertCount; ++av) {
                            // Animation vertex in EQ coords
                            float animX = frame0.positions[av * 3 + 0];
                            float animY = frame0.positions[av * 3 + 1];  // EQ Y
                            float animZ = frame0.positions[av * 3 + 2];  // EQ Z

                            // Compare with transform: mesh(X,Y,Z) should match anim(X,Z,Y)
                            float dx = meshX - animX;
                            float dy = meshY - animZ;  // mesh Y (Irrlicht) = anim Z (EQ)
                            float dz = meshZ - animY;  // mesh Z (Irrlicht) = anim Y (EQ)
                            float dist = dx*dx + dy*dy + dz*dz;

                            if (dist < bestDist) {
                                bestDist = dist;
                                bestIdx = av;
                            }
                        }

                        if (bestDist < 0.001f) {  // Close enough match
                            vam.vertexMapping[b][mv] = bestIdx;
                        }
                    }
                }
            }

            vertexAnimatedMeshes_.push_back(vam);
            LOG_DEBUG(MOD_GRAPHICS, "Registered vertex animated mesh '{}' with {} frames", objName, vam.animData->frames.size());
        }

        // Store the object name in the scene node for later identification
        node->setName(objName.c_str());
        objectNodes_.push_back(node);

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
            // Lanterns/lamps - warm yellow
            isLightSource = true;
            lightColor = irr::video::SColorf(1.0f, 0.85f, 0.6f, 1.0f);
            lightRadius = 100.0f;
        } else if (upperName.find("CANDLE") != std::string::npos) {
            // Candles - soft yellow, smaller radius
            isLightSource = true;
            lightColor = irr::video::SColorf(1.0f, 0.9f, 0.7f, 1.0f);
            lightRadius = 50.0f;
        }

        if (isLightSource) {
            // Create light at object position (at object height, not elevated)
            irr::core::vector3df lightPos(x, z + heightOffset, y);

            irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
                nullptr, lightPos, lightColor, lightRadius * 1.5f);  // Increase effective radius

            if (lightNode) {
                irr::video::SLight& lightData = lightNode->getLightData();
                lightData.Type = irr::video::ELT_POINT;
                // Lower attenuation for more visible ground lighting
                lightData.Attenuation = irr::core::vector3df(0.1f, 0.0f, 0.00002f);
                lightNode->setVisible(false);  // Start hidden, updateObjectLights will enable nearby ones

                ObjectLight objLight;
                objLight.node = lightNode;
                objLight.position = lightPos;
                objLight.objectName = objName;
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
    for (auto* node : zoneLightNodes_) {
        if (node) {
            node->remove();
        }
    }
    zoneLightNodes_.clear();

    if (!currentZone_ || currentZone_->lights.empty()) {
        return;
    }

    // Limit number of lights for performance (software renderer is slow with many lights)
    // Irrlicht software renderer typically supports 8 lights max per scene
    const size_t maxLights = 8;
    size_t lightCount = std::min(currentZone_->lights.size(), maxLights);

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = currentZone_->lights[i];

        // Transform EQ coordinates (Z-up) to Irrlicht (Y-up)
        // EQ: x, y, z -> Irrlicht: x, z, y
        irr::core::vector3df pos(light->x, light->z, light->y);

        // Create point light
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

            // Start with lights disabled (user can toggle with F4)
            lightNode->setVisible(zoneLightsEnabled_);

            zoneLightNodes_.push_back(lightNode);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Created {} zone lights (of {} available)", zoneLightNodes_.size(), currentZone_->lights.size());
}

bool IrrlichtRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                     float x, float y, float z, float heading, bool isPlayer,
                                     uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                     bool isCorpse) {
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->createEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse);

    // If this is the player entity, handle visibility based on current mode
    if (result && isPlayer) {
        // In Player mode with FirstPerson camera, hide the player entity
        bool shouldHide = (rendererMode_ == RendererMode::Player && cameraMode_ == CameraMode::FirstPerson);
        entityRenderer_->setPlayerEntityVisible(!shouldHide);
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

void IrrlichtRenderer::clearEntities() {
    if (entityRenderer_) {
        entityRenderer_->clearEntities();
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

        // Debug: Log player model info at zone-in
        float modelYOffset = entityRenderer_->getPlayerModelYOffset();
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] setPlayerSpawnId: modelYOffset={:.2f} visible={}", modelYOffset, !shouldHide);
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
        // Position camera at player location (fallback - main update is in updatePlayerMovement)
        // Server Z is model center, so approximate head height using |modelYOffset|
        float camZ = z + 5.0f;  // Default fallback
        float modelYOffset = 0.0f;
        if (entityRenderer_) {
            modelYOffset = entityRenderer_->getPlayerModelYOffset();
            camZ = z + std::abs(modelYOffset);  // Head is at top of model
        }
        camZ += playerConfig_.eyeHeight;

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

        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] Camera mode=FirstPerson: modelYOffset={:.2f} eyeHeight={:.2f}",
                 modelYOffset, playerConfig_.eyeHeight);
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

bool IrrlichtRenderer::processFrame(float deltaTime) {
    LOG_TRACE(MOD_GRAPHICS, "processFrame: entered");

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

    // Handle vendor toggle (V key) - only in Player mode, not when chat focused
    if (eventReceiver_->vendorToggleRequested() && rendererMode_ == RendererMode::Player && !chatInputFocused) {
        if (vendorToggleCallback_) {
            vendorToggleCallback_();
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

    // Handle helm debug adjustments - not when chat focused (uses letter/number keys)
    if (entityRenderer_ && entityRenderer_->isHelmDebugEnabled() && !chatInputFocused) {
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
            // Don't use mouse for camera if UI has capture
            bool mouseEnabled = eventReceiver_->isLeftButtonDown() && !windowManagerCapture_;
            int mouseDeltaX = windowManagerCapture_ ? 0 : eventReceiver_->getMouseDeltaX();
            int mouseDeltaY = windowManagerCapture_ ? 0 : eventReceiver_->getMouseDeltaY();

            // Skip movement keys if chat has focus
            bool forward = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_W) || eventReceiver_->isKeyDown(irr::KEY_UP));
            bool backward = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_S) || eventReceiver_->isKeyDown(irr::KEY_DOWN));
            bool left = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_A) || eventReceiver_->isKeyDown(irr::KEY_LEFT));
            bool right = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_D) || eventReceiver_->isKeyDown(irr::KEY_RIGHT));
            bool up = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_E) || eventReceiver_->isKeyDown(irr::KEY_SPACE));
            bool down = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_Q) || eventReceiver_->isKeyDown(irr::KEY_LCONTROL));

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

    // Update entity interpolation (smooth NPC movement between server updates)
    if (entityRenderer_) {
        entityRenderer_->updateInterpolation(deltaTime);
        // Update entity casting bars (timeout checks, etc.)
        entityRenderer_->updateEntityCastingBars(deltaTime, camera_);
    }

    // Update door animations
    if (doorManager_) {
        doorManager_->update(deltaTime);
    }

    // Update spell visual effects (particles, cast glows, impacts)
    if (spellVisualFX_) {
        spellVisualFX_->update(deltaTime);
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint F (before animated textures)");

    // Update animated textures (flames, water, etc.)
    if (animatedTextureManager_) {
        animatedTextureManager_->update(deltaTime * 1000.0f);  // Convert to milliseconds
    }

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint G (before vertex animations)");

    // Update vertex animations (flags, banners, etc.)
    updateVertexAnimations(deltaTime * 1000.0f);  // Convert to milliseconds

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint H (before object lights)");

    // Update object lights (distance-based culling)
    updateObjectLights();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint I (before HUD)");

    // Update HUD animation timer
    hudAnimTimer_ += deltaTime;
    if (hudAnimTimer_ > 10000.0f) {
        hudAnimTimer_ = 0.0f;  // Prevent overflow
    }

    // Update HUD
    updateHUD();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint J (before render)");

    // If zone not ready, show loading screen instead of rendering zone
    if (!zoneReady_) {
        drawLoadingScreen(loadingProgress_, loadingText_);
        LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint M (done - loading screen)");
        return true;
    }

    // Render
    driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
    smgr_->drawAll();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint K (after drawAll)");

    // Draw selection box around targeted entity
    drawTargetSelectionBox();

    // Draw bounding box around repair target (if in Repair mode with target)
    if (rendererMode_ == RendererMode::Repair && repairTargetNode_) {
        drawRepairTargetBoundingBox();
    }

    // Draw collision debug lines (if debug mode enabled)
    if (playerConfig_.collisionDebug) {
        drawCollisionDebugLines(deltaTime);
    }

    // Draw entity casting bars (above other entities' heads)
    if (entityRenderer_) {
        entityRenderer_->renderEntityCastingBars(driver_, guienv_, camera_);
    }

    guienv_->drawAll();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: checkpoint L (after GUI)");

    // Render inventory UI windows (on top of HUD)
    if (windowManager_) {
        windowManager_->render();
    }

    // Draw zone line overlay (pink border when in zone line)
    drawZoneLineOverlay();

    driver_->endScene();

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
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

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

    LOG_INFO(MOD_GRAPHICS, "Lighting: {}", (lightingEnabled_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleZoneLights() {
    zoneLightsEnabled_ = !zoneLightsEnabled_;

    // Toggle visibility of all zone lights
    for (auto* node : zoneLightNodes_) {
        if (node) {
            node->setVisible(zoneLightsEnabled_);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Zone lights: {} ({} lights)", (zoneLightsEnabled_ ? "ON" : "OFF"), zoneLightNodes_.size());
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

// --- Renderer Mode Implementation ---

void IrrlichtRenderer::setRendererMode(RendererMode mode) {
    if (rendererMode_ == mode) {
        return;
    }

    rendererMode_ = mode;

    if (mode == RendererMode::Player) {
        // Switch to Player mode
        // Default to FirstPerson camera if not already in FirstPerson or Follow
        if (cameraMode_ == CameraMode::Free) {
            cameraMode_ = CameraMode::FirstPerson;
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
        // Keep current camera mode
        if (cameraMode_ == CameraMode::Free) {
            cameraMode_ = CameraMode::FirstPerson;
        }
        // Show player entity
        if (entityRenderer_) {
            entityRenderer_->setPlayerEntityVisible(true);
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

// --- Player Mode Movement Implementation ---

void IrrlichtRenderer::updatePlayerMovement(float deltaTime) {
    if (rendererMode_ != RendererMode::Player) {
        return;
    }

    // Check if chat has focus - skip movement keys if so
    bool chatHasFocus = windowManager_ && windowManager_->isChatInputFocused();

    // Read keyboard state for EQ-style movement (disabled when chat has focus)
    bool forward = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_W) || eventReceiver_->isKeyDown(irr::KEY_UP));
    bool backward = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_S) || eventReceiver_->isKeyDown(irr::KEY_DOWN));
    bool turnLeft = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_A) || eventReceiver_->isKeyDown(irr::KEY_LEFT));
    bool turnRight = !chatHasFocus && (eventReceiver_->isKeyDown(irr::KEY_KEY_D) || eventReceiver_->isKeyDown(irr::KEY_RIGHT));
    bool strafeLeft = !chatHasFocus && eventReceiver_->isKeyDown(irr::KEY_KEY_Q);
    bool strafeRight = !chatHasFocus && eventReceiver_->isKeyDown(irr::KEY_KEY_E);
    bool jumpPressed = !chatHasFocus && eventReceiver_->isKeyDown(irr::KEY_SPACE);

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

    // Mouse look (only when left mouse button held and UI doesn't have capture)
    if (eventReceiver_->isLeftButtonDown() && !windowManagerCapture_) {
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

        // Get the player's model Y offset for ground snapping
        // Server Z represents the CENTER of the model, so when snapping to ground,
        // we need to offset by -modelYOffset to place feet at ground level
        float modelYOffset = 0.0f;
        if (entityRenderer_) {
            modelYOffset = entityRenderer_->getPlayerModelYOffset();
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
                    // Compare feet position (serverZ + modelYOffset) with ground
                    float feetZ = newZ + modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap feet to ground (server Z = groundZ - modelYOffset)
                        newZ = groundZ - modelYOffset;
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
                    float currentFeetZ = playerZ_ + modelYOffset;
                    float stepHeight = groundZ - currentFeetZ;

                    // Only limit stepping UP (positive step height) - player can always walk down/off edges
                    if (stepHeight <= playerConfig_.collisionStepHeight) {
                        // Snap feet to ground (server Z = groundZ - modelYOffset)
                        newZ = groundZ - modelYOffset;
                        positionChanged = true;
                    } else if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[Irrlicht] Step up too high: {}", stepHeight);
                    }
                }
            } else {
                // Horizontal movement blocked - try wall sliding or continue jump vertically
                if (playerMovement_.isJumping) {
                    // Even if horizontal is blocked, continue vertical jump movement
                    float groundZ = findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
                    float feetZ = newZ + modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap feet to ground
                        newZ = groundZ - modelYOffset;
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
                        float currentFeetZ = playerZ_ + modelYOffset;
                        float stepHeight = groundZ - currentFeetZ;
                        // Only limit stepping UP - can always step down
                        if (stepHeight <= playerConfig_.collisionStepHeight) {
                            newY = playerY_;
                            newZ = groundZ - modelYOffset;
                            positionChanged = true;
                            LOG_TRACE(MOD_MOVEMENT, "Wall slide X");
                        }
                    }
                    // Try wall sliding - Y only
                    if (!positionChanged) {
                        irr::core::vector3df rayEndY(playerX_, playerZ_ + checkHeight, newY);
                        if (!checkCollisionIrrlicht(rayStart, rayEndY, hitPoint, hitTriangle)) {
                            float groundZ = findGroundZIrrlicht(playerX_, newY, playerZ_, modelYOffset);
                            float currentFeetZ = playerZ_ + modelYOffset;
                            float stepHeight = groundZ - currentFeetZ;
                            // Only limit stepping UP - can always step down
                            if (stepHeight <= playerConfig_.collisionStepHeight) {
                                newX = playerX_;
                                newZ = groundZ - modelYOffset;
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
                    float feetZ = newZ + modelYOffset;
                    if (targetGroundZ != BEST_Z_INVALID && feetZ <= targetGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = targetGroundZ - modelYOffset;
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
                    float feetZ = newZ + modelYOffset;
                    if (currentGroundZ != BEST_Z_INVALID && feetZ <= currentGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = currentGroundZ - modelYOffset;
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
                float currentFeetZ = playerZ_ + modelYOffset;
                float stepHeight = targetGroundZ - currentFeetZ;

                LOG_TRACE(MOD_MOVEMENT, "HCMap Step height: {} (max up: {})", stepHeight, playerConfig_.collisionStepHeight);

                // Only limit stepping UP - player can always walk down/off edges
                if (stepHeight <= playerConfig_.collisionStepHeight) {
                    bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, targetGroundZ);

                    LOG_TRACE(MOD_MOVEMENT, "HCMap LOS check: {}", (losCheck ? "CLEAR" : "BLOCKED"));

                    if (losCheck) {
                        newZ = targetGroundZ - modelYOffset;
                        positionChanged = true;
                    } else {
                        // Wall sliding for HCMap
                        float xGroundZ = findGroundZ(newX, playerY_, playerZ_);
                        float xStepHeight = xGroundZ - currentFeetZ;
                        if (xGroundZ != BEST_Z_INVALID &&
                            xStepHeight <= playerConfig_.collisionStepHeight &&
                            checkMovementCollision(playerX_, playerY_, playerZ_, newX, playerY_, xGroundZ)) {
                            newY = playerY_;
                            newZ = xGroundZ - modelYOffset;
                            positionChanged = true;
                        } else {
                            float yGroundZ = findGroundZ(playerX_, newY, playerZ_);
                            float yStepHeight = yGroundZ - currentFeetZ;
                            if (yGroundZ != BEST_Z_INVALID &&
                                yStepHeight <= playerConfig_.collisionStepHeight &&
                                checkMovementCollision(playerX_, playerY_, playerZ_, playerX_, newY, yGroundZ)) {
                                newX = playerX_;
                                newZ = yGroundZ - modelYOffset;
                                positionChanged = true;
                            }
                        }
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

    // Update camera if anything changed
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

            // If no head bone, compute approximate head height from model offset
            // Server Z (playerZ_) is model center, |modelYOffset| is half-height of model
            // Head is at top of model = serverZ + |modelYOffset|
            if (!gotHeadBone && entityRenderer_) {
                float modelYOffset = entityRenderer_->getPlayerModelYOffset();
                camZ = playerZ_ + std::abs(modelYOffset);  // Approximate head height

                // Debug logging
                static int fallbackLogCount = 0;
                if (fallbackLogCount++ % 500 == 0) {
                    LOG_DEBUG(MOD_GRAPHICS, "Camera fallback: playerZ={:.2f} modelYOffset={:.2f} => camZ={:.2f} (before eyeHeight)",
                              playerZ_, modelYOffset, camZ);
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
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_);
        }

        // Compute movement state for server sync and local animation
        bool isMoving = playerMovement_.moveForward || playerMovement_.moveBackward ||
                       playerMovement_.strafeLeft || playerMovement_.strafeRight;

        // Track movement state transitions to detect when player stops
        static bool wasMoving = false;
        bool stoppedMoving = wasMoving && !isMoving;
        wasMoving = isMoving;

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
            update.dx = playerX_ - prevX;
            update.dy = playerY_ - prevY;
            update.dz = playerZ_ - prevZ;
            movementCallback_(update);
            lastCallbackTime = now;

            // Update previous position only when we actually send an update
            prevX = playerX_;
            prevY = playerY_;
            prevZ = playerZ_;
        }
    }

    // Update player entity animation based on movement state (runs every frame)
    if (entityRenderer_) {
        if (isMoving) {
            // Use run animation for forward movement when running, walk for everything else
            if (playerMovement_.moveForward && playerMovement_.isRunning) {
                entityRenderer_->setPlayerEntityAnimation("l02", true);  // Run
            } else {
                entityRenderer_->setPlayerEntityAnimation("l01", true);  // Walk
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

    if (groundZ == BEST_Z_INVALID) {
        return currentZ;  // No ground found, keep current Z
    }

    return groundZ;
}

// --- Irrlicht-based Collision Detection (using zone mesh) ---

void IrrlichtRenderer::setupZoneCollision() {
    // Clean up old selector
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }

    if (!zoneMeshNode_ || !smgr_) {
        return;
    }

    // Create triangle selector from zone mesh
    zoneTriangleSelector_ = smgr_->createTriangleSelectorFromBoundingBox(zoneMeshNode_);
    // For more accurate collision, use createTriangleSelector instead (slower but precise)
    // But that can be very slow for large zone meshes, so let's try octree selector
    irr::scene::IMesh* mesh = zoneMeshNode_->getMesh();
    if (mesh) {
        // Use octree selector for better performance on large meshes
        zoneTriangleSelector_ = smgr_->createOctreeTriangleSelector(mesh, zoneMeshNode_, 128);
    }

    if (zoneTriangleSelector_) {
        zoneMeshNode_->setTriangleSelector(zoneTriangleSelector_);
        LOG_DEBUG(MOD_GRAPHICS, "Zone triangle selector created (Irrlicht collision enabled)");
    }

    // Get collision manager
    collisionManager_ = smgr_->getSceneCollisionManager();
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
        return currentZ + modelYOffset;  // Return current feet position
    }

    // currentZ is the model center (server Z), modelYOffset is offset from center to feet (typically negative)
    // Feet position = currentZ + modelYOffset
    // Head position = currentZ - modelYOffset (approximately, assuming symmetric model)
    float feetZ = currentZ + modelYOffset;
    float headZ = currentZ - modelYOffset;  // Approximate head position (mirror of feet offset)
    float maxStepUp = playerConfig_.collisionStepHeight;

    // Cast ray downward from above the head to find ground
    // Irrlicht coords: (x, y, z) where Y is up
    // Input is EQ coords where Z is up, so we convert: EQ(x,y,z) -> Irr(x,z,y)
    irr::core::vector3df rayStart(x, headZ + 2.0f, y);  // Start slightly above head
    irr::core::vector3df rayEnd(x, feetZ - 500.0f, y);  // Cast down from below feet

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;

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

    if (hit) {
        float floorZ = hitPoint.Y;

        // Only accept as valid ground if:
        // 1. It's at or below our feet + step height (we can step up to it)
        // 2. It's not a ceiling (above our feet + step tolerance)
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            return floorZ;
        } else {
            // Hit a ceiling/obstruction - player can't fit, block movement
            // Return an invalid value to signal blocked (return as if ground is way above)
            return feetZ + 1000.0f;
        }
    }

    return feetZ;  // No ground found, return current feet position
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

    // In admin mode or if no collision map, fall back to distance-only
    if (rendererMode_ != RendererMode::Player || !collisionMap_) {
        entityRenderer_->updateNameTags(camera_);
        return;
    }

    // Throttle LOS checks for performance
    lastLOSCheckTime_ += deltaTime;
    if (lastLOSCheckTime_ < playerConfig_.nameTagLOSCheckInterval) {
        return;  // Skip this frame
    }
    lastLOSCheckTime_ = 0.0f;

    // Player eye position (EQ coordinates)
    glm::vec3 playerEye(playerX_, playerY_, playerZ_ + playerConfig_.eyeHeight);

    // Check each entity for LOS visibility
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
        if (doorManager_ && doorInteractCallback_) {
            uint8_t doorId = doorManager_->getDoorAtScreenPos(clickX, clickY, camera_, collisionManager_);
            if (doorId != 0) {
                LOG_INFO(MOD_GRAPHICS, "Door clicked: ID {}", doorId);
                doorInteractCallback_(doorId);
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

} // namespace Graphics
} // namespace EQT
