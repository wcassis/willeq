#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "client/graphics/environment/spell_population_model.h"
#include "client/spell/spells_eff_reader.h"
#include "client/spell/spell_fx_timing.h"
#include "client/data_paths.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <limits>

namespace EQT {
namespace Graphics {
namespace Environment {

SpellEffectsConfig::SpellEffectsConfig() {
    buildTextureNameMap();
}

SpellEffectsConfig& SpellEffectsConfig::instance() {
    static SpellEffectsConfig instance;
    return instance;
}

void SpellEffectsConfig::buildTextureNameMap() {
    textureNameMap_["SoftCircle"] = ParticleAtlas::SoftCircle;
    textureNameMap_["StarShape"] = ParticleAtlas::StarShape;
    textureNameMap_["WispyCloud"] = ParticleAtlas::WispyCloud;
    textureNameMap_["SporeShape"] = ParticleAtlas::SporeShape;
    textureNameMap_["GrainShape"] = ParticleAtlas::GrainShape;
    textureNameMap_["LeafShape"] = ParticleAtlas::LeafShape;
    textureNameMap_["Snowflake"] = ParticleAtlas::Snowflake;
    textureNameMap_["Ember"] = ParticleAtlas::Ember;
    textureNameMap_["SpellSparkle"] = ParticleAtlas::SpellSparkle;
    textureNameMap_["WaterDroplet"] = ParticleAtlas::WaterDroplet;
    textureNameMap_["RippleRing"] = ParticleAtlas::RippleRing;
    textureNameMap_["SnowPatch"] = ParticleAtlas::SnowPatch;
    textureNameMap_["RainStreak"] = ParticleAtlas::RainStreak;
    textureNameMap_["SmokeWisp"] = ParticleAtlas::SmokeWisp;
    textureNameMap_["IceCrystal"] = ParticleAtlas::IceCrystal;

    // T417 (board B300 -- Will ruled 2026-09-06: draw the REAL sprites,
    // "as close to the original as possible").
    //
    // T389 and T409 mapped twelve `spells.eff` sprite names onto the nearest
    // of the sixteen PROCEDURAL tiles, and stopped there because four of the
    // sprites are figurative and no 16x16 procedural tile stands in for them
    // honestly: `spelg1.tga` is a SWORD, `speli1.tga` a flaming SKULL,
    // `spelk2.tga` a row of skulls, `spelh1.tga` a lightning-wreathed sphere
    // -- and `GENI00_SPRITE`, the skull, is the single commonest name in the
    // whole file (32 of its 301 populated slots).
    //
    // Those twelve approximation rows are GONE. The official client's own
    // sprite art now ships as a second particle sheet
    // (`data/textures/spell_sprite_atlas.bmp`, built by
    // `tools/spell_sprite_atlas_builder` from `SpellEffects/spel*.tga`), and
    // `getTextureIndex()` below resolves any GEN name straight to its sprite
    // ordinal on that sheet through the same 0-based rule the reader uses. No hand-written name
    // table can drift from the sheet, because there is no name table: the
    // rule is `effectNameToTexture()` and the layout is
    // `spell_sprite_atlas.h`, which the builder includes too.
    //
    // The FOUR non-GEN names have no file to resolve to -- the 0-based rule
    // does not apply to them -- so they keep a procedural tile, chosen from
    // what the name itself says. These are the only tile approximations left
    // for a `spells.eff` name.
    textureNameMap_["I_SNOWFLAKESPRITE"] = ParticleAtlas::Snowflake;
    textureNameMap_["I_TFIRE_SPB"] = ParticleAtlas::Ember;
    textureNameMap_["I_TSMOKE_SPB"] = ParticleAtlas::SmokeWisp;
    textureNameMap_["GEN_Z_SPB"] = ParticleAtlas::WispyCloud;
}

uint8_t SpellEffectsConfig::getTextureIndex(const std::string& name) const {
    // An explicit row wins: the fifteen procedural shape names that
    // config/spell_effects.json uses, and the four non-GEN sprite names.
    auto it = textureNameMap_.find(name);
    if (it != textureNameMap_.end()) {
        return it->second;
    }

    // T417 (board B300): a `spells.eff` sprite name resolves to its REAL
    // sprite. The 0-based rule is the reader's -- one implementation, so the
    // atlas and the resolution cannot disagree -- and the ordinal comes from
    // the layout header the builder also includes. T420 (B355): the ordinal
    // names the SPRITE; the animation CELL is chosen per particle at draw
    // time from its age, so nothing here changes when the sheet grew 16x.
    // B1127 (#447): a spellsnew.edd stage names its texture FILE directly
    // ("spelua.png", "gena20.bmp") -- that is the art the oracle draws.
    {
        const int slot = SpellSpriteAtlas::slotForFile(name.c_str());
        if (slot >= 0) return static_cast<uint8_t>(SpellSpriteAtlas::kBase + slot);
    }

    const std::string file = EQ::SpellsEffReader::effectNameToTexture(name);
    if (!file.empty()) {
        const int slot = SpellSpriteAtlas::slotForFile(file.c_str());
        if (slot >= 0) {
            return static_cast<uint8_t>(SpellSpriteAtlas::kBase + slot);
        }
        // The rule named a file the sheet does not carry: GENA20_SPRITE ->
        // spela3.tga / GENA30_SPRITE -> spela4.tga (not shipped), or a
        // spells.eff file no spell without a field-145 record reaches
        // (B1127). It falls through to the procedural circle below; a spell
        // WITH a field-145 record never asks for it.
    }

    return ParticleAtlas::SoftCircle;
}

// T397, board B221. Two defects lived in the four lines below.
//
// (1) THE PATH. ParticleManager::init() calls this with the literal
//     "config/spell_effects.json", which is resolved against the CURRENT
//     WORKING DIRECTORY. Started from anywhere but the repo root the open
//     fails. eqt::ResolveDataPath() is the fix and it tries the CWD-relative
//     path FIRST, so a run from the repo root is byte-for-byte unchanged; the
//     executable-relative fallback is what makes a run from elsewhere work.
//     configPath_ keeps the RESOLVED path so /reloadspellfx reloads the same
//     file the client actually read.
//
// (2) THE SILENCE. A miss logged at WARN, said "using defaults", and returned
//     TRUE -- so every caller was told the config had loaded. It had not: the
//     built-in table below is a DIFFERENT effect, not a degraded one. Its
//     `impact` burst is 12 particles against the shipped file's 36, and a
//     preset built with no load() call at all gets EmitterConfig's own struct
//     defaults -- spawnRate 10/s with burstCount 0, i.e. a permanent emitter
//     where the shipped effect is a 0.1 s one-shot. A missing shipped data
//     file is an ERROR, and the return value now says so.
bool SpellEffectsConfig::load(const std::string& path) {
    const std::string resolved = eqt::ResolveDataPath(path);
    configPath_ = resolved;

    std::ifstream file(resolved);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS,
                  "SpellEffectsConfig: could not open '{}' (resolved from '{}') -- falling back "
                  "to the built-in style table, which is a DIFFERENT effect, not a degraded one "
                  "(built-in impact burst 12, shipped 36)",
                  resolved, path);
        setDefaults();
        loaded_ = true;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: Failed to parse '{}': {}", resolved, errors);
        setDefaults();
        loaded_ = true;
        return false;
    }

    // Start with defaults, then override from JSON
    setDefaults();

    // Load global settings
    if (root.isMember("global")) {
        const Json::Value& g = root["global"];
        if (g.isMember("enabled")) global_.enabled = g["enabled"].asBool();
        // B1123 (#443): the pool is the platform preset's `spellParticlePool`
        // (applyPool); a second copy here would drift, so it is refused.
        if (g.isMember("maxParticles"))
            LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: '{}' global.maxParticles ({}) is IGNORED -- "
                     "the spell particle pool is config/constrained_presets.json's spellParticlePool",
                     resolved, g["maxParticles"].asInt());
        if (g.isMember("densityMultiplier")) global_.densityMultiplier = g["densityMultiplier"].asFloat();
    }

    // Load resist colors
    if (root.isMember("resistColors")) {
        const Json::Value& rc = root["resistColors"];
        for (const auto& name : rc.getMemberNames()) {
            const Json::Value& arr = rc[name];
            if (arr.isArray() && arr.size() >= 4) {
                resistColors_[name] = glm::vec4(
                    arr[0].asFloat(), arr[1].asFloat(),
                    arr[2].asFloat(), arr[3].asFloat());
            }
        }
    }

    // Load styles (override defaults)
    if (root.isMember("styles")) {
        const Json::Value& stylesJson = root["styles"];
        for (const auto& styleName : stylesJson.getMemberNames()) {
            // Start from existing default if present
            StyleSettings& style = styles_[styleName];
            loadStyle(stylesJson[styleName], styleName, style);
        }
    }

    // B1131: the per-record Source rows (anchor, shape, onset, hold).
    loadSourceRecords(root);
    // B1132: the per-record Target (landing) rows (anchor, shape).
    loadTargetRecords(root);
    // B1153 (#473): the six B1120/B1122 spell overrides stay (the file is not
    // the whole story for them -- refactor/B1153/README.md documents each);
    // the retired blocks fail loudly if a stale config brings them back
    // (their evidence is the bands, not the file).
    if (root.isMember("spellOverrides"))
        loadSpellOverrides(root["spellOverrides"]);
    if (root.isMember("animRecords"))
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: `animRecords` is retired (B1153) -- the field-145 "
                  "record -> row mapping is the band in tests/spell_anim_rows.h; ignoring");

    // B1123: landing populations follow the platform pool (applyPool).
    global_.maxParticles = pool_;
    if (pool_ > 0) applyPopulationRule(root);

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Loaded settings from '{}'", resolved);
    return true;
}

bool SpellEffectsConfig::reload() {
    if (configPath_.empty()) {
        LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: No config path set, cannot reload");
        return false;
    }

    bool result = load(configPath_);

    if (result && reloadCallback_) {
        reloadCallback_();
        LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Reload callback invoked");
    }

    return result;
}

// ---------------------------------------------------------------------------
// B1123 (#443): THE POOL RULE.  Will ruled the pool per platform (8192 desktop,
// 4096 sbc; PROVISIONAL, B1130 re-sizes it) and that populations follow it by
// ONE rule, so a re-sized pool re-flows them without a config rewrite:
//
//   the style landing (impact + spellComplete) PLUS its own held Source
//   peaks at HALF THE POOL.
//
// B1161 (#480) + B1153 (#473): the bound is the NO-RECORD path's.  A spell
// with a spellsnew.eff record draws its slots' FILE populations (B1149) and
// the rule scales none of them: the oracle's pool (EQGraphicsDX9 -- per-
// emitter rings, ring full drops the rest of the frame's emission,
// 0x10061180; no global cap in its create path) is a CAP, not a scaler, and
// fable applies that rule at its one pool: new particles are dropped while
// it is full (allocateParticle returns -1, sim_particles.cpp:774), no count
// is scaled (refactor/B1149/README.md sec. 4).  The
// largest record field a class-level 1-60 spell draws is record 35's
// (spellsnew.eff 104, spell 253): 3746 + 1168 = 4914 peak live (each
// slot's burst + perTick x tps x min(window, L), summed: an upper bound) --
// inside the desktop 8192, over the sbc 4096 by 818, where the pool fills
// and drops (pinned by SpellLandingPool.TheLargestRecordFieldIsCappedNotScaled
// in tests/test_spell_landing_volume.cpp).  The half-pool bound still serves spells with no record
// (the style landing) and the kept overrides (B1153's five: 38/41/54/458/
// 656), whose style-drawn landings it rescales.
//
// The JSON rates give each landing's SCHEDULE (peak vs sustain, impact vs
// spellComplete); this rule scales spawnRate and sustainRate together so the
// expected peak (spell_population_model.h) is the target.
// ---------------------------------------------------------------------------
static spell_population::Schedule scheduleOf(const SpellEffectsConfig::StyleSettings& s) {
    spell_population::Schedule c;
    c.spawnRate = s.spawnRate;
    c.burstCount = s.burstCount;
    c.spawnWindow = s.spawnWindow;
    c.sustainRate = s.sustainRate;
    c.sustainFrom = s.sustainFrom;
    c.emitterLifetime = s.emitterLifetime;
    c.lifetimeMin = s.lifetimeMin;
    c.lifetimeMax = s.lifetimeMax;
    return c;
}

void SpellEffectsConfig::applyPool(int pool) {
    if (pool <= 0) {
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: no spell particle pool (preset key "
                  "spellParticlePool missing) -- landing populations stay the file's schedule rates");
        return;
    }
    pool_ = pool;
    if (!configPath_.empty()) {
        load(configPath_);
    } else {
        global_.maxParticles = pool_;
    }
}

void SpellEffectsConfig::applyPopulationRule(const Json::Value& root) {
    static const char* kHalves[2] = {"impact", "spellComplete"};
    auto styleIt0 = styles_.find(kHalves[0]);
    auto styleIt1 = styles_.find(kHalves[1]);
    if (styleIt0 == styles_.end() || styleIt1 == styles_.end()) return;
    const StyleSettings base[2] = {styleIt0->second, styleIt1->second};
    const float bound = 0.5f * static_cast<float>(pool_);

    // B1149 (#469): the per-spell `exceptions` (B1123's hand-fitted peaks for
    // 41/54/656/458) are retired: every spell with a spellsnew.eff record
    // draws its slots' FILE populations; their oracle coverages are bands in
    // tests/spell_fx_population_oracle.h.
    const Json::Value& lp = root["landingPopulation"];
    if (lp.isObject() && lp.isMember("rule"))
        LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: landing population rule: {}", lp["rule"].asString());

    auto scaleInto = [](StyleSettings& out, const StyleSettings& resolved, float k) {
        out.spawnRate = resolved.spawnRate * k;   out.hasSpawnRate = true;
        out.sustainRate = resolved.sustainRate * k; out.hasSustainRate = true;
    };

    // The bound: one landing PLUS its own held Source peaks at half the pool.
    // B1131: the Source keeps emitting its RECORD's hold past OP_Action (the
    // longer of holdOther / holdSelf: the target is not known here), at the
    // style's (or the spell override's) rate, and then its sprites
    // finish their life.  Which Source a spell draws (castGlow or castSpray)
    // is decided at cast time, so the larger overlap of the two counts.
    // B1153 (#473): the rows are retired. A spell that HAS a spellsnew.eff
    // record holds the cast-animation term (EQ::kSpellFxCastAnimationSeconds,
    // B1148) -- what every retired row carried, and what all five kept
    // override spells have (their rows 0/20/23/33/35, anims 80/89/82/121/104 in
    // tests/spell_anim_rows.h); the style landing (id == 0) is the no-record
    // path and keeps the default row's hold.  B1161 (#480): the 253 override
    // is retired, so record 35's (its spellsnew.eff 104) field is no longer
    // among the rescaled landings below.  Computed from the UNSCALED
    // style each spell merges over.
    auto resolve = [&](uint32_t id, const char* style) {
        auto st = styles_.find(style);
        StyleSettings r = st != styles_.end() ? st->second : StyleSettings{};
        auto sp = spellOverrides_.find(id);
        if (id != 0 && sp != spellOverrides_.end()) {
            auto it = sp->second.find(style);
            if (it != sp->second.end()) r = mergeOverrides(r, it->second);
        }
        return r;
    };
    auto factor = [&](uint32_t id, const StyleSettings landing[2], float hold, float* peakOut,
                      float* targetOut, float* sourceScaleOut = nullptr) {
        const std::vector<spell_population::Schedule> halves = {scheduleOf(landing[0]), scheduleOf(landing[1])};
        const float alone = spell_population::PeakLive(halves);
        float k = std::numeric_limits<float>::max(), kSource = 1.0f;
        for (const char* src : {"castGlow", "castSpray"}) {
            spell_population::Schedule sch = scheduleOf(resolve(id, src));
            auto o = spell_population::OverlapOf(halves, landing[0].triggerDelay, sch, hold);
            const float kS = std::min(1.0f, spell_population::ScaleBothToFit(o, bound));
            for (auto& v : o.source) v *= kS;
            kSource = std::min(kSource, kS);
            k = std::min(k, spell_population::ScaleToFit(o, bound));
        }
        if (sourceScaleOut) *sourceScaleOut = kSource;
        *peakOut = alone;
        *targetOut = alone * k;
        return k;
    };

    std::vector<uint32_t> ids;
    spellSourceScale_.clear();
    for (const auto& kv : spellOverrides_) {
        if (kv.second.count(kHalves[0]) || kv.second.count(kHalves[1])) ids.push_back(kv.first);
    }
    struct Scaled { uint32_t id; StyleSettings resolved[2]; float k; };
    std::vector<Scaled> scaled;
    for (uint32_t id : ids) {
        Scaled sc{id, {resolve(id, kHalves[0]), resolve(id, kHalves[1])}, 1.0f};
        float alone = 0.0f, target = 0.0f;
        float kSource = 1.0f;
        sc.k = factor(id, sc.resolved, EQ::kSpellFxCastAnimationSeconds, &alone, &target, &kSource);
        if (alone <= 0.0f) continue;
        spellSourceScale_[id] = kSource;
        scaled.push_back(sc);
        LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: pool {} -> spell {} landing peak {:.0f} -> {:.0f} (x{:.3f})",
                  pool_, id, alone, target, sc.k);
    }
    float styleAlone = 0.0f, styleTarget = 0.0f;
    float kSourceStyle = 1.0f;
    const float kStyle = factor(0, base, sourceDefault_.maxHold(), &styleAlone, &styleTarget, &kSourceStyle);
    recordSourceScale_[-1] = kSourceStyle;
    // B1153 (#473): the per-record rows (and with them the per-record
    // factors) are retired: a record's landing draws its slots' FILE
    // populations (applySlotEmitter, B1149), not the style's scaled rates.
    // B1161 (#480): that is now the whole record path -- the largest field
    // (record 35 / spellsnew.eff 104 / 253: 4914 peak live) is drawn at the
    // file's counts and capped by the pool at runtime (drop while full), not
    // scaled.  The style's own landing (a spell with no record) carries
    // kStyle, the five kept overrides' landings are rescaled above, and a
    // record spell's style-drawn Source (the ones without a spellsnew.eff
    // Source slot) keeps its factor: the style's overlap at the
    // cast-animation hold -- what B1152's recordSourceScale_[record] carried
    // for every row.
    {
        float a = 0.0f, t = 0.0f, kSourceRecord = 1.0f;
        factor(0, base, EQ::kSpellFxCastAnimationSeconds, &a, &t, &kSourceRecord);
        recordSourceScale_[0] = kSourceRecord;
    }

    for (auto& sc : scaled)
        for (int h = 0; h < 2; ++h) scaleInto(spellOverrides_[sc.id][kHalves[h]], sc.resolved[h], sc.k);
    if (styleAlone > 0.0f) {
        scaleInto(styleIt0->second, base[0], kStyle);
        scaleInto(styleIt1->second, base[1], kStyle);
    }
    LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: spell particle pool {} -> a landing and its held Source peak at "
             "<= {:.0f} (style landing {:.0f} -> {:.0f}; {} override spells resolved)",
             pool_, bound, styleAlone, styleTarget, scaled.size());
}

static glm::vec3 readVec3(const Json::Value& arr) {
    if (arr.isArray() && arr.size() >= 3) {
        return glm::vec3(arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat());
    }
    return glm::vec3(0.0f);
}

void SpellEffectsConfig::loadStyle(const Json::Value& json, const std::string& name, StyleSettings& s) {
    if (json.isMember("spawnRate")) { s.spawnRate = json["spawnRate"].asFloat(); s.hasSpawnRate = true; }
    if (json.isMember("burstCount")) { s.burstCount = json["burstCount"].asInt(); s.hasBurstCount = true; }
    if (json.isMember("emitterLifetime")) { s.emitterLifetime = json["emitterLifetime"].asFloat(); s.hasEmitterLifetime = true; }

    if (json.isMember("spawnShape")) { s.spawnShape = json["spawnShape"].asString(); s.hasSpawnShape = true; }
    if (json.isMember("spawnExtents")) { s.spawnExtents = readVec3(json["spawnExtents"]); s.hasSpawnExtents = true; }

    if (json.isMember("motionType")) { s.motionType = json["motionType"].asString(); s.hasMotionType = true; }
    if (json.isMember("velocityBase")) { s.velocityBase = readVec3(json["velocityBase"]); s.hasVelocityBase = true; }
    if (json.isMember("velocitySpread")) { s.velocitySpread = readVec3(json["velocitySpread"]); s.hasVelocitySpread = true; }

    if (json.isMember("gravity")) { s.gravity = readVec3(json["gravity"]); s.hasGravity = true; }
    if (json.isMember("drag")) { s.drag = json["drag"].asFloat(); s.hasDrag = true; }

    if (json.isMember("colorStartAlpha")) { s.colorStartAlpha = json["colorStartAlpha"].asFloat(); s.hasColorStartAlpha = true; }
    if (json.isMember("colorEndAlpha")) { s.colorEndAlpha = json["colorEndAlpha"].asFloat(); s.hasColorEndAlpha = true; }

    if (json.isMember("sizeStartMin")) { s.sizeStartMin = json["sizeStartMin"].asFloat(); s.hasSizeStartMin = true; }
    if (json.isMember("sizeStartMax")) { s.sizeStartMax = json["sizeStartMax"].asFloat(); s.hasSizeStartMax = true; }
    if (json.isMember("sizeEndMin")) { s.sizeEndMin = json["sizeEndMin"].asFloat(); s.hasSizeEndMin = true; }
    if (json.isMember("sizeEndMax")) { s.sizeEndMax = json["sizeEndMax"].asFloat(); s.hasSizeEndMax = true; }
    if (json.isMember("lifetimeMin")) { s.lifetimeMin = json["lifetimeMin"].asFloat(); s.hasLifetimeMin = true; }
    if (json.isMember("lifetimeMax")) { s.lifetimeMax = json["lifetimeMax"].asFloat(); s.hasLifetimeMax = true; }

    if (json.isMember("blendMode")) { s.blendMode = json["blendMode"].asString(); s.hasBlendMode = true; }
    if (json.isMember("textureRegions")) {
        s.textureRegions.clear();
        const Json::Value& tr = json["textureRegions"];
        for (Json::ArrayIndex i = 0; i < tr.size(); ++i) {
            s.textureRegions.push_back(tr[i].asString());
        }
        s.hasTextureRegions = true;
    }

    if (json.isMember("orbitalRadius")) { s.orbitalRadius = json["orbitalRadius"].asFloat(); s.hasOrbitalRadius = true; }
    if (json.isMember("orbitalAngularVelocity")) { s.orbitalAngularVelocity = json["orbitalAngularVelocity"].asFloat(); s.hasOrbitalAngularVelocity = true; }
    if (json.isMember("expandSpeed")) { s.expandSpeed = json["expandSpeed"].asFloat(); s.hasExpandSpeed = true; }

    if (json.isMember("positionOffset")) { s.positionOffset = readVec3(json["positionOffset"]); s.hasPositionOffset = true; }
    if (json.isMember("triggerDelay")) { s.triggerDelay = json["triggerDelay"].asFloat(); s.hasTriggerDelay = true; }
    if (json.isMember("spawnWindow")) { s.spawnWindow = json["spawnWindow"].asFloat(); s.hasSpawnWindow = true; }
    if (json.isMember("sustainRate")) { s.sustainRate = json["sustainRate"].asFloat(); s.hasSustainRate = true; }
    if (json.isMember("sustainFrom")) { s.sustainFrom = json["sustainFrom"].asFloat(); s.hasSustainFrom = true; }

    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Loaded style '{}'", name);
}

SpellEffectsConfig::StyleSettings SpellEffectsConfig::getStyleSettings(
        const std::string& styleName, uint32_t spellId, int record) const {
    // B1153 (#473): no per-record landing scale any more (a record's landing
    // draws its slots' FILE populations, applySlotEmitter, B1149). The spell
    // overrides stay -- the measured no-file clouds, sizes and populations for
    // the spells whose spellsnew file is not the whole story (refactor/
    // B1153/README.md documents each with its numbers). B1161 (#480) retires
    // the 253 one of them.
    // B1161 (#480): `record` adds NOTHING to a landing half. The cast-animation
    // term (EQ::kSpellFxCastAnimationSeconds, B1145/B1148) is the client's
    // SOURCE duration: eqgame passes a Source stage cast_ms + the caster's
    // CastingAnim track (0x49b977 / 0x47ddd4) and a Target stage NO duration
    // (0x47f5e1; SpellFxCallerDurationMs in spell_fx_timing.h), so the record
    // path carries the term in createCastGlow's Source caller duration, never
    // in a landing's emitterLifetime. Record 0 is "no record" (spells_us.txt
    // field 145 = 0, spell_visual_vocabulary.h) and lands exactly as -1 does.
    (void)record;
    // Find base style
    auto baseIt = styles_.find(styleName);
    if (baseIt == styles_.end()) {
        LOG_WARN(MOD_GRAPHICS, "SpellEffectsConfig: Unknown style '{}', returning empty", styleName);
        return StyleSettings{};
    }

    // Check for spell-specific overrides
    if (spellId != 0) {
        auto spellIt = spellOverrides_.find(spellId);
        if (spellIt != spellOverrides_.end()) {
            auto styleIt = spellIt->second.find(styleName);
            if (styleIt != spellIt->second.end()) {
                return mergeOverrides(baseIt->second, styleIt->second);
            }
        }
    }
    return baseIt->second;
}

void SpellEffectsConfig::loadSpellOverrides(const Json::Value& root) {
    spellOverrides_.clear();

    for (const auto& spellIdStr : root.getMemberNames()) {
        uint32_t spellId = static_cast<uint32_t>(std::stoul(spellIdStr));
        const Json::Value& spellJson = root[spellIdStr];

        for (const auto& styleName : spellJson.getMemberNames()) {
            StyleSettings override;
            loadStyle(spellJson[styleName], styleName, override);
            spellOverrides_[spellId][styleName] = override;
        }
    }

    if (!spellOverrides_.empty()) {
        LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Loaded overrides for {} spells",
                 spellOverrides_.size());
    }
}

SpellEffectsConfig::StyleSettings SpellEffectsConfig::mergeOverrides(
        const StyleSettings& base, const StyleSettings& over) {
    StyleSettings result = base;

    if (over.hasSpawnRate) { result.spawnRate = over.spawnRate; result.hasSpawnRate = true; }
    if (over.hasBurstCount) { result.burstCount = over.burstCount; result.hasBurstCount = true; }
    if (over.hasEmitterLifetime) { result.emitterLifetime = over.emitterLifetime; result.hasEmitterLifetime = true; }
    if (over.hasSpawnShape) { result.spawnShape = over.spawnShape; result.hasSpawnShape = true; }
    if (over.hasSpawnExtents) { result.spawnExtents = over.spawnExtents; result.hasSpawnExtents = true; }
    if (over.hasMotionType) { result.motionType = over.motionType; result.hasMotionType = true; }
    if (over.hasVelocityBase) { result.velocityBase = over.velocityBase; result.hasVelocityBase = true; }
    if (over.hasVelocitySpread) { result.velocitySpread = over.velocitySpread; result.hasVelocitySpread = true; }
    if (over.hasGravity) { result.gravity = over.gravity; result.hasGravity = true; }
    if (over.hasDrag) { result.drag = over.drag; result.hasDrag = true; }
    if (over.hasColorStartAlpha) { result.colorStartAlpha = over.colorStartAlpha; result.hasColorStartAlpha = true; }
    if (over.hasColorEndAlpha) { result.colorEndAlpha = over.colorEndAlpha; result.hasColorEndAlpha = true; }
    if (over.hasSizeStartMin) { result.sizeStartMin = over.sizeStartMin; result.hasSizeStartMin = true; }
    if (over.hasSizeStartMax) { result.sizeStartMax = over.sizeStartMax; result.hasSizeStartMax = true; }
    if (over.hasSizeEndMin) { result.sizeEndMin = over.sizeEndMin; result.hasSizeEndMin = true; }
    if (over.hasSizeEndMax) { result.sizeEndMax = over.sizeEndMax; result.hasSizeEndMax = true; }
    if (over.hasLifetimeMin) { result.lifetimeMin = over.lifetimeMin; result.hasLifetimeMin = true; }
    if (over.hasLifetimeMax) { result.lifetimeMax = over.lifetimeMax; result.hasLifetimeMax = true; }
    if (over.hasBlendMode) { result.blendMode = over.blendMode; result.hasBlendMode = true; }
    if (over.hasTextureRegions) { result.textureRegions = over.textureRegions; result.hasTextureRegions = true; }
    if (over.hasOrbitalRadius) { result.orbitalRadius = over.orbitalRadius; result.hasOrbitalRadius = true; }
    if (over.hasOrbitalAngularVelocity) { result.orbitalAngularVelocity = over.orbitalAngularVelocity; result.hasOrbitalAngularVelocity = true; }
    if (over.hasExpandSpeed) { result.expandSpeed = over.expandSpeed; result.hasExpandSpeed = true; }
    if (over.hasPositionOffset) { result.positionOffset = over.positionOffset; result.hasPositionOffset = true; }
    if (over.hasTriggerDelay) { result.triggerDelay = over.triggerDelay; result.hasTriggerDelay = true; }
    if (over.hasSpawnWindow) { result.spawnWindow = over.spawnWindow; result.hasSpawnWindow = true; }
    if (over.hasSustainRate) { result.sustainRate = over.sustainRate; result.hasSustainRate = true; }
    if (over.hasSustainFrom) { result.sustainFrom = over.sustainFrom; result.hasSustainFrom = true; }

    return result;
}

// ---------------------------------------------------------------------------
// B1153 (#473): THE SOURCE ROW IS THE DEFAULT ROW.  B1122 drew every record's
// Source as one body cloud; the B1130 survey measured all 41 records the
// level-1-60 spells use (review/B1130/anchor_table.tsv); B1149-B1151 retired
// the rows' words one by one (population, shape, timing) as the
// spellsnew.edd reader supplied them; B1153 retires the rows themselves --
// the measured anchor classes and holds are the band in
// tests/spell_fx_row_oracle.h.  What stays: the NO-RECORD fallback, a spell
// whose spellsnew.eff field 145 is absent draws this default row (the style's
// body cloud on the caster, the B1130 median onset, B1122's holds).
// createCastGlow builds its emitters; releaseCastGlow applies the hold
// (record spells run their own spellsnew.edd durations, B1145).
// ---------------------------------------------------------------------------
static bool parseAnchor(const std::string& a, SpellEffectsConfig::SourceAnchor& out) {
    using A = SpellEffectsConfig::SourceAnchor;
    if (a == "body") { out = A::Body; return true; }
    if (a == "hands") { out = A::Hands; return true; }
    if (a == "feet-ring") { out = A::FeetRing; return true; }
    if (a == "orbit") { out = A::Orbit; return true; }
    if (a == "head") { out = A::Head; return true; }
    return false;
}

void SpellEffectsConfig::loadSourceRecords(const Json::Value& root) {
    recordSourceScale_.clear();
    sourceDefault_ = SourceRecord{};
    sourceScale_ = SourceScale{};
    const Json::Value& sr = root["sourceRecords"];
    if (!sr.isObject()) {
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: no `sourceRecords` block -- every Source draws the "
                  "style's body cloud with no onset or hold (B1131)");
        return;
    }
    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Source rule: {} | default: {}",
              sr["rule"].asString(), sr["defaultWhy"].asString());
    const Json::Value& sc = sr["scale"];
    if (sc.isMember("anchorAboveFeet")) sourceScale_.anchorAboveFeet = sc["anchorAboveFeet"].asFloat();
    // The measured block carries its reason ("why"); it is logged here.
    if (sc.isMember("why")) LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Source scale: {}", sc["why"].asString());

    auto readRow = [](const Json::Value& j, SourceRecord& r) {
        if (j.isMember("anchor") && !parseAnchor(j["anchor"].asString(), r.anchor))
            LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: unknown Source anchor '{}'", j["anchor"].asString());
        if (j.isMember("onset")) r.onset = j["onset"].asFloat();
        if (j.isMember("holdSelf")) r.holdSelf = j["holdSelf"].asFloat();
        // A self-only spell has no other-target hold: its Source never has another target.
        if (j.isMember("holdOther")) r.holdOther = j["holdOther"].asFloat();
        else if (j.isMember("holdSelf")) r.holdOther = r.holdSelf;
    };
    readRow(sr["default"], sourceDefault_);

    // B1153 (#473): the per-record measured rows (`records`) and the pool
    // rule's `overrideRecords` are retired: every record is drawn by its
    // spellsnew.eff record's EDD emitters (B1149-B1151) and the measured
    // anchor classes / holds are the band in tests/spell_fx_row_oracle.h.
    // The default row is the only row the runtime carries.
    if (sr.isMember("records") || sr.isMember("overrideRecords"))
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: `sourceRecords.records` / `overrideRecords` are retired "
                  "(B1153) -- the bands in tests/spell_fx_row_oracle.h are the source of truth; ignoring them");
    LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Source default row (B1153: rows retired), onset {:.2f} s, "
             "hold other/self {:.2f}/{:.2f} s", sourceDefault_.onset,
             sourceDefault_.holdOther, sourceDefault_.holdSelf);
}

float SpellEffectsConfig::targetStageLatency() const {
    auto it = styles_.find("spellComplete");
    return it != styles_.end() && it->second.hasTriggerDelay ? it->second.triggerDelay : 0.0f;
}

const SpellEffectsConfig::SourceRecord& SpellEffectsConfig::sourceRecord(int record) const {
    (void)record;  // B1153: the rows are retired -- the default row
    return sourceDefault_;
}

float SpellEffectsConfig::sourceHold(int record, bool targetIsSelf) const {
    // B1153 (#473): the per-record rows are retired. A spell that HAS a
    // spellsnew.eff record (record > 0) holds its Source for the caster's
    // cast-animation term past OP_Action (B1148: the Source includes the
    // caster's CastingAnim track) -- the 4.35 s every retired row carried;
    // its band is in tests/spell_fx_row_oracle.h. A spell with NO record
    // (record <= 0) draws the no-record fallback row's measured holds
    // (B1122: holdOther with another target, holdSelf on the caster).
    if (record > 0)
        return EQ::kSpellFxCastAnimationSeconds;
    const SourceRecord& r = sourceRecord(record);
    return targetIsSelf ? r.holdSelf : r.holdOther;
}

float SpellEffectsConfig::sourcePoolScale(uint32_t spellId, int record) const {
    // B1153 (#473): a landing override keeps its own factor; otherwise the
    // factor of the Source the spell draws -- the style's (a spell with no
    // spellsnew.eff record) or the record's (the style's overlap at the
    // cast-animation hold, recordSourceScale_[0]).
    auto sp = spellSourceScale_.find(spellId);
    if (spellId != 0 && sp != spellSourceScale_.end()) return sp->second;
    const int key = record > 0 ? 0 : -1;
    auto r = recordSourceScale_.find(key);
    return r != recordSourceScale_.end() ? r->second : 1.0f;
}

bool SpellEffectsConfig::hasSourceShapeOverride(uint32_t spellId, const std::string& styleName) const {
    auto sp = spellOverrides_.find(spellId);
    if (spellId == 0 || sp == spellOverrides_.end()) return false;
    auto it = sp->second.find(styleName);
    return it != sp->second.end() && it->second.hasSpawnShape;
}

static bool parseTargetAnchor(const std::string& a, SpellEffectsConfig::TargetAnchor& out) {
    using T = SpellEffectsConfig::TargetAnchor;
    if (a == "body") { out = T::Body; return true; }
    if (a == "caster") { out = T::Caster; return true; }
    if (a == "head") { out = T::Head; return true; }
    if (a == "feet") { out = T::FeetGround; return true; }
    if (a == "column") { out = T::Column; return true; }
    return false;
}

void SpellEffectsConfig::loadTargetRecords(const Json::Value& root) {
    targetDefault_ = TargetRecord{};
    const Json::Value& tr = root["targetRecords"];
    if (!tr.isObject()) {
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: no `targetRecords` block -- every landing draws the "
                  "style's body volume on the target (B1132)");
        return;
    }
    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Target rule: {} | default: {}", tr["rule"].asString(),
              tr["defaultWhy"].asString());
    auto readRow = [](const Json::Value& j, TargetRecord& r) {
        if (j.isMember("anchor") && !parseTargetAnchor(j["anchor"].asString(), r.anchor))
            LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: unknown Target anchor '{}'", j["anchor"].asString());
    };
    readRow(tr["default"], targetDefault_);

    // B1153 (#473): the per-record measured rows (`records`) are retired:
    // every record lands as its spellsnew.eff Target slots (B1144, B1152) and
    // the measured anchor classes / extents are the bands in
    // tests/spell_fx_row_oracle.h and tests/spell_fx_shape_oracle.h.
    // The default row is the only row the runtime carries.
    if (tr.isMember("records"))
        LOG_ERROR(MOD_GRAPHICS, "SpellEffectsConfig: `targetRecords.records` is retired (B1153) -- the "
                  "bands in tests/spell_fx_row_oracle.h are the source of truth; ignoring it");
    LOG_INFO(MOD_GRAPHICS, "SpellEffectsConfig: Target default row (B1153: rows retired)");
}

const SpellEffectsConfig::TargetRecord& SpellEffectsConfig::targetRecord(int record) const {
    (void)record;  // B1153: the rows are retired -- the default row
    return targetDefault_;
}

glm::vec4 SpellEffectsConfig::getResistColor(uint8_t resistType) const {
    switch (resistType) {
        case 1: return getResistColor("magic");
        case 2: return getResistColor("fire");
        case 3: return getResistColor("cold");
        case 4: return getResistColor("poison");
        case 5: return getResistColor("disease");
        default: return getResistColor("none");
    }
}

glm::vec4 SpellEffectsConfig::getResistColor(const std::string& name) const {
    auto it = resistColors_.find(name);
    if (it != resistColors_.end()) {
        return it->second;
    }
    // Fallback: light blue default
    return glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
}

// Helper to set all has* flags to true for a fully-populated StyleSettings
static void markAllPresent(SpellEffectsConfig::StyleSettings& s) {
    s.hasSpawnRate = true;
    s.hasBurstCount = true;
    s.hasEmitterLifetime = true;
    s.hasSpawnShape = true;
    s.hasSpawnExtents = true;
    s.hasMotionType = true;
    s.hasVelocityBase = true;
    s.hasVelocitySpread = true;
    s.hasGravity = true;
    s.hasDrag = true;
    s.hasColorStartAlpha = true;
    s.hasColorEndAlpha = true;
    s.hasSizeStartMin = true;
    s.hasSizeStartMax = true;
    s.hasSizeEndMin = true;
    s.hasSizeEndMax = true;
    s.hasLifetimeMin = true;
    s.hasLifetimeMax = true;
    s.hasBlendMode = true;
    s.hasTextureRegions = true;
    s.hasOrbitalRadius = true;
    s.hasOrbitalAngularVelocity = true;
    s.hasExpandSpeed = true;
    s.hasPositionOffset = true;
}

void SpellEffectsConfig::setDefaults() {
    global_.enabled = true;
    global_.maxParticles = pool_;
    global_.densityMultiplier = 1.0f;

    // Resist colors
    resistColors_["none"] = glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
    resistColors_["magic"] = glm::vec4(0.78f, 0.39f, 1.0f, 1.0f);
    resistColors_["fire"] = glm::vec4(1.0f, 0.47f, 0.2f, 1.0f);
    resistColors_["cold"] = glm::vec4(0.39f, 0.71f, 1.0f, 1.0f);
    resistColors_["poison"] = glm::vec4(0.31f, 0.78f, 0.31f, 1.0f);
    resistColors_["disease"] = glm::vec4(0.59f, 0.39f, 0.2f, 1.0f);
    resistColors_["chromatic"] = glm::vec4(0.59f, 0.71f, 1.0f, 1.0f);
    resistColors_["corruption"] = glm::vec4(0.4f, 0.1f, 0.5f, 1.0f);

    styles_.clear();

    // castSpray
    {
        StyleSettings& s = styles_["castSpray"];
        s.spawnRate = 15.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "LINEAR";
        s.velocityBase = glm::vec3(4.0f, 0.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.8f, 0.8f, 0.8f);
        s.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
        s.drag = 0.3f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.04f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.02f;
        s.sizeEndMax = 0.04f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.7f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // castGlow
    {
        StyleSettings& s = styles_["castGlow"];
        s.spawnRate = 8.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "ORBITAL";
        s.velocityBase = glm::vec3(0.0f, 2.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.0f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.7f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.08f;
        s.sizeStartMax = 0.12f;
        s.sizeEndMin = 0.04f;
        s.sizeEndMax = 0.06f;
        s.lifetimeMin = 0.8f;
        s.lifetimeMax = 1.2f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 1.5f;
        s.orbitalAngularVelocity = 4.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f, 0.5f, 0.0f);
        markAllPresent(s);
    }

    // spellComplete
    {
        StyleSettings& s = styles_["spellComplete"];
        s.spawnRate = 0.0f;
        s.burstCount = 15;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "RING";
        s.spawnExtents = glm::vec3(0.5f, 0.0f, 0.0f);
        s.motionType = "BURST";
        s.velocityBase = glm::vec3(3.0f, 1.5f, 0.0f);
        s.velocitySpread = glm::vec3(1.0f, 0.5f, 1.0f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.9f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.06f;
        s.sizeStartMax = 0.10f;
        s.sizeEndMin = 0.12f;
        s.sizeEndMax = 0.18f;
        s.lifetimeMin = 0.5f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SpellSparkle", "StarShape"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // impact
    {
        StyleSettings& s = styles_["impact"];
        s.spawnRate = 0.0f;
        s.burstCount = 12;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "RADIAL_EXPAND";
        s.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.05f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.10f;
        s.sizeEndMax = 0.15f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 4.0f;
        s.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);
        markAllPresent(s);
    }

    // projectileTrail
    {
        StyleSettings& s = styles_["projectileTrail"];
        s.spawnRate = 25.0f;
        s.burstCount = 0;
        s.emitterLifetime = 0.0f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "LINEAR";
        s.velocityBase = glm::vec3(0.0f, 0.3f, 0.0f);
        s.velocitySpread = glm::vec3(0.3f, 0.3f, 0.3f);
        s.gravity = glm::vec3(0.0f);
        s.drag = 2.0f;
        s.colorStartAlpha = 0.9f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.06f;
        s.sizeStartMax = 0.10f;
        s.sizeEndMin = 0.02f;
        s.sizeEndMax = 0.04f;
        s.lifetimeMin = 0.2f;
        s.lifetimeMax = 0.4f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 0.0f;
        s.positionOffset = glm::vec3(0.0f);
        markAllPresent(s);
    }

    // projectileImpact
    {
        StyleSettings& s = styles_["projectileImpact"];
        s.spawnRate = 0.0f;
        s.burstCount = 12;
        s.emitterLifetime = 0.1f;
        s.spawnShape = "POINT";
        s.spawnExtents = glm::vec3(0.0f);
        s.motionType = "RADIAL_EXPAND";
        s.velocityBase = glm::vec3(0.0f, 1.0f, 0.0f);
        s.velocitySpread = glm::vec3(0.5f, 0.5f, 0.5f);
        s.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
        s.drag = 0.0f;
        s.colorStartAlpha = 0.8f;
        s.colorEndAlpha = 0.0f;
        s.sizeStartMin = 0.05f;
        s.sizeStartMax = 0.08f;
        s.sizeEndMin = 0.10f;
        s.sizeEndMax = 0.15f;
        s.lifetimeMin = 0.4f;
        s.lifetimeMax = 0.8f;
        s.blendMode = "ADDITIVE";
        s.textureRegions = {"SoftCircle", "Ember"};
        s.orbitalRadius = 0.0f;
        s.orbitalAngularVelocity = 0.0f;
        s.expandSpeed = 4.0f;
        s.positionOffset = glm::vec3(0.0f, 1.0f, 0.0f);
        markAllPresent(s);
    }

    LOG_DEBUG(MOD_GRAPHICS, "SpellEffectsConfig: Using default settings");
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
