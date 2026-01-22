#ifndef EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_H
#define EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_H

#include <irrlicht.h>
#include <vector>
#include <unordered_map>
#include "client/graphics/detail/foliage_disturbance_config.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Represents an entity that can disturb foliage
struct DisturbanceSource {
    irr::core::vector3df position;
    irr::core::vector3df velocity;  // Movement direction (for directional bending)
    float radius;                    // Effect radius
    float strength;                  // 0-1 intensity
};

// Tracks residual disturbance at a location for smooth recovery
struct ResidualDisturbance {
    irr::core::vector3df position;
    irr::core::vector3df direction;  // Last push direction
    float intensity;                  // Current intensity (fades over time)
};

// Manages foliage disturbance from entities (player movement bending grass)
class FoliageDisturbanceManager {
public:
    explicit FoliageDisturbanceManager(const FoliageDisturbanceConfig& config);
    ~FoliageDisturbanceManager() = default;

    // Update config at runtime
    void setConfig(const FoliageDisturbanceConfig& config) { config_ = config; }
    const FoliageDisturbanceConfig& getConfig() const { return config_; }

    // Update disturbance state (call each frame)
    // deltaTimeMs: time since last frame in milliseconds
    void update(float deltaTimeMs);

    // Add a disturbance source for this frame
    void addDisturbanceSource(const DisturbanceSource& source);

    // Clear all sources (called before gathering new sources each frame)
    void clearSources();

    // Calculate displacement for a foliage vertex
    // position: world position of the vertex
    // heightFactor: 0 = ground/base, 1 = top of grass (normalized height)
    // Returns: displacement vector to apply to vertex
    irr::core::vector3df getDisplacement(
        const irr::core::vector3df& position,
        float heightFactor
    ) const;

    // Check if disturbance is enabled
    bool isEnabled() const { return config_.enabled; }

    // Get active source count (for debug info)
    size_t getSourceCount() const { return sources_.size(); }

    // Get residual count (for debug info)
    size_t getResidualCount() const { return residuals_.size(); }

private:
    // Calculate displacement from a single source
    irr::core::vector3df calculateSourceDisplacement(
        const DisturbanceSource& source,
        const irr::core::vector3df& position,
        float heightFactor
    ) const;

    // Calculate displacement from residual disturbances
    irr::core::vector3df calculateResidualDisplacement(
        const irr::core::vector3df& position,
        float heightFactor
    ) const;

    // Update residual disturbances (fade over time, add new from sources)
    void updateResiduals(float deltaTimeSec);

    // Hash function for grid-based residual storage
    int64_t positionToGridKey(float x, float z) const;

    FoliageDisturbanceConfig config_;
    std::vector<DisturbanceSource> sources_;

    // Grid-based residual storage for efficient lookup
    // Key is grid cell (coarse spatial hash), value is residual state
    std::unordered_map<int64_t, ResidualDisturbance> residuals_;

    // Grid cell size for residual tracking (coarser than foliage for performance)
    static constexpr float kResidualGridSize = 1.0f;

    // Maximum number of residuals to track (prevent unbounded memory)
    static constexpr size_t kMaxResiduals = 500;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_FOLIAGE_DISTURBANCE_H
