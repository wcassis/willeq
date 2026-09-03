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

#include <cstdint>
#include <map>
#include <memory>
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
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_CONTEXT_H
