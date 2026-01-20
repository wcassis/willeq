#ifndef EQT_GRAPHICS_DETAIL_WIND_CONTROLLER_H
#define EQT_GRAPHICS_DETAIL_WIND_CONTROLLER_H

#include <irrlicht.h>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Detail {

// Wind parameters that can vary per-zone
struct WindParams {
    float strength = 1.0f;        // Overall wind strength multiplier
    float frequency = 0.5f;       // Base oscillation frequency (Hz)
    float gustFrequency = 0.1f;   // Gust overlay frequency
    float gustStrength = 0.3f;    // Gust amplitude relative to base
    irr::core::vector2df direction{1.0f, 0.0f};  // Primary wind direction (XZ plane)
};

class WindController {
public:
    WindController() = default;
    ~WindController() = default;

    // Update wind state (call each frame)
    void update(float deltaTimeMs) {
        time_ += deltaTimeMs / 1000.0f;  // Convert to seconds
    }

    // Get current wind displacement for a vertex
    // position: world position of vertex
    // heightFactor: 0 = ground, 1 = top of grass (normalized height within detail)
    // windResponse: detail type's wind sensitivity (0-1)
    irr::core::vector3df getDisplacement(
        const irr::core::vector3df& position,
        float heightFactor,
        float windResponse) const
    {
        if (windResponse < 0.001f || params_.strength < 0.001f) {
            return irr::core::vector3df(0, 0, 0);
        }

        // Spatial variation based on position (creates wave effect across terrain)
        float spatialPhase = (position.X * 0.1f + position.Z * 0.13f);

        // Base oscillation
        float baseWave = std::sin(time_ * params_.frequency * 6.28318f + spatialPhase);

        // Add gust variation (slower, larger amplitude)
        float gustWave = std::sin(time_ * params_.gustFrequency * 6.28318f +
                                  spatialPhase * 0.3f) * params_.gustStrength;

        // Combined wave
        float wave = (baseWave + gustWave) * params_.strength * windResponse;

        // Height-based falloff: more movement at top, less at base
        // Use squared falloff for more natural grass bending
        float heightInfluence = heightFactor * heightFactor;
        wave *= heightInfluence;

        // Apply to X and Z based on wind direction
        // Y displacement is minimal (grass bends, doesn't rise)
        return irr::core::vector3df(
            wave * params_.direction.X * 0.15f,   // X displacement
            -std::abs(wave) * 0.02f,              // Slight Y dip when bent
            wave * params_.direction.Y * 0.15f   // Z displacement
        );
    }

    // Get current wind direction (normalized)
    irr::core::vector2df getWindDirection() const {
        return params_.direction;
    }

    // Get current wind magnitude (for visual effects)
    float getWindMagnitude() const {
        return params_.strength;
    }

    void setParams(const WindParams& params) { params_ = params; }
    const WindParams& getParams() const { return params_; }

    // Global time for animation
    float getTime() const { return time_; }

private:
    WindParams params_;
    float time_ = 0.0f;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_WIND_CONTROLLER_H
