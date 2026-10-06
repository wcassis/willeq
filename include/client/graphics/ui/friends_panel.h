/*
 * The Friends window -- renderer-side state, draw and click resolution
 * (B897, GitLab #213). Geometry: friends_layout.h (quotes
 * EQUI_FriendsWnd.xml); this file computes no rect.
 *
 * THE LISTS ARE THE GAME THREAD'S. state::SocialListsModel
 * (include/client/state/social_lists.h) owns both; this panel holds a COPY,
 * refreshed by the SocialListsChanged event, and never edits it. A click on
 * Add / Delete / Who resolves to a FriendsAction which FrameComponent turns
 * into a SocialListIntent -- the intent's handler runs the SAME
 * ApplySocialEdit()/RunFriendsWho() bodies `/friends`, `/ignore` and
 * `/friendswho` run, so the window and the slash commands print the same
 * oracle strings and cannot drift. The panel does not update its copy
 * optimistically: the new list arrives back on SocialListsChanged.
 *
 * CONTACT, MEASURED (review/UIGAP-ORACLE/213/friends.tsv row 15): puts
 * `/tell <selected> ` into the chat input, unsent -- fable uses the R key's
 * prefill path (T319). WHO (rows 13-14): no selection queries the stored
 * list (OP_FriendsWho); a selected friend is a who-all for that name.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/friends_layout.h"
#include "client/graphics/ui/text_entry.h"
#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct SocialListsChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

struct FriendsPanelState {
    static constexpr int PAGE_COUNT = FriendsLayout::PAGE_COUNT;

    bool isOpen = false;
    int activePage = 0;                       // FriendsLayout::Page

    // The copies, index = FriendsLayout::Page.
    std::vector<std::string> names[PAGE_COUNT];
    int selected[PAGE_COUNT] = {-1, -1};      // index into names[page]
    int scroll[PAGE_COUNT] = {0, 0};          // first visible row

    // FW_NameInput / FW_IgnoreNameInput.
    TextEntryState nameEntry[PAGE_COUNT];
    int focusedEntry = -1;                    // page whose Editbox has the caret

    // Hover, for the draw only.
    int hoveredRow = -1;
    bool closeHovered = false;

    TextEntryState* focusedTextEntry() {
        return (focusedEntry >= 0 && focusedEntry < PAGE_COUNT)
                   ? &nameEntry[focusedEntry] : nullptr;
    }
    int maxScroll(int page) const;
    /** The selected name on `page`, or empty. */
    std::string selectedName(int page) const;
};

/** What a click hit. */
enum class FriendsHit : uint8_t {
    None = 0,
    Window,       // inside the window, on nothing interactive
    Close,
    Tab0,
    Tab1,
    Row,          // a list row; FriendsClickTarget::row says which
    ScrollUp,     // the VScroll strip's upper half
    ScrollDown,   // its lower half
    NameInput,
    Add,
    Delete,
    Contact,      // Friends page only
    Who,          // Friends page only
};

struct FriendsClickTarget {
    FriendsHit hit = FriendsHit::None;
    int row = -1;   // absolute index into names[activePage] for Row
};

/** What FrameComponent must do after a click. */
struct FriendsAction {
    enum class Kind : uint8_t { None, Add, Remove, Who, Contact, Close };
    Kind kind = Kind::None;
    int page = 0;        // FriendsLayout::Page
    std::string name;    // Add / Remove / Contact
};

/** SocialListsChanged -> the two copies. Clamps selection and scroll. */
void applySocialListsChanged(FriendsPanelState& state,
                             const eqt::state::SocialListsChangedData& data);

/** Draw the window. Only when state.isOpen. */
void renderFriendsWindow(UIRenderer& ui, const UILayout& layout,
                         const FriendsPanelState& state);

/** What is at the point, on the ACTIVE page. None when closed or outside. */
FriendsClickTarget hitTestFriendsWindow(const UILayout& layout,
                                        const FriendsPanelState& state,
                                        int mouseX, int mouseY);

/**
 * Apply one click: switches tab, selects a row, scrolls, moves the caret,
 * and returns the action that needs the game thread (or the chat input).
 * Add takes the Editbox's first word and clears the box; Delete and Contact
 * take the selected row; a click with nothing to act on returns None.
 */
FriendsAction applyFriendsClick(FriendsPanelState& state,
                                const FriendsClickTarget& target);

}  // namespace Graphics
}  // namespace EQT
