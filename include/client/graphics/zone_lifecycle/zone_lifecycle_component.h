#ifndef EQT_GRAPHICS_ZONE_LIFECYCLE_ZONE_LIFECYCLE_COMPONENT_H
#define EQT_GRAPHICS_ZONE_LIFECYCLE_ZONE_LIFECYCLE_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L09 -- three
// families lifted out of class IrrlichtRenderer in one component, because two
// of them own no state of their own (batch correction 26):
//
//   zone_lifecycle (renderer_zone_lifecycle.cpp) -- zone teardown, the stored
//       zone environment, the HCMap placeholder + instant scene, and the
//       entity-count / network-ready bookkeeping.
//   zone_draw      (renderer_zone_draw.cpp)      -- zone mesh creation, the
//       sorted manual draw path, per-region meshes, the portal/stencil
//       recursion and the VBO upload/delete helpers. 0 single-family members.
//   gpu_upload     (renderer_gpu_upload.cpp)     -- the GPU upload thread's
//       renderer-side driver: init, batch apply, drain and the GREEN-gated
//       process-one step. 0 single-family members.
//
// Every body in src/client/graphics/zone_lifecycle/*.cpp is the body that used
// to live in src/client/graphics/renderer_zone_{lifecycle,draw}.cpp and
// renderer_gpu_upload.cpp, moved verbatim. Only the directory moved; all three
// basenames are unchanged, which keeps tools/hwcaps' six citations of
// "renderer_zone_draw.cpp" resolvable and keeps the Q02 textureGroupId() copy
// in zone_bench_loader.cpp paired with the production one it mirrors.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, and a method that stayed behind becomes owner_.Method()
// (there are exactly four: isLoading(), setRenderDistance(),
// clearWorldObjects() and getDisplaySettings()).
//
// ⚠ THREADING -- NOTHING CHANGED. gpuUploadThread_ is a RendererContext core
// member under #ifdef EQT_HAS_GLES2 and stays there; GPUUploadThread owns its
// own thread and this component drives it from the render thread through
// ctx_.gpuUploadThread exactly as IrrlichtRenderer did. The plan's hard STOP
// was "if the simulation WORKER thread calls a body this unit moves, report
// the shape". It does not: the single mention of drawZoneGeometryWithPortals()
// in src/client/graphics/sim_visibility.cpp (line 159) is a COMMENT, and the
// six drawZoneGeometrySorted() mentions in diagnostics/, sim/, zone_stream/,
// zone_load_env/objects and constrained_renderer_config.h are comments too.
// K04 correction (g) for the ninth consecutive unit. L07 correction 31
// restated: owner_/ctx_ is the answer to a thread-ownership question, not a
// workaround.
//
// ⚠ IrrlichtRenderer::RenderPassTimer DID NOT MOVE. Its
// OnRenderPassPreRender() body sits in zone_lifecycle/renderer_zone_draw.cpp
// (batch correction 34: a multi-class TU), it is a nested class of
// IrrlichtRenderer and it stays one -- renderPassTimer_ is handed to Irrlicht's
// scene manager as an ILightManager and is torn down by the renderer. It is the
// ONLY caller of drawZoneGeometryWithPortals()/drawZoneGeometrySorted(), which
// it now reaches as renderer_->ZoneLifecycle().draw...(). Its own members
// (passStart_, currentPass_, entityGPUQueryActive_, ...) and its
// renderer_->captured3DView_/Proj_ writes were left byte-identical: "code lines
// only" means the MOVING class's lines.
//
// FORWARDERS on IrrlichtRenderer, each justified by a NAMED caller and never
// by an interface (batch corrections 20/23 -- IrrlichtRenderer implements only
// IZoneLoadSurface, whose two methods are initLoadingScreen() and
// initDeferredGraphics(), neither of which this unit touches):
//
//   unloadZone()             src/client/bridge/irrlicht_bridge.cpp:740,
//                            src/client/application_zone_loading.cpp:191,
//                            src/client/mode/graphical_mode.cpp:213
//   setupInstantScene()      src/client/application_zone_loading.cpp:206, 316
//   storeZoneEnvironment()   src/client/application_zone_loading.cpp:210, 320
//   setExpectedEntityCount() src/client/bridge/irrlicht_bridge.cpp:179
//   setNetworkReady()        src/client/bridge/irrlicht_bridge.cpp:180
//   setWeather()             src/client/bridge/irrlicht_bridge.cpp:623
//
// ⚠ setWeather() is the batch's K10 (t) trap in its cleanest form: five OTHER
// classes declare a same-named method -- WeatherAudio, ParticleManager,
// TreeWindController, WeatherEffectsController, WeatherSystem and
// WorldState -- and the only call whose receiver is an IrrlichtRenderer is the
// bridge line above. Likewise unloadZone(): ZoneAudioManager and GraphicalMode
// have their own, and NullRenderer/ConsoleRenderer/OutputRenderer declare
// unloadZone/setExpectedEntityCount/notifyEntityLoaded/setNetworkReady on a
// class hierarchy IrrlichtRenderer is not part of (correction 13).
//
// DELETED under §Mechanism 5 (zero callers tree-wide, in-family included --
// K03 correction (a)), none of them publishing a GameEventType (K12 (ff): no
// pushEvent/GameEventType:: appears anywhere in the three moved TUs):
//   notifyEntityLoaded()    empty body
//   checkAndSetZoneReady()  empty body
//   areEntitiesLoaded()     inline accessor on entitiesLoaded_ (K04 (f))
// Both bodies carried a "kept for interface compatibility" comment whose
// premise correction 13 refutes.
//
// The component needs friendship on axis 1 of K13 correction (ii): it reaches
// 100 private IrrlichtRenderer members through owner_ and four private
// IrrlichtRenderer methods. It names no IrrlichtRenderer static, but two
// bodies in renderer_gpu_upload.cpp name the private nested type
// IrrlichtRenderer::TextureRebuildEntry in a sort comparator (K05 correction
// (j), the third substitution), so axis 2 IS live too.

#include "client/graphics/renderer_context.h"
#include "client/graphics/portal_system.h"   // Portal, in drawPortalQuad()'s signature
#include "client/hc_map.h"                   // HCMap::Triangle, cachedPlaceholderTriangles_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
// Defined in include/client/graphics/gles2_egl_helper.h, which this header
// deliberately does NOT include: irrlicht_renderer.h includes this component
// header and pulls that one in only under
// #if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2). Forward-declaring it
// keeps the member's type independent of that guard; the constructor and the
// destructor are therefore both out-of-line (batch correction 35 -- a ctor's
// unwind path destroys already-initialised members and needs the complete
// type just as the dtor does).
class GLES2EGLHelper;

class ZoneLifecycleComponent {
public:
	ZoneLifecycleComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~ZoneLifecycleComponent();

	ZoneLifecycleComponent(const ZoneLifecycleComponent&) = delete;
	ZoneLifecycleComponent& operator=(const ZoneLifecycleComponent&) = delete;

	// --- Zone teardown ---
	// Unload current zone. Retains the persistent EQE1 archives named by
	// isPersistentEQE1Archive() (T214) instead of destroying every loader.
	void unloadZone();

	// --- Zone environment ---
	// Set zone environment parameters (sky type, fog colors) from server data.
	// skyType: sky type from NewZone_Struct (0-255)
	// zoneType: zone type (0=outdoor, 1=dungeon, etc. - indoor zones disable sky)
	// fogRed/Green/Blue: fog color arrays (4 values for different fog ranges)
	// fogMinClip/MaxClip: fog distance arrays (4 values)
	void setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
	                        const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
	                        const float fogMinClip[4], const float fogMaxClip[4]);
	// Store zone environment data for deferred application (after sky renderer init)
	void storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
	                          const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
	                          const float fogMinClip[4], const float fogMaxClip[4]);
	// ⚠ ZERO CALLERS, tree-wide, and so is setZoneEnvironment() apart from this
	// body. Kept rather than deleted under L03 correction 17's shape: the state
	// it consumes IS written live (application_zone_loading.cpp calls
	// storeZoneEnvironment() twice) and IS read live, but by a DIFFERENT and
	// later path -- renderer_zone_load_env.cpp reads storedZoneEnvironment_
	// directly at lines 232-256 and clears .pending at 327. So this pair is a
	// superseded duplicate of the apply path, not dead scaffolding, and
	// deleting it would erase the evidence rather than fix the duplication.
	// Boarded for a measured wire-or-delete decision; §Mechanism 5's
	// zero-caller delete deliberately not applied.
	void applyStoredZoneEnvironment();

	// --- Instant scene + placeholder ---
	// Build HCMap placeholder + collision for immediate gameplay (<5ms)
	void setupInstantScene(const std::string& zoneName, float playerX = 0, float playerY = 0, float playerZ = 0);
	// Create collision from HCMap data only (no S3D mesh needed)
	void setupHCMapCollision();
	void buildZonePlaceholder(float playerIrrX = 0, float playerIrrY = 0, float playerIrrZ = 0);
	void destroyZonePlaceholder();

	// --- Entity-count / ready-state bookkeeping ---
	// Set expected number of entities from ZoneSpawns
	void setExpectedEntityCount(size_t count);
	// Called when network packet exchange is complete
	void setNetworkReady(bool ready);
	// Set weather from server packet (type: 0=none, 1=rain, 2=snow; intensity: 1-10)
	void setWeather(uint8_t type, uint8_t intensity);

	// --- zone_draw ---
	void createZoneMesh();
	bool rebuildRegionMesh(size_t regionIdx);  // Build mesh for a single region
	// Hardware buffer (VBO/EBO) helpers for static zone geometry
	void uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode* node);
	void deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode* node);
	// Region neighbor map (built from portal system for 1-depth PVS expansion);
	// writes owner_.regionNeighbors_, which stays on IrrlichtRenderer because
	// ZoneStreamComponent (L08) reads it.
	void buildRegionNeighborMap();
	// The manual zone draw. Called ONLY from
	// IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(), which stays
	// on the renderer -- see the note above.
	void drawZoneGeometrySorted();
	void drawZoneGeometryWithPortals();
	void drawRegionMesh(size_t regionIdx);
	void drawPortalQuad(const Portal& portal);
	void drawPortalRecursive(size_t fromRegion, int stencilLevel, int maxDepth,
	                         std::unordered_set<size_t>& drawn);

	// --- gpu_upload ---
	// Toggle GPU upload thread on/off at runtime (for A/B frame timing comparison)
	void toggleGPUUploadThread();
#ifdef EQT_HAS_GLES2
	bool isGPUUploadThreadEnabled() const { return gpuUploadEnabled_; }
	void initGPUUploadThread();
	void processCompletedUploads();   // Batch mode (loading screen)
	void drainGPUResults();           // Cheap queue drain, no GL
	bool processOneGPUResult();       // Process 1 result (fence wait, register). GREEN-gated.
#else
	bool isGPUUploadThreadEnabled() const { return false; }
#endif


	// ------------------------------------------------------------------
	// L13: two IrrlichtRenderer-nested types travelled with the members they
	// type (batch correction 10). They are PUBLIC here because
	// renderer_gpu_upload.cpp names TextureRebuildEntry in a sort comparator.
	//
	// Why this component owns them (the pairwise rule):
	//   textureRebuildQueue_  13 reaches -- this component is the only WRITER
	//     (3 writes, 8 reads); ZoneStreamComponent's 2 reads come through
	//     ctx_.ZoneLifecycle().textureRebuildQueue().
	//   storedZoneEnvironment_  37 reaches, the second-largest two-component
	//     member in the tree. Both families write it, so the tie-break is use
	//     count: this component 20 (10W/10R) against zone_load's 17 (2W/15R).
	//     zone_load reaches it as ctx_.ZoneLifecycle().storedZoneEnvironment().
	// ------------------------------------------------------------------
	// Regions queued for texture-ready rebuild (sorted: player region first, then by distance)
	struct TextureRebuildEntry {
		size_t regionIdx;
		float distanceSq;
	};
	// Stored zone environment from NewZone packet (applied after global assets load)
	struct StoredZoneEnvironment {
		uint8_t skyType = 0;
		uint8_t zoneType = 0;
		uint8_t fogR[4] = {0}, fogG[4] = {0}, fogB[4] = {0};
		float fogMinClip[4] = {0}, fogMaxClip[4] = {0};
		bool pending = false;
	};

	std::vector<TextureRebuildEntry>& textureRebuildQueue() { return textureRebuildQueue_; }
	std::vector<TextureRebuildEntry> const& textureRebuildQueue() const { return textureRebuildQueue_; }
	StoredZoneEnvironment& storedZoneEnvironment() { return storedZoneEnvironment_; }
	StoredZoneEnvironment const& storedZoneEnvironment() const { return storedZoneEnvironment_; }


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.ZoneLifecycle().<accessor>();
	// the renderer's own init/teardown reaches them through ZoneLifecycle().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	irr::core::vector3df const& lastPlaceholderBuildPos() const { return lastPlaceholderBuildPos_; }
	size_t& loadedEntityCount() { return loadedEntityCount_; }
	size_t const& loadedEntityCount() const { return loadedEntityCount_; }
	size_t& mergeZoneDrawsBuildEvictionCount() { return mergeZoneDrawsBuildEvictionCount_; }
	size_t const& mergeZoneDrawsBuildEvictionCount() const { return mergeZoneDrawsBuildEvictionCount_; }
	bool& mergeZoneDrawsDisabledForZone() { return mergeZoneDrawsDisabledForZone_; }
	bool const& mergeZoneDrawsDisabledForZone() const { return mergeZoneDrawsDisabledForZone_; }
	bool& networkReady() { return networkReady_; }
	bool const& networkReady() const { return networkReady_; }
	std::unordered_map<std::string, std::unordered_set<uint8_t>>& pendingTextureDoors() { return pendingTextureDoors_; }
	std::unordered_map<std::string, std::unordered_set<uint8_t>> const& pendingTextureDoors() const { return pendingTextureDoors_; }
	std::vector<ZoneDrawEntry>& sortedDrawEntries() { return sortedDrawEntries_; }
	std::vector<ZoneDrawEntry> const& sortedDrawEntries() const { return sortedDrawEntries_; }
	irr::scene::ITriangleSelector*& terrainOnlySelector() { return terrainOnlySelector_; }
	irr::scene::ITriangleSelector* const& terrainOnlySelector() const { return terrainOnlySelector_; }
	float& userRenderDistance() { return userRenderDistance_; }
	float const& userRenderDistance() const { return userRenderDistance_; }
	irr::scene::IMeshSceneNode* const& zoneCollisionNode() const { return zoneCollisionNode_; }
	float& zoneMaxClip() { return zoneMaxClip_; }
	float const& zoneMaxClip() const { return zoneMaxClip_; }
	std::unique_ptr<ZoneMergeBuffers> const& zoneMergeBuffers() const { return zoneMergeBuffers_; }
	irr::scene::ITriangleSelector*& zoneTriangleSelector() { return zoneTriangleSelector_; }
	irr::scene::ITriangleSelector* const& zoneTriangleSelector() const { return zoneTriangleSelector_; }

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's single-family members. Every other member these bodies
	// touch is either RendererContext core (ctx_.) or shared with another
	// family and stays on IrrlichtRenderer (owner_.).
	//
	// gpuUploadEnabled_ carries the same #ifdef its accessor does.
	// gles2Helper_ is only ever READ (unloadZone()'s
	// #if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2) branch) -- nothing
	// tree-wide ever constructs it, so that branch can never fire. Moved
	// verbatim and boarded rather than deleted: it is referenced, so it is not
	// the §Mechanism 3 zero-reference case.
	// entitiesLoaded_ is likewise write-only after L09 deleted its zero-caller
	// accessor areEntitiesLoaded().
#ifdef EQT_HAS_GLES2
	bool gpuUploadEnabled_ = true;   // Runtime toggle for /upload command
#endif
#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
	std::unique_ptr<GLES2EGLHelper> gles2Helper_;  // GLES2 context for ETC1 EGL image sharing
#endif
	// Cached terrain triangles for placeholder rebuild
	std::vector<HCMap::Triangle> cachedPlaceholderTriangles_;
	// Regions waiting for async fallback textures to complete GPU upload.
	// Maps texture name -> set of region indices built without that texture.
	std::unordered_map<std::string, std::unordered_set<size_t>> pendingTextureRegions_;
	bool entitiesLoaded_ = false;    // True when all entities have been loaded with models/textures
	size_t expectedEntityCount_ = 0; // Expected number of entities from ZoneSpawns

	// L13: owned state, see the accessors above.
	std::vector<TextureRebuildEntry> textureRebuildQueue_;
	StoredZoneEnvironment storedZoneEnvironment_;

	// L13: owned state, see the accessors above.
	irr::core::vector3df lastPlaceholderBuildPos_;  // Irrlicht coords of last placeholder build
	size_t loadedEntityCount_ = 0;  // Number of entities fully loaded so far
	size_t mergeZoneDrawsBuildEvictionCount_ = 0;
	bool mergeZoneDrawsDisabledForZone_ = false;
	bool networkReady_ = false;  // True when network packet exchange is complete
	std::unordered_map<std::string, std::unordered_set<uint8_t>> pendingTextureDoors_;
	std::vector<ZoneDrawEntry> sortedDrawEntries_;  // Material-sorted mesh buffer draw list
	irr::scene::ITriangleSelector* terrainOnlySelector_ = nullptr;
	float userRenderDistance_ = 300.0f;  // User's desired render distance (slider value)
	irr::scene::IMeshSceneNode* zoneCollisionNode_ = nullptr;  // Hidden node for zone collision in PVS mode
	float zoneMaxClip_ = 99999.0f;  // Server-provided max clip plane for current zone (0 = no limit)
	std::unique_ptr<ZoneMergeBuffers> zoneMergeBuffers_;
	irr::scene::ITriangleSelector* zoneTriangleSelector_ = nullptr;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_LIFECYCLE_ZONE_LIFECYCLE_COMPONENT_H
