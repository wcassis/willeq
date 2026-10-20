/*
 * B895 (GitLab #211) -- the Tracking Window.
 *
 * Its own TU (M00). The geometry is in
 * include/client/graphics/ui/tracking_layout.h, which quotes
 * EQUI_TrackingWnd.xml; this file computes no rect.
 */

#include "client/graphics/ui/tracking_panel.h"
#include "client/graphics/ui/window_flags.h"  // B1002

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kTitle(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kDisabledText(255, 120, 120, 128);
const irr::video::SColor kInner(200, 16, 16, 20);         // WDT_Inner
const irr::video::SColor kButton(200, 56, 62, 84);
const irr::video::SColor kButtonHover(220, 120, 60, 60);
const irr::video::SColor kFilteredOut(230, 150, 130, 30);  // the oracle's yellow highlight
const irr::video::SColor kSelected(120, 90, 110, 170);

int clampScroll(const TrackingPanelState& s, const UILayout& layout, int v) {
    const int maxOff = s.maxScrollOffset(layout);
    return v < 0 ? 0 : (v > maxOff ? maxOff : v);
}

const std::vector<std::string>& comboItems(const TrackingPanelState& s, TrackingCombo c) {
    static const std::vector<std::string> kNone;
    if (c == TrackingCombo::Sort) return s.sortOptions;
    if (c == TrackingCombo::Players) return s.playersOptions;
    return kNone;
}

irr::core::rect<irr::s32> comboRect(const UILayout& layout, TrackingCombo c) {
    return c == TrackingCombo::Sort ? TrackingLayout::sortComboRect(layout)
                                    : TrackingLayout::playersComboRect(layout);
}

std::string itemText(const std::vector<std::string>& items, int i) {
    return (i >= 0 && i < static_cast<int>(items.size())) ? items[static_cast<size_t>(i)]
                                                          : std::string();
}

// UIRenderer flushes every quad before any text, so a dropped-down list
// cannot cover the TEXT of the controls beneath it. Their text is withheld
// while the list overlaps them -- otherwise "Track Players:" reads through.
irr::core::rect<irr::s32> openListRect(const UILayout& layout, const TrackingPanelState& s) {
    if (s.openCombo == TrackingCombo::None) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return TrackingLayout::comboListRect(layout, comboRect(layout, s.openCombo),
                                         static_cast<int>(comboItems(s, s.openCombo).size()));
}

TextBatch* textUnless(TextBatch* tb, const irr::core::rect<irr::s32>& under,
                      const irr::core::rect<irr::s32>& r) {
    return (under.getArea() > 0 && under.isRectCollided(r)) ? nullptr : tb;
}

void drawButton(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
                const char* text, bool hovered, bool enabled) {
    ui.drawRect(r, hovered && enabled ? kButtonHover : kButton);
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(text, r, enabled ? kText : kDisabledText);
}

void drawCombo(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
               const std::string& value) {
    ui.drawRect(r, kInner);
    ui.drawFrame(r, kFrame);
    // BDT_Combo: the drop button at the right end of the box.
    const irr::core::rect<irr::s32> drop(r.LowerRightCorner.X - r.getHeight(), r.UpperLeftCorner.Y,
                                         r.LowerRightCorner.X, r.LowerRightCorner.Y);
    ui.drawRect(drop, kButton);
    ui.drawFrame(drop, kFrame);
    if (!tb) return;
    tb->addTextCentered("v", drop, kText);
    tb->addText(value, r.UpperLeftCorner.X + 4,
                r.UpperLeftCorner.Y + (r.getHeight() - TrackingLayout::ROW_H) / 2 + 1, kText);
}

}  // namespace

// --- bridge arm --------------------------------------------------------------

void applyTrackingListChanged(TrackingPanelState& state,
                              const eqt::state::TrackingListChangedData& data,
                              const UILayout& layout) {
    state.rows = data.rows;
    state.filters = data.filters;
    state.sort = data.sort;
    state.players = data.players;
    state.sortOptions = data.sortOptions;
    state.playersOptions = data.playersOptions;
    state.trackedSpawnId = data.trackedSpawnId;
    if (data.openWindow) {
        // A fresh list: the official flow opens the window on the reply.
        state.isOpen = true;
        state.scrollOffset = 0;
    }
    const bool selectionSurvives = std::any_of(
        state.rows.begin(), state.rows.end(),
        [&](const eqt::state::TrackingRowData& r) { return r.spawnId == state.selectedSpawnId; });
    if (!selectionSurvives) state.selectedSpawnId = 0;
    state.scrollOffset = clampScroll(state, layout, state.scrollOffset);
    state.hoveredRow = -1;
}

// --- draw ----------------------------------------------------------------------

void renderTrackingWindow(UIRenderer& ui, const UILayout& layout,
                          const TrackingPanelState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();
    ui.drawPanelFor(TrackingLayout::windowRect(layout), UIWindow::Tracking);

    // --- Style_Titlebar / Style_Closebox ---
    const auto title = TrackingLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText(TrackingLayout::TITLE_TEXT, title.UpperLeftCorner.X + 6,
                    title.UpperLeftCorner.Y + 2, kTitle);
    }
    const auto close = TrackingLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered ? kButtonHover : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    // --- TRW_FiltersLabel + the six Style_Checkbox colour buttons ---
    if (tb) tb->addTextCentered(TrackingLayout::FILTERS_TEXT, TrackingLayout::filtersLabelRect(layout), kText);
    for (int i = 0; i < TrackingLayout::FILTER_COUNT; ++i) {
        const auto r = TrackingLayout::filterButtonRect(layout, i);
        // MEASURED (tracking.tsv row 4): a button shows a highlighted
        // (yellow) background while its colour is FILTERED OUT.
        const bool filteredOut = !state.filters[static_cast<size_t>(i)];
        ui.drawRect(r, state.hoveredFilter == i ? kButtonHover : (filteredOut ? kFilteredOut : kButton));
        ui.drawFrame(r, filteredOut ? irr::video::SColor(255, 230, 200, 60) : kFrame);
        if (tb) {
            const auto& c = TrackingLayout::FILTER_TEXT_COLOR[i];
            tb->addTextCentered(TrackingLayout::FILTER_TEXT[i], r,
                                irr::video::SColor(255, c.r, c.g, c.b));
        }
    }

    // --- TRW_TrackingList: one column, con-coloured rows, VScroll ---
    const auto list = TrackingLayout::listRect(layout);
    ui.drawRect(list, kInner);
    ui.drawFrame(list, kFrame);
    const int visible = TrackingLayout::visibleRows(layout);
    for (int v = 0; v < visible; ++v) {
        const int idx = state.scrollOffset + v;
        if (idx >= static_cast<int>(state.rows.size())) break;
        const auto& row = state.rows[static_cast<size_t>(idx)];
        const auto rr = TrackingLayout::rowRect(layout, v);
        if (row.spawnId == state.selectedSpawnId) {
            ui.drawRect(rr, kSelected);
        } else if (idx == state.hoveredRow) {
            ui.drawRect(rr, irr::video::SColor(60, 255, 255, 255));
        }
        if (tb) {
            tb->addText(row.name, rr.UpperLeftCorner.X + 3, rr.UpperLeftCorner.Y + 1,
                        irr::video::SColor(255, row.r, row.g, row.b));
        }
    }
    // The scroll thumb, only when there is somewhere to scroll.
    const int maxOff = state.maxScrollOffset(layout);
    if (maxOff > 0 && visible > 0) {
        const irr::s32 trackH = list.getHeight();
        const irr::s32 thumbH = std::max<irr::s32>(8, trackH * visible / static_cast<int>(state.rows.size()));
        const irr::s32 thumbY = list.UpperLeftCorner.Y + (trackH - thumbH) * state.scrollOffset / maxOff;
        ui.drawRect(irr::core::rect<irr::s32>(list.LowerRightCorner.X - 5, thumbY,
                                              list.LowerRightCorner.X - 1, thumbY + thumbH),
                    irr::video::SColor(200, 140, 140, 150));
    }

    // --- the two labels and comboboxes ---
    const auto under = openListRect(layout, state);
    const auto sl = TrackingLayout::sortLabelRect(layout);
    const auto pl = TrackingLayout::playersLabelRect(layout);
    if (auto* t = textUnless(tb, under, sl)) {
        t->addText(TrackingLayout::SORT_LABEL_TEXT, sl.UpperLeftCorner.X, sl.UpperLeftCorner.Y, kText);
    }
    if (auto* t = textUnless(tb, under, pl)) {
        t->addText(TrackingLayout::PLAYERS_LABEL_TEXT, pl.UpperLeftCorner.X, pl.UpperLeftCorner.Y, kText);
    }
    const auto sc = TrackingLayout::sortComboRect(layout);
    const auto pc = TrackingLayout::playersComboRect(layout);
    drawCombo(ui, textUnless(tb, under, sc), sc, itemText(state.sortOptions, state.sort));
    drawCombo(ui, textUnless(tb, under, pc), pc, itemText(state.playersOptions, state.players));

    // --- TRW_TrackButton (greyed with nothing selected) and TRW_DoneButton ---
    const auto tr = TrackingLayout::trackButtonRect(layout);
    const auto dn = TrackingLayout::doneButtonRect(layout);
    drawButton(ui, textUnless(tb, under, tr), tr, TrackingLayout::TRACK_TEXT,
               state.trackHovered, state.selectedSpawnId != 0);
    drawButton(ui, textUnless(tb, under, dn), dn, TrackingLayout::CANCEL_TEXT,
               state.doneHovered, true);

    // --- the dropped-down combobox list, last: it overlays the window ---
    if (state.openCombo != TrackingCombo::None) {
        const auto& items = comboItems(state, state.openCombo);
        const auto box = TrackingLayout::comboListRect(layout, comboRect(layout, state.openCombo),
                                                       static_cast<int>(items.size()));
        ui.drawRect(box, irr::video::SColor(240, 20, 20, 26));
        ui.drawFrame(box, kFrame);
        const int current = state.openCombo == TrackingCombo::Sort ? state.sort : state.players;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const irr::core::rect<irr::s32> ir(box.UpperLeftCorner.X,
                                               box.UpperLeftCorner.Y + i * TrackingLayout::ROW_H,
                                               box.LowerRightCorner.X,
                                               box.UpperLeftCorner.Y + (i + 1) * TrackingLayout::ROW_H);
            if (ir.LowerRightCorner.Y > box.LowerRightCorner.Y) break;
            if (i == state.hoveredComboItem || i == current) {
                ui.drawRect(ir, i == state.hoveredComboItem ? kSelected : irr::video::SColor(80, 90, 110, 170));
            }
            if (tb) tb->addText(items[static_cast<size_t>(i)], ir.UpperLeftCorner.X + 4, ir.UpperLeftCorner.Y + 1, kText);
        }
    }
}

// --- hit tests -------------------------------------------------------------------

int hitTestTrackingRow(const UILayout& layout, const TrackingPanelState& state, int mx, int my) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mx, my);
    if (!TrackingLayout::listRect(layout).isPointInside(p)) return -1;
    const int visible = TrackingLayout::visibleRows(layout);
    for (int v = 0; v < visible; ++v) {
        if (!TrackingLayout::rowRect(layout, v).isPointInside(p)) continue;
        const int idx = state.scrollOffset + v;
        return idx < static_cast<int>(state.rows.size()) ? idx : -1;
    }
    return -1;
}

int hitTestTrackingFilter(const UILayout& layout, const TrackingPanelState& state, int mx, int my) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mx, my);
    for (int i = 0; i < TrackingLayout::FILTER_COUNT; ++i) {
        if (TrackingLayout::filterButtonRect(layout, i).isPointInside(p)) return i;
    }
    return -1;
}

int hitTestTrackingComboItem(const UILayout& layout, const TrackingPanelState& state, int mx, int my) {
    if (!state.isOpen || state.openCombo == TrackingCombo::None) return -1;
    const auto& items = comboItems(state, state.openCombo);
    const auto box = TrackingLayout::comboListRect(layout, comboRect(layout, state.openCombo),
                                                   static_cast<int>(items.size()));
    if (!box.isPointInside(irr::core::vector2di(mx, my))) return -1;
    const int i = (my - box.UpperLeftCorner.Y) / TrackingLayout::ROW_H;
    return (i >= 0 && i < static_cast<int>(items.size())) ? i : -1;
}

bool trackingContains(const UILayout& layout, const TrackingPanelState& state, int mx, int my) {
    if (!state.isOpen) return false;
    return TrackingLayout::windowRect(layout).isPointInside(irr::core::vector2di(mx, my));
}

// --- input -------------------------------------------------------------------

bool processTrackingInput(eqt::bridge::GameStateBridge* bridge, int mx, int my,
                          bool leftClicked, const UILayout& layout,
                          TrackingPanelState& state) {
    state.hoveredRow = -1;
    state.hoveredFilter = -1;
    state.hoveredComboItem = -1;
    state.trackHovered = false;
    state.doneHovered = false;
    state.closeHovered = false;
    if (!state.isOpen) return false;

    using Op = eqt::events::TrackingIntentOp;
    const irr::core::vector2di p(mx, my);

    // A dropped-down list is on top of everything in the window.
    if (state.openCombo != TrackingCombo::None) {
        const int item = hitTestTrackingComboItem(layout, state, mx, my);
        if (item >= 0) {
            state.hoveredComboItem = item;
            if (leftClicked) {
                if (bridge) {
                    eqt::events::TrackingIntent intent;
                    intent.op = state.openCombo == TrackingCombo::Sort ? Op::SetSort : Op::SetPlayers;
                    intent.value = static_cast<uint8_t>(item);
                    bridge->pushIntent(intent);
                }
                state.openCombo = TrackingCombo::None;
            }
            return true;
        }
        // Any other click closes the list (and is otherwise handled below).
        if (leftClicked) state.openCombo = TrackingCombo::None;
    }

    if (TrackingLayout::closeBoxRect(layout).isPointInside(p) ||
        TrackingLayout::doneButtonRect(layout).isPointInside(p)) {
        const bool onClose = TrackingLayout::closeBoxRect(layout).isPointInside(p);
        state.closeHovered = onClose;
        state.doneHovered = !onClose;
        if (leftClicked) {
            // A pure client view: closing it cannot desync anything, and it
            // does not stop tracking.
            state.isOpen = false;
            state.openCombo = TrackingCombo::None;
        }
        return true;
    }

    const int filter = hitTestTrackingFilter(layout, state, mx, my);
    if (filter >= 0) {
        state.hoveredFilter = filter;
        if (leftClicked && bridge) {
            eqt::events::TrackingIntent intent;
            intent.op = Op::ToggleFilter;
            intent.value = static_cast<uint8_t>(filter);
            bridge->pushIntent(intent);
        }
        return true;
    }

    if (TrackingLayout::sortComboRect(layout).isPointInside(p) ||
        TrackingLayout::playersComboRect(layout).isPointInside(p)) {
        if (leftClicked) {
            const TrackingCombo which = TrackingLayout::sortComboRect(layout).isPointInside(p)
                                            ? TrackingCombo::Sort : TrackingCombo::Players;
            state.openCombo = which;
        }
        return true;
    }

    if (TrackingLayout::trackButtonRect(layout).isPointInside(p)) {
        state.trackHovered = true;
        if (leftClicked && bridge && state.selectedSpawnId != 0) {
            eqt::events::TrackingIntent intent;
            intent.op = Op::Track;
            intent.spawnId = state.selectedSpawnId;
            bridge->pushIntent(intent);
        }
        return true;
    }

    const int row = hitTestTrackingRow(layout, state, mx, my);
    if (row >= 0) {
        state.hoveredRow = row;
        // Selection is purely client side; the server hears nothing until Track.
        if (leftClicked) state.selectedSpawnId = state.rows[static_cast<size_t>(row)].spawnId;
        return true;
    }

    return trackingContains(layout, state, mx, my);
}

bool scrollTrackingList(TrackingPanelState& state, const UILayout& layout,
                        int mx, int my, float wheelDelta) {
    if (!state.isOpen || wheelDelta == 0.0f) return false;
    if (!TrackingLayout::listRect(layout).isPointInside(irr::core::vector2di(mx, my))) return false;
    // T313's convention for a top-counted list: up (+) moves towards the top.
    const int lines = static_cast<int>(wheelDelta * 3.0f);
    state.scrollOffset = clampScroll(state, layout, state.scrollOffset - lines);
    return true;
}

}  // namespace Graphics
}  // namespace EQT
