#ifndef EQT_GRAPHICS_ZONE_LOAD_ZONE_LOAD_COMPONENT_H
#define EQT_GRAPHICS_ZONE_LOAD_ZONE_LOAD_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L10 -- the
// zone_load family lifted out of class IrrlichtRenderer: loadZoneSequential()
// and the twenty per-step methods that build a zone (G01's decomposition of
// what used to be one 2,673-line method), across four TUs:
//
//   zone_load/renderer_zone_load.cpp          the driver + steps 0 (EQE1),
//       1 (S3D), 2 (BSP), 4 (Regions), 5 (Assets).
//   zone_load/renderer_zone_load_objects.cpp  steps 6 (Objects), 7 (Doors),
//       9 (Collision).
//   zone_load/renderer_zone_load_entities.cpp step 8 (Entities) and its six
//       8a-8f sub-steps.
//   zone_load/renderer_zone_load_env.cpp      steps 10 (Sky), 11 (Env),
//       12 (Lights), 12b (Simulation), 13 (Cleanup).
//
// Every body is the body that used to live in
// src/client/graphics/renderer_zone_load{,_objects,_entities,_env}.cpp, moved
// verbatim. Only the directory moved; all four basenames are unchanged, which
// keeps the Q02 pairing with tools/hwcaps/zone_bench_loader.cpp (which mirrors
// this code and cites it by basename) a one-token relationship and leaves
// docs/module_map.md's four rows resolvable.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, a method that stayed behind becomes owner_.Method(), and an
// IrrlichtRenderer-scoped nested type named in a body becomes
// IrrlichtRenderer::Type (batch correction 38).
//
// ⚠ THREADING -- NOTHING CHANGED, AND NOTHING HERE OWNS A THREAD.
// loadZoneSequential() runs on the LOADING THREAD, which owns the GL context
// for the whole of Application::loadZoneGraphicsOnThread()
// (src/client/application_zone_loading.cpp): startLoadingThread() extracts the
// GL handles on the main thread, releases the context, sets
// IrrlichtRenderer::setLoading(true) and hands
// EQT::Graphics::LoadingThread a callback that runs
// loadZoneGraphicsOnThread(status); that function clears the loading flag,
// unloads the previous zone, registers the snapshot's entities and doors, and
// then calls loadZoneSequential() -- reached through the forwarder below --
// as its step 8. The completion handshake is untouched: the step bodies write
// LoadingStatus only through ZoneLoadContext::updateProgress (which is
// status.setProgress + the loading-screen draw), the failure signal is
// loadFailed_ read back through hasLoadFailed() by the same function on the
// same thread, and loadingComplete/quitRequested are set by
// LoadingThread/Application, not here. The component is a plain member of
// IrrlichtRenderer with the same lifetime as every other L component, so the
// thread that called into IrrlichtRenderer is the thread that runs these
// bodies -- exactly as before. L07 correction 31: owner_/ctx_ is the answer to
// a thread-ownership question, not a workaround. The plan's STOP (a
// thread-ownership change) did not fire.
//
// FORWARDERS on IrrlichtRenderer, each justified by a NAMED caller and never
// by an interface (batch corrections 20/23/37 -- IrrlichtRenderer's only base
// is IZoneLoadSurface, whose two methods are initLoadingScreen() and
// initDeferredGraphics(), neither of which this unit touches). There are
// exactly two, and they are the loading thread's entry and its failure check:
//
//   loadZoneSequential()  src/client/application_zone_loading.cpp:275
//   hasLoadFailed()       src/client/application_zone_loading.cpp:277
//
// hasLoadFailed() is an inline accessor on a MOVING member (K03 correction (d)
// / K04 correction (f)): loadFailed_ moved into this component, so the
// accessor is part of the move and its one external caller turns it into a
// forwarder rather than a deletion. Application and IrrlichtBridge do not
// learn this component exists (the K11 rule).
//
// §Mechanism 5: NOTHING WAS DELETED. All twenty step methods have exactly one
// caller each -- loadZoneSequential() -- and loadZoneSequential() has the one
// production caller above, so the zero-caller delete never fired. Checked for
// K12 correction (ff) anyway: no body in any of the four TUs contains
// pushEvent or GameEventType::, so none of them could have been a sole
// publisher.
//
// ⚠ ZoneLoadContext MOVED WITH THE FAMILY. It is the parameter type of all
// twenty step methods, so under batch correction 10 it is exempt from
// §Mechanism 3 and follows the signatures it appears in; leaving it on
// IrrlichtRenderer would force this header to name
// IrrlichtRenderer::ZoneLoadContext and re-include the god header for a
// complete type. Its definition is in
// include/client/graphics/zone_load/zone_load_context.h -- still a private
// header that nothing outside these four TUs includes. Note the two "ctx"
// spellings in a moved body are DIFFERENT objects and always were: the
// parameter `ctx` is the ZoneLoadContext threaded through the step sequence,
// while `ctx_` is this component's RendererContext reference.
//
// MEMBERS: two moved (loadFailed_, pendingZoneComputations_ -- referenced by
// no TU outside this family), 49 reached through RendererContext, and 61
// shared members reached through owner_. The zone blob (currentZone_,
// regionMeshNodes_, objectNodes_, objectInSceneGraph_, portalSystem_,
// currentPvsRegion_) is jointly owned by six families and stays on the
// context; zoneShader_, constrainedTextureCache_ and doorManager_ likewise.
//
// TWO PRIVATE IrrlichtRenderer METHODS STAYED and are reached as owner_.X():
// ensureEntityPrepWorkerStarted() and deferEntityForLoadGate(), both defined
// in src/client/graphics/renderer_entity_load_gate.cpp -- a TU this unit's
// territory does not include. Their only callers are in
// renderer_zone_load_entities.cpp, so they are single-family by reference
// count and a later unit may fold that TU in here; neither touches a member
// this unit moved. Three more stay for the ordinary reason (other families
// call them too): getDisplaySettings(), isRegionPvsVisible(),
// setRenderDistance().
//
// SIBLING COMPONENTS reached across the boundary through IrrlichtRenderer's
// accessors (K06/K07 correction (r)'s already-on-fable form -- every one of
// these families is already extracted):
//   owner_.Eqe1()          resolveEQE1Archives, getOrOpenEQE1,
//                          openArchiveContaining, addSpawnedRaceEQE1Candidates,
//                          getEQE1MappedBytesTotal, logEQE1ArchiveUsage,
//                          compileIcosphereShader, buildIcosphereMesh
//   owner_.ZoneStream()    logAssetBuildTime, indexObjectMeshes
//   owner_.ZoneLifecycle() createZoneMesh, rebuildRegionMesh,
//                          buildRegionNeighborMap, uploadMeshHardwareBuffers
//   owner_.PlayerCamera()  extractWorldTriangles, rayIntersectTriangles,
//                          rayTestDoorsAndObjects, setupMinimalZoneCollision
//   owner_.Overlays()      drawLoadingScreen, setupFog,
//                          applyEnvironmentalDisplaySettings
//   owner_.SimBridge()     stopSimulationWorker, buildSimulationZoneData,
//                          applySimulationOutput
//   owner_.Diagnostics()   dumpScene
//
// The component needs friendship on BOTH axes of K13 correction (ii): axis 1,
// it reaches 61 private IrrlichtRenderer members and five private methods
// through owner_; axis 2, two bodies in renderer_zone_load_env.cpp name the
// private nested types IrrlichtRenderer::FireGlowLight (loadZoneStep11_Env)
// and IrrlichtRenderer::ZoneLightData (loadZoneStep12_Lights) as local
// declarations -- the third substitution (K05 (j) / batch correction 38),
// which no owner_. census can see.

#include "client/graphics/renderer_context.h"

#include <memory>
#include <string>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
struct LoadingStatus;

class ZoneLoadComponent {
public:
	ZoneLoadComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~ZoneLoadComponent();

	ZoneLoadComponent(const ZoneLoadComponent&) = delete;
	ZoneLoadComponent& operator=(const ZoneLoadComponent&) = delete;

	// Sequential zone loading — executes all 13 steps inline on the calling
	// thread (the loading thread; see the threading note above).
	// Updates status.percent/text between steps, checks status.quitRequested.
	void loadZoneSequential(const std::string& eqClientPath, LoadingStatus& status);
	bool hasLoadFailed() const { return loadFailed_; }

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// loadZoneSequential() per-step helpers (G01 decomposition).
	// ZoneLoadContext is forward-declared here and defined in
	// include/client/graphics/zone_load/zone_load_context.h; it holds every
	// local that crosses a step boundary. Steps that can hit an unrecoverable
	// error return false; the driver stops and returns immediately, matching
	// the original early-return control flow.
	struct ZoneLoadContext;
	void loadZoneStep0_EQE1(ZoneLoadContext& ctx);
	bool loadZoneStep1_S3D(ZoneLoadContext& ctx);
	void loadZoneStep2_BSP(ZoneLoadContext& ctx);
	void loadZoneStep4_Regions(ZoneLoadContext& ctx);
	void loadZoneStep5_Assets(ZoneLoadContext& ctx);
	void loadZoneStep6_Objects(ZoneLoadContext& ctx);
	void loadZoneStep7_Doors(ZoneLoadContext& ctx);
	bool loadZoneStep8_Entities(ZoneLoadContext& ctx);
	bool loadZoneStep8a_BaseModelPrep(ZoneLoadContext& ctx);
	void loadZoneStep8b_VariantModelPrep(ZoneLoadContext& ctx);
	void loadZoneStep8c_VariantTextures(ZoneLoadContext& ctx);
	bool loadZoneStep8d_EquipmentPrep(ZoneLoadContext& ctx);
	void loadZoneStep8e_DistributePrepResults(ZoneLoadContext& ctx);
	void loadZoneStep8f_BuildEntityMesh(ZoneLoadContext& ctx);
	void loadZoneStep9_Collision(ZoneLoadContext& ctx);
	bool loadZoneStep10_Sky(ZoneLoadContext& ctx);
	void loadZoneStep11_Env(ZoneLoadContext& ctx);
	void loadZoneStep12_Lights(ZoneLoadContext& ctx);
	void loadZoneStep12b_Simulation(ZoneLoadContext& ctx);
	void loadZoneStep13_Cleanup(ZoneLoadContext& ctx);

	// The family's two single-family members. PendingZoneComputations is a
	// namespace-scope type in renderer_types.h (not a nested one), so it needs
	// no qualification; it is held by unique_ptr and the destructor is
	// out-of-line for it (batch correction 35 -- the constructor's unwind path
	// needs the complete type just as the destructor does).
	std::unique_ptr<PendingZoneComputations> pendingZoneComputations_;
	bool loadFailed_ = false;   // True if loadZoneSequential() hit a FATAL error
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_LOAD_ZONE_LOAD_COMPONENT_H
