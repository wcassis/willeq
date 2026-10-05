#ifndef EQT_GRAPHICS_RENDERER_CONTEXT_H
#define EQT_GRAPHICS_RENDERER_CONTEXT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L01.
//
// RendererContext is the bundle of shared state every extracted renderer
// component receives by reference. Its membership is the measured "core" of
// class IrrlichtRenderer: the data members referenced by five or more of the
// src/client/graphics/renderer_*.cpp TU families (see the batch doc's
// "Coupling" table). It is the renderer's answer to eqt::ClientContext, which
// Batch K created for class EverQuest.
//
// Each member is a REFERENCE to the IrrlichtRenderer member of the SAME type,
// named without the trailing underscore -- a std::unique_ptr<X> member becomes
// std::unique_ptr<X>&, not X&. Nothing is dereferenced here: a component sees
// exactly what the code it was moved out of saw, so a moved body's
// `doorManager_->Foo()` becomes `ctx_.doorManager->Foo()` and nothing else
// changes.
//
// IrrlichtRenderer owns one of these (IrrlichtRenderer::ctx_), built in its
// constructor's initialiser list from its own members and declared after every
// member it binds. It is not copyable or assignable by construction (reference
// members), which is what we want: there is exactly one per IrrlichtRenderer.

#include "client/graphics/renderer_types.h"   // RendererConfig, ObjectLight, S3DZone, irr, std containers
#include "client/graphics/placed_object_meshes.h"  // B938: PlacedObjectRecord (objectRecords binding)
#include "client/graphics/renderer_draw_types.h"  // L12: SortedRegionEntry (sortedZoneDrawList)
#include "client/graphics/object_collision_set.h"  // B909b: objectCollision

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

class HCMap;

namespace eqt {
namespace bridge {
class GameStateBridge;
}
}

namespace EQT {
namespace Graphics {

// Forward declarations for the core members held by unique_ptr or by
// reference: a reference member needs no complete type here.
class EntityRenderer;
class ZoneShaderManager;
class DoorManager;
struct BspTree;
class AnimatedTextureManager;
class RendererEventReceiver;
class PortalSystem;
class FrustumCuller;
class FrameBudgetGovernor;
class GPUUploadThread;
class SoftwareOcclusionCuller;
class TreeWindController;
class EntityPrepWorker;
class RenderPassTimer;  // L13: hoisted out of IrrlichtRenderer (render_pass_timer.h)

// L12: the eleven extracted components, reached through the accessors below.
// Pointers only -- no component header is included here, so renderer_context.h
// stays free of the transitive include set irrlicht_renderer.h drags in.
class FrameComponent;
class DiagnosticsComponent;
class OverlaysComponent;
class PlayerCameraComponent;
class SlashCommandsComponent;
class SimBridgeComponent;
class ZoneStreamComponent;
class ZoneLifecycleComponent;
class ZoneLoadComponent;
class Eqe1ArchiveComponent;
class RdpComponent;
namespace Environment {
class ParticleManager;
class BoidsManager;
class TumbleweedManager;
}
namespace Detail {
class DetailManager;
}

struct RendererContext {
	// 13 families
	RendererConfig& config;
	std::unique_ptr<EntityRenderer>& entityRenderer;
	irr::video::IVideoDriver*& driver;
	// 12 families
	ConstrainedTextureCache*& constrainedTextureCache;
	irr::scene::ISceneManager*& smgr;
	// 11 families
	std::unique_ptr<ZoneShaderManager>& zoneShader;
	// 10 families -- the zone blob is jointly owned and stays here, not in
	// any one zone family (batch doc, "Coupling").
	std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes;
	std::unique_ptr<DoorManager>& doorManager;
	std::shared_ptr<EQT::Graphics::BspTree>& zoneBspTree;
	std::vector<irr::scene::IMeshSceneNode*>& objectNodes;
	// B938 (#256): the per-placement state -- the source of truth the manual
	// draw path, the sim worker and the texture-eviction guard read. Indexed
	// parallel to objectNodes (null node in PVS zones).
	std::vector<PlacedObjectRecord>& objectRecords;
	size_t& currentPvsRegion;
	// 5-9 families
	irr::scene::ICameraSceneNode*& camera;
	std::unique_ptr<WeatherEffectsController>& weatherEffects;
	irr::IrrlichtDevice*& device;
	std::shared_ptr<S3DZone>& currentZone;
	std::unique_ptr<SkyRenderer>& skyRenderer;
	float& playerX;
	float& playerY;
	float& playerZ;
	std::unique_ptr<Environment::ParticleManager>& particleManager;
	std::unique_ptr<Detail::DetailManager>& detailManager;
	std::string& currentZoneName;
	std::unique_ptr<Environment::BoidsManager>& boidsManager;
	std::map<size_t, irr::core::aabbox3df>& regionBoundingBoxes;
	bool& manualZoneDrawEnabled;
	bool& progressiveLoadingActive;
	bool& portalOcclusionEnabled;
	bool& fogEnabled;
	bool& usePvsCulling;
	bool& entityPrepReady;
	bool& wireframeMode;
	bool& portalOcclusionEligible;
	bool& lightingEnabled;
	bool& fireEffectsEnabled;
	std::unique_ptr<RendererEventReceiver>& eventReceiver;
	std::unique_ptr<CameraController>& cameraController;
	std::vector<ObjectLight>& objectLights;
	std::shared_ptr<const HCMap>& collisionMap;
	std::unique_ptr<AnimatedTextureManager>& animatedTextureManager;
	std::vector<bool>& objectInSceneGraph;
	std::unique_ptr<PortalSystem>& portalSystem;
	float& renderDistance;
	std::unique_ptr<FrustumCuller>& frustumCuller;
	std::unique_ptr<FrameBudgetGovernor>& governor;
	eqt::bridge::GameStateBridge*& bridge;
#ifdef EQT_HAS_GLES2
	// IrrlichtRenderer::gpuUploadThread_ is itself #ifdef EQT_HAS_GLES2, so
	// this member cannot be unconditional (K01 correction 1's shape).
	std::unique_ptr<GPUUploadThread>& gpuUploadThread;
#endif
	std::unique_ptr<Environment::TumbleweedManager>& tumbleweedManager;
	std::unique_ptr<class EntityPrepWorker>& entityPrepWorker;
	std::unique_ptr<SoftwareOcclusionCuller>& occlusionCuller;
	std::unordered_set<size_t>& indoorRegions;
	TreeWindController& treeWindController;
	uint8_t& currentHour;
	uint8_t& currentMinute;
	std::vector<irr::core::vector3df>& objectPositions;

	// ------------------------------------------------------------------
	// L12 (Batch L close-out): the SHARED-BUT-BELOW-THRESHOLD set, promoted.
	// Measured at 1c9afba7 across the ten extracted component directories:
	// every IrrlichtRenderer member reached through owner_ by THREE OR MORE
	// components. Same rule and same binding as the core above -- a reference
	// to the IrrlichtRenderer member of the same type, named without the
	// trailing underscore -- so a moved body's owner_.zoneMeshNode_ becomes
	// ctx_.zoneMeshNode and nothing else changes.
	// ------------------------------------------------------------------
	irr::scene::IMeshSceneNode*& zoneMeshNode;  // 58: diagnostics 33,zone_lifecycle 19,zone_load 6
	RegionWorldTriangles& regionWorldTriangles;  // 25: player 14,zone_load 9,zone_lifecycle 2
	std::vector<DeferredObject>& deferredObjects;  // 24: zone_load 13,zone_stream 8,frame 3
	// T475: objectTextureRebuildQueue, doorTextureRebuildQueue and
	// pendingTextureObjects (below) were deleted with the dead texture-arrival arm.
	irr::scene::IMeshSceneNode*& zonePlaceholderNode;  // 20: zone_lifecycle 17,diagnostics 2,frame 1
	float& playerHeading;  // 18: player 10,frame 4,sim 2,zone_load 2
	std::unordered_map<size_t, std::vector<size_t>>& regionNeighbors;  // 17: zone_load 10,zone_lifecycle 4,zone_stream 3
	std::vector<size_t>& objectRegions;  // 15: zone_load 7,zone_stream 4,player 2,sim 2
	bool& zoneReady;  // 14: frame 7,sim 4,zone_lifecycle 3
	VisionType& currentVision;  // 13: player 11,sim 1,zone_load 1
	std::unique_ptr<WeatherSystem>& weatherSystem;  // 12: zone_load 6,sim 4,overlays 2
	std::vector<VertexAnimatedMesh>& vertexAnimatedMeshes;  // 11: sim 9,zone_lifecycle 1,zone_load 1
	std::vector<bool>& objectLightInSceneGraph;  // 11: sim 6,zone_lifecycle 3,zone_load 1,zone_stream 1
	bool& fireGlowIcospheresEnabled;  // 10: zone_load 7,diagnostics 1,frame 1,overlays 1
	std::vector<irr::core::vector3df>& zoneLightPositions;  // 10: sim 6,zone_load 3,zone_lifecycle 1
	bool& fireGlowLightingEnabled;  // 9: zone_load 5,overlays 2,diagnostics 1,frame 1
	ObjectCollisionSet& objectCollision;  // B909b: placed objects, each distinct mesh held once. 9: zone_load 5,player 2,zone_lifecycle 2
	int& maxFireGlowLights;  // 7: zone_load 5,diagnostics 1,overlays 1
	bool& mergeZoneDrawsEnabled;  // 7: diagnostics 5,zone_lifecycle 1,zone_load 1
	std::vector<SortedRegionEntry>& sortedZoneDrawList;  // 7: sim 3,zone_lifecycle 3,frame 1
	std::unordered_set<size_t>& occlusionCulledRegions;  // 7: frame 5,sim 1,zone_lifecycle 1
	irr::scene::ISceneCollisionManager*& collisionManager;  // 7: overlays 3,zone_lifecycle 3,player 1
	float& zoneBoundsMinX;  // 7: zone_load 5,overlays 1,player 1
	float& zoneBoundsMinY;  // 7: zone_load 5,overlays 1,player 1
	float& zoneBoundsMaxX;  // 7: zone_load 5,overlays 1,player 1
	float& zoneBoundsMaxY;  // 7: zone_load 5,overlays 1,player 1
	uint32_t& frameNumber;  // 6: frame 4,sim 1,zone_load 1
	bool& zoneBoundsValid;  // 6: zone_load 3,overlays 1,player 1,zone_lifecycle 1
	bool& isIndoorZone;  // 6: overlays 2,zone_lifecycle 2,zone_load 2
	int& entityPrepScanCounter;  // 5: zone_stream 2,frame 1,zone_lifecycle 1,zone_load 1
	std::vector<irr::core::aabbox3df>& objectBoundingBoxes;  // 5: sim 2,zone_lifecycle 1,zone_load 1,zone_stream 1
	const std::unordered_map<size_t, uint8_t>*& regionPvsDepthMap;  // 5: zone_stream 3,sim 1,zone_lifecycle 1
	bool& portalBuildPending;  // 5: zone_load 2,zone_stream 2,zone_lifecycle 1
	bool& loadingScreenVisible;  // 4: zone_stream 2,frame 1,sim 1
	irr::core::matrix4& captured3DView;  // 4: frame 2,overlays 1,zone_lifecycle 1
	irr::core::matrix4& captured3DProj;  // 4: frame 2,overlays 1,zone_lifecycle 1
	const std::vector<Environment::UnifiedParticle>*& particleRenderBuffer;  // 4: sim 2,frame 1,zone_lifecycle 1
	float& fogThickness;  // 4: overlays 2,zone_lifecycle 1,zone_load 1
	std::chrono::steady_clock::time_point& progressiveLoadStartTime;  // 3: player 1,zone_load 1,zone_stream 1
	bool& sequentialLoadComplete;
	// The EQE1 archive set -- eqe1/ is the eleventh component directory and was
	// missed by this unit's first census (L12 correction 49); these four cross
	// the >= 3 threshold once it is counted. The two eqe1 MUTEXES deliberately
	// did NOT come with them: relocating the access path of a synchronisation
	// primitive is exactly what Batch L's second STOP condition forbids in a
	// structural unit, so they stay as owner_.eqe1Mutex_ / owner_.eqe1CandidatesMutex_.
	std::unordered_map<std::string, std::unique_ptr<EQE1Loader>>& eqe1Loaders;  // 13: eqe1 8,zone_lifecycle 4,diagnostics 1
	std::vector<std::string>& pendingGlobalEQE1Archives;  // 12: eqe1 9,zone_load 2,zone_lifecycle 1
	size_t& eqe1LoadTimeCandidateCount;  // 4: zone_load 2,zone_lifecycle 1,eqe1 1
	std::set<uint16_t>& eqe1CandidateRaces;  // 3: zone_lifecycle 1,zone_load 1,eqe1 1  // 3: zone_lifecycle 1,zone_load 1,zone_stream 1
#ifdef EQT_HAS_DRM
	uint32_t& icosphereProgram;  // 3: diagnostics 1,frame 1,zone_load 1
#endif

	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): B050 cluster 3. All four
	// clear L12's >= 3 threshold and were excluded ONLY because their type
	// was nested in IrrlichtRenderer, which would have forced this header to
	// include irrlicht_renderer.h. L13 hoisted the four types to
	// renderer_types.h / render_pass_timer.h, so the members promote on the
	// unchanged L12 rule.
	// ------------------------------------------------------------------
	std::vector<FireGlowLight>& fireGlowLights;  // 26: eqe1 16,zone_load 8,diagnostics 1,frame 1
	std::vector<RendererZoneLightData>& zoneLightData;  // 22: sim 12,zone_load 5,diagnostics 4,zone_lifecycle 1
	RenderPassTimer*& renderPassTimer;  // 14: frame 9,zone_load 3,diagnostics 2
	std::map<uint8_t, DoorCollisionData>& doorCollisionData;  // 7: player 4,zone_lifecycle 2,zone_load 1

	// ------------------------------------------------------------------
	// L12: the SIBLING-COMPONENT pointers (K13's third target group).
	// A component used to reach a sibling as owner_.<Family>(); it now reaches
	// it as ctx_.<Family>(). The pointers are RAW and LATE-BOUND: ctx_ is
	// brace-initialised in IrrlichtRenderer's ctor initialiser list, before any
	// component exists, so a reference member could not be bound. They are
	// public because RendererContext must stay an AGGREGATE (K13 deviation 9:
	// an aggregate may not have private data members) -- only SetComponents()
	// writes them and only the accessors below read them.
	// IrrlichtRenderer's ctor calls SetComponents() immediately after the last
	// make_unique; nothing above that line may reach a sibling.
	// ------------------------------------------------------------------
	FrameComponent* frameComponent = nullptr;
	DiagnosticsComponent* diagnosticsComponent = nullptr;
	OverlaysComponent* overlaysComponent = nullptr;
	PlayerCameraComponent* playerCameraComponent = nullptr;
	SlashCommandsComponent* slashComponent = nullptr;
	SimBridgeComponent* simBridgeComponent = nullptr;
	ZoneStreamComponent* zoneStreamComponent = nullptr;
	ZoneLifecycleComponent* zoneLifecycleComponent = nullptr;
	ZoneLoadComponent* zoneLoadComponent = nullptr;
	Eqe1ArchiveComponent* eqe1Component = nullptr;
	RdpComponent* rdpComponent = nullptr;

	void SetComponents(FrameComponent* frame, DiagnosticsComponent* diagnostics, OverlaysComponent* overlays, PlayerCameraComponent* playerCamera, SlashCommandsComponent* slash, SimBridgeComponent* simBridge, ZoneStreamComponent* zoneStream, ZoneLifecycleComponent* zoneLifecycle, ZoneLoadComponent* zoneLoad, Eqe1ArchiveComponent* eqe1, RdpComponent* rdp) {
		frameComponent = frame;
		diagnosticsComponent = diagnostics;
		overlaysComponent = overlays;
		playerCameraComponent = playerCamera;
		slashComponent = slash;
		simBridgeComponent = simBridge;
		zoneStreamComponent = zoneStream;
		zoneLifecycleComponent = zoneLifecycle;
		zoneLoadComponent = zoneLoad;
		eqe1Component = eqe1;
		rdpComponent = rdp;
	}

	FrameComponent& Frame() { return *frameComponent; }
	DiagnosticsComponent& Diagnostics() { return *diagnosticsComponent; }
	OverlaysComponent& Overlays() { return *overlaysComponent; }
	PlayerCameraComponent& PlayerCamera() { return *playerCameraComponent; }
	SlashCommandsComponent& Slash() { return *slashComponent; }
	SimBridgeComponent& SimBridge() { return *simBridgeComponent; }
	ZoneStreamComponent& ZoneStream() { return *zoneStreamComponent; }
	ZoneLifecycleComponent& ZoneLifecycle() { return *zoneLifecycleComponent; }
	ZoneLoadComponent& ZoneLoad() { return *zoneLoadComponent; }
	Eqe1ArchiveComponent& Eqe1() { return *eqe1Component; }
	RdpComponent& Rdp() { return *rdpComponent; }
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_CONTEXT_H
