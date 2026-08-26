#pragma once

#include "client/action/action_dispatcher.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <optional>

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace output {
class IOutputRenderer;
}

namespace action {

/**
 * CommandInfo - Information about a registered command.
 */
struct CommandInfo {
    std::string name;                      // Primary name (e.g., "say")
    std::vector<std::string> aliases;      // Alternative names (e.g., {"s"})
    std::string usage;                     // Usage string (e.g., "/say <message>")
    std::string description;               // Short description
    std::string category;                  // Category for help grouping
    bool requiresArgs = false;             // Whether arguments are required
};

/**
 * CommandProcessor - Processes text commands and executes corresponding actions.
 *
 * This class provides a unified command processing system that can be used by:
 * - Console input in headless mode
 * - Chat window input in graphical mode
 * - Automation scripts
 *
 * Commands are registered with handlers that receive the command arguments
 * and use the ActionDispatcher to execute the appropriate actions.
 *
 * Built-in commands include:
 * - Chat: /say, /shout, /ooc, /auction, /gsay, /gu, /tell, /emote, /reply
 *   (T100 -- migrated upward from EverQuest::m_command_registry, see below)
 * - Movement: /face, /walk, /run, /sneak, /crouch, /feign, /jump, /turn
 * - Combat: /consider, /hail, /hunt, /autoloot, /listtargets, /loot
 * - Utility: /npcstate, /door, /afk, /anon, /list, /dump, /pathfinding,
 *   /roleplay, /logonly, /logexclude, /logmodule, /logclear
 * - Group: /invite, /disband, /decline, /groupaccept (T99 -- migrated
 *   upward from EverQuest::m_command_registry, the game registry's
 *   handlers deleted from eq_commands.cpp in the same change; /groupaccept
 *   is new, see cmdGroupAccept)
 *
 * T98: 34 commands that were always shadowed by EverQuest::m_command_registry
 * (registry 1 wins every dispatch collision -- see
 * refactor/T97_command_registry_shadowing_audit.md) were deleted along with
 * their handlers: say, ooc, auction, tell, reply, gsay, gu, emote, loc, sit,
 * stand, camp, move, moveto, follow, stopfollow, target, cast, interrupt,
 * invite, disband, decline, who, help, quit, q, debug, timestamp, skills,
 * gems, mem, forget, pet, filter. `shout` was kept despite its primary name
 * being shadowed too -- its alias "sh" escapes because `game`'s own shout
 * alias is spelled "sho".
 *
 * T99: invite/disband/decline MIGRATED BACK, this time as the sole
 * implementation -- the game-registry registrations that used to shadow
 * them were removed from eq_commands.cpp in the same change (see
 * refactor/T99_action_layer_unification_pilot.md). Not a reversal of T98:
 * T98 deleted the worse duplicate that could never run; T99 moves the
 * better implementation into the layer Will designated as the route
 * commands take (ActionDispatcher), rather than reviving the deleted one.
 *
 * T101: cast/interrupt/gems/mem/forget/pet MIGRATED BACK the same way,
 * from EverQuest::m_command_registry's Spells (6) and Pet (1) categories --
 * game's registrations removed from eq_commands_spells.cpp in the same
 * change. See refactor/T101_migrate_spells_pet_family.md.
 * T100: say/ooc/auction/gsay/gu/tell/emote/reply MIGRATED BACK too, same
 * shape as T99 -- game's Chat-category handlers (T97: "Equivalent", both
 * sides terminate in the same EverQuest::SendChatMessage() call) ported
 * here re-expressed against ActionDispatcher::sendChatMessage()/sendTell()/
 * replyToLastTell(), with the corresponding registrations removed from
 * eq_commands.cpp's RegisterSocialCommands() in the same change. `shout`
 * (already living here since before T99, reachable only via alias "sh")
 * is RECONCILED, not duplicated: its alias list gained "sho" and game's
 * own `shout` registration was deleted, so shout is now fully migrated
 * like its 8 Chat siblings instead of existing in both registries at
 * once. See refactor/T100_migrate_chat_family.md.
 */
class CommandProcessor {
public:
    /**
     * Command handler function type.
     * Receives the command arguments (text after the command name).
     * Returns an ActionResult indicating success or failure.
     */
    using CommandHandler = std::function<ActionResult(const std::string& args)>;

    /**
     * Create a command processor.
     * @param state Game state for validation
     * @param dispatcher Action dispatcher for executing actions
     */
    CommandProcessor(state::GameState& state, ActionDispatcher& dispatcher);
    ~CommandProcessor();

    /**
     * Set the output renderer for displaying messages.
     * @param renderer Output renderer (may be nullptr)
     */
    void setOutputRenderer(output::IOutputRenderer* renderer);

    /**
     * Process a command string.
     * The string may or may not include the leading '/'.
     * @param input Command string (e.g., "/say hello" or "say hello")
     * @return Result of command execution
     */
    ActionResult processCommand(const std::string& input);

    /**
     * Process raw text input.
     * If it starts with '/', treat as command; otherwise treat as /say.
     * @param input Text input
     * @return Result of processing
     */
    ActionResult processInput(const std::string& input);

    // ========== Command Registration ==========

    /**
     * Register a custom command.
     * @param info Command information
     * @param handler Function to execute the command
     */
    void registerCommand(const CommandInfo& info, CommandHandler handler);

    /**
     * Unregister a command.
     * @param name Command name
     */
    void unregisterCommand(const std::string& name);

    /**
     * Check if a command is registered.
     */
    bool hasCommand(const std::string& name) const;

    /**
     * Get information about a command.
     */
    std::optional<CommandInfo> getCommandInfo(const std::string& name) const;

    /**
     * Get all registered commands.
     */
    std::vector<CommandInfo> getAllCommands() const;

    /**
     * Get commands in a category.
     */
    std::vector<CommandInfo> getCommandsByCategory(const std::string& category) const;

    /**
     * Get all category names.
     */
    std::vector<std::string> getCategories() const;

    // ========== Command Completion ==========

    /**
     * Get completions for a partial command name.
     * @param partial Partial command name
     * @return List of matching command names
     */
    std::vector<std::string> getCompletions(const std::string& partial) const;

    /**
     * Get all command names (including aliases).
     */
    std::vector<std::string> getAllCommandNames() const;

    // ========== Configuration ==========

    /**
     * Enable/disable command echo (showing what was typed).
     */
    void setEchoEnabled(bool enabled) { m_echoEnabled = enabled; }
    bool isEchoEnabled() const { return m_echoEnabled; }

    /**
     * Set the default chat channel for messages without a command.
     */
    void setDefaultChannel(ChatChannel channel) { m_defaultChannel = channel; }
    ChatChannel getDefaultChannel() const { return m_defaultChannel; }

private:
    state::GameState& m_state;
    ActionDispatcher& m_dispatcher;
    output::IOutputRenderer* m_renderer = nullptr;

    bool m_echoEnabled = false;
    ChatChannel m_defaultChannel = ChatChannel::Say;

    // Command storage
    struct RegisteredCommand {
        CommandInfo info;
        CommandHandler handler;
    };
    std::map<std::string, RegisteredCommand> m_commands;
    std::map<std::string, std::string> m_aliases;  // alias -> primary name

    // Register built-in commands
    void registerBuiltinCommands();

    // Built-in command handlers
    // T98 deleted cmdSay/cmdOOC/cmdAuction/cmdTell/cmdReply/cmdGroupSay/
    // cmdGuildSay/cmdEmote as shadowed dead code (registry 1 always won).
    // T100 re-adds all eight, this time as the sole implementation -- ported
    // from game's Chat-category handlers (eq_commands.cpp's
    // RegisterSocialCommands(), registrations removed from there in the same
    // change). cmdShout predates T100 (added when T98 kept `shout` alive via
    // its escaping "sh" alias); T100 reconciled it by adding "sho" to its
    // alias list and deleting game's own now-redundant `shout` registration,
    // so it's fully migrated alongside its siblings rather than living in
    // both registries. See refactor/T100_migrate_chat_family.md.
    ActionResult cmdShout(const std::string& args);
    ActionResult cmdSay(const std::string& args);
    ActionResult cmdOOC(const std::string& args);
    ActionResult cmdAuction(const std::string& args);
    ActionResult cmdGsay(const std::string& args);
    ActionResult cmdGu(const std::string& args);
    ActionResult cmdTell(const std::string& args);
    ActionResult cmdEmote(const std::string& args);
    ActionResult cmdReply(const std::string& args);

    // T98: cmdLoc/cmdSit/cmdStand/cmdCamp/cmdMove/cmdMoveTo/cmdFollow/
    // cmdStopFollow removed -- shadowed dead code (loc/sit/stand/camp/move/
    // moveto/follow/stopfollow). cmdFace stays: no game "face" command exists
    // at all, so it was never shadowed.
    ActionResult cmdFace(const std::string& args);

    // T98 removed cmdTarget/cmdCast/cmdInterrupt (shadowed dead code:
    // target/cast/interrupt). T101 migrates cast/interrupt back, this time
    // as the sole implementation (game's registrations removed from
    // eq_commands_spells.cpp in the same change) -- see cmdCast/cmdInterrupt
    // below. cmdTarget stays removed: not in the Spells/Pet scope T101
    // covers. T51: cmdAttack/cmdStopAttack/cmdAutoAttack removed earlier --
    // their command registrations were shadowed dead code (registry 1
    // always won the dispatch collision). ActionDispatcher::
    // enableAutoAttack()/disableAutoAttack()/toggleAutoAttack() are untouched
    // and still used directly by InputActionBridge (the keyboard gesture).
    ActionResult cmdConsider(const std::string& args);
    ActionResult cmdHail(const std::string& args);

    // T49: reads m_state.player()'s vendor/banker/trainer fields. Since
    // T227 m_state IS EverQuest::m_game_state (the only instance), written
    // at the source by eq_merchant.cpp's window open/close handlers; the
    // T49-era "which instance does this reader see" trap
    // (refactor/T49_npc_interaction_state.md) no longer exists.
    ActionResult cmdNpcState(const std::string& args);

    // T99: invite/disband/decline migrated HERE from EverQuest::
    // m_command_registry -- the game registry's implementations were better
    // (T97's divergence table) and are ported below, verbatim in behaviour,
    // with the corresponding registrations REMOVED from eq_commands.cpp in
    // the same change (atomicity: game is tried first, so leaving the old
    // registration in place would silently keep routing through it). Also
    // new here: cmdGroupAccept (/groupaccept, alias /gaccept) -- gives
    // ActionDispatcher::acceptGroupInvite() a live caller, fixing the
    // group-invite-accept gap T97/T98 found dead by every route (game's own
    // Social follow_cmd shadowed by Movement's follow; action's old copy
    // shadowed then deleted; eq_bridge.cpp's GroupAcceptIntent handler has no
    // live producer). See refactor/T99_action_layer_unification_pilot.md.
    ActionResult cmdInvite(const std::string& args);
    ActionResult cmdDisband(const std::string& args);
    ActionResult cmdRaid(const std::string& args);  // T179
    ActionResult cmdDecline(const std::string& args);
    ActionResult cmdGroupAccept(const std::string& args);

    // T102: Combat (12) migrated here from EverQuest::m_command_registry's
    // Combat category (assist/shield/duel/duelaccept/dueldecline in
    // eq_commands.cpp; target/attack/stopattack/aa/combat/taunt/autofire in
    // eq_commands_movement.cpp) and Social (10 distinct names/11
    // registrations -- "who" is duplicated, see cmdWho) from
    // eq_commands.cpp's RegisterSocialCommands() plus Movement's second
    // `who`. Registrations removed from both game-registry files in the
    // same change. See refactor/T102_migrate_combat_social.md.
    ActionResult cmdTarget(const std::string& args);
    ActionResult cmdAttack(const std::string& args);
    ActionResult cmdStopAttack(const std::string& args);
    ActionResult cmdAA(const std::string& args);
    ActionResult cmdCombat(const std::string& args);
    ActionResult cmdTaunt(const std::string& args);
    // T156: /useskill — the ONLY scriptable route to SkillManager::activateSkill.
    ActionResult cmdUseSkill(const std::string& args);
    ActionResult cmdAutofire(const std::string& args);
    ActionResult cmdAssist(const std::string& args);
    ActionResult cmdShield(const std::string& args);
    ActionResult cmdDuel(const std::string& args);
    ActionResult cmdDuelAccept(const std::string& args);
    ActionResult cmdDuelDecline(const std::string& args);

    ActionResult cmdTrade(const std::string& args);
    ActionResult cmdTradeAccept(const std::string& args);
    ActionResult cmdTradeDecline(const std::string& args);
    ActionResult cmdCancelTrade(const std::string& args);
    // T102: ported from the Social section's who_cmd (real server /who
    // request, SendWhoAllRequest()) -- CHOSEN over Movement's who (a
    // client-side local-entity-scan substitute that currently wins the
    // intra-registry `who`/`who_cmd` collision T97/T82 found; see cmdWho's
    // own comment for the full reasoning).
    ActionResult cmdWho(const std::string& args);
    ActionResult cmdInspect(const std::string& args);
    ActionResult cmdLFG(const std::string& args);
    ActionResult cmdSurname(const std::string& args);
    ActionResult cmdRandom(const std::string& args);
    ActionResult cmdYell(const std::string& args);

    // T132: Guild (7), migrated from eq_commands.cpp's Guild block.
    // action::CommandProcessor had ZERO guild registrations before this
    // unit -- T98 emptied the Guild category and there was nothing
    // underneath it to revive, unlike T99-T102's reconcile-and-delete
    // families. This is net-new plumbing over the pre-existing
    // EverQuest::SendGuild*() senders (eq_guild.cpp), not a reconciliation.
    // Registrations removed from eq_commands.cpp's Guild block in the same
    // change. See refactor/T132_migrate_guild_family.md.
    ActionResult cmdGuildInvite(const std::string& args);
    ActionResult cmdGuildAccept(const std::string& args);
    ActionResult cmdGuildDecline(const std::string& args);
    ActionResult cmdGuildRemove(const std::string& args);
    ActionResult cmdGuildDemote(const std::string& args);
    ActionResult cmdGuildLeader(const std::string& args);
    ActionResult cmdGuildMotd(const std::string& args);

    // T134: Corpse (4) + Resurrection (2), migrated from eq_commands.cpp's
    // Corpse and Resurrection blocks. Same net-new shape as Guild (T132):
    // action::CommandProcessor had ZERO registrations for either family
    // before this unit. Net-new plumbing over the pre-existing
    // EverQuest::SendCorpseDrag()/SendCorpseDrop()/SendConsent()/
    // SendConsentDeny()/SendRezzAnswer(bool) senders (eq_player_info.cpp).
    // Registrations removed from eq_commands.cpp's Corpse/Resurrection
    // blocks in the same change. See refactor/T134_migrate_corpse_rez.md.
    ActionResult cmdCorpseDrag(const std::string& args);
    ActionResult cmdCorpseDrop(const std::string& args);
    ActionResult cmdConsent(const std::string& args);
    ActionResult cmdConsentDeny(const std::string& args);
    ActionResult cmdRezAccept(const std::string& args);
    ActionResult cmdRezDecline(const std::string& args);

    // T144: /clickobject <dropId>. ActionDispatcher::clickWorldObject() ->
    // EqActionHandler::clickWorldObject() -> EverQuest::SendClickObject() was
    // fully plumbed but had ZERO production callers: the only way to open a
    // tradeskill container was a renderer keypress (HotkeyAction::Interact ->
    // RA::WorldObjectInteract -> WorldObjectInteractIntent), which no headless
    // or scripted session can reach. T144's acceptance bar is "a forge
    // actually opens at the wire", so this registration is what makes that
    // bar reachable at all -- and it retires a dead dispatcher method.
    ActionResult cmdClickObject(const std::string& args);

    // T149: tradeskill recipes. All four recipe senders
    // (SendRecipesSearch/SendRecipeDetails/SendRecipeAutoCombine/
    // SendRecipesFavorite, src/client/eq_merchant.cpp) had ZERO production
    // callers while BOTH recipe reply parsers were already wired into the
    // zone dispatcher -- the feature was exactly half-built. T144 fixed
    // Object_Struct so containers can be opened at all; these four commands
    // are the other half. object_type is deliberately NOT an argument: it is
    // sourced from the open container in
    // EqActionHandler::resolveOpenContainerObjectType(). See
    // refactor/T149_tradeskill_recipes.md.
    ActionResult cmdRecipeSearch(const std::string& args);
    ActionResult cmdRecipeDetails(const std::string& args);
    ActionResult cmdRecipeAutoCombine(const std::string& args);
    ActionResult cmdRecipesFavorite(const std::string& args);

    // T135: Utility (6), migrated from eq_commands.cpp's leftover Group
    // Split / Misc / Skill / Movement / Utility blocks -- these six do not
    // share a scenario, unlike Guild/Corpse-Rez; grouped here only because
    // they share a unit. Same net-new shape: action::CommandProcessor had
    // ZERO registrations for any of them before this unit. Net-new plumbing
    // over the pre-existing EverQuest::SendSplit()/SendSave()/
    // SendBindWound()/SendRewind()/SendReport()/SendPetition() senders
    // (eq_player_info.cpp / eq.cpp). Registrations removed from
    // eq_commands.cpp in the same change. See
    // refactor/T135_migrate_utility_family.md.
    ActionResult cmdSplit(const std::string& args);
    ActionResult cmdSave(const std::string& args);
    ActionResult cmdBandage(const std::string& args);
    ActionResult cmdRewind(const std::string& args);
    ActionResult cmdBug(const std::string& args);
    ActionResult cmdPetition(const std::string& args);

    // T136: Merchant (vendor/vendorlist/buy/sell -- 4 of the 5-command
    // family; equip stays in eq_commands.cpp, see this unit's Review for
    // why), migrated from eq_commands.cpp's Commerce block. Unlike every
    // prior family, no EverQuest::Send*() senders existed to wrap -- see
    // ActionDispatcher's Merchant section for what these route to.
    // cmdBuy/cmdSell own the numeric slot/quantity token parsing (a
    // formatting concern, ported from the old buy_cmd/sell_cmd lambdas'
    // istringstream parsing), not a guard -- same precedent as cmdSplit's.
    ActionResult cmdVendor(const std::string& args);
    ActionResult cmdVendorList(const std::string& args);
    ActionResult cmdBuy(const std::string& args);
    ActionResult cmdSell(const std::string& args);
    ActionResult cmdEquip(const std::string& args);  // T141

    // T140: final 16 of the game-registry migration programme (equip
    // deliberately excluded -- see refactor/T140_migrate_final_seventeen.md).
    // cmdSit/cmdStand/cmdMove/cmdMoveTo/cmdFollow/cmdStopFollow REUSE the
    // pre-existing ActionDispatcher::sit()/stand()/moveToLocation()/
    // moveToEntity()/followEntity()/stopFollow() wrappers (T98 had removed
    // this side's shadowed dead copies; those dispatcher methods themselves
    // were never dead, just unreachable from a game-registry-shadowed
    // command name) -- only cmdLoc/cmdWarp/cmdZoning/cmdQuit/cmdCamp/
    // cmdDebug/cmdPerf/cmdTime/cmdBank/cmdTrain route to new
    // ActionDispatcher/IActionHandler surface. cmdMove/cmdMoveTo/cmdFollow
    // override their dispatcher's generic success message with the
    // original game-registry wording (T88's dual-frame echo for /move in
    // particular -- load-bearing, see eq_commands_movement.cpp's comment).
    ActionResult cmdLoc(const std::string& args);
    ActionResult cmdSit(const std::string& args);
    ActionResult cmdStand(const std::string& args);
    ActionResult cmdMove(const std::string& args);
    ActionResult cmdMoveTo(const std::string& args);
    ActionResult cmdWarp(const std::string& args);
    ActionResult cmdZoning(const std::string& args);
    ActionResult cmdFollow(const std::string& args);
    ActionResult cmdStopFollow(const std::string& args);
    ActionResult cmdQuit(const std::string& args);
    ActionResult cmdCamp(const std::string& args);
    ActionResult cmdDebug(const std::string& args);
    ActionResult cmdPerf(const std::string& args);
    ActionResult cmdTime(const std::string& args);
    ActionResult cmdBank(const std::string& args);
    ActionResult cmdTrain(const std::string& args);

    // T148: GM (9). Net-new, Guild's shape (T132) -- action::CommandProcessor
    // had ZERO GM registrations, and neither did the legacy `game` registry:
    // all nine EverQuest::SendGM*() senders (src/client/eq_gm.cpp) had zero
    // production callers, which is why T146 could not exercise /kick or /kill
    // at all. Registered here ONLY (never in the legacy registry -- T97's
    // shadowing bug).
    //
    // Every one is gated by ActionDispatcher::checkGMMode(), the login-time
    // PlayerProfile.gm @13054 check. Registration itself is UNCONDITIONAL on
    // purpose: a non-GM typing /kick must learn the command exists and is
    // refused, not that it does not exist.
    ActionResult cmdGMZone(const std::string& args);
    ActionResult cmdGMSummon(const std::string& args);
    ActionResult cmdGMGoto(const std::string& args);
    ActionResult cmdGMFind(const std::string& args);
    ActionResult cmdGMKick(const std::string& args);
    ActionResult cmdGMKill(const std::string& args);
    ActionResult cmdGMHideMe(const std::string& args);
    ActionResult cmdGMEmoteZone(const std::string& args);
    ActionResult cmdGMLastName(const std::string& args);

    // T153: /appearance <field> <0-255> [...] -- parses and range-checks the
    // named face/hair/beard fields. The merge with the character's current
    // appearance happens in EqActionHandler, not here.
    ActionResult cmdAppearance(const std::string& args);
    ActionResult cmdInspectMessage(const std::string& args);  // T171

    // Shared helper: resolve a target spawn id for the Combat/Social
    // commands above that take an optional player name and otherwise fall
    // back to the current combat target (m_state.combat(), kept live by
    // Application::syncCombatState() -- a per-frame CombatManager poll,
    // NOT GameStateSyncBridge; see this unit's Review for why that's the
    // correct read path here). Returns 0 (with *outError set) if neither a
    // name match nor a current target is available.
    uint16_t resolveCombatTarget(const std::string& name, std::string* outError);

    // T98: cmdWho/cmdHelp/cmdQuit/cmdDebug/cmdTimestamp removed -- shadowed
    // dead code (who/help/quit/q/debug/timestamp).

    ActionResult cmdDoor(const std::string& args);
    ActionResult cmdAFK(const std::string& args);
    ActionResult cmdAnon(const std::string& args);

    // T101: cmdGems/cmdMem/cmdForget/cmdCast/cmdInterrupt/cmdSpellbook
    // migrated here from EverQuest::m_command_registry (T98 had removed
    // action's shadowed dead copies of gems/mem/forget/cast/interrupt;
    // spellbook was never shadowed -- it simply didn't exist on this side).
    // The corresponding game registrations are removed from
    // eq_commands_spells.cpp in the same change (atomicity). cmdSkills
    // stays NOT migrated: game's /skills is registered category "Utility",
    // not "Spells" -- outside this unit's confirmed membership. See
    // refactor/T101_migrate_spells_pet_family.md.
    ActionResult cmdCast(const std::string& args);
    ActionResult cmdMem(const std::string& args);
    ActionResult cmdForget(const std::string& args);
    ActionResult cmdGems(const std::string& args);
    ActionResult cmdInterrupt(const std::string& args);
    ActionResult cmdSpellbook(const std::string& args);

    // T101: cmdPet migrated here -- T98's shadowed dead copy is gone;
    // game's registration removed from eq_commands_spells.cpp in the same
    // change. game's /pet vocabulary was a strict superset of the
    // ActionDispatcher wrapper set even after T96's literal fixes (T97);
    // this port carries the full vocabulary, using 5 new ActionDispatcher
    // wrappers (petStand/petTauntOff/petHoldOff/petLeader/petSpellHold) for
    // the subcommands that had none.
    ActionResult cmdPet(const std::string& args);

    // T98: cmdFilter removed -- shadowed dead code (filter).

    ActionResult cmdLogOnly(const std::string& args);
    ActionResult cmdLogExclude(const std::string& args);
    ActionResult cmdLogModule(const std::string& args);
    ActionResult cmdLogClear(const std::string& args);

    // Extended commands from main.cpp ProcessCommand
    ActionResult cmdWalk(const std::string& args);
    ActionResult cmdRun(const std::string& args);
    ActionResult cmdSneak(const std::string& args);
    ActionResult cmdCrouch(const std::string& args);
    ActionResult cmdFeign(const std::string& args);
    ActionResult cmdJump(const std::string& args);
    ActionResult cmdTurn(const std::string& args);
    ActionResult cmdList(const std::string& args);
    ActionResult cmdDump(const std::string& args);
    ActionResult cmdHunt(const std::string& args);
    ActionResult cmdAutoLoot(const std::string& args);
    ActionResult cmdListTargets(const std::string& args);
    ActionResult cmdLoot(const std::string& args);
    ActionResult cmdLootAll(const std::string& args);    // T268
    ActionResult cmdLootClose(const std::string& args);  // T268
    ActionResult cmdPathfinding(const std::string& args);
    ActionResult cmdRoleplay(const std::string& args);
    ActionResult cmdWave(const std::string& args);
    ActionResult cmdDance(const std::string& args);
    ActionResult cmdCheer(const std::string& args);
    ActionResult cmdLaugh(const std::string& args);

    // T150: Social / Find family -- four commands over four previously
    // zero-caller senders. groupassist/track/find take a name or the current
    // target via resolveCombatTarget(); friendswho takes a comma-delimited
    // NAME list, because that is literally what the server parses.
    ActionResult cmdGroupAssist(const std::string& args);
    ActionResult cmdTrack(const std::string& args);
    ActionResult cmdFind(const std::string& args);
    ActionResult cmdFriendsWho(const std::string& args);

    // Helper methods
    static std::string toLower(const std::string& str);
    static std::pair<std::string, std::string> parseCommandLine(const std::string& input);
    static std::vector<std::string> splitArgs(const std::string& args);
    void displayMessage(const std::string& message);
    void displayError(const std::string& message);
    // T98: displayHelp() removed -- its only caller was cmdHelp (deleted, see
    // refactor/T98_delete_shadowed_commands.md); getCommandInfo()/
    // getCategories()/getCommandsByCategory() it used are public API used
    // elsewhere and are untouched.
};

} // namespace action
} // namespace eqt
