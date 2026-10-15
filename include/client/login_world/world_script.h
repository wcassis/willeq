#pragma once

// B888 (GitLab #204): the world-stage SCRIPT -- what the client does at
// character select instead of entering the configured character's world.
// It is how automation creates, deletes or lists characters without the UI:
//
//   --create-character "Name,Race,Gender,Class,Deity,City[,STR=n,...][,FACE=n,HAIR=n,HAIRCOLOR=n,EYES=n,BEARD=n,BEARDCOLOR=n]"
//       create it (OP_ApproveName -> OP_CharacterCreate), then enter world
//       with it -- in its home city, never the tutorial.
//   --delete-character Name      delete it (OP_DeleteCharacter), then quit.
//   --list-characters            print the account's characters, then quit.
//
// Names are matched the way the screen's labels read, case-insensitive with
// spaces ignored: "DarkElf" == "Dark Elf", deity "FACELESS" == "THE FACELESS",
// city "QEYNOS". Only in-era entries parse (data/char_create.tsv), and the
// deity/city must be one the screen OFFERS for the race/class, so the script
// can create nothing the UI could not. With no STR=.. terms the stats are
// charcreate::defaultAllocation(); with some, the rest stay at base and every
// point must be spent (eqstr 1577), exactly as Next demands on page 0.
// Appearance keys are optional decimal values validated against the same
// race/gender ranges as FacePick (data/char_create_faces.tsv). EYES sets both eyes.
//
// A refused script (bad spec at startup; a name or creation the server
// refuses; a delete of a name the account does not hold) logs
// `[CHARCREATE] FAILED: <reason>` and ends the run through
// eqt::RecordFatalExit(), so main() exits non-zero (kClientFatalExit).

#include "client/login_world/char_create_packets.h"

#include <string>

namespace eqt {
namespace charcreate {

class CharCreateData;

struct WorldScript {
    enum class Kind : uint8_t { None, Create, Delete, List };
    Kind kind = Kind::None;
    CharCreateRequest create;   // Kind::Create
    std::string name;           // Kind::Delete (and the Create name, mirrored)

    bool active() const { return kind != Kind::None; }
};

/** Parse the --create-character spec against the era data. */
bool ParseCreateSpec(const std::string& spec, const CharCreateData& data,
                     CharCreateRequest& out, std::string& error);

}  // namespace charcreate
}  // namespace eqt
