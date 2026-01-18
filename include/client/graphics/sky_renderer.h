#ifndef EQT_GRAPHICS_SKY_RENDERER_H
#define EQT_GRAPHICS_SKY_RENDERER_H

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <irrlicht.h>

namespace EQT {
namespace Graphics {

// Forward declarations
class SkyLoader;
class SkyConfig;
class ZoneGeometry;
struct SkyLayer;
struct CelestialBody;
struct TextureInfo;
struct SkyTrack;
struct SkyTrackKeyframe;

// Special sky type categories
enum class SkyCategory {
    Normal,      // Classic Norrath sky with day/night cycle
    Luclin,      // Luclin sky with earthrise and different moons
    PoFire,      // Plane of Fire - red/orange, no day/night
    PoStorms,    // Plane of Storms - dark/grey, lightning
    PoAir,       // Plane of Air - light blue, airy
    PoWar,       // Plane of War - dark red
    TheGrey,     // The Grey / Nightmare - uniform grey, no celestials
    PoTranq,     // Plane of Tranquility - soft colors
    Indoor       // Indoor/NULL - no sky
};

// Sky color set for a specific time of day
struct SkyColorSet {
    irr::video::SColor zenith;     // Top of sky dome color
    irr::video::SColor horizon;    // Horizon color (blends with zenith)
    irr::video::SColor fog;        // Recommended fog color
    float sunIntensity;            // Sun brightness (0.0 - 1.0)
    float cloudBrightness;         // Cloud layer brightness (0.0 - 1.0)
};

// Sky renderer - renders sky dome, clouds, and celestial bodies
class SkyRenderer {
public:
    SkyRenderer(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                irr::io::IFileSystem* fileSystem);
    ~SkyRenderer();

    // Initialize sky renderer with EQ client path
    // Loads sky.s3d and sky.ini
    bool initialize(const std::string& eqClientPath);

    // Set sky type for current zone
    // skyTypeId: sky type from NewZone_Struct::sky
    // zoneName: current zone name for sky.ini lookup
    void setSkyType(uint8_t skyTypeId, const std::string& zoneName);

    // Update time of day for celestial body positioning
    void updateTimeOfDay(uint8_t hour, uint8_t minute);

    // Update sky animation (cloud scrolling, etc.)
    // deltaTime: time since last update in seconds
    void update(float deltaTime);

    // Update camera position - sky dome and celestial bodies follow the camera
    // so they appear infinitely far away regardless of player position
    void setCameraPosition(const irr::core::vector3df& cameraPos);

    // Enable/disable sky rendering
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // Get current sky type ID
    uint8_t getCurrentSkyType() const { return currentSkyType_; }

    // Check if sky was successfully initialized
    bool isInitialized() const { return initialized_; }

    // Get current sky colors for time of day (for external fog/lighting use)
    SkyColorSet getCurrentSkyColors() const;

    // Get recommended fog color based on current time of day
    irr::video::SColor getRecommendedFogColor() const;

private:
    // Calculate sky colors for given time of day (decimal hours 0-24)
    SkyColorSet calculateSkyColors(float timeOfDay) const;

    // Interpolate between two color sets based on factor (0.0 - 1.0)
    SkyColorSet interpolateSkyColors(const SkyColorSet& a, const SkyColorSet& b, float t) const;

    // Update sky layer colors based on time of day
    void updateSkyLayerColors();

    // Update sun glow color based on time of day
    void updateSunGlowColor();

    // Determine sky category from sky type ID
    SkyCategory determineSkyCategory(uint8_t skyTypeId) const;

    // Get special colors for non-normal sky types (Planes, etc.)
    // Returns colors that don't change with time of day
    SkyColorSet getSpecialSkyColors(SkyCategory category) const;

    // Check if sky category has day/night cycle
    bool hasDayNightCycle(SkyCategory category) const;

    // Apply cloud UV scrolling animation
    void updateCloudScrolling();

    // Create sky dome mesh from layer geometry
    void createSkyDome();

    // Create celestial body billboards (sun, moon)
    void createCelestialBodies();

    // Load texture from TextureInfo into Irrlicht
    irr::video::ITexture* loadSkyTexture(const std::string& name);

    // Update celestial body positions based on time
    void updateCelestialPositions();

    // Calculate sun position for given hour (0-24)
    irr::core::vector3df calculateSunPosition(float hour) const;

    // Calculate moon position for given hour (0-24)
    irr::core::vector3df calculateMoonPosition(float hour) const;

    // Calculate position from track keyframes (if available)
    // Returns position in Irrlicht coordinates
    irr::core::vector3df calculateTrackPosition(const std::shared_ptr<SkyTrack>& track, float hour) const;

    // Calculate celestial body size based on elevation (larger near horizon)
    float calculateCelestialSize(float baseSize, float elevation) const;

    // Update celestial body sizes based on current positions
    void updateCelestialSizes();

    // Update sky dome visibility based on current sky type
    void updateSkyVisibility();

    // Clear all sky scene nodes
    void clearSkyNodes();

    // Create Irrlicht mesh from ZoneGeometry
    irr::scene::SMesh* createMeshFromGeometry(const ZoneGeometry* geometry);

    // Scene manager, driver, and file system
    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;

    // Sky data loaders
    std::unique_ptr<SkyLoader> skyLoader_;
    std::unique_ptr<SkyConfig> skyConfig_;

    // Sky dome scene nodes (one per layer) - legacy, kept for cloud layers
    std::vector<irr::scene::IMeshSceneNode*> skyDomeNodes_;

    // Irrlicht built-in sky dome (handles render order automatically)
    irr::scene::ISceneNode* irrlichtSkyDome_ = nullptr;

    // Celestial body scene nodes
    irr::scene::IBillboardSceneNode* sunNode_ = nullptr;
    irr::scene::IBillboardSceneNode* moonNode_ = nullptr;

    // Sun glow billboard (additive blending for glow effect)
    irr::scene::IBillboardSceneNode* sunGlowNode_ = nullptr;

    // Track data for celestial body animation (from sky.wld)
    std::shared_ptr<SkyTrack> sunTrack_;
    std::shared_ptr<SkyTrack> moonTrack_;

    // Texture cache
    std::map<std::string, irr::video::ITexture*> textureCache_;

    // Current state
    uint8_t currentSkyType_ = 0;
    SkyCategory currentSkyCategory_ = SkyCategory::Normal;
    uint8_t currentHour_ = 12;
    uint8_t currentMinute_ = 0;
    float cloudScrollOffset_ = 0.0f;
    bool enabled_ = true;
    bool initialized_ = false;

    // Camera position for sky following
    irr::core::vector3df lastCameraPos_{0, 0, 0};

    // Cached sky colors for current time of day
    SkyColorSet currentSkyColors_;

    // Cloud layer nodes for UV scrolling (subset of skyDomeNodes_)
    std::vector<irr::scene::IMeshSceneNode*> cloudLayerNodes_;

    // Sky dome radius (must be within camera far clip plane, which is 2000 by default)
    static constexpr float SKY_DOME_RADIUS = 1800.0f;

    // Celestial body distance from camera (slightly less than sky dome)
    static constexpr float CELESTIAL_DISTANCE = 1700.0f;

    // Celestial body billboard base sizes (smaller = more realistic)
    // At 1700 units distance: 30 units = ~1 degree visual angle
    static constexpr float SUN_BASE_SIZE = 30.0f;
    static constexpr float MOON_BASE_SIZE = 25.0f;

    // Size scaling range (min/max multipliers for horizon effect)
    static constexpr float SIZE_SCALE_MIN = 1.0f;
    static constexpr float SIZE_SCALE_MAX = 1.5f;  // 50% larger at horizon

    // Glow size relative to sun
    static constexpr float GLOW_SIZE_MULTIPLIER = 2.0f;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SKY_RENDERER_H
