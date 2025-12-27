#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace eqt {
namespace ui {

// Command handler function type
using CommandHandler = std::function<void(const std::string& args)>;

// Command definition structure
struct Command {
    std::string name;                      // Primary name (e.g., "say")
    std::vector<std::string> aliases;      // Alternative names (e.g., {"s"})
    std::string usage;                     // Usage string for help (e.g., "/say <message>")
    std::string description;               // Short description
    std::string detailedHelp;              // Long description with examples
    std::string category;                  // Category for grouping in help (e.g., "Chat", "Movement")
    bool requiresArgs = false;             // True if command needs arguments
    CommandHandler handler;                // Function to execute the command
};

// Command category for grouping in help display
struct CommandCategory {
    std::string name;
    std::string description;
    int displayOrder = 0;  // Lower numbers shown first
};

// Registry for all chat commands
class CommandRegistry {
public:
    CommandRegistry();
    ~CommandRegistry() = default;

    // Register a command
    void registerCommand(const Command& cmd);

    // Register a category (for help display ordering)
    void registerCategory(const std::string& name, const std::string& description, int order = 0);

    // Execute a command from input string (without leading /)
    // Returns true if command was found and executed
    bool executeCommand(const std::string& input);

    // Find a command by name or alias
    const Command* findCommand(const std::string& name) const;

    // Get all registered commands
    std::vector<const Command*> getAllCommands() const;

    // Get commands by category
    std::vector<const Command*> getCommandsByCategory(const std::string& category) const;

    // Get all categories in display order
    std::vector<std::string> getCategories() const;

    // Get completions for partial command name
    std::vector<std::string> getCompletions(const std::string& partial) const;

    // Get all command names (including aliases)
    std::vector<std::string> getAllCommandNames() const;

private:
    // Map from command name/alias (lowercase) to command
    std::map<std::string, Command> commands_;

    // Map from alias to primary command name
    std::map<std::string, std::string> aliasMap_;

    // Categories for help display
    std::map<std::string, CommandCategory> categories_;

    // Helper to convert string to lowercase
    static std::string toLower(const std::string& str);

    // Parse command and arguments from input
    static std::pair<std::string, std::string> parseInput(const std::string& input);
};

} // namespace ui
} // namespace eqt
