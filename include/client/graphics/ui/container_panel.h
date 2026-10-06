/*
 * The era container window -- renderer-side state, draw and hit tests
 * (B899, GitLab #215 / #151 Leg B).
 *
 * ONE window shape, EQUI_Container.xml's ContainerWindow, for BOTH kinds of
 * container the era has:
 *   - a WORLD container (forge, oven, loom on the ground). The server opens
 *     it (OP_ClickObjectAction open=1) and closes it; its ten cells are wire
 *     slots WORLD_BEGIN..WORLD_END (T282).
 *   - a CARRIED container (any bag, and tradeskill kits such as the sewing
 *     kit) in a general slot. The official client opens it locally; fable's
 *     producer is CommandsComponent::RequestToggleBag() (`/bag <slot>`). Its
 *     cells are the bag's sub-slots, inventory_constants.h calcBagSlotId().
 *
 * Will's ruling on #215 (2026-09-23): opening a tradeskill container shows the
 * normal container slots and the Combine-style button and NOTHING ELSE -- the
 * recipe search/list/favourites half of EQUI_TradeskillWnd.xml is out. So the
 * world container's open event feeds THIS panel, not TradeskillPanelState.
 *
 * Several windows can be open at once, as in the official client: one per
 * general slot (eight) plus the one world container the server allows.
 * Window index 0 is the world container; index 1 + (g - GENERAL_BEGIN) is
 * the bag in general slot g. Every window sits at the oracle's one <Location>
 * and the most recently opened is on top (MEASURED, UIGAP-ORACLE 151
 * gestures.tsv row 20).
 *
 * A tradeskill container opens straight in the plain Container view. The
 * official client opens a USABLE one in the recipe window first and its
 * Experiment button switches to this view (UIGAP-ORACLE 215 combine.tsv rows
 * 1-2, 8-9, 11, 15-16); Will ruled the recipe half out, so fable opens in the
 * Experiment view directly (discrepancy flagged to Will by the coordinator).
 *
 * Same static-panel idiom as the bank and loot windows: plain state plus free
 * functions, so a test drives the REAL publisher's data struct through the
 * REAL applier with no Irrlicht device. No window closes itself: every close
 * is a TradeskillCloseIntent, answered by a TradeskillContainerClosed event
 * from the game thread (the server's, for a world container).
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/inventory_constants.h"

namespace eqt { namespace state {
struct TradeskillContainerOpenedEvent;
struct TradeskillContainerClosedEvent;
struct InventorySlotChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One ContainerSlot cell, the same shape as the bank's. */
struct ContainerSlotDisplay {
    bool hasItem = false;
    std::string itemName;
    uint32_t iconId = 0;
    int32_t quantity = 0;
};

/** One open ContainerWindow. */
struct ContainerWindow {
    bool isOpen = false;
    bool isWorldObject = false;
    uint32_t objectId = 0;        // world drop id, for TradeskillCloseIntent
    int16_t inventorySlot = -1;   // the carried bag's general slot, else -1
    std::string name;             // Container_Label
    uint32_t iconId = 0;          // Container_Icon (0: draw the recess only)
    uint8_t containerType = 0;    // ItemInstance::bagType / object type
    uint8_t slotCount = 0;        // how many of the ten cells are real
    uint32_t openSequence = 0;    // draw/ESC order: higher is on top

    ContainerSlotDisplay slots[ContainerLayout::SLOT_COUNT];

    int hoveredSlot = -1;
    bool combineHovered = false;
    bool doneHovered = false;
    bool closeHovered = false;
};

struct ContainerWindowsState {
    // 0 = the world container, 1..8 = general slots 22..29.
    static constexpr int WORLD_WINDOW = 0;
    static constexpr int MAX_WINDOWS = 1 + eqt::inventory::GENERAL_COUNT;
    ContainerWindow windows[MAX_WINDOWS];
    uint32_t nextSequence = 1;
};

/** Which part of a window a point is over. */
enum class ContainerPart : uint8_t {
    None = 0,
    Body,      // inside the window, on no control
    Slot,
    Combine,
    Done,
    CloseBox
};

struct ContainerHit {
    int window = -1;                         // index into windows[]
    ContainerPart part = ContainerPart::None;
    int slot = -1;                           // 0..9 when part == Slot
};

/**
 * The window index for a container: WORLD_WINDOW for a world container,
 * 1 + (g - GENERAL_BEGIN) for a bag in general slot g, -1 for anything else
 * (a bag in the bank, on the cursor, in a trade window: no window here).
 */
int containerWindowIndex(bool isWorldObject, int16_t inventorySlot);

/** How many cells the window draws and hit-tests: slotCount clamped 0..10. */
int containerVisibleSlots(const ContainerWindow& w);

/**
 * Whether the window carries Container_Combine: a world container always, a
 * carried one unless it is a plain storage bag (bag type 0..8). MEASURED --
 * see the citation in container_panel.cpp.
 */
bool containerHasCombine(const ContainerWindow& w);

/**
 * The Combine button's label: "Combine" for every container type, as
 * EQUI_Container.xml says and as measured on every container the official
 * client opened (UIGAP-ORACLE 215 combine.tsv).
 */
const char* containerCombineLabel(const ContainerWindow& w);

/**
 * The wire slot cell `i` addresses: WORLD_BEGIN + i for a world container,
 * calcBagSlotId(inventorySlot, i) for a carried one. SLOT_INVALID when `i`
 * is outside the window's visible cells.
 */
int16_t containerSlotId(const ContainerWindow& w, int i);

/**
 * The container_slot TradeskillCombineIntent carries:
 * SLOT_TRADESKILL_EXPERIMENT_COMBINE for a world container, the bag's own
 * general slot for a carried one -- exactly what `/combine` and
 * `/combine <slot>` push (T278).
 */
int16_t containerCombineSlot(const ContainerWindow& w);

// --- bridge arms (T290's free-function idiom) --------------------------------

/** Open (or refresh) the window the event names. Ignores unaddressable slots. */
void applyContainerOpened(ContainerWindowsState& state,
                          const eqt::state::TradeskillContainerOpenedEvent& data);
/** Close the window the event names. */
void applyContainerClosed(ContainerWindowsState& state,
                          const eqt::state::TradeskillContainerClosedEvent& data);
/**
 * Route one InventorySlotChanged into a container's cells. Returns true for
 * the world range (4000-4009) and the general bag range (251-330), whether or
 * not that window is open (a bag's contents survive its window closing, like
 * the bank's), false for every other slot id.
 */
bool applyContainerSlotChanged(ContainerWindowsState& state,
                               const eqt::state::InventorySlotChangedData& data);

/**
 * The open window on top: the most recently opened. ESC dismisses it first,
 * it is the one drawn and the one a click reaches (they all share one rect).
 * -1 when none is open.
 */
int topmostContainerWindow(const ContainerWindowsState& state);

// --- draw --------------------------------------------------------------------

/** Draw the topmost open window (the others are covered by it). */
void renderContainerWindows(UIRenderer& ui, const UILayout& layout,
                            const ContainerWindowsState& state);

// --- hit tests ---------------------------------------------------------------

/**
 * What the point is over, topmost (most recently opened) window first. A
 * closed window is never hit (T290's rule: a hidden panel eats no click). A
 * cell beyond the window's slotCount is Body, and so is Container_Combine on
 * a window without one (containerHasCombine()).
 */
ContainerHit hitTestContainer(const UILayout& layout,
                              const ContainerWindowsState& state,
                              int mouseX, int mouseY);

}  // namespace Graphics
}  // namespace EQT
