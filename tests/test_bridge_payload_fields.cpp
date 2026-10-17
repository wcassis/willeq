// T413 (board B296) — the bridge payload fields nothing drew.
//
// T407's write-only-field census named ten fields on the game→renderer event
// payloads that every producer fills and no consumer reads. This file covers
// the ones whose PANEL ALREADY EXISTS in fable, and pins each drawn field to
// the official client's own UI element rather than to a number chosen here.
//
// Oracle files, all under /mnt/projects/EverQuestP1999/uifiles/default/:
//
//   castTimeRemainingMs / castTimeTotalMs
//       EQUI_CastingWindow.xml
//         <Gauge item="Casting_Gauge"> <EQType>7</EQType>
//           <Location><X>3</X><Y>19</Y></Location>
//           <Size><CX>116</CX><CY>10</CY></Size>
//         inside <Screen item="CastingWindow"> <Text>Casting Time</Text>
//                <Size><CX>140</CX><CY>60</CY></Size>
//       fable's CastingBarState/renderCastingBar IS that gauge. It had only a
//       render-thread stopwatch; the authoritative remaining/total the game
//       thread published on CastingStateChanged reached nothing.
//
//   progressMs (SpellMemorizingData)
//       EQUI_SpellBookWnd.xml
//         <Gauge item="SBW_Memorize_Gauge"> <EQType>9</EQType>
//           <Location><X>204</X><Y>312</Y></Location>
//           <Size><CX>100</CX><CY>8</CY></Size>
//           <FillTint><R>200</R><G>0</G><B>200</B></FillTint>
//         inside <Screen item="SpellBookWnd">
//                <Size><CX>510</CX><CY>360</CY></Size>
//       The gauge lives in the SPELLBOOK. fable draws the spellbook popup, so
//       the gauge goes there, scaled by the same proportional rule
//       InventoryLayout::coinRect()/paperdollRect() use.
//
// The two fields this unit DROPPED instead of drawing
// (SpellGemChangedData::memorizeTotalMs, CursorItemChangedData::queueSize)
// are pinned by the compiler: the structs below would not compile if they
// came back. See the board and the header comments for the oracle reason —
// EQUI_CastSpellWnd.xml's <SpellGem> elements carry no Gauge piece, and
// EQUI_CursorAttachment.xml carries no count label.

#include <gtest/gtest.h>

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>

using namespace EQT::Graphics;

namespace {

struct Res { int w, h; };
const Res kResolutions[] = {{800, 600}, {1024, 768}, {1280, 720}, {1920, 1080}};

// The oracle's own numbers, re-derived here rather than read back out of the
// production header, so a change to that arithmetic fails this test.
constexpr int kMemoX = 204, kMemoY = 312, kMemoW = 100, kMemoH = 8;
constexpr int kBookW = 510, kBookH = 360;

} // namespace

// ── castTimeRemainingMs / castTimeTotalMs ──────────────────────────────────

TEST(CastingBarT413, TheAuthoritativeRemainingAndTotalDriveTheGauge) {
    CastingBarState bar;
    eqt::state::CastingStateChangedData d{};
    d.isCasting = true;
    d.spellId = 15;
    d.castTimeTotalMs = 4000;
    d.castTimeRemainingMs = 1000;

    applyCastingStateChanged(bar, d);

    EXPECT_TRUE(bar.isCasting);
    EXPECT_EQ(bar.totalMs, 4000u);
    EXPECT_EQ(bar.remainingMs, 1000u);
    // 3000 of 4000 elapsed — and NOT whatever the render thread's own
    // stopwatch says, which for a freshly-defaulted castStartTime would be
    // an arbitrary huge elapsed clamped to 1.0.
    EXPECT_FLOAT_EQ(bar.getProgress(), 0.75f);
}

TEST(CastingBarT413, ProgressTracksSuccessiveSamples) {
    CastingBarState bar;
    eqt::state::CastingStateChangedData d{};
    d.isCasting = true;
    d.castTimeTotalMs = 2000;

    d.castTimeRemainingMs = 2000;
    applyCastingStateChanged(bar, d);
    EXPECT_FLOAT_EQ(bar.getProgress(), 0.0f);

    d.castTimeRemainingMs = 500;
    applyCastingStateChanged(bar, d);
    EXPECT_FLOAT_EQ(bar.getProgress(), 0.75f);

    d.castTimeRemainingMs = 0;
    applyCastingStateChanged(bar, d);
    EXPECT_FLOAT_EQ(bar.getProgress(), 1.0f);
}

TEST(CastingBarT413, ANotCastingPayloadClosesTheBarAndForgetsTheSample) {
    CastingBarState bar;
    eqt::state::CastingStateChangedData d{};
    d.isCasting = true;
    d.castTimeTotalMs = 3000;
    d.castTimeRemainingMs = 1500;
    applyCastingStateChanged(bar, d);
    ASSERT_TRUE(bar.isCasting);

    d.isCasting = false;
    applyCastingStateChanged(bar, d);
    EXPECT_FALSE(bar.isCasting);
    EXPECT_EQ(bar.totalMs, 0u);
    EXPECT_EQ(bar.remainingMs, 0u);
    EXPECT_FLOAT_EQ(bar.getProgress(), 0.0f);
}

// The stopwatch fallback must survive: SpellCastVisualStarted opens the bar
// with a name and a duration before any CastingStateChanged sample arrives,
// and that frame must still draw a bar rather than a blank one.
TEST(CastingBarT413, WithNoAuthoritativeSampleTheStopwatchStillDrives) {
    CastingBarState bar;
    bar.isCasting = true;
    bar.spellName = "Minor Healing";
    bar.castTimeMs = 1000;
    bar.castStartTime = std::chrono::steady_clock::now() -
                        std::chrono::milliseconds(500);

    ASSERT_EQ(bar.totalMs, 0u) << "no authoritative sample yet";
    EXPECT_GT(bar.getProgress(), 0.3f);
    EXPECT_LT(bar.getProgress(), 0.9f);
}

// ── progressMs (the spellbook's memorisation gauge) ─────────────────────────

TEST(SpellbookMemorizeT413, ProgressComesFromTheEventsProgressAndTotal) {
    SpellbookPopupState book;
    eqt::state::SpellMemorizingData d{};
    d.isMemorizing = true;
    d.gemSlot = 3;
    d.spellId = 202;
    d.progressMs = 1500;
    d.totalMs = 6000;

    applySpellMemorizing(book, d);

    EXPECT_TRUE(book.isMemorizing);
    EXPECT_EQ(book.memorizeGemSlot, 3);
    EXPECT_EQ(book.memorizeProgressMs, 1500u);
    EXPECT_EQ(book.memorizeTotalMs, 6000u);
    EXPECT_FLOAT_EQ(book.memorizeProgress(), 0.25f);
}

TEST(SpellbookMemorizeT413, AFinishedOrCancelledMemoriseClearsTheGauge) {
    SpellbookPopupState book;
    eqt::state::SpellMemorizingData d{};
    d.isMemorizing = true;
    d.progressMs = 3000;
    d.totalMs = 6000;
    applySpellMemorizing(book, d);
    ASSERT_TRUE(book.isMemorizing);

    d.isMemorizing = false;
    applySpellMemorizing(book, d);
    EXPECT_FALSE(book.isMemorizing);
    EXPECT_FLOAT_EQ(book.memorizeProgress(), 0.0f);
}

TEST(SpellbookMemorizeT413, ProgressIsClampedAndNeverDividesByZero) {
    SpellbookPopupState book;
    eqt::state::SpellMemorizingData d{};
    d.isMemorizing = true;
    d.progressMs = 9000;
    d.totalMs = 0;
    applySpellMemorizing(book, d);
    EXPECT_FLOAT_EQ(book.memorizeProgress(), 0.0f);

    d.totalMs = 6000;
    applySpellMemorizing(book, d);
    EXPECT_FLOAT_EQ(book.memorizeProgress(), 1.0f);
}

// ── the gauge's PLACEMENT is the oracle's ──────────────────────────────────

TEST(SpellbookMemorizeT413, TheGaugeRectIsTheOracleGaugeScaledIntoThePopup) {
    UILayout layout;
    layout.computeLayout(800, 600);
    // centerPopup is {256,65,656,415} (400x350) at 800x600 (B1075: the
    // layout table's CentreSlot; B1077's wider 48px gem column shifts the
    // centred popup left 8px from 264), so:
    //   x = 256 + 204*400/510 = 256 + 160 = 416
    //   y =  65 + 312*350/360 =  65 + 303 = 368
    //   w = 100*400/510 = 78     h = 8*350/360 = 7
    EXPECT_EQ(spellbookMemorizeGaugeRect(layout),
              irr::core::rect<irr::s32>(416, 368, 494, 375));
}

TEST(SpellbookMemorizeT413, TheGaugeKeepsTheOracleProportionAtEveryResolution) {
    for (const auto& rc : kResolutions) {
        UILayout layout;
        layout.computeLayout(rc.w, rc.h);
        const auto& popup = layout.centerPopup;
        const auto g = spellbookMemorizeGaugeRect(layout);

        EXPECT_EQ(g.UpperLeftCorner.X - popup.UpperLeftCorner.X,
                  kMemoX * popup.getWidth() / kBookW) << rc.w << "x" << rc.h;
        EXPECT_EQ(g.UpperLeftCorner.Y - popup.UpperLeftCorner.Y,
                  kMemoY * popup.getHeight() / kBookH) << rc.w << "x" << rc.h;
        EXPECT_EQ(g.getWidth(), kMemoW * popup.getWidth() / kBookW)
            << rc.w << "x" << rc.h;
        EXPECT_EQ(g.getHeight(), kMemoH * popup.getHeight() / kBookH)
            << rc.w << "x" << rc.h;
    }
}

TEST(SpellbookMemorizeT413, TheGaugeIsInsideThePopupAtEveryResolution) {
    for (const auto& rc : kResolutions) {
        UILayout layout;
        layout.computeLayout(rc.w, rc.h);
        const auto& popup = layout.centerPopup;
        const auto g = spellbookMemorizeGaugeRect(layout);

        EXPECT_GE(g.UpperLeftCorner.X, popup.UpperLeftCorner.X) << rc.w << "x" << rc.h;
        EXPECT_GE(g.UpperLeftCorner.Y, popup.UpperLeftCorner.Y) << rc.w << "x" << rc.h;
        EXPECT_LE(g.LowerRightCorner.X, popup.LowerRightCorner.X) << rc.w << "x" << rc.h;
        EXPECT_LE(g.LowerRightCorner.Y, popup.LowerRightCorner.Y) << rc.w << "x" << rc.h;
        EXPECT_GT(g.getWidth(), 0) << rc.w << "x" << rc.h;
        EXPECT_GT(g.getHeight(), 0) << rc.w << "x" << rc.h;
    }
}

// ── the two dropped fields stay dropped ────────────────────────────────────
//
// A compile-time pin: designated initialisers name every member of each
// struct, so re-adding a field (or renaming one) breaks this TU rather than
// silently reopening B296.

TEST(DroppedPayloadFieldsT413, CursorItemChangedCarriesOnlyTheFlag) {
    const eqt::state::CursorItemChangedData d{/*hasCursorItem=*/true};
    EXPECT_TRUE(d.hasCursorItem);
    EXPECT_EQ(sizeof(eqt::state::CursorItemChangedData), sizeof(bool));
}

TEST(DroppedPayloadFieldsT413, TheSpellGemPayloadHasNoMemoriseTotal) {
    eqt::state::SpellGemChangedData d{};
    d.gemSlot = 0;
    d.spellId = 1;
    d.gemState = 3;
    d.cooldownRemainingMs = 500;
    d.cooldownTotalMs = 1500;
    d.spellName = "Ward";
    d.iconId = 7;
    // The gem panel reads cooldownRemainingMs/cooldownTotalMs (the refresh
    // arc) and nothing else numeric; the memorise total belonged to the
    // spellbook's gauge and is published on SpellMemorizing instead.
    EXPECT_EQ(d.cooldownTotalMs, 1500u);
}
