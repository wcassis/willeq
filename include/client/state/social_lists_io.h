/*
 * The friends/ignore lists on disk (B897, GitLab #213): the read, the write,
 * and the ONE tick that decides when either happens.
 *
 * WHERE: `<cache dir>/ui/<Char>_<server>.ini` -- the official client's own
 * filename and its own [Friends] / [Ignored] sections (social_lists.h quotes
 * them), beside the `UI_<Char>_<server>.ini` T432 writes, through
 * eqt::ResolveCachePathForRead/Write (T402's rule: generated state lives
 * outside the checkout).
 *
 * WHY NOT UiPreferences. T432's file is the RENDERER's: FrameComponent loads
 * and saves it, so it does not exist in a --no-graphics run. These lists are
 * read by the game thread's chat filter and edited by registry-2 slash
 * commands, both of which run headless too, and the oracle keeps them in a
 * DIFFERENT file from its UI_ file. The friends window's own open/closed
 * state is renderer UI state and does go through UiPreferences
 * (`[FriendsWindow] Show=`). Same sanitising, same temp-file-and-rename
 * write, same "no file is not an error" rule as T432 -- only the owner and
 * the filename differ.
 *
 * WHEN: there is deliberately no save call at an edit site (T432's rule for
 * the UI file). SocialListsSync::tick() runs once per game tick
 * (BridgeComponent::GameTick) and compares the model's revision against the
 * one it last wrote, so the slash commands, the window's intents and any
 * future editor all persist through the same line.
 */

#pragma once

#include <string>

#include "client/state/social_lists.h"

namespace eqt {
namespace state {

/** THE read. False (model untouched) when the character has no file yet. */
bool LoadSocialLists(const std::string& character, const std::string& server,
                     SocialListsModel& model);

/** THE write. False when no cache directory is writable (not an error). */
bool SaveSocialLists(const std::string& character, const std::string& server,
                     const SocialListsModel& model);

class SocialListsSync {
public:
    /**
     * Once per game tick. On a new (character, server) the model is reset and
     * re-read from that character's file -- nothing carries over from the
     * previous character, T467's rule for the UI file; on a changed revision
     * it is written. Returns true whenever the lists the renderer shows are
     * now stale, i.e. the caller must publish SocialListsChanged. An empty
     * character name does nothing (not logged in yet).
     */
    bool tick(SocialListsModel& model, const std::string& character,
              const std::string& server);

private:
    std::string identity_;
    uint64_t syncedRevision_ = 0;
    bool bound_ = false;
};

}  // namespace state
}  // namespace eqt
