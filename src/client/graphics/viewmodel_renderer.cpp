/**
 * T296 -- first-person viewmodel pass, stage 1.
 *
 * See include/client/graphics/viewmodel_transform.h for the frame algebra
 * and docs/decisions.md (2026-08-27) for why this exists at all.
 *
 * Shape (verified against the renderer before it was built, see the plan's
 * Review): an explicit-transform manual draw after smgr_->drawAll(), NOT a
 * second camera or a second scene manager. Three reasons, all from this
 * codebase:
 *   1. Manual "setTransform + setMaterial + drawMeshBuffer" after drawAll()
 *      is already the established idiom here (renderer_zone_draw.cpp's
 *      front-to-back zone draw), and EQAnimatedMeshSceneNode::render() is
 *      itself exactly that loop -- so the pass reuses a known-good path.
 *   2. A second ICameraSceneNode would need a second smgr_->drawAll() (a
 *      full re-traversal of every scene node, the frame's dominant cost) or
 *      a mid-frame active-camera swap; both are far more invasive than
 *      three setTransform() calls.
 *   3. The GLES2 driver has no clearZBuffer() override (COpenGLES2Driver
 *      extends CNullDriver and does not implement it), so the depth clear is
 *      a raw glClear(GL_DEPTH_BUFFER_BIT) -- raw GL for exactly this kind of
 *      state work is already the pattern here (renderer_zone_draw.cpp's
 *      stencil portal code, renderer_overlays.cpp).
 */

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/viewmodel_transform.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "common/logging.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#elif defined(EQT_HAS_DRM)
#include <GL/gl.h>
#endif
#if defined(EQT_HAS_GLES2) || defined(EQT_HAS_DRM)
#define EQT_VIEWMODEL_HAS_RAW_GL 1
#endif

#ifdef EQT_HAS_GRAPHICS

namespace EQT {
namespace Graphics {

// Defined here rather than in entity_renderer.cpp to keep the viewmodel
// pass's code in one place (and off an already-oversized TU).
bool EntityRenderer::getPlayerViewmodelParts(ViewmodelParts& out) const {
    for (const auto& [spawnId, visual] : entities_) {
        if (!visual.isPlayer) continue;
        if (!visual.isFirstPersonMode) return false;
        if (!visual.animatedNode) return false;
        // The T74 fallback: a skeleton with no usable arm bones hides the
        // player entirely rather than showing an unfiltered body. Nothing
        // for the viewmodel pass to draw in that case.
        if (!visual.animatedNode->hasValidFirstPersonGeometry()) return false;
        out.animatedNode = visual.animatedNode;
        out.primaryEquipNode = visual.primaryEquipNode;
        out.secondaryEquipNode = visual.secondaryEquipNode;
        return true;
    }
    return false;
}

ViewmodelPlacement computeViewmodelPlacement(
    const irr::core::aabbox3d<irr::f32>& armBoxBaked,
    float headingY,
    float fovYRadians,
    float aspect,
    const ViewmodelTuning& tuning)
{
    ViewmodelPlacement out;

    // baked -> viewmodel orientation is a pure Y rotation by the node's own
    // heading (RotY(-90) * RotY(h + 90) == RotY(h)); see the header.
    irr::core::matrix4 armRot;
    armRot.setRotationDegrees(irr::core::vector3df(0.0f, headingY, 0.0f));

    irr::core::aabbox3d<irr::f32> box = armBoxBaked;
    armRot.transformBoxEx(box);

    const irr::core::vector3df centre = box.getCenter();
    const irr::core::vector3df extent = box.getExtent() * 0.5f;
    if (extent.X <= 0.0f && extent.Y <= 0.0f && extent.Z <= 0.0f) {
        return out;  // degenerate geometry -- draw nothing rather than garbage
    }

    const float fill = std::max(0.05f, tuning.fillFraction);
    const float tanY = std::tan(fovYRadians * 0.5f);
    const float tanX = tanY * std::max(0.01f, aspect);

    // Distance at which the arm box fills `fill` of the frame in whichever
    // axis is the binding one. The box's NEAR face is what binds, not its
    // centre -- a box centred at D with half-depth eZ has its widest
    // apparent extent at (D - eZ), so solving at the centre lets the arms
    // spill off the edges of the frame (measured: NDC X reached +/-1.73).
    float distance = std::max(extent.X / (fill * tanX), extent.Y / (fill * tanY))
                     + extent.Z;
    // ...but never so close that the box pokes through the pass's near plane.
    // This is the whole point of the unit: T76 measured the in-world arms
    // spanning Z=[-1.06, 1.11] with the eye at 0, i.e. partly BEHIND the
    // camera. Here the box's near face is pushed to nearPlane + minClearance
    // by construction and the tests assert it.
    distance = std::max(distance, extent.Z + tuning.nearPlane + tuning.minClearance);

    // Vertical bias is expressed as a fraction of the SLACK left over after
    // the fit, measured on the box's near face (the tightest depth), so it
    // can never push the arms off the bottom of the frame no matter how the
    // fill fraction is tuned.
    const float nearFaceDepth = std::max(tuning.nearPlane, distance - extent.Z);
    const float slackY = std::max(0.0f, tanY * nearFaceDepth - extent.Y);
    const irr::core::vector3df target(
        0.0f,
        std::max(-1.0f, std::min(1.0f, tuning.verticalBias)) * slackY,
        distance);
    const irr::core::vector3df offset = target - centre;

    out.armWorld = armRot;
    out.armWorld.setTranslation(offset);

    out.nodeLocalToViewmodel.setRotationDegrees(
        irr::core::vector3df(0.0f, kViewmodelNodeLocalYawDegrees, 0.0f));
    out.nodeLocalToViewmodel.setTranslation(offset);

    // Eye at the origin looking down +Z: the viewmodel's view matrix is the
    // identity by construction, which is what makes the offset above read
    // directly as "in front of / below the camera".
    out.view = irr::core::matrix4();
    out.projection.buildProjectionMatrixPerspectiveFovLH(
        fovYRadians, std::max(0.01f, aspect), tuning.nearPlane, tuning.farPlane);

    out.placedArmBox = box;
    out.placedArmBox.MinEdge += offset;
    out.placedArmBox.MaxEdge += offset;
    out.valid = true;
    return out;
}

namespace {

void appendMeshBuffers(irr::scene::IMesh* mesh,
                       const irr::core::matrix4& world,
                       irr::scene::ISceneNode* materialSource,
                       std::vector<ViewmodelDrawItem>& out)
{
    if (!mesh) return;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getIndexCount() == 0) continue;
        ViewmodelDrawItem item;
        item.world = world;
        item.buffer = mb;
        item.material = (materialSource && i < materialSource->getMaterialCount())
                            ? materialSource->getMaterial(i)
                            : mb->getMaterial();
        // The pass owns its own depth buffer (it is cleared first), so the
        // viewmodel must both test and write depth against itself -- that is
        // what keeps a weapon correctly occluded by the hand holding it.
        item.material.ZBuffer = irr::video::ECFN_LESSEQUAL;
        item.material.ZWriteEnable = true;
        out.push_back(item);
    }
}

}  // namespace

size_t buildViewmodelDrawList(const EntityRenderer::ViewmodelParts& parts,
                              const ViewmodelPlacement& placement,
                              std::vector<ViewmodelDrawItem>& out)
{
    out.clear();
    if (!placement.valid || !parts.animatedNode) return 0;

    appendMeshBuffers(parts.animatedNode->getInstanceMesh(), placement.armWorld,
                      parts.animatedNode, out);

    // Equipment nodes are children of the player node and live in its LOCAL
    // frame (updateEquipmentTransforms() writes their relative transform from
    // the hand bone every frame, whether or not they are visible), so they
    // compose straight onto nodeLocalToViewmodel.
    irr::scene::IMeshSceneNode* equips[2] = {parts.primaryEquipNode,
                                             parts.secondaryEquipNode};
    for (irr::scene::IMeshSceneNode* equip : equips) {
        if (!equip) continue;
        appendMeshBuffers(equip->getMesh(),
                          placement.nodeLocalToViewmodel * equip->getRelativeTransformation(),
                          equip, out);
    }
    return out.size();
}

irr::core::aabbox3d<irr::f32> measureIndexedBounds(irr::scene::IMesh* mesh) {
    irr::core::aabbox3d<irr::f32> box;
    bool first = true;
    if (!mesh) return box;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
        if (!mb) continue;
        const irr::u16* idx = mb->getIndices();
        const irr::u32 icount = mb->getIndexCount();
        if (!idx || icount == 0) continue;
        for (irr::u32 i = 0; i < icount; ++i) {
            const irr::core::vector3df& p = mb->getPosition(idx[i]);
            if (first) { box.reset(p); first = false; } else { box.addInternalPoint(p); }
        }
    }
    return box;
}

// ---------------------------------------------------------------------------

void IrrlichtRenderer::renderFirstPersonViewmodel() {
    lastViewmodelDrawCallCount_ = 0;
    if (cameraMode_ != CameraMode::FirstPerson) {
        viewmodelArmBoxValid_ = false;
        return;
    }
    if (!driver_ || !entityRenderer_ || !camera_) return;

    EntityRenderer::ViewmodelParts parts;
    if (!entityRenderer_->getPlayerViewmodelParts(parts)) {
        viewmodelArmBoxValid_ = false;
        return;
    }

    irr::scene::SMesh* instanceMesh = parts.animatedNode->getInstanceMesh();
    if (!instanceMesh) return;

    // The node's own bounding box covers ALL its vertices, including the
    // torso/legs the T74 index filter dropped -- the filter rewrites index
    // buffers, never vertex arrays. Measuring the INDEXED bounds is what
    // gives the arms' real extent. Cached across the first-person session so
    // the framing does not breathe as the arms animate (stable framing is a
    // stage-1 requirement; pose-tracking framing would be stage-2 polish).
    if (!viewmodelArmBoxValid_) {
        irr::core::aabbox3d<irr::f32> measured = measureIndexedBounds(instanceMesh);
        if (measured.MaxEdge == measured.MinEdge) return;  // nothing filtered in yet
        viewmodelArmBoxBaked_ = measured;
        viewmodelArmBoxHeadingY_ = parts.animatedNode->getRotation().Y;
        viewmodelArmBoxValid_ = true;
        LOG_DEBUG(MOD_GRAPHICS,
                  "Viewmodel: cached arm box baked=[{:.2f},{:.2f},{:.2f}]..[{:.2f},{:.2f},{:.2f}] headingY={:.1f}",
                  measured.MinEdge.X, measured.MinEdge.Y, measured.MinEdge.Z,
                  measured.MaxEdge.X, measured.MaxEdge.Y, measured.MaxEdge.Z,
                  viewmodelArmBoxHeadingY_);
    }

    // Re-express the cached box in the CURRENT frame's baked orientation: the
    // box was measured at heading A, the arms are now baked at heading B, and
    // baked frames differ by exactly RotY(B - A).
    const float headingY = parts.animatedNode->getRotation().Y;
    irr::core::matrix4 reframe;
    reframe.setRotationDegrees(
        irr::core::vector3df(0.0f, headingY - viewmodelArmBoxHeadingY_, 0.0f));
    irr::core::aabbox3d<irr::f32> boxNow = viewmodelArmBoxBaked_;
    reframe.transformBoxEx(boxNow);

    const irr::core::dimension2du screen = driver_->getScreenSize();
    const float aspect = screen.Height > 0
        ? static_cast<float>(screen.Width) / static_cast<float>(screen.Height)
        : 4.0f / 3.0f;
    const float fovY = camera_->getFOV();

    ViewmodelPlacement placement =
        computeViewmodelPlacement(boxNow, headingY, fovY, aspect, viewmodelTuning_);
    if (!placement.valid) return;

    std::vector<ViewmodelDrawItem> items;
    if (buildViewmodelDrawList(parts, placement, items) == 0) return;

    // The viewmodel must never be clipped by world geometry, and must not
    // leave its own depth in the buffer for anything that draws later.
    // COpenGLES2Driver has no clearZBuffer() of its own, so clear directly;
    // glClear ignores the depth buffer unless depth writes are enabled.
#ifdef EQT_VIEWMODEL_HAS_RAW_GL
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
#else
#error "T296 viewmodel pass needs a raw GL header for the mid-frame depth clear"
#endif

    driver_->setTransform(irr::video::ETS_VIEW, placement.view);
    driver_->setTransform(irr::video::ETS_PROJECTION, placement.projection);
    for (const auto& item : items) {
        driver_->setTransform(irr::video::ETS_WORLD, item.world);
        driver_->setMaterial(item.material);
        driver_->drawMeshBuffer(item.buffer);
    }
    lastViewmodelDrawCallCount_ = items.size();

    // Hand the driver back the world's transforms; several later stages
    // (portal/frustum debug overlays, casting bars) assume them.
    driver_->setTransform(irr::video::ETS_VIEW, captured3DView_);
    driver_->setTransform(irr::video::ETS_PROJECTION, captured3DProj_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    // The viewmodel's own depth is deliberately LEFT in the buffer. Nothing
    // 3D draws after this point in a normal frame (world, entities,
    // particles, weather are all done; what follows is opt-in debug overlays
    // and 2D UI), and a second clear here would destroy the WORLD's depth
    // for those overlays -- a strictly larger behaviour change than leaving
    // a viewmodel-shaped region of near depth behind.
}

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_HAS_GRAPHICS
