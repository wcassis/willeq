#include "client/graphics/weather_ambient_oracle.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

// Every row is a measurement, not a fit. See the header for the rig and the
// plateaus; review/T418/logs/oracle_o*_geo_windows.txt carries the raw table.
const WeatherAmbientKnot kOracleRainAmbient[] = {
    {0,  1.0f},   // clear, by definition and re-proven at every block boundary
    {2,  1.0f},   // measured
    {5,  1.0f},   // measured
    {9,  1.0f},   // measured
    {10, 1.0f},   // table bound; the 5/9 plateaus agree to 0.06%, so flat
};
const std::size_t kOracleRainAmbientCount =
    sizeof(kOracleRainAmbient) / sizeof(kOracleRainAmbient[0]);

const WeatherAmbientKnot kOracleSnowAmbient[] = {
    {0,  1.0f},
    {5,  1.0f},   // measured; lands on the rain plateau to 0.44%
    {10, 1.0f},
};
const std::size_t kOracleSnowAmbientCount =
    sizeof(kOracleSnowAmbient) / sizeof(kOracleSnowAmbient[0]);

const float kOracleWeatherTransitionSeconds = 9.3f;

float oracleWeatherAmbientModifier(uint8_t type, uint8_t intensity) {
    const WeatherAmbientKnot* table = nullptr;
    std::size_t count = 0;
    if (type == 1) {
        table = kOracleRainAmbient;
        count = kOracleRainAmbientCount;
    } else if (type == 2) {
        table = kOracleSnowAmbient;
        count = kOracleSnowAmbientCount;
    } else {
        return 1.0f;   // clear: the T396 rule, unchanged
    }

    if (intensity == 0) return 1.0f;
    if (intensity <= table[0].intensity) return table[0].modifier;
    for (std::size_t i = 1; i < count; ++i) {
        if (intensity <= table[i].intensity) {
            const float lo = static_cast<float>(table[i - 1].intensity);
            const float hi = static_cast<float>(table[i].intensity);
            const float t = (hi > lo) ? (static_cast<float>(intensity) - lo) / (hi - lo) : 0.0f;
            return table[i - 1].modifier + t * (table[i].modifier - table[i - 1].modifier);
        }
    }
    return table[count - 1].modifier;
}

float advanceWeatherDarkening(float current, float target, float rate, float deltaTime) {
    if (rate <= 0.0f) return target;
    const float step = rate * deltaTime;
    if (current < target) return std::min(target, current + step);
    if (current > target) return std::max(target, current - step);
    return current;
}

}  // namespace Graphics
}  // namespace EQT
