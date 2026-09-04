#ifndef EQT_GRAPHICS_DIAGNOSTICS_DIAGNOSTICS_COMPONENT_H
#define EQT_GRAPHICS_DIAGNOSTICS_DIAGNOSTICS_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L04 -- the
// diagnostics family, lifted out of class IrrlichtRenderer: scene dumps,
// frame-timing and scene-breakdown profiling, the memory report, and the
// runtime debug toggles.
//
// Every body in src/client/graphics/diagnostics/ is the body that used to
// live in src/client/graphics/renderer_diagnostics.cpp and
// src/client/graphics/renderer_debug.cpp, moved verbatim. The only edits
// inside a body are the mechanism's substitutions: a core member becomes
// ctx_.member, a non-core IrrlichtRenderer member becomes owner_.member_, a
// method that stayed behind becomes owner_.Method(), and the one nested type
// a body names becomes IrrlichtRenderer::SceneBreakdown.
//
// This is the batch's BREADTH test of RendererContext: the family reads 30 of
// the context's 54 members, and a further 39 IrrlichtRenderer members that are
// shared with other families but sit below the 5-family core threshold. Those
// 39 are reached as owner_.member_ through the transition handle exactly as
// §Mechanism 3 prescribes -- the context is NOT widened for them; the unit
// that extracts the last family using one of them moves it.
//
// The component therefore needs friendship (unlike RdpComponent, L01
// correction 7): it names IrrlichtRenderer private members and one private
// nested type. See the friend line in irrlicht_renderer.h.
//
// measureSection() did NOT come along: sectionStart_ is declared under "Frame
// phase shared state" and all 35 call sites are the frame family's, so it
// stayed on IrrlichtRenderer and moved to renderer_frame.cpp. See that file.

#include "client/graphics/renderer_context.h"
#include "client/graphics/renderer_draw_types.h"

#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class DiagnosticsComponent {
public:
	DiagnosticsComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// --- Scene dump, profiling, memory report (was renderer_diagnostics.cpp)
	// dumpScene(), getMemoryReport() and runSceneProfile() also survive as
	// one-line forwarders on IrrlichtRenderer, because irrlicht_bridge.cpp and
	// application.cpp call them and must not learn a component exists (the K11
	// rule). Everything else here is reached through Diagnostics().
	void dumpScene() const;
	void logFrameTimings();
	void runSceneProfile();
	void profileSceneBreakdown();
	std::vector<std::string> getMemoryReport(const MemoryReportInput& ext) const;
	// T69: structured accessor backing getMemoryReport() -- the same tally,
	// named fields instead of formatted strings. Use this from code that
	// needs to consume the accounting programmatically (CI, benchmarks);
	// do not parse getMemoryReport()'s text output.
	MemoryReportBreakdown getMemoryReportBreakdown(const MemoryReportInput& ext) const;

	// --- Runtime debug toggles (was renderer_debug.cpp)
	// L04 measured a defect while counting callers: toggleWireframe(),
	// toggleHUD(), toggleNameTags() and toggleOldModels() have ZERO callers on
	// IrrlichtRenderer. RendererEventReceiver::OnEvent queues
	// RendererAction::ToggleWireframe / ToggleHUD / ToggleNameTags /
	// ToggleOldModels, and IrrlichtRenderer::processCommonInput()'s switch has
	// no arm for any of them -- they fall through its default. The hotkeys are
	// bound (hotkey_manager.cpp) and do nothing. GraphicalMode's same-named
	// methods reach IOutputRenderer's empty default bodies; IrrlichtRenderer
	// does NOT derive from IOutputRenderer, so they never arrive here.
	// The implementations are kept and made reachable through Diagnostics();
	// wiring the four actions is a behaviour change and is boarded, not done
	// in this structural unit.
	void toggleWireframe();
	void toggleHUD();
	void toggleAllUI();
	void toggleNameTags();
	void toggleFog();
	void toggleZoneLights();
	void cycleObjectLights();
	void toggleOldModels();
	bool isUsingOldModels() const;
	void toggleManualZoneDraw();
	void toggleMergeZoneDraws();
	void togglePlayerLight();
	void swapToDebugShader(irr::s32 debugMat);    // Swap all per-pixel materials to debug visualization program
	void swapFromDebugShader(irr::s32 debugMat);  // Restore original materials from debug mode
	void toggleObjectLights();
	void toggleDirectionalLight();
	void togglePortalOcclusion();
	void togglePortalDebugDraw();
	void toggleStencilDebugDraw();

	// Per-region indoor classification (loaded from data/region_maps/<zone>.json)
	bool loadIndoorRegionMap(const std::string& zoneName);


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.Diagnostics().<accessor>();
	// the renderer's own init/teardown reaches them through Diagnostics().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	bool& debugDirectionalLightEnabled() { return debugDirectionalLightEnabled_; }
	bool const& debugDirectionalLightEnabled() const { return debugDirectionalLightEnabled_; }
	bool& debugObjectLightsEnabled() { return debugObjectLightsEnabled_; }
	bool const& debugObjectLightsEnabled() const { return debugObjectLightsEnabled_; }
	bool& debugPlayerLightEnabled() { return debugPlayerLightEnabled_; }
	bool const& debugPlayerLightEnabled() const { return debugPlayerLightEnabled_; }
	bool const& hudEnabled() const { return hudEnabled_; }
	int const& maxObjectLights() const { return maxObjectLights_; }
	irr::scene::ILightSceneNode*& playerLightNode() { return playerLightNode_; }
	irr::scene::ILightSceneNode* const& playerLightNode() const { return playerLightNode_; }
	bool const& portalDebugDraw() const { return portalDebugDraw_; }
	bool const& sceneProfileEnabled() const { return sceneProfileEnabled_; }
	int& sceneProfileFrameCount() { return sceneProfileFrameCount_; }
	int const& sceneProfileFrameCount() const { return sceneProfileFrameCount_; }
	bool const& stencilDebugDraw() const { return stencilDebugDraw_; }
	irr::scene::ILightSceneNode*& sunLight() { return sunLight_; }
	irr::scene::ILightSceneNode* const& sunLight() const { return sunLight_; }
	bool& zoneLightsEnabled() { return zoneLightsEnabled_; }
	bool const& zoneLightsEnabled() const { return zoneLightsEnabled_; }

private:
	// Walk zone meshes and swap material types after shader variant change.
	// One caller, in-family (toggleZoneLights) -- K03 correction (a).
	void swapZoneMeshMaterials();

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's one single-family member, and the nested type it holds:
	// debug shader swap state (original material types saved when entering
	// debug mode). Nothing outside these two TUs ever named either.
	struct DebugMaterialEntry {
		irr::scene::IMeshSceneNode* node;
		irr::u32 materialIndex;
		irr::s32 originalType;
	};
	std::vector<DebugMaterialEntry> debugOriginalMaterials_;

	// L13: owned state, see the accessors above.
	bool debugDirectionalLightEnabled_ = true;  // /zlight toggle
	bool debugObjectLightsEnabled_ = true;  // /olight toggle
	bool debugPlayerLightEnabled_ = true;  // /plight toggle
	bool hudEnabled_ = true;
	int maxObjectLights_ = 8;  // Max object lights to display (1-8), cycles with L key
	irr::scene::ILightSceneNode* playerLightNode_ = nullptr;
	bool portalDebugDraw_ = false;
	bool sceneProfileEnabled_ = false;
	int sceneProfileFrameCount_ = 0;
	bool stencilDebugDraw_ = false;
	irr::scene::ILightSceneNode* sunLight_ = nullptr;  // Directional sun light
	bool zoneLightsEnabled_ = false;  // Zone lights off by default
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DIAGNOSTICS_DIAGNOSTICS_COMPONENT_H
