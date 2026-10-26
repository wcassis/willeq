// tools/model_viewer/model_viewer_state.cpp -- definitions of the
// model_viewer tool's file-scope state. Moved verbatim from
// tools/model_viewer.cpp by T385 (board B167); see model_viewer_state.h.

#include "model_viewer_state.h"

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
// T62: EquipmentModelLoader::buildMeshFromGeometry's texture-upload path is
// gated entirely on constrainedTextureCache_ being non-null (no fallback,
// unlike RaceModelLoader's mesh path, which has its own MeshBuilder-based
// texture route). Without this, every equipped weapon/shield this tool
// renders is untextured -- found while investigating T62's held-item
// alignment report (see refactor/T62_held_item_alignment.md's Review).
std::unique_ptr<ConstrainedTextureCache> equipmentTextureCache;
scene::IMeshSceneNode* primaryEquipNode = nullptr;   // Right hand weapon
scene::IMeshSceneNode* secondaryEquipNode = nullptr; // Left hand/shield

// Entity data loaded from JSON
// struct EntityData moved to model_viewer_state.h
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
std::string spellTestCastTrack;  // B1157

// Hand bone indices (the FX anchors are SpellVisualFX's own hand/head
// pseudo-entities since B1138, resolved by spell_fx_bone_anchor.h)
int casterRightHandBoneIdx = -1;
int casterLeftHandBoneIdx = -1;

// Cast effect type: smolder (orbital glow) vs spray (directional cone)
// enum class CastEffectType moved to model_viewer_state.h
CastEffectType castEffectType = CastEffectType::Smolder;

// Spell categories for filtering
// enum class SpellTestCategory moved to model_viewer_state.h

const char* spellTestCategoryNames[] = {
    "Direct Damage", "Bolt", "DoT", "AE Damage", "PBAE", "AE Buff",
    "Heal", "Buff", "Debuff", "Utility"
};

// The event receiver instance; its OnEvent body is in model_viewer_input.cpp.
// (validTextureValues keeps its original home in model_viewer_entity.cpp.)
MyEventReceiver receiver;

