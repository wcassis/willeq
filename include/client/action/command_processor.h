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
 * - Chat: /say, /shout, /ooc, /auction, /tell, /reply, /gsay, /gu
 * - Movement: /loc, /sit, /stand, /camp, /move, /moveto, /follow
 * - Combat: /target, /attack, /stopattack, /cast, /aa
 * - Group: /invite, /disband, /decline
 * - Utility: /who, /help, /quit, /debug
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
    ActionResult cmdSay(const std::string& args);
    ActionResult cmdShout(const std::string& args);
    ActionResult cmdOOC(const std::string& args);
    ActionResult cmdAuction(const std::string& args);
    ActionResult cmdTell(const std::string& args);
    ActionResult cmdReply(const std::string& args);
    ActionResult cmdGroupSay(const std::string& args);
    ActionResult cmdGuildSay(const std::string& args);
    ActionResult cmdEmote(const std::string& args);

    ActionResult cmdLoc(const std::string& args);
    ActionResult cmdSit(const std::string& args);
    ActionResult cmdStand(const std::string& args);
    ActionResult cmdCamp(const std::string& args);
    ActionResult cmdMove(const std::string& args);
    ActionResult cmdMoveTo(const std::string& args);
    ActionResult cmdFollow(const std::string& args);
    ActionResult cmdStopFollow(const std::string& args);
    ActionResult cmdFace(const std::string& args);

    ActionResult cmdTarget(const std::string& args);
    ActionResult cmdAttack(const std::string& args);
    ActionResult cmdStopAttack(const std::string& args);
    ActionResult cmdAutoAttack(const std::string& args);
    ActionResult cmdCast(const std::string& args);
    ActionResult cmdInterrupt(const std::string& args);
    ActionResult cmdConsider(const std::string& args);
    ActionResult cmdHail(const std::string& args);

    ActionResult cmdInvite(const std::string& args);
    ActionResult cmdDisband(const std::string& args);
    ActionResult cmdDecline(const std::string& args);

    ActionResult cmdWho(const std::string& args);
    ActionResult cmdHelp(const std::string& args);
    ActionResult cmdQuit(const std::string& args);
    ActionResult cmdDebug(const std::string& args);
    ActionResult cmdTimestamp(const std::string& args);

    ActionResult cmdDoor(const std::string& args);
    ActionResult cmdAFK(const std::string& args);
    ActionResult cmdAnon(const std::string& args);

    ActionResult cmdSkills(const std::string& args);
    ActionResult cmdGems(const std::string& args);
    ActionResult cmdMem(const std::string& args);
    ActionResult cmdForget(const std::string& args);

    ActionResult cmdPet(const std::string& args);

    ActionResult cmdFilter(const std::string& args);

    // Helper methods
    static std::string toLower(const std::string& str);
    static std::pair<std::string, std::string> parseCommandLine(const std::string& input);
    static std::vector<std::string> splitArgs(const std::string& args);
    void displayMessage(const std::string& message);
    void displayError(const std::string& message);
    void displayHelp(const std::string& commandName = "");
};

} // namespace action
} // namespace eqt
