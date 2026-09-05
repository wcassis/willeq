// tools/model_viewer/model_viewer_state.h -- shared state and declarations
// for the model_viewer tool.
//
// T385 (2026-09-05, board B167): tools/model_viewer.cpp was 3,246 lines, over
// even the 3,000-line hard cap, because scripts/refactor/check_file_sizes.sh
// never scanned tools/. It is now decomposed by function into
// tools/model_viewer/*.cpp; this header is the seam between those TUs.
//
// The tool is a single-purpose debugging viewer built around file-scope
// globals -- the split is deliberately mechanical (declare here, define in
// model_viewer_state.cpp, move bodies verbatim) so that no behaviour changes.
// The two `using namespace` lines are carried over from the original TU for
// the same reason: rewriting every irr:: / EQT::Graphics:: name would have
// been an edit, not a move. This header is private to tools/model_viewer/ and
// is never included by src/ or include/.

#ifndef WILLEQ_TOOLS_MODEL_VIEWER_STATE_H
#define WILLEQ_TOOLS_MODEL_VIEWER_STATE_H

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
#include "tool_race_db.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/bone_attachment.h"
#include "client/graphics/entity_renderer.h"  // For EntityAppearance
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
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

// ============================================================================
// Types (definitions -- these are the tool's own, not shared with the client)
// ============================================================================

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

// Cast effect type: smolder (orbital glow) vs spray (directional cone)
enum class CastEffectType : uint8_t { Smolder, Spray };

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

// The keyboard/mouse receiver. Defined in model_viewer_input.cpp.
class MyEventReceiver : public IEventReceiver {
public:
    bool OnEvent(const SEvent& event) override;
};

// ============================================================================
// Global state -- defined in model_viewer_state.cpp
// ============================================================================

// View state
extern bool swapXY;
extern bool wireframe;
extern bool flipU;             // Flip texture U coordinate (X axis)
extern bool filterOutliers;    // Filter out vertices with extreme values
extern float outlierThreshold; // Max coordinate value to consider valid
extern float cameraDistance;   // Camera distance from model

// Irrlicht handles
extern scene::IMeshSceneNode* modelNode;
extern EQAnimatedMeshSceneNode* animatedNode;
extern scene::ICameraSceneNode* camera;
extern video::IVideoDriver* driver;
extern scene::ISceneManager* smgr;
extern io::IFileSystem* fileSystem;
extern IrrlichtDevice* device;
extern MyEventReceiver receiver;

// Animation state
extern bool useAnimatedMesh;
extern std::vector<std::string> availableAnimations;
extern int currentAnimIndex;   // -1 = no animation (pose)
extern bool animationPaused;
extern float globalAnimationSpeed;

// Character data
extern std::shared_ptr<ZoneGeometry> characterGeometry;
extern std::map<std::string, std::shared_ptr<TextureInfo>> characterTextures;
extern std::vector<std::shared_ptr<ZoneGeometry>> bodyVariants;
extern std::vector<std::shared_ptr<ZoneGeometry>> headVariants;
extern size_t currentBodyVariant;
extern size_t currentHeadVariant;

// Race / equipment loaders
extern std::unique_ptr<RaceModelLoader> raceModelLoader;
extern std::string currentRaceCode;
extern uint16_t currentRaceId;
extern uint8_t currentGender;
extern std::unique_ptr<EquipmentModelLoader> equipmentModelLoader;
extern std::unique_ptr<ConstrainedTextureCache> equipmentTextureCache;
extern scene::IMeshSceneNode* primaryEquipNode;   // Right hand weapon
extern scene::IMeshSceneNode* secondaryEquipNode; // Left hand/shield

// Entity data loaded from JSON
extern std::vector<EntityData> loadedEntities;
extern int currentEntityIndex;
extern std::string currentZoneName;  // Zone name from entity JSON (e.g. "qeynos2")
extern std::string clientPath;       // EQ client path

// Current texture values for cycling
extern uint8_t currentBodyTexture;
extern uint8_t currentHelmTexture;
extern bool useEquipmentTextures;
extern const std::vector<uint8_t> validTextureValues;

// Spell bar for casting visualization
extern EQT::SpellBar spellBar;
extern EQT::CastingBar castingBar;
extern EQT::ModelViewerFX spellFX;

// Effect cycling for testing EDD emitter definitions
extern int currentEffectIndex;  // -1 = default (spell category based), 0+ = specific emitter

// Spell test mode state
extern bool spellTestMode;
extern bool useGLRenderer;
extern bool useGLES2Renderer;
extern std::unique_ptr<EQ::SpellDatabase> spellDatabase;
extern std::unique_ptr<EQ::SpellVisualFX> spellVisualFX;
#ifdef EQT_HAS_GLES2
extern std::unique_ptr<EQT::Graphics::Environment::ParticleManager> particleManager;
#endif

extern int spellTestClassIndex;     // 1-16, maps to PlayerClass enum
extern int spellTestCategoryIndex;
extern int spellTestSpellIndex;
extern bool spellTestSelfMode;      // true = self-cast, false = targeted
extern std::vector<const EQ::SpellData*> spellTestSpellList;

extern EQAnimatedMeshSceneNode* targetAnimatedNode;
extern uint16_t casterEntityId;
extern uint16_t targetEntityId;

extern bool spellTestCasting;
extern float spellTestCastElapsed;
extern float spellTestCastDuration;
extern const EQ::SpellData* spellTestCurrentSpell;
extern bool spellTestCastAnimPlaying;

// Virtual entity IDs for hand bone particle tracking
extern uint16_t casterRightHandEntityId;
extern uint16_t casterLeftHandEntityId;
extern int casterRightHandBoneIdx;
extern int casterLeftHandBoneIdx;

extern CastEffectType castEffectType;
extern const char* spellTestCategoryNames[];

// ============================================================================
// Functions, grouped by the TU that defines them
// ============================================================================

// model_viewer_spell_test.cpp
const char* getClassName(int classIndex);
const char* getResistTypeName(EQ::ResistType rt);
bool matchesCategory(const EQ::SpellData* spell, SpellTestCategory cat);
void rebuildSpellTestList();
void cycleEffect(int direction);
void triggerSpellTestCast();
void completeSpellTestCast();
void cancelSpellTestCast();
bool initSpellTestMode();

// model_viewer_entity.cpp
std::string getTextureName(uint8_t texVal);
std::string extractJsonString(const std::string& json, const std::string& key);
int extractJsonInt(const std::string& json, const std::string& key);
std::vector<uint32_t> extractJsonIntArray(const std::string& json, const std::string& key);
std::vector<EntityData> parseEntityFile(const std::string& filename);
int findEntityByName(const std::string& name);
void loadEntityModel(int entityIndex);
void cycleEntity(int direction);
std::string getEquipmentVariantTextureName(const std::string& baseTexName, uint8_t textureVariant);
void reloadEntityWithTextures();

// model_viewer_mesh.cpp
void cycleAnimation(int direction);
void toggleAnimationMode();
void combineCurrentVariants();
video::ITexture* loadTexture(const std::string& name, const std::vector<char>& data);
bool isOutlierVertex(const Vertex3D& v, float threshold);
scene::IMesh* buildMesh(const ZoneGeometry& geometry, bool doSwapXY);
void rebuildMesh();
void rebuildCurrentVariant();
std::shared_ptr<ZoneGeometry> combineCharacterParts(
    const std::vector<std::shared_ptr<ZoneGeometry>>& parts);
bool loadVariantsFromS3D(const std::string& s3dPath, const std::string& raceCode);

// model_viewer_equipment.cpp
void updateEquipmentPositions();
void attachEquipmentModels(uint32_t primaryItemId, uint32_t secondaryItemId);

// model_viewer_startup.cpp
struct ViewerArgs {
    std::string raceCode = "HUM";
    std::string entityFile;
    std::string entityName;
    int debugLevel = 0;
};
void parseCommandLine(int argc, char* argv[], ViewerArgs& args);
void printUsageBanner(const ViewerArgs& args);
bool loadDirectRaceMode(const ViewerArgs& args, std::shared_ptr<CharacterModel>& outCharacter);
bool createViewerDevice();
void initViewerSubsystems();

// model_viewer_loop.cpp
void runViewerLoop();

#endif  // WILLEQ_TOOLS_MODEL_VIEWER_STATE_H
