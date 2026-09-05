// tools/model_viewer/spell_effect_db.h -- the EQ spell-effect data model: resist/school/category enums, the
// EDD emitter parser, the EFF spell-effect parser, and the database that
// joins them.
//
// T385 (2026-09-05, board B167): split out of tools/model_viewer_spell_bar.h,
// which was 1,965 lines -- over the ceiling that check_file_sizes.sh did not
// apply to tools/ until this unit. Declarations moved verbatim; the original
// header is now an umbrella that includes this one.

#pragma once

#include <irrlicht.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <unordered_map>
#include "client/graphics/eq/dds_decoder.h"
// Forward declaration for hand bone queries
namespace EQT {
namespace Graphics {
class EQAnimatedMeshSceneNode;
}
}

namespace EQT {

// ============================================================================
// Resist Types (determines particle color/texture)
// Matches spell_constants.h ResistType enum
// ============================================================================
enum class ResistType : uint8_t {
    None         = 0,   // Unresistable / no resist check
    Magic        = 1,   // Magic resist - purple/blue
    Fire         = 2,   // Fire resist - red/orange
    Cold         = 3,   // Cold resist - blue/cyan
    Poison       = 4,   // Poison resist - green
    Disease      = 5,   // Disease resist - brown/green
    Chromatic    = 6,   // Lowest of all resists - rainbow
    Prismatic    = 7,   // Average of all resists - rainbow
    Physical     = 8,   // Physical resist - grey
    Corruption   = 9,   // Corruption resist - dark purple
};

// ============================================================================
// Spell Schools (determines casting animation)
// Matches spell_constants.h SpellSchool enum
// ============================================================================
enum class SpellSchool : uint8_t {
    Abjuration   = 0,   // Protective spells
    Alteration   = 1,   // Heals, buffs, CC
    Conjuration  = 2,   // Summoning, DoTs
    Divination   = 3,   // Invis, vision
    Evocation    = 4,   // Direct damage
};

// ============================================================================
// Spell Effect Categories (determines completion visual effect)
// ============================================================================
enum class SpellCategory : uint8_t {
    DirectDamage,   // Single target nuke - burst at target
    DoT,            // Damage over time - lingering particles
    AEDamage,       // Area effect - expanding ring
    PBAE,           // Point-blank AE - radiates from caster
    Rain,           // Targeted rain - particles fall from above
    Heal,           // Direct heal - rising sparkles
    Buff,           // Buff spell - swirling aura
    SummonPet,      // Pet summon - portal effect
    SummonItem,     // Item summon - brief flash
    Mez,            // Mesmerize - sleep particles
    Root,           // Root - ground effect
    Stun,           // Stun - stars/daze
    Fear,           // Fear - dark wisps
    Gate,           // Teleport - dimensional rift
    Invisibility,   // Invis - fading effect
    Utility,        // Generic utility spell
};

// ============================================================================
// EDD Parser - Parse EverQuest particle emitter definitions
// File format: spellsnew.edd
// ============================================================================
struct EmitterDefinition {
    std::string name;           // Emitter name (e.g., "Healing", "FireBolt01")
    std::string texture;        // Texture filename (e.g., "spelab.tga")

    // Particle properties (extracted from 288-byte property block)
    uint32_t particleCount1 = 0;    // Min particles per second
    uint32_t particleCount2 = 0;    // Max particles per second
    float emitRadius = 0.0f;        // Emission radius
    float velocity = 0.0f;          // Particle velocity
    float lifetime = 0.0f;          // Particle lifetime in seconds
    float sizeStart = 1.0f;         // Starting particle size
    float sizeEnd = 1.0f;           // Ending particle size
    float gravity = 0.0f;           // Gravity effect on particles

    // Colors (6 ARGB values for gradient)
    uint32_t colors[6] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                          0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

    // Convert first color to Irrlicht SColor
    irr::video::SColor getColor() const {
        uint32_t c = colors[0];
        return irr::video::SColor(
            (c >> 24) & 0xFF,  // A
            (c >> 16) & 0xFF,  // R
            (c >> 8) & 0xFF,   // G
            c & 0xFF           // B
        );
    }
};

class EDDParser {
public:
    EDDParser() = default;

    bool load(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open EDD file: " << filepath << std::endl;
            return false;
        }

        // Read header (8 bytes: "EDD\0" + version "110\0")
        char header[8];
        file.read(header, 8);
        if (header[0] != 'E' || header[1] != 'D' || header[2] != 'D') {
            std::cerr << "Invalid EDD header" << std::endl;
            return false;
        }

        m_version = std::string(header + 4, 3);  // "110"

        // Read entries (each 416 bytes)
        const size_t ENTRY_SIZE = 416;
        char buffer[ENTRY_SIZE];

        while (file.read(buffer, ENTRY_SIZE)) {
            EmitterDefinition emitter;

            // Name: first 64 bytes
            emitter.name = std::string(buffer, 64);
            size_t nullPos = emitter.name.find('\0');
            if (nullPos != std::string::npos) {
                emitter.name = emitter.name.substr(0, nullPos);
            }

            // Skip empty entries
            if (emitter.name.empty() || emitter.name == "None") {
                continue;
            }

            // Texture: bytes 64-128
            emitter.texture = std::string(buffer + 64, 64);
            nullPos = emitter.texture.find('\0');
            if (nullPos != std::string::npos) {
                emitter.texture = emitter.texture.substr(0, nullPos);
            }

            // Properties start at byte 128
            const char* props = buffer + 128;

            // Parse key properties (based on reverse engineering)
            emitter.particleCount1 = *reinterpret_cast<const uint32_t*>(props + 0);
            emitter.particleCount2 = *reinterpret_cast<const uint32_t*>(props + 4);
            emitter.emitRadius = *reinterpret_cast<const float*>(props + 8);
            emitter.velocity = *reinterpret_cast<const float*>(props + 20);  // At offset 148
            emitter.lifetime = *reinterpret_cast<const float*>(props + 32);  // At offset 160
            emitter.sizeStart = *reinterpret_cast<const float*>(props + 224); // At offset 352
            emitter.sizeEnd = *reinterpret_cast<const float*>(props + 228);   // At offset 356
            emitter.gravity = *reinterpret_cast<const float*>(props + 192);   // At offset 320

            // Colors at offset 212-236 (6 uint32 ARGB values)
            for (int i = 0; i < 6; ++i) {
                emitter.colors[i] = *reinterpret_cast<const uint32_t*>(props + 84 + i * 4);
            }

            m_emitters[emitter.name] = emitter;
        }

        std::cout << "Loaded " << m_emitters.size() << " emitter definitions from EDD" << std::endl;
        return true;
    }

    const EmitterDefinition* getEmitter(const std::string& name) const {
        auto it = m_emitters.find(name);
        return (it != m_emitters.end()) ? &it->second : nullptr;
    }

    // Get emitter by partial name match (useful for finding variants)
    const EmitterDefinition* findEmitter(const std::string& partialName) const {
        for (const auto& [name, emitter] : m_emitters) {
            if (name.find(partialName) != std::string::npos) {
                return &emitter;
            }
        }
        return nullptr;
    }

    // List all emitter names (for debugging)
    std::vector<std::string> getEmitterNames() const {
        std::vector<std::string> names;
        names.reserve(m_emitters.size());
        for (const auto& [name, _] : m_emitters) {
            names.push_back(name);
        }
        return names;
    }

    size_t size() const { return m_emitters.size(); }
    const std::string& version() const { return m_version; }

    // Build index for iteration (call after loading)
    void buildIndex() {
        m_indexedNames.clear();
        m_indexedNames.reserve(m_emitters.size());
        for (const auto& [name, _] : m_emitters) {
            m_indexedNames.push_back(name);
        }
        // Sort for consistent ordering
        std::sort(m_indexedNames.begin(), m_indexedNames.end());
    }

    // Get emitter by index (for cycling through effects)
    const EmitterDefinition* getEmitterByIndex(size_t index) const {
        if (index >= m_indexedNames.size()) return nullptr;
        return getEmitter(m_indexedNames[index]);
    }

    // Get name by index
    const std::string& getNameByIndex(size_t index) const {
        static const std::string empty;
        if (index >= m_indexedNames.size()) return empty;
        return m_indexedNames[index];
    }

    size_t indexSize() const { return m_indexedNames.size(); }

private:
    std::unordered_map<std::string, EmitterDefinition> m_emitters;
    std::vector<std::string> m_indexedNames;  // For indexed access
    std::string m_version;
};

// ============================================================================
// EFF Parser - Parse EverQuest spell effect definitions
// File format: spellsnew.eff
// Maps spell names to emitter names from the EDD file
// ============================================================================
struct SpellEffectEntry {
    std::string name;                    // Spell name (e.g., "Fire Bolt")
    std::vector<std::string> emitters;   // List of emitter names used by this spell
};

class EFFParser {
public:
    EFFParser() = default;

    bool load(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open EFF file: " << filepath << std::endl;
            return false;
        }

        // Read entire file
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> data(fileSize);
        file.read(data.data(), fileSize);

        // EFF format has 268-byte entries (T79: was 256, which drifted 12
        // bytes short per record — 112024 / 268 = 418 exactly for
        // spellsnew.eff, while /256 gives 437.59 and walks the read window
        // out of alignment as it goes. The "skip entries that look like
        // fragments" hack below existed only to paper over the resulting
        // garbled names; with the correct stride it never fires (measured:
        // 0 of 418 records), so it has been removed rather than left as
        // dead camouflage for a future regression.
        // Each entry has a spell name in first 64 bytes
        const size_t ENTRY_SIZE = 268;

        for (size_t offset = 0; offset + ENTRY_SIZE <= fileSize; offset += ENTRY_SIZE) {
            const char* entry = data.data() + offset;

            // Extract spell name (first 64 bytes, null-terminated)
            std::string name(entry, 64);
            size_t nullPos = name.find('\0');
            if (nullPos != std::string::npos) {
                name = name.substr(0, nullPos);
            }

            // Clean up the name - remove leading/trailing spaces
            while (!name.empty() && std::isspace(name.front())) name.erase(0, 1);
            while (!name.empty() && std::isspace(name.back())) name.pop_back();

            // Skip empty or short entries
            if (name.length() < 3 || name == "None") {
                continue;
            }

            SpellEffectEntry spell;
            spell.name = name;

            // Parse emitter indices from byte 128 to the end of the entry
            // These appear to be uint32 values referencing EDD entries
            for (size_t i = 128; i + 16 <= ENTRY_SIZE; i += 16) {
                uint32_t emitterIdx = *reinterpret_cast<const uint32_t*>(entry + i);
                // Valid emitter indices are typically in range 0-2105
                if (emitterIdx > 0 && emitterIdx < 3000) {
                    // Store index as string for now (we'll resolve to names later)
                    spell.emitters.push_back(std::to_string(emitterIdx));
                }
            }

            if (!spell.emitters.empty() || !spell.name.empty()) {
                m_spells[spell.name] = spell;
            }
        }

        std::cout << "Loaded " << m_spells.size() << " spell effect definitions from EFF" << std::endl;
        return true;
    }

    const SpellEffectEntry* getSpell(const std::string& name) const {
        auto it = m_spells.find(name);
        return (it != m_spells.end()) ? &it->second : nullptr;
    }

    // Find spell by partial name match
    const SpellEffectEntry* findSpell(const std::string& partialName) const {
        std::string lowerPartial = partialName;
        std::transform(lowerPartial.begin(), lowerPartial.end(), lowerPartial.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        for (const auto& [name, spell] : m_spells) {
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lowerName.find(lowerPartial) != std::string::npos) {
                return &spell;
            }
        }
        return nullptr;
    }

    // List all spell names
    std::vector<std::string> getSpellNames() const {
        std::vector<std::string> names;
        names.reserve(m_spells.size());
        for (const auto& [name, _] : m_spells) {
            names.push_back(name);
        }
        return names;
    }

    size_t size() const { return m_spells.size(); }

private:
    std::unordered_map<std::string, SpellEffectEntry> m_spells;
};

// ============================================================================
// Spell Effect Database - combines EDD and EFF data for spell effect lookup
// ============================================================================
class SpellEffectDatabase {
public:
    SpellEffectDatabase() = default;

    bool load(const std::string& eqClientPath) {
        std::string path = eqClientPath;
        if (!path.empty() && path.back() != '/') path += '/';

        bool eddLoaded = m_eddParser.load(path + "spellsnew.edd");
        bool effLoaded = m_effParser.load(path + "spellsnew.eff");

        // Build emitter lookup by index
        if (eddLoaded) {
            buildEmitterIndex();
        }

        return eddLoaded || effLoaded;
    }

    // Find emitter definition for a spell category
    const EmitterDefinition* getEmitterForCategory(SpellCategory category) const {
        // Map categories to emitter name patterns
        std::string pattern;
        switch (category) {
            case SpellCategory::DirectDamage:
            case SpellCategory::AEDamage:
            case SpellCategory::PBAE:
                pattern = "Bolt";
                break;
            case SpellCategory::DoT:
            case SpellCategory::Rain:
                pattern = "Rain";
                break;
            case SpellCategory::Heal:
                pattern = "Heal";
                break;
            case SpellCategory::Buff:
                pattern = "Buff";
                break;
            case SpellCategory::SummonPet:
            case SpellCategory::Gate:
                pattern = "Gate";
                break;
            case SpellCategory::Mez:
            case SpellCategory::Root:
            case SpellCategory::Stun:
            case SpellCategory::Fear:
                pattern = "Stun";
                break;
            default:
                pattern = "Light";
        }
        return m_eddParser.findEmitter(pattern);
    }

    // Find emitter for a resist type
    const EmitterDefinition* getEmitterForResistType(ResistType resistType) const {
        std::string pattern;
        switch (resistType) {
            case ResistType::Fire:
                pattern = "Fire";
                break;
            case ResistType::Cold:
                pattern = "Frost";
                break;
            case ResistType::Poison:
                pattern = "Poison";
                break;
            case ResistType::Disease:
                pattern = "Disease";
                break;
            case ResistType::Magic:
            default:
                pattern = "Light";
        }
        return m_eddParser.findEmitter(pattern);
    }

    // Direct access to parsers
    const EDDParser& eddParser() const { return m_eddParser; }
    const EFFParser& effParser() const { return m_effParser; }

    // Indexed access for cycling through effects
    size_t getEmitterCount() const { return m_eddParser.indexSize(); }
    const EmitterDefinition* getEmitterByIndex(size_t index) const {
        return m_eddParser.getEmitterByIndex(index);
    }
    const std::string& getEmitterNameByIndex(size_t index) const {
        return m_eddParser.getNameByIndex(index);
    }

private:
    void buildEmitterIndex() {
        // Build sorted index for iteration
        m_eddParser.buildIndex();
    }

    EDDParser m_eddParser;
    EFFParser m_effParser;
};

} // namespace EQT
