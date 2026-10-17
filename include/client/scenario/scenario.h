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
     * B892 (#208): the client's OWN levitation -- any active buff whose
     * spell carries SPA 57, or a self flymode 2/5 the server sent. The
     * game thread publishes it as LevitationChanged; the real world reads
     * the MovementComponent's stored state. False by default (no reader).
     */
    virtual bool PlayerLevitating() const { return false; }
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

    // --- UIGAP-live observables (#151 #213 #214 #215 #211) ---
    //
    // These have DEFAULT bodies that answer "not readable" (-1 / empty) on
    // purpose: every older scenario and every fake keeps compiling unchanged,
    // and a scenario that needs one of them treats the default as "the world
    // cannot tell me" rather than as a value.

    /**
     * The item id THIS client's InventoryManager holds at `serverSlot`
     * (`inventory.slot_id` numbering: general 23-30, cursor 33, bag contents
     * 4010+, bank 2000+, world container 4000+), so a scenario and the
     * database speak one numbering. 33 is the cursor's FRONT item. 0 = empty,
     * -1 = not readable / not a slot this client has.
     */
    virtual long long ClientItemAt(int serverSlot) const { (void)serverSlot; return -1; }
    /** The stack count (charges) at `serverSlot`; 0 empty, -1 not readable. */
    virtual long long ClientChargesAt(int serverSlot) const { (void)serverSlot; return -1; }
    /** Items queued on the cursor, front included; -1 not readable. */
    virtual long long CursorQueueSize() const { return -1; }
    /** The skill value this client holds for `skillId`; -1 not readable. */
    virtual long long SkillValue(int skillId) const { (void)skillId; return -1; }
    /** The training points this client holds; -1 not readable. */
    virtual long long PracticePoints() const { return -1; }
    /**
     * Every line this client SHOWED in chat since the lane armed (system
     * lines and channel messages alike), oldest first, each as
     * "<channel>|<sender>|<text>". A line the client filtered out (the ignore
     * list) never appears here: this is the transcript a player would read.
     */
    virtual size_t ChatLineCount() const { return 0; }
    virtual std::string ChatLine(size_t index) const { (void)index; return std::string(); }
    /**
     * How many game events of `GameEventType` (its integer value) this client
     * published since the lane armed; -1 not readable. The scenario lane's
     * stand-in for "the packet behind that event arrived".
     */
    virtual long long EventCount(int gameEventType) const { (void)gameEventType; return -1; }
    /** The latest SkillValueChanged value seen for `skillId`; -1 none yet. */
    virtual long long LastSkillUpdateValue(int skillId) const { (void)skillId; return -1; }
    /** Rows in the latest TrackingListChanged event; -1 none yet. */
    virtual long long TrackingRowCount() const { return -1; }
    /**
     * The drop id of the nearest world object (OP_GroundSpawn) of
     * `objectType` (17 = forge), 0 when none is known.
     */
    virtual long long NearestWorldObjectId(int objectType) const { (void)objectType; return 0; }

    // --- UIGAP-live2 observables (#212 trade/give, #151 drop/destroy/coin,
    //     #26 bank change). Same "default = not readable" rule as above. ---

    /** TradeFlags() bits. */
    static constexpr int kTradeActive = 1;          // a trade or NPC give window is open
    static constexpr int kTradeOwnAccepted = 2;     // we pressed Trade/Give
    static constexpr int kTradePartnerAccepted = 4; // the partner pressed Trade
    static constexpr int kTradePartnerOffered = 8;  // the partner offered an item or coin
    static constexpr int kTradeWithNpc = 16;        // the window is an NPC give
    /** The trade session as this client holds it (bits above); -1 not readable. */
    virtual int TradeFlags() const { return -1; }
    /**
     * The first `inventory.slot_id` holding `itemId`: general 23-30, their bag
     * contents, the cursor 33 (front item), our trade slots 3000-3007. 0 = not
     * held anywhere there; -1 not readable.
     */
    virtual long long FindItemSlot(long long itemId) const { (void)itemId; return -1; }
    /** {pp, gp, sp, cp} of the purse (`bank` false) or the bank; empty = not readable. */
    virtual std::vector<long long> Coins(bool bank) const { (void)bank; return {}; }
    /** Coin held on the cursor, in copper; -1 not readable. */
    virtual long long CursorCoinCopper() const { return -1; }
    /** World objects (ground spawns, containers) this client knows; -1 not readable. */
    virtual long long WorldObjectCount() const { return -1; }

    // --- B1005 (#324): /camp and the character-select screen. Same
    //     "default = not readable" rule. ---

    /** CampPhase() values (eqt::player::CampPhase). */
    static constexpr int kCampIdle = 0;
    static constexpr int kCampCounting = 1;     // /camp typed, the countdown runs
    static constexpr int kCampLogoutSent = 2;   // countdown over, OP_Logout out
    static constexpr int kCampToSelect = 3;     // the server answered: back to select
    static constexpr int kCampToDesktop = 4;    // the server answered: the client exits
    /** The camp state machine's phase; -1 not readable. */
    virtual int CampPhase() const { return -1; }
    /**
     * The character-select state is up: the screen is on, the FRESH
     * OP_SendCharInfo after a camp has arrived, and no OP_EnterWorld is out.
     */
    virtual bool AtCharacterSelect() const { return false; }
    /** The names on the latest character list, slot order. */
    virtual std::vector<std::string> CharacterListNames() const { return {}; }
    /**
     * B1081 (#401): the character this run will enter the world as, BEFORE a
     * zone-in: the entry at the select screen's cursor on the character
     * list, or the run's configured character when the list is still owed.
     * "" when the world cannot know (the base). zone_change_no_flash reads
     * it in WaitList -- game-state's player name is empty until the zone
     * session is up, so PlayerName() is the wrong source there.
     */
    virtual std::string CharacterSelectName() const { return {}; }
    /** OP_SendCharInfo deliveries this session; -1 not readable. */
    virtual long long CharacterListGeneration() const { return -1; }
    /** Enter World for a listed name (the screen's button); false if refused. */
    virtual bool EnterWorldAs(const std::string& name) { (void)name; return false; }
    /** B1072: zone UDP sessions opened this run (ConnectToZone + retries); -1 not readable. */
    virtual long long ZoneConnectOpens() const { return -1; }
    /** B1072: the next `attempts` zone connects go to `port` on the zone host; false if unsupported. */
    virtual bool SimulateUnreachableZoneConnects(int attempts, int port) { (void)attempts; (void)port; return false; }

    // --- B1081 (#401): the zone-change flash. ---

    /**
     * B1081: CharacterSelectService::Activations() -- how many times the
     * character-select screen went UP (channel inactive -> active) this run;
     * -1 when the run has no service (no character-select channel). On a run
     * that draws the screen the render thread logs "[CHARSELECT] screen up"
     * at the same moments.
     */
    virtual long long CharacterSelectActivations() const { return -1; }

    // --- actions ---
    /** Dispatch one slash command line, exactly as --script would. */
    virtual void SendCommand(const std::string& rawLine) = 0;
    /** One line of scenario trace (MOD_SCENARIO, INFO). */
    virtual void Note(const std::string& line) = 0;
    /**
     * UIGAP-live: a synthetic LEFT (or RIGHT) click on an item-window slot,
     * `serverSlot` in `inventory.slot_id` numbering. It runs the same
     * CursorGesture::click() a window's slot arm runs (B442), with the slot
     * and cursor snapshots read off the model, so what reaches the game thread
     * is the CursorGestureIntent a real click pushes. Returns the
     * GestureAction as an int (eqt::inventory::GestureAction), -1 when the
     * world cannot click (the default).
     */
    virtual int ClickSlot(int serverSlot, bool rightButton, bool shift, bool ctrl) {
        (void)serverSlot; (void)rightButton; (void)shift; (void)ctrl;
        return -1;
    }
    /**
     * UIGAP-live2: a world click on the nearest entity whose name contains
     * `name` -- the TargetIntent the renderer pushes for a click on a body.
     * With an item or coin held the game thread turns it into a trade
     * (player) or a give (NPC), as a real click does (B896). False when no
     * such entity is known or the world cannot click (the default).
     */
    virtual bool ClickEntity(const std::string& name) { (void)name; return false; }
    /**
     * UIGAP-live2: a trade / give window button (TradeWindowIntent):
     * 0 = Trade / Give, 1 = Cancel, 2 = the coin row with coin held.
     */
    virtual bool TradeButton(int op) { (void)op; return false; }
    /**
     * UIGAP-live2: a cursor gesture that is not a slot click
     * (CursorGestureIntent): `op` is eqt::events::CursorGestureOp as an int --
     * CoinClick (3) with coinType 0 cp .. 3 pp, quantity (0 = the whole pile)
     * and coinSource (1 purse, 2 bank); RequestDrop (5) / ConfirmDrop (6) /
     * RequestDestroy (7) / ConfirmDestroy (8) with the item held.
     */
    virtual bool CursorGesture(int op, int coinType, int quantity, int coinSource) {
        (void)op; (void)coinType; (void)quantity; (void)coinSource;
        return false;
    }

    // --- B893: fall_damage ---
    //
    // The scenario's live observable is the player's CURRENT HP (eqemu
    // applies a non-GM fall's damage and SendHPUpdate()s it; a GM takes 1 HP
    // and gets the "Your GM status protects you from N points of ... " chat
    // line). The fall itself is driven by the world, not the client: a
    // headless run has no renderer free-fall integrator (the server-driven
    // pathing only drops the player along its path, and UpdateJump's bounded
    // arc always lands safely), so the scenario asks the world to run the
    // client's own fall-damage SEND PATH against a synthetic height. Both
    // have defaults -- an older fake answers "not readable" / no-op, which a
    // scenario that needs them treats as "the world can't do this".
    /**
     * The player's current HP; -1 not readable before zone-in.
     */
    virtual long long PlayerHP() const { return -1; }
    /**
     * Drive the client's fall-damage sender against a synthetic `fallHeight`
     * (eqt::jumpfall::damageForFallHeight + SendFallDamage, dmgtype 252). The
     * server then applies the damage (or, for a GM, echoes the would-be
     * amount and takes 1). True when the world sent it; false when the world
     * cannot (the default).
     */
    virtual bool SimulateFall(float fallHeight) { (void)fallHeight; return false; }
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
    /**
     * B888 (issue #204): create_character. The name --create-character made
     * and the zones its chosen home city may land in (data/char_create.tsv's
     * `city` row). Empty name = the scenario refuses (it has nothing to check).
     */
    std::string expectCharacterName;
    std::vector<std::string> expectHomeZones;
    /**
     * B893 (fall_damage): the synthetic drop height the scenario drives
     * through the client's own fall-damage sender. It must exceed
     * eqt::jumpfall::fall().safeHeight for the live leg to deal damage; the
     * default 20.0 does (safeHeight is 18.0 [MEASURED, lane-2 capture]; the
     * measured table interpolates to 2 points of damage at the default
     * height, B1011).
     */
    float fallDamageHeight = 20.0f;
    /**
     * B1081 (issue #401): zone_change_no_flash -- the zone id the GM
     * /gmzone teleports the character to. Must be a DIFFERENT zone than the
     * character's (a same-zone move would relocate in place, no world
     * reconnect at all); 0 = the scenario refuses (it has nothing to zone
     * to). A cross-zone #gmzone is the zone change that drives the client's
     * ProcessDeferredZoneChange (the same path a zone-line crossing or a
     * death-to-bind re-entry take), so it exercises the reconnect the fix
     * gates.
     */
    long long zoneChangeTargetZone = 0;
    /**
     * B893 (fall_damage): how long to wait for the server's HP change to
     * arrive after SimulateFall. The send is a single C->S packet; the
     * server's SendHPUpdate() round-trip is well under a second, but a
     * flaky lane should not pass on the first tick's un-echoed value.
     */
    double fallWaitSeconds = 8.0;
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

    /**
     * B1081 (#401): opt out of the runner's pre-flight wait. zone_change_no_
     * flash turns the character-select screen on (WantsCharacterSelectScreen)
     * and presses its own Enter World -- the client never auto-zones-in
     * while the screen is up (WorldProcessExpansionInfo), so waiting for
     * IsFullyZonedIn before the first Step would wait forever. It starts at
     * t=0 and drives its own zone-in.
     */
    virtual bool WantsPreFlight() const { return true; }

    /**
     * B1005 (#324): one last look after the client stopped for a reason that
     * was not this runner (ScenarioRunner::Conclude). Default: no verdict,
     * so an external quit still reads as the unfinished-run code. Only a
     * scenario whose subject IS the client exiting (camp_desktop) answers.
     */
    virtual StepResult OnClientStopped(IScenarioWorld& world, const ScenarioTick& tick) {
        (void)world; (void)tick;
        return StepResult::Running;
    }

    /**
     * B1005: a headless --scenario run has no character-select screen, so
     * /camp exits there. A scenario that plays the screen's part returns
     * true and the lane turns the camp return on for its run.
     */
    virtual bool WantsCampToCharacterSelect() const { return false; }

    /**
     * B1081 (#401): the lane turns the character-select screen ON for this
     * run (a headless --scenario run never shows it by default). zone_change_
     * no_flash needs it up for the first login -- so a zone change can then
     * be measured against it -- exactly as the camp scenarios opt the run in
     * for the camp return. The screen is then up for the FRESH login and the
     * whole first zone-in, and a zone change must not bring it back up.
     */
    virtual bool WantsCharacterSelectScreen() const { return false; }

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
