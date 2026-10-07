#pragma once

// B964: the scene node that draws every impostor billboard. One draw call per
// atlas page (B952: per-billboard draws cost ~0.12 ms each on the Pi). The
// quads are rebuilt in render() so they face the camera the frame is drawn
// with; the entity pass fills the list before drawAll().
//
// The program (impostor_batch_node.cpp): a constant 0.5 alpha threshold
// (B958: never a derivative-dependent discard on Lima), RGB divided by the
// sampled alpha (the capture clears to (0,0,0,0), so bilinear filtering
// premultiplies the edge texels -- dividing it out removes the dark fringe of
// #274), and the entity shader's own linear fog evaluated at the billboard's
// vertices, so a billboard is fogged like the model it replaces.
//
// Impostored entities are withheld from the render list (T309's seam), which
// also withholds their name-tag children; the entity pass hands those name
// nodes here and this node registers them in its place.

#include <vector>

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

class ImpostorBatchNode : public irr::scene::ISceneNode {
public:
    ImpostorBatchNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr);
    ~ImpostorBatchNode() override;

    void OnRegisterSceneNode() override;
    void render() override;
    const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override { return box_; }

    void clearQuads();
    void addQuad(int page, irr::video::ITexture* pageTex, const irr::core::vector3df& center,
                 float halfW, float halfH, float u0, float v0, float u1, float v1);
    // Grabbed until the next clearQuads().
    void addNameNode(irr::scene::ISceneNode* node);

    int drawCallsLastRender() const { return drawCallsLastRender_; }
    bool hasProgram() const { return materialType_ >= 0; }

    // B972 (#290) seam for the pixel-identity test: when false, the page
    // draws go through the pre-B972 driver->drawVertexPrimitiveList()
    // client-array path. The default (true) streams each page's quads
    // through the bounded VBO/EBO pair below.
    void setVboPathEnabled(bool enabled) { useVbo_ = enabled; }
    bool isVboPathEnabled() const { return useVbo_; }
    size_t vboBytes() const { return vertexBytes_; }

    // Tools only (tools/hwcaps/impostor_pixel_compare): 0 = the shipped
    // program, 1 = the built-in alpha test (no un-premultiply, no fog: B952's
    // billboard), 2 = the shipped program with its fog off.
    void setDiagnosticMode(int mode);

private:
    struct Quad {
        irr::core::vector3df center;
        float halfW, halfH, u0, v0, u1, v1;
    };
    struct PageQuads {
        irr::video::ITexture* tex = nullptr;
        std::vector<Quad> quads;
    };
    void compileProgram();

    // B972 (#290): free the bounded VBO/EBO and take their bytes out of the
    // driver's logical tally (gles2AdjustRawBufferBytes), B975's pattern.
    void releaseGpuBuffers();

    // B972 (#290): stream the just-built verts_/indices_ of ONE page through
    // the bounded VBO/EBO and draw them, under the material state
    // driver->setMaterial() established. Gated on EQT_HAS_GLES2 in the cpp.
    void drawPageVbo(irr::video::IVideoDriver* driver);

    irr::core::aabbox3d<irr::f32> box_;
    std::vector<PageQuads> pages_;
    std::vector<irr::scene::ISceneNode*> names_;
    std::vector<irr::video::S3DVertex> verts_;
    std::vector<irr::u16> indices_;
    irr::video::SMaterial material_;
    irr::s32 materialType_ = -1;
    int quadCount_ = 0;
    int drawCallsLastRender_ = 0;
    int diagMode_ = 0;
    // B972: one bounded VBO/EBO pair every page's quads stream into
    // (B975's pattern: the allocation grows at high-water + one slack
    // vertex, so each glBufferSubData stays strictly smaller than it).
    // `unsigned` is the GL handle type (GLuint) without the GLES2 header --
    // the same convention name_tag_batch.h uses for its own buffers.
    unsigned vertexVbo_ = 0;
    unsigned indexVbo_ = 0;
    size_t vertexBytes_ = 0;
    size_t indexBytes_ = 0;
    irr::video::IVideoDriver* gpuDriver_ = nullptr;
    bool useVbo_ = true;
};

}  // namespace Graphics
}  // namespace EQT
