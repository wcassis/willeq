// B899 (GitLab #215 / #151 Leg B) -- the era container window.
//
// What this pins, in order:
//
//   1. GEOMETRY against the oracle file itself: every <Location>/<Size> in
//      /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Container.xml is
//      parsed at test time and compared with ContainerLayout, so a
//      transcription slip is a red test rather than a wrong pixel.
//   2. The APPLIERS, driven with the real publishers' data structs: a
//      carried container's open event produces a window; world and bag cells
//      route, nothing else does.
//   3. The CLICKS on a real GameStateBridge: Combine pushes
//      TradeskillCombineIntent with 1000 for a world container and the bag's
//      own slot for a carried one; Done / the close box / ESC push
//      TradeskillCloseIntent and close nothing locally; a cell click goes
//      through B442's CursorGesture with the cell's wire slot.
//   4. The GAME side through the real command chain (CommandProcessor ->
//      ActionDispatcher -> EqActionHandler -> CommandsComponent): /bag opens
//      and closes, refuses a non-container and a non-general slot, the close
//      intent closes a carried bag without touching the world container, and
//      the bag leaving its slot closes its window.
//   5. The world container no longer opens the recipe window (Will's ruling
//      on #215, 2026-09-23): a server ClickObjectAction open=1 lands in the
//      container window, and the bridge arm no longer feeds TradeskillPanelState.
//
// Behaviour rows cite the official-client ledger they pin:
// /mnt/projects/fable/review/UIGAP-ORACLE/215/combine.tsv and
// /mnt/projects/fable/review/UIGAP-ORACLE/151/gestures.tsv (UIGAP-ORACLE,
// 2026-10-05).
//
// No Irrlicht device, no server.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/bridge_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/commands/commands_component.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/state/event_bus.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace EQT::Graphics;
namespace inv = eqt::inventory;

namespace {

// The wire slot of the container cell under a point (what the call site gets).
int16_t slotIdAt(const UILayout& l, const ContainerWindowsState& s, int x, int y) {
    const ContainerHit hit = hitTestContainer(l, s, x, y);
    if (hit.part != ContainerPart::Slot) return inv::SLOT_INVALID;
    return containerSlotId(s.windows[hit.window], hit.slot);
}

const std::string kRoot = WILLEQ_REPO_ROOT;
const char* kXmlPath = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Container.xml";

std::string slurpAbs(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string slurp(const std::string& rel) {
    const std::string s = slurpAbs(kRoot + "/" + rel);
    EXPECT_FALSE(s.empty()) << "cannot open " << rel;
    return s;
}

struct XmlRect {
    bool found = false;
    int x = 0, y = 0, w = 0, h = 0;
};

// The element whose item="<name>" opens it; its first <Location> and <Size>.
XmlRect xmlRect(const std::string& xml, const std::string& name) {
    XmlRect r;
    const size_t a = xml.find("item=\"" + name + "\"");
    if (a == std::string::npos) return r;
    const size_t e = xml.find("</Size>", a);
    if (e == std::string::npos) return r;
    const std::string body = xml.substr(a, e + 7 - a);
    static const std::regex loc(
        "<Location>\\s*<X>(-?\\d+)</X>\\s*<Y>(-?\\d+)</Y>\\s*</Location>");
    static const std::regex size("<Size>\\s*<CX>(\\d+)</CX>\\s*<CY>(\\d+)</CY>");
    std::smatch m;
    if (!std::regex_search(body, m, loc)) return r;
    r.x = std::stoi(m[1]);
    r.y = std::stoi(m[2]);
    if (!std::regex_search(body, m, size)) return r;
    r.w = std::stoi(m[1]);
    r.h = std::stoi(m[2]);
    r.found = true;
    return r;
}

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

eqt::state::TradeskillContainerOpenedEvent carried(int16_t slot, uint8_t type,
                                                   uint8_t count,
                                                   const char* name = "Sewing Kit") {
    eqt::state::TradeskillContainerOpenedEvent e;
    e.isWorldObject = false;
    e.objectId = 0;
    e.inventorySlot = slot;
    e.containerName = name;
    e.containerType = type;
    e.slotCount = count;
    e.iconId = 598;
    return e;
}

eqt::state::TradeskillContainerOpenedEvent world(uint32_t dropId = 4242) {
    eqt::state::TradeskillContainerOpenedEvent e;
    e.isWorldObject = true;
    e.objectId = dropId;
    e.inventorySlot = -1;
    e.containerName = "Forge";
    e.containerType = inv::CONTAINER_BLACKSMITHING;
    e.slotCount = inv::WORLD_COUNT;
    return e;
}

eqt::state::InventorySlotChangedData slotEvent(int16_t id, bool has, uint32_t icon = 700) {
    eqt::state::InventorySlotChangedData d;
    d.slotId = id;
    d.hasItem = has;
    d.itemId = has ? 1234u : 0u;
    d.itemName = has ? "Silk Thread" : "";
    d.iconId = has ? icon : 0;
    d.quantity = has ? 3u : 0u;
    return d;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return irr::core::vector2di((r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
                                (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2);
}

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

template <class T>
const T* findIntent(const std::vector<eqt::events::RendererIntent>& v) {
    for (const auto& i : v) {
        if (const T* p = std::get_if<T>(&i)) return p;
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// 1. Geometry, against the oracle file
// ===========================================================================

TEST(ContainerLayoutOracle, TheWindowIsTheScreensOwnSizeAndLocation) {
    const std::string xml = slurpAbs(kXmlPath);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    const XmlRect s = xmlRect(xml, "ContainerWindow");
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.x, ContainerLayout::ORACLE_X);
    EXPECT_EQ(s.y, ContainerLayout::ORACLE_Y);
    EXPECT_EQ(s.w, ContainerLayout::WINDOW_W);
    EXPECT_EQ(s.h, ContainerLayout::WINDOW_H);
    // Style_Sizable false is WHY it is drawn unscaled.
    EXPECT_NE(xml.find("<Style_Sizable>false</Style_Sizable>"), std::string::npos);
}

TEST(ContainerLayoutOracle, EveryEraControlIsTranscribedVerbatim) {
    const std::string xml = slurpAbs(kXmlPath);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    struct Row { const char* item; int x, y, w, h; };
    const Row rows[] = {
        {"Container_Label", ContainerLayout::LABEL_X, ContainerLayout::LABEL_Y,
         ContainerLayout::LABEL_W, ContainerLayout::LABEL_H},
        {"Container_Icon", ContainerLayout::ICON_X, ContainerLayout::ICON_Y,
         ContainerLayout::ICON_SIZE, ContainerLayout::ICON_SIZE},
        {"Container_Combine", ContainerLayout::COMBINE_X, ContainerLayout::COMBINE_Y,
         ContainerLayout::COMBINE_W, ContainerLayout::COMBINE_H},
        {"Container_DoneButton", ContainerLayout::DONE_X, ContainerLayout::DONE_Y,
         ContainerLayout::DONE_W, ContainerLayout::DONE_H},
    };
    for (const auto& r : rows) {
        const XmlRect x = xmlRect(xml, r.item);
        ASSERT_TRUE(x.found) << r.item;
        EXPECT_EQ(x.x, r.x) << r.item;
        EXPECT_EQ(x.y, r.y) << r.item;
        EXPECT_EQ(x.w, r.w) << r.item;
        EXPECT_EQ(x.h, r.h) << r.item;
    }
    for (int i = 0; i < ContainerLayout::SLOT_COUNT; ++i) {
        const std::string item = "ContainerSlot" + std::to_string(i + 1);
        const XmlRect x = xmlRect(xml, item);
        ASSERT_TRUE(x.found) << item;
        EXPECT_EQ(x.x, ContainerLayout::SLOT_X[i]) << item;
        EXPECT_EQ(x.y, ContainerLayout::SLOT_Y[i]) << item;
        EXPECT_EQ(x.w, ContainerLayout::SLOT_SIZE) << item;
        EXPECT_EQ(x.h, ContainerLayout::SLOT_SIZE) << item;
        // EQType 30 + i: a RELATIVE container index, not a wire slot.
        const size_t a = xml.find("item=\"" + item + "\"");
        const std::string body = xml.substr(a, xml.find("</InvSlot>", a) - a);
        EXPECT_NE(body.find("<EQType>" + std::to_string(ContainerLayout::EQTYPE_BEGIN + i) +
                            "</EQType>"),
                  std::string::npos)
            << item;
        // No ItemOffset on these cells (the bank's have one).
        EXPECT_EQ(body.find("ItemOffset"), std::string::npos) << item;
    }
    // The XML's own button text -- the label containerCombineLabel() uses.
    ContainerWindow w;
    w.isWorldObject = true;
    EXPECT_NE(xml.find("<Text>Combine</Text>"), std::string::npos);
    EXPECT_STREQ(containerCombineLabel(w), "Combine");
}

TEST(ContainerLayoutOracle, PlacementZeroIsTheOraclesLocationAndFitsAt640x480) {
    const UILayout l = layoutAt(640, 480);
    const auto w = ContainerLayout::windowRect(l);
    EXPECT_EQ(w.UpperLeftCorner.X, 350);
    EXPECT_EQ(w.UpperLeftCorner.Y, 100);
    EXPECT_EQ(w.getWidth(), 100);
    EXPECT_EQ(w.getHeight(), 366);
    // Every control is inside the window, Done included under fable's
    // own 18px title bar.
    const irr::core::rect<irr::s32> screen(0, 0, 640, 480);
    EXPECT_TRUE(screen.isRectCollided(w));
    EXPECT_LE(w.LowerRightCorner.Y, 480);
    EXPECT_LE(ContainerLayout::doneButtonRect(l).LowerRightCorner.Y, w.LowerRightCorner.Y);
    for (int i = 0; i < ContainerLayout::SLOT_COUNT; ++i) {
        const auto c = ContainerLayout::slotRect(l, i);
        EXPECT_GE(c.UpperLeftCorner.X, w.UpperLeftCorner.X);
        EXPECT_LE(c.LowerRightCorner.X, w.LowerRightCorner.X);
    }
    // Cells 1 and 2 share a row (row-major, two wide).
    EXPECT_EQ(ContainerLayout::slotRect(l, 0).UpperLeftCorner.Y,
              ContainerLayout::slotRect(l, 1).UpperLeftCorner.Y);
    EXPECT_EQ(ContainerLayout::slotRect(l, 9).getWidth(), 0 + 40);
    EXPECT_EQ(ContainerLayout::slotRect(l, 10).getWidth(), 0);  // out of range
}

TEST(ContainerLayoutOracle, TheWindowStaysOnScreenAtEveryResolution) {
    for (const auto& dims : {std::pair<int, int>{640, 480}, {800, 600}, {1024, 768}}) {
        const UILayout l = layoutAt(dims.first, dims.second);
        const auto w = ContainerLayout::windowRect(l);
        EXPECT_EQ(w.UpperLeftCorner.X, 350) << dims.first;
        EXPECT_EQ(w.UpperLeftCorner.Y, 100) << dims.first;
        EXPECT_LE(w.LowerRightCorner.Y, dims.second);
    }
    // Clamped, never off screen, on a screen narrower than the window's spot.
    const UILayout tiny = layoutAt(400, 300);
    const auto t = ContainerLayout::windowRect(tiny);
    EXPECT_GE(t.UpperLeftCorner.X, 0);
    EXPECT_GE(t.UpperLeftCorner.Y, 0);
    EXPECT_LE(t.LowerRightCorner.X, 400);
}

TEST(ContainerLayoutOracle, TheAugmentControlsAreInTheFileAndOutOfFable) {
    // Pinned NEGATIVE: the augment half is post-Velious (#215/#151).
    const std::string xml = slurpAbs(kXmlPath);
    if (!xml.empty()) {
        EXPECT_NE(xml.find("<Pieces>InsertButton</Pieces>"), std::string::npos);
    }
    for (const char* f : {"include/client/graphics/ui/container_layout.h",
                          "src/client/graphics/ui/container_panel.cpp",
                          "src/client/graphics/ui/container_input.cpp"}) {
        const std::string src = slurp(f);
        // Code identifiers only -- the header's comment NAMES the augment
        // controls on purpose, to say they are out.
        for (const char* aug : {"AUGMENT_", "INSERT_X", "REMOVE_X", "DELETE_X",
                                "augmentRect", "insertButtonRect",
                                "removeButtonRect", "deleteButtonRect"}) {
            EXPECT_EQ(src.find(aug), std::string::npos) << f << " names " << aug;
        }
    }
}

// ===========================================================================
// 2. Appliers
// ===========================================================================

TEST(ContainerApplier, ACarriedContainersOpenEventProducesAWindow) {
    ContainerWindowsState s;
    applyContainerOpened(s, carried(inv::GENERAL_BEGIN + 1, inv::CONTAINER_TAILORING, 10));
    const int idx = containerWindowIndex(false, inv::GENERAL_BEGIN + 1);
    ASSERT_EQ(idx, 2);
    const auto& w = s.windows[idx];
    EXPECT_TRUE(w.isOpen);
    EXPECT_FALSE(w.isWorldObject);
    EXPECT_EQ(w.inventorySlot, inv::GENERAL_BEGIN + 1);
    EXPECT_EQ(w.name, "Sewing Kit");
    EXPECT_EQ(w.iconId, 598u);
    EXPECT_EQ(containerVisibleSlots(w), 10);
    EXPECT_EQ(topmostContainerWindow(s), idx);
}

TEST(ContainerApplier, ABagOutsideTheGeneralSlotsOpensNothing) {
    ContainerWindowsState s;
    applyContainerOpened(s, carried(inv::CURSOR_SLOT, 0, 8));
    applyContainerOpened(s, carried(inv::BANK_BEGIN, 0, 8));
    applyContainerOpened(s, carried(inv::SLOT_PRIMARY, 0, 8));
    EXPECT_EQ(topmostContainerWindow(s), -1);
}

// UIGAP-ORACLE 151 gestures.tsv row 20: a second bag opens at the SAME
// screen position, on top of the first.
TEST(ContainerApplier, ASecondBagOpensAtTheSamePlaceOnTop) {
    ContainerWindowsState s;
    applyContainerOpened(s, carried(inv::GENERAL_BEGIN, 0, 6, "Large Bag"));
    applyContainerOpened(s, carried(inv::GENERAL_BEGIN + 1, 0, 8, "Backpack"));
    EXPECT_TRUE(s.windows[1].isOpen);
    EXPECT_TRUE(s.windows[2].isOpen);
    EXPECT_GT(s.windows[2].openSequence, s.windows[1].openSequence);
    const UILayout l = layoutAt(800, 600);
    const auto c = centre(ContainerLayout::doneButtonRect(l));
    const ContainerHit hit = hitTestContainer(l, s, c.X, c.Y);
    EXPECT_EQ(hit.window, 2) << "the newest window is on top";
    EXPECT_EQ(hit.part, ContainerPart::Done);
    // Closing it uncovers the first; a repeat open refreshes in place.
    applyContainerClosed(s, {false, 0, static_cast<int16_t>(inv::GENERAL_BEGIN + 1)});
    EXPECT_EQ(hitTestContainer(l, s, c.X, c.Y).window, 1);
    const uint32_t seq = s.windows[1].openSequence;
    applyContainerOpened(s, carried(inv::GENERAL_BEGIN, 0, 6, "Large Bag"));
    EXPECT_EQ(s.windows[1].openSequence, seq);
}

TEST(ContainerApplier, WorldAndBagCellsRouteAndNothingElseDoes) {
    ContainerWindowsState s;
    EXPECT_TRUE(applyContainerSlotChanged(s, slotEvent(inv::WORLD_BEGIN + 2, true)));
    EXPECT_TRUE(s.windows[0].slots[2].hasItem);
    const int16_t sub = inv::calcBagSlotId(inv::GENERAL_BEGIN + 1, 4);  // 265
    EXPECT_EQ(sub, 265);
    EXPECT_TRUE(applyContainerSlotChanged(s, slotEvent(sub, true, 1093)));
    EXPECT_TRUE(s.windows[2].slots[4].hasItem);
    EXPECT_EQ(s.windows[2].slots[4].iconId, 1093u);
    EXPECT_EQ(s.windows[2].slots[4].quantity, 3);
    EXPECT_TRUE(applyContainerSlotChanged(s, slotEvent(sub, false)));
    EXPECT_FALSE(s.windows[2].slots[4].hasItem);
    // Negatives: the cursor, a general slot, the bank, a cursor-bag slot.
    for (int16_t id : {int16_t(30), int16_t(22), int16_t(2000), int16_t(331),
                       int16_t(2031), int16_t(3000)}) {
        EXPECT_FALSE(applyContainerSlotChanged(s, slotEvent(id, true))) << id;
    }
}

TEST(ContainerApplier, ClosingAWorldContainerForgetsItsCellsAndABagKeepsThem) {
    ContainerWindowsState s;
    applyContainerOpened(s, world());
    applyContainerSlotChanged(s, slotEvent(inv::WORLD_BEGIN, true));
    applyContainerOpened(s, carried(inv::GENERAL_BEGIN, 0, 8));
    applyContainerSlotChanged(s, slotEvent(inv::calcBagSlotId(inv::GENERAL_BEGIN, 0), true));
    applyContainerClosed(s, {true, 4242, -1});
    applyContainerClosed(s, {false, 0, inv::GENERAL_BEGIN});
    EXPECT_FALSE(s.windows[0].slots[0].hasItem);
    EXPECT_TRUE(s.windows[1].slots[0].hasItem);
    EXPECT_EQ(topmostContainerWindow(s), -1);
}

// UIGAP-ORACLE 215 combine.tsv rows 1, 9-13, 16 (Combine on the Sewing Kit,
// Mixing Bowl, Medicine Bag, Jeweler's Kit, Toolbox, Mortar and Pestle and
// forge) and row 20 / 151 gestures.tsv row 19 (no Combine on the Large Bag or
// Backpack).
TEST(ContainerApplier, CombineAppearsOnEveryTradeskillContainerAndNoPlainBag) {
    ContainerWindow w;
    w.isWorldObject = true;
    EXPECT_TRUE(containerHasCombine(w));
    w.isWorldObject = false;
    for (uint8_t t : {uint8_t(inv::CONTAINER_TAILORING), uint8_t(inv::CONTAINER_MIXING_BOWL),
                      uint8_t(inv::CONTAINER_MEDICINE), uint8_t(inv::CONTAINER_JEWELRY_MAKING),
                      uint8_t(inv::CONTAINER_TINKERING), uint8_t(inv::CONTAINER_POISON_MAKING),
                      uint8_t(inv::CONTAINER_FLETCHING), uint8_t(inv::CONTAINER_BLACKSMITHING)}) {
        w.containerType = t;
        EXPECT_TRUE(containerHasCombine(w)) << int(t);
        EXPECT_STREQ(containerCombineLabel(w), "Combine") << int(t);
    }
    // Plain storage (eqemu BagType 0..8): no Combine.
    for (uint8_t t = 0; t <= inv::CONTAINER_BANDOLIER; ++t) {
        w.containerType = t;
        EXPECT_FALSE(containerHasCombine(w)) << int(t);
    }
}

// ===========================================================================
// 3. Clicks
// ===========================================================================

class ContainerClickTest : public ::testing::Test {
protected:
    void SetUp() override {
        layout = layoutAt(800, 600);
        input.init(&bridge);
    }
    std::vector<eqt::events::RendererIntent> click(const irr::core::rect<irr::s32>& r) {
        const auto c = centre(r);
        consumed = input.processContainerInput(c.X, c.Y, true, layout, state);
        return bridge.drainIntents();
    }
    UILayout layout;
    TestBridge bridge;
    StaticUIInput input;
    ContainerWindowsState state;
    bool consumed = false;
};

// UIGAP-ORACLE 215 combine.tsv row 18: OP_TradeSkillCombine container_slot=1000.
TEST_F(ContainerClickTest, CombineOnAWorldContainerSendsTheExperimentSlot) {
    applyContainerOpened(state, world());
    auto intents = click(ContainerLayout::combineButtonRect(layout));
    EXPECT_TRUE(consumed);
    const auto* c = findIntent<eqt::events::TradeskillCombineIntent>(intents);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->containerSlot, inv::SLOT_TRADESKILL_EXPERIMENT_COMBINE);
}

// UIGAP-ORACLE 215 combine.tsv rows 4 and 6: container_slot = the kit's general slot.
TEST_F(ContainerClickTest, CombineOnACarriedKitSendsTheBagsOwnSlot) {
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 1, inv::CONTAINER_TAILORING, 10));
    auto intents = click(ContainerLayout::combineButtonRect(layout));
    const auto* c = findIntent<eqt::events::TradeskillCombineIntent>(intents);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->containerSlot, inv::GENERAL_BEGIN + 1);
}

TEST_F(ContainerClickTest, APlainBagHasNoCombineToClick) {
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, inv::CONTAINER_BACKPACK, 8));
    auto intents = click(ContainerLayout::combineButtonRect(layout));
    EXPECT_TRUE(consumed);  // still the window's body
    EXPECT_EQ(findIntent<eqt::events::TradeskillCombineIntent>(intents), nullptr);
    EXPECT_TRUE(intents.empty());
}

TEST_F(ContainerClickTest, DoneAndTheCloseBoxAskAndCloseNothingLocally) {
    applyContainerOpened(state, world(777));
    // World Done: the drop id, no inventory slot. UIGAP-ORACLE 215
    // combine.tsv row 19: Done on the forge sends OP_ClickObjectAction open=0.
    auto intents = click(ContainerLayout::doneButtonRect(layout));
    const auto* w = findIntent<eqt::events::TradeskillCloseIntent>(intents);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->dropId, 777u);
    EXPECT_EQ(w->inventorySlot, -1);
    // Carried close box (the bag opens on top): drop id 0, the bag's slot.
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 2, 0, 6));
    intents = click(ContainerLayout::closeBoxRect(layout));
    const auto* b = findIntent<eqt::events::TradeskillCloseIntent>(intents);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->dropId, 0u);
    EXPECT_EQ(b->inventorySlot, inv::GENERAL_BEGIN + 2);
    // T290's rule: both are still open until the Closed events come back.
    EXPECT_TRUE(state.windows[0].isOpen);
    EXPECT_TRUE(state.windows[3].isOpen);
}

// UIGAP-ORACLE 151 gestures.tsv rows 22-23 and 215 combine.tsv row 3: a bag
// sub-slot is 251 + (general-22)*10 + n; 215 row 17: world slots are 4000+.
TEST_F(ContainerClickTest, ACellClickReachesTheOneCallSiteWithTheWireSlot) {
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, 0, 8));
    // An EMPTY cell with an empty cursor: nothing to pick up, no intent.
    auto intents = click(ContainerLayout::slotRect(layout, 3));
    EXPECT_TRUE(consumed);
    EXPECT_EQ(findIntent<eqt::events::CursorGestureIntent>(intents), nullptr);
    EXPECT_EQ(state.windows[1].hoveredSlot, 3);
    // An occupied cell: B442's CursorGesture picks it up, addressed by the
    // cell's wire slot 251 + 0*10 + 3 = 254.
    applyContainerSlotChanged(state, slotEvent(inv::calcBagSlotId(inv::GENERAL_BEGIN, 3), true));
    // A single item (a stack of 3 opens B442's QuantityWnd instead -- 151
    // gestures.tsv row 3 -- which pushes nothing until Accept).
    state.windows[1].slots[3].quantity = 1;
    intents = click(ContainerLayout::slotRect(layout, 3));
    const auto* g = findIntent<eqt::events::CursorGestureIntent>(intents);
    ASSERT_NE(g, nullptr) << "a container cell click did not reach CursorGesture";
    EXPECT_EQ(g->op, eqt::events::CursorGestureOp::Click);
    EXPECT_EQ(g->slot, 254);
    EXPECT_EQ(g->slot, inv::calcBagSlotId(inv::GENERAL_BEGIN, 3));

    ContainerWindowsState ws;
    applyContainerOpened(ws, world());
    const auto c = centre(ContainerLayout::slotRect(layout, 2));
    EXPECT_EQ(slotIdAt(layout, ws, c.X, c.Y), inv::WORLD_BEGIN + 2);
    // A world cell's click carries its 4000+ slot.
    applyContainerSlotChanged(ws, slotEvent(inv::WORLD_BEGIN + 2, true));
    ws.windows[0].slots[2].quantity = 1;
    input.processContainerInput(c.X, c.Y, true, layout, ws);
    intents = bridge.drainIntents();
    const auto* gw = findIntent<eqt::events::CursorGestureIntent>(intents);
    ASSERT_NE(gw, nullptr);
    EXPECT_EQ(gw->slot, inv::WORLD_BEGIN + 2);
}

TEST_F(ContainerClickTest, ACellBeyondTheBagsSlotCountIsNotASlot) {
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, 0, 4));  // belt pouch
    const auto c = centre(ContainerLayout::slotRect(layout, 6));
    EXPECT_EQ(slotIdAt(layout, state, c.X, c.Y), inv::SLOT_INVALID);
    applyContainerSlotChanged(state, slotEvent(inv::calcBagSlotId(inv::GENERAL_BEGIN, 6), true));
    auto intents = click(ContainerLayout::slotRect(layout, 6));
    EXPECT_EQ(findIntent<eqt::events::CursorGestureIntent>(intents), nullptr);
}

TEST_F(ContainerClickTest, AClosedWindowEatsNoClickAndTheTopmostWins) {
    // Nothing open: nothing consumed.
    auto intents = click(ContainerLayout::doneButtonRect(layout));
    EXPECT_FALSE(consumed);
    EXPECT_TRUE(intents.empty());
    // Two windows share the one placement: the newer one takes the click.
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, 0, 8));
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 5, 0, 8));
    intents = click(ContainerLayout::doneButtonRect(layout));
    const auto* b = findIntent<eqt::events::TradeskillCloseIntent>(intents);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->inventorySlot, inv::GENERAL_BEGIN + 5);
}

// B992 (#312): EVERY open window is drawn, oldest first, so the newest -- the
// one ESC closes and a click reaches -- is drawn last, on top (151 row 20).
// Before B992 only the topmost was drawn, because a covered window's text
// bled through the one above it (test_ui_panel_z_order.cpp pins the fix).
TEST_F(ContainerClickTest, EveryOpenWindowIsDrawnOldestFirstAndTheNewestLast) {
    int order[ContainerWindowsState::MAX_WINDOWS];
    EXPECT_EQ(containerDrawOrder(state, order), 0);
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 4, 0, 8));
    applyContainerOpened(state, world(55));
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, 0, 8));
    ASSERT_EQ(containerDrawOrder(state, order), 3);
    EXPECT_EQ(order[0], 5);
    EXPECT_EQ(order[1], ContainerWindowsState::WORLD_WINDOW);
    EXPECT_EQ(order[2], 1);
    EXPECT_EQ(order[2], topmostContainerWindow(state));
    // Re-opening an open window keeps its place; closing one drops it.
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 4, 0, 8));
    applyContainerClosed(state, {true, 55, -1});
    ASSERT_EQ(containerDrawOrder(state, order), 2);
    EXPECT_EQ(order[0], 5);
    EXPECT_EQ(order[1], 1);
    EXPECT_EQ(order[1], topmostContainerWindow(state));
}

// ESC follows the z-order: the newest window is on top (151 row 20), world
// container or bag alike -- and it is the window drawn last (above).
TEST_F(ContainerClickTest, EscapeAsksTheNewestWindowFirst) {
    EXPECT_FALSE(input.processContainerEscape(state));
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN, 0, 8));
    applyContainerOpened(state, world(55));
    applyContainerOpened(state, carried(inv::GENERAL_BEGIN + 4, 0, 8));
    EXPECT_TRUE(input.processContainerEscape(state));
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::TradeskillCloseIntent>(intents[0]).inventorySlot,
              inv::GENERAL_BEGIN + 4);
    EXPECT_TRUE(state.windows[5].isOpen);  // not hidden locally
    applyContainerClosed(state, {false, 0, static_cast<int16_t>(inv::GENERAL_BEGIN + 4)});
    EXPECT_TRUE(input.processContainerEscape(state));
    intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::TradeskillCloseIntent>(intents[0]).dropId, 55u);
    EXPECT_EQ(std::get<eqt::events::TradeskillCloseIntent>(intents[0]).inventorySlot, -1);
}

// ===========================================================================
// 4. The game side, through the real command chain
// ===========================================================================

namespace {

class BagHarness {
public:
    BagHarness()
        : eq("127.0.0.1", 0, "test", "test", "test", "Testchar"),
          handler(eq),
          dispatcher(eq.GetGameState()),
          processor(eq.GetGameState(), dispatcher) {
        eq.setBridge(&bridge);
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        dispatcher.setActionHandler(&handler);
    }
    ~BagHarness() { eq.setBridge(nullptr); }

    void putBag(int16_t slot, const char* name, uint8_t type, uint8_t slots) {
        inv::ItemInstance bag;
        bag.itemId = 17005;
        bag.name = name;
        bag.bagType = type;
        bag.bagSlots = slots;
        bag.icon = 598;
        eq.GetInventoryManager()->setItem(slot, bag);
    }

    std::vector<eqt::state::GameEvent> events() { return bridge.drainEvents(); }

    // /bag takes inventory.slot_id: general 1 is server 23 (model 22).
    static std::string userSlot(int16_t model) {
        return std::to_string(inv::clientSlotToServer(model));
    }

    EverQuest eq;
    TestBridge bridge;
    eqt::EqActionHandler handler;
    eqt::action::ActionDispatcher dispatcher;
    eqt::action::CommandProcessor processor;
};

const eqt::state::TradeskillContainerOpenedEvent* findOpened(
        const std::vector<eqt::state::GameEvent>& ev) {
    for (const auto& e : ev) {
        if (e.type == eqt::state::GameEventType::TradeskillContainerOpened) {
            return &std::get<eqt::state::TradeskillContainerOpenedEvent>(e.data);
        }
    }
    return nullptr;
}

const eqt::state::TradeskillContainerClosedEvent* findClosed(
        const std::vector<eqt::state::GameEvent>& ev) {
    for (const auto& e : ev) {
        if (e.type == eqt::state::GameEventType::TradeskillContainerClosed) {
            return &std::get<eqt::state::TradeskillContainerClosedEvent>(e.data);
        }
    }
    return nullptr;
}

}  // namespace

// UIGAP-ORACLE 151 gestures.tsv rows 19 and 21: a bag's window opens (label =
// item name, slots = bag size) and the same gesture again closes it.
TEST(BagCommand, OpensACarriedKitAndSnapshotsItsCells) {
    BagHarness h;
    const int16_t g = inv::GENERAL_BEGIN + 1;
    h.putBag(g, "Sewing Kit", inv::CONTAINER_TAILORING, 10);
    inv::ItemInstance thread;
    thread.itemId = 13056;
    thread.name = "Silk Thread";
    thread.icon = 1093;
    thread.quantity = 2;
    h.eq.GetInventoryManager()->setItem(inv::calcBagSlotId(g, 1), thread);
    h.events();

    auto r = h.processor.processCommand("/bag " + BagHarness::userSlot(g));
    EXPECT_TRUE(r.success) << r.message;
    const auto ev = h.events();
    const auto* o = findOpened(ev);
    ASSERT_NE(o, nullptr) << "/bag published no TradeskillContainerOpened";
    EXPECT_FALSE(o->isWorldObject);
    EXPECT_EQ(o->inventorySlot, g);
    EXPECT_EQ(o->containerName, "Sewing Kit");
    EXPECT_EQ(o->containerType, inv::CONTAINER_TAILORING);
    EXPECT_EQ(o->slotCount, 10);
    EXPECT_EQ(o->iconId, 598u);
    EXPECT_TRUE(h.eq.Commands().IsBagOpen(g));

    // The ten cells, as enriched InventorySlotChanged, and they land in the
    // window through the real applier.
    ContainerWindowsState s;
    int cells = 0;
    for (const auto& e : ev) {
        if (e.type == eqt::state::GameEventType::TradeskillContainerOpened) {
            applyContainerOpened(s, std::get<eqt::state::TradeskillContainerOpenedEvent>(e.data));
        }
        if (e.type == eqt::state::GameEventType::InventorySlotChanged) {
            ++cells;
            applyContainerSlotChanged(s, std::get<eqt::state::InventorySlotChangedData>(e.data));
        }
    }
    EXPECT_EQ(cells, 10);
    EXPECT_TRUE(s.windows[2].isOpen);
    EXPECT_TRUE(s.windows[2].slots[1].hasItem);
    EXPECT_EQ(s.windows[2].slots[1].iconId, 1093u);
    EXPECT_FALSE(s.windows[2].slots[0].hasItem);

    // A second /bag on the same slot closes it.
    r = h.processor.processCommand("/bag " + BagHarness::userSlot(g));
    EXPECT_TRUE(r.success);
    const auto ev2 = h.events();
    const auto* c = findClosed(ev2);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->wasWorldObject);
    EXPECT_EQ(c->inventorySlot, g);
    EXPECT_EQ(findOpened(ev2), nullptr);
    EXPECT_FALSE(h.eq.Commands().IsBagOpen(g));
}

TEST(BagCommand, RefusesANonContainerAnEmptySlotAndANonGeneralSlot) {
    BagHarness h;
    inv::ItemInstance sword;
    sword.itemId = 5019;
    sword.name = "Rusty Short Sword";
    h.eq.GetInventoryManager()->setItem(inv::GENERAL_BEGIN, sword);
    h.events();

    h.processor.processCommand("/bag " + BagHarness::userSlot(inv::GENERAL_BEGIN));
    EXPECT_EQ(findOpened(h.events()), nullptr);
    EXPECT_FALSE(h.eq.Commands().IsBagOpen(inv::GENERAL_BEGIN));

    h.processor.processCommand("/bag " + BagHarness::userSlot(inv::GENERAL_BEGIN + 6));
    EXPECT_EQ(findOpened(h.events()), nullptr);

    // Usage failures: no argument, the cursor, a primary slot, junk.
    for (const char* bad : {"/bag", "/bag 33", "/bag 13", "/bag pack", "/bag 23 24"}) {
        auto r = h.processor.processCommand(bad);
        EXPECT_FALSE(r.success) << bad;
    }
    EXPECT_EQ(findOpened(h.events()), nullptr);
}

TEST(BagCommand, TheCloseIntentClosesTheBagAndLeavesTheWorldContainerAlone) {
    BagHarness h;
    const int16_t g = inv::GENERAL_BEGIN + 2;
    h.putBag(g, "Backpack", inv::CONTAINER_BACKPACK, 8);
    h.processor.processCommand("/bag " + BagHarness::userSlot(g));
    h.events();
    h.eq.m_active_tradeskill_object_id = 190;  // a forge the server opened

    eqt::events::TradeskillCloseIntent close{0};
    close.inventorySlot = g;
    h.bridge.pushIntent(close);
    h.eq.Bridge().ProcessBridgeIntents();

    // B1044: hold the drained events; a pointer into the temporary vector
    // dangled (read-after-free, 0 on macOS's allocator, stale bytes on glibc).
    const auto closedEvents = h.events();
    const auto* c = findClosed(closedEvents);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->inventorySlot, g);
    EXPECT_FALSE(h.eq.Commands().IsBagOpen(g));
    EXPECT_EQ(h.eq.m_active_tradeskill_object_id, 190u)
        << "closing a carried bag cleared the open world container";
}

TEST(BagCommand, TheBagLeavingItsSlotClosesItsWindow) {
    BagHarness h;
    const int16_t g = inv::GENERAL_BEGIN + 3;
    h.putBag(g, "Large Bag", inv::CONTAINER_LARGE_BAG, 8);
    h.processor.processCommand("/bag " + BagHarness::userSlot(g));
    h.events();
    // A sub-slot change does NOT close it...
    h.eq.Merchant().SyncInventorySlotToGameState(inv::calcBagSlotId(g, 0), true);
    EXPECT_TRUE(h.eq.Commands().IsBagOpen(g));
    EXPECT_EQ(findClosed(h.events()), nullptr);
    // ...a change to the bag's own slot does.
    h.eq.Merchant().SyncInventorySlotToGameState(g, false);
    EXPECT_FALSE(h.eq.Commands().IsBagOpen(g));
    // B1044: hold the drained events; a pointer into the temporary vector
    // dangled (read-after-free, 0 on macOS's allocator, stale bytes on glibc).
    const auto closedEvents = h.events();
    const auto* c = findClosed(closedEvents);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->inventorySlot, g);
}

// UIGAP-ORACLE 215 combine.tsv row 7: Combine with a stack in the container is
// refused CLIENT-side (eqstr 8504, no packet).
TEST(BagCommand, CombineWithAStackInTheContainerIsRefusedClientSide) {
    BagHarness h;
    const int16_t g = inv::GENERAL_BEGIN + 4;
    h.putBag(g, "Collapsible Sewing Kit", inv::CONTAINER_TAILORING, 8);
    inv::ItemInstance skins;
    skins.itemId = 13752;
    skins.name = "Wolf Skin";
    skins.quantity = 2;
    h.eq.GetInventoryManager()->setItem(inv::calcBagSlotId(g, 0), skins);
    EXPECT_TRUE(h.eq.Commands().ContainerHoldsAStack(g));
    skins.quantity = 1;
    h.eq.GetInventoryManager()->setItem(inv::calcBagSlotId(g, 0), skins);
    h.eq.GetInventoryManager()->setItem(inv::calcBagSlotId(g, 1), skins);
    EXPECT_FALSE(h.eq.Commands().ContainerHoldsAStack(g))
        << "two singles in different slots is the legal form eqstr 8504 asks for";
    // The world container form reads 4000-4009.
    skins.quantity = 3;
    h.eq.GetInventoryManager()->setItem(inv::WORLD_BEGIN + 2, skins);
    EXPECT_TRUE(h.eq.Commands().ContainerHoldsAStack(inv::SLOT_TRADESKILL_EXPERIMENT_COMBINE));
    // And the refusal sits on the ONE path both Combine and /combine take.
    const std::string src = slurp("src/client/bridge/bridge_component_intents.cpp");
    const size_t a = src.find("BridgeComponent::onTradeskillCombineIntent(");
    ASSERT_NE(a, std::string::npos);
    const std::string body = src.substr(a, src.find("\n}", a) - a);
    EXPECT_LT(body.find("ContainerHoldsAStack"), body.find("SendTradeSkillCombine"));
    EXPECT_NE(body.find("8504"), std::string::npos);
}

// ===========================================================================
// 5. The world container opens the container window, not the recipe window
// ===========================================================================

// UIGAP-ORACLE 215 combine.tsv rows 15-16: the forge opens (the official
// client shows the recipe view first; fable opens straight in the Experiment /
// Container view, Will's ruling) with label "Forge", 10 slots, Combine, Done.
TEST(WorldContainer, AServerOpenLandsInTheContainerWindow) {
    BagHarness h;
    EQT::ClickObjectAction_Struct action;
    std::memset(&action, 0, sizeof(action));
    action.drop_id = 190;
    action.open = 1;
    action.type = inv::CONTAINER_BLACKSMITHING;
    std::snprintf(action.object_name, sizeof(action.object_name), "%s", "Forge");
    std::vector<uint8_t> body(sizeof(action));
    std::memcpy(body.data(), &action, sizeof(action));
    h.eq.ReplayZonePacket(HC_OP_ClickObjectAction, body.data(), body.size());

    const auto ev = h.events();
    const auto* o = findOpened(ev);
    ASSERT_NE(o, nullptr);
    ContainerWindowsState s;
    applyContainerOpened(s, *o);
    EXPECT_TRUE(s.windows[ContainerWindowsState::WORLD_WINDOW].isOpen);
    EXPECT_EQ(s.windows[0].objectId, 190u);
    EXPECT_EQ(containerVisibleSlots(s.windows[0]), 10);
    EXPECT_TRUE(containerHasCombine(s.windows[0]));
    EXPECT_EQ(containerCombineSlot(s.windows[0]), inv::SLOT_TRADESKILL_EXPERIMENT_COMBINE);
}

TEST(WorldContainer, TheBridgeArmNoLongerFeedsTheRecipeWindow) {
    const std::string src = slurp("src/client/bridge/irrlicht_bridge.cpp");
    const size_t a = src.find("case state::GameEventType::TradeskillContainerOpened:");
    ASSERT_NE(a, std::string::npos);
    const size_t b = src.find("case state::GameEventType::TradeskillRecipeListCleared:", a);
    ASSERT_NE(b, std::string::npos);
    const std::string arms = src.substr(a, b - a);
    EXPECT_NE(arms.find("applyContainerOpened"), std::string::npos);
    EXPECT_NE(arms.find("applyContainerClosed"), std::string::npos);
    // Pinned negative: the recipe window (EQUI_TradeskillWnd) is not opened.
    EXPECT_EQ(arms.find("applyTradeskillContainerOpened"), std::string::npos);
    EXPECT_EQ(arms.find("tradeskillState_"), std::string::npos);
    // And the window is drawn and hit-tested every frame.
    const std::string frame = slurp("src/client/graphics/frame/renderer_frame_render.cpp");
    EXPECT_NE(frame.find("renderContainerWindows"), std::string::npos);
    EXPECT_NE(frame.find("processContainerInput"), std::string::npos);
    const std::string esc = slurp("src/client/graphics/frame/renderer_frame_input.cpp");
    EXPECT_NE(esc.find("processContainerEscape"), std::string::npos);
}
