#ifndef EQT_GRAPHICS_SLASH_SLASH_COMMANDS_COMPONENT_H
#define EQT_GRAPHICS_SLASH_SLASH_COMMANDS_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L06 -- the slash
// family, lifted out of class IrrlichtRenderer: registry 3, the renderer's own
// slash-command if-chain, which the game thread reaches through
// GameStateBridge (src/client/bridge/irrlicht_bridge.cpp) so that every
// renderer command runs on the RENDER thread.
//
// The family is ONE body. processSlashCommand() is 593 lines of if/else and it
// moved WHOLE -- Batch G decides its decomposition, not this unit. The only
// edits inside it are the mechanism's substitutions: a core member becomes
// ctx_.member, a shared IrrlichtRenderer member becomes owner_.member_, a
// method that stayed behind becomes owner_.Method(), and a sibling component
// reached through its own pointer becomes owner_.Diagnostics().X() /
// owner_.PlayerCamera().X() (K06/K07 correction r).
//
// THE BASENAME IS LOAD-BEARING. tests/cmake/commands.cmake compiles
// tests/test_merged_help.cpp with
// RENDERER_COMMAND_SOURCE=<repo>/src/client/graphics/slash/renderer_slash_commands.cpp
// and RendererCommandCatalogTest.MatchesRendererIfChainExactly PARSES that
// file: it locates the body by name and requires the set of `cmd == "/name"`
// literals to equal the static catalog in
// src/client/help/renderer_command_catalog.cpp exactly. L06 taught that
// matcher the component spelling (L05 correction 21) and re-proved it with a
// deliberate break. Only the directory moved.
//
// ZERO members moved. The slash family owns no state at all: every member the
// body touches is shared (the frame family's seven UI-panel members reached as
// owner_.X_ -- 23 uses -- plus ctx_.bridge, ctx_.zoneShader and
// ctx_.eventReceiver on the context). That is why this unit's
// irrlicht_renderer.h delta is not negative; the deliverable is the 593-line
// body leaving the god class, not the header count.
//
// IrrlichtRenderer::processSlashCommand() SURVIVES as a one-line forwarder,
// because src/client/bridge/irrlicht_bridge.cpp:742 calls it on an
// IrrlichtRenderer receiver and must not learn a component exists (the K11
// rule), as do six tests (test_irrlicht_camera_command, test_merged_help's
// sibling test_renderer_command_echo, test_inventory_toggle,
// test_first_person_tunables, test_ui_hide_command, test_static_ui_scroll).
//
// Friendship is needed on axis 1 of K13 correction (ii): the component reaches
// ten private IrrlichtRenderer members through owner_. It names no private
// static and no private nested type, so axis 2 is empty.

#include "client/graphics/renderer_context.h"

#include <string>

// L12 (Batch L close-out): THIS COMPONENT NEEDS NO friend class LINE, and
// IrrlichtRenderer no longer declares one. K13 correction (ii)'s two-axis
// census, applied to the renderer at 1c9afba7: axis 1 (owner_. reaches of a
// PRIVATE IrrlichtRenderer member) is EMPTY -- all 37 owner_. uses are public
// methods, the /toggle-and-query command surface (isSkyEnabled, toggleSky,
// setCameraMode, getRenderDistance, setClipDistance, isFrameTimingEnabled,
// getDetailManager, getParticleManager, getGovernor, saveScreenshot, ...);
// axis 2 (an IrrlichtRenderer-scoped qualified name) is EMPTY too -- the four
// IrrlichtRenderer::CameraMode uses resolve through the PUBLIC using-alias
// batch correction 22 put in the class. The IrrlichtRenderer& handle STAYS:
// zero PRIVATE reaches is the friend test, zero reaches of ANY kind is the
// ctor-parameter test, and only RdpComponent passes that one.
//
namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class SlashCommandsComponent {
public:
	SlashCommandsComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// Registry 3. Runs on the render thread; see the note above on why the
	// IrrlichtRenderer forwarder stays.
	void processSlashCommand(const std::string& command);

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SLASH_SLASH_COMMANDS_COMPONENT_H
