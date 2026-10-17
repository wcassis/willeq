// B998 (GitLab #318) -- the pre-zone UI host. Its own TU (M00). What it is
// and when it runs: include/client/graphics/ui/char_select_host.h.
// B888-CC2 (GitLab #204): also hosts the 3D PREVIEW (CharPreviewScene) --
// the select screen's selected character and the creation screen's sample
// model, blitted into the model area with the Rotate spin. The model is
// drawn HERE, in the host's drawPreZoneUI lambda, BEFORE the screen's own
// draw: renderCharSelectScreen / renderCharCreateScreen are device-free and
// called directly by the visual gate (CC-4) and the frame tests, so they
// must stay model-free.

#include "client/graphics/ui/char_select_host.h"

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/char_preview_host.h"
#include "client/graphics/ui/char_preview_scene.h"
#include "client/graphics/ui/char_select_layout.h"
#include "client/graphics/ui/char_select_panel.h"
#include "client/graphics/ui/explore_screen.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/login_world/character_select.h"
#include "common/logging.h"

#include <chrono>

namespace EQT {
namespace Graphics {

CharacterSelectHost::~CharacterSelectHost() = default;  // unique_ptr<CharPreviewScene> needs its complete type (included above)

CharacterSelectHost::CharacterSelectHost() {
    preview_ = std::make_unique<eqt::ui::CharPreviewScene>();  // B888-CC2: the 3D model
    if (!data_.loadShipped()) {
        // The select screen still works; "Create a New Character" then opens
        // an empty creation screen whose Next can never submit.
        LOG_ERROR(MOD_GRAPHICS, "[CHARSELECT] {}", data_.error());
    }
    seed_ = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

void CharacterSelectHost::handle(const CharSelectAction& a, eqt::login_world::CharacterSelectChannel& channel) {
    switch (a.kind) {
    case CharSelectAction::Kind::Post:
        channel.post(a.request);
        break;
    case CharSelectAction::Kind::Quit:
        LOG_INFO(MOD_GRAPHICS, "[CHARSELECT] Quit");
        quit_ = true;
        break;
    case CharSelectAction::Kind::None:
        break;
    }
}

namespace {
// B888-CC2: a click INSIDE the select window's Rotate button toggles the
// spin. The button is drawn greyed today (the screen has no scene behind it);
// the preview gives it one. Only on the select screen, not over the prompt.
//
// The caller passes in the frame's left-click read, it does NOT read one
// here: wasLeftButtonClicked() is a read-and-reset (the
// test_read_and_reset_ownership guard), and frame() consumes it for the
// screen's own click handling below. A second read in the same frame would
// always see false and steal the click from the screen. The click's
// coordinates are set with the flag and outlive the reset, so the rect hit
// test is stable.
bool isRotateClick(bool leftClicked, RendererEventReceiver* rx, const UILayout& layout) {
    if (!leftClicked || !rx) {
        return false;
    }
    const auto r = CharSelectLayout::rotateRect(layout.screenW, layout.screenH);
    const int x = rx->getClickMouseX(), y = rx->getClickMouseY();
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}
// B888-CC3 (GitLab #204): a click on the select window's Explore button
// (CSW_Explore_Button, now live). Not while the mode is open, the creation
// screen is up, the delete prompt is modal, or a request is in flight --
// enterExplore() guards all of it; this only does the hit test.
bool isExploreButtonClick(int x, int y, const UILayout& layout) {
    const auto r = CharSelectLayout::exploreRect(layout.screenW, layout.screenH);
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}
// B888-CC3: the row a click on the backdrop scene selects (-1 = nothing hit).
// The 3D slice (the clz showroom's character nodes) implements the raycast;
// until it stands up the models, every click is a miss and the selection
// cursor is kept (the machine's own rule for a miss, oracle-consistent:
// A4/A5 showed no selection change from a non-landing click).
int backdropRowForClick(int, int) { return -1; }
// B888-CC2: the preview's model, from the screen's state. The SELECT screen
// stands up the selected row's body (race/gender/face from the T14 wire);
// the CREATION screen stands up the sample model of the page-0 race + gender
// (+ committed FacePick picks). No model otherwise (delete prompt up, page 1).
eqt::graphics::CharPreviewAppearance previewFor(const CharSelectScreenState& s) {
    if (s.confirm.isOpen) {
        return {};
    }
    if (s.create.isOpen) {
        // Page 1 is deity/city/name: the model is still the page-0 body (the
        // oracle keeps it up; only Back leaves the creation screen).
        return eqt::graphics::createPreviewAppearance(s.create.race, static_cast<uint8_t>(s.create.gender),
                                                      s.create.appearance);
    }
    if (s.selected >= 0 && s.selected < static_cast<int>(s.rows.size())) {
        const auto& r = s.rows[static_cast<size_t>(s.selected)];
        return eqt::graphics::selectPreviewAppearance(r.race, r.gender, r.face, r.hair_color, r.hair_style);
    }
    return {};
}
}  // namespace

bool CharacterSelectHost::frame(IrrlichtRenderer& renderer, eqt::login_world::CharacterSelectChannel& channel,
                                float dt) {
    eqt::login_world::CharacterSelectView view;
    if (channel.takeViewIfNewer(seenView_, view)) applyCharacterSelectView(state_, view);
    tickCharSelect(state_, dt);

    RendererEventReceiver* rx = renderer.getEventReceiver();
    const bool alive = renderer.drawPreZoneUI([&](UIRenderer& ui, const UILayout& layout) {
        const int sw = layout.screenW, sh = layout.screenH;

        // B888-CC2: the 3D preview. Stand up this state's model, advance the
        // spin, render to its texture, and blit it into the model area --
        // BEFORE the screen draws, so the UI (the left window, the creation
        // columns) draws over the backdrop as it does in the oracle. The
        // render thread owns all of this: the view builds its own scene
        // manager off the renderer's, so nothing of the zone scene graph is
        // touched.
        const bool onSelectScreen = !state_.create.isOpen;
        // The frame's left click, read ONCE: wasLeftButtonClicked() is a
        // read-and-reset, and both the Rotate toggle and the screen's own
        // click handling below consume it. Reading it here, before the
        // screen's, would otherwise leave the second read seeing false.
        const bool leftClicked = rx && rx->wasLeftButtonClicked();
        if (leftClicked && onSelectScreen && !state_.confirm.isOpen && isRotateClick(leftClicked, rx, layout)) {
            preview_->setRotateEnabled(!preview_->rotateEnabled());
        }
        preview_->init(renderer);
        const auto app = previewFor(state_);
        if (app.valid) {
            preview_->setModel(app.race, app.gender, app.appearance);
        } else {
            preview_->clearModel();
        }
        preview_->update(dt * 1000.0f);
        if (preview_->hasModel() && preview_->texture()) {
            const auto& modelRect = CharSelectLayout::modelAreaRect(sw, sh);
            ui.drawTexture(modelRect, preview_->texture());
        }

        if (rx) {
            // The keyboard: the creation name field, or ESC on the prompt, or
            // (B1082) Up/Down navigation and Enter/Return.
            //
            // B888-CC3 (GitLab #204): Explore mode. Escape CLOSES Explore
            // (MEASURED, oracle-a6 A9: Esc returns to the normal select screen)
            // and is otherwise this screen's, never the world's. Enter/Return
            // while Explore is open goes through the Explore machine (the
            // list-selected character enters -- oracle-b1), not the select
            // screen's own Enter. The free camera moves on the held keys
            // (forward on `d` measured, oracle-a6 A6) -- the held set is
            // read from the receiver each frame (the 3D slice applies it to
            // the backdrop scene), so here the keys are simply Explore's.
            while (rx->hasPendingKeyEvents()) {
                const auto e = rx->popKeyEvent();
                if (state_.exploreOpen && !state_.confirm.isOpen) {
                    if (e.key == irr::KEY_ESCAPE) {
                        exitExplore(state_);
                        continue;   // Esc is consumed by Explore; nothing else gets it
                    }
                    if (e.key == irr::KEY_RETURN) {
                        handle(exploreEnterWorld(state_), channel);
                        continue;
                    }
                    continue;   // the camera keys (e.g. `d`) are Explore's; the held set drives the scene
                }
                handle(applyCharSelectKey(state_, e.key, e.character), channel);
            }
            // Spent here so a key typed into the name field is not replayed
            // as "open chat" on the first in-zone frame.
            (void)rx->enterKeyPressed();
            (void)rx->slashKeyPressed();
            (void)rx->escapeKeyPressed();
            // Every key is this screen's: with the receiver's text-entry gate
            // up, no key queues an in-game hotkey action (inventory, camera
            // ...) that the first in-zone frame would then replay. release()
            // lowers it again.
            rx->setChatInputFocused(true);
            if (leftClicked) {
                const int cx = rx->getClickMouseX(), cy = rx->getClickMouseY();
                if (state_.exploreOpen && !state_.confirm.isOpen) {
                    // A click in Explore selects the character whose model was
                    // clicked: the backdrop scene's raycast maps the click to
                    // an account row (one model node per row), -1 when it hits
                    // nothing. The select window is HIDDEN in this mode, so
                    // the screen's own click resolution does not run.
                    const int row = backdropRowForClick(cx, cy);
                    exploreSelectRow(state_, row);
                } else if (isExploreButtonClick(cx, cy, layout)) {
                    enterExplore(state_);   // CSW_Explore_Button (now live): open the mode
                } else {
                    handle(applyCharSelectClick(state_, layout, cx, cy,
                                                false, data_, ++seed_, renderer.Diagnostics().isUsingOldModels()),
                           channel);
                }
            }
            if (rx->wasRightButtonClicked()) {
                handle(applyCharSelectClick(state_, layout, rx->getClickMouseX(), rx->getClickMouseY(),
                                            true, data_, ++seed_, renderer.Diagnostics().isUsingOldModels()),
                       channel);
            }
        }
        renderCharSelectScreen(ui, layout, state_);
    });
    if (!alive) return false;
    if (quit_ || (rx && rx->quitRequested())) return false;
    return true;
}

void CharacterSelectHost::release(IrrlichtRenderer& renderer) {
    if (RendererEventReceiver* rx = renderer.getEventReceiver()) {
        rx->setChatInputFocused(false);
        rx->clearPendingKeyEvents();
    }
}

}  // namespace Graphics
}  // namespace EQT
