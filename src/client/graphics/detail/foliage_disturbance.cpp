#include "client/graphics/detail/foliage_disturbance.h"
#include <cmath>
#include <algorithm>

namespace EQT {
namespace Graphics {
namespace Detail {

FoliageDisturbanceManager::FoliageDisturbanceManager(const FoliageDisturbanceConfig& config)
    : config_(config)
{
}

void FoliageDisturbanceManager::update(float deltaTimeMs) {
    if (!config_.enabled) {
        return;
    }

    float deltaTimeSec = deltaTimeMs / 1000.0f;
    updateResiduals(deltaTimeSec);
}

void FoliageDisturbanceManager::addDisturbanceSource(const DisturbanceSource& source) {
    sources_.push_back(source);
}

void FoliageDisturbanceManager::clearSources() {
    sources_.clear();
}

irr::core::vector3df FoliageDisturbanceManager::getDisplacement(
    const irr::core::vector3df& position,
    float heightFactor
) const {
    if (!config_.enabled) {
        return irr::core::vector3df(0, 0, 0);
    }

    irr::core::vector3df totalDisp(0, 0, 0);

    // Calculate displacement from active sources
    for (const auto& source : sources_) {
        totalDisp += calculateSourceDisplacement(source, position, heightFactor);
    }

    // Add displacement from residual disturbances (for smooth recovery)
    totalDisp += calculateResidualDisplacement(position, heightFactor);

    return totalDisp;
}

irr::core::vector3df FoliageDisturbanceManager::calculateSourceDisplacement(
    const DisturbanceSource& source,
    const irr::core::vector3df& position,
    float heightFactor
) const {
    // 2D distance check (ignore Y for ground-based effect)
    irr::core::vector3df diff = position - source.position;
    diff.Y = 0;
    float dist = diff.getLength();

    // Early exit if outside radius
    if (dist >= source.radius || dist < 0.01f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Inverse falloff: stronger when closer
    float falloff = 1.0f - (dist / source.radius);
    falloff = falloff * falloff;  // Quadratic for natural feel

    // Displacement direction: push away from source
    irr::core::vector3df pushDir = diff;
    pushDir.normalize();

    // Add velocity influence (bend in movement direction)
    if (source.velocity.getLengthSQ() > 0.01f) {
        irr::core::vector3df velDir = source.velocity;
        velDir.Y = 0;
        if (velDir.getLengthSQ() > 0.01f) {
            velDir.normalize();
            // Blend push direction with velocity direction
            pushDir = pushDir + velDir * config_.velocityInfluence;
            pushDir.normalize();
        }
    }

    // Apply height factor (base doesn't move, top moves most)
    float heightScale = std::pow(heightFactor, config_.heightExponent);
    float displacement = falloff * source.strength * heightScale;

    // Calculate final displacement
    irr::core::vector3df result;
    result.X = pushDir.X * displacement * config_.maxDisplacement;
    result.Z = pushDir.Z * displacement * config_.maxDisplacement;
    result.Y = -displacement * config_.verticalDipFactor;  // Slight downward bend

    return result;
}

irr::core::vector3df FoliageDisturbanceManager::calculateResidualDisplacement(
    const irr::core::vector3df& position,
    float heightFactor
) const {
    irr::core::vector3df totalDisp(0, 0, 0);

    // Check nearby grid cells for residuals
    // We check a 3x3 area around the position for overlapping effects
    float checkRadius = config_.playerRadius;

    for (const auto& [key, residual] : residuals_) {
        if (residual.intensity < 0.01f) continue;

        // 2D distance check
        irr::core::vector3df diff = position - residual.position;
        diff.Y = 0;
        float dist = diff.getLength();

        // Residuals have a fixed radius based on config
        float residualRadius = config_.playerRadius * 0.8f;  // Slightly smaller than active
        if (dist >= residualRadius) continue;

        // Falloff from residual center
        float falloff = 1.0f - (dist / residualRadius);
        falloff = falloff * falloff;

        // Height scaling
        float heightScale = std::pow(heightFactor, config_.heightExponent);

        // Use stored direction
        float displacement = falloff * residual.intensity * heightScale;

        totalDisp.X += residual.direction.X * displacement * config_.maxDisplacement;
        totalDisp.Z += residual.direction.Z * displacement * config_.maxDisplacement;
        totalDisp.Y -= displacement * config_.verticalDipFactor;
    }

    return totalDisp;
}

void FoliageDisturbanceManager::updateResiduals(float deltaTimeSec) {
    // First, update existing residuals from current sources
    // This creates new residuals or reinforces existing ones
    for (const auto& source : sources_) {
        int64_t key = positionToGridKey(source.position.X, source.position.Z);

        // Calculate push direction
        irr::core::vector3df pushDir(0, 0, 1);  // Default direction
        if (source.velocity.getLengthSQ() > 0.01f) {
            pushDir = source.velocity;
            pushDir.Y = 0;
            pushDir.normalize();
        }

        auto it = residuals_.find(key);
        if (it != residuals_.end()) {
            // Update existing residual
            it->second.position = source.position;
            it->second.direction = pushDir;
            it->second.intensity = std::max(it->second.intensity, source.strength);
        } else if (residuals_.size() < kMaxResiduals) {
            // Create new residual
            ResidualDisturbance residual;
            residual.position = source.position;
            residual.direction = pushDir;
            residual.intensity = source.strength;
            residuals_[key] = residual;
        }
    }

    // Fade all residuals over time
    std::vector<int64_t> toRemove;
    for (auto& [key, residual] : residuals_) {
        residual.intensity -= config_.recoveryRate * deltaTimeSec;
        if (residual.intensity <= 0.0f) {
            toRemove.push_back(key);
        }
    }

    // Remove faded residuals
    for (int64_t key : toRemove) {
        residuals_.erase(key);
    }
}

int64_t FoliageDisturbanceManager::positionToGridKey(float x, float z) const {
    // Simple spatial hash for grid-based lookup
    int32_t gx = static_cast<int32_t>(std::floor(x / kResidualGridSize));
    int32_t gz = static_cast<int32_t>(std::floor(z / kResidualGridSize));
    return (static_cast<int64_t>(gx) << 32) | static_cast<uint32_t>(gz);
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
