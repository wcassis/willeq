#include "client/graphics/environment/zone_biome.h"
#include <algorithm>

namespace EQT {
namespace Graphics {
namespace Environment {

ZoneBiomeDetector& ZoneBiomeDetector::instance() {
    static ZoneBiomeDetector instance;
    return instance;
}

ZoneBiomeDetector::ZoneBiomeDetector() {
    initBiomeTable();
}

void ZoneBiomeDetector::initBiomeTable() {
    // === Forest Zones ===
    // Greater Faydark, Lesser Faydark, Nektulos Forest, etc.
    biomeTable_["gfaydark"] = ZoneBiome::Forest;
    biomeTable_["lfaydark"] = ZoneBiome::Forest;
    biomeTable_["nektulos"] = ZoneBiome::Forest;
    biomeTable_["misty"] = ZoneBiome::Forest;      // Misty Thicket
    biomeTable_["ecommons"] = ZoneBiome::Forest;   // East Commonlands (border)
    biomeTable_["wfreeport"] = ZoneBiome::Forest;  // West Freeport outskirts
    biomeTable_["kithicor"] = ZoneBiome::Forest;   // Kithicor Forest
    biomeTable_["jaggedpine"] = ZoneBiome::Forest; // Jaggedpine Forest
    biomeTable_["emeraldjungle"] = ZoneBiome::Forest;
    biomeTable_["stonebrunt"] = ZoneBiome::Forest;
    biomeTable_["wakening"] = ZoneBiome::Forest;
    biomeTable_["firiona"] = ZoneBiome::Forest;    // Firiona Vie

    // === Plains/Grassland Zones ===
    biomeTable_["qeynos2"] = ZoneBiome::Plains;    // North Qeynos outskirts
    biomeTable_["qeytoqrg"] = ZoneBiome::Plains;   // Qeynos Hills
    biomeTable_["northkarana"] = ZoneBiome::Plains;
    biomeTable_["southkarana"] = ZoneBiome::Plains;
    biomeTable_["eastkarana"] = ZoneBiome::Plains;
    biomeTable_["fieldofbone"] = ZoneBiome::Plains;
    biomeTable_["lakeofillomen"] = ZoneBiome::Plains;
    biomeTable_["wcommons"] = ZoneBiome::Plains;   // West Commonlands
    biomeTable_["nro"] = ZoneBiome::Plains;        // North Ro (desert border)
    biomeTable_["steamfont"] = ZoneBiome::Plains;
    biomeTable_["butcher"] = ZoneBiome::Plains;    // Butcherblock

    // === Desert Zones ===
    biomeTable_["oasis"] = ZoneBiome::Desert;      // Oasis of Marr
    biomeTable_["sro"] = ZoneBiome::Desert;        // South Ro
    biomeTable_["nro"] = ZoneBiome::Desert;        // North Ro (override to desert)
    biomeTable_["scarlet"] = ZoneBiome::Desert;    // Scarlet Desert

    // === Swamp Zones ===
    biomeTable_["innothule"] = ZoneBiome::Swamp;   // Innothule Swamp
    biomeTable_["feerrott"] = ZoneBiome::Swamp;    // The Feerrott
    biomeTable_["swampofnohope"] = ZoneBiome::Swamp;
    biomeTable_["trakanon"] = ZoneBiome::Swamp;    // Trakanon's Teeth
    biomeTable_["overthere"] = ZoneBiome::Swamp;   // Partial swamp

    // === Snow/Tundra Zones ===
    biomeTable_["everfrost"] = ZoneBiome::Snow;    // Everfrost Peaks
    biomeTable_["halas"] = ZoneBiome::Snow;
    biomeTable_["permafrost"] = ZoneBiome::Snow;
    biomeTable_["eastwastes"] = ZoneBiome::Snow;
    biomeTable_["westwastes"] = ZoneBiome::Snow;
    biomeTable_["thurgadina"] = ZoneBiome::Snow;
    biomeTable_["thurgadinb"] = ZoneBiome::Snow;
    biomeTable_["velketor"] = ZoneBiome::Snow;
    biomeTable_["kael"] = ZoneBiome::Snow;
    biomeTable_["sirens"] = ZoneBiome::Snow;       // Siren's Grotto (ice)
    biomeTable_["iceclad"] = ZoneBiome::Snow;
    biomeTable_["cobaltscar"] = ZoneBiome::Snow;
    biomeTable_["crystal"] = ZoneBiome::Snow;      // Crystal Caverns

    // === Urban Zones ===
    biomeTable_["qeynos"] = ZoneBiome::Urban;      // South Qeynos
    biomeTable_["qeynos2"] = ZoneBiome::Urban;     // North Qeynos
    biomeTable_["freporte"] = ZoneBiome::Urban;    // East Freeport
    biomeTable_["freportw"] = ZoneBiome::Urban;    // West Freeport
    biomeTable_["freportn"] = ZoneBiome::Urban;    // North Freeport
    biomeTable_["felwithea"] = ZoneBiome::Urban;   // Northern Felwithe
    biomeTable_["felwitheb"] = ZoneBiome::Urban;   // Southern Felwithe
    biomeTable_["akanon"] = ZoneBiome::Urban;      // Ak'Anon
    biomeTable_["kaladima"] = ZoneBiome::Urban;    // North Kaladim
    biomeTable_["kaladimb"] = ZoneBiome::Urban;    // South Kaladim
    biomeTable_["rivervale"] = ZoneBiome::Urban;
    biomeTable_["erudsxing"] = ZoneBiome::Urban;   // Erud's Crossing dock
    biomeTable_["erudnint"] = ZoneBiome::Urban;    // Erudin Interior
    biomeTable_["erudnext"] = ZoneBiome::Urban;    // Erudin
    biomeTable_["paineel"] = ZoneBiome::Urban;
    biomeTable_["cabeast"] = ZoneBiome::Urban;     // Cabilis East
    biomeTable_["cabwest"] = ZoneBiome::Urban;     // Cabilis West
    biomeTable_["oggok"] = ZoneBiome::Urban;
    biomeTable_["grobb"] = ZoneBiome::Urban;
    biomeTable_["neriak"] = ZoneBiome::Urban;
    biomeTable_["neriaka"] = ZoneBiome::Urban;
    biomeTable_["neriakb"] = ZoneBiome::Urban;
    biomeTable_["neriakc"] = ZoneBiome::Urban;     // Neriak Third Gate (dungeon-like)
    biomeTable_["shadowhaven"] = ZoneBiome::Urban;

    // === Dungeon Zones ===
    biomeTable_["befallen"] = ZoneBiome::Dungeon;
    biomeTable_["blackburrow"] = ZoneBiome::Dungeon;
    biomeTable_["crushbone"] = ZoneBiome::Dungeon;
    biomeTable_["unrest"] = ZoneBiome::Dungeon;
    biomeTable_["mistmoore"] = ZoneBiome::Dungeon;
    biomeTable_["najena"] = ZoneBiome::Dungeon;
    biomeTable_["soldungb"] = ZoneBiome::Dungeon;  // Solusek's Eye
    biomeTable_["soldunga"] = ZoneBiome::Dungeon;  // Solusek A
    biomeTable_["lavastorm"] = ZoneBiome::Dungeon; // Volcanic dungeon
    biomeTable_["nagafen"] = ZoneBiome::Dungeon;   // Nagafen's Lair
    biomeTable_["gukbottom"] = ZoneBiome::Dungeon;
    biomeTable_["guktop"] = ZoneBiome::Dungeon;
    biomeTable_["runnyeye"] = ZoneBiome::Dungeon;
    biomeTable_["highkeep"] = ZoneBiome::Dungeon;
    biomeTable_["paw"] = ZoneBiome::Dungeon;       // Splitpaw
    biomeTable_["cazicthule"] = ZoneBiome::Dungeon;
    biomeTable_["kedge"] = ZoneBiome::Dungeon;     // Kedge Keep (underwater)
    biomeTable_["hole"] = ZoneBiome::Dungeon;
    biomeTable_["nurga"] = ZoneBiome::Dungeon;
    biomeTable_["droga"] = ZoneBiome::Dungeon;
    biomeTable_["chardok"] = ZoneBiome::Dungeon;
    biomeTable_["sebilis"] = ZoneBiome::Dungeon;
    biomeTable_["karnor"] = ZoneBiome::Dungeon;    // Karnor's Castle
    biomeTable_["veeshan"] = ZoneBiome::Dungeon;   // Veeshan's Peak
    biomeTable_["sleeper"] = ZoneBiome::Dungeon;   // Sleeper's Tomb
    biomeTable_["templeveeshan"] = ZoneBiome::Dungeon;
    biomeTable_["ssratemple"] = ZoneBiome::Dungeon;
    biomeTable_["vexthal"] = ZoneBiome::Dungeon;

    // === Cave Zones ===
    biomeTable_["warrens"] = ZoneBiome::Cave;
    biomeTable_["stonebrunt"] = ZoneBiome::Cave;

    // === Ocean/Coastal Zones ===
    biomeTable_["oot"] = ZoneBiome::Ocean;         // Ocean of Tears
    biomeTable_["erudsxing"] = ZoneBiome::Ocean;
    biomeTable_["timorous"] = ZoneBiome::Ocean;    // Timorous Deep
    biomeTable_["iceclad"] = ZoneBiome::Ocean;     // Override - icy ocean

    // === Volcanic Zones ===
    biomeTable_["lavastorm"] = ZoneBiome::Volcanic;
    biomeTable_["soltemple"] = ZoneBiome::Volcanic; // Temple of Solusek Ro
    biomeTable_["nagafen"] = ZoneBiome::Volcanic;

    // === Water Table ===
    // Zones with significant water features
    waterTable_["oasis"] = true;
    waterTable_["oot"] = true;
    waterTable_["erudsxing"] = true;
    waterTable_["timorous"] = true;
    waterTable_["kedge"] = true;
    waterTable_["innothule"] = true;
    waterTable_["feerrott"] = true;
    waterTable_["lakeofillomen"] = true;
    waterTable_["iceclad"] = true;
    waterTable_["cobaltscar"] = true;
    waterTable_["sirens"] = true;
    waterTable_["gukbottom"] = true;
    waterTable_["guktop"] = true;
    waterTable_["qeynos"] = true;      // Fountain
    waterTable_["felwithea"] = true;   // Canals
    waterTable_["neriak"] = true;      // Underground river

    // === Indoor Table ===
    // Zones that are fully or mostly indoors
    indoorTable_["befallen"] = true;
    indoorTable_["blackburrow"] = true;
    indoorTable_["crushbone"] = true;
    indoorTable_["unrest"] = true;
    indoorTable_["mistmoore"] = true;
    indoorTable_["najena"] = true;
    indoorTable_["soldungb"] = true;
    indoorTable_["soldunga"] = true;
    indoorTable_["nagafen"] = true;
    indoorTable_["gukbottom"] = true;
    indoorTable_["guktop"] = true;
    indoorTable_["runnyeye"] = true;
    indoorTable_["highkeep"] = true;
    indoorTable_["paw"] = true;
    indoorTable_["cazicthule"] = true;
    indoorTable_["kedge"] = true;
    indoorTable_["hole"] = true;
    indoorTable_["nurga"] = true;
    indoorTable_["droga"] = true;
    indoorTable_["chardok"] = true;
    indoorTable_["sebilis"] = true;
    indoorTable_["karnor"] = true;
    indoorTable_["veeshan"] = true;
    indoorTable_["sleeper"] = true;
    indoorTable_["templeveeshan"] = true;
    indoorTable_["ssratemple"] = true;
    indoorTable_["vexthal"] = true;
    indoorTable_["warrens"] = true;
    indoorTable_["crystal"] = true;
    indoorTable_["velketor"] = true;
    indoorTable_["akanon"] = true;
    indoorTable_["kaladima"] = true;
    indoorTable_["kaladimb"] = true;
    indoorTable_["neriakc"] = true;
    indoorTable_["paineel"] = true;
    indoorTable_["erudnint"] = true;
    indoorTable_["permafrost"] = true;
}

ZoneBiome ZoneBiomeDetector::getBiome(const std::string& zoneName) const {
    // Convert to lowercase for lookup
    std::string lowerName = zoneName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    auto it = biomeTable_.find(lowerName);
    if (it != biomeTable_.end()) {
        return it->second;
    }

    // Default heuristics based on zone name patterns
    if (lowerName.find("forest") != std::string::npos ||
        lowerName.find("wood") != std::string::npos ||
        lowerName.find("grove") != std::string::npos) {
        return ZoneBiome::Forest;
    }

    if (lowerName.find("swamp") != std::string::npos ||
        lowerName.find("marsh") != std::string::npos ||
        lowerName.find("bog") != std::string::npos) {
        return ZoneBiome::Swamp;
    }

    if (lowerName.find("desert") != std::string::npos ||
        lowerName.find("sand") != std::string::npos) {
        return ZoneBiome::Desert;
    }

    if (lowerName.find("snow") != std::string::npos ||
        lowerName.find("ice") != std::string::npos ||
        lowerName.find("frost") != std::string::npos ||
        lowerName.find("tundra") != std::string::npos) {
        return ZoneBiome::Snow;
    }

    if (lowerName.find("cave") != std::string::npos ||
        lowerName.find("cavern") != std::string::npos ||
        lowerName.find("dungeon") != std::string::npos ||
        lowerName.find("tomb") != std::string::npos ||
        lowerName.find("crypt") != std::string::npos) {
        return ZoneBiome::Dungeon;
    }

    if (lowerName.find("city") != std::string::npos ||
        lowerName.find("town") != std::string::npos) {
        return ZoneBiome::Urban;
    }

    if (lowerName.find("ocean") != std::string::npos ||
        lowerName.find("sea") != std::string::npos) {
        return ZoneBiome::Ocean;
    }

    if (lowerName.find("lava") != std::string::npos ||
        lowerName.find("volcano") != std::string::npos ||
        lowerName.find("fire") != std::string::npos) {
        return ZoneBiome::Volcanic;
    }

    // Default to Plains for outdoor zones, Dungeon for indoor
    if (isIndoorZone(zoneName)) {
        return ZoneBiome::Dungeon;
    }

    return ZoneBiome::Plains;
}

std::string ZoneBiomeDetector::getBiomeName(ZoneBiome biome) {
    switch (biome) {
        case ZoneBiome::Unknown: return "Unknown";
        case ZoneBiome::Forest: return "Forest";
        case ZoneBiome::Swamp: return "Swamp";
        case ZoneBiome::Desert: return "Desert";
        case ZoneBiome::Snow: return "Snow";
        case ZoneBiome::Plains: return "Plains";
        case ZoneBiome::Dungeon: return "Dungeon";
        case ZoneBiome::Urban: return "Urban";
        case ZoneBiome::Ocean: return "Ocean";
        case ZoneBiome::Volcanic: return "Volcanic";
        case ZoneBiome::Cave: return "Cave";
        default: return "Unknown";
    }
}

bool ZoneBiomeDetector::isIndoorZone(const std::string& zoneName) const {
    std::string lowerName = zoneName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    auto it = indoorTable_.find(lowerName);
    return it != indoorTable_.end() && it->second;
}

bool ZoneBiomeDetector::hasWater(const std::string& zoneName) const {
    std::string lowerName = zoneName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    auto it = waterTable_.find(lowerName);
    return it != waterTable_.end() && it->second;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
