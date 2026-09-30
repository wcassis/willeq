// placed_object_meshes.cpp -- see include/client/graphics/placed_object_meshes.h.
//
// B936: the binding moved here verbatim from loadZoneStep6_Objects() and the
// pose loop verbatim from SimBridgeComponent::applySimulationOutput(), so the
// arithmetic is the one fable ran; only who calls it (once per geometry, not
// once per placement) changed. The mesh table is T504's per-geometry cache
// from the same step, without the animated exclusion.

#include "client/graphics/placed_object_meshes.h"

#include "client/graphics/eq/wld_loader.h"

#include <cmath>
#include <cstdint>

namespace EQT {
namespace Graphics {

bool isVertexAnimatedGeometry(const ZoneGeometry& geom) {
    return geom.animatedVertices && geom.animatedVertices->frames.size() >= 2;
}

size_t bindVertexAnimation(const ZoneGeometry& geom, irr::scene::IMesh* mesh,
                           VertexAnimatedMesh& vam) {
    vam.animData = geom.animatedVertices;
    vam.vertexMapping.clear();
    vam.centerOffsetX = vam.centerOffsetY = vam.centerOffsetZ = 0.0f;
    if (!mesh || !vam.animData || vam.animData->frames.empty()) return 0;

    // Centre offset: geometry vertices have the centre baked in, animation
    // frame positions are raw. Offset = geomVertex - animFrame0.
    const auto& frame0 = vam.animData->frames[0];
    const size_t expectedVerts = frame0.positions.size() / 3;
    if (!geom.vertices.empty() && expectedVerts > 0) {
        const auto& gv = geom.vertices[0];
        vam.centerOffsetX = gv.x - frame0.positions[0];
        vam.centerOffsetY = gv.y - frame0.positions[1];
        vam.centerOffsetZ = gv.z - frame0.positions[2];
    }

    // For each mesh-buffer vertex, the geometry vertex index (= animation
    // vertex index) at the same position (mesh stores (x, z, y) of geometry
    // (x, y, z)).
    size_t mapped = 0;
    vam.vertexMapping.resize(mesh->getMeshBufferCount());
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
        auto* vertices = static_cast<irr::video::S3DVertex*>(buf->getVertices());
        const irr::u32 vertCount = buf->getVertexCount();
        vam.vertexMapping[b].resize(vertCount, SIZE_MAX);
        for (irr::u32 v = 0; v < vertCount; ++v) {
            const float mx = vertices[v].Pos.X;
            const float my = vertices[v].Pos.Y;  // = geom z
            const float mz = vertices[v].Pos.Z;  // = geom y
            for (size_t gi = 0; gi < geom.vertices.size(); ++gi) {
                const auto& gv = geom.vertices[gi];
                if (std::abs(gv.x - mx) < 0.01f && std::abs(gv.z - my) < 0.01f &&
                    std::abs(gv.y - mz) < 0.01f) {
                    if (gi < expectedVerts) {
                        vam.vertexMapping[b][v] = gi;
                        ++mapped;
                    }
                    break;
                }
            }
        }
    }
    return mapped;
}

void applyVertexAnimFrame(VertexAnimatedMesh& vam, int frame) {
    if (!vam.mesh || !vam.animData || frame < 0 ||
        static_cast<size_t>(frame) >= vam.animData->frames.size())
        return;
    vam.currentFrame = frame;
    const VertexAnimFrame& kf = vam.animData->frames[static_cast<size_t>(frame)];
    const size_t expectedVerts = kf.positions.size() / 3;
    for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* buffer = vam.mesh->getMeshBuffer(b);
        auto* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
        const irr::u32 vertexCount = buffer->getVertexCount();
        if (b >= vam.vertexMapping.size() || vam.vertexMapping[b].size() != vertexCount) continue;
        for (irr::u32 v = 0; v < vertexCount; ++v) {
            const size_t animIdx = vam.vertexMapping[b][v];
            if (animIdx == SIZE_MAX || animIdx >= expectedVerts) continue;
            const float eqX = kf.positions[animIdx * 3 + 0] + vam.centerOffsetX;
            const float eqY = kf.positions[animIdx * 3 + 1] + vam.centerOffsetY;
            const float eqZ = kf.positions[animIdx * 3 + 2] + vam.centerOffsetZ;
            vertices[v].Pos.X = eqX;
            vertices[v].Pos.Y = eqZ;
            vertices[v].Pos.Z = eqY;
        }
        buffer->setDirty(irr::scene::EBT_VERTEX);
    }
}

irr::scene::IMesh* PlacedObjectMeshes::acquire(const ZoneGeometry& geom,
                                               const std::function<irr::scene::IMesh*()>& build) {
    if (share_) {
        auto it = byGeometry_.find(&geom);
        if (it != byGeometry_.end()) {
            it->second->grab();   // the caller's reference
            ++reused_;
            return it->second;
        }
    }
    irr::scene::IMesh* mesh = build();
    if (!mesh) return nullptr;
    ++built_;
    if (share_) {
        mesh->grab();   // the table's reference, dropped by release()
        byGeometry_[&geom] = mesh;
    }
    return mesh;
}

VertexAnimatedMesh* PlacedObjectMeshes::registerAnimated(const ZoneGeometry& geom, irr::scene::IMesh* mesh,
                                                         const std::string& name,
                                                         std::vector<VertexAnimatedMesh>& out,
                                                         size_t* mapped) {
    if (!mesh) return nullptr;
    ++animatedPlacements_;
    auto it = animatedIndex_.find(mesh);
    if (it != animatedIndex_.end() && it->second < out.size() && out[it->second].mesh == mesh) {
        ++out[it->second].placements;
        return nullptr;
    }
    VertexAnimatedMesh vam;
    vam.mesh = mesh;
    vam.placements = 1;
    vam.objectName = name;
    const size_t m = bindVertexAnimation(geom, mesh, vam);
    if (mapped) *mapped = m;
    animatedIndex_[mesh] = out.size();
    out.push_back(std::move(vam));
    return &out.back();
}

void PlacedObjectMeshes::release() {
    for (auto& [geom, mesh] : byGeometry_) mesh->drop();
    byGeometry_.clear();
}

}  // namespace Graphics
}  // namespace EQT
