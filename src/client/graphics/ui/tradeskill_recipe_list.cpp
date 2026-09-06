/*
 * T419 (board B335) — the tradeskill window's LEFT side: the recipe search
 * box, the known-recipe list and the favourites row.
 *
 * A separate TU from tradeskill_panel.cpp for the same reason that file is
 * separate from static_panels.cpp: substantial new code goes in a new TU
 * (M00). The geometry is NOT here — every rect comes from
 * include/client/graphics/ui/tradeskill_layout.h, which quotes
 * EQUI_TradeskillWnd.xml's COMBW_LeftSideArea and writes out the anchor
 * arithmetic.
 *
 * WHAT WAS BROKEN. `MerchantComponent::ZoneProcessRecipeReply` parsed
 * `RecipeReply_Struct` and its entire body was one `LOG_DEBUG`: the four
 * recipe opcodes were being SENT (`/recipesearch`, `/recipefavorites`,
 * `/recipedetails`, `/recipecombine`), the server was answering, and every
 * answer was thrown away. This file is the consumer that was missing.
 *
 * WHAT THE SERVER ACTUALLY SENDS, from eqemu common/eq_packet_structs.h:4313
 * (identical in common/patches/titanium_structs.h:2798):
 *
 *     //one sent for each item, from server in reply to favorites or search
 *     struct RecipeReply_Struct {
 *         uint32 object_type;
 *         uint32 some_id;   //same as in favorites
 *         uint32 component_count;
 *         uint32 recipe_id;
 *         uint32 trivial;
 *         char recipe_name[64];
 *     /*84*\/
 *     };
 *
 * `Client::SendTradeskillSearchResults` (zone/tradeskills.cpp:866) queues ONE
 * of those per matching row and never marks the start or the end of the run,
 * so the only honest list boundary is the client's own request — see
 * `applyTradeskillRecipeListCleared`.
 *
 * NOT ON THE WIRE, and therefore not drawable here: the recipe's TRADESKILL
 * (the server filters by it in SQL before building the packet) and its
 * COMPONENT NAMES (sent only in reply to OP_RecipeDetails, as a separate
 * hand-packed buffer). A row shows a name, a trivial and a component count.
 *
 * INERT BY DESIGN, boarded rather than faked: the three editboxes
 * (COMBW_MinTrivialEdit / COMBW_MaxTrivialEdit / COMBW_SearchTextEdit), the
 * COMBW_SearchButton and the three favourites buttons are DRAWN — they are
 * the oracle's own chrome and leaving a hole where they belong is what B335
 * complained about — but they are not clickable, because fable's static UI
 * has no text-entry widget outside the chat box. The search itself is
 * reached from `/recipesearch`, and the edit boxes echo what that command
 * last asked for. Board B365.
 */

#include "client/graphics/ui/tradeskill_panel.h"

#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"

#include <fmt/format.h>
#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {

// A sunken field, the shape WDT_Inner gives every Editbox in this window.
void drawField(UIRenderer& ui, TextBatch* tb,
               const irr::core::rect<irr::s32>& r,
               const std::string& text) {
    ui.drawRect(r, irr::video::SColor(200, 18, 18, 24));
    ui.drawRect({r.UpperLeftCorner.X, r.UpperLeftCorner.Y,
                 r.LowerRightCorner.X, r.UpperLeftCorner.Y + 1},
                irr::video::SColor(255, 30, 30, 38));
    ui.drawRect({r.UpperLeftCorner.X, r.LowerRightCorner.Y - 1,
                 r.LowerRightCorner.X, r.LowerRightCorner.Y},
                irr::video::SColor(255, 90, 90, 110));
    if (tb && !text.empty()) {
        tb->addText(text, r.UpperLeftCorner.X + 2, r.UpperLeftCorner.Y + 1,
                    irr::video::SColor(255, 230, 230, 230));
    }
}

// The same button shape tradeskill_panel.cpp's Combine/Close use. Drawn
// dimmed, because none of the left side's buttons is wired (see the header).
void drawInertButton(UIRenderer& ui, TextBatch* tb,
                     const irr::core::rect<irr::s32>& r,
                     const std::string& label) {
    ui.drawRect(r, irr::video::SColor(180, 35, 35, 40));
    ui.drawRect({r.UpperLeftCorner.X, r.UpperLeftCorner.Y,
                 r.LowerRightCorner.X, r.UpperLeftCorner.Y + 1},
                irr::video::SColor(255, 80, 80, 100));
    if (tb) {
        tb->addTextCentered(label, r, irr::video::SColor(255, 120, 120, 120));
    }
}

}  // namespace

int TradeskillPanelState::maxRecipeScroll() const {
    const int over = static_cast<int>(recipes.size()) -
                     TradeskillLayout::RECIPE_ROWS_VISIBLE;
    return over > 0 ? over : 0;
}

int TradeskillPanelState::visibleRecipeRows() const {
    const int remaining = static_cast<int>(recipes.size()) - recipeScroll;
    if (remaining <= 0) return 0;
    return std::min(remaining, TradeskillLayout::RECIPE_ROWS_VISIBLE);
}

void applyTradeskillRecipeListCleared(
    TradeskillPanelState& state,
    const eqt::state::TradeskillRecipeListClearedData& data) {
    state.recipes.clear();
    state.recipeScroll = 0;
    state.selectedRecipe = -1;
    state.hoveredRecipe = -1;
    state.searchQuery = data.query;
    state.minTrivial = data.minTrivial;
    state.maxTrivial = data.maxTrivial;
    state.showingFavorites = data.isFavorites;
}

void applyTradeskillRecipeReceived(
    TradeskillPanelState& state,
    const eqt::state::TradeskillRecipeReceivedData& data) {
    if (state.recipes.size() >= TradeskillPanelState::MAX_RECIPES) {
        return;
    }
    // The server can repeat a recipe id across a favourites fetch and a
    // search that overlap; a row is a recipe, not a packet.
    for (const auto& r : state.recipes) {
        if (r.recipeId == data.recipeId) return;
    }
    RecipeListEntry e;
    e.recipeId = data.recipeId;
    e.name = data.name;
    e.trivial = data.trivial;
    e.componentCount = data.componentCount;
    e.objectType = data.objectType;
    e.someId = data.someId;
    state.recipes.push_back(std::move(e));
}

void renderTradeskillRecipeList(UIRenderer& ui, const UILayout& layout,
                                const TradeskillPanelState& state) {
    auto* tb = ui.getTextBatch();
    const irr::video::SColor kLabel(255, 255, 255, 255);

    // --- COMBW_SearchArea: three labels, three editboxes, one button ---
    if (tb) {
        // <Text> verbatim from the XML.
        tb->addText("Min. Trivial:",
                    TradeskillLayout::minTrivialLabelRect(layout).UpperLeftCorner.X,
                    TradeskillLayout::minTrivialLabelRect(layout).UpperLeftCorner.Y,
                    kLabel);
        tb->addText("Max. Trivial:",
                    TradeskillLayout::maxTrivialLabelRect(layout).UpperLeftCorner.X,
                    TradeskillLayout::maxTrivialLabelRect(layout).UpperLeftCorner.Y,
                    kLabel);
        tb->addText("Partial Name:",
                    TradeskillLayout::searchLabelRect(layout).UpperLeftCorner.X,
                    TradeskillLayout::searchLabelRect(layout).UpperLeftCorner.Y,
                    kLabel);
    }
    drawField(ui, tb, TradeskillLayout::minTrivialEditRect(layout),
              state.minTrivial ? fmt::format("{}", state.minTrivial) : std::string());
    drawField(ui, tb, TradeskillLayout::maxTrivialEditRect(layout),
              state.maxTrivial ? fmt::format("{}", state.maxTrivial) : std::string());
    drawField(ui, tb, TradeskillLayout::searchEditRect(layout), state.searchQuery);
    drawInertButton(ui, tb, TradeskillLayout::searchButtonRect(layout),
                    "Search Recipes");

    // --- COMBW_RecipeListArea ---
    if (tb) {
        const auto lbl = TradeskillLayout::recipeListLabelRect(layout);
        tb->addText("Known Matching Recipes:",
                    lbl.UpperLeftCorner.X, lbl.UpperLeftCorner.Y, kLabel);
    }

    const auto listRect = TradeskillLayout::recipeListRect(layout);
    ui.drawRect(listRect, irr::video::SColor(200, 18, 18, 24));

    const int rows = state.visibleRecipeRows();
    for (int row = 0; row < rows; ++row) {
        const int idx = state.recipeScroll + row;
        const auto rowRect = TradeskillLayout::recipeRowRect(layout, row);
        if (idx == state.selectedRecipe) {
            ui.drawRect(rowRect, irr::video::SColor(120, 60, 90, 150));
        } else if (idx == state.hoveredRecipe) {
            ui.drawRect(rowRect, irr::video::SColor(50, 255, 255, 255));
        }
        if (!tb) continue;
        const auto& e = state.recipes[static_cast<size_t>(idx)];
        tb->addText(e.name,
                    TradeskillLayout::recipeNameCellRect(layout, row).UpperLeftCorner.X + 2,
                    rowRect.UpperLeftCorner.Y,
                    irr::video::SColor(255, 220, 220, 220));
        tb->addText(fmt::format("{}", e.trivial),
                    TradeskillLayout::recipeTrivialCellRect(layout, row).UpperLeftCorner.X + 2,
                    rowRect.UpperLeftCorner.Y,
                    irr::video::SColor(255, 180, 200, 220));
    }

    // --- COMBW_FavoritesArea ---
    if (tb) {
        const auto lbl = TradeskillLayout::favoritesLabelRect(layout);
        tb->addText("Favorite Recipes:",
                    lbl.UpperLeftCorner.X, lbl.UpperLeftCorner.Y, kLabel);
    }
    drawInertButton(ui, tb, TradeskillLayout::addFavoriteRect(layout), "Add");
    drawInertButton(ui, tb, TradeskillLayout::removeFavoriteRect(layout), "Remove");
    drawInertButton(ui, tb, TradeskillLayout::showFavoritesRect(layout), "Show");
}

int hitTestTradeskillRecipe(const UILayout& layout,
                            const TradeskillPanelState& state,
                            int mouseX, int mouseY) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    const int rows = state.visibleRecipeRows();
    for (int row = 0; row < rows; ++row) {
        if (TradeskillLayout::recipeRowRect(layout, row).isPointInside(p)) {
            return state.recipeScroll + row;
        }
    }
    return -1;
}

}  // namespace Graphics
}  // namespace EQT
