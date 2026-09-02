#pragma once

// T143: registry-aware /help.
//
// EverQuest::m_command_registry ("game", registry 1) and
// action::CommandProcessor ("action", registry 2) expose the identical
// introspection API (getAllCommands()/getCommandsByCategory()/
// getCategories(), see both headers) but return different value types
// (Command* vs CommandInfo). Neither registry can see the other's catalog,
// so /help -- previously implemented purely inside registry 1 as a
// "stay-game" command -- only ever described registry 1's contents. This
// module merges both catalogs into one /help response.
//
// SCOPE (T223): registries 1 and 2 are merged from their live objects;
// registry 3 (the renderer's if/else chain reached via IrrlichtRenderer::
// processSlashCommand()) is described from the static catalog in
// renderer_command_catalog.h, tagged "(graphics)". Before T223 registry 3
// was invisible here (T143) because it had no catalog at all.
//
// Lives in its own TU (not folded into application.cpp, which was over the
// file-size warn threshold) so it is independently testable without
// linking Application, EverQuest, or Irrlicht -- see tests/
// test_merged_help.cpp.

#include <optional>
#include <string>
#include <vector>

namespace eqt {
namespace ui {
class CommandRegistry;
}
namespace action {
class CommandProcessor;
}

namespace help {

// True iff `name` (case-insensitive) is /help's own name or one of its
// aliases (h, ?). Application uses this to decide whether to intercept a
// command line before consulting registry 2 / registry 3, now that /help
// is no longer registered in registry 1 to intercept it there instead.
bool IsHelpCommandName(const std::string& name);

// Coverage counters for verification/reporting -- NOT used to build the
// displayed text, so tests can assert on exact counts independent of
// formatting.
struct HelpCoverage {
    size_t gameCount = 0;      // registry 1's getAllCommands().size()
    size_t actionCount = 0;    // registry 2's getAllCommands().size()
    // Registry-2 commands hidden from the merged view because their name
    // or an alias collides with a registry-1 identifier (game is tried
    // first in the real dispatch order, so the registry-1 version is the
    // one actually reachable -- see Application::dispatchToActionOrRenderer).
    size_t dedupedCount = 0;
    size_t rendererCount = 0;  // T223: registry 3's static catalog size
    // gameCount + actionCount - dedupedCount + rendererCount: the number of
    // distinct, reachable commands the merged /help can describe.
    size_t totalUnique = 0;
};

HelpCoverage ComputeHelpCoverage(const ui::CommandRegistry* game,
                                  const action::CommandProcessor* action);

// Result of a /help invocation: `handled` is false iff `cmdBody`'s first
// token isn't a /help name (see IsHelpCommandName) -- callers should fall
// through to the next dispatch stage in that case. `lines` is the response,
// one chat-system-message per entry, in display order.
struct HelpResponse {
    bool handled = false;
    std::vector<std::string> lines;
};

// `cmdBody` is the full command text with no leading '/' (e.g. "help",
// "help move", "h combat", "? move" -- matching the same looseness
// Application::dispatchToActionOrRenderer already applies to every other
// command name). `game` and/or `action` may be null (defensive -- neither
// registry is expected to be null in production, but tests build partial
// fixtures); a null registry simply contributes nothing to the merge.
HelpResponse BuildHelpResponse(const std::string& cmdBody,
                                const ui::CommandRegistry* game,
                                const action::CommandProcessor* action);

} // namespace help
} // namespace eqt
