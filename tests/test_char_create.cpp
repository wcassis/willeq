// B888 (GitLab #204): native character creation -- the rules, the era data,
// the spec parser, the packet bytes and the world-stage flow. No network, no
// graphics, no eqemu: the flow runs through LoginWorldComponent's T05 replay
// seam and the T268 ordered-capture seam.
//
// Oracle sources, all read-only:
//   /mnt/projects/akk-stack/code/world/client.cpp  -- CheckCharCreateInfoTitanium,
//       HandleNameApprovalPacket, HandleCharacterCreatePacket, StartInTutorial
//   /mnt/projects/akk-stack/code/common/patches/titanium_structs.h  -- the wire
//   /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterCreate.xml
//   /mnt/projects/eq-oracle/logs/shots/cc_01_screen.png, cc_06_stats.png,
//       cc_08_deity_city.png, create_Oracleecho/04_deitypage.png

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/eq_opcodes.h"
#include "client/fatal_exit.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_packets.h"
#include "client/login_world/char_create_rules.h"
#include "client/login_world/world_script.h"
#include "common/net/packet.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace eqt::charcreate;
namespace fs = std::filesystem;

namespace {

const CharCreateData& Data() {
    static CharCreateData d = [] {
        CharCreateData x;
        EXPECT_TRUE(x.loadFile(std::string(WILLEQ_SOURCE_DIR) + "/data/char_create.tsv")) << x.error();
        return x;
    }();
    return d;
}

std::vector<std::string> Labels(const std::vector<EraEntry>& v) {
    std::vector<std::string> out;
    for (const auto& e : v) out.push_back(e.label);
    return out;
}

Stats S(int a, int b, int c, int d, int e, int f, int g) { return Stats{a, b, c, d, e, f, g}; }

}  // namespace

// ===========================================================================
// The server's tables, against the official screen
// ===========================================================================

TEST(CharCreateRules, BaseStatsMatchTheOfficialScreen) {
    // cc_01_screen.png: Iksar Beastlord, nothing spent, POINTS LEFT 20.
    EXPECT_EQ(baseStats(Race::Iksar, Class::Beastlord), S(70, 80, 95, 85, 90, 75, 60));
    EXPECT_EQ(bonusPoints(Class::Beastlord), 20);
    // cc_06_stats.png: Human Warrior with all 25 points in AGILITY (105).
    EXPECT_EQ(baseStats(Race::Human, Class::Warrior), S(85, 85, 80, 75, 75, 75, 75));
    EXPECT_EQ(bonusPoints(Class::Warrior), 25);
}

TEST(CharCreateRules, GreyedClassesMatchTheOfficialScreen) {
    // cc_06: a Human cannot be a Beastlord, Berserker or Shaman -- and can be
    // everything else.
    for (uint32_t c = 1; c <= 16; ++c) {
        const bool greyed = c == Class::Beastlord || c == Class::Berserker || c == Class::Shaman;
        EXPECT_EQ(isValidRaceClass(Race::Human, c), !greyed) << "class " << c;
    }
    // cc_01: an Iksar can be Warrior, Shadow Knight, Monk, Shaman, Necromancer,
    // Beastlord; every other button is grey.
    const std::set<uint32_t> iksar = {Class::Warrior, Class::ShadowKnight, Class::Monk, Class::Shaman,
                                      Class::Necromancer, Class::Beastlord};
    for (uint32_t c = 1; c <= 16; ++c) {
        EXPECT_EQ(isValidRaceClass(Race::Iksar, c), iksar.count(c) == 1) << "class " << c;
    }
    EXPECT_FALSE(isValidRaceClass(9999, Class::Warrior));
    EXPECT_FALSE(isValidRaceClass(Race::Human, 17));
}

TEST(CharCreateRules, PrimaryStatsAreTheGreenOnes) {
    // cc_01: Beastlord greens STA AGI WIS CHA; cc_06: Warrior greens STR STA AGI.
    std::vector<int> bst, war;
    for (int i = 0; i < kStatCount; ++i) {
        if (isPrimaryStat(Class::Beastlord, i)) bst.push_back(i);
        if (isPrimaryStat(Class::Warrior, i)) war.push_back(i);
    }
    EXPECT_EQ(bst, (std::vector<int>{STA, AGI, WIS, CHA}));
    EXPECT_EQ(war, (std::vector<int>{STR, STA, AGI}));
}

TEST(CharCreateRules, StatCheckRefusesExactlyWhatTheServerRefuses) {
    const uint32_t r = Race::Human, c = Class::Warrior;
    const Stats base = baseStats(r, c);
    Stats ok = base;
    ok[AGI] += 25;
    EXPECT_TRUE(checkCharCreateStats(r, c, ok));
    Stats under = base;
    under[AGI] += 24;                                  // one point unspent
    EXPECT_FALSE(checkCharCreateStats(r, c, under));
    Stats over = base;
    over[AGI] += 26;                                   // one point too many
    EXPECT_FALSE(checkCharCreateStats(r, c, over));
    Stats shifted = base;                              // the right total, CHA below base
    shifted[CHA] -= 1;
    shifted[STR] += 26;
    EXPECT_FALSE(checkCharCreateStats(r, c, shifted));
    EXPECT_FALSE(checkCharCreateStats(Race::Human, Class::Shaman, base));  // invalid pair
}

TEST(CharCreateRules, TheAllocatorCannotBuildARefusalAndAllSpentAlwaysPasses) {
    // A random walk of + / - / max / reset over every valid in-era pair: the
    // per-stat range rule holds at every step, and the server's whole check
    // passes exactly when the pool is empty.
    std::mt19937 rng(888);
    for (const auto& race : Data().visibleRaces()) {
        for (const auto& cls : Data().visibleClasses()) {
            if (!isValidRaceClass(race.id, cls.id)) continue;
            StatAllocator a(race.id, cls.id);
            for (int step = 0; step < 300; ++step) {
                const int stat = static_cast<int>(rng() % kStatCount);
                switch (rng() % 7) {
                case 0: case 1: case 2: a.increment(stat); break;
                case 3: case 4: a.decrement(stat); break;
                case 5: a.spendMax(stat); break;
                default: if (rng() % 10 == 0) a.reset(race.id, cls.id); break;
                }
                ASSERT_GE(a.pointsLeft(), 0);
                for (int i = 0; i < kStatCount; ++i) ASSERT_GE(a.value(i), a.base(i));
                ASSERT_EQ(checkCharCreateStats(race.id, cls.id, a.stats()), a.allSpent())
                    << race.label << " " << cls.label << " step " << step;
            }
            EXPECT_TRUE(checkCharCreateStats(race.id, cls.id, defaultAllocation(race.id, cls.id)))
                << race.label << " " << cls.label;
        }
    }
}

TEST(CharCreateRules, NameRulesMirrorHandleNameApprovalPacket) {
    EXPECT_EQ(validateCharacterName("Cinewbie"), NameCheck::kOk);
    EXPECT_EQ(validateCharacterName("Abcd"), NameCheck::kOk);              // 4: the minimum
    EXPECT_EQ(validateCharacterName("Abcdefghijklmno"), NameCheck::kOk);   // 15: the maximum
    EXPECT_EQ(validateCharacterName("Xyz"), NameCheck::kTooShort);         // the issue's reject case
    EXPECT_EQ(validateCharacterName("Abcdefghijklmnop"), NameCheck::kTooLong);
    EXPECT_EQ(validateCharacterName("abcde"), NameCheck::kLeadingLower);
    EXPECT_EQ(validateCharacterName("AbCde"), NameCheck::kInnerUpper);
    EXPECT_EQ(validateCharacterName("Ab cde"), NameCheck::kSpace);
    EXPECT_EQ(validateCharacterName("Ab1de"), NameCheck::kNonLetter);
}

// ===========================================================================
// The era data: hidden, not greyed
// ===========================================================================

TEST(CharCreateData, PostVeliousEntriesAreInTheOracleButNeverOffered) {
    const auto& d = Data();
    const std::vector<std::string> hidden = {"Vah Shir", "Froglok", "Beastlord", "Berserker",
                                             "SHARVAHL", "GUKTA"};
    std::vector<std::string> all, visible;
    for (const auto* v : {&d.allRaces(), &d.allClasses(), &d.allCities()})
        for (const auto& e : *v) all.push_back(e.label);
    for (const auto& v : {d.visibleRaces(), d.visibleClasses(), d.visibleCities()})
        for (const auto& e : v) visible.push_back(e.label);
    for (const auto& h : hidden) {
        EXPECT_NE(std::find(all.begin(), all.end(), h), all.end()) << h << " missing from the data";
        EXPECT_EQ(std::find(visible.begin(), visible.end(), h), visible.end()) << h << " is offered";
    }
    EXPECT_EQ(d.visibleRaces().size(), 13u);    // the issue: 15 - Vah Shir - Froglok
    EXPECT_EQ(d.visibleClasses().size(), 14u);  // 16 - Beastlord - Berserker
    EXPECT_EQ(d.visibleDeities().size(), 17u);  // all in era
    EXPECT_EQ(d.visibleCities().size(), 13u);   // 15 - SHARVAHL - GUKTA
    EXPECT_EQ(d.raceByName("Vah Shir"), nullptr);
    EXPECT_EQ(d.classByName("Beastlord"), nullptr);
    EXPECT_EQ(d.cityByName("GUKTA"), nullptr);
}

TEST(CharCreateData, ButtonOrderIsTheScreensWithTheHiddenOnesRemoved) {
    // The oracle's columns, top to bottom (cc_01_screen.png), minus the hidden.
    EXPECT_EQ(Labels(Data().visibleRaces()),
              (std::vector<std::string>{"Barbarian", "Dark Elf", "Dwarf", "Erudite", "Gnome", "Half Elf",
                                        "Halfling", "High Elf", "Human", "Iksar", "Ogre", "Troll",
                                        "Wood Elf"}));
    EXPECT_EQ(Labels(Data().visibleClasses()),
              (std::vector<std::string>{"Bard", "Cleric", "Druid", "Enchanter", "Magician", "Monk",
                                        "Necromancer", "Paladin", "Ranger", "Rogue", "ShadowKnight",
                                        "Shaman", "Warrior", "Wizard"}));
    EXPECT_EQ(Data().visibleDeities().front().label, "AGNOSTIC");
    EXPECT_EQ(Data().visibleDeities().back().label, "THE WARLORD");
    EXPECT_EQ(Data().visibleCities().back().label, "CABILIS");
}

TEST(CharCreateData, LabelsAreTheXmlTextVerbatim) {
    const fs::path xmlPath = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterCreate.xml";
    if (!fs::exists(xmlPath)) GTEST_SKIP() << "no oracle install";
    std::ifstream in(xmlPath);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string xml = ss.str();
    auto textOf = [&xml](const std::string& item) {
        const size_t at = xml.find("\"" + item + "\"");
        if (at == std::string::npos) return std::string("<missing>");
        const size_t t = xml.find("<Text>", at);
        return xml.substr(t + 6, xml.find("</Text>", t) - t - 6);
    };
    for (const auto& e : Data().allRaces())
        EXPECT_EQ(textOf("CC0_Race" + std::to_string(e.xmlIndex) + "_Button"), e.label);
    for (const auto& e : Data().allClasses())
        EXPECT_EQ(textOf("CC0_Class" + std::to_string(e.xmlIndex) + "_Button"), e.label);
    for (const auto& e : Data().allDeities())
        EXPECT_EQ(textOf("CC1_Deity" + std::to_string(e.xmlIndex) + "_Button"), e.label);
    for (const auto& e : Data().allCities())
        EXPECT_EQ(textOf("CC1_City" + std::to_string(e.xmlIndex) + "_Button"), e.label);
}

TEST(CharCreateData, MeasuredOffersMatchTheOracleFrames) {
    const auto& d = Data();
    // cc_08_deity_city.png: Human Warrior.
    std::vector<std::string> deities;
    for (uint32_t id : d.allowedDeities(Race::Human, Class::Warrior)) deities.push_back(d.deityById(id)->label);
    EXPECT_EQ(deities, (std::vector<std::string>{"AGNOSTIC", "THE PLAGUEBRINGER", "THE PRINCE OF HATE",
                                                 "THE RAINKEEPER", "THE QUEEN OF LOVE", "THE LIGHTBEARER",
                                                 "THE PRIME HEALER", "THE WARLORD"}));
    EXPECT_EQ(d.allowedCities(Race::Human, Class::Warrior, 396), (std::vector<uint32_t>{1, 4}));
    EXPECT_NE(d.citySource(Race::Human, Class::Warrior, 396).find("oracle:"), std::string::npos);
    // create_Oracleecho/04_deitypage.png: Iksar Warrior -> THE FACELESS, CABILIS only.
    EXPECT_EQ(d.allowedDeities(Race::Iksar, Class::Warrior), (std::vector<uint32_t>{203}));
    EXPECT_EQ(d.allowedCities(Race::Iksar, Class::Warrior, 203), (std::vector<uint32_t>{12}));
    // review/UIGAP-ORACLE/204/07 + 08: Human Ranger -> Karana or Tunare, QEYNOS only;
    // the screen pre-selects THE RAINKEEPER, the first one offered.
    EXPECT_EQ(d.allowedDeities(Race::Human, Class::Ranger), (std::vector<uint32_t>{207, 215}));
    EXPECT_EQ(d.allowedCities(Race::Human, Class::Ranger, 207), (std::vector<uint32_t>{1}));
    EXPECT_EQ(d.allowedCities(Race::Human, Class::Ranger, 215), (std::vector<uint32_t>{1}));
}

TEST(CharCreateData, EveryOfferedCombinationCanBeCompleted) {
    // The screen must never strand a player: every valid in-era race/class
    // has a deity, and every offered deity has a city.
    const auto& d = Data();
    for (const auto& r : d.visibleRaces()) {
        for (const auto& c : d.visibleClasses()) {
            if (!isValidRaceClass(r.id, c.id)) continue;
            const auto deities = d.allowedDeities(r.id, c.id);
            ASSERT_FALSE(deities.empty()) << r.label << " " << c.label;
            for (uint32_t deity : deities)
                EXPECT_FALSE(d.allowedCities(r.id, c.id, deity).empty())
                    << r.label << " " << c.label << " " << deity;
        }
    }
}

TEST(CharCreateData, HomeZonesAreInEraZones) {
    std::ifstream in(std::string(WILLEQ_SOURCE_DIR) + "/data/era_zones.tsv");
    ASSERT_TRUE(in);
    std::set<std::string> era;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string exp, id, name;
        if (std::getline(ss, exp, '\t') && std::getline(ss, id, '\t') && std::getline(ss, name, '\t'))
            era.insert(name);
    }
    for (const auto& c : Data().visibleCities()) {
        ASSERT_FALSE(c.homeZones.empty()) << c.label;
        for (const auto& z : c.homeZones) EXPECT_EQ(era.count(z), 1u) << c.label << " -> " << z;
    }
}

// ===========================================================================
// --create-character
// ===========================================================================

TEST(CharCreateSpec, ParsesTheIssuesFormAndDefaultsTheStats) {
    CharCreateRequest r;
    std::string err;
    ASSERT_TRUE(ParseCreateSpec("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS", Data(), r, err)) << err;
    EXPECT_EQ(r.name, "Cinewbie");
    EXPECT_EQ(r.race, Race::Human);
    EXPECT_EQ(r.gender, Gender::Male);
    EXPECT_EQ(r.cls, Class::Warrior);
    EXPECT_EQ(r.deity, 396u);
    EXPECT_EQ(r.startZone, 1u);
    EXPECT_TRUE(checkCharCreateStats(r.race, r.cls, r.stats));

    ASSERT_TRUE(ParseCreateSpec("Ciscale,iksar,female,warrior,faceless,cabilis", Data(), r, err)) << err;
    EXPECT_EQ(r.race, Race::Iksar);
    EXPECT_EQ(r.gender, Gender::Female);
    EXPECT_EQ(r.deity, 203u);
    EXPECT_EQ(r.startZone, 12u);

    ASSERT_TRUE(ParseCreateSpec("Cistats,Human,M,Warrior,AGNOSTIC,FREEPORT,AGI=105", Data(), r, err)) << err;
    EXPECT_EQ(r.stats, S(85, 85, 105, 75, 75, 75, 75));   // cc_06's allocation
}

TEST(CharCreateSpec, RefusesWhatTheScreenWouldNotOffer) {
    CharCreateRequest r;
    std::string err;
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Vah Shir,Male,Warrior,AGNOSTIC,QEYNOS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Froglok,Male,Warrior,AGNOSTIC,QEYNOS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Beastlord,AGNOSTIC,QEYNOS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Shaman,AGNOSTIC,QEYNOS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Iksar,Male,Warrior,AGNOSTIC,CABILIS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Warrior,AGNOSTIC,HALAS", Data(), r, err));
    EXPECT_NE(err.find("1579"), std::string::npos) << err;
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Warrior,AGNOSTIC,SHARVAHL", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Xyz,Human,Male,Warrior,AGNOSTIC,QEYNOS", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS,AGI=100", Data(), r, err));
    EXPECT_NE(err.find("You must spend all of your ability points"), std::string::npos) << err;
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS,CHA=74,AGI=106", Data(), r, err));
    EXPECT_FALSE(ParseCreateSpec("Cinewbie,Human,Male,Warrior", Data(), r, err));
}

// ===========================================================================
// The packet bytes
// ===========================================================================

namespace {
uint32_t U32(const EQ::Net::Packet& p, size_t off) { return p.GetUInt32(off); }
std::string Str(const EQ::Net::Packet& p, size_t off) {
    return std::string(reinterpret_cast<const char*>(p.Data()) + off);
}
}  // namespace

TEST(CharCreatePackets, ApproveNameIsTitaniumsNameApproval) {
    const auto p = BuildApproveName("Cinewbie", Race::Iksar, Class::Monk, 203);
    ASSERT_EQ(p.Length(), 2u + 76u);
    EXPECT_EQ(p.GetUInt16(0), HC_OP_ApproveName);
    EXPECT_EQ(Str(p, 2), "Cinewbie");
    for (size_t i = 2 + 8; i < 2 + 64; ++i) EXPECT_EQ(p.GetUInt8(i), 0u) << i;  // NUL-padded
    // NameApproval_Struct as world reads it: race_id @64, class_id @68.
    EXPECT_EQ(U32(p, 2 + 64), 128u);
    EXPECT_EQ(U32(p, 2 + 68), 7u);
    EXPECT_EQ(U32(p, 2 + 72), 203u);   // Titanium's deity
}

TEST(CharCreatePackets, CharacterCreateIsTheEightyByteTitaniumStruct) {
    CharCreateRequest r;
    r.name = "Cinewbie";
    r.cls = 1; r.haircolor = 2; r.beardcolor = 3; r.beard = 4; r.gender = 1; r.race = 128;
    r.startZone = 12; r.hairstyle = 5; r.deity = 203;
    r.stats = S(80, 81, 82, 83, 84, 85, 86);
    r.face = 6; r.eyecolor1 = 7; r.eyecolor2 = 8;
    const auto p = BuildCharacterCreate(r);
    ASSERT_EQ(p.Length(), 2u + 80u);   // DECODE_LENGTH_EXACT
    EXPECT_EQ(p.GetUInt16(0), HC_OP_CharacterCreate);
    const uint32_t expect[20] = {1, 2, 3, 4, 1, 128, 12, 5, 203, 80, 81, 82, 83, 84, 85, 86, 6, 7, 8, 0};
    for (size_t i = 0; i < 20; ++i) EXPECT_EQ(U32(p, 2 + 4 * i), expect[i]) << "field at +" << 4 * i;
}

TEST(CharCreatePackets, TutorialIsAlwaysZero) {
    CharCreateRequest r;
    r.race = 1; r.cls = 1;
    EXPECT_EQ(U32(BuildCharacterCreate(r), 2 + 76), 0u);
}

TEST(CharCreatePackets, RandomNameAndDeleteAndTheReplies) {
    const auto g = BuildRandomName(Race::Human, Gender::Female);
    ASSERT_EQ(g.Length(), 2u + 72u);
    EXPECT_EQ(g.GetUInt16(0), HC_OP_RandomNameGenerator);
    EXPECT_EQ(U32(g, 2), 1u);
    EXPECT_EQ(U32(g, 6), 1u);

    const auto d = BuildDeleteCharacter("Cinewbie");
    ASSERT_EQ(d.Length(), 2u + 64u);
    EXPECT_EQ(d.GetUInt16(0), HC_OP_DeleteCharacter);
    EXPECT_EQ(Str(d, 2), "Cinewbie");

    const uint8_t yes = 1, no = 0;
    EXPECT_EQ(ParseApproveNameReply(&yes, 1), 1);
    EXPECT_EQ(ParseApproveNameReply(&no, 1), 0);
    EXPECT_EQ(ParseApproveNameReply(nullptr, 0), -1);

    std::vector<uint8_t> echo(72, 0);
    std::memcpy(echo.data() + 8, "Ozanas", 6);
    EXPECT_EQ(ParseRandomNameReply(echo.data(), echo.size()), "Ozanas");
    EXPECT_EQ(ParseRandomNameReply(echo.data(), 40), "");
}

// ===========================================================================
// The world-stage flow
// ===========================================================================

namespace {

// A Titanium CharacterSelect_Struct body (titanium_structs.h:199-226): names
// at 1024 + 64*i, "<none>" in empty slots, as eqemu fills them.
std::vector<uint8_t> CharInfo(const std::vector<std::string>& names) {
    std::vector<uint8_t> body(1704, 0);
    for (int i = 0; i < 10; ++i) {
        const std::string n = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : "<none>";
        std::memcpy(body.data() + 1024 + 64 * i, n.data(), n.size());
        body[1694 + i] = 1;
    }
    return body;
}

struct Flow {
    Flow() : eq("127.0.0.1", 0, "test", "test", "test", "Testchar") {
        eqt::ResetFatalExitForTest();
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
    }
    ~Flow() { eqt::ResetFatalExitForTest(); }
    void World(uint16_t op, const std::vector<uint8_t>& body) {
        eq.LoginWorld().ReplayWorldPacket(op, body.data(), body.size());
    }
    std::vector<uint16_t> Ops() const {
        std::vector<uint16_t> v;
        for (const auto& p : eq.Test_GetQueuedPackets()) v.push_back(p.opcode);
        return v;
    }
    const std::vector<uint8_t>* Last(uint16_t op) const {
        const std::vector<uint8_t>* out = nullptr;
        for (const auto& p : eq.Test_GetQueuedPackets())
            if (p.opcode == op) out = &p.bytes;
        return out;
    }
    void Clear() { eq.Test_ClearQueuedPackets(); }
    EverQuest eq;
};

WorldScript CreateScript(const std::string& spec) {
    WorldScript s;
    std::string err;
    EXPECT_TRUE(ParseCreateSpec(spec, Data(), s.create, err)) << err;
    s.kind = WorldScript::Kind::Create;
    s.name = s.create.name;
    return s;
}

const std::vector<uint8_t> kExpansion = {0xff, 0, 0, 0};

}  // namespace

TEST(CharCreateFlow, WithoutAScriptTheHandshakeIsUnchanged) {
    Flow f;
    f.World(HC_OP_ExpansionInfo, kExpansion);
    EXPECT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_AckPacket, HC_OP_WorldClientReady, HC_OP_EnterWorld}));
    f.Clear();
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    EXPECT_TRUE(f.Ops().empty());
}

TEST(CharCreateFlow, CreateApprovesCreatesThenEntersWorldOutOfTheTutorial) {
    Flow f;
    f.eq.LoginWorld().SetWorldScript(CreateScript("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS"));
    f.World(HC_OP_ExpansionInfo, kExpansion);
    // No OP_EnterWorld for a name the account does not hold yet.
    EXPECT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_AckPacket, HC_OP_WorldClientReady}));
    f.Clear();

    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    ASSERT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_ApproveName}));
    EXPECT_STREQ(reinterpret_cast<const char*>(f.Last(HC_OP_ApproveName)->data()), "Cinewbie");
    f.Clear();

    f.World(HC_OP_ApproveName, {1});
    ASSERT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_CharacterCreate}));
    const auto& cc = *f.Last(HC_OP_CharacterCreate);
    ASSERT_EQ(cc.size(), 80u);
    uint32_t tutorial = 0, zone = 0;
    std::memcpy(&tutorial, cc.data() + 76, 4);
    std::memcpy(&zone, cc.data() + 24, 4);
    EXPECT_EQ(tutorial, 0u);
    EXPECT_EQ(zone, 1u);  // QEYNOS
    f.Clear();

    // World lists the new character: the CRC pair (clears StartInTutorial),
    // THEN OP_EnterWorld with the new name.
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar", "Cinewbie"}));
    EXPECT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_World_Client_CRC1, HC_OP_World_Client_CRC2,
                                              HC_OP_EnterWorld}));
    EXPECT_STREQ(reinterpret_cast<const char*>(f.Last(HC_OP_EnterWorld)->data()), "Cinewbie");
    EXPECT_EQ(f.eq.LoginWorld().GetCharCreateStatus().last,
              eqt::login_world::LoginWorldComponent::CharCreateStatus::Result::Created);
    EXPECT_FALSE(f.eq.LoginWorld().GetWorldScript().active());
    EXPECT_FALSE(eqt::FatalExitRecorded());
    EXPECT_FALSE(f.eq.IsQuitRequested());
}

TEST(CharCreateFlow, ARefusedNameSendsNoCreateAndFailsTheRun) {
    Flow f;
    f.eq.LoginWorld().SetWorldScript(CreateScript("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS"));
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.World(HC_OP_SendCharInfo, CharInfo({}));
    f.Clear();
    f.World(HC_OP_ApproveName, {0});
    EXPECT_TRUE(f.Ops().empty());
    EXPECT_EQ(f.eq.LoginWorld().GetCharCreateStatus().last,
              eqt::login_world::LoginWorldComponent::CharCreateStatus::Result::NameRefused);
    EXPECT_TRUE(eqt::FatalExitRecorded());
    EXPECT_TRUE(f.eq.IsQuitRequested());
}

TEST(CharCreateFlow, ARefusedCreateIsReportedNotEntered) {
    Flow f;
    f.eq.LoginWorld().SetWorldScript(CreateScript("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS"));
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.World(HC_OP_SendCharInfo, CharInfo({}));
    f.World(HC_OP_ApproveName, {1});
    f.Clear();
    f.World(HC_OP_ApproveName, {0});   // world's OPCharCreate failure (:760-765)
    EXPECT_TRUE(f.Ops().empty());
    EXPECT_EQ(f.eq.LoginWorld().GetCharCreateStatus().last,
              eqt::login_world::LoginWorldComponent::CharCreateStatus::Result::CreateRefused);
    EXPECT_TRUE(eqt::FatalExitRecorded());
}

TEST(CharCreateFlow, AnExistingNameIsNotRecreated) {
    Flow f;
    f.eq.LoginWorld().SetWorldScript(CreateScript("Cinewbie,Human,Male,Warrior,AGNOSTIC,QEYNOS"));
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.Clear();
    f.World(HC_OP_SendCharInfo, CharInfo({"Cinewbie"}));
    EXPECT_TRUE(f.Ops().empty());
    EXPECT_TRUE(eqt::FatalExitRecorded());
}

TEST(CharCreateFlow, DeleteSendsTheNameAndQuitsWhenItIsGone) {
    Flow f;
    WorldScript s;
    s.kind = WorldScript::Kind::Delete;
    s.name = "Cinewbie";
    f.eq.LoginWorld().SetWorldScript(s);
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.Clear();
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar", "Cinewbie"}));
    ASSERT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_DeleteCharacter}));
    EXPECT_STREQ(reinterpret_cast<const char*>(f.Last(HC_OP_DeleteCharacter)->data()), "Cinewbie");
    EXPECT_FALSE(f.eq.IsQuitRequested());
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    EXPECT_TRUE(f.eq.IsQuitRequested());
    EXPECT_FALSE(eqt::FatalExitRecorded());
}

TEST(CharCreateFlow, DeletingANameTheAccountDoesNotHoldFails) {
    Flow f;
    WorldScript s;
    s.kind = WorldScript::Kind::Delete;
    s.name = "Nobody";
    f.eq.LoginWorld().SetWorldScript(s);
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.Clear();
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    EXPECT_TRUE(f.Ops().empty());
    EXPECT_TRUE(eqt::FatalExitRecorded());
}

TEST(CharCreateFlow, ListQuitsWithoutEnteringWorld) {
    Flow f;
    WorldScript s;
    s.kind = WorldScript::Kind::List;
    f.eq.LoginWorld().SetWorldScript(s);
    f.World(HC_OP_ExpansionInfo, kExpansion);
    f.World(HC_OP_SendCharInfo, CharInfo({"Testchar", "Cinewbie"}));
    for (uint16_t op : f.Ops()) EXPECT_NE(op, HC_OP_EnterWorld);
    EXPECT_TRUE(f.eq.IsQuitRequested());
    EXPECT_FALSE(eqt::FatalExitRecorded());
}

TEST(CharCreateFlow, GetNameRoundTrip) {
    Flow f;
    ASSERT_TRUE(f.eq.LoginWorld().RequestRandomName(Race::Human, Gender::Male));
    ASSERT_EQ(f.Ops(), (std::vector<uint16_t>{HC_OP_RandomNameGenerator}));
    std::vector<uint8_t> echo(72, 0);
    std::memcpy(echo.data() + 8, "Ozanas", 6);
    f.World(HC_OP_RandomNameGenerator, echo);
    EXPECT_EQ(f.eq.LoginWorld().GetCharCreateStatus().randomName, "Ozanas");
}

TEST(CharCreateFlow, AnInvalidNameIsRefusedBeforeSending) {
    Flow f;
    CharCreateRequest r;
    r.name = "Xyz";
    r.race = Race::Human;
    r.cls = Class::Warrior;
    r.stats = defaultAllocation(r.race, r.cls);
    EXPECT_FALSE(f.eq.LoginWorld().RequestCreateCharacter(r));
    EXPECT_TRUE(f.Ops().empty());
}
