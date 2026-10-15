// B1051 (GitLab #371): a multi-buffer character mesh must resolve a per-part
// texture from the ConstrainedTextureCache when its raw S3D bytes have been
// released (the EQE1 pack is present: "472 textures with 0 raw bytes (EQE1
// authoritative)").
//
// The live symptom was the ETC1 "smear": Guard_Treitan (race 71, one mesh
// buffer per part) rendered with a single flat (255,200,180,160) body while
// the raw-RGBA run and every single-buffer zone mesh rendered correctly.
//
// Root cause: ZoneMeshBuilder::getOrLoadTexture() consulted only its own
// textureCache_/pendingTextures_ maps. In the EQE1-authoritative run the raw
// bytes are released at parse time (RaceModelLoader::loadModelZone ->
// releaseBytesIfEqe1Authoritative), so pendingTextures_ is empty; the per-part
// GPU textures exist only in the ConstrainedTextureCache (created by the
// Compressed upload path, which registers with the renderer's OTHER
// ZoneMeshBuilder, not the RaceModelLoader's own builder). buildTexturedMesh()
// already had the constrainedCache_->getTexture() fallback for exactly this;
// getOrLoadTexture() (and hasTexture(), used by the isTextureAvailable()
// override check in mesh_building.cpp) did not.
//
// This test reds on the pre-fix code: with raw data empty and no pending
// entry, getOrLoadTexture() returned nullptr -> the buffer's textureless
// diffuse fallback (the smear). With the fix it returns the cache's texture
// and the built buffer's material holds exactly that per-part texture.
//
// EDT_NULL device (no window/GPU/DISPLAY): getOrLoad()/uploadTexture() on the
// null driver produce real ITexture* objects, exactly the idiom
// test_constrained_texture_cache.cpp uses for the unmasked-variant queries.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"   // TextureInfo (complete type)
#include "client/graphics/eq/zone_geometry.h"
#include <irrlicht.h>
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

namespace {

// A minimal valid 24bpp BMP so decodeBMP() succeeds (copied idiom from
// test_constrained_texture_cache.cpp).
std::vector<char> makeTestBmp(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    int rowSize = ((width * 3 + 3) / 4) * 4;
    uint32_t pixelDataSize = static_cast<uint32_t>(rowSize) * height;
    uint32_t fileSize = 54 + pixelDataSize;

    std::vector<char> bmp(fileSize, 0);
    uint8_t* p = reinterpret_cast<uint8_t*>(bmp.data());

    p[0] = 'B'; p[1] = 'M';
    *reinterpret_cast<uint32_t*>(p + 2) = fileSize;
    *reinterpret_cast<uint32_t*>(p + 10) = 54;
    *reinterpret_cast<uint32_t*>(p + 14) = 40;
    *reinterpret_cast<int32_t*>(p + 18) = width;
    *reinterpret_cast<int32_t*>(p + 22) = height;
    *reinterpret_cast<uint16_t*>(p + 26) = 1;
    *reinterpret_cast<uint16_t*>(p + 28) = 24;
    *reinterpret_cast<uint32_t*>(p + 34) = pixelDataSize;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = p + 54 + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    return bmp;
}

// Two parts (two texture slots -> two mesh buffers, the multi-buffer
// character shape), each part a quad, distinct per-part texture names.
ZoneGeometry makeTwoPartGeometry() {
    ZoneGeometry g;
    for (int i = 0; i < 4; ++i) {
        Vertex3D v{};
        v.x = static_cast<float>(i);
        v.y = 0.0f;
        v.z = 0.1f;
        v.nz = 1.0f;
        v.u = 0.2f * static_cast<float>(i);
        v.v = 0.5f;
        g.vertices.push_back(v);
    }
    g.triangles.push_back(Triangle{0, 1, 2, 0, 0});
    g.triangles.push_back(Triangle{1, 3, 2, 0, 0});
    g.triangles.push_back(Triangle{0, 1, 2, 1, 0});
    g.triangles.push_back(Triangle{1, 3, 2, 1, 0});
    g.minZ = 0.0f;
    g.maxZ = 0.2f;
    auto& mat = g.mutableMaterialData();
    mat.textureNames.push_back("qcfch0001.bmp");
    mat.textureNames.push_back("qcflg0001.bmp");
    mat.textureInvisible.push_back(false);
    mat.textureInvisible.push_back(false);
    mat.textureMasked.push_back(false);
    mat.textureMasked.push_back(false);
    return g;
}

class B1051MeshTextureFallbackTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        driver_ = device_->getVideoDriver();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // Upload one name through the cache's public API (the same path a
    // Compressed-source ETC1 upload ends in: a cached resident texture).
    irr::video::ITexture* uploadToCache(const std::string& name, uint8_t r, uint8_t g, uint8_t b) {
        cache_->getOrLoad(name, makeTestBmp(8, 8, r, g, b), "test", "B1051MeshTextureFallbackTest",
                          /*applyPaletteMask=*/false);
        return cache_->uploadTexture(name, /*applyPaletteMask=*/false);
    }
};

// The on-demand lookup (the one mesh_building.cpp's per-buffer loop uses via
// meshBuilder_->getOrLoadTexture()): raw data empty, nothing pending in the
// builder's own maps, texture resident in the constrained cache. Pre-fix this
// returned nullptr (the buffer then drew the textureless (255,200,180,160)
// diffuse -- the ETC1 smear); post-fix it returns the cache's texture.
TEST_F(B1051MeshTextureFallbackTest, GetOrLoadTextureFallsBackToConstrainedCache) {
    irr::video::ITexture* chestTex = uploadToCache("qcfch0001.bmp", 200, 180, 30);
    ASSERT_NE(chestTex, nullptr);

    ZoneMeshBuilder builder(device_->getSceneManager(), driver_, device_->getFileSystem());
    builder.setConstrainedTextureCache(cache_.get());
    // Deliberately do NOT seed textureCache_/pendingTextures_: that is the
    // EQE1-authoritative state (raw bytes released, upload landed in the
    // cache alone).

    // Pre-fix: nullptr -> the mesh buffer's textureless diffuse fallback.
    irr::video::ITexture* resolved = builder.getOrLoadTexture("qcfch0001.bmp", false);
    EXPECT_EQ(resolved, chestTex)
        << "getOrLoadTexture must find the cache-resident per-part texture when raw bytes are released";

    // And it must be findable by hasTexture() too -- that is what the
    // isTextureAvailable() override check (mesh_building.cpp) asks before
    // committing a buffer to an equipment-variant name.
    EXPECT_TRUE(builder.hasTexture("qcfch0001.bmp"));
    // A name nobody uploaded still misses (no spurious hits).
    EXPECT_FALSE(builder.hasTexture("qcffoobar.bmp"));
    EXPECT_EQ(builder.getOrLoadTexture("qcffoobar.bmp", false), nullptr);
}

// The end-to-end shape of the bug: a multi-buffer character mesh (two
// per-part texture slots) whose textures exist ONLY in the constrained cache
// must build with each buffer holding its own per-part texture -- not the
// textureless diffuse fallback on one or both.
TEST_F(B1051MeshTextureFallbackTest, MultiBufferMeshResolvesBothPartTexturesFromCache) {
    irr::video::ITexture* chestTex = uploadToCache("qcfch0001.bmp", 200, 180, 30);
    irr::video::ITexture* legsTex = uploadToCache("qcflg0001.bmp", 60, 140, 20);
    ASSERT_NE(chestTex, nullptr);
    ASSERT_NE(legsTex, nullptr);
    ASSERT_NE(chestTex, legsTex) << "two per-part names must map to distinct GPU textures";

    ZoneMeshBuilder builder(device_->getSceneManager(), driver_, device_->getFileSystem());
    builder.setConstrainedTextureCache(cache_.get());

    const ZoneGeometry g = makeTwoPartGeometry();
    // present-but-EMPTY TextureInfo entries: the EQE1-authoritative state
    // (names known, raw data released). Pre-fix these took the textured path,
    // found no pixels, and left the buffers textureless.
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    textures["qcfch0001.bmp"] = std::make_shared<TextureInfo>();
    textures["qcflg0001.bmp"] = std::make_shared<TextureInfo>();

    irr::scene::IMesh* mesh = builder.buildTexturedMesh(g, textures);
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->getMeshBufferCount(), 2u) << "two texture slots -> two buffers (the multi-buffer character)";

    auto* b0 = static_cast<irr::scene::SMeshBuffer*>(mesh->getMeshBuffer(0));
    auto* b1 = static_cast<irr::scene::SMeshBuffer*>(mesh->getMeshBuffer(1));
    // Pre-fix: nullptr texture + the (255,200,180,160) diffuse = the smear.
    EXPECT_EQ(b0->Material.TextureLayer[0].Texture, chestTex) << "chest buffer must hold the chest texture";
    EXPECT_EQ(b1->Material.TextureLayer[0].Texture, legsTex) << "legs buffer must hold the legs texture";
    mesh->drop();
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS
