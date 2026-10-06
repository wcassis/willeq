#pragma once

// B888 (GitLab #204): native character creation -- the RULES half.
//
// Everything here is the SERVER's, quoted, because the server is
// authoritative and a Titanium client has no OP_CharacterCreateRequest (that
// opcode is SoF+): the client must carry the same tables the server validates
// against, or it offers combinations the server refuses.
//
//   * baseStats() / bonusPoints() / isValidRaceClass() quote
//     /mnt/projects/akk-stack/code/world/client.cpp CheckCharCreateInfoTitanium()
//     (:2002-2160): BaseRace[16][7], BaseClass[16][8] and
//     ClassRaceLookupTable[16][16], verbatim, with its race-index mapping
//     (Iksar 128 -> 12, Vah Shir 130 -> 13, Froglok 330 -> 14).
//   * checkCharCreateStats() is the same function's stat check: the sum must
//     equal base + points EXACTLY and each stat must lie in
//     [base, base + points]. It returns the server's verdict for any input,
//     so a test can prove the client's allocator never builds a refusal.
//   * validateCharacterName() is HandleNameApprovalPacket() (:567-634) minus
//     the two checks only the server can run (CheckNameFilter and
//     ReserveName): 4-15 characters, a leading capital, no other capital, no
//     space. It is a PRE-flight; the server still decides.
//
// The race/class/deity/city offers the screen DRAWS are data, not code:
// data/char_create.tsv (char_create_data.h), the era filter Will ruled.
//
// The official client's own check, measured on its screens
// (/mnt/projects/eq-oracle/logs/shots/cc_01_screen.png, cc_06_stats.png):
// the same base stats (Iksar Beastlord 70/80/95/85/90/75/60 + 20 points;
// Human Warrior 85/85/80/75/75/75/75 + 25), the same greyed classes (Human:
// Beastlord, Berserker, Shaman), and the class-bonus stats drawn GREEN.

#include <array>
#include <cstdint>
#include <string>

namespace eqt {
namespace charcreate {

// eqemu ids (common/races.h, common/classes.h, common/deity.h). These are the
// values the wire carries.
namespace Race {
constexpr uint32_t Human = 1, Barbarian = 2, Erudite = 3, WoodElf = 4, HighElf = 5,
                   DarkElf = 6, HalfElf = 7, Dwarf = 8, Troll = 9, Ogre = 10,
                   Halfling = 11, Gnome = 12, Iksar = 128, VahShir = 130, Froglok = 330;
}
namespace Class {
constexpr uint32_t Warrior = 1, Cleric = 2, Paladin = 3, Ranger = 4, ShadowKnight = 5,
                   Druid = 6, Monk = 7, Bard = 8, Rogue = 9, Shaman = 10,
                   Necromancer = 11, Wizard = 12, Magician = 13, Enchanter = 14,
                   Beastlord = 15, Berserker = 16;
}
namespace Gender {
constexpr uint32_t Male = 0, Female = 1;
}

/** STR, STA, AGI, DEX, WIS, INT, CHA -- CharCreate_Struct's field order. */
constexpr int kStatCount = 7;
using Stats = std::array<int, kStatCount>;
enum StatIndex : int { STR = 0, STA, AGI, DEX, WIS, INT, CHA };
/** The labels on CC0_Stat0..6_Button, EQUI_CharacterCreate.xml. */
const char* statLabel(int stat);

/** The server's race-table row for a wire race id, or -1 (out of its table). */
int serverRaceIndex(uint32_t race);
/** ClassRaceLookupTable[class-1][raceIndex]. False for anything off the table. */
bool isValidRaceClass(uint32_t race, uint32_t cls);
/** BaseClass[c] + BaseRace[r]. Zeros when the pair is off the table. */
Stats baseStats(uint32_t race, uint32_t cls);
/** BaseClass[c][7], the points to spend. 0 when off the table. */
int bonusPoints(uint32_t cls);
/** BaseClass[c][stat] != 0 -- the stats the official client draws GREEN. */
bool isPrimaryStat(uint32_t cls, int stat);

/** CheckCharCreateInfoTitanium()'s verdict on a full stat line. */
bool checkCharCreateStats(uint32_t race, uint32_t cls, const Stats& stats);

/**
 * The point pool behind CC0_Stat0..6 and POINTS LEFT. Starts at base with
 * every point unspent. A stat can never drop below its base and the pool can
 * never go negative, so every reachable state passes the server's per-stat
 * range check; only "all points spent" remains, which is eqstr 1577.
 */
class StatAllocator {
public:
    StatAllocator() = default;
    StatAllocator(uint32_t race, uint32_t cls) { reset(race, cls); }

    /** Back to base, every point unspent (CC0_Points_Left_Button's tooltip). */
    void reset(uint32_t race, uint32_t cls);
    /** CC0_StatN_Up_Button: +1 if a point is left. */
    bool increment(int stat);
    /** CC0_StatN_Down_Button: -1 if above base. */
    bool decrement(int stat);
    /** Right-click on CC0_StatN_Button: every remaining point into `stat`. */
    int spendMax(int stat);
    /** Set one stat directly (the scripted path). False if it would break a rule. */
    bool set(int stat, int value);

    int pointsLeft() const { return pointsLeft_; }
    int value(int stat) const { return stats_[static_cast<size_t>(stat)]; }
    int base(int stat) const { return base_[static_cast<size_t>(stat)]; }
    const Stats& stats() const { return stats_; }
    bool allSpent() const { return pointsLeft_ == 0; }

private:
    Stats base_{};
    Stats stats_{};
    int pointsLeft_ = 0;
};

/**
 * fable's default allocation when a scripted create names no stats: every
 * point dealt round-robin over the class's primary (green) stats, STR first.
 * The server's own DefaultPointAllocation lives in its database table
 * char_create_point_allocations, which a Titanium client is never sent; this
 * is a deterministic, server-valid stand-in, NOT the oracle's default
 * (assumption awaiting oracle: the official screen starts with every point
 * unspent, cc_01_screen.png, so it has no default to copy).
 */
Stats defaultAllocation(uint32_t race, uint32_t cls);

/** Why a name is refused before it is sent. kOk = send it. */
enum class NameCheck : uint8_t { kOk, kTooShort, kTooLong, kLeadingLower, kInnerUpper,
                                 kSpace, kNonLetter };
NameCheck validateCharacterName(const std::string& name);
/** One line for the log / the chat line. */
const char* nameCheckText(NameCheck check);

}  // namespace charcreate
}  // namespace eqt
