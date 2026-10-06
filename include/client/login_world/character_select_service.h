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

#include <functional>

namespace eqt {
namespace login_world {

class LoginWorldComponent;

class CharacterSelectService {
public:
    using StringLookup = std::function<std::string(uint32_t)>;

    void tick(LoginWorldComponent& lw, CharacterSelectChannel& channel, const StringLookup& eqstr);

private:
    uint32_t handled_ = 0;
    uint32_t listGeneration_ = 0;
    std::vector<CharacterSelectRow> rows_;
    std::shared_ptr<const EqstrTable> strings_;
    CharacterSelectView published_;
};

}  // namespace login_world
}  // namespace eqt
