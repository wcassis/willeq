// B1148 (#468): the corpse-fade list's model alpha -- see corpse_fade.h.

#include "client/graphics/corpse_fade.h"

#include "client/graphics/eq/animated_mesh_scene_node.h"

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

namespace {

irr::video::E_MATERIAL_TYPE fadeTypeFor(irr::s32 t, const CorpseFadeMaterials& m) {
    if (t == m.fadeSolid || t == m.fadeAlphaTest || t == m.fadeAlphaTestRGBA ||
        t == irr::video::EMT_TRANSPARENT_VERTEX_ALPHA)
        return static_cast<irr::video::E_MATERIAL_TYPE>(t);  // already blended
    if (m.fadeSolid >= 0 && t == m.solid) return static_cast<irr::video::E_MATERIAL_TYPE>(m.fadeSolid);
    if (m.fadeAlphaTest >= 0 && t == m.alphaTest) return static_cast<irr::video::E_MATERIAL_TYPE>(m.fadeAlphaTest);
    if (m.fadeAlphaTestRGBA >= 0 && t == m.alphaTestRGBA)
        return static_cast<irr::video::E_MATERIAL_TYPE>(m.fadeAlphaTestRGBA);
    // No shader twin (the built-in path, or a type this map does not know):
    // the driver's Solid3D multiplies texture alpha x companion x vertex alpha.
    return irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;
}

}  // namespace

bool ApplyCorpseFadeAlpha(EQAnimatedMeshSceneNode* node, float alpha, const CorpseFadeMaterials& mats) {
    if (!node) return false;
    const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    const irr::u32 byte = static_cast<irr::u32>(a * 255.0f + 0.5f);
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        irr::video::SMaterial& mat = node->getMaterial(i);
        mat.MaterialType = fadeTypeFor(static_cast<irr::s32>(mat.MaterialType), mats);
    }
    irr::scene::SMesh* mesh = node->getInstanceMesh();
    if (!mesh) return true;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
        if (!mb || mb->getVertexType() != irr::video::EVT_STANDARD) continue;
        auto* v = static_cast<irr::video::S3DVertex*>(mb->getVertices());
        for (irr::u32 k = 0; k < mb->getVertexCount(); ++k) v[k].Color.setAlpha(byte);
        mb->setDirty(irr::scene::EBT_VERTEX);
    }
    return true;
}

}  // namespace Graphics
}  // namespace EQT
