#pragma once

// T224: OP_WhoAllResponse entry parsing + per-line formatting.
//
// Before T224 ZoneProcessWhoAllResponse read only the 64-byte header and
// printed the player COUNT; the entries that follow the header were never
// parsed, so /who showed nothing useful. This TU is the pure half: it walks
// the packed variable-length entries and renders each one through the EQ
// string table exactly the way eqemu expects a Titanium client to.
//
// Wire layout (eqemu common/eq_packet_structs.h, WhoAllPlayer /
// WhoAllPlayerPart1..4 under #pragma pack(1); the senders are
// zone/entity.cpp EntityList::ZoneWho and world/clientlist.cpp
// ClientList::SendWhoAll / SendFriendsWho, all of which size the packet as
// 64 + 47*entries + strlen(name) + strlen(guild)):
//
//   u32 formatstring   5025 normal / 5024 anon / 5023 roleplay
//   u32 pidstring      0xFFFFFFFF, or 5003 "(USER %1: PID %2)" for GM viewers
//                      (world SendWhoAll; args = account, unknown100)
//   cstr name
//   u32 rankstring     zone: 12312 "* GM * " / 12315 " TRADER" / 6056 "BUYER" /
//                      0xFFFFFFFF none; world: 5007..5021 GM rank / 0 none
//   cstr guild         "<Name>" or ""
//   u32 unknown80[2]   [0]=0xFFFFFFFF (zone) or the NUMERIC admin level for GM
//                      viewers (world) -- never a string id; [1]=12313
//                      " <LINKDEAD>" (zone) or 0xFFFFFFFF
//   u32 zonestring     5006 "ZONE: %1" (world /who all) or 0xFFFFFFFF (zone /who)
//   u32 zone           zone id (0 when zonestring is 0xFFFFFFFF)
//   u32 class
//   u32 level
//   u32 race
//   cstr account       always "" from eqemu
//   u32 unknown100     0 (zone) / 207 (world)
//
// 47 = the 11 u32s + three NUL terminators. Strings are unbounded on the
// wire, so parsing is bounds-checked byte by byte.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace EQT {

constexpr uint32_t kWhoStringNone = 0xFFFFFFFFu;

// eqstr ids the who response references. Values are eqemu's (entity.cpp /
// clientlist.cpp) and match /mnt/projects/EverQuestP1999/eqstr_us.txt.
constexpr uint32_t kWhoStrPlayersInEq      = 5001;   // "Players in EverQuest:"
constexpr uint32_t kWhoStrZone             = 5006;   // "ZONE: %1"
constexpr uint32_t kWhoStrFormatRoleplay   = 5023;   // "%T1[ANONYMOUS] %2 %3 %4"
constexpr uint32_t kWhoStrFormatAnon       = 5024;   // "%T1[ANONYMOUS] %2 %3"
constexpr uint32_t kWhoStrFormatNormal     = 5025;   // "%T1[%2 %3] %4 (%5) %6 %7 %8 %9"
constexpr uint32_t kWhoStrFormatAnonGmView = 5022;   // "%T1[ANON (%2 %3)] %4 (%5) %6 %7 %8"
constexpr uint32_t kWhoStrUserPid          = 5003;   // "(USER %1: PID %2)"
constexpr uint32_t kWhoStrCutShort         = 5033;   // "Your who request was cut short..too many players."
constexpr uint32_t kWhoStrOnePlayer        = 5028;   // "There is %1 player in EverQuest."
constexpr uint32_t kWhoStrNoPlayers        = 5029;
constexpr uint32_t kWhoStrManyPlayers      = 5036;   // "There are %1 players in EverQuest."
constexpr uint32_t kWhoStrBuyer            = 6056;   // "BUYER"
constexpr uint32_t kWhoStrGm               = 12312;  // "* GM * "
constexpr uint32_t kWhoStrLinkdead         = 12313;  // " <LINKDEAD>"
constexpr uint32_t kWhoStrTrader           = 12315;  // " TRADER"

struct WhoAllEntry {
    uint32_t formatString = kWhoStringNone;
    uint32_t pidString = kWhoStringNone;
    std::string name;
    uint32_t rankString = kWhoStringNone;
    std::string guild;
    uint32_t unknown80[2] = {kWhoStringNone, kWhoStringNone};
    uint32_t zoneString = kWhoStringNone;
    uint32_t zone = 0;
    uint32_t classId = 0;
    uint32_t level = 0;
    uint32_t race = 0;
    std::string account;
    uint32_t unknown100 = 0;

    bool isLinkdead() const { return unknown80[1] == kWhoStrLinkdead; }
};

// Parses up to `count` entries from `data[0..len)` (the bytes AFTER the
// 64-byte WhoAllReturnHeader). Appends to `out`. Returns false if the buffer
// ran out before `count` entries were read (whatever parsed completely is
// still in `out`); true when all `count` were read. Trailing bytes after the
// last entry are ignored.
bool parseWhoAllEntries(const uint8_t* data, size_t len, uint32_t count,
                        std::vector<WhoAllEntry>& out);

// String-table hooks so the formatter has no StringDatabase dependency and
// is testable standalone.
struct WhoFormatContext {
    // eqstr id -> template ("" if unknown). The caller normally wraps
    // StringDatabase::getString and falls back to whoDefaultString().
    std::function<std::string(uint32_t)> eqstr;
    std::function<std::string(uint32_t)> className;   // class id -> "Warrior"
    std::function<std::string(uint32_t)> raceName;    // race id -> "Human"
    std::function<std::string(uint32_t)> zoneName;    // zone id -> "qeynos2" ("" if unknown)
};

// Built-in copies of the who strings from eqstr_us.txt, used when the string
// database has no entry (headless tests, unloaded db). "" for other ids.
std::string whoDefaultString(uint32_t id);

// Renders one entry as the official client would, e.g.
//   "[50 Warrior] Bob (Human) <Guild> ZONE: qeynos2"
//   "* GM * [50 Warrior] Bob (Human)  <LINKDEAD>"
//   "[ANONYMOUS] Bob <Guild>"
// Argument positions per format id (inferred from the templates' shapes and
// eqemu's field comments; there is no client source to cite):
//   5025: %T1 rank, %2 level, %3 class, %4 name, %5 race, %6 guild,
//         %7 zone ("ZONE: x" via 5006), %8 linkdead,
//         %9 "(USER account: PID unknown100)" via pidstring 5003 (GM viewers)
//   5022 (anon as seen by a GM): same first eight
//   5024: %T1 rank, %2 name, %3 guild
//   5023: %T1 rank, %2 name, %3 guild, %4 zone
// Runs of blanks left by empty arguments are collapsed and the line trimmed.
std::string formatWhoAllEntry(const WhoAllEntry& e, const WhoFormatContext& ctx);

// Substitutes %Tn (string-id arg, looked up through ctx.eqstr and recursively
// formatted) and %n placeholders in `tmpl` from 1-based `args`. Missing
// args render as "". Exposed for tests.
std::string formatWhoTemplate(const std::string& tmpl,
                              const std::vector<std::string>& args,
                              const WhoFormatContext& ctx);

}  // namespace EQT
