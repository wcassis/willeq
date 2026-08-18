#include "client/help/merged_help.h"

#include "client/graphics/ui/command_registry.h"
#include "client/action/command_processor.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace eqt {
namespace help {

namespace {

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return out;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    return ToLower(a) == ToLower(b);
}

// All lowercase identifiers (primary name + every alias) registry 1 answers
// to. Used to detect registry-2 commands that are shadowed -- reachable
// only through registry 1 in the real dispatch order (Application::
// dispatchToActionOrRenderer tries registry 1 first via EverQuest::
// DispatchSlashCommand before ever trying registry 2), so a registry-2
// entry whose identifier collides is invisible in practice and must not be
// listed as if it were reachable.
std::set<std::string> CollectGameIdentifiers(const ui::CommandRegistry* game) {
    std::set<std::string> ids;
    if (!game) {
        return ids;
    }
    for (const auto* cmd : game->getAllCommands()) {
        if (!cmd) continue;
        ids.insert(ToLower(cmd->name));
        for (const auto& alias : cmd->aliases) {
            ids.insert(ToLower(alias));
        }
    }
    return ids;
}

bool IsShadowed(const action::CommandInfo& cmd, const std::set<std::string>& gameIds) {
    if (gameIds.count(ToLower(cmd.name))) {
        return true;
    }
    for (const auto& alias : cmd.aliases) {
        if (gameIds.count(ToLower(alias))) {
            return true;
        }
    }
    return false;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) out += ", ";
        out += n;
    }
    return out;
}

std::string JoinAliases(const std::vector<std::string>& aliases) {
    std::string out;
    for (const auto& a : aliases) {
        if (!out.empty()) out += ", ";
        out += "/" + a;
    }
    return out;
}

// Merged category display order: registry 1's categories in registry 1's
// own display order (unchanged from the pre-T143 behaviour), followed by
// any registry-2-only categories in registry 2's (alphabetical) order.
// Registry 1 is not reordered by this merge -- only appended to.
std::vector<std::string> MergedCategories(const ui::CommandRegistry* game,
                                           const action::CommandProcessor* action) {
    std::vector<std::string> categories;
    std::set<std::string> seen;
    if (game) {
        for (const auto& cat : game->getCategories()) {
            categories.push_back(cat);
            seen.insert(cat);
        }
    }
    if (action) {
        for (const auto& cat : action->getCategories()) {
            if (!seen.count(cat)) {
                categories.push_back(cat);
                seen.insert(cat);
            }
        }
    }
    return categories;
}

std::vector<std::string> BuildCategoryOverview(const ui::CommandRegistry* game,
                                                const action::CommandProcessor* action) {
    std::vector<std::string> lines;
    lines.push_back("=== Available Commands ===");

    const std::set<std::string> gameIds = CollectGameIdentifiers(game);
    for (const auto& cat : MergedCategories(game, action)) {
        std::vector<std::string> names;
        if (game) {
            for (const auto* cmd : game->getCommandsByCategory(cat)) {
                if (cmd) names.push_back(cmd->name);
            }
        }
        if (action) {
            for (const auto& info : action->getCommandsByCategory(cat)) {
                if (!IsShadowed(info, gameIds)) {
                    names.push_back(info.name);
                }
            }
        }
        if (!names.empty()) {
            lines.push_back(fmt::format("{}: {}", cat, JoinNames(names)));
        }
    }

    lines.push_back("Type /help <command> for detailed help.");
    return lines;
}

std::vector<std::string> BuildCategoryDetail(const std::string& category,
                                              const ui::CommandRegistry* game,
                                              const action::CommandProcessor* action) {
    std::vector<std::string> lines;
    lines.push_back(fmt::format("=== {} ===", category));

    const std::set<std::string> gameIds = CollectGameIdentifiers(game);
    if (game) {
        for (const auto* cmd : game->getCommandsByCategory(category)) {
            if (!cmd) continue;
            lines.push_back(fmt::format("/{} - {} (game)", cmd->name, cmd->description));
        }
    }
    if (action) {
        for (const auto& info : action->getCommandsByCategory(category)) {
            if (IsShadowed(info, gameIds)) continue;
            lines.push_back(fmt::format("/{} - {} (action)", info.name, info.description));
        }
    }
    return lines;
}

std::vector<std::string> BuildCommandDetail(const std::string& name,
                                             const ui::CommandRegistry* game,
                                             const action::CommandProcessor* action) {
    std::vector<std::string> lines;

    // Registry 1 is tried first, matching the real dispatch order -- see
    // Application::dispatchToActionOrRenderer's own doc comment. A name (or
    // alias) present in both registries is answered by registry 1 here for
    // the same reason it is the one actually reached at runtime.
    if (game) {
        if (const auto* cmd = game->findCommand(name)) {
            lines.push_back(fmt::format("=== {} ===", cmd->name));
            lines.push_back(cmd->description);
            lines.push_back(fmt::format("Usage: {}", cmd->usage));
            if (!cmd->aliases.empty()) {
                lines.push_back(fmt::format("Aliases: {}", JoinAliases(cmd->aliases)));
            }
            lines.push_back("Registry: game");
            return lines;
        }
    }

    if (action) {
        if (auto info = action->getCommandInfo(name)) {
            lines.push_back(fmt::format("=== {} ===", info->name));
            lines.push_back(info->description);
            lines.push_back(fmt::format("Usage: {}", info->usage));
            if (!info->aliases.empty()) {
                lines.push_back(fmt::format("Aliases: {}", JoinAliases(info->aliases)));
            }
            lines.push_back("Registry: action");
            return lines;
        }
    }

    lines.push_back(fmt::format("Unknown command: {}", name));
    return lines;
}

} // namespace

bool IsHelpCommandName(const std::string& name) {
    const std::string lower = ToLower(name);
    return lower == "help" || lower == "h" || lower == "?";
}

HelpCoverage ComputeHelpCoverage(const ui::CommandRegistry* game,
                                  const action::CommandProcessor* action) {
    HelpCoverage cov;
    if (game) {
        cov.gameCount = game->getAllCommands().size();
    }
    if (action) {
        const std::set<std::string> gameIds = CollectGameIdentifiers(game);
        for (const auto& info : action->getAllCommands()) {
            ++cov.actionCount;
            if (IsShadowed(info, gameIds)) {
                ++cov.dedupedCount;
            }
        }
    }
    cov.totalUnique = cov.gameCount + cov.actionCount - cov.dedupedCount;
    return cov;
}

HelpResponse BuildHelpResponse(const std::string& cmdBody,
                                const ui::CommandRegistry* game,
                                const action::CommandProcessor* action) {
    HelpResponse response;

    size_t spacePos = cmdBody.find(' ');
    std::string name = (spacePos == std::string::npos) ? cmdBody : cmdBody.substr(0, spacePos);
    if (!IsHelpCommandName(name)) {
        return response;  // handled = false
    }
    response.handled = true;

    std::string args;
    if (spacePos != std::string::npos) {
        size_t start = cmdBody.find_first_not_of(' ', spacePos + 1);
        if (start != std::string::npos) {
            args = cmdBody.substr(start);
        }
    }

    if (args.empty()) {
        response.lines = BuildCategoryOverview(game, action);
        return response;
    }

    // A bare argument may name a category (new: neither registry's /help
    // supported this before T143 -- there was nothing to merge a category
    // query across) or a command/alias (the pre-existing behaviour,
    // preserved and simply extended across both registries).
    for (const auto& cat : MergedCategories(game, action)) {
        if (EqualsIgnoreCase(cat, args)) {
            response.lines = BuildCategoryDetail(cat, game, action);
            return response;
        }
    }

    response.lines = BuildCommandDetail(args, game, action);
    return response;
}

} // namespace help
} // namespace eqt
