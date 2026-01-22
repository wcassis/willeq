#ifndef EQT_GRAPHICS_RDP_PEER_CONTEXT_H
#define EQT_GRAPHICS_RDP_PEER_CONTEXT_H

#ifdef WITH_RDP

#include <freerdp/freerdp.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/server/rdpsnd.h>

#include <winpr/stream.h>

namespace EQT {
namespace Graphics {

// Forward declaration
class RDPServer;

/**
 * Per-peer context structure for RDP connections.
 *
 * IMPORTANT: The rdpContext _p member MUST be the first member of this struct.
 * This is a FreeRDP convention that allows casting between rdpContext* and
 * RDPPeerContext*.
 */
struct RDPPeerContext {
    rdpContext _p;  // Must be first member (FreeRDP convention)

    // Codec contexts for frame encoding
    RFX_CONTEXT* rfxContext;
    NSC_CONTEXT* nscContext;

    // Stream for encoding frame data
    wStream* encodeStream;

    // Frame tracking
    uint32_t frameId;

    // Connection state
    bool activated;

    // Virtual channel manager for this peer
    HANDLE vcm;

    // Audio context for RDPSND
    RdpsndServerContext* rdpsndContext;
    bool audioActivated;
    int16_t selectedAudioFormat;  // Index into client's format list, -1 if not selected

    // Back-pointer to server (set during context initialization)
    RDPServer* server;
};

/**
 * Initialize a new peer context.
 * Called by FreeRDP when a new client connects.
 *
 * @param peer The FreeRDP peer
 * @param ctx The context to initialize (cast to RDPPeerContext*)
 * @return TRUE on success, FALSE on failure
 */
BOOL rdpPeerContextNew(freerdp_peer* peer, rdpContext* ctx);

/**
 * Free a peer context.
 * Called by FreeRDP when a client disconnects.
 *
 * @param peer The FreeRDP peer
 * @param ctx The context to free (cast to RDPPeerContext*)
 */
void rdpPeerContextFree(freerdp_peer* peer, rdpContext* ctx);

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP

#endif // EQT_GRAPHICS_RDP_PEER_CONTEXT_H
