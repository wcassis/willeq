#ifndef EQT_GRAPHICS_SIM_SIM_BRIDGE_COMPONENT_H
#define EQT_GRAPHICS_SIM_SIM_BRIDGE_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L07 -- the
// simulation-worker bridge, lifted out of class IrrlichtRenderer: the zone
// data snapshot handed to the worker, the worker's start/stop, the per-frame
// input marshaling, and the two halves of the output apply (values, then the
// frame-scoped pointer binds).
//
// Every body in src/client/graphics/sim/renderer_sim_bridge.cpp is the body
// that used to live in src/client/graphics/renderer_sim_bridge.cpp, moved
// verbatim. Only the directory moved; the basename is unchanged.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, and a method that stayed behind becomes owner_.Method()
// (there are exactly three, all PVS-neighborhood bookkeeping that belongs to
// the zone_stream family: isInPvsNeighborhood(), rebuildPvsNeighborhood() and
// onPvsRegionChanged()).
//
// ⚠ THREADING. SimulationWorker runs on its OWN thread and this component did
// NOT take ownership of it. simulationWorker_ (the std::unique_ptr, its
// construction in initLoadingScreen(), its start()/stop() lifetime and its
// double-buffered output_[2]) all stay exactly where they were, on
// IrrlichtRenderer, and are reached here through owner_.simulationWorker_.
// Every body in this component runs on the render thread or on the loading
// thread, precisely as it did before L07 -- this unit changed no thread
// ownership, no synchronisation and no buffer lifetime. That was the unit's
// hard STOP condition and it never fired.
//
// ⚠ THE T116 LIFETIME SPLIT IS THE POINT OF TWO OF THESE METHODS
// (refactor/T116_simulationoutput_lifetime.md). applySimulationOutput() is
// VALUE-ONLY: it stores no pointer into its SimulationOutput anywhere, so it
// is safe with an argument of any lifetime -- including loadZoneSequential()'s
// Step12b stack local (renderer_zone_load_env.cpp). bindFrameSimulationBuffers()
// is the ONLY function permitted to store a pointer/reference into a
// SimulationOutput on a member, and its argument MUST outlive every frame
// those members are read on, so applySimulationResults() -- holding the
// worker's persistent output_[2] -- is its only production caller. Adding a
// pointer-storing line to applySimulationOutput() reopens a dangling-pointer
// class that was shipped three separate times (T111 x2, T115 x1). The split is
// pinned by tests/test_simulation_output_lifetime.cpp, which calls BOTH of
// these real bodies through this component.
//
// The four pointer members that split protects (currentFrameSimResults_,
// regionPvsDepthMap_, portalVisibleRegions_, particleRenderBuffer_) all STAY
// on IrrlichtRenderer under §Mechanism 3 -- renderer_zone_draw.cpp,
// renderer_zone_stream.cpp, renderer_zone_lifecycle.cpp and
// renderer_frame_render.cpp read them -- and so do the six hand-expanded
// FRIEND_TEST friend lines in irrlicht_renderer.h that let the lifetime
// fixtures assert on them. L07 moved neither; the fixtures observe exactly
// what they observed before, and only the RECEIVER of their two production
// calls changed (renderer.SimBridge().applySimulationOutput(...)).
//
// The component needs friendship on axis 1 of K13 correction (ii): it reaches
// 28 private IrrlichtRenderer members through owner_ and three private
// IrrlichtRenderer methods. It names no IrrlichtRenderer static and no
// IrrlichtRenderer nested type, so axis 2 is not live.

#include "client/graphics/renderer_context.h"
#include "client/graphics/simulation_worker.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class SimBridgeComponent {
public:
	SimBridgeComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// --- Worker lifetime and zone data ---
	// Build the immutable zone snapshot the worker culls/lights against.
	// Callers: startSimulationWorkerEarly() below and Step12b
	// (renderer_zone_load_env.cpp).
	SimulationZoneData buildSimulationZoneData();
	// Build core zone data (BSP/regions/objects/lights) and start the worker.
	// S05: the worker itself is already created by initLoadingScreen().
	void startSimulationWorkerEarly();
	// registerSimulationWorkerTreeData() removed (P32): it fed
	// AnimatedTreeManager's CPU-instancing tree buffers into the
	// SimulationWorker, but was never called from anywhere in the tree --
	// dead code, confirmed uncalled before deletion. Went with the class.
	void registerSimulationWorkerVertexAnimData();  // Add vertex anim data after objects are installed
	void stopSimulationWorker();     // Stop worker (zone transition)

	// --- Per-frame input ---
	void postSimulationInput(float deltaTime);  // Snapshot main thread state → worker input

	// --- Per-frame output ---
	// The runtime path, called every frame from IrrlichtRenderer::
	// processFrameSimulation(): swap the worker's buffers, bind the frame
	// pointers, then apply the values. The ONLY caller of the bind step.
	void applySimulationResults();
	// Value-only: no pointers stored. Safe with ANY lifetime, including a
	// stack local -- see this header's T116 note.
	void applySimulationOutput(const SimulationOutput& results);
	// Stores pointers into `persistent`'s containers on four IrrlichtRenderer
	// members. `persistent` MUST outlive every frame those pointers are read
	// on -- call this ONLY with a pointer into SimulationWorker's persistent,
	// double-buffered output_[2]. Passing a stack-local SimulationOutput here
	// reopens the exact dangling-pointer hazard T111 and T115 each patched.
	// This is why the load path (Step12b) does not call it.
	void bindFrameSimulationBuffers(const SimulationOutput* persistent);

	// /simworker debug command. Its one caller is the renderer's own slash
	// command dispatcher, so there is no forwarder on IrrlichtRenderer.
	std::vector<std::string> getSimWorkerDebugInfo() const;

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's two single-family members: the particle entity
	// positions/directions resolved from one frame's worker output and posted
	// back as the next frame's worker input. Written and read only by
	// applySimulationOutput() and postSimulationInput(); nothing outside this
	// TU ever named them.
	std::unordered_map<uint16_t, glm::vec3> pendingParticleEntityPositions_;   // Resolved positions for next frame
	std::unordered_map<uint16_t, glm::vec3> pendingParticleEntityDirections_;  // Resolved directions for next frame
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SIM_SIM_BRIDGE_COMPONENT_H
