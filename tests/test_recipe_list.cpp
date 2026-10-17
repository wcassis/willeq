// T419 (board B335) -- the tradeskill recipe list.
//
// The four recipe opcodes were already being SENT (/recipesearch,
// /recipefavorites, /recipedetails, /recipecombine) and
// `MerchantComponent::ZoneProcessRecipeReply`'s whole body was a LOG_DEBUG,
// so every recipe the server returned was parsed and thrown away and the
// tradeskill window's entire left side (COMBW_LeftSideArea) had nothing to
// draw. Pure state + geometry, so no Irrlicht device, no server and no
// DISPLAY -- the shape test_tradeskill_combat_panels.cpp (T415) uses.
//
// What is pinned: the wire bytes decode through the REAL decode the packet
// handler calls (not a copy of it), the decoded payload lands in the panel
// state through the REAL bridge appliers, and every rect on the window's left
// side is the oracle's own, scaled the way T415 scaled its right side.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/merchant/merchant_component.h"
#include "client/state/event_bus.h"
#include "common/packet_structs.h"

#include <irrlicht.h>
#include <cstring>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

UILayout layout800() { return layoutAt(800, 600); }

std::string rectStr(const irr::core::rect<irr::s32>& r) {
    return "{" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + "," +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + "}";
}

// One OP_RecipeReply exactly as eqemu's Client::SendTradeskillSearchResults
// builds it: a two-byte opcode followed by RecipeReply_Struct.
std::vector<uint8_t> recipeReplyPacket(uint32_t objectType, uint32_t someId,
                                       uint32_t componentCount, uint32_t recipeId,
                                       uint32_t trivial, const char* name) {
    std::vector<uint8_t> bytes(2 + sizeof(EQT::RecipeReply_Struct), 0);
    bytes[0] = 0x11;  // the opcode value is irrelevant to the decode
    bytes[1] = 0x22;
    EQT::RecipeReply_Struct s{};
    s.object_type = objectType;
    s.some_id = someId;
    s.component_count = componentCount;
    s.recipe_id = recipeId;
    s.trivial = trivial;
    std::strncpy(s.recipe_name, name, sizeof(s.recipe_name) - 1);
    std::memcpy(bytes.data() + 2, &s, sizeof(s));
    return bytes;
}

eqt::state::TradeskillRecipeReceivedData decoded(const std::vector<uint8_t>& p) {
    eqt::state::TradeskillRecipeReceivedData out;
    EXPECT_TRUE(eqt::merchant::DecodeRecipeReply(p.data() + 2, p.size() - 2, out));
    return out;
}

eqt::state::TradeskillContainerOpenedEvent forgeOpened() {
    eqt::state::TradeskillContainerOpenedEvent d;
    d.isWorldObject = true;
    d.objectId = 4242;
    d.inventorySlot = -1;
    d.containerName = "Forge";
    d.containerType = 15;
    d.slotCount = 10;
    return d;
}

eqt::state::TradeskillRecipeListClearedData searchIssued(const char* q,
                                                         uint32_t lo, uint32_t hi) {
    eqt::state::TradeskillRecipeListClearedData d;
    d.objectType = 15;
    d.query = q;
    d.minTrivial = lo;
    d.maxTrivial = hi;
    d.isFavorites = false;
    return d;
}

}  // namespace

// ===========================================================================
// Leg A -- B335, the recipe list
// ===========================================================================

// The bytes. Every field of eqemu's RecipeReply_Struct has to survive the
// hop; before T419 the handler read three of them into a log line.
TEST(RecipeListT419, WireBytesDecodeToEveryField) {
    const auto p = recipeReplyPacket(15, 0, 4, 12345, 188, "Iron Ration");
    const auto d = decoded(p);

    EXPECT_EQ(d.objectType, 15u);
    EXPECT_EQ(d.someId, 0u);
    EXPECT_EQ(d.componentCount, 4u);
    EXPECT_EQ(d.recipeId, 12345u);
    EXPECT_EQ(d.trivial, 188u);
    EXPECT_EQ(d.name, "Iron Ration");
}

// The server strn0cpy's into a char[64] and does not have to terminate a name
// that fills it. A decode that trusted the NUL would read past the struct.
TEST(RecipeListT419, AnUnterminatedSixtyFourCharacterNameIsBounded) {
    const std::string full(64, 'A');
    auto p = recipeReplyPacket(15, 0, 1, 7, 10, "x");
    std::memcpy(p.data() + 2 + 20, full.data(), 64);  // recipe_name at offset 20

    const auto d = decoded(p);
    EXPECT_EQ(d.name.size(), 64u);
    EXPECT_EQ(d.name, full);
}

// A short packet is not a recipe. The handler's own gate.
TEST(RecipeListT419, AShortPacketDecodesToNothing) {
    eqt::state::TradeskillRecipeReceivedData out;
    out.recipeId = 999;
    std::vector<uint8_t> tooShort(40, 0);
    EXPECT_FALSE(eqt::merchant::DecodeRecipeReply(tooShort.data(), tooShort.size(), out));
    EXPECT_EQ(out.recipeId, 999u) << "a rejected packet must not half-fill the payload";
    EXPECT_FALSE(eqt::merchant::DecodeRecipeReply(nullptr, 84, out));
}

// The appliers: a search request clears, and each reply appends one row.
TEST(RecipeListT419, RepliesAppendToTheListTheRequestCleared) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    EXPECT_TRUE(state.recipes.empty());

    applyTradeskillRecipeListCleared(state, searchIssued("ration", 1, 200));
    EXPECT_EQ(state.searchQuery, "ration");
    EXPECT_EQ(state.minTrivial, 1u);
    EXPECT_EQ(state.maxTrivial, 200u);
    EXPECT_FALSE(state.showingFavorites);

    applyTradeskillRecipeReceived(state, decoded(recipeReplyPacket(15, 0, 4, 100, 21, "Iron Ration")));
    applyTradeskillRecipeReceived(state, decoded(recipeReplyPacket(15, 0, 2, 101, 46, "Fish Roll")));
    ASSERT_EQ(state.recipes.size(), 2u);
    EXPECT_EQ(state.recipes[0].name, "Iron Ration");
    EXPECT_EQ(state.recipes[0].recipeId, 100u);
    EXPECT_EQ(state.recipes[0].trivial, 21u);
    EXPECT_EQ(state.recipes[0].componentCount, 4u);
    EXPECT_EQ(state.recipes[1].name, "Fish Roll");

    // A second request starts a new list -- the server marks no boundary.
    applyTradeskillRecipeListCleared(state, searchIssued("bread", 0, 0));
    EXPECT_TRUE(state.recipes.empty());
    EXPECT_EQ(state.selectedRecipe, -1);
    EXPECT_EQ(state.recipeScroll, 0);
}

// A favourites fetch and a search that overlap must not double a row.
TEST(RecipeListT419, ARepeatedRecipeIdIsNotDuplicated) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    applyTradeskillRecipeListCleared(state, searchIssued("", 0, 0));

    applyTradeskillRecipeReceived(state, decoded(recipeReplyPacket(15, 0, 4, 100, 21, "Iron Ration")));
    applyTradeskillRecipeReceived(state, decoded(recipeReplyPacket(15, 0, 4, 100, 21, "Iron Ration")));
    EXPECT_EQ(state.recipes.size(), 1u);
}

// The list is bounded: a tradeskill search can return hundreds of rows.
TEST(RecipeListT419, TheListIsCappedAndTheCapDoesNotCorruptIt) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    for (uint32_t i = 0; i < TradeskillPanelState::MAX_RECIPES + 50; ++i) {
        applyTradeskillRecipeReceived(
            state, decoded(recipeReplyPacket(15, 0, 1, 1000 + i, 10, "Thing")));
    }
    EXPECT_EQ(state.recipes.size(), TradeskillPanelState::MAX_RECIPES);
    EXPECT_EQ(state.recipes.front().recipeId, 1000u);
}

// Closing the container takes the list with it -- a second forge must not
// show the first one's search results.
TEST(RecipeListT419, ClosingTheContainerClearsTheList) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    applyTradeskillRecipeReceived(state, decoded(recipeReplyPacket(15, 0, 4, 100, 21, "Iron Ration")));
    ASSERT_EQ(state.recipes.size(), 1u);

    eqt::state::TradeskillContainerClosedEvent closed;
    closed.wasWorldObject = true;
    closed.objectId = 4242;
    closed.inventorySlot = -1;
    applyTradeskillContainerClosed(state, closed);
    EXPECT_TRUE(state.recipes.empty());
}

// Scroll bookkeeping: sixteen rows fit, so a seventeenth is what makes the
// list scrollable at all.
TEST(RecipeListT419, ScrollBoundsFollowTheVisibleRowCount) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    EXPECT_EQ(TradeskillLayout::RECIPE_ROWS_VISIBLE, 16);
    EXPECT_EQ(state.maxRecipeScroll(), 0);
    EXPECT_EQ(state.visibleRecipeRows(), 0);

    for (uint32_t i = 0; i < 20; ++i) {
        applyTradeskillRecipeReceived(
            state, decoded(recipeReplyPacket(15, 0, 1, 500 + i, 10, "Thing")));
    }
    EXPECT_EQ(state.visibleRecipeRows(), 16);
    EXPECT_EQ(state.maxRecipeScroll(), 4);

    state.recipeScroll = 4;
    EXPECT_EQ(state.visibleRecipeRows(), 16);
    state.recipeScroll = 18;
    EXPECT_EQ(state.visibleRecipeRows(), 2);
}

// The geometry. COMBW_LeftSideArea's children, quoted in tradeskill_layout.h
// and mapped into the window the way T415 mapped the right side: at 800x600
// B1078's window is {264,48,556,431} (one uniform factor, 384/525 = 73% of
// the oracle's 400x525, in the centre slot at the band's top).
TEST(RecipeListLayoutT419, LeftSideRectsAreTheOraclesScaled) {
    const UILayout l = layout800();

    // COMBW_MinTrivialEdit  absolute (108,4)  50x24
    EXPECT_EQ(rectStr(TradeskillLayout::minTrivialEditRect(l)), "{342,50,378,67}");
    // COMBW_MaxTrivialEdit  absolute (108,30) 50x24
    EXPECT_EQ(rectStr(TradeskillLayout::maxTrivialEditRect(l)), "{342,69,378,86}");
    // COMBW_SearchTextEdit  absolute (108,56) 80x24
    EXPECT_EQ(rectStr(TradeskillLayout::searchEditRect(l)), "{342,88,400,105}");
    // COMBW_SearchButton    absolute (52,84)  100x20
    EXPECT_EQ(rectStr(TradeskillLayout::searchButtonRect(l)), "{301,109,374,123}");
    // COMBW_KnownRecipeText absolute (4,126)  234x20
    EXPECT_EQ(rectStr(TradeskillLayout::recipeListLabelRect(l)), "{266,139,436,153}");
    // COMBW_RecipeList      absolute (4,148)  234x325 -- endpoint-scaled,
    // so the frame's bottom lands on the last row's bottom (not on a
    // separately divided height).
    EXPECT_EQ(rectStr(TradeskillLayout::recipeListRect(l)), "{266,156,437,393}");
    // COMBW_AddFavoriteButton / RemoveFavoriteButton / ShowFavoritesButton
    EXPECT_EQ(rectStr(TradeskillLayout::addFavoriteRect(l)), "{266,412,309,429}");
    EXPECT_EQ(rectStr(TradeskillLayout::removeFavoriteRect(l)), "{312,412,355,429}");
    EXPECT_EQ(rectStr(TradeskillLayout::showFavoritesRect(l)), "{355,412,398,429}");
}

// The rows tile the listbox, in order, and the two columns are the XML's
// <Columns> widths (140 "Recipe Name", 50 "Trivial").
TEST(RecipeListLayoutT419, RowsTileTheListboxAndTheColumnsAreTheOracles) {
    const UILayout l = layout800();
    const auto list = TradeskillLayout::recipeListRect(l);

    irr::s32 prevBottom = list.UpperLeftCorner.Y;
    for (int row = 0; row < TradeskillLayout::RECIPE_ROWS_VISIBLE; ++row) {
        const auto r = TradeskillLayout::recipeRowRect(l, row);
        EXPECT_EQ(r.UpperLeftCorner.X, list.UpperLeftCorner.X) << rectStr(r);
        EXPECT_EQ(r.LowerRightCorner.X, list.LowerRightCorner.X) << rectStr(r);
        EXPECT_EQ(r.UpperLeftCorner.Y, prevBottom) << "row " << row << " " << rectStr(r);
        EXPECT_GE(r.UpperLeftCorner.Y, list.UpperLeftCorner.Y);
        EXPECT_LE(r.LowerRightCorner.Y, list.LowerRightCorner.Y)
            << "row " << row << " runs past the listbox " << rectStr(r);
        prevBottom = r.LowerRightCorner.Y;

        const auto name = TradeskillLayout::recipeNameCellRect(l, row);
        const auto triv = TradeskillLayout::recipeTrivialCellRect(l, row);
        EXPECT_EQ(name.UpperLeftCorner.X, r.UpperLeftCorner.X);
        EXPECT_EQ(name.LowerRightCorner.X, triv.UpperLeftCorner.X)
            << "the Trivial column starts where the Recipe Name column ends";
        EXPECT_LE(triv.LowerRightCorner.X, r.LowerRightCorner.X);
    }
    // Out of range is degenerate, the slotRect() contract.
    EXPECT_EQ(TradeskillLayout::recipeRowRect(l, -1).getWidth(), 0);
    EXPECT_EQ(TradeskillLayout::recipeRowRect(
                  l, TradeskillLayout::RECIPE_ROWS_VISIBLE).getWidth(), 0);
}

// Containment at five resolutions, and the left side must not collide with
// the container half T415 drew on the right.
TEST(RecipeListLayoutT419, TheLeftSideFitsTheWindowAndClearsTheRightSide) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        const UILayout l = layoutAt(rc.w, rc.h);
        // B1078 (GitLab #398): the frame is the tradeskill's own window,
        // not the centre slot it sits in.
        const auto& p = TradeskillLayout::windowRect(l);

        std::vector<irr::core::rect<irr::s32>> left = {
            TradeskillLayout::minTrivialLabelRect(l),
            TradeskillLayout::maxTrivialLabelRect(l),
            TradeskillLayout::searchLabelRect(l),
            TradeskillLayout::minTrivialEditRect(l),
            TradeskillLayout::maxTrivialEditRect(l),
            TradeskillLayout::searchEditRect(l),
            TradeskillLayout::searchButtonRect(l),
            TradeskillLayout::recipeListLabelRect(l),
            TradeskillLayout::recipeListRect(l),
            TradeskillLayout::favoritesLabelRect(l),
            TradeskillLayout::addFavoriteRect(l),
            TradeskillLayout::removeFavoriteRect(l),
            TradeskillLayout::showFavoritesRect(l),
        };
        for (int row = 0; row < TradeskillLayout::RECIPE_ROWS_VISIBLE; ++row) {
            left.push_back(TradeskillLayout::recipeRowRect(l, row));
        }

        for (const auto& r : left) {
            EXPECT_GE(r.UpperLeftCorner.X, p.UpperLeftCorner.X) << rectStr(r);
            EXPECT_GE(r.UpperLeftCorner.Y, p.UpperLeftCorner.Y) << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.X, p.LowerRightCorner.X) << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.Y, p.LowerRightCorner.Y) << rectStr(r);

            for (int i = 0; i < TradeskillPanelState::MAX_SLOTS; ++i) {
                EXPECT_FALSE(r.isRectCollided(TradeskillLayout::slotRect(l, i)))
                    << rc.w << "x" << rc.h << ": " << rectStr(r) << " hits slot " << i;
            }
            EXPECT_FALSE(r.isRectCollided(TradeskillLayout::combineRect(l)))
                << rc.w << "x" << rc.h << ": " << rectStr(r) << " hits Combine";
            EXPECT_FALSE(r.isRectCollided(TradeskillLayout::closeRect(l)))
                << rc.w << "x" << rc.h << ": " << rectStr(r) << " hits Close";
            EXPECT_FALSE(r.isRectCollided(TradeskillLayout::titleRect(l)))
                << rc.w << "x" << rc.h << ": " << rectStr(r) << " hits the title";
        }
    }
}

// A row is clickable, a closed window swallows nothing, and the hit test
// returns the LIST index rather than the on-screen row.
TEST(RecipeListT419, RowHitTestHonoursScrollAndTheClosedWindow) {
    const UILayout l = layout800();
    TradeskillPanelState state;

    const auto row0 = TradeskillLayout::recipeRowRect(l, 0);
    const int px = row0.UpperLeftCorner.X + 4;
    const int py = row0.UpperLeftCorner.Y + 2;
    EXPECT_EQ(hitTestTradeskillRecipe(l, state, px, py), -1)
        << "a closed tradeskill window must not consume clicks";

    applyTradeskillContainerOpened(state, forgeOpened());
    EXPECT_EQ(hitTestTradeskillRecipe(l, state, px, py), -1)
        << "an empty list has no rows to hit";

    for (uint32_t i = 0; i < 20; ++i) {
        applyTradeskillRecipeReceived(
            state, decoded(recipeReplyPacket(15, 0, 1, 700 + i, 10, "Thing")));
    }
    EXPECT_EQ(hitTestTradeskillRecipe(l, state, px, py), 0);
    state.recipeScroll = 4;
    EXPECT_EQ(hitTestTradeskillRecipe(l, state, px, py), 4)
        << "the hit test returns an index into recipes, not the screen row";
    EXPECT_EQ(hitTestTradeskillRecipe(l, state, 0, 0), -1);
}
