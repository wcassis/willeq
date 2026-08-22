#include "client/inspect_answer.h"

#include <cstring>

namespace EQT {

InspectResponse_Struct MakeInspectAnswer(uint32_t requester_id, uint32_t my_id,
                                         const std::string& message) {
    InspectResponse_Struct r;
    std::memset(&r, 0, sizeof(r));

    // The inversion: TargetID is the ROUTING target (whoever asked), not the
    // inspected player. eqemu's own builder does exactly this --
    // Client::ProcessInspectRequest, zone/client.cpp:6590-6591.
    r.TargetID = requester_id;
    r.playerid = my_id;

    // 0xFFFFFFFF is eqemu's "no item" icon value (client_packet.cpp:9041). The
    // server overwrites all of these, but sending its own empty-marker rather
    // than 0 keeps the packet meaningful if it is ever read by anything else.
    for (auto& icon : r.itemicons) {
        icon = 0xFFFFFFFF;
    }

    // Truncate to 255 + NUL, NOT to the 288-byte field.
    //
    // T171: the bound that matters is the SERVER's, not ours.
    // Handle_OP_InspectAnswer memcpy()s exactly sizeof(InspectMessage_Struct)
    // == 256 bytes out of this 288-byte field into a 256-byte struct. A
    // message of 256+ characters would therefore be stored with NO terminator
    // inside it, and eqemu later strcpy()s that stored text into a 288-byte
    // buffer (client.cpp:6628) -- an overread on their side. Unreachable while
    // T170 always sent an empty message; reachable the moment T171 lets a user
    // set one, which is why it is fixed in the same unit.
    const size_t max_text = EQT::INSPECT_MESSAGE_MAX_LEN;
    const size_t n = message.size() < max_text ? message.size() : max_text;
    std::memcpy(r.text, message.data(), n);
    r.text[n] = '\0';

    return r;
}

}  // namespace EQT
