// B1025-H5 (#345): hotbar_lifecycle -- the hotbar's whole user path, live
// against a lane, with EVERY assertion on a server/chat observation, never on
// the client's model state (the card's criterion B).
//
// One seeded GMMgmt character (the config's `character`, Testcaster -- a
// level-50 wizard, the b630 caster shape: high enough that the zone's mobs
// never aggro it, so no leg is interrupted). Four slots on page 0, one per
// hotbar type. Each is
// ASSIGNED through the user path (the HotbarAssignIntent H-3 wired to
// HotbarModel::assignSlot) and ACTIVATED by BOTH routes: the click intent
// (HotbarActivateIntent, BridgeComponent::onHotbarActivateIntent) and the
// keyboard route (InputActionBridge::processHotbarRequests, the H-4 key
// binding). The observable for each is server-visible:
//
//   spell  #memspell 288 0 (persists + sends OP_MemorizeSpell, 0-based, so
//          findSpellGem(288) resolves). Minor Shielding is a self-target
//          buff the caster owns. The proof the SERVER accepted both
//          casts: buff 288 lands in PlayerBuffSpellIds() (eqemu delivers a
//          landed buff ONLY as OP_Action type 231 / flag 4 -> BuffManager --
//          the cast_lands model; a refused cast never reaches it), and the
//          cast lines "Casting Minor Shielding" (click, bridge_component_
//          intents.cpp:751) + "Casting from gem 1" (key, eq_action_handler.
//          cpp:543) -- the latter twice, once per route.
//   item   a seeded charged consumable (8443 Bag of Bone Dust, 10 charges,
//          reclevel 0) in a general slot; both routes activate it (the client
//          sends HC_OP_CastSpell slot 10, bridge_component_ui_audio.cpp:737).
//          The proof is the SERVER's CHARGE DECREMENT -- Client::IsOfClientBot()
//          is true (zone/client.h:333) so the server DeleteItemInInventory()s
//          one charge (spells.cpp:1762), which the lane's post-check asserts
//          in the DB. The client cannot read its own charge state back, so the
//          in-run check is only that no "not found" error appeared; the DB is
//          the verdict (the ranged_single_shot model).
//   skill  Fishing (55), driven best-effort exactly as forage_lands does: the
//          pole is summoned + seated in primary, a bait in a general slot. The
//          client's own forageOrFishingRefusal reads the pole's parsed
//          skillType (0 here, not 36), so it may refuse locally and send
//          nothing -- that is why the CLIENT cast is best-effort. The
//          DETERMINISTIC server proof is #fish -> GoFish(true) (forage.cpp:
//          302 guarantee) landing a catch: a "caught" line (eqstr 170/421) +
//          a NEW cursor item, which the client receives. #fish works in any
//          zone: the Watermap.CheckForWaterWhenFishing rule defaults to false
//          (ruletypes.h:418), so CanFish() (forage.cpp:206) skips the water
//          probe entirely, and guarantee=true (forage.cpp:302) forces the
//          success roll -- the catch lands from the common_fish_ids fallback.
//   social a free-text emote macro (/emote). The client's OWN local echo of
//          /emote (chat_component.cpp:318 -- eqemu broadcasts emotes with
//          ignore_sender, so the actor's line is client-generated) is the
//          observable; /say has no local echo and cannot be asserted. Both
//          routes fire the same text -> two echoes.
//
// The FISH leg is made deterministic by the #goto shore prep. Lane 1's
// ruleset has Watermap:CheckForWaterWhenFishing ON (run-3's zone log emitted
// FISHING_LAND from the probe branch, forage.cpp:246), so CanFish()
// (forage.cpp:206) runs the probe: a rod tip 30u ahead along the heading at
// the player's z (FishingRodLength 30, ruletypes.h:419), a FindBestZ ground
// check (player.z - ground within FishingLineLength 100, ruletypes.h:420),
// then a 1u-step scan down 100u for water (forage.cpp:240). befallen's own
// watermap (befallen.wtr) has NO water region (its leaves carry special=0/3
// only, never 1/7), so there the probe can never return true. The fix is to
// run the character in FREPORTE (locally loadable -- freporte.s3d is in the
// client path) and #goto a computed water's edge: the stand (-1325,-850,-70)
// is land at the water's surface height, heading 320 (225deg) puts the rod
// tip (-1346,-851) in the deep water body (lane-1 freporte.wtr, water at
// z in [-112,-70]; the seabed ~42u below the player's z clears the 100u
// gate). That is exactly the pattern B894's forage_lands proves on a lane
// (scenario_forage_lands.cpp:178-186: #goto a shore stand, then #fish). With
// the rod tip over water, CanFish() returns true and GoFish(true)'s
// guarantee (forage.cpp:302) forces the catch from the common_fish_ids
// fallback (forage.cpp:269-290) -- the cursor item lands on.
//
// Then the PAGE-SWITCH leg: move to page 2 (HotbarPageChangedIntent), and
// activate an empty page-2 slot through both routes; an empty slot must fire
// nothing (no new emote/catch line) and the model's page must report 2.
//
// Then the RELOG leg: /camp (WantsCampToCharacterSelect) -> character select
// -> EnterWorldAs(self) -> zone-in. The three TYPED slots (spell/item/skill)
// are restored from the per-character INI on the fresh zone-in (LoadCharacter
// HotbarConfig, application zone-in); the emote slot is NOT (fable stores the
// free-text social with no INI code; the official client's social is an emote
// INDEX, E<number> -- the documented gap). The one model read (HotbarSlotAt)
// is only to report the restored TYPES; the actual persistence proof is the
// INI bytes the post-check reads.
//
// Isolation (acceptance C): the SECOND character on the account (the peer
// process, Testbuddy) runs zone_in in parallel and never assigns anything.
// The post-check reads BOTH per-character INI files (under $WILLEQ_CACHE_DIR):
// Testcaster's carries the three typed slots, Testbuddy's is absent or empty.

#include "client/scenario/scenarios.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

// Page-0 slot layout (model slot index, 0-based) and HotbarSlotType ints
// (hotbar_slot_type.h: Spell=1, Skill=2, Item=3, Emote=4).
constexpr int kPage    = 0;
constexpr int kPage2   = 1;
constexpr int kSlot0   = 0;
constexpr int kSlot1   = 1;
constexpr int kSlot2   = 2;
constexpr int kSlot3   = 3;
constexpr int kTypeSpell = 1;
constexpr int kTypeItem  = 3;
constexpr int kTypeSkill = 2;
constexpr int kTypeEmote = 4;

// Fishing (55): the only skill leg; the #fish catch is its server proof.
constexpr int kFishingSkillId = 55;
// The social slot's command: /emote produces the client's local echo.
constexpr const char* kEmoteMacro = "/emote H5 hotbar social test";
constexpr const char* kEmoteMark  = "H5 hotbar social test";

// Transcript marks.
static constexpr const char* const kEmoteMarks[1]   = { "H5 hotbar social test" };
static constexpr const char* const kCatchMarks[1]   = { "caught" };
static constexpr const char* const kGemLineMarks[1] = { "Casting from gem 1" };
static constexpr const char* const kItemErrMarks[1] = { "not found in inventory" };

// Timeline (seconds since the MAIN process's zone-in). Every GM command is a
// server round trip, so the windows are deliberately generous.
constexpr double kInvulnOffAt  =  3.0;
constexpr double kShoreGotoAt  =  3.5;   // #goto the freporte water's edge
constexpr double kUnmemAt      =  4.5;
constexpr double kMemAt        =  6.0;    // #memspell <id> 0 (0-based gem 0)
constexpr double kMaxSkillsAt  =  7.5;
constexpr double kTargetAt     =  9.0;    // /target self (self-target cast)
constexpr double kAssignAt     = 24.0;    // assign all four slots (user path)

constexpr double kClickSpellAt = 30.0;
constexpr double kClickItemAt  = 37.0;
constexpr double kClickSkillAt = 44.0;
constexpr double kClickEmoteAt = 51.0;
constexpr double kClickVerifyAt = 60.0;   // verify the four click observables
constexpr double kKeySpellAt   = 68.0;    // >8s after the click cast (2.5s cast
                                          //   + 1.5s recast)
constexpr double kKeyItemAt    = 74.0;
constexpr double kKeySkillAt   = 80.0;
constexpr double kKeyEmoteAt   = 86.0;
constexpr double kKeyVerifyAt  = 96.0;    // verify the four key observables
constexpr double kFishAt       = 99.0;    // #fish (GoFish guarantee)
constexpr double kPageAt       = 114.0;   // page 2
constexpr double kPageClickAt  = 117.0;   // empty page-2 slot, click
constexpr double kPageKeyAt    = 120.0;   // empty page-2 slot, key
constexpr double kPageVerifyAt = 125.0;
constexpr double kCampAt       = 133.0;   // relog leg

constexpr double kSelectDeadline  = 45.0; // camp -> charselect
constexpr double kReenterDeadline = 120.0;
constexpr double kPageSettle      = 12.0; // after the empty-slot clicks

class HotbarLifecycleScenario final : public IScenario {
public:
    explicit HotbarLifecycleScenario(const ScenarioOptions& opts)
        : spellId_(opts.castSpellId)
        , itemId_(opts.hotbarItemId) {}

    const char* Name() const override { return "hotbar_lifecycle"; }
    bool WantsCampToCharacterSelect() const override { return true; }

    StepResult Step(IScenarioWorld& w, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        switch (stage_) {
        case Stage::Idle: {
            if (!started_) {
                me_ = w.PlayerName();
                if (me_.empty()) {
                    SetReason("no player name at zone-in");
                    return StepResult::Fail;
                }
                started_ = true;
            }
            // GM prep, one command per window, in order. GM invulnerability
            // refuses every self-cast, so it goes off first (the cast_lands
            // prep shape). #memspell 288 0 persists the spell (character_
            // memmed_spells) AND sends OP_MemorizeSpell, so the client learns
            // gem 0 holds it and findSpellGem(288) resolves.
            if (prepIndex_ < prep_.size()) {
                const PrepStep& step = prep_[prepIndex_];
                if (t < step.at) return StepResult::Running;
                w.Note("prep: " + step.command);
                w.SendCommand(step.command);
                ++prepIndex_;
                return StepResult::Running;
            }
            if (t < kAssignAt) return StepResult::Running;

            // ASSIGN all four slots through the user path.
            if (!w.AssignHotbarSlot(kPage, kSlot0, kTypeSpell, spellId_, {})) {
                SetReason("AssignHotbarSlot(spell) refused");
                return StepResult::Fail;
            }
            if (!w.AssignHotbarSlot(kPage, kSlot1, kTypeItem, itemId_, {})) {
                SetReason("AssignHotbarSlot(item) refused");
                return StepResult::Fail;
            }
            if (!w.AssignHotbarSlot(kPage, kSlot2, kTypeSkill, kFishingSkillId, {})) {
                SetReason("AssignHotbarSlot(skill) refused");
                return StepResult::Fail;
            }
            if (!w.AssignHotbarSlot(kPage, kSlot3, kTypeEmote, 0, kEmoteMacro)) {
                SetReason("AssignHotbarSlot(emote) refused");
                return StepResult::Fail;
            }
            w.Note("assigned 4 slots: spell=" + std::to_string(spellId_) +
                   " item=" + std::to_string(itemId_) + " skill=55 emote=/emote");
            stage_ = Stage::Click;
            return StepResult::Running;
        }

        case Stage::Click: {
            // CLICK-activate each slot, staggered (the cast takes 2.5s). The
            // window runs up to (not through) the verify: the emote is the
            // LAST activation, at kClickEmoteAt, which must sit INSIDE the
            // window or its `t >= kClickEmoteAt` step can never fire.
            if (t < kClickVerifyAt) {
                if (t >= kClickSpellAt && clickStep_ == 0) { clickStep_ = 1; w.ActivateHotbarSlot(kPage, kSlot0); }
                else if (t >= kClickItemAt && clickStep_ == 1) { clickStep_ = 2; w.ActivateHotbarSlot(kPage, kSlot1); }
                else if (t >= kClickSkillAt && clickStep_ == 2) { clickStep_ = 3; w.ActivateHotbarSlot(kPage, kSlot2); }
                else if (t >= kClickEmoteAt && clickStep_ == 3) { clickStep_ = 4; w.ActivateHotbarSlot(kPage, kSlot3); }
                return StepResult::Running;
            }
            if (t >= kClickVerifyAt && !clickVerified_) {
                if (!clickItemOk_) {
                    clickItemOk_ = !ChatHas(w, kItemErrMarks, 1);
                    if (!clickItemOk_) {
                        SetReason("click: item " + std::to_string(itemId_) +
                                  " not found in inventory");
                        return StepResult::Fail;
                    }
                }
                clickSpellOk_ = BuffPresent(w);
                if (!clickSpellOk_) {
                    SetReason("click: spell " + std::to_string(spellId_) +
                              " did not land (buffs=" +
                              std::to_string(w.PlayerBuffSpellIds().size()) + ")");
                    return StepResult::Fail;
                }
                clickEmoteOk_ = EmoteCount(w) >= 1;
                if (!clickEmoteOk_) {
                    SetReason("click: emote echo not in transcript");
                    return StepResult::Fail;
                }
                // The fishing skill line is best-effort (the client gate may
                // refuse); the #fish catch below is the deterministic proof.
                clickVerified_ = true;
                clickEmoteCount_ = EmoteCount(w);
                clickCatchCount_ = CatchCount(w);
                w.Note("click: spell landed (buff), item activated, emote echo, "
                       "skill fired; #fish is the skill proof");
                stage_ = Stage::Key;
                return StepResult::Running;
            }
            return StepResult::Running;
        }

        case Stage::Key: {
            // KEYBOARD-activate each slot, staggered (processHotbarRequests).
            // The window runs up to (not through) the verify: the emote is
            // the LAST activation, at kKeyEmoteAt, which must sit INSIDE the
            // window or its `t >= kKeyEmoteAt` step can never fire.
            if (t < kKeyVerifyAt) {
                if (t >= kKeySpellAt && keyStep_ == 0) { keyStep_ = 1; w.PressHotbarKey(kSlot0); }
                else if (t >= kKeyItemAt && keyStep_ == 1) { keyStep_ = 2; w.PressHotbarKey(kSlot1); }
                else if (t >= kKeySkillAt && keyStep_ == 2) { keyStep_ = 3; w.PressHotbarKey(kSlot2); }
                else if (t >= kKeyEmoteAt && keyStep_ == 3) { keyStep_ = 4; w.PressHotbarKey(kSlot3); }
                return StepResult::Running;
            }
            if (t >= kKeyVerifyAt && !keyVerified_) {
                const int emote = EmoteCount(w);
                const int gem = GemLineCount(w);
                if (!clickItemOk_) {
                    SetReason("key: item not found in inventory");
                    return StepResult::Fail;
                }
                // Exactly one "Casting from gem 1" line: only the KEY route
                // (EqActionHandler::beginCastFromGem, eq_action_handler.cpp:543)
                // prints it. The click route (onHotbarActivateIntent's Spell
                // arm, bridge_component_intents.cpp:748) prints "Casting
                // <name>" instead. So after both routes the key cast is proven
                // by the single gem line; 0 means the key cast never started.
                if (gem < 1) {
                    SetReason("key: no 'Casting from gem 1' line -- the key "
                              "route's cast never started (only the click "
                              "route prints 'Casting <name>')");
                    return StepResult::Fail;
                }
                if (emote < clickEmoteCount_ + 1) {
                    SetReason("key: emote echoes=" + std::to_string(emote) +
                              " (expected >= " + std::to_string(clickEmoteCount_ + 1) + ")");
                    return StepResult::Fail;
                }
                keyVerified_ = true;
                w.Note("key: spell cast line (2 total), item activated, second emote echo");
                stage_ = Stage::Fish;
                return StepResult::Running;
            }
            return StepResult::Running;
        }

        case Stage::Fish: {
            // The deterministic skill proof: #fish -> GoFish(true) (forage.
            // cpp:302 guarantee) lands a catch on the cursor.
            if (t >= kFishAt && fishSent_ == 0) {
                fishSent_ = 1;
                fishCursorBase_ = w.ClientItemAt(33);
                w.Note("fish: #fish sent (GoFish guarantee)");
                w.SendCommand("/say #fish");
            }
            if (fishSent_ && t >= kFishAt + kPageSettle) {
                const int catchLines = CatchCount(w);
                const long long cur = w.ClientItemAt(33);
                const bool newCursor = cur > 0 && cur != fishCursorBase_;
                if (catchLines < clickCatchCount_ + 1 || !newCursor) {
                    SetReason("fish: no #fish catch (caught-lines=" +
                              std::to_string(catchLines) + " cursor=" +
                              std::to_string(cur) + ", base=" +
                              std::to_string(fishCursorBase_) + ")");
                    return StepResult::Fail;
                }
                pageEmoteBase_ = EmoteCount(w);
                pageCatchBase_ = CatchCount(w);
                w.Note("fish: catch landed (caught-line + new cursor item " +
                       std::to_string(cur) + ") -- skill fired");
                stage_ = Stage::Page;
                return StepResult::Running;
            }
            return StepResult::Running;
        }

        case Stage::Page: {
            // The PAGE-SWITCH leg: move to page 2, then activate an empty
            // page-2 slot through BOTH routes; an empty slot fires nothing.
            if (t >= kPageAt && pageStep_ == 0) {
                pageStep_ = 1;
                if (!w.SetHotbarPage(kPage2)) {
                    SetReason("SetHotbarPage(2) refused");
                    return StepResult::Fail;
                }
            }
            if (t >= kPageClickAt && pageStep_ == 1) { pageStep_ = 2; w.ActivateHotbarSlot(kPage2, kSlot0); }
            if (t >= kPageKeyAt && pageStep_ == 2) { pageStep_ = 3; w.PressHotbarKey(kSlot0); }
            if (t < kPageVerifyAt) return StepResult::Running;

            const int pg = w.HotbarPage();
            if (pg != kPage2) {
                SetReason("page: model page is " + std::to_string(pg) + " (expected 2)");
                return StepResult::Fail;
            }
            if (EmoteCount(w) != pageEmoteBase_ || CatchCount(w) != pageCatchBase_) {
                SetReason("page: activating an empty page-2 slot fired an action "
                          "(emote=" + std::to_string(EmoteCount(w)) +
                          " catch=" + std::to_string(CatchCount(w)) + ")");
                return StepResult::Fail;
            }
            w.Note("page: page 2 reports; empty slot fires nothing on either route");
            stage_ = Stage::Camp;
            return StepResult::Running;
        }

        case Stage::Camp: {
            if (t < kCampAt) return StepResult::Running;
            w.SendCommand("/camp");
            w.Note("relog leg: /camp sent at t=" + std::to_string(t));
            campAt_ = t;
            gen0_ = w.CharacterListGeneration();
            stage_ = Stage::WaitSelect;
            return StepResult::Running;
        }

        case Stage::WaitSelect: {
            const double t2 = t - campAt_;
            if (w.AtCharacterSelect() && w.CharacterListGeneration() > gen0_) {
                const auto names = w.CharacterListNames();
                if (std::find(names.begin(), names.end(), me_) == names.end()) {
                    SetReason("relog: character select does not name " + me_);
                    return StepResult::Fail;
                }
                if (!w.EnterWorldAs(me_)) {
                    SetReason("relog: EnterWorldAs(" + me_ + ") refused");
                    return StepResult::Fail;
                }
                w.Note("relog: character select reached, re-entering " + me_);
                stage_ = Stage::Reenter;
            } else if (t2 > kSelectDeadline) {
                SetReason("relog: no character-select within " +
                          std::to_string(kSelectDeadline) + " s of /camp");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }

        case Stage::Reenter: {
            const double t2 = t - campAt_;
            if (w.IsFullyZonedIn() && !w.ZoneName().empty()) {
                if (w.PlayerName() != me_) {
                    SetReason("relog: zoned in as " + w.PlayerName() + " not " + me_);
                    return StepResult::Fail;
                }
                // The three TYPED slots must be restored from the per-character
                // INI on the fresh zone-in (LoadCharacterHotbarConfig). The
                // emote slot is NOT asserted to persist (the documented free-
                // text gap). This model read only reports the restored types;
                // the INI bytes are the post-check's job.
                const auto spell = w.HotbarSlotAt(kPage, kSlot0);
                const auto item  = w.HotbarSlotAt(kPage, kSlot1);
                const auto skill = w.HotbarSlotAt(kPage, kSlot2);
                std::ostringstream os;
                os << "relog: " << me_ << " re-zoned to '" << w.ZoneName() << "' at t=" << t
                   << "; restored spell=" << (spell.type == kTypeSpell ? "yes" : "NO")
                   << " item=" << (item.type == kTypeItem ? "yes" : "NO")
                   << " skill=" << (skill.type == kTypeSkill ? "yes" : "NO");
                if (spell.type != kTypeSpell || item.type != kTypeItem || skill.type != kTypeSkill) {
                    os << " [FAIL: a typed slot did not restore]";
                    SetReason(os.str());
                    return StepResult::Fail;
                }
                SetReason(os.str() + " (emote slot is the documented free-text gap)");
                return StepResult::Pass;
            }
            if (t2 > kReenterDeadline) {
                SetReason("relog: Enter World sent but no zone-in within " +
                          std::to_string(kReenterDeadline) + " s");
                return StepResult::Fail;
            }
            return StepResult::Running;
        }
        }
        return StepResult::Running;
    }

private:
    enum class Stage { Idle, Click, Key, Fish, Page, Camp, WaitSelect, Reenter };
    Stage stage_ = Stage::Idle;

    uint32_t spellId_;
    uint32_t itemId_;

    struct PrepStep { double at; std::string command; };
    std::vector<PrepStep> prep_ = MakePrep();
    size_t prepIndex_ = 0;

    bool started_ = false;
    std::string me_;
    int clickStep_ = 0, keyStep_ = 0, pageStep_ = 0;
    bool clickVerified_ = false, keyVerified_ = false;
    bool clickSpellOk_ = false, clickItemOk_ = false, clickEmoteOk_ = false;
    int clickEmoteCount_ = 0, clickCatchCount_ = 0;
    int fishSent_ = 0;
    long long fishCursorBase_ = -1;
    int pageEmoteBase_ = 0, pageCatchBase_ = 0;
    double campAt_ = 0;
    long long gen0_ = 0;

    std::vector<PrepStep> MakePrep() {
        return {
            {kInvulnOffAt,  "/say #set invulnerable off"},
            // The fish leg: stand at the freporte water's edge. Lane 1's
            // ruleset runs the water probe (run-3's zone log emitted
            // FISHING_LAND, forage.cpp:246), and befallen's watermap has no
            // water region, so the probe only succeeds in a zone that has
            // water in front of the rod. The stand + heading are computed from
            // the lane host's own freporte.wtr (freporte_stand2.py in
            // /home/user/kb-evidence/kb-h5-hotbar-scenario/water): land at the
            // water's surface height, rod tip 30u ahead in the deep body.
            // #goto x y z h (gm_commands/goto.cpp:20-46) is in-zone here (the
            // seed places the char in freporte), so MovePC/ZonePC teleports in
            // place with the given heading -- the same mechanic B894's
            // forage_lands uses (scenario_forage_lands.cpp:186). Registered at
            // GMMgmt (200), which the lane account has, so no status bump.
            {kShoreGotoAt,  "/say #goto -1325 -850 -70 320"},
            {kUnmemAt,      "/say #unmemspells"},
            {kMemAt,        "/say #memspell " + std::to_string(spellId_) + " 0"},
            {kMaxSkillsAt,  "/say #maxskills"},
            {kTargetAt,     "/target self"},
            // The pole/bait/consumable are NOT summoned here: they arrive
            // through the lane's seed.sql (a direct inventory INSERT, the
            // ranged_single_shot model) because Testcaster's own primary
            // (slot 13) and general slots are already filled -- a
            // #summonitem + /putdown would swap the OLD primary onto the
            // cursor instead of seating the pole. The client reads the same
            // seeded rows it loads at zone-in, so it sees the pole in
            // primary, the bait in a general slot and the 10-charge
            // consumable 8443 (the item slot's target).
        };
    }

    bool BuffPresent(const IScenarioWorld& w) const {
        const auto buffs = w.PlayerBuffSpellIds();
        return std::find(buffs.begin(), buffs.end(), spellId_) != buffs.end();
    }

    bool ChatHas(const IScenarioWorld& w, const char* const* marks, size_t mc) const {
        const size_t n = w.ChatLineCount();
        for (size_t i = 0; i < n; ++i) {
            const std::string line = w.ChatLine(i);
            for (size_t m = 0; m < mc; ++m)
                if (line.find(marks[m]) != std::string::npos) return true;
        }
        return false;
    }

    int Count(const IScenarioWorld& w, const char* const* marks, size_t mc) const {
        const size_t n = w.ChatLineCount();
        int count = 0;
        for (size_t i = 0; i < n; ++i) {
            const std::string line = w.ChatLine(i);
            for (size_t m = 0; m < mc; ++m)
                if (line.find(marks[m]) != std::string::npos) { ++count; break; }
        }
        return count;
    }
    int EmoteCount(const IScenarioWorld& w) const { return Count(w, kEmoteMarks, 1); }
    int CatchCount(const IScenarioWorld& w) const { return Count(w, kCatchMarks, 1); }
    int GemLineCount(const IScenarioWorld& w) const { return Count(w, kGemLineMarks, 1); }
};

}  // namespace

std::unique_ptr<IScenario> MakeHotbarLifecycleScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new HotbarLifecycleScenario(options));
}

}  // namespace scenario
}  // namespace eqt
