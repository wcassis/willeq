#pragma once

// B888-CC3 (GitLab #204) -- the 3D `clz` character-select backdrop: the
// showroom geometry (ter_clz.ter + the 29 .mod static objects, placed by
// clz.zon's 157 placements) stood up in the pre-zone scene manager, the
// account's character nodes on the pedestals, the Explore free camera, and the
// click-to-row raycast. Render thread only.
//
// This is the graphics half of the clz backdrop (the device-free format layer
// is clz_backdrop.{h,cpp}; the README's "3D slice"). It stands the geometry in
// the host's OWN scene manager -- the pre-zone frame draws no zone scene, so
// (like the CC-2 preview, char_preview_scene.h) this uses the renderer's scene
// manager directly, not the zone path. The production zone-load path is
// untouched: clz has no .wld and S3DLoader refuses it (pinned by
// ExploreScreen.BackdropZoneLoadsHeadless); this parses the members client-side.
//
// COORDINATES (settled from the measured geometry, not guessed): the clz
// geometry is Z-up -- every model's smallest span is its local z, and the
// reference importer maps a model vertex (x,y,z) to world (x,z,y) (local z is
// world y = up). A node therefore carries rotation * ZupToYup as its rotation
// (the placement rotation, then the Z-up->Y-up swap), the placement position
// as its translation, and its vertices stay raw local. The node's mesh is
// built once per model (shared across its placements) and re-used by every
// instance of that model.
//
// EXPLORE FREE CAMERA (the state machine's ExploreCameraVerb): the scene owns
// the camera (an FPS-style camera node at the showroom vantage) and applies
// the held verb each frame. Forward (measured, oracle-a6 A6: holding `d` 8 s
// moved the free camera, diff 45.48) moves the camera along its look direction;
// strafe/orbit/zoom are the stock set the notes name but were NOT captured on
// this rig, so they are implemented as plain scene operations (a host may leave
// them at the verb's zero default with no parity break, per explore_screen.h).

#include "client/graphics/ui/clz_backdrop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <irrlicht.h>

namespace EQT {
namespace Graphics {
class IrrlichtRenderer;
struct EntityAppearance;     // race_model_loader.h (the setCharacter argument)
struct ExploreCameraVerb;    // explore_screen.h (the update() argument)
}  // namespace Graphics
}  // namespace EQT

namespace eqt {
namespace ui {

/** The clz backdrop scene (the 3D slice). Render thread only. */
class CharBackdropScene {
public:
    CharBackdropScene();
    ~CharBackdropScene();

    // Stand up the showroom: open clz.eqg (WILLEQ_CLZ overrides the path),
    // parse every placement, build one mesh per distinct model (terrain +
    // .mod), place a node per placement, and create the free camera. Idempotent.
    // Returns false when the renderer has no scene manager yet (pre-init) --
    // the host retries next frame; a format failure logs and leaves the scene
    // empty (the UI still draws, the model preview is unaffected).
    bool init(EQT::Graphics::IrrlichtRenderer& renderer);

    // Drop the scene's own nodes (keep the camera until shutdown) -- the
    // host goes away; the next init() rebuilds.
    void clear();

    // Advance the Explore free camera by dtMs under the held verb (no-op
    // outside Explore). Call BEFORE the UI draw, on the render thread.
    void update(const EQT::Graphics::ExploreCameraVerb& verb, float dtMs);

    // The account's characters: stand row `row` (0-based) on its pedestal.
    // `race`/`gender`/`appearance` come from the select row (the T14 wire, the
    // same shape the CC-2 preview stands up). The node is the row's click
    // target. No-op when the model cannot be stood up (no loader, pre-init).
    void setCharacter(int row, uint16_t race, uint8_t gender,
                      const EQT::Graphics::EntityAppearance& appearance);

    // Drop one row's character node (the screen closed, or the row's model
    // changed -- setCharacter() rebuilds).
    void clearCharacter(int row);

    // The account's characters that stand up (row -> node), for the raycast.
    int characterCount() const;

    // Render the scene (the showroom + the character stands) into the current
    // scene using the scene's OWN camera. No-op until init() stood it up. The
    // host calls this inside drawPreZoneUI's scene slot, between
    // begin::beginScene and the UI draw, on the render thread -- the same slot
    // the oracle's 3D character-select scene draws in (renderer_pre_zone.cpp's
    // "OUT of this unit").
    void renderScene(EQT::Graphics::IrrlichtRenderer& renderer);

    // A click at screen (x,y): the account row whose model was hit (-1 = no
    // character hit; a hit on backdrop geometry is NOT a selection). Backs the
    // host's backdropRowForClick() seam. -1 when Explore's camera is unset.
    int rowForClick(int x, int y);

    // A non-null pointer once init() stood up the showroom (the test's "the
    // 3D slice landed" assertion).
    bool hasScene() const { return impl_ && impl_->root != nullptr; }
    // The distinct models stood up (terrain + static .mod) -- the test's
    // window onto "the geometry is there".
    int nodeCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ui
}  // namespace eqt
