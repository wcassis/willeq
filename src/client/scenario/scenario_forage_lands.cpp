#include "client/scenario/scenarios.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * forage_lands -- B894 (issue #210). One forage landing (the HARD leg --
 * the card's "the caught item on the cursor") and one fishing landing
 * (best-effort), each asserted where the client can actually see the
 * result: the caught item ON THE CURSOR plus the skill line. eqemu answers
 * both successes the same way (zone/forage.cpp :353-369 and :499-508 --
 * MessageString then PushItemOnCursor + SendItemPacket(slotCursor)), and the
 * client routes that as a ChatMessage (ZoneProcessSimpleMessage, Chat::Skills)
 * plus a cursor item (processItemPacket), so the observable is a chat line
 * from the forage/fishing vocabulary AND a new cursor item after the
 * trigger. A line without an item (or an item without a line) is a Fail --
 * the half-landing is exactly what this card exists to rule out.
 *
 * The triggers are eqemu GM commands (the lane's account is GMMgmt/200),
 * chosen so a run is a verdict and not a coin toss:
 *   #forage -> c->ForageItem(true)  (gm_commands/forage.cpp:22): the
 *              `guarantee` half of the roll at forage.cpp:458 -- no class or
 *              skill check at all (forage.cpp:442-458), and the common-food
 *              fallback (forage.cpp:446-470) lands an item even in a zone
 *              with no forage rows. The SERVER sends the reply; it never
 *              touches the client's possession gate
 *              (skill_manager.cpp:405-410), so a warrior can be foraged.
 *   #fish   -> c->GoFish(true, false) (gm_commands/fish.cpp:22): the
 *              guarantee half of forage.cpp:302, but CanFish() (forage.cpp:
 *              264, :185-204) still needs a pole in primary + bait in a
 *              worn/personal slot, so the fishing leg summons both
 *              (#summonitem) and seats them.
 *
 * Fishing is ALSO driven through the client's own `/useskill fishing` path
 * (criterion 2: "fish ... succeeds" -- the OP_Fording the client sends for
 * skill 55) with the server's guaranteed #fish behind it. That client-side
 * cast is best-effort too: if the client's own precondition gate
 * (forageOrFishingRefusal) or the server's "already fishing" refuses it, the
 * #fish still lands the catch.
 *
 * The cursor choreography matters. #forage leaves its catch ON THE CURSOR,
 * and #summonitem also lands on the cursor, so the foraged item is stashed
 * into general 1 (/putdown 23) BEFORE the pole is summoned; otherwise the
 * pole's putdown would swap with the foraged food instead of reaching
 * primary. The pole then goes straight to primary (/putdown 13) and the
 * bait to general 2 (/putdown 24) -- CanFish's HasItemByUse(...Worn|
 * Personal) (forage.cpp:188) sees general slots, only the pole must reach
 * primary. The forage landing is detected STICKY as early as it is seen
 * (new cursor item + a "scrounged" line), so stashing it later does not
 * un-prove it. serverslot (inventory.slot_id) numbering throughout -- the
 * /putdown and /equip contract (T494): 13 = SLOT_PRIMARY, 23/24 = general
 * 1/2, 33 = cursor.
 *
 * GM invulnerability can refuse self-affecting server actions, so the very
 * first prep line turns it off (the same reason cast_lands' prep leads
 * with it).
 */
class ForageLandsScenario final : public IScenario {
public:
    ForageLandsScenario() = default;

    const char* Name() const override { return "forage_lands"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!baselineTaken_) {
            baselineCursor_ = world.ClientItemAt(33);
            baselineTaken_ = true;
        }

        // The forage catch is detected STICKY, as early as it is seen:
        // once a "scrounged" line and a NEW cursor item are both present the
        // leg is proven, and stashing the food later does not un-prove it.
        if (!forageLanded_ && forageAt_ > 0.0 &&
            t >= forageAt_ + kForageCheckDelay && ForageLanded(world)) {
            forageLanded_ = true;
            world.Note("forage landing at t=" + std::to_string(t));
        }

        // Prep + triggers, one command per window, in order. Every one is a
        // server round trip; the windows are deliberately generous.
        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            if (step.command.find("#forage") != std::string::npos) {
                forageAt_ = t;
            }
            if (step.command.find("#fish") != std::string::npos) {
                fishAt_ = t;
                // By here the foraged food is stashed (general 1) and the
                // pole/bait are seated (primary/general 2), so the cursor is
                // clear again: the catch must be a NEW cursor item.
                fishCursorBaseline_ = world.ClientItemAt(33);
            }
            return StepResult::Running;
        }

        // The forage leg is the HARD one and its window closes on its own
        // clock (trigger t=8 + 45s = t=53), before the fish trigger (t=58).
        if (!forageLanded_ && forageAt_ > 0.0 &&
            t >= forageAt_ + kForageWindow) {
            std::ostringstream os;
            os << "no forage landing " << (t - forageAt_)
               << "s after #forage (cursor=" << CursorNow(world)
               << ", scrounged lines=" << CountLines(world, kForageMarks, 1)
               << ")";
            SetReason(os.str());
            return StepResult::Fail;
        }

        // The fishing leg's window: report its outcome and close the run.
        if (forageLanded_ && !fishChecked_ && fishAt_ > 0.0 &&
            t >= fishAt_ + kFishWindow) {
            fishChecked_ = true;
            std::ostringstream os;
            os << "forage landed (scrounged line + item on cursor); fishing "
               << "leg: " << (FishLanded(world) ? "landed"
                                               : "no landing (best-effort)")
               << " by t=" << t;
            SetReason(os.str());
            return StepResult::Pass;
        }

        return StepResult::Running;
    }

private:
    // Timeline (seconds since zone-in).
    static constexpr double kInvulnOffAt   = 3.0;
    static constexpr double kShoreGotoAt   = 5.0;   // #goto the harbor shore
    static constexpr double kForageAt      = 8.0;
    static constexpr double kStashForageAt = 18.0;  // /putdown 23 (general 1)
    static constexpr double kPoleSummonAt  = 24.0;
    static constexpr double kPoleSeatAt    = 28.0;  // /putdown 13 (primary)
    static constexpr double kBaitSummonAt  = 32.0;
    static constexpr double kBaitSeatAt    = 36.0;  // /putdown 24 (general 2)
    static constexpr double kMaxSkillsAt   = 42.0;
    static constexpr double kUseSkillAt    = 48.0;  // /useskill fishing
    static constexpr double kFishAt        = 58.0;  // #fish (guaranteed)

    // Windows.
    static constexpr double kForageCheckDelay = 3.0;  // detect from t=11
    static constexpr double kForageWindow     = 45.0; // hard fail by t=53
    static constexpr double kFishWindow       = 90.0; // report by t=148

    struct PrepStep {
        double at;
        std::string command;
    };

    static std::vector<PrepStep> MakePrep() {
        // #summonitem puts the item on the CURSOR (gm_commands/summonitem.
        // cpp), so each summon is followed by a /putdown that seats it. The
        // foraged catch is stashed into general 1 FIRST (it is the cursor's
        // front item at that point, so /putdown 23 takes the food, not the
        // pole).
        //
        // #goto first: the fishing leg is WATER-DEPENDENT. The server's
        // CanFish() (forage.cpp:206-247) probes a rod tip 30u ahead along the
        // heading at the player's z, then a 100u vertical line down: no water
        // in reach is eqstr 166 FISHING_LAND ("Trying to catch land sharks
        // perhaps?"), the refusal the pre-run lane runs measured (the run at
        // heading 0 cast the rod NORTH to dry land). #goto's heading is the
        // raw wire value (gm_commands/goto.cpp:76-77 -> ProcessMovePC), and
        // CanFish converts it (forage.cpp:214): degrees = hu*360/512. The
        // stand is parsed from the lane's own freporte watermap
        // (freporte_lane3.wtr, md5 f52d9aa088; probes wtr_headings.py /
        // wtr_cols.py in /home/user/kb-evidence/b894-forage-fishing): the
        // harbor's water is SOUTH of the dock (y ~ -855, surface z ~ -74.4),
        // so the stand sits at (0,-825,-74) -- the server-stored zone-in z --
        // with heading 256 (180deg, rod tip (0,-855), water depth ~10u; the
        // FindBestZ 100u gate holds: the seabed is only ~10u below the
        // player's z). #goto x y z h (gm_commands/goto.cpp:62-75).
        return {
            {kInvulnOffAt, "/say #set invulnerable off"},
            {kShoreGotoAt, "/say #goto 0 -825 -74 256"},
            {kForageAt, "/say #forage"},
            {kStashForageAt, "/putdown 23"},  // foraged food -> general 1
            {kPoleSummonAt, "/say #summonitem 13100 1"},  // Fishing Pole (36)
            {kPoleSeatAt, "/putdown 13"},     // pole -> primary
            {kBaitSummonAt, "/say #summonitem 13101 1"},  // Fishing Bait (37)
            {kBaitSeatAt, "/putdown 24"},     // bait -> general 2
            {kMaxSkillsAt, "/say #maxskills"},  // all skills 100
            {kUseSkillAt, "/useskill fishing"},
            {kFishAt, "/say #fish"},
        };
    }

    static constexpr const char* const kForageMarks[] = {
        "scrounged",  // 152/153/154/150/151: "You have scrounged up ..."
    };
    static constexpr const char* const kFishMarks[] = {
        "caught",  // 170 "You caught, something..." / 421 "You caught %1!"
    };

    bool ChatHas(const IScenarioWorld& world, const char* const* marks,
                 size_t markCount) const {
        const size_t n = world.ChatLineCount();
        for (size_t i = 0; i < n; ++i) {
            const std::string line = world.ChatLine(i);
            for (size_t m = 0; m < markCount; ++m) {
                if (line.find(marks[m]) != std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    }

    int CountLines(const IScenarioWorld& world, const char* const* marks,
                   size_t markCount) const {
        const size_t n = world.ChatLineCount();
        int count = 0;
        for (size_t i = 0; i < n; ++i) {
            const std::string line = world.ChatLine(i);
            for (size_t m = 0; m < markCount; ++m) {
                if (line.find(marks[m]) != std::string::npos) {
                    ++count;
                    break;
                }
            }
        }
        return count;
    }

    long long CursorNow(const IScenarioWorld& world) const {
        const long long cur = world.ClientItemAt(33);
        return cur < 0 ? baselineCursor_ : cur;
    }

    bool ForageLanded(const IScenarioWorld& world) const {
        const long long cur = world.ClientItemAt(33);
        const bool item = cur > 0 && cur != baselineCursor_;
        return item && ChatHas(world, kForageMarks, 1);
    }

    bool FishLanded(const IScenarioWorld& world) const {
        const long long cur = world.ClientItemAt(33);
        const long long base =
            fishCursorBaseline_ < 0 ? baselineCursor_ : fishCursorBaseline_;
        const bool item = cur > 0 && cur != base;
        return item && ChatHas(world, kFishMarks, 1);
    }

    bool baselineTaken_ = false;
    long long baselineCursor_ = -1;
    bool forageLanded_ = false;
    bool fishChecked_ = false;
    double forageAt_ = -1.0;
    double fishAt_ = -1.0;
    long long fishCursorBaseline_ = -1;
    std::vector<PrepStep> prep_ = MakePrep();
    size_t prepIndex_ = 0;
};

}  // namespace

std::unique_ptr<IScenario> MakeForageLandsScenario(const ScenarioOptions&) {
    return std::unique_ptr<IScenario>(new ForageLandsScenario());
}

}  // namespace scenario
}  // namespace eqt
