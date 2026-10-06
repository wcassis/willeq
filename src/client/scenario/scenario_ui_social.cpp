// UIGAP-live: the live legs of the trainer (#214, B898), friends / ignore
// (#213, B897) and tracking (#211, B895) units.
//
//   train_skill    at the Warrior guildmaster: /target, /train (the session),
//                  /train <id>. The client's skill value and training points
//                  must NOT move until OP_SkillUpdate arrives (it is the only
//                  reply a Titanium server sends), and must move by exactly
//                  one when it does. The DB side: train_skill.post.sh.
//   friends_who    /friends <peer> (eqstr 12984), then /friendswho: the
//                  12307 heading AND the peer's who line come back.
//   ignore_tell    control first -- the peer's first tell IS shown -- then
//                  /ignore <peer> (13243); the peer's second tell is NOT
//                  shown. That it was DELIVERED (the server sent it and this
//                  client dropped it) is ignore_tell.post.sh's grep of the
//                  client's MAIN:DEBUG "Ignored tell message from" line.
//   social_peer    the peer for both: two tells to the main character
//                  (--scenario-cast-target), 30 s apart, then it holds.
//   track_list     a Ranger (track_list_seed.sql): /useskill tracking -> the
//                  OP_Track reply's TrackingListChanged with the peer in it
//                  (and /tracklist names it), /track <peer> -> eqstr 12040 +
//                  direction lines on the ~2 s cadence (211 cadence.txt:
//                  mean 2.09 s), then the peer walks away and camps out ->
//                  eqstr 12102 "You have lost your target.".
//   track_peer     that peer: stays, walks off, camps.

#include "client/scenario/scenarios.h"
#include "client/state/event_types.h"
#include "scenario_steps.h"

#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

using steps::ChatHas;
using steps::Step;
using steps::StepScenario;

constexpr int kSkillValueChanged = static_cast<int>(eqt::state::GameEventType::SkillValueChanged);
constexpr int kTrainerOpened = static_cast<int>(eqt::state::GameEventType::TrainerWindowOpened);
constexpr int kTrackingList = static_cast<int>(eqt::state::GameEventType::TrackingListChanged);

std::string LastLines(IScenarioWorld& w, size_t from, size_t max = 6) {
    const size_t n = w.ChatLineCount();
    const size_t start = n > from + max ? n - max : from;
    std::string out;
    for (size_t i = start; i < n; ++i) {
        if (!out.empty()) out += " || ";
        out += w.ChatLine(i);
    }
    return out.empty() ? std::string("(no chat lines)") : out;
}

// ---------------------------------------------------------------------------
// train_skill
// ---------------------------------------------------------------------------

// Fixture (train_skill_seed.sql): Testchar (Warrior 20, 160 training points)
// beside Brin Stolunger, the Warrior guildmaster in South Qeynos. Skill 0
// (1H Blunt) is 1: free to train (cost is 0 below 11), well under the cap.
constexpr int kTrainSkill = 0;

struct TrainState {
    long long value = -1;
    long long points = -1;
    long long updates = -1;
    long long openedBase = 0;
};

std::unique_ptr<IScenario> MakeTrain() {
    auto st = std::make_shared<TrainState>();
    auto desc = [st](IScenarioWorld& w) {
        std::ostringstream os;
        os << "skill " << kTrainSkill << " = " << w.SkillValue(kTrainSkill) << " (was " << st->value
           << "), points = " << w.PracticePoints() << " (was " << st->points
           << "), SkillValueChanged events " << w.EventCount(kSkillValueChanged) << " (was "
           << st->updates << "), last update value " << w.LastSkillUpdateValue(kTrainSkill)
           << "; chat: " << LastLines(w, 0, 4);
        return os.str();
    };
    std::vector<steps::Step> s;
    s.push_back({"fixture: the skill and the points are readable",
                 [st](IScenarioWorld& w) { st->openedBase = w.EventCount(kTrainerOpened); },
                 [](IScenarioWorld& w) { return w.SkillValue(kTrainSkill) >= 0 && w.PracticePoints() > 0; },
                 nullptr, desc, 20.0, 0.0});
    s.push_back({"/target Brin, /train -> the trainer session opens",
                 [](IScenarioWorld& w) {
                     w.SendCommand("/target Brin");
                     w.SendCommand("/train");
                 },
                 [st](IScenarioWorld& w) { return w.EventCount(kTrainerOpened) > st->openedBase; },
                 nullptr, desc, 15.0, 2.0});
    s.push_back({"/train 0 -> nothing moves until OP_SkillUpdate, then +1 skill and -1 point",
                 [st](IScenarioWorld& w) {
                     st->value = w.SkillValue(kTrainSkill);
                     st->points = w.PracticePoints();
                     st->updates = w.EventCount(kSkillValueChanged);
                     w.SendCommand("/train " + std::to_string(kTrainSkill));
                 },
                 [st](IScenarioWorld& w) {
                     return w.EventCount(kSkillValueChanged) > st->updates &&
                            w.LastSkillUpdateValue(kTrainSkill) == st->value + 1 &&
                            w.SkillValue(kTrainSkill) == st->value + 1 &&
                            w.PracticePoints() == st->points - 1;
                 },
                 [st](IScenarioWorld& w) -> std::string {
                     // The rule under test: before the server's OP_SkillUpdate
                     // the client must not have written either number itself.
                     if (w.EventCount(kSkillValueChanged) > st->updates) return std::string();
                     if (w.SkillValue(kTrainSkill) != st->value) {
                         return "the skill value moved to " + std::to_string(w.SkillValue(kTrainSkill)) +
                                " BEFORE any OP_SkillUpdate (an optimistic local write)";
                     }
                     if (w.PracticePoints() != st->points) {
                         return "the training points moved to " + std::to_string(w.PracticePoints()) +
                                " BEFORE any OP_SkillUpdate";
                     }
                     return std::string();
                 },
                 desc, 15.0, 3.0});
    s.push_back({"/trainclose", [](IScenarioWorld& w) { w.SendCommand("/trainclose"); }, nullptr,
                 nullptr, desc, 1.0, 2.0});
    class Train final : public StepScenario {
    public:
        Train(std::vector<steps::Step> s, std::shared_ptr<TrainState> st)
            : StepScenario("train_skill", 20.0, std::move(s)), st_(std::move(st)) {}

    protected:
        std::string Summary(IScenarioWorld& w) override {
            std::ostringstream os;
            os << "MODEL skill" << kTrainSkill << "=" << w.SkillValue(kTrainSkill)
               << " points=" << w.PracticePoints() << " (before: " << st_->value << " / "
               << st_->points << ") -- both moved only on the server's OP_SkillUpdate";
            return os.str();
        }

    private:
        std::shared_ptr<TrainState> st_;
    };
    return std::make_unique<Train>(std::move(s), st);
}

// ---------------------------------------------------------------------------
// friends_who / ignore_tell / social_peer
// ---------------------------------------------------------------------------

const char* const kTell1 = "UIGAPLIVE-tell-1";
const char* const kTell2 = "UIGAPLIVE-tell-2";

struct ChatMark {
    size_t at = 0;
    double t = 0.0;
};

std::unique_ptr<IScenario> MakeFriendsWho(const std::string& peer) {
    auto mark = std::make_shared<ChatMark>();
    auto desc = [mark](IScenarioWorld& w) { return LastLines(w, mark->at); };
    std::vector<steps::Step> s;
    s.push_back({"the peer is online (in the zone)", nullptr,
                 [](IScenarioWorld& w) { return w.OtherPlayerCount() >= 1; }, nullptr, desc, 150.0, 0.0});
    s.push_back({"/friends <peer> -> '<peer> is now your friend.' (12984)",
                 [mark, peer](IScenarioWorld& w) {
                     mark->at = w.ChatLineCount();
                     w.SendCommand("/friends " + peer);
                 },
                 [mark, peer](IScenarioWorld& w) {
                     return ChatHas(w, mark->at, peer + " is now your friend");
                 },
                 nullptr, desc, 10.0, 1.0});
    s.push_back({"/friendswho -> the 12307 heading and the peer's who line",
                 [mark](IScenarioWorld& w) {
                     mark->at = w.ChatLineCount();
                     w.SendCommand("/friendswho");
                 },
                 [mark, peer](IScenarioWorld& w) {
                     return ChatHas(w, mark->at, "Friends currently on EverQuest") &&
                            ChatHas(w, mark->at, peer);
                 },
                 nullptr, desc, 15.0, 1.0});
    s.push_back({"/friends <peer> again -> removed (12982), the list is left as found",
                 [mark, peer](IScenarioWorld& w) {
                     mark->at = w.ChatLineCount();
                     w.SendCommand("/friends " + peer);
                 },
                 [mark, peer](IScenarioWorld& w) {
                     return ChatHas(w, mark->at, peer + " is no longer your friend");
                 },
                 nullptr, desc, 10.0, 0.0});
    return std::make_unique<StepScenario>("friends_who", 15.0, std::move(s));
}

std::unique_ptr<IScenario> MakeIgnoreTell(const std::string& peer) {
    auto mark = std::make_shared<ChatMark>();
    auto desc = [mark](IScenarioWorld& w) { return LastLines(w, mark->at); };
    std::vector<steps::Step> s;
    s.push_back({"control: the peer's FIRST tell is shown",
                 [mark](IScenarioWorld& w) { mark->at = w.ChatLineCount(); },
                 [](IScenarioWorld& w) { return ChatHas(w, 0, kTell1); }, nullptr, desc, 200.0, 0.0});
    s.push_back({"/ignore <peer> -> 'You have added <peer> to your IGNORE list.' (13243)",
                 [mark, peer](IScenarioWorld& w) {
                     mark->at = w.ChatLineCount();
                     mark->t = 0.0;
                     w.SendCommand("/ignore " + peer);
                 },
                 [mark, peer](IScenarioWorld& w) {
                     return ChatHas(w, mark->at, "added " + peer + " to your IGNORE list");
                 },
                 nullptr, desc, 10.0, 0.0});
    // The peer sends its second tell 30 s after its first; hold 45 s past the
    // ignore and fail the instant it shows.
    s.push_back({"the peer's SECOND tell is not shown (45 s window)",
                 [mark](IScenarioWorld& w) { mark->at = w.ChatLineCount(); },
                 [](IScenarioWorld&) { return true; },
                 [](IScenarioWorld& w) {
                     return ChatHas(w, 0, kTell2)
                                ? std::string("the ignored peer's tell was SHOWN: ") +
                                      LastLines(w, w.ChatLineCount() > 3 ? w.ChatLineCount() - 3 : 0)
                                : std::string();
                 },
                 desc, 1.0, 45.0});
    s.push_back({"/ignore <peer> again -> removed (13241), the list is left as found",
                 [mark, peer](IScenarioWorld& w) {
                     mark->at = w.ChatLineCount();
                     w.SendCommand("/ignore " + peer);
                 },
                 [mark, peer](IScenarioWorld& w) {
                     return ChatHas(w, mark->at, "removed " + peer + " from your IGNORE list");
                 },
                 nullptr, desc, 10.0, 0.0});
    // A dwell step's `fail` is not re-run during the dwell, so re-check here.
    s.push_back({"and the second tell never showed", nullptr,
                 [](IScenarioWorld& w) { return !ChatHas(w, 0, kTell2); }, nullptr, desc, 1.0, 0.0});
    return std::make_unique<StepScenario>("ignore_tell", 10.0, std::move(s));
}

std::unique_ptr<IScenario> MakeSocialPeer(const std::string& target) {
    std::vector<steps::Step> s;
    s.push_back({"first tell", [target](IScenarioWorld& w) {
                     w.SendCommand("/tell " + target + " " + kTell1);
                 },
                 nullptr, nullptr, nullptr, 1.0, 30.0});
    s.push_back({"second tell", [target](IScenarioWorld& w) {
                     w.SendCommand("/tell " + target + " " + kTell2);
                 },
                 nullptr, nullptr, nullptr, 1.0, 60.0});
    return std::make_unique<StepScenario>("social_peer", 20.0, std::move(s));
}

// ---------------------------------------------------------------------------
// track_list / track_peer
// ---------------------------------------------------------------------------

struct TrackState {
    size_t mark = 0;
    size_t scanned = 0;
    std::vector<double> directionTimes;
    long long listBase = 0;
};

class TrackList final : public StepScenario {
public:
    TrackList(std::vector<steps::Step> s, std::shared_ptr<TrackState> st)
        : StepScenario("track_list", 15.0, std::move(s)), st_(std::move(st)) {}

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        // Time-stamp every new direction line on the tick it appears (the
        // tick is 1/60 s; the cadence under test is ~2 s).
        const size_t n = w.ChatLineCount();
        for (; st_->scanned < n; ++st_->scanned) {
            const std::string line = w.ChatLine(st_->scanned);
            if (st_->mark > 0 && st_->scanned >= st_->mark && IsDirection(line)) {
                st_->directionTimes.push_back(tick.sinceStart);
            }
        }
        return StepScenario::Step(w, tick);
    }

    static bool IsDirection(const std::string& line) {
        return line.find(" is to the ") != std::string::npos ||
               line.find(" is straight ahead") != std::string::npos ||
               line.find(" is behind") != std::string::npos ||
               line.find(" is ahead") != std::string::npos;
    }

    static double MeanInterval(const std::vector<double>& t) {
        if (t.size() < 2) return 0.0;
        return (t.back() - t.front()) / static_cast<double>(t.size() - 1);
    }

protected:
    std::string Summary(IScenarioWorld&) override {
        std::ostringstream os;
        os << "TRACK direction lines=" << st_->directionTimes.size()
           << " mean interval=" << Fmt(MeanInterval(st_->directionTimes))
           << "s (oracle 2.09 s); list rows seen; lost-target line 12102 printed";
        return os.str();
    }

private:
    std::shared_ptr<TrackState> st_;
};

std::unique_ptr<IScenario> MakeTrackList(const std::string& peer) {
    auto st = std::make_shared<TrackState>();
    auto desc = [st](IScenarioWorld& w) {
        std::ostringstream os;
        os << "tracking rows " << w.TrackingRowCount() << ", direction lines "
           << st->directionTimes.size() << " (mean " << TrackList::MeanInterval(st->directionTimes)
           << " s), chat: " << LastLines(w, 0, 4);
        return os.str();
    };
    std::vector<steps::Step> s;
    s.push_back({"the peer is in the zone", nullptr,
                 [](IScenarioWorld& w) { return w.OtherPlayerCount() >= 1; }, nullptr, desc, 150.0, 2.0});
    s.push_back({"/useskill tracking -> the OP_Track reply's list (TrackingListChanged)",
                 [st](IScenarioWorld& w) {
                     st->listBase = w.EventCount(kTrackingList);
                     w.SendCommand("/useskill tracking");
                 },
                 [st](IScenarioWorld& w) {
                     return w.EventCount(kTrackingList) > st->listBase && w.TrackingRowCount() >= 1;
                 },
                 nullptr, desc, 15.0, 1.0});
    s.push_back({"/tracklist names the peer",
                 [st](IScenarioWorld& w) {
                     st->mark = w.ChatLineCount();
                     w.SendCommand("/tracklist");
                 },
                 [st, peer](IScenarioWorld& w) { return ChatHas(w, st->mark, peer + " - "); }, nullptr,
                 desc, 10.0, 1.0});
    s.push_back({"/track <peer> -> 'You begin tracking <peer>.' (12040)",
                 [st, peer](IScenarioWorld& w) {
                     st->mark = w.ChatLineCount();
                     st->directionTimes.clear();
                     w.SendCommand("/track " + peer);
                 },
                 [st, peer](IScenarioWorld& w) {
                     return ChatHas(w, st->mark, "You begin tracking " + peer);
                 },
                 nullptr, desc, 10.0, 0.0});
    s.push_back({"six direction lines on the ~2 s cadence", nullptr,
                 [st](IScenarioWorld&) { return st->directionTimes.size() >= 6; },
                 [st](IScenarioWorld&) -> std::string {
                     if (st->directionTimes.size() < 6) return std::string();
                     const double mean = TrackList::MeanInterval(st->directionTimes);
                     if (mean < 1.6 || mean > 2.6) {
                         return "direction-line cadence " + std::to_string(mean) +
                                " s is outside 1.6..2.6 (oracle mean 2.09 s)";
                     }
                     return std::string();
                 },
                 desc, 30.0, 0.0});
    s.push_back({"the peer walks off and camps -> 'You have lost your target.' (12102)", nullptr,
                 [st](IScenarioWorld& w) { return ChatHas(w, st->mark, "You have lost your target"); },
                 nullptr, desc, 240.0, 0.0});
    return std::make_unique<TrackList>(std::move(s), st);
}

class TrackPeer final : public StepScenario {
public:
    explicit TrackPeer(std::vector<steps::Step> s) : StepScenario("track_peer", 20.0, std::move(s)) {
        AllowZoneOut();  // camping out is this peer's last step
    }
};

std::unique_ptr<IScenario> MakeTrackPeer() {
    std::vector<steps::Step> s;
    s.push_back({"stand still while the tracker lists and tracks us", nullptr, nullptr, nullptr, nullptr,
                 1.0, 60.0});
    // Walk away (client frame, see /move's usage), then camp: the despawn is
    // what loses the tracker's target.
    s.push_back({"walk away", [](IScenarioWorld& w) { w.SendCommand("/move 400 -380 4"); }, nullptr,
                 nullptr, nullptr, 1.0, 15.0});
    s.push_back({"camp out", [](IScenarioWorld& w) { w.SendCommand("/camp"); }, nullptr, nullptr,
                 nullptr, 1.0, 0.0});
    s.push_back({"out of the zone", nullptr, [](IScenarioWorld& w) { return !w.IsFullyZonedIn(); },
                 nullptr, nullptr, 90.0, 0.0});
    return std::make_unique<TrackPeer>(std::move(s));
}

std::unique_ptr<IScenario> Refuse(const char* name, const char* why) {
    class Refusal final : public IScenario {
    public:
        Refusal(const char* n, const char* w) : name_(n), why_(w) {}
        const char* Name() const override { return name_; }
        StepResult Step(IScenarioWorld&, const ScenarioTick&) override {
            SetReason(why_);
            return StepResult::Fail;
        }

    private:
        const char* name_;
        const char* why_;
    };
    return std::make_unique<Refusal>(name, why);
}

}  // namespace

std::unique_ptr<IScenario> MakeTrainSkillScenario(const ScenarioOptions&) { return MakeTrain(); }

std::unique_ptr<IScenario> MakeFriendsWhoScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) return Refuse("friends_who", "--scenario-cast-target <peer> is required");
    return MakeFriendsWho(o.castTargetName);
}

std::unique_ptr<IScenario> MakeIgnoreTellScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) return Refuse("ignore_tell", "--scenario-cast-target <peer> is required");
    return MakeIgnoreTell(o.castTargetName);
}

std::unique_ptr<IScenario> MakeSocialPeerScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) return Refuse("social_peer", "--scenario-cast-target <main> is required");
    return MakeSocialPeer(o.castTargetName);
}

std::unique_ptr<IScenario> MakeTrackListScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) return Refuse("track_list", "--scenario-cast-target <peer> is required");
    return MakeTrackList(o.castTargetName);
}

std::unique_ptr<IScenario> MakeTrackPeerScenario(const ScenarioOptions&) { return MakeTrackPeer(); }

}  // namespace scenario
}  // namespace eqt
