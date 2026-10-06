// B888 (GitLab #204) -- the character-creation rules, quoted from the server.
// See include/client/login_world/char_create_rules.h for the citations.

#include "client/login_world/char_create_rules.h"

#include <cctype>

namespace eqt {
namespace charcreate {

namespace {

constexpr int kTableRaces = 16;    // _TABLE_RACES, world/client.cpp:2011
constexpr int kTableClasses = 16;  // Class::PLAYER_CLASS_COUNT

// world/client.cpp:2013-2031, verbatim. STR STA AGI DEX WIS INT CHA.
constexpr int kBaseRace[kTableRaces][7] = {
    /*Human*/     { 75,  75,  75,  75,  75,  75,  75},
    /*Barbarian*/ {103,  95,  82,  70,  70,  60,  55},
    /*Erudite*/   { 60,  70,  70,  70,  83, 107,  70},
    /*Wood Elf*/  { 65,  65,  95,  80,  80,  75,  75},
    /*High Elf*/  { 55,  65,  85,  70,  95,  92,  80},
    /*Dark Elf*/  { 60,  65,  90,  75,  83,  99,  60},
    /*Half Elf*/  { 70,  70,  90,  85,  60,  75,  75},
    /*Dwarf*/     { 90,  90,  70,  90,  83,  60,  45},
    /*Troll*/     {108, 109,  83,  75,  60,  52,  40},
    /*Ogre*/      {130, 122,  70,  70,  67,  60,  37},
    /*Halfling*/  { 70,  75,  95,  90,  80,  67,  50},
    /*Gnome*/     { 60,  70,  85,  85,  67,  98,  60},
    /*Iksar*/     { 70,  70,  90,  85,  80,  75,  55},
    /*Vah Shir*/  { 90,  75,  90,  70,  70,  65,  65},
    /*Froglok*/   { 70,  80, 100, 100,  75,  75,  50},
    /*Drakkin*/   { 70,  80,  85,  75,  80,  85,  75},
};

// world/client.cpp:2033-2051, verbatim. STR STA AGI DEX WIS INT CHA ADD.
constexpr int kBaseClass[kTableClasses][8] = {
    /*Warrior*/      {10, 10,  5,  0,  0,  0,  0, 25},
    /*Cleric*/       { 5,  5,  0,  0, 10,  0,  0, 30},
    /*Paladin*/      {10,  5,  0,  0,  5,  0, 10, 20},
    /*Ranger*/       { 5, 10, 10,  0,  5,  0,  0, 20},
    /*ShadowKnight*/ {10,  5,  0,  0,  0, 10,  5, 20},
    /*Druid*/        { 0, 10,  0,  0, 10,  0,  0, 30},
    /*Monk*/         { 5,  5, 10, 10,  0,  0,  0, 20},
    /*Bard*/         { 5,  0,  0, 10,  0,  0, 10, 25},
    /*Rogue*/        { 0,  0, 10, 10,  0,  0,  0, 30},
    /*Shaman*/       { 0,  5,  0,  0, 10,  0,  5, 30},
    /*Necromancer*/  { 0,  0,  0, 10,  0, 10,  0, 30},
    /*Wizard*/       { 0, 10,  0,  0,  0, 10,  0, 30},
    /*Magician*/     { 0, 10,  0,  0,  0, 10,  0, 30},
    /*Enchanter*/    { 0,  0,  0,  0,  0, 10, 10, 30},
    /*Beastlord*/    { 0, 10,  5,  0, 10,  0,  5, 20},
    /*Berserker*/    {10,  5,  0, 10,  0,  0,  0, 25},
};

// world/client.cpp:2053-2071, verbatim (true = 1).
// Human Barb Erud WElf HElf DElf HfElf Dwarf Troll Ogre Hfling Gnome Iksar VahShir Frog Drakkin
constexpr bool kClassRace[kTableClasses][kTableRaces] = {
    /*Warrior*/      {1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /*Cleric*/       {1, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    /*Paladin*/      {1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    /*Ranger*/       {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    /*ShadowKnight*/ {1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1},
    /*Druid*/        {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    /*Monk*/         {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    /*Bard*/         {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1},
    /*Rogue*/        {1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    /*Shaman*/       {0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    /*Necromancer*/  {1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1},
    /*Wizard*/       {1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1},
    /*Magician*/     {1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    /*Enchanter*/    {1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    /*Beastlord*/    {0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0},
    /*Berserker*/    {0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0},
};

int classIndex(uint32_t cls) {
    return (cls >= 1 && cls <= static_cast<uint32_t>(kTableClasses)) ? static_cast<int>(cls) - 1 : -1;
}

}  // namespace

const char* statLabel(int stat) {
    static const char* const kLabels[kStatCount] = {"STRENGTH", "STAMINA", "AGILITY", "DEXTERITY",
                                                    "WISDOM", "INTELLIGENCE", "CHARISMA"};
    return (stat >= 0 && stat < kStatCount) ? kLabels[stat] : "";
}

int serverRaceIndex(uint32_t race) {
    // world/client.cpp:2079-2084: race - 1, then the four non-sequential ids.
    switch (race) {
    case Race::Froglok: return 14;
    case Race::VahShir: return 13;
    case Race::Iksar: return 12;
    case 522: return 15;  // Race::Drakkin
    default: break;
    }
    if (race >= 1 && race <= 12) return static_cast<int>(race) - 1;
    return -1;
}

bool isValidRaceClass(uint32_t race, uint32_t cls) {
    const int r = serverRaceIndex(race);
    const int c = classIndex(cls);
    return r >= 0 && c >= 0 && kClassRace[c][r];
}

Stats baseStats(uint32_t race, uint32_t cls) {
    Stats s{};
    const int r = serverRaceIndex(race);
    const int c = classIndex(cls);
    if (r < 0 || c < 0) return s;
    for (int i = 0; i < kStatCount; ++i) s[static_cast<size_t>(i)] = kBaseClass[c][i] + kBaseRace[r][i];
    return s;
}

int bonusPoints(uint32_t cls) {
    const int c = classIndex(cls);
    return c < 0 ? 0 : kBaseClass[c][7];
}

bool isPrimaryStat(uint32_t cls, int stat) {
    const int c = classIndex(cls);
    return c >= 0 && stat >= 0 && stat < kStatCount && kBaseClass[c][stat] != 0;
}

bool checkCharCreateStats(uint32_t race, uint32_t cls, const Stats& stats) {
    if (!isValidRaceClass(race, cls)) return false;  // returns before the stat check
    const Stats base = baseStats(race, cls);
    const int points = bonusPoints(cls);
    int baseTotal = 0, total = 0;
    bool ok = true;
    for (int i = 0; i < kStatCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        baseTotal += base[k];
        total += stats[k];
        if (stats[k] > base[k] + points || stats[k] < base[k]) ok = false;
    }
    if (baseTotal + points != total) ok = false;
    return ok;
}

void StatAllocator::reset(uint32_t race, uint32_t cls) {
    base_ = baseStats(race, cls);
    stats_ = base_;
    pointsLeft_ = isValidRaceClass(race, cls) ? bonusPoints(cls) : 0;
}

bool StatAllocator::increment(int stat) {
    if (stat < 0 || stat >= kStatCount || pointsLeft_ <= 0) return false;
    ++stats_[static_cast<size_t>(stat)];
    --pointsLeft_;
    return true;
}

bool StatAllocator::decrement(int stat) {
    if (stat < 0 || stat >= kStatCount) return false;
    const size_t k = static_cast<size_t>(stat);
    if (stats_[k] <= base_[k]) return false;
    --stats_[k];
    ++pointsLeft_;
    return true;
}

int StatAllocator::spendMax(int stat) {
    if (stat < 0 || stat >= kStatCount) return 0;
    const int spent = pointsLeft_;
    stats_[static_cast<size_t>(stat)] += spent;
    pointsLeft_ = 0;
    return spent;
}

bool StatAllocator::set(int stat, int value) {
    if (stat < 0 || stat >= kStatCount) return false;
    const size_t k = static_cast<size_t>(stat);
    const int delta = value - stats_[k];
    if (value < base_[k] || delta > pointsLeft_) return false;
    stats_[k] = value;
    pointsLeft_ -= delta;
    return true;
}

Stats defaultAllocation(uint32_t race, uint32_t cls) {
    StatAllocator a(race, cls);
    int primaries[kStatCount];
    int n = 0;
    for (int i = 0; i < kStatCount; ++i) {
        if (isPrimaryStat(cls, i)) primaries[n++] = i;
    }
    if (n == 0) {
        a.spendMax(STR);
        return a.stats();
    }
    for (int i = 0; a.pointsLeft() > 0; i = (i + 1) % n) a.increment(primaries[i]);
    return a.stats();
}

NameCheck validateCharacterName(const std::string& name) {
    if (name.size() < 4) return NameCheck::kTooShort;   // EQ::ValueWithin(length, 4, 15)
    if (name.size() > 15) return NameCheck::kTooLong;
    if (name.find(' ') != std::string::npos) return NameCheck::kSpace;
    for (unsigned char c : name) {
        if (!std::isalpha(c)) return NameCheck::kNonLetter;
    }
    if (std::islower(static_cast<unsigned char>(name[0]))) return NameCheck::kLeadingLower;
    for (size_t i = 1; i < name.size(); ++i) {
        if (std::isupper(static_cast<unsigned char>(name[i]))) return NameCheck::kInnerUpper;
    }
    return NameCheck::kOk;
}

const char* nameCheckText(NameCheck check) {
    switch (check) {
    case NameCheck::kOk: return "ok";
    case NameCheck::kTooShort: return "a name must be at least 4 letters";
    case NameCheck::kTooLong: return "a name must be at most 15 letters";
    case NameCheck::kLeadingLower: return "a name must begin with a capital letter";
    case NameCheck::kInnerUpper: return "only the first letter of a name may be a capital";
    case NameCheck::kSpace: return "a name may not contain a space";
    case NameCheck::kNonLetter: return "a name may contain letters only";
    }
    return "?";
}

}  // namespace charcreate
}  // namespace eqt
