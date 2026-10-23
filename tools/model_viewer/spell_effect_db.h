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
#include <cstring>
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
// File format: spellsnew.edd ("EDD\0" "110\0", then 416-byte entries).
// B1141 (#461): the field names below come from the official client's code
// (EQGraphicsDX9.dll CEmitterDef::CreateEmitter 0x1005a430 and
// CParticleSystem::Render 0x100605b0); docs/spellsnew_format.md has the
// whole table with addresses. Offsets are from the start of the 416-byte
// entry. REFUTED by that code and removed: min/max particles per second at
// +0x80/+0x84, emit radius +0x88, velocity +0x94, lifetime +0xA0, six ARGB
// colours at +0xD4, gravity +0x140, size start/end +0x160/+0x164.
// ============================================================================
struct EmitterDefinition {
    std::string name;           // +0x00 char[64] emitter name
    std::string texture;        // +0x40 char[32] texture file name

    float duration = 0.0f;        // +0x78 s; < 0 fixed |d|, >= 0 caller may override
    float particleLifetime = 0.0f;// +0x7C s; < 0 = lives for the emitter duration
    uint32_t burstCount = 0;      // +0x80 particles emitted on the first frame
    uint32_t perEmission = 0;     // +0x84 particles per emission tick
    float emissionRate = 0.0f;    // +0x88 emission ticks per second
    int32_t shape = 0;            // +0xA8 spawn shape (0 point .. 9 random ring)
    float shapeRadiusX = 0.0f;    // +0xAC shape radius / half-extent X
    float upSpeedMax = 0.0f;      // +0xF0 speed along the emitter axis (up), max
    float spriteWidth = 1.0f;     // +0xCC sprite width (min of the +0x184 range)
    float spriteWidthMax = 1.0f;  // +0x184
    float worldGravity = 0.0f;    // +0x128 downward acceleration in world units/s^2

    // +0xD4..+0xDC start R,G,B and +0xE0..+0xE8 end R,G,B (ints 0..255);
    // the client interpolates them linearly over the particle's age.
    uint8_t startRGB[3] = {255, 255, 255};
    uint8_t endRGB[3] = {255, 255, 255};

    // The start colour (alpha 255) as an Irrlicht SColor
    irr::video::SColor getColor() const {
        return irr::video::SColor(255, startRGB[0], startRGB[1], startRGB[2]);
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

            // Texture: bytes 64-96 (char[32]; +0x60 starts the flag words)
            emitter.texture = std::string(buffer + 64, 32);
            nullPos = emitter.texture.find('\0');
            if (nullPos != std::string::npos) {
                emitter.texture = emitter.texture.substr(0, nullPos);
            }

            // Words at their code-proven entry offsets (B1141).
            auto f32 = [&](size_t off) { float v; std::memcpy(&v, buffer + off, 4); return v; };
            auto i32 = [&](size_t off) { int32_t v; std::memcpy(&v, buffer + off, 4); return v; };
            emitter.duration = f32(0x78);
            emitter.particleLifetime = f32(0x7C);
            emitter.burstCount = static_cast<uint32_t>(i32(0x80));
            emitter.perEmission = static_cast<uint32_t>(i32(0x84));
            emitter.emissionRate = f32(0x88);
            emitter.shape = i32(0xA8);
            emitter.shapeRadiusX = f32(0xAC);
            emitter.upSpeedMax = f32(0xF0);
            emitter.spriteWidth = f32(0xCC);
            emitter.spriteWidthMax = f32(0x184);
            emitter.worldGravity = f32(0x128);
            for (int i = 0; i < 3; ++i) {
                emitter.startRGB[i] = static_cast<uint8_t>(i32(0xD4 + i * 4));
                emitter.endRGB[i] = static_cast<uint8_t>(i32(0xE0 + i * 4));
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

            // Emitter indices: three 68-byte stages (Source, Middle, Target)
            // at +0x40, each an int sound then four 16-byte slots
            // {emitter, min caster level, anchor actor, attach point}
            // (eqgame.exe 0x495cc3; B1141). 0 = empty slot.
            for (size_t stage = 0; stage < 3; ++stage) {
                for (size_t slot = 0; slot < 4; ++slot) {
                    int32_t emitterIdx;
                    std::memcpy(&emitterIdx, entry + 0x44 + stage * 0x44 + slot * 16, 4);
                    if (emitterIdx > 0) {
                        spell.emitters.push_back(std::to_string(emitterIdx));
                    }
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
