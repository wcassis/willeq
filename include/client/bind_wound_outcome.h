#pragma once

#include <cstdint>
#include <string>

namespace EQT {

// T177: map a server OP_Bind_Wound reply's `type` field to the player-facing
// outcome message, or "" for the control/state types that carry no outcome.
//
// The client SENDS OP_Bind_Wound to start a bandage; ~10 s later (or at once,
// on failure) the server replies with the SAME opcode carrying a BindWound_Struct
// whose `type` is the result. Before T177 the client had no parser for that
// reply, so the player was shown nothing -- not success, and not the common
// "you have no bandages" failure.
//
// Authority: eqemu Client::BindWound (zone/client.cpp:3462). The values:
//   0  Client Unlocked / state reset -- follows every real message; not shown.
//   1  Done -- the wound was bound.
//   3  Start / begin bind -- a control tick, sent BEFORE the outcome is known
//      (it precedes both the normal start AND the no-bandage failure, which
//      then sends type 7), so surfacing it would contradict the failure that
//      may immediately follow. The send side already prints "You use Bind
//      Wound!", so the start is covered; type 3 is deliberately not shown.
//   4  No / dead target ("bindmob dead").
//   5  Target no longer in the zone (left before the 10 s timer finished).
//   7  No bandage in inventory (eqemu's own comment calls this the "wrong
//      message", but it is the only signal sent for the missing-bandage case).
//
// Returning "" (not throwing/logging) for 0, 3, and any unknown type keeps the
// mapping pure and total; the caller logs the raw type for diagnosis.
std::string bindWoundOutcomeMessage(uint16_t type);

}  // namespace EQT
