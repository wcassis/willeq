#ifndef EQT_GRAPHICS_OVERLAYS_OVERLAYS_COMPONENT_H
#define EQT_GRAPHICS_OVERLAYS_OVERLAYS_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L03 -- the
// overlays family, lifted out of class IrrlichtRenderer following the L01
// RdpComponent shape.
//
// The family is the HUD, the map / navmesh / zone-line debug overlays, mouse
// targeting and the target highlight, the loading screen, fog, the
// environmental display settings and the inventory paperdoll. Its two TUs are
// src/client/graphics/overlays/overlays_component.cpp (was renderer_overlays)
// and src/client/graphics/overlays/overlays_hud_env.cpp (was
// renderer_hud_env); every body in them is the body that used to live on
// IrrlichtRenderer, moved verbatim.
//
// The four nested types below (HudCachedState, ZoneLineBoxNode,
// MapOverlayTriangle, NavmeshOverlayTriangle) were nested types of
// IrrlichtRenderer and are single-family, so they moved WITH the members they
// type rather than being qualified (K05 correction j).
//
// This component keeps the IrrlichtRenderer& transition handle and needs it:
// thirty of the members its bodies touch are shared with another family but
// are outside the RendererContext core, so they stay on IrrlichtRenderer and
// are reached as owner_.X_ (Batch K Mechanism 3), and six IrrlichtRenderer
// methods that stayed behind are called as owner_.Foo(). IrrlichtRenderer
// therefore declares this class a friend. It names no IrrlichtRenderer
// static, so only the first axis of K13 correction (ii) is live.

#include "client/graphics/renderer_context.h"
#include "client/graphics/entity_renderer.h"   // EQT::Graphics::EntityAppearance (by value below)

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

class PathfinderNavmesh;
namespace EQT { struct ZoneLineBoundingBox; }

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class OverlaysComponent {
public:
	OverlaysComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// --- Forwarded from IrrlichtRenderer (the K11 rule: Application and
	// IrrlichtBridge must not learn a component exists) -------------------
	void setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent = 100, uint8_t level = 0);
	void setCurrentTargetInfo(const TargetInfo& info);
	void clearCurrentTarget();
	void setNavmesh(PathfinderNavmesh* navmesh);
	void setLoadingTitle(const std::wstring& title) { loadingTitle_ = title; }
	void setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>& boxes);
	void markPaperdollDirty();

	// --- Called from the renderer's own TUs through overlays_->X() --------
	void clearZoneLineBoundingBoxes();
	void toggleZoneLineVisualization();
	void setupFog();
	void setupHUD();
	void updateHUD();
	void applyEnvironmentalDisplaySettings();
	void drawLoadingScreen(float progress, const std::wstring& stageText);
	void handleMouseTargeting(int clickX, int clickY);
	void toggleMapOverlay();
	void updateMapOverlay(const glm::vec3& playerPos);
	void drawMapOverlay();
	void toggleNavmeshOverlay();
	void updateNavmeshOverlay(const glm::vec3& playerPos);
	void drawNavmeshOverlay();
#ifdef EQT_HAS_GLES2
	void drawTargetOutline();       // Stencil-based silhouette outline (GLES2)
#else
	void drawTargetSelectionBox();  // Wireframe bounding box (desktop GL)
#endif
	void drawZoneLineOverlay();
	void drawZoneLineBoxLabels();
	void drawFPSCounter();
	void updatePaperdollForFrame();

private:
	// In-family only (K03 correction a: every one of these has a caller
	// inside the two TUs above).
	irr::video::SColor getMapOverlayColor(float z, float minZ, float maxZ, const glm::vec3& normal) const;
	irr::video::SColor getNavmeshAreaColor(uint8_t areaType) const;
	uint16_t getEntityAtScreenPos(int screenX, int screenY);
	bool checkEntityLOS(const irr::core::vector3df& cameraPos, const irr::core::vector3df& entityPos);
	void ensurePaperdollInitialized();

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// --- Loading screen ---------------------------------------------------
	std::wstring loadingTitle_ = L"EverQuest";  // Loading screen title

	// --- HUD --------------------------------------------------------------
	irr::gui::IGUIStaticText* headingDebugText_ = nullptr;  // Heading debug info (right side)

	// Cached state values to detect changes
	struct HudCachedState {
		int fps = 0;
		int playerX = 0, playerY = 0, playerZ = 0;
		size_t entityCount = 0;
		size_t modeledEntityCount = 0;
		uint16_t targetId = 0;
		uint8_t targetHpPercent = 0;
		bool wireframeMode = false;
		bool oldModels = true;
		std::string cameraMode;
		std::string zoneName;
	};
	HudCachedState hudCachedState_;

	// --- Target selection -------------------------------------------------
	// currentTargetId_ / currentTargetName_ stay on IrrlichtRenderer: the
	// frame family reads them too (Batch K Mechanism 3).
	uint8_t currentTargetHpPercent_ = 100;
	uint8_t currentTargetLevel_ = 0;
	TargetInfo currentTargetInfo_;  // Extended target info for HUD display

	// --- Zone line debugging ----------------------------------------------
	// L03: setZoneLineDebug() was the sole writer of these three and had zero
	// callers tree-wide, so it went under Mechanism 5. They are kept because
	// drawZoneLineOverlay() (live, called every frame) reads them; the
	// overlay simply never turns on. See the L03 Review and the BOARD entry.
	bool inZoneLine_ = false;
	uint16_t zoneLineTargetZoneId_ = 0;
	std::string zoneLineDebugText_;

	// --- Zone line bounding box visualization -----------------------------
	struct ZoneLineBoxNode {
		irr::scene::IMeshSceneNode* node = nullptr;
		uint16_t targetZoneId = 0;
		bool isProximityBased = false;
		// Store original EQ coords for recreating scene nodes
		float minX = 0, minY = 0, minZ = 0;
		float maxX = 0, maxY = 0, maxZ = 0;
	};
	std::vector<ZoneLineBoxNode> zoneLineBoxNodes_;
	bool showZoneLineBoxes_ = false;  // Toggle with Ctrl+Z or /zone
	bool zoneLineBoxesCreated_ = false;  // Whether scene nodes exist
	void createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox& box);
	void createZoneLineBoxMeshForNode(const EQT::ZoneLineBoundingBox& box, ZoneLineBoxNode& boxNode);
	void removeZoneLineBoxSceneNodes();
	void recreateZoneLineBoxSceneNodes();

	// --- Map overlay visualization (Ctrl+M) -------------------------------
	// showMapOverlay_ / mapOverlayRotation_ / mapOverlayMirrorX_ stay on
	// IrrlichtRenderer: renderer_frame_input.cpp and renderer_frame_render.cpp
	// touch them (Batch K Mechanism 3).
	glm::vec3 lastMapOverlayUpdatePos_ = glm::vec3(0.0f);
	float mapOverlayRadius_ = 100.0f;  // Radius around player to show
	struct MapOverlayTriangle {
		irr::core::vector3df v1, v2, v3;
		irr::video::SColor color;  // Based on height and normal
		bool isPlaceable;          // True if from placeable object (rotation applies)
	};
	std::vector<MapOverlayTriangle> mapOverlayTriangles_;

	// --- Navmesh overlay visualization (Ctrl+N) ---------------------------
	// showNavmeshOverlay_ / navmeshOverlayRotation_ / navmeshOverlayMirrorX_
	// stay on IrrlichtRenderer for the same reason.
	PathfinderNavmesh* navmesh_ = nullptr;
	glm::vec3 lastNavmeshOverlayUpdatePos_ = glm::vec3(0.0f);
	float navmeshOverlayRadius_ = 100.0f;  // Radius around player to show
	struct NavmeshOverlayTriangle {
		irr::core::vector3df v1, v2, v3;
		irr::video::SColor color;  // Based on area type
	};
	std::vector<NavmeshOverlayTriangle> navmeshOverlayTriangles_;

	// --- T60: inventory paperdoll -----------------------------------------
	// characterModelView_ itself stays on IrrlichtRenderer (renderer_entities
	// and the frame family reach it); only the bookkeeping moves.
	bool paperdollInitAttempted_ = false;  // Avoid retrying init() every frame if it failed once
	uint16_t paperdollLastRaceId_ = 0;
	uint8_t paperdollLastGender_ = 0;
	EQT::Graphics::EntityAppearance paperdollLastAppearance_{};
	bool paperdollHasAppearance_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_OVERLAYS_OVERLAYS_COMPONENT_H
