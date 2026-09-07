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
	// L13: the ctor and dtor are OUT OF LINE (batch corrections 35 and 45).
	// characterModelView_ moved here as a std::unique_ptr to a forward-declared
	// type, and both the destructor and the ctor's unwind path instantiate the
	// deleter -- so the TU holding them must see the complete type.
	OverlaysComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~OverlaysComponent();

	// --- Forwarded from IrrlichtRenderer (the K11 rule: Application and
	// IrrlichtBridge must not learn a component exists) -------------------
	// T448 moved these two paragraphs here out of irrlicht_renderer.h, where
	// they annotated the forwarders this family left behind.
	//
	// L03 (Batch L): setCurrentTarget / setCurrentTargetInfo /
	// clearCurrentTarget stay as one-line forwarders on IrrlichtRenderer
	// because irrlicht_bridge.cpp calls them and must not learn a component
	// exists (the K11 rule) -- setCurrentTarget/clearCurrentTarget are also the
	// IOutputRenderer-shaped public surface ConsoleRenderer/NullRenderer
	// implement. getCurrentTargetInfo() had zero callers tree-wide and moved
	// with currentTargetInfo_, so it was deleted (Mechanism 5).
	//
	// setZoneLineBoundingBoxes() likewise stays a forwarder there because
	// application_zone_loading.cpp calls it. The renderer's own callers of
	// clearZoneLineBoundingBoxes()/toggleZoneLineVisualization() were
	// retargeted to overlays_->. setZoneLineDebug(), isInZoneLine() and
	// isZoneLineVisualizationEnabled() had zero callers tree-wide
	// (Mechanism 5); T354 then deleted the debug border those wrote for (B042).
	void setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent = 100, uint8_t level = 0);
	void setCurrentTargetInfo(const TargetInfo& info);
	void clearCurrentTarget();
	void setNavmesh(PathfinderNavmesh* navmesh);
	const PathfinderNavmesh* navmesh() const { return navmesh_; }
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
	void drawZoneLineBoxLabels();
	void drawFPSCounter();
	void updatePaperdollForFrame();


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.Overlays().<accessor>();
	// the renderer's own init/teardown reaches them through Overlays().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	std::unique_ptr<eqt::ui::CharacterModelView> const& characterModelView() const { return characterModelView_; }
	uint16_t& currentTargetId() { return currentTargetId_; }
	uint16_t const& currentTargetId() const { return currentTargetId_; }
	std::string const& currentTargetName() const { return currentTargetName_; }
	irr::gui::IGUIEnvironment*& guienv() { return guienv_; }
	irr::gui::IGUIEnvironment* const& guienv() const { return guienv_; }
	irr::gui::IGUIStaticText*& hotkeysText() { return hotkeysText_; }
	irr::gui::IGUIStaticText* const& hotkeysText() const { return hotkeysText_; }
	irr::gui::IGUIStaticText*& hudText() { return hudText_; }
	irr::gui::IGUIStaticText* const& hudText() const { return hudText_; }
	bool& mapOverlayMirrorX() { return mapOverlayMirrorX_; }
	bool const& mapOverlayMirrorX() const { return mapOverlayMirrorX_; }
	int& mapOverlayRotation() { return mapOverlayRotation_; }
	int const& mapOverlayRotation() const { return mapOverlayRotation_; }
	bool& navmeshOverlayMirrorX() { return navmeshOverlayMirrorX_; }
	bool const& navmeshOverlayMirrorX() const { return navmeshOverlayMirrorX_; }
	int& navmeshOverlayRotation() { return navmeshOverlayRotation_; }
	int const& navmeshOverlayRotation() const { return navmeshOverlayRotation_; }
	bool& showMapOverlay() { return showMapOverlay_; }
	bool const& showMapOverlay() const { return showMapOverlay_; }
	bool& showNavmeshOverlay() { return showNavmeshOverlay_; }
	bool const& showNavmeshOverlay() const { return showNavmeshOverlay_; }

	// T333 test seam. drawNavmeshOverlay()'s only production filler is
	// updateNavmeshOverlay(), which needs a live Detour-backed
	// PathfinderNavmesh; the pixel test (tests/test_navmesh_overlay_draw.cpp)
	// has a real GLES2 device but no navmesh, so it seeds the triangle list
	// directly. Production code must keep using updateNavmeshOverlay().
	void seedNavmeshOverlayTriangleForTest(const irr::core::vector3df& v1,
	                                       const irr::core::vector3df& v2,
	                                       const irr::core::vector3df& v3,
	                                       irr::video::SColor color);
	size_t navmeshOverlayTriangleCount() const { return navmeshOverlayTriangles_.size(); }

	// T335 test seam, the same shape and for the same reason:
	// drawMapOverlay()'s only production filler is updateMapOverlay(), which
	// needs a loaded zone's collision mesh. tests/test_map_overlay_draw.cpp
	// has a real GLES2 device but no zone.
	void seedMapOverlayTriangleForTest(const irr::core::vector3df& v1,
	                                   const irr::core::vector3df& v2,
	                                   const irr::core::vector3df& v3,
	                                   irr::video::SColor color,
	                                   bool isPlaceable);
	size_t mapOverlayTriangleCount() const { return mapOverlayTriangles_.size(); }

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
	// GONE. L03 deleted setZoneLineDebug(), the sole writer of inZoneLine_,
	// zoneLineTargetZoneId_ and zoneLineDebugText_, under Mechanism 5; the
	// three members and their reader drawZoneLineOverlay() were kept so the
	// decision could be taken with evidence (B042). T354 measured zero
	// callers again and deleted all four: the debug border could never turn
	// on, and it is a developer aid with no official-client oracle. The zone
	// line BOUNDING BOX visualization below is live and unrelated.

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

	// L13: owned state, see the accessors above.
	std::unique_ptr<eqt::ui::CharacterModelView> characterModelView_;
	uint16_t currentTargetId_ = 0;
	std::string currentTargetName_;
	irr::gui::IGUIEnvironment* guienv_ = nullptr;
	irr::gui::IGUIStaticText* hotkeysText_ = nullptr;  // Hotkey hints in upper right
	irr::gui::IGUIStaticText* hudText_ = nullptr;
	bool mapOverlayMirrorX_ = false;  // Mirror placeables across YZ plane (negate X)
	int mapOverlayRotation_ = 0;  // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
	bool navmeshOverlayMirrorX_ = false;  // Mirror across YZ plane (negate X)
	int navmeshOverlayRotation_ = 0;  // Rotation index: 0=0°, 1=90°, 2=180°, 3=270° around Y axis
	bool showMapOverlay_ = false;
	bool showNavmeshOverlay_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_OVERLAYS_OVERLAYS_COMPONENT_H
