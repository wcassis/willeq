#ifdef WITH_RDP

#include "client/graphics/rdp/rdp_peer_context.h"
#include "client/graphics/rdp/rdp_server.h"

#include <freerdp/freerdp.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <winpr/stream.h>

#include <iostream>

namespace EQT {
namespace Graphics {

BOOL rdpPeerContextNew(freerdp_peer* peer, rdpContext* ctx) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(ctx);
    RDPServer* server = static_cast<RDPServer*>(peer->ContextExtra);

    if (!context || !server) {
        return FALSE;
    }

    // Initialize context fields
    context->server = server;
    context->frameId = 0;
    context->activated = false;
    context->rfxContext = nullptr;
    context->nscContext = nullptr;
    context->encodeStream = nullptr;
    context->rdpsndContext = nullptr;
    context->audioActivated = false;
    context->selectedAudioFormat = -1;

    // Initialize RFX (RemoteFX) encoder
    context->rfxContext = rfx_context_new(TRUE);  // TRUE = encoder mode
    if (!context->rfxContext) {
        std::cerr << "[RDP] Failed to create RFX context" << std::endl;
        return FALSE;
    }

    // Reset RFX context with server resolution
    if (!rfx_context_reset(context->rfxContext, server->getWidth(), server->getHeight())) {
        std::cerr << "[RDP] Failed to reset RFX context" << std::endl;
        rfx_context_free(context->rfxContext);
        context->rfxContext = nullptr;
        return FALSE;
    }

    // Initialize NSC encoder
    context->nscContext = nsc_context_new();
    if (!context->nscContext) {
        std::cerr << "[RDP] Failed to create NSC context" << std::endl;
        rfx_context_free(context->rfxContext);
        context->rfxContext = nullptr;
        return FALSE;
    }

    // Create encoding stream (start with 64KB, will grow as needed)
    context->encodeStream = Stream_New(nullptr, 65536);
    if (!context->encodeStream) {
        std::cerr << "[RDP] Failed to create encode stream" << std::endl;
        nsc_context_free(context->nscContext);
        rfx_context_free(context->rfxContext);
        context->nscContext = nullptr;
        context->rfxContext = nullptr;
        return FALSE;
    }

    return TRUE;
}

void rdpPeerContextFree(freerdp_peer* peer, rdpContext* ctx) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(ctx);

    if (!context) {
        return;
    }

    // Free RDPSND context
    if (context->rdpsndContext) {
        rdpsnd_server_context_free(context->rdpsndContext);
        context->rdpsndContext = nullptr;
    }
    context->audioActivated = false;
    context->selectedAudioFormat = -1;

    // Free encoding stream
    if (context->encodeStream) {
        Stream_Free(context->encodeStream, TRUE);
        context->encodeStream = nullptr;
    }

    // Free NSC context
    if (context->nscContext) {
        nsc_context_free(context->nscContext);
        context->nscContext = nullptr;
    }

    // Free RFX context
    if (context->rfxContext) {
        rfx_context_free(context->rfxContext);
        context->rfxContext = nullptr;
    }

    context->server = nullptr;
    context->activated = false;
}

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP
