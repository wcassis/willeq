// B997 (GitLab #317): the per-material client-array batch of the PVS doors.
//
// The census (refactor/B997/README.md) shows smgr->drawAll()'s SOLID residual
// is one client-array draw per PVS door (q2 ~29/f, gd ~78/f); each costs its
// own Mesa per-draw varying BO (cost-sheet fact 5). SceneGraphBatch collapses a
// run of same-material opaque unlit S3DVertex doors into ONE gathered draw per
// material group; the pixel-identity rests on the construction argument
// (scene_graph_batch.h). These tests pin the BATCHING DECISION so the draw
// count cannot silently regress:
//   * grouping: doors sharing a full SMaterial collapse to one group (one
//     draw); a differing material is a separate group;
//   * the opaque-only guard: a node with a transparent buffer is NOT batched
//     (addNode returns false) so it stays per-node and keeps SOLID/TRANSPARENT
//     pass order (exercised via the setTransparencyClassifier seam -- the
//     EDT_NULL driver reports every built-in type opaque);
//   * the math: baking each door's world matrix W into the client array
//     (position + normal) and drawing with an identity world reproduces the
//     node's own transform, and the gathered u16 indices address the right
//     scratch vertices across a multi-entry group.
//
// IRR-LICHT CONVENTION (the bug this test exists to pin): CMatrix4 is row-major
// with the translation in the 4th ROW -- setTranslation writes M[12/13/14]
// ("D3D style, row major with translations in the 4th row"). The GLES2 driver
// uploads wvp.pointer() UNTRANSPOSED and the built-in VS does
// mWorldViewProj * vec4(p,1) -- which, given irrlicht's storage, computes the
// ROW-VECTOR transform (out.X = x*M[0]+y*M[4]+z*M[8]+M[12]). The CPU bake must
// mirror that exactly or it reads the translation from M[3/7/11] (empty on a
// rotation) and every door collapses to the origin. The transform assertions
// below use hand-built matrices whose irrlicht layout is unambiguous, so the
// expected values are authoritative, not derived from the same code under test.
//
// The null driver (EDT_NULL) runs the whole add/build/render path headless: its
// setTransform/drawVertexPrimitiveList no-op and lastBakedDraw() exposes the
// client array exactly as render() would submit it.

#include <gtest/gtest.h>

#include <irrlicht.h>
#include "client/graphics/scene_graph_batch.h"

#include <cmath>
#include <vector>

using namespace EQT::Graphics;
using namespace irr::video;

namespace {

// One unit quad (4 vertices, 2 triangles) as a raw S3DVertex buffer, positions
// offset by `ox` so two quads are spatially distinct. Returns a heap SMesh the
// caller owns (drop()).
irr::scene::IMesh* makeQuadMesh(irr::video::SMaterial mat, float ox) {
    auto* mesh = new irr::scene::SMesh();
    auto* buf = new irr::scene::SMeshBuffer();
    const float x[4] = {0, 1, 1, 0};
    const float y[4] = {0, 0, 1, 1};
    const unsigned short idx[6] = {0, 1, 2, 0, 2, 3};
    for (int v = 0; v < 4; ++v) {
        S3DVertex vx;
        vx.Pos = irr::core::vector3df(x[v] + ox, y[v], 0.0f);
        vx.Normal = irr::core::vector3df(0, 0, 1);
        vx.Color = irr::video::SColor(255, 255, 255, 255);
        vx.TCoords = irr::core::vector2df(0, 0);
        buf->Vertices.push_back(vx);
    }
    for (int i = 0; i < 6; ++i) buf->Indices.push_back(idx[i]);
    buf->Material = mat;
    mesh->addMeshBuffer(buf);
    return mesh;
}

// The row-vector transform the GLES2 driver computes (and the CPU bake must
// mirror), over a row-major irrlicht matrix (translation in M[12/13/14]):
//   out.X = x*M[0] + y*M[4] + z*M[8]  + M[12]
//   out.Y = x*M[1] + y*M[5] + z*M[9]  + M[13]
//   out.Z = x*M[2] + y*M[6] + z*M[10] + M[14]
irr::core::vector3df rowVectorApply(const irr::core::matrix4& m,
                                    const irr::core::vector3df& p) {
    const float* M = m.pointer();
    return irr::core::vector3df(
        p.X * M[0] + p.Y * M[4] + p.Z * M[8]  + M[12],
        p.X * M[1] + p.Y * M[5] + p.Z * M[9]  + M[13],
        p.X * M[2] + p.Y * M[6] + p.Z * M[10] + M[14]);
}

} // namespace

class SceneGraphBatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        irr::SIrrlichtCreationParameters p;
        p.DriverType = irr::video::EDT_NULL;
        p.LoggingLevel = irr::ELL_NONE;
        device_ = irr::createDeviceEx(p);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        driver_ = device_->getVideoDriver();
    }
    void TearDown() override { if (device_) device_->drop(); }
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
};

// Two doors with the SAME opaque unlit material collapse to ONE group / ONE
// draw. This is the exact collapse the census predicts (gd 3 door models ->
// D_sg draws, q2 5 -> ~5).
TEST_F(SceneGraphBatchTest, SameOpaqueMaterialCollapsesToOneDraw) {
    SMaterial mat;
    mat.MaterialType = irr::video::EMT_SOLID;   // opaque to the driver's predicate
    mat.Lighting = false;                        // doors are unlit
    mat.BackfaceCulling = false;

    auto* m1 = makeQuadMesh(mat, 0.0f);
    auto* m2 = makeQuadMesh(mat, 5.0f);
    SceneGraphBatch b;
    irr::core::matrix4 wA; wA.setTranslation({10, 0, 0});
    irr::core::matrix4 wB; wB.setTranslation({50, 0, 0});
    EXPECT_TRUE(b.addNode(driver_, m1, wA, 10.0f));
    EXPECT_TRUE(b.addNode(driver_, m2, wB, 40.0f));
    EXPECT_EQ(2u, b.batchedNodes());

    EXPECT_EQ(1, b.build());          // one material group -> one gather
    EXPECT_EQ(1, b.groupCount());
    EXPECT_EQ(1, b.render(driver_));  // one client-array draw emitted
    m1->drop(); m2->drop();
}

// A door with a DIFFERENT material is a second group -> one more draw. The
// group count must track the number of distinct materials, so this pins the
// regression the batch is supposed to prevent (a shared key wrongly split, or
// a distinct material wrongly merged, both move this number).
TEST_F(SceneGraphBatchTest, DifferentMaterialsAreSeparateGroups) {
    SMaterial solid;
    solid.MaterialType = irr::video::EMT_SOLID;
    solid.Lighting = false;
    solid.BackfaceCulling = false;
    SMaterial alpha = solid;
    alpha.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;  // alpha-test: distinct type (opaque to the driver predicate -> still batches)

    auto* m1 = makeQuadMesh(solid, 0.0f);
    auto* m2 = makeQuadMesh(alpha, 5.0f);
    SceneGraphBatch b;
    irr::core::matrix4 w; w.setTranslation({1, 0, 0});
    EXPECT_TRUE(b.addNode(driver_, m1, w, 10.0f));
    irr::core::matrix4 w2; w2.setTranslation({9, 0, 0});
    EXPECT_TRUE(b.addNode(driver_, m2, w2, 40.0f));
    EXPECT_EQ(2, b.build());      // two distinct materials -> two groups
    EXPECT_EQ(2, b.groupCount());
    EXPECT_EQ(2, b.render(driver_));
    m1->drop(); m2->drop();
}

// A node with a TRUE-transparent buffer is NOT batched (addNode returns false)
// -- it must stay on the per-node path so its buffer runs in the TRANSPARENT
// pass with correct ordering. This is the guard that keeps the batch
// opaque-only. Exercised via the classifier seam because the EDT_NULL driver
// reports every built-in type as opaque (base isTransparent()==false), which
// would otherwise make the guard unobservable.
TEST_F(SceneGraphBatchTest, TransparentNodeIsNotBatched) {
    SceneGraphBatch b;
    // EMT_TRANSPARENT_ALPHA_CHANNEL (true blend) -> transparent; everything
    // else opaque (mirrors the real driver's classification for these types).
    b.setTransparencyClassifier([](irr::video::E_MATERIAL_TYPE t) {
        return t == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    });

    SMaterial blend;
    blend.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    blend.Lighting = false;
    auto* mBlend = makeQuadMesh(blend, 0.0f);
    irr::core::matrix4 w; w.setTranslation({1, 0, 0});
    EXPECT_FALSE(b.addNode(driver_, mBlend, w, 10.0f));  // transparent -> not batched
    EXPECT_EQ(0u, b.batchedNodes());

    // ...but an opaque unlit door in the same batch DOES batch (control).
    SMaterial solid;
    solid.MaterialType = irr::video::EMT_SOLID;
    solid.Lighting = false;
    auto* mSolid = makeQuadMesh(solid, 0.0f);
    EXPECT_TRUE(b.addNode(driver_, mSolid, w, 10.0f));   // opaque -> batched
    EXPECT_EQ(1u, b.batchedNodes());
    mBlend->drop(); mSolid->drop();
}

// The math, split so each axis is checked independently:
//
// (A) TRANSLATION is the bug this test was written to pin: an irrlicht
//     translation lives in M[12/13/14]. A bake that reads the wrong cells
//     (M[3/7/11], the column-vector form) drops it and leaves the vertex at
//     the origin. Two same-material doors in one group:
//       door A world = translate(10,3,-2), local (0,0,0) -> baked (10,3,-2)
//       door A local (1,0,0)                       -> baked (11,3,-2)
//       door B world = identity,       local (0,0,0) -> baked (0,0,0)
// (B) ROTATION: a 180-degree yaw (matrix with -1 on the X and Z axes) flips a
//     local (1,0,0) vertex to (-1,0,0); the normal (0,0,1) to (0,0,-1).
// (C) INDEX REMAP: door A's u16 indices stay 0,1,2,0,2,3 (it's first in the
//     group); door B's are remapped by its 4-vertex offset -> 4,5,6,4,6,7.
//     A wrong remap (the classic gathered-draw bug) points door B at door A's
//     vertices.
TEST_F(SceneGraphBatchTest, BakedArrayCarriesWorldTransformAndRemappedIndices) {
    SMaterial mat;
    mat.MaterialType = irr::video::EMT_SOLID;
    mat.Lighting = false;
    mat.BackfaceCulling = false;

    // Hand-built worlds (irrlicht row-major). setTranslation -> M[12/13/14]
    // (verified: getTranslation() returns (M[12],M[13],M[14])).
    irr::core::matrix4 WA; WA.setTranslation({10, 3, -2});
    ASSERT_FLOAT_EQ(10.0f, WA.getTranslation().X);  // sanity: irrlicht layout
    ASSERT_FLOAT_EQ(3.0f, WA.getTranslation().Y);
    ASSERT_FLOAT_EQ(-2.0f, WA.getTranslation().Z);
    irr::core::matrix4 WB;  // identity
    ASSERT_FLOAT_EQ(0.0f, rowVectorApply(WB, {5, 5, 5}).X - 5.0f);

    auto* mA = makeQuadMesh(mat, 0.0f);   // local (0,0,0),(1,0,0),(1,1,0),(0,1,0)
    auto* mB = makeQuadMesh(mat, 0.0f);   // same local quad
    SceneGraphBatch b;
    ASSERT_TRUE(b.addNode(driver_, mA, WA, 10.0f));   // A nearer
    ASSERT_TRUE(b.addNode(driver_, mB, WB, 40.0f));
    ASSERT_EQ(1, b.build());
    ASSERT_EQ(1, b.render(driver_));

    const auto baked = b.lastBakedDraw();
    ASSERT_NE(baked.vertices, nullptr);
    ASSERT_EQ(8u, baked.vertexCount);   // 4 + 4 gathered
    ASSERT_EQ(12u, baked.indexCount);   // 6 + 6

    // Entry order in the group is the add order (stable sort by group, then
    // add-order preserved): vertices 0..3 are door A, 4..7 door B.
    // (A) translation applied from M[12/13/14]:
    //   door A vertex 0 local (0,0,0) -> (10,3,-2)
    EXPECT_FLOAT_EQ(10.0f, baked.vertices[0].Pos.X);
    EXPECT_FLOAT_EQ(3.0f, baked.vertices[0].Pos.Y);
    EXPECT_FLOAT_EQ(-2.0f, baked.vertices[0].Pos.Z);
    //   door A vertex 1 local (1,0,0) -> (11,3,-2)
    EXPECT_FLOAT_EQ(11.0f, baked.vertices[1].Pos.X);
    EXPECT_FLOAT_EQ(3.0f, baked.vertices[1].Pos.Y);
    EXPECT_FLOAT_EQ(-2.0f, baked.vertices[1].Pos.Z);
    //   door B vertex 4 local (0,0,0), identity world -> (0,0,0)
    EXPECT_FLOAT_EQ(0.0f, baked.vertices[4].Pos.X);
    EXPECT_FLOAT_EQ(0.0f, baked.vertices[4].Pos.Y);
    EXPECT_FLOAT_EQ(0.0f, baked.vertices[4].Pos.Z);

    // (C) indices: door A stays local 0,1,2,0,2,3; door B remapped by +4.
    const unsigned short expectedIdx[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    for (int i = 0; i < 12; ++i)
        EXPECT_EQ(expectedIdx[i], baked.indices[i]) << "index " << i;

    mA->drop(); mB->drop();

    // (B) rotation: a 180-degree yaw about Y. Identity (default ctor) with the
    //     X and Z axes negated -> flips local (1,0,0) to (-1,0,0) and normal
    //     (0,0,1) to (0,0,-1). Built via the (row,col) operator (pointer() is
    //     const in the null-compile).
    irr::core::matrix4 RY180;  // identity
    RY180(0, 0) = -1;          // M[0]  = -1
    RY180(2, 2) = -1;          // M[10] = -1
    auto* mR = makeQuadMesh(mat, 0.0f);
    SceneGraphBatch b2;
    ASSERT_TRUE(b2.addNode(driver_, mR, RY180, 10.0f));
    ASSERT_EQ(1, b2.build());
    ASSERT_EQ(1, b2.render(driver_));
    const auto b2d = b2.lastBakedDraw();
    // local (0,0,0) -> (0,0,0); local (1,0,0) -> (-1,0,0); local (0,1,0) -> (0,1,0).
    EXPECT_FLOAT_EQ(0.0f, b2d.vertices[0].Pos.X);
    EXPECT_FLOAT_EQ(-1.0f, b2d.vertices[1].Pos.X);
    EXPECT_FLOAT_EQ(1.0f, b2d.vertices[2].Pos.Y);
    // normal (0,0,1) -> (0,0,-1): the 3x3 flips Z.
    EXPECT_FLOAT_EQ(-1.0f, b2d.vertices[0].Normal.Z);
    mR->drop();
}
