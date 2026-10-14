#ifndef EQT_GRAPHICS_ZONE_GEOMETRY_H
#define EQT_GRAPHICS_ZONE_GEOMETRY_H

#include "wld_loader.h"
#include <irrlicht.h>
#include <cstddef>
#include <memory>
#include <map>
#include <set>

#include "client/graphics/texture_origin.h"

namespace EQT {
namespace Graphics {

// Forward declarations
struct S3DZone;
struct TextureInfo;
struct CompositeTextureData;
class ConstrainedTextureCache;

// Converts EQ zone geometry to Irrlicht mesh
class ZoneMeshBuilder {
public:
    ZoneMeshBuilder(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    irr::io::IFileSystem* fileSystem);
    ~ZoneMeshBuilder() = default;

    // B1019 (GitLab #340): the layout-equality net's CORE side, STATIC so a
    // consumer can call it without constructing a builder. Defined in
    // zone_geometry.cpp, the TU willeq_core compiles -- the sizeof there
    // carries the core's own macro state, the very thing a consumer that
    // links willeq_core must match. tests/test_zone_geometry_atlas_layout
    // .cpp compares its own sizeof against this. (No target links
    // willeq_core and compiles zone_geometry.cpp itself, so the definition
    // cannot collide with a local copy.)
    static size_t layoutSize();

    // Build an Irrlicht mesh from zone geometry
    irr::scene::IMesh* buildMesh(const ZoneGeometry& geometry);

    // Build mesh with vertex coloring based on height/normals
    irr::scene::IMesh* buildColoredMesh(const ZoneGeometry& geometry);

    // Build textured mesh from zone with texture data
    // Set flipV=true for character models (they need V coordinate flipped)
    irr::scene::IMesh* buildTexturedMesh(const ZoneGeometry& geometry,
                                          const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                          bool flipV = false);

    // Load a texture from raw BMP/DDS data.
    // P50: applyPaletteMask, default false, forwards to
    // ConstrainedTextureCache::getOrLoad()/uploadTexture() — pass true ONLY
    // when the caller has already confirmed (via the WLD-derived
    // textureMasked() flag) that this specific texture is WLD-masked. Same
    // contract as ConstrainedTextureCache::getOrLoad() (see P46 there);
    // default false preserves pre-P50 behavior byte-for-byte.
    // B947: origin (the TextureInfo's, when there is one) is handed to the
    // cache so an evicted texture restores from the same bytes.
    irr::video::ITexture* loadTextureFromBMP(const std::string& name, const std::vector<char>& data,
                                              bool applyPaletteMask = false,
                                              const TextureOrigin* origin = nullptr);

    // Get texture, loading lazily if needed.
    // P50: applyPaletteMask is forwarded to loadTextureFromBMP() ONLY for the
    // lazy-load-now path (pendingTextures_); an already-loaded/-cached
    // texture (textureCache_ hit) is returned as-is regardless of this flag
    // since its mask status was already resolved when it was first loaded.
    irr::video::ITexture* getOrLoadTexture(const std::string& name, bool applyPaletteMask = false);

    // Check if a texture is registered (either loaded or pending)
    bool hasTexture(const std::string& name) const;

    // Register a pre-uploaded texture in the cache (for multi-frame entity pipeline)
    void registerUploadedTexture(const std::string& name, irr::video::ITexture* texture, bool hasAlpha);

    // Clear texture cache (forces fresh texture lookups on next build)
    void clearTextureCache();

    // B944: drop every name mapped to `texture`, which ConstrainedTextureCache
    // is about to destroy -- this map is raw pointers registered by the cache
    // itself (registerUploadedTexture), so the cache scrubs it the same way.
    void forgetTexture(const irr::video::ITexture* texture);

    // Constrained rendering support
    // Set optional constrained texture cache for memory-limited rendering
    // When set, textures are loaded through the cache with downsampling and 16-bit conversion
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // P13: tag this builder instance with an upload-ledger category (e.g.
    // "zone_region", "object") — a plain string, not threaded through every
    // call, since each ZoneMeshBuilder instance is already scoped to one
    // source (regions vs objects use separate instances in
    // renderer_zone_load.cpp). Passed through to getOrLoad() by
    // loadTextureFromBMP(). No-op when the ledger is disabled.
    void setUploadCategory(const std::string& category) { uploadCategory_ = category; }

    // Set GLSL shader material type IDs (negative = not available, use fixed-function).
    // alphaTestRGBAType (H02d, Option A) is the variant that reads alpha from
    // the main texture's own alpha channel (uTexture.a) — used for masked
    // textures on the direct RGBA/DDS decode path, which has no ETC1 alpha
    // companion texture. alphaTestType remains the ETC1-companion variant
    // (uAlphaTexture, unit 1), used only for ETC1-sourced textures. Defaults
    // to -1 (not wired) for callers that don't build zone geometry directly;
    // in that case masked RGBA textures safely fall back to the fixed-function
    // EMT_TRANSPARENT_ALPHA_CHANNEL_REF material, which already reads the main
    // texture's own alpha and was never affected by the companion-unit bug.
    void setShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType,
                                 irr::s32 alphaTestRGBAType = -1) {
        shaderMaterialSolid_ = solidType;
        shaderMaterialAlphaTest_ = alphaTestType;
        shaderMaterialAlphaTestRGBA_ = alphaTestRGBAType;
    }

    // Textures that were null during the last build (async GPU upload not yet complete).
    // Caller can check after buildTexturedMesh to queue rebuilds.
    const std::vector<std::string>& getMissingTextures() const { return missingTextures_; }

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
    std::string uploadCategory_ = "uncategorized";  // P13

    // Textures that resolved to null during mesh build (async upload pending)
    std::vector<std::string> missingTextures_;

    // GLSL shader material type IDs (-1 = not available)
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;         // ETC1 companion-texture variant
    irr::s32 shaderMaterialAlphaTestRGBA_ = -1;      // H02d: main-texture-alpha variant
};

// Helper to generate colors for visualization
irr::video::SColor heightToColor(float height, float minHeight, float maxHeight);

// Estimate a built region mesh's GPU-adjacent memory footprint (vertex +
// index buffer bytes) from its Irrlicht scene node. Relocated verbatim
// (T67, 2026-08-11) from the now-removed ConstrainedMeshCache::
// estimateMeshSize() -- the calculation itself never changed, only its
// home. See src/client/graphics/CLAUDE.md's "Constrained Memory
// Management" section for the removed cache's history/reference pointer.
size_t estimateMeshSizeBytes(irr::scene::IMeshSceneNode* node);

// B904: finish one built mesh buffer -- reallocate its vertex and index
// arrays to exactly their element counts, then recompute its bounding box.
// Every ZoneMeshBuilder path grows these arrays by push_back, and
// irr::core::array's default strategy roughly doubles a small array (6, 13,
// 27, 55, ...), so a finished buffer held up to twice its data for the life
// of the mesh (timorous: 15,674 region buffers). Contents are unchanged.
// Returns the bytes of capacity released (element slack, not chunk bytes).
size_t finishMeshBuffer(irr::scene::SMeshBuffer* buffer);

// B904: the same for a finished SMesh's buffer-pointer array, then its
// bounding box. Returns the bytes of capacity released.
size_t finishMesh(irr::scene::SMesh* mesh);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_GEOMETRY_H
