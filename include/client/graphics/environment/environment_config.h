#pragma once

#include <string>
#include <functional>

// Forward declaration
namespace Json {
class Value;
}

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * EnvironmentEffectsConfig - JSON-based configuration for environmental effects.
 *
 * Allows runtime tuning of particle emitter settings without recompilation.
 * Settings can be reloaded while the application is running.
 */
class EnvironmentEffectsConfig {
public:
    static EnvironmentEffectsConfig& instance();

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
     * Settings for a particle emitter type.
     */
    struct EmitterSettings {
        bool enabled = true;
        int maxParticles = 80;
        float spawnRate = 10.0f;
        float spawnRadiusMin = 3.0f;
        float spawnRadiusMax = 20.0f;
        float spawnHeightMin = -1.0f;
        float spawnHeightMax = 6.0f;
        float sizeMin = 0.15f;
        float sizeMax = 0.35f;
        float lifetimeMin = 6.0f;
        float lifetimeMax = 10.0f;
        float driftSpeed = 0.3f;
        float windFactor = 2.0f;
        float alphaIndoor = 0.9f;
        float alphaOutdoor = 0.8f;
        // Color (RGBA, 0-1)
        float colorR = 1.0f;
        float colorG = 1.0f;
        float colorB = 1.0f;
        float colorA = 1.0f;
    };

    /**
     * Settings for detail objects (grass, plants, etc.)
     */
    struct DetailSettings {
        bool enabled = true;
        float density = 1.0f;
        float viewDistance = 150.0f;
        bool grassEnabled = true;
        bool plantsEnabled = true;
        bool rocksEnabled = true;
        bool debrisEnabled = true;
    };

    // Getters for emitter settings
    const EmitterSettings& getDustMotes() const { return dustMotes_; }
    const EmitterSettings& getPollen() const { return pollen_; }
    const EmitterSettings& getFireflies() const { return fireflies_; }
    const EmitterSettings& getMist() const { return mist_; }
    const EmitterSettings& getSandDust() const { return sandDust_; }
    const DetailSettings& getDetailObjects() const { return detailObjects_; }

    /**
     * Callback invoked when config is reloaded.
     */
    using ReloadCallback = std::function<void()>;
    void setReloadCallback(ReloadCallback cb) { reloadCallback_ = cb; }

    /**
     * Check if config has been loaded.
     */
    bool isLoaded() const { return loaded_; }

private:
    EnvironmentEffectsConfig() = default;
    ~EnvironmentEffectsConfig() = default;

    // Non-copyable
    EnvironmentEffectsConfig(const EnvironmentEffectsConfig&) = delete;
    EnvironmentEffectsConfig& operator=(const EnvironmentEffectsConfig&) = delete;

    void loadEmitterSettings(const Json::Value& json, const std::string& name, EmitterSettings& settings);
    void loadDetailSettings(const Json::Value& json);
    void setDefaults();

    std::string configPath_;
    bool loaded_ = false;

    EmitterSettings dustMotes_;
    EmitterSettings pollen_;
    EmitterSettings fireflies_;
    EmitterSettings mist_;
    EmitterSettings sandDust_;
    DetailSettings detailObjects_;

    ReloadCallback reloadCallback_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
