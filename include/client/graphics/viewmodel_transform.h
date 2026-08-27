#pragma once

/**
 * T296 -- first-person viewmodel pass, stage 1: the placement math.
 *
 * Four previous units (T71/T74/T75/T76) rendered the IN-WORLD player model
 * seen from inside the head. T76 measured why that cannot work: in camera
 * space the arm-filtered geometry STRADDLES the near plane in every pose
 * (idle Z=[-1.06, 1.11], 1H-slash Z=[-1.02, 2.18]) -- part of it is behind
 * the eye -- and no camera pullback clears a ~2-3 unit depth span. Will's
 * 2026-08-27 decision: draw the arms as a real viewmodel instead, at a
 * DESIGNED offset in view space with the pass's own projection.
 *
 * This header holds the pure, driver-free math so it can be exercised
 * headlessly (tests/test_viewmodel_pass.cpp). The frame relationships it
 * encodes were derived from applyBoneTransformsToMeshImpl() and confirmed by
 * measurement against real S3D skeletons:
 *
 *   - The player's per-instance mesh is baked with an extra rotation of
 *     (nodeRotationY + 90) degrees about the EQ Z axis, then swapped to
 *     Irrlicht Y-up. In Irrlicht terms that is a Y rotation of
 *     -(h + 90) applied to the node-local frame:
 *         baked      = RotY(-(h + 90)) * nodeLocal
 *   - Equipment nodes are CHILDREN of the player node and carry no such
 *     baking, so they live in nodeLocal directly (their relative transform).
 *   - The character faces +X in nodeLocal (verified from the humbi_r /
 *     humbi_l shoulder bones: right shoulder at Irrlicht Z=-0.60, left at
 *     Z=+0.62, so the lateral axis is Z and forward is +X).
 *
 * The viewmodel's own view space has the eye at the origin looking down +Z,
 * so nodeLocal is rotated by -90 degrees about Y to point the character
 * forward at the viewer's forward. Composing the two gives the pleasantly
 * simple arm transform used below:
 *
 *     viewmodel = offset + RotY(-90)      * nodeLocal      (equipment)
 *     viewmodel = offset + RotY(h)        * baked          (arms)
 *
 * because RotY(-90) * RotY(h + 90) == RotY(h).
 */

#include <vector>
#include <cstddef>

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#include "client/graphics/viewmodel_tuning.h"
#include "client/graphics/entity_renderer.h"

namespace EQT {
namespace Graphics {

/// Rotation (degrees about Y) taking the player node's local frame into
/// viewmodel view space. The character faces +X in node-local space; the
/// viewmodel camera looks down +Z.
inline constexpr float kViewmodelNodeLocalYawDegrees = -90.0f;

struct ViewmodelPlacement {
    bool valid = false;
    irr::core::matrix4 view;                 ///< identity: eye at origin, looking +Z
    irr::core::matrix4 projection;           ///< the pass's own perspective matrix
    irr::core::matrix4 nodeLocalToViewmodel; ///< T(offset) * RotY(-90)
    irr::core::matrix4 armWorld;             ///< T(offset) * RotY(headingY)
    /// The arm box after placement, in viewmodel view space -- what the
    /// tests assert against (must be entirely in front of nearPlane).
    irr::core::aabbox3d<irr::f32> placedArmBox;
};

/**
 * Fit the arm geometry into the viewmodel frustum.
 *
 * @param armBoxBaked  AABB of the arm-filtered geometry in the player node's
 *                     BAKED frame (i.e. the frame instanceMesh_ vertices are
 *                     in), which is what the caller can measure directly.
 * @param headingY     the player node's Y rotation in degrees (its
 *                     getRotation().Y) at the time armBoxBaked was measured.
 */
ViewmodelPlacement computeViewmodelPlacement(
    const irr::core::aabbox3d<irr::f32>& armBoxBaked,
    float headingY,
    float fovYRadians,
    float aspect,
    const ViewmodelTuning& tuning = ViewmodelTuning());

/// One driver draw call the viewmodel pass will issue.
struct ViewmodelDrawItem {
    irr::core::matrix4 world;
    irr::scene::IMeshBuffer* buffer = nullptr;
    irr::video::SMaterial material;
};

/**
 * Turn the player's arm mesh + equipment nodes into the flat list of draw
 * calls the pass issues. Split out from the pass itself so a headless test
 * can assert that the pass emits draw calls, and where they land, without a
 * GPU or a live frame.
 */
size_t buildViewmodelDrawList(const EntityRenderer::ViewmodelParts& parts,
                              const ViewmodelPlacement& placement,
                              std::vector<ViewmodelDrawItem>& out);

/**
 * AABB over the vertices a mesh's INDEX buffers actually reference.
 * The T74 arm filter rewrites index buffers and never touches vertex arrays,
 * so the mesh's own bounding box still covers the whole body; this is what
 * measures the arms.
 */
irr::core::aabbox3d<irr::f32> measureIndexedBounds(irr::scene::IMesh* mesh);

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_HAS_GRAPHICS
