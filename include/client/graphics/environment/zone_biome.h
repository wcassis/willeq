#pragma once

#include "particle_types.h"
#include <string>
#include <unordered_map>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * ZoneBiomeDetector - Maps zone short names to biomes.
 *
 * Uses zone name patterns and a lookup table to determine
 * which environmental particles should appear in each zone.
 */
class ZoneBiomeDetector {
public:
    /**
     * Get the singleton instance.
     */
    static ZoneBiomeDetector& instance();

    /**
     * Detect the biome for a given zone.
     * @param zoneName Zone short name (e.g., "qeynos", "gfaydark")
     * @return The detected biome, or ZoneBiome::Unknown if not found
     */
    ZoneBiome getBiome(const std::string& zoneName) const;

    /**
     * Get the biome name for display/debugging.
     */
    static std::string getBiomeName(ZoneBiome biome);

    /**
     * Check if a zone is an indoor/dungeon zone.
     * Indoor zones typically have different lighting and no sky.
     */
    bool isIndoorZone(const std::string& zoneName) const;

    /**
     * Check if a zone has water (for mist/shoreline effects).
     */
    bool hasWater(const std::string& zoneName) const;

private:
    ZoneBiomeDetector();

    // Prevent copying
    ZoneBiomeDetector(const ZoneBiomeDetector&) = delete;
    ZoneBiomeDetector& operator=(const ZoneBiomeDetector&) = delete;

    /**
     * Initialize the zone biome lookup table.
     */
    void initBiomeTable();

    // Zone name -> biome lookup
    std::unordered_map<std::string, ZoneBiome> biomeTable_;

    // Zones with water features
    std::unordered_map<std::string, bool> waterTable_;

    // Indoor/dungeon zones
    std::unordered_map<std::string, bool> indoorTable_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
