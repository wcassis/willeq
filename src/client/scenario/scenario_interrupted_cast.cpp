// B630 (issue #48, owed live proof) + B760 (issue #45, record only).
//
// The two-client interrupted-cast lane as small state machines.
//
// Every scenario here is a pure DRIVER: it runs its leg's command sequence on
// a calibrated clock and reports "the leg executed to completion". Every
// ASSERTION the card lists -- cast bar clears, gem castable at once, the chat
// string, no late impact, the observer's "<name>'s casting is interrupted!",
// and the B760 packet order between `ManaChange keepcasting 0` and
// `OP_MemorizeSpell` -- is made from the run's DEBUG log by
// scripts/ci/b760_analyze.py. That matches the card's "assert from pasted log
// lines" and keeps IScenarioWorld's deliberately small surface intact: the
// in-process world cannot see cast-bar / gem / chat state, and the DEBUG log
// IS the authoritative wire record -- every zone packet now logs its opcode
// at MOD_ZONE DEBUG (the B760 opcode log in ZoneComponent::DispatchZonePacket),
// every spell packet logs at MOD_SPELL DEBUG (BeginCast, ManaChange,
// MemorizeSpell, Interrupt), and every chat string logs at MOD_MAIN INFO via
// ChatOutput::System, so the log is where a live claim is made.
//
// The interrupt trigger is the deterministic GM `#interrupt`, not movement:
// eqemu does NOT interrupt a cast on a position update (the move path has no
// InterruptSpell). `#interrupt` (zone/gm_commands/interrupt.cpp) calls
// c->InterruptSpell(439) on the ISSUING client -- so the GM caster
// SELF-interrupts its own in-progress cast: it receives OP_InterruptCast msg
// 439 (INTERRUPT_SPELL, "Your spell is interrupted.") + a ManaChange
// keepcasting 0 (SendSpellBarEnable), and every other client in range receives
// OP_InterruptCar msg 12478 (INTERRUPT_SPELL_OTHER, "%1's casting is
// interrupted!", zone/spells.cpp:1306/1339). A second client is still required
// -- as the OBSERVER -- because that bystander line is exactly what
// single-client self-observation cannot prove (the card's two-client rule).
//
// Character kit is seeded by the harness (scripts/scenario_configs/b630_seed.sql):
//   * Testcaster (char 67, GM account willeq_test status 200) gets gm=1 and is
//     moved to cazicthule (zone 36) beside its peer, so its `#` commands route
//     through command_dispatch (ChannelMessageReceived, zone/client.cpp:1209)
//     with full GM privileges and it can run every caster leg in one zone-in.
//   * Testbuddy (char 68, plain account willeq_buddy status 0) is moved to the
//     same coordinates as the co-located peer.
// Both are co-located by SQL so neither client has to summon or path to the
// other; the observer just has to SEE the caster (OtherPlayerCount >= 1).

#include "client/scenario/scenarios.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

// A timed prep command, relative to zone-in. Every one is a server round trip,
// so the windows are generous (the same shape as cast_lands').
struct PrepStep {
    double at;
    std::string command;
};

std::string Fmt(const double value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

// A scenario whose options are incomplete: it reports the missing flag and
// fails on the first tick, which the runner turns into exit 1 with a readable
// reason (better than a crash, and better than guessing a character).
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

// Wait until a named peer is tracked, then send one command. Shared by every
// leg that has to target the co-located peer.
class TargetPeer {
public:
    TargetPeer(double at, const std::string& name)
        : at_(at), name_(name) {}

    // Returns false while still waiting (or the peer never showed, which the
    // caller turns into a Fail). True once the target is sent.
    bool MaybeTarget(IScenarioWorld& world, double t, double deadline) {
        if (done_) {
            return true;
        }
        if (t < at_) {
            return false;
        }
        if (world.OtherPlayerCount() >= 1) {
            done_ = true;
            world.Note("targeted " + name_ + " at t=" + Fmt(t));
            world.SendCommand("/target " + name_);
            return true;
        }
        if (t >= deadline) {
            fail = "peer '" + name_ + "' never appeared in '" + world.ZoneName() +
                   "' within " + Fmt(deadline) + "s";
            return true;
        }
        return false;
    }

    const std::string& Failure() const { return fail; }

private:
    double at_;
    std::string name_;
    bool done_ = false;
    std::string fail;
};

// ---------------------------------------------------------------------------
// cast_interrupt -- the CASTER half of the owed B630 proof (leg A).
//
// The GM caster zones into the shared rig zone, memorises a 3 s nuke on gem 1
// (#memspell 378 0), targets the observer, casts it, and fires the GM
// `#interrupt` ~1.5 s into the 3 s cast. The caster-side assertions (cast bar
// clears, gem ready at once, "was interrupted", no late impact) and the B760
// packet order are read off this process's log by the analyzer.
// ---------------------------------------------------------------------------
class CastInterruptScenario final : public IScenario {
public:
    CastInterruptScenario(uint32_t spellId, std::string targetName)
        : spellId_(spellId),
          target_(kTargetAt, targetName),
          // B630 item 1: eqemu's GM `#unmemspells`/`#memspell` apply to
          // c->GetTarget() (zone/gm_commands/unmemspells.cpp:22-24,
          // memspell.cpp:31-33), so the prep must run with nothing targeted.
          // This is the first leg of the chain, but a fresh zone-in can still
          // carry a server-side target (last-target persists across logins on
          // the character), so clear it explicitly before the GM prep.
          prep_({
              {1.0, "/cleartarget"},
              {3.0, "/say #unmemspells"},
              {7.0, "/say #memspell " + std::to_string(spellId) + " 0"},
          }) {}

    const char* Name() const override { return "cast_interrupt"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            return StepResult::Running;
        }

        if (!target_.MaybeTarget(world, t, kPeerDeadline)) {
            if (!target_.Failure().empty()) {
                SetReason(target_.Failure());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        // MaybeTarget returned true: either the peer was found (done_) or the
        // deadline was hit (Failure() set). The caller must distinguish: if the
        // failure string is set, do NOT fall through to the cast.
        if (!target_.Failure().empty()) {
            SetReason(target_.Failure());
            return StepResult::Fail;
        }

        if (!castSent_ && t >= kCastAt) {
            castSent_ = true;
            castAt_ = t;
            world.Note("casting: /cast 1 (spell " + std::to_string(spellId_) +
                       ") at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }

        // Fire the interrupt mid-cast: kInterruptAfter into a ~3 s cast.
        if (!interruptSent_ && castSent_ && (t - castAt_) >= kInterruptAfter) {
            interruptSent_ = true;
            interruptAt_ = t;
            world.Note("fired GM #interrupt at t=" + Fmt(interruptAt_) + " (" +
                       Fmt(t - castAt_) + "s into the cast)");
            world.SendCommand("/say #interrupt");
            return StepResult::Running;
        }

        if (interruptSent_ && (t - interruptAt_) >= kHoldAfter) {
            world.Note("leg complete: cast " + std::to_string(spellId_) +
                       " at t=" + Fmt(castAt_) + ", #interrupt at t=" +
                       Fmt(interruptAt_) + ", held " + Fmt(t - interruptAt_) +
                       "s. Assert from the log: caster cast bar clear + gem "
                       "ready + 'was interrupted' + no late impact; B760 "
                       "packet order.");
            SetReason("interrupt leg executed (cast " +
                      std::to_string(spellId_) + " at t=" + Fmt(castAt_) +
                      ", #interrupt at t=" + Fmt(interruptAt_) +
                      "); assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    static constexpr double kTargetAt = 9.0;
    static constexpr double kPeerDeadline = 60.0;
    static constexpr double kCastAt = 14.0;
    static constexpr double kInterruptAfter = 1.5;
    static constexpr double kHoldAfter = 10.0;

    uint32_t spellId_;
    std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    TargetPeer target_;
    bool castSent_ = false;
    bool interruptSent_ = false;
    double castAt_ = 0.0;
    double interruptAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// cast_success_tail -- T482 leg 1 / the B760 SUCCESS row (D1). Same caster,
// but the nuke is ALLOWED to finish: the success tail is ManaChange
// keepcasting 0 (SendSpellBarEnable) then OP_MemorizeSpell scribing 3
// (spells.cpp:1817-1824), and the cast resolves to SpellCastComplete=Success.
// This records the B760 question for the SUCCESS outcome: does any packet
// arrive between the ManaChange and the MemorizeSpell?
// ---------------------------------------------------------------------------
class CastSuccessTailScenario final : public IScenario {
public:
    CastSuccessTailScenario(uint32_t spellId, std::string targetName)
        : spellId_(spellId),
          target_(kTargetAt, targetName),
          // B630 item 1: eqemu's GM `#unmemspells`/`#memspell` apply to
          // c->GetTarget() (zone/gm_commands/unmemspells.cpp:22-24,
          // memspell.cpp:31-33), so the PREP must run with nothing targeted.
          // The previous leg's `/target` persists on the server, so the first
          // prep step clears it. The peer is targeted only AFTER the prep.
          prep_({
              {1.0, "/cleartarget"},
              {3.0, "/say #unmemspells"},
              {7.0, "/say #memspell " + std::to_string(spellId) + " 0"},
          }) {}

    const char* Name() const override { return "cast_success_tail"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            return StepResult::Running;
        }

        if (!target_.MaybeTarget(world, t, kPeerDeadline)) {
            if (!target_.Failure().empty()) {
                SetReason(target_.Failure());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        // MaybeTarget returned true: either the peer was found (done_) or the
        // deadline was hit (Failure() set). The caller must distinguish: if the
        // failure string is set, do NOT fall through to the cast.
        if (!target_.Failure().empty()) {
            SetReason(target_.Failure());
            return StepResult::Fail;
        }

        if (!castSent_ && t >= kCastAt) {
            castSent_ = true;
            castAt_ = t;
            world.Note("casting to completion: /cast 1 (spell " +
                       std::to_string(spellId_) + ") at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }

        // Give the ~1.5 s cast plus its tail room to finish, then pass. The
        // success (SpellCastComplete=Success, MemorizeSpell scribing 3, one
        // impact burst) is asserted from the log.
        if (castSent_ && (t - castAt_) >= kTailWindow) {
            world.Note("leg complete: cast " + std::to_string(spellId_) +
                       " and let it finish. Assert from the log: BeginCast, "
                       "one impact, 'completed successfully', and the "
                       "ManaChange->MemorizeSpell order (Success row of B760).");
            SetReason("success-tail leg executed (cast " +
                      std::to_string(spellId_) + "); assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    // The prep ends at 7.0 s (memspell), so targeting only starts at 8.0 s;
    // it also waits for the peer to be tracked. The cast follows the target.
    static constexpr double kTargetAt = 8.0;
    static constexpr double kPeerDeadline = 60.0;
    static constexpr double kCastAt = 14.0;
    static constexpr double kTailWindow = 12.0;

    uint32_t spellId_;
    std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    TargetPeer target_;
    bool castSent_ = false;
    double castAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// cast_recast_stop -- T482 leg 2 / the B760 RECAST row (D2). The server arms a
// per-spell recast timer on a successful finish (pTimerSpellStart + spell_id,
// started for recast_time at spells.cpp:2834). A second cast of the same spell
// that starts while that timer still runs reaches CastedSpellFinished's
// recast gate (spells.cpp:1419): the server answers with the SPELL_RECAST 236
// chat string ("Spell recast time not yet met.") and StopCasting -- a
// ManaChange keepcasting 0 with NO MemorizeSpell. The client's own gem
// recovers on its (shorter) recovery timer, so the gem IS castable again
// server-side-desynced -- exactly the desync T482 names. The spell must have
// recast_time > recovery_time so the two timers can disagree: 293 Haze
// (cast 2000, recovery 2250, recast 4000).
// ---------------------------------------------------------------------------
class CastRecastStopScenario final : public IScenario {
public:
    CastRecastStopScenario(uint32_t spellId, std::string targetName)
        : spellId_(spellId),
          target_(kTargetAt, targetName),
          // B630 item 1: prep with nothing targeted (see D1). The second cast
          // of this leg must still be the same spell, so the prep memorises it.
          prep_({
              {1.0, "/cleartarget"},
              {3.0, "/say #unmemspells"},
              {7.0, "/say #memspell " + std::to_string(spellId) + " 0"},
          }) {}

    const char* Name() const override { return "cast_recast_stop"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            return StepResult::Running;
        }

        if (!target_.MaybeTarget(world, t, kPeerDeadline)) {
            if (!target_.Failure().empty()) {
                SetReason(target_.Failure());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        // MaybeTarget returned true: either the peer was found (done_) or the
        // deadline was hit (Failure() set). The caller must distinguish: if the
        // failure string is set, do NOT fall through to the cast.
        if (!target_.Failure().empty()) {
            SetReason(target_.Failure());
            return StepResult::Fail;
        }

        if (!firstSent_ && t >= kFirstCastAt) {
            firstSent_ = true;
            firstAt_ = t;
            world.Note("first cast (to completion): /cast 1 (spell " +
                       std::to_string(spellId_) + ") at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }

        // Wait for the gem to recover from the first cast (recovery 2250 ms +
        // margin), then cast again INSIDE the 4000 ms server recast window.
        if (firstSent_ && !secondSent_ && (t - firstAt_) >= kSecondAfter) {
            secondSent_ = true;
            secondAt_ = t;
            world.Note("second cast into the server recast window: /cast 1 at "
                       "t=" + Fmt(t) + " (" + Fmt(t - firstAt_) +
                       "s after the first; recast 4000ms > recovery 2250ms)");
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }

        if (secondSent_ && (t - secondAt_) >= kHoldAfter) {
            world.Note("leg complete: first cast at t=" + Fmt(firstAt_) +
                       ", second at t=" + Fmt(secondAt_) + ". Assert from the "
                       "log: the SECOND cast's BeginCast then "
                       "ManaChange keepcasting 0 with NO MemorizeSpell and "
                       "the 'Spell recast time not yet met.' chat (SPELL_RECAST "
                       "236). B760 recast row.");
            SetReason("recast-stop leg executed (two casts of " +
                      std::to_string(spellId_) + "); assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    // The prep ends at 7.0 s, so targeting starts at 8.0 s (also waits for the
    // peer). First cast at 14 s; the second at +3.5 s lands inside the 4 s
    // server recast window of the first.
    static constexpr double kTargetAt = 8.0;
    static constexpr double kPeerDeadline = 60.0;
    static constexpr double kFirstCastAt = 14.0;
    // Gem recovers at ~2.25 s after the first cast starts; cast again at 3.5 s,
    // still inside the 4 s server recast window.
    static constexpr double kSecondAfter = 3.5;
    static constexpr double kHoldAfter = 8.0;

    uint32_t spellId_;
    std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    TargetPeer target_;
    bool firstSent_ = false;
    bool secondSent_ = false;
    double firstAt_ = 0.0;
    double secondAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// cast_buff_notake -- T482 leg 3 (D3). A buff that does NOT take hold: the
// caster lays a higher-caster-level, same-resist-type blocker on the target
// first, then casts a second buff of that resist type. eqemu's AddBuff
// returns -1 ("stacking prevented", zone/spells.cpp:3583) and the caster gets
// "Your <spell> did not take hold on <target>. (Blocked by <blocker>.)"
// (spells.cpp:3590). Like D2 the cast bar clears (keepcasting 0) but the buff
// never lands -- asserted from the log: the notake string on the caster, and
// the observer's target NEVER gaining the buff spell id.
// ---------------------------------------------------------------------------
class CastBuffNotakeScenario final : public IScenario {
public:
    CastBuffNotakeScenario(uint32_t buffId, std::string targetName)
        : buffId_(buffId),
          target_(kTargetAt, targetName),
          // B630 item 1: the GM prep applies to c->GetTarget(), so clear any
          // target first even though this leg targets the peer at 8.0 s.
          prep_({
              {1.0, "/cleartarget"},
              {3.0, "/say #unmemspells"},
              {7.0, "/say #memspell " + std::to_string(buffId) + " 0"},
          }) {}

    const char* Name() const override { return "cast_buff_notake"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            return StepResult::Running;
        }

        if (!target_.MaybeTarget(world, t, kPeerDeadline)) {
            if (!target_.Failure().empty()) {
                SetReason(target_.Failure());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        if (!target_.Failure().empty()) {
            SetReason(target_.Failure());
            return StepResult::Fail;
        }

        if (!castSent_ && t >= kCastAt) {
            castSent_ = true;
            castAt_ = t;
            world.Note("buff leg: /cast 1 (spell " + std::to_string(buffId_) +
                       ") at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }

        if (castSent_ && (t - castAt_) >= kHoldAfter) {
            world.Note("leg complete: buff cast at t=" + Fmt(castAt_) +
                       ". Assert from the log: the caster's buff bar gains " +
                       "spell " + std::to_string(buffId_) + " and the success "
                       "tail (ManaChange keepcasting 0 then MemorizeSpell "
                       "scribing 3); the observer sees the target gain it.");
            SetReason("buff-notake leg executed (spell " +
                      std::to_string(buffId_) + "); assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    static constexpr double kTargetAt = 3.0;
    static constexpr double kPeerDeadline = 45.0;
    static constexpr double kCastAt = 12.0;
    static constexpr double kHoldAfter = 10.0;

    uint32_t buffId_;
    std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    TargetPeer target_;
    bool castSent_ = false;
    double castAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// cast_interrupt_chain -- the CASTER for the B630 lane. Runs legs A, D1, D2
// and D3 back-to-back in the same zone-in (the co-located peer never leaves),
// so one client login exercises every caster-side outcome. Each leg's
// assertions are read off this process's log by the analyzer.
// ---------------------------------------------------------------------------
class CasterChainScenario final : public IScenario {
public:
    CasterChainScenario(const ScenarioOptions& options) {
        legs_.push_back(std::make_unique<CastInterruptScenario>(
            options.castSpellId, options.castTargetName));
        legs_.push_back(std::make_unique<CastSuccessTailScenario>(
            options.successSpellId, options.castTargetName));
        legs_.push_back(std::make_unique<CastRecastStopScenario>(
            options.recastStopSpellId, options.castTargetName));
        legs_.push_back(std::make_unique<CastBuffNotakeScenario>(
            options.buffNotakeVictimId, options.castTargetName));
    }

    const char* Name() const override { return "cast_interrupt_chain"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (legIndex_ >= legs_.size()) {
            SetReason("all " + std::to_string(legs_.size()) +
                      " legs executed; assert each from the log");
            return StepResult::Pass;
        }
        IScenario& leg = *legs_[legIndex_];
        // B630 fix pass: each leg's prep/cast offsets are calibrated from ITS
        // OWN start, not the run's. Passing the global run clock let every D
        // leg's prep offsets (1/3/7/8/14 s) land already-elapsed, so the whole
        // prep fired in a ~50 ms burst BEFORE the server's #memspell response
        // arrived, and the leg's /cast used the PREVIOUS leg's gem (D1 cast
        // 378, D2 cast 372, D3 cast nothing). Offset by when this leg started
        // so its t=0 is the first tick after the previous leg finished; the
        // offsets then re-open and each #memspell round-trip lands before the
        // leg's cast.
        if (!legStartedAt_) {
            legStartedAt_ = tick.sinceStart;
            world.Note("leg " + std::to_string(legIndex_ + 1) + " clock "
                       "re-baselined at t=" + Fmt(tick.sinceStart));
        }
        ScenarioTick legTick = tick;
        legTick.sinceStart = tick.sinceStart - *legStartedAt_;
        const StepResult r = leg.Step(world, legTick);
        if (r == StepResult::Pass) {
            legStartedAt_.reset();
            world.Note("leg " + std::to_string(legIndex_ + 1) + " complete: " +
                       leg.Name());
            ++legIndex_;
            if (legIndex_ < legs_.size()) {
                world.Note("starting leg " +
                           std::to_string(legIndex_ + 1) + " (" +
                           legs_[legIndex_]->Name() + ")");
            }
            return StepResult::Running;
        }
        if (r == StepResult::Fail) {
            SetReason("leg " + std::to_string(legIndex_ + 1) + " (" +
                      leg.Name() + ") failed: " + leg.Reason());
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    std::vector<std::unique_ptr<IScenario>> legs_;
    size_t legIndex_ = 0;
    // The run-clock instant the current leg started (for the per-leg offset);
    // reset to nullopt when a leg finishes so the next leg re-baselines.
    std::optional<double> legStartedAt_ = std::nullopt;
};

// ---------------------------------------------------------------------------
// cast_interrupt_observe -- the OBSERVER half. Sends nothing except the
// target; its entire claim is that it watched the caster's interrupted cast
// from OUTSIDE. It targets the caster so SpellManager registers the caster as
// its target (handleBeginCast sets m_target_caster_id when caster_id == the
// CombatManager target), which is what makes the bystander interrupt line
// carry the caster's name and clear the observer's target cast bar + glow.
// It holds the target for holdSeconds to cover the caster's whole chain, and
// its log is the source for the observer-side assertions:
//   * "Testcaster's casting is interrupted!" (msg 12478, the A leg)
//   * the target's BeginCast lines for A/D1/D2 (cast bar + glow up)
//   * the target NOT gaining the D3 victim buff.
// ---------------------------------------------------------------------------
class CastInterruptObserveScenario final : public IScenario {
public:
    CastInterruptObserveScenario(std::string targetName, double holdSeconds)
        : targetName_(std::move(targetName)), holdSeconds_(holdSeconds) {}

    const char* Name() const override { return "cast_interrupt_observe"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!targetSent_) {
            if (t < kTargetAt) {
                return StepResult::Running;
            }
            // The observer targets the PEER (its caster), which is the OTHER
            // player in the zone -- world.PlayerName() from the observer's
            // process is the observer's OWN name, so the peer is whatever the
            // spawn is. In practice the peer is the caster's name, which the
            // lane passes in as targetName_ (the caster's character name).
            if (world.OtherPlayerCount() >= 1) {
                targetSent_ = true;
                targetAt_ = t;
                world.Note("targeted " + targetName_ + " at t=" + Fmt(t) +
                           "; observing for " + Fmt(holdSeconds_) + "s");
                world.SendCommand("/target " + targetName_);
                return StepResult::Running;
            }
            if (t >= kPeerDeadline) {
                SetReason("caster '" + targetName_ + "' never appeared in '" +
                          world.ZoneName() + "' within " + Fmt(kPeerDeadline) +
                          "s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }

        if (!world.IsFullyZonedIn()) {
            SetReason("observer left the zone during the hold");
            return StepResult::Fail;
        }
        // B630 fix pass: in the CHAIN lane the caster runs its four legs
        // back-to-back and then EXITS, while the observer holds on a timer.
        // Without this the observer always failed with "caster left the zone
        // during the hold" shortly after the chain ended -- a false negative
        // that read as a lane defect (devrun-1 2026-09-19: observer exit 1,
        // the chain itself clean). The caster's clean departure IS the
        // chain's end: the observer saw every cast from outside (that is what
        // the analyzer's observer-side cells assert from THIS log), so a
        // caster that is gone after the observer has watched it for a
        // meaningful floor is a pass, not a failure. The floor (not the hold)
        // is the guard: a caster that vanishes almost immediately (before it
        // could have run any leg) still fails, and the analyzer still checks
        // the observer log independently, so a too-early departure cannot
        // mask a missing 12478 line.
        if (world.OtherPlayerCount() < 1) {
            if (t - targetAt_ >= kDepartureFloor) {
                world.Note("caster left the zone at t=" + Fmt(t) +
                           " (" + Fmt(t - targetAt_) + "s after targeting, "
                           "end of the chain); the observer-side assertions "
                           "are read from this log; pass");
                SetReason("caster departed after " + Fmt(t - targetAt_) +
                          "s of observation (end of chain); assert from the log");
                return StepResult::Pass;
            }
            SetReason("caster left the zone during the hold");
            return StepResult::Fail;
        }

        if (t - targetAt_ >= holdSeconds_) {
            world.Note("held target on " + targetName_ + " for " +
                       Fmt(t - targetAt_) + "s, sending nothing else. "
                       "Observer-side assertions (cast bar/glow, "
                       "'<name>'s casting is interrupted!', no D3 buff) are "
                       "read from this log.");
            SetReason("observer held " + targetName_ + " for " +
                      Fmt(t - targetAt_) + "s; assert from the log");
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    static constexpr double kTargetAt = 6.0;
    static constexpr double kPeerDeadline = 90.0;
    // The observer must have actually watched the caster for this long before
    // a caster departure counts as "end of the chain" rather than a failure.
    // Leg A alone takes ~19 s (cast 14 s + interrupt + hold 10 s), so 45 s is
    // well past it and comfortably inside the ~85 s chain.
    static constexpr double kDepartureFloor = 45.0;

    std::string targetName_;
    double holdSeconds_;
    bool targetSent_ = false;
    double targetAt_ = 0.0;
};

// ---------------------------------------------------------------------------
// cast_fizzle -- leg B, the NON-GM caster. eqemu never fizzles a GM
// (Client::CheckFizzle, zone/spells.cpp: "GMs don't fizzle"), so this leg MUST
// be the plain account (Testbuddy, status 0). Casting a spell whose class-13
// minimum is far above the caster's level makes the non-legacy fizzle roll
// clamp to ~95%: each attempt that does NOT fizzle just completes (the buddy
// is a level-1 caster, so a landed nuke is harmless) and the gem cools for its
// short recast. The caster targets ITSELF and loops a fixed number of gem
// casts; the analyzer decides which attempt (if any) fizzled from the log --
// a fizzle is OP_ManaChange keepcasting 0 with NO BeginCast before it, and
// the "Your spell fizzles!" (SPELL_FIZZLE 173) chat.
// ---------------------------------------------------------------------------
class CastFizzleScenario final : public IScenario {
public:
    CastFizzleScenario(uint32_t spellId, std::string targetName)
        : spellId_(spellId), targetName_(std::move(targetName)) {}

    const char* Name() const override { return "cast_fizzle"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!targetSent_) {
            if (t < kTargetAt) {
                return StepResult::Running;
            }
            targetSent_ = true;
            targetAt_ = t;
            world.Note("targeting self (" + targetName_ + ") at t=" + Fmt(t));
            world.SendCommand("/target " + targetName_);
            return StepResult::Running;
        }

        if (!prepDone_ && t >= kPrepAt) {
            prepDone_ = true;
            world.Note("prep: /say #unmemspells");
            world.SendCommand("/say #unmemspells");
            return StepResult::Running;
        }
        if (!memDone_ && t >= kMemAt) {
            memDone_ = true;
            world.Note("prep: /say #memspell " + std::to_string(spellId_) + " 0");
            world.SendCommand("/say #memspell " + std::to_string(spellId_) + " 0");
            return StepResult::Running;
        }
        if (!memDone_) {
            return StepResult::Running;
        }

        // One gem cast per attempt, throttled by a gap long enough for the
        // (short) cast + gem recovery to finish, so a landed attempt never
        // clobbers the next.
        if (!firstCastSent_ && t >= kFirstCastAt) {
            firstCastSent_ = true;
            lastCastAt_ = t;
            attemptCount_ = 1;
            world.Note("fizzle attempt 1: /cast 1 (spell " +
                       std::to_string(spellId_) + ") at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }
        if (firstCastSent_ && (t - lastCastAt_) >= kAttemptGap) {
            if (attemptCount_ >= kMaxAttempts) {
                world.Note("fizzle leg: made " + std::to_string(attemptCount_) +
                           " gem casts of " + std::to_string(spellId_) +
                           " in " + Fmt(t) + "s. Assert from the log: a "
                           "'fizzled' outcome (BeginCast NOT followed by "
                           "MemorizeSpell, then ManaChange keepcasting 0 + "
                           "'Your spell fizzles!'); if NONE fizzled, re-run.");
                SetReason("fizzle leg executed (" +
                          std::to_string(attemptCount_) + " attempts of " +
                          std::to_string(spellId_) + "); assert from the log");
                return StepResult::Pass;
            }
            ++attemptCount_;
            lastCastAt_ = t;
            world.Note("fizzle attempt " + std::to_string(attemptCount_) +
                       ": /cast 1 at t=" + Fmt(t));
            world.SendCommand("/cast 1");
            return StepResult::Running;
        }
        return StepResult::Running;
    }

private:
    static constexpr double kTargetAt = 3.0;
    static constexpr double kPrepAt = 5.0;
    static constexpr double kMemAt = 9.0;
    static constexpr double kFirstCastAt = 13.0;
    // The fizzle spell's recovery (~1500 ms) + a landed cast's margin.
    static constexpr double kAttemptGap = 4.0;
    static constexpr int kMaxAttempts = 24;

    uint32_t spellId_;
    std::string targetName_;
    bool targetSent_ = false;
    bool prepDone_ = false;
    bool memDone_ = false;
    bool firstCastSent_ = false;
    double targetAt_ = 0.0;
    double lastCastAt_ = 0.0;
    int attemptCount_ = 0;
};

}  // namespace

std::unique_ptr<IScenario> MakeCastInterruptScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_interrupt",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CastInterruptScenario>(o.castSpellId,
                                                   o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastInterruptChainScenario(
    const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_interrupt_chain",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CasterChainScenario>(o);
}

std::unique_ptr<IScenario> MakeCastSuccessTailScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_success_tail",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CastSuccessTailScenario>(o.successSpellId,
                                                     o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastRecastStopScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_recast_stop",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CastRecastStopScenario>(o.recastStopSpellId,
                                                    o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastBuffNotakeScenario(const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_buff_notake",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CastBuffNotakeScenario>(
        o.buffNotakeVictimId, o.castTargetName);
}

std::unique_ptr<IScenario> MakeCastInterruptObserveScenario(
    const ScenarioOptions& o) {
    if (o.castTargetName.empty()) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_interrupt_observe",
                               "--scenario-cast-target is required"));
    }
    return std::make_unique<CastInterruptObserveScenario>(o.castTargetName,
                                                          o.holdSeconds);
}

std::unique_ptr<IScenario> MakeCastFizzleScenario(const ScenarioOptions& o) {
    if (o.fizzleSpellId == 0) {
        return std::unique_ptr<IScenario>(
            new RefuseScenario("cast_fizzle",
                               "--scenario-fizzle-spell is required"));
    }
    return std::make_unique<CastFizzleScenario>(
        o.fizzleSpellId, o.castTargetName.empty() ? std::string()
                                                  : o.castTargetName);
}

}  // namespace scenario
}  // namespace eqt
