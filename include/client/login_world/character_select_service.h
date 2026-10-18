#pragma once

// B998 (GitLab #318): the character-select screen's GAME-THREAD half, one
// tick at a time. Application::serviceCharacterSelect() calls tick() from
// the game loop; tests drive the same object against a replayed world stream.
//
//   1. drain the screen's requests into LoginWorldComponent -- Enter World
//      (EnterWorldFromSelect), Delete / Create / Get Name (B888's
//      RequestDeleteCharacter / RequestCreateCharacter / RequestRandomName,
//      the bodies the world script uses), counting each one handled;
//   2. once OP_EnterWorld is out, take the screen down for good;
//   3. otherwise, once a list exists, publish the view -- rows rebuilt only on
//      a new OP_SendCharInfo, eqstr text resolved once per activation on this
//      thread (the string table is the game thread's), and only when it changed.

#include "client/login_world/character_select.h"

#include <cstdint>
#include <functional>

namespace eqt {
namespace login_world {

class LoginWorldComponent;

class CharacterSelectService {
public:
    using StringLookup = std::function<std::string(uint32_t)>;

    void tick(LoginWorldComponent& lw, CharacterSelectChannel& channel, const StringLookup& eqstr);

    /**
     * B1081 (GitLab #401): how many times this service took the screen UP
     * (channel inactive -> active) this run. The render thread logs
     * "[CHARSELECT] screen up" at the same moment on a run that draws it, so
     * on every other run this counter IS the "screen up" the live lane can
     * assert: a zone-change world reconnect must contribute exactly 0, a
     * fresh login with the screen exactly 1. The service is the game thread's
     * and the channel's active() flag is its output, so counting here is the
     * count. (A screen that comes down and back up again would count twice;
     * it only goes down for good when OP_EnterWorld is out.)
     */
    uint32_t Activations() const { return activations_; }

private:
    uint32_t handled_ = 0;
    uint32_t activations_ = 0;   // B1081: screen-up count, see Activations()
    uint32_t listGeneration_ = 0;
    std::vector<CharacterSelectRow> rows_;
    std::shared_ptr<const EqstrTable> strings_;
    CharacterSelectView published_;
};

}  // namespace login_world
}  // namespace eqt
