#pragma once

// B998 (GitLab #318, #204 Leg M): the PRE-ZONE UI HOST -- the render-thread
// object that runs the Character Select screen (and, through it, B888's
// Character Creation screen) before any zone exists. It is reached from
// Application::render()'s pre-zone branch, the same branch that draws the
// network-handshake loading screen, and only while the game thread holds the
// CharacterSelectChannel active (the list has arrived, OP_EnterWorld has not
// gone out). It owns nothing that outlives the screen: Application drops it
// the frame the channel goes inactive.
//
// One frame: take the newest view, advance the delete prompt's clock, then
// -- inside IrrlichtRenderer::drawPreZoneUI(), which pumps the device first --
// drain the keyboard and the two click latches into the screen, post what it
// asks for, and draw it. The pre-zone receiver reads are the render thread's
// and run only while processFrame() does not (tests/test_read_and_reset_ownership.cpp).

#include "client/graphics/ui/char_select_screen.h"
#include "client/login_world/char_create_data.h"

#include <cstdint>

namespace eqt {
namespace login_world {
class CharacterSelectChannel;
}
}  // namespace eqt

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class CharacterSelectHost {
public:
    CharacterSelectHost();

    /** One frame. False when the user pressed Quit or the device stopped. */
    bool frame(IrrlichtRenderer& renderer, eqt::login_world::CharacterSelectChannel& channel, float dt);
    /** The screen is going away: hand the keyboard back to the in-game frame. */
    static void release(IrrlichtRenderer& renderer);

private:
    void handle(const CharSelectAction& a, eqt::login_world::CharacterSelectChannel& channel);

    CharSelectScreenState state_;
    eqt::charcreate::CharCreateData data_;   // data/char_create.tsv, B888's era data
    uint64_t seenView_ = 0;
    uint32_t seed_ = 0;
    bool quit_ = false;
};

}  // namespace Graphics
}  // namespace EQT
