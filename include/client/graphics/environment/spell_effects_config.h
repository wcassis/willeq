#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

// Forward declaration
namespace Json {
class Value;
}

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * SpellEffectsConfig - JSON-based configuration for spell particle effects.
 *
 * Follows the EnvironmentEffectsConfig singleton pattern.
 * Provides 3-tier config hierarchy: global, per-style, per-spell overrides.
 */
class SpellEffectsConfig {
public:
    static SpellEffectsConfig& instance();

    /**
     * Load configuration from a JSON file.
     * @param path Path to the JSON config file
     * @return true if loaded successfully
     */
    bool load(const std::string& path);

    /**
     * Reload configuration from the last loaded path.
     * @return true if reloaded successfully
     */
    bool reload();

    /**
     * B1123 (#443): set the spell particle pool (the platform's preset value,
     * ConstrainedRendererConfig::spellParticlePool) and derive every landing
     * population from it by the one pool rule (spell_effects_config.cpp,
     * applyPopulationRule): each landing peaks at half the pool -- two
     * overlapping landings, or 253's field beside one, fit -- unless
     * `landingPopulation.exceptions` names a smaller, oracle-measured peak.
     * Re-reads the loaded file, so it may be called again (a re-sized pool).
     */
    void applyPool(int pool);

    /**
     * Get the path to the config file.
     */
    const std::string& getConfigPath() const { return configPath_; }

    /**
     * Global settings for the spell particle system.
     */
    struct GlobalSettings {
        bool enabled = true;
        // B1123 (#443): the spell particle pool IN EFFECT -- written only by
        // applyPool() from the platform's preset key `spellParticlePool`
        // (config/constrained_presets.json). spell_effects.json no longer
        // carries a pool; 0 = no pool applied yet (the worker then falls back).
        int maxParticles = 0;
        float densityMultiplier = 1.0f;
    };

    /**
     * Per-style settings. Each field has a companion has* flag
     * to support sparse per-spell overrides.
     */
    struct StyleSettings {
        float spawnRate = 0.0f;           bool hasSpawnRate = false;
        int burstCount = 0;               bool hasBurstCount = false;
        float emitterLifetime = 0.0f;     bool hasEmitterLifetime = false;
        // B1121: EmitterConfig's two-phase emission (spawnRate's window, then a
        // sustain that feeds the landing to the oracle's measured end).
        float spawnWindow = 0.0f;         bool hasSpawnWindow = false;
        float sustainRate = 0.0f;         bool hasSustainRate = false;
        float sustainFrom = 0.0f;         bool hasSustainFrom = false;

        std::string spawnShape;           bool hasSpawnShape = false;
        glm::vec3 spawnExtents{0.0f};     bool hasSpawnExtents = false;

        std::string motionType;           bool hasMotionType = false;
        glm::vec3 velocityBase{0.0f};     bool hasVelocityBase = false;
        glm::vec3 velocitySpread{0.0f};   bool hasVelocitySpread = false;

        glm::vec3 gravity{0.0f};          bool hasGravity = false;
        float drag = 0.0f;               bool hasDrag = false;

        float colorStartAlpha = 1.0f;    bool hasColorStartAlpha = false;
        float colorEndAlpha = 0.0f;      bool hasColorEndAlpha = false;

        float sizeStartMin = 0.0f;       bool hasSizeStartMin = false;
        float sizeStartMax = 0.0f;       bool hasSizeStartMax = false;
        float sizeEndMin = 0.0f;         bool hasSizeEndMin = false;
        float sizeEndMax = 0.0f;         bool hasSizeEndMax = false;
        float lifetimeMin = 0.0f;        bool hasLifetimeMin = false;
        float lifetimeMax = 0.0f;        bool hasLifetimeMax = false;

        std::string blendMode;           bool hasBlendMode = false;
        std::vector<std::string> textureRegions;  bool hasTextureRegions = false;

        float orbitalRadius = 0.0f;      bool hasOrbitalRadius = false;
        float orbitalAngularVelocity = 0.0f;  bool hasOrbitalAngularVelocity = false;
        float expandSpeed = 0.0f;        bool hasExpandSpeed = false;

        glm::vec3 positionOffset{0.0f};  bool hasPositionOffset = false;

        // B1120: seconds between the effect's creation and its emitter starting
        // (SpellTrigger::DELAYED). The official client's landing becomes visible
        // Act+0.40-0.80 s after OP_Action (B1118 sec. 4); a volume emitter that
        // starts at once crosses the visibility threshold ~0.1 s after it.
        float triggerDelay = 0.0f;       bool hasTriggerDelay = false;

        // B1131: the Source's anchor, onset and hold are per spells.eff record
        // (SourceRecord below), no longer style keys (B1122's `anchor` /
        // `releaseDelay` were one rule for every record).
    };

    /**
     * Get merged style settings for a given style name and optional spell ID.
     * If spellId is non-zero and has overrides, those fields replace base style values.
     *
     * B1131: `record` is the spell's spells.eff record (spells_us.txt field
     * 123, -1 = none). A landing half ("impact" / "spellComplete") the spell
     * does not override is the style scaled for THAT record's held Source
     * (applyPool: a landing plus its own held Source peaks at half the pool).
     */
    StyleSettings getStyleSettings(const std::string& styleName, uint32_t spellId = 0,
                                   int record = -1) const;

    /**
     * B1131 (#451): the Source (cast) stage of one spells.eff record, measured
     * on the official client by the B1130 survey (review/B1130/anchor_table.tsv,
     * record_table.tsv; config `sourceRecords`). Heights and widths are in
     * caster heights above the feet (sourceScale() converts).
     */
    enum class SourceAnchor : uint8_t { Body, Hands, FeetRing, Orbit, Head };
    struct SourceRecord {
        SourceAnchor anchor = SourceAnchor::Body;
        bool hasShape = false;      // false: the castGlow/castSpray style's own geometry
        float lo = 0.0f, hi = 0.0f; // lit extent, caster heights above the feet
        float halfWidth = 0.0f;     // lateral half-span (orbit/ring radius), caster heights
        float centroid = 0.0f;      // lit centroid height, caster heights
        bool spiral = false;        // orbit: born at lo, rising to hi over one sprite life
        // head: the halo disc (band discLo..discHi, radius discHalfWidth) gets
        // discShare of the sprites; the rest fall from it to lo.
        float discLo = 0.0f, discHi = 0.0f, discHalfWidth = 0.0f, discShare = 0.0f;
        float coverage = 0.0f;      // the oracle's Source peak coverage, caster heights^2
        float spawnRate = 0.0f;     // `population` rule (0 = the style's rate)
        float onset = 0.0f;         // seconds after BeginCast
        float holdOther = 0.0f;     // seconds emitting past OP_Action, target = another entity
        float holdSelf = 0.0f;      // ... target = the caster
        int record = -1;            // the row this is (-1 = the default row)
        int measuredAs = -1;        // an unmeasured record's stand-in (51 -> 13)
        float maxHold() const { return holdOther > holdSelf ? holdOther : holdSelf; }
    };
    struct SourceScale {
        float casterHeight = 6.5f;       // world units (B1120)
        float anchorAboveFeet = 3.6f;    // entity anchor above the feet, world units
        float orbitAngularVelocity = 3.0f;
    };
    /** The record's row, or the default row for a record the survey did not reach. */
    const SourceRecord& sourceRecord(int record) const;
    const SourceScale& sourceScale() const { return sourceScale_; }
    /** The hold past OP_Action for this record and target. */
    float sourceHold(int record, bool targetIsSelf) const;
    /** True when spellOverrides gives this spell's Source style its own geometry. */
    bool hasSourceShapeOverride(uint32_t spellId, const std::string& styleName) const;
    /** True when spellOverrides gives this spell's Source style its own rate. */
    bool hasSourceRateOverride(uint32_t spellId, const std::string& styleName) const;
    /** spells.eff record of a spell with an override/exception row (-1 = not listed). */
    int overrideRecord(uint32_t spellId) const;
    /** B1131: the pool rule's factor on this spell's held Source (1 = its own rate). */
    float sourcePoolScale(uint32_t spellId, int record) const;
    /** Number of measured rows loaded (tests). */
    size_t sourceRecordCount() const { return sourceRecords_.size(); }

    /**
     * B1132 (#452): the Target (landing) stage of one spells.eff record,
     * measured on the official client by the B1130 survey (config
     * `targetRecords`; review/B1132/scripts/gen_target_records.py). Heights
     * and widths are in TARGET heights above the target's feet (the same
     * B1120 scale, sourceScale()). Body is the style's B1120 volume on the
     * target; Caster the same volume round the caster whatever the target
     * (record 27's PB-AE, and the self-only records); Head a cloud at the
     * target's head bone; FeetGround rings on the ground at the target's
     * feet; Column a sheet falling over the target from above its head.
     */
    enum class TargetAnchor : uint8_t { Body, Caster, Head, FeetGround, Column };
    struct TargetRecord {
        TargetAnchor anchor = TargetAnchor::Body;
        bool hasShape = false;      // true: Head / FeetGround / Column draw lo..hi x halfWidth
        float lo = 0.0f, hi = 0.0f; // landing run p5..p95 height, target heights above the feet
        float halfWidth = 0.0f;     // lateral half-span (p5..p95 / 2), target heights
        float centroid = 0.0f;      // landing run centroid, target heights
        float coverage = 0.0f;      // the oracle's landing coverage
        float onset = 0.0f;         // Act+ s the oracle's landing rises (RISE rule)
        float faceOnset = 0.0f;     // Head: Act+ s the glow lights the face (0 = none)
        float end = 0.0f;           // Act+ s the oracle's landing ends
        float population = 1.0f;    // share of the style landing (shaped rows)
        float boneAt = 0.0f;        // Head: the height the head bone stands for (target heights)
        bool unmeasured = false;    // 40/51: the default row stands in
        int record = -1;            // -1 = the default row
    };
    /** The record's landing row, or the default row (the style volume on the target). */
    const TargetRecord& targetRecord(int record) const;
    size_t targetRecordCount() const { return targetRecords_.size(); }
    /** The record-0 landing coverage the shaped rows' population is relative to. */
    float targetReferenceCoverage() const { return targetReferenceCoverage_; }

    /**
     * Get global settings.
     */
    const GlobalSettings& getGlobal() const { return global_; }

    /**
     * Get resist color by numeric type.
     */
    glm::vec4 getResistColor(uint8_t resistType) const;

    /**
     * Get resist color by name.
     */
    glm::vec4 getResistColor(const std::string& name) const;

    /**
     * Get atlas texture index by name.
     * Returns 0 (SoftCircle) if name not found.
     */
    uint8_t getTextureIndex(const std::string& name) const;

    /**
     * Check if config has been loaded.
     */
    bool isLoaded() const { return loaded_; }

    /**
     * Callback invoked when config is reloaded.
     */
    using ReloadCallback = std::function<void()>;
    void setReloadCallback(ReloadCallback cb) { reloadCallback_ = cb; }

private:
    SpellEffectsConfig();
    ~SpellEffectsConfig() = default;

    // Non-copyable
    SpellEffectsConfig(const SpellEffectsConfig&) = delete;
    SpellEffectsConfig& operator=(const SpellEffectsConfig&) = delete;

    void setDefaults();
    void loadStyle(const Json::Value& json, const std::string& name, StyleSettings& settings);
    void loadSpellOverrides(const Json::Value& root);
    static StyleSettings mergeOverrides(const StyleSettings& base, const StyleSettings& over);
    void buildTextureNameMap();
    void applyPopulationRule(const Json::Value& root);
    void loadSourceRecords(const Json::Value& root);
    void loadTargetRecords(const Json::Value& root);

    std::string configPath_;
    bool loaded_ = false;
    int pool_ = 0;  // B1123: the platform pool, kept across reload()

    GlobalSettings global_;
    std::unordered_map<std::string, StyleSettings> styles_;
    // spellOverrides_[spellId][styleName] = sparse StyleSettings
    std::unordered_map<uint32_t, std::unordered_map<std::string, StyleSettings>> spellOverrides_;
    std::unordered_map<std::string, glm::vec4> resistColors_;
    std::unordered_map<std::string, uint8_t> textureNameMap_;
    // B1131: per-record Source rows, the default row, and the per-record
    // factor on the style landing (applyPopulationRule).
    std::unordered_map<int, SourceRecord> sourceRecords_;
    SourceRecord sourceDefault_;
    SourceScale sourceScale_;
    std::unordered_map<uint32_t, int> overrideRecords_;
    std::unordered_map<int, float> recordLandingScale_;
    std::unordered_map<int, float> recordSourceScale_;      // -1 = the default row
    std::unordered_map<uint32_t, float> spellSourceScale_;  // spells with a landing override
    // B1132: per-record Target (landing) rows.
    std::unordered_map<int, TargetRecord> targetRecords_;
    TargetRecord targetDefault_;
    float targetReferenceCoverage_ = 0.0f;

    ReloadCallback reloadCallback_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
