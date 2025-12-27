#include "client/graphics/eq/animation_mapping.h"
#include <algorithm>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

std::string getAnimationSourceCode(const std::string& raceCode) {
    // Convert to uppercase for comparison
    std::string upper = raceCode;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // ===== HUMANOID MALES -> ELM (Elf Male has 43 animations) =====
    static const std::set<std::string> maleHumanoids = {
        "HUM", "BAM", "ERM", "HIM", "DAM", "HAM", "BRM", "FPM", "BGM",
        "SKE", "HHM", "ZOM", "QCM", "NGM", "EGM", "HLM", "FEM", "GFM",
        "TRI", "ENA"
    };

    // ===== HUMANOID FEMALES -> ELF (Elf Female has 42 animations) =====
    static const std::set<std::string> femaleHumanoids = {
        "HUF", "BAF", "ERF", "HIF", "DAF", "HAF", "BRF", "ZOF", "QCF",
        "HLF", "FEF", "GFF", "ERO", "ABH", "HAG", "SIR"
    };

    // ===== SHORT RACE MALES -> DWM (Dwarf Male) =====
    static const std::set<std::string> dwarfMaleTypes = {
        "HOM", "GNM", "RIM", "CLM", "KAM", "COM", "COK", "BRI"
    };

    // ===== SHORT RACE FEMALES -> DWF (Dwarf Female) =====
    static const std::set<std::string> dwarfFemaleTypes = {
        "HOF", "GNF", "RIF", "CLF", "KAF", "COF"
    };

    // ===== LARGE HUMANOIDS -> OGF (Ogre Female) =====
    static const std::set<std::string> ogreTypes = {
        "TRM", "TRF", "OGM", "GRM", "GRF", "OKM", "OKF"
    };

    // ===== IKSAR VARIANTS -> IKM (Iksar Male) =====
    static const std::set<std::string> iksarTypes = {
        "IKF", "ICM", "ICF", "ICN", "IKS"
    };

    // ===== FELINE CREATURES -> LIM (Lion Male) =====
    static const std::set<std::string> felineTypes = {
        "LIF", "TIG", "PUM", "STC"
    };

    // ===== DRAGONS/WURMS -> DRA (Dragon) =====
    static const std::set<std::string> dragonTypes = {
        "WUR", "GDR"
    };

    // ===== GOLEMS/GIANTS -> GIA (Giant) =====
    static const std::set<std::string> giantTypes = {
        "GOM", "GOL"
    };

    // ===== GARGOYLE-LIKE -> GAR (Gargoyle) =====
    static const std::set<std::string> gargoyleTypes = {
        "GAM", "IMP"
    };

    // ===== SPIDER-LIKE -> SPI (Spider) =====
    static const std::set<std::string> spiderTypes = {
        "BET"  // Beetle
    };

    if (maleHumanoids.count(upper)) return "ELM";
    if (femaleHumanoids.count(upper)) return "ELF";
    if (dwarfMaleTypes.count(upper)) return "DWM";
    if (dwarfFemaleTypes.count(upper)) return "DWF";
    if (ogreTypes.count(upper)) return "OGF";
    if (iksarTypes.count(upper)) return "IKM";
    if (felineTypes.count(upper)) return "LIM";
    if (dragonTypes.count(upper)) return "DRA";
    if (giantTypes.count(upper)) return "GIA";
    if (gargoyleTypes.count(upper)) return "GAR";
    if (spiderTypes.count(upper)) return "SPI";

    // ===== OTHER CREATURE MAPPINGS (individual) =====
    static const std::map<std::string, std::string> otherMappings = {
        {"CPF", "CPM"},  // Campfire Female -> Campfire Male
        {"FRG", "FRO"},  // Frog variant -> Frog base
        {"GHU", "GOB"},  // Ghoul -> Goblin
        {"GRI", "DRK"},  // Griffin -> Drake
        {"KOB", "WER"},  // Kobold -> Werewolf
        {"MIN", "GNN"},  // Minion -> Gnoll
        {"PIF", "FAF"},  // Pixie Female -> Fairy Female
        {"BGG", "KGO"},  // Big Goblin -> King Goblin
        {"SKU", "RAT"},  // Skunk -> Rat
        {"SPH", "DRK"},  // Sphinx -> Drake
        {"ARM", "RAT"},  // Armadillo -> Rat
        {"FDF", "FDR"},  // Fire Drake Female -> Fire Drake
        {"SSK", "SRW"},  // Sea Snake -> Sea Serpent
        {"VRF", "VRM"},  // Vampire Female -> Vampire Male
        {"IKH", "REA"},  // Iksar Hybrid -> Reaver
        {"FMO", "DRK"},  // Fire Monster -> Drake
        {"BTM", "RHI"},  // Bat Monster -> Rhino
        {"SDE", "DML"},  // Shadow -> Demon Lord
        {"TOT", "SCA"},  // Totem -> Scarecrow
        {"SPC", "SPE"},  // Specter variant -> Specter
        {"YAK", "GNN"},  // Yak -> Gnoll
        {"DR2", "TRK"},  // Dragon 2 -> Drake
        {"STG", "FSG"},  // Stone Golem -> Frost Giant
        {"CCD", "TRK"},  // Cockatrice -> Drake
        {"BWD", "TRK"},  // Bloodworm -> Drake
        {"PRI", "TRK"},  // Prismatic Dragon -> Drake
    };

    auto it = otherMappings.find(upper);
    if (it != otherMappings.end()) {
        return it->second;
    }

    // Default fallback based on last character (gender indicator)
    if (upper.length() == 3) {
        if (upper.back() == 'F') return "ELF";
        if (upper.back() == 'M') return "ELM";
    }

    return "";
}

} // namespace Graphics
} // namespace EQT
