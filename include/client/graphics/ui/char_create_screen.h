#pragma once

// B888 (GitLab #204): the Character Creation screen's STATE MACHINE -- the
// official two-page flow, device-free. Geometry: char_create_layout.h; draw
// and hit test: char_create_panel.h; rules: login_world/char_create_rules.h;
// the era filter: login_world/char_create_data.h.
//
// The flow, as the official client runs it (oracle frames in
// /mnt/projects/eq-oracle/logs/shots/, create-character.sh):
//
//   page 0  race / class / gender, the point pool. Opens on a RANDOM valid
//           selection with every point unspent (cc_01_screen.png: Iksar
//           female Beastlord, POINTS LEFT 20). A class the race cannot be is
//           GREYED and inert (cc_06: Human greys Beastlord, Berserker,
//           Shaman). Next is refused while points are left -- eqstr 1577.
//   page 1  deity / city / name. Deities and cities the combination is not
//           offered are greyed (cc_08). Next submits: the name is checked by
//           the server's own rules first (eqstr 1576 "Name is not valid."),
//           then the request goes out -- eqstr 1580 while it is in flight.
//           A refused name is the modal eqstr 1581 "Name has been rejected.
//           Please choose another." with Ok (review/UIGAP-ORACLE/204/12, a
//           taken name); a created character goes STRAIGHT INTO THE WORLD,
//           not back to character select (204/14) -- LoginWorldComponent
//           does that half.
//   Back    page 1 -> page 0; page 0 -> character select.
//
// The live oracle run (review/UIGAP-ORACLE/204/, Human male Ranger of Tunare)
// confirmed the rest: a random selection at open (Froglok female
// Shadowknight there), the Human class greys, Ranger's green primaries, "+"
// spends one point, a right-click on DEXTERITY spends the remaining 19, the
// first offered deity pre-selected (THE RAINKEEPER) with QEYNOS the only city.
//
// HIDDEN, not greyed (Will 2026-09-23): Vah Shir, Froglok, Beastlord,
// Berserker, SHARVAHL, GUKTA are never in the lists this state holds.
// NO TUTORIAL: there is no Start Tutorial control, and the request this
// state builds can only carry tutorial = 0 (char_create_packets.h).
//
// Unmeasured and therefore FLAGGED: what the official screen does to the
// class when a race is picked that cannot be it (fable: the first class the
// race CAN be is selected, in button order -- assumption awaiting oracle);
// the deity/city it pre-selects (fable: the first one offered).

#include "client/graphics/ui/text_entry.h"
#include "client/graphics/ui/face_pick_panel.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_packets.h"
#include "client/login_world/char_create_rules.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct CharCreateScreenState {
    bool isOpen = false;
    int page = 0;                       // 0 = race/class/abilities, 1 = deity/city/name

    // The era data the screen was opened with (owned by the host, outlives it)
    // and its in-era lists, in button order, filtered once at open.
    const eqt::charcreate::CharCreateData* data = nullptr;
    std::vector<eqt::charcreate::EraEntry> races, classes, deities, cities;

    uint32_t race = 0, cls = 0, gender = eqt::charcreate::Gender::Male, deity = 0, city = 0;
    eqt::charcreate::StatAllocator alloc;
    TextEntryState nameEntry;           // CC1_NameInput

    FacePickData faceData;              // data/char_create_faces.tsv
    FacePickState facePick;             // modal draft, separate from committed picks
    std::array<uint32_t, kFacePickFeatures> appearance{};

    // CC0_Help / CC1_Help: an eqstr id (0 = none) and, for a deity or city,
    // nothing else -- the pane prints the string. helpTitle is the label shown
    // above a race/class/city whose eqstr has no title of its own.
    uint32_t helpEqstr = 0;
    std::string helpTitle;

    // The modal line ("Name is not valid." etc.), eqstr id or a server text.
    uint32_t messageEqstr = 0;
    std::string messageText;
    bool submitting = false;            // eqstr 1580 up; input is ignored

    bool raceClassValid(uint32_t r, uint32_t c) const;
    std::vector<uint32_t> offeredDeities() const;
    std::vector<uint32_t> offeredCities() const;
    bool deityOffered(uint32_t d) const;
    bool cityOffered(uint32_t c) const;
};

/** What the host must do after an input. */
struct CharCreateAction {
    enum class Kind : uint8_t { None, BackToCharacterSelect, Submit, GetName };
    Kind kind = Kind::None;
    eqt::charcreate::CharCreateRequest request;   // Submit
    uint32_t race = 0, gender = 0;                // GetName
};

/** Open on page 0 with a random valid in-era selection (`seed`). */
void openCharCreate(CharCreateScreenState& s, const eqt::charcreate::CharCreateData& data,
                    uint32_t seed);
/** Open on a FIXED selection (captures, tests). False if it is not offered. */
bool openCharCreateWith(CharCreateScreenState& s, const eqt::charcreate::CharCreateData& data,
                        uint32_t race, uint32_t gender, uint32_t cls);

bool selectRace(CharCreateScreenState& s, uint32_t race);
bool selectClass(CharCreateScreenState& s, uint32_t cls);
void selectGender(CharCreateScreenState& s, uint32_t gender);
bool selectDeity(CharCreateScreenState& s, uint32_t deity);
bool selectCity(CharCreateScreenState& s, uint32_t city);

bool statUp(CharCreateScreenState& s, int stat);
bool statDown(CharCreateScreenState& s, int stat);
/** Right-click on a stat name: every point left into it, plus its help. */
int statMax(CharCreateScreenState& s, int stat);
/** CC0_Points_Left_Button: "take back all of your starting stat choices". */
void resetPoints(CharCreateScreenState& s);
/** Left-click on a stat name: its description (eqstr 3332 + stat). */
void showStatHelp(CharCreateScreenState& s, int stat);

/** CC_Next_Button. */
CharCreateAction pressNext(CharCreateScreenState& s);
/** CC_Back_Button. */
CharCreateAction pressBack(CharCreateScreenState& s);
/** CC1_Get_Name_Button. */
CharCreateAction pressGetName(CharCreateScreenState& s);
/** World's Get Name reply -> the Editbox. */
void applyRandomName(CharCreateScreenState& s, const std::string& name);
/** The server's verdict on a Submit: refused -> message, back to input. */
void applySubmitRefused(CharCreateScreenState& s, const std::string& serverText);
/** Dismiss the modal line. */
void dismissMessage(CharCreateScreenState& s);

/** The request page 1's Next would send (no validation). */
eqt::charcreate::CharCreateRequest buildRequest(const CharCreateScreenState& s);

}  // namespace Graphics
}  // namespace EQT
