/*
 * The tradeskill container window — renderer-side state and draw (T415,
 * board B321).
 *
 * Before this, `TradeskillContainerOpenedEvent`'s `containerName`,
 * `isWorldObject` and `slotCount` and `TradeskillContainerClosedEvent`'s
 * `wasWorldObject` reached `IrrlichtBridge` and died in a `LOG_TRACE`:
 * fable had no container window for them to describe. They are exactly a
 * container window's title, its close behaviour and its grid size, so this
 * is the panel they were always for.
 *
 * Same static-panel idiom as T290's loot window and T393's purse: a plain
 * state struct plus free functions, so a test drives the REAL publisher's
 * data struct through the REAL applier with no Irrlicht device.
 *
 * WHERE THE CONTENTS COME FROM. Not a new event — the existing one.
 * eqemu's `Object::HandleClick` sends a world container's items one
 * `ITEM_PACKET_WORLD_CONTAINER` packet per occupied index;
 * `InventoryManager::processItemPacket` maps index i to
 * `WORLD_BEGIN + i` (T282) and `zone_dispatch.cpp`'s generic ItemPacket
 * fallback publishes an ENRICHED `InventorySlotChanged` for it — name, icon
 * and quantity included. That event already crossed the bridge; the bridge
 * arm just dropped every slot id outside 0..29. `applyTradeskillSlotChanged`
 * below is the missing branch. `ZoneProcessClearObject` publishes the same
 * event with `hasItem = false` for all ten slots when the server empties the
 * container, so the clear path needs nothing extra either.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct TradeskillContainerOpenedEvent;
struct TradeskillContainerClosedEvent;
struct InventorySlotChangedData;
struct TradeskillRecipeListClearedData;
struct TradeskillRecipeReceivedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One container cell, the same shape as SlotDisplayInfo. */
struct TradeskillSlotDisplay {
    bool hasItem = false;
    std::string itemName;
    uint32_t iconId = 0;
    int32_t quantity = 1;
};

/**
 * One row of COMBW_RecipeList. Every field is `RecipeReply_Struct`'s, and
 * the struct has no more: the server does NOT put the recipe's tradeskill or
 * its component NAMES on this packet (it filters by tradeskill before
 * building it, and sends the component list only in reply to
 * OP_RecipeDetails). A row can therefore show a name, a trivial and a
 * component count, and nothing else -- see the T419 Review.
 */
struct RecipeListEntry {
    uint32_t recipeId = 0;
    std::string name;
    uint32_t trivial = 0;
    uint32_t componentCount = 0;
    uint32_t objectType = 0;
    uint32_t someId = 0;
};

struct TradeskillPanelState {
    bool isOpen = false;

    // The four fields B321 named.
    std::string containerName;   // -> the window title (COMBW_ContainerName)
    bool isWorldObject = false;  // -> which close the Close button sends
    uint8_t slotCount = 0;       // -> how many of the ten cells are drawn
    uint32_t objectId = 0;       // world drop id, for TradeskillCloseIntent
    int16_t inventorySlot = -1;  // inventory container slot, when not a world one

    // COMBW_RequiredIcon0..9 — the oracle has exactly ten.
    static constexpr int MAX_SLOTS = 10;
    TradeskillSlotDisplay slots[MAX_SLOTS];

    int hoveredSlot = -1;

    // --- The left side: COMBW_RecipeList (T419, board B335) ---
    //
    // Before T419 `ZoneProcessRecipeReply`'s whole body was a LOG_DEBUG: the
    // server's recipe list arrived on the wire and was thrown away, so the
    // oracle's entire COMBW_LeftSideArea had nothing to draw. These are the
    // rows it drops into.
    std::vector<RecipeListEntry> recipes;

    // A search on a big tradeskill returns hundreds of rows; the window shows
    // sixteen. The cap is fable's, not the server's -- eqemu streams one
    // packet per matching recipe with no count and no end marker.
    static constexpr size_t MAX_RECIPES = 200;

    int recipeScroll = 0;    // first visible row in `recipes`
    int selectedRecipe = -1; // index into `recipes`, -1 for none
    int hoveredRecipe = -1;  // index into `recipes`, recomputed every frame

    // What the last list request asked for; drawn in COMBW_SearchTextEdit /
    // COMBW_MinTrivialEdit / COMBW_MaxTrivialEdit, which fable does not yet
    // let the player type into (the request comes from /recipesearch).
    std::string searchQuery;
    uint32_t minTrivial = 0;
    uint32_t maxTrivial = 0;
    bool showingFavorites = false;

    /** Highest legal `recipeScroll`, the LootPanelState::maxScrollOffset shape. */
    int maxRecipeScroll() const;

    /** How many rows are actually drawn this frame (0..RECIPE_ROWS_VISIBLE). */
    int visibleRecipeRows() const;
};

/**
 * The window's two CLICKABLE buttons. COMBW_SearchButton and the three
 * favourites buttons are drawn but have no enumerator here on purpose: a
 * name in this enum that no hit test can ever return is exactly the
 * unreached-code defect B335 is an instance of. Board B365 adds them when
 * fable grows a text-entry widget for COMBW_SearchTextEdit.
 */
enum class TradeskillButton : uint8_t {
    None = 0,
    Combine,
    Close
};

/**
 * How many of the ten cells this container actually has, clamped into
 * 0..MAX_SLOTS. The server's `slotCount` is not trusted to be in range;
 * the drawn grid, the hit test and the tests all ask this one body.
 */
int tradeskillVisibleSlots(const TradeskillPanelState& state);

/** The three bridge arms, as free functions (T290's idiom). */
void applyTradeskillContainerOpened(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerOpenedEvent& data);
void applyTradeskillContainerClosed(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerClosedEvent& data);
/**
 * Route one InventorySlotChanged into the container grid. A slot id outside
 * WORLD_BEGIN..WORLD_END is not this panel's and is ignored, so this can be
 * called for every inventory event unconditionally.
 */
void applyTradeskillSlotChanged(
    TradeskillPanelState& state,
    const eqt::state::InventorySlotChangedData& data);

/**
 * T419 -- the two recipe-list arms.
 *
 * `Cleared` is published by the client's own OP_RecipesSearch /
 * OP_RecipesFavorite send, because eqemu marks neither the start nor the end
 * of a reply run (`Client::SendTradeskillSearchResults`, zone/tradeskills.cpp,
 * loops over its query result queuing one OP_RecipeReply each). `Received` is
 * one such packet; it APPENDS, and a repeat of a recipe id already in the
 * list is ignored rather than duplicated.
 */
void applyTradeskillRecipeListCleared(
    TradeskillPanelState& state,
    const eqt::state::TradeskillRecipeListClearedData& data);
void applyTradeskillRecipeReceived(
    TradeskillPanelState& state,
    const eqt::state::TradeskillRecipeReceivedData& data);

/** Render the container window. Only when state.isOpen. */
void renderTradeskillPopup(UIRenderer& ui, const UILayout& layout,
                           const TradeskillPanelState& state);

/**
 * Draw COMBW_LeftSideArea -- the search box, the recipe list and the
 * favourites row (T419, src/client/graphics/ui/tradeskill_recipe_list.cpp).
 * Called by renderTradeskillPopup(); it does NOT re-check `isOpen`.
 */
void renderTradeskillRecipeList(UIRenderer& ui, const UILayout& layout,
                                const TradeskillPanelState& state);

/**
 * Hit-test the two buttons. `TradeskillButton::None` on a miss and on a
 * CLOSED window — a hidden panel must not eat clicks (T290's rule).
 */
TradeskillButton hitTestTradeskillButton(const UILayout& layout,
                                         const TradeskillPanelState& state,
                                         int mouseX, int mouseY);

/**
 * Hit-test the container cells; returns 0..slotCount-1 or -1. The twin of
 * renderTradeskillPopup()'s cell loop — same geometry, same bound — so the
 * cell the cursor is over and the cell a click addresses cannot disagree
 * (T277's rule).
 */
int hitTestTradeskillSlot(const UILayout& layout,
                          const TradeskillPanelState& state,
                          int mouseX, int mouseY);

/**
 * Hit-test COMBW_RecipeList. Returns an index into `state.recipes` (i.e.
 * scroll-adjusted, not the on-screen row) or -1. Same closed-window rule as
 * the two above.
 */
int hitTestTradeskillRecipe(const UILayout& layout,
                            const TradeskillPanelState& state,
                            int mouseX, int mouseY);

} // namespace Graphics
} // namespace EQT
