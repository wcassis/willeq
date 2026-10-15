// B888-CC2 (GitLab #204) -- the pre-zone preview's model: a CharacterModelView
// hosted on the render thread. Its own TU (M00). What it is and the B1029
// loader rule: include/client/graphics/ui/char_preview_scene.h.

#include "client/graphics/ui/char_preview_scene.h"

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/character_model_view.h"

#include <algorithm>
#include <cstddef>

namespace eqt {
namespace ui {

namespace {
bool appearanceEquals(const EQT::Graphics::EntityAppearance& a,
                      const EQT::Graphics::EntityAppearance& b) {
    if (a.face != b.face || a.haircolor != b.haircolor || a.hairstyle != b.hairstyle ||
        a.beardcolor != b.beardcolor || a.beard != b.beard || a.texture != b.texture ||
        a.helm != b.helm) {
        return false;
    }
    for (int i = 0; i < 9; ++i) {
        if (a.equipment[i] != b.equipment[i] || a.equipment_tint[i] != b.equipment_tint[i]) {
            return false;
        }
    }
    return true;
}
}  // namespace

// The render target the model is rendered into, and the rect it is blitted
// into are the same constants: the model's view is square-ish and the blit
// scales it into the official model rect (the select screen's model area and
// the creation screen's model area differ only by placement, not proportion --
// both are the character's standing height in the 3D area). 256x256 is the
// paperdoll's RT supersampled (InventoryLayout::paperdollRenderTargetSize()
// is paperdollRect() x PAPERDOLL_RT_SUPERSAMPLE; the preview keeps it a single
// fixed size so the RT never depends on the screen resolution, the same rule
// as the paperdoll's "Constant regardless of screen resolution").
namespace {
constexpr int kPreviewW = 256;
constexpr int kPreviewH = 256;
// The spin's step, in degrees per ms. 0.15 deg/ms = 150 deg/s (~2.4 s per
// full turn), the paperdoll's idle rate, a slow show-rotation. An assumed
// default, NOT an oracle measurement -- the spin period was NOT measured from
// the capture (refactor/B888-CC2/README.md, "Not verified"). Kept as a
// constant, not a magic number in update().
constexpr float kSpinDegreesPerMs = 0.15f;  // 150 deg/s (~2.4 s/turn), slow show-rotation
}  // namespace

class CharPreviewScene::Impl {
public:
    std::unique_ptr<eqt::ui::CharacterModelView> view;
    bool ready = false;

    // The last model stood up, so setModel() rebuilds only on a change.
    bool hasModel = false;
    uint16_t race = 0;
    uint8_t gender = 0;
    EQT::Graphics::EntityAppearance appearance;
};

CharPreviewScene::CharPreviewScene() : impl_(std::make_unique<Impl>()) {}
CharPreviewScene::~CharPreviewScene() = default;

bool CharPreviewScene::init(EQT::Graphics::IrrlichtRenderer& renderer) {
    if (impl_->ready) {
        return true;
    }
    if (!renderer.getSceneManager() || !renderer.getDriver()) {
        return false;  // pre-init: no scene yet; the host retries next frame
    }
    if (!impl_->view) {
        impl_->view = std::make_unique<eqt::ui::CharacterModelView>();
        impl_->view->init(renderer.getSceneManager(), renderer.getDriver(), kPreviewW, kPreviewH);
        // THE B1029 RULE: borrow the entity renderer's loader, never build a
        // second one or re-wire the cache. This is the paperdoll's own wiring
        // (OverlaysComponent::ensurePaperdollInitialized): the loader the
        // entity renderer wired at init already has the client path AND the
        // constrained texture cache (both loader_ and its mesh builder).
        if (auto* er = renderer.getEntityRenderer()) {
            impl_->view->setRaceModelLoader(er->getRaceModelLoader());
            impl_->view->setEquipmentModelLoader(er->getEquipmentModelLoader());
        }
        impl_->ready = true;
    }
    return impl_->ready;
}

void CharPreviewScene::setModel(uint16_t race, uint8_t gender,
                                const EQT::Graphics::EntityAppearance& appearance) {
    if (!impl_->ready || race == 0) {
        return;
    }
    const bool changed = !impl_->hasModel || impl_->race != race || impl_->gender != gender ||
        !appearanceEquals(impl_->appearance, appearance);
    if (!changed) {
        return;
    }
    impl_->race = race;
    impl_->gender = gender;
    impl_->appearance = appearance;
    impl_->hasModel = true;
    // setCharacter() rebuilds the model node and marks the view dirty (the
    // RTT re-renders on the next update()).
    impl_->view->setCharacter(race, gender, appearance);
}

void CharPreviewScene::clearModel() {
    if (!impl_->ready) {
        return;
    }
    impl_->view->releaseCharacterModel();
    impl_->hasModel = false;
    impl_->race = 0;
    spinAngle_ = 90.0f;  // the next screen starts at the paperdoll's facing
}

void CharPreviewScene::update(float dtMs) {
    if (!impl_->ready) {
        return;
    }
    if (rotateEnabled_) {
        // The host OWNS the spin angle and only ever SETS it -- it never
        // reads the view's rotation. That keeps a stray `getRotationY(`
        // token out of production so the zero-caller sweep does not (a) make
        // CharacterModelView::getRotationY look live (a stale row) or (b)
        // mask the genuinely-dead EntityRenderer::getRotationY. The view
        // starts its model facing rotationY_ = 90, so the spin bases on that.
        spinAngle_ += kSpinDegreesPerMs * dtMs;
        impl_->view->setRotationY(spinAngle_);
    }
    impl_->view->update(dtMs);
    impl_->view->renderToTexture();
    texture_ = impl_->view->getTexture();
}

bool CharPreviewScene::hasModel() const {
    return impl_->ready && impl_->hasModel && impl_->view->hasCharacterModel();
}
uint16_t CharPreviewScene::modelRace() const { return impl_->race; }

}  // namespace ui
}  // namespace eqt
