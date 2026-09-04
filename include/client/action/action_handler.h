// The action layer's pure-virtual callback interface, which the dispatcher
// calls into.
// Extracted from the action layer's original combined header by refactor unit
// J15 (its plan lives in refactor/). Move-only: nothing was edited.

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>  // T149: recipesFavorite() takes a recipe-id list
#include <glm/glm.hpp>
#include "client/hotbar/hotbar_slot_type.h"  // T239: activateHotbarCommandSlot
#include "client/action/action_types.h"

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {


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
    // T356 (B003c): these take a RESOLVED spawn id, not a name -- the same
    // rule T230 already applied to targetEntity(). Name resolution is the
    // dispatcher's job (EntityManager::findEntityByName, nearest-first
    // ranked overload); a handler that re-resolved the name would use the
    // game-side EntitiesComponent::FindEntityByName, an unranked
    // lowest-spawn-id prefix match, and could move to / follow a DIFFERENT
    // same-named entity than the one the dispatcher validated.
    virtual void moveToEntity(uint16_t spawnId) = 0;
    virtual void followEntity(uint16_t spawnId) = 0;
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

    /**
     * T316: the name of the last player who sent us a tell, or "" if none.
     * ActionDispatcher::openReplyToTell() needs the NAME, not just the
     * yes/no of hasTellToReplyTo(), so it can hand back the prefill text
     * ("/tell <name> ") the chat input is meant to open with.
     */
    virtual std::string getLastTellSender() const = 0;

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

    /**
     * T316: drop id of the nearest clickable world object (forge, chest,
     * groundspawn) within interaction range, or 0 if there is none.
     *
     * World objects live in EverQuest::m_world_objects, not in GameState, so
     * ActionDispatcher cannot run the query itself the way it runs
     * DoorStateManager::getNearestDoor(). This is the same shape as
     * hasTellToReplyTo(): a pure query on the handler that lets the
     * dispatcher refuse with a user-visible message instead of dispatching
     * blind.
     *
     * @param outDistance set to the distance to that object when one is
     *        found (untouched otherwise), so interactNearest() can rank it
     *        against the nearest door and NPC on the same scale.
     */
    virtual uint32_t findNearestWorldObject(float& outDistance) const = 0;

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

    // T278: the MANUAL combine (OP_TradeSkillCombine) and the container
    // close (OP_ClickObjectAction open=0). Argument-less for the same reason
    // the recipe requests take no object_type. Delegate to EverQuest.
    virtual void tradeskillCombine(int16_t containerSlot) = 0;
    virtual void closeTradeskillContainer() = 0;

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
    // see command_processor_social.cpp's cmdWho for why.
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
    // T280: /train <skill|id|list> and /trainclose. The skill resolver,
    // the local refusals and the TrainSkillIntent/CloseTrainerIntent
    // pushes all live in EverQuest (the trainer session's caps are
    // private there); these are the routing hops.
    virtual void trainSkill(const std::string& args) = 0;
    virtual void closeTrainer() = 0;

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

} // namespace action
} // namespace eqt
