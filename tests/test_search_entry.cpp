// T421 Leg B (board B365) -- ONE text-entry widget, and the tradeskill
// search box using it.
//
// T419 drew COMBW_MinTrivialEdit / COMBW_MaxTrivialEdit /
// COMBW_SearchTextEdit, COMBW_SearchButton and the three favourites buttons
// and left every one of them inert, because "fable's static UI has no
// text-entry widget outside the chat box". That was true:
// FrameComponent::processChatInput() was the only keyboard consumer and its
// caret, backspace and printable filter were written inline against
// ChatPanelState's three members.
//
// What is pinned here:
//   1. applyTextEntryKey() is the ONE edit body, and ChatPanelState IS a
//      TextEntryState -- the chat entry runs through the same code.
//   2. Typing into a focused search box changes ITS text.
//   3. Enter and the Search button publish the SAME RecipeSearchIntent.
//   4. The four buttons T419 could not name now come back from the hit test.
//
// Device-free, server-free: the state structs and the real StaticUIInput.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/text_entry.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <string>
#include <type_traits>

using namespace EQT::Graphics;

namespace {

UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

void type(TextEntryState& s, const std::string& text) {
    for (char c : text) {
        applyTextEntryKey(s, irr::KEY_KEY_CODES_COUNT, static_cast<wchar_t>(c));
    }
}

eqt::state::TradeskillContainerOpenedEvent forgeOpened() {
    eqt::state::TradeskillContainerOpenedEvent e{};
    e.containerName = "Forge";
    e.isWorldObject = true;
    e.slotCount = 10;
    e.objectId = 900;
    e.inventorySlot = -1;
    return e;
}

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

}  // namespace

// ===================================================================
// 1 -- there is exactly ONE entry widget
// ===================================================================

TEST(SearchEntryT421, ChatPanelStateIsATextEntryState) {
    static_assert(std::is_base_of<TextEntryState, ChatPanelState>::value,
                  "the chat box must BE the shared entry widget, not a copy of it");
    ChatPanelState chat;
    type(chat, "/ooc hello");
    EXPECT_EQ(chat.inputText, "/ooc hello");
    EXPECT_EQ(chat.cursorPos, 10);
    EXPECT_EQ(applyTextEntryKey(chat, irr::KEY_RETURN, 0), TextEntryAction::Submit);
    EXPECT_EQ(applyTextEntryKey(chat, irr::KEY_ESCAPE, 0), TextEntryAction::Cancel);
    EXPECT_EQ(applyTextEntryKey(chat, irr::KEY_TAB, 0), TextEntryAction::Complete);
    EXPECT_EQ(chat.inputText, "/ooc hello")
        << "Submit/Cancel/Complete are the CALLER's policy; the widget edits nothing";
}

TEST(SearchEntryT421, TheEditBodyIsTheChatDrainsOwnBehaviour) {
    TextEntryState s;
    type(s, "abc");
    EXPECT_EQ(s.inputText, "abc");

    EXPECT_EQ(applyTextEntryKey(s, irr::KEY_BACK, 0), TextEntryAction::Changed);
    EXPECT_EQ(s.inputText, "ab");
    EXPECT_EQ(s.cursorPos, 2);

    applyTextEntryKey(s, irr::KEY_BACK, 0);
    applyTextEntryKey(s, irr::KEY_BACK, 0);
    EXPECT_EQ(applyTextEntryKey(s, irr::KEY_BACK, 0), TextEntryAction::None)
        << "backspace on an empty field is a no-op, not an underflow";
    EXPECT_EQ(s.cursorPos, 0);

    // The printable filter: 32..126 only, exactly as processChatInput() had it.
    EXPECT_EQ(applyTextEntryKey(s, irr::KEY_KEY_CODES_COUNT, 31), TextEntryAction::None);
    EXPECT_EQ(applyTextEntryKey(s, irr::KEY_KEY_CODES_COUNT, 127), TextEntryAction::None);
    EXPECT_TRUE(s.inputText.empty());

    focusTextEntry(s, "seeded");
    EXPECT_TRUE(s.inputActive);
    EXPECT_EQ(s.cursorPos, 6);
    clearTextEntry(s);
    EXPECT_FALSE(s.inputActive);
    EXPECT_TRUE(s.inputText.empty());
}

// ===================================================================
// 2 -- typing into the focused search box changes ITS text
// ===================================================================

TEST(SearchEntryT421, TypingLandsInTheFocusedFieldOnly) {
    TradeskillPanelState st;
    EXPECT_EQ(st.focusedEntry(), nullptr) << "nothing is focused by default";

    st.focusedField = TradeskillField::SearchText;
    ASSERT_EQ(st.focusedEntry(), &st.searchEntry);
    type(*st.focusedEntry(), "iron");
    EXPECT_EQ(st.searchEntry.inputText, "iron");
    EXPECT_TRUE(st.minTrivialEntry.inputText.empty());
    EXPECT_TRUE(st.maxTrivialEntry.inputText.empty());

    st.focusedField = TradeskillField::MinTrivial;
    type(*st.focusedEntry(), "21");
    st.focusedField = TradeskillField::MaxTrivial;
    type(*st.focusedEntry(), "188");
    EXPECT_EQ(st.typedMinTrivial(), 21u);
    EXPECT_EQ(st.typedMaxTrivial(), 188u);
    EXPECT_EQ(st.searchEntry.inputText, "iron") << "the other fields are untouched";
}

// A typo is 0, i.e. "no bound" -- which is what /recipesearch sends when the
// player omits one. A bad bound must never become a silent filter.
TEST(SearchEntryT421, AnUnparseableBoundIsNoBound) {
    TradeskillPanelState st;
    st.minTrivialEntry.inputText = "";
    EXPECT_EQ(st.typedMinTrivial(), 0u);
    st.minTrivialEntry.inputText = "abc";
    EXPECT_EQ(st.typedMinTrivial(), 0u);
    st.minTrivialEntry.inputText = "-5";
    EXPECT_EQ(st.typedMinTrivial(), 0u);
    st.minTrivialEntry.inputText = "42x";
    EXPECT_EQ(st.typedMinTrivial(), 42u);
}

// A click focuses one box; a click elsewhere in the window blurs it, which is
// what releases the keyboard gate.
TEST(SearchEntryT421, ClickingAnEditboxFocusesItAndClickingAwayBlurs) {
    const UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    TradeskillPanelState st;
    CombatAbilityPanelState abilities;

    const auto box = TradeskillLayout::searchEditRect(l);
    const int bx = box.UpperLeftCorner.X + 2;
    const int by = box.UpperLeftCorner.Y + 2;

    EXPECT_EQ(hitTestTradeskillField(l, st, bx, by), TradeskillField::None)
        << "a closed window must not consume clicks";

    applyTradeskillContainerOpened(st, forgeOpened());
    EXPECT_EQ(hitTestTradeskillField(l, st, bx, by), TradeskillField::SearchText);

    input.processPanelInput(bx, by, /*leftClicked=*/true, l, st, abilities);
    EXPECT_EQ(st.focusedField, TradeskillField::SearchText);
    EXPECT_TRUE(st.searchEntry.inputActive);

    const auto minBox = TradeskillLayout::minTrivialEditRect(l);
    input.processPanelInput(minBox.UpperLeftCorner.X + 2, minBox.UpperLeftCorner.Y + 2,
                            /*leftClicked=*/true, l, st, abilities);
    EXPECT_EQ(st.focusedField, TradeskillField::MinTrivial);
    EXPECT_FALSE(st.searchEntry.inputActive) << "only one caret at a time";

    const auto list = TradeskillLayout::recipeListRect(l);
    input.processPanelInput(list.UpperLeftCorner.X + 4, list.UpperLeftCorner.Y + 4,
                            /*leftClicked=*/true, l, st, abilities);
    EXPECT_EQ(st.focusedField, TradeskillField::None);
    EXPECT_FALSE(st.minTrivialEntry.inputActive);
}

// ===================================================================
// 3 -- the Search button publishes the intent with the TYPED text
// ===================================================================

TEST(SearchEntryT421, TheSearchButtonPublishesWhatWasTyped) {
    const UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    TradeskillPanelState st;
    CombatAbilityPanelState abilities;
    applyTradeskillContainerOpened(st, forgeOpened());

    st.searchEntry.inputText = "iron";
    st.minTrivialEntry.inputText = "21";
    st.maxTrivialEntry.inputText = "188";

    const auto btn = TradeskillLayout::searchButtonRect(l);
    EXPECT_EQ(hitTestTradeskillButton(l, st, btn.UpperLeftCorner.X + 2,
                                      btn.UpperLeftCorner.Y + 2),
              TradeskillButton::Search);

    input.processPanelInput(btn.UpperLeftCorner.X + 2, btn.UpperLeftCorner.Y + 2,
                            /*leftClicked=*/true, l, st, abilities);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* s = std::get_if<eqt::events::RecipeSearchIntent>(&intents[0]);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->query, "iron");
    EXPECT_EQ(s->minTrivial, 21u);
    EXPECT_EQ(s->maxTrivial, 188u);
}

TEST(SearchEntryT421, TheFavouriteButtonsCarryTheirOwnOperation) {
    const UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    TradeskillPanelState st;
    CombatAbilityPanelState abilities;
    applyTradeskillContainerOpened(st, forgeOpened());

    RecipeListEntry e;
    e.recipeId = 4242;
    e.name = "Iron Ration";
    st.recipes.push_back(e);
    st.selectedRecipe = 0;

    struct Case {
        irr::core::rect<irr::s32> rect;
        TradeskillButton button;
        eqt::events::RecipeFavoriteOp op;
    };
    const Case cases[] = {
        {TradeskillLayout::addFavoriteRect(l), TradeskillButton::AddFavorite,
         eqt::events::RecipeFavoriteOp::Add},
        {TradeskillLayout::removeFavoriteRect(l), TradeskillButton::RemoveFavorite,
         eqt::events::RecipeFavoriteOp::Remove},
        {TradeskillLayout::showFavoritesRect(l), TradeskillButton::ShowFavorites,
         eqt::events::RecipeFavoriteOp::Show},
    };

    for (const auto& c : cases) {
        const int x = c.rect.UpperLeftCorner.X + 2;
        const int y = c.rect.UpperLeftCorner.Y + 2;
        EXPECT_EQ(hitTestTradeskillButton(l, st, x, y), c.button);
        input.processPanelInput(x, y, /*leftClicked=*/true, l, st, abilities);
        auto intents = bridge.drainIntents();
        ASSERT_EQ(intents.size(), 1u);
        const auto* f = std::get_if<eqt::events::RecipeFavoriteIntent>(&intents[0]);
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(static_cast<int>(f->op), static_cast<int>(c.op));
        EXPECT_EQ(f->recipeId, 4242u);
    }
}

// The four enumerators T419 deliberately withheld are reachable, and a
// closed window still returns None for all of them.
TEST(SearchEntryT421, TheFourWithheldEnumeratorsAreNowReturnable) {
    const UILayout l = layout800();
    TradeskillPanelState st;
    const auto probes = {
        TradeskillLayout::searchButtonRect(l),
        TradeskillLayout::addFavoriteRect(l),
        TradeskillLayout::removeFavoriteRect(l),
        TradeskillLayout::showFavoritesRect(l),
    };
    for (const auto& r : probes) {
        EXPECT_EQ(hitTestTradeskillButton(l, st, r.UpperLeftCorner.X + 2,
                                          r.UpperLeftCorner.Y + 2),
                  TradeskillButton::None);
    }
    applyTradeskillContainerOpened(st, forgeOpened());
    EXPECT_NE(hitTestTradeskillButton(l, st,
                                      TradeskillLayout::searchButtonRect(l).UpperLeftCorner.X + 2,
                                      TradeskillLayout::searchButtonRect(l).UpperLeftCorner.Y + 2),
              TradeskillButton::None);
}

// The boxes still echo the SERVER-acknowledged query -- what they did before
// they were typeable -- except the one the player is editing right now.
TEST(SearchEntryT421, TheListClearSeedsTheBoxesButNeverTheFocusedOne) {
    TradeskillPanelState st;
    applyTradeskillContainerOpened(st, forgeOpened());

    eqt::state::TradeskillRecipeListClearedData d;
    d.query = "iron";
    d.minTrivial = 21;
    d.maxTrivial = 188;
    d.isFavorites = false;
    applyTradeskillRecipeListCleared(st, d);
    EXPECT_EQ(st.searchEntry.inputText, "iron");
    EXPECT_EQ(st.minTrivialEntry.inputText, "21");
    EXPECT_EQ(st.maxTrivialEntry.inputText, "188");

    st.focusedField = TradeskillField::SearchText;
    st.searchEntry.inputText = "half-typ";
    eqt::state::TradeskillRecipeListClearedData d2;
    d2.query = "iron";
    d2.minTrivial = 0;
    d2.maxTrivial = 0;
    d2.isFavorites = false;
    applyTradeskillRecipeListCleared(st, d2);
    EXPECT_EQ(st.searchEntry.inputText, "half-typ")
        << "a reply must never overwrite the caret the player is holding";
    EXPECT_TRUE(st.minTrivialEntry.inputText.empty());
}
