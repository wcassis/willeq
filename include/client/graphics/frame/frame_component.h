#ifndef EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H
#define EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L11 -- the frame
// family lifted out of class IrrlichtRenderer: the per-frame loop, the input
// drain and the static UI panel state, across three TUs:
//
//   frame/renderer_frame.cpp         processFrame(), the four-phase
//       orchestrator, plus the frame-timing accumulation and the spike/RED/
//       budget diagnostics it emits.
//   frame/renderer_frame_input.cpp   phase 1 (processFrameInput,
//       processCommonInput, processPlayerInput, processInputDeltas,
//       processChatInput), phase 2 (processFrameVisibility) and phase 3
//       (processFrameSimulation).
//   frame/renderer_frame_render.cpp  phase 4 (processFrameRender).
//
// Every body is the body that used to live in
// src/client/graphics/renderer_frame{,_input,_render}.cpp, moved verbatim.
// Only the directory moved; all three basenames are unchanged, because
// tests/test_ui_hide_command.cpp and tests/test_static_ui_scroll.cpp read two
// of them AS TEXT and docs/module_map.md lists all three.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, a method that stayed behind becomes owner_.Method(), a
// sibling component pointer becomes owner_.<Family>(), and an
// IrrlichtRenderer-scoped nested type named in a body becomes
// IrrlichtRenderer::Type (batch corrections 38/41 -- here exactly once,
// FrameTimings() in processFrame's accumulator reset).
//
// THREADING -- NOTHING CHANGED, AND NOTHING HERE OWNS A THREAD.
// This is the render/game thread boundary, so it is the one place in Batch L
// where the STOP condition had to be checked line by line rather than
// asserted. The shape, unchanged by this unit:
//
//   * processFrame() runs on the RENDER thread. Its two production callers
//     are Application::render() (src/client/application_frame.cpp:181,
//     through the forwarder kept on IrrlichtRenderer) and
//     IrrlichtRenderer::run() (irrlicht_renderer.cpp), which is the render
//     thread's own loop. Every phase below it runs on that same thread, in
//     the same order, with the same early-returns.
//   * processFrameInput() drains RendererEventReceiver's render-thread
//     queues (drainActions, wasLeftButtonClicked, getMouseWheelDelta, the
//     key events) exactly as before. eventReceiver_ itself is a
//     RendererContext core member and did NOT move; the receiver's own
//     mutex-guarded bridgeQueue_ (T312) is untouched, and this component
//     never reaches into it except through the receiver's public API.
//   * processChatInput() is the render-thread end of the T319 prefill trip.
//     The parked text lives in IrrlichtRenderer::pendingChatPrefill_ under
//     IrrlichtRenderer::chatPrefillMutex_ and is taken through
//     owner_.takeChatPrefill(). BOTH the member and the mutex stayed on
//     IrrlichtRenderer; the accessor pair is the only access path, exactly as
//     the header there requires. L07 correction 31 for the fifth time: owner_
//     is the ANSWER to a thread-ownership question, not a workaround for one.
//   * processFrameSimulation()/processFrame() talk to the SimulationWorker
//     only through owner_.SimBridge() (applySimulationResults,
//     postSimulationInput), whose worker handle stayed on IrrlichtRenderer at
//     L07. No synchronisation primitive, no queue, no buffer lifetime and no
//     thread ownership changed anywhere in this unit.
//
// The UI PANEL STATE below is written by the GAME thread through
// src/client/bridge/irrlicht_bridge.cpp and read by the RENDER thread here,
// and that has always been true -- these sixteen members were already PUBLIC
// on IrrlichtRenderer with the comment "Public so IrrlichtBridge can update
// them directly". Moving them changes the receiver expression and nothing
// else: same objects, same lifetime (a plain member of IrrlichtRenderer),
// same absence of a lock, same threads. Five more that were private
// (uiLayout_, uiRenderer_, newUIEnabled_, allUIHidden_, staticUIInput_) are
// public here for the same reason -- their external reachers are
// renderer_init.cpp, SlashCommandsComponent and DiagnosticsComponent, and a
// public member keeps every one of those a pure receiver retarget. THAT IS
// THE SMALLEST PUBLIC SURFACE AVAILABLE: it adds no new named API at all,
// where an accessor pair per member would have added ten method names (and,
// for the three unique_ptr members that renderer_init.cpp assigns into, would
// have had to hand out a reference to the smart pointer itself).
//
// FORWARDERS on IrrlichtRenderer, each justified by a NAMED caller and never
// by an interface (batch corrections 20/23/37 -- IrrlichtRenderer's only base
// is IZoneLoadSurface, whose two methods are initLoadingScreen() and
// initDeferredGraphics(), neither of them in this move set):
//
//   processFrame()        src/client/application_frame.cpp:181,
//                         src/client/graphics/renderer/irrlicht_renderer.cpp (run()),
//                         tests/test_zoning_graphics_integration.cpp (4),
//                         tests/test_beneficial_spell_casting.cpp (2)
//   isAllUIHidden()       inline accessor on a MOVING member (K03 (d) /
//                         K04 (f)); callers in slash/ and two tests
//   isNewUIEnabled()      inline accessor on a MOVING member; slash/
//   setNewUIEnabled(bool) inline accessor on a MOVING member
//   getUIRenderer()       inline accessor on a MOVING member
//
// run() and shutdown() stay on IrrlichtRenderer (core, not frame). So does
// measureSection(): L04 recorded it as frame-phase state whose body was
// relocated into this family's TU without moving its ownership, and this unit
// leaves that decision alone -- sectionStart_ and the body stay on
// IrrlichtRenderer, in frame/renderer_frame.cpp, and the 35 call sites in the
// moved bodies reach it as owner_.measureSection().
//
// MEMBERS: 31 moved (21 UI panel/static-UI state, 10 private frame-loop
// state) plus the two tier-interval constants; 42 reached through
// RendererContext; the rest -- frameTimings_/frameTimingsAccum_ and their two
// bookkeeping members above all (shared with DiagnosticsComponent, 93
// references there) -- reached through owner_.
//
// SIBLING COMPONENTS reached across the boundary through IrrlichtRenderer's
// accessors (K06/K07 correction (r)'s already-on-fable form):
//   owner_.Overlays()      handleMouseTargeting, clearCurrentTarget,
//                          toggleZoneLineVisualization, toggleMapOverlay,
//                          toggleNavmeshOverlay, updateHUD, drawLoadingScreen,
//                          drawTargetOutline/drawTargetSelectionBox,
//                          updateMapOverlay/drawMapOverlay,
//                          updateNavmeshOverlay/drawNavmeshOverlay,
//                          drawFPSCounter, updatePaperdollForFrame,
//                          drawZoneLineOverlay, drawZoneLineBoxLabels
//   owner_.Diagnostics()   toggleAllUI, toggleZoneLights, cycleObjectLights,
//                          toggleWireframe, toggleHUD, toggleNameTags,
//                          toggleOldModels, logFrameTimings,
//                          profileSceneBreakdown
//   owner_.PlayerCamera()  updatePlayerMovement, applyFirstPersonPitchClamp,
//                          drawCollisionDebugLines
//   owner_.SimBridge()     applySimulationResults, postSimulationInput
//   owner_.ZoneStream()    processFrameProgressiveLoad, buildDeferredObject,
//                          queueEntityPrepRequests
//   owner_.ZoneLifecycle() processCompletedUploads, drainGPUResults,
//                          processOneGPUResult, buildZonePlaceholder,
//                          setupHCMapCollision
//   owner_.Eqe1()          updateFireGlowLighting, renderFireGlowIcospheres
//   owner_.Rdp()           captureFrameForRDP
//
// SIX IrrlichtRenderer METHODS THAT STAYED are reached as owner_.X():
// isRunning(), measureSection(), takeChatPrefill(), getNearestWorldObject(),
// toggleInventory() and saveScreenshot().
//
// The component needs friendship on BOTH axes of K13 correction (ii): axis 1,
// it reaches ~60 private IrrlichtRenderer members and the six private methods
// above through owner_; axis 2, processFrame() names the private nested type
// IrrlichtRenderer::FrameTimings when it resets the accumulator.

#include "client/graphics/renderer_context.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace eqt { namespace ui { class ChatMessageBuffer; } }

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
class UIRenderer;
class StaticUIInput;

class FrameComponent {
public:
	FrameComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~FrameComponent();

	FrameComponent(const FrameComponent&) = delete;
	FrameComponent& operator=(const FrameComponent&) = delete;

	// The per-frame entry point. Returns false when the renderer should stop.
	// Reached from production through IrrlichtRenderer::processFrame().
	bool processFrame(float deltaTime);

	// Phase 1 sub-steps that tests drive directly (each on a bare,
	// never-Init()'d renderer -- the T66/T233/T235 no-device idiom):
	//   processCommonInput  tests/test_ui_hide_command.cpp,
	//                       tests/test_debug_hotkey_arms.cpp
	//   processPlayerInput  tests/test_inventory_toggle.cpp
	//   processChatInput    tests/test_spell_gem_key_race.cpp,
	//                       tests/test_reply_to_tell_key.cpp
	void processCommonInput(const std::vector<RendererEvent>& actions);
	void processPlayerInput(const std::vector<RendererEvent>& actions);
	void processChatInput();

	// ---- UI panel state ----
	// The first sixteen were already public on IrrlichtRenderer, with the
	// comment "Public so IrrlichtBridge can update them directly"; the game
	// thread writes them through src/client/bridge/irrlicht_bridge.cpp and
	// this component's phase-4 body reads them. Nothing about that changed.
	PlayerStatsData cachedPlayerStats_;
	TargetInfoData cachedTargetInfo_;
	ChatPanelState chatPanelState_;        // U04
	std::unique_ptr<eqt::ui::ChatMessageBuffer> newUIChatBuffer_;  // U04
	InventoryPanelState inventoryState_;  // U05
	HotbarPanelState hotbarState_;       // U06a
	SpellGemPanelState spellGemState_;  // U06b
	BuffBarState buffBarState_;         // U06c
	CastingBarState castingBarState_;  // U06d
	GroupPanelState groupPanelState_;  // U06e
	PetPanelState petPanelState_;       // U06f
	SpellbookPopupState spellbookState_; // U06g
	SkillsPopupState skillsPopupState_; // U06h
	// T290: renderer-side corpse contents, restored. Fed only by the four
	// loot bridge arms (applyLoot*), read by renderLootPopup() and
	// StaticUIInput's row/button hit tests.
	LootPanelState lootState_;          // T290
	XPBarState xpBarState_;             // U06i
	std::unique_ptr<StaticUIInput> staticUIInput_;  // U06j

	// The five that were private on IrrlichtRenderer. Public here so that
	// renderer_init.cpp (which constructs uiRenderer_/staticUIInput_/
	// newUIChatBuffer_ and computes the layout), SlashCommandsComponent
	// (/newui, /scroll) and DiagnosticsComponent (toggleAllUI) each stay a
	// pure receiver retarget rather than needing an accessor pair.
	std::unique_ptr<UIRenderer> uiRenderer_;  // U03b
	UILayout uiLayout_;                       // U03a
	bool newUIEnabled_ = true;                // U07c1: new UI on by default
	bool allUIHidden_ = false;


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.Frame().<accessor>();
	// the renderer's own init/teardown reaches them through Frame().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	bool const& chatInputFocused() const { return chatInputFocused_; }
	int const& currentFps() const { return currentFps_; }
	float const& essentialSimCostAvgUs() const { return essentialSimCostAvgUs_; }
	float const& fireGlowTime() const { return fireGlowTime_; }
	bool& frameTimingEnabled() { return frameTimingEnabled_; }
	bool const& frameTimingEnabled() const { return frameTimingEnabled_; }
	FrameTimings& timingsAccum() { return frameTimingsAccum_; }
	FrameTimings const& timingsAccum() const { return frameTimingsAccum_; }
	int& timingsSampleCount() { return frameTimingsSampleCount_; }
	int const& timingsSampleCount() const { return frameTimingsSampleCount_; }
	FrameTimings& timings() { return frameTimings_; }
	FrameTimings const& timings() const { return frameTimings_; }
	bool& have3DTransforms() { return have3DTransforms_; }
	bool const& have3DTransforms() const { return have3DTransforms_; }
	irr::u32& lastFpsTime() { return lastFpsTime_; }
	irr::u32 const& lastFpsTime() const { return lastFpsTime_; }
	uint32_t& lastPolygonCount() { return lastPolygonCount_; }
	uint32_t const& lastPolygonCount() const { return lastPolygonCount_; }
	float& loadingProgress() { return loadingProgress_; }
	float const& loadingProgress() const { return loadingProgress_; }
	std::wstring& loadingText() { return loadingText_; }
	std::wstring const& loadingText() const { return loadingText_; }
	float const& renderCostAvgUs() const { return renderCostAvgUs_; }

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// Phase methods with no caller outside processFrame().
	void processFrameInput(float deltaTime);
	void processFrameVisibility();
	void processFrameSimulation(float deltaTime);
	bool processFrameRender(float deltaTime);  // Returns false for loading screen early-return
	void processInputDeltas(float deltaTime);

	// G05 (refactor/G05_process_frame_render.md): the sixteen render passes
	// processFrameRender() calls, in this order. Each is a verbatim slice of
	// the pre-G05 body INCLUDING its measureSection() bracket, so the
	// frameTimings_ categories are unchanged; the ORDER is load-bearing and is
	// pinned by tests/test_frame_render_order.cpp against
	// tests/goldens/frame_render_order_g05.txt. No local of the original body
	// crossed a cut, so there is no cross-pass state object: everything these
	// share is already a member or reached through ctx_. Nothing about the
	// render/game thread boundary changed -- all sixteen run on the render
	// thread, in the same order, exactly as the single body did.
	// Callers: processFrameRender() and nothing else.
	void runSceneProfileIfScheduled();
	void beginSceneAndDrawWorld(float deltaTime);
	void collectScenePassTimings();
	void renderFootprints();
	void checkFrameBudgetsAndLogStats();
	void renderTargetIndicator();
	void renderParticleEffects();
	void renderFireGlowAndBoids(float deltaTime);
	void renderWeatherEffects();
	void renderDebugOverlays(float deltaTime);
	void renderEntityCastingBars();
	void renderLegacyGui();
	void renderStaticUI();
	void renderZoneLineOverlays();
	void renderFrameTail();
	void endSceneAndSwap();

	// L12 (Batch L close-out, L11's hand-off): the frame-phase stopwatch.
	// L04 put the body in this family's TU but left the declaration and
	// sectionStart_ on IrrlichtRenderer, and L11 was told to leave it there;
	// the measurement was single-family in both units (35 call sites, all in
	// renderer_frame{,_input,_render}.cpp; zero readers of sectionStart_
	// outside them). Both come here now, in one substitution pass -- the body
	// in frame/renderer_frame.cpp is unchanged apart from its qualifier,
	// because sectionStart_ keeps its spelling.
	int64_t measureSection();
	std::chrono::steady_clock::time_point sectionStart_;

	// Frame-loop state referenced by no TU outside this family.
	bool pendingUIClick_ = false;  // Click consumed in Phase 1, applied to UI in render phase
	bool runTier2_ = true;
	bool runTier3_ = true;
	static constexpr uint32_t kTier2Interval = 3;   // ~20Hz at 60fps
	static constexpr uint32_t kTier3Interval = 6;   // ~10Hz at 60fps
	float tier3DeltaAccum_ = 0.0f;  // Accumulated delta for Tier 3 simulation
	float windTime_ = 0.0f;  // Monotonic wind time for GPU wind shader (seconds)
	bool pendingScreenshot_ = false;  // Deferred to after render, before swap
	float hudAnimTimer_ = 0.0f;  // Timer for HUD animations (auto attack indicator)
	int polygonBudgetExceededFrames_ = 0;  // Throttle warnings
	int constrainedStatsLogCounter_ = 0;   // Periodic stats logging
	bool frustumDebugDraw_ = false;  // Draw region bboxes colored by frustum result

	// L13: owned state, see the accessors above.
	bool chatInputFocused_ = false;
	int currentFps_ = 0;
	float essentialSimCostAvgUs_ = 3000.0f;  // EMA of essential sim cost (us), init ~3ms
	float fireGlowTime_ = 0.0f;  // Animation time accumulator
	int frameCount_ = 0;
	bool frameTimingEnabled_ = false;  // Enable with /frametiming command
	FrameTimings frameTimingsAccum_;  // Accumulated over multiple frames
	int frameTimingsSampleCount_ = 0;
	FrameTimings frameTimings_;
	bool have3DTransforms_ = false;
	irr::u32 lastFpsTime_ = 0;
	uint32_t lastPolygonCount_ = 0;  // DiagnosticsComponent reads it; the two
	float loadingProgress_ = 0.0f;  // Loading progress for zone transitions (0.0 - 1.0)
	std::wstring loadingText_ = L"Initializing...";  // Loading stage text
	float renderCostAvgUs_ = 15000.0f;  // EMA of render phase cost (us), init ~15ms
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H
