/*
 * B895 (GitLab #211) -- the Tracking Window's one game -> renderer feed.
 *
 * Its own family header (T451's split) rather than a row in
 * event_payloads_ui.h, so the unit's payload lives beside nothing it does not
 * own. event_bus.h includes it with the other six.
 *
 * The GAME thread owns the tracking list (eqt::tracking::TrackingController,
 * fed by the OP_Track reply, the /track* commands and the window's own
 * controls). Every change publishes the WHOLE visible list -- already
 * filtered, sorted and con-coloured -- so the panel only draws it and never
 * re-derives a rule. The combobox labels are composed game-side from
 * eqstr_us.txt for the same reason the item display's lines are: the renderer
 * has no string table.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eqt {
namespace state {

/** One TRW_TrackingList row. */
struct TrackingRowData {
    uint16_t spawnId = 0;
    std::string name;
    // The row's con colour (eqt::ConTierRgb of the shared con table).
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    bool isPlayer = false;
};

struct TrackingListChangedData {
    // Filtered + sorted, in the order TRW_TrackingList draws them.
    std::vector<TrackingRowData> rows;
    // True only when this publish answers an OP_Track request -- the
    // official flow opens the window on the reply. A settings change made
    // with the window shut does not open it.
    bool openWindow = false;
    // TRW_Filter{Red,Yellow,White,Blue,LightBlue,Green}Button, in the XML's
    // own element order; true = that con colour is shown.
    std::array<bool, 6> filters{{true, true, true, true, true, true}};
    // Index into sortOptions / playersOptions (eqt::tracking::TrackSort /
    // TrackPlayers numbering).
    uint8_t sort = 0;
    uint8_t players = 0;
    // The two comboboxes' item text, from eqstr (see tracking_state.h).
    std::vector<std::string> sortOptions;
    std::vector<std::string> playersOptions;
    // The target "You begin tracking" named, 0 when none.
    uint16_t trackedSpawnId = 0;
};

}  // namespace state
}  // namespace eqt
