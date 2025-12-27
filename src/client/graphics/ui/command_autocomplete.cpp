#include "client/graphics/ui/command_autocomplete.h"
#include "client/graphics/ui/command_registry.h"
#include <algorithm>
#include <cctype>

namespace eqt {
namespace ui {

CommandAutoComplete::CommandAutoComplete() = default;

std::vector<std::string> CommandAutoComplete::getCompletions(const std::string& input) {
    generateCompletions(input);
    return currentCompletions_;
}

std::string CommandAutoComplete::applyNextCompletion(const std::string& input) {
    // If input changed since last completion, regenerate
    if (currentCompletions_.empty() || input != lastCompletedInput_) {
        originalInput_ = input;
        generateCompletions(input);
    }

    if (currentCompletions_.empty()) {
        return input;  // No completions available
    }

    // Move to next completion (wrapping around)
    if (!lastCompletedInput_.empty() && input == lastCompletedInput_) {
        completionIndex_ = (completionIndex_ + 1) % currentCompletions_.size();
    }

    return applyCompletion(input);
}

std::string CommandAutoComplete::applyPrevCompletion(const std::string& input) {
    // If input changed since last completion, regenerate
    if (currentCompletions_.empty() || input != lastCompletedInput_) {
        originalInput_ = input;
        generateCompletions(input);
    }

    if (currentCompletions_.empty()) {
        return input;  // No completions available
    }

    // Move to previous completion (wrapping around)
    if (!lastCompletedInput_.empty() && input == lastCompletedInput_) {
        if (completionIndex_ == 0) {
            completionIndex_ = currentCompletions_.size() - 1;
        } else {
            completionIndex_--;
        }
    }

    return applyCompletion(input);
}

void CommandAutoComplete::reset() {
    currentCompletions_.clear();
    completionIndex_ = 0;
    originalInput_.clear();
    lastCompletedInput_.clear();
    completionType_ = CompletionType::None;
    commandPrefix_.clear();
}

void CommandAutoComplete::generateCompletions(const std::string& input) {
    currentCompletions_.clear();
    completionIndex_ = 0;
    completionType_ = CompletionType::None;
    commandPrefix_.clear();

    if (input.empty()) {
        return;
    }

    // Check if this is a command (starts with /)
    if (input[0] == '/') {
        // Find the first space to determine if we're completing the command or an argument
        size_t spacePos = input.find(' ');

        if (spacePos == std::string::npos) {
            // No space - completing the command name itself
            completionType_ = CompletionType::Command;
            std::string partial = input.substr(1);  // Remove leading /
            currentCompletions_ = getCommandCompletions(partial);
        } else {
            // Space found - check if this is a command that takes a player name
            std::string cmdName = input.substr(1, spacePos - 1);

            // Convert to lowercase for comparison
            std::string cmdLower = cmdName;
            std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            // Commands that take player names as first argument
            if (cmdLower == "tell" || cmdLower == "t" || cmdLower == "msg" ||
                cmdLower == "target" || cmdLower == "tar" ||
                cmdLower == "invite" || cmdLower == "inv" ||
                cmdLower == "follow" || cmdLower == "fol" ||
                cmdLower == "assist" || cmdLower == "a") {

                completionType_ = CompletionType::PlayerName;
                commandPrefix_ = input.substr(0, spacePos + 1);  // Include the space

                // Get the partial name typed so far
                std::string afterCommand = input.substr(spacePos + 1);

                // For tell, only complete up to the next space (the player name)
                size_t nextSpace = afterCommand.find(' ');
                if (nextSpace == std::string::npos) {
                    // Still typing player name
                    currentCompletions_ = getPlayerNameCompletions(afterCommand);
                }
                // If there's a space after the name, don't complete (they're typing message)
            }
        }
    }
}

std::vector<std::string> CommandAutoComplete::getCommandCompletions(const std::string& partial) {
    std::vector<std::string> result;

    if (!commandRegistry_) {
        return result;
    }

    // Get completions from registry
    std::vector<std::string> matches = commandRegistry_->getCompletions(partial);

    // Add the / prefix back
    for (const auto& match : matches) {
        result.push_back("/" + match);
    }

    return result;
}

std::vector<std::string> CommandAutoComplete::getPlayerNameCompletions(const std::string& partial) {
    std::vector<std::string> result;

    if (!entityNameProvider_) {
        return result;
    }

    // Get all entity names
    std::vector<std::string> allNames = entityNameProvider_();

    if (partial.empty()) {
        // Return all names if no partial typed
        result = allNames;
    } else {
        // Convert partial to lowercase for case-insensitive matching
        std::string partialLower = partial;
        std::transform(partialLower.begin(), partialLower.end(), partialLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Find names that start with partial (case-insensitive)
        for (const auto& name : allNames) {
            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (nameLower.find(partialLower) == 0) {
                result.push_back(name);
            }
        }
    }

    // Sort alphabetically
    std::sort(result.begin(), result.end());

    return result;
}

std::string CommandAutoComplete::applyCompletion(const std::string& input) {
    if (currentCompletions_.empty()) {
        return input;
    }

    std::string result;

    switch (completionType_) {
        case CompletionType::Command:
            // Replace the entire input with the completed command
            result = currentCompletions_[completionIndex_];
            break;

        case CompletionType::PlayerName:
            // Replace just the player name part
            result = commandPrefix_ + currentCompletions_[completionIndex_];
            break;

        default:
            result = input;
            break;
    }

    lastCompletedInput_ = result;
    return result;
}

} // namespace ui
} // namespace eqt
