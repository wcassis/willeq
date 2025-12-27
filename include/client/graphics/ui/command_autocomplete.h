#pragma once

#include <string>
#include <vector>
#include <functional>

namespace eqt {
namespace ui {

class CommandRegistry;

// Callback to get player/entity names for completion
using EntityNameProvider = std::function<std::vector<std::string>()>;

// Auto-completion for chat commands and player names
class CommandAutoComplete {
public:
    CommandAutoComplete();
    ~CommandAutoComplete() = default;

    // Set the command registry for command completion
    void setCommandRegistry(CommandRegistry* registry) { commandRegistry_ = registry; }

    // Set the entity name provider for /tell completion
    void setEntityNameProvider(EntityNameProvider provider) { entityNameProvider_ = provider; }

    // Get completions for the current input
    // Returns list of possible completions (full command/name strings)
    std::vector<std::string> getCompletions(const std::string& input);

    // Tab pressed - apply next completion
    // Returns the new complete input string, or empty if no completions
    std::string applyNextCompletion(const std::string& input);

    // Shift+Tab - apply previous completion
    std::string applyPrevCompletion(const std::string& input);

    // Reset completion state (called when input changes by typing)
    void reset();

    // Check if we're currently in completion mode
    bool isActive() const { return !currentCompletions_.empty(); }

    // Get current completion index (for display purposes)
    size_t getCompletionIndex() const { return completionIndex_; }
    size_t getCompletionCount() const { return currentCompletions_.size(); }

private:
    // Generate completions for the given input
    void generateCompletions(const std::string& input);

    // Get command completions (for /cmd)
    std::vector<std::string> getCommandCompletions(const std::string& partial);

    // Get player name completions (for /tell <name>)
    std::vector<std::string> getPlayerNameCompletions(const std::string& partial);

    // Apply the current completion to the input
    std::string applyCompletion(const std::string& input);

    // The command registry for command completions
    CommandRegistry* commandRegistry_ = nullptr;

    // Entity name provider for player name completions
    EntityNameProvider entityNameProvider_;

    // Current completion state
    std::vector<std::string> currentCompletions_;
    size_t completionIndex_ = 0;
    std::string originalInput_;      // Input before any completion applied
    std::string lastCompletedInput_; // Input after completion was applied

    // What type of completion we're doing
    enum class CompletionType {
        None,
        Command,    // /cmd...
        PlayerName  // /tell <name>...
    };
    CompletionType completionType_ = CompletionType::None;

    // For player name completion, track the command prefix
    std::string commandPrefix_;  // e.g., "/tell " for player name completion
};

} // namespace ui
} // namespace eqt
