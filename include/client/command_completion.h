#pragma once

// T362 (B069): the ONE slash-command prefix-completion idiom.
//
// It was written twice. `action::CommandProcessor::getCompletions()`
// (registry 2, src/client/action/command_processor.cpp) and
// `ui::CommandRegistry::getCompletions()` (registry 1,
// src/client/graphics/ui/command_registry.cpp) each walked their own
// lowercase-keyed command map matching `name.find(lowerPartial) == 0`, then
// their own lowercase-keyed alias map the same way, each lowered the partial
// with its own private static toLower, and each sorted. Then they DRIFTED by
// exactly one line: CommandProcessor de-duplicated (std::unique after the
// sort), CommandRegistry did not -- so a CommandRegistry command registered
// with its own name as an alias came back TWICE. T357 hid that from the one
// production caller (Application::commandCompletions() sorts and
// de-duplicates the union of both) without fixing it; any other caller still
// saw the duplicate. Both now call this.
//
// WHERE IT LIVES, and why: `include/client/` root, in its own namespace,
// rather than under `include/client/action/` or `include/client/graphics/ui/`.
// The two callers sit on opposite sides of a boundary the tree keeps clean --
// registry 1 is UI, registry 2 is the action layer, and neither includes the
// other's headers. Putting the helper in either directory would have created
// a new include edge in one of those directions purely to share nine lines of
// string matching. It is header-only and templated on the command map's
// mapped type (the two maps hold different value types -- `RegisteredCommand`
// with `.info.name` and `ui::Command` with `.name`), so it adds no link
// dependency to anything: no new TU, no CMake change, no library edge.
// `include/client/help/` was the other candidate -- it is the established
// neutral home for logic that spans both registries -- but tab completion is
// not help, and merged_help.h actually includes both registries, which this
// deliberately does not.

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace eqt {
namespace completion {

/// ASCII lowercase. Both registries key their command and alias maps with
/// this, and both used to carry a private copy of it.
inline std::string ToLower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// Every command name and alias starting with `partial`, sorted and
/// de-duplicated.
///
/// `commandsByLowerName` must be keyed by the LOWERCASE primary name;
/// `displayNameOf(value)` yields the original-case name to offer the user.
/// `aliasesByLowerAlias` must be keyed by the LOWERCASE alias, which is what
/// is offered for an alias hit (there is no original-case alias to recover --
/// both registries lowercase aliases at registration).
///
/// An empty `partial` matches everything, which is what both callers'
/// existing tests assert.
template <typename CommandMap, typename DisplayNameOf>
std::vector<std::string> PrefixCompletions(const std::string& partial,
                                           const CommandMap& commandsByLowerName,
                                           DisplayNameOf displayNameOf,
                                           const std::map<std::string, std::string>& aliasesByLowerAlias)
{
    const std::string lowerPartial = ToLower(partial);
    std::vector<std::string> result;

    for (const auto& entry : commandsByLowerName) {
        if (entry.first.find(lowerPartial) == 0) {
            result.push_back(displayNameOf(entry.second));
        }
    }

    for (const auto& entry : aliasesByLowerAlias) {
        if (entry.first.find(lowerPartial) == 0) {
            result.push_back(entry.first);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

}  // namespace completion
}  // namespace eqt
