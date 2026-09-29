// B904 -- a built zone/object/character mesh holds exactly its data.
//
// Every ZoneMeshBuilder path fills its SMeshBuffers by push_back, and
// irr::core::array's default growth strategy roughly doubles a small array
// (6, 13, 27, 55, 111, ...). Before B904 the finished buffers kept that
// capacity for the life of the mesh: timorous' 15,674 region buffers held
// 29.4 MB of zone_mesh against a 28 MiB budget (gate-kunark-velious, #224),
// of which 5.8 MB was growth slack. finishMeshBuffer()/finishMesh()
// (zone_geometry.h) reallocate the vertex, index and buffer-pointer arrays
// to their element counts; the contents are unchanged.
//
// Built with a null driver/scene manager: buildColoredMesh and the
// no-pixel-data branch of buildTexturedMesh never touch either.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "client/graphics/eq/s3d_loader.h"   // TextureInfo
#include "client/graphics/eq/zone_geometry.h"

using namespace EQT::Graphics;

namespace {

// A strip of `quads` quads over `textures` material slots: enough triangles
// that every array in the finished buffers has grown through several
// push_back reallocations.
ZoneGeometry makeStrip(int quads, int textures) {
    ZoneGeometry g;
    for (int i = 0; i <= quads; ++i) {
        for (int j = 0; j < 2; ++j) {
            Vertex3D v{};
            v.x = static_cast<float>(i);
            v.y = static_cast<float>(j);
            v.z = 0.25f * static_cast<float>(i % 3);
            v.nz = 1.0f;
            v.u = 0.3f * static_cast<float>(i);
            v.v = static_cast<float>(j);
            g.vertices.push_back(v);
        }
    }
    for (int i = 0; i < quads; ++i) {
        const uint32_t a = 2 * i, b = a + 1, c = a + 2, d = a + 3;
        const uint32_t tex = static_cast<uint32_t>(i % textures);
        g.triangles.push_back(Triangle{a, b, c, tex, 0});
        g.triangles.push_back(Triangle{b, d, c, tex, 0});
    }
    g.minZ = 0.0f;
    g.maxZ = 0.5f;
    auto& mat = g.mutableMaterialData();
    for (int t = 0; t < textures; ++t) {
        mat.textureNames.push_back("tex" + std::to_string(t));
        mat.textureInvisible.push_back(false);
        mat.textureMasked.push_back(false);
    }
    return g;
}

// Every array a finished mesh owns is at exactly its element count.
void expectExact(irr::scene::IMesh* mesh, const char* what) {
    ASSERT_NE(mesh, nullptr) << what;
    auto* smesh = static_cast<irr::scene::SMesh*>(mesh);
    EXPECT_EQ(smesh->MeshBuffers.allocated_size(), smesh->MeshBuffers.size()) << what << ": buffer-pointer array";
    size_t vertices = 0, indices = 0;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        auto* buf = static_cast<irr::scene::SMeshBuffer*>(mesh->getMeshBuffer(i));
        EXPECT_EQ(buf->Vertices.allocated_size(), buf->Vertices.size()) << what << ": buffer " << i << " vertices";
        EXPECT_EQ(buf->Indices.allocated_size(), buf->Indices.size()) << what << ": buffer " << i << " indices";
        vertices += buf->Vertices.size();
        indices += buf->Indices.size();
    }
    EXPECT_GT(vertices, 0u) << what;
    EXPECT_GT(indices, 0u) << what;
}

TEST(ZoneMeshCapacity, ColoredMeshHoldsExactlyItsData) {
    ZoneMeshBuilder builder(nullptr, nullptr, nullptr);
    const ZoneGeometry g = makeStrip(40, 1);   // 82 vertices, 240 indices: several growth steps
    irr::scene::IMesh* mesh = builder.buildColoredMesh(g);
    expectExact(mesh, "buildColoredMesh");
    mesh->drop();
}

TEST(ZoneMeshCapacity, TexturedMeshHoldsExactlyItsData) {
    ZoneMeshBuilder builder(nullptr, nullptr, nullptr);
    const ZoneGeometry g = makeStrip(60, 3);   // three material buffers
    // A present-but-empty texture entry takes the textured path without a
    // driver: no pixels to decode, the buffer is built untextured.
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    textures["tex0"] = std::make_shared<TextureInfo>();
    irr::scene::IMesh* mesh = builder.buildTexturedMesh(g, textures);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->getMeshBufferCount(), 3u);
    expectExact(mesh, "buildTexturedMesh");
    mesh->drop();
}

TEST(ZoneMeshCapacity, FinishingKeepsContentsAndReportsTheSlack) {
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    for (int i = 0; i < 7; ++i) {                    // capacity 6 -> 13
        irr::video::S3DVertex v;
        v.Pos.X = static_cast<float>(i);
        buf->Vertices.push_back(v);
        buf->Indices.push_back(static_cast<irr::u16>(6 - i));
    }
    ASSERT_EQ(buf->Vertices.allocated_size(), 13u);
    const size_t slack = finishMeshBuffer(buf);
    EXPECT_EQ(slack, 6u * sizeof(irr::video::S3DVertex) + 6u * sizeof(irr::u16));
    EXPECT_EQ(buf->Vertices.allocated_size(), 7u);
    EXPECT_EQ(buf->Indices.allocated_size(), 7u);
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(buf->Vertices[i].Pos.X, static_cast<float>(i));
        EXPECT_EQ(buf->Indices[i], static_cast<irr::u16>(6 - i));
    }
    EXPECT_EQ(buf->getBoundingBox().MaxEdge.X, 6.0f);
    EXPECT_EQ(finishMeshBuffer(buf), 0u) << "a finished buffer has nothing left to release";
    buf->drop();
}

}  // namespace
