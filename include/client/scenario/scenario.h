#pragma once

// T410 (board B138): the scenario lane.
//
// A scenario is a small, deterministic state machine that runs INSIDE a live
// client and ends in a verdict. It exists so a live verification stops being
// "a human reads a transcript": the process exit code is the result.
//
// The two halves are deliberately separated so the state machines are
// testable with no client at all:
//   * IScenarioWorld -- the only things a scenario may look at or do. The
//     real implementation (ApplicationScenarioWorld) polls EverQuest; the
//     tests use a fake.
//   * IScenario      -- pure logic over that interface, driven one Step()
//     per game tick by ScenarioRunner.
//
// Scenarios POLL. They deliberately do not subscribe to the event bus:
// GameState::events() (which AutomatedMode forwards) and the renderer-facing
// GameStateBridge are two different streams, and the interesting landings
// (buffs, T348's OP_Action path) go to the bridge, which a headless client
// does not drain. Polling reads the same authoritative state either stream
// was published from, and works identically headless.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {

enum class StepResult {
    Running,  // no verdict yet -- call me again next tick
    Pass,
    Fail,
};

/**
 * Everything a scenario is allowed to see or do.
 *
 * Keep this interface small: every method added here is a method every fake
 * in the tests must implement, and a wider surface is how a scenario ends up
 * depending on the whole client instead of one observable.
 */
class IScenarioWorld {
public:
    virtual ~IScenarioWorld() = default;

    // --- observables ---
    virtual bool IsConnected() const = 0;
    virtual bool IsFullyZonedIn() const = 0;
    virtual std::string ZoneName() const = 0;
    /** Other PLAYERS (npc_type 0) currently tracked, excluding ourselves. */
    virtual int OtherPlayerCount() const = 0;
    /** Our own character name, for a self-target. Empty before zone-in. */
    virtual std::string PlayerName() const = 0;
    /** Spell ids of the buffs currently on the player, in slot order. */
    virtual std::vector<uint32_t> PlayerBuffSpellIds() const = 0;
    /**
     * T494: the equipment MATERIAL another player is wearing, as this client
     * was told by the server (Entity::equipment[], filled only by
     * EntitiesComponent::ZoneProcessWearChange()). `peerName` empty means "the
     * one other player". Returns -1 when there is no such peer, so a scenario
     * can tell "not there" from "there, wearing nothing" (0).
     */
    virtual long long PeerEquipmentMaterial(const std::string& peerName,
                                            int materialSlot) const = 0;
    /**
     * T498 (board B755/B823): the equipment MATERIAL this client has computed
     * for ITSELF, i.e. GameState's own entity's `equipment[materialSlot]`, the
     * value EntitiesComponent::UpdatePlayerAppearanceFromInventory() derives
     * from the inventory. Paired with PeerEquipmentMaterial() it is what makes
     * "self and peers agree" an assertion rather than an argument: the observer
     * process reads the server's number off the spawn packet, this reads the
     * number the actor's own renderer will draw. Returns -1 when our own entity
     * is not readable yet.
     */
    virtual long long SelfEquipmentMaterial(int materialSlot) const = 0;

    // --- actions ---
    /** Dispatch one slash command line, exactly as --script would. */
    virtual void SendCommand(const std::string& rawLine) = 0;
    /** One line of scenario trace (MOD_SCENARIO, INFO). */
    virtual void Note(const std::string& line) = 0;
};

struct ScenarioTick {
    float deltaTime = 0.0f;
    /** Seconds since the scenario's FIRST Step() -- i.e. since zone-in. */
    double sinceStart = 0.0;
};

/**
 * Tunables the runner hands to a scenario at construction. Anything a
 * harness needs to vary belongs here, not in a scenario's constant.
 */
struct ScenarioOptions {
    /** two_client_survive: how long the pair must hold together. */
    double holdSeconds = 300.0;
    /**
     * cast_lands: the beneficial, durational spell to memorise and cast.
     * 288 = Minor Shielding (classes12 = 1), the spell T449 proved lands for
     * the config's Wizard. The old default, 46 (Ultravision, classes12 = 255),
     * could not be cast by that class at any level, so the lane fizzled (B484,
     * issue #35). scripts/scenario.sh re-checks the value it passes against
     * spells_new.classesN for the character's class and level before casting.
     */
    uint32_t castSpellId = 288;
    /**
     * T498: equip_actor_return asserts that its OWN primary material equals
     * this. Negative = do not assert (the default), which is what every
     * scenario but that one wants.
     */
    long long expectSelfMaterial = -1;
    /**
     * B630 (issue #48): the character the interrupted-cast pair TARGETS.
     * cast_interrupt targets the observer's character before it casts; the
     * observer's mirror targets the caster. Empty (the default) means "no
     * target name is supplied -- the scenario that needs one fails fast",
     * which keeps the flag optional for every existing scenario.
     */
    std::string castTargetName;
    /**
     * B630: the D3 leg's VICTIM buff -- a beneficial, targetable buff whose
     * caster-level minimum is above the target's level, so eqemu's
     * CheckSpellLevelRestriction (spells.cpp:4589) makes SpellEffect fail and
     * the caster's cast ends with keepcasting 0 + "did not take hold" but no
     * recast and no buff on the target. 0 = the scenario refuses.
     */
    uint32_t buffNotakeVictimId = 0;
    /**
     * B630: the D1 leg's success nuke (a short, targetable damage spell that
     * the caster lets finish, so the success tail's ManaChange->MemorizeSpell
     * order is recorded). 372 = Blast of Cold.
     */
    uint32_t successSpellId = 372;
    /**
     * B630: the D2 leg's recast-stop spell -- recast_time longer than its
     * recovery_time so the client gem is castable again while the server's
     * per-spell recast timer still runs. 293 = Haze (cast 2000, recovery 2250,
     * recast 4000).
     */
    uint32_t recastStopSpellId = 293;
    /**
     * B630: the B leg's fizzle spell, cast by the NON-GM account. Its class
     * minimum must be far above that character's level so the fizzle roll
     * clamps to ~95%. 41 = Weaken (classes13 255). 0 = the scenario refuses.
     */
    uint32_t fizzleSpellId = 0;
};

class IScenario {
public:
    virtual ~IScenario() = default;

    virtual const char* Name() const = 0;

    /**
     * Called once per game tick, but ONLY after the client is fully zoned in
     * (ScenarioRunner owns the pre-flight and reports a client that never got
     * there as could-not-start). So a scenario's first Step() may assume a
     * live, zoned-in client.
     */
    virtual StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) = 0;

    /** Human-readable reason for the verdict. Set before returning Pass/Fail. */
    const std::string& Reason() const { return reason_; }

protected:
    void SetReason(std::string reason) { reason_ = std::move(reason); }

private:
    std::string reason_;
};

/** Registry. Returns nullptr for an unknown name. */
std::unique_ptr<IScenario> MakeScenario(const std::string& name, const ScenarioOptions& options);
std::vector<std::string> ScenarioNames();
bool IsScenarioName(const std::string& name);

}  // namespace scenario
}  // namespace eqt
