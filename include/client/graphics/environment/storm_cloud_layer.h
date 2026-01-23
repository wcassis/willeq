#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace irr {
namespace scene {
class ISceneManager;
class IMeshSceneNode;
class IMesh;
}
namespace video {
class IVideoDriver;
class ITexture;
class IImage;
}
}

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * StormCloudSettings - Configuration for storm cloud overlay.
 */
struct StormCloudSettings {
    bool enabled = true;

    // Appearance
    float domeRadius = 500.0f;          // Radius of cloud dome
    float domeHeight = 150.0f;          // Height above player
    int domeSegments = 24;              // Mesh tessellation

    // Animation
    float scrollSpeedBase = 0.02f;      // Base UV scroll speed
    float scrollSpeedVariance = 0.01f;  // Per-layer speed variation
    float windInfluence = 0.5f;         // How much wind affects scroll direction

    // Frame animation (multi-texture cycling)
    int frameCount = 4;                 // Number of cloud texture frames
    float frameDuration = 3.0f;         // Seconds per frame cycle
    float blendDuration = 1.5f;         // Seconds to blend between frames

    // Opacity
    float maxOpacity = 0.7f;            // Maximum cloud opacity
    float fadeInSpeed = 0.5f;           // Fade in rate (units/sec)
    float fadeOutSpeed = 0.3f;          // Fade out rate (units/sec)
    int intensityThreshold = 3;         // Min storm intensity to show clouds

    // Texture (only used if pre-built textures not found)
    int textureSize = 256;              // Procedural texture size
    float cloudScale = 1.0f;            // UV tiling scale (1.0 = no tiling, seamless)
    int octaves = 4;                    // Perlin noise octaves
    float persistence = 0.5f;           // Perlin noise persistence

    // Colors
    float cloudColorR = 0.4f;
    float cloudColorG = 0.42f;
    float cloudColorB = 0.45f;

    // Time-of-day brightness settings
    float dayBrightness = 0.5f;         // Cloud brightness during day (0-1)
    float nightBrightness = 0.05f;      // Cloud brightness at night (0-1)
    float dawnStartHour = 6.0f;         // Hour when dawn starts
    float dawnEndHour = 8.0f;           // Hour when dawn ends (full day)
    float duskStartHour = 18.0f;        // Hour when dusk starts
    float duskEndHour = 20.0f;          // Hour when dusk ends (full night)
    float lightningFlashMultiplier = 2.0f;  // How much lightning brightens clouds
};

/**
 * StormCloudLayer - Dynamic cloud overlay during storms.
 *
 * Creates an animated cloud dome that appears during rain storms,
 * with procedurally generated cloud texture and UV scrolling animation.
 *
 * Features:
 * - Hemisphere dome mesh that follows the player
 * - Procedural Perlin noise-based cloud texture
 * - UV scrolling synchronized with wind direction
 * - Opacity fades with storm intensity
 * - Multiple overlapping layers for depth
 */
class StormCloudLayer {
public:
    StormCloudLayer();
    ~StormCloudLayer();

    // Non-copyable
    StormCloudLayer(const StormCloudLayer&) = delete;
    StormCloudLayer& operator=(const StormCloudLayer&) = delete;

    /**
     * Initialize the cloud layer.
     * @param smgr Scene manager
     * @param driver Video driver
     * @param eqClientPath Path to EQ client (for loading textures)
     * @return true if initialized successfully
     */
    bool initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    const std::string& eqClientPath = "");

    /**
     * Clean up resources.
     */
    void shutdown();

    /**
     * Update cloud layer settings.
     */
    void setSettings(const StormCloudSettings& settings);
    const StormCloudSettings& getSettings() const { return settings_; }

    /**
     * Update the cloud layer.
     * @param deltaTime Time since last update
     * @param playerPos Current player position
     * @param windDirection Current wind direction (normalized)
     * @param windStrength Current wind strength (0-1)
     * @param stormIntensity Current storm intensity (0-10)
     * @param timeOfDay Hour of day (0-24) for lighting adjustment
     */
    void update(float deltaTime, const glm::vec3& playerPos,
                const glm::vec3& windDirection, float windStrength,
                int stormIntensity, float timeOfDay = 12.0f);

    /**
     * Enable/disable the cloud layer.
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_ && settings_.enabled; }

    /**
     * Check if clouds are currently visible.
     */
    bool isVisible() const { return currentOpacity_ > 0.01f; }

    /**
     * Get current opacity (for debug display).
     */
    float getCurrentOpacity() const { return currentOpacity_; }

    /**
     * Called when entering a new zone.
     */
    void onZoneEnter(const std::string& zoneName, bool isIndoor);

    /**
     * Called when leaving a zone.
     */
    void onZoneLeave();

    /**
     * Set lightning flash intensity (0-1) for brief illumination.
     * Called by WeatherEffectsController when lightning strikes.
     */
    void setLightningFlash(float intensity) { lightningFlashIntensity_ = intensity; }

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    /**
     * Create the hemisphere dome mesh.
     */
    void createDomeMesh();

    /**
     * Generate all cloud texture frames using Perlin noise.
     */
    void generateCloudTextures();

    /**
     * Generate a single seamless cloud texture frame.
     * @param seed Random seed for this frame
     * @return The generated texture
     */
    irr::video::ITexture* generateSeamlessCloudTexture(int seed);

    /**
     * Seamless tileable Perlin noise (uses toroidal mapping).
     */
    float seamlessPerlinNoise2D(float x, float y, int seed, int octaves, float persistence) const;

    /**
     * Basic noise function with seed.
     */
    float noise2D(float x, float y, int seed) const;

    /**
     * Smooth noise with interpolation.
     */
    float smoothNoise2D(float x, float y, int seed) const;

    /**
     * Interpolated noise for smooth gradients.
     */
    float interpolatedNoise2D(float x, float y, int seed) const;

    /**
     * Update UV offset for scrolling animation.
     */
    void updateScrolling(float deltaTime, const glm::vec3& windDirection, float windStrength);

    /**
     * Update opacity based on storm intensity.
     */
    void updateOpacity(float deltaTime, int stormIntensity);

    /**
     * Update frame animation and blending.
     */
    void updateFrameAnimation(float deltaTime);

    /**
     * Update the blended texture by combining current and next frames.
     */
    void updateBlendedTexture();

    /**
     * Update mesh node position and material.
     */
    void updateMeshNode(const glm::vec3& playerPos);

    // Settings
    StormCloudSettings settings_;
    bool enabled_ = true;
    bool initialized_ = false;

    // Irrlicht objects
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::IMesh* domeMesh_ = nullptr;
    irr::scene::IMeshSceneNode* domeNode_ = nullptr;

    // Multi-texture animation
    std::vector<irr::video::ITexture*> cloudFrames_;  // Source frames
    irr::video::ITexture* blendedTexture_ = nullptr;  // Current blended output
    irr::video::IImage* blendImage_ = nullptr;        // Working image for blending
    int currentFrame_ = 0;                            // Current frame index
    int nextFrame_ = 1;                               // Next frame to blend to
    float frameTimer_ = 0.0f;                         // Time in current frame
    float blendFactor_ = 0.0f;                        // 0-1 blend between frames

    // Animation state
    glm::vec2 uvOffset_{0.0f, 0.0f};
    float currentOpacity_ = 0.0f;
    float targetOpacity_ = 0.0f;

    // Lighting state
    float currentTimeOfDay_ = 12.0f;      // Current time for color adjustment
    float lightningFlashIntensity_ = 0.0f; // 0-1 lightning brightness

    // Zone state
    bool isIndoorZone_ = false;

    // Path for loading textures
    std::string eqClientPath_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
