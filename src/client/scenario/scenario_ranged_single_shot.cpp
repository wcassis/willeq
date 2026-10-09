// B900 (#216): a single ranged attack, live on the lane.
//
// The offline wire shape (m_atk = slotRange, m_skill Archery/Throwing, the
// eqstr 9150 refusal) is test_ranged_attack_wire.cpp's. This is the PACKET
// half's LANE leg (acceptance criterion 3): a GM-kitted character fires ONE
// shot at a live NPC and the SERVER agrees.
//
// What "the server agrees" is, and why the client's own inventory is NOT the
// observable: eqemu's Client::RangedAttack (zone/special_attacks.cpp:860)
// does the whole verdict server-side -- it checks the bow in slotRange, the
// ammo in slotAmmo (special_attacks.cpp:880-891), the distance band
// (MinRangedAttackDist rule 25 .. Range+Arrow Range), then
// DoArcheryAttackDmg + DeleteItemInInventory(ammo_slot, 1) (special_attacks.cpp:
// 986-1002). A hit and a miss BOTH broadcast an OP_Damage to the attacker
// (attack.cpp's CommonDamage: damage>0 to the attacker, damage==0 to the
// nearby clients), and the client turns it into a combat chat line
// ("You hit <t> for N points of damage." on a hit, "You try to hit <t> but
// miss!" on a miss -- combat_actions.cpp:334/354). Either line proves the
// server ran the ranged path; a packet it refused (no bow, no target, out of
// range) produces NEITHER.
//
// The ammo COUNT is asserted by the lane's post-check
// (scripts/scenario_configs/ranged_single_shot.post.sh) against the database:
// one shot consumed exactly one arrow (special_attacks.cpp:1001). The client
// CANNOT assert the count itself -- the DB is the server's record, and the
// post-check runs with the world still up (before the snapshot restore takes
// the seed out again).
//
// The kit is DB-seeded (ranged_single_shot_seed.sql), not client-equipped:
// the arrow's server slot bitmask is bit 22 (RoF2 slotAmmo) while the
// client's ammo slot is 21 (Titanium), so canEquipInSlot() rejects it and
// /equip can never seat the ammo. The bow (bit 11, both numberings agree)
// COULD be client-equipped, but seating both from the one seed keeps the
// kit identical to what RangedAttack() reads, and the seed is removed by the
// lane's own inventory restore.
//
// The target is a GM #spawn AT the character's own position (zone/
// gm_commands/spawn.cpp:46, SpawnNPC(name, c->GetPosition(), c) -- the
// trailing numbers are race/level/texture/health/gender/class, NOT an
// offset), and the CHARACTER then moves 50 units away (#goto x y z,
// zone/gm_commands/goto.cpp's position branch). 50 is inside the 25..240
// band: the near bound is the MinRangedAttackDist rule (common/ruletypes.
// h:576, default 25, checked at zone/special_attacks.cpp:969) and the far
// bound is bow range 215 + arrow range 25, both measured from the lane's
// items table (Bloodscream 111529, Arrow 8005). Both GM commands go out as
// /say -- #commands reach the server only as chat text for a GM character,
// the same recipe scenario_cast_lands.cpp uses for #set / #memspell.

#include "client/scenario/scenarios.h"

#include <cstddef>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

class RangedSingleShotScenario final : public IScenario {
public:
    explicit RangedSingleShotScenario(std::string targetName)
        : targetName_(std::move(targetName)) {
        // #spawn takes a SINGLE-TOKEN name (zone/npc.cpp:1054, sep.arg[0]) --
        // a space in the argument splits it into the name + a race field. So
        // the spawn name is "Ranged_Dummy" (one token); the server renders the
        // entity's display name as that token PLUS a spawn-index suffix, so
        // the client sees an entity named "Ranged_Dummy000" (underscore
        // preserved, "000" appended; measured live 2026-10-08, lane 4). The
        // client's /target resolver is a case-insensitive SUBSTRING match
        // (entity_manager.cpp:186, entityNameLower.find(searchLower)), so we
        // target "Ranged_Dummy" -- a substring of "Ranged_Dummy000". (A space
        // in the target, "Ranged Dummy", does NOT substring-match an
        // underscore name -- that was the first two live failures.) Target
        // AFTER the #goto -- a teleport can clear the current target, and the
        // one-shot /range must not fire at a stale/absent one.
        prep_ = {
            {3.0, "/say #spawn Ranged_Dummy"},
            {8.0, "/say #goto 70 5 5"},
            {12.0, "/target Ranged_Dummy"},
        };
    }

    const char* Name() const override { return "ranged_single_shot"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!baselineTaken_) {
            // Arm BEFORE any command: lines up to here are zone-in noise, and
            // the pass condition scans from this index, so the only combat
            // line it can find is the one OUR shot produced.
            baseline_ = world.ChatLineCount();
            baselineTaken_ = true;
        }

        // Prep, one command per window (each is a server round trip).
        if (prepIndex_ < prep_.size()) {
            if (t < prep_[prepIndex_].at) {
                return StepResult::Running;
            }
            world.Note("prep: " + prep_[prepIndex_].command);
            world.SendCommand(prep_[prepIndex_].command);
            ++prepIndex_;
            return StepResult::Running;
        }

        // ONE shot.
        if (!shotFired_) {
            if (t < kShotAt) {
                return StepResult::Running;
            }
            shotFired_ = true;
            shotAt_ = t;
            world.Note("firing ONE shot: /range (aliases: /rangedattack, /fire)");
            world.SendCommand("/range");
            return StepResult::Running;
        }

        if (HasVerdict(world)) {
            std::ostringstream os;
            os << "server fired one shot: '" << HitLine(world) << "' "
               << (t - shotAt_) << "s after /range (one OP_CombatAbility{slotRange, Archery} "
               << "sent; the post-check asserts the DB ammo count dropped by 1)";
            SetReason(os.str());
            return StepResult::Pass;
        }

        // On the way to the no-verdict fail, dump what the CLIENT believes
        // about the range slot (the ammo slot, server 21 = client-general 22,
        // is SLOT_INVALID to this client -- inventory_constants.h:294 -- so
        // it is not addressable through the server-slot interface). An empty
        // range slot says "the server never told us about the bow" (a
        // login/seed gap); a full kit says "the packet reached a server that
        // could not see the bow" (a server-side precondition).
        if (!stateDumped_ && t >= kShotDeadline) {
            stateDumped_ = true;
            std::ostringstream os;
            os << "client-side state at failure: range slot (DB 11) item="
               << world.ClientItemAt(11);
            world.Note(os.str());
        }

        if (t >= kShotDeadline) {
            std::ostringstream os;
            os << "no server hit/miss verdict within " << (kShotDeadline - shotAt_)
               << "s of the shot -- the server either never ran RangedAttack() "
               << "(no bow/ammo in the range slot, no target, or out of the 25..240 "
               << "unit band) or the shot never left the client; see the "
               << "client log's MOD_COMBAT RangedAttack line and the server "
               << "combat log";
            SetReason(os.str());
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    static constexpr double kShotAt = 16.0;
    static constexpr double kShotDeadline = 45.0;

    struct PrepStep {
        double at;
        std::string command;
    };

    bool HasVerdict(IScenarioWorld& world) const {
        return HitLine(world).size() > 0;
    }

    // The first post-shot line in a combat channel that is OURS (starts
    // "You"), names the target, and says hit or miss. A hit is "You <verb>
    // <t> for N points of damage." (channel combat_self), a miss "You try to
    // <verb> <t> but miss!" (channel combat_miss -- combat_actions.cpp:324/354,
    // chat_output.cpp:46/62). "The "You"" requirement is what keeps a
    // bystander's line -- "<target> hits <other>" in channel combat, or the
    // NPC-on-us "<source> ... YOU ..." (capital) -- from reading as our
    // verdict; zone-in combat (before the shot) is excluded by the baseline.
    std::string HitLine(IScenarioWorld& world) const {
        const size_t count = world.ChatLineCount();
        for (size_t i = baseline_; i < count; ++i) {
            const std::string line = world.ChatLine(i);
            if (line.find("combat") == std::string::npos) {
                continue;
            }
            if (line.find("You ") == std::string::npos) {
                continue;
            }
            if (line.find(targetName_) == std::string::npos) {
                continue;
            }
            if (line.find(" points of damage") != std::string::npos ||
                line.find("but miss") != std::string::npos) {
                return line;
            }
        }
        return std::string();
    }

    std::string targetName_;
    bool baselineTaken_ = false;
    size_t baseline_ = 0;
    mutable std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    bool shotFired_ = false;
    double shotAt_ = 0.0;
    bool stateDumped_ = false;
};

}  // namespace

std::unique_ptr<IScenario> MakeRangedSingleShotScenario(const ScenarioOptions& options) {
    (void)options;
    return std::unique_ptr<IScenario>(new RangedSingleShotScenario("Ranged Dummy"));
}

}  // namespace scenario
}  // namespace eqt
