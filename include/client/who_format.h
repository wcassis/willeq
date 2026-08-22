#pragma once

#include <cstdint>
#include <string>

namespace EQT {

// T178: substitute the EQ `%1` count placeholder in an OP_WhoAllResponse header
// string. eqemu sends header strings like "There is %1 player in EverQuest." /
// "There are %1 players in EverQuest." whose single argument is the player
// count; the client is expected to fill %1 before display. Before T178 the
// client never substituted it and appended a raw ": N", so the line rendered
// as "There is %1 player in EverQuest.: 1".
//
// Replaces every "%1" occurrence with `count`. Returns the filled string and
// sets `substituted` to whether any "%1" was present -- the caller appends the
// count itself only when it was not (an unexpected string with no placeholder),
// rather than dropping the number on the floor. Other placeholders (%2, %3...)
// are left untouched; this header carries only the count.
std::string substituteWhoCount(const std::string& tmpl, uint32_t count,
                               bool& substituted);

}  // namespace EQT
