#ifndef EQT_GRAPHICS_ZONE_GEOMETRY_H
#define EQT_GRAPHICS_ZONE_GEOMETRY_H

#include "wld_loader.h"
#include <irrlicht.h>
#include <memory>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Forward declaration
struct S3DZone;
struct TextureInfo;

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

private:
    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;

    // Cache of loaded textures
    std::map<std::string, irr::video::ITexture*> textureCache_;

    // Track which textures have alpha transparency
    std::set<std::string> texturesWithAlpha_;
};

// Helper to generate colors for visualization
irr::video::SColor heightToColor(float height, float minHeight, float maxHeight);
irr::video::SColor normalToColor(float nx, float ny, float nz);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_GEOMETRY_H
