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
     * B1123 (#443) as re-derived by B1153 (#473) and B1161 (#480): set the
     * spell particle pool (the platform's preset value,
     * ConstrainedRendererConfig::spellParticlePool) and derive the landing
     * populations from it by the one pool rule (spell_effects_config.cpp,
     * applyPopulationRule).  The oracle's pool is a CAP, not a scaler: a
     * landing drawn from a spellsnew.eff record (or from one of the five kept
     * spellOverrides) is emitted at the file's / the override's own counts
     * and the pool drops the rest once it is full (EQGraphicsDX9's per-
     * emitter rings, refactor/B1149/README.md sec. 4) -- the rule scales none
     * of them.  The half-pool bound is the no-record style landing's (two
     * overlapping ones fit).  Re-reads the loaded file, so it may be called
     * again (a re-sized pool).
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

        // B1131: the Source's anchor, onset and hold are per measured row
        // (B1137: the spell's field-145 record, tests/spell_anim_rows.h)
        // (SourceRecord below), no longer style keys (B1122's `anchor` /
        // `releaseDelay` were one rule for every record). B1153 (#473): the
        // per-record rows are retired -- the runtime carries the default row
        // only, and the band is tests/spell_fx_row_oracle.h.
    };

    /**
     * Get merged style settings for a given style name and optional spell ID.
     * If spellId is non-zero and has overrides, those fields replace base style values.
     *
     * `record` is the spell's spellsnew.eff record (B1137: spells_us.txt
     * field 145, the vocabulary's spellAnim; 0 = none, -1 = not given; a
     * record exists only for record > 0). B1153 (#473) retired the per-record
     * landing scale and B1161 (#480) confirmed the parameter adds nothing to
     * any style: a record's landing is its spellsnew.edd slots
     * (applySlotEmitter), and the cast-animation term is the Source stage's
     * caller duration (spell_fx_timing.h), never a landing's lifetime -- the
     * client passes a Target stage no duration (eqgame 0x47f5e1). Kept for
     * the callers' signature; record 0, -1 and any record give the same
     * settings (SpellStyleOverrides.RecordZeroIsTheNoRecordPath).
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
        // B1153 (#473): the survey's per-record measured rows are retired --
        // the runtime carries the DEFAULT row only, and every record is drawn
        // by its spellsnew.eff record's EDD emitters (B1149 population/sprite,
        // B1150 shape, B1151 motion). The retired row words (hasShape,
        // lo/hi/halfWidth/centroid, spiral, disc*, measuredAs) are gone; the
        // measured anchor classes and holds are the band in
        // tests/spell_fx_row_oracle.h, the timings tests/spell_fx_timing_oracle.h.
        // B1145 (#465): a spellsnew.eff Source takes its timing from the file
        // per slot (SpellPresets::applySlotEmitter); the default row's own
        // measured onset / holds stay: a spell with no spellsnew.eff record
        // (field 145 = 0) draws them.
        float onset = 0.0f;         // seconds after BeginCast
        float holdOther = 0.0f;     // seconds emitting past OP_Action, target = another entity
        float holdSelf = 0.0f;      // ... target = the caster
        int record = -1;            // the row this is (-1 = the default row)
        float maxHold() const { return holdOther > holdSelf ? holdOther : holdSelf; }
    };
    struct SourceScale {
        float anchorAboveFeet = 3.6f;    // entity anchor above the feet, world units
    };
    /** The record's row, or the default row. B1153: the rows are retired, so
     *  this is always the default row (a spell with no spellsnew.eff record). */
    const SourceRecord& sourceRecord(int record) const;
    const SourceScale& sourceScale() const { return sourceScale_; }
    /** The hold past OP_Action for this record and target. B1153 (#473): a
     *  spell that HAS a spellsnew.eff record (record > 0) holds the
     *  cast-animation term (B1148); a spell with none keeps the default
     *  row's measured holds (B1122: other target vs the caster). */
    float sourceHold(int record, bool targetIsSelf) const;
    /**
     * B1145: the stages' measured start -- the delay from the packet to the
     * first emission, which no spellsnew.edd word holds (the first slot of
     * every measured record has +0x8C = 0): the Source from OP_BeginCast is
     * the default row's onset (the median of the 40 B1130 onsets), the Target
     * from OP_Action the landing styles' triggerDelay (B1121/B1123).
     */
    float sourceStageLatency() const { return sourceDefault_.onset; }
    float targetStageLatency() const;
    /** B1131: the pool rule's factor on this spell's held Source (1 = its own rate). */
    float sourcePoolScale(uint32_t spellId, int record) const;
    /** True when spellOverrides gives this spell's Source style its own geometry. */
    bool hasSourceShapeOverride(uint32_t spellId, const std::string& styleName) const;
    /** B1153: the measured rows are retired -- 0 rows loaded, always. */
    size_t sourceRecordCount() const { return 0; }

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
        // B1153 (#473): the survey's per-record measured rows are retired --
        // the runtime carries the DEFAULT row (the style's volume on the
        // target), and every record lands as its spellsnew.eff Target slots
        // (B1144, B1152). The retired row words (hasShape, lo/hi/halfWidth/
        // centroid, onset, faceOnset, end, boneAt, unmeasured) are gone; the
        // measured anchor classes and extents are the bands in
        // tests/spell_fx_row_oracle.h and tests/spell_fx_shape_oracle.h.
        int record = -1;            // -1 = the default row
    };
    /** The record's landing row. B1153: always the default row. */
    const TargetRecord& targetRecord(int record) const;
    /** B1153: the measured rows are retired -- 0 rows loaded, always. */
    size_t targetRecordCount() const { return 0; }

    /**
     * B1153 (#473): the measured row a spell is drawn with -- the field-145
     * record -> row mapping (B1137) -- is retired with the rows: the runtime
     * carries the default rows only, and the mapping is the band in
     * tests/spell_anim_rows.h (pinned by test_spell_anim_rows).
     */

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
    void buildTextureNameMap();
    void applyPopulationRule(const Json::Value& root);
    void loadSourceRecords(const Json::Value& root);
    void loadTargetRecords(const Json::Value& root);
    void loadSpellOverrides(const Json::Value& root);
    static StyleSettings mergeOverrides(const StyleSettings& base, const StyleSettings& over);

    std::string configPath_;
    bool loaded_ = false;
    int pool_ = 0;  // B1123: the platform pool, kept across reload()

    GlobalSettings global_;
    std::unordered_map<std::string, StyleSettings> styles_;
    std::unordered_map<std::string, glm::vec4> resistColors_;
    std::unordered_map<std::string, uint8_t> textureNameMap_;
    // B1153 (#473): the per-record ROWS are retired (the bands in
    // tests/spell_fx_row_oracle.h); the runtime carries the default rows and
    // the pool rule's factor on the held Source (applyPopulationRule).
    // The six B1120/B1122 spellOverrides are KEPT (the file is not the whole
    // story; refactor/B1153/README.md documents each with before/after
    // numbers) -- spellOverrides_[spellId][styleName] = sparse StyleSettings.
    SourceRecord sourceDefault_;
    SourceScale sourceScale_;
    std::unordered_map<int, float> recordSourceScale_;      // -1 = no record, 0 = the record's factor
    std::unordered_map<uint32_t, std::unordered_map<std::string, StyleSettings>> spellOverrides_;
    std::unordered_map<uint32_t, float> spellSourceScale_;  // spells with a landing override
    TargetRecord targetDefault_;

    ReloadCallback reloadCallback_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
