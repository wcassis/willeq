#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>  // T149: recipesFavorite() takes a recipe-id list
#include <glm/glm.hpp>
#include "client/hotbar/hotbar_slot_type.h"  // T239: activateHotbarCommandSlot

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {

/**
 * ChatChannel - Types of chat channels.
 */
enum class ChatChannel {
    Say,
    Shout,
    OOC,
    Auction,
    Tell,
    Group,
    Guild,
    Raid,
    Emote
};

/**
 * AppearanceChange - T153: the seven fields of a Titanium OP_FaceChange, each
 * optional.
 *
 * `/appearance` sets fields by name, but the wire struct is all-or-nothing:
 * eqemu's Handle_OP_FaceChange (zone/client_packet.cpp:6387) copies all seven
 * into the PlayerProfile and Save()s. So the unset fields have to be filled in
 * from the character's current appearance, and only the handler can see that
 * state -- the merge deliberately happens there, not here, and this type
 * carries "what the user asked to change" rather than "what goes on the wire".
 *
 * Note for the merge site: this client parses eyecolor1/eyecolor2 nowhere it
 * keeps them (EverQuest's PlayerProfile read drops them into unused locals at
 * eq.cpp:2097-2098, and Entity has no eyecolor fields at all), so they cannot
 * be preserved and default to 0. The handler prints the full seven-field body
 * it is about to send so nothing is changed silently.
 */
struct AppearanceChange {
    std::optional<uint8_t> haircolor;
    std::optional<uint8_t> beardcolor;
    std::optional<uint8_t> eyecolor1;
    std::optional<uint8_t> eyecolor2;
    std::optional<uint8_t> hairstyle;
    std::optional<uint8_t> beard;
    std::optional<uint8_t> face;

    bool empty() const {
        return !haircolor && !beardcolor && !eyecolor1 && !eyecolor2 &&
               !hairstyle && !beard && !face;
    }
};

/**
 * ActionResult - Result of an action attempt.
 */
struct ActionResult {
    bool success = false;
    std::string message;  // Error or status message

    static ActionResult Success(const std::string& msg = "") {
        return {true, msg};
    }

    static ActionResult Failure(const std::string& msg) {
        return {false, msg};
    }
};

/**
 * IActionHandler - Interface for objects that can execute game actions.
 *
 * This is typically implemented by the EverQuest class or a wrapper around it.
 * The ActionDispatcher delegates action execution to this interface.
 */
class IActionHandler {
public:
    virtual ~IActionHandler() = default;

    // ========== Movement ==========

    virtual void setHeading(float heading) = 0;
    virtual void jump() = 0;
    virtual void sit() = 0;
    virtual void stand() = 0;
    virtual void toggleAutorun() = 0;
    virtual void stopAllMovement() = 0;

    // Pathfinding movement
    virtual void moveToLocation(float x, float y, float z) = 0;
    virtual void moveToEntity(const std::string& name) = 0;
    virtual void followEntity(const std::string& name) = 0;
    virtual void stopFollow() = 0;

    // ========== Combat ==========

    virtual void targetEntity(uint16_t spawnId) = 0;
    // T230: targetEntityByName(name) was deleted from this interface -- it
    // was a second, divergent name resolver with no production caller. Name
    // resolution is CommandProcessor's job (EntityManager::findEntityByName);
    // the handler only ever receives a resolved spawn id.
    virtual void targetNearestPC() = 0;
    virtual void targetNearestNPC() = 0;
    virtual void cycleTargets() = 0;
    virtual void cycleTargetsReverse() = 0;
    virtual void clearTarget() = 0;
    virtual void targetSelf() = 0;
    virtual void targetGroupMember(int index) = 0;

    virtual void enableAutoAttack() = 0;
    virtual void disableAutoAttack() = 0;
    virtual void toggleAutoAttack() = 0;

    // T102: ported from game's Combat-category handlers
    // (eq_commands_movement.cpp/eq_commands.cpp). assist()/shieldTarget()/
    // duelRequest() take a resolved spawnId (0 = "use the live CombatManager
    // target", resolved inside the handler -- CommandProcessor does its own
    // by-name resolution via GameState::entities() first, matching game's
    // precedence, then falls back to 0 for "no name given").
    virtual void assist(uint16_t targetId) = 0;
    virtual void shieldTarget(uint16_t targetId) = 0;
    virtual void taunt() = 0;
    virtual void toggleAutoFire() = 0;
    // CombatManager's own m_enabled flag (Enable()/Disable()/IsEnabled()) --
    // NOT the same as auto-attack or setAutoHunting()'s m_auto_hunting_enabled,
    // both pre-existing and untouched. This is /combat on|off|<toggle>,
    // historically unreachable (T26) until T29's unified command channel.
    virtual void enableCombatManager() = 0;
    virtual void disableCombatManager() = 0;
    virtual void toggleCombatManager() = 0;

    // T102: dueling -- ported from game's duel_cmd/duelaccept_cmd/
    // dueldecline_cmd. duelAccept()/duelDecline() read EverQuest's own
    // pending-duel state (HasPendingDuel()/GetPendingDuelInitiatorId(),
    // both public) since GameState has no duel domain to project it into.
    virtual void duelRequest(uint16_t targetId) = 0;
    virtual void duelAccept() = 0;
    virtual void duelDecline() = 0;

    virtual void castSpell(uint8_t gemSlot) = 0;
    virtual void interruptCast() = 0;

    // T101: the REAL casting path, ported from game's /cast (eq_commands_
    // spells.cpp) -- routes through EQ::SpellManager::beginCastFromGem(),
    // NOT CombatManager (the subsystem castSpell() above
    // use). T97 found these are two different, unconnected casting
    // subsystems; see refactor/T101_migrate_spells_pet_family.md's Review.
    // gemSlot is 1-indexed (1-8), same convention as castSpell() above.
    virtual void beginCastFromGem(uint8_t gemSlot, uint16_t targetId) = 0;

    virtual void useSkill(uint32_t skillId) = 0;

    // ========== Interaction ==========

    virtual void hail() = 0;
    virtual void hailTarget() = 0;

    virtual void clickNearestDoor() = 0;

    virtual void lootCorpse(uint16_t corpseId) = 0;
    // T268: the loot window actions. corpseId 0 = the open window. Each
    // returns false when nothing was sent (no window open, no such slot /
    // item, nothing to take) so the dispatcher can refuse with a message
    // naming what was asked for (T178 standard).
    virtual bool lootItem(uint16_t corpseId, int16_t slot) = 0;
    virtual bool lootItemByName(const std::string& name) = 0;
    virtual bool lootAll(uint16_t corpseId) = 0;
    virtual bool closeLootWindow() = 0;

    virtual void consider() = 0;

    // ========== Chat ==========

    virtual void sendChatMessage(ChatChannel channel, const std::string& message) = 0;
    virtual void sendTell(const std::string& target, const std::string& message) = 0;
    virtual void replyToLastTell(const std::string& message) = 0;
    /**
     * T222: whether a tell has been received that /reply could answer.
     * Queried by ActionDispatcher::replyToLastTell() so the dispatcher can
     * refuse with a user-visible message instead of the handler silently
     * no-oping (deferred.md T100 entry).
     */
    virtual bool hasTellToReplyTo() const = 0;

    // ========== Group ==========

    virtual void inviteToGroup(const std::string& playerName) = 0;
    virtual void inviteTarget() = 0;
    virtual void acceptGroupInvite() = 0;
    virtual void declineGroupInvite() = 0;
    virtual void leaveGroup() = 0;

    // ========== Character State ==========

    virtual void setAFK(bool afk) = 0;
    virtual void setAnonymous(bool anon) = 0;

    // ========== Inventory ==========

    virtual void useItem(int16_t slot) = 0;

    // T239: the keyboard hotbar route for the three COMMAND slot types.
    // Emote/Macro carry command text (`text`, the slot's emoteText -- a line
    // exactly as the chat window would run it, "/wave", "/say hi"); Combat
    // carries nothing and toggles auto-attack. The handler owns the body
    // (EverQuest::ActivateHotbarCommandSlot, shared with the mouse path).
    virtual void activateHotbarCommandSlot(HotbarSlotType type, const std::string& text) = 0;

    // T230: the item-id -> inventory-slot hop the keyboard hotbar route needs.
    // A hotbar Item slot stores an ITEM id; useItem() takes an INVENTORY slot.
    // Returns eqt::inventory::SLOT_INVALID (-1) when the item is not in the
    // player's inventory (or there is no inventory). Same lookup the mouse
    // path performs (InventoryManager::findItemSlotByItemId, eq_bridge.cpp).
    virtual int16_t findItemSlotByItemId(uint32_t itemId) = 0;

    // T141: /equip's three input forms (slot-pair / item-name / list) all
    // arrive as one free-text string, same shape as chat's sendChatMessage
    // family -- the form dispatch is command-layer text parsing, not a
    // guard, so it stays bundled here rather than being pre-split into a
    // separate virtual per form. Delegates straight to
    // EverQuest::RequestEquip() (eq_commands.cpp), which does the dispatch
    // and calls InventoryManager for everything else -- see
    // refactor/T141_equip_extraction.md.
    virtual void equip(const std::string& args) = 0;

    // T273: /read's one free-text argument (slot number or item-name
    // substring), same shape as equip() above. Delegates straight to
    // EverQuest::RequestReadItem() (eq_commands.cpp), which resolves the
    // item against InventoryManager and pushes the kept ReadItemIntent.
    virtual void readItem(const std::string& args) = 0;

    // T276: /scribe -- EverQuest::RequestScribeItem() resolves the
    // carried scroll, refuses locally, and pushes ScribeSpellIntent.
    virtual void scribeItem(const std::string& args) = 0;

    // ========== Spellbook ==========

    virtual void forgetSpell(uint8_t gemSlot) = 0;

    // T101: ported from game's /mem and /gems (eq_commands_spells.cpp).
    // memorizeSpellByName() does the real name lookup (exact + partial +
    // multi-match disambiguation) against EQ::SpellManager's database --
    // (T264: the by-id memorizeSpell() that used to sit beside it had no
    // caller outside tests and was deleted.) listMemorizedSpells() reports
    // all 8 gems' real spell names
    // and states, replacing the "(Spell data not available)" stub that used
    // to sit behind this same dispatcher.
    virtual void memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) = 0;
    virtual void listMemorizedSpells() = 0;

    // ========== Buffs ==========

    // T277: click off a buff. `args` is a 1-based buff slot number or a
    // spell-name substring; resolution happens against the player's live
    // BuffManager buffs (EverQuest::RequestCancelBuff), which is the only
    // place that model exists -- GameState carries no buff list.
    virtual void cancelBuff(const std::string& args) = 0;

    // ========== Trade ==========

    virtual void requestTrade(uint16_t targetId) = 0;
    virtual void acceptTrade() = 0;
    virtual void cancelTrade() = 0;
    // T102: game's tradedecline_cmd -- decline a RECEIVED trade request
    // (TradeState::PendingAccept), distinct from cancelTrade() (an ACTIVE
    // trade, TradeState::Active). Both existed as ActionDispatcher stubs
    // before T102; only cancelTrade() had a name already -- this adds the
    // missing decline half.
    virtual void declineTrade() = 0;


    // ========== Pet ==========

    virtual void sendPetCommand(uint8_t command, uint16_t targetId = 0) = 0;
    virtual void dismissPet() = 0;

    // ========== Tradeskill ==========

    virtual void clickWorldObject(uint32_t dropId) = 0;

    // T149: the four recipe requests. NONE of them takes an object_type --
    // that is the container's type, and it is sourced inside the handler from
    // the container the server told us is open (T144 made
    // WorldObject::object_type correct, and ZoneProcessClickObjectAction
    // records the drop_id in EverQuest::m_active_tradeskill_object_id). A
    // user-typed numeric object_type would be technically wired and
    // practically unreachable -- the exact defect class this arc removes.
    // See refactor/T149_tradeskill_recipes.md.
    virtual void recipeSearch(const std::string& query, uint32_t minTrivial,
                              uint32_t maxTrivial) = 0;
    virtual void recipeDetails(uint32_t recipeId) = 0;
    virtual void recipeAutoCombine(uint32_t recipeId) = 0;
    virtual void recipesFavorite(const std::vector<uint32_t>& recipeIds) = 0;

    // T153: request an appearance (face/hair/beard) change. Only the fields
    // the user named are set; the handler merges the rest from the
    // character's current appearance before calling SendFaceChange().
    virtual void setAppearance(const AppearanceChange& change) = 0;

    // T171: set (or clear) the inspect message this client answers inspects
    // with. No packet is sent -- there is no Titanium opcode for pushing it --
    // so unlike almost everything else on this interface it is local state
    // plus a config write. std::nullopt means SHOW the current value.
    virtual void setInspectMessage(const std::optional<std::string>& message) = 0;

    // ========== Extended Movement ==========

    virtual void setMovementMode(int mode) = 0;
    virtual void setPositionState(int state) = 0;

    // ========== Extended Character State ==========

    virtual void setRoleplay(bool rp) = 0;
    virtual void setPathfinding(bool enabled) = 0;

    // ========== Extended Combat ==========

    virtual void setAutoHunting(bool enabled) = 0;
    virtual void setAutoLoot(bool enabled) = 0;
    virtual void listHuntTargets() = 0;

    // ========== Entity Query ==========

    virtual void listEntities(const std::string& filter) = 0;
    virtual void dumpEntityAppearance(const std::string& name) = 0;

    // ========== Utility ==========

    virtual void sendAnimation(uint8_t animationId, uint8_t speed = 10) = 0;

    // ========== Social (T102) ==========
    // Ported from game's Social-category handlers (eq_commands.cpp);
    // /who ported from the Social section's who_cmd, CHOSEN over
    // Movement's later-registered, intra-registry-shadowing-winner `who` --
    // see command_processor.cpp's cmdWho for why.
    virtual void inspectTarget(uint16_t targetId) = 0;
    virtual void toggleLFG() = 0;
    virtual void setSurname(const std::string& surname) = 0;
    virtual void rollRandom(uint32_t low, uint32_t high) = 0;
    virtual void yellForHelp() = 0;
    virtual void requestWho(const std::string& name, int lvlLow, int lvlHigh) = 0;

    // ========== Guild (T132) ==========
    // Net-new: no game-registry counterpart was reconciled here -- T98
    // emptied the Guild category and there was nothing underneath it (see
    // refactor/T132_migrate_guild_family.md). These wrap the pre-existing
    // EverQuest::SendGuild*() senders, which already implement the guard/
    // argument logic; the wrappers are pure routing.
    virtual void guildInvite(const std::string& playerName) = 0;
    virtual void guildAcceptInvite() = 0;
    virtual void guildDeclineInvite() = 0;
    virtual void guildRemove(const std::string& playerName) = 0;
    virtual void guildDemote(const std::string& playerName) = 0;
    virtual void guildMakeLeader(const std::string& playerName) = 0;
    virtual void requestGuildMOTD() = 0;
    virtual void setGuildMOTD(const std::string& motd) = 0;

    // ========== Corpse / Resurrection (T134) ==========
    // Net-new, same shape as Guild (T132): no game-registry counterpart was
    // reconciled here -- action::CommandProcessor had zero registrations for
    // this family before this unit. These wrap the pre-existing
    // EverQuest::SendCorpseDrag()/SendCorpseDrop()/SendConsent()/
    // SendConsentDeny()/SendRezzAnswer(bool) senders, which already implement
    // the guard/argument logic (m_is_dragging_corpse, m_has_pending_rezz);
    // the wrappers are pure routing. rezAccept()/rezDecline() both route to
    // SendRezzAnswer(bool) -- two thin methods, matching guildAcceptInvite()/
    // guildDeclineInvite()'s identical shape over SendGuildInviteAccept(bool).
    virtual void corpseDrag(const std::string& corpseName) = 0;
    virtual void corpseDrop() = 0;
    virtual void consent(const std::string& playerName) = 0;
    virtual void consentDeny(const std::string& playerName) = 0;
    virtual void rezAccept() = 0;
    virtual void rezDecline() = 0;

    // ========== Utility (T135) ==========
    // Net-new, same shape as Guild/Corpse-Rez: no game-registry counterpart
    // was reconciled here -- action::CommandProcessor had zero registrations
    // for this family before this unit. These wrap the pre-existing
    // EverQuest::SendSplit()/SendSave()/SendBindWound()/SendRewind()/
    // SendReport()/SendPetition() senders, which already implement the
    // guard/argument logic (m_in_group + affordability inside SendSplit);
    // the wrappers are pure routing. bandage() resolves targetId==0 to the
    // current CombatManager target, falling back to SELF (m_my_spawn_id) --
    // NOT an error like assist()/shieldTarget() -- matching game's original
    // bandage handler exactly (eq_commands.cpp bindwound_cmd).
    virtual void split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) = 0;
    virtual void save() = 0;
    virtual void bandage(uint16_t targetId) = 0;
    virtual void rewind() = 0;
    virtual void bug(const std::string& description) = 0;
    virtual void petition(const std::string& text) = 0;

    // ========== Raid (T179) ==========
    // The vendorList() shape: the handler does its own AddChatSystemMessage and
    // reads EverQuest raid state directly; the dispatcher just guards. raidInvite
    // /Accept/Leave reach EverQuest::SendRaidCommand; raidStatus is a pure read.
    virtual void raidInvite(const std::string& name) = 0;
    virtual void raidAccept() = 0;
    virtual void raidLeave() = 0;
    virtual void raidStatus() = 0;
    virtual void raidList() = 0;
    virtual void raidRemove(const std::string& name) = 0;
    virtual void raidMakeLeader(const std::string& name) = 0;
    virtual void raidLootType(const std::string& mode) = 0;
    virtual void raidLooter(bool add, const std::string& name) = 0;
    virtual void raidLock(bool lock) = 0;
    virtual void raidGroup(const std::string& name, const std::string& group_text) = 0;

    // ========== Merchant (T136) ==========
    // Unlike every prior family, NOT thin wrappers over a pre-existing
    // EverQuest::Send*() -- there were no senders. vendorToggle()/
    // buyFromVendor()/sellToVendor() wrap three new EverQuest methods
    // (RequestVendorToggle/RequestVendorBuy/RequestVendorSell,
    // eq_commands.cpp) that push the SAME eqt::events::Vendor*Intent the
    // renderer's vendor-window gesture pushes -- the T48 maximum-fidelity
    // choice, preserved. These new EverQuest methods exist only because
    // EqActionHandler has no access to EverQuest::m_bridge (private);
    // IActionHandler still can't reach the intent queue directly, so
    // EverQuest keeps owning the push, same as it always has.
    // vendorList() is a pure state read + print -- no dispatcher-level
    // guard beyond checkZoneConnection(); see EqActionHandler::vendorList()
    // for the guard/format logic (ported verbatim from the old
    // vendorlist_cmd handler), which reads the two new EverQuest accessors
    // (GetVendorItems()/GetVendorName()) added for exactly this purpose --
    // see the plan's "the state gap" section for why accessors were chosen
    // over a vendor state domain.
    virtual void vendorToggle() = 0;
    virtual void vendorList() = 0;
    virtual void buyFromVendor(uint32_t slot, uint8_t quantity) = 0;
    virtual void sellToVendor(uint32_t slot, uint8_t quantity) = 0;

    // ========== Movement / Utility / Skills (T140) ==========
    // Final batch of the game-registry migration programme. loc/warp/
    // zoning/quit/camp/debug/perf/timeOfDay/bankToggle/trainWithTarget are
    // net-new IActionHandler methods -- each does its own AddChatSystemMessage
    // reporting inside EqActionHandler (same shape as vendorList() above),
    // so the ActionDispatcher wrapper just guards and calls, returning a
    // generic Success(). warp/timeOfDay/bankToggle/trainWithTarget route to
    // four new EverQuest methods (RequestWarp/RequestTimeOfDay/
    // RequestBankToggle/RequestTraining) for the same reason Merchant
    // needed new EverQuest methods: EqActionHandler has no access to
    // EverQuest's private m_bridge/m_combat_manager/m_entities. sit()/
    // stand()/moveToLocation()/moveToEntity()/followEntity()/stopFollow()
    // (declared above) are REUSED, not duplicated here, for /sit /stand
    // /move /moveto /follow /stopfollow.
    virtual void showLocation() = 0;
    virtual void warp(const std::string& args) = 0;
    virtual void showOrSetZoning(const std::string& args) = 0;
    virtual void requestQuit() = 0;
    virtual void campNow() = 0;
    virtual void debugLevel(const std::string& args) = 0;
    virtual void perfReport() = 0;
    virtual void timeOfDay(const std::string& args) = 0;
    virtual void bankToggle() = 0;
    virtual void trainWithTarget() = 0;

    // ========== Bank coins (T269) ==========
    // /bank deposit|withdraw|convert. coinType scale is the wire's
    // (EQT::COINTYPE_*: 0=cp 1=sp 2=gp 3=pp). The pool check ("do you
    // actually hold that many coins") is done by CommandProcessor against
    // GameState BEFORE dispatch -- a refused command reaches none of these.
    virtual void bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank) = 0;
    virtual void bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount) = 0;

    // ========== Social / Find (T150) ==========
    // Four senders that had zero production callers until this unit
    // (refactor/T150_social_find_family.md). Each is a thin route to the
    // matching EverQuest::Send*(); the "no explicit name given" convention
    // is the SAME as assist()/shieldTarget() above -- CommandProcessor
    // resolves an explicit name to a spawn id via GameState::entities()
    // and passes 0 to mean "use the live CombatManager target", which
    // EqActionHandler reads.
    virtual void assistGroup(uint16_t targetId) = 0;
    virtual void trackTarget(uint16_t targetId) = 0;
    virtual void findPerson(uint16_t targetId) = 0;
    virtual void friendsWho(const std::string& friendsList) = 0;

    // ========== GM (T148) ==========
    // Net-new, T132's shape: thin wrappers over the pre-existing
    // EverQuest::SendGM*() senders in src/client/eq_gm.cpp, all NINE of
    // which had ZERO production callers and no registration in either
    // registry until this unit (T146's finding -- reachability pattern
    // #11-#13). T145's GMKick_Struct 132 -> 129 fix was correct and
    // unreachable; gmKick/gmKill are the first thing that can put it on the
    // wire.
    //
    // isGMMode() is the ONLY non-void method on this interface, and it is
    // the login-time gate: it reports EverQuest::GetGMMode(), i.e.
    // PlayerProfile.gm @13054 -- "GM mode was ON at login", NOT "this
    // account has GM privileges" (account status never reaches a Titanium
    // client at all). ActionDispatcher::checkGMMode() is the single point
    // that consults it; see there for the refusal contract.
    virtual bool isGMMode() const = 0;

    virtual void gmZoneRequest(const std::string& charName, uint16_t zoneId) = 0;
    virtual void gmSummon(const std::string& charName) = 0;
    virtual void gmGoto(const std::string& charName) = 0;
    virtual void gmFind(const std::string& charName) = 0;
    virtual void gmKick(const std::string& charName) = 0;
    virtual void gmKill(const std::string& charName) = 0;
    virtual void gmHideMe(bool hide) = 0;
    virtual void gmEmoteZone(const std::string& text) = 0;
    virtual void gmLastName(const std::string& charName, const std::string& lastName) = 0;
};

/**
 * ActionDispatcher - Central dispatcher for all game actions.
 *
 * This class provides a unified interface for executing game actions from any
 * input source (keyboard, console, automation scripts). It validates actions
 * before execution and provides consistent error handling.
 *
 * Usage:
 * 1. Create an ActionDispatcher with a GameState reference
 * 2. Set the action handler (typically the EverQuest instance or adapter)
 * 3. Call action methods to execute game actions
 *
 * The dispatcher can be used by:
 * - InputActionBridge for translating input events to actions
 * - CommandProcessor for slash command handling
 * - AutomatedMode for scripted actions
 */
class ActionDispatcher {
public:
    explicit ActionDispatcher(state::GameState& state);
    ~ActionDispatcher();

    /**
     * Set the action handler that will execute the actions.
     * Must be called before any actions can be dispatched.
     */
    void setActionHandler(IActionHandler* handler);

    /**
     * Check if the dispatcher is ready to execute actions.
     */
    bool isReady() const { return m_handler != nullptr; }

    // ========== Movement Actions ==========

    /**
     * Stop all movement.
     */
    ActionResult stopAllMovement();

    /**
     * Set player heading in degrees (0-360, 0 = north).
     */
    ActionResult setHeading(float heading);

    /**
     * Face a specific location.
     */
    ActionResult faceLocation(float x, float y, float z);

    /**
     * Face an entity by name.
     */
    ActionResult faceEntity(const std::string& name);

    /**
     * Jump.
     */
    ActionResult jump();

    /**
     * Sit down.
     */
    ActionResult sit();

    /**
     * Stand up.
     */
    ActionResult stand();

    /**
     * Toggle autorun.
     */
    ActionResult toggleAutorun();

    /**
     * Move to a specific location using pathfinding.
     */
    ActionResult moveToLocation(float x, float y, float z);

    /**
     * Move to an entity by name.
     */
    ActionResult moveToEntity(const std::string& name);

    /**
     * Follow an entity.
     */
    ActionResult followEntity(const std::string& name);

    /**
     * Stop following.
     */
    ActionResult stopFollow();

    // ========== Combat Actions ==========

    /**
     * Target an entity by spawn ID.
     */
    ActionResult targetEntity(uint16_t spawnId);

    /**
     * Target the nearest player character.
     */
    ActionResult targetNearestPC();

    /**
     * Target the nearest NPC.
     */
    ActionResult targetNearestNPC();

    /**
     * Cycle through nearby targets (forward).
     */
    ActionResult cycleTargets();

    /**
     * Cycle through nearby targets (reverse).
     */
    ActionResult cycleTargetsReverse();

    /**
     * Clear current target.
     */
    ActionResult clearTarget();

    /**
     * Target self (F1), with player/pet toggle.
     */
    ActionResult targetSelf();

    /**
     * Target group member by index (0-4 for F2-F6).
     */
    ActionResult targetGroupMember(int index);

    /**
     * Enable auto-attack.
     */
    ActionResult enableAutoAttack();

    /**
     * Disable auto-attack.
     */
    ActionResult disableAutoAttack();

    /**
     * Toggle auto-attack.
     */
    ActionResult toggleAutoAttack();

    // ===== T102: Combat family additions =====

    /**
     * Assist a target (or the current combat target if targetId is 0).
     */
    ActionResult assist(uint16_t targetId);

    /**
     * Shield a target (or the current combat target if targetId is 0).
     */
    ActionResult shieldTarget(uint16_t targetId);

    /**
     * Taunt the current combat target.
     */
    ActionResult taunt();

    /**
     * Toggle ranged auto-attack (archery/throwing).
     */
    ActionResult toggleAutoFire();

    /**
     * Enable the combat manager (auto-attack/hunting subsystem).
     */
    ActionResult enableCombatManager();

    /**
     * Disable the combat manager.
     */
    ActionResult disableCombatManager();

    /**
     * Toggle the combat manager.
     */
    ActionResult toggleCombatManager();

    /**
     * Challenge a target (or the current combat target if targetId is 0) to a duel.
     */
    ActionResult duelRequest(uint16_t targetId);

    /**
     * Accept a pending duel challenge.
     */
    ActionResult duelAccept();

    /**
     * Decline a pending duel challenge.
     */
    ActionResult duelDecline();

    /**
     * Cast spell from gem slot (1-8).
     */
    ActionResult castSpell(uint8_t gemSlot);

    /**
     * Interrupt current spellcast.
     */
    ActionResult interruptCast();

    /**
     * Cast a spell from a gem slot (1-8) via the real EQ::SpellManager path
     * -- ported from game's /cast, T101. Distinct from castSpell() above,
     * which routes through the older CombatManager subsystem.
     */
    ActionResult beginCastFromGem(uint8_t gemSlot, uint16_t targetId = 0);

    /**
     * Use a skill by ID.
     */
    ActionResult useSkill(uint32_t skillId);

    /**
     * Consider current target.
     */
    ActionResult consider();

    // ========== Interaction Actions ==========

    /**
     * Hail (say "Hail").
     */
    ActionResult hail();

    /**
     * Hail current target.
     */
    ActionResult hailTarget();

    /**
     * Click the nearest door.
     */
    ActionResult clickNearestDoor();

    /**
     * Interact with the nearest world object (forge, loom, etc.).
     */
    ActionResult interactNearestWorldObject();

    /**
     * Interact with the nearest interactable (door, object, or NPC).
     * Unified interaction that finds the closest thing to interact with.
     */
    ActionResult interactNearest();

    /**
     * Open loot window for a corpse.
     */
    ActionResult lootCorpse(uint16_t corpseId);

    /**
     * Loot a specific item (by corpse slot) from the open loot window.
     * T268: corpseId 0 means the currently open window.
     */
    ActionResult lootItem(uint16_t corpseId, int16_t slot);

    /**
     * T268: loot the open window's item whose name matches (exact, else
     * case-insensitive substring).
     */
    ActionResult lootItemByName(const std::string& name);

    /**
     * Loot all items from a corpse (the open loot window).
     */
    ActionResult lootAll(uint16_t corpseId);

    /**
     * T268: close the open loot window (OP_EndLootRequest).
     */
    ActionResult closeLootWindow();

    // ========== Chat Actions ==========

    /**
     * Send a chat message to a channel.
     */
    ActionResult sendChatMessage(ChatChannel channel, const std::string& message);

    /**
     * Send a tell to a player.
     */
    ActionResult sendTell(const std::string& target, const std::string& message);

    /**
     * Reply to the last tell received.
     */
    ActionResult replyToLastTell(const std::string& message);

    /**
     * Open chat input pre-filled for replying to the last tell.
     * This is for the R key binding - opens chat with /tell <last_sender>
     */
    ActionResult openReplyToTell();

    // ========== Group Actions ==========

    /**
     * Invite a player to group.
     */
    ActionResult inviteToGroup(const std::string& playerName);

    /**
     * Invite current target to group.
     */
    ActionResult inviteTarget();

    /**
     * Accept pending group invite.
     */
    ActionResult acceptGroupInvite();

    /**
     * Decline pending group invite.
     */
    ActionResult declineGroupInvite();

    /**
     * Leave current group.
     */
    ActionResult leaveGroup();

    // ========== Character State Actions ==========

    /**
     * Set AFK status.
     */
    ActionResult setAFK(bool afk);

    /**
     * Toggle AFK status.
     */
    ActionResult toggleAFK();

    /**
     * Set anonymous status.
     */
    ActionResult setAnonymous(bool anon);

    // ========== Inventory Actions ==========

    /**
     * Use an item (right-click).
     */
    ActionResult useItem(int16_t slot);

    /**
     * T230: use an item identified by ITEM id (what a hotbar Item slot
     * stores). Resolves the inventory slot through the handler
     * (IActionHandler::findItemSlotByItemId) and then calls useItem(slot).
     * Fails -- with the mouse path's exact message, "Item not found in
     * inventory" -- and sends nothing when the item is not carried.
     */
    ActionResult useItemById(uint32_t itemId);

    /**
     * T239: fire a hotbar slot of a COMMAND type (Emote, Macro, Combat).
     * Refuses any other type ("Not a command hotbar slot") and an Emote/Macro
     * slot with empty text ("Hotbar slot has no command") before reaching the
     * handler; otherwise IActionHandler::activateHotbarCommandSlot.
     */
    ActionResult activateHotbarCommandSlot(HotbarSlotType type, const std::string& text);

    /**
     * T141: /equip -- move/equip an item by slot numbers, by name, or
     * report state (`list`). See EqActionHandler::equip() ->
     * EverQuest::RequestEquip() for the form dispatch.
     */
    ActionResult equip(const std::string& args);

    /**
     * T273: /read <slot|item name> -- ask the server for a carried book or
     * note's text. Item resolution and the not-a-book refusal live in
     * EverQuest::RequestReadItem(); this only gates on a zone connection
     * (OP_ReadBook goes to the zone server) and on a non-empty argument.
     */
    ActionResult readItem(const std::string& args);

    /**
     * T276: /scribe <slot|item name>. Zone-connection + empty-argument
     * gate only; every other refusal lives in
     * EverQuest::RequestScribeItem().
     */
    ActionResult scribeItem(const std::string& args);

    // ========== Spellbook Actions ==========

    /**
     * Forget a spell from a gem slot.
     */
    ActionResult forgetSpell(uint8_t gemSlot);

    /**
     * Memorize a spell by name (exact/partial lookup + disambiguation),
     * ported from game's /mem, T101.
     */
    ActionResult memorizeSpellByName(const std::string& spellName, uint8_t gemSlot);

    /**
     * List memorized spells (all 8 gems), ported from game's /gems, T101.
     */
    ActionResult listMemorizedSpells();

    // ========== Buff Actions ==========

    /**
     * Ask the server to drop one of the player's buffs (T277).
     * `args` is a 1-based buff slot number or a spell-name substring.
     * Requires a zone connection and a non-empty argument.
     */
    ActionResult cancelBuff(const std::string& args);

    // ========== Trade Actions ==========

    /**
     * Request trade with target.
     */
    ActionResult requestTrade(uint16_t targetId);

    /**
     * Accept trade.
     */
    ActionResult acceptTrade();

    /**
     * Cancel trade.
     */
    ActionResult cancelTrade();

    /**
     * Decline a received (PendingAccept) trade request. Distinct from
     * cancelTrade(), which cancels an already-Active trade.
     */
    ActionResult declineTrade();

    // ========== Pet Actions ==========

    /**
     * Send a pet command.
     * @param command Pet command ID (see pet_constants.h PetCommand enum)
     * @param targetId Optional target for attack commands
     */
    ActionResult sendPetCommand(uint8_t command, uint16_t targetId = 0);

    /**
     * Dismiss the current pet.
     */
    ActionResult dismissPet();

    /**
     * Command pet to attack. targetId defaults to 0 (server uses the pet
     * owner's current target); T101 added the explicit-target overload to
     * carry game's /pet attack <name> resolution (the pet convenience
     * wrappers previously always sent 0).
     */
    ActionResult petAttack(uint16_t targetId = 0);

    /**
     * Command pet to back off (stop attacking).
     */
    ActionResult petBackOff();

    /**
     * Command pet to follow owner.
     */
    ActionResult petFollow();

    /**
     * Command pet to guard current location.
     */
    ActionResult petGuard();

    /**
     * Toggle pet sit.
     */
    ActionResult petSit();

    /**
     * Toggle pet taunt.
     */
    ActionResult petTaunt();

    /**
     * Toggle pet hold (don't add to hate list).
     */
    ActionResult petHold();

    /**
     * Toggle pet focus.
     */
    ActionResult petFocus();

    /**
     * Get pet health report.
     */
    ActionResult petHealth();

    // T101: game's /pet vocabulary (eq_commands_spells.cpp) is a strict
    // superset of the pre-T101 wrapper set above -- these five close that
    // gap (see refactor/T101_migrate_spells_pet_family.md's Review for
    // which were added and why). All route through the existing generic
    // sendPetCommand()/IActionHandler::sendPetCommand() -- no new
    // IActionHandler virtual needed for any of them.

    /**
     * Command pet to stand up (explicit, distinct from petSit()'s toggle).
     */
    ActionResult petStand();

    /**
     * Explicitly disable pet taunt (distinct from petTaunt()'s toggle).
     */
    ActionResult petTauntOff();

    /**
     * Explicitly disable pet hold (distinct from petHold()'s toggle).
     */
    ActionResult petHoldOff();

    /**
     * Command pet to become leader (show who it follows).
     */
    ActionResult petLeader();

    /**
     * Toggle pet spell-hold (don't cast spells).
     */
    ActionResult petSpellHold();

    // ========== Tradeskill Actions ==========

    /**
     * Click/interact with a world object (forge, loom, etc.).
     */
    ActionResult clickWorldObject(uint32_t dropId);

    /**
     * T149: search the OPEN tradeskill container's recipes. `query` may be
     * empty (the server then omits the name clause and returns everything
     * makeable in that container). The trivial range is a real filter on the
     * server -- `tr.trivial <= maxTrivial` -- so a maxTrivial of 0 returns
     * nothing; CommandProcessor supplies a sane default rather than 0.
     */
    ActionResult recipeSearch(const std::string& query, uint32_t minTrivial,
                              uint32_t maxTrivial);

    /**
     * T149: request the component list for one recipe id (as printed by a
     * prior search). Carries no object_type on the wire.
     */
    ActionResult recipeDetails(uint32_t recipeId);

    /**
     * T149: ask the server to auto-combine a recipe in the open container.
     */
    ActionResult recipeAutoCombine(uint32_t recipeId);

    /**
     * T149: request details for a set of favorite recipe ids in the open
     * container.
     */
    ActionResult recipesFavorite(const std::vector<uint32_t>& recipeIds);

    // ========== Extended Movement Actions ==========

    /**
     * Set movement mode (run/walk/sneak).
     */
    ActionResult setMovementMode(int mode);

    /**
     * Set position state (standing/sitting/crouching/feign death).
     */
    ActionResult setPositionState(int state);

    // ========== Extended Character State Actions ==========

    /**
     * Set roleplay flag.
     */
    ActionResult setRoleplay(bool rp);

    /**
     * Set pathfinding enabled/disabled.
     */
    ActionResult setPathfinding(bool enabled);

    // ========== Extended Combat Actions ==========

    /**
     * Set auto-hunting mode.
     */
    ActionResult setAutoHunting(bool enabled);

    /**
     * Set auto-loot mode.
     */
    ActionResult setAutoLoot(bool enabled);

    /**
     * List hunt targets.
     */
    ActionResult listHuntTargets();

    // ========== Entity Query Actions ==========

    /**
     * List entities matching a filter.
     */
    ActionResult listEntities(const std::string& filter);

    /**
     * Dump entity appearance/equipment info.
     */
    ActionResult dumpEntityAppearance(const std::string& name);

    // ========== Utility Actions ==========

    /**
     * Play an animation.
     */
    ActionResult sendAnimation(uint8_t animationId, uint8_t speed = 10);

    // ========== Social Actions (T102) ==========

    /**
     * Inspect a target (or the current combat target if targetId is 0).
     */
    ActionResult inspectTarget(uint16_t targetId);

    /**
     * Toggle Looking For Group status.
     */
    ActionResult toggleLFG();

    /**
     * Request a surname (last name).
     */
    ActionResult setSurname(const std::string& surname);

    /**
     * Roll a random number in [low, high].
     */
    ActionResult rollRandom(uint32_t low, uint32_t high);

    /**
     * Yell for help.
     */
    ActionResult yellForHelp();

    /**
     * Search the server's /who listing.
     */
    ActionResult requestWho(const std::string& name, int lvlLow, int lvlHigh);

    // ========== Guild Actions (T132) ==========

    /**
     * Invite a player to your guild.
     */
    ActionResult guildInvite(const std::string& playerName);

    /**
     * Accept a pending guild invite.
     */
    ActionResult guildAcceptInvite();

    /**
     * Decline a pending guild invite.
     */
    ActionResult guildDeclineInvite();

    /**
     * Remove a member from your guild.
     */
    ActionResult guildRemove(const std::string& playerName);

    /**
     * Demote a guild member.
     */
    ActionResult guildDemote(const std::string& playerName);

    /**
     * Transfer guild leadership to a member.
     */
    ActionResult guildMakeLeader(const std::string& playerName);

    /**
     * Request the guild's message of the day.
     */
    ActionResult requestGuildMOTD();

    /**
     * Set the guild's message of the day.
     */
    ActionResult setGuildMOTD(const std::string& motd);

    // ========== Corpse / Resurrection Actions (T134) ==========

    /**
     * Start dragging a corpse (requires consent from the corpse's owner).
     */
    ActionResult corpseDrag(const std::string& corpseName);

    /**
     * Drop the corpse currently being dragged.
     */
    ActionResult corpseDrop();

    /**
     * Grant a player permission to drag your corpse.
     */
    ActionResult consent(const std::string& playerName);

    /**
     * Revoke a player's permission to drag your corpse.
     */
    ActionResult consentDeny(const std::string& playerName);

    /**
     * Accept a pending resurrection offer.
     */
    ActionResult rezAccept();

    /**
     * Decline a pending resurrection offer.
     */
    ActionResult rezDecline();

    // ========== Utility Actions (T135) ==========

    /**
     * Split money with your group. Guarded inside SendSplit (m_in_group,
     * affordability), not duplicated here.
     */
    ActionResult split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);

    /**
     * Request a character save.
     */
    ActionResult save();

    /**
     * Use bind wound skill. targetId==0 resolves to the current combat
     * target, falling back to self.
     */
    ActionResult bandage(uint16_t targetId);

    // Raid (T179)
    ActionResult raidInvite(const std::string& name);
    ActionResult raidAccept();
    ActionResult raidLeave();
    ActionResult raidStatus();
    ActionResult raidList();
    ActionResult raidRemove(const std::string& name);
    ActionResult raidMakeLeader(const std::string& name);
    ActionResult raidLootType(const std::string& mode);
    ActionResult raidLooter(bool add, const std::string& name);
    ActionResult raidLock(bool lock);
    ActionResult raidGroup(const std::string& name, const std::string& group_text);

    /**
     * Attempt to return to a safe position.
     */
    ActionResult rewind();

    /**
     * Submit a bug report.
     */
    ActionResult bug(const std::string& description);

    /**
     * Submit a petition.
     */
    ActionResult petition(const std::string& text);

    // ========== Merchant Actions (T136) ==========

    /**
     * Open a vendor window with your current target, or close it if
     * already open. Target/NPC-type/class validation is guarded inside
     * EverQuest::RequestVendorToggle(), not duplicated here.
     */
    ActionResult vendorToggle();

    /**
     * List the open vendor's inventory. Pure state read + print --
     * EqActionHandler::vendorList() reads EverQuest::GetVendorItems()/
     * GetVendorName() directly and reports "not at a vendor" itself.
     */
    ActionResult vendorList();

    /**
     * Buy an item from the open vendor. Slot-existence and
     * vendor-window-open guards live inside EverQuest::RequestVendorBuy().
     */
    ActionResult buyFromVendor(uint32_t slot, uint8_t quantity);

    /**
     * Sell an item to the open vendor. Vendor-window-open guard lives
     * inside EverQuest::RequestVendorSell(); item-in-slot/NO_DROP/stack
     * validation stays entirely inside SellToVendor() (eq_merchant.cpp),
     * untouched.
     */
    ActionResult sellToVendor(uint32_t slot, uint8_t quantity);

    // ========== Movement / Utility / Skills Actions (T140) ==========

    /**
     * Show current coordinates. Pure state read + print (EverQuest::
     * GetPosition(), already public) -- no dispatcher-level guard beyond
     * checkHandler().
     */
    ActionResult showLocation();

    /**
     * Instantly teleport to coordinates, entity, or current target (raw
     * arg string, parsed inside EverQuest::RequestWarp() -- see that
     * method for why). No server round trip for the teleport itself;
     * SendPositionUpdate() is still called afterward, unchanged.
     */
    ActionResult warp(const std::string& args);

    /**
     * Show or set whether crossing a zone line triggers a real zone
     * change. Pure state read/write via the already-public
     * SetZoningEnabled()/IsZoningEnabled().
     */
    ActionResult showOrSetZoning(const std::string& args);

    /**
     * Exit the game immediately (no server-side logout courtesy) --
     * distinct from camp(). checkHandler() only, deliberately NOT
     * checkZoneConnection(): quit must work whether connected or not
     * (T30).
     */
    ActionResult quit();

    /**
     * Sit down and start the 30-second camp-out timer.
     */
    ActionResult camp();

    /**
     * Show or set the client debug log level (0-6).
     */
    ActionResult debugLevel(const std::string& args);

    /**
     * Show a performance metrics report (console + chat summary).
     */
    ActionResult perfReport();

    /**
     * Show or set the game clock (0-23 hour).
     */
    ActionResult timeOfDay(const std::string& args);

    /**
     * Open a bank window with your current target, or close it if
     * already open. Target guard lives inside EverQuest::
     * RequestBankToggle(), not duplicated here.
     */
    ActionResult bankToggle();

    /**
     * T269: move coins between the inventory and bank pools (deposit when
     * fromBank is false, withdraw when true), or convert one bank
     * denomination into another. Both push the kept BankCurrencyMoveIntent /
     * BankCurrencyConvertIntent via EverQuest::RequestBankCoinMove/Convert;
     * the bank-window-open guard lives there, the pool check in
     * CommandProcessor::cmdBank.
     */
    ActionResult bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank);
    ActionResult bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount);

    /**
     * Request the trainer window for your current target. Target/
     * npc_type/class-range/distance/trained_class guards live inside
     * EverQuest::RequestTraining(), not duplicated here.
     */
    ActionResult trainWithTarget();

    /**
     * T150: group assist -- ask the server to give you your group main
     * assist's target. targetId 0 means "use the current target"; the
     * fallback and its "You don't have a target." message live in
     * EqActionHandler, matching assist().
     */
    ActionResult assistGroup(uint16_t targetId);

    /**
     * T150: set the server-side tracking target (OP_TrackTarget).
     * targetId 0 means "use the current target". NOTE: eqemu's
     * Handle_OP_TrackTarget only calls SetTrackingID() -- it sends NO
     * reply. The track LIST (OP_Track -> ZoneProcessTrack) is a separate
     * request this client still has no sender for.
     */
    ActionResult trackTarget(uint16_t targetId);

    /**
     * T150: ask the server to path you to an entity (OP_FindPersonRequest);
     * the reply is OP_FindPersonReply -> ZoneProcessFindPersonReply.
     * targetId 0 means "use the current target".
     */
    ActionResult findPerson(uint16_t targetId);

    /**
     * T150: ask world who from a comma-delimited name list is online
     * (OP_FriendsWho). The reply is an OP_WhoAllResponse, the same packet
     * /who all produces -- ZoneProcessWhoAllResponse already handles it.
     */
    ActionResult friendsWho(const std::string& friendsList);

    // ========== GM Actions (T148) ==========
    //
    // Every one of these is gated by checkGMMode() BEFORE checkZoneConnection()
    // -- see checkGMMode() below for why the order matters and what the
    // refusal has to look like.

    /** Ask the server to move a character to a zone. */
    ActionResult gmZoneRequest(const std::string& charName, uint16_t zoneId);

    /** Summon a character to your location. */
    ActionResult gmSummon(const std::string& charName);

    /** Teleport yourself to a character. */
    ActionResult gmGoto(const std::string& charName);

    /** Ask the server where a character is. */
    ActionResult gmFind(const std::string& charName);

    /** Disconnect a character from the server. */
    ActionResult gmKick(const std::string& charName);

    /** Kill a character outright. */
    ActionResult gmKill(const std::string& charName);

    /** Hide (or unhide) yourself from other players. */
    ActionResult gmHideMe(bool hide);

    /** Broadcast an emote to everyone in the zone. */
    ActionResult gmEmoteZone(const std::string& text);

    /** Set a character's last name. */
    ActionResult gmLastName(const std::string& charName, const std::string& lastName);

    /**
     * T153: change the character's face/hair/beard appearance (`/appearance`).
     * Only the named fields are changed; the handler fills the rest in from
     * the character's current appearance.
     */
    ActionResult setAppearance(const AppearanceChange& change);
    ActionResult setInspectMessage(const std::optional<std::string>& message);

private:
    state::GameState& m_state;
    IActionHandler* m_handler = nullptr;

    // Helper to check if handler is available
    ActionResult checkHandler() const;

    // Helper to validate zone connection
    ActionResult checkZoneConnection() const;

    // T148: the login-time GM-mode gate, the single point every gm*() method
    // above consults.
    //
    // Reports IActionHandler::isGMMode(), which is EverQuest::GetGMMode(),
    // which is PlayerProfile.gm @13054 read ONCE in
    // ZoneProcessPlayerProfile. It means "GM mode was ON at login". A GM who
    // toggles `#gm` mid-session must relog; that staleness is Will's
    // accepted design (2026-08-19), not a defect, and the AT_GM live-update
    // path was explicitly declined.
    //
    // The refusal string MUST stay distinguishable from "no such command":
    // the commands are registered unconditionally, so a non-GM typing /kick
    // learns the command EXISTS and is unavailable to them, rather than
    // being told it does not exist. Do not "fix" this by registering the GM
    // commands conditionally.
    //
    // Checked BEFORE checkZoneConnection() on purpose: an ordinary player
    // who is not zoned in should still be told the honest reason (not a GM),
    // not the incidental one (not connected).
    ActionResult checkGMMode() const;

    // Calculate heading to face a position
    float calculateHeadingTo(float x, float y) const;
};

} // namespace action
} // namespace eqt
