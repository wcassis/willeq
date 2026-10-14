// B888 (GitLab #204): the two-page Character Creation screen -- geometry
// against EQUI_CharacterCreate.xml, the hidden-entry layout, the state
// machine's page flow and refusals, and click resolution. Device-free.
//
// Oracle: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterCreate.xml
// (the geometry cases skip without it) and the frames in
// /mnt/projects/eq-oracle/logs/shots/ cited in char_create_screen.h.

#include <gtest/gtest.h>

#include "client/graphics/ui/char_create_layout.h"
#include "client/graphics/ui/char_create_panel.h"
#include "client/graphics/ui/char_create_screen.h"
#include "client/graphics/ui/face_pick_layout.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_rules.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace EQT::Graphics;
using namespace eqt::charcreate;
namespace fs = std::filesystem;
using L = CharCreateLayout;

namespace {

const fs::path kXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterCreate.xml";
const fs::path kFaceXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_FacePick.xml";

std::string ReadXml() {
    std::ifstream in(kXml);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int Tag(const std::string& body, const std::string& tag) {
    const size_t at = body.find("<" + tag + ">");
    if (at == std::string::npos) return -99999;
    return std::atoi(body.c_str() + at + tag.size() + 2);
}

// The element body of item `name` (up to the next element's item= attribute).
std::string Body(const std::string& xml, const std::string& name) {
    const size_t at = xml.find("item=\"" + name + "\"");
    if (at == std::string::npos) return std::string();
    const size_t next = xml.find("item=\"", at + 6);
    return xml.substr(at, next == std::string::npos ? std::string::npos : next - at);
}

// FacePick's XML uses whitespace around item=, unlike CharacterCreate's.
std::string FaceBody(const std::string& xml, const std::string& name) {
    const std::regex start("<(Button|StaticText|Screen)\\s+item\\s*=\\s*\"" + name + "\"");
    std::smatch m;
    if (!std::regex_search(xml, m, start)) return {};
    const size_t from = static_cast<size_t>(m.position());
    const size_t end = xml.find("</" + m[1].str() + ">", from);
    return xml.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

struct XRect { int x, y, w, h; };
XRect Loc(const std::string& xml, const std::string& name) {
    const std::string b = Body(xml, name);
    return {Tag(b, "X"), Tag(b, "Y"), Tag(b, "CX"), Tag(b, "CY")};
}

XRect FaceLoc(const std::string& xml, const std::string& name) {
    const std::string b = FaceBody(xml, name);
    return {Tag(b, "X"), Tag(b, "Y"), Tag(b, "CX"), Tag(b, "CY")};
}

XRect FaceRel(const FacePickLayout::Rect& child, const FacePickLayout::Rect& parent) {
    return {child.UpperLeftCorner.X - parent.UpperLeftCorner.X - FacePickLayout::kClientX,
            child.UpperLeftCorner.Y - parent.UpperLeftCorner.Y - FacePickLayout::kClientY,
            child.getWidth(), child.getHeight()};
}

std::string FaceText(const std::string& xml, const std::string& name) {
    const std::string b = FaceBody(xml, name);
    const size_t begin = b.find("<Text>");
    const size_t end = b.find("</Text>", begin);
    return begin == std::string::npos || end == std::string::npos
        ? std::string() : b.substr(begin + 6, end - begin - 6);
}

// A child's rect relative to its parent's CLIENT origin.
XRect Rel(const L::Rect& child, const L::Rect& parent) {
    return {child.UpperLeftCorner.X - parent.UpperLeftCorner.X - L::kClientInset,
            child.UpperLeftCorner.Y - parent.UpperLeftCorner.Y - L::kClientInset,
            child.getWidth(), child.getHeight()};
}

void ExpectSame(const XRect& want, const XRect& got, const std::string& what) {
    EXPECT_EQ(got.x, want.x) << what << " x";
    EXPECT_EQ(got.y, want.y) << what << " y";
    EXPECT_EQ(got.w, want.w) << what << " w";
    EXPECT_EQ(got.h, want.h) << what << " h";
}

const CharCreateData& Data() {
    static CharCreateData d = [] {
        CharCreateData x;
        EXPECT_TRUE(x.loadFile(std::string(WILLEQ_SOURCE_DIR) + "/data/char_create.tsv")) << x.error();
        return x;
    }();
    return d;
}

constexpr int W = L::kDesignW, H = L::kDesignH;

CharCreateScreenState OpenHumanWarrior() {
    CharCreateScreenState s;
    EXPECT_TRUE(openCharCreateWith(s, Data(), Race::Human, Gender::Male, Class::Warrior));
    return s;
}

}  // namespace

// ===========================================================================
// Geometry against the XML
// ===========================================================================

TEST(FacePickLayout, EveryXmlRectAndLabelMatches) {
    if (!fs::exists(kFaceXml)) GTEST_SKIP() << "no oracle install";
    std::ifstream in(kFaceXml);
    std::stringstream contents;
    contents << in.rdbuf();
    const std::string xml = contents.str();
    using F = FacePickLayout;
    const auto window = F::windowRect(W, H);
    ExpectSame(FaceLoc(xml, "FacePickWindow"),
               {F::kXmlX, F::kXmlY, F::kWindowW, F::kWindowH}, "FacePickWindow");
    const std::array<const char*, F::kRows> names = {
        "Face", "Hair", "Hair_Color", "Eyes", "Beard", "Beard_Color", "Wode"
    };
    for (int row = 0; row < F::kRows; ++row) {
        const std::string stem = "FPW_" + std::string(names[static_cast<size_t>(row)]);
        ExpectSame(FaceLoc(xml, stem + "_Down_Button"), FaceRel(F::downRect(W, H, row), window), stem + " down");
        ExpectSame(FaceLoc(xml, stem + "_Text"), FaceRel(F::textRect(W, H, row), window), stem + " text");
        ExpectSame(FaceLoc(xml, stem + "_Up_Button"), FaceRel(F::upRect(W, H, row), window), stem + " up");
        EXPECT_EQ(FaceText(xml, stem + "_Text"), F::rowLabel(row)) << stem;
    }
    ExpectSame(FaceLoc(xml, "FPW_Random_Button"), FaceRel(F::randomRect(W, H), window), "Random");
    ExpectSame(FaceLoc(xml, "FPW_Accept_Button"), FaceRel(F::acceptRect(W, H), window), "Accept");
    ExpectSame(FaceLoc(xml, "FPW_Cancel_Button"), FaceRel(F::cancelRect(W, H), window), "Cancel");
    EXPECT_EQ(FaceText(xml, "FPW_Random_Button"), "Random");
    EXPECT_EQ(FaceText(xml, "FPW_Accept_Button"), "Accept");
    EXPECT_EQ(FaceText(xml, "FPW_Cancel_Button"), "Cancel");
    EXPECT_EQ(FaceText(xml, "FacePickWindow"), "Choose Your Appearance");
    const std::regex controls("<(?:Button|StaticText)\\s+item\\s*=\\s*\"FPW_");
    EXPECT_EQ(std::distance(std::sregex_iterator(xml.begin(), xml.end(), controls),
                            std::sregex_iterator()), 3 * F::kRows + 3);
}

TEST(FacePick, TsvRangesAndModalAcceptCancelReachPacket) {
    const std::string fixture =
        "2\t0\tface\t0\t7\t1\tfixture\n"
        "2\t0\thair\t0\t3\t1\tfixture\n"
        "2\t0\thaircolor\t0\t5\t1\tfixture\n"
        "2\t0\teyes\t0\t9\t1\tfixture\n"
        "2\t0\tbeard\t0\t2\t1\tfixture\n"
        "2\t0\tbeardcolor\t0\t5\t1\tfixture\n"
        "2\t0\twode\t0\t8\t1\tfixture\n";
    FacePickData ranges;
    ASSERT_TRUE(ranges.loadText(fixture)) << ranges.error();
    ASSERT_NE(ranges.find(Race::Barbarian, Gender::Male), nullptr);
    EXPECT_EQ(ranges.find(Race::Barbarian, Gender::Female), nullptr);
    EXPECT_FALSE(ranges.loadText(fixture + "2\t0\tface\t0\t7\t1\tfixture\n"));
    EXPECT_EQ(ranges.find(Race::Barbarian, Gender::Male), nullptr);
    ASSERT_TRUE(ranges.loadText(fixture)) << ranges.error();

    CharCreateScreenState s;
    ASSERT_TRUE(openCharCreateWith(s, Data(), Race::Barbarian, Gender::Male, Class::Warrior,
                                   /*useOldModels=*/false));   // the fixture rows are the Luclin column
    s.faceData = ranges;
    const auto button = L::faceButtonRect(W, H).getCenter();
    const auto open = hitTestCharCreate(W, H, s, button.X, button.Y);
    ASSERT_EQ(open.hit, CharCreateHit::FacePick);
    applyCharCreateClick(s, open, false);
    ASSERT_TRUE(s.facePick.isOpen);
    EXPECT_EQ(pressNext(s).kind, CharCreateAction::Kind::None);
    EXPECT_EQ(s.messageEqstr, 0u);  // the modal blocks page navigation

    stepFacePick(s.facePick, 0, -1);
    EXPECT_EQ(s.facePick.values[0], 7u);  // down from min wraps to max
    stepFacePick(s.facePick, 0, 1);
    EXPECT_EQ(s.facePick.values[0], 0u);  // up from max wraps to min
    for (int n = 0; n < 100; ++n) {
        randomFacePick(s.facePick);
        for (size_t row = 0; row < kFacePickFeatures; ++row) {
            EXPECT_GE(s.facePick.values[row], s.facePick.ranges[row].min);
            EXPECT_LE(s.facePick.values[row], s.facePick.ranges[row].max);
        }
    }
    const auto cancel = FacePickLayout::cancelRect(W, H).getCenter();
    const auto cancelHit = hitTestCharCreate(W, H, s, cancel.X, cancel.Y);
    ASSERT_EQ(cancelHit.hit, CharCreateHit::FacePickControl);
    EXPECT_EQ(cancelHit.facePick.hit, FacePickHit::Cancel);
    applyCharCreateClick(s, cancelHit, false);
    EXPECT_FALSE(s.facePick.isOpen);
    EXPECT_EQ(s.appearance[0], 0u);  // discarded

    ASSERT_TRUE(openFacePick(s));
    stepFacePick(s.facePick, 0, -1);  // face 7
    stepFacePick(s.facePick, 6, 1);
    stepFacePick(s.facePick, 6, 1);   // wode 2
    stepFacePick(s.facePick, 3, 1);   // eyes 1
    const auto accept = FacePickLayout::acceptRect(W, H).getCenter();
    applyCharCreateClick(s, hitTestCharCreate(W, H, s, accept.X, accept.Y), false);
    EXPECT_FALSE(s.facePick.isOpen);
    EXPECT_EQ(s.appearance[0], 7u);
    EXPECT_EQ(s.appearance[6], 2u);
    const auto request = buildRequest(s);
    EXPECT_EQ(request.face, 27u);      // Barbarian wode occupies face's tens
    EXPECT_EQ(request.eyecolor1, 1u);
    EXPECT_EQ(request.eyecolor2, 1u);
}

TEST(FacePick, ShippedRangesCoverEveryInEraRaceAndGender) {
    FacePickData ranges;
    ASSERT_TRUE(ranges.loadShipped()) << ranges.error();
    const auto races = Data().visibleRaces();
    for (const auto& race : races) {
        for (uint32_t gender : {Gender::Male, Gender::Female}) {
            const auto* pair = ranges.find(race.id, gender);
            ASSERT_NE(pair, nullptr) << race.label << " gender=" << gender;
            for (const auto& picker : *pair) {
                EXPECT_FALSE(picker.source.empty());
                EXPECT_LE(picker.min, picker.max);
                if (!picker.visible) {
                    EXPECT_EQ(picker.min, 0u);
                    EXPECT_EQ(picker.max, 0u);
                }
            }
        }
    }
}

// B888-CC1 finding 1 (MR !168): the visible_classic column gates the CLASSIC
// UI (useOldModels, the client's default) and the visible (Luclin) column
// gates the Luclin UI. Drawing, hit-testing and Random all consume the
// era-selected column, because openFacePick copies it into the modal's plain
// `visible` field. The fixture rows: HAIR is visible in BOTH eras, WODE is
// Luclin-only (a Barbarian's woad is classic-visible in the real table --
// this fixture deliberately makes it Luclin-only to exercise the gate), EYES
// is classic-only.
TEST(FacePick, ClassicModelsGateTheRowsOnTheModelEra) {
    const std::string fixture =
        "2\t0\tface\t0\t7\t1\t1\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\thair\t0\t3\t1\t1\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\thaircolor\t0\t19\t0\t0\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\teyes\t0\t9\t0\t1\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\tbeard\t0\t5\t1\t1\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\tbeardcolor\t0\t19\t1\t1\tfixture\tUNMEASURED\tUNMEASURED\n"
        "2\t0\twode\t0\t8\t1\t0\tfixture\tUNMEASURED\tUNMEASURED\n";
    FacePickData ranges;
    ASSERT_TRUE(ranges.loadText(fixture)) << ranges.error();
    const auto* pair = ranges.find(Race::Barbarian, Gender::Male);
    ASSERT_NE(pair, nullptr);
    // The parser keeps both columns apart.
    EXPECT_TRUE((*pair)[static_cast<size_t>(FacePickFeature::Hair)].visibleFor(true));
    EXPECT_TRUE((*pair)[static_cast<size_t>(FacePickFeature::Hair)].visibleFor(false));
    EXPECT_TRUE((*pair)[static_cast<size_t>(FacePickFeature::Eyes)].visibleFor(true));
    EXPECT_FALSE((*pair)[static_cast<size_t>(FacePickFeature::Eyes)].visibleFor(false));
    EXPECT_FALSE((*pair)[static_cast<size_t>(FacePickFeature::Wode)].visibleFor(true));
    EXPECT_TRUE((*pair)[static_cast<size_t>(FacePickFeature::Wode)].visibleFor(false));
    // A 10-column row with a bad visible_classic is refused (and a failed
    // load clears the table, so reload the fixture before using it).
    EXPECT_FALSE(ranges.loadText(fixture + "2\t0\tface\t0\t7\t1\t2\tfixture\tUNMEASURED\tUNMEASURED\n"));
    ASSERT_TRUE(ranges.loadText(fixture)) << ranges.error();

    const auto open = [&](bool oldModels, CharCreateScreenState& s) -> bool {
        EXPECT_TRUE(openCharCreateWith(s, Data(), Race::Barbarian, Gender::Male, Class::Warrior, oldModels));
        s.faceData = ranges;
        EXPECT_TRUE(openFacePick(s));
        return s.facePick.isOpen;
    };
    const auto rowCentre = [](int row) {
        const auto r = FacePickLayout::upRect(W, H, row);
        return r.getCenter();
    };
    {   // Classic models: EYES shown, WODE not; hitting the WODE row is blocked.
        CharCreateScreenState s;
        ASSERT_TRUE(open(true, s));
        EXPECT_TRUE(s.facePick.ranges[static_cast<size_t>(FacePickFeature::Eyes)].visible);
        EXPECT_FALSE(s.facePick.ranges[static_cast<size_t>(FacePickFeature::Wode)].visible);
        EXPECT_EQ(hitTestFacePick(W, H, s.facePick, rowCentre(3).X, rowCentre(3).Y).hit, FacePickHit::Up);
        EXPECT_EQ(hitTestFacePick(W, H, s.facePick, rowCentre(6).X, rowCentre(6).Y).hit, FacePickHit::Blocked);
        // Stepping and Random honour the same gate.
        stepFacePick(s.facePick, static_cast<int>(FacePickFeature::Wode), 1);
        EXPECT_EQ(s.facePick.values[static_cast<size_t>(FacePickFeature::Wode)], 0u);
        for (int n = 0; n < 100; ++n) {
            randomFacePick(s.facePick);
            EXPECT_EQ(s.facePick.values[static_cast<size_t>(FacePickFeature::Wode)], 0u);
            EXPECT_GE(s.facePick.values[static_cast<size_t>(FacePickFeature::Eyes)], 0u);
            EXPECT_LE(s.facePick.values[static_cast<size_t>(FacePickFeature::Eyes)], 9u);
        }
    }
    {   // Luclin models: WODE shown, EYES not.
        CharCreateScreenState s;
        ASSERT_TRUE(open(false, s));
        EXPECT_FALSE(s.facePick.ranges[static_cast<size_t>(FacePickFeature::Eyes)].visible);
        EXPECT_TRUE(s.facePick.ranges[static_cast<size_t>(FacePickFeature::Wode)].visible);
        EXPECT_EQ(hitTestFacePick(W, H, s.facePick, rowCentre(6).X, rowCentre(6).Y).hit, FacePickHit::Up);
        EXPECT_EQ(hitTestFacePick(W, H, s.facePick, rowCentre(3).X, rowCentre(3).Y).hit, FacePickHit::Blocked);
        stepFacePick(s.facePick, static_cast<int>(FacePickFeature::Eyes), 1);
        EXPECT_EQ(s.facePick.values[static_cast<size_t>(FacePickFeature::Eyes)], 0u);
    }
}

TEST(FacePick, NonzeroMinimumWrapsAtItsOwnEndpoints) {
    FacePickState draft;
    draft.isOpen = true;
    draft.ranges[static_cast<size_t>(FacePickFeature::HairColor)] = {13, 18, true, true, "fixture"};
    draft.values[static_cast<size_t>(FacePickFeature::HairColor)] = 13;
    stepFacePick(draft, static_cast<int>(FacePickFeature::HairColor), -1);
    EXPECT_EQ(draft.values[static_cast<size_t>(FacePickFeature::HairColor)], 18u);
    stepFacePick(draft, static_cast<int>(FacePickFeature::HairColor), 1);
    EXPECT_EQ(draft.values[static_cast<size_t>(FacePickFeature::HairColor)], 13u);
    for (int n = 0; n < 100; ++n) {
        randomFacePick(draft);
        EXPECT_GE(draft.values[static_cast<size_t>(FacePickFeature::HairColor)], 13u);
        EXPECT_LE(draft.values[static_cast<size_t>(FacePickFeature::HairColor)], 18u);
    }
}

TEST(FacePick, HiddenRowsCannotBeClickedOrRandomized) {
    FacePickData data;
    const std::string fixture =
        "6\t1\tface\t0\t7\t1\tfixture\n"
        "6\t1\thair\t0\t3\t1\tfixture\n"
        "6\t1\thaircolor\t13\t18\t1\tfixture\n"
        "6\t1\teyes\t0\t9\t1\tfixture\n"
        "6\t1\tbeard\t0\t0\t0\tfixture\n"
        "6\t1\tbeardcolor\t0\t0\t0\tfixture\n"
        "6\t1\twode\t0\t0\t0\tfixture\n";
    ASSERT_TRUE(data.loadText(fixture)) << data.error();
    CharCreateScreenState s;
    ASSERT_TRUE(openCharCreateWith(s, Data(), Race::DarkElf, Gender::Female, Class::Necromancer,
                                   /*useOldModels=*/false));   // the fixture rows are the Luclin column
    s.faceData = data;
    ASSERT_TRUE(openFacePick(s));
    const auto beard = FacePickLayout::upRect(W, H, 4).getCenter();
    EXPECT_EQ(hitTestFacePick(W, H, s.facePick, beard.X, beard.Y).hit, FacePickHit::Blocked);
    stepFacePick(s.facePick, 4, 1);
    EXPECT_EQ(s.facePick.values[4], 0u);
    for (int n = 0; n < 100; ++n) {
        randomFacePick(s.facePick);
        EXPECT_EQ(s.facePick.values[4], 0u);
        EXPECT_EQ(s.facePick.values[5], 0u);
        EXPECT_EQ(s.facePick.values[6], 0u);
        EXPECT_GE(s.facePick.values[2], 13u);
        EXPECT_LE(s.facePick.values[2], 18u);
    }
}

TEST(CharCreateLayout, ScreenSizesAreTheXmls) {
    if (!fs::exists(kXml)) GTEST_SKIP() << "no oracle install";
    const std::string xml = ReadXml();
    auto size = [&xml](const char* item) { const XRect r = Loc(xml, item); return std::make_pair(r.w, r.h); };
    EXPECT_EQ(size("CharacterCreation"), std::make_pair(L::kDesignW, L::kDesignH));
    EXPECT_EQ(size("CC_CharacterCreationTitle"), std::make_pair(L::kTitleW, L::kTitleH));
    EXPECT_EQ(size("CharacterCreationScreen0a"), std::make_pair(L::kScreen0aW, L::kScreen0aH));
    EXPECT_EQ(size("CharacterCreationScreen0b"), std::make_pair(L::kScreen0bW, L::kScreen0bH));
    EXPECT_EQ(size("CharacterCreationScreen0c"), std::make_pair(L::kScreen0cW, L::kScreen0cH));
    EXPECT_EQ(size("CharacterCreationScreen1a"), std::make_pair(L::kScreen1aW, L::kScreen1aH));
    EXPECT_EQ(size("CharacterCreationScreen1b"), std::make_pair(L::kScreen1bW, L::kScreen1bH));
    EXPECT_EQ(size("CharacterCreationScreen1c"), std::make_pair(L::kScreen1cW, L::kScreen1cH));
    EXPECT_EQ(size("CharacterCreationScreen1d"), std::make_pair(L::kScreen1dW, L::kScreen1dH));
}

TEST(CharCreateLayout, PageZeroControlsAreTheXmlsLocations) {
    if (!fs::exists(kXml)) GTEST_SKIP() << "no oracle install";
    const std::string xml = ReadXml();
    const auto a = L::screen0aRect(W, H);
    ExpectSame(Loc(xml, "CC0_Face_Button"), Rel(L::faceButtonRect(W, H), a), "Face");
    ExpectSame(Loc(xml, "CC0_Male_Button"), Rel(L::maleButtonRect(W, H), a), "Male");
    ExpectSame(Loc(xml, "CC0_Female_Button"), Rel(L::femaleButtonRect(W, H), a), "Female");
    ExpectSame(Loc(xml, "CC0a_Title"), Rel(L::characterTitleRect(W, H), a), "CC0a_Title");
    for (int n = 0; n < L::kClassSlots; ++n) {
        const std::string item = "CC0_Class" + std::to_string(n) + "_Button";
        ExpectSame(Loc(xml, item), Rel(L::classSlotRect(W, H, n), a), item);
    }
    // The race column's slots are the XML's, sorted by y (CC0_Race14 Froglok at 181).
    std::set<int> ys;
    for (int n = 0; n < L::kRaceSlots; ++n) {
        const XRect r = Loc(xml, "CC0_Race" + std::to_string(n) + "_Button");
        ys.insert(r.y);
        EXPECT_EQ(r.x, 2);
        EXPECT_EQ(r.w, 96);
        EXPECT_EQ(r.h, 24);
    }
    int slot = 0;
    for (int y : ys) EXPECT_EQ(Rel(L::raceSlotRect(W, H, slot++), a).y, y);

    const auto b = L::screen0bRect(W, H);
    ExpectSame(Loc(xml, "CC0b_Title"), Rel(L::abilitiesTitleRect(W, H), b), "CC0b_Title");
    // Anchored: right-measured L/R, top-measured T/B, against the 0b client area.
    auto anchors = [&xml](const std::string& item) {
        const std::string body = Body(xml, item);
        return std::vector<int>{Tag(body, "LeftAnchorOffset"), Tag(body, "TopAnchorOffset"),
                                Tag(body, "RightAnchorOffset"), Tag(body, "BottomAnchorOffset")};
    };
    const int cw = b.getWidth() - 2 * L::kClientInset;
    auto asAnchors = [&](const L::Rect& r) {
        const int ox = b.UpperLeftCorner.X + L::kClientInset, oy = b.UpperLeftCorner.Y + L::kClientInset;
        return std::vector<int>{cw - (r.UpperLeftCorner.X - ox), r.UpperLeftCorner.Y - oy,
                                cw - (r.LowerRightCorner.X - ox), r.LowerRightCorner.Y - oy};
    };
    EXPECT_EQ(asAnchors(L::pointsLeftButtonRect(W, H)), anchors("CC0_Points_Left_Button"));
    EXPECT_EQ(asAnchors(L::pointsLeftDisplayRect(W, H)), anchors("CC0_Points_Left_Display_Button"));
    for (int n = 0; n < L::kStatRows; ++n) {
        const std::string s = "CC0_Stat" + std::to_string(n);
        EXPECT_EQ(asAnchors(L::statButtonRect(W, H, n)), anchors(s + "_Button")) << s;
        EXPECT_EQ(asAnchors(L::statDisplayRect(W, H, n)), anchors(s + "_Display_Button")) << s;
        EXPECT_EQ(asAnchors(L::statUpRect(W, H, n)), anchors(s + "_Up_Button")) << s;
        EXPECT_EQ(asAnchors(L::statDownRect(W, H, n)), anchors(s + "_Down_Button")) << s;
    }
}

TEST(CharCreateLayout, PageOneControlsAreTheXmlsLocations) {
    if (!fs::exists(kXml)) GTEST_SKIP() << "no oracle install";
    const std::string xml = ReadXml();
    const auto a = L::screen1aRect(W, H);
    ExpectSame(Loc(xml, "CC1_NameLabelBorder"), Rel(L::nameLabelRect(W, H), a), "NameLabelBorder");
    ExpectSame(Loc(xml, "CC1_NameInput"), Rel(L::nameInputRect(W, H), a), "NameInput");
    ExpectSame(Loc(xml, "CC1_Get_Name_Button"), Rel(L::getNameButtonRect(W, H), a), "Get Name");
    const auto b = L::screen1bRect(W, H);
    ExpectSame(Loc(xml, "CC1_DeityLabelBorder"), Rel(L::deityTitleRect(W, H), b), "DeityLabelBorder");
    for (int n = 0; n < L::kDeitySlots; ++n) {
        const std::string item = "CC1_Deity" + std::to_string(n) + "_Button";
        ExpectSame(Loc(xml, item), Rel(L::deitySlotRect(W, H, n), b), item);
    }
    const auto c = L::screen1cRect(W, H);
    ExpectSame(Loc(xml, "CC1_CityLabelBorder"), Rel(L::cityTitleRect(W, H), c), "CityLabelBorder");
    for (int n = 0; n < L::kCitySlots; ++n) {
        const std::string item = "CC1_City" + std::to_string(n) + "_Button";
        ExpectSame(Loc(xml, item), Rel(L::citySlotRect(W, H, n), c), item);
    }
    const auto t = L::titleRect(W, H);
    ExpectSame(Loc(xml, "CC_Help_Button"), Rel(L::helpButtonRect(W, H), t), "CC_Help_Button");
    ExpectSame(Loc(xml, "CCT_TitleLabel"), Rel(L::titleLabelRect(W, H), t), "CCT_TitleLabel");
}

TEST(CharCreateLayout, PlacementRulesMatchTheOracleAndTheXml) {
    // Back/Next: right-anchored (CC_Next L69 R5 T8 B40) -- measured at
    // 1024x768 on cc_06_stats.png: Next 955..1019, Back 867..941.
    EXPECT_EQ(L::nextButtonRect(1024, 768), L::Rect(955, 8, 1019, 40));
    EXPECT_EQ(L::backButtonRect(1024, 768), L::Rect(867, 8, 941, 40));
    // The help panes: bottom-centre at the XML size reproduces
    // CharacterCreationScreen0c's own (200,300) at the design resolution.
    EXPECT_EQ(L::screen0cRect(W, H), L::Rect(200, 300, 440, 480));
    // 0a at the left, vertically centred: (0,0) at 640x480, y 144 at 768.
    EXPECT_EQ(L::screen0aRect(W, H).UpperLeftCorner, irr::core::vector2di(0, 0));
    EXPECT_EQ(L::screen0aRect(1024, 768).UpperLeftCorner.Y, 144);
    // 0b and 1c at the right edge.
    EXPECT_EQ(L::screen0bRect(1024, 768).UpperLeftCorner.X, 1024 - 168);
    EXPECT_EQ(L::screen1cRect(1024, 768).UpperLeftCorner.X, 1024 - 128);
}

// ===========================================================================
// The state machine
// ===========================================================================

TEST(CharCreateScreen, HiddenEntriesAreNeverInTheListsOrOpenedOn) {
    for (uint32_t seed = 0; seed < 300; ++seed) {
        CharCreateScreenState s;
        openCharCreate(s, Data(), seed);
        ASSERT_TRUE(s.isOpen);
        EXPECT_NE(s.race, Race::VahShir);
        EXPECT_NE(s.race, Race::Froglok);
        EXPECT_NE(s.cls, Class::Beastlord);
        EXPECT_NE(s.cls, Class::Berserker);
        EXPECT_TRUE(isValidRaceClass(s.race, s.cls)) << seed;
        EXPECT_EQ(s.alloc.pointsLeft(), bonusPoints(s.cls));   // cc_01: nothing spent
        EXPECT_EQ(s.helpEqstr, 3238u);                           // cc_01's help text
    }
    CharCreateScreenState s = OpenHumanWarrior();
    EXPECT_EQ(s.races.size(), 13u);
    EXPECT_EQ(s.classes.size(), 14u);
    EXPECT_FALSE(selectRace(s, Race::VahShir));
    EXPECT_FALSE(selectClass(s, Class::Beastlord));
    EXPECT_FALSE(selectCity(s, 13));   // SHARVAHL
}

TEST(CharCreateScreen, GreyedClassesAreInertAndARaceChangeKeepsAValidClass) {
    CharCreateScreenState s = OpenHumanWarrior();
    EXPECT_FALSE(selectClass(s, Class::Shaman));            // greyed for a Human (cc_06)
    EXPECT_EQ(s.cls, Class::Warrior);
    EXPECT_TRUE(selectClass(s, Class::Wizard));
    EXPECT_TRUE(selectRace(s, Race::Barbarian));            // Barbarians cannot be wizards
    EXPECT_TRUE(isValidRaceClass(s.race, s.cls));
    EXPECT_EQ(s.helpEqstr, 3239u);                          // the Barbarian description
}

TEST(CharCreateScreen, NextIsRefusedUntilEveryPointIsSpent) {
    CharCreateScreenState s = OpenHumanWarrior();
    CharCreateAction a = pressNext(s);
    EXPECT_EQ(a.kind, CharCreateAction::Kind::None);
    EXPECT_EQ(s.page, 0);
    EXPECT_EQ(s.messageEqstr, 1577u);
    dismissMessage(s);
    EXPECT_EQ(statMax(s, AGI), 25);                         // right-click AGILITY (cc_06)
    EXPECT_EQ(s.alloc.value(AGI), 105);
    EXPECT_EQ(s.helpEqstr, 3334u);                          // "Agility is one factor ..."
    pressNext(s);
    EXPECT_EQ(s.page, 1);
    EXPECT_EQ(s.deity, 396u);                               // AGNOSTIC, first offered
    EXPECT_EQ(s.city, 1u);                                  // QEYNOS, first offered
    EXPECT_EQ(s.helpEqstr, 4002u);                          // cc_08: the deity's text
}

TEST(CharCreateScreen, TheLiveOracleRunReplays) {
    // review/UIGAP-ORACLE/204: Human male Ranger; one "+" on STRENGTH (POINTS
    // LEFT 19, STR 81); right-click DEXTERITY (DEX 94, 0 left); Next ->
    // THE RAINKEEPER + QEYNOS pre-selected; pick THE MOTHER OF ALL.
    CharCreateScreenState s;
    ASSERT_TRUE(openCharCreateWith(s, Data(), Race::Human, Gender::Male, Class::Ranger));
    ASSERT_TRUE(statUp(s, STR));
    EXPECT_EQ(s.alloc.pointsLeft(), 19);
    EXPECT_EQ(s.alloc.value(STR), 81);
    EXPECT_EQ(statMax(s, DEX), 19);
    EXPECT_EQ(s.alloc.stats(), (Stats{81, 85, 85, 94, 80, 75, 75}));   // the DB row read back
    pressNext(s);
    EXPECT_EQ(s.deity, 207u);
    EXPECT_EQ(s.city, 1u);
    EXPECT_TRUE(selectDeity(s, 215));
    EXPECT_EQ(s.city, 1u);
    EXPECT_EQ(s.helpEqstr, 4016u);   // "TUNARE - THE MOTHER OF ALL"
}

TEST(CharCreateScreen, PageOneOffersOnlyWhatTheOracleOffers) {
    CharCreateScreenState s = OpenHumanWarrior();
    statMax(s, STR);
    pressNext(s);
    EXPECT_FALSE(selectDeity(s, 205));                      // THE KING OF THIEVES: greyed (cc_08)
    EXPECT_FALSE(selectCity(s, 2));                         // HALAS: greyed
    EXPECT_TRUE(selectCity(s, 4));                          // FREEPORT
    EXPECT_TRUE(selectDeity(s, 211));                       // THE WARLORD
    EXPECT_TRUE(s.cityOffered(s.city));
}

TEST(CharCreateScreen, SubmitChecksTheNameThenSendsATutorialFreeRequest) {
    CharCreateScreenState s = OpenHumanWarrior();
    statMax(s, STA);
    pressNext(s);
    s.nameEntry.inputText = "Xyz";
    EXPECT_EQ(pressNext(s).kind, CharCreateAction::Kind::None);
    EXPECT_EQ(s.messageEqstr, 1576u);                       // "Name is not valid."
    dismissMessage(s);
    s.nameEntry.inputText = "Cinewbie";
    const CharCreateAction a = pressNext(s);
    ASSERT_EQ(a.kind, CharCreateAction::Kind::Submit);
    EXPECT_EQ(a.request.name, "Cinewbie");
    EXPECT_EQ(a.request.startZone, 1u);
    EXPECT_TRUE(checkCharCreateStats(a.request.race, a.request.cls, a.request.stats));
    EXPECT_TRUE(s.submitting);
    EXPECT_EQ(s.messageEqstr, 1580u);
    // In flight: nothing moves.
    EXPECT_FALSE(selectCity(s, 4));
    EXPECT_EQ(pressBack(s).kind, CharCreateAction::Kind::None);
    EXPECT_EQ(hitTestCharCreate(W, H, s, 1, 1).hit, CharCreateHit::Blocked);
    applySubmitRefused(s, "The name Cinewbie has been rejected by the server. Please try a different one");
    EXPECT_FALSE(s.submitting);
    EXPECT_FALSE(s.messageText.empty());
}

TEST(CharCreateScreen, BackWalksThePagesThenLeaves) {
    CharCreateScreenState s = OpenHumanWarrior();
    statMax(s, STR);
    pressNext(s);
    EXPECT_EQ(pressBack(s).kind, CharCreateAction::Kind::None);
    EXPECT_EQ(s.page, 0);
    EXPECT_EQ(pressBack(s).kind, CharCreateAction::Kind::BackToCharacterSelect);
    EXPECT_FALSE(s.isOpen);
}

// ===========================================================================
// Clicks
// ===========================================================================

TEST(CharCreateClicks, ButtonsResolveAndGreyedOnesAreNotHitTestable) {
    CharCreateScreenState s = OpenHumanWarrior();
    auto centre = [](const L::Rect& r) { return r.getCenter(); };
    auto at = [&](const L::Rect& r) {
        return hitTestCharCreate(W, H, s, centre(r).X, centre(r).Y);
    };
    EXPECT_EQ(at(L::nextButtonRect(W, H)).hit, CharCreateHit::Next);
    EXPECT_EQ(at(L::backButtonRect(W, H)).hit, CharCreateHit::Back);
    EXPECT_EQ(at(L::faceButtonRect(W, H)).hit, CharCreateHit::FacePick);
    int shaman = -1;
    for (size_t i = 0; i < s.classes.size(); ++i) {
        if (s.classes[i].id == Class::Shaman) shaman = static_cast<int>(i);
    }
    ASSERT_GE(shaman, 0);
    EXPECT_EQ(at(L::classSlotRect(W, H, shaman)).hit, CharCreateHit::None);
    const auto race = at(L::raceSlotRect(W, H, 4));
    EXPECT_EQ(race.hit, CharCreateHit::Race);
    EXPECT_EQ(race.id, Race::Gnome);                       // Gnome takes Froglok's slot
    const auto stat = at(L::statButtonRect(W, H, DEX));
    ASSERT_EQ(stat.hit, CharCreateHit::StatName);
    applyCharCreateClick(s, stat, /*rightButton=*/true);
    EXPECT_EQ(s.alloc.pointsLeft(), 0);
    EXPECT_EQ(s.alloc.value(DEX), 100);
    applyCharCreateClick(s, at(L::pointsLeftButtonRect(W, H)), false);
    EXPECT_EQ(s.alloc.pointsLeft(), 25);
}

TEST(CharCreateClicks, PlainTextDropsTheMarkup) {
    EXPECT_EQ(charCreatePlainText("AGNOSTIC<BR><BR>Agnostics will <c \"#00A000\">not</c> commit."),
              "AGNOSTIC\n\nAgnostics will not commit.");
    EXPECT_EQ(charCreatePlainText("NPC\xe2\x80\x99s"), "NPC's");   // eqstr 3334's apostrophe
}

// B888-CC1 finding 6 (MR !168): the GREEN half of the genuine RED. The same
// test body compiles and RUNS on the pre-feature baseline (origin/fable,
// where CC0_Face_Button is greyed -- hitTestCharCreate returns
// CharCreateHit::None, "greyed: no FacePick yet") and there FAILs (the RED
// log, refactor/B888-CC1/finding6-RED-baseline-f8ead59d.log); here, at the
// feature head, Set Face resolves to the FacePick modal, so it passes. Only
// seam symbols present in BOTH trees are named (hitTestCharCreate,
// CharCreateHit::None, OpenHumanWarrior), so the body is byte-identical.
TEST(CharCreateClicks, SetFaceButtonIsNotGreyed) {
    CharCreateScreenState s = OpenHumanWarrior();
    const auto c = L::faceButtonRect(W, H).getCenter();
    const auto hit = hitTestCharCreate(W, H, s, c.X, c.Y);
    EXPECT_NE(hit.hit, CharCreateHit::None)
        << "Set Face is greyed: the FacePick modal the feature adds is absent";
}
