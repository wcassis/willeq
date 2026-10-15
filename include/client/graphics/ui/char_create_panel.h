#pragma once

// B888 (GitLab #204): the Character Creation screen's DRAW and CLICK
// resolution. Geometry: char_create_layout.h (quotes EQUI_CharacterCreate.xml;
// this file computes no rect). State: char_create_screen.h.
//
// What is drawn, per the oracle frames (eq-oracle/logs/shots/cc_*.png):
// selected buttons carry YELLOW text, greyed (not offered) ones GREY text and
// are inert, the class's primary stats are GREEN, POINTS LEFT shows the pool.
// NOT drawn, each flagged:
//   * the 3D creation scene and the sample model (the issue's Leg M:
//     CharacterModelView before a zone exists);
//   * CC_Help_Button opens CharacterCreationHelp (eqstr 5874/5875/5876) only
//     as the help pane's text, not as its own 300x200 window;
//   * the modal message box: the official one is a generic dialog whose XML
//     is not in this file -- drawn at a fable rect, flagged.

#include "client/graphics/ui/char_create_layout.h"
#include "client/graphics/ui/char_create_screen.h"

#include <cstdint>
#include <functional>
#include <string>

namespace EQT {
namespace Graphics {

class UIRenderer;

/** eqstr id -> text. Empty function or empty result = "eqstr <id>" is drawn. */
using EqstrResolver = std::function<std::string(uint32_t)>;

enum class CharCreateHit : uint8_t {
    None = 0,
    Back, Next, Help,
    Male, Female,
    FacePick,      // open CC0_Face_Button
    FacePickControl,  // popup input; facePick holds the modal target
    Race,          // index = slot, id = race
    Class,         // index = slot, id = class
    StatName,      // index = stat (left: help; right: spend max)
    StatUp, StatDown,
    PointsLeft,
    Deity,         // index = slot, id = deity
    City,          // index = slot, id = city
    NameInput,
    GetName,
    MessageOk,
    Blocked,       // a click swallowed by the modal message / a submit in flight
};

struct CharCreateClickTarget {
    CharCreateHit hit = CharCreateHit::None;
    int index = -1;
    uint32_t id = 0;
    FacePickClickTarget facePick;
};

/** The modal message box (fable rect, flagged) and its Ok button. */
irr::core::rect<irr::s32> charCreateMessageRect(int sw, int sh);
irr::core::rect<irr::s32> charCreateMessageOkRect(int sw, int sh);

/** eqstr markup -> plain lines: <BR> breaks, <c ...>/</c> dropped. */
std::string charCreatePlainText(const std::string& eqstr);

void renderCharCreateScreen(UIRenderer& ui, int sw, int sh, const CharCreateScreenState& s,
                            const EqstrResolver& eqstr);

CharCreateClickTarget hitTestCharCreate(int sw, int sh, const CharCreateScreenState& s,
                                        int mouseX, int mouseY);

/** Apply a click (rightButton: the stat-name "spend max" gesture). */
CharCreateAction applyCharCreateClick(CharCreateScreenState& s, const CharCreateClickTarget& t,
                                      bool rightButton);

}  // namespace Graphics
}  // namespace EQT
