#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

// ============================================================================
// Entity Casting Management
// ============================================================================

void EntityRenderer::startEntityCast(uint16_t spawnId, uint32_t spellId, const std::string& spellName, uint32_t castTimeMs) {
    // Don't track casting for our own player (handled separately by player casting bar)
    // Check against stored playerSpawnId_ as primary check (in case isPlayer flag isn't set yet)
    if (spawnId == playerSpawnId_ && playerSpawnId_ != 0) {
        return;
    }

    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Also check the isPlayer flag as a backup
    if (visual.isPlayer) {
        return;
    }

    // Set casting state
    visual.isCasting = true;
    visual.castSpellId = spellId;
    visual.castSpellName = spellName;
    visual.castDurationMs = castTimeMs;
    visual.castStartTime = std::chrono::steady_clock::now();

    // Phase 6.2: Match casting animation speed to cast time
    // Play cast animation (t04 = cast start, t05 = channeling loop)
    if (visual.animatedNode) {
        // Start with cast pullback animation, then it will loop on t05
        if (visual.animatedNode->hasAnimation("t04")) {
            visual.animatedNode->playAnimation("t04", false, true);  // playThrough, no loop
            // Set duration to roughly match cast time (cast start is quick, most time in loop)
            visual.animatedNode->getAnimator().setTargetDuration(static_cast<float>(castTimeMs) * 0.2f);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) started casting '{}' ({}ms)",
              spawnId, visual.name, spellName, castTimeMs);
}

void EntityRenderer::cancelEntityCast(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    if (visual.isCasting) {
        LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) cast interrupted", spawnId, visual.name);
    }

    // Clear casting state
    visual.isCasting = false;
    visual.castSpellId = 0;
    visual.castSpellName.clear();
    visual.castDurationMs = 0;
}

void EntityRenderer::completeEntityCast(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    if (visual.isCasting) {
        LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) completed casting '{}'",
                  spawnId, visual.name, visual.castSpellName);
    }

    // Clear casting state
    visual.isCasting = false;
    visual.castSpellId = 0;
    visual.castSpellName.clear();
    visual.castDurationMs = 0;
}

bool EntityRenderer::isEntityCasting(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }
    return it->second.isCasting;
}

float EntityRenderer::getEntityCastProgress(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end() || !it->second.isCasting) {
        return 0.0f;
    }

    const EntityVisual& visual = it->second;
    if (visual.castDurationMs == 0) {
        return 1.0f;  // Instant cast
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - visual.castStartTime).count();

    return std::min(1.0f, static_cast<float>(elapsed) / visual.castDurationMs);
}

void EntityRenderer::updateEntityCastingBars(float deltaTime, irr::scene::ICameraSceneNode* camera) {
    if (!camera) {
        return;
    }

    // Check and auto-complete any casts that have timed out
    auto now = std::chrono::steady_clock::now();

    for (auto& [spawnId, visual] : entities_) {
        if (!visual.isCasting) {
            continue;
        }

        // Check if cast has completed (server should send action packet, but this is a safety check)
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - visual.castStartTime).count();

        // Give some extra time (500ms) before auto-completing to allow server response
        if (elapsed > visual.castDurationMs + 500) {
            // Cast timed out without server confirmation - clear state
            visual.isCasting = false;
            visual.castSpellId = 0;
            visual.castSpellName.clear();
            visual.castDurationMs = 0;
        }
    }
}

void EntityRenderer::renderEntityCastingBars(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                                              irr::scene::ICameraSceneNode* camera) {
    if (!driver || !camera || !smgr_) {
        return;
    }

    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;

    // Get screen dimensions
    irr::core::dimension2du screenSize = driver->getScreenSize();
    int screenWidth = screenSize.Width;
    int screenHeight = screenSize.Height;

    // Casting bar dimensions (in pixels)
    const int BAR_WIDTH = 60;
    const int BAR_HEIGHT = 6;
    const int BAR_OFFSET_Y = -35;  // Offset above entity center (negative = up in screen space)

    // Colors
    const irr::video::SColor BAR_BG_COLOR(180, 40, 40, 40);
    const irr::video::SColor BAR_FILL_COLOR(255, 80, 180, 255);  // Blue/purple for casting
    const irr::video::SColor BAR_BORDER_COLOR(255, 100, 100, 100);
    const irr::video::SColor TEXT_COLOR(255, 255, 255, 255);

    for (auto& [spawnId, visual] : entities_) {
        // Skip if not casting, or if this is the player entity
        if (!visual.isCasting || visual.isPlayer || (spawnId == playerSpawnId_ && playerSpawnId_ != 0)) {
            continue;
        }

        // Get entity's 3D position
        irr::core::vector3df entityPos(visual.lastX, visual.lastZ + 8.0f, visual.lastY);  // Offset up for head height

        // Project to screen space
        irr::core::vector2di screenPos2D = smgr_->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(
                entityPos, camera, false);

        // Check if position is valid (not offscreen)
        // getScreenCoordinatesFrom3DPosition returns (-1000, -1000) if behind camera
        if (screenPos2D.X == -1000 && screenPos2D.Y == -1000) {
            continue;  // Entity is behind camera
        }

        int x = screenPos2D.X;
        int y = screenPos2D.Y;

        // Check if on screen
        if (x < -BAR_WIDTH || x > screenWidth + BAR_WIDTH ||
            y < -50 || y > screenHeight + 50) {
            continue;
        }

        // Apply offset (move bar up above entity)
        y += BAR_OFFSET_Y;

        // Calculate progress
        float progress = getEntityCastProgress(spawnId);

        // Calculate bar position (centered on x)
        int barLeft = x - BAR_WIDTH / 2;
        int barTop = y - BAR_HEIGHT / 2;
        int barRight = barLeft + BAR_WIDTH;
        int barBottom = barTop + BAR_HEIGHT;

        // Draw background
        driver->draw2DRectangle(BAR_BG_COLOR,
            irr::core::recti(barLeft, barTop, barRight, barBottom));

        // Draw fill based on progress
        int fillWidth = static_cast<int>(BAR_WIDTH * progress);
        if (fillWidth > 0) {
            driver->draw2DRectangle(BAR_FILL_COLOR,
                irr::core::recti(barLeft, barTop, barLeft + fillWidth, barBottom));
        }

        // Draw border
        driver->draw2DLine(irr::core::position2di(barLeft, barTop),
                           irr::core::position2di(barRight, barTop), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barLeft, barBottom),
                           irr::core::position2di(barRight, barBottom), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barLeft, barTop),
                           irr::core::position2di(barLeft, barBottom), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barRight, barTop),
                           irr::core::position2di(barRight, barBottom), BAR_BORDER_COLOR);

        // Draw spell name below the bar
        if (font && !visual.castSpellName.empty()) {
            std::wstring spellNameW(visual.castSpellName.begin(), visual.castSpellName.end());
            int textY = barBottom + 2;
            irr::core::recti textRect(barLeft - 30, textY, barRight + 30, textY + 12);
            font->draw(spellNameW.c_str(), textRect, TEXT_COLOR, true, false);
        }
    }
}

} // namespace Graphics
} // namespace EQT
