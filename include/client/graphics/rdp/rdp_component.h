#ifndef EQT_GRAPHICS_RDP_RDP_COMPONENT_H
#define EQT_GRAPHICS_RDP_RDP_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L01 -- the
// pilot. The rdp family, lifted out of class IrrlichtRenderer following
// Batch K's eqt::guild::GuildComponent shape.
//
// Every body in src/client/graphics/rdp/rdp_component.cpp is the body that
// used to live in src/client/graphics/renderer_rdp.cpp, moved verbatim; the
// only edits inside a body are the four core-member substitutions
// RendererContext allows (driver_ -> ctx_.driver, device_ -> ctx_.device,
// config_ -> ctx_.config, eventReceiver_ -> ctx_.eventReceiver). No method
// that stayed on IrrlichtRenderer is called and no IrrlichtRenderer static,
// nested type or constant is named, so this component needs no friendship --
// but it keeps the IrrlichtRenderer& transition handle the batch mechanism
// prescribes, unused so far, for the families that follow.
//
// The three lambda captures in initRDP() stay [this] and capture the
// COMPONENT (K11 correction dd: a [this] capture is not an identity key).

#ifdef WITH_RDP

#include "client/graphics/renderer_context.h"
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"

#include <cstdint>
#include <memory>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class RdpComponent {
public:
	RdpComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// The five IrrlichtRenderer declarations that survive as one-line
	// forwarders on the renderer, because Application calls them and must not
	// learn a component exists (the K11 rule). getRDPClientCount() had zero
	// callers tree-wide and published no event, so it was deleted (§5).
	bool initRDP(uint16_t port = 3389);
	bool startRDPServer();
	void stopRDPServer();
	bool isRDPRunning() const;
	RDPServer* getRDPServer() { return rdpServer_.get(); }

	// Called from renderer_frame_render.cpp through rdp_->captureFrameForRDP().
	void captureFrameForRDP();

private:
	void handleRDPKeyboard(uint16_t flags, uint8_t scancode);
	void handleRDPSynchronize(uint32_t syncFlags);
	void handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y);

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// RDP server for native remote desktop streaming
	std::unique_ptr<RDPServer> rdpServer_;

	// T317: caps lock is a TOGGLE, not a held modifier, so it cannot be read
	// off the Irrlicht event receiver the way shift is -- it has to be
	// remembered across key events. Fed by both the RDP key stream and the
	// client's KBD_SYNC message. See rdp/rdp_input_handler.h.
	RDPLockKeyState rdpLockKeys_;
};

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP

#endif // EQT_GRAPHICS_RDP_RDP_COMPONENT_H
