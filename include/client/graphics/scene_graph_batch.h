#ifndef EQT_GRAPHICS_SCENE_GRAPH_BATCH_H
#define EQT_GRAPHICS_SCENE_GRAPH_BATCH_H

// B997 (GitLab #317) step 3: the per-material client-array batch of the
// smgr->drawAll() scene-graph residual.
//
// The step-2 census (refactor/B997/README.md) found the residual -- every
// 3D client-array draw the client does not directly instrument (PVS doors,
// the small equipment/overlay tail) -- is the dominant per-frame lever:
// 42 draws/f on qeynos2, 120 on gfaydark. Each per-node CMeshSceneNode draw
// allocates its own ~4 KiB Mesa varying BO (lima cost sheet fact 5);
// collapsing a run of same-material draws into ONE gathered client-array
// draw removes that many BOs per frame.
//
// The zone-merge (P09/P09c, renderer_zone_merge.cpp) is the proven template:
// group by FULL SMaterial equality, sort materialKey primary + front-to-back
// distance secondary, emit one gathered draw per group on an UNREGISTERED
// buffer so COpenGLES2Driver takes the drawVertexPrimitiveList client-array
// fallback (COpenGLES2Driver.cpp:1484) -- the same primitive the zone-merge
// reaches through drawMeshBuffer on its unregistered gen->buf.
//
// PIXEL-IDENTITY, BY CONSTRUCTION (the argument that blessed the zone-merge;
// the x86 live-client frame-diff is NON-DETERMINISTIC -- see
// ~/kb-evidence/b997-draw-count/x86frameab-20261009T2155Z/, 0/18 frames
// identical across two baseline legs -- so no client A/B pixel channel exists
// for this scene):
//   1. Only OPAQUE buffers batch (material renderer isTransparent()==false,
//      i.e. the buffers CMeshSceneNode::render draws in the SOLID pass). A
//      node with ANY transparent buffer is left entirely on the per-node path
//      (correct SOLID+TRANSPARENT pass ordering, unchanged pixels).
//   2. Opaque geometry under a Z-buffer is rasterization-order-identical
//      regardless of draw order (same fragments, same depth test result).
//   3. Each buffer is drawn with exactly the material its node used (full
//      SMaterial equality groups, one setMaterial per group) and the exact
//      world transform its node used (getAbsoluteTransformation(), refreshed
//      every frame by DoorManager::update()).
//
// SUPPRESSION: a node registers for the SOLID pass in OnRegisterSceneNode()
// gated on IsVisible (CMeshSceneNode.cpp:51), and the base class only
// recurses into children of attached + VISIBLE parents (ISceneNode.h:93).
// The batch takes each batched door's pivot OUT of the scene graph this frame
// (the door manager's own grab/remove idiom -- clearDoors detaches + drops the
// pivot the same way) and re-attaches it after the SOLID pass, so
// drawAll()'s SOLID list never contains it. A door is therefore drawn exactly
// once per frame: by the batch (when batched) or by drawAll (when not).
//
// CENSUS: the batched draws still land in the driver's generic draw3d:client
// pool, so the derived scene_graph count (draw_census.h: win_3d_client -
// zone_draws - entity_body) drops by exactly the per-node draws replaced.
// scene_graph -> ~D_sg (distinct in-graph materials) is the acceptance
// measurement.

#include <irrlicht.h>
#include <functional>
#include <vector>

namespace EQT {
namespace Graphics {

class SceneGraphBatch {
public:
    // Collect this frame's batchable buffers from one node. `driver` supplies
    // the per-buffer transparency classification
    // (getMaterialRenderer(...)->isTransparent()). Returns true if the
    // node's buffers all went into the batch (the caller must keep the node
    // OUT of drawAll() this frame -- the door manager removes its pivot),
    // false if the node must stay per-node (any transparent buffer,
    // non-S3DVertex, or no buffers).
    // `world` is the node's getAbsoluteTransformation() at gather time;
    // `distance` is the caller's front-to-back sort key (squared is fine).
    bool addNode(irr::video::IVideoDriver* driver,
                 const irr::scene::IMesh* mesh,
                 const irr::core::matrix4& world,
                 float distance);

    // Test seam: override the per-type transparency classification (normally
    // read from driver->getMaterialRenderer(type)->isTransparent()). When set,
    // addNode() consults it INSTEAD of the driver -- lets a unit test exercise
    // the opaque-only guard on the EDT_NULL driver, whose built-in renderers
    // all report isTransparent()==false (the base-class default), which would
    // otherwise make the guard unobservable. Production never sets this.
    void setTransparencyClassifier(
        std::function<bool(irr::video::E_MATERIAL_TYPE)> fn) {
        transparentClassifier_ = std::move(fn);
    }

    // Group by full-SMaterial equality, sort (materialKey primary, distance
    // secondary -- zone-merge convention), assign group layout.
    // Returns the draw count render() will emit.
    int build();

    // Reset per-frame state (call before re-gathering the next frame).
    void clear();

    // Test seam: the baked client array exactly as render() would draw it --
    // the gathered S3DVertex run (world matrix already applied to position +
    // normal, vertices ordered by group then entry) plus the gathered u16
    // indices (each relative to the array start). Empty until render() has
    // baked the current build(). Lets a unit test verify the transform math
    // without a real GL driver.
    struct BakedDraw {
        const irr::video::S3DVertex* vertices = nullptr;
        unsigned int vertexCount = 0;
        const unsigned short* indices = nullptr;
        unsigned int indexCount = 0;
    };
    BakedDraw lastBakedDraw() const {
        return BakedDraw{
            reinterpret_cast<const irr::video::S3DVertex*>(vertexScratch_.data()),
            renderedVerts_,
            indexScratch_.data(),
            renderedIdx_};
    }

    // Emit one gathered client-array draw per group:
    //   setTransform(ETS_WORLD, identity); setMaterial(group material);
    //   drawVertexPrimitiveList(group vertex slice, group indices, 16bit).
    // Returns the draws emitted. `outVerticesDrawn` accumulates the vertex
    // count re-uploaded this frame (the cost side of the trade).
    int render(irr::video::IVideoDriver* driver,
               unsigned long long* outVerticesDrawn = nullptr) const;

    bool empty() const { return entries_.empty(); }
    unsigned long long batchedNodes() const { return batchedNodes_; }
    int groupCount() const { return groupCount_; }

private:
    struct Entry {
        irr::video::SMaterial material;
        const void* vertices = nullptr;    // source-local S3DVertex
        unsigned int vertexCount = 0;
        const void* indices = nullptr;     // source u16 indices (mesh's own type)
        unsigned int indexCount = 0;
        irr::core::matrix4 world;
        float distance = 0.0f;
        int group = -1;
        int node = -1;                     // added-run start (diagnostics)
    };

    struct Group {
        irr::video::SMaterial material;
        unsigned int vertexCount = 0;      // gathered vertex total
        unsigned int indexCount = 0;       // gathered index total
        int firstEntry = -1;               // contiguous run start in entries_
        int nEntries = 0;                  // number of entries in the group
        int vertexBase = 0;                // offset into the vertex scratch
        int indexBase = 0;                 // offset into the index scratch
        bool valid = true;                 // false = u16 overflow, doors stay per-node
    };

    static unsigned int textureGroupId(const irr::video::SMaterial& mat);

    std::vector<Entry> entries_;
    std::vector<Group> groups_;
    std::vector<int> order_;               // group indices in draw order
    unsigned long long batchedNodes_ = 0;
    int groupCount_ = 0;
    // Scratch reused across render() calls (vertices + u16 indices).
    mutable std::vector<unsigned char> vertexScratch_;
    mutable std::vector<unsigned short> indexScratch_;
    // Test seam: the baked scratch extent from the last render().
    mutable unsigned int renderedVerts_ = 0;
    mutable unsigned int renderedIdx_ = 0;
    // Test seam: per-type transparency override (unset -> driver's answer).
    std::function<bool(irr::video::E_MATERIAL_TYPE)> transparentClassifier_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SCENE_GRAPH_BATCH_H
