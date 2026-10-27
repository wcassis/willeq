#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/spell_cast_animation.h"
#include "client/spell/spell_action_anim.h"
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

// B1157 (#476): the track the official client plays on OP_BeginCast -- the
// spell's CastingAnim (field 120) through eqgame's code table onto this
// model, at speed 1.0, one shot (hold/loop per the model's own looped flag)
// and held through the channel, ended at OP_Action / interrupt. The
// pre-B1157 hard-coded "t04" (stretched to 20 % of the cast time) is gone;
// the player is included (her model is an entity like any other -- the
// entity cast BAR still skips her, renderEntityCastingBars).
void EntityRenderer::startEntityCast(uint16_t spawnId, uint32_t spellId, const std::string& spellName,
                                     uint32_t castTimeMs, int castAnimId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Set casting state
    visual.isCasting = true;
    visual.actionAnimCode.clear();  // B1174: the cast track replaces an action one
    visual.castSpellId = spellId;
    visual.castSpellName = spellName;
    visual.castDurationMs = castTimeMs;
    visual.castStartTime = std::chrono::steady_clock::now();

    // The model: the spell's own track, started at the packet (the client
    // plays at the packet, no delay -- the FX Source stage's 0.41 s latency
    // is a particle-emission property, not a model one).
    visual.castAnimCode = playCastAnim(visual.animatedNode, castAnimId);
    if (!visual.castAnimCode.empty()) {
        visual.currentAnimation = visual.castAnimCode;
    }

    LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) started casting '{}' ({}ms, cast-anim track '{}')",
              spawnId, visual.name, spellName, castTimeMs, visual.castAnimCode);
}

// OP_Action / interrupt: the casting state ends; the model takes the new
// state's animation (the idle of a standing caster).
static void endEntityCastAnim(EntityVisual& visual) {
    if (visual.animatedNode && !visual.castAnimCode.empty()) {
        endCastAnim(visual.animatedNode, visual.isPlayer ? "p01" : "o01");
        visual.currentAnimation = visual.isPlayer ? "p01" : "o01";
    }
    visual.castAnimCode.clear();
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
    endEntityCastAnim(visual);
}

// B1174 (#493): eqgame's OP_Action handler (0x47f4af) asks the standing
// source's animation manager for the spell's CastingAnim, the call
// OP_BeginCast makes (spell_action_anim.h). A source in its cast state
// already plays that track (the client's same-id no-op), so only a source
// with no cast -- a /disc -- starts one here; the per-frame walk returns it
// to the idle when the track stops (updateMainThreadEntityState).
std::string EntityRenderer::actionEntityCastAnim(uint16_t spawnId, int castAnimId,
                                                 const EQ::ActionCastAnimInputs& in) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) return std::string();
    EntityVisual& visual = it->second;
    if (visual.isCasting || visual.isCorpse || visual.isFading || !visual.animatedNode) return std::string();
    // The client's conditions, as read (spell id, record, target type, the
    // source's stand byte, field 123, Bard/instrument_mod): the stand byte
    // is the spawn's Entity::stand_state, not the renderer's poseState.
    if (!EQ::ActionPlaysSourceCastAnim(in)) return std::string();
    const std::string code = playCastAnim(visual.animatedNode, castAnimId);
    if (!code.empty()) {
        visual.actionAnimCode = code;
        visual.currentAnimation = code;
    }
    LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) OP_Action cast-anim track '{}' (CastingAnim {})", spawnId, visual.name,
              code, castAnimId);
    return code;
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
    endEntityCastAnim(visual);
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
            // Cast timed out without server confirmation - clear state (the
            // cast pose ends with it, the way completeEntityCast does)
            visual.isCasting = false;
            visual.castSpellId = 0;
            visual.castSpellName.clear();
            visual.castDurationMs = 0;
            endEntityCastAnim(visual);
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
