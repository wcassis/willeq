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
 * RainOverlaySettings - Configuration for screen-space rain effect.
 */
struct RainOverlaySettings {
    bool enabled = true;

    // Texture scrolling (1.0 = one full texture scroll per second)
    float scrollSpeedBase = 0.3f;       // Base scroll speed (texture scrolls/sec)
    float scrollSpeedIntensity = 0.05f; // Additional speed per intensity level

    // Layer configuration (multiple layers for parallax depth)
    int numLayers = 1;
    float layerDepthMin = 2.0f;        // Closest layer distance from camera
    float layerDepthMax = 8.0f;        // Farthest layer distance
    float layerScaleMin = 0.5f;        // UV scale for closest layer (larger = fewer streaks)
    float layerScaleMax = 2.0f;        // UV scale for farthest layer
    float layerSpeedMin = 0.5f;        // Scroll speed multiplier for farthest layer
    float layerSpeedMax = 1.0f;        // Scroll speed multiplier for closest layer

    // Opacity
    float baseOpacity = 0.3f;          // Base opacity at intensity 1
    float maxOpacity = 0.8f;           // Max opacity at intensity 10

    // Wind tilt (screen-space angle)
    float windTiltBase = 0.0f;         // Base angle in radians (0 = vertical)
    float windTiltIntensity = 0.3f;    // Additional tilt per intensity level (radians)

    // Fog reduction (original EQ behavior)
    bool fogReductionEnabled = true;
    float fogStartMin = 10.0f;         // Fog start at intensity 10
    float fogStartMax = 200.0f;        // Fog start at intensity 1
    float fogEndMin = 50.0f;           // Fog end at intensity 10
    float fogEndMax = 500.0f;          // Fog end at intensity 1

    // Daylight reduction (darker during rain)
    bool daylightReductionEnabled = true;
    float daylightMin = 0.05f;         // Daylight multiplier at intensity 10 (5%)
    float daylightMax = 0.50f;         // Daylight multiplier at intensity 1 (50%)
};

/**
 * RainOverlay - Screen-space rain effect like original EQ client.
 *
 * Renders rain as camera-attached billboard quads with scrolling textures,
 * rather than expensive world-space particles. Multiple layers provide
 * parallax depth. Combined with fog reduction at high intensity for
 * the "can't see in heavy rain" effect.
 */
class RainOverlay {
public:
    RainOverlay();
    ~RainOverlay();

    // Non-copyable
    RainOverlay(const RainOverlay&) = delete;
    RainOverlay& operator=(const RainOverlay&) = delete;

    /**
     * Initialize the rain overlay system.
     * @param driver Irrlicht video driver
     * @param smgr Scene manager
     * @param eqClientPath Path to EQ client for textures
     * @return true if initialized successfully
     */
    bool initialize(irr::video::IVideoDriver* driver,
                    irr::scene::ISceneManager* smgr,
                    const std::string& eqClientPath = "");

    /**
     * Set rain intensity (0-10).
     * 0 = off, 1-10 = increasing intensity
     */
    void setIntensity(uint8_t intensity);
    uint8_t getIntensity() const { return intensity_; }

    /**
     * Update animation (UV scrolling).
     * @param deltaTime Time since last update in seconds
     * @param cameraPos Camera position in world space
     * @param cameraDir Camera forward direction
     */
    void update(float deltaTime, const glm::vec3& cameraPos, const glm::vec3& cameraDir);

    /**
     * Render the rain overlay.
     * Should be called after scene render but before UI.
     */
    void render();

    /**
     * Check if rain overlay is active.
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
     * Get daylight multiplier based on intensity.
     * @param outMultiplier Output daylight multiplier (0.0-1.0)
     * @return true if daylight should be reduced
     */
    bool getDaylightMultiplier(float& outMultiplier) const;

    /**
     * Set configuration.
     */
    void setSettings(const RainOverlaySettings& settings) { settings_ = settings; }
    const RainOverlaySettings& getSettings() const { return settings_; }

    /**
     * Enable/disable the overlay.
     */
    void setEnabled(bool enabled) { settings_.enabled = enabled; }
    bool isEnabled() const { return settings_.enabled; }

private:
    /**
     * Create or update the rain texture.
     */
    bool createRainTexture();

    /**
     * Render a single rain layer.
     */
    void renderLayer(int layer, float depth, float uvScale, float opacity);

    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;

    RainOverlaySettings settings_;
    uint8_t intensity_ = 0;
    bool initialized_ = false;

    // Intensity-based foreground textures (10 textures, one per intensity level)
    // Long streaks, density increases with intensity
    std::vector<irr::video::ITexture*> intensityTextures_;

    // Background layer textures (mid and far)
    irr::video::ITexture* midLayerTexture_ = nullptr;   // Medium streaks
    irr::video::ITexture* farLayerTexture_ = nullptr;   // Short streaks

    // UV scroll offset (animated)
    float scrollOffset_ = 0.0f;

    // Current camera state
    glm::vec3 cameraPos_{0, 0, 0};
    glm::vec3 cameraDir_{1, 0, 0};
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
