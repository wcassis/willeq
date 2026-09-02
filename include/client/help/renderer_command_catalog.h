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

} // namespace help
} // namespace eqt
