#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
#include "client/skill/skill_constants.h"  // T156: /useskill id/name resolution
#include "client/inventory/inventory_constants.h"  // T278: /combine <general slot>
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cctype>

namespace eqt {
namespace action {

CommandProcessor::CommandProcessor(state::GameState& state, ActionDispatcher& dispatcher)
    : m_state(state)
    , m_dispatcher(dispatcher) {
    registerBuiltinCommands();
}

CommandProcessor::~CommandProcessor() = default;

void CommandProcessor::setOutputRenderer(output::IOutputRenderer* renderer) {
    m_renderer = renderer;
}

void CommandProcessor::setMessageSink(MessageSink sink) {  // T308
    m_messageSink = std::move(sink);
}

ActionResult CommandProcessor::processCommand(const std::string& input) {
    if (input.empty()) {
        return ActionResult::Success();
    }

    // Remove leading '/' if present
    std::string cmdLine = input;
    if (!cmdLine.empty() && cmdLine[0] == '/') {
        cmdLine = cmdLine.substr(1);
    }

    // Parse command and arguments
    auto [cmdName, args] = parseCommandLine(cmdLine);
    if (cmdName.empty()) {
        return ActionResult::Success();
    }

    // Echo the command if enabled
    if (m_echoEnabled) {
        emitMessage("/" + cmdName + (args.empty() ? "" : " " + args));
    }

    // Look up command
    std::string lowerName = toLower(cmdName);

    // Check aliases first
    auto aliasIt = m_aliases.find(lowerName);
    if (aliasIt != m_aliases.end()) {
        lowerName = aliasIt->second;
    }

    // Find command
    auto cmdIt = m_commands.find(lowerName);
    if (cmdIt == m_commands.end()) {
        displayError("Unknown command: " + cmdName);
        return ActionResult::Failure("Unknown command: " + cmdName);
    }

    const auto& cmd = cmdIt->second;

    // Check if arguments are required
    if (cmd.info.requiresArgs && args.empty()) {
        displayError("Usage: " + cmd.info.usage);
        return ActionResult::Failure("Missing arguments");
    }

    // Execute the command
    return cmd.handler(args);
}

ActionResult CommandProcessor::processInput(const std::string& input) {
    if (input.empty()) {
        return ActionResult::Success();
    }

    // If it starts with '/', treat as command
    if (input[0] == '/') {
        return processCommand(input);
    }

    // Otherwise, send as default channel message
    return m_dispatcher.sendChatMessage(m_defaultChannel, input);
}

void CommandProcessor::registerCommand(const CommandInfo& info, CommandHandler handler) {
    std::string lowerName = toLower(info.name);

    RegisteredCommand regCmd;
    regCmd.info = info;
    regCmd.handler = std::move(handler);

    m_commands[lowerName] = std::move(regCmd);

    // Register aliases
    for (const auto& alias : info.aliases) {
        m_aliases[toLower(alias)] = lowerName;
    }
}

void CommandProcessor::unregisterCommand(const std::string& name) {
    std::string lowerName = toLower(name);

    auto it = m_commands.find(lowerName);
    if (it != m_commands.end()) {
        // Remove aliases
        for (const auto& alias : it->second.info.aliases) {
            m_aliases.erase(toLower(alias));
        }
        m_commands.erase(it);
    }
}

bool CommandProcessor::hasCommand(const std::string& name) const {
    std::string lowerName = toLower(name);
    return m_commands.count(lowerName) > 0 || m_aliases.count(lowerName) > 0;
}

std::optional<CommandInfo> CommandProcessor::getCommandInfo(const std::string& name) const {
    std::string lowerName = toLower(name);

    // Check aliases
    auto aliasIt = m_aliases.find(lowerName);
    if (aliasIt != m_aliases.end()) {
        lowerName = aliasIt->second;
    }

    auto it = m_commands.find(lowerName);
    if (it != m_commands.end()) {
        return it->second.info;
    }
    return std::nullopt;
}

std::vector<CommandInfo> CommandProcessor::getAllCommands() const {
    std::vector<CommandInfo> result;
    result.reserve(m_commands.size());
    for (const auto& [name, cmd] : m_commands) {
        result.push_back(cmd.info);
    }
    return result;
}

std::vector<CommandInfo> CommandProcessor::getCommandsByCategory(const std::string& category) const {
    std::vector<CommandInfo> result;
    for (const auto& [name, cmd] : m_commands) {
        if (cmd.info.category == category) {
            result.push_back(cmd.info);
        }
    }
    return result;
}

std::vector<std::string> CommandProcessor::getCategories() const {
    std::vector<std::string> result;
    for (const auto& [name, cmd] : m_commands) {
        if (std::find(result.begin(), result.end(), cmd.info.category) == result.end()) {
            result.push_back(cmd.info.category);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CommandProcessor::getCompletions(const std::string& partial) const {
    std::vector<std::string> result;
    std::string lowerPartial = toLower(partial);

    for (const auto& [name, cmd] : m_commands) {
        if (name.find(lowerPartial) == 0) {
            result.push_back(cmd.info.name);
        }
    }

    for (const auto& [alias, primary] : m_aliases) {
        if (alias.find(lowerPartial) == 0) {
            result.push_back(alias);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::string> CommandProcessor::getAllCommandNames() const {
    std::vector<std::string> result;

    for (const auto& [name, cmd] : m_commands) {
        result.push_back(cmd.info.name);
        for (const auto& alias : cmd.info.aliases) {
            result.push_back(alias);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ========== Helper Methods ==========

std::string CommandProcessor::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::pair<std::string, std::string> CommandProcessor::parseCommandLine(const std::string& input) {
    if (input.empty()) {
        return {"", ""};
    }

    // Find first space
    size_t spacePos = input.find(' ');
    if (spacePos == std::string::npos) {
        return {input, ""};
    }

    std::string cmd = input.substr(0, spacePos);
    std::string args = input.substr(spacePos + 1);

    // Trim leading whitespace from args
    size_t start = args.find_first_not_of(" \t");
    if (start == std::string::npos) {
        args = "";
    } else {
        args = args.substr(start);
    }

    return {cmd, args};
}

std::vector<std::string> CommandProcessor::splitArgs(const std::string& args) {
    std::vector<std::string> result;
    std::istringstream iss(args);
    std::string token;
    while (iss >> token) {
        result.push_back(token);
    }
    return result;
}

// T308: registry 2's text now reaches the on-screen chat window as well as
// stdout. See setMessageSink() in the header for why stdout alone was a live
// user-visible defect and why both sinks are written rather than one swapped
// for the other.
void CommandProcessor::emitMessage(const std::string& message) {
    if (m_renderer) {
        m_renderer->displaySystemMessage(message);
    }
    if (m_messageSink) {
        m_messageSink(message);
    }
}

void CommandProcessor::displayMessage(const std::string& message) {
    emitMessage(message);
}

void CommandProcessor::displayError(const std::string& message) {
    emitMessage("Error: " + message);
}

// T98: displayHelp() removed -- its only caller was cmdHelp (deleted, the
// "help" registration was shadowed dead code). getCommandInfo()/
// getCategories()/getCommandsByCategory() it used are public API used
// elsewhere and are untouched.

} // namespace action
} // namespace eqt
