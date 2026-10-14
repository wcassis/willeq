// B998 (GitLab #318) -- the pre-zone UI host. Its own TU (M00). What it is
// and when it runs: include/client/graphics/ui/char_select_host.h.

#include "client/graphics/ui/char_select_host.h"

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/char_select_panel.h"
#include "client/login_world/character_select.h"
#include "common/logging.h"

#include <chrono>

namespace EQT {
namespace Graphics {

CharacterSelectHost::CharacterSelectHost() {
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

bool CharacterSelectHost::frame(IrrlichtRenderer& renderer, eqt::login_world::CharacterSelectChannel& channel,
                                float dt) {
    eqt::login_world::CharacterSelectView view;
    if (channel.takeViewIfNewer(seenView_, view)) applyCharacterSelectView(state_, view);
    tickCharSelect(state_, dt);

    RendererEventReceiver* rx = renderer.getEventReceiver();
    const bool alive = renderer.drawPreZoneUI([&](UIRenderer& ui, const UILayout& layout) {
        if (rx) {
            // The keyboard: the creation name field, or ESC on the prompt.
            while (rx->hasPendingKeyEvents()) {
                const auto e = rx->popKeyEvent();
                applyCharSelectKey(state_, e.key, e.character);
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
            if (rx->wasLeftButtonClicked()) {
                handle(applyCharSelectClick(state_, layout, rx->getClickMouseX(), rx->getClickMouseY(),
                                            false, data_, ++seed_, renderer.Diagnostics().isUsingOldModels()),
                       channel);
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
