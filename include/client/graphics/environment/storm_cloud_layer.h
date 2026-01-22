#pragma once

#include <glm/glm.hpp>
#include <string>

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

    // Opacity
    float maxOpacity = 0.7f;            // Maximum cloud opacity
    float fadeInSpeed = 0.5f;           // Fade in rate (units/sec)
    float fadeOutSpeed = 0.3f;          // Fade out rate (units/sec)
    int intensityThreshold = 3;         // Min storm intensity to show clouds

    // Texture
    int textureSize = 256;              // Procedural texture size
    float cloudScale = 4.0f;            // UV tiling scale
    int octaves = 4;                    // Perlin noise octaves
    float persistence = 0.5f;           // Perlin noise persistence

    // Colors
    float cloudColorR = 0.4f;
    float cloudColorG = 0.42f;
    float cloudColorB = 0.45f;
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
     * @return true if initialized successfully
     */
    bool initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);

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
     */
    void update(float deltaTime, const glm::vec3& playerPos,
                const glm::vec3& windDirection, float windStrength,
                int stormIntensity);

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
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    /**
     * Create the hemisphere dome mesh.
     */
    void createDomeMesh();

    /**
     * Generate procedural cloud texture using Perlin noise.
     */
    void generateCloudTexture();

    /**
     * Perlin noise helper functions.
     */
    float noise2D(float x, float y) const;
    float smoothNoise2D(float x, float y) const;
    float interpolatedNoise2D(float x, float y) const;
    float perlinNoise2D(float x, float y, int octaves, float persistence) const;

    /**
     * Update UV offset for scrolling animation.
     */
    void updateScrolling(float deltaTime, const glm::vec3& windDirection, float windStrength);

    /**
     * Update opacity based on storm intensity.
     */
    void updateOpacity(float deltaTime, int stormIntensity);

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
    irr::video::ITexture* cloudTexture_ = nullptr;

    // Animation state
    glm::vec2 uvOffset_{0.0f, 0.0f};
    float currentOpacity_ = 0.0f;
    float targetOpacity_ = 0.0f;

    // Zone state
    bool isIndoorZone_ = false;

    // Noise seed (for reproducible procedural texture)
    int noiseSeed_ = 12345;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
