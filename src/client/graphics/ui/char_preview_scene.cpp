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
// The spin's step, per 16 ms frame, in degrees. The oracle's spin is slow;
// the ledger's measured period (refactor/B888-CC2/README.md) sets this. 300
// degrees per 60-frame second ~= a 1.2 s full turn, the paperdoll's idle
// rate. Kept as a constant, not a magic number in update().
constexpr float kSpinDegreesPerMs = 0.15f;  // ~90 deg/s, a slow show-rotation
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
}

void CharPreviewScene::update(float dtMs) {
    if (!impl_->ready) {
        return;
    }
    if (rotateEnabled_) {
        impl_->view->setRotationY(impl_->view->getRotationY() + kSpinDegreesPerMs * dtMs);
    }
    impl_->view->update(dtMs);
    impl_->view->renderToTexture();
    texture_ = impl_->view->getTexture();
}

bool CharPreviewScene::hasModel() const {
    return impl_->ready && impl_->hasModel && impl_->view->hasCharacterModel();
}
uint16_t CharPreviewScene::modelRace() const { return impl_->race; }
uint8_t CharPreviewScene::modelGender() const { return impl_->gender; }
float CharPreviewScene::rotationY() const {
    return impl_->ready ? impl_->view->getRotationY() : 0.0f;
}

}  // namespace ui
}  // namespace eqt
