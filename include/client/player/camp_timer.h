#pragma once

// B1005 (GitLab #324): the /camp state machine -- pure logic, no client, no
// clock of its own (the caller passes seconds), so tests drive it directly.
// PlayerComponent (src/client/player/player_camp.cpp) owns one and turns its
// effects into the sit, the packets and the chat lines.
//
// THE MEASURED RULE (official Titanium client, local lane, 2026-10-07;
// review/UIGAP-ORACLE/324/camp.tsv, row numbers in brackets):
//   * /camp sits the character if it is standing (OP_SpawnAppearance 14/110)
//     and sends OP_Camp in the same instant [1][2]; OP_Camp's body is a u32
//     MODE: 0 /camp, 1 /camp desktop, 2 /camp server [3].
//   * eqstr 12293 at once [4]; eqstr 12684 with 25/20/15/10/5 at +4.2, +9.2,
//     +14.2, +19.2, +24.2 s [5]; OP_Logout (empty) at +30 s [6]; no line at
//     the end [6].
//   * /stand cancels: eqstr 12290, the stand appearance is the only packet
//     [12]. /camp again cancels: 12290, NO packet, still sitting [13].
//   * The server's OP_LogoutReply ends the zone session [7]. /camp then goes
//     back to world as a fresh login and shows Character Select [8-10];
//     /camp desktop exits [14]; /camp server goes to the LOGIN server [15]
//     (fable has no server-select screen: it lands on Character Select --
//     flagged deviation). A GM's camp is answered at once [16].
// NOT MEASURED: movement and damage during the countdown (camp.tsv NM1).
// fable cancels on the STAND a move causes (rule [12]) -- an assumption.

#include <cstdint>
#include <string>
#include <vector>

namespace eqt {
namespace player {

/** OP_Camp's body (camp.tsv [3]). */
enum class CampMode : uint32_t { CharacterSelect = 0, Desktop = 1, Server = 2 };

/** Where the camp is. The values are IScenarioWorld::kCamp* (scenario.h). */
enum class CampPhase : uint8_t {
    Idle = 0,
    Counting = 1,     // /camp typed, the countdown runs
    LogoutSent = 2,   // +30 s: OP_Logout out, waiting for the server
    ToSelect = 3,     // the server answered: back to Character Select
    ToDesktop = 4,    // the server answered: the client exits
};

constexpr uint32_t kEqstrCampStart = 12293;      // "It will take you about 30 seconds to prepare your camp."
constexpr uint32_t kEqstrCampRemaining = 12684;  // "It will take about %1 more seconds to prepare your camp."
constexpr uint32_t kEqstrCampAbandon = 12290;    // "You abandon your preparations to camp."

/** OP_Logout goes out this long after OP_Camp (camp.tsv [6]: 29.98-30.26 s). */
constexpr double kCampSeconds = 30.0;

/** The 12684 lines: when (seconds after OP_Camp) and the %1 they carry. */
struct CampCountdownStep {
    double at;
    int remaining;
};
constexpr CampCountdownStep kCampCountdown[] = {
    {4.2, 25}, {9.2, 20}, {14.2, 15}, {19.2, 10}, {24.2, 5},
};

/** One chat line: an eqstr id and its %1 (empty = no argument). */
struct CampLine {
    uint32_t eqstr = 0;
    std::string arg;
    bool operator==(const CampLine& o) const { return eqstr == o.eqstr && arg == o.arg; }
};

/** What the caller must do, in this order: sit, OP_Camp, lines, OP_Logout. */
struct CampEffects {
    bool sit = false;
    bool sendCamp = false;
    uint32_t campBody = 0;   // OP_Camp's u32
    std::vector<CampLine> lines;
    bool sendLogout = false;
};

/** "/camp <args>": "" -> CharacterSelect, "desktop" -> Desktop, "server" ->
 *  Server (case-insensitive, surrounding blanks ignored). Anything else is a
 *  plain /camp (unmeasured: the official client was not given other words). */
CampMode ParseCampMode(const std::string& args);

class CampTimer {
public:
    /** The /camp command at `now`. While counting, a second /camp CANCELS
     *  (12290, nothing sent, stays sitting -- [13]); after OP_Logout it is
     *  ignored. `sitting` = the character is already sitting. */
    CampEffects command(CampMode mode, double now, bool sitting);
    /** Advance to `now`: the due 12684 lines, then OP_Logout at +30 s. */
    CampEffects tick(double now);
    /** The character stood up while counting: 12290 ([12]). Otherwise nothing. */
    CampEffects stood();
    /**
     * The server's OP_LogoutReply (not a zone-out acknowledgement). Returns
     * where the client goes: ToSelect for a /camp or /camp server camp when
     * `selectAvailable`, else ToDesktop -- including a reply that answers no
     * camp at all (today's quit, unchanged).
     */
    CampPhase logoutReply(bool selectAvailable);
    /** Back in a zone (or a fresh session): Idle. */
    void reset();

    CampPhase phase() const { return phase_; }
    CampMode mode() const { return mode_; }
    double startedAt() const { return startedAt_; }
    bool counting() const { return phase_ == CampPhase::Counting; }

private:
    CampPhase phase_ = CampPhase::Idle;
    CampMode mode_ = CampMode::CharacterSelect;
    double startedAt_ = 0.0;
    size_t nextStep_ = 0;
};

}  // namespace player
}  // namespace eqt
