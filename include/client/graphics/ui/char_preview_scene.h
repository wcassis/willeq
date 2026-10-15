#pragma once

// B888-CC2 (GitLab #204): the pre-zone 3D PREVIEW -- the render host and the
// device-free appearance-selection logic. What it is and why it is two TUs:
//
//   char_preview_host.h/.cpp (device-free)   which race/gender/appearance to
//       stand up, per screen state. No Irrlicht, no loader. The headless
//       "right model in the view" test links only this half.
//   char_preview_scene.h/.cpp (graphics)     the model itself: a
//       CharacterModelView (the T60 paperdoll) hosted on the pre-zone render
//       thread -- the select screen's selected character, the creation
//       screen's sample model, and the Rotate spin. Render thread only.
//
// WHY a CharacterModelView and not the main scene graph: a CharacterModelView
// owns its OWN scene manager (init() calls smgr->createNewSceneManager(false)),
// its own camera + light, and renders a single model to a driver texture
// (renderToTexture()). The pre-zone frame (IrrlichtRenderer::drawPreZoneUI)
// draws NO scene graph -- no zone, no camera, no entities -- so the preview
// cannot stand up a model in the main scene. CharacterModelView is the one
// component that renders a race model into a texture with no zone, and the
// inventory paperdoll already runs it in production (overlays_hud_env.cpp).
// This host runs a SECOND instance, fed from the select/creation state, and
// blits its texture into the official model rect.
//
// THE RACE MODEL LOADER (B1029's trap, sidestepped): the view's
// setRaceModelLoader() takes the SAME loader the entity renderer wires at
// init (createEntityRenderer(): RaceModelLoader + setClientPath + the
// constrained texture cache + its mesh builder). This host NEVER creates a
// second loader and NEVER re-wires the cache -- it borrows the entity
// renderer's, exactly as the paperdoll does (OverlaysComponent::
// ensurePaperdollInitialized() -> ctx_.entityRenderer->getRaceModelLoader()).
// The loader loads a race on demand (createAnimatedNodeWithEquipment ->
// ensureModelDataLoaded -> loadGlobalModels), so a model stands up with no
// zone loaded: no zone geometry enters the scene (acceptance D).

#include "client/graphics/entity_renderer.h"
#include "client/login_world/char_create_faces.h"

#include <array>
#include <cstdint>
#include <irrlicht.h>

namespace EQT {
namespace Graphics {
class IrrlichtRenderer;
}  // namespace Graphics
}  // namespace EQT

namespace eqt {
namespace ui {

/** One frame of the preview: advance the spin (if on), render to texture. */
class CharPreviewScene {
public:
    CharPreviewScene();
    ~CharPreviewScene();

    // Build the CharacterModelView off the renderer's scene manager + driver,
    // borrowing the entity renderer's race loader (the B1029 rule above).
    // Idempotent: a second call re-uses the view. Returns false when the
    // renderer has no scene manager/driver yet (pre-init) -- then no preview.
    bool init(EQT::Graphics::IrrlichtRenderer& renderer);

    // Stand up the model for this race/gender/appearance. No-op when invalid
    // or when the view is not initialised. Rebuilds only on a change.
    void setModel(uint16_t race, uint8_t gender, const EQT::Graphics::EntityAppearance& appearance);
    // Drop the model nodes (keep the view + its render target) -- the
    // screen is going away; the next setModel() rebuilds.
    void clearModel();

    // The Rotate spin: on by default (the oracle's button is lit). When on,
    // each update() advances the model's Y rotation a small step.
    bool rotateEnabled() const { return rotateEnabled_; }
    void setRotateEnabled(bool on) { rotateEnabled_ = on; }

    // Advance the spin by dt ms (no-op when off), then render to the
    // internal texture. Call BEFORE the UI draw, on the render thread.
    void update(float dtMs);

    // The rendered model texture (nullptr until the first renderToTexture).
    irr::video::ITexture* texture() const { return texture_; }
    // The model node that stood up (nullptr = none) -- the frame test's
    // "the right race model is in the view" assertion.
    bool hasModel() const;
    uint16_t modelRace() const;
    uint8_t modelGender() const;
    // The model's current Y rotation, for the spin round-trip test.
    float rotationY() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool rotateEnabled_ = true;
    irr::video::ITexture* texture_ = nullptr;
};

}  // namespace ui
}  // namespace eqt
