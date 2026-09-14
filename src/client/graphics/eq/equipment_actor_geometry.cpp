#include "client/graphics/eq/equipment_actor_geometry.h"

#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/eq/wld_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace EQT {
namespace Graphics {

namespace {

std::shared_ptr<ZoneGeometry> meshForReference(const WldLoader& wld, uint32_t ref) {
    const auto& refs = wld.getModelRefs();
    auto it = refs.find(ref);
    if (it == refs.end() || it->second.geometryFragRef == 0) return nullptr;
    return wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
}

// Appends `src` to `out`, centre first, then `xf` (EQ space), remapping its
// texture indices into `out`'s own texture list.
void appendMesh(ZoneGeometry& out, const ZoneGeometry& src, const BoneMat4& xf) {
    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
    for (const Vertex3D& v : src.vertices) {
        const float px = v.x + src.centerX, py = v.y + src.centerY, pz = v.z + src.centerZ;
        Vertex3D w = v;
        w.x = xf.m[0] * px + xf.m[4] * py + xf.m[8] * pz + xf.m[12];
        w.y = xf.m[1] * px + xf.m[5] * py + xf.m[9] * pz + xf.m[13];
        w.z = xf.m[2] * px + xf.m[6] * py + xf.m[10] * pz + xf.m[14];
        float nx = xf.m[0] * v.nx + xf.m[4] * v.ny + xf.m[8] * v.nz;
        float ny = xf.m[1] * v.nx + xf.m[5] * v.ny + xf.m[9] * v.nz;
        float nz = xf.m[2] * v.nx + xf.m[6] * v.ny + xf.m[10] * v.nz;
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
