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
    // B972: the bounded VBO/EBO RING's bytes (every slot is counted into the
    // driver's logical tally by uploadFrameVbo/releaseGpuBuffers).
    size_t vboBytes() const;

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
    // B972-FT (alternative shape): one frame page's window into the packed
    // per-frame VBO/EBO -- its index range (offset + count) and its texture.
    struct PageRun {
        size_t indexOffset = 0;
        size_t indexCount = 0;
        irr::video::ITexture* tex = nullptr;
    };
    // `unsigned` is the GL handle type (GLuint) without the GLES2 header --
    // the same convention name_tag_batch.h uses for its own buffers.
    // A pair that is still being allocated is 0 (glGenBuffers hands out
    // nonzero ids); a pair that was never used is nullptr-sized.
    struct VboPair {
        unsigned vert = 0, idx = 0;
        size_t vertBytes = 0, idxBytes = 0;
    };
    void compileProgram();

    // B972 (#290): free the bounded VBO/EBO and take their bytes out of the
    // driver's logical tally (gles2AdjustRawBufferBytes), B975's pattern.
    void releaseGpuBuffers();

    // B972 (#290) / B972-FT (#326): the ALTERNATIVE shape (chosen by
    // measurement -- the per-page ring re-added 4-5 MiB of driver scratch).
    // ONE bounded VBO/EBO pair per frame (ring slot), written ONCE with the
    // whole frame's concatenated verts/indices, then drawn per page through
    // uploadFrameVbo does the single grow + subdata (the write-wait
    // measurement lives here); drawPackedPageVbo draws one page's index
    // window of the pair just written (offset via the EBO). Gated on
    // EQT_HAS_GLES2 in the cpp.
    // uploadFrameVbo returns false when the VBO path is unavailable
    // (driver type / setVboPathEnabled(false) -- the pixel-identity
    // baseline keeps the client-array draw).
    bool uploadFrameVbo(irr::video::IVideoDriver* driver, size_t slot);
    void drawPackedPageVbo(const PageRun& run) const;

    // B972-FT: the ring slot this render() passes down: the render()-count
    // modulo the configured depth (B972FT_RING_DEPTH, default 3, clamped
    // 1..3 -- 1 is the pre-B972-FT shared pair, the A/C control).
    size_t ringDepth() const;

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
    // B972 (#290) / B972-FT (#326), the ALTERNATIVE shape: a RING of bounded
    // VBO/EBO pairs, ONE per FRAME (frame row = render-count % ringDepth),
    // not per page. Frame N writes its whole packed verts_/indices_ into
    // pair (frameCount % ringDepth) ONCE; every page then draws through an
    // index window into that pair. The written BO is referenced by no draw
    // of frame N (the window is read-only for the rest of the frame) NOR of
    // the in-flight frames N-1 / N-2 (distinct slots), so Lima's
    // lima_flush_job_accessing_bo + lima_bo_wait in lima_transfer_map find
    // no matching job and return without the per-frame write-wait. Each pair
    // grows at its own high-water + one slack vertex, so every glBufferSubData
    // stays strictly smaller than the allocation (B975's pattern).
    // Why not per-page (the first, measured-and-rejected shape): it put depth
    // * maxPages distinct VBOs in flight per frame, and Mesa/Lima's
    // driver-retained per-BO scratch scaled with that -- the 3-leg A/B gave
    // back 4-5 MiB of gpu_unnamed that the cut had taken (card gate 1).
    // One pair per frame keeps the frame's job referencing exactly one VBO +
    // one EBO, the pre-ring cut's shape, with the ring removing the wait.
    // ringDepth=1 (B972FT_RING_DEPTH=1) is the pre-B972-FT shared pair, the
    // A/C control.
    static constexpr size_t kVboRingDepthMax = 3;
    std::vector<VboPair> vboRing_;  // ringDepth() pairs, grown in uploadFrameVbo
    unsigned frameCount_ = 0;  // render() count; frame row is % ringDepth()
    irr::video::IVideoDriver* gpuDriver_ = nullptr;
    bool useVbo_ = true;
};

}  // namespace Graphics
}  // namespace EQT
