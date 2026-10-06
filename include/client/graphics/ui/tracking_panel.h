/*
 * The Tracking Window -- renderer-side state, draw, hit tests and input
 * (B895, GitLab #211).
 *
 * WHAT WAS MISSING. The OP_Track reply was printed into chat; the official
 * client's Tracking Window (EQUI_TrackingWnd.xml) did not exist. Geometry is
 * include/client/graphics/ui/tracking_layout.h, which quotes the XML; this
 * panel computes no rect.
 *
 * THE FEED is GameEventType::TrackingListChanged, which the GAME thread's
 * eqt::tracking::TrackingController publishes whole -- rows already filtered,
 * sorted and con-coloured, combobox text already composed from eqstr. The
 * panel draws it and owns only client-view state: which row is selected,
 * the list's scroll, which combobox is dropped down, and whether the window
 * is shown. The window opens on an OP_Track reply (the official flow: press
 * the Tracking ability, the list comes back, the window appears) and closes
 * on TRW_DoneButton ("Cancel"), its close box or ESC -- all local, because
 * nothing about the window is server-owned. Closing it does NOT stop
 * tracking; /stoptracking does.
 *
 * INPUT (processTrackingInput) pushes ONE intent type, TrackingIntent:
 *   a TRW_Filter*Button  -> ToggleFilter   (/trackfilter <colour>)
 *   a sort combobox item -> SetSort        (/tracksort <type>)
 *   a players item       -> SetPlayers     (/trackplayers <on|off|group>)
 *   TRW_TrackButton      -> Track(selected row's spawn id)  (/track <name>)
 * A setting is NOT changed optimistically: the panel shows the new value
 * when the controller's republished TrackingListChanged comes back.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/tracking_layout.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_payloads_tracking.h"

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace EQT {
namespace Graphics {

class UIRenderer;

enum class TrackingCombo : uint8_t { None = 0, Sort, Players };

struct TrackingPanelState {
    bool isOpen = false;

    // --- the feed (TrackingListChanged), verbatim ---
    std::vector<eqt::state::TrackingRowData> rows;
    std::array<bool, TrackingLayout::FILTER_COUNT> filters{{true, true, true, true, true, true}};
    uint8_t sort = 0;
    uint8_t players = 0;
    std::vector<std::string> sortOptions;
    std::vector<std::string> playersOptions;
    uint16_t trackedSpawnId = 0;

    // --- client-view state ---
    uint16_t selectedSpawnId = 0;   // by id, so a re-sort keeps the selection
    int scrollOffset = 0;           // rows scrolled off the top
    TrackingCombo openCombo = TrackingCombo::None;

    // --- hover (written by processTrackingInput, read by the draw) ---
    int hoveredRow = -1;            // index into rows
    int hoveredFilter = -1;
    int hoveredComboItem = -1;
    bool trackHovered = false;
    bool doneHovered = false;
    bool closeHovered = false;

    int maxScrollOffset(const UILayout& layout) const {
        const int extra = static_cast<int>(rows.size()) - TrackingLayout::visibleRows(layout);
        return extra > 0 ? extra : 0;
    }
};

/** The TrackingListChanged bridge arm. */
void applyTrackingListChanged(TrackingPanelState& state,
                              const eqt::state::TrackingListChangedData& data,
                              const UILayout& layout);

/** Draw the window. Only when state.isOpen. */
void renderTrackingWindow(UIRenderer& ui, const UILayout& layout,
                          const TrackingPanelState& state);

// --- hit tests (-1 / false on a miss or a closed window) ---
int hitTestTrackingRow(const UILayout& layout, const TrackingPanelState& state, int mx, int my);
int hitTestTrackingFilter(const UILayout& layout, const TrackingPanelState& state, int mx, int my);
/** An item of the DROPPED-DOWN combobox list. */
int hitTestTrackingComboItem(const UILayout& layout, const TrackingPanelState& state, int mx, int my);
bool trackingContains(const UILayout& layout, const TrackingPanelState& state, int mx, int my);

/**
 * Hover + click for the window. Returns true if the position is the
 * window's (the click must not fall through to the 3D scene). `bridge` may
 * be null (a test of hover alone).
 */
bool processTrackingInput(eqt::bridge::GameStateBridge* bridge, int mx, int my,
                          bool leftClicked, const UILayout& layout,
                          TrackingPanelState& state);

/**
 * A wheel notch over TRW_TrackingList (Style_VScroll true). `wheelDelta` > 0
 * is up -- towards the top of the list. Returns true if consumed.
 */
bool scrollTrackingList(TrackingPanelState& state, const UILayout& layout,
                        int mx, int my, float wheelDelta);

}  // namespace Graphics
}  // namespace EQT
