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
#ifdef EQT_HAS_TEXTURE_ATLAS
class TextureAtlas;
#endif

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

    // Clear texture cache (forces fresh texture lookups on next build)
    void clearTextureCache();

    // Constrained rendering support
    // Set optional constrained texture cache for memory-limited rendering
    // When set, textures are loaded through the cache with downsampling and 16-bit conversion
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // Get the constrained texture cache (may be nullptr)
    ConstrainedTextureCache* getConstrainedTextureCache() const { return constrainedCache_; }

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Build mesh batched by atlas page — groups all triangles sharing the same
    // atlas page into a single mesh buffer using S3DVertex2TCoords.
    // TCoords = original UVs (for fract() tiling), TCoords2 = atlas tile offset.
    // Textures not in the atlas (animated, missing) fall through to per-texture buffers.
    irr::scene::IMesh* buildAtlasedMesh(
        const ZoneGeometry& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        const TextureAtlas& atlas,
        int pageIndexOffset = 0);
#endif

    // Set GLSL shader material type IDs (negative = not available, use fixed-function)
    void setShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType) {
        shaderMaterialSolid_ = solidType;
        shaderMaterialAlphaTest_ = alphaTestType;
    }

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Set atlas-specific GLSL shader material type IDs
    void setAtlasShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType) {
        shaderMaterialAtlasSolid_ = solidType;
        shaderMaterialAtlasAlpha_ = alphaTestType;
    }
#endif

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

    // GLSL shader material type IDs (-1 = not available)
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Atlas-specific shader material type IDs (-1 = not available)
    irr::s32 shaderMaterialAtlasSolid_ = -1;
    irr::s32 shaderMaterialAtlasAlpha_ = -1;
#endif
};

// Helper to generate colors for visualization
irr::video::SColor heightToColor(float height, float minHeight, float maxHeight);
irr::video::SColor normalToColor(float nx, float ny, float nz);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_GEOMETRY_H
