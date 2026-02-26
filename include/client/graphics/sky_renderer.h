#ifndef EQT_GRAPHICS_SKY_RENDERER_H
#define EQT_GRAPHICS_SKY_RENDERER_H

#include <string>
#include <memory>
#include <vector>
#include <set>
#include <map>
#include <irrlicht.h>
#include "client/graphics/sky_config.h"

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

    // Initialize from pre-loaded data (background thread loaded sky.s3d + sky.ini)
    bool initializeFromPreloaded(std::unique_ptr<SkyLoader> loader,
                                 std::unique_ptr<SkyConfig> config);

    // Set sky type for current zone (convenience: calls prepareSkyType + applySkyType)
    // skyTypeId: sky type from NewZone_Struct::sky
    // zoneName: current zone name for sky.ini lookup
    void setSkyType(uint8_t skyTypeId, const std::string& zoneName);

    // Prepare sky config/state for a zone (no scene nodes created, no GL calls).
    // Call applySkyType() separately to create the actual scene nodes.
    void prepareSkyType(uint8_t skyTypeId, const std::string& zoneName);

    // Create sky scene nodes using state from prepareSkyType().
    // Must be called after prepareSkyType(). Requires GL context.
    void applySkyType();

    // --- Progressive sub-step methods (called from progressive pipeline) ---

    // Create dome mesh node from pre-computed vertex/index data (no trig, just memcpy + GL upload).
    // Requires prepareSkyType() to have been called first.
    void createSkyDomeFromPrecomputed(const std::vector<irr::video::S3DVertex>& vertices,
                                       const std::vector<irr::u16>& indices);

    // Create sun/moon billboard scene nodes (cache-hit texture lookups).
    void createCelestialBodiesOnly();

    // Calculate and apply initial sky colors + vertex alpha + celestial positions.
    void applyInitialColors();

    // Pre-compute dome mesh geometry on background thread (pure CPU math, no GL).
    // Returns vertex/index arrays for later GPU upload via createSkyDomeFromPrecomputed().
    static void precomputeDomeMesh(std::vector<irr::video::S3DVertex>& outVertices,
                                    std::vector<irr::u16>& outIndices);

    // Upload a pre-decoded A8R8G8B8 pixel buffer as a GPU texture.
    // Stores in textureCache_ for subsequent loadSkyTexture() cache hits.
    irr::video::ITexture* uploadPreDecodedTexture(const std::string& name,
                                                   const uint8_t* argbPixels,
                                                   uint32_t width, uint32_t height);

#ifdef EQT_HAS_GLES2
    // Strip upload for sky textures (GLES2 only — splits large texture uploads across frames)
    // Returns true if texture already cached (nothing to do).
    // Returns false if strip upload was started (call continueStripUpload on subsequent frames).
    bool beginStripUpload(const std::string& name, const uint8_t* argbPixels, uint32_t w, uint32_t h);

    // Upload next strip. Returns true when all strips are done.
    bool continueStripUpload();

    // Wrap completed GL texture as ITexture and cache it.
    void finalizeStripUpload();

    // Check if a strip upload is in progress.
    bool isStripActive() const;
#endif

    // Update time of day for celestial body positioning
    void updateTimeOfDay(uint8_t hour, uint8_t minute);

    // Set weather brightness modifier (1.0 = normal, 0.0 = completely dark)
    // Used to darken sky during rain/storms
    void setWeatherBrightness(float brightness);

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

    // Check if prepareSkyType() has been called (pending applySkyType)
    bool isSkyPrepared() const { return skyPrepared_; }

    // Clear the skyPrepared_ flag after progressive pipeline has consumed it
    void consumeSkyPrepared() { skyPrepared_ = false; }

    // Clear sky scene nodes for rebuild (used by progressive pipeline before creating new dome)
    void clearSkyForRebuild() { clearSkyNodes(); }

    // Get number of cached sky textures (for memory reporting)
    size_t getTextureCount() const { return textureCache_.size(); }

    // Get count of active scene nodes managed by sky renderer (for dumpScene accounting)
    int getSceneNodeCount() const {
        int count = 0;
        if (skyDomeMeshNode_) count++;
        if (sunNode_) count++;
        if (moonNode_) count++;
        if (sunGlowNode_) count++;
        count += static_cast<int>(cloudLayerNodes_.size());
        return count;
    }

    // Collect all scene node pointers into a set (for dumpScene node identification)
    void collectSceneNodes(std::set<irr::scene::ISceneNode*>& nodes) const {
        if (skyDomeMeshNode_) nodes.insert(skyDomeMeshNode_);
        if (sunNode_) nodes.insert(sunNode_);
        if (moonNode_) nodes.insert(moonNode_);
        if (sunGlowNode_) nodes.insert(sunGlowNode_);
        for (auto* n : cloudLayerNodes_) { if (n) nodes.insert(n); }
    }

    // Get current sky colors for time of day (for external fog/lighting use)
    SkyColorSet getCurrentSkyColors() const;

    // Get recommended fog color based on current time of day
    irr::video::SColor getRecommendedFogColor() const;

    // Get current background clear color for day/night cycle
    irr::video::SColor getCurrentClearColor() const { return currentClearColor_; }

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

    // Build custom hemisphere dome mesh with per-vertex alpha
    void createCustomSkyDome(irr::video::ITexture* texture);

    // Recalculate vertex alpha from zone config + camera Z
    void updateDomeVertexAlpha();

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

    // Custom hemisphere dome mesh node and buffer for per-vertex alpha
    irr::scene::IMeshSceneNode* skyDomeMeshNode_ = nullptr;
    irr::scene::SMeshBuffer* skyDomeMeshBuffer_ = nullptr;

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
    bool skyPrepared_ = false;  // True after prepareSkyType(), cleared after applySkyType()

    // Camera position for sky following
    irr::core::vector3df lastCameraPos_{0, 0, 0};

    // Cached sky colors for current time of day
    SkyColorSet currentSkyColors_;

    // Weather brightness modifier (1.0 = normal, 0.0 = completely dark)
    float weatherBrightness_ = 1.0f;

    // Current background clear color for day/night cycle
    irr::video::SColor currentClearColor_{255, 50, 80, 120};

    // Cloud layer nodes for UV scrolling (subset of skyDomeNodes_)
    std::vector<irr::scene::IMeshSceneNode*> cloudLayerNodes_;

    // Current zone's sky.ini config (horizon fade, camera height params)
    ZoneSkyConfig currentZoneConfig_;

    // Last camera Z in EQ coords (for height-based interpolation)
    float lastCameraZ_ = 0.0f;

#ifdef EQT_HAS_GLES2
    // Strip upload state for progressive sky texture uploads
    static constexpr int SKY_STRIP_HEIGHT = 64;  // Rows per strip

    struct SkyStripUploadState {
        unsigned int glTexName = 0;
        std::string textureName;
        int currentStrip = 0;
        int totalStrips = 0;
        uint32_t texWidth = 0;
        uint32_t texHeight = 0;
        const uint8_t* argbPixels = nullptr;  // Borrowed from PreDecodedTexture::pixels
        std::vector<uint8_t> swizzleBuf;      // Reusable BGRA→RGBA conversion buffer
        bool active = false;
    };
    SkyStripUploadState stripState_;
#endif

    // Dome geometry parameters
    static constexpr int SKY_DOME_HORI_SEGMENTS = 32;
    static constexpr int SKY_DOME_VERT_RINGS = 20;
    static constexpr float SKY_DOME_BOTTOM_PITCH = -0.5f; // radians below horizon

    // Sky dome radius (must be within camera far clip plane, which is 2000 by default)
    static constexpr float SKY_DOME_RADIUS = 1800.0f;

    // Celestial body distance from camera (slightly less than sky dome)
    static constexpr float CELESTIAL_DISTANCE = 1700.0f;

    // Celestial body billboard base sizes
    // At 1700 units distance: 100 units = ~3.4 degree visual angle
    static constexpr float SUN_BASE_SIZE = 120.0f;
    static constexpr float MOON_BASE_SIZE = 100.0f;

    // Size scaling range (min/max multipliers for horizon effect)
    static constexpr float SIZE_SCALE_MIN = 1.0f;
    static constexpr float SIZE_SCALE_MAX = 1.5f;  // 50% larger at horizon

    // Glow size relative to sun
    static constexpr float GLOW_SIZE_MULTIPLIER = 2.0f;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SKY_RENDERER_H
