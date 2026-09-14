#include "client/graphics/eq/equipment_actor_geometry.h"

#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/eq/wld_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace EQT {
namespace Graphics {

namespace {

std::shared_ptr<ZoneGeometry> meshForReference(const WldLoader& wld, uint32_t ref) {
    const auto& refs = wld.getModelRefs();
    auto it = refs.find(ref);
    if (it == refs.end() || it->second.geometryFragRef == 0) return nullptr;
    return wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
}

// True when `src`'s vertex pieces cover its vertices exactly and every piece
// names a bone of a skeleton with `boneCount` bones.
bool piecesFit(const ZoneGeometry& src, size_t boneCount) {
    if (src.vertexPieces.empty()) return false;
    size_t covered = 0;
    for (const VertexPiece& p : src.vertexPieces) {
        if (p.boneIndex >= boneCount) return false;
        covered += p.count;
    }
    return covered == src.vertices.size();
}

// Appends `src` to `out`, centre first, then a transform (EQ space), remapping
// its texture indices into `out`'s own texture list. The transform is `xf` for
// every vertex, unless `boneWorld` is given and `src`'s vertex pieces fit it:
// then each piece's vertices take the rest transform of the bone it names
// (T476, B660 -- the skinning SkeletalAnimator applies, at the rest pose).
void appendMesh(ZoneGeometry& out, const ZoneGeometry& src, const BoneMat4& xf,
                const std::vector<BoneMat4>* boneWorld = nullptr) {
    const bool skinned = boneWorld && piecesFit(src, boneWorld->size());
    std::vector<const BoneMat4*> perVertex(src.vertices.size(), &xf);
    if (skinned) {
        size_t v = 0;
        for (const VertexPiece& p : src.vertexPieces) {
            for (int k = 0; k < p.count; ++k) perVertex[v++] = &(*boneWorld)[p.boneIndex];
        }
    }

    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
    for (size_t i = 0; i < src.vertices.size(); ++i) {
        const Vertex3D& v = src.vertices[i];
        const BoneMat4& m = *perVertex[i];
        const float px = v.x + src.centerX, py = v.y + src.centerY, pz = v.z + src.centerZ;
        Vertex3D w = v;
        w.x = m.m[0] * px + m.m[4] * py + m.m[8] * pz + m.m[12];
        w.y = m.m[1] * px + m.m[5] * py + m.m[9] * pz + m.m[13];
        w.z = m.m[2] * px + m.m[6] * py + m.m[10] * pz + m.m[14];
        float nx = m.m[0] * v.nx + m.m[4] * v.ny + m.m[8] * v.nz;
        float ny = m.m[1] * v.nx + m.m[5] * v.ny + m.m[9] * v.nz;
        float nz = m.m[2] * v.nx + m.m[6] * v.ny + m.m[10] * v.nz;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
        w.nx = nx; w.ny = ny; w.nz = nz;
        out.vertices.push_back(w);
    }

    ResolvedMaterialList& dst = out.mutableMaterialData();
    const auto& names = src.textureNames();
    std::vector<uint32_t> remap(names.size(), 0);
    for (size_t i = 0; i < names.size(); ++i) {
        auto found = std::find(dst.textureNames.begin(), dst.textureNames.end(), names[i]);
        if (found != dst.textureNames.end()) {
            remap[i] = static_cast<uint32_t>(found - dst.textureNames.begin());
            continue;
        }
        remap[i] = static_cast<uint32_t>(dst.textureNames.size());
        dst.textureNames.push_back(names[i]);
        dst.textureInvisible.push_back(i < src.textureInvisible().size() && src.textureInvisible()[i]);
        dst.textureMasked.push_back(i < src.textureMasked().size() && src.textureMasked()[i]);
        dst.textureAnimations.push_back(i < src.textureAnimations().size() ? src.textureAnimations()[i]
                                                                           : TextureAnimationInfo{});
    }

    for (Triangle t : src.triangles) {
        t.v1 += base;
        t.v2 += base;
        t.v3 += base;
        if (t.textureIndex < remap.size()) t.textureIndex = remap[t.textureIndex];
        out.triangles.push_back(t);
    }
}

BoneMat4 restTransform(const SkeletonBone& bone) {
    if (!bone.orientation) return BoneMat4::identity();
    const BoneOrientation& o = *bone.orientation;
    const float s = (o.scale > 0.0f) ? o.scale : 1.0f;
    return BoneMat4::translate(o.shiftX, o.shiftY, o.shiftZ) *
           BoneMat4::fromQuaternion(o.quatX, o.quatY, o.quatZ, o.quatW) * BoneMat4::scale(s);
}

void appendBone(ZoneGeometry& out, const WldLoader& wld, const SkeletonBone& bone, const BoneMat4& parent) {
    const BoneMat4 world = parent * restTransform(bone);
    if (bone.modelRef > 0) {
        if (auto mesh = meshForReference(wld, bone.modelRef)) appendMesh(out, *mesh, world);
    }
    for (const auto& child : bone.children) {
        if (child) appendBone(out, wld, *child, world);
    }
}

// Every bone's accumulated rest transform, indexed like allBones (the index a
// vertex piece names), walked from the roots the same way appendBone() walks.
std::vector<BoneMat4> restWorldTransforms(const SkeletonTrack& track) {
    std::unordered_map<const SkeletonBone*, size_t> index;
    for (size_t i = 0; i < track.allBones.size(); ++i) index[track.allBones[i].get()] = i;
    std::vector<BoneMat4> world(track.allBones.size(), BoneMat4::identity());
    std::vector<std::pair<const SkeletonBone*, BoneMat4>> stack;
    for (const auto& root : track.bones) {
        if (root) stack.push_back({root.get(), BoneMat4::identity()});
    }
    size_t guard = 0;
    while (!stack.empty() && guard++ < 100000) {
        auto [bone, parent] = stack.back();
        stack.pop_back();
        const BoneMat4 w = parent * restTransform(*bone);
        auto it = index.find(bone);
        if (it != index.end()) world[it->second] = w;
        for (const auto& child : bone->children) {
            if (child) stack.push_back({child.get(), w});
        }
    }
    return world;
}

}  // namespace

std::shared_ptr<ZoneGeometry> resolveEquipmentActorGeometry(const WldLoader& wld,
                                                            const std::string& actorName) {
    auto out = std::make_shared<ZoneGeometry>();
    out->name = actorName;

    const auto& defs = wld.getObjectDefs();
    auto def = defs.find(actorName);
    if (def != defs.end()) {
        for (uint32_t ref : def->second.meshRefs) {
            if (auto mesh = meshForReference(wld, ref)) {
                appendMesh(*out, *mesh, BoneMat4::identity());
                continue;
            }
            auto skelRef = wld.getSkeletonRefs().find(ref);
            if (skelRef == wld.getSkeletonRefs().end()) continue;
            auto track = wld.getSkeletonTracks().find(skelRef->second);
            if (track == wld.getSkeletonTracks().end() || !track->second) continue;
            for (const auto& root : track->second->bones) {
                if (root) appendBone(*out, wld, *root, BoneMat4::identity());
            }
            // T476 (B660): the skeleton's own skinned meshes (0x10 flag 0x200).
            if (!track->second->meshRefs.empty()) {
                const std::vector<BoneMat4> boneWorld = restWorldTransforms(*track->second);
                for (uint32_t meshRef : track->second->meshRefs) {
                    if (auto mesh = meshForReference(wld, meshRef)) {
                        appendMesh(*out, *mesh, BoneMat4::identity(), &boneWorld);
                    }
                }
            }
        }
    }

    if (out->vertices.empty()) {
        // The pre-T472 rule, kept as the fallback: any mesh named "<actor>_...".
        const std::string prefix = actorName + "_";
        for (const auto& geom : wld.getGeometries()) {
            if (!geom || geom->name.empty()) continue;
            std::string upper = geom->name;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (upper.find(prefix) != std::string::npos) appendMesh(*out, *geom, BoneMat4::identity());
        }
    }

    if (out->vertices.empty()) return nullptr;

    out->minX = out->minY = out->minZ = std::numeric_limits<float>::max();
    out->maxX = out->maxY = out->maxZ = std::numeric_limits<float>::lowest();
    for (const Vertex3D& v : out->vertices) {
        out->minX = std::min(out->minX, v.x); out->maxX = std::max(out->maxX, v.x);
        out->minY = std::min(out->minY, v.y); out->maxY = std::max(out->maxY, v.y);
        out->minZ = std::min(out->minZ, v.z); out->maxZ = std::max(out->maxZ, v.z);
    }
    return out;
}

}  // namespace Graphics
}  // namespace EQT
