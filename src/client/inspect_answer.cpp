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

    // Truncate rather than overflow: the server reads exactly
    // sizeof(InspectMessage_Struct) == 256 bytes out of this 288-byte field.
    const size_t max_text = sizeof(r.text) - 1;
    const size_t n = message.size() < max_text ? message.size() : max_text;
    std::memcpy(r.text, message.data(), n);
    r.text[n] = '\0';

    return r;
}

}  // namespace EQT
