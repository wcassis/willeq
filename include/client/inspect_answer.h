// Building an OP_InspectAnswer body.
//
// T170. A Titanium client is REQUIRED to answer its own inspect requests:
// eqemu's Handle_OP_InspectRequest (zone/client_packet.cpp:9084) forwards the
// request to the target and only builds the answer server-side for SoF+
// clients and bots. This client parsed OP_InspectAnswer but never generated
// one, so inspecting a WillEQ player produced nothing at all -- the
// implemented-but-unreached pattern with the two halves split across two
// machines, which is why single-client testing could never have found it.
//
// The body is built here rather than inline in the sender so the field
// semantics -- which are inverted relative to the REQUEST struct, and which
// this codebase already got wrong once on the receive side -- can be tested.

#pragma once

#include "common/packet_structs.h"

#include <cstdint>
#include <string>

namespace EQT {

// The longest inspect message that survives a round trip. The server copies
// sizeof(InspectMessage_Struct) == 256 bytes out of the 288-byte wire field
// into a 256-byte struct, so 255 characters plus a terminator is the real
// limit -- NOT the 288 the field appears to offer.
inline constexpr size_t INSPECT_MESSAGE_MAX_LEN = 255;

// Builds the answer to an inspect request.
//
// `requester_id` is the inspecting player -- Inspect_Struct::PlayerID from the
// request we received -- and goes in TargetID, which is what the server routes
// on. `my_id` is us, the player being inspected, and goes in playerid.
//
// EQUIPMENT IS DELIBERATELY LEFT EMPTY. The server does not trust and does not
// read it: Handle_OP_InspectAnswer (client_packet.cpp:9023-9043) overwrites
// every itemname and itemicon from the inspected character's own inventory
// before forwarding. Filling them in from our client-side inventory would be
// work whose result is discarded, and would invite the reader to think the
// values matter.
//
// `message` becomes the character's SERVER-STORED inspect message -- see the
// hazard note on InspectResponse_Struct and in SendInspectAnswer().
InspectResponse_Struct MakeInspectAnswer(uint32_t requester_id, uint32_t my_id,
                                         const std::string& message);

}  // namespace EQT
