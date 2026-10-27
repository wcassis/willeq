#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_population_model.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellPresets {

// === Helpers ===

static SpawnShape parseSpawnShape(const std::string& s) {
    if (s == "BOX") return SpawnShape::BOX;
    if (s == "SPHERE") return SpawnShape::SPHERE;
    if (s == "RING") return SpawnShape::RING;
    if (s == "DISC") return SpawnShape::DISC;
    return SpawnShape::POINT;
}

static MotionType parseMotionType(const std::string& s) {
    if (s == "LINEAR") return MotionType::LINEAR;
    if (s == "ORBITAL") return MotionType::ORBITAL;
    if (s == "RADIAL_EXPAND") return MotionType::RADIAL_EXPAND;
    if (s == "BURST") return MotionType::BURST;
    if (s == "CAMERA_RELATIVE") return MotionType::CAMERA_RELATIVE;
    return MotionType::LINEAR;
}

static UnifiedBlendMode parseBlendMode(const std::string& s) {
    if (s == "ALPHA") return UnifiedBlendMode::ALPHA;
    return UnifiedBlendMode::ADDITIVE;
}

// Apply a StyleSettings to an EmitterConfig, using color for start/end RGBA
static void applyStyle(const SpellEffectsConfig::StyleSettings& s, EmitterConfig& c, glm::vec4 color) {
    const auto& cfg = SpellEffectsConfig::instance();
    float density = cfg.getGlobal().densityMultiplier;

    if (s.hasMotionType) c.motionType = parseMotionType(s.motionType);
    if (s.hasSpawnRate) c.spawnRate = s.spawnRate * density;
    if (s.hasBurstCount) c.burstCount = static_cast<int>(s.burstCount * density);
    if (s.hasEmitterLifetime) c.emitterLifetime = s.emitterLifetime;
    if (s.hasSpawnWindow) c.spawnWindow = s.spawnWindow;
    if (s.hasSustainRate) c.sustainRate = s.sustainRate * density;
    if (s.hasSustainFrom) c.sustainFrom = s.sustainFrom;

    if (s.hasSpawnShape) c.spawnShape = parseSpawnShape(s.spawnShape);
    if (s.hasSpawnExtents) c.spawnExtents = s.spawnExtents;

    if (s.hasVelocityBase) c.velocityBase = s.velocityBase;
    if (s.hasVelocitySpread) c.velocitySpread = s.velocitySpread;

    if (s.hasGravity) c.gravity = s.gravity;
    if (s.hasDrag) c.drag = s.drag;

    c.colorStart = glm::vec4(color.r, color.g, color.b,
        s.hasColorStartAlpha ? s.colorStartAlpha : 0.8f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b,
        s.hasColorEndAlpha ? s.colorEndAlpha : 0.0f);

    if (s.hasSizeStartMin) c.sizeStartMin = s.sizeStartMin;
    if (s.hasSizeStartMax) c.sizeStartMax = s.sizeStartMax;
    if (s.hasSizeEndMin) c.sizeEndMin = s.sizeEndMin;
    if (s.hasSizeEndMax) c.sizeEndMax = s.sizeEndMax;
    if (s.hasLifetimeMin) c.lifetimeMin = s.lifetimeMin;
    if (s.hasLifetimeMax) c.lifetimeMax = s.lifetimeMax;

    if (s.hasBlendMode) c.blendMode = parseBlendMode(s.blendMode);

    if (s.hasTextureRegions && !s.textureRegions.empty()) {
        c.textureRegionCount = static_cast<uint8_t>(
            std::min(s.textureRegions.size(), size_t(4)));
        for (uint8_t i = 0; i < c.textureRegionCount; ++i) {
            c.textureRegions[i] = cfg.getTextureIndex(s.textureRegions[i]);
        }
    }

    if (s.hasOrbitalRadius) c.orbitalRadius = s.orbitalRadius;
    if (s.hasOrbitalAngularVelocity) c.orbitalAngularVelocity = s.orbitalAngularVelocity;
    if (s.hasExpandSpeed) c.expandSpeed = s.expandSpeed;
}

// B1120: a style's `triggerDelay` turns an IMMEDIATE emitter into a DELAYED
// one (the sim's SpellTrigger::DELAYED arm, effect.age >= triggerDelay).
// B1122: the Source styles read it too (castGlow/castSpray).
static void applyTrigger(const SpellEffectsConfig::StyleSettings& s, SpellEmitterDef& ed) {
    if (s.hasTriggerDelay && s.triggerDelay > 0.0f && ed.trigger == SpellTrigger::IMMEDIATE) {
        ed.trigger = SpellTrigger::DELAYED;
        ed.triggerDelay = s.triggerDelay;
    }
}

// === Preset Functions ===

SpellEffectDef CastSpray(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castSpray", spellId);

    SpellEffectDef def;
    def.name = "CastSpray";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);  // the record's onset replaces it (SourceForRecord, B1131)

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef CastGlow(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castGlow", spellId);

    SpellEffectDef def;
    def.name = "CastGlow";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.5f, 0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);  // the record's onset replaces it (SourceForRecord, B1131)

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef SpellComplete(glm::vec4 color, uint32_t spellId, int record) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("spellComplete", spellId, record);

    SpellEffectDef def;
    def.name = "SpellComplete";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef Impact(glm::vec4 color, uint32_t spellId, int record) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("impact", spellId, record);

    SpellEffectDef def;
    def.name = "Impact";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);

    def.emitters.push_back(ed);
    return def;
}

// B1131 (#451): one spells.eff record's Source, from the caster-height row the
// B1130 survey measured (SpellEffectsConfig::SourceRecord). `style` is the
// castGlow/castSpray definition: its sprite sizes, lifetimes, art and alphas
// stay (sizes-first, !280); this sets WHERE the sprites are born and how they
// move. `anchorAboveFeet` is the height of the point the emitter follows
// above the caster's feet, world units: the entity anchor for body / orbit /
// feet-ring, the hand or head bone for hands / head, so every row's heights
// land where the oracle drew them whatever the bone's pose. `share` is this
// definition's part of the record's population (each hand draws half).
SpellEffectDef SourceForRecord(const SpellEffectDef& style,
                               const SpellEffectsConfig::SourceRecord& rec,
                               float anchorAboveFeet, float share) {
    (void)anchorAboveFeet;
    const SpellEffectDef styleDef = style;
    if (styleDef.emitters.empty()) return styleDef;
    SpellEmitterDef base = styleDef.emitters[0];
    base.trigger = rec.onset > 0.0f ? SpellTrigger::DELAYED : SpellTrigger::IMMEDIATE;
    base.triggerDelay = rec.onset;
    base.config.emitterLifetime = 0.0f;  // emits until releaseCastGlow's hold ends it
    base.config.burstCount = 0;
    base.config.spawnRate *= share;  // B1149: no row population (a record's slots draw the file's)
    SpellEffectDef def = styleDef;
    def.emitters.assign(1, base);
    // B1153 (#473): the row's shape switch (hands / feet-ring / orbit / body /
    // head) is retired with the rows: the default row is the style's own body
    // cloud, and a record's Source is drawn by its spellsnew.edd emitters
    // (the measured anchor classes are the band in
    // tests/spell_fx_row_oracle.h).
    return def;
}

// B1153 (#473): the row a spell's Source is drawn with -- the rows are
// retired, so always the default row (the style's own body cloud on the
// caster). A record's Source is its spellsnew.edd emitters.
static SpellEffectsConfig::SourceRecord rowForSpell(uint32_t spellId, int record, bool spray) {
    (void)spellId;
    (void)spray;
    return SpellEffectsConfig::instance().sourceRecord(record);
}

SpellEffectsConfig::SourceAnchor SourceAnchorForSpell(uint32_t spellId, int record, bool spray) {
    const auto rec = rowForSpell(spellId, record, spray);
    // B1153: the default row's anchor (the measured classes are the band in
    // tests/spell_fx_row_oracle.h).
    return rec.anchor;
}

SpellEffectDef SourceForSpell(glm::vec4 color, uint32_t spellId, int record, bool spray,
                              float anchorAboveFeet, float share) {
    const SpellEffectDef style = spray ? CastSpray(color, spellId) : CastGlow(color, spellId);
    // The pool rule's factor (1 unless the held Source cannot sit beside its landing).
    const float k = SpellEffectsConfig::instance().sourcePoolScale(spellId, record);
    return SourceForRecord(style, rowForSpell(spellId, record, spray), anchorAboveFeet, share * k);
}

// B1153 (#473): the per-record Target rows (the measured anchor class and
// extent, B1130/B1132) are retired with the config block: a record's landing
// is its spellsnew.eff Target slots (B1144, B1152) and the measured shapes
// are the bands in tests/spell_fx_row_oracle.h and
// tests/spell_fx_shape_oracle.h. The style's own B1120 volume is the landing,
// the row's anchor class choosing the entity only (TargetAnchorForSpell).
static SpellEffectsConfig::TargetRecord targetRowForSpell(uint32_t spellId, int record) {
    (void)spellId;
    return SpellEffectsConfig::instance().targetRecord(record);
}

SpellEffectsConfig::TargetAnchor TargetAnchorForSpell(uint32_t spellId, int record) {
    const auto rec = targetRowForSpell(spellId, record);
    // The default row's anchor class (B1150: honored without a shape).
    return rec.anchor;
}

// B1153 (#473): TargetForRecord (the row's shaped geometry) is deleted with
// the rows: the landing is the style's B1120 volume (or the spell override's
// own measured cloud, merged in getStyleSettings), its entity the row's
// anchor class.
bool TargetHasOverrideShape(uint32_t spellId) {
    const auto& cfg = SpellEffectsConfig::instance();
    return cfg.hasSourceShapeOverride(spellId, "impact") ||
           cfg.hasSourceShapeOverride(spellId, "spellComplete");
}

SpellEffectDef TargetForSpell(glm::vec4 color, uint32_t spellId, int record, bool complete,
                              float anchorAboveFeet, float share, bool followsHeadBone) {
    (void)anchorAboveFeet;
    (void)followsHeadBone;
    SpellEffectDef def = complete ? SpellComplete(color, spellId, record)
                                  : Impact(color, spellId, record);
    if (share == 1.0f) return def;
    for (auto& ed : def.emitters) {
        ed.config.spawnRate *= share;
        ed.config.sustainRate *= share;
        ed.config.burstCount =
            static_cast<int>(std::lround(static_cast<float>(ed.config.burstCount) * share));
    }
    return def;
}

void applyStageSpriteNames(SpellEffectDef& def,
                           const std::vector<std::string>& spriteNames,
                           uint32_t spellId) {
    (void)spellId;
    const auto& cfg = SpellEffectsConfig::instance();

    uint8_t regions[4] = {0, 0, 0, 0};
    uint8_t count = 0;
    for (const auto& name : spriteNames) {
        if (name.empty()) continue;
        if (count >= 4) break;
        regions[count++] = cfg.getTextureIndex(name);
    }
    if (count == 0) return;

    for (auto& ed : def.emitters) {
        ed.config.textureRegionCount = count;
        for (uint8_t i = 0; i < 4; ++i) {
            ed.config.textureRegions[i] = (i < count) ? regions[i] : regions[0];
        }
        // T441 (board B398): no resist / school tint over the stage's art
        // -- see the header. The alphas stay: they are the style's fade
        // curve. B1146: a spellsnew.edd emitter's own colour words come from
        // applyStageEmitterColours().
        ed.config.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, ed.config.colorStart.a);
        ed.config.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, ed.config.colorEnd.a);
        for (auto& rc : ed.config.regionColours) rc = EmitterConfig::RegionColour{};
    }
}

void applyStageEmitterColours(SpellEffectDef& def,
                              const std::vector<EQ::SpellsNewEddColour>& colours,
                              bool clientAlpha) {
    if (colours.empty() || colours.size() > 4) return;
    for (auto& ed : def.emitters) {
        auto& c = ed.config;
        // The pairing is texture k <-> colour k; a count mismatch means the
        // regions came from somewhere else (an override's own list).
        if (c.textureRegionCount != colours.size()) continue;
        for (size_t k = 0; k < colours.size(); ++k) {
            const EQ::SpellsNewEddColour& w = colours[k];
            EmitterConfig::RegionColour rc;
            // B1149: on the client's alpha every region is the client's
            // (white too); RGB-only keeps a white region inactive (B1146).
            rc.active = clientAlpha || !w.isWhite();
            rc.clientAlpha = clientAlpha;
            rc.additive = w.additive;
            for (int i = 0; i < 3; ++i) {
                rc.start[i] = static_cast<float>(w.start[static_cast<size_t>(i)]) / 255.0f;
                rc.end[i] = static_cast<float>(w.end[static_cast<size_t>(i)]) / 255.0f;
            }
            rc.fadeIn = w.fadeIn;
            rc.fadeOut = w.fadeOut;
            rc.peakOpacity = w.peakOpacity;
            c.regionColours[k] = rc;
        }
    }
}

// B1149 (#469): see the header. The emitter's age starts at its trigger, so
// emitterLifetime is the window; a zero window (a burst-only slot such as
// 281's) still needs one simulation step to fire its burst, hence the floor.
void applySlotEmitter(const std::vector<SpellEffectDef*>& slotDefs, const EQ::SpellFxSlotTiming& t,
                      const EQ::SpellsNewEddSprite* sprite, float stageLatency, float attachScale,
                      const EQ::SpellsNewEddShape* shape, const EQ::SpellsNewEddMotion* motion) {
    constexpr float kOneStep = 0.05f;
    const float L = t.lifetime;
    const bool emits = t.emits() && L > 0.0f && t.peakLive() > 0.0f;

    // The geometry split: each emitter's measured (pre-B1149) peak, so the
    // slot's file count is shared the way the row was measured.
    std::vector<SpellEmitterDef*> eds;
    std::vector<float> weight;
    for (SpellEffectDef* d : slotDefs)
        for (auto& ed : d->emitters) {
            const auto& c = ed.config;
            spell_population::Schedule before;
            before.spawnRate = c.spawnRate;
            before.burstCount = c.burstCount;
            before.spawnWindow = c.spawnWindow;
            before.sustainRate = c.sustainRate;
            before.sustainFrom = c.sustainFrom;
            before.emitterLifetime = c.emitterLifetime;
            before.lifetimeMin = c.lifetimeMin;
            before.lifetimeMax = c.lifetimeMax;
            eds.push_back(&ed);
            weight.push_back(std::max(spell_population::PeakLive({before}), 0.0f));
        }
    float total = 0.0f;
    for (float w : weight) total += w;
    for (float& w : weight) w = total > 0.0f ? w / total : 1.0f / static_cast<float>(weight.size());

    // The burst is whole sprites: largest remainder, so the shares add to it.
    std::vector<int> burst(eds.size(), 0);
    if (emits && t.burst > 0 && !eds.empty()) {
        int given = 0;
        std::vector<std::pair<float, size_t>> rem;
        for (size_t i = 0; i < eds.size(); ++i) {
            const float exact = weight[i] * static_cast<float>(t.burst);
            burst[i] = static_cast<int>(std::floor(exact));
            given += burst[i];
            rem.push_back({exact - static_cast<float>(burst[i]), i});
        }
        std::stable_sort(rem.begin(), rem.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t k = 0; given < t.burst && k < rem.size(); ++k, ++given) ++burst[rem[k].second];
    }

    for (size_t i = 0; i < eds.size(); ++i) {
        SpellEmitterDef& ed = *eds[i];
        auto& c = ed.config;
        if (shape) {
            // B1150 (#470): the record's spawn geometry (spell_spawn_shape.h,
            // one case per +0xA8 word). The config's spawnShape/spawnExtents
            // below are then dead for this emitter: sim_particles.cpp's
            // eddShape.active branch precedes them.
            c.eddShape.active = true;
            c.eddShape.shape = shape->shape;
            c.eddShape.dimX = shape->dimX;
            c.eddShape.dimY = shape->dimY;
            c.eddShape.dimZ = shape->dimZ;
            c.eddShape.offX = shape->offX;
            c.eddShape.offY = shape->offY;
            c.eddShape.offZ = shape->offZ;
            c.eddShape.tiltA = shape->tiltA;
            c.eddShape.tiltB = shape->tiltB;
            c.eddShape.depthBias = shape->depth;
            c.eddShape.scale = shape->scaleShapes ? attachScale : 1.0f;
        }
        if (motion) {
            // B1151 (#471): the record's motion words (TSV rows in
            // review/B1141/edd_fields_from_code.tsv). s is the attach scale
            // the +0x6C read path proved (TRACE.md (a)); fable passes 1.0.
            auto& em = c.eddMotion;
            em.active = true;
            em.orientToAttach = motion->orientToAttach;   // +0x60
            em.noDepthWrite = motion->noDepthWrite;       // +0x64
            em.spriteMode = motion->spriteMode;           // +0x70
            em.follow = motion->follow;                   // +0x74
            em.upMin = motion->upMin; em.upMax = motion->upMax; em.upAccel = motion->upAccel;
            em.axis2Min = motion->axis2Min; em.axis2Max = motion->axis2Max; em.axis2Accel = motion->axis2Accel;
            em.axis3Min = motion->axis3Min; em.axis3Max = motion->axis3Max; em.axis3Accel = motion->axis3Accel;
            em.radialMin = motion->radialMin; em.radialMax = motion->radialMax; em.radialAccel = motion->radialAccel;
            em.swirlMin = motion->swirlMin; em.swirlMax = motion->swirlMax; em.swirlAccel = motion->swirlAccel;
            em.gravity = motion->gravity;                 // +0x128
            em.driftX = motion->driftX;                   // +0x12C
            em.randomStartSpin = motion->randomStartSpin; // +0x174
            em.spinMin = motion->spinMin; em.spinMax = motion->spinMax;  // +0x138/+0x188
            em.spinAlign = motion->spinAlign;             // +0x178
            em.frameCount = motion->frameCount;           // +0x130
            em.frameRate = motion->frameRate;             // +0x134
            em.drawNegativeRadius = motion->drawNegativeRadius;  // +0x198
            em.scale = sprite && sprite->scaleSizes ? attachScale : 1.0f;  // s
        }
        const float oldLife = 0.5f * (c.lifetimeMin + c.lifetimeMax);
        const float delay = std::max(stageLatency, 0.0f) + t.start;
        ed.trigger = delay > 0.0f ? SpellTrigger::DELAYED : SpellTrigger::IMMEDIATE;
        ed.triggerDelay = delay;
        c.spawnWindow = 0.0f;
        c.sustainRate = 0.0f;
        c.sustainFrom = 0.0f;
        c.spawnRate = 0.0f;
        c.emitterLifetime = std::max(t.window(), kOneStep);
        c.burstCount = 0;
        c.ticksPerSecond = 0.0f;
        c.tickParticles = 0.0f;
        c.densityAtCreate = true;  // B1165: `t` carries the Spell page's density
        if (!emits) continue;
        // B1145's motion time-scaling (oldLife / L on the config velocities)
        // applies to non-record spells only: a record's particles move on
        // the file's own motion words (eddMotion, B1151), which carry the
        // extents -- scaling the config motion under them would double-drive
        // anything that still reads it. Non-record spells keep it.
        if (!shape && oldLife > 0.0f) {
            const float k = oldLife / L;
            c.velocityBase *= k;
            c.velocitySpread *= k;
            c.gravity *= k * k;
            c.drag *= k;
            c.expandSpeed *= k;
        }
        c.lifetimeMin = L;
        c.lifetimeMax = L;
        c.burstCount = burst[i];
        if (t.window() > 0.0f && t.ticksPerSecond > 0.0f && t.perTick > 0) {
            c.ticksPerSecond = t.ticksPerSecond;
            c.tickParticles = weight[i] * static_cast<float>(t.perTick);
        }
        if (sprite) {
            auto& es = c.eddSprite;
            es.active = true;
            es.widthMin = sprite->widthMin;
            es.widthMax = sprite->widthMax;
            es.heightMin = sprite->heightMin;
            es.heightMax = sprite->heightMax;
            es.scale = sprite->multiplier * (sprite->scaleSizes ? attachScale : 1.0f);
            es.sameRandom = sprite->sameRandom;
            es.growIn = sprite->growIn;
            es.shrinkOut = sprite->shrinkOut;
            es.heightShrink = sprite->heightShrink;
            es.widthShrink = sprite->widthShrink;
        }
    }
}

SpellEffectDef Projectile(glm::vec4 color, uint32_t spellId) {
    SpellEffectDef def;
    def.name = "Projectile";

    // Emitter 0: Trail particles (attached to the moving projectile)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileTrail", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::IMMEDIATE;
        ed.attach = SpellAttach::PROJECTILE_PATH;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    // Emitter 1: Impact burst on arrival (triggers when projectile hits)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileImpact", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::ON_HIT;
        ed.attach = SpellAttach::TARGET;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    return def;
}

// Map resist type to particle color — delegates to config
glm::vec4 resistToParticleColor(uint8_t resistType) {
    return SpellEffectsConfig::instance().getResistColor(resistType);
}

} // namespace SpellPresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
