// B997 (GitLab #317) step 3: the per-material client-array batch of the
// smgr->drawAll() scene-graph residual. See the header for the full design
// + the pixel-identity-by-construction argument.
//
// Buffer layout (scratch, one render() call):
//   vertexScratch_ : [ group0 baked verts | group1 baked verts | ... ]
//   indexScratch_  : [ group0 u16 indices | group1 u16 indices | ... ]
// A group's draw is (vertices = scratch + g.vertexBase, count = g.vertexCount,
// indices = indexScratch + g.indexBase, count = g.indexCount) with the world
// transform set to identity: the per-node world matrix is BAKED into the baked
// vertices (mWorldViewProj * (W*p) with an identity ETS_WORLD) and the u16
// indices are remapped to absolute scratch positions. drawVertexPrimitiveList()
// feeds both as client arrays on the same fallback path COpenGLES2Driver uses
// for every unregistered buffer (COpenGLES2Driver.cpp:1484) -- the primitive
// the zone-merge reaches through drawMeshBuffer on its unregistered gen->buf.

#include "client/graphics/scene_graph_batch.h"
#include "common/logging.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {
// One group's gathered vertices must fit uint16 index space (the zone-merge
// convention, kMaxVertsPerBucket). A group over the limit is skipped whole
// (its nodes stay on the per-node path) -- never re-indexed.
constexpr unsigned int kMaxVertsPerGroup = 65535;
}

unsigned int SceneGraphBatch::textureGroupId(const irr::video::SMaterial& mat) {
    // Byte-identical to renderer_zone_merge.cpp's copy (documented
    // duplication precedent, H02b/Q02) and to renderer_zone_draw.cpp's.
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}

// Clear this frame's state so the batch can be re-gathered next frame. The
// scratch vectors keep capacity (reused), entries/groups/order are reset.
void SceneGraphBatch::clear() {
    entries_.clear();
    groups_.clear();
    order_.clear();
    batchedNodes_ = 0;
    groupCount_ = 0;
}

bool SceneGraphBatch::addNode(irr::video::IVideoDriver* driver,
                              const irr::scene::IMesh* mesh,
                              const irr::core::matrix4& world,
                              float distance) {
    if (!driver || !mesh) return false;
    const irr::u32 nbufs = mesh->getMeshBufferCount();
    if (nbufs == 0) return false;

    for (irr::u32 i = 0; i < nbufs; ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexCount() == 0 || mb->getIndexCount() == 0) continue;
        const irr::video::SMaterial& mat = mb->getMaterial();
        // (1) A node with ANY transparent buffer stays on the per-node path:
        //     the batch emits one draw at the SOLID-pre point, and a
        //     transparent buffer must run in the TRANSPARENT pass (blend,
        //     back-to-front). Alpha-TEST is NOT transparent (the driver's
        //     TRANSPARENT_ALPHA_CHANNEL_REF renderer reports isTransparent
        //     == false -- the discard shader keeps it in the solid pass), so
        //     alpha-tested doors batch.
        if (transparentClassifier_) {
            if (transparentClassifier_(mat.MaterialType)) return false;
        } else {
            irr::video::IMaterialRenderer* rnd =
                driver->getMaterialRenderer(mat.MaterialType);
            if (rnd && rnd->isTransparent()) return false;
        }
        // (2) Only S3DVertex buffers are gathered (the type door meshes
        //     carry, per ZoneMeshBuilder); anything else leaves the node
        //     per-node rather than being silently dropped.
        if (mb->getVertexType() != irr::video::EVT_STANDARD) return false;
        // (3) UNLIT: the batch bakes the node's world matrix W into the
        //     position AND normal, then draws with identity mWorld /
        //     mWorldViewProj=P*V. That reproduces exactly the built-in
        //     SOLID3D/ALPHA3D driver path (the material types doors carry,
        //     EMT_SOLID / EMT_TRANSPARENT_ALPHA_CHANNEL_REF --
        //     zone_shader.cpp:84/98), whose VS is gl_Position = PVP*pos and
        //     whose lighting collapses to vColor = aColor (ambient-only:
        //     sun/point lights are zeroed, COpenGLES2Driver.cpp:1711-1726) --
        //     independent of the normal and of the Lighting flag. The only
        //     residual delta is the transform-order reorder P*V*(W*p) vs
        //     (P*V*W)*p -- the P09c-accepted FP-transform-order class.
        //     A LIT material would additionally read mWorld through its own
        //     light/shadow path, and a WIND shader displaces `local` in
        //     object space before PVP, so baking breaks both -- reject them
        //     (they stay per-node). Doors are built Lighting=false
        //     (door_manager_create.cpp:303), so every door passes (3).
        if (mat.Lighting) return false;
    }

    int firstIdx = static_cast<int>(entries_.size());
    for (irr::u32 i = 0; i < nbufs; ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexCount() == 0 || mb->getIndexCount() == 0) continue;
        Entry e;
        e.material = mb->getMaterial();
        e.vertices = mb->getVertices();
        e.vertexCount = mb->getVertexCount();
        e.indices = mb->getIndices();
        e.indexCount = mb->getIndexCount();
        e.world = world;
        e.distance = distance;
        entries_.push_back(std::move(e));
    }
    if (static_cast<int>(entries_.size()) > firstIdx) {
        for (int i = firstIdx; i < static_cast<int>(entries_.size()); ++i)
            entries_[i].node = firstIdx;
        ++batchedNodes_;
    }
    return static_cast<int>(entries_.size()) > firstIdx;
}

int SceneGraphBatch::build() {
    groups_.clear();
    order_.clear();
    groupCount_ = 0;
    if (entries_.empty()) return 0;

    // Group by FULL SMaterial equality (Irrlicht's operator==, the complete
    // field walk -- zone-merge P09b convention: grouping can only SHRINK vs
    // materialKey-only, never grow, so this is strictly a correctness
    // hardening that costs nothing when materials already match).
    for (auto& e : entries_) {
        int gid = -1;
        for (size_t g = 0; g < groups_.size(); ++g) {
            if (groups_[g].material == e.material) { gid = static_cast<int>(g); break; }
        }
        if (gid < 0) {
            Group g;
            g.material = e.material;
            groups_.push_back(std::move(g));
            gid = static_cast<int>(groups_.size()) - 1;
        }
        e.group = gid;
    }

    // Make each group's entries contiguous (stable -- within-group order is
    // node order), then firstEntry/entryCount/vertexBase/indexBase fall out
    // of the runs.
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry& a, const Entry& b) { return a.group < b.group; });
    size_t vbase = 0, ibase = 0;
    for (size_t g = 0; g < groups_.size(); ++g) {
        Group& grp = groups_[g];
        grp.vertexCount = grp.indexCount = 0;
        grp.entryCount = 0;
        size_t i = 0;
        while (i < entries_.size() && entries_[i].group != static_cast<int>(g)) ++i;
        grp.firstEntry = static_cast<int>(i);
        for (; i < entries_.size() && entries_[i].group == static_cast<int>(g); ++i) {
            grp.vertexCount += entries_[i].vertexCount;
            grp.indexCount += entries_[i].indexCount;
            ++grp.entryCount;
        }
        grp.vertexBase = static_cast<int>(vbase);
        grp.indexBase = static_cast<int>(ibase);
        grp.valid = grp.vertexCount <= kMaxVertsPerGroup;
        // Advance the running bases only for valid groups: an invalid group's
        // entries are NOT laid out (its nodes stay per-node).
        if (grp.valid) {
            vbase += grp.vertexCount;
            ibase += grp.indexCount;
        }
    }

    // Draw order: materialKey primary, nearest entry's distance secondary
    // (front-to-back -- zone-merge convention). Opaque geometry under the
    // Z-buffer makes this a pure state-change optimization, never a
    // correctness input.
    order_.resize(groups_.size());
    for (size_t g = 0; g < groups_.size(); ++g) order_[g] = static_cast<int>(g);
    std::stable_sort(order_.begin(), order_.end(), [this](int a, int b) {
        const Group& ga = groups_[a];
        const Group& gb = groups_[b];
        const unsigned int ka = textureGroupId(ga.material);
        const unsigned int kb = textureGroupId(gb.material);
        if (ka != kb) return ka < kb;
        return entries_[ga.firstEntry].distance < entries_[gb.firstEntry].distance;
    });
    for (int g : order_) if (groups_[g].valid) ++groupCount_;
    return groupCount_;
}

int SceneGraphBatch::render(irr::video::IVideoDriver* driver,
                            unsigned long long* outVerticesDrawn) const {
    if (outVerticesDrawn) *outVerticesDrawn = 0;
    if (!driver || order_.empty() || groupCount_ == 0) return 0;

    unsigned long long totalVerts = 0, totalIdx = 0;
    for (int g : order_) {
        if (!groups_[g].valid) continue;
        totalVerts += groups_[g].vertexCount;
        totalIdx += groups_[g].indexCount;
    }
    if (totalVerts == 0 || totalIdx == 0) return 0;

    vertexScratch_.resize(totalVerts * sizeof(irr::video::S3DVertex));
    indexScratch_.resize(totalIdx);
    renderedVerts_ = static_cast<unsigned int>(totalVerts);
    renderedIdx_ = static_cast<unsigned int>(totalIdx);
    irr::video::S3DVertex* const vbase =
        reinterpret_cast<irr::video::S3DVertex*>(vertexScratch_.data());
    unsigned short* const ibase = indexScratch_.data();

    // Bake: transform each entry's source-local S3DVertex by the node's world
    // matrix, remap its u16 indices to absolute scratch positions.
    for (int g : order_) {
        const Group& grp = groups_[g];
        if (!grp.valid) continue;
        irr::video::S3DVertex* vd = vbase + grp.vertexBase;
        unsigned short* id = ibase + grp.indexBase;
        for (int i = grp.firstEntry; i < grp.firstEntry + grp.entryCount; ++i) {
            const Entry& e = entries_[i];
            const irr::video::S3DVertex* src = static_cast<const irr::video::S3DVertex*>(e.vertices);
            // Bake: transform each entry's source-local S3DVertex by the node's
            // world matrix, remap its u16 indices to absolute scratch positions.
            // IRR-LICHT CONVENTION: CMatrix4 is row-major with the translation in
            // the 4th ROW (setTranslation -> M[12/13/14], "D3D style, row major
            // with translations in the 4th row"). The GLES2 driver uploads
            // wvp.pointer() UNTRANSPOSED and the built-in VS does
            // mWorldViewProj * vec4(p,1) -- given irrlicht's storage that
            // computes the ROW-VECTOR transform, i.e. out = v * M with
            // out.X = v0*M[0]+v1*M[4]+v2*M[8]+M[12]. This CPU bake must mirror
            // that EXACTLY (read each output component down a COLUMN of M:
            // x<-M[0],M[4],M[8],M[12]) or the translation (M[12/13/14]) is
            // read from the wrong cells and every door collapses to the origin
            // -- the very bug the test BakedArrayCarriesWorldTransformAndRemapped
            // Indices pins. (A naive column-vector M[3/7/11] translation read is
            // WRONG for irrlicht storage.) Same float ops the VS does with
            // P*V*(W*p) when the node's own world matrix was the transform --
            // mathematically equivalent (the P09c-accepted FP-transform-order
            // class, quantified and Will-reviewed for the zone-merge).
            const float* M = e.world.pointer();
            for (unsigned int v = 0; v < e.vertexCount; ++v) {
                const irr::video::S3DVertex& s = src[v];
                const float x = s.Pos.X, y = s.Pos.Y, z = s.Pos.Z;
                vd[v].Pos = irr::core::vector3df(
                    M[0]  * x + M[4]  * y + M[8]  * z + M[12],
                    M[1]  * x + M[5]  * y + M[9]  * z + M[13],
                    M[2]  * x + M[6]  * y + M[10] * z + M[14]);
                // Normal: same 3x3 (rule (3) -- doors are unlit so this is
                // never read; kept consistent for the record). Row-vector 3x3.
                vd[v].Normal = irr::core::vector3df(
                    M[0]  * s.Normal.X + M[4]  * s.Normal.Y + M[8]  * s.Normal.Z,
                    M[1]  * s.Normal.X + M[5]  * s.Normal.Y + M[9]  * s.Normal.Z,
                    M[2]  * s.Normal.X + M[6]  * s.Normal.Y + M[10] * s.Normal.Z);
                vd[v].Color = s.Color;
                vd[v].TCoords = s.TCoords;
            }
            const unsigned short* srcIdx = static_cast<const unsigned short*>(e.indices);
            // Remap the source-local u16 indices to be RELATIVE to the group's
            // vertex slice (drawVertexPrimitiveList interprets each index as a
            // vertex offset from the passed vertex pointer, vbase + grp.vertexBase).
            // This entry's first source vertex sits at (vd - vbase) in the
            // scratch, so the entry-local offset = (vd - vbase) - grp.vertexBase.
            const int entryBase = static_cast<int>(vd - vbase) - grp.vertexBase;
            for (unsigned int ix = 0; ix < e.indexCount; ++ix)
                id[ix] = static_cast<unsigned short>(entryBase + srcIdx[ix]);
            vd += e.vertexCount;
            id += e.indexCount;
        }
    }

    irr::core::matrix4 identity;
    int drawCalls = 0;
    for (int g : order_) {
        const Group& grp = groups_[g];
        if (!grp.valid) continue;
        driver->setTransform(irr::video::ETS_WORLD, identity);
        driver->setMaterial(grp.material);
        driver->drawVertexPrimitiveList(
            vbase + grp.vertexBase,
            grp.vertexCount,
            ibase + grp.indexBase,
            grp.indexCount / 3,
            irr::video::EVT_STANDARD,
            irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);
        if (outVerticesDrawn) *outVerticesDrawn += grp.vertexCount;
        ++drawCalls;
    }
    return drawCalls;
}

} // namespace Graphics
} // namespace EQT
