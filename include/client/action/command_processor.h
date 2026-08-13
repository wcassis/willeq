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
 * - Chat: /shout, /wave, /dance, /cheer, /laugh
 * - Movement: /face, /walk, /run, /sneak, /crouch, /feign, /jump, /turn
 * - Combat: /consider, /hail, /hunt, /autoloot, /listtargets, /loot
 * - Utility: /npcstate, /door, /afk, /anon, /list, /dump, /pathfinding,
 *   /roleplay, /logonly, /logexclude, /logmodule, /logclear
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
    // T98: cmdSay/cmdOOC/cmdAuction/cmdTell/cmdReply/cmdGroupSay/cmdGuildSay/
    // cmdEmote removed -- their registrations (say/ooc/auction/tell/reply/
    // gsay/gu/emote) were shadowed dead code (registry 1 always won). cmdShout
    // stays: its primary name is shadowed too, but alias "sh" escapes because
    // game's own shout alias is spelled "sho". See
    // refactor/T97_command_registry_shadowing_audit.md /
    // refactor/T98_delete_shadowed_commands.md.
    ActionResult cmdShout(const std::string& args);

    // T98: cmdLoc/cmdSit/cmdStand/cmdCamp/cmdMove/cmdMoveTo/cmdFollow/
    // cmdStopFollow removed -- shadowed dead code (loc/sit/stand/camp/move/
    // moveto/follow/stopfollow). cmdFace stays: no game "face" command exists
    // at all, so it was never shadowed.
    ActionResult cmdFace(const std::string& args);

    // T98: cmdTarget/cmdCast/cmdInterrupt removed -- shadowed dead code
    // (target/cast/interrupt). T51: cmdAttack/cmdStopAttack/cmdAutoAttack
    // removed earlier -- their command registrations were shadowed dead code
    // (registry 1 always won the dispatch collision). ActionDispatcher::
    // enableAutoAttack()/disableAutoAttack()/toggleAutoAttack() are untouched
    // and still used directly by InputActionBridge (the keyboard gesture).
    ActionResult cmdConsider(const std::string& args);
    ActionResult cmdHail(const std::string& args);

    // T49: reads m_state.player()'s vendor/banker/trainer fields -- the
    // state GameStateSyncBridge now feeds from bridge events instead of
    // Application::syncNPCInteractionState() (deleted). Lives here, not in
    // EverQuest::m_command_registry (where T48 put /vendor, /vendorlist,
    // /buy, /sell), because this reader must see Application::m_gameState --
    // the instance GameStateSyncBridge populates -- and
    // EverQuest::m_command_registry's commands read a *different*
    // state::GameState (EverQuest::m_game_state). See
    // refactor/T49_npc_interaction_state.md ("THE TRAP THAT DECIDES WHERE
    // THE READER LIVES") and refactor/deferred.md's "Two GameState
    // instances" entry.
    ActionResult cmdNpcState(const std::string& args);

    // T98: cmdInvite/cmdDisband/cmdDecline removed -- shadowed dead code
    // (invite/disband/decline). This empties the "Group" category in this
    // registry entirely; game's Group commands are the only reachable ones.

    // T98: cmdWho/cmdHelp/cmdQuit/cmdDebug/cmdTimestamp removed -- shadowed
    // dead code (who/help/quit/q/debug/timestamp).

    ActionResult cmdDoor(const std::string& args);
    ActionResult cmdAFK(const std::string& args);
    ActionResult cmdAnon(const std::string& args);

    // T98: cmdSkills/cmdGems/cmdMem/cmdForget removed -- shadowed dead code
    // (skills/gems/mem/forget). This empties the "Spells" category entirely.

    // T98: cmdPet removed -- shadowed dead code (pet). This empties the
    // "Pet" category entirely. ActionDispatcher's petAttack()/petBackOff()/
    // petFollow()/petGuard()/petSit()/petTaunt()/petHold()/petFocus()/
    // petHealth()/dismissPet() (the T96-fixed wrappers) are untouched -- see
    // refactor/T98_delete_shadowed_commands.md's cascade list.

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
    ActionResult cmdPathfinding(const std::string& args);
    ActionResult cmdRoleplay(const std::string& args);
    ActionResult cmdWave(const std::string& args);
    ActionResult cmdDance(const std::string& args);
    ActionResult cmdCheer(const std::string& args);
    ActionResult cmdLaugh(const std::string& args);

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
