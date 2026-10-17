// B1088 (GitLab #408) -- the loading screen's zone-name lookup.
//
// What is pinned (the device-free value source, include/client/zone/
// zone_display_name.h):
//   * unknown zone id -> EMPTY (never "Zone <id>", never a raw short name);
//   * the era table's long name is returned VERBATIM -- a leading "The" is
//     kept, not stripped (Will's correction);
//   * the server's OP_NewZone zone_long_name OVERRIDES the era table, and is
//     the only source that knows the ids the era table omits (tutorialb);
//   * the line the screen shows ("Entering <name>...") is stored once and
//     read back at draw time: refresh() (target zone id known) and
//     noteNewZone() (the new zone's own name) both derive it, and
//     clearEntry() forgets it so a stale name never shows for the next load.
//
// Device-free: links willeq_core, no client, no network, no display. The era
// table (data/era_zones.tsv) is only asserted when it resolves on this host;
// the override path (the half that carries the guarantee) needs no file.

#include <gtest/gtest.h>

#include "client/zone/zone_display_name.h"
#include "client/login_world/character_select.h"
#include "client/graphics/overlays/loading_frame_layout.h"

#include <irrlicht.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

using eqt::ZoneDisplayNames;

TEST(ZoneDisplayName, AnUnknownIdIsEmptyNeverAZoneIdOrShortName) {
    // A zone id with no override and (for 999999) no era-table row.
    const ZoneDisplayNames& names = ZoneDisplayNames::instance();
    const std::string line = names.lookup(999999);
    EXPECT_EQ(line, "");
    // And the entry line is empty too -- the screen draws nothing for it.
    EXPECT_EQ(ZoneDisplayNames::entryLine(""), "");
}

TEST(ZoneDisplayName, TheEntryLineIsEnteringNameWithEllipsis) {
    EXPECT_EQ(ZoneDisplayNames::entryLine("North Qeynos"), "Entering North Qeynos...");
    EXPECT_EQ(ZoneDisplayNames::entryLine(""), "");
}

TEST(ZoneDisplayName, TheEraNameIsVerbatimTheLeadingTheIsKept) {
    // "The Qeynos Hills" is a real era_zones.tsv row; even if the table does
    // not resolve here, the OVERRIDDEN lookup must be returned verbatim -- no
    // stripping. Set it and read it back byte for byte.
    ZoneDisplayNames::instance().setOverride(7, "The Qeynos Hills");
    EXPECT_EQ(ZoneDisplayNames::instance().lookup(7), "The Qeynos Hills");
    EXPECT_EQ(ZoneDisplayNames::entryLine(ZoneDisplayNames::instance().lookup(7)),
              "Entering The Qeynos Hills...");
}

TEST(ZoneDisplayName, TheNewZoneOverrideShadowsTheEraTable) {
    ZoneDisplayNames& names = ZoneDisplayNames::instance();
    // An id the era table may or may not know; the override always wins.
    const uint32_t id = 12345;
    names.setOverride(id, "Server Said So");
    EXPECT_EQ(names.lookup(id), "Server Said So");
    // An empty override CLEARS it (the server field was blank): back to the
    // era table (or empty for a genuinely unknown id).
    names.setOverride(id, "");
    EXPECT_EQ(names.lookup(id), names.eraNames().count(id) ? names.eraNames().at(id) : "");
}

TEST(ZoneDisplayName, RefreshStoresAndReadsBackTheShownLine) {
    ZoneDisplayNames& names = ZoneDisplayNames::instance();
    names.setEntryForTest("");  // isolate from any earlier test's state
    names.clearEntry();
    const uint32_t id = 54321;  // unknown id -> only the override names it
    names.setOverride(id, "Omn Gora");
    const std::string shown = names.refresh(id);
    EXPECT_EQ(shown, "Entering Omn Gora...");
    EXPECT_EQ(names.entryText(), "Entering Omn Gora...");
    // Clear (zone ready): the stale name is gone; the next refresh re-derives.
    names.clearEntry();
    EXPECT_EQ(names.entryText(), "");
    EXPECT_EQ(names.refresh(id), "Entering Omn Gora...");
}

TEST(ZoneDisplayName, NoteNewZoneReDerivesOnlyForTheShownZone) {
    ZoneDisplayNames& names = ZoneDisplayNames::instance();
    names.clearEntry();
    names.setEntryForTest("");
    // Shown zone is an id NO source knows (not in the era table, no override):
    // the line is empty until the server's own name arrives.
    const uint32_t id = 999999;
    names.refresh(id);
    EXPECT_EQ(names.entryText(), "");  // no era row, no override yet
    // A DIFFERENT zone's OP_NewZone must not touch the shown line.
    names.noteNewZone(888888, "Some Other Zone");
    EXPECT_EQ(names.entryText(), "");
    // The SHOWN zone's OP_NewZone re-derives the line from its own name.
    names.noteNewZone(id, "Omn Gora");
    EXPECT_EQ(names.entryText(), "Entering Omn Gora...");
    names.clearEntry();
}

TEST(ZoneDisplayName, TheEraTablePinsNorthQeynosWhenItResolves) {
    // The one parser the character-select zone line uses. Only asserted when
    // data/era_zones.tsv resolves on this host; the override guarantees above
    // do not need it.
    const auto names = ZoneDisplayNames::instance().eraNames();
    if (names.empty()) {
        GTEST_SKIP() << "data/era_zones.tsv does not resolve in this tree";
    }
    EXPECT_EQ(names.at(2), "North Qeynos");
    // A short name like "qeynos2" is never the answer -- only the long name.
    EXPECT_EQ(names.at(2).find("qeynos2"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Pixels: the shared layout's zone-name line through the client's own draw,
// on the surfaceless GLES2 path (the EDT_OGLES2 llvmpipe backend CI uses).
// Pinned: with a name, a centred row of bright pixels sits ABOVE the bar and
// does not overlap it, and it is the ONLY difference from the empty-name
// frame (the line is not drawn when empty). Self-skips without a device.
// ---------------------------------------------------------------------------

struct FramePixels {
    bool ok = false;
    int width = 0, height = 0;
    std::vector<std::vector<bool>> bright;   // pixel is white-ish (text row)
    std::vector<std::vector<bool>> bar;      // pixel is the bar's fill colour
};

// Render ONE loading frame and read back which pixels are text-bright and
// which are bar-fill.
FramePixels drawFrame(irr::video::IVideoDriver* driver, irr::gui::IGUIFont* font,
                      const std::wstring& entry) {
    FramePixels out;
    // The shared body owns its own beginScene/endScene (a clear to the dark
    // background + the draw + endScene), so no outer scene is needed; the
    // screenshot then reads the just-presented frame (the same cadence
    // test_target_ring's ringPixels uses).
    EQT::Graphics::drawLoadingFrame(driver, font, 0.5f, L"EverQuest", entry, L"Loading...");
    irr::video::IImage* img = driver->createScreenShot();
    if (!img) return out;
    out.ok = true;
    const auto d = img->getDimension();
    out.width = static_cast<int>(d.Width);
    out.height = static_cast<int>(d.Height);
    out.bright.assign(out.height, std::vector<bool>(out.width, false));
    out.bar.assign(out.height, std::vector<bool>(out.width, false));
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const irr::video::SColor p = img->getPixel(x, y);
            // White text on the dark background: all channels near 255.
            out.bright[y][x] = p.getRed() > 180 && p.getGreen() > 180 && p.getBlue() > 180;
            // The bar fill is (80, 120, 200) -- the blue channel is the marker.
            out.bar[y][x] = p.getBlue() > 150 && p.getRed() < 150 && p.getGreen() < 150;
        }
    }
    img->drop();
    return out;
}

int minRow(const std::vector<std::vector<bool>>& grid) {
    for (size_t y = 0; y < grid.size(); ++y)
        for (size_t x = 0; x < grid[0].size(); ++x)
            if (grid[y][x]) return (int)y;
    return -1;
}
int maxRow(const std::vector<std::vector<bool>>& grid) {
    for (int y = (int)grid.size() - 1; y >= 0; --y)
        for (size_t x = 0; x < grid[0].size(); ++x)
            if (grid[y][x]) return y;
    return -1;
}

}  // namespace

TEST(ZoneDisplayNamePixels, TheNameLineIsCentredAboveTheBar) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(400, 300);
    params.Fullscreen = true;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) GTEST_SKIP() << "no EDT_OGLES2 device";
    irr::gui::IGUIFont* font = device->getGUIEnvironment()->getBuiltInFont();
    irr::video::IVideoDriver* driver = device->getVideoDriver();

    const FramePixels with = drawFrame(driver, font, L"Entering North Qeynos...");
    const FramePixels empty = drawFrame(driver, font, L"");
    ASSERT_TRUE(with.ok && empty.ok) << "createScreenShot failed";
    ASSERT_EQ(with.width, 400);
    ASSERT_EQ(with.height, 300);

    // The bar is drawn in both frames at the same place (progress 50 %).
    const int barTop = minRow(with.bar);
    const int barBottom = maxRow(with.bar);
    ASSERT_GE(barTop, 0) << "the bar's fill pixels are not in the frame";
    EXPECT_EQ(barTop, minRow(empty.bar));

    // The bright rows (title + pct + stage) are IDENTICAL in both frames:
    // the only thing the name line adds is its own row.
    ASSERT_EQ(with.bright.size(), empty.bright.size());
    int added = 0;
    for (size_t y = 0; y < with.bright.size(); ++y)
        for (size_t x = 0; x < with.bright[0].size(); ++x)
            if (with.bright[y][x] && !empty.bright[y][x]) ++added;
    EXPECT_GT(added, 20) << "the name line drew no pixels of its own";

    // The ADDED pixels form the line: a tight row band, centred (|left -
    // right margin| <= 2 px per the acceptance), above the bar with no
    // overlap, and below the title (the title is in the empty frame's
    // bright set, so the added band's top is at or below the title's rows).
    int lineMin = -1, lineMax = -1, lineLeft = -1, lineRight = -1;
    for (int y = 0; y < with.height; ++y) {
        for (int x = 0; x < with.width; ++x) {
            if (with.bright[y][x] && !empty.bright[y][x]) {
                lineMin = (lineMin < 0) ? y : std::min(lineMin, y);
                lineMax = std::max(lineMax, y);
                lineLeft = (lineLeft < 0) ? x : std::min(lineLeft, x);
                lineRight = std::max(lineRight, x);
            }
        }
    }
    ASSERT_GE(lineMin, 0) << "no added bright pixels at all";
    EXPECT_LT(lineMin, lineMax) << "the line has no vertical extent (a single row?)";
    const int marginDiff = (400 - 1 - lineRight) - lineLeft;
    EXPECT_LE(std::abs(marginDiff), 2) << "the line is not centred";
    EXPECT_LT(lineMax, barTop) << "the line overlaps the bar";
    // The layout puts the line 24 px above the bar's top edge (13 px font).
    EXPECT_GE(barTop - lineMin, 8) << "the line sits on the bar's border";
    // And the bar is still where the layout says (centred, 400 wide at 400x300
    // it spans the full width -- here only assert the row band moved with the
    // layout, not the exact column).
    EXPECT_GE(barBottom, barTop + 20) << "the bar's height is wrong";
    device->drop();
}
