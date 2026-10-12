// B760 (issue #45): the repeated cast-tail lane.
//
// B630's chain casts each outcome ONCE, so it can only say "this run's tail was
// contiguous". B760's question is a rate and a latency: across many casts, does
// any zone packet reach the client between `ManaChange keepcasting 0` and the
// cast's terminal packet, how long does the client take to settle the outcome,
// and does unrelated traffic from a second client change either? So this lane
// casts the same two outcomes again and again in one zone-in:
//
//   * SUCCESS: Blast of Cold (successSpellId, 1.5 s, recast 2.25 s) at the
//     peer, let finish. eqemu's tail is SendSpellBarEnable (ManaChange
//     keepcasting 0), [OP_LinkedReuse], OP_MemorizeSpell scribing 3
//     (zone/spells.cpp:1817-1824), queued contiguously on the reliable stream.
//   * INTERRUPT: castSpellId (378, 3 s) and the GM `#interrupt` 1.5 s in:
//     OP_InterruptCast then ManaChange keepcasting 0 (spells.cpp:1306-1314).
//
// Like the B630 legs this is a pure DRIVER: every claim is read off the DEBUG
// log (the `[ZonePacket] <op> rel=<0|1>` line in DispatchZonePacket and the
// SpellManager outcome lines) by scripts/scenario_configs/b760_tail_analyze.py.
//
// The peer is either cast_tail_quiet_peer (zones in and stands still: the
// quiet condition) or cast_tail_traffic_peer (sits/stands, moves, talks and
// so makes the server broadcast reliable AND unreliable packets to the caster
// throughout every cast: the traffic condition). scripts/scenario.sh picks the
// peer from the caster's scenario name and seeds both characters co-located
// (b630_seed.sql).

#include "client/scenario/scenarios.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

std::string Fmt(const double value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

class RefuseScenario final : public IScenario {
public:
    RefuseScenario(const char* name, std::string reason)
        : name_(name), reason_(std::move(reason)) {}
    const char* Name() const override { return name_; }
    StepResult Step(IScenarioWorld&, const ScenarioTick&) override {
        SetReason(reason_);
        return StepResult::Fail;
    }

private:
    const char* name_;
    std::string reason_;
};

struct TimedCommand {
    double at;
    std::string command;
};

class CastTailRepeatScenario final : public IScenario {
public:
    CastTailRepeatScenario(const char* name, uint32_t successSpell,
                           uint32_t interruptSpell, std::string target)
        : name_(name),
          successSpell_(successSpell),
          interruptSpell_(interruptSpell),
          target_(std::move(target)),
          // eqemu's GM #unmemspells / #memspell apply to c->GetTarget()
          // (B630 item 1), so the prep runs with nothing targeted.
          prep_({
              {1.0, "/cleartarget"},
              {3.0, "/say #unmemspells"},
              {6.0, "/say #memspell " + std::to_string(successSpell) + " 0"},
              {9.0, "/say #memspell " + std::to_string(interruptSpell) + " 1"},
          }) {}

    const char* Name() const override { return name_; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (prepIndex_ < prep_.size()) {
            if (t >= prep_[prepIndex_].at) {
                world.Note("prep: " + prep_[prepIndex_].command);
                world.SendCommand(prep_[prepIndex_].command);
                ++prepIndex_;
            }
            return StepResult::Running;
        }

        if (!targeted_) {
            if (t < kTargetAt) {
                return StepResult::Running;
            }
            if (world.OtherPlayerCount() >= 1) {
                targeted_ = true;
                world.Note("targeted " + target_ + " at t=" + Fmt(t));
                world.SendCommand("/target " + target_);
                nextCycleAt_ = t + kFirstCastAfterTarget;
                return StepResult::Running;
            }
            if (t >= kPeerDeadline) {
                SetReason("peer '" + target_ + "' never appeared within " +
                          Fmt(kPeerDeadline) + "s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }

        if (pendingInterruptAt_ > 0.0 && t >= pendingInterruptAt_) {
            pendingInterruptAt_ = 0.0;
            world.Note("cycle " + std::to_string(cycle_ - 1) +
                       ": fired GM #interrupt at t=" + Fmt(t));
            world.SendCommand("/say #interrupt");
            return StepResult::Running;
        }

        if (cycle_ < kCycles && t >= nextCycleAt_) {
            const bool interrupt = (cycle_ % 2) == 1;
            world.Note("cycle " + std::to_string(cycle_) + ": " +
                       (interrupt ? "INTERRUPT /cast 2 (spell " +
                                        std::to_string(interruptSpell_) + ")"
                                  : "SUCCESS /cast 1 (spell " +
                                        std::to_string(successSpell_) + ")") +
                       " at t=" + Fmt(t));
            world.SendCommand(interrupt ? "/cast 2" : "/cast 1");
            if (interrupt) {
                pendingInterruptAt_ = t + kInterruptAfter;
            }
            ++cycle_;
            nextCycleAt_ = t + kCyclePeriod;
            return StepResult::Running;
        }

        if (cycle_ >= kCycles && t >= nextCycleAt_) {
            world.Note("repeat lane complete: " + std::to_string(kCycles) +
                       " cycles. Assert from the log with b760_tail_analyze.py.");
            SetReason("cast-tail repeat lane executed (" + std::to_string(kCycles) +
                      " cycles); assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    // The prep ends at 9 s; target from 11 s (and wait for the peer).
    static constexpr double kTargetAt = 11.0;
    static constexpr double kPeerDeadline = 75.0;
    static constexpr double kFirstCastAfterTarget = 3.0;
    // 12 successes + 12 interrupts. 9 s covers the 3 s cast, the 2.25 s
    // recovery and the 2.25 s / 6 s recasts of the two gems.
    static constexpr int kCycles = 24;
    static constexpr double kCyclePeriod = 9.0;
    static constexpr double kInterruptAfter = 1.5;

    const char* name_;
    uint32_t successSpell_;
    uint32_t interruptSpell_;
    std::string target_;
    std::vector<TimedCommand> prep_;
    size_t prepIndex_ = 0;
    bool targeted_ = false;
    int cycle_ = 0;
    double nextCycleAt_ = 0.0;
    double pendingInterruptAt_ = 0.0;
};

// The peer. `traffic` false: zone in and stand still until the caster is
// done (the quiet condition). `traffic` true: every kStep seconds one of
// /sit, /stand, a short /move step and back, or a /say -- the server
// broadcasts OP_SpawnAppearance (reliable to others, entity.h:427),
// OP_ClientUpdate (unreliable, client_packet.cpp:4865) and channel messages
// to the co-located caster all through its casts.
class CastTailPeerScenario final : public IScenario {
public:
    CastTailPeerScenario(const char* name, bool traffic, double hold)
        : name_(name), traffic_(traffic), hold_(hold) {}

    const char* Name() const override { return name_; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;
        if (t >= hold_) {
            SetReason(std::string(traffic_ ? "traffic" : "quiet") +
                      " peer held for " + Fmt(hold_) + "s (" +
                      std::to_string(sent_) + " commands)");
            return StepResult::Pass;
        }
        if (!traffic_ || t < kStartAt || t < nextAt_) {
            return StepResult::Running;
        }
        // /move is in the CLIENT frame (x/y swapped from the server's /loc):
        // b630_seed.sql puts the peer at server (-82, 35, 3).
        static const char* const kPattern[] = {
            "/sit", "/stand", "/say b760 traffic", "/move 45 -82 3",
            "/sit", "/stand", "/jump", "/move 35 -82 3",
        };
        const char* cmd = kPattern[sent_ % (sizeof(kPattern) / sizeof(kPattern[0]))];
        world.SendCommand(cmd);
        if (sent_ % 50 == 0) {
            world.Note("traffic: " + std::to_string(sent_) + " commands sent by t=" + Fmt(t));
        }
        ++sent_;
        nextAt_ = t + kStep;
        return StepResult::Running;
    }

private:
    static constexpr double kStartAt = 5.0;
    static constexpr double kStep = 0.25;

    const char* name_;
    bool traffic_;
    double hold_;
    double nextAt_ = 0.0;
    long sent_ = 0;
};

}  // namespace

std::unique_ptr<IScenario> MakeCastTailRepeatQuietScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::make_unique<RefuseScenario>("cast_tail_repeat_quiet",
                                                "--scenario-cast-target is required");
    }
    return std::make_unique<CastTailRepeatScenario>(
        "cast_tail_repeat_quiet", o.successSpellId, o.castSpellId, o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastTailRepeatTrafficScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::make_unique<RefuseScenario>("cast_tail_repeat_traffic",
                                                "--scenario-cast-target is required");
    }
    return std::make_unique<CastTailRepeatScenario>(
        "cast_tail_repeat_traffic", o.successSpellId, o.castSpellId, o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastTailQuietPeerScenario(const ScenarioOptions& o) {
    return std::make_unique<CastTailPeerScenario>("cast_tail_quiet_peer", false, o.holdSeconds);
}

std::unique_ptr<IScenario> MakeCastTailTrafficPeerScenario(const ScenarioOptions& o) {
    return std::make_unique<CastTailPeerScenario>("cast_tail_traffic_peer", true, o.holdSeconds);
}

}  // namespace scenario
}  // namespace eqt
