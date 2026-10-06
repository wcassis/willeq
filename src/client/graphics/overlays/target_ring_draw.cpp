/*
 * The 3D target ring's ONE draw (B994): the overlay and the capture tool
 * (tools/hwcaps/target_ring_capture.cpp) both go through it, so the pixels
 * the tool shows are the client's. The caller has set ETS_VIEW / ETS_PROJECTION.
 *
 * Additive=1 in TargetIndicator.ini -> EMT_TRANSPARENT_ADD_COLOR, the GLES2
 * driver's GL_ONE, GL_ONE blend with depth write off; no texture (R3 in
 * target_ring.h), so the driver draws the vertex colours. Depth-tested, so
 * the floor in front of the ring hides it, like any world geometry.
 */

#include "client/graphics/overlays/target_ring.h"
#include "client/graphics/overlays/target_ring_draw.h"

#include <algorithm>

#include "client/con_tier.h"
#include "client/core/memory_ledger.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/overlays/overlays_component.h"

namespace EQT {
namespace Graphics {

// B994: the overlay's per-frame entry, called right after drawTargetOutline()
// in FrameComponent's render phase (tests/goldens/frame_render_order_g05.txt).
void OverlaysComponent::drawTargetRing() {
    const bool optionOn = targetRingEnabled_;
    if (!optionOn || currentTargetId_ == 0 || !ctx_.entityRenderer || !ctx_.driver) return;
    if (!ctx_.Frame().have3DTransforms()) return;  // the outline's own precondition

    const auto& entities = ctx_.entityRenderer->getEntities();
    const auto it = entities.find(currentTargetId_);
    const bool found = it != entities.end();
    irr::scene::ISceneNode* node = found ? it->second.sceneNode : nullptr;
    if (!TargetRing::shouldDraw(optionOn, currentTargetId_, found && node != nullptr,
                                node && node->isVisible())) {
        return;
    }

    // The feet: the node's own position on the floor of its world box; the
    // footprint: half the larger horizontal extent of that box (R1).
    const irr::core::aabbox3df box = node->getTransformedBoundingBox();
    const irr::core::vector3df pos = node->getAbsolutePosition();
    const irr::core::vector3df ext = box.getExtent();
    const float footprint = 0.5f * std::max(ext.X, ext.Z);
    const eqt::ConTier tier =
        eqt::ConTierForLevels(ctx_.entityRenderer->getPlayerLevel(), currentTargetLevel_);
    {
        // The ring's two vectors are the only memory it owns: ~11 KiB,
        // reserved once, charged to the ui row.
        eqt::memory::MemoryScope scope(eqt::memory::Category::UI);
        TargetRing::build(TargetRing::styleFor(TargetRing::sectionForCon(tier)), pos.X,
                          box.MinEdge.Y, pos.Z, footprint, targetRingGeometry_);
    }

    // After drawAll() the driver may hold 2D matrices; the outline restores
    // the captured 3D pair the same way.
    ctx_.driver->setTransform(irr::video::ETS_VIEW, ctx_.captured3DView);
    ctx_.driver->setTransform(irr::video::ETS_PROJECTION, ctx_.captured3DProj);
    TargetRing::drawGeometry(ctx_.driver, targetRingGeometry_);
    ++targetRingDrawCount_;
}

namespace TargetRing {

void drawGeometry(irr::video::IVideoDriver* driver, const Geometry& g) {
    if (!driver || g.vertices.empty() || g.indices.empty()) return;
    irr::video::SMaterial m;
    m.Lighting = false;
    m.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
    m.ZBuffer = irr::video::ECFN_LESSEQUAL;
    m.ZWriteEnable = false;
    m.BackfaceCulling = false;
    m.FogEnable = false;
    driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    driver->setMaterial(m);
    driver->drawVertexPrimitiveList(g.vertices.data(), static_cast<irr::u32>(g.vertices.size()),
                                    g.indices.data(), static_cast<irr::u32>(g.indices.size() / 3),
                                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                                    irr::video::EIT_16BIT);
}

}  // namespace TargetRing
}  // namespace Graphics
}  // namespace EQT
