#pragma once

#include <glm/glm.hpp>
#include <irrlicht.h>
#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * SnowOverlaySettings - Configuration for screen-space snow effect.
 */
struct SnowOverlaySettings {
    bool enabled = true;

    // Vertical scroll speed (slower than rain for gentle fall)
    float scrollSpeedBase = 0.03f;       // Base scroll speed (texture scrolls/sec)
    float scrollSpeedIntensity = 0.02f;  // Additional speed per intensity level

    // Horizontal sway motion (sine wave drift)
    float swayAmplitude = 30.0f;         // Max horizontal pixel offset
    float swayFrequency = 0.5f;          // Sway cycles per second
    float swayPhaseVariation = 0.3f;     // Phase offset between layers (radians factor)

    // Layer configuration (multiple layers for parallax depth)
    int numLayers = 1;
    float layerDepthMin = 2.0f;          // Closest layer distance from camera
    float layerDepthMax = 8.0f;          // Farthest layer distance
    float layerScaleMin = 1.0f;          // UV scale for closest layer (foreground)
    float layerScaleMax = 2.0f;          // UV scale for farthest layer (smaller flakes)
    float layerSpeedMin = 0.6f;          // Scroll speed multiplier for farthest layer
    float layerSpeedMax = 1.0f;          // Scroll speed multiplier for closest layer

    // Opacity
    float baseOpacity = 0.3f;            // Base opacity at intensity 1
    float maxOpacity = 0.8f;             // Max opacity at intensity 10

    // Fog reduction (reduced visibility in heavy snow)
    bool fogReductionEnabled = true;
    float fogStartMin = 50.0f;           // Fog start at intensity 10
    float fogStartMax = 200.0f;          // Fog start at intensity 1
    float fogEndMin = 300.0f;            // Fog end at intensity 10
    float fogEndMax = 800.0f;            // Fog end at intensity 1

    // Sky darkening (less dramatic than rain - overcast feel)
    bool skyDarkeningEnabled = true;
    float skyBrightnessMin = 0.3f;       // Sky brightness at intensity 10
    float skyBrightnessMax = 0.7f;       // Sky brightness at intensity 1
};

/**
 * SnowOverlay - Screen-space snow effect similar to RainOverlay.
 *
 * Renders snow as camera-attached billboard quads with scrolling textures
 * and horizontal sway motion for a gentle drifting appearance. Multiple
 * layers provide parallax depth. Combined with fog reduction for the
 * "blizzard visibility" effect at high intensity.
 */
class SnowOverlay {
public:
    SnowOverlay();
    ~SnowOverlay();

    // Non-copyable
    SnowOverlay(const SnowOverlay&) = delete;
    SnowOverlay& operator=(const SnowOverlay&) = delete;

    /**
     * Initialize the snow overlay system.
     * @param driver Irrlicht video driver
     * @param smgr Scene manager
     * @param eqClientPath Path to EQ client for textures
     * @return true if initialized successfully
     */
    bool initialize(irr::video::IVideoDriver* driver,
                    irr::scene::ISceneManager* smgr,
                    const std::string& eqClientPath = "");

    /**
     * Set snow intensity (0-10).
     * 0 = off, 1-10 = increasing intensity
     */
    void setIntensity(uint8_t intensity);
    uint8_t getIntensity() const { return intensity_; }

    /**
     * Update animation (UV scrolling and sway).
     * @param deltaTime Time since last update in seconds
     * @param cameraPos Camera position in world space
     * @param cameraDir Camera forward direction
     */
    void update(float deltaTime, const glm::vec3& cameraPos, const glm::vec3& cameraDir);

    /**
     * Render the snow overlay.
     * Should be called after scene render but before UI.
     */
    void render();

    /**
     * Check if snow overlay is active.
     */
    bool isActive() const { return intensity_ > 0 && settings_.enabled && initialized_; }

    /**
     * Get current fog settings based on intensity.
     * @param outFogStart Output fog start distance
     * @param outFogEnd Output fog end distance
     * @return true if fog should be modified
     */
    bool getFogSettings(float& outFogStart, float& outFogEnd) const;

    /**
     * Get sky brightness multiplier based on intensity.
     * @param outMultiplier Output brightness multiplier (0.0-1.0)
     * @return true if sky brightness should be modified
     */
    bool getSkyBrightnessMultiplier(float& outMultiplier) const;

    /**
     * Set configuration.
     */
    void setSettings(const SnowOverlaySettings& settings) { settings_ = settings; }
    const SnowOverlaySettings& getSettings() const { return settings_; }

    /**
     * Enable/disable the overlay.
     */
    void setEnabled(bool enabled) { settings_.enabled = enabled; }
    bool isEnabled() const { return settings_.enabled; }

private:
    /**
     * Create or update the snow textures.
     */
    bool createSnowTextures();

    /**
     * Render a single snow layer.
     */
    void renderLayer(int layer, float depth, float uvScale, float opacity, float swayOffset);

    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;

    SnowOverlaySettings settings_;
    uint8_t intensity_ = 0;
    bool initialized_ = false;

    // Intensity-based foreground textures (10 textures, one per intensity level)
    // Small dots/flakes, density increases with intensity
    std::vector<irr::video::ITexture*> intensityTextures_;

    // Background layer textures (mid and far)
    irr::video::ITexture* midLayerTexture_ = nullptr;   // Medium flakes
    irr::video::ITexture* farLayerTexture_ = nullptr;   // Small distant flakes

    // UV scroll offset (animated, vertical)
    float scrollOffset_ = 0.0f;

    // Elapsed time for sway calculation
    float elapsedTime_ = 0.0f;

    // Current camera state
    glm::vec3 cameraPos_{0, 0, 0};
    glm::vec3 cameraDir_{1, 0, 0};
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
