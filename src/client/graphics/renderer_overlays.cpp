#include "client/graphics/irrlicht_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/zone_lines.h"
#include "common/logging.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include <algorithm>
#include <cstdint>
#include <limits>

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::toggleMapOverlay() {
    showMapOverlay_ = !showMapOverlay_;
    LOG_INFO(MOD_GRAPHICS, "Map overlay: {}", showMapOverlay_ ? "ON" : "OFF");

    if (!showMapOverlay_) {
        mapOverlayTriangles_.clear();
    } else {
        // Force update on next frame
        lastMapOverlayUpdatePos_ = glm::vec3(std::numeric_limits<float>::max());
    }
}

void IrrlichtRenderer::updateMapOverlay(const glm::vec3& playerPos) {
    if (!showMapOverlay_ || !collisionMap_) {
        return;
    }

    // Only update if player moved more than 10 units horizontally (X and Z in Y-up coords)
    float dx = playerPos.x - lastMapOverlayUpdatePos_.x;
    float dz = playerPos.z - lastMapOverlayUpdatePos_.z;
    float distSq = dx * dx + dz * dz;
    if (distSq < 100.0f) {  // 10^2
        return;
    }

    lastMapOverlayUpdatePos_ = playerPos;
    mapOverlayTriangles_.clear();

    // Get triangles within radius from HCMap
    auto triangles = collisionMap_->GetTrianglesInRadius(playerPos, mapOverlayRadius_);

    if (triangles.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Map overlay: No triangles found within radius {} at ({}, {}, {})",
                  mapOverlayRadius_, playerPos.x, playerPos.y, playerPos.z);
        return;
    }

    // Get Z range for color gradient
    float minZ, maxZ;
    collisionMap_->GetZRange(minZ, maxZ);

    // Triangles are already in Irrlicht Y-up format from HCMap
    for (const auto& tri : triangles) {
        MapOverlayTriangle overlayTri;

        // Vertices already in Irrlicht format (Y-up), use directly
        overlayTri.v1 = irr::core::vector3df(tri.v1.x, tri.v1.y, tri.v1.z);
        overlayTri.v2 = irr::core::vector3df(tri.v2.x, tri.v2.y, tri.v2.z);
        overlayTri.v3 = irr::core::vector3df(tri.v3.x, tri.v3.y, tri.v3.z);

        // Calculate average Y for color (Y is vertical in Irrlicht coords)
        float avgY = (tri.v1.y + tri.v2.y + tri.v3.y) / 3.0f;
        overlayTri.color = getMapOverlayColor(avgY, minZ, maxZ, tri.normal);

        // Copy placeable flag for rotation
        overlayTri.isPlaceable = tri.isPlaceable;

        mapOverlayTriangles_.push_back(overlayTri);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Map overlay: Updated with {} triangles", mapOverlayTriangles_.size());
}

irr::video::SColor IrrlichtRenderer::getMapOverlayColor(float y, float minY, float maxY, const glm::vec3& normal) const {
    // Normalize Y (height) to 0-1 range
    float range = maxY - minY;
    float t = (range > 0.001f) ? (y - minY) / range : 0.5f;
    t = std::max(0.0f, std::min(1.0f, t));  // Clamp to [0,1]

    // Determine if it's a floor (normal pointing up in Y-up coords, Y > 0.7)
    // or a wall (normal mostly horizontal)
    bool isFloor = std::abs(normal.y) > 0.7f;

    irr::u8 r, g, b;

    if (isFloor) {
        // Floor: Blue (low) -> Green (mid) -> Red (high)
        if (t < 0.5f) {
            // Blue to Green
            float localT = t * 2.0f;
            r = 0;
            g = static_cast<irr::u8>(localT * 255);
            b = static_cast<irr::u8>((1.0f - localT) * 255);
        } else {
            // Green to Red
            float localT = (t - 0.5f) * 2.0f;
            r = static_cast<irr::u8>(localT * 255);
            g = static_cast<irr::u8>((1.0f - localT) * 255);
            b = 0;
        }
    } else {
        // Wall: Use purple/magenta tones to distinguish from floors
        // Darker purple (low) to brighter magenta (high)
        r = static_cast<irr::u8>(128 + t * 127);  // 128-255
        g = static_cast<irr::u8>(t * 100);        // 0-100
        b = static_cast<irr::u8>(180 + t * 75);   // 180-255
    }

    return irr::video::SColor(200, r, g, b);  // Slightly transparent
}

void IrrlichtRenderer::drawMapOverlay() {
    if (!showMapOverlay_ || !driver_ || mapOverlayTriangles_.empty()) {
        return;
    }

    // Set up material for wireframe line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;  // Respect depth
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);

    // Build transform for placeables (rotation and/or mirroring)
    // These transforms are around the world origin to test coordinate alignment
    irr::core::matrix4 placeableTransform;
    placeableTransform.makeIdentity();

    // Apply X-axis mirror if enabled (scale X by -1)
    if (mapOverlayMirrorX_) {
        irr::core::matrix4 mirrorMatrix;
        mirrorMatrix.makeIdentity();
        mirrorMatrix.setScale(irr::core::vector3df(-1.0f, 1.0f, 1.0f));
        placeableTransform *= mirrorMatrix;
    }

    // Apply rotation if set
    if (mapOverlayRotation_ != 0) {
        irr::core::matrix4 rotMatrix;
        rotMatrix.setRotationDegrees(irr::core::vector3df(0, mapOverlayRotation_ * 90.0f, 0));
        placeableTransform *= rotMatrix;
    }

    // Draw terrain triangles first (no transform)
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    for (const auto& tri : mapOverlayTriangles_) {
        if (!tri.isPlaceable) {
            driver_->draw3DLine(tri.v1, tri.v2, tri.color);
            driver_->draw3DLine(tri.v2, tri.v3, tri.color);
            driver_->draw3DLine(tri.v3, tri.v1, tri.color);
        }
    }

    // Draw placeable triangles (with transform if set)
    driver_->setTransform(irr::video::ETS_WORLD, placeableTransform);
    for (const auto& tri : mapOverlayTriangles_) {
        if (tri.isPlaceable) {
            driver_->draw3DLine(tri.v1, tri.v2, tri.color);
            driver_->draw3DLine(tri.v2, tri.v3, tri.color);
            driver_->draw3DLine(tri.v3, tri.v1, tri.color);
        }
    }
}

void IrrlichtRenderer::toggleNavmeshOverlay() {
    showNavmeshOverlay_ = !showNavmeshOverlay_;
    LOG_INFO(MOD_GRAPHICS, "Navmesh overlay: {}", showNavmeshOverlay_ ? "ON" : "OFF");

    if (!showNavmeshOverlay_) {
        navmeshOverlayTriangles_.clear();
    } else {
        // Force update on next frame
        lastNavmeshOverlayUpdatePos_ = glm::vec3(std::numeric_limits<float>::max());
    }
}

void IrrlichtRenderer::updateNavmeshOverlay(const glm::vec3& playerPos) {
    if (!showNavmeshOverlay_ || !navmesh_) {
        return;
    }

    // Only update if player moved more than 10 units horizontally (X and Z in Y-up coords)
    float dx = playerPos.x - lastNavmeshOverlayUpdatePos_.x;
    float dz = playerPos.z - lastNavmeshOverlayUpdatePos_.z;
    float distSq = dx * dx + dz * dz;
    if (distSq < 100.0f) {  // 10^2
        return;
    }

    lastNavmeshOverlayUpdatePos_ = playerPos;
    navmeshOverlayTriangles_.clear();

#ifdef EQT_HAS_NAVMESH
    // Get triangles within radius from navmesh (already in Irrlicht Y-up coords)
    auto triangles = navmesh_->GetTrianglesInRadius(playerPos, navmeshOverlayRadius_);

    if (triangles.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Navmesh overlay: No triangles found within radius {} at ({}, {}, {})",
                  navmeshOverlayRadius_, playerPos.x, playerPos.y, playerPos.z);
        return;
    }

    // Triangles are already in Irrlicht Y-up format, use directly
    for (const auto& tri : triangles) {
        NavmeshOverlayTriangle overlayTri;

        // Vertices already in Irrlicht format (Y-up), use directly
        overlayTri.v1 = irr::core::vector3df(tri.v1.x, tri.v1.y, tri.v1.z);
        overlayTri.v2 = irr::core::vector3df(tri.v2.x, tri.v2.y, tri.v2.z);
        overlayTri.v3 = irr::core::vector3df(tri.v3.x, tri.v3.y, tri.v3.z);

        // Color based on area type
        overlayTri.color = getNavmeshAreaColor(tri.areaType);

        navmeshOverlayTriangles_.push_back(overlayTri);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Navmesh overlay: Updated with {} triangles", navmeshOverlayTriangles_.size());
#endif
}

irr::video::SColor IrrlichtRenderer::getNavmeshAreaColor(uint8_t areaType) const {
    // Area types from PathfinderNavmesh (see pathfinder_interface.h):
    // 0 = Normal (walkable)
    // 1 = Water
    // 2 = Lava
    // 4 = PvP
    // 5 = Slime
    // 6 = Ice
    // 7 = V Water (Frigid Water)
    // 8 = General Area
    // 9 = Portal
    // 10 = Prefer

    switch (areaType) {
        case 0:  // Normal - Green
            return irr::video::SColor(180, 0, 200, 0);
        case 1:  // Water - Blue
            return irr::video::SColor(180, 0, 100, 255);
        case 2:  // Lava - Red/Orange
            return irr::video::SColor(180, 255, 80, 0);
        case 4:  // PvP - Purple
            return irr::video::SColor(180, 180, 0, 200);
        case 5:  // Slime - Yellow-Green
            return irr::video::SColor(180, 180, 200, 0);
        case 6:  // Ice - Cyan
            return irr::video::SColor(180, 0, 220, 220);
        case 7:  // V Water (Frigid) - Dark Blue
            return irr::video::SColor(180, 0, 50, 180);
        case 8:  // General Area - Gray
            return irr::video::SColor(180, 150, 150, 150);
        case 9:  // Portal - Magenta
            return irr::video::SColor(180, 255, 0, 255);
        case 10: // Prefer - Yellow
            return irr::video::SColor(180, 255, 255, 0);
        default: // Unknown - White
            return irr::video::SColor(180, 255, 255, 255);
    }
}

void IrrlichtRenderer::drawNavmeshOverlay() {
    if (!showNavmeshOverlay_ || !driver_ || navmeshOverlayTriangles_.empty()) {
        return;
    }

    // Set up material for wireframe line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;  // Respect depth
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);

    // Build transform matrix for rotation and/or mirroring
    // These transforms are around the world origin to test coordinate alignment
    irr::core::matrix4 navmeshTransform;
    navmeshTransform.makeIdentity();

    // Apply X-axis mirror if enabled (scale X by -1)
    if (navmeshOverlayMirrorX_) {
        irr::core::matrix4 mirrorMatrix;
        mirrorMatrix.makeIdentity();
        mirrorMatrix.setScale(irr::core::vector3df(-1.0f, 1.0f, 1.0f));
        navmeshTransform *= mirrorMatrix;
    }

    // Apply rotation if set
    if (navmeshOverlayRotation_ != 0) {
        irr::core::matrix4 rotMatrix;
        rotMatrix.setRotationDegrees(irr::core::vector3df(0, navmeshOverlayRotation_ * 90.0f, 0));
        navmeshTransform *= rotMatrix;
    }

    // Set transform and draw all triangles as wireframe
    driver_->setTransform(irr::video::ETS_WORLD, navmeshTransform);

    for (const auto& tri : navmeshOverlayTriangles_) {
        driver_->draw3DLine(tri.v1, tri.v2, tri.color);
        driver_->draw3DLine(tri.v2, tri.v3, tri.color);
        driver_->draw3DLine(tri.v3, tri.v1, tri.color);
    }
}

#ifdef EQT_HAS_GLES2
void IrrlichtRenderer::drawTargetOutline() {
    if (currentTargetId_ == 0 || !entityRenderer_ || !driver_) return;
    if (!have3DTransforms_) return;  // Need captured 3D matrices

    const auto& entities = entityRenderer_->getEntities();
    auto it = entities.find(currentTargetId_);
    if (it == entities.end()) return;

    const EntityVisual& visual = it->second;
    if (!visual.animatedNode || !visual.meshBuilt) return;

    irr::scene::ISceneNode* node = visual.sceneNode;
    if (!node || !node->isVisible()) return;

    irr::scene::SMesh* mesh = visual.animatedNode->getInstanceMesh();
    if (!mesh || mesh->getMeshBufferCount() == 0) return;

    // Restore 3D camera matrices — after drawAll(), ETS_VIEW and ETS_PROJECTION
    // may have been overwritten by 2D rendering (billboards, GUI nodes, overlays).
    // The correct 3D matrices were captured during the ESNRP_SOLID render pass.
    driver_->setTransform(irr::video::ETS_VIEW, captured3DView_);
    driver_->setTransform(irr::video::ETS_PROJECTION, captured3DProj_);

    // Stencil reference value above portal range (portals use 1-4)
    const GLint STENCIL_REF = 128;

    // Compute normal transform (mirrors EQAnimatedMeshSceneNode::render() logic)
    irr::core::matrix4 normalTransform;
    if (visual.animatedNode->isPlayerNode()) {
        normalTransform.setTranslation(node->getAbsoluteTransformation().getTranslation());
    } else {
        normalTransform = node->getAbsoluteTransformation();
    }

    // Scaled transform for outline pass (scale from entity center)
    irr::core::vector3df pos = normalTransform.getTranslation();
    const float scaleFactor = 1.10f;
    irr::core::matrix4 toOrigin, fromOrigin, scaleMat;
    toOrigin.setTranslation(-pos);
    fromOrigin.setTranslation(pos);
    scaleMat.setScale(irr::core::vector3df(scaleFactor, scaleFactor, scaleFactor));
    irr::core::matrix4 outlineTransform = fromOrigin * scaleMat * toOrigin * normalTransform;

    // Material: no texture → driver selects Color2D shader (renders vertex colors).
    // EMT_SOLID forces depth write ON in the driver, so we override with raw GL
    // after setMaterial() calls.
    irr::video::SMaterial outlineMat;
    outlineMat.Lighting = false;
    outlineMat.ZBuffer = irr::video::ECFN_LESSEQUAL;
    outlineMat.BackfaceCulling = false;
    outlineMat.MaterialType = irr::video::EMT_SOLID;

    // --- Pass 1: Write stencil at normal scale (no color output) ---
    // Depth test ON (only mark visible pixels), depth write OFF, color OFF.
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, STENCIL_REF, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    driver_->setTransform(irr::video::ETS_WORLD, normalTransform);
    driver_->setMaterial(outlineMat);
    // Override depth write after setMaterial (EMT_SOLID forces it ON)
    glDepthMask(GL_FALSE);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (mb) driver_->drawMeshBuffer(mb);
    }

    // --- Pass 2: Draw outline at scaled-up size (only where stencil != ref) ---
    // Depth test ON with LESSEQUAL so outline is occluded by closer geometry (pet, etc.)
    // but draws over geometry behind/at the target.
    glStencilFunc(GL_NOTEQUAL, STENCIL_REF, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Rotating outline color: cycle hue over time (full rotation every 3 seconds)
    static float outlineHue = 0.0f;
    outlineHue += 360.0f / 180.0f;  // ~2 deg/frame at 60fps → full cycle in 3s
    if (outlineHue >= 360.0f) outlineHue -= 360.0f;
    // HSV to RGB (S=0.8, V=1.0 for vivid bright colors)
    float h = outlineHue / 60.0f;
    int hi = static_cast<int>(h) % 6;
    float f = h - static_cast<int>(h);
    float v = 1.0f, s = 0.8f;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float r, g, b;
    switch (hi) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    const irr::video::SColor outlineColor(255,
        static_cast<irr::u32>(r * 255.0f),
        static_cast<irr::u32>(g * 255.0f),
        static_cast<irr::u32>(b * 255.0f));
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexType() != irr::video::EVT_STANDARD) continue;
        auto* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
        for (irr::u32 v = 0; v < mb->getVertexCount(); ++v)
            verts[v].Color = outlineColor;
    }

    driver_->setTransform(irr::video::ETS_WORLD, outlineTransform);
    driver_->setMaterial(outlineMat);
    // Override depth write after setMaterial (don't contaminate depth buffer)
    glDepthMask(GL_FALSE);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (mb) driver_->drawMeshBuffer(mb);
    }

    // Restore white vertex colors (entity's normal rendering expects white)
    const irr::video::SColor white(255, 255, 255, 255);
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb || mb->getVertexType() != irr::video::EVT_STANDARD) continue;
        auto* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
        for (irr::u32 v = 0; v < mb->getVertexCount(); ++v)
            verts[v].Color = white;
    }

    // --- Cleanup: restore to defaults matching SOGLES2State::reset() ---
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
#else
void IrrlichtRenderer::drawTargetSelectionBox() {
    // Check if we have a target
    if (currentTargetId_ == 0 || !entityRenderer_ || !driver_) {
        return;
    }

    // Find the targeted entity
    const auto& entities = entityRenderer_->getEntities();
    auto it = entities.find(currentTargetId_);
    if (it == entities.end()) {
        return;
    }

    const EntityVisual& visual = it->second;
    irr::scene::ISceneNode* node = visual.sceneNode;
    if (!node || !node->isVisible()) {
        return;
    }

    // Get bounding box in world space
    irr::core::aabbox3df bbox = node->getTransformedBoundingBox();

    // Set up material for line drawing
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 2.0f;
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_LESSEQUAL;
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // White color for selection box
    irr::video::SColor white(255, 255, 255, 255);

    // Get the 8 corners of the bounding box
    irr::core::vector3df corners[8];
    corners[0] = irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z);
    corners[1] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z);
    corners[2] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MinEdge.Z);
    corners[3] = irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y, bbox.MinEdge.Z);
    corners[4] = irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MaxEdge.Z);
    corners[5] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y, bbox.MaxEdge.Z);
    corners[6] = irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
    corners[7] = irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);

    // Draw the 12 edges of the bounding box
    // Bottom face
    driver_->draw3DLine(corners[0], corners[1], white);
    driver_->draw3DLine(corners[1], corners[2], white);
    driver_->draw3DLine(corners[2], corners[3], white);
    driver_->draw3DLine(corners[3], corners[0], white);

    // Top face
    driver_->draw3DLine(corners[4], corners[5], white);
    driver_->draw3DLine(corners[5], corners[6], white);
    driver_->draw3DLine(corners[6], corners[7], white);
    driver_->draw3DLine(corners[7], corners[4], white);

    // Vertical edges
    driver_->draw3DLine(corners[0], corners[4], white);
    driver_->draw3DLine(corners[1], corners[5], white);
    driver_->draw3DLine(corners[2], corners[6], white);
    driver_->draw3DLine(corners[3], corners[7], white);
}
#endif

// --- Mouse Targeting Implementation ---

void IrrlichtRenderer::setCurrentTarget(uint16_t spawnId, const std::string& name, uint8_t hpPercent, uint8_t level) {
    currentTargetId_ = spawnId;
    currentTargetName_ = name;
    currentTargetHpPercent_ = hpPercent;
    currentTargetLevel_ = level;
    // Enable animation debugging for targeted entity
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(spawnId);
    }
}

void IrrlichtRenderer::clearCurrentTarget() {
    if (isLoading()) return;
    currentTargetId_ = 0;
    currentTargetName_.clear();
    currentTargetHpPercent_ = 100;
    currentTargetLevel_ = 0;
    currentTargetInfo_ = TargetInfo();  // Reset to defaults
    // Disable animation debugging
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(0);
    }
}

void IrrlichtRenderer::setCurrentTargetInfo(const TargetInfo& info) {
    if (isLoading()) return;
    currentTargetInfo_ = info;
    // Also update the legacy fields for backward compatibility
    currentTargetId_ = info.spawnId;
    currentTargetName_ = info.name;
    currentTargetHpPercent_ = info.hpPercent;
    currentTargetLevel_ = info.level;
    // Enable animation debugging for targeted entity
    if (entityRenderer_) {
        entityRenderer_->setDebugTargetId(info.spawnId);
    }
}

void IrrlichtRenderer::updateCurrentTargetHP(uint8_t hpPercent) {
    if (isLoading()) return;
    currentTargetHpPercent_ = hpPercent;
    currentTargetInfo_.hpPercent = hpPercent;
}

void IrrlichtRenderer::handleMouseTargeting(int clickX, int clickY) {
    if (!eventReceiver_ || !camera_ || !entityRenderer_) {
        return;
    }

    bool shiftHeld = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
    bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);

    // Get entity at click position
    uint16_t targetId = getEntityAtScreenPos(clickX, clickY);

    if (targetId != 0) {
        // Found an entity - verify it's visible (LOS from camera)
        const auto& entities = entityRenderer_->getEntities();
        auto it = entities.find(targetId);
        if (it != entities.end()) {
            const auto& visual = it->second;

            // Convert entity position to Irrlicht coords for LOS check
            // EQ (x, y, z) -> Irrlicht (x, z, y)
            irr::core::vector3df entityPos(visual.lastX, visual.lastZ + 5.0f, visual.lastY);
            irr::core::vector3df cameraPos = camera_->getPosition();

            if (checkEntityLOS(cameraPos, entityPos)) {
                // Check for shift+click on corpse to loot
                if (shiftHeld && visual.isCorpse) {
                    if (bridge_) bridge_->pushIntent(eqt::events::LootCorpseIntent{targetId});
                } else if (ctrlHeld && visual.isNPC && !visual.isCorpse) {
                    // Ctrl+click on NPC - banker interaction
                    LOG_INFO(MOD_GRAPHICS, "Ctrl+click on NPC: {} (ID: {})", visual.name, targetId);
                    if (bridge_) bridge_->pushIntent(eqt::events::BankerInteractIntent{targetId});
                } else {
                    // Entity is visible - set as target
                    LOG_INFO(MOD_GRAPHICS, "Target selected: {} (ID: {})", visual.name, targetId);
                    if (bridge_) bridge_->pushIntent(eqt::events::TargetIntent{targetId});
                }
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "Cannot target {} - obstructed", visual.name);
            }
        }
    } else {
        // No entity found - check for door click
        bool handledClick = false;
        if (doorManager_) {
            uint8_t doorId = doorManager_->getDoorAtScreenPos(clickX, clickY, camera_, collisionManager_);
            if (doorId != 0) {
                LOG_INFO(MOD_GRAPHICS, "Door clicked: ID {}", doorId);
                if (bridge_) bridge_->pushIntent(eqt::events::DoorInteractIntent{doorId});
                handledClick = true;
            }
        }

        // Check for world object (tradeskill container) click
        if (!handledClick) {
            uint32_t objectId = getWorldObjectAtScreenPos(clickX, clickY);
            if (objectId != 0) {
                LOG_INFO(MOD_GRAPHICS, "World object clicked: dropId {}", objectId);
                if (bridge_) bridge_->pushIntent(eqt::events::WorldObjectInteractIntent{objectId});
            }
        }
    }
}

uint16_t IrrlichtRenderer::getEntityAtScreenPos(int screenX, int screenY) {
    if (!collisionManager_ || !camera_ || !entityRenderer_ || !driver_) {
        return 0;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionManager_->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera_);

    // Check each entity for intersection
    const auto& entities = entityRenderer_->getEntities();
    float closestDist = std::numeric_limits<float>::max();
    uint16_t closestEntity = 0;

    for (const auto& [spawnId, visual] : entities) {
        // Skip player entity
        if (visual.isPlayer) {
            continue;
        }

        irr::scene::ISceneNode* node = visual.sceneNode;
        if (!node || !node->isVisible()) {
            continue;
        }

        // Get bounding box in world space
        irr::core::aabbox3df bbox = node->getTransformedBoundingBox();

        // Expand bounding box slightly for easier targeting
        irr::core::vector3df extent = bbox.getExtent();
        float minSize = 5.0f;  // Minimum clickable size
        if (extent.X < minSize) {
            float expand = (minSize - extent.X) / 2.0f;
            bbox.MinEdge.X -= expand;
            bbox.MaxEdge.X += expand;
        }
        if (extent.Y < minSize) {
            float expand = (minSize - extent.Y) / 2.0f;
            bbox.MinEdge.Y -= expand;
            bbox.MaxEdge.Y += expand;
        }
        if (extent.Z < minSize) {
            float expand = (minSize - extent.Z) / 2.0f;
            bbox.MinEdge.Z -= expand;
            bbox.MaxEdge.Z += expand;
        }

        // Check ray intersection with bounding box
        if (bbox.intersectsWithLine(ray)) {
            // Calculate distance to entity center
            irr::core::vector3df center = bbox.getCenter();
            float dist = ray.start.getDistanceFrom(center);

            if (dist < closestDist) {
                closestDist = dist;
                closestEntity = spawnId;
            }
        }
    }

    return closestEntity;
}

bool IrrlichtRenderer::checkEntityLOS(const irr::core::vector3df& cameraPos, const irr::core::vector3df& entityPos) {
    if (!zoneBspTree_) {
        return true;  // No BSP data — can't do collision, assume visible
    }

    // BSP PVS check first — if target region isn't visible from camera region, blocked by zone geometry
    if (zoneBspTree_) {
        // Irrlicht (x, y, z) -> EQ (x, z, y)
        size_t camRegion = zoneBspTree_->findRegionIndexForPoint(cameraPos.X, cameraPos.Z, cameraPos.Y);
        size_t entRegion = zoneBspTree_->findRegionIndexForPoint(entityPos.X, entityPos.Z, entityPos.Y);

        if (camRegion != SIZE_MAX && entRegion != SIZE_MAX && camRegion != entRegion) {
            if (camRegion < zoneBspTree_->regions.size()) {
                const auto& pvs = zoneBspTree_->regions[camRegion]->visibleRegions;
                if (entRegion < pvs.size() && !pvs[entRegion]) {
                    return false;  // Not PVS-visible — wall between camera and entity
                }
            }
        }
    }

    // PVS says visible (or no BSP) — raycast against BSP-filtered region geometry + doors/objects
    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;

    bool hit = checkCollisionBspFiltered(cameraPos, entityPos, hitPoint, hitTriangle);

    if (!hit) {
        return true;  // No obstruction
    }

    // Check if hit point is closer than entity
    float hitDist = cameraPos.getDistanceFrom(hitPoint);
    float entityDist = cameraPos.getDistanceFrom(entityPos);

    // Add a small margin to account for bounding box size
    return hitDist > (entityDist - 10.0f);
}

void IrrlichtRenderer::setZoneLineDebug(bool inZoneLine, uint16_t targetZoneId, const std::string& debugText) {
    if (isLoading()) return;
    inZoneLine_ = inZoneLine;
    zoneLineTargetZoneId_ = targetZoneId;
    zoneLineDebugText_ = debugText;
}

void IrrlichtRenderer::drawZoneLineOverlay() {
    if (!inZoneLine_ || !driver_) {
        return;
    }

    // Draw a semi-transparent pink overlay on the screen edges
    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

    // Pink color with alpha
    irr::video::SColor pink(100, 255, 50, 150);  // Semi-transparent pink

    // Draw border rectangles (top, bottom, left, right)
    int borderWidth = 15;

    // Top border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, 0, screenSize.Width, borderWidth));
    // Bottom border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, screenSize.Height - borderWidth, screenSize.Width, screenSize.Height));
    // Left border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(0, borderWidth, borderWidth, screenSize.Height - borderWidth));
    // Right border
    driver_->draw2DRectangle(pink, irr::core::rect<irr::s32>(screenSize.Width - borderWidth, borderWidth, screenSize.Width, screenSize.Height - borderWidth));

    // Draw zone line text using GUI environment's built-in font
    if (guienv_) {
        irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
        if (font) {
            std::wstring text = L"[ZONE LINE] Target Zone: " + std::to_wstring(zoneLineTargetZoneId_);
            irr::core::dimension2d<irr::u32> textSize = font->getDimension(text.c_str());
            int textX = (screenSize.Width - textSize.Width) / 2;
            int textY = borderWidth + 5;
            font->draw(text.c_str(), irr::core::rect<irr::s32>(textX, textY, textX + textSize.Width, textY + textSize.Height),
                       irr::video::SColor(255, 255, 100, 200));  // Bright pink text

            // Draw debug text if available
            if (!zoneLineDebugText_.empty()) {
                std::wstring debugWStr(zoneLineDebugText_.begin(), zoneLineDebugText_.end());
                irr::core::dimension2d<irr::u32> debugSize = font->getDimension(debugWStr.c_str());
                int debugX = (screenSize.Width - debugSize.Width) / 2;
                int debugY = textY + textSize.Height + 5;
                font->draw(debugWStr.c_str(), irr::core::rect<irr::s32>(debugX, debugY, debugX + debugSize.Width, debugY + debugSize.Height),
                           irr::video::SColor(255, 255, 200, 255));  // Light pink text
            }
        }
    }
}

void IrrlichtRenderer::setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>& boxes) {
    if (isLoading()) return;
    // Clear existing boxes first
    clearZoneLineBoundingBoxes();

    if (!smgr_ || !driver_) {
        LOG_WARN(MOD_GRAPHICS, "Cannot create zone line boxes - renderer not initialized");
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "Creating {} zone line visualization boxes", boxes.size());

    for (const auto& box : boxes) {
        createZoneLineBoxMesh(box);
    }
    zoneLineBoxesCreated_ = showZoneLineBoxes_;
}

void IrrlichtRenderer::clearZoneLineBoundingBoxes() {
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->remove();
            boxNode.node = nullptr;
        }
    }
    zoneLineBoxNodes_.clear();
    zoneLineBoxesCreated_ = false;
}

void IrrlichtRenderer::toggleZoneLineVisualization() {
    showZoneLineBoxes_ = !showZoneLineBoxes_;

    // Remove/recreate scene nodes instead of toggling visibility.
    // Calling setVisible(false) on nodes with EMT_TRANSPARENT_VERTEX_ALPHA
    // triggers a GPU hang on Lima (Mali 400) driver.
    if (showZoneLineBoxes_) {
        recreateZoneLineBoxSceneNodes();
    } else {
        removeZoneLineBoxSceneNodes();
    }

    // Notify EverQuest to enable/disable zoning when zone lines are toggled
    if (bridge_) bridge_->pushIntent(eqt::events::ZoningEnabledIntent{showZoneLineBoxes_});

    LOG_INFO(MOD_GRAPHICS, "Zone line visualization and zoning {}", showZoneLineBoxes_ ? "enabled" : "disabled");
}

void IrrlichtRenderer::removeZoneLineBoxSceneNodes() {
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) {
            boxNode.node->remove();
            boxNode.node = nullptr;
        }
    }
    zoneLineBoxesCreated_ = false;
}

void IrrlichtRenderer::recreateZoneLineBoxSceneNodes() {
    if (zoneLineBoxesCreated_ || !smgr_ || !driver_) return;

    // Recreate scene nodes from stored bounding box data
    for (auto& boxNode : zoneLineBoxNodes_) {
        if (boxNode.node) continue;  // Already has a node

        // Reconstruct bounding box from stored data
        EQT::ZoneLineBoundingBox box;
        box.targetZoneId = boxNode.targetZoneId;
        box.isProximityBased = boxNode.isProximityBased;
        box.minX = boxNode.minX; box.minY = boxNode.minY; box.minZ = boxNode.minZ;
        box.maxX = boxNode.maxX; box.maxY = boxNode.maxY; box.maxZ = boxNode.maxZ;
        createZoneLineBoxMeshForNode(box, boxNode);
    }
    zoneLineBoxesCreated_ = true;
}

void IrrlichtRenderer::createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox& box) {
    // Store metadata and coordinates for later recreation
    ZoneLineBoxNode boxNode;
    boxNode.node = nullptr;
    boxNode.targetZoneId = box.targetZoneId;
    boxNode.isProximityBased = box.isProximityBased;
    boxNode.minX = box.minX; boxNode.minY = box.minY; boxNode.minZ = box.minZ;
    boxNode.maxX = box.maxX; boxNode.maxY = box.maxY; boxNode.maxZ = box.maxZ;

    // Only create scene nodes if visualization is currently enabled
    if (showZoneLineBoxes_ && smgr_ && driver_) {
        createZoneLineBoxMeshForNode(box, boxNode);
    }

    zoneLineBoxNodes_.push_back(boxNode);
}

void IrrlichtRenderer::createZoneLineBoxMeshForNode(const EQT::ZoneLineBoundingBox& box, ZoneLineBoxNode& boxNode) {
    if (!smgr_ || !driver_) return;

    // Convert EQ coordinates to Irrlicht coordinates
    // EQ uses Z-up: (x, y, z) -> Irrlicht Y-up: (x, z, y)
    float irrMinX = box.minX;
    float irrMinY = box.minZ;  // EQ Z -> Irrlicht Y
    float irrMinZ = box.minY;  // EQ Y -> Irrlicht Z
    float irrMaxX = box.maxX;
    float irrMaxY = box.maxZ;
    float irrMaxZ = box.maxY;

    // Calculate box dimensions and center
    float width = irrMaxX - irrMinX;
    float height = irrMaxY - irrMinY;
    float depth = irrMaxZ - irrMinZ;
    float centerX = (irrMinX + irrMaxX) / 2.0f;
    float centerY = (irrMinY + irrMaxY) / 2.0f;
    float centerZ = (irrMinZ + irrMaxZ) / 2.0f;

    LOG_INFO(MOD_GRAPHICS, "Zone line box -> zone {}: EQ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}) => Irr center({:.1f},{:.1f},{:.1f}) size({:.1f},{:.1f},{:.1f})",
        box.targetZoneId, box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ,
        centerX, centerY, centerZ, width, height, depth);

    // Create a cube mesh using geometry creator
    const irr::scene::IGeometryCreator* geomCreator = smgr_->getGeometryCreator();
    if (!geomCreator) {
        LOG_WARN(MOD_GRAPHICS, "No geometry creator available");
        return;
    }

    // Create a unit cube mesh and scale it
    irr::scene::IMesh* cubeMesh = geomCreator->createCubeMesh(irr::core::vector3df(width, height, depth));
    if (!cubeMesh) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create cube mesh");
        return;
    }

    // Create scene node for the box
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(cubeMesh);
    cubeMesh->drop();  // Scene manager now owns it

    if (!node) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create mesh scene node");
        return;
    }

    // Position the box
    node->setPosition(irr::core::vector3df(centerX, centerY, centerZ));

    // Use wireframe rendering - avoids EMT_TRANSPARENT_VERTEX_ALPHA which
    // causes GPU hangs on Lima (Mali 400) driver when toggling visibility
    irr::video::SColor color;
    if (box.isProximityBased) {
        color = irr::video::SColor(255, 0, 255, 255);  // Cyan
    } else {
        color = irr::video::SColor(255, 255, 0, 255);  // Magenta
    }

    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        irr::video::SMaterial& mat = node->getMaterial(i);
        mat.MaterialType = irr::video::EMT_SOLID;
        mat.Wireframe = true;
        mat.Lighting = false;
        mat.BackfaceCulling = false;
        mat.Thickness = 2.0f;
        mat.AmbientColor = color;
        mat.DiffuseColor = color;
    }

    // Set vertex colors for wireframe
    irr::scene::IMeshBuffer* meshBuffer = node->getMesh()->getMeshBuffer(0);
    if (meshBuffer) {
        irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(meshBuffer->getVertices());
        irr::u32 vertexCount = meshBuffer->getVertexCount();
        for (irr::u32 i = 0; i < vertexCount; ++i) {
            vertices[i].Color = color;
        }
    }

    node->setVisible(true);
    boxNode.node = node;

    LOG_TRACE(MOD_GRAPHICS, "Created zone line box for zone {} at ({},{},{}) size ({},{},{})",
        box.targetZoneId, centerX, centerY, centerZ, width, height, depth);
}

void IrrlichtRenderer::drawZoneLineBoxLabels() {
    if (!showZoneLineBoxes_ || !driver_ || !guienv_ || !camera_) {
        return;
    }

    irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
    if (!font) return;

    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

    for (const auto& boxNode : zoneLineBoxNodes_) {
        if (!boxNode.node || !boxNode.node->isVisible()) continue;

        // Get 3D position of box center
        irr::core::vector3df boxPos = boxNode.node->getAbsolutePosition();

        // Convert 3D position to 2D screen position
        irr::core::position2d<irr::s32> screenPos = smgr_->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(
            boxPos, camera_);

        // Check if on screen
        if (screenPos.X < 0 || screenPos.X >= (irr::s32)screenSize.Width ||
            screenPos.Y < 0 || screenPos.Y >= (irr::s32)screenSize.Height) {
            continue;
        }

        // Create label text
        std::wstring label = L"Zone " + std::to_wstring(boxNode.targetZoneId);
        if (boxNode.isProximityBased) {
            label += L" (prox)";
        }

        // Draw label
        irr::core::dimension2d<irr::u32> textSize = font->getDimension(label.c_str());
        int textX = screenPos.X - textSize.Width / 2;
        int textY = screenPos.Y - textSize.Height / 2;

        // Background rectangle
        irr::video::SColor bgColor(150, 0, 0, 0);
        driver_->draw2DRectangle(bgColor, irr::core::rect<irr::s32>(
            textX - 2, textY - 2, textX + textSize.Width + 2, textY + textSize.Height + 2));

        // Text color based on type
        irr::video::SColor textColor = boxNode.isProximityBased ?
            irr::video::SColor(255, 0, 255, 255) :  // Cyan
            irr::video::SColor(255, 255, 0, 255);   // Magenta

        font->draw(label.c_str(), irr::core::rect<irr::s32>(
            textX, textY, textX + textSize.Width, textY + textSize.Height), textColor);
    }
}

void IrrlichtRenderer::drawFPSCounter() {
    if (!driver_ || !guienv_) {
        return;
    }

    irr::gui::IGUIFont* font = guienv_->getBuiltInFont();
    if (!font) {
        return;
    }

    // Build FPS text
    std::wstring fpsText = L"FPS: " + std::to_wstring(currentFps_);

    // Get screen dimensions and text size for centering
    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();
    irr::core::dimension2d<irr::u32> textSize = font->getDimension(fpsText.c_str());

    // Position at top center with small padding from top edge
    int textX = (screenSize.Width - textSize.Width) / 2;
    int textY = 5;

    // Draw with a semi-transparent black background for readability
    irr::core::rect<irr::s32> bgRect(textX - 4, textY - 2,
                                      textX + textSize.Width + 4,
                                      textY + textSize.Height + 2);
    driver_->draw2DRectangle(irr::video::SColor(128, 0, 0, 0), bgRect);

    // Draw FPS text in white
    font->draw(fpsText.c_str(),
               irr::core::rect<irr::s32>(textX, textY, textX + textSize.Width, textY + textSize.Height),
               irr::video::SColor(255, 255, 255, 255));
}

} // namespace Graphics
} // namespace EQT
