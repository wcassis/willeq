#ifndef EQT_GRAPHICS_SKY_LOADER_H
#define EQT_GRAPHICS_SKY_LOADER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace EQT {
namespace Graphics {

// Forward declarations
struct TextureInfo;
struct ZoneGeometry;

// Sky layer types
enum class SkyLayerType {
    Background,     // Main sky texture (e.g., normalsky, desertsky)
    Cloud,          // Cloud layer (e.g., normalcloud, fluffycloud)
    CelestialBody,  // Sun, moon, saturn, etc.
    Bottom          // Bottom sky (botsky1, botsky2)
};

// Single sky layer mesh with texture
struct SkyLayer {
    std::string name;                           // e.g., "LAYER11", "LAYER13"
    SkyLayerType type = SkyLayerType::Background;
    std::shared_ptr<ZoneGeometry> geometry;     // Mesh geometry
    std::string textureName;                    // Primary texture name
    int layerNumber = 0;                        // Layer number (11, 13, 21, etc.)
    bool isCloud = false;                       // True if cloud layer (for scrolling)
};

// Keyframe for celestial body animation track
struct SkyTrackKeyframe {
    glm::quat rotation;     // Rotation quaternion
    glm::vec3 translation;  // Position offset
    float scale = 1.0f;     // Scale factor
};

// Animation track for celestial bodies (sun/moon orbit)
struct SkyTrack {
    std::string name;                           // e.g., "SUN_TRACK", "MOON_TRACK"
    std::vector<SkyTrackKeyframe> keyframes;    // Animation keyframes
    int frameDelayMs = 0;                       // Milliseconds per frame
};

// Celestial body (sun, moon, saturn, etc.)
struct CelestialBody {
    std::string name;                           // e.g., "SUN", "MOON", "MOON32"
    std::shared_ptr<ZoneGeometry> geometry;     // Billboard mesh
    std::string textureName;                    // Texture name (sun.bmp, moon.bmp)
    std::shared_ptr<SkyTrack> track;            // Orbital animation track (may be null)
    bool isSun = false;                         // True for sun
    bool isMoon = false;                        // True for any moon variant
};

// Sky type definition (groups layers and celestial bodies)
// Layer numbering convention:
//   - Layer X1: Sky background (LAYER11, LAYER21, etc.)
//   - Layer X2: Celestial bodies layer (LAYER12, LAYER32, etc.)
//   - Layer X3: Cloud layer (LAYER13, LAYER23, etc.)
// Where X is the sky type: 1=normal, 2=desert, 3=air, 4=cottony, 5=red, 6=luclin, etc.
struct SkyType {
    std::string name;                           // e.g., "DEFAULT", "LUCLIN", "POFIRE"
    int typeId = 0;                             // Numeric type ID
    std::vector<int> backgroundLayers;          // Layer numbers for backgrounds
    std::vector<int> cloudLayers;               // Layer numbers for clouds
    std::vector<std::string> celestialBodies;   // Names of celestial bodies to use
};

// Complete sky data loaded from sky.s3d
struct SkyData {
    // All sky layers keyed by layer number (11, 13, 21, 23, etc.)
    std::map<int, std::shared_ptr<SkyLayer>> layers;

    // All celestial bodies keyed by name (SUN, MOON, MOON32, etc.)
    std::map<std::string, std::shared_ptr<CelestialBody>> celestialBodies;

    // All animation tracks keyed by name
    std::map<std::string, std::shared_ptr<SkyTrack>> tracks;

    // All textures loaded from sky.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;

    // Pre-defined sky types
    std::map<int, SkyType> skyTypes;
};

// Sky loader class - loads sky.s3d and parses sky.wld
class SkyLoader {
public:
    SkyLoader() = default;
    ~SkyLoader() = default;

    // Load sky data from EQ client path
    // eqClientPath: path to EQ client directory containing sky.s3d
    // Returns true on success
    bool load(const std::string& eqClientPath);

    // Get loaded sky data
    const std::shared_ptr<SkyData>& getSkyData() const { return skyData_; }

    // Get a specific layer by number
    std::shared_ptr<SkyLayer> getLayer(int layerNumber) const;

    // Get a celestial body by name
    std::shared_ptr<CelestialBody> getCelestialBody(const std::string& name) const;

    // Get texture by name
    std::shared_ptr<TextureInfo> getTexture(const std::string& name) const;

    // Get all layers for a sky type
    std::vector<std::shared_ptr<SkyLayer>> getLayersForSkyType(int skyTypeId) const;

    // Get celestial bodies for a sky type
    std::vector<std::shared_ptr<CelestialBody>> getCelestialBodiesForSkyType(int skyTypeId) const;

    // Check if loaded successfully
    bool isLoaded() const { return skyData_ != nullptr; }

    // Get error message if loading failed
    const std::string& getError() const { return error_; }

    // Get layer count
    size_t getLayerCount() const { return skyData_ ? skyData_->layers.size() : 0; }

    // Get celestial body count
    size_t getCelestialBodyCount() const { return skyData_ ? skyData_->celestialBodies.size() : 0; }

    // Get texture count
    size_t getTextureCount() const { return skyData_ ? skyData_->textures.size() : 0; }

private:
    // Load textures from sky.s3d archive
    bool loadTextures(const std::string& archivePath);

    // Parse sky.wld for geometry and animation data
    bool parseWld(const std::string& archivePath);

    // Parse layer meshes from WLD
    void parseLayers();

    // Parse celestial body meshes
    void parseCelestialBodies();

    // Parse animation tracks for celestial bodies
    void parseTracks();

    // Build pre-defined sky type configurations
    void buildSkyTypes();

    // Extract layer number from name (e.g., "LAYER11_DMSPRITEDEF" -> 11)
    static int extractLayerNumber(const std::string& name);

    // Determine layer type from name
    static SkyLayerType determineLayerType(const std::string& name, int layerNumber);

    // Check if a name represents a celestial body
    static bool isCelestialBodyName(const std::string& name);

    std::shared_ptr<SkyData> skyData_;
    std::string error_;

    // Temporary storage during parsing
    std::vector<char> wldBuffer_;
    std::map<std::string, std::shared_ptr<ZoneGeometry>> geometries_;
    std::map<std::string, std::string> materialTextures_;  // Material name -> texture name
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SKY_LOADER_H
