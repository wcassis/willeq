// B895 (GitLab #211) -- the Tracking Window and the tracking direction lines.
//
// WHAT WAS MISSING. `/useskill tracking` sent OP_Track and the reply became
// "name - N units" chat lines. There was no Tracking Window, no
// /trackfilter, /tracksort, /trackplayers or /stoptracking, and the client
// never said where the tracked target was -- which on Titanium is the
// CLIENT's job: eqemu runs DoTracking() only for SoD and later.
//
// What this pins, all offline (no server, no device):
//   1. the layout, number for number against EQUI_TrackingWnd.xml;
//   2. the direction rule across the whole compass, against DoTracking's
//      thresholds, in client coordinates, and the exact eqstr lines;
//   3. the list rules (filters, players, the five sorts) and their negatives;
//   4. the whole route: OP_Track reply -> TrackingListChanged event -> panel
//      rows -> a click on a row and on Track -> TrackingIntent -> "You begin
//      tracking" -> a direction line on the tick -> the lost-target line;
//   5. the slash surface's official confirmation and FORMAT lines.
//
// THE ORACLE LEDGER. review/UIGAP-ORACLE/211/tracking.tsv measured the
// official Titanium client; the cases that pin one of its rows say "ledger
// row N". Row 1 (OP_Track + OP_TrackUnknown) is pinned in
// TrackingRouteB895.TheAbilitySendsTrackAndTrackUnknown, rows 3/4/5/6/7/8
// below; row 9 (players combo, r* sorts, Cancel) was not captured, and the
// cases for those are fable's assumptions, said so where they stand.

#include <gtest/gtest.h>

#include "eqstr_test_db.h"
#include "zone_stream_test_utils.h"

#include "client/con_tier.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/tracking_layout.h"
#include "client/graphics/ui/tracking_panel.h"
#include "client/player/player_component.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_manager.h"
#include "client/tracking/tracking_controller.h"
#include "client/tracking/tracking_state.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <vector>

using namespace zonetest;
namespace TL = EQT::Graphics::TrackingLayout;
using EQT::Graphics::TrackingPanelState;
using EQT::Graphics::UILayout;
using eqt::tracking::TrackDirection;
using eqt::tracking::TrackEntry;
using eqt::tracking::TrackFilter;
using eqt::tracking::TrackPlayers;
using eqt::tracking::TrackSort;
using eqt::tracking::TrackingSettings;

namespace {

const EQT::StringDatabase& strings() { return eqt_test::ShippedStrings(); }

UILayout Layout(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

// A rect relative to the client area's origin, for comparing against the XML.
struct Rel { int x0, y0, x1, y1; };
Rel RelTo(const UILayout& l, const irr::core::rect<irr::s32>& r) {
    const auto c = TL::clientRect(l);
    return {r.UpperLeftCorner.X - c.UpperLeftCorner.X, r.UpperLeftCorner.Y - c.UpperLeftCorner.Y,
            r.LowerRightCorner.X - c.UpperLeftCorner.X, r.LowerRightCorner.Y - c.UpperLeftCorner.Y};
}
void ExpectRel(const Rel& a, int x0, int y0, int x1, int y1, const char* what) {
    EXPECT_EQ(a.x0, x0) << what;
    EXPECT_EQ(a.y0, y0) << what;
    EXPECT_EQ(a.x1, x1) << what;
    EXPECT_EQ(a.y1, y1) << what;
}

}  // namespace

// ===================================================================
// 1. Layout
// ===================================================================

TEST(TrackingLayoutB895, WindowIsTheOraclesSizeCentred) {
    const auto l = Layout(640, 480);
    const auto w = TL::windowRect(l);
    EXPECT_EQ(w.getWidth(), 150);   // <CX>150</CX>
    EXPECT_EQ(w.getHeight(), 460);  // <CY>460</CY>
    EXPECT_EQ(w.UpperLeftCorner.X, (640 - 150) / 2);
    EXPECT_EQ(w.UpperLeftCorner.Y, (480 - 460) / 2);
    EXPECT_EQ(TL::clientRect(l).getHeight(), 460 - TL::TITLE_H);
}

// Every anchor, resolved in the 150 x 442 client area.
TEST(TrackingLayoutB895, AnchorsResolveExactlyAsTheXmlSays) {
    const auto l = Layout(640, 480);
    const int ch = 460 - TL::TITLE_H;  // 442
    ExpectRel(RelTo(l, TL::filtersLabelRect(l)), 3, 0, 147, 20, "TRW_FiltersLabel");
    ExpectRel(RelTo(l, TL::listRect(l)), 0, 36, 150, ch - 141, "TRW_TrackingList");
    ExpectRel(RelTo(l, TL::sortLabelRect(l)), 3, ch - 136, 147, ch - 122, "TRW_TrackSortLabel");
    ExpectRel(RelTo(l, TL::sortComboRect(l)), 3, ch - 120, 147, ch - 96, "TRW_TrackSortCombobox");
    ExpectRel(RelTo(l, TL::playersLabelRect(l)), 3, ch - 94, 147, ch - 80, "TRW_TrackPlayersLabel");
    ExpectRel(RelTo(l, TL::playersComboRect(l)), 3, ch - 78, 147, ch - 54, "TRW_TrackPlayersCombobox");
    ExpectRel(RelTo(l, TL::trackButtonRect(l)), 3, ch - 45, 147, ch - 25, "TRW_TrackButton");
    ExpectRel(RelTo(l, TL::doneButtonRect(l)), 3, ch - 22, 147, ch - 2, "TRW_DoneButton");
    const int xs[6] = {3, 25, 47, 69, 91, 113};
    for (int i = 0; i < 6; ++i) {
        ExpectRel(RelTo(l, TL::filterButtonRect(l, i)), xs[i], 15, xs[i] + 20, 35, "TRW_Filter*Button");
    }
    EXPECT_EQ(TL::filterButtonRect(l, 6).getArea(), 0) << "out of range is degenerate";
    EXPECT_STREQ(TL::FILTER_TEXT[3], "B");
    EXPECT_EQ(TL::FILTER_TEXT_COLOR[3].b, 240);
    EXPECT_EQ(TL::FILTER_TEXT_COLOR[3].r, 0);
}

// Style_Sizable: on a screen shorter than the window, the bottom-anchored
// controls ride up and nothing overlaps or leaves the screen.
TEST(TrackingLayoutB895, ASmallScreenClampsAndKeepsTheAnchors) {
    const auto l = Layout(640, 400);
    const auto w = TL::windowRect(l);
    EXPECT_EQ(w.getHeight(), 400);
    EXPECT_EQ(w.UpperLeftCorner.Y, 0);
    EXPECT_EQ(TL::doneButtonRect(l).LowerRightCorner.Y, w.LowerRightCorner.Y - 2);
    EXPECT_LT(TL::listRect(l).LowerRightCorner.Y, TL::sortLabelRect(l).UpperLeftCorner.Y);
    EXPECT_GT(TL::visibleRows(l), 0);
}

// ===================================================================
// 2. Direction across the compass
// ===================================================================

// DoTracking's own thresholds, used as the reference the sweep is held to.
TrackDirection Reference(float rel) {
    if (rel > 480) return TrackDirection::StraightAhead;
    if (rel > 416) return TrackDirection::AheadLeft;
    if (rel > 352) return TrackDirection::Left;
    if (rel > 288) return TrackDirection::BehindLeft;
    if (rel > 224) return TrackDirection::Behind;
    if (rel > 160) return TrackDirection::BehindRight;
    if (rel > 96) return TrackDirection::Right;
    if (rel > 32) return TrackDirection::AheadRight;
    return TrackDirection::StraightAhead;
}

TEST(TrackingDirectionB895, TheEightCardinalAndOrdinalPointsFacingNorth) {
    // Client coords: heading 0 faces +Y and +X is to the player's right
    // (Irrlicht maps client x->X, y->Z: +X is right of +Z).
    struct Case { float tx, ty; TrackDirection want; };
    const Case cases[] = {
        {0, 100, TrackDirection::StraightAhead},
        {100, 100, TrackDirection::AheadRight},
        {100, 0, TrackDirection::Right},
        {100, -100, TrackDirection::BehindRight},
        {0, -100, TrackDirection::Behind},
        {-100, -100, TrackDirection::BehindLeft},
        {-100, 0, TrackDirection::Left},
        {-100, 100, TrackDirection::AheadLeft},
    };
    for (const auto& c : cases) {
        EXPECT_EQ(eqt::tracking::DirectionTo(0.0f, 0, 0, c.tx, c.ty), c.want)
            << "target (" << c.tx << "," << c.ty << ")";
    }
}

TEST(TrackingDirectionB895, TheAnswerIsRelativeToThePlayersHeading) {
    // Facing +X (client 90 degrees): a target at +X is ahead, +Y is to the left.
    EXPECT_EQ(eqt::tracking::DirectionTo(90.0f, 0, 0, 100, 0), TrackDirection::StraightAhead);
    EXPECT_EQ(eqt::tracking::DirectionTo(90.0f, 0, 0, 0, 100), TrackDirection::Left);
    EXPECT_EQ(eqt::tracking::DirectionTo(90.0f, 0, 0, 0, -100), TrackDirection::Right);
    EXPECT_EQ(eqt::tracking::DirectionTo(90.0f, 0, 0, -100, 0), TrackDirection::Behind);
    // Translation does not matter, only the offset.
    EXPECT_EQ(eqt::tracking::DirectionTo(180.0f, 500, -300, 500, -400), TrackDirection::StraightAhead);
}

TEST(TrackingDirectionB895, AFullSweepMatchesDoTrackingAtEveryDegree) {
    for (int h = 0; h < 360; h += 15) {
        for (int b = 0; b < 360; ++b) {
            const float rad = b * 3.14159265f / 180.0f;
            const float tx = 200.0f * std::sin(rad);
            const float ty = 200.0f * std::cos(rad);
            const float rel = eqt::tracking::RelativeHeading512(float(h), 0, 0, tx, ty);
            ASSERT_GE(rel, 0.0f);
            ASSERT_LT(rel, 512.0f);
            float want = (b - h) * 512.0f / 360.0f;
            while (want < 0) want += 512.0f;
            while (want >= 512.0f) want -= 512.0f;
            EXPECT_NEAR(rel, want, 0.05f) << "heading " << h << " bearing " << b;
            EXPECT_EQ(eqt::tracking::DirectionForRelative(rel), Reference(rel));
        }
    }
}

TEST(TrackingDirectionB895, TheBoundariesAreDoTrackingsStrictInequalities) {
    using eqt::tracking::DirectionForRelative;
    EXPECT_EQ(DirectionForRelative(32.0f), TrackDirection::StraightAhead);
    EXPECT_EQ(DirectionForRelative(32.5f), TrackDirection::AheadRight);
    EXPECT_EQ(DirectionForRelative(96.0f), TrackDirection::AheadRight);
    EXPECT_EQ(DirectionForRelative(256.0f), TrackDirection::Behind);
    EXPECT_EQ(DirectionForRelative(480.0f), TrackDirection::AheadLeft);
    EXPECT_EQ(DirectionForRelative(480.5f), TrackDirection::StraightAhead);
}

TEST(TrackingDirectionB895, EveryLineIsTheOraclesOwnString) {
    ASSERT_TRUE(strings().isLoaded()) << WILLEQ_DB_REAL_PATH;
    using eqt::tracking::DirectionLine;
    const std::string n = "a gnoll";
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::StraightAhead), "a gnoll is straight ahead.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::AheadRight), "a gnoll is ahead and to the right.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::Right), "a gnoll is to the right.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::BehindRight), "a gnoll is behind and to the right.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::Behind), "a gnoll is behind you.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::BehindLeft), "a gnoll is behind and to the left.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::Left), "a gnoll is to the left.");
    EXPECT_EQ(DirectionLine(strings(), n, TrackDirection::AheadLeft), "a gnoll is ahead and to the left.");
}

TEST(TrackingDirectionB895, AMissingTableDrawsNothingRatherThanAnInventedLine) {
    EQT::StringDatabase empty;
    EXPECT_TRUE(eqt::tracking::DirectionLine(empty, "x", TrackDirection::Left).empty());
}

TEST(TrackingDirectionB895, EveryTrackingIdExistsInTheShippedDb) {
    ASSERT_TRUE(strings().isLoaded());
    for (uint32_t id : eqt::tracking::TrackingEqStrIds()) {
        EXPECT_FALSE(strings().getString(id).empty()) << "eqstr " << id;
    }
    EXPECT_EQ(strings().getString(eqt::tracking::SortOptionStringId(TrackSort::RConsider)), "rconsider");
    EXPECT_EQ(strings().getString(eqt::tracking::PlayersOptionStringId(TrackPlayers::Group)), "Group");
}

// ===================================================================
// 3. The list rules
// ===================================================================

namespace {

TrackEntry E(uint16_t id, const char* name, eqt::ConTier con, float dist, uint32_t order,
             bool player = false, bool group = false) {
    TrackEntry e;
    e.spawnId = id; e.name = name; e.con = con; e.distance = dist; e.serverOrder = order;
    e.isPlayer = player; e.isGroupMember = group;
    return e;
}

std::vector<uint16_t> Ids(const std::vector<TrackEntry>& rows) {
    std::vector<uint16_t> v;
    for (const auto& r : rows) v.push_back(r.spawnId);
    return v;
}

std::vector<TrackEntry> Sample() {
    using eqt::ConTier;
    return {
        E(1, "red", ConTier::Red, 30, 0),
        E(2, "gray", ConTier::Gray, 100, 1),
        E(3, "white", ConTier::White, 50, 2),
        E(4, "PlayerA", ConTier::White, 20, 3, /*player=*/true, /*group=*/false),
        E(5, "PlayerB", ConTier::Yellow, 70, 4, /*player=*/true, /*group=*/true),
        E(6, "lblue", ConTier::LightBlue, 10, 5),
    };
}

}  // namespace

TEST(TrackingListB895, NormalIsTheServersOrder) {
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), TrackingSettings{})),
              (std::vector<uint16_t>{1, 2, 3, 4, 5, 6}));
}

TEST(TrackingListB895, TheFiveSortsAreStableOverTheServerOrder) {
    TrackingSettings s;
    s.sort = TrackSort::Distance;
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{6, 4, 1, 3, 5, 2}));
    s.sort = TrackSort::RDistance;
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{2, 5, 3, 1, 4, 6}));
    // Ledger row 3: "consider = hardest (red) first".
    s.sort = TrackSort::Consider;   // red, yellow(PC), white, white(PC), lblue, gray
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{1, 5, 3, 4, 6, 2}));
    s.sort = TrackSort::RConsider;  // the reverse (row 9: not captured -- assumption)
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{2, 6, 3, 4, 5, 1}));
}

TEST(TrackingListB895, FiltersHideNpcsOfThatColourAndNeverAPlayer) {
    TrackingSettings s;
    s.filters[static_cast<size_t>(TrackFilter::White)] = false;
    s.filters[static_cast<size_t>(TrackFilter::Yellow)] = false;
    // NPC 3 (white) goes; PlayerA (white) and PlayerB (yellow) stay -- the
    // filters are NPC filters ("You will see %1 NPCs when tracking").
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{1, 2, 4, 5, 6}));
}

TEST(TrackingListB895, GrayIsGovernedByTheGreenButton) {
    TrackingSettings s;
    s.filters[static_cast<size_t>(TrackFilter::Green)] = false;
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{1, 3, 4, 5, 6}));
}

TEST(TrackingListB895, TheBlueButtonHidesNothingTheSharedConTableProduces) {
    // Negative: the shared con table has no dark-blue tier (recorded as an
    // assumption awaiting oracle), so B must not hide light blue.
    TrackingSettings s;
    s.filters[static_cast<size_t>(TrackFilter::Blue)] = false;
    EXPECT_EQ(eqt::tracking::VisibleRows(Sample(), s).size(), Sample().size());
}

TEST(TrackingListB895, TrackPlayersOffAndGroup) {
    TrackingSettings s;
    s.players = TrackPlayers::Off;
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{1, 2, 3, 6}));
    s.players = TrackPlayers::Group;
    EXPECT_EQ(Ids(eqt::tracking::VisibleRows(Sample(), s)), (std::vector<uint16_t>{1, 2, 3, 5, 6}));
}

TEST(TrackingListB895, ParsersTakeTheFormatWordsAndRejectEverythingElse) {
    using namespace eqt::tracking;
    EXPECT_EQ(ParseTrackSort("RDistance"), TrackSort::RDistance);
    EXPECT_EQ(ParseTrackSort(" consider "), TrackSort::Consider);
    EXPECT_FALSE(ParseTrackSort("").has_value());
    EXPECT_FALSE(ParseTrackSort("reverse").has_value());
    EXPECT_EQ(ParseTrackPlayers("GROUP"), TrackPlayers::Group);
    EXPECT_FALSE(ParseTrackPlayers("1").has_value());
    EXPECT_EQ(ParseTrackFilter("lblue"), TrackFilter::LightBlue);
    EXPECT_FALSE(ParseTrackFilter("lightblue").has_value()) << "13162 spells it lblue";
    EXPECT_FALSE(ParseTrackFilter("gray").has_value()) << "there is no gray button";
}

TEST(TrackingListB895, ConTiersComeFromTheSharedTable) {
    // The renderer's placeholder colours, unchanged by the move.
    EXPECT_EQ(eqt::ConTierForLevels(10, 10), eqt::ConTier::White);
    EXPECT_EQ(eqt::ConTierForLevels(10, 1), eqt::ConTier::Gray);
    EXPECT_EQ(eqt::ConTierForLevels(10, 30), eqt::ConTier::Red);
    EXPECT_EQ(eqt::ConTierForLevels(0, 30), eqt::ConTier::White);
    const auto lb = eqt::ConTierRgb(eqt::ConTier::LightBlue);
    EXPECT_EQ(lb.r, 0); EXPECT_EQ(lb.g, 160); EXPECT_EQ(lb.b, 255);
}

// ===================================================================
// 4. The whole route
// ===================================================================

namespace {

std::vector<std::string> SystemLines(std::vector<eqt::state::GameEvent>& evs) {
    std::vector<std::string> out;
    for (auto& ev : evs) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

const eqt::state::TrackingListChangedData* LastList(std::vector<eqt::state::GameEvent>& evs) {
    const eqt::state::TrackingListChangedData* last = nullptr;
    for (auto& ev : evs) {
        if (ev.type == eqt::state::GameEventType::TrackingListChanged) {
            last = &std::get<eqt::state::TrackingListChangedData>(ev.data);
        }
    }
    return last;
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

class TrackingRouteB895 : public ::testing::Test {
protected:
    TrackingRouteB895() : eq(MakeEq()), handler(eq) { eq.setBridge(&bridge); }
    ~TrackingRouteB895() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(strings().isLoaded());
        ASSERT_TRUE(eq.Strings().LoadStringFiles(""));
        auto& p = eq.GetGameState().player();
        p.setLevel(10);
        p.setPositionAndHeading(0, 0, 0, 0);  // client (0,0), facing +Y
        // Client (x, y) = (server_y, server_x); the wire carries 1/8-unit
        // fixed point, hence the * 8.
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(301, "a_gnoll001", 50 * 8, 0, 0, 10));   // ahead, white
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(302, "a_bat002", 0, 100 * 8, 0, 1));     // right, gray
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(303, "an_orc_pawn", -30 * 8, 0, 0, 30)); // behind, red
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(304, "Ranger", 0, -20 * 8, 0, 10, 0));   // a PC
        bridge.drainEvents();
    }

    void ReplyTrack(const std::vector<std::pair<uint32_t, float>>& rows) {
        ReplayZone(eq, HC_OP_Track, BuildPayload(2 + 8 * rows.size(), [&](EQ::Net::DynamicPacket& p) {
            size_t off = 2;
            for (const auto& [id, d] : rows) { p.PutUInt32(off, id); p.PutFloat(off + 4, d); off += 8; }
        }));
    }

    EverQuest eq;
    TestBridge bridge;
    eqt::EqActionHandler handler;
};

}  // namespace

TEST_F(TrackingRouteB895, AReplyOpensTheWindowWithResolvedConColouredRows) {
    ReplyTrack({{303, 30.f}, {302, 100.f}, {301, 50.f}, {304, 20.f}, {999, 5.f}});
    auto evs = bridge.drainEvents();
    EXPECT_TRUE(SystemLines(evs).empty()) << "the reply is a window now, not chat lines";
    const auto* d = LastList(evs);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->openWindow);
    ASSERT_EQ(d->rows.size(), 4u) << "id 999 is not in the spawn list: no name, no row";
    EXPECT_EQ(d->rows[0].name, "an orc pawn");
    EXPECT_EQ(d->rows[1].name, "a bat");
    EXPECT_EQ(d->rows[2].name, "a gnoll");
    EXPECT_EQ(d->rows[3].name, "Ranger");
    EXPECT_TRUE(d->rows[3].isPlayer);
    EXPECT_EQ(d->rows[0].r, 255); EXPECT_EQ(d->rows[0].g, 0);    // red
    EXPECT_EQ(d->rows[1].r, 128); EXPECT_EQ(d->rows[1].g, 128);  // gray
    ASSERT_EQ(d->sortOptions.size(), 5u);
    EXPECT_EQ(d->sortOptions[0], "normal");
    EXPECT_EQ(d->sortOptions[3], "rdistance");
    ASSERT_EQ(d->playersOptions.size(), 3u);
    EXPECT_EQ(d->playersOptions[0], "on");

    // ... and the panel shows exactly that.
    TrackingPanelState panel;
    const auto l = Layout(640, 480);
    EQT::Graphics::applyTrackingListChanged(panel, *d, l);
    EXPECT_TRUE(panel.isOpen);
    ASSERT_EQ(panel.rows.size(), 4u);
    EXPECT_EQ(panel.rows[2].spawnId, 301);
}

TEST_F(TrackingRouteB895, ClickARowThenTrackStartsTrackingAndTheTickSpeaks) {
    ReplyTrack({{303, 30.f}, {302, 100.f}, {301, 50.f}});
    auto evs = bridge.drainEvents();
    TrackingPanelState panel;
    const auto l = Layout(640, 480);
    EQT::Graphics::applyTrackingListChanged(panel, *LastList(evs), l);

    // Select row 2 (a gnoll), then press Track.
    const auto row = TL::rowRect(l, 2);
    EQT::Graphics::processTrackingInput(&bridge, row.UpperLeftCorner.X + 5, row.UpperLeftCorner.Y + 5, true, l, panel);
    EXPECT_EQ(panel.selectedSpawnId, 301);
    const auto tb = TL::trackButtonRect(l);
    EXPECT_TRUE(EQT::Graphics::processTrackingInput(&bridge, tb.UpperLeftCorner.X + 5, tb.UpperLeftCorner.Y + 5, true, l, panel));
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* ti = std::get_if<eqt::events::TrackingIntent>(&intents[0]);
    ASSERT_NE(ti, nullptr);
    EXPECT_EQ(ti->op, eqt::events::TrackingIntentOp::Track);
    EXPECT_EQ(ti->spawnId, 301);

    // The game side of that intent. Ledger row 6: "You begin tracking %1."
    // then an IMMEDIATE direction line.
    eq.Player().Tracking().ApplyIntent(*ti);
    evs = bridge.drainEvents();
    {
        const auto lines = SystemLines(evs);
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[0], "You begin tracking a gnoll.");
        EXPECT_EQ(lines[1], "a gnoll is straight ahead.");
    }
    EXPECT_EQ(eq.Player().Tracking().TrackedSpawnId(), 301);
    ASSERT_NE(LastList(evs), nullptr);
    EXPECT_EQ(LastList(evs)->trackedSpawnId, 301);

    // Ledger row 7 + cadence.txt (~2 s): nothing before the interval, a line
    // after it.
    auto now = eqt::tracking::TrackingController::Clock::now();
    eq.Player().Tracking().Tick(now);
    EXPECT_TRUE(SystemLines(evs = bridge.drainEvents()).empty());
    now += std::chrono::milliseconds(2100);
    eq.Player().Tracking().Tick(now);
    evs = bridge.drainEvents();
    EXPECT_TRUE(Has(SystemLines(evs), "a gnoll is straight ahead.")) ;

    // Turn to face +X (client 90): the gnoll at +Y is now to the left.
    eq.GetGameState().player().setHeading(90.0f);
    now += std::chrono::milliseconds(2100);
    eq.Player().Tracking().Tick(now);
    evs = bridge.drainEvents();
    EXPECT_TRUE(Has(SystemLines(evs), "a gnoll is to the left."));

    // The target despawns: lost, once, and tracking stops. Ledger row 8: the
    // official client printed eqstr 12102, NOT 12681.
    ReplayZone(eq, HC_OP_DeleteSpawn, BuildPayload(6, [](EQ::Net::DynamicPacket& p) { p.PutUInt32(2, 301); }));
    bridge.drainEvents();
    now += std::chrono::milliseconds(2100);
    eq.Player().Tracking().Tick(now);
    evs = bridge.drainEvents();
    EXPECT_TRUE(Has(SystemLines(evs), "You have lost your target."));
    EXPECT_FALSE(Has(SystemLines(evs), "You have lost your tracking target."));
    EXPECT_EQ(eq.Player().Tracking().TrackedSpawnId(), 0);
    now += std::chrono::milliseconds(2100);
    eq.Player().Tracking().Tick(now);
    EXPECT_TRUE(SystemLines(evs = bridge.drainEvents()).empty()) << "lost is said once";
}

TEST_F(TrackingRouteB895, TrackWithNothingSelectedPushesNothing) {
    ReplyTrack({{301, 50.f}});
    auto evs = bridge.drainEvents();
    TrackingPanelState panel;
    const auto l = Layout(640, 480);
    EQT::Graphics::applyTrackingListChanged(panel, *LastList(evs), l);
    const auto tb = TL::trackButtonRect(l);
    EQT::Graphics::processTrackingInput(&bridge, tb.UpperLeftCorner.X + 5, tb.UpperLeftCorner.Y + 5, true, l, panel);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(TrackingRouteB895, WindowControlsPushTheSettingIntentsAndCancelOnlyHides) {
    ReplyTrack({{301, 50.f}});
    auto evs = bridge.drainEvents();
    TrackingPanelState panel;
    const auto l = Layout(640, 480);
    EQT::Graphics::applyTrackingListChanged(panel, *LastList(evs), l);

    const auto red = TL::filterButtonRect(l, 0);
    EQT::Graphics::processTrackingInput(&bridge, red.UpperLeftCorner.X + 3, red.UpperLeftCorner.Y + 3, true, l, panel);
    // Drop the sort list down, pick item 1 (distance).
    const auto sc = TL::sortComboRect(l);
    EQT::Graphics::processTrackingInput(&bridge, sc.UpperLeftCorner.X + 3, sc.UpperLeftCorner.Y + 3, true, l, panel);
    EXPECT_EQ(panel.openCombo, EQT::Graphics::TrackingCombo::Sort);
    const auto list = TL::comboListRect(l, sc, 5);
    EQT::Graphics::processTrackingInput(&bridge, list.UpperLeftCorner.X + 3,
                                        list.UpperLeftCorner.Y + TL::ROW_H + 3, true, l, panel);
    EXPECT_EQ(panel.openCombo, EQT::Graphics::TrackingCombo::None);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 2u);
    const auto& f = std::get<eqt::events::TrackingIntent>(intents[0]);
    EXPECT_EQ(f.op, eqt::events::TrackingIntentOp::ToggleFilter);
    EXPECT_EQ(f.value, 0);
    const auto& s = std::get<eqt::events::TrackingIntent>(intents[1]);
    EXPECT_EQ(s.op, eqt::events::TrackingIntentOp::SetSort);
    EXPECT_EQ(s.value, 1);
    EXPECT_EQ(panel.sort, 0) << "not optimistic: the panel waits for the republish";

    eq.Player().Tracking().ApplyIntent(s);
    evs = bridge.drainEvents();
    EXPECT_TRUE(Has(SystemLines(evs), "Track Sort type set to distance."));
    EQT::Graphics::applyTrackingListChanged(panel, *LastList(evs), l);
    EXPECT_EQ(panel.sort, 1);
    EXPECT_TRUE(panel.isOpen) << "a settings publish does not reopen or close";

    const auto done = TL::doneButtonRect(l);
    EQT::Graphics::processTrackingInput(&bridge, done.UpperLeftCorner.X + 3, done.UpperLeftCorner.Y + 3, true, l, panel);
    EXPECT_FALSE(panel.isOpen);
    EXPECT_TRUE(bridge.drainIntents().empty()) << "Cancel is local";

    // A settings change with the window shut does not open it.
    eq.Player().Tracking().SetPlayers(TrackPlayers::Off);
    evs = bridge.drainEvents();
    EQT::Graphics::applyTrackingListChanged(panel, *LastList(evs), l);
    EXPECT_FALSE(panel.isOpen);
}

// ===================================================================
// 5. The slash surface
// ===================================================================

// Ledger rows 3/4/5: the sort and filter lines, verbatim from the official
// client's chat log (13150/13151, 13163/13164). The players and FORMAT lines
// are the eqstr rows themselves (row 9: not captured).
TEST_F(TrackingRouteB895, SlashCommandsSpeakTheOfficialLines) {
    using eqt::tracking::TrackingCommand;
    handler.trackingCommand(TrackingCommand::Sort, "bogus");
    handler.trackingCommand(TrackingCommand::Sort, "rconsider");
    handler.trackingCommand(TrackingCommand::Players, "");
    handler.trackingCommand(TrackingCommand::Players, "group");
    handler.trackingCommand(TrackingCommand::Filter, "purple");
    handler.trackingCommand(TrackingCommand::Filter, "red");
    handler.trackingCommand(TrackingCommand::Filter, "red");
    handler.trackingCommand(TrackingCommand::Stop, "");
    auto evs = bridge.drainEvents();
    const auto lines = SystemLines(evs);
    EXPECT_TRUE(Has(lines, "FORMAT: /tracksort <normal/distance/consider/rdistance/rconsider>."));
    EXPECT_TRUE(Has(lines, "Track Sort type set to reverse consider."));
    EXPECT_TRUE(Has(lines, "FORMAT: /trackplayers <on/off/group>."));
    EXPECT_TRUE(Has(lines, "Track players * GROUP ONLY *"));
    EXPECT_TRUE(Has(lines, "FORMAT: /trackfilter <red/yellow/white/blue/lblue/green>."));
    EXPECT_TRUE(Has(lines, "You will NOT see red NPCs when tracking."));
    EXPECT_TRUE(Has(lines, "You will see red NPCs when tracking."));
    EXPECT_TRUE(Has(lines, "You have lost or do not have a tracking target."));
    const auto& st = eq.Player().Tracking().Settings();
    EXPECT_EQ(st.sort, TrackSort::RConsider);
    EXPECT_EQ(st.players, TrackPlayers::Group);
    EXPECT_TRUE(st.filters[0]) << "red toggled twice is back on";
}

TEST_F(TrackingRouteB895, TrackListPrintsTheVisibleRowsHeadlessly) {
    ReplyTrack({{303, 30.f}, {302, 100.f}, {301, 50.f}});
    bridge.drainEvents();
    handler.trackingCommand(eqt::tracking::TrackingCommand::Sort, "distance");
    handler.trackingCommand(eqt::tracking::TrackingCommand::List, "");
    auto evs = bridge.drainEvents();
    const auto lines = SystemLines(evs);
    ASSERT_GE(lines.size(), 4u);
    // Distances are recomputed locally from the spawn list, not the reply's.
    EXPECT_TRUE(Has(lines, "=== Tracking (3 targets) ==="));
    EXPECT_TRUE(Has(lines, "  an orc pawn - 30 units - red"));
    EXPECT_TRUE(Has(lines, "  a gnoll - 50 units - white"));
    EXPECT_TRUE(Has(lines, "  a bat - 100 units - gray"));
    EXPECT_EQ(lines[lines.size() - 3], "  an orc pawn - 30 units - red");
}

TEST_F(TrackingRouteB895, SlashTrackReachesTheSameBodyAsTheButton) {
    EQ::SkillManager* sm = eq.GetSkillManager();
    ASSERT_NE(sm, nullptr);
    ASSERT_TRUE(EQ::SkillCapTable::instance().load(WILLEQ_REPO_ROOT "/config/skill_caps.json"));
    sm->initialize(4 /*Ranger*/, 1, 10);
    bridge.drainEvents();
    handler.trackTarget(303);
    auto evs = bridge.drainEvents();
    EXPECT_TRUE(Has(SystemLines(evs), "You begin tracking an orc pawn."));
    EXPECT_EQ(eq.Player().Tracking().TrackedSpawnId(), 303);
}

// Ledger row 1: pressing the Tracking ability sends OP_Track AND an empty
// OP_TrackUnknown at the same instant. Asserted at the source, because no
// headless EverQuest has a zone connection to capture the queue from.
TEST(TrackingWireB895, TheAbilitySendsTrackAndTrackUnknown) {
    std::ifstream in(WILLEQ_REPO_ROOT "/src/client/skill/skill_manager.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    const size_t track = src.find("if (opcode == HC_OP_Track)");
    ASSERT_NE(track, std::string::npos) << "the OP_Track send no longer branches to OP_TrackUnknown";
    const size_t unknown = src.find("QueuePacket(HC_OP_TrackUnknown", track);
    ASSERT_NE(unknown, std::string::npos);
    EXPECT_NE(src.find("unknown.Resize(0)", track), std::string::npos) << "OP_TrackUnknown is EMPTY";
}
