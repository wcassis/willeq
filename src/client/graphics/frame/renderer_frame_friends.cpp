// renderer_frame_friends.cpp - the Friends window's toggle, click pass, Editbox
// key gate and action applier (B897, GitLab #213).
//
// The window's geometry is friends_layout.h, its draw and click resolution
// friends_panel.cpp; this TU is only the seam between a resolved click and
// the rest of the client:
//
//   Add / Delete / Who  -> SocialListIntent. Its handler on the game thread
//                          runs the SAME bodies /friends, /ignore and
//                          /friendswho run (state/social_lists.h), so the
//                          window and the commands print the same strings.
//   Contact             -> the chat input, prefilled `/tell <name> ` through
//                          the T319 prefill path the R key uses -- MEASURED,
//                          review/UIGAP-ORACLE/213/friends.tsv row 15.
//   Close box           -> closes locally; nothing about the window is
//                          server-owned.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/friends_panel.h"
#include "client/events/renderer_intents.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool FrameComponent::toggleFriendsWindow() {
    friendsState_.isOpen = !friendsState_.isOpen;
    if (!friendsState_.isOpen && friendsState_.focusedEntry >= 0) {
        friendsState_.nameEntry[friendsState_.focusedEntry].inputActive = false;
        friendsState_.focusedEntry = -1;
    }
    LOG_INFO(MOD_GRAPHICS, "Friends window {}",
             friendsState_.isOpen ? "opened" : "closed");
    return friendsState_.isOpen;
}

void FrameComponent::applyFriendsAction(const FriendsAction& action) {
    using Kind = FriendsAction::Kind;
    switch (action.kind) {
        case Kind::None:
            return;
        case Kind::Close:
            if (friendsState_.isOpen) toggleFriendsWindow();
            return;
        case Kind::Contact:
            owner_.requestChatPrefill("/tell " + action.name + " ");
            return;
        case Kind::Add:
        case Kind::Remove:
        case Kind::Who:
            break;
    }
    if (!ctx_.bridge) return;
    eqt::events::SocialListIntent intent;
    intent.list = static_cast<uint8_t>(action.page);
    intent.op = action.kind == Kind::Add ? eqt::events::SocialListOp::Add
              : action.kind == Kind::Remove ? eqt::events::SocialListOp::Remove
                                            : eqt::events::SocialListOp::Who;
    intent.name = action.name;
    ctx_.bridge->pushIntent(std::move(intent));
}

void FrameComponent::processFriendsWindowInput() {
    if (!friendsState_.isOpen) {
        friendsState_.hoveredRow = -1;
        friendsState_.closeHovered = false;
        return;
    }
    const int mx = ctx_.eventReceiver->getMouseX();
    const int my = ctx_.eventReceiver->getMouseY();
    const FriendsClickTarget target = hitTestFriendsWindow(uiLayout_, friendsState_, mx, my);
    friendsState_.hoveredRow = (target.hit == FriendsHit::Row) ? target.row : -1;
    friendsState_.closeHovered = (target.hit == FriendsHit::Close);
    if (!pendingUIClick_) return;
    if (target.hit == FriendsHit::None) {
        // A click elsewhere takes the caret out of the Editbox.
        if (friendsState_.focusedEntry >= 0) {
            friendsState_.nameEntry[friendsState_.focusedEntry].inputActive = false;
            friendsState_.focusedEntry = -1;
        }
        return;
    }
    applyFriendsAction(applyFriendsClick(friendsState_, target));
}

// The tradeskill window's key gate (processTradeskillSearchInput) with the
// friends window's own flag, so neither releases the other's hold.
bool FrameComponent::processFriendsNameInput() {
    TextEntryState* entry = friendsState_.isOpen ? friendsState_.focusedTextEntry() : nullptr;
    if (entry == nullptr) {
        if (friendsGateHeld_) {
            friendsGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
        }
        return false;
    }
    if (!friendsGateHeld_) {
        friendsGateHeld_ = true;
        ctx_.eventReceiver->setChatInputFocused(true);
        ctx_.eventReceiver->clearPendingKeyEvents();
        return true;
    }
    while (ctx_.eventReceiver->hasPendingKeyEvents()) {
        auto ke = ctx_.eventReceiver->popKeyEvent();
        const TextEntryAction action = applyTextEntryKey(*entry, ke.key, ke.character);
        if (action == TextEntryAction::Submit) {
            // Return in the Editbox is its page's Add button.
            FriendsClickTarget add;
            add.hit = FriendsHit::Add;
            applyFriendsAction(applyFriendsClick(friendsState_, add));
            entry->inputActive = true;
            return true;
        }
        if (action == TextEntryAction::Cancel) {
            entry->inputActive = false;
            friendsState_.focusedEntry = -1;
            friendsGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
            return true;
        }
        // Complete (Tab): no vocabulary for a player name -- a no-op, T357's rule.
    }
    return true;
}

}  // namespace Graphics
}  // namespace EQT
