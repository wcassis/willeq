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
     * Get the path to the config file.
     */
    const std::string& getConfigPath() const { return configPath_; }

    /**
     * Global settings for the spell particle system.
     */
    struct GlobalSettings {
        bool enabled = true;
        int maxParticles = 1024;
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
    };

    /**
     * Get merged style settings for a given style name and optional spell ID.
     * If spellId is non-zero and has overrides, those fields replace base style values.
     */
    StyleSettings getStyleSettings(const std::string& styleName, uint32_t spellId = 0) const;

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

    std::string configPath_;
    bool loaded_ = false;

    GlobalSettings global_;
    std::unordered_map<std::string, StyleSettings> styles_;
    // spellOverrides_[spellId][styleName] = sparse StyleSettings
    std::unordered_map<uint32_t, std::unordered_map<std::string, StyleSettings>> spellOverrides_;
    std::unordered_map<std::string, glm::vec4> resistColors_;
    std::unordered_map<std::string, uint8_t> textureNameMap_;

    ReloadCallback reloadCallback_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
