#pragma once

// T223: a queryable catalog for registry 3 -- the renderer's
// IrrlichtRenderer::processSlashCommand() if/else chain.
//
// Registry 3 had no metadata at all (T143): its commands are plain
// `cmd == "/x"` branches on the RENDER thread, so /help could not list them
// and Application::dispatchToActionOrRenderer() could not tell a real
// renderer command from a typo -- both were "queued" (T139/T142).
//
// This table is the catalog. It is deliberately graphics-independent (no
// Irrlicht, no renderer symbols) so the GAME thread can read it and so it
// links into headless (EQT_GRAPHICS=OFF) builds -- that is exactly what lets
// a --no-graphics run refuse a renderer command by name instead of queueing
// it to a bridge nobody is listening on.
//
// DRIFT GUARD: the if/else chain is still the dispatcher; this table is not
// derived from it at compile time. tests/test_merged_help.cpp parses
// renderer_slash_commands.cpp's `cmd == "/name"` literals and asserts the two
// sets are identical, so adding a branch without a catalog entry (or the
// reverse) fails the suite rather than silently drifting.

#include <string>
#include <vector>

namespace eqt {
namespace help {

struct RendererCommandInfo {
    std::string name;                  // primary name, no leading '/'
    std::vector<std::string> aliases;  // no leading '/'
    std::string description;
};

// The /help category every renderer command is listed under.
const std::string& RendererCommandCategory();

// Every registry-3 command, in the if/else chain's own order.
const std::vector<RendererCommandInfo>& AllRendererCommands();

// Case-insensitive lookup by primary name or alias (with or without a
// leading '/'). Null if registry 3 does not know the name.
const RendererCommandInfo* FindRendererCommand(const std::string& name);

// T365 (B091): registry 3's half of Tab completion. Every catalog name and
// alias starting with `partial` (case-insensitive), sorted and de-duplicated,
// in the same shape registries 1 and 2 return -- it is literally the same
// body, eqt::completion::PrefixCompletions (include/client/command_completion.h).
//
// Registry 3 had no getCompletions() at all, so `Application::commandCompletions`
// unioned only registries 1 and 2 while `/help` listed all three: none of the
// ~70 renderer commands was tab-completable. This closes that gap.
std::vector<std::string> RendererCommandCompletions(const std::string& partial);

} // namespace help
} // namespace eqt
