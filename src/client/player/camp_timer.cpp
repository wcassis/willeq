// B1005 (GitLab #324): the /camp state machine. The rule and its oracle rows:
// include/client/player/camp_timer.h.

#include "client/player/camp_timer.h"

#include <algorithm>
#include <cctype>

namespace eqt {
namespace player {

CampMode ParseCampMode(const std::string& args) {
    std::string w;
    for (char c : args) {
        if (!std::isspace(static_cast<unsigned char>(c))) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (w == "desktop") return CampMode::Desktop;
    if (w == "server") return CampMode::Server;
    return CampMode::CharacterSelect;
}

CampEffects CampTimer::command(CampMode mode, double now, bool sitting) {
    CampEffects fx;
    if (phase_ == CampPhase::Counting) {
        // [13]: /camp again abandons -- the line, no packet, still sitting.
        phase_ = CampPhase::Idle;
        fx.lines.push_back({kEqstrCampAbandon, {}});
        return fx;
    }
    if (phase_ != CampPhase::Idle) return fx;   // OP_Logout is out: the server decides now
    phase_ = CampPhase::Counting;
    mode_ = mode;
    startedAt_ = now;
    nextStep_ = 0;
    fx.sit = !sitting;   // [1] vs [2]
    fx.sendCamp = true;
    fx.campBody = static_cast<uint32_t>(mode);
    fx.lines.push_back({kEqstrCampStart, {}});
    return fx;
}

CampEffects CampTimer::tick(double now) {
    CampEffects fx;
    if (phase_ != CampPhase::Counting) return fx;
    // A hair of slack so a schedule written in decimal seconds is met on the
    // tick that reaches it (10.0 + 4.2 - 10.0 < 4.2 in binary).
    const double elapsed = now - startedAt_ + 1e-6;
    constexpr size_t kSteps = sizeof(kCampCountdown) / sizeof(kCampCountdown[0]);
    while (nextStep_ < kSteps && elapsed >= kCampCountdown[nextStep_].at) {
        fx.lines.push_back({kEqstrCampRemaining, std::to_string(kCampCountdown[nextStep_].remaining)});
        ++nextStep_;
    }
    if (elapsed >= kCampSeconds) {
        phase_ = CampPhase::LogoutSent;
        fx.sendLogout = true;
    }
    return fx;
}

CampEffects CampTimer::stood() {
    CampEffects fx;
    if (phase_ == CampPhase::Counting) {
        phase_ = CampPhase::Idle;
        fx.lines.push_back({kEqstrCampAbandon, {}});
    }
    return fx;
}

CampPhase CampTimer::logoutReply(bool selectAvailable) {
    const bool camped = phase_ == CampPhase::Counting || phase_ == CampPhase::LogoutSent;
    if (camped && mode_ != CampMode::Desktop && selectAvailable) {
        phase_ = CampPhase::ToSelect;
    } else {
        phase_ = CampPhase::ToDesktop;
    }
    return phase_;
}

void CampTimer::reset() {
    phase_ = CampPhase::Idle;
    nextStep_ = 0;
}

}  // namespace player
}  // namespace eqt
