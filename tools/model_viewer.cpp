// Standalone EQ Character Model Viewer
// For debugging model/texture rendering issues
// Keys:
//   Arrow keys: Rotate model (Left/Right = Y axis, Up/Down = X axis)
//   Q/E: Rotate model on Z axis
//   +/-: Zoom in/out
//   V: Cycle body variant (static mode)
//   H: Cycle head variant (static mode)
//   B/N: Cycle body texture (prev/next) - 0=naked, 1=leather, 2=chain, 3=plate, 10+=robes
//   G/J: Cycle helm texture (prev/next)
//   A: Cycle animations (next)
//   Z: Cycle animations (previous)
//   Space: Pause/Resume animation
//   [/]: Decrease/Increase animation speed
//   W: Toggle wireframe
//   U: Toggle texture U flip (X axis mirror)
//   R: Reset rotation
//   S: Toggle static/animated mesh mode
//   ,/.: Cycle entities (prev/next)
//   F12: Screenshot (saves to model_viewer.png)
//   ESC: Quit

#include <irrlicht.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <regex>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/entity_renderer.h"  // For EntityAppearance
#include "common/logging.h"
#include "model_viewer_spell_bar.h"  // Spell bar UI for casting visualization

// Spell test mode includes
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_constants.h"
#include "client/graphics/spell_visual_fx.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#endif

using namespace irr;
using namespace EQT::Graphics;

// Global state
bool swapXY = false;
bool wireframe = false;
bool flipU = false;  // Flip texture U coordinate (X axis)
bool filterOutliers = false;  // Filter out vertices with extreme values
float outlierThreshold = 1.0f;  // Max coordinate value to consider valid
float cameraDistance = 75.0f;  // Camera distance from model
scene::IMeshSceneNode* modelNode = nullptr;
EQAnimatedMeshSceneNode* animatedNode = nullptr;
scene::ICameraSceneNode* camera = nullptr;
video::IVideoDriver* driver = nullptr;
scene::ISceneManager* smgr = nullptr;
io::IFileSystem* fileSystem = nullptr;
IrrlichtDevice* device = nullptr;

// Animation state
bool useAnimatedMesh = false;  // Start with static mesh mode
std::vector<std::string> availableAnimations;
int currentAnimIndex = -1;  // -1 = no animation (pose)
bool animationPaused = false;
float globalAnimationSpeed = 1.0f;  // 1.0 = normal speed

// Character data
std::shared_ptr<ZoneGeometry> characterGeometry;
std::map<std::string, std::shared_ptr<TextureInfo>> characterTextures;

// Model variants - separate body and head meshes
std::vector<std::shared_ptr<ZoneGeometry>> bodyVariants;   // e.g., HUM, HUM01
std::vector<std::shared_ptr<ZoneGeometry>> headVariants;   // e.g., HUMHE00, HUMHE01, etc.
size_t currentBodyVariant = 0;
size_t currentHeadVariant = 0;

// Race model loader for animated meshes
std::unique_ptr<RaceModelLoader> raceModelLoader;
std::string currentRaceCode = "HUM";
uint16_t currentRaceId = 1;  // Human
uint8_t currentGender = 0;   // 0 = male

// Equipment model loader for weapon/shield rendering
std::unique_ptr<EquipmentModelLoader> equipmentModelLoader;
scene::IMeshSceneNode* primaryEquipNode = nullptr;   // Right hand weapon
scene::IMeshSceneNode* secondaryEquipNode = nullptr; // Left hand/shield

// Entity data loaded from JSON
struct EntityData {
    std::string name;
    uint16_t race_id = 1;
    uint8_t gender = 0;
    uint8_t face = 0;
    uint8_t haircolor = 0;
    uint8_t hairstyle = 0;
    uint8_t beardcolor = 0;
    uint8_t beard = 0;
    uint8_t texture = 0;
    uint32_t equipment[9] = {0};
    uint32_t equipment_tint[9] = {0};
};
std::vector<EntityData> loadedEntities;
int currentEntityIndex = -1;
std::string currentZoneName;  // Zone name from entity JSON (e.g., "qeynos2")
std::string clientPath = "/home/user/projects/claude/EverQuestP1999/";  // EQ client path

// Current texture values for cycling
uint8_t currentBodyTexture = 0;
uint8_t currentHelmTexture = 0;
bool useEquipmentTextures = false;  // Toggle for equipment texture overrides

// Spell bar for casting visualization
EQT::SpellBar spellBar;
EQT::CastingBar castingBar;
EQT::ModelViewerFX spellFX;

// Effect cycling for testing EDD emitter definitions
int currentEffectIndex = -1;  // -1 = use default (spell category based), 0+ = specific emitter

// ============================================================================
// Spell Test Mode state
// ============================================================================
bool spellTestMode = false;
bool useGLRenderer = false;
bool useGLES2Renderer = false;

// Spell database and visual FX (spell test mode)
std::unique_ptr<EQ::SpellDatabase> spellDatabase;
std::unique_ptr<EQ::SpellVisualFX> spellVisualFX;
#ifdef EQT_HAS_GLES2
std::unique_ptr<EQT::Graphics::Environment::ParticleManager> particleManager;
#endif

// Spell test navigation state
int spellTestClassIndex = 1;      // 1-16, maps to PlayerClass enum (start at Warrior)
int spellTestCategoryIndex = 0;   // Index into spell categories
int spellTestSpellIndex = 0;      // Current spell within filtered list
bool spellTestSelfMode = false;   // true = self-cast, false = targeted
std::vector<const EQ::SpellData*> spellTestSpellList;  // Filtered spells for current class+category

// Target entity for spell test mode
EQAnimatedMeshSceneNode* targetAnimatedNode = nullptr;
uint16_t casterEntityId = 1;
uint16_t targetEntityId = 2;

// Spell test cast state
bool spellTestCasting = false;
float spellTestCastElapsed = 0;
float spellTestCastDuration = 0;
const EQ::SpellData* spellTestCurrentSpell = nullptr;
bool spellTestCastAnimPlaying = false;

// Virtual entity IDs for hand bone particle tracking
uint16_t casterRightHandEntityId = 101;
uint16_t casterLeftHandEntityId = 102;
int casterRightHandBoneIdx = -1;
int casterLeftHandBoneIdx = -1;

// Cast effect type: smolder (orbital glow) vs spray (directional cone)
enum class CastEffectType : uint8_t { Smolder, Spray };
CastEffectType castEffectType = CastEffectType::Smolder;

// Spell categories for filtering
enum class SpellTestCategory : uint8_t {
    DirectDamage,
    Bolt,
    DoT,
    AEDamage,
    PBAE,
    AEBuff,
    Heal,
    Buff,
    Debuff,
    Utility,
    Count
};

const char* spellTestCategoryNames[] = {
    "Direct Damage", "Bolt", "DoT", "AE Damage", "PBAE", "AE Buff",
    "Heal", "Buff", "Debuff", "Utility"
};

const char* getClassName(int classIndex) {
    switch (static_cast<EQ::PlayerClass>(classIndex)) {
        case EQ::PlayerClass::Warrior:     return "Warrior";
        case EQ::PlayerClass::Cleric:      return "Cleric";
        case EQ::PlayerClass::Paladin:     return "Paladin";
        case EQ::PlayerClass::Ranger:      return "Ranger";
        case EQ::PlayerClass::ShadowKnight:return "Shadow Knight";
        case EQ::PlayerClass::Druid:       return "Druid";
        case EQ::PlayerClass::Monk:        return "Monk";
        case EQ::PlayerClass::Bard:        return "Bard";
        case EQ::PlayerClass::Rogue:       return "Rogue";
        case EQ::PlayerClass::Shaman:      return "Shaman";
        case EQ::PlayerClass::Necromancer: return "Necromancer";
        case EQ::PlayerClass::Wizard:      return "Wizard";
        case EQ::PlayerClass::Magician:    return "Magician";
        case EQ::PlayerClass::Enchanter:   return "Enchanter";
        case EQ::PlayerClass::Beastlord:   return "Beastlord";
        case EQ::PlayerClass::Berserker:   return "Berserker";
        default: return "Unknown";
    }
}

const char* getResistTypeName(EQ::ResistType rt) {
    switch (rt) {
        case EQ::ResistType::None:       return "None";
        case EQ::ResistType::Magic:      return "Magic";
        case EQ::ResistType::Fire:       return "Fire";
        case EQ::ResistType::Cold:       return "Cold";
        case EQ::ResistType::Poison:     return "Poison";
        case EQ::ResistType::Disease:    return "Disease";
        case EQ::ResistType::Chromatic:  return "Chromatic";
        case EQ::ResistType::Prismatic:  return "Prismatic";
        case EQ::ResistType::Physical:   return "Physical";
        case EQ::ResistType::Corruption: return "Corruption";
        default: return "Unknown";
    }
}

// Filter spells by category
bool matchesCategory(const EQ::SpellData* spell, SpellTestCategory cat) {
    switch (cat) {
        case SpellTestCategory::DirectDamage:
            return spell->isDamageSpell() && !spell->hasDuration() && !spell->isAESpell() && !spell->isBoltSpell();
        case SpellTestCategory::Bolt:
            return spell->isBoltSpell();
        case SpellTestCategory::DoT:
            return spell->isDoTSpell();
        case SpellTestCategory::AEDamage:
            return spell->isDamageSpell() && spell->isAESpell() && !spell->isPBAE();
        case SpellTestCategory::PBAE:
            return spell->isPBAE();
        case SpellTestCategory::AEBuff:
            return spell->isAESpell() && !spell->isDamageSpell() && !spell->isPBAE();
        case SpellTestCategory::Heal:
            return spell->isHealSpell();
        case SpellTestCategory::Buff:
            return spell->isBuffSpell() && !spell->isAESpell();
        case SpellTestCategory::Debuff:
            return !spell->is_beneficial && spell->hasDuration() && !spell->isDamageSpell();
        case SpellTestCategory::Utility:
            // Everything else
            return !spell->isDamageSpell() && !spell->isHealSpell() &&
                   !spell->isBuffSpell() && !(spell->hasDuration() && !spell->is_beneficial);
        default:
            return false;
    }
}

// Rebuild the filtered spell list for current class + category
void rebuildSpellTestList() {
    spellTestSpellList.clear();
    if (!spellDatabase || !spellDatabase->isLoaded()) return;

    auto pc = static_cast<EQ::PlayerClass>(spellTestClassIndex);
    auto cat = static_cast<SpellTestCategory>(spellTestCategoryIndex);

    // Get all spells for this class at level 60
    auto classSpells = spellDatabase->getSpellsForClass(pc, 60);

    // Filter by category
    for (const auto* spell : classSpells) {
        if (matchesCategory(spell, cat)) {
            spellTestSpellList.push_back(spell);
        }
    }

    // Sort by level then name
    std::sort(spellTestSpellList.begin(), spellTestSpellList.end(),
        [pc](const EQ::SpellData* a, const EQ::SpellData* b) {
            uint8_t la = a->getClassLevel(pc);
            uint8_t lb = b->getClassLevel(pc);
            if (la != lb) return la < lb;
            return a->name < b->name;
        });

    spellTestSpellIndex = 0;

    std::cout << "Spell list: " << getClassName(spellTestClassIndex) << " / "
              << spellTestCategoryNames[spellTestCategoryIndex] << " -> "
              << spellTestSpellList.size() << " spells" << std::endl;
}

// Forward declarations for spell test mode
void cancelSpellTestCast();
void triggerSpellTestCast();

// Valid texture values: 0=naked, 1=leather, 2=chain, 3=plate, 4=monk, 10-16=robes, 17-23=velious
const std::vector<uint8_t> validTextureValues = {0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};

std::string getTextureName(uint8_t texVal) {
    return ""; // these texture names only apply to players and not NPCs, so they're irrelevant when looking at zone entities since they're all npcs!

    switch (texVal) {
        case 0: return "Naked";
        case 1: return "Leather";
        case 2: return "Chain";
        case 3: return "Plate";
        case 4: return "Monk";
        case 10: return "Robe1";
        case 11: return "Robe2";
        case 12: return "Robe3";
        case 13: return "Robe4";
        case 14: return "Robe5";
        case 15: return "Robe6";
        case 16: return "Robe7";
        case 17: return "VelLeather";
        case 18: return "VelChain";
        case 19: return "VelPlate";
        case 20: return "VelCloth";
        case 21: return "VelRing";
        case 22: return "VelScale";
        case 23: return "VelMonk";
        default: return "Unknown";
    }
}

// Forward declaration for texture cycling
void reloadEntityWithTextures();

// Helper to extract a string value from JSON
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find("\"", pos + searchKey.length());
    if (pos == std::string::npos) return "";
    size_t endPos = json.find("\"", pos + 1);
    if (endPos == std::string::npos) return "";
    return json.substr(pos + 1, endPos - pos - 1);
}

// Helper to extract an integer value from JSON
int extractJsonInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0;
    pos += searchKey.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    int value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        pos++;
    }
    return value;
}

// Helper to extract an array of integers from JSON
std::vector<uint32_t> extractJsonIntArray(const std::string& json, const std::string& key) {
    std::vector<uint32_t> result;
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return result;
    pos = json.find("[", pos);
    if (pos == std::string::npos) return result;
    size_t endPos = json.find("]", pos);
    if (endPos == std::string::npos) return result;

    std::string arrayStr = json.substr(pos + 1, endPos - pos - 1);
    uint32_t value = 0;
    bool inNumber = false;
    for (char c : arrayStr) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            inNumber = true;
        } else if (inNumber) {
            result.push_back(value);
            value = 0;
            inNumber = false;
        }
    }
    if (inNumber) result.push_back(value);
    return result;
}

// Simple JSON parser for entity data (just enough to parse our format)
// Also extracts zone name and stores it in currentZoneName global
std::vector<EntityData> parseEntityFile(const std::string& filename) {
    std::vector<EntityData> entities;
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR(MOD_MAIN, "Failed to open entity file: {}", filename);
        return entities;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Extract zone name from top-level JSON
    currentZoneName = extractJsonString(content, "zone");
    if (!currentZoneName.empty()) {
        std::cout << "Zone: " << currentZoneName << std::endl;
    }

    // Find each entity object
    size_t pos = 0;
    while ((pos = content.find("\"spawn_id\":", pos)) != std::string::npos) {
        // Find the start of this entity object
        size_t objStart = content.rfind("{", pos);
        if (objStart == std::string::npos) { pos++; continue; }

        // Find the end of this entity object
        size_t objEnd = content.find("}", pos);
        if (objEnd == std::string::npos) break;

        std::string entityJson = content.substr(objStart, objEnd - objStart + 1);

        EntityData entity;
        entity.name = extractJsonString(entityJson, "name");
        entity.race_id = extractJsonInt(entityJson, "race_id");
        entity.gender = extractJsonInt(entityJson, "gender");
        entity.face = extractJsonInt(entityJson, "face");
        entity.haircolor = extractJsonInt(entityJson, "haircolor");
        entity.hairstyle = extractJsonInt(entityJson, "hairstyle");
        entity.beardcolor = extractJsonInt(entityJson, "beardcolor");
        entity.beard = extractJsonInt(entityJson, "beard");
        entity.texture = extractJsonInt(entityJson, "texture");

        auto equipArray = extractJsonIntArray(entityJson, "equipment");
        for (size_t i = 0; i < equipArray.size() && i < 9; i++) {
            entity.equipment[i] = equipArray[i];
        }

        auto tintArray = extractJsonIntArray(entityJson, "equipment_tint");
        for (size_t i = 0; i < tintArray.size() && i < 9; i++) {
            entity.equipment_tint[i] = tintArray[i];
        }

        entities.push_back(entity);
        pos = objEnd + 1;
    }

    std::cout << "Loaded " << entities.size() << " entities from " << filename << std::endl;
    return entities;
}

// Find entity by name (case-insensitive partial match)
int findEntityByName(const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (size_t i = 0; i < loadedEntities.size(); ++i) {
        std::string entityName = loadedEntities[i].name;
        std::transform(entityName.begin(), entityName.end(), entityName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (entityName.find(lowerName) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Forward declarations for entity handling
void loadEntityModel(int entityIndex);
void cycleEntity(int direction);
void attachEquipmentModels(uint32_t primaryItemId, uint32_t secondaryItemId);

// Forward declarations
void rebuildMesh();
void rebuildCurrentVariant();
void cycleAnimation(int direction);
void toggleAnimationMode();

// Cycle through EDD effect definitions
void cycleEffect(int direction) {
    auto& effectDB = spellFX.getSpellEffectDB();
    size_t effectCount = effectDB.getEmitterCount();

    if (effectCount == 0) {
        std::cout << "No effect definitions loaded" << std::endl;
        return;
    }

    currentEffectIndex += direction;

    // Wrap around with -1 as "default"
    if (currentEffectIndex < -1) {
        currentEffectIndex = static_cast<int>(effectCount) - 1;
    } else if (currentEffectIndex >= static_cast<int>(effectCount)) {
        currentEffectIndex = -1;
    }

    if (currentEffectIndex == -1) {
        std::cout << "Effect: DEFAULT (category-based)" << std::endl;
    } else {
        const auto* emitter = effectDB.getEmitterByIndex(currentEffectIndex);
        if (emitter) {
            std::cout << "Effect [" << currentEffectIndex << "/" << effectCount << "]: "
                      << emitter->name << " (tex: " << emitter->texture << ")" << std::endl;
        }
    }
}

// ============================================================================
// Spell Test Mode: Cast trigger and cancel
// ============================================================================
void triggerSpellTestCast() {
    if (spellTestSpellList.empty() || !spellVisualFX) return;

    const auto* spell = spellTestSpellList[spellTestSpellIndex];
    spellTestCurrentSpell = spell;
    spellTestCastDuration = spell->cast_time_ms / 1000.0f;
    if (spellTestCastDuration < 0.5f) spellTestCastDuration = 0.5f;  // Min 0.5s for visual
    spellTestCastElapsed = 0;
    spellTestCasting = true;

    // Play idle animation looping during cast channeling
    if (animatedNode && useAnimatedMesh) {
        animatedNode->playAnimation("o01", true, false);
    }

    std::cout << "Casting: " << spell->name << " (ID " << spell->id
              << ", " << spellTestCastDuration << "s, "
              << getResistTypeName(spell->resist_type) << ")" << std::endl;

    uint32_t castMs = static_cast<uint32_t>(spellTestCastDuration * 1000);
    bool hasHandBones = (casterRightHandBoneIdx >= 0 || casterLeftHandBoneIdx >= 0);

#ifdef EQT_HAS_GLES2
    // GLES2: create particle effects at each hand bone position
    if (hasHandBones && particleManager) {
        using namespace EQT::Graphics::Environment;
        auto scolor = spellVisualFX->getSpellColorForSpell(spell->id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);

        bool isSpray = spellVisualFX->isSprayCastStyle(spell->id);
        auto def = isSpray ? SpellPresets::CastSpray(c, spell->id) : SpellPresets::CastGlow(c, spell->id);
        float durSec = spellTestCastDuration;
        for (auto& e : def.emitters) {
            e.config.emitterLifetime = durSec;
            if (!isSpray) {
                e.config.orbitalRadius = 0.4f;     // Tight orbit around hand
            }
            e.positionOffset = glm::vec3(0.0f); // Bone position is already accurate
        }
        if (casterRightHandBoneIdx >= 0) {
            particleManager->createSpellEffect(def, casterRightHandEntityId, 0, durSec, isSpray);
        }
        if (casterLeftHandBoneIdx >= 0) {
            particleManager->createSpellEffect(def, casterLeftHandEntityId, 0, durSec, isSpray);
        }
    } else
#endif
    {
        // Non-GLES2 or no hand bones: glow at entity center
        spellVisualFX->createCastGlow(casterEntityId, spell->id, castMs);
    }
}

void completeSpellTestCast() {
    if (!spellTestCurrentSpell || !spellVisualFX) return;

    const auto* spell = spellTestCurrentSpell;
    std::cout << "Cast complete: " << spell->name << std::endl;

    // Remove cast glows from hands and fallback entity center
#ifdef EQT_HAS_GLES2
    if (particleManager) {
        particleManager->removeSpellEffectsForEntity(casterRightHandEntityId);
        particleManager->removeSpellEffectsForEntity(casterLeftHandEntityId);
    }
#endif
    spellVisualFX->removeCastGlow(casterEntityId);

    // Create spell completion effect on caster (at midpoint)
    spellVisualFX->createSpellComplete(casterEntityId, spell->id);

    // Play cast animation once, then return to idle
    if (animatedNode && useAnimatedMesh) {
        std::string castAnim = "t06";  // Default: offensive
        if (spell->is_beneficial) castAnim = "t04";  // Beneficial
        if (spell->isHealSpell()) castAnim = "t05";  // Heal
        animatedNode->playAnimation(castAnim, false, false);  // loop=false: play once
    }

    // Create appropriate effects based on spell type
    uint16_t effectTarget = spellTestSelfMode ? casterEntityId : targetEntityId;

    if (spell->isBoltSpell()) {
        // Bolt: projectile from caster to target, impact on arrival
        if (!spellTestSelfMode) {
            spellVisualFX->createProjectile(casterEntityId, targetEntityId, spell->id);
        }
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isDamageSpell() && !spell->isAESpell()) {
        // Single target direct damage: instant impact on target
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isPBAE()) {
        // PBAE: impact at caster position
        irr::core::vector3df casterPos;
        if (animatedNode) casterPos = animatedNode->getAbsolutePosition();
        spellVisualFX->createImpactAtPosition(casterPos, spell->id);
    } else if (spell->isAESpell()) {
        // Targeted AE: rain/ground circle at target's feet
        irr::core::vector3df targetPos;
        irr::scene::IAnimatedMeshSceneNode* aeNode = nullptr;
        if (targetAnimatedNode && !spellTestSelfMode) {
            aeNode = targetAnimatedNode;
        } else if (animatedNode) {
            aeNode = animatedNode;
        }
        if (aeNode) {
            targetPos = aeNode->getAbsolutePosition();
            // Model origin is at center — lower to feet using bounding box
            auto bbox = aeNode->getTransformedBoundingBox();
            float halfHeight = (bbox.MaxEdge.Y - bbox.MinEdge.Y) * 0.5f;
            targetPos.Y -= halfHeight;
        }
        float radius = spell->aoe_range > 0 ? spell->aoe_range * 0.1f : 5.0f;
        if (spell->isDamageSpell()) {
            spellVisualFX->createRainEffect(targetPos, radius, spell->id, 3.0f);
        } else {
            spellVisualFX->createGroundCircle(targetPos, radius, spell->id, 3.0f);
        }
    } else if (spell->isHealSpell()) {
        // Heal: impact on target (or self)
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isBuffSpell()) {
        // Buff: aura on target
        spellVisualFX->createBuffAura(effectTarget, spell->id);
    } else if (spell->hasDuration() && !spell->is_beneficial) {
        // Debuff: projectile + impact on target
        if (!spellTestSelfMode) {
            spellVisualFX->createProjectile(casterEntityId, targetEntityId, spell->id);
        }
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else {
        // Utility: just impact on target
        spellVisualFX->createImpact(effectTarget, spell->id);
    }

    spellTestCasting = false;
    spellTestCurrentSpell = nullptr;
}

void cancelSpellTestCast() {
    if (spellTestCasting && spellVisualFX) {
#ifdef EQT_HAS_GLES2
        // Remove hand bone particle effects created directly on PM
        if (particleManager) {
            particleManager->removeSpellEffectsForEntity(casterRightHandEntityId);
            particleManager->removeSpellEffectsForEntity(casterLeftHandEntityId);
        }
#endif
        spellVisualFX->removeCastGlow(casterEntityId);
        spellTestCasting = false;
        spellTestCurrentSpell = nullptr;
        std::cout << "Cast cancelled" << std::endl;

        // Return to idle
        if (animatedNode && useAnimatedMesh) {
            animatedNode->playAnimation("p01", true, false);
        }
    }
    // Also clear all lingering effects (Irrlicht scene nodes + GLES2 particles)
    if (spellVisualFX) {
        spellVisualFX->clearAllEffects();
        std::cout << "Effects cleared" << std::endl;
    }
#ifdef EQT_HAS_GLES2
    if (particleManager) {
        particleManager->clearAllSpellEffects();
    }
#endif
}

class MyEventReceiver : public IEventReceiver {
public:
    virtual bool OnEvent(const SEvent& event) {
        if (event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown) {
            switch (event.KeyInput.Key) {
                case KEY_ESCAPE:
                    exit(0);
                    return true;
                case KEY_F12:
                    // Take screenshot
                    if (driver) {
                        video::IImage* screenshot = driver->createScreenShot();
                        if (screenshot) {
                            driver->writeImageToFile(screenshot, "model_viewer.png");
                            screenshot->drop();
                            std::cout << "Screenshot saved to model_viewer.png" << std::endl;
                        }
                    }
                    return true;
                case KEY_LEFT:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.Y -= 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.Y -= 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_RIGHT:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.Y += 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.Y += 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_UP:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.X -= 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.X -= 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_DOWN:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.X += 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.X += 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_KEY_Q:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.Z -= 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.Z -= 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_KEY_E:
                    if (animatedNode) {
                        core::vector3df rot = animatedNode->getRotation();
                        rot.Z += 10.0f;
                        animatedNode->setRotation(rot);
                    } else if (modelNode) {
                        core::vector3df rot = modelNode->getRotation();
                        rot.Z += 10.0f;
                        modelNode->setRotation(rot);
                    }
                    return true;
                case KEY_KEY_R:
                    if (animatedNode) {
                        animatedNode->setRotation(core::vector3df(0, 0, 0));
                    } else if (modelNode) {
                        modelNode->setRotation(core::vector3df(0, 0, 0));
                    }
                    return true;
                case KEY_KEY_W:
                    wireframe = !wireframe;
                    if (animatedNode) {
                        for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
                            animatedNode->getMaterial(i).Wireframe = wireframe;
                        }
                    }
                    if (modelNode) {
                        for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
                            modelNode->getMaterial(i).Wireframe = wireframe;
                        }
                    }
                    std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << std::endl;
                    return true;
                case KEY_KEY_U:
                    // Toggle U coordinate flip (texture X axis)
                    flipU = !flipU;
                    std::cout << "Flip U (texture X): " << (flipU ? "ON" : "OFF") << std::endl;
                    if (useAnimatedMesh) {
                        reloadEntityWithTextures();
                    } else {
                        rebuildMesh();
                    }
                    return true;
                case KEY_KEY_V:
                    // Cycle body variant - V=next, Shift+V=prev
                    if (!bodyVariants.empty()) {
                        if (event.KeyInput.Shift) {
                            currentBodyVariant = (currentBodyVariant + bodyVariants.size() - 1) % bodyVariants.size();
                        } else {
                            currentBodyVariant = (currentBodyVariant + 1) % bodyVariants.size();
                        }
                        std::cout << "Body variant: " << currentBodyVariant << "/" << bodyVariants.size()
                                  << " (" << bodyVariants[currentBodyVariant]->name << ")" << std::endl;
                        if (useAnimatedMesh) {
                            reloadEntityWithTextures();
                        } else {
                            rebuildCurrentVariant();
                        }
                    }
                    return true;
                case KEY_KEY_A:
                    // Cycle animations - A=next, Shift+A=prev
                    if (event.KeyInput.Shift) {
                        cycleAnimation(-1);
                    } else {
                        cycleAnimation(1);
                    }
                    return true;
                case KEY_SPACE:
                    // Pause/Resume animation
                    if (animatedNode && useAnimatedMesh) {
                        animationPaused = !animationPaused;
                        if (animationPaused) {
                            animatedNode->setAnimationSpeed(0.0f);
                            std::cout << "Animation PAUSED" << std::endl;
                        } else {
                            // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
                            animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                            std::cout << "Animation RESUMED" << std::endl;
                        }
                    }
                    return true;
                case KEY_KEY_S:
                    // Toggle static/animated mesh mode
                    toggleAnimationMode();
                    return true;
                case KEY_KEY_F:
                    filterOutliers = !filterOutliers;
                    std::cout << "Filter outliers: " << (filterOutliers ? "ON" : "OFF") << " - Rebuilding mesh..." << std::endl;
                    rebuildMesh();
                    return true;
                case KEY_PLUS:
                case KEY_ADD:
                    // Zoom in
                    cameraDistance = std::max(1.0f, cameraDistance - 2.0f);
                    if (camera) {
                        camera->setPosition(core::vector3df(0, 5, -cameraDistance));
                    }
                    std::cout << "Zoom: " << cameraDistance << std::endl;
                    return true;
                case KEY_MINUS:
                case KEY_SUBTRACT:
                    // Zoom out
                    cameraDistance = std::min(100.0f, cameraDistance + 2.0f);
                    if (camera) {
                        camera->setPosition(core::vector3df(0, 5, -cameraDistance));
                    }
                    std::cout << "Zoom: " << cameraDistance << std::endl;
                    return true;
                case KEY_OEM_4:  // '[' key - decrease animation speed
                    globalAnimationSpeed = std::max(0.1f, globalAnimationSpeed - 0.1f);
                    if (animatedNode && useAnimatedMesh) {
                        animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                    }
                    std::cout << "Animation speed: " << globalAnimationSpeed << "x" << std::endl;
                    return true;
                case KEY_OEM_6:  // ']' key - increase animation speed
                    globalAnimationSpeed = std::min(5.0f, globalAnimationSpeed + 0.1f);
                    if (animatedNode && useAnimatedMesh) {
                        animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                    }
                    std::cout << "Animation speed: " << globalAnimationSpeed << "x" << std::endl;
                    return true;
                case KEY_COMMA:  // '<' key - previous entity
                    cycleEntity(-1);
                    return true;
                case KEY_PERIOD:  // '>' key - next entity
                    cycleEntity(1);
                    return true;
                case KEY_KEY_B:  // B=toggle equipment textures on/off
                    useEquipmentTextures = !useEquipmentTextures;
                    std::cout << "Equipment textures: " << (useEquipmentTextures ? "ON" : "OFF");
                    if (useEquipmentTextures) {
                        std::cout << " (texture=" << (int)currentBodyTexture << " " << getTextureName(currentBodyTexture) << ")";
                    }
                    std::cout << std::endl;
                    if (useAnimatedMesh) {
                        reloadEntityWithTextures();
                    } else {
                        rebuildCurrentVariant();
                    }
                    return true;
                case KEY_KEY_N:  // N=next body texture value, Shift+N=prev (when equipment textures enabled)
                    if (useEquipmentTextures) {
                        auto it = std::find(validTextureValues.begin(), validTextureValues.end(), currentBodyTexture);
                        if (event.KeyInput.Shift) {
                            if (it != validTextureValues.begin() && it != validTextureValues.end()) {
                                --it;
                            } else {
                                it = validTextureValues.end() - 1;
                            }
                        } else {
                            if (it != validTextureValues.end()) {
                                ++it;
                                if (it == validTextureValues.end()) {
                                    it = validTextureValues.begin();
                                }
                            } else {
                                it = validTextureValues.begin();
                            }
                        }
                        currentBodyTexture = *it;
                        std::cout << "Body texture: " << (int)currentBodyTexture << " (" << getTextureName(currentBodyTexture) << ")" << std::endl;
                        if (useAnimatedMesh) {
                            reloadEntityWithTextures();
                        } else {
                            rebuildCurrentVariant();
                        }
                    } else {
                        std::cout << "Equipment textures are OFF. Press B to enable." << std::endl;
                    }
                    return true;
                case KEY_KEY_H:
                    // H=next head variant, Shift+H=prev
                    if (!headVariants.empty()) {
                        if (event.KeyInput.Shift) {
                            currentHeadVariant = (currentHeadVariant + headVariants.size() - 1) % headVariants.size();
                        } else {
                            currentHeadVariant = (currentHeadVariant + 1) % headVariants.size();
                        }
                        std::cout << "Head variant: " << currentHeadVariant << "/" << headVariants.size()
                                  << " (" << headVariants[currentHeadVariant]->name << ")" << std::endl;
                        if (useAnimatedMesh) {
                            reloadEntityWithTextures();
                        } else {
                            rebuildCurrentVariant();
                        }
                    }
                    return true;
                case KEY_F3:
                    // Spell test: cycle class
                    if (spellTestMode) {
                        spellTestClassIndex += event.KeyInput.Shift ? -1 : 1;
                        if (spellTestClassIndex < 1) spellTestClassIndex = 16;
                        if (spellTestClassIndex > 16) spellTestClassIndex = 1;
                        rebuildSpellTestList();
                        return true;
                    }
                    break;
                case KEY_F4:
                    // Spell test: cycle category
                    if (spellTestMode) {
                        spellTestCategoryIndex += event.KeyInput.Shift ? -1 : 1;
                        int catCount = static_cast<int>(SpellTestCategory::Count);
                        if (spellTestCategoryIndex < 0) spellTestCategoryIndex = catCount - 1;
                        if (spellTestCategoryIndex >= catCount) spellTestCategoryIndex = 0;
                        rebuildSpellTestList();
                        return true;
                    }
                    break;
                case KEY_F5:
                    // Spell test: cycle spell within category
                    if (spellTestMode && !spellTestSpellList.empty()) {
                        spellTestSpellIndex += event.KeyInput.Shift ? -1 : 1;
                        if (spellTestSpellIndex < 0)
                            spellTestSpellIndex = static_cast<int>(spellTestSpellList.size()) - 1;
                        if (spellTestSpellIndex >= static_cast<int>(spellTestSpellList.size()))
                            spellTestSpellIndex = 0;
                        const auto* sp = spellTestSpellList[spellTestSpellIndex];
                        std::cout << "Spell [" << (spellTestSpellIndex + 1) << "/"
                                  << spellTestSpellList.size() << "]: " << sp->name
                                  << " (ID " << sp->id << ", " << getResistTypeName(sp->resist_type)
                                  << ", cast " << sp->cast_time_ms << "ms)" << std::endl;
                        return true;
                    }
                    break;
                case KEY_F6:
                    // Spell test: toggle self/targeted
                    if (spellTestMode) {
                        spellTestSelfMode = !spellTestSelfMode;
                        std::cout << "Mode: " << (spellTestSelfMode ? "SELF" : "TARGETED") << std::endl;
                        return true;
                    }
                    break;
                case KEY_RETURN:
                    // Spell test: cast current spell
                    if (spellTestMode && !spellTestSpellList.empty() && !spellTestCasting) {
                        triggerSpellTestCast();
                        return true;
                    }
                    break;
                case KEY_BACK:
                    // Spell test: cancel cast / clear effects
                    if (spellTestMode) {
                        cancelSpellTestCast();
                        return true;
                    }
                    break;
                case KEY_F7:
                    // F7=next effect, Shift+F7=prev effect (for testing EDD emitter definitions)
                    if (event.KeyInput.Shift) {
                        cycleEffect(-1);
                    } else {
                        cycleEffect(1);
                    }
                    return true;
                case KEY_F8:
                    // F8: (reserved — cast effect style now auto-detected from spell school)
                    break;
                default:
                    // Check if it's a number key for spell casting (1-9, 0)
                    if (event.KeyInput.Key >= KEY_KEY_0 && event.KeyInput.Key <= KEY_KEY_9) {
                        if (spellBar.handleKeyPress(event.KeyInput.Key)) {
                            return true;
                        }
                    }
                    break;
            }
        }

        // Handle mouse events for spell bar
        if (event.EventType == EET_MOUSE_INPUT_EVENT) {
            int mouseX = event.MouseInput.X;
            int mouseY = event.MouseInput.Y;

            switch (event.MouseInput.Event) {
                case EMIE_MOUSE_MOVED:
                    spellBar.handleMouseMove(mouseX, mouseY);
                    break;
                case EMIE_LMOUSE_PRESSED_DOWN:
                    if (spellBar.handleMouseClick(mouseX, mouseY, true)) {
                        return true;
                    }
                    break;
                default:
                    break;
            }
        }

        return false;
    }
};

MyEventReceiver receiver;

// Cycle through available animations
void cycleAnimation(int direction) {
    if (!animatedNode || !useAnimatedMesh || availableAnimations.empty()) {
        std::cout << "No animations available" << std::endl;
        return;
    }

    // Cycle index
    currentAnimIndex += direction;
    if (currentAnimIndex < -1) {
        currentAnimIndex = static_cast<int>(availableAnimations.size()) - 1;
    } else if (currentAnimIndex >= static_cast<int>(availableAnimations.size())) {
        currentAnimIndex = -1;
    }

    // Apply animation
    if (currentAnimIndex < 0) {
        animatedNode->stopAnimation();
        std::cout << "Animation: NONE (pose)" << std::endl;
    } else {
        const std::string& animCode = availableAnimations[currentAnimIndex];
        if (animatedNode->playAnimation(animCode, true)) {
            std::cout << "Animation: " << animCode << " (" << (currentAnimIndex + 1) << "/"
                      << availableAnimations.size() << ")" << std::endl;
        } else {
            std::cout << "Failed to play animation: " << animCode << std::endl;
        }
    }

    // Reset pause state
    animationPaused = false;
    // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
    animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
}

// Toggle between static and animated mesh mode
void toggleAnimationMode() {
    useAnimatedMesh = !useAnimatedMesh;
    std::cout << "Mode: " << (useAnimatedMesh ? "ANIMATED" : "STATIC") << std::endl;

    // Rebuild with current settings preserved
    if (useAnimatedMesh) {
        reloadEntityWithTextures();
    } else {
        rebuildCurrentVariant();
    }
}

// Combine current body and head variant into characterGeometry
void combineCurrentVariants() {
    std::vector<std::shared_ptr<ZoneGeometry>> parts;

    if (!bodyVariants.empty() && currentBodyVariant < bodyVariants.size()) {
        parts.push_back(bodyVariants[currentBodyVariant]);
    }
    if (!headVariants.empty() && currentHeadVariant < headVariants.size()) {
        parts.push_back(headVariants[currentHeadVariant]);
    }

    if (parts.empty()) return;

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    u32 vertexOffset = 0;
    u32 textureOffset = 0;

    for (const auto& part : parts) {
        if (!part || part->vertices.empty()) continue;

        for (const auto& v : part->vertices) {
            combined->vertices.push_back(v);
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        for (const auto& tri : part->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        for (const auto& texName : part->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }
        for (bool inv : part->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        vertexOffset += static_cast<u32>(part->vertices.size());
        textureOffset += static_cast<u32>(part->textureNames().size());
    }

    combined->name = "combined_variant";
    characterGeometry = combined;
}

// Load texture from BMP/DDS data
video::ITexture* loadTexture(const std::string& name, const std::vector<char>& data) {
    if (data.empty() || !driver) return nullptr;

    // Check if DDS
    if (data.size() >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
        // DDS file - decode it
        DecodedImage decoded = DDSDecoder::decode(data);
        if (!decoded.isValid()) return nullptr;

        std::vector<u32> argbPixels(decoded.width * decoded.height);
        for (u32 y = 0; y < decoded.height; ++y) {
            for (u32 x = 0; x < decoded.width; ++x) {
                size_t srcIdx = (y * decoded.width + x) * 4;
                u8 r = decoded.pixels[srcIdx + 0];
                u8 g = decoded.pixels[srcIdx + 1];
                u8 b = decoded.pixels[srcIdx + 2];
                u8 a = decoded.pixels[srcIdx + 3];
                argbPixels[y * decoded.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }

        video::IImage* image = driver->createImageFromData(
            video::ECF_A8R8G8B8,
            core::dimension2d<u32>(decoded.width, decoded.height),
            argbPixels.data(), false, false);

        if (!image) return nullptr;

        video::ITexture* texture = driver->addTexture(name.c_str(), image);
        image->drop();
        return texture;
    }

    // BMP file - write to temp and load
    if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
        std::string tempPath = "/tmp/model_viewer_" + name;
        std::ofstream outFile(tempPath, std::ios::binary);
        if (!outFile) return nullptr;
        outFile.write(data.data(), data.size());
        outFile.close();
        return driver->getTexture(tempPath.c_str());
    }

    return nullptr;
}

// Check if a vertex is an outlier
bool isOutlierVertex(const Vertex3D& v, float threshold) {
    return std::abs(v.x) > threshold || std::abs(v.y) > threshold || std::abs(v.z) > threshold;
}

// Build mesh from geometry with optional X/Y swap
scene::IMesh* buildMesh(const ZoneGeometry& geometry, bool doSwapXY) {
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    scene::SMesh* mesh = new scene::SMesh();

    // Count outliers if filtering
    if (filterOutliers) {
        int outlierCount = 0;
        for (const auto& v : geometry.vertices) {
            if (isOutlierVertex(v, outlierThreshold)) {
                outlierCount++;
            }
        }
        std::cout << "Found " << outlierCount << " outlier vertices (threshold=" << outlierThreshold << ")" << std::endl;
    }

    // Group triangles by texture, optionally skipping triangles with outlier vertices
    std::map<u32, std::vector<size_t>> trianglesByTexture;
    int skippedTriangles = 0;
    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        const auto& tri = geometry.triangles[i];

        // Check if any vertex in this triangle is an outlier
        if (filterOutliers) {
            bool hasOutlier = isOutlierVertex(geometry.vertices[tri.v1], outlierThreshold) ||
                              isOutlierVertex(geometry.vertices[tri.v2], outlierThreshold) ||
                              isOutlierVertex(geometry.vertices[tri.v3], outlierThreshold);
            if (hasOutlier) {
                skippedTriangles++;
                continue;
            }
        }

        trianglesByTexture[geometry.triangles[i].textureIndex].push_back(i);
    }

    if (filterOutliers && skippedTriangles > 0) {
        std::cout << "Skipped " << skippedTriangles << " triangles with outlier vertices" << std::endl;
    }

    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        scene::SMeshBuffer* buffer = new scene::SMeshBuffer();

        // Get texture
        std::string texName;
        video::ITexture* texture = nullptr;
        if (texIdx < geometry.textureNames().size()) {
            texName = geometry.textureNames()[texIdx];
            if (!texName.empty()) {
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // If equipment textures enabled, try to find the equipment variant texture
                std::string finalTexName = lowerTexName;
                if (useEquipmentTextures) {
                    // Try to map base texture to equipment variant
                    // e.g., humch0001.bmp -> humch0101.bmp (leather), humch0201.bmp (chain), etc.
                    // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
                    size_t pos = lowerTexName.find("00");
                    if (pos != std::string::npos && pos >= 3 && lowerTexName.length() > pos + 4) {
                        // Check if this looks like a body texture (e.g., humch0001.bmp)
                        char variantStr[8];
                        snprintf(variantStr, sizeof(variantStr), "%02d", currentBodyTexture);
                        std::string equipTexName = lowerTexName.substr(0, pos) + variantStr + lowerTexName.substr(pos + 2);

                        // Check if equipment texture exists
                        auto equipIt = characterTextures.find(equipTexName);
                        if (equipIt != characterTextures.end() && equipIt->second && !equipIt->second->data.empty()) {
                            if (IsDebugEnabled()) {
                                std::cout << "    Texture override: " << lowerTexName << " -> " << equipTexName << std::endl;
                            }
                            finalTexName = equipTexName;
                        } else if (IsDebugEnabled()) {
                            std::cout << "    Texture override failed: " << equipTexName << " not found" << std::endl;
                        }
                    }
                }

                auto texIt = characterTextures.find(finalTexName);
                if (texIt != characterTextures.end() && texIt->second && !texIt->second->data.empty()) {
                    texture = loadTexture(finalTexName, texIt->second->data);
                    if (IsDebugEnabled()) {
                        std::cout << "    Loaded texture: " << finalTexName << std::endl;
                    }
                } else if (IsDebugEnabled()) {
                    std::cout << "    Texture not found: " << finalTexName << std::endl;
                }
            }
        }

        buffer->Material.BackfaceCulling = false;
        buffer->Material.Lighting = false;
        if (texture) {
            buffer->Material.setTexture(0, texture);
            buffer->Material.MaterialType = video::EMT_SOLID;
        }

        std::unordered_map<size_t, u16> globalToLocal;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];

            for (size_t vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    const auto& v = geometry.vertices[vidx];
                    video::S3DVertex vertex;

                    if (doSwapXY) {
                        // Swap X and Y before coordinate transform
                        vertex.Pos.X = v.y;  // Swapped
                        vertex.Pos.Y = v.z;  // EQ Z -> Irrlicht Y
                        vertex.Pos.Z = v.x;  // Swapped
                        vertex.Normal.X = v.ny;
                        vertex.Normal.Y = v.nz;
                        vertex.Normal.Z = v.nx;
                    } else {
                        // Standard transform
                        vertex.Pos.X = v.x;
                        vertex.Pos.Y = v.z;  // EQ Z -> Irrlicht Y
                        vertex.Pos.Z = v.y;
                        vertex.Normal.X = v.nx;
                        vertex.Normal.Y = v.nz;
                        vertex.Normal.Z = v.ny;
                    }

                    vertex.TCoords.X = flipU ? (1.0f - v.u) : v.u;  // Optionally flip U coordinate
                    vertex.TCoords.Y = 1.0f - v.v;  // Flip V coordinate for correct texture orientation
                    vertex.Color = video::SColor(255, 255, 255, 255);

                    globalToLocal[vidx] = static_cast<u16>(buffer->Vertices.size());
                    buffer->Vertices.push_back(vertex);
                }
            }

            buffer->Indices.push_back(globalToLocal[tri.v1]);
            buffer->Indices.push_back(globalToLocal[tri.v2]);
            buffer->Indices.push_back(globalToLocal[tri.v3]);
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

void rebuildMesh() {
    if (!characterGeometry || !smgr) return;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove old nodes (both types)
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }

    // Build new mesh
    scene::IMesh* mesh = buildMesh(*characterGeometry, swapXY);
    if (!mesh) {
        LOG_ERROR(MOD_MAIN, "Failed to build mesh");
        return;
    }

    // Create new node
    modelNode = smgr->addMeshSceneNode(mesh);
    if (modelNode) {
        modelNode->setScale(core::vector3df(10, 10, 10));  // Scale up for visibility
        modelNode->setPosition(core::vector3df(0, 0, 0));
        modelNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

        for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
            modelNode->getMaterial(i).Wireframe = wireframe;
            modelNode->getMaterial(i).BackfaceCulling = false;
            modelNode->getMaterial(i).Lighting = false;
        }
    }
    mesh->drop();
}

void rebuildCurrentVariant() {
    combineCurrentVariants();
    rebuildMesh();
}

// Combine character parts into single geometry
std::shared_ptr<ZoneGeometry> combineCharacterParts(
    const std::vector<std::shared_ptr<ZoneGeometry>>& parts) {

    if (parts.empty()) return nullptr;

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    u32 vertexOffset = 0;
    u32 textureOffset = 0;

    for (const auto& part : parts) {
        if (!part || part->vertices.empty()) continue;

        for (const auto& v : part->vertices) {
            combined->vertices.push_back(v);
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        for (const auto& tri : part->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        for (const auto& texName : part->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }
        for (bool inv : part->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        vertexOffset += static_cast<u32>(part->vertices.size());
        textureOffset += static_cast<u32>(part->textureNames().size());
    }

    if (combined->vertices.empty() || combined->triangles.empty()) return nullptr;

    combined->name = "combined";
    return combined;
}

// Load body/head variants for a specific race code from an S3D file
// Returns true if variants were found and loaded
bool loadVariantsFromS3D(const std::string& s3dPath, const std::string& raceCode) {
    S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        return false;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        return false;
    }

    // Convert race code to uppercase for matching
    std::string upperRaceCode = raceCode;
    std::transform(upperRaceCode.begin(), upperRaceCode.end(), upperRaceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Find the character model matching this race code
    std::shared_ptr<CharacterModel> character = nullptr;
    for (const auto& ch : zone->characters) {
        std::string charName = ch->name;
        std::transform(charName.begin(), charName.end(), charName.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        if (charName.find(upperRaceCode) != std::string::npos) {
            character = ch;
            break;
        }
    }

    if (!character || character->parts.empty()) {
        return false;
    }

    // Clear existing variants
    bodyVariants.clear();
    headVariants.clear();

    // Categorize parts into body variants and head variants
    std::string headPattern = upperRaceCode + "HE";
    for (const auto& part : character->parts) {
        std::string partName = part->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        if (partName.find(headPattern) == 0) {
            headVariants.push_back(part);
            if (IsDebugEnabled()) {
                std::cout << "  Head variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
            }
        } else if (partName.find(upperRaceCode) == 0) {
            bodyVariants.push_back(part);
            if (IsDebugEnabled()) {
                std::cout << "  Body variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
            }
        }
    }

    // Also merge textures from this S3D file
    for (const auto& [name, tex] : zone->characterTextures) {
        characterTextures[name] = tex;  // Override existing
    }

    if (IsDebugEnabled()) {
        std::cout << "Loaded " << bodyVariants.size() << " body variants, "
                  << headVariants.size() << " head variants from " << s3dPath << std::endl;
    }

    // Reset variant indices
    currentBodyVariant = 0;
    currentHeadVariant = 0;

    return !bodyVariants.empty();
}

// Load entity model with equipment textures
void loadEntityModel(int entityIndex) {
    if (entityIndex < 0 || entityIndex >= static_cast<int>(loadedEntities.size())) {
        std::cout << "Invalid entity index: " << entityIndex << std::endl;
        return;
    }

    const EntityData& entity = loadedEntities[entityIndex];
    currentEntityIndex = entityIndex;

    std::cout << "\n=== Loading Entity ===" << std::endl;
    std::cout << "Name: " << entity.name << std::endl;
    std::cout << "Race ID: " << entity.race_id << std::endl;
    std::cout << "Gender: " << (int)entity.gender << std::endl;
    std::cout << "Face: " << (int)entity.face << std::endl;
    std::cout << "Texture: " << (int)entity.texture << std::endl;
    std::cout << "Equipment: [";
    for (int i = 0; i < 9; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << entity.equipment[i];
    }
    std::cout << "]" << std::endl;
    std::cout << "Equipment Tint: [";
    for (int i = 0; i < 9; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << entity.equipment_tint[i];
    }
    std::cout << "]" << std::endl;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove existing nodes
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }

    // Update current race info
    currentRaceId = entity.race_id;
    currentGender = entity.gender;

    // Get base race code and apply gender suffix for playable races
    // E.g., HUM -> HUF for female human, BAM -> BAF for female barbarian
    std::string baseRaceCode = RaceModelLoader::getRaceCode(entity.race_id);
    if (!baseRaceCode.empty() && baseRaceCode.length() == 3 && baseRaceCode.back() == 'M' && entity.gender == 1) {
        currentRaceCode = baseRaceCode;
        currentRaceCode.back() = 'F';  // Convert male to female code
    } else {
        currentRaceCode = baseRaceCode;
    }
    std::cout << "Race Code: " << currentRaceCode << " (gender=" << (int)entity.gender << ")" << std::endl;

    // Try to load body/head variants from zone-specific S3D first, then fall back to global
    bool variantsLoaded = false;

    // 1. Try zone-specific _chr.s3d (e.g., qeynos2_chr.s3d for QCM)
    if (!currentZoneName.empty()) {
        std::string zoneChrPath = clientPath + currentZoneName + "_chr.s3d";
        if (IsDebugEnabled()) {
            std::cout << "Trying to load " << currentRaceCode << " variants from " << zoneChrPath << std::endl;
        }
        variantsLoaded = loadVariantsFromS3D(zoneChrPath, currentRaceCode);
    }

    // 2. If not found, try global_chr.s3d with race code
    if (!variantsLoaded) {
        std::string globalChrPath = clientPath + "global_chr.s3d";
        if (IsDebugEnabled()) {
            std::cout << "Trying to load " << currentRaceCode << " variants from " << globalChrPath << std::endl;
        }
        variantsLoaded = loadVariantsFromS3D(globalChrPath, currentRaceCode);
    }

    // 3. If still not found, try fallback race code (e.g., QCM -> HUM/HUF)
    if (!variantsLoaded) {
        // Get fallback code for citizen races
        std::string fallbackCode;
        switch (entity.race_id) {
            case 71:  // Qeynos Citizen -> Human
            case 77:  // Freeport Citizen -> Human
            case 15:  // Freeport Guard -> Human
            case 80:  // Karana Citizen -> Human
                fallbackCode = (entity.gender == 1) ? "HUF" : "HUM";
                break;
            case 61:  // Neriak Citizen -> Dark Elf
                fallbackCode = (entity.gender == 1) ? "DAF" : "DAM";
                break;
            default:
                break;
        }

        if (!fallbackCode.empty()) {
            std::string globalChrPath = clientPath + "global_chr.s3d";
            std::cout << "Falling back to " << fallbackCode << " variants from " << globalChrPath << std::endl;
            variantsLoaded = loadVariantsFromS3D(globalChrPath, fallbackCode);
            if (variantsLoaded) {
                currentRaceCode = fallbackCode;  // Update race code to the fallback
            }
        }
    }

    if (!variantsLoaded) {
        std::cout << "Warning: Could not load variants for " << currentRaceCode << std::endl;
    }

    // Build EntityAppearance from entity data
    EntityAppearance appearance;
    appearance.face = entity.face;
    appearance.haircolor = entity.haircolor;
    appearance.hairstyle = entity.hairstyle;
    appearance.beardcolor = entity.beardcolor;
    appearance.beard = entity.beard;
    appearance.texture = entity.texture;
    for (int i = 0; i < 9; i++) {
        appearance.equipment[i] = entity.equipment[i];
        appearance.equipment_tint[i] = entity.equipment_tint[i];
    }

    // Initialize current textures from entity data
    // For NPCs, the texture value determines:
    // 1. Body variant (mesh) - texture value selects which body mesh (0=default, 1=variant 01, etc.)
    // 2. Head variant (mesh) - same as body variant for NPCs
    // 3. Equipment texture - the skin/armor texture to apply
    currentBodyTexture = entity.texture;
    currentHelmTexture = entity.texture;  // NPCs use same texture for helm as body

    // Set body and head variants based on texture value
    // Texture 0 = variant 0 (default mesh), texture 1 = variant 1, etc.
    if (!bodyVariants.empty()) {
        currentBodyVariant = std::min(static_cast<size_t>(entity.texture), bodyVariants.size() - 1);
    } else {
        currentBodyVariant = 0;
    }

    if (!headVariants.empty()) {
        currentHeadVariant = std::min(static_cast<size_t>(entity.texture), headVariants.size() - 1);
    } else {
        currentHeadVariant = 0;
    }

    std::cout << "Body variant: " << (currentBodyVariant + 1) << "/" << bodyVariants.size();
    if (!bodyVariants.empty() && currentBodyVariant < bodyVariants.size()) {
        std::cout << " (" << bodyVariants[currentBodyVariant]->name << ")";
    }
    std::cout << std::endl;

    std::cout << "Head variant: " << (currentHeadVariant + 1) << "/" << headVariants.size();
    if (!headVariants.empty() && currentHeadVariant < headVariants.size()) {
        std::cout << " (" << headVariants[currentHeadVariant]->name << ")";
    }
    std::cout << std::endl;

    if (useEquipmentTextures) {
        std::cout << "Equipment textures: ON (texture=" << (int)currentBodyTexture
                  << " " << getTextureName(currentBodyTexture) << ")" << std::endl;
    }

    // Start in static mode - build static mesh from current variants
    useAnimatedMesh = false;
    rebuildCurrentVariant();

    // Attach equipment models (weapons/shields) if present
    // Equipment slots: 7 = Primary (right hand), 8 = Secondary (left hand/shield)
    if (entity.equipment[7] > 0 || entity.equipment[8] > 0) {
        attachEquipmentModels(entity.equipment[7], entity.equipment[8]);
    }

    std::cout << "Loaded entity in static mode. Press 'S' to switch to animated mode." << std::endl;
}

// Cycle through loaded entities
void cycleEntity(int direction) {
    if (loadedEntities.empty()) {
        std::cout << "No entities loaded. Use --entities <file.json> to load entity data." << std::endl;
        return;
    }

    currentEntityIndex += direction;
    if (currentEntityIndex < 0) {
        currentEntityIndex = static_cast<int>(loadedEntities.size()) - 1;
    } else if (currentEntityIndex >= static_cast<int>(loadedEntities.size())) {
        currentEntityIndex = 0;
    }

    // Update current textures from entity data
    if (currentEntityIndex >= 0 && currentEntityIndex < static_cast<int>(loadedEntities.size())) {
        currentBodyTexture = loadedEntities[currentEntityIndex].texture;
        currentHelmTexture = loadedEntities[currentEntityIndex].equipment[0];  // Head slot
    }

    loadEntityModel(currentEntityIndex);
}

// Helper to get equipment variant texture name (same logic as static mode buildMesh)
std::string getEquipmentVariantTextureName(const std::string& baseTexName, uint8_t textureVariant) {
    if (textureVariant == 0) {
        return baseTexName;  // No override for variant 0
    }

    std::string lowerTexName = baseTexName;
    std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Pattern: {race}{part}01{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
    // Variant 1 textures have "01" in the name, variant 2 has "02", etc.
    // e.g., qcmhe0101.bmp is already variant 1, qcmch0001.bmp base -> qcmch0101.bmp for variant 1

    // First try to find "00" (base texture) and replace with variant
    size_t pos = lowerTexName.find("00");
    if (pos != std::string::npos && pos >= 3 && lowerTexName.length() > pos + 4) {
        char variantStr[8];
        snprintf(variantStr, sizeof(variantStr), "%02d", textureVariant);
        return lowerTexName.substr(0, pos) + variantStr + lowerTexName.substr(pos + 2);
    }

    return baseTexName;  // Return original if pattern doesn't match
}

// Reload current model with updated body/helm textures
void reloadEntityWithTextures() {
    if (!raceModelLoader) {
        std::cout << "Race model loader not initialized" << std::endl;
        return;
    }

    std::cout << "\n=== Reloading with textures ===" << std::endl;
    std::cout << "Equipment: " << (useEquipmentTextures ? "ON" : "OFF");
    if (useEquipmentTextures) {
        std::cout << " Body: " << (int)currentBodyTexture << " (" << getTextureName(currentBodyTexture) << ")";
    }
    std::cout << std::endl;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove existing nodes
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }

    // Create animated node with current body/head variants
    animatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
        currentRaceId, currentGender,
        static_cast<uint8_t>(currentHeadVariant),
        static_cast<uint8_t>(currentBodyVariant),
        nullptr, -1);

    if (animatedNode) {
        animatedNode->setScale(core::vector3df(10, 10, 10));
        animatedNode->setPosition(core::vector3df(0, 0, 0));
        animatedNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

        // Apply equipment textures using same logic as static mode
        if (useEquipmentTextures) {
            if (IsDebugEnabled()) {
                std::cout << "  Applying equipment texture overrides (animated mode):" << std::endl;
                std::cout << "    Body texture: " << (int)currentBodyTexture << ", Helm texture: " << (int)currentHelmTexture << std::endl;
            }
            // Get the mesh to read current texture names
            irr::scene::IAnimatedMesh* animMesh = animatedNode->getMesh();
            if (animMesh) {
                irr::scene::IMesh* mesh = animMesh->getMesh(0);
                if (mesh) {
                    for (u32 i = 0; i < mesh->getMeshBufferCount() && i < animatedNode->getMaterialCount(); ++i) {
                        irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(i);
                        if (!buffer) continue;

                        irr::video::ITexture* currentTex = buffer->getMaterial().getTexture(0);
                        if (!currentTex) continue;

                        std::string currentTexName = currentTex->getName().getPath().c_str();
                        // Extract just filename
                        size_t lastSlash = currentTexName.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            currentTexName = currentTexName.substr(lastSlash + 1);
                        }
                        // Strip "eqt_tex_" or "model_viewer_" prefix if present
                        if (currentTexName.find("eqt_tex_") == 0) {
                            currentTexName = currentTexName.substr(8);
                        } else if (currentTexName.find("model_viewer_") == 0) {
                            currentTexName = currentTexName.substr(13);
                        }

                        std::string lowerTexName = currentTexName;
                        std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                                      [](unsigned char c) { return std::tolower(c); });

                        // Check if this is a head texture (contains "he" after race code, e.g., humhe0001.bmp)
                        bool isHeadTexture = false;
                        /*
			size_t hePos = lowerTexName.find("he");
                        if (hePos != std::string::npos && hePos >= 3 && hePos <= 4) {
                            // Check if followed by digits (head texture pattern)
                            if (hePos + 2 < lowerTexName.size() && std::isdigit(lowerTexName[hePos + 2])) {
                                isHeadTexture = true;
                            }
                        }
			*/

                        // Determine which texture variant to use
                        uint8_t textureVariant = isHeadTexture ? currentHelmTexture : currentBodyTexture;

                        if (textureVariant > 0) {
                            // Get equipment variant texture name
                            std::string equipTexName = getEquipmentVariantTextureName(currentTexName, textureVariant);

                            if (equipTexName != currentTexName) {
                                // Look up the equipment texture in characterTextures
                                std::string lowerEquipTex = equipTexName;
                                std::transform(lowerEquipTex.begin(), lowerEquipTex.end(), lowerEquipTex.begin(),
                                              [](unsigned char c) { return std::tolower(c); });

                                std::string lowerBaseTex = currentTexName;
                                std::transform(lowerBaseTex.begin(), lowerBaseTex.end(), lowerBaseTex.begin(),
                                              [](unsigned char c) { return std::tolower(c); });

                                auto texIt = characterTextures.find(lowerEquipTex);
                                if (texIt != characterTextures.end() && texIt->second && !texIt->second->data.empty()) {
                                    irr::video::ITexture* equipTex = loadTexture(equipTexName, texIt->second->data);
                                    if (equipTex) {
                                        animatedNode->getMaterial(i).setTexture(0, equipTex);
                                        if (IsDebugEnabled()) {
                                            std::cout << "    Texture override (" << (isHeadTexture ? "head" : "body") << "): "
                                                      << currentTexName << " -> " << equipTexName << std::endl;
                                        }
                                    }
                                } else if (IsDebugEnabled()) {
                                    std::cout << "    Texture override failed: " << equipTexName << " not found in cache" << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Get available animations
        availableAnimations = animatedNode->getAnimationList();

        // Restore animation if one was playing, or start with 'o01' (idle) if available
        if (currentAnimIndex >= 0 && currentAnimIndex < static_cast<int>(availableAnimations.size())) {
            animatedNode->playAnimation(availableAnimations[currentAnimIndex], true);
            animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
        } else if (!availableAnimations.empty()) {
            // Prefer 'o01' (idle/stand) animation if it exists
            currentAnimIndex = 0;
            auto it = std::find(availableAnimations.begin(), availableAnimations.end(), "o01");
            if (it != availableAnimations.end()) {
                currentAnimIndex = static_cast<int>(std::distance(availableAnimations.begin(), it));
            }
            animatedNode->playAnimation(availableAnimations[currentAnimIndex], true);
            animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
        }

        for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
            animatedNode->getMaterial(i).Wireframe = wireframe;
            animatedNode->getMaterial(i).BackfaceCulling = false;
            animatedNode->getMaterial(i).Lighting = false;
        }

        std::cout << "Reloaded with " << animatedNode->getMaterialCount() << " materials" << std::endl;

        // Update hand bone indices for spell particle emission
        if (spellTestMode) {
            casterRightHandBoneIdx = animatedNode->findRightHandBoneIndex();
            casterLeftHandBoneIdx = animatedNode->findLeftHandBoneIndex();
            std::cout << "  Hand bones: R=" << casterRightHandBoneIdx
                      << " L=" << casterLeftHandBoneIdx << std::endl;
        }

        // Attach equipment models if we have entity data
        if (currentEntityIndex >= 0 && currentEntityIndex < static_cast<int>(loadedEntities.size())) {
            const EntityData& entity = loadedEntities[currentEntityIndex];
            if (entity.equipment[7] > 0 || entity.equipment[8] > 0) {
                attachEquipmentModels(entity.equipment[7], entity.equipment[8]);
            }
        }
    } else {
        std::cout << "Failed to create animated mesh" << std::endl;
    }
}

// Helper to find bone index by trying multiple name variants (case-insensitive)
int findBoneIndex(const std::shared_ptr<CharacterSkeleton>& skeleton, const std::string& raceCode,
                  const std::vector<std::string>& suffixes, bool verbose = true) {
    // Try each suffix with different case combinations
    for (const auto& suffix : suffixes) {
        // Try: RACECODESUFFIX, racecodesuffix, RaceCodeSuffix variations
        std::vector<std::string> variants;

        // Uppercase race + suffix
        std::string upper = raceCode + suffix;
        variants.push_back(upper);

        // Lowercase race + suffix
        std::string lower = raceCode + suffix;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        variants.push_back(lower);

        // Also try without _DAG suffix if present
        if (suffix.length() > 4 && suffix.substr(suffix.length() - 4) == "_DAG") {
            std::string noDAG = suffix.substr(0, suffix.length() - 4);
            std::string upperNoDAG = raceCode + noDAG;
            variants.push_back(upperNoDAG);
            std::string lowerNoDAG = raceCode + noDAG;
            std::transform(lowerNoDAG.begin(), lowerNoDAG.end(), lowerNoDAG.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            variants.push_back(lowerNoDAG);
        }

        for (const auto& boneName : variants) {
            int idx = skeleton->getBoneIndex(boneName);
            if (idx >= 0) {
                if (verbose) {
                    std::cout << "  Found bone: " << boneName << " (index " << idx << ")" << std::endl;
                }
                return idx;
            }
        }
    }
    return -1;
}

// Stored bone indices for equipment attachment (cached after first lookup)
int primaryBoneIndex = -1;
int secondaryBoneIndex = -1;
float primaryWeaponOffset = 0.0f;  // Offset to position handle at hand
float secondaryWeaponOffset = 0.0f;  // Offset for shield/secondary weapon

// Update equipment positions based on current bone states
// Extract Euler angles from a bone transformation matrix
// Returns angles in degrees for Irrlicht (converting from EQ coordinate system)
core::vector3df extractBoneRotation(const BoneMat4& m) {
    // Build an Irrlicht matrix from the bone transform, swapping Y/Z for coordinate conversion
    // EQ matrix is column-major: m[0-3] = col0, m[4-7] = col1, etc.
    // We need to swap Y and Z axes
    core::matrix4 irrMat;
    irrMat[0] = m.m[0];   irrMat[1] = m.m[2];   irrMat[2] = m.m[1];   irrMat[3] = 0;
    irrMat[4] = m.m[8];   irrMat[5] = m.m[10];  irrMat[6] = m.m[9];   irrMat[7] = 0;
    irrMat[8] = m.m[4];   irrMat[9] = m.m[6];   irrMat[10] = m.m[5];  irrMat[11] = 0;
    irrMat[12] = 0;       irrMat[13] = 0;       irrMat[14] = 0;       irrMat[15] = 1;

    return irrMat.getRotationDegrees();
}

void updateEquipmentPositions() {
    if (!animatedNode || !useAnimatedMesh) return;

    auto skeleton = animatedNode->getEQMesh()->getSkeleton();
    if (!skeleton) return;

    const auto& boneStates = animatedNode->getAnimator().getBoneStates();
    if (boneStates.empty()) return;

    // Update primary weapon position and rotation
    if (primaryEquipNode && primaryBoneIndex >= 0 && primaryBoneIndex < static_cast<int>(boneStates.size())) {
        const BoneMat4& worldXform = boneStates[primaryBoneIndex].worldTransform;
        float px = worldXform.m[12];
        float py = worldXform.m[13];
        float pz = worldXform.m[14];

        // Extract and apply bone rotation, then add weapon's base rotation
        core::vector3df boneRot = extractBoneRotation(worldXform);
        core::vector3df weaponRot = boneRot + core::vector3df(180, 0, 0);
        primaryEquipNode->setRotation(weaponRot);

        // Apply offset along the weapon's local X axis (blade direction)
        // Transform offset by the weapon's rotation to get world-space offset
        core::matrix4 rotMat;
        rotMat.setRotationDegrees(weaponRot);
        core::vector3df localOffset(primaryWeaponOffset, 0, 0);  // Offset along blade
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        primaryEquipNode->setPosition(core::vector3df(px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }

    // Update secondary weapon/shield position and rotation
    if (secondaryEquipNode && secondaryBoneIndex >= 0 && secondaryBoneIndex < static_cast<int>(boneStates.size())) {
        const BoneMat4& worldXform = boneStates[secondaryBoneIndex].worldTransform;
        float px = worldXform.m[12];
        float py = worldXform.m[13];
        float pz = worldXform.m[14];

        // Extract and apply bone rotation, then add shield's base rotation (180° around X and Z)
        core::vector3df boneRot = extractBoneRotation(worldXform);
        core::vector3df shieldRot = boneRot + core::vector3df(180, 0, 180);
        secondaryEquipNode->setRotation(shieldRot);

        // Apply offset along local X axis
        core::matrix4 rotMat;
        rotMat.setRotationDegrees(shieldRot);
        core::vector3df localOffset(secondaryWeaponOffset, 0, 0);
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        secondaryEquipNode->setPosition(core::vector3df(px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }
}

// Attach equipment models (weapons/shields) to the current entity
void attachEquipmentModels(uint32_t primaryItemId, uint32_t secondaryItemId) {
    std::cout << "attachEquipmentModels(" << primaryItemId << ", " << secondaryItemId << ")" << std::endl << std::flush;

    // Remove existing equipment nodes and reset bone indices/offsets
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    primaryBoneIndex = -1;
    primaryWeaponOffset = 0.0f;

    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }
    secondaryBoneIndex = -1;
    secondaryWeaponOffset = 0.0f;

    std::cout << "  Checking equipmentModelLoader..." << std::flush;
    if (!equipmentModelLoader || !equipmentModelLoader->isLoaded()) {
        std::cout << " not loaded, returning" << std::endl;
        return;
    }
    std::cout << " OK" << std::endl;

    // Get the parent node - use animatedNode if in animated mode, modelNode for static mode
    scene::ISceneNode* parentNode = nullptr;
    if (animatedNode && useAnimatedMesh) {
        parentNode = animatedNode;
    } else if (modelNode) {
        parentNode = modelNode;
    }

    std::cout << "Equipment parent: " << (parentNode ? (useAnimatedMesh ? "animatedNode" : "modelNode") : "none (root)") << std::endl << std::flush;

    // Get skeleton and bone states from animated node (only available in animated mode)
    std::shared_ptr<CharacterSkeleton> skeleton;
    const std::vector<AnimatedBoneState>* boneStates = nullptr;

    // Get race code for bone name lookup (uppercase)
    std::string raceCode = currentRaceCode;
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });
    std::cout << "  Race code for bone lookup: " << raceCode << std::endl;

    if (animatedNode) {
        std::cout << "  Getting skeleton from animatedNode..." << std::flush;
        EQAnimatedMesh* eqMesh = animatedNode->getEQMesh();
        std::cout << " eqMesh=" << (eqMesh ? "OK" : "NULL") << std::flush;
        if (eqMesh) {
            skeleton = eqMesh->getSkeleton();
            std::cout << " skeleton=" << (skeleton ? "OK" : "NULL") << std::flush;
        }
        std::cout << std::endl;

        std::cout << "  Getting bone states..." << std::flush;
        const auto& states = animatedNode->getAnimator().getBoneStates();
        boneStates = &states;
        std::cout << " count=" << boneStates->size() << std::endl << std::flush;

        // Debug: list all bone names
        if (skeleton) {
            std::cout << "  Available bones: ";
            for (size_t i = 0; i < skeleton->bones.size() && i < 10; ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << skeleton->bones[i].name;
            }
            if (skeleton->bones.size() > 10) std::cout << "... (" << skeleton->bones.size() << " total)";
            std::cout << std::endl;
        }
    }

    // Character scale factor - equipment inherits parent scale, so use 1:1 when parented
    const float equipScale = parentNode ? 1.0f : 10.0f;

    // Primary weapon (right hand) - equipment slot 7
    if (primaryItemId > 0) {
        int modelId = static_cast<int>(primaryItemId);
        std::cout << "Looking for primary weapon model IT" << modelId << std::endl;
        scene::IMesh* equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
        std::cout << "  Direct lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;

        if (!equipMesh) {
            int mappedId = equipmentModelLoader->getModelIdForItem(primaryItemId);
            std::cout << "  Item lookup for " << primaryItemId << " -> " << mappedId << std::endl;
            if (mappedId >= 0) {
                modelId = mappedId;
                equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
                std::cout << "  Mapped lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;
            }
        }

        if (equipMesh) {
            const auto* modelData = equipmentModelLoader->getEquipmentModelData(modelId);
            if (modelData) {
                std::cout << "  Source: " << modelData->sourceArchive << " / " << modelData->sourceWld << std::endl;
                std::cout << "  Geometry: " << modelData->geometryName << std::endl;
                std::cout << "  Textures: ";
                for (size_t i = 0; i < modelData->textureNames.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modelData->textureNames[i];
                }
                std::cout << std::endl;
            }

            // Create equipment node parented to character
            primaryEquipNode = smgr->addMeshSceneNode(equipMesh, parentNode);
            if (primaryEquipNode) {
                // Default position/rotation - offset to right side of character
                core::vector3df pos(2.0f, 1.5f, 0.0f);  // Local offset in character space
                // Rotate 180° around X to point outward and face edge inward
                core::vector3df rot(180, 0, 0);

                // Try to find right hand bone (various naming conventions)
                if (skeleton && boneStates && !boneStates->empty()) {
                    // List all bones with relevant keywords to help debug
                    std::cout << "  Available bones with R/POINT/WRIST/HAND:" << std::endl;
                    for (size_t i = 0; i < skeleton->bones.size(); ++i) {
                        const std::string& name = skeleton->bones[i].name;
                        std::string upperName = name;
                        std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                                      [](unsigned char c) { return std::toupper(c); });
                        if (upperName.find("R_") != std::string::npos ||
                            upperName.find("_R") != std::string::npos ||
                            upperName.find("POINT") != std::string::npos ||
                            upperName.find("WRIST") != std::string::npos ||
                            upperName.find("HAND") != std::string::npos) {
                            std::cout << "    [" << i << "] " << name << std::endl;
                        }
                    }
                    // Bone names: qcmr_point (lowercase, no underscore between race and "r")
                    primaryBoneIndex = findBoneIndex(skeleton, raceCode, {"r_point", "R_POINT", "R_POINT_DAG", "BO_R_DAG", "TO_R_DAG"});
                    if (primaryBoneIndex >= 0 && primaryBoneIndex < static_cast<int>(boneStates->size())) {
                        const BoneMat4& worldXform = (*boneStates)[primaryBoneIndex].worldTransform;
                        // Extract position from matrix (EQ coords)
                        float px = worldXform.m[12];
                        float py = worldXform.m[13];
                        float pz = worldXform.m[14];
                        // Convert EQ(x,y,z) to Irrlicht(x,z,y)
                        pos = core::vector3df(px, pz, py);
                        std::cout << "  Bone position: (" << px << ", " << py << ", " << pz << ") -> Irr(" << pos.X << ", " << pos.Y << ", " << pos.Z << ")" << std::endl;
                    }
                }

                // Offset along blade (local X axis) so handle grip is at hand
                core::aabbox3df bbox = equipMesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;
                primaryWeaponOffset = weaponLength * 0.35f;  // ~35% puts grip at hand

                // Apply rotated offset to initial position
                core::matrix4 rotMat;
                rotMat.setRotationDegrees(rot);
                core::vector3df localOffset(primaryWeaponOffset, 0, 0);
                rotMat.rotateVect(localOffset);
                pos += localOffset;

                primaryEquipNode->setPosition(pos);
                primaryEquipNode->setScale(core::vector3df(equipScale, equipScale, equipScale));
                primaryEquipNode->setRotation(rot);
                std::cout << "  Position: (" << pos.X << ", " << pos.Y << ", " << pos.Z << "), scale: " << equipScale << std::endl;

                for (u32 i = 0; i < primaryEquipNode->getMaterialCount(); ++i) {
                    primaryEquipNode->getMaterial(i).BackfaceCulling = false;
                    primaryEquipNode->getMaterial(i).Lighting = false;
                    primaryEquipNode->getMaterial(i).Wireframe = wireframe;
                }

                std::cout << "Attached primary weapon: IT" << modelId << std::endl;
            }
        } else {
            std::cout << "Primary weapon mesh not found: IT" << primaryItemId << std::endl;
        }
    }

    // Secondary weapon/shield (left hand) - equipment slot 8
    if (secondaryItemId > 0) {
        int modelId = static_cast<int>(secondaryItemId);
        std::cout << "Looking for secondary weapon/shield model IT" << modelId << std::endl;
        scene::IMesh* equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
        std::cout << "  Direct lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;

        if (!equipMesh) {
            int mappedId = equipmentModelLoader->getModelIdForItem(secondaryItemId);
            std::cout << "  Item lookup for " << secondaryItemId << " -> " << mappedId << std::endl;
            if (mappedId >= 0) {
                modelId = mappedId;
                equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
                std::cout << "  Mapped lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;
            }
        }

        if (equipMesh) {
            const auto* modelData = equipmentModelLoader->getEquipmentModelData(modelId);
            if (modelData) {
                std::cout << "  Source: " << modelData->sourceArchive << " / " << modelData->sourceWld << std::endl;
                std::cout << "  Geometry: " << modelData->geometryName << std::endl;
                std::cout << "  Textures: ";
                for (size_t i = 0; i < modelData->textureNames.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modelData->textureNames[i];
                }
                std::cout << std::endl;
            }

            bool isShield = EquipmentModelLoader::isShield(modelId);

            // Create equipment node parented to character
            secondaryEquipNode = smgr->addMeshSceneNode(equipMesh, parentNode);
            if (secondaryEquipNode) {
                // Default position/rotation - offset to left side of character
                core::vector3df pos(-2.0f, 1.5f, 0.0f);  // Local offset in character space
                // Shields rotate 180° around X and Z so outside faces outward with correct texture, weapons use (180,0,0)
                core::vector3df rot(180.0f, 0, isShield ? 180.0f : 0);

                // Try to find left hand bone (various naming conventions)
                // Bone names: qcmshield_point (lowercase, no underscore between race and "shield")
                if (skeleton && boneStates && !boneStates->empty()) {
                    std::vector<std::string> suffixes = isShield
                        ? std::vector<std::string>{"shield_point", "SHIELD_POINT", "SHIELD_POINT_DAG", "l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG"}
                        : std::vector<std::string>{"l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG"};
                    secondaryBoneIndex = findBoneIndex(skeleton, raceCode, suffixes);
                    if (secondaryBoneIndex >= 0 && secondaryBoneIndex < static_cast<int>(boneStates->size())) {
                        const BoneMat4& worldXform = (*boneStates)[secondaryBoneIndex].worldTransform;
                        float px = worldXform.m[12];
                        float py = worldXform.m[13];
                        float pz = worldXform.m[14];
                        pos = core::vector3df(px, pz, py);
                        std::cout << "  Bone position: (" << px << ", " << py << ", " << pz << ") -> Irr(" << pos.X << ", " << pos.Y << ", " << pos.Z << ")" << std::endl;
                    }
                }

                // Offset shield down (along local X axis after rotation)
                if (isShield) {
                    core::aabbox3df bbox = equipMesh->getBoundingBox();
                    float shieldHeight = bbox.MaxEdge.X - bbox.MinEdge.X;
                    secondaryWeaponOffset = shieldHeight * 0.175f;

                    core::matrix4 rotMat;
                    rotMat.setRotationDegrees(rot);
                    core::vector3df localOffset(secondaryWeaponOffset, 0, 0);
                    rotMat.rotateVect(localOffset);
                    pos += localOffset;
                }

                secondaryEquipNode->setPosition(pos);
                secondaryEquipNode->setScale(core::vector3df(equipScale, equipScale, equipScale));
                secondaryEquipNode->setRotation(rot);
                std::cout << "  Position: (" << pos.X << ", " << pos.Y << ", " << pos.Z << "), scale: " << equipScale << std::endl;

                for (u32 i = 0; i < secondaryEquipNode->getMaterialCount(); ++i) {
                    secondaryEquipNode->getMaterial(i).BackfaceCulling = false;
                    secondaryEquipNode->getMaterial(i).Lighting = false;
                    secondaryEquipNode->getMaterial(i).Wireframe = wireframe;
                }

                std::cout << "Attached secondary " << (isShield ? "shield" : "weapon")
                          << ": IT" << modelId << std::endl;
            }
        } else {
            std::cout << "Secondary weapon mesh not found: IT" << secondaryItemId << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    std::string raceCodeArg = "HUM";
    std::string entityFile = "";
    std::string entityName = "";
    int debugLevel = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--entities" && i + 1 < argc) {
            entityFile = argv[++i];
        } else if (arg == "--entity" && i + 1 < argc) {
            entityName = argv[++i];
        } else if (arg == "--client" && i + 1 < argc) {
            clientPath = argv[++i];
            if (clientPath.back() != '/') clientPath += '/';
        } else if (arg == "--spell-test") {
            spellTestMode = true;
        } else if (arg == "--gl") {
            useGLRenderer = true;
        } else if (arg == "--gles2") {
            useGLES2Renderer = true;
        } else if (arg == "--debug" || arg == "-d") {
            debugLevel = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                // Check if next arg is a number
                try {
                    debugLevel = std::stoi(argv[i + 1]);
                    ++i;
                } catch (...) {
                    // Not a number, keep debugLevel = 1
                }
            }
        } else if (arg[0] != '-') {
            // First non-flag arg is client path, second is race code
            if (clientPath == "/home/user/projects/claude/EverQuestP1999/") {
                clientPath = arg;
                if (clientPath.back() != '/') clientPath += '/';
            } else {
                raceCodeArg = arg;
                std::transform(raceCodeArg.begin(), raceCodeArg.end(), raceCodeArg.begin(),
                              [](unsigned char c) { return std::toupper(c); });
            }
        }
    }

    // Set the global debug level
    SetDebugLevel(debugLevel);

    std::cout << "EQ Model Viewer" << std::endl;
    std::cout << "Client path: " << clientPath << std::endl;
    std::cout << "Debug level: " << debugLevel << std::endl;
    std::cout << "Usage: model_viewer [client_path] [race_code]" << std::endl;
    std::cout << "       model_viewer --entities <file.json> [--entity <name>]" << std::endl;
    std::cout << "       model_viewer --spell-test [--gl|--gles2] --client <path>" << std::endl;
    std::cout << "       --debug, -d [level]  Enable debug output (default level 1)" << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Arrow keys: Rotate (Y/X axis)" << std::endl;
    std::cout << "  Q/E: Rotate Z axis" << std::endl;
    std::cout << "  +/-: Zoom in/out" << std::endl;
    std::cout << "  S: Toggle static/animated mode" << std::endl;
    std::cout << "  V/Shift+V: Cycle body variant next/prev" << std::endl;
    std::cout << "  H/Shift+H: Cycle head variant next/prev" << std::endl;
    std::cout << "  B: Toggle equipment textures on/off" << std::endl;
    std::cout << "  N/Shift+N: Cycle body texture next/prev (when equipment textures on)" << std::endl;
    std::cout << "  A/Shift+A: Cycle animations next/prev" << std::endl;
    std::cout << "  Space: Pause/Resume animation" << std::endl;
    std::cout << "  [/]: Decrease/Increase animation speed" << std::endl;
    std::cout << "  W: Toggle wireframe" << std::endl;
    std::cout << "  R: Reset rotation" << std::endl;
    std::cout << "  ,/.: Cycle entities (prev/next)" << std::endl;
    std::cout << "  F7/Shift+F7: Cycle spell effects (next/prev)" << std::endl;
    std::cout << "  1-0: Cast spell from slot" << std::endl;
    std::cout << "  ESC: Quit" << std::endl;
    std::cout << std::endl;

    // Load entity file if specified
    bool usingEntityMode = false;
    if (!entityFile.empty()) {
        loadedEntities = parseEntityFile(entityFile);
        if (!loadedEntities.empty()) {
            usingEntityMode = true;
            std::cout << "Entities available:" << std::endl;
            for (size_t i = 0; i < loadedEntities.size(); i++) {
                std::cout << "  [" << i << "] " << loadedEntities[i].name
                          << " (race=" << loadedEntities[i].race_id
                          << ", gender=" << (int)loadedEntities[i].gender << ")" << std::endl;
            }
            std::cout << std::endl;
        }
    }

    // For entity mode, skip upfront S3D loading - RaceModelLoader handles it lazily
    std::shared_ptr<S3DZone> zone = nullptr;
    std::shared_ptr<CharacterModel> character = nullptr;

    if (!usingEntityMode && !spellTestMode) {
        // Direct race code mode - load global_chr.s3d
        std::string s3dPath = clientPath + "global_chr.s3d";
        std::cout << "Loading: " << s3dPath << std::endl;

        S3DLoader loader;
        if (!loader.loadZone(s3dPath)) {
            LOG_ERROR(MOD_MAIN, "Failed to load {}: {}", s3dPath, loader.getError());
            return 1;
        }

        zone = loader.getZone();
        if (!zone || zone->characters.empty()) {
            LOG_ERROR(MOD_MAIN, "No character models found in {}", s3dPath);
            return 1;
        }

        std::cout << "Found " << zone->characters.size() << " character models" << std::endl;

        // Find the requested model
        currentRaceCode = raceCodeArg;

        for (const auto& ch : zone->characters) {
        // Extract model base from skeleton name (remove _HS_DEF suffix)
        std::string modelBase = ch->name;
        size_t hsDefPos = modelBase.find("_HS_DEF");
        if (hsDefPos != std::string::npos) {
            modelBase = modelBase.substr(0, hsDefPos);
        }
        // Convert to uppercase for comparison
        std::string upperBase = modelBase;
        std::transform(upperBase.begin(), upperBase.end(), upperBase.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        std::cout << "  Available: " << ch->name << " (base: " << upperBase
                  << ", " << ch->parts.size() << " parts";
        if (ch->animatedSkeleton) {
            std::cout << ", " << ch->animatedSkeleton->animations.size() << " anims";
        }
        std::cout << ")" << std::endl;

        // Look for requested model
        if (upperBase == currentRaceCode) {
            character = ch;
            std::cout << "  -> Selected " << currentRaceCode << " model" << std::endl;
        }
    }

    // Fall back to first model if not found
    if (!character && !zone->characters.empty()) {
        character = zone->characters[0];
        std::cout << "  -> " << currentRaceCode << " not found, using first model" << std::endl;
    }

    if (!character) {
        LOG_ERROR(MOD_MAIN, "No character model found");
        return 1;
    }
    std::cout << "Character: " << character->name << " with " << character->parts.size() << " parts" << std::endl;

    // Map race code to ID for animated mesh loading
    // This is a simplified mapping - could be expanded
    if (currentRaceCode == "HUM") currentRaceId = 1;
    else if (currentRaceCode == "BAM") currentRaceId = 2;
    else if (currentRaceCode == "ERM") currentRaceId = 3;
    else if (currentRaceCode == "ELM") currentRaceId = 4;
    else if (currentRaceCode == "HIM") currentRaceId = 5;
    else if (currentRaceCode == "DAM") currentRaceId = 6;
    else if (currentRaceCode == "HAM") currentRaceId = 7;
    else if (currentRaceCode == "DWM") currentRaceId = 8;
    else if (currentRaceCode == "TRM") currentRaceId = 9;
    else if (currentRaceCode == "OGM") currentRaceId = 10;
    else if (currentRaceCode == "HOM") currentRaceId = 11;
    else if (currentRaceCode == "GNM") currentRaceId = 12;
    else if (currentRaceCode == "ELE") currentRaceId = 75;  // Elemental
    else if (currentRaceCode == "SKE") currentRaceId = 21;  // Skeleton
    else currentRaceId = 1;  // Default to human

    // Extract race code from skeleton name for classifying parts
    std::string raceCode = character->name;
    size_t hsDefPos = raceCode.find("_HS_DEF");
    if (hsDefPos != std::string::npos) {
        raceCode = raceCode.substr(0, hsDefPos);
    }
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Categorize parts into body variants and head variants
    for (const auto& part : character->parts) {
        std::string partName = part->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        // Check if it's a head variant (contains "HE" after race code)
        std::string headPattern = raceCode + "HE";
        if (partName.find(headPattern) == 0) {
            headVariants.push_back(part);
            std::cout << "  Head variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
        } else if (partName.find(raceCode) == 0) {
            bodyVariants.push_back(part);
            std::cout << "  Body variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
        }
    }

    std::cout << "Found " << bodyVariants.size() << " body variants, "
              << headVariants.size() << " head variants" << std::endl;

    // Start with first body and first head variant
    currentBodyVariant = 0;
    currentHeadVariant = 0;

    // Combine initial variants
    combineCurrentVariants();
    if (!characterGeometry || characterGeometry->vertices.empty()) {
        LOG_ERROR(MOD_MAIN, "Failed to combine character variants");
        return 1;
    }

    std::cout << "Initial geometry: " << characterGeometry->vertices.size() << " vertices, "
              << characterGeometry->triangles.size() << " triangles" << std::endl;

    // Load textures from all S3D files (base textures first, then overrides)
    // Order: global_chr.s3d -> global2-7_chr.s3d -> zone_chr.s3d (zone takes precedence)
    if (debugLevel >= 1) {
        std::cout << "\n=== Loading Textures ===" << std::endl;
    }

    // 1. Start with textures from global_chr.s3d
    characterTextures = zone->characterTextures;
    if (debugLevel >= 1) {
        std::cout << "  [global_chr.s3d] Loaded " << characterTextures.size() << " textures" << std::endl;
        if (debugLevel >= 2) {
            for (const auto& [name, tex] : characterTextures) {
                std::cout << "    - " << name << " (" << (tex ? tex->data.size() : 0) << " bytes)" << std::endl;
            }
        }
    }

    // 2. Load textures from global2-7_chr.s3d files
    for (int globalNum = 2; globalNum <= 7; ++globalNum) {
        std::string globalPath = clientPath + "global" + std::to_string(globalNum) + "_chr.s3d";
        S3DLoader globalLoader;
        if (globalLoader.loadZone(globalPath)) {
            auto globalData = globalLoader.getZone();
            if (globalData && !globalData->characterTextures.empty()) {
                size_t addedCount = 0;
                for (const auto& [name, tex] : globalData->characterTextures) {
                    if (characterTextures.find(name) == characterTextures.end()) {
                        characterTextures[name] = tex;
                        addedCount++;
                    }
                }
                if (debugLevel >= 1) {
                    std::cout << "  [global" << globalNum << "_chr.s3d] Added " << addedCount
                              << " new textures (skipped " << (globalData->characterTextures.size() - addedCount)
                              << " duplicates)" << std::endl;
                }
            } else if (debugLevel >= 1) {
                std::cout << "  [global" << globalNum << "_chr.s3d] Loaded (no character textures)" << std::endl;
            }
        } else if (debugLevel >= 1) {
            std::cout << "  [global" << globalNum << "_chr.s3d] Not found or failed to load" << std::endl;
        }
    }

    // 3. Load zone-specific textures (these take precedence over global textures)
    if (!currentZoneName.empty()) {
        std::string zoneS3dPath = clientPath + currentZoneName + "_chr.s3d";
        S3DLoader zoneLoader;
        if (zoneLoader.loadZone(zoneS3dPath)) {
            auto zoneData = zoneLoader.getZone();
            if (zoneData && !zoneData->characterTextures.empty()) {
                size_t overrideCount = 0;
                size_t newCount = 0;
                for (const auto& [name, tex] : zoneData->characterTextures) {
                    if (characterTextures.find(name) != characterTextures.end()) {
                        overrideCount++;
                    } else {
                        newCount++;
                    }
                    characterTextures[name] = tex;  // Override or add
                }
                if (debugLevel >= 1) {
                    std::cout << "  [" << currentZoneName << "_chr.s3d] Added " << newCount
                              << " new textures, overrode " << overrideCount << " existing" << std::endl;
                }
                if (debugLevel >= 2) {
                    std::cout << "    Zone textures:" << std::endl;
                    for (const auto& [name, tex] : zoneData->characterTextures) {
                        std::cout << "      - " << name << " (" << (tex ? tex->data.size() : 0) << " bytes)" << std::endl;
                    }
                }
            }
        } else if (debugLevel >= 1) {
            std::cout << "  [" << currentZoneName << "_chr.s3d] Not found or failed to load" << std::endl;
        }
    }

        if (debugLevel >= 1) {
            std::cout << "  Total: " << characterTextures.size() << " textures loaded" << std::endl;
        }
    } // end if (!usingEntityMode)

    // Select renderer driver type
    video::E_DRIVER_TYPE driverType = video::EDT_SOFTWARE;
    if (useGLRenderer) {
        driverType = video::EDT_OPENGL;
        std::cout << "Using OpenGL renderer" << std::endl;
    }
#ifdef EQT_HAS_GLES2
    else if (useGLES2Renderer) {
        driverType = video::EDT_OGLES2;
        std::cout << "Using GLES2 renderer" << std::endl;
    }
#endif
    else if (spellTestMode) {
        // Spell test mode defaults to GL for particle effects
        driverType = video::EDT_OPENGL;
        std::cout << "Spell test mode: using OpenGL renderer (use --gles2 for GLES2)" << std::endl;
    }

    // Create Irrlicht device
    device = createDevice(driverType,
                          core::dimension2d<u32>(800, 600),
                          32, false, false, false, &receiver);

    if (!device) {
        LOG_ERROR(MOD_MAIN, "Failed to create Irrlicht device");
        return 1;
    }

    device->setWindowCaption(L"EQ Model Viewer - A/Z: animations, Space: pause, S: toggle mode");

    driver = device->getVideoDriver();
    smgr = device->getSceneManager();
    fileSystem = device->getFileSystem();

    // Initialize spell bar for casting visualization
    spellBar.initialize(driver, device->getGUIEnvironment());
    spellBar.setScreenSize(800, 600);
    spellBar.setSpellClickCallback([](int index, const EQT::SpellBarEntry& spell) {
        // Don't start a new cast if already casting
        if (castingBar.isCasting()) {
            std::cout << "Already casting - cannot cast " << spell.name << std::endl;
            return;
        }

        // Create a modified spell with 2x cast time for testing
        EQT::SpellBarEntry testSpell = spell;
        testSpell.castTime = spell.castTime * 2.0f;  // Double cast time for testing

        std::cout << "Casting spell [" << (index + 1) << "]: " << testSpell.name
                  << " (" << testSpell.getCastAnimation() << ", " << testSpell.castTime << "s)" << std::endl;

        // Start the cast with progress tracking
        castingBar.startCast(testSpell);

        // Play the casting animation if we have an animated node
        if (animatedNode && useAnimatedMesh) {
            std::string animCode = testSpell.getCastAnimation();
            if (animatedNode->playAnimation(animCode, true, false)) {  // loop=true while casting
                std::cout << "  Playing animation: " << animCode << std::endl;
            } else {
                std::cout << "  Animation not found: " << animCode << std::endl;
            }

            // Create casting particle effect using selected effect or default
            if (currentEffectIndex >= 0) {
                const auto* emitter = spellFX.getSpellEffectDB().getEmitterByIndex(currentEffectIndex);
                if (emitter) {
                    spellFX.createCastingEffectFromEmitter(animatedNode, *emitter, testSpell);
                } else {
                    spellFX.createCastingEffect(animatedNode, testSpell);
                }
            } else {
                spellFX.createCastingEffect(animatedNode, testSpell);
            }
        }
    });

    // Initialize casting bar
    castingBar.initialize(driver, device->getGUIEnvironment());
    castingBar.setScreenSize(800, 600);

    // Initialize spell particle effects
    spellFX.initialize(smgr, driver, clientPath);

    // Create race model loader for animated meshes
    raceModelLoader = std::make_unique<RaceModelLoader>(smgr, driver, fileSystem);
    raceModelLoader->setClientPath(clientPath);

    // Create equipment model loader for weapons/shields
    equipmentModelLoader = std::make_unique<EquipmentModelLoader>(smgr, driver, fileSystem);
    equipmentModelLoader->setClientPath(clientPath);

    // Load item ID to model ID mapping
    std::string mappingPath = "data/item_models.json";
    int mappingCount = equipmentModelLoader->loadItemModelMapping(mappingPath);
    if (mappingCount > 0) {
        std::cout << "Loaded " << mappingCount << " item-to-model mappings from " << mappingPath << std::endl;
    } else {
        std::cout << "Warning: Failed to load item mappings from " << mappingPath << std::endl;
    }

    // Load equipment models from gequip archives
    if (equipmentModelLoader->loadEquipmentArchives()) {
        std::cout << "Loaded " << equipmentModelLoader->getLoadedModelCount() << " equipment models" << std::endl;
    } else {
        std::cout << "Warning: Failed to load equipment archives" << std::endl;
    }

    // Set current zone if loaded from entity file (enables zone-specific models)
    if (!currentZoneName.empty()) {
        std::cout << "Setting zone for model loading: " << currentZoneName << std::endl;
        raceModelLoader->setCurrentZone(currentZoneName);
    }

    // ========================================================================
    // Spell Test Mode initialization
    // ========================================================================
    if (spellTestMode) {
        // Load spell database
        std::string spellsPath = clientPath + "spells_us.txt";
        spellDatabase = std::make_unique<EQ::SpellDatabase>();
        if (spellDatabase->loadFromFile(spellsPath)) {
            std::cout << "Loaded " << spellDatabase->getSpellCount() << " spells from " << spellsPath << std::endl;
        } else {
            std::cout << "WARNING: Failed to load spells: " << spellDatabase->getLoadError() << std::endl;
        }

        // Create SpellVisualFX
        spellVisualFX = std::make_unique<EQ::SpellVisualFX>(smgr, driver, clientPath);
        spellVisualFX->setSpellDatabase(spellDatabase.get());

        // Set up entity position callback (handles real + virtual hand entity IDs)
        spellVisualFX->setEntityPositionCallback(
            [](uint16_t entity_id, irr::core::vector3df& out_pos) -> bool {
                // Hand bone virtual entities — track hand positions on caster
                if (entity_id == casterRightHandEntityId && animatedNode && casterRightHandBoneIdx >= 0) {
                    return animatedNode->getBoneWorldPosition(casterRightHandBoneIdx, out_pos);
                }
                if (entity_id == casterLeftHandEntityId && animatedNode && casterLeftHandBoneIdx >= 0) {
                    return animatedNode->getBoneWorldPosition(casterLeftHandBoneIdx, out_pos);
                }
                if (entity_id == casterEntityId && animatedNode) {
                    out_pos = animatedNode->getAbsolutePosition();
                    out_pos.Y += 1.0f;
                    return true;
                }
                if (entity_id == targetEntityId && targetAnimatedNode) {
                    out_pos = targetAnimatedNode->getAbsolutePosition();
                    out_pos.Y += 1.0f;
                    return true;
                }
                return false;
            });

#ifdef EQT_HAS_GLES2
        if (useGLES2Renderer) {
            particleManager = std::make_unique<EQT::Graphics::Environment::ParticleManager>(smgr, driver);
            particleManager->init(clientPath);
            particleManager->initUnifiedRenderer();
            spellVisualFX->setParticleManager(particleManager.get());
            // Set entity position callback on ParticleManager (uses glm::vec3)
            particleManager->setEntityPositionCallback(
                [](uint16_t entity_id, glm::vec3& out_pos) -> bool {
                    // Hand bone virtual entities
                    if (entity_id == casterRightHandEntityId && animatedNode && casterRightHandBoneIdx >= 0) {
                        irr::core::vector3df bonePos;
                        if (animatedNode->getBoneWorldPosition(casterRightHandBoneIdx, bonePos)) {
                            out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
                            return true;
                        }
                    }
                    if (entity_id == casterLeftHandEntityId && animatedNode && casterLeftHandBoneIdx >= 0) {
                        irr::core::vector3df bonePos;
                        if (animatedNode->getBoneWorldPosition(casterLeftHandBoneIdx, bonePos)) {
                            out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
                            return true;
                        }
                    }
                    if (entity_id == casterEntityId && animatedNode) {
                        auto p = animatedNode->getAbsolutePosition();
                        out_pos = glm::vec3(p.X, p.Y + 1.0f, p.Z);
                        return true;
                    }
                    if (entity_id == targetEntityId && targetAnimatedNode) {
                        auto p = targetAnimatedNode->getAbsolutePosition();
                        out_pos = glm::vec3(p.X, p.Y + 1.0f, p.Z);
                        return true;
                    }
                    return false;
                });
            // Set direction callback for spray effects (bone forward vector)
            particleManager->setEntityDirectionCallback(
                [](uint16_t entity_id, glm::vec3& out_dir) -> bool {
                    if (!animatedNode) return false;
                    int boneIdx = -1;
                    if (entity_id == casterRightHandEntityId) boneIdx = casterRightHandBoneIdx;
                    else if (entity_id == casterLeftHandEntityId) boneIdx = casterLeftHandBoneIdx;
                    if (boneIdx < 0) return false;
                    irr::core::vector3df dir;
                    if (animatedNode->getBoneWorldDirection(boneIdx, dir)) {
                        out_dir = glm::vec3(dir.X, dir.Y, dir.Z);
                        return true;
                    }
                    return false;
                });

            std::cout << "GLES2 ParticleManager + UnifiedRenderer initialized" << std::endl;
        }
#endif

        // Create caster entity (Human male at origin) — 1x scale for correct particle sizing
        animatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
            currentRaceId, currentGender, 0, 0, nullptr, -1);
        if (animatedNode) {
            animatedNode->setPosition(core::vector3df(0, 0, 0));
            animatedNode->setRotation(core::vector3df(0, 90, 0));
            animatedNode->playAnimation("p01", true, false);  // Idle
            for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
                animatedNode->getMaterial(i).Lighting = false;
                animatedNode->getMaterial(i).BackfaceCulling = false;
            }
            useAnimatedMesh = true;

            // Cache hand bone indices for spell particle emission
            casterRightHandBoneIdx = animatedNode->findRightHandBoneIndex();
            casterLeftHandBoneIdx = animatedNode->findLeftHandBoneIndex();
            std::cout << "Created caster entity (race " << currentRaceId
                      << ") R_HAND=" << casterRightHandBoneIdx
                      << " L_HAND=" << casterLeftHandBoneIdx << std::endl;
        }

        // Create target entity (Human male offset to right) — ~5 units apart at 1x scale
        targetAnimatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
            1, 0, 0, 0, nullptr, -1);  // Human male
        if (targetAnimatedNode) {
            targetAnimatedNode->setPosition(core::vector3df(5, 0, 0));
            targetAnimatedNode->setRotation(core::vector3df(0, -90, 0));  // Face caster
            targetAnimatedNode->playAnimation("p01", true, false);
            for (u32 i = 0; i < targetAnimatedNode->getMaterialCount(); ++i) {
                targetAnimatedNode->getMaterial(i).Lighting = false;
                targetAnimatedNode->getMaterial(i).BackfaceCulling = false;
            }
            std::cout << "Created target entity at (5, 0, 0)" << std::endl;
        }

        // Build initial spell list
        rebuildSpellTestList();

        // Hide the old spell bar in spell test mode
        spellBar.hide();
    }

    // If we have loaded entities and a specific entity was requested, load it
    bool loadedEntityFromFile = spellTestMode;  // Spell test mode already created entities
    if (!spellTestMode && !loadedEntities.empty()) {
        int entityIdx = -1;
        if (!entityName.empty()) {
            entityIdx = findEntityByName(entityName);
            if (entityIdx >= 0) {
                std::cout << "Found entity '" << entityName << "' at index " << entityIdx << std::endl;
            } else {
                std::cout << "Entity '" << entityName << "' not found, loading first entity" << std::endl;
                entityIdx = 0;
            }
        } else {
            // Load first entity by default
            entityIdx = 0;
        }

        loadEntityModel(entityIdx);
        loadedEntityFromFile = true;
    }

    // If no entity was loaded, use the default behavior
    if (!loadedEntityFromFile) {
        // Start in animated mode if animations are available
        if (character->animatedSkeleton && !character->animatedSkeleton->animations.empty()) {
            useAnimatedMesh = true;
            toggleAnimationMode();  // This will create the animated node
        } else {
            // Fall back to static mesh
            useAnimatedMesh = false;
            scene::IMesh* mesh = buildMesh(*characterGeometry, swapXY);
            if (!mesh) {
                LOG_ERROR(MOD_MAIN, "Failed to build mesh");
                spellFX.clearAllEffects();  // Clear before device destruction
                device->drop();
                return 1;
            }

            modelNode = smgr->addMeshSceneNode(mesh);
            if (modelNode) {
                modelNode->setScale(core::vector3df(10, 10, 10));
                modelNode->setPosition(core::vector3df(0, 0, 0));
                modelNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

                for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
                    modelNode->getMaterial(i).BackfaceCulling = false;
                    modelNode->getMaterial(i).Lighting = false;
                }
            }
            mesh->drop();
        }
    }

    // Add camera
    if (spellTestMode) {
        // Camera looking at midpoint between caster (0,0,0) and target (5,0,0)
        camera = smgr->addCameraSceneNode(
            nullptr, core::vector3df(2.5f, 3.0f, -10.0f), core::vector3df(2.5f, 1.5f, 0));
    } else {
        camera = smgr->addCameraSceneNode(
            nullptr, core::vector3df(0, 5, -cameraDistance), core::vector3df(0, 0, 0));
    }

    // Track time for animation updates
    u32 lastTime = device->getTimer()->getTime();

    // Main loop
    while (device->run()) {
        // Update animation
        u32 currentTime = device->getTimer()->getTime();
        u32 deltaMs = currentTime - lastTime;
        lastTime = currentTime;

        if (animatedNode && useAnimatedMesh && !animationPaused) {
            animatedNode->OnAnimate(currentTime);
            // Update equipment positions to follow hand bones
            updateEquipmentPositions();
        }

        // Update spell test mode casting
        float deltaSeconds = deltaMs / 1000.0f;

        if (spellTestMode && spellTestCasting) {
            spellTestCastElapsed += deltaSeconds;
            if (spellTestCastElapsed >= spellTestCastDuration) {
                completeSpellTestCast();
            }
        }

        // Return to idle after one-shot cast animation completes
        if (spellTestMode && !spellTestCasting && animatedNode) {
            // isPlayingThrough() is true while a non-looping anim is playing;
            // once it finishes (returns false), switch back to idle loop
            if (animatedNode->isPlayingThrough()) {
                spellTestCastAnimPlaying = true;
            } else if (spellTestCastAnimPlaying) {
                animatedNode->playAnimation("p01", true, false);
                spellTestCastAnimPlaying = false;
            }
        }

        // Update SpellVisualFX
        if (spellVisualFX) {
            spellVisualFX->update(deltaSeconds);
        }

        // Animate target node in spell test mode
        if (spellTestMode && targetAnimatedNode) {
            targetAnimatedNode->OnAnimate(currentTime);
        }

#ifdef EQT_HAS_GLES2
        if (particleManager) {
            glm::vec3 camPosGlm(0);
            if (camera) {
                auto cp = camera->getAbsolutePosition();
                camPosGlm = glm::vec3(cp.X, cp.Y, cp.Z);
            }
            particleManager->updateUnified(deltaSeconds, camPosGlm);
        }
#endif

        // Update casting bar and check for cast completion
        if (castingBar.update(deltaSeconds)) {
            // Cast completed!
            const EQT::SpellBarEntry* spell = castingBar.getCurrentSpell();
            if (spell) {
                std::cout << "Cast complete: " << spell->name << std::endl;

                // Stop casting particle effect and create completion effect
                spellFX.stopCastingEffect();
                if (animatedNode && useAnimatedMesh) {
                    spellFX.createCompletionEffect(animatedNode, *spell);

                    // Stop the looping cast animation and return to idle
                    animatedNode->stopAnimation();
                    // Optionally play idle animation
                    animatedNode->playAnimation("p01", true, false);
                }
            }
            castingBar.completeCast();
        }

        // Update spell particle effects
        spellFX.update(deltaSeconds);

        driver->beginScene(true, true, video::SColor(255, 50, 50, 80));
        smgr->drawAll();

#ifdef EQT_HAS_GLES2
        // Render unified particles (point sprites) after 3D scene, before 2D overlay
        if (particleManager) {
            irr::core::matrix4 viewMat = driver->getTransform(video::ETS_VIEW);
            irr::core::matrix4 projMat = driver->getTransform(video::ETS_PROJECTION);
            irr::core::vector3df camPos = camera ? camera->getAbsolutePosition()
                                                 : irr::core::vector3df(0, 0, 0);
            float screenH = static_cast<float>(driver->getScreenSize().Height);
            particleManager->renderUnified(viewMat, projMat, camPos,
                                           999999.0f, 999999.0f, nullptr, screenH);
        }
#endif

        // Draw info text
        gui::IGUIFont* font = device->getGUIEnvironment()->getBuiltInFont();
        if (font) {
            // Mode and animation info
            std::wstring modeInfo = L"[S] Mode: ";
            modeInfo += useAnimatedMesh ? L"ANIMATED" : L"STATIC";
            if (useAnimatedMesh) {
                modeInfo += L"  [A/Z] Anim: ";
                if (currentAnimIndex < 0) {
                    modeInfo += L"NONE (pose)";
                } else if (currentAnimIndex < static_cast<int>(availableAnimations.size())) {
                    std::string animName = availableAnimations[currentAnimIndex];
                    modeInfo += std::wstring(animName.begin(), animName.end());
                    modeInfo += L" (" + std::to_wstring(currentAnimIndex + 1) + L"/" +
                                std::to_wstring(availableAnimations.size()) + L")";
                }
                modeInfo += animationPaused ? L" [PAUSED]" : L"";
                // Add speed info
                modeInfo += L"  [/] Speed: ";
                // Format speed with 1 decimal place
                int speedInt = static_cast<int>(globalAnimationSpeed * 10);
                modeInfo += std::to_wstring(speedInt / 10) + L"." + std::to_wstring(speedInt % 10) + L"x";
            }
            font->draw(modeInfo.c_str(), core::rect<s32>(10, 10, 700, 30), video::SColor(255, 255, 255, 255));

            // Variant info line
            {
                std::wstring variantInfo = L"[V] Body: " + std::to_wstring(currentBodyVariant + 1) + L"/" + std::to_wstring(bodyVariants.size());
                variantInfo += L"  [H] Head: " + std::to_wstring(currentHeadVariant + 1) + L"/" + std::to_wstring(headVariants.size());
                font->draw(variantInfo.c_str(), core::rect<s32>(10, 30, 600, 50), video::SColor(255, 255, 255, 255));
            }

            // Equipment texture info line
            {
                std::wstring texInfo = L"[B] Equip: ";
                texInfo += useEquipmentTextures ? L"ON" : L"OFF";
                if (useEquipmentTextures) {
                    texInfo += L"  [N] Tex: " + std::to_wstring(currentBodyTexture);
                }
                font->draw(texInfo.c_str(), core::rect<s32>(10, 50, 700, 70), video::SColor(255, 200, 255, 200));
            }

            // Wireframe and rotation info
            std::wstring wireInfo = L"[W] Wire: ";
            wireInfo += wireframe ? L"ON" : L"OFF";

            core::vector3df rot;
            if (animatedNode) {
                rot = animatedNode->getRotation();
            } else if (modelNode) {
                rot = modelNode->getRotation();
            }
            wireInfo += L"  Rot: X=" + std::to_wstring((int)rot.X)
                      + L" Y=" + std::to_wstring((int)rot.Y)
                      + L" Z=" + std::to_wstring((int)rot.Z);
            font->draw(wireInfo.c_str(), core::rect<s32>(10, 70, 500, 90),
                       video::SColor(255, 255, 255, 255));

            // Model info
            std::wstring modelInfo = L"Model: ";
            modelInfo += std::wstring(currentRaceCode.begin(), currentRaceCode.end());
            modelInfo += L" (race " + std::to_wstring(currentRaceId) + L")";
            font->draw(modelInfo.c_str(), core::rect<s32>(10, 90, 400, 110),
                       video::SColor(255, 200, 200, 200));

            // Spell bar controls info (only when not in spell test mode)
            if (!spellTestMode) {
                font->draw(L"[1-0] Cast spell  [Click gems on left]", core::rect<s32>(10, 110, 400, 130),
                           video::SColor(255, 180, 180, 255));
            }

            // Spell test mode HUD
            if (spellTestMode) {
                int hudY = 130;
                const int lineHeight = 14;
                video::SColor headerColor(255, 255, 200, 100);
                video::SColor valueColor(255, 255, 255, 255);
                video::SColor dimColor(255, 150, 150, 150);
                video::SColor castingColor(255, 100, 255, 100);

                // Header
                font->draw(L"=== SPELL TEST MODE ===", core::rect<s32>(10, hudY, 400, hudY + lineHeight), headerColor);
                hudY += lineHeight;

                // Class
                std::string classStr = "[F3] Class: " + std::string(getClassName(spellTestClassIndex));
                std::wstring wClassStr(classStr.begin(), classStr.end());
                font->draw(wClassStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Category
                std::string catStr = "[F4] Category: " + std::string(spellTestCategoryNames[spellTestCategoryIndex]);
                std::wstring wCatStr(catStr.begin(), catStr.end());
                font->draw(wCatStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Current spell
                if (!spellTestSpellList.empty() && spellTestSpellIndex < static_cast<int>(spellTestSpellList.size())) {
                    const auto* sp = spellTestSpellList[spellTestSpellIndex];
                    std::string spellStr = "[F5] Spell: " + sp->name + " (" +
                        std::to_string(spellTestSpellIndex + 1) + "/" +
                        std::to_string(spellTestSpellList.size()) + ")";
                    std::wstring wSpellStr(spellStr.begin(), spellStr.end());
                    font->draw(wSpellStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), valueColor);
                    hudY += lineHeight;

                    // Spell details
                    std::string detailStr = "  ID:" + std::to_string(sp->id) +
                        " Resist:" + getResistTypeName(sp->resist_type) +
                        " Cast:" + std::to_string(sp->cast_time_ms / 1000) + "." +
                        std::to_string((sp->cast_time_ms / 100) % 10) + "s" +
                        " Mana:" + std::to_string(sp->mana_cost);
                    std::wstring wDetailStr(detailStr.begin(), detailStr.end());
                    font->draw(wDetailStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), dimColor);
                    hudY += lineHeight;

                    // Target type
                    std::string targetStr = "  Target:" + std::string(
                        sp->isSelfOnly() ? "Self" :
                        sp->isPBAE() ? "PBAE" :
                        sp->isAESpell() ? "AE" :
                        sp->isGroupSpell() ? "Group" : "Single");
                    if (sp->is_beneficial) targetStr += " (Beneficial)";
                    std::wstring wTargetStr(targetStr.begin(), targetStr.end());
                    font->draw(wTargetStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                } else {
                    font->draw(L"[F5] Spell: (none available)", core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Mode
                std::string modeStr = "[F6] Mode: " + std::string(spellTestSelfMode ? "SELF" : "TARGETED");
                std::wstring wModeStr(modeStr.begin(), modeStr.end());
                font->draw(wModeStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Cast effect style (auto-detected from spell school)
                {
                    std::string styleLabel = "SMOLDER";
                    std::string schoolLabel = "?";
                    if (!spellTestSpellList.empty() && spellTestSpellIndex < static_cast<int>(spellTestSpellList.size()) && spellVisualFX) {
                        const auto* curSpell = spellTestSpellList[spellTestSpellIndex];
                        bool spray = spellVisualFX->isSprayCastStyle(curSpell->id);
                        styleLabel = spray ? "SPRAY" : "SMOLDER";
                        switch (curSpell->getSchool()) {
                            case EQ::SpellSchool::Abjuration:  schoolLabel = "Abjuration"; break;
                            case EQ::SpellSchool::Alteration:  schoolLabel = "Alteration"; break;
                            case EQ::SpellSchool::Conjuration: schoolLabel = "Conjuration"; break;
                            case EQ::SpellSchool::Divination:  schoolLabel = "Divination"; break;
                            case EQ::SpellSchool::Evocation:   schoolLabel = "Evocation"; break;
                        }
                    }
                    std::string effectStr = "Cast: " + styleLabel + " (" + schoolLabel + ")";
                    std::wstring wEffectStr(effectStr.begin(), effectStr.end());
                    font->draw(wEffectStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Cast state
                if (spellTestCasting && spellTestCurrentSpell) {
                    float progress = spellTestCastElapsed / spellTestCastDuration;
                    int pct = static_cast<int>(progress * 100);
                    std::string castStr = "CASTING: " + spellTestCurrentSpell->name + " " +
                        std::to_string(pct) + "%";
                    std::wstring wCastStr(castStr.begin(), castStr.end());
                    font->draw(wCastStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), castingColor);
                    hudY += lineHeight;
                }

                // Active effects count
                if (spellVisualFX) {
                    std::string fxStr = "Active effects: " +
                        std::to_string(spellVisualFX->getActiveEffectCount());
                    std::wstring wFxStr(fxStr.begin(), fxStr.end());
                    font->draw(wFxStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Controls reminder
                hudY += lineHeight;
                font->draw(L"[Enter] Cast  [Backspace] Cancel/Clear", core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
            }
        }

        // Render spell bar UI
        spellBar.render();

        // Render casting bar UI
        castingBar.render();

        driver->endScene();
    }

    // Clean up spell effects before device destruction to avoid segfault
    spellFX.clearAllEffects();
    if (spellVisualFX) {
        spellVisualFX->clearAllEffects();
        spellVisualFX.reset();
    }
#ifdef EQT_HAS_GLES2
    particleManager.reset();
#endif
    spellDatabase.reset();

    device->drop();
    return 0;
}
