// The action layer's dispatcher class. Its free value types and its
// pure-virtual callback interface now live in the two sibling headers included
// below, moved there by refactor unit J15 (its plan lives in refactor/).
// Move-only: nothing was edited.

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>  // T149: recipesFavorite() takes a recipe-id list
#include <glm/glm.hpp>
#include "client/hotbar/hotbar_slot_type.h"  // T239: activateHotbarCommandSlot
#include "client/action/action_types.h"
#include "client/action/action_handler.h"

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {


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

    /**
     * B442 (#151): /pickup /putdown /cursor /coin. Zone-connection gate only;
     * parsing and every refusal live in CommandsComponent::RequestCursorCommand().
     */
    ActionResult cursorCommand(const std::string& verb, const std::string& args);

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

    /**
     * T278: /combine (manual combine of the open WORLD container's contents)
     * and /closecontainer. Gated here only on a zone connection; the
     * open-container refusal lives in EverQuest. See T278's plan.
     */
    ActionResult tradeskillCombine(int16_t containerSlot);
    ActionResult closeTradeskillContainer();

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
     * Instantly teleport to coordinates, an entity, or the current target.
     * No server round trip for the teleport itself; SendPositionUpdate() is
     * still called afterward, unchanged.
     *
     * T362 (B070): the ENTITY form is resolved here, with the T244
     * nearest-first ranked overload (the resolver /target, /face, /moveto and
     * /follow use), and reaches the handler as a spawn id via
     * warpToEntity(). The empty and coordinate forms still go down as the raw
     * arg string to warp(), parsed inside CommandsComponent::RequestWarp().
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
     * T280: train one skill at the trainer whose window is already
     * open (/train <skill|id>, or `list` to see the offerings), and
     * close that window (/trainclose). Every guard that needs the
     * session -- no session, no training points, unknown/ambiguous
     * skill, not taught here, already at this trainer's cap -- lives
     * in EverQuest::RequestTrainSkill(); this layer only gates the
     * zone connection and the empty argument.
     */
    ActionResult trainSkill(const std::string& args);
    ActionResult closeTrainer();

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
     * reply. The track LIST is OP_Track (skill 53, /useskill tracking);
     * since B895 both feed eqt::tracking::TrackingController, which also
     * says "You begin tracking %1." and the direction lines.
     */
    ActionResult trackTarget(uint16_t targetId);

    /**
     * B895 (GitLab #211): the Tracking Window's slash surface --
     * /trackfilter, /tracksort, /trackplayers, /stoptracking, /tracklist.
     * `arg` is parsed by TrackingController (usage line on a bad one).
     */
    ActionResult trackingCommand(eqt::tracking::TrackingCommand cmd, const std::string& arg);

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
