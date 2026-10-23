#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"

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
    using A = SpellEffectsConfig::SourceAnchor;
    const auto& sc = SpellEffectsConfig::instance().sourceScale();
    const float H = sc.casterHeight;
    SpellEffectDef def = style;
    if (def.emitters.empty()) return def;
    SpellEmitterDef base = def.emitters[0];
    base.trigger = rec.onset > 0.0f ? SpellTrigger::DELAYED : SpellTrigger::IMMEDIATE;
    base.triggerDelay = rec.onset;
    base.config.emitterLifetime = 0.0f;  // emits until releaseCastGlow's hold ends it
    base.config.burstCount = 0;
    if (rec.spawnRate > 0.0f)
        base.config.spawnRate = rec.spawnRate * share *
                                SpellEffectsConfig::instance().getGlobal().densityMultiplier;
    else
        base.config.spawnRate *= share;
    def.emitters.assign(1, base);
    if (!rec.hasShape) return def;  // the default row: the style's own body cloud

    auto& ed = def.emitters[0];
    auto& c = ed.config;
    const float life = 0.5f * (c.lifetimeMin + c.lifetimeMax);
    const float w = rec.halfWidth * H;
    const float span = (rec.hi - rec.lo) * H;
    // Height h (caster heights above the feet) relative to the followed point.
    auto at = [&](float h) { return h * H - anchorAboveFeet; };
    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;
    switch (rec.anchor) {
        case A::Body: {
            // B1122's cloud over the measured box; its rise is the style's.
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::BOX;
            c.spawnExtents = glm::vec3(w, 0.5f * span, w);
            ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)) - 0.5f * c.velocityBase.y * life, 0.0f);
            break;
        }
        case A::Orbit: {
            c.motionType = MotionType::ORBITAL;
            c.spawnShape = SpawnShape::BOX;
            c.orbitalRadius = w;
            c.orbitalAngularVelocity = sc.orbitAngularVelocity;
            c.velocitySpread = glm::vec3(0.0f);
            if (rec.spiral) {  // born at the bottom, rising to the top over one life
                c.spawnExtents = glm::vec3(0.0f);
                c.velocityBase = glm::vec3(0.0f, span / life, 0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(rec.lo), 0.0f);
            } else {           // a band over lo..hi
                c.spawnExtents = glm::vec3(0.0f, 0.5f * span, 0.0f);
                c.velocityBase = glm::vec3(0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)), 0.0f);
            }
            break;
        }
        case A::FeetRing: {
            // A ring round the feet rising into a curtain up to hi over one life.
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::RING;
            c.spawnExtents = glm::vec3(w, 0.0f, 0.0f);
            c.velocityBase = glm::vec3(0.0f, span / life, 0.0f);
            c.velocitySpread = glm::vec3(0.0f);
            ed.positionOffset = glm::vec3(0.0f, at(rec.lo), 0.0f);
            break;
        }
        case A::Hands: {
            // Born AT the hand; the spray's mean sits at the measured centroid
            // and its 5th..95th percentile spans the measured box. A sprite's
            // offset is (velocity spread U(-s,s)) x (age ~U(0,life)), and the
            // 95th percentile of a product of two U(0,1) is 0.70, so s is the
            // half-span over 0.70 x life.
            constexpr float kP95OfUniformProduct = 0.70f;
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::POINT;
            c.spawnExtents = glm::vec3(0.0f);
            c.velocityBase = glm::vec3(0.0f, 2.0f * at(rec.centroid) / life, 0.0f);
            const float k = 1.0f / (kP95OfUniformProduct * life);
            c.velocitySpread = glm::vec3(w * k, 0.5f * span * k, w * k);
            ed.positionOffset = glm::vec3(0.0f);
            break;
        }
        case A::Head: {
            // The halo disc at the head bone, and the column that falls from it.
            const float discMid = 0.5f * (rec.discLo + rec.discHi);
            SpellEmitterDef column = ed;
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::DISC;
            c.spawnExtents = glm::vec3(rec.discHalfWidth * H, 0.5f * (rec.discHi - rec.discLo) * H, 0.0f);
            c.velocityBase = glm::vec3(0.0f);
            c.velocitySpread = glm::vec3(0.0f);
            c.spawnRate *= rec.discShare;
            ed.positionOffset = glm::vec3(0.0f, at(discMid), 0.0f);
            auto& cc = column.config;
            cc.motionType = MotionType::LINEAR;
            cc.spawnShape = SpawnShape::DISC;
            cc.spawnExtents = glm::vec3(rec.discHalfWidth * H, 0.0f, 0.0f);
            cc.velocityBase = glm::vec3(0.0f, -(rec.discLo - rec.lo) * H / life, 0.0f);
            cc.velocitySpread = glm::vec3(0.0f);
            cc.spawnRate *= (1.0f - rec.discShare);
            column.positionOffset = glm::vec3(0.0f, at(rec.discLo), 0.0f);
            def.emitters.push_back(column);
            break;
        }
    }
    return def;
}

// The record row a spell's Source is drawn with: a spellOverrides castGlow /
// castSpray that sets its own spawnShape (B1122's measured 54/656/41/253/458
// clouds) keeps that geometry on the caster's entity, and one that sets its
// own spawnRate keeps that rate; the anchor class, onset and hold are always
// the record's.
static SpellEffectsConfig::SourceRecord rowForSpell(uint32_t spellId, int record, bool spray) {
    const auto& cfg = SpellEffectsConfig::instance();
    SpellEffectsConfig::SourceRecord rec = cfg.sourceRecord(record);
    const char* style = spray ? "castSpray" : "castGlow";
    if (cfg.hasSourceShapeOverride(spellId, style)) rec.hasShape = false;
    if (cfg.hasSourceRateOverride(spellId, style)) rec.spawnRate = 0.0f;
    return rec;
}

SpellEffectsConfig::SourceAnchor SourceAnchorForSpell(uint32_t spellId, int record, bool spray) {
    const auto rec = rowForSpell(spellId, record, spray);
    return rec.hasShape ? rec.anchor : SpellEffectsConfig::SourceAnchor::Body;
}

SpellEffectDef SourceForSpell(glm::vec4 color, uint32_t spellId, int record, bool spray,
                              float anchorAboveFeet, float share) {
    const SpellEffectDef style = spray ? CastSpray(color, spellId) : CastGlow(color, spellId);
    // The pool rule's factor (1 unless the held Source cannot sit beside its landing).
    const float k = SpellEffectsConfig::instance().sourcePoolScale(spellId, record);
    return SourceForRecord(style, rowForSpell(spellId, record, spray), anchorAboveFeet, share * k);
}

// B1132 (#452): one landing half for its spells.eff record's Target row
// (B1130 survey, config `targetRecords`, target heights above the target's
// feet). Body and Caster rows are the style's B1120 volume unchanged (its
// geometry, sizes, lifetimes and the B1123 population); the caller only
// chooses the entity. The shaped rows keep the style's sprite sizes,
// lifetimes, alphas, onset delay and emission schedule (sizes-first) and set
// WHERE the sprites are born and how they move:
//   Head       the impact half is a glow AT the head bone (the face, where the
//              oracle's landing starts, e5 f8233); the spellComplete half a
//              cloud over lo..hi x +-halfWidth round head and shoulders.
//   FeetGround the impact half swirls as a ring of radius halfWidth at lo (the
//              rings on the ground round the target); the spellComplete half
//              is a ring at lo rising to hi over one life (climbing the legs).
//   Column     the spellComplete half falls from hi to lo over +-halfWidth
//              (the sheet that sweeps down over the target); the impact half
//              fills lo..hi (the wash).
// Both halves of a shaped row carry `population` of the style's rates.
SpellEffectDef TargetForRecord(const SpellEffectDef& style,
                               const SpellEffectsConfig::TargetRecord& rec,
                               float anchorAboveFeet, bool complete, bool followsHeadBone) {
    using T = SpellEffectsConfig::TargetAnchor;
    SpellEffectDef def = style;
    if (!rec.hasShape || def.emitters.empty()) return def;
    const auto& sc = SpellEffectsConfig::instance().sourceScale();
    const float H = sc.casterHeight;
    auto& ed = def.emitters[0];
    auto& c = ed.config;
    c.spawnRate *= rec.population;
    c.sustainRate *= rec.population;
    c.burstCount = static_cast<int>(static_cast<float>(c.burstCount) * rec.population);
    const float life = 0.5f * (c.lifetimeMin + c.lifetimeMax);
    const float w = rec.halfWidth * H;
    const float span = (rec.hi - rec.lo) * H;
    // A head row is placed relative to the head bone alone (boneAt = the height
    // the bone stands for), so the target's own feet and size do not move it.
    const float followed =
        (rec.anchor == T::Head && rec.boneAt > 0.0f && followsHeadBone) ? rec.boneAt * H : anchorAboveFeet;
    auto at = [&](float h) { return h * H - followed; };
    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;
    c.expandSpeed = 0.0f;
    c.velocitySpread = glm::vec3(0.0f);
    switch (rec.anchor) {
        case T::Head:
            c.motionType = MotionType::LINEAR;
            c.velocityBase = glm::vec3(0.0f);
            if (complete) {
                c.spawnShape = SpawnShape::BOX;
                c.spawnExtents = glm::vec3(w, 0.5f * span, w);
                ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)), 0.0f);
            } else {  // the face: a small glow at the head bone itself
                c.spawnShape = SpawnShape::SPHERE;
                c.spawnExtents = glm::vec3(0.06f * H, 0.0f, 0.0f);
                ed.positionOffset = glm::vec3(0.0f);
            }
            break;
        case T::FeetGround:
            if (complete) {
                c.motionType = MotionType::LINEAR;
                c.spawnShape = SpawnShape::RING;
                c.spawnExtents = glm::vec3(w, 0.0f, 0.0f);
                c.velocityBase = glm::vec3(0.0f, span / life, 0.0f);
            } else {
                c.motionType = MotionType::ORBITAL;
                c.spawnShape = SpawnShape::BOX;
                c.spawnExtents = glm::vec3(0.0f);
                c.orbitalRadius = w;
                c.orbitalAngularVelocity = sc.orbitAngularVelocity;
                c.velocityBase = glm::vec3(0.0f);
            }
            ed.positionOffset = glm::vec3(0.0f, at(rec.lo), 0.0f);
            break;
        case T::Column:
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::BOX;
            if (complete) {
                c.spawnExtents = glm::vec3(w, 0.0f, w);
                c.velocityBase = glm::vec3(0.0f, -span / life, 0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(rec.hi), 0.0f);
            } else {
                c.spawnExtents = glm::vec3(w, 0.5f * span, w);
                c.velocityBase = glm::vec3(0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)), 0.0f);
            }
            break;
        default:
            break;
    }
    return def;
}

static SpellEffectsConfig::TargetRecord targetRowForSpell(uint32_t spellId, int record) {
    const auto& cfg = SpellEffectsConfig::instance();
    SpellEffectsConfig::TargetRecord rec = cfg.targetRecord(record);
    // B1120/B1122's measured landing overrides (54/656/41/253/458) keep their
    // geometry: none of their records (23, 20, 35, 33) is a shaped row.
    if (cfg.hasSourceShapeOverride(spellId, "impact") ||
        cfg.hasSourceShapeOverride(spellId, "spellComplete"))
        rec.hasShape = false;
    return rec;
}

SpellEffectsConfig::TargetAnchor TargetAnchorForSpell(uint32_t spellId, int record) {
    const auto rec = targetRowForSpell(spellId, record);
    using T = SpellEffectsConfig::TargetAnchor;
    if (rec.anchor == T::Caster) return T::Caster;
    return rec.hasShape ? rec.anchor : T::Body;
}

bool TargetHasOverrideShape(uint32_t spellId) {
    const auto& cfg = SpellEffectsConfig::instance();
    return cfg.hasSourceShapeOverride(spellId, "impact") ||
           cfg.hasSourceShapeOverride(spellId, "spellComplete");
}

SpellEffectDef TargetForSpell(glm::vec4 color, uint32_t spellId, int record, bool complete,
                              float anchorAboveFeet, float share, bool followsHeadBone) {
    const SpellEffectDef style = complete ? SpellComplete(color, spellId, record)
                                          : Impact(color, spellId, record);
    SpellEffectDef def = TargetForRecord(style, targetRowForSpell(spellId, record), anchorAboveFeet,
                                         complete, followsHeadBone);
    if (share != 1.0f) {
        for (auto& ed : def.emitters) {
            ed.config.spawnRate *= share;
            ed.config.sustainRate *= share;
            ed.config.burstCount =
                static_cast<int>(std::lround(static_cast<float>(ed.config.burstCount) * share));
        }
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
            rc.active = !w.isWhite();
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
