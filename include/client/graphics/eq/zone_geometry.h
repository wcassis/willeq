#ifndef EQT_GRAPHICS_ZONE_GEOMETRY_H
#define EQT_GRAPHICS_ZONE_GEOMETRY_H

#include "wld_loader.h"
#include <irrlicht.h>
#include <memory>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Forward declarations
struct S3DZone;
struct TextureInfo;
class ConstrainedTextureCache;

// Converts EQ zone geometry to Irrlicht mesh
class ZoneMeshBuilder {
public:
    ZoneMeshBuilder(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    irr::io::IFileSystem* fileSystem);
    ~ZoneMeshBuilder() = default;

    // Build an Irrlicht mesh from zone geometry
    irr::scene::IMesh* buildMesh(const ZoneGeometry& geometry);

    // Build mesh with vertex coloring based on height/normals
    irr::scene::IMesh* buildColoredMesh(const ZoneGeometry& geometry);

    // Build textured mesh from zone with texture data
    // Set flipV=true for character models (they need V coordinate flipped)
    irr::scene::IMesh* buildTexturedMesh(const ZoneGeometry& geometry,
                                          const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                          bool flipV = false);

    // Load a texture from raw BMP/DDS data
    irr::video::ITexture* loadTextureFromBMP(const std::string& name, const std::vector<char>& data);

    // Performance: Lazy texture loading
    // Register texture data for deferred loading (doesn't create Irrlicht texture yet)
    void registerLazyTexture(const std::string& name, std::shared_ptr<TextureInfo> texInfo);

    // Get texture, loading lazily if needed
    irr::video::ITexture* getOrLoadTexture(const std::string& name);

    // Check if a texture is registered (either loaded or pending)
    bool hasTexture(const std::string& name) const;

    // Constrained rendering support
    // Set optional constrained texture cache for memory-limited rendering
    // When set, textures are loaded through the cache with downsampling and 16-bit conversion
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // Get the constrained texture cache (may be nullptr)
    ConstrainedTextureCache* getConstrainedTextureCache() const { return constrainedCache_; }

private:
    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;

    // Cache of loaded textures
    std::map<std::string, irr::video::ITexture*> textureCache_;

    // Performance: Pending textures for lazy loading (registered but not yet loaded)
    std::map<std::string, std::shared_ptr<TextureInfo>> pendingTextures_;

    // Track which textures have alpha transparency
    std::set<std::string> texturesWithAlpha_;

    // Optional constrained texture cache for memory-limited rendering
    ConstrainedTextureCache* constrainedCache_ = nullptr;
};

// Helper to generate colors for visualization
irr::video::SColor heightToColor(float height, float minHeight, float maxHeight);
irr::video::SColor normalToColor(float nx, float ny, float nz);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_GEOMETRY_H
