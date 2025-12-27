// Model Viewer Spell Bar - UI component for spell casting visualization
// Part of the standalone model viewer tool

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

        // EFF format has 256-byte entries
        // Each entry has a spell name in first 64 bytes
        const size_t ENTRY_SIZE = 256;

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

            // Skip entries that look like fragments (common issue with this format)
            if (name.find("Effect") != std::string::npos && name.length() < 10) {
                continue;
            }

            SpellEffectEntry spell;
            spell.name = name;

            // Parse emitter indices from bytes 128-256
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

// ============================================================================
// Spell Bar Entry - represents a single spell in the bar
// ============================================================================
struct SpellBarEntry {
    std::string name;           // Display name (e.g., "Fire Bolt")
    ResistType resistType;      // Determines particle color
    SpellSchool school;         // Determines casting animation
    SpellCategory category;     // Determines completion effect style
    float castTime;             // Cast duration in seconds

    // Derived from category - uses authentic EQ casting animations:
    // t04: beneficial spells (buffs, shields, invis)
    // t05: heals, summoning, creating items
    // t06: offensive/detrimental spells (damage, CC)
    std::string getCastAnimation() const {
        switch (category) {
            // Offensive/detrimental -> t06
            case SpellCategory::DirectDamage:
            case SpellCategory::DoT:
            case SpellCategory::AEDamage:
            case SpellCategory::PBAE:
            case SpellCategory::Rain:
            case SpellCategory::Mez:
            case SpellCategory::Root:
            case SpellCategory::Fear:
                return "t06";

            // Heals, summoning, creating -> t05
            case SpellCategory::Heal:
            case SpellCategory::SummonPet:
            case SpellCategory::SummonItem:
                return "t05";

            // Beneficial (buffs, utility) -> t04
            case SpellCategory::Buff:
            case SpellCategory::Gate:
            case SpellCategory::Invisibility:
            default:
                return "t04";
        }
    }

    // Get color based on resist type
    irr::video::SColor getColor() const {
        switch (resistType) {
            case ResistType::Fire:       return irr::video::SColor(255, 255, 100, 0);    // Orange
            case ResistType::Cold:       return irr::video::SColor(255, 0, 200, 255);    // Cyan
            case ResistType::Magic:      return irr::video::SColor(255, 180, 100, 255);  // Purple
            case ResistType::Poison:     return irr::video::SColor(255, 0, 200, 0);      // Green
            case ResistType::Disease:    return irr::video::SColor(255, 139, 90, 43);    // Brown
            case ResistType::Chromatic:
            case ResistType::Prismatic:  return irr::video::SColor(255, 255, 200, 100);  // Gold/Rainbow
            case ResistType::Corruption: return irr::video::SColor(255, 100, 0, 150);    // Dark purple
            case ResistType::Physical:   return irr::video::SColor(255, 150, 150, 150);  // Grey
            case ResistType::None:
            default:                     return irr::video::SColor(255, 255, 255, 255);  // White
        }
    }

    // UI state
    irr::core::recti bounds;    // Screen bounds for hit testing
    bool isHovered = false;
};

// Callback when a spell is clicked
using SpellClickCallback = std::function<void(int spellIndex, const SpellBarEntry& spell)>;

// Callback when a spell cast completes
using CastCompleteCallback = std::function<void(const SpellBarEntry& spell)>;

// ============================================================================
// CastingState - tracks the current spell being cast
// ============================================================================
struct CastingState {
    bool isCasting = false;
    int spellIndex = -1;
    SpellBarEntry spell;        // Copy of the spell being cast
    float castTime = 0;         // Total cast time in seconds
    float elapsed = 0;          // Time elapsed since cast started
    std::string castAnim;       // Animation being played

    // Get progress as 0.0 to 1.0
    float getProgress() const {
        if (castTime <= 0) return 1.0f;
        return std::min(1.0f, elapsed / castTime);
    }

    // Check if cast is complete
    bool isComplete() const {
        return elapsed >= castTime;
    }

    // Reset to not casting
    void reset() {
        isCasting = false;
        spellIndex = -1;
        castTime = 0;
        elapsed = 0;
        castAnim.clear();
    }
};

// ============================================================================
// CastingBar - UI component showing cast progress
// ============================================================================
class CastingBar {
public:
    CastingBar() = default;
    ~CastingBar() = default;

    void initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
        m_driver = driver;
        m_gui = gui;
    }

    void setScreenSize(int width, int height) {
        m_screenWidth = width;
        m_screenHeight = height;
        updateLayout();
    }

    // Start a new cast
    void startCast(const SpellBarEntry& spell) {
        m_state.isCasting = true;
        m_state.spell = spell;
        m_state.castTime = spell.castTime;
        m_state.elapsed = 0;
        m_state.castAnim = spell.getCastAnimation();
        m_visible = true;
    }

    // Update cast progress (call each frame)
    // Returns true if cast just completed this frame
    bool update(float deltaSeconds) {
        if (!m_state.isCasting) return false;

        m_state.elapsed += deltaSeconds;

        if (m_state.isComplete()) {
            return true;  // Cast completed
        }
        return false;
    }

    // Complete the current cast (called after completion effect)
    void completeCast() {
        m_state.reset();
        m_fadeTime = FADE_DURATION;  // Start fade out
    }

    // Cancel/interrupt the current cast
    void cancelCast() {
        m_state.reset();
        m_interrupted = true;
        m_fadeTime = FADE_DURATION;
    }

    // Render the casting bar
    void render() {
        if (!m_driver) return;

        // Handle fade out
        if (!m_state.isCasting && m_fadeTime > 0) {
            m_fadeTime -= 0.016f;  // Assume ~60fps
            if (m_fadeTime <= 0) {
                m_visible = false;
                m_interrupted = false;
                return;
            }
        }

        if (!m_visible) return;

        float alpha = m_state.isCasting ? 1.0f : (m_fadeTime / FADE_DURATION);
        renderBar(alpha);
    }

    // Check if currently casting
    bool isCasting() const { return m_state.isCasting; }

    // Get current casting state
    const CastingState& getState() const { return m_state; }

    // Get the spell being cast
    const SpellBarEntry* getCurrentSpell() const {
        return m_state.isCasting ? &m_state.spell : nullptr;
    }

private:
    void updateLayout() {
        // Position bar at bottom center of screen
        m_barWidth = 300;
        m_barHeight = 20;
        m_barX = (m_screenWidth - m_barWidth) / 2;
        m_barY = m_screenHeight - 100;  // 100 pixels from bottom
    }

    void renderBar(float alpha) {
        irr::u32 a = static_cast<irr::u32>(alpha * 255);

        // Background
        irr::core::recti bgRect(m_barX - 2, m_barY - 2,
                                 m_barX + m_barWidth + 2, m_barY + m_barHeight + 2);
        m_driver->draw2DRectangle(irr::video::SColor(a * 200 / 255, 20, 20, 30), bgRect);

        // Border
        m_driver->draw2DRectangleOutline(bgRect, irr::video::SColor(a, 80, 80, 100));

        // Progress fill
        float progress = m_state.getProgress();
        int fillWidth = static_cast<int>(m_barWidth * progress);

        irr::video::SColor fillColor = m_interrupted ?
            irr::video::SColor(a, 200, 50, 50) :  // Red if interrupted
            m_state.spell.getColor();
        fillColor.setAlpha(a);

        irr::core::recti fillRect(m_barX, m_barY,
                                   m_barX + fillWidth, m_barY + m_barHeight);
        m_driver->draw2DRectangle(fillColor, fillRect);

        // Empty portion (darker)
        if (fillWidth < m_barWidth) {
            irr::core::recti emptyRect(m_barX + fillWidth, m_barY,
                                        m_barX + m_barWidth, m_barY + m_barHeight);
            m_driver->draw2DRectangle(irr::video::SColor(a * 150 / 255, 30, 30, 40), emptyRect);
        }

        // Spell name and time
        if (m_gui) {
            irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
            if (font) {
                // Spell name above bar
                std::wstring nameStr(m_state.spell.name.begin(), m_state.spell.name.end());
                if (m_interrupted) {
                    nameStr += L" - INTERRUPTED";
                }
                irr::core::recti nameRect(m_barX, m_barY - 16, m_barX + m_barWidth, m_barY);
                font->draw(nameStr.c_str(), nameRect, irr::video::SColor(a, 255, 255, 255));

                // Time remaining
                float remaining = std::max(0.0f, m_state.castTime - m_state.elapsed);
                int tenths = static_cast<int>(remaining * 10) % 10;
                int seconds = static_cast<int>(remaining);
                std::wstring timeStr = std::to_wstring(seconds) + L"." + std::to_wstring(tenths) + L"s";
                irr::core::recti timeRect(m_barX + m_barWidth - 40, m_barY + 3,
                                          m_barX + m_barWidth - 2, m_barY + m_barHeight);
                font->draw(timeStr.c_str(), timeRect, irr::video::SColor(a, 200, 200, 200));
            }
        }
    }

    irr::video::IVideoDriver* m_driver = nullptr;
    irr::gui::IGUIEnvironment* m_gui = nullptr;

    CastingState m_state;
    bool m_visible = false;
    bool m_interrupted = false;
    float m_fadeTime = 0;

    int m_screenWidth = 800;
    int m_screenHeight = 600;
    int m_barX = 250;
    int m_barY = 500;
    int m_barWidth = 300;
    int m_barHeight = 20;

    static constexpr float FADE_DURATION = 0.5f;
};

// ============================================================================
// SpellBar - UI component for spell casting visualization
// ============================================================================
class SpellBar {
public:
    SpellBar();
    ~SpellBar() = default;

    // Initialize with Irrlicht components
    void initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Set screen dimensions for layout
    void setScreenSize(int width, int height);

    // Populate with demo spells
    void initializeDefaultSpells();

    // Rendering
    void render();

    // Input handling - returns true if event was consumed
    bool handleMouseMove(int x, int y);
    bool handleMouseClick(int x, int y, bool leftButton);

    // Keyboard shortcuts (1-0 keys)
    bool handleKeyPress(int keyCode);

    // Set callback for spell clicks
    void setSpellClickCallback(SpellClickCallback callback) {
        m_clickCallback = std::move(callback);
    }

    // Get spell at index (for casting)
    const SpellBarEntry* getSpell(int index) const {
        if (index >= 0 && index < static_cast<int>(m_spells.size())) {
            return &m_spells[index];
        }
        return nullptr;
    }

    int getSpellCount() const { return static_cast<int>(m_spells.size()); }

    // Visibility
    void show() { m_visible = true; }
    void hide() { m_visible = false; }
    void toggle() { m_visible = !m_visible; }
    bool isVisible() const { return m_visible; }

private:
    void updateLayout();
    void renderGem(const SpellBarEntry& spell, int index);
    void renderTooltip(const SpellBarEntry& spell);

    irr::video::IVideoDriver* m_driver = nullptr;
    irr::gui::IGUIEnvironment* m_gui = nullptr;

    std::vector<SpellBarEntry> m_spells;
    SpellClickCallback m_clickCallback;

    // Layout
    int m_screenWidth = 800;
    int m_screenHeight = 600;
    int m_barX = 10;          // Left edge position
    int m_barY = 100;         // Starting Y position

    // Gem dimensions
    static constexpr int GEM_SIZE = 40;
    static constexpr int GEM_SPACING = 4;
    static constexpr int GEM_BORDER = 2;

    // State
    bool m_visible = true;
    int m_hoveredIndex = -1;
    int m_mouseX = 0;
    int m_mouseY = 0;
};

// ============================================================================
// Implementation
// ============================================================================

inline SpellBar::SpellBar() {}

inline void SpellBar::initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
    m_driver = driver;
    m_gui = gui;
    initializeDefaultSpells();
    updateLayout();
}

inline void SpellBar::setScreenSize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;
    updateLayout();
}

inline void SpellBar::initializeDefaultSpells() {
    m_spells.clear();

    // Representative spells covering different resist types and categories
    // Based on the plan document

    m_spells.push_back({"Fire Bolt", ResistType::Fire, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 2.0f});

    m_spells.push_back({"Frost Shock", ResistType::Cold, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 1.5f});

    m_spells.push_back({"Lightning Bolt", ResistType::Magic, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 2.5f});

    m_spells.push_back({"Poison Bolt", ResistType::Poison, SpellSchool::Conjuration,
                        SpellCategory::DoT, 2.0f});

    m_spells.push_back({"Plague", ResistType::Disease, SpellSchool::Conjuration,
                        SpellCategory::DoT, 3.0f});

    m_spells.push_back({"Chromatic Flash", ResistType::Chromatic, SpellSchool::Evocation,
                        SpellCategory::AEDamage, 3.5f});

    m_spells.push_back({"Greater Heal", ResistType::None, SpellSchool::Alteration,
                        SpellCategory::Heal, 4.0f});

    m_spells.push_back({"Spirit Armor", ResistType::None, SpellSchool::Abjuration,
                        SpellCategory::Buff, 3.0f});

    m_spells.push_back({"Summon Pet", ResistType::None, SpellSchool::Conjuration,
                        SpellCategory::SummonPet, 8.0f});

    m_spells.push_back({"Root", ResistType::Magic, SpellSchool::Alteration,
                        SpellCategory::Root, 1.5f});

    updateLayout();
}

inline void SpellBar::updateLayout() {
    // Position spell bar vertically centered on left side
    int totalHeight = static_cast<int>(m_spells.size()) * (GEM_SIZE + GEM_SPACING) - GEM_SPACING;
    m_barY = (m_screenHeight - totalHeight) / 2;

    // Update bounds for each spell
    for (size_t i = 0; i < m_spells.size(); ++i) {
        int y = m_barY + static_cast<int>(i) * (GEM_SIZE + GEM_SPACING);
        m_spells[i].bounds = irr::core::recti(
            m_barX, y,
            m_barX + GEM_SIZE, y + GEM_SIZE
        );
    }
}

inline void SpellBar::render() {
    if (!m_visible || !m_driver) return;

    // Draw semi-transparent background panel
    int panelPadding = 6;
    int totalHeight = static_cast<int>(m_spells.size()) * (GEM_SIZE + GEM_SPACING) - GEM_SPACING;
    irr::core::recti panelRect(
        m_barX - panelPadding,
        m_barY - panelPadding,
        m_barX + GEM_SIZE + panelPadding,
        m_barY + totalHeight + panelPadding
    );
    m_driver->draw2DRectangle(irr::video::SColor(160, 20, 20, 30), panelRect);

    // Draw border around panel
    m_driver->draw2DRectangleOutline(panelRect, irr::video::SColor(200, 80, 80, 100));

    // Render each gem
    for (size_t i = 0; i < m_spells.size(); ++i) {
        renderGem(m_spells[i], static_cast<int>(i));
    }

    // Render tooltip for hovered spell
    if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_spells.size())) {
        renderTooltip(m_spells[m_hoveredIndex]);
    }
}

inline void SpellBar::renderGem(const SpellBarEntry& spell, int index) {
    const irr::core::recti& bounds = spell.bounds;

    // Gem background (darker)
    irr::video::SColor bgColor(220, 30, 30, 40);
    if (spell.isHovered) {
        bgColor = irr::video::SColor(240, 50, 50, 70);
    }
    m_driver->draw2DRectangle(bgColor, bounds);

    // Gem colored inner area (based on resist type)
    irr::video::SColor gemColor = spell.getColor();
    if (spell.isHovered) {
        // Brighten on hover
        gemColor.setAlpha(255);
    } else {
        gemColor.setAlpha(200);
    }

    irr::core::recti innerRect(
        bounds.UpperLeftCorner.X + GEM_BORDER,
        bounds.UpperLeftCorner.Y + GEM_BORDER,
        bounds.LowerRightCorner.X - GEM_BORDER,
        bounds.LowerRightCorner.Y - GEM_BORDER
    );
    m_driver->draw2DRectangle(gemColor, innerRect);

    // Border
    irr::video::SColor borderColor = spell.isHovered ?
        irr::video::SColor(255, 255, 255, 200) :
        irr::video::SColor(200, 100, 100, 120);
    m_driver->draw2DRectangleOutline(bounds, borderColor);

    // Draw slot number in corner
    if (m_gui) {
        irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
        if (font) {
            std::wstring numStr = std::to_wstring((index + 1) % 10);  // 1-9, 0
            irr::core::recti numRect(
                bounds.UpperLeftCorner.X + 2,
                bounds.UpperLeftCorner.Y + 1,
                bounds.UpperLeftCorner.X + 12,
                bounds.UpperLeftCorner.Y + 12
            );
            font->draw(numStr.c_str(), numRect, irr::video::SColor(200, 200, 200, 200));
        }
    }
}

inline void SpellBar::renderTooltip(const SpellBarEntry& spell) {
    if (!m_gui) return;

    irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
    if (!font) return;

    // Build tooltip text
    std::wstring nameStr(spell.name.begin(), spell.name.end());

    // Get school name
    std::wstring schoolStr;
    switch (spell.school) {
        case SpellSchool::Abjuration:  schoolStr = L"Abjuration"; break;
        case SpellSchool::Alteration:  schoolStr = L"Alteration"; break;
        case SpellSchool::Conjuration: schoolStr = L"Conjuration"; break;
        case SpellSchool::Divination:  schoolStr = L"Divination"; break;
        case SpellSchool::Evocation:   schoolStr = L"Evocation"; break;
    }

    // Get resist type name
    std::wstring resistStr;
    switch (spell.resistType) {
        case ResistType::None:       resistStr = L"Unresistable"; break;
        case ResistType::Magic:      resistStr = L"Magic"; break;
        case ResistType::Fire:       resistStr = L"Fire"; break;
        case ResistType::Cold:       resistStr = L"Cold"; break;
        case ResistType::Poison:     resistStr = L"Poison"; break;
        case ResistType::Disease:    resistStr = L"Disease"; break;
        case ResistType::Chromatic:  resistStr = L"Chromatic"; break;
        case ResistType::Prismatic:  resistStr = L"Prismatic"; break;
        case ResistType::Physical:   resistStr = L"Physical"; break;
        case ResistType::Corruption: resistStr = L"Corruption"; break;
    }

    std::wstring castStr = L"Cast: " + std::to_wstring(static_cast<int>(spell.castTime * 10) / 10) + L"." +
                           std::to_wstring(static_cast<int>(spell.castTime * 10) % 10) + L"s";

    // Position tooltip to the right of the spell bar
    int tooltipX = m_barX + GEM_SIZE + 10;
    int tooltipY = spell.bounds.UpperLeftCorner.Y;
    int tooltipWidth = 150;
    int tooltipHeight = 52;

    // Background
    irr::core::recti tooltipRect(tooltipX, tooltipY, tooltipX + tooltipWidth, tooltipY + tooltipHeight);
    m_driver->draw2DRectangle(irr::video::SColor(230, 20, 20, 30), tooltipRect);
    m_driver->draw2DRectangleOutline(tooltipRect, irr::video::SColor(255, 100, 100, 120));

    // Text
    font->draw(nameStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 2, tooltipX + tooltipWidth - 4, tooltipY + 14),
               spell.getColor());

    font->draw((schoolStr + L" - " + resistStr).c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 16, tooltipX + tooltipWidth - 4, tooltipY + 28),
               irr::video::SColor(255, 180, 180, 180));

    font->draw(castStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 30, tooltipX + tooltipWidth - 4, tooltipY + 42),
               irr::video::SColor(255, 150, 150, 150));

    // Animation code
    std::string animCode = spell.getCastAnimation();
    std::wstring animStr = L"Anim: " + std::wstring(animCode.begin(), animCode.end());
    font->draw(animStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 40, tooltipX + tooltipWidth - 4, tooltipY + 52),
               irr::video::SColor(255, 120, 120, 120));
}

inline bool SpellBar::handleMouseMove(int x, int y) {
    if (!m_visible) return false;

    m_mouseX = x;
    m_mouseY = y;

    int previousHovered = m_hoveredIndex;
    m_hoveredIndex = -1;

    for (size_t i = 0; i < m_spells.size(); ++i) {
        m_spells[i].isHovered = false;
        if (m_spells[i].bounds.isPointInside(irr::core::position2di(x, y))) {
            m_hoveredIndex = static_cast<int>(i);
            m_spells[i].isHovered = true;
        }
    }

    // Return true if we're over the spell bar (to prevent other input handling)
    return m_hoveredIndex >= 0 || previousHovered >= 0;
}

inline bool SpellBar::handleMouseClick(int x, int y, bool leftButton) {
    if (!m_visible || !leftButton) return false;

    for (size_t i = 0; i < m_spells.size(); ++i) {
        if (m_spells[i].bounds.isPointInside(irr::core::position2di(x, y))) {
            if (m_clickCallback) {
                m_clickCallback(static_cast<int>(i), m_spells[i]);
            }
            return true;
        }
    }

    return false;
}

inline bool SpellBar::handleKeyPress(int keyCode) {
    if (!m_visible) return false;

    // Handle 1-9, 0 keys (Irrlicht KEY_KEY_1 = 0x31, KEY_KEY_0 = 0x30)
    int spellIndex = -1;

    if (keyCode >= 0x31 && keyCode <= 0x39) {
        // Keys 1-9 -> spell indices 0-8
        spellIndex = keyCode - 0x31;
    } else if (keyCode == 0x30) {
        // Key 0 -> spell index 9
        spellIndex = 9;
    }

    if (spellIndex >= 0 && spellIndex < static_cast<int>(m_spells.size())) {
        if (m_clickCallback) {
            m_clickCallback(spellIndex, m_spells[spellIndex]);
        }
        return true;
    }

    return false;
}

// ============================================================================
// ModelViewerFX - Particle effects for spell casting visualization
// ============================================================================
class ModelViewerFX {
public:
    ModelViewerFX() = default;
    ~ModelViewerFX() {
        // Don't call clearAllEffects() here - the scene manager may already be destroyed
        // if we're being killed by SIGTERM or during abnormal exit.
        // clearAllEffects() should be called explicitly before device->drop().
    }

    void initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    const std::string& eqClientPath) {
        m_smgr = smgr;
        m_driver = driver;
        m_eqClientPath = eqClientPath;
        loadTextures();

        // Load spell effect database (EDD/EFF files)
        if (m_spellEffectDB.load(eqClientPath)) {
            std::cout << "Loaded spell effect database: "
                      << m_spellEffectDB.eddParser().size() << " emitters, "
                      << m_spellEffectDB.effParser().size() << " spell effects" << std::endl;
        }
    }

    // Get authentic emitter parameters for a spell
    const EmitterDefinition* getAuthenticEmitter(const SpellBarEntry& spell) const {
        // First try to find by resist type
        const EmitterDefinition* emitter = m_spellEffectDB.getEmitterForResistType(spell.resistType);
        if (emitter) return emitter;

        // Fall back to category-based lookup
        return m_spellEffectDB.getEmitterForCategory(spell.category);
    }

    // Access to spell effect database
    const SpellEffectDatabase& getSpellEffectDB() const { return m_spellEffectDB; }
    SpellEffectDatabase& getSpellEffectDB() { return m_spellEffectDB; }

    // Create casting effect using a specific EmitterDefinition
    void createCastingEffectFromEmitter(Graphics::EQAnimatedMeshSceneNode* animNode,
                                         const EmitterDefinition& emitter,
                                         const SpellBarEntry& spell) {
        if (!m_smgr || !animNode) return;

        m_animatedNode = animNode;
        m_currentSpellColor = emitter.getColor();  // Use emitter color
        m_currentResistType = spell.resistType;

        // Find hand bone indices
        m_rightHandBoneIndex = animNode->findRightHandBoneIndex();
        m_leftHandBoneIndex = animNode->findLeftHandBoneIndex();

        std::cout << "Using emitter: " << emitter.name << " (texture: " << emitter.texture << ")" << std::endl;
        std::cout << "  Particles: " << emitter.particleCount1 << "-" << emitter.particleCount2
                  << ", Radius: " << emitter.emitRadius << ", Velocity: " << emitter.velocity << std::endl;

        // Get initial hand positions
        irr::core::vector3df rightPos, leftPos;
        bool hasRightHand = (m_rightHandBoneIndex >= 0) &&
                            animNode->getBoneWorldPosition(m_rightHandBoneIndex, rightPos);
        bool hasLeftHand = (m_leftHandBoneIndex >= 0) &&
                           animNode->getBoneWorldPosition(m_leftHandBoneIndex, leftPos);

        // Create particle systems using emitter definition
        if (hasRightHand) {
            m_rightHandEffect = createHandParticleSystemFromEmitter(rightPos, emitter);
        }
        if (hasLeftHand) {
            m_leftHandEffect = createHandParticleSystemFromEmitter(leftPos, emitter);
        }

        // Create body glow using emitter colors
        createBodyGlowEffectFromEmitter(animNode, emitter);
    }

    // Create casting effect with hand-emanating particles (Phase 4)
    // Uses EQAnimatedMeshSceneNode to get hand bone positions
    void createCastingEffect(Graphics::EQAnimatedMeshSceneNode* animNode, const SpellBarEntry& spell) {
        if (!m_smgr || !animNode) return;

        m_animatedNode = animNode;
        m_currentSpellColor = spell.getColor();
        m_currentResistType = spell.resistType;

        // Find hand bone indices
        m_rightHandBoneIndex = animNode->findRightHandBoneIndex();
        m_leftHandBoneIndex = animNode->findLeftHandBoneIndex();

        std::cout << "Hand bone indices: R=" << m_rightHandBoneIndex
                  << " L=" << m_leftHandBoneIndex << std::endl;

        // Get initial hand positions
        irr::core::vector3df rightPos, leftPos;
        bool hasRightHand = (m_rightHandBoneIndex >= 0) &&
                            animNode->getBoneWorldPosition(m_rightHandBoneIndex, rightPos);
        bool hasLeftHand = (m_leftHandBoneIndex >= 0) &&
                           animNode->getBoneWorldPosition(m_leftHandBoneIndex, leftPos);

        // Create particle systems for each hand
        if (hasRightHand) {
            m_rightHandEffect = createHandParticleSystem(rightPos, spell);
            std::cout << "Created right hand particles at: " << rightPos.X << ", "
                      << rightPos.Y << ", " << rightPos.Z << std::endl;
        }

        if (hasLeftHand) {
            m_leftHandEffect = createHandParticleSystem(leftPos, spell);
            std::cout << "Created left hand particles at: " << leftPos.X << ", "
                      << leftPos.Y << ", " << leftPos.Z << std::endl;
        }

        // Also create body glow effect for visual richness
        createBodyGlowEffect(animNode, spell);
    }

    // Backwards-compatible overload for non-animated nodes
    void createCastingEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        if (!m_smgr || !casterNode) return;

        // Clear any previous animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;

        // Fall back to body-centered effect
        createBodyGlowEffect(casterNode, spell);
    }

private:
    // Create particle system at hand position
    irr::scene::IParticleSystemSceneNode* createHandParticleSystem(
        const irr::core::vector3df& pos, const SpellBarEntry& spell) {

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return nullptr;

        irr::video::SColor color = spell.getColor();

        // Point emitter at hand with outward/upward direction
        irr::scene::IParticleEmitter* emitter = ps->createPointEmitter(
            irr::core::vector3df(0, 0.03f, 0),    // Slight upward direction
            40, 60,                                // Higher particle rate for visibility
            color, color,                          // Color based on spell type
            300, 600,                              // Shorter lifetime for hand effects
            30,                                    // Max angle for spread
            irr::core::dimension2df(0.6f, 0.6f),  // Min size
            irr::core::dimension2df(1.2f, 1.2f)   // Max size
        );

        ps->setEmitter(emitter);
        emitter->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), 150);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Set texture
        irr::video::ITexture* tex = getTextureForResistType(spell.resistType);
        if (tex) {
            ps->setMaterialTexture(0, tex);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        return ps;
    }

    // Create body glow effect (used alongside hand effects or as fallback)
    void createBodyGlowEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        // Get caster position
        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;  // Offset above character center

        // Create particle system for casting glow
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor color = spell.getColor();

        // Box emitter around caster
        irr::scene::IParticleEmitter* emitter = ps->createBoxEmitter(
            irr::core::aabbox3df(-1.5f, -2.0f, -1.5f, 1.5f, 2.0f, 1.5f),  // Box around caster
            irr::core::vector3df(0, 0.02f, 0),     // Slight upward direction
            20, 35,                                  // Lower rate since we have hand effects too
            color, color,                            // Color
            500, 1000,                               // Lifetime ms
            45,                                      // Max angle
            irr::core::dimension2df(0.6f, 0.6f),    // Min size
            irr::core::dimension2df(1.2f, 1.2f)     // Max size
        );

        ps->setEmitter(emitter);
        emitter->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), 200);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Set texture
        irr::video::ITexture* tex = getTextureForResistType(spell.resistType);
        if (tex) {
            ps->setMaterialTexture(0, tex);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_castingEffect = ps;
        m_castingNode = casterNode;
    }

    // Create hand particle system using EmitterDefinition parameters
    irr::scene::IParticleSystemSceneNode* createHandParticleSystemFromEmitter(
        const irr::core::vector3df& pos, const EmitterDefinition& emitter) {

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return nullptr;

        irr::video::SColor color = emitter.getColor();

        // Use emitter parameters with sensible defaults
        int minRate = std::max(10u, emitter.particleCount1 / 4);
        int maxRate = std::max(20u, emitter.particleCount2 / 4);
        float velocity = std::max(0.02f, emitter.velocity * 0.01f);
        int lifetime = static_cast<int>(std::max(200.0f, emitter.lifetime * 200.0f));
        float sizeMin = std::max(0.3f, emitter.sizeStart * 0.5f);
        float sizeMax = std::max(0.6f, emitter.sizeEnd * 0.5f);

        // Point emitter at hand
        irr::scene::IParticleEmitter* em = ps->createPointEmitter(
            irr::core::vector3df(0, velocity, 0),
            minRate, maxRate,
            color, color,
            lifetime, lifetime * 2,
            30,
            irr::core::dimension2df(sizeMin, sizeMin),
            irr::core::dimension2df(sizeMax, sizeMax)
        );

        ps->setEmitter(em);
        em->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), lifetime / 2);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Use default texture (TODO: load emitter.texture from SpellEffects folder)
        if (m_defaultTexture) {
            ps->setMaterialTexture(0, m_defaultTexture);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        return ps;
    }

    // Create body glow using EmitterDefinition
    void createBodyGlowEffectFromEmitter(irr::scene::ISceneNode* casterNode, const EmitterDefinition& emitter) {
        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor color = emitter.getColor();

        // Use emitter parameters
        int minRate = std::max(15u, emitter.particleCount1 / 3);
        int maxRate = std::max(30u, emitter.particleCount2 / 3);
        float radius = std::max(1.5f, emitter.emitRadius * 0.1f);
        int lifetime = static_cast<int>(std::max(400.0f, emitter.lifetime * 300.0f));
        float sizeMin = std::max(0.5f, emitter.sizeStart * 0.4f);
        float sizeMax = std::max(1.0f, emitter.sizeEnd * 0.4f);

        irr::scene::IParticleEmitter* em = ps->createBoxEmitter(
            irr::core::aabbox3df(-radius, -2.0f, -radius, radius, 2.0f, radius),
            irr::core::vector3df(0, 0.02f, 0),
            minRate, maxRate,
            color, color,
            lifetime, lifetime * 2,
            45,
            irr::core::dimension2df(sizeMin, sizeMin),
            irr::core::dimension2df(sizeMax, sizeMax)
        );

        ps->setEmitter(em);
        em->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), lifetime / 2);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        if (m_defaultTexture) {
            ps->setMaterialTexture(0, m_defaultTexture);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_castingEffect = ps;
        m_castingNode = casterNode;
    }

public:

    // Create completion effect based on spell category
    void createCompletionEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        if (!m_smgr || !casterNode) return;

        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;

        irr::video::SColor color = spell.getColor();

        // Different effects based on category
        switch (spell.category) {
            case SpellCategory::DirectDamage:
            case SpellCategory::AEDamage:
            case SpellCategory::PBAE:
                createBurstEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::DoT:
                createLingeringEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::Heal:
                createHealEffect(pos, spell.resistType);
                break;

            case SpellCategory::Buff:
                createBuffEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::SummonPet:
            case SpellCategory::Gate:
                createPortalEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::Root:
            case SpellCategory::Mez:
            case SpellCategory::Stun:
                createCCEffect(pos, color, spell.resistType);
                break;

            default:
                createBurstEffect(pos, color, spell.resistType);
                break;
        }
    }

    // Stop casting effect
    void stopCastingEffect() {
        // Stop body glow effect
        if (m_castingEffect) {
            m_castingEffect->setEmitter(nullptr);  // Stop emitting
            // Schedule for removal after particles fade
            m_pendingRemoval.push_back({m_castingEffect, 1.5f});
            m_castingEffect = nullptr;
            m_castingNode = nullptr;
        }

        // Stop hand effects
        if (m_rightHandEffect) {
            m_rightHandEffect->setEmitter(nullptr);
            m_pendingRemoval.push_back({m_rightHandEffect, 1.0f});
            m_rightHandEffect = nullptr;
        }

        if (m_leftHandEffect) {
            m_leftHandEffect->setEmitter(nullptr);
            m_pendingRemoval.push_back({m_leftHandEffect, 1.0f});
            m_leftHandEffect = nullptr;
        }

        // Clear animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;
    }

    // Update effects (call each frame)
    void update(float deltaSeconds) {
        // Update hand effect positions to follow hand bones
        if (m_animatedNode) {
            irr::core::vector3df pos;

            // Update right hand particles
            if (m_rightHandEffect && m_rightHandBoneIndex >= 0) {
                if (m_animatedNode->getBoneWorldPosition(m_rightHandBoneIndex, pos)) {
                    m_rightHandEffect->setPosition(pos);
                }
            }

            // Update left hand particles
            if (m_leftHandEffect && m_leftHandBoneIndex >= 0) {
                if (m_animatedNode->getBoneWorldPosition(m_leftHandBoneIndex, pos)) {
                    m_leftHandEffect->setPosition(pos);
                }
            }
        }

        // Update casting effect position to follow caster
        if (m_castingEffect && m_castingNode) {
            irr::core::vector3df pos = m_castingNode->getAbsolutePosition();
            pos.Y += 3.0f;
            m_castingEffect->setPosition(pos);
        }

        // Update completion effects
        for (auto it = m_completionEffects.begin(); it != m_completionEffects.end(); ) {
            it->lifetime -= deltaSeconds;
            if (it->lifetime <= 0) {
                if (it->ps) {
                    it->ps->remove();
                }
                it = m_completionEffects.erase(it);
            } else {
                ++it;
            }
        }

        // Update pending removals
        for (auto it = m_pendingRemoval.begin(); it != m_pendingRemoval.end(); ) {
            it->second -= deltaSeconds;
            if (it->second <= 0) {
                if (it->first) {
                    it->first->remove();
                }
                it = m_pendingRemoval.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Clear all effects
    void clearAllEffects() {
        // Clear hand effects first (stopCastingEffect will handle them too)
        if (m_rightHandEffect) {
            m_rightHandEffect->remove();
            m_rightHandEffect = nullptr;
        }
        if (m_leftHandEffect) {
            m_leftHandEffect->remove();
            m_leftHandEffect = nullptr;
        }

        stopCastingEffect();
        for (auto& effect : m_completionEffects) {
            if (effect.ps) {
                effect.ps->remove();
            }
        }
        m_completionEffects.clear();
        for (auto& pr : m_pendingRemoval) {
            if (pr.first) {
                pr.first->remove();
            }
        }
        m_pendingRemoval.clear();

        // Clear animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;
    }

private:
    struct CompletionEffect {
        irr::scene::IParticleSystemSceneNode* ps = nullptr;
        float lifetime = 0;
    };

    void loadTextures() {
        if (!m_driver) return;

        std::string path = m_eqClientPath;
        if (!path.empty() && path.back() != '/') path += '/';
        path += "SpellEffects/";

        // Load textures for each resist type
        m_defaultTexture = loadDDSTexture(path + "Glowflare03.dds");
        if (!m_defaultTexture) m_defaultTexture = loadDDSTexture(path + "glittersp501.dds");
        if (!m_defaultTexture) createFallbackTexture();

        m_fireTexture = loadDDSTexture(path + "Firesp501.dds");
        if (!m_fireTexture) m_fireTexture = loadDDSTexture(path + "flamesp501.dds");

        m_coldTexture = loadDDSTexture(path + "frostsp501.dds");
        if (!m_coldTexture) m_coldTexture = loadDDSTexture(path + "frostsp502.dds");

        m_magicTexture = loadDDSTexture(path + "ElectricA.dds");
        if (!m_magicTexture) m_magicTexture = loadDDSTexture(path + "ElectricB.dds");

        m_poisonTexture = loadDDSTexture(path + "PoisonC.dds");
        if (!m_poisonTexture) m_poisonTexture = loadDDSTexture(path + "Acid1.dds");

        m_diseaseTexture = loadDDSTexture(path + "diseasesp501.dds");
        if (!m_diseaseTexture) m_diseaseTexture = loadDDSTexture(path + "genbrownA.dds");

        m_chromaticTexture = loadDDSTexture(path + "Corona3.dds");
        m_corruptionTexture = loadDDSTexture(path + "darknesssp501.dds");

        // Fallback to default if not loaded
        if (!m_fireTexture) m_fireTexture = m_defaultTexture;
        if (!m_coldTexture) m_coldTexture = m_defaultTexture;
        if (!m_magicTexture) m_magicTexture = m_defaultTexture;
        if (!m_poisonTexture) m_poisonTexture = m_defaultTexture;
        if (!m_diseaseTexture) m_diseaseTexture = m_defaultTexture;
        if (!m_chromaticTexture) m_chromaticTexture = m_defaultTexture;
        if (!m_corruptionTexture) m_corruptionTexture = m_defaultTexture;

        std::cout << "Loaded spell effect textures" << std::endl;
    }

    irr::video::ITexture* loadDDSTexture(const std::string& path) {
        if (!m_driver) return nullptr;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cout << "Could not load texture: " << path << std::endl;
            return nullptr;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> data(fileSize);
        if (!file.read(data.data(), fileSize)) {
            std::cout << "Could not read texture: " << path << std::endl;
            return nullptr;
        }

        // Use DDSDecoder to decode the DDS file
        if (!Graphics::DDSDecoder::isDDS(data)) {
            std::cout << "Not a valid DDS file: " << path << std::endl;
            return nullptr;
        }

        Graphics::DecodedImage decoded = Graphics::DDSDecoder::decode(data);
        if (!decoded.isValid()) {
            std::cout << "Failed to decode DDS: " << path << std::endl;
            return nullptr;
        }

        // Create Irrlicht image from decoded RGBA data
        irr::core::dimension2du dim(decoded.width, decoded.height);
        irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
        if (!image) {
            std::cout << "Failed to create image for: " << path << std::endl;
            return nullptr;
        }

        // Copy decoded pixels to Irrlicht image (RGBA to ARGB)
        uint8_t* dest = static_cast<uint8_t*>(image->lock());
        if (dest) {
            const uint8_t* src = decoded.pixels.data();
            for (size_t i = 0; i < decoded.width * decoded.height; ++i) {
                // Source is RGBA, dest is ARGB (Irrlicht's ECF_A8R8G8B8)
                dest[i * 4 + 0] = src[i * 4 + 2]; // B
                dest[i * 4 + 1] = src[i * 4 + 1]; // G
                dest[i * 4 + 2] = src[i * 4 + 0]; // R
                dest[i * 4 + 3] = src[i * 4 + 3]; // A
            }
            image->unlock();
        }

        // Extract filename for texture name
        std::string texName = path;
        size_t lastSlash = texName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            texName = texName.substr(lastSlash + 1);
        }

        // Create texture from image
        irr::video::ITexture* tex = m_driver->addTexture(texName.c_str(), image);
        image->drop();

        if (tex) {
            std::cout << "Loaded spell texture: " << texName << " (" << decoded.width << "x" << decoded.height << ")" << std::endl;
        }

        return tex;
    }

    void createFallbackTexture() {
        if (!m_driver) return;

        const int size = 32;
        irr::core::dimension2du dim(size, size);
        irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
        if (!image) return;

        float center = size / 2.0f;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float dx = x - center + 0.5f;
                float dy = y - center + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float normalizedDist = dist / center;
                int alpha = 0;
                if (normalizedDist < 1.0f) {
                    float falloff = std::cos(normalizedDist * 3.14159f * 0.5f);
                    alpha = static_cast<int>(falloff * 255.0f);
                }
                image->setPixel(x, y, irr::video::SColor(alpha, 255, 255, 255));
            }
        }

        m_defaultTexture = m_driver->addTexture("spell_fallback", image);
        image->drop();
    }

    irr::video::ITexture* getTextureForResistType(ResistType type) {
        switch (type) {
            case ResistType::Fire:       return m_fireTexture;
            case ResistType::Cold:       return m_coldTexture;
            case ResistType::Magic:      return m_magicTexture;
            case ResistType::Poison:     return m_poisonTexture;
            case ResistType::Disease:    return m_diseaseTexture;
            case ResistType::Chromatic:
            case ResistType::Prismatic:  return m_chromaticTexture;
            case ResistType::Corruption: return m_corruptionTexture;
            default:                     return m_defaultTexture;
        }
    }

    // Burst effect for damage spells
    void createBurstEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Ring emitter for expanding burst
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            0.5f, 3.0f,                              // Expanding ring
            irr::core::vector3df(0, 0.05f, 0),       // Slight upward
            80, 120,                                  // High particle rate
            color, color,
            200, 400,
            60,
            irr::core::dimension2df(1.5f, 1.5f),
            irr::core::dimension2df(3.0f, 3.0f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        // Track effect lifetime - particles will fade naturally
        // Don't add to m_pendingRemoval since that would double-remove the node
        m_completionEffects.push_back({ps, 1.5f});
    }

    // Lingering effect for DoTs
    void createLingeringEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Cylinder emitter for cloud effect
        auto* emitter = ps->createCylinderEmitter(
            irr::core::vector3df(0, 0, 0),
            2.0f, irr::core::vector3df(0, 1, 0), 3.0f,  // Radius, normal, length
            false,                                       // Not outline only
            irr::core::vector3df(0, 0.01f, 0),          // Slow rise
            20, 40,                                      // Lower particle rate
            color, color,
            800, 1500,                                   // Longer lifetime
            30,
            irr::core::dimension2df(1.0f, 1.0f),
            irr::core::dimension2df(2.5f, 2.5f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.5f});
    }

    // Rising sparkles for heals
    void createHealEffect(const irr::core::vector3df& pos, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor healColor(255, 200, 255, 200);  // Light green/gold

        auto* emitter = ps->createBoxEmitter(
            irr::core::aabbox3df(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f),
            irr::core::vector3df(0, 0.08f, 0),          // Rise upward
            40, 70,
            healColor, irr::video::SColor(255, 255, 255, 150),
            600, 1200,
            20,
            irr::core::dimension2df(0.5f, 0.5f),
            irr::core::dimension2df(1.2f, 1.2f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.0f});
    }

    // Swirling aura for buffs
    void createBuffEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Ring emitter for swirling effect
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            1.5f, 2.5f,
            irr::core::vector3df(0, 0.03f, 0),
            30, 50,
            color, color,
            600, 1000,
            40,
            irr::core::dimension2df(0.8f, 0.8f),
            irr::core::dimension2df(1.5f, 1.5f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        // Rotation affector for swirl
        auto* rotAff = ps->createRotationAffector(irr::core::vector3df(0, 50.0f, 0));
        ps->addAffector(rotAff);
        rotAff->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.0f});
    }

    // Portal effect for summons/gates
    void createPortalEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        irr::core::vector3df groundPos = pos;
        groundPos.Y -= 2.0f;  // On ground

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, groundPos);
        if (!ps) return;

        // Ring emitter on ground
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            0.2f, 2.0f,
            irr::core::vector3df(0, 0.1f, 0),          // Rise from ground
            60, 100,
            color, irr::video::SColor(255, 150, 150, 255),
            400, 800,
            10,
            irr::core::dimension2df(1.0f, 1.0f),
            irr::core::dimension2df(2.0f, 2.0f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 3.0f});
    }

    // CC effect (roots, mez, stun)
    void createCCEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Sphere emitter around target
        auto* emitter = ps->createSphereEmitter(
            irr::core::vector3df(0, 0, 0),
            2.0f,                                        // Radius
            irr::core::vector3df(0, 0, 0),              // No direction (orbiting)
            15, 30,                                      // Low rate
            color, color,
            1000, 2000,                                  // Long lifetime
            20,
            irr::core::dimension2df(0.6f, 0.6f),
            irr::core::dimension2df(1.2f, 1.2f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        // Attraction to center for orbiting effect
        auto* attrAff = ps->createAttractionAffector(pos, 5.0f);
        ps->addAffector(attrAff);
        attrAff->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.5f});
    }

    irr::scene::ISceneManager* m_smgr = nullptr;
    irr::video::IVideoDriver* m_driver = nullptr;
    std::string m_eqClientPath;

    // Textures by resist type
    irr::video::ITexture* m_defaultTexture = nullptr;
    irr::video::ITexture* m_fireTexture = nullptr;
    irr::video::ITexture* m_coldTexture = nullptr;
    irr::video::ITexture* m_magicTexture = nullptr;
    irr::video::ITexture* m_poisonTexture = nullptr;
    irr::video::ITexture* m_diseaseTexture = nullptr;
    irr::video::ITexture* m_chromaticTexture = nullptr;
    irr::video::ITexture* m_corruptionTexture = nullptr;

    // Active effects
    irr::scene::IParticleSystemSceneNode* m_castingEffect = nullptr;
    irr::scene::ISceneNode* m_castingNode = nullptr;
    std::vector<CompletionEffect> m_completionEffects;
    std::vector<std::pair<irr::scene::IParticleSystemSceneNode*, float>> m_pendingRemoval;

    // Hand-emanating particle effects (Phase 4)
    Graphics::EQAnimatedMeshSceneNode* m_animatedNode = nullptr;
    irr::scene::IParticleSystemSceneNode* m_rightHandEffect = nullptr;
    irr::scene::IParticleSystemSceneNode* m_leftHandEffect = nullptr;
    int m_rightHandBoneIndex = -1;
    int m_leftHandBoneIndex = -1;
    irr::video::SColor m_currentSpellColor;
    ResistType m_currentResistType = ResistType::None;

    // Spell effect database (Phase 5 - EDD/EFF parsers)
    SpellEffectDatabase m_spellEffectDB;
};

} // namespace EQT
