#ifndef EQT_GRAPHICS_ZONE_GEOMETRY_H
#define EQT_GRAPHICS_ZONE_GEOMETRY_H

#include "wld_loader.h"
#include <irrlicht.h>
#include <cstddef>
#include <memory>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Forward declarations
struct S3DZone;
struct TextureInfo;
struct CompositeTextureData;
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

    // Build textured mesh using pre-uploaded ITexture pointers (for multi-frame entity building).
    // textures: one ITexture* per texture index in geometry.textureNames
    // textureAlpha: whether each texture has alpha channel
    irr::scene::IMesh* buildTexturedMeshFromUploaded(
        const ZoneGeometry& geometry,
        const std::vector<irr::video::ITexture*>& textures,
        const std::vector<bool>& textureAlpha,
        bool flipV = false);

    // Build a single-buffer mesh using a composite texture sheet.
    // All body-part triangles are merged into one mesh buffer with UVs remapped
    // to the composite sheet, enabling 1 draw call per entity instead of 6-12.
    // compositeTexture: pre-uploaded composite sheet texture
    // compositeData: UV remap metadata from entity_composite_builder JSON
    // hasAlpha: whether the composite texture requires alpha testing
    irr::scene::IMesh* buildCompositeMesh(
        const ZoneGeometry& geometry,
        irr::video::ITexture* compositeTexture,
        const CompositeTextureData& compositeData,
        bool hasAlpha = false,
        bool flipV = true);

    // Load a texture from raw BMP/DDS data.
    // P50: applyPaletteMask, default false, forwards to
    // ConstrainedTextureCache::getOrLoad()/uploadTexture() — pass true ONLY
    // when the caller has already confirmed (via the WLD-derived
    // textureMasked() flag) that this specific texture is WLD-masked. Same
    // contract as ConstrainedTextureCache::getOrLoad() (see P46 there);
    // default false preserves pre-P50 behavior byte-for-byte.
    irr::video::ITexture* loadTextureFromBMP(const std::string& name, const std::vector<char>& data,
                                              bool applyPaletteMask = false);

    // Performance: Lazy texture loading
    // Register texture data for deferred loading (doesn't create Irrlicht texture yet)
    void registerLazyTexture(const std::string& name, std::shared_ptr<TextureInfo> texInfo);

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

    // Constrained rendering support
    // Set optional constrained texture cache for memory-limited rendering
    // When set, textures are loaded through the cache with downsampling and 16-bit conversion
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // Get the constrained texture cache (may be nullptr)
    ConstrainedTextureCache* getConstrainedTextureCache() const { return constrainedCache_; }

    // P13: tag this builder instance with an upload-ledger category (e.g.
    // "zone_region", "object") — a plain string, not threaded through every
    // call, since each ZoneMeshBuilder instance is already scoped to one
    // source (regions vs objects use separate instances in
    // renderer_zone_load.cpp). Passed through to getOrLoad() by
    // loadTextureFromBMP(). No-op when the ledger is disabled.
    void setUploadCategory(const std::string& category) { uploadCategory_ = category; }

    // Info about a fallback mesh buffer whose texture was deferred (background mesh building).
    // Used by the render thread to assign real textures or placeholders after finalization.
    struct FallbackBufferInfo {
        irr::u32 bufferIndex;
        std::string textureName;  // lowercase
    };

    // Get fallback buffer map from last buildAtlasedMesh(deferTextures=true) call.
    // Maps buffer indices to texture names that need assignment on the render thread.
    const std::vector<FallbackBufferInfo>& getFallbackBufferMap() const { return fallbackBufferMap_; }

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Build mesh batched by atlas page — groups all triangles sharing the same
    // atlas page into a single mesh buffer using S3DVertex2TCoords.
    // TCoords = original UVs (for fract() tiling), TCoords2 = atlas tile offset.
    // Textures not in the atlas (animated, missing) fall through to per-texture buffers.
    // When deferTextures=true, fallback buffers skip texture loading and use white
    // vertex colors + solid material. Caller must assign textures via getFallbackBufferMap().
    irr::scene::IMesh* buildAtlasedMesh(
        const ZoneGeometry& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        const TextureAtlas& atlas,
        int pageIndexOffset = 0,
        bool deferTextures = false);
#endif

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

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Set atlas-specific GLSL shader material type IDs
    void setAtlasShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType) {
        shaderMaterialAtlasSolid_ = solidType;
        shaderMaterialAtlasAlpha_ = alphaTestType;
    }
#endif

    // Textures that were null during the last build (async GPU upload not yet complete).
    // Caller can check after buildAtlasedMesh/buildTexturedMesh to queue rebuilds.
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

    // Fallback buffer map from last buildAtlasedMesh(deferTextures=true) call
    std::vector<FallbackBufferInfo> fallbackBufferMap_;

#ifdef EQT_HAS_TEXTURE_ATLAS
    // Atlas-specific shader material type IDs (-1 = not available)
    irr::s32 shaderMaterialAtlasSolid_ = -1;
    irr::s32 shaderMaterialAtlasAlpha_ = -1;
#endif
};

// Helper to generate colors for visualization
irr::video::SColor heightToColor(float height, float minHeight, float maxHeight);
irr::video::SColor normalToColor(float nx, float ny, float nz);

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
