#pragma once

// B888-CC3 (GitLab #204) -- the 3D `clz` character-select backdrop: the
// showroom geometry (ter_clz.ter + the 29 .mod static objects, placed by
// clz.zon's placements) stood up in its OWN scene manager (createNewSceneManager,
// the CC-2 CharacterModelView idiom), the account's character nodes on the
// pedestals, the Explore free camera, and the click-to-row projection. Render
// thread only.
//
// This is the graphics half of the clz backdrop (the device-free format layer
// is clz_backdrop.{h,cpp}; the README's "3D slice"). It stands the geometry
// WITHOUT touching the production zone path: clz has no .wld and S3DLoader
// refuses it (pinned by ExploreScreen.BackdropZoneLoadsHeadless), so the
// members are parsed client-side and stood up in a scene manager that the
// pre-zone frame's main scene graph never sees (the pre-zone frame draws no
// zone scene; the host blits the UI over this scene's draw).
//
// COORDINATES (settled from the measured geometry, not guessed): the clz
// geometry is Z-up -- the terrain's height is its local z, the models' tallest
// axis is z. A node therefore carries placementRotation * ZupToYup as its
// rotation (the placement rotation, then the Z-up->Y-up swap; the reference
// importer's own order), the placement position as its translation, and its
// vertices stay raw local. The node's mesh is built once per distinct model
// (terrain + .mod) and shared by every placement of that model.
//
// EXPLORE FREE CAMERA (the state machine's ExploreCameraVerb): the scene owns
// the camera (an orbit-style camera node at the showroom vantage) and applies
// the held verb each frame. Forward (measured, oracle-a6 A6: holding `d` 8 s
// moved the free camera, diff 45.48) moves the camera along its look
// direction; strafe/orbit/zoom are the stock set the notes name but were NOT
// captured on this rig, so they are plain scene operations (a host may leave
// them at the verb's zero default with no parity break, per explore_screen.h).

#include "client/graphics/ui/clz_backdrop.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <irrlicht.h>

namespace EQT {
namespace Graphics {
class IrrlichtRenderer;
struct EntityAppearance;     // entity_renderer.h (the setCharacter argument)
struct ExploreCameraVerb;    // explore_screen.h (the update() argument)
}  // namespace Graphics
}  // namespace EQT

namespace irr { namespace scene { class ISceneManager; } }

namespace eqt {
namespace ui {

/** The clz backdrop scene (the 3D slice). Render thread only. */
class CharBackdropScene {
public:
    CharBackdropScene();
    ~CharBackdropScene();

    // Stand up the showroom: open clz.eqg (WILLEQ_CLZ overrides the path),
    // parse every placement, build one mesh per distinct model (terrain +
    // .mod), place a node per placement, and create the free camera.
    // Idempotent. Returns false when the scene manager is not up yet
    // (pre-init) -- the host retries next frame; a format failure logs and
    // leaves the scene empty (the UI still draws, the model preview is
    // unaffected).
    bool init(EQT::Graphics::IrrlichtRenderer& renderer);
    // The device entry: the host's own scene manager + the borrowed driver.
    // The headless test feeds it an EDT_NULL device's (no renderer object, no
    // zone, the same shape the CC-2 test feeds CharacterModelView).
    bool init(irr::scene::ISceneManager* mainSmgr, irr::video::IVideoDriver* driver);

    // Drop the scene's own nodes -- the host goes away; the next init()
    // rebuilds.
    void clear();

    // Advance the Explore free camera by dtMs under the held verb (no-op
    // outside Explore). Call BEFORE the UI draw, on the render thread.
    void update(const EQT::Graphics::ExploreCameraVerb& verb, float dtMs);

    // The account's characters: stand row `row` (0-based) on its pedestal.
    // `race`/`gender`/`appearance` come from the select row (the T14 wire, the
    // same shape the CC-2 preview stands up). The node is the row's click
    // target. No-op when the scene is not stood up.
    void setCharacter(int row, uint16_t race, uint8_t gender,
                      const EQT::Graphics::EntityAppearance& appearance);

    // Drop one row's character node (the screen closed, or the row's model
    // changed -- setCharacter() rebuilds).
    void clearCharacter(int row);

    // The account's characters that stand up (row -> node), for the raycast.
    int characterCount() const;

    // Render the scene (the showroom + the character stands) using the scene's
    // OWN camera. No-op until init() stood it up. The host calls this inside
    // drawPreZoneUI's scene slot, before the UI draw, on the render thread --
    // the same slot the oracle's 3D character-select scene draws in
    // (renderer_pre_zone.cpp's "OUT of this unit").
    void renderScene(EQT::Graphics::IrrlichtRenderer& renderer);
    // The device entry (the headless test's draw).
    void renderScene(irr::video::IVideoDriver* driver);

    // A click at screen (x,y): the account row whose model was hit (-1 = no
    // character hit; a hit on backdrop geometry is NOT a selection). Backs the
    // host's backdropRowForClick() seam. -1 when the camera is unset.
    int rowForClick(int x, int y);

    // A true value once init() stood up the showroom (the test's "the 3D
    // slice landed" assertion).
    bool hasScene() const { return scene_ != nullptr; }
    // The distinct models stood up (terrain + static .mod) -- the test's
    // window onto "the geometry is there".
    int nodeCount() const { return static_cast<int>(backdropNodes_.size()); }

private:
    irr::scene::ISceneManager* scene_ = nullptr;  // own manager (createNewSceneManager)
    irr::video::IVideoDriver* driver_ = nullptr;  // borrowed: image/texture upload
    EQT::Graphics::PfsArchive archive_;
    EQT::Graphics::ClzZone zone_;
    std::unordered_map<std::string, std::vector<char>> texData_;

    irr::scene::ISceneNode* root_ = nullptr;
    std::vector<irr::scene::ISceneNode*> backdropNodes_;
    std::unordered_map<std::string, irr::scene::IMesh*> meshes_;  // model name -> shared mesh
    std::unordered_map<std::string, irr::video::ITexture*> texCache_;
    std::vector<irr::scene::ISceneNode*> charNodes_;  // row -> node (sparse)
    irr::scene::ICameraSceneNode* camera_ = nullptr;
    // The free camera's orbit state (the fork's camera is target-based; the
    // scene owns azimuth/elevation/distance and re-derives position and look
    // each update).
    float camAzimuthDeg_ = 0.0f;
    float camElevationDeg_ = 10.0f;
    float camDistance_ = 26.0f;
    // Brace-init: the paren form (x, y, z) is the most-vexing-parse (GCC tries
    // the function-declaration reading first), so the camera state is
    // brace-initialized.
    irr::core::vector3df camTarget_{0.0f, 2.0f, 0.0f};
    irr::core::vector3df camPos_{0.0f, 8.0f, 26.0f};
    irr::scene::ICameraSceneNode* prevActiveCamera_ = nullptr;  // restored by renderScene
};

}  // namespace ui
}  // namespace eqt
