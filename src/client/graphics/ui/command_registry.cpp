#include "client/graphics/ui/command_registry.h"
#include "client/command_completion.h"  // T362 (B069): the shared prefix idiom
#include <algorithm>
#include <cctype>

namespace eqt {
namespace ui {

CommandRegistry::CommandRegistry() {
    // Register default categories
    registerCategory("Chat", "Chat and communication commands", 0);
    registerCategory("Movement", "Movement and position commands", 1);
    registerCategory("Combat", "Combat and targeting commands", 2);
    registerCategory("Character", "Character status commands", 3);
    registerCategory("Social", "Social and group commands", 4);
    registerCategory("Utility", "Utility and system commands", 5);
}

void CommandRegistry::registerCommand(const Command& cmd) {
    // Store command under its primary name (lowercase)
    std::string primaryName = toLower(cmd.name);
    commands_[primaryName] = cmd;

    // Map all aliases to the primary name
    for (const auto& alias : cmd.aliases) {
        std::string lowerAlias = toLower(alias);
        aliasMap_[lowerAlias] = primaryName;
    }
}

void CommandRegistry::registerCategory(const std::string& name, const std::string& description, int order) {
    CommandCategory cat;
    cat.name = name;
    cat.description = description;
    cat.displayOrder = order;
    categories_[name] = cat;
}

bool CommandRegistry::executeCommand(const std::string& input) {
    if (input.empty()) {
        return false;
    }

    // Parse command and arguments
    auto [cmdName, args] = parseInput(input);

    // Find the command
    const Command* cmd = findCommand(cmdName);
    if (!cmd) {
        return false;
    }

    // Check if arguments are required but not provided
    if (cmd->requiresArgs && args.empty()) {
        // Could add error handling here
        return false;
    }

    // Execute the command
    if (cmd->handler) {
        cmd->handler(args);
    }

    return true;
}

const Command* CommandRegistry::findCommand(const std::string& name) const {
    std::string lowerName = toLower(name);

    // Check if it's a primary command name
    auto it = commands_.find(lowerName);
    if (it != commands_.end()) {
        return &it->second;
    }

    // Check if it's an alias
    auto aliasIt = aliasMap_.find(lowerName);
    if (aliasIt != aliasMap_.end()) {
        auto cmdIt = commands_.find(aliasIt->second);
        if (cmdIt != commands_.end()) {
            return &cmdIt->second;
        }
    }

    return nullptr;
}

std::vector<const Command*> CommandRegistry::getAllCommands() const {
    std::vector<const Command*> result;
    for (const auto& [name, cmd] : commands_) {
        result.push_back(&cmd);
    }
    return result;
}

std::vector<const Command*> CommandRegistry::getCommandsByCategory(const std::string& category) const {
    std::vector<const Command*> result;
    for (const auto& [name, cmd] : commands_) {
        if (cmd.category == category) {
            result.push_back(&cmd);
        }
    }
    // Sort by name
    std::sort(result.begin(), result.end(), [](const Command* a, const Command* b) {
        return a->name < b->name;
    });
    return result;
}

std::vector<std::string> CommandRegistry::getCategories() const {
    std::vector<std::pair<std::string, int>> cats;
    for (const auto& [name, cat] : categories_) {
        cats.emplace_back(name, cat.displayOrder);
    }
    // Sort by display order
    std::sort(cats.begin(), cats.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    std::vector<std::string> result;
    for (const auto& [name, order] : cats) {
        result.push_back(name);
    }
    return result;
}

// T362 (B069): this was the DRIFTED half -- the same prefix idiom over the
// same two map shapes as action::CommandProcessor::getCompletions(), sorted
// but never de-duplicated, so a command registered with its own name as an
// alias came back twice. Both now call the one shared helper, which
// de-duplicates.
std::vector<std::string> CommandRegistry::getCompletions(const std::string& partial) const {
    return eqt::completion::PrefixCompletions(
        partial, commands_,
        [](const Command& cmd) { return cmd.name; },  // original case
        aliasMap_);
}

std::vector<std::string> CommandRegistry::getAllCommandNames() const {
    std::vector<std::string> result;

    // Add primary names
    for (const auto& [name, cmd] : commands_) {
        result.push_back(cmd.name);
    }

    // Add aliases
    for (const auto& [alias, primaryName] : aliasMap_) {
        result.push_back(alias);
    }

    std::sort(result.begin(), result.end());
    return result;
}

// T362 (B069): one implementation, shared with
// action::CommandProcessor::toLower.
std::string CommandRegistry::toLower(const std::string& str) {
    return eqt::completion::ToLower(str);
}

std::pair<std::string, std::string> CommandRegistry::parseInput(const std::string& input) {
    // Find first space to separate command from arguments
    size_t spacePos = input.find(' ');

    if (spacePos == std::string::npos) {
        // No arguments
        return {input, ""};
    }

    std::string cmd = input.substr(0, spacePos);
    std::string args = input.substr(spacePos + 1);

    // Trim leading spaces from args
    size_t start = args.find_first_not_of(' ');
    if (start != std::string::npos) {
        args = args.substr(start);
    } else {
        args = "";
    }

    return {cmd, args};
}

} // namespace ui
} // namespace eqt
