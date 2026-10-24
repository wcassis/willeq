/*
 * WillEQ Spell Visual Effects System
 *
 * Manages visual effects for spell casting, impacts, and persistent auras.
 * Uses Irrlicht particle systems and billboards for spell visualization.
 */

#ifndef EQT_GRAPHICS_SPELL_VISUAL_FX_H
#define EQT_GRAPHICS_SPELL_VISUAL_FX_H

#include <irrlicht.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>

#include <unordered_map>

#include "client/spell/spells_eff_reader.h"
#include "client/spell/spells_new_eff_reader.h"
#include "client/spell/spells_new_edd_colour.h"
#include "client/spell/spell_visual_vocabulary.h"
#include "client/graphics/spell_visual_selection.h"

namespace EQT { namespace Graphics { class ConstrainedTextureCache; }}

#ifdef EQT_HAS_GLES2
namespace EQT { namespace Graphics { namespace Environment { class ParticleManager; struct SpellEffectDef; }}}
#endif

// Forward declarations for worker-driven spell VFX
namespace EQT { namespace Graphics {
    struct SpellVFXCommandData;
    struct SimulationOutput;
}}

namespace EQ {


// Visual effect types
enum class SpellFXType : uint8_t {
    None = 0,
    CastGlow,           // Glow around caster during casting
    ProjectileTravel,   // Bolt traveling to target
    ImpactBurst,        // Explosion/flash on impact
    AuraPersistent,     // Ongoing aura around entity
    RainEffect,         // Rain spell particles (AE)
    BeamConnect,        // Beam connecting caster to target
    GroundCircle,       // AE indicator on ground
};

// Individual visual effect instance
struct SpellFXInstance {
    SpellFXType type = SpellFXType::None;
    uint32_t spell_id = 0;
    uint16_t source_entity = 0;
    uint16_t target_entity = 0;
    irr::core::vector3df source_pos;
    irr::core::vector3df target_pos;
    irr::video::SColor color{255, 255, 255, 255};
    float lifetime = 0;         // Total duration in seconds (0 = permanent until removed)
    float elapsed = 0;          // Time elapsed in seconds
    float scale = 1.0f;
    bool active = true;

    uint32_t effect_id = 0;  // Unique ID for worker<->main mapping

    // Scene nodes (may be nullptr if not applicable)
    irr::scene::ISceneNode* scene_node = nullptr;
    irr::scene::IBillboardSceneNode* billboard = nullptr;
    irr::scene::IParticleSystemSceneNode* particle_system = nullptr;
};

// Callback for entity position lookup
using EntityPositionCallback = std::function<bool(uint16_t entity_id,
    irr::core::vector3df& out_pos)>;

class SpellVisualFX {
public:
    // Hand bone pseudo-entity ID encoding.
    // EQ spawn IDs are uint16_t but never exceed ~4000, so high bits are safe.
    // Bit 15 = hand bone flag, Bit 14 = left hand (0 = right, 1 = left).
    static uint16_t rightHandEntityId(uint16_t caster_id) { return caster_id | 0x8000; }
    static uint16_t leftHandEntityId(uint16_t caster_id)  { return caster_id | 0xC000; }
    static bool isHandBoneId(uint16_t id) { return (id & 0x8000) != 0; }
    static uint16_t realEntityId(uint16_t id) { return id & 0x3FFF; }
    static bool isRightHand(uint16_t id) { return (id & 0xC000) == 0x8000; }
    // B1131: head bone pseudo-entity (bit 14 alone) -- record 22's halo disc.
    static uint16_t headEntityId(uint16_t caster_id) { return (caster_id & 0x3FFF) | 0x4000; }
    static bool isHeadBoneId(uint16_t id) { return (id & 0xC000) == 0x4000; }

    SpellVisualFX(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                   const std::string& eqClientPath = "");

    // Set constrained texture cache (mandatory, must be called before loadParticleTextures)
    void setConstrainedTextureCache(EQT::Graphics::ConstrainedTextureCache* cache) { m_constrained_cache = cache; }
    ~SpellVisualFX();

    // T381 (B160): record the per-spell render vocabulary (resist type,
    // school, spell_affect_index) carried by the SpellCastVisual* bridge
    // events. This REPLACES the old setSpellDatabase()/m_spell_db pointer,
    // which nothing in the client ever called -- so every colour, the spray
    // cast style and the whole spells.eff resolution were unreachable and
    // every spell drew one identical lavender-blue glow.
    //
    // Call it before the create*() call for that spell id; the bridge does
    // exactly that on SpellCastVisualStarted / SpellCastVisualComplete. An
    // id never noted keeps the pre-T381 fallback behaviour, which is what a
    // spell the game thread could not find in its database must do.
    //
    // Bound: one entry per distinct spell id seen this session, worst case
    // the 8,066 spells in willeq.db (~16 bytes each). Renderer thread only.
    void noteSpellVocabulary(uint32_t spell_id, const SpellVisualVocabulary& vocab);

    // Test/diagnostic read-back of the above (never null-returning: an
    // unknown id yields a default-constructed, `known == false` vocabulary).
    SpellVisualVocabulary spellVocabulary(uint32_t spell_id) const;

    // Set callback for getting entity positions
    void setEntityPositionCallback(EntityPositionCallback callback) {
        m_entity_pos_callback = callback;
    }

    // Update all active effects (call each frame)
    // delta_time: seconds since last update
    void update(float delta_time);

    // ========================================================================
    // Effect Creation
    //
    // WHO CALLS WHAT (board B161; census re-taken by B1138 over src/,
    // include/, tests/, tools/, benchmarks/ and docker/ -- the B166 rule: a
    // zero-caller claim names the directories it grepped, tools/ among them):
    //
    //   Reached from the GAME through src/client/bridge/spell_cast_fx.cpp
    //   (the one FX body IrrlichtBridge AND tools/model_viewer --spell-test
    //   run, B1138): createCastGlow / releaseCastGlow / removeCastGlow and
    //   createLanding, which draws createSpellComplete + createImpact's legacy
    //   halves on the scene-node path; createImpact is also the projectile's
    //   arrival (update(), applyWorkerResults()).
    //
    //   createProjectile: reached only from tests/test_spell_visual_vocabulary.cpp
    //   until B1128 wires it (B165: 85 spells carry TravelType == 3).
    //
    //   B1138 DELETED createImpactAtPosition, createBuffAura, createRainEffect
    //   and createGroundCircle with their presets and config styles: only the
    //   model_viewer's own pre-B1131 cast heuristics reached them, and the
    //   client never does. Do not rebuild them for the viewer; the viewer runs
    //   the client's sequence.
    //
    //   B1139 DELETED removeBuffAura and removeAllForEntity (B195: zero
    //   callers in any directory, ZeroCallers) with their worker commands;
    //   hasBuffAura went with createBuffAura (B1138).
    // ========================================================================

    // Determine cast effect style from spell school (spray vs smolder/orbital)
    bool isSprayCastStyle(uint32_t spell_id) const;

    // Create casting glow around caster (auto-selects spray vs smolder by school)
    // Returns effect ID for later removal
    uint32_t createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms);

    // Create projectile traveling from caster to target
    void createProjectile(uint16_t caster_id, uint16_t target_id, uint32_t spell_id);

    // Create impact effect at target location
    void createImpact(uint16_t target_id, uint32_t spell_id);

    // Create spell completion effect (burst of particles when cast succeeds)
    void createSpellComplete(uint16_t caster_id, uint32_t spell_id);

    // B1132 (#452): the whole landing of a cast (OP_Action, cast-animation
    // packet): both halves (spellComplete + impact) on the record's Target
    // anchor -- the TARGET's body, head bone or feet, or a column above it;
    // the CASTER only for a caster-anchored record (27's PB-AE, the self-only
    // records) or when there is no target. Never on the caster of an
    // other-target cast otherwise (B1131 FOLLOW-UP 1). Returns the entity the
    // landing follows (the head pseudo-entity for a head row), 0 when none.
    uint16_t createLanding(uint16_t caster_id, uint16_t target_id, uint32_t spell_id);

    // ========================================================================
    // Effect Removal
    // ========================================================================

    // Remove casting glow (when cast ends)
    void removeCastGlow(uint16_t caster_id);
    // B1131: OP_Action's end of the Source: it keeps emitting its spells.eff
    // record's hold (holdOther when the target is another entity, holdSelf
    // when it is the caster or unknown), then its sprites finish their life.
    void releaseCastGlow(uint16_t caster_id, uint32_t spell_id, uint16_t target_id);

    // B1131/B1137: the measured Source/Target row of this spell: the row of
    // its spellsnew.eff record (spells_us.txt field 145 via the vocabulary,
    // SpellEffectsConfig::rowForAnim), -1 = the default row.
    int sourceRecordOf(uint32_t spell_id) const;

    // Clear all effects
    void clearAllEffects();

    // ========================================================================
    // Queries
    // ========================================================================

    bool hasCastGlow(uint16_t caster_id) const;
    size_t getActiveEffectCount() const { return m_effects.size(); }

    // ========================================================================
    // Particle Settings
    // ========================================================================

    // Get/set particle density multiplier (0.0 = off, 1.0 = normal, 2.0 = double)
    float getParticleMultiplier() const { return m_particle_multiplier; }
    void setParticleMultiplier(float mult) { m_particle_multiplier = std::max(0.0f, std::min(mult, 3.0f)); }

    // Adjust particle multiplier by delta (for hotkey control)
    void adjustParticleMultiplier(float delta);

    // Get spell color by spell ID (for external particle creation)
    irr::video::SColor getSpellColorForSpell(uint32_t spell_id) const { return getSpellColor(spell_id); }

    // Worker-driven facade (desktop GL path, offloads update math to SimulationWorker)
    std::vector<EQT::Graphics::SpellVFXCommandData> drainCommands();
    void applyWorkerResults(const EQT::Graphics::SimulationOutput& results);
    bool isWorkerDriven() const { return m_worker_driven; }
    void setWorkerDriven(bool driven) { m_worker_driven = driven; }

#ifdef EQT_HAS_GLES2
    // Set particle manager for GLES2 delegation
    void setParticleManager(EQT::Graphics::Environment::ParticleManager* pm) { m_particle_manager = pm; }
#endif

    // T124: called by the renderer's SpellVisualFX eviction guard
    // (renderer_zone_merge.h's ForwardingTextureGuard) whenever
    // ConstrainedTextureCache evicts a texture. Swaps any of the 8 cached
    // particle-texture members below that currently equal `from` to `to`.
    //
    // Gap this closes: loadParticleTextures() resolves these 8 members
    // exactly ONCE, in the constructor (SpellVisualFX is a renderer-
    // lifetime singleton, lazily constructed once in createEntityRenderer()
    // -- see renderer_entities.cpp -- and never rebuilt per zone). Every
    // createXNode() call (createGlowNode/createProjectileNode/...) reads
    // one of these members via getTextureForSpell() and calls
    // setMaterialTexture(0, tex) on a BRAND NEW particle/billboard node.
    // The nodes themselves, once created, ARE ordinary scene-graph-attached
    // nodes covered by ConstrainedTextureCache::clearTextureReferences()
    // while they exist (cleanupEffectNode() always remove()s them outright
    // on cleanup, never detaches-and-reattaches the way doors/objects do)
    // -- so this is NOT the same "detached node" shape T123/T124's other
    // guards fix. It's a distinct hazard: these 8 members are a persistent
    // cache that seeds every FUTURE node's material, so if one of them goes
    // dangling after an eviction, the next spell cast bakes a
    // use-after-free pointer straight into a brand new node's material at
    // creation time. `to` is expected to be the cache's placeholder texture
    // (resolved once, safely, by the guard's own constructor) -- matches
    // every other T123/T124 guard's convention instead of nulling and
    // losing the particle's texture entirely.
    //
    // T124 finding, IMPORTANT: loadParticleTextures() (the ONLY code that
    // ever populates the 8 members below) is called from the constructor
    // only under `#ifndef EQT_HAS_GLES2` -- and EQT_HAS_GLES2 is
    // unconditionally defined in every build since P32 (docs/decisions.md),
    // the only supported renderer. So in every shipped build, all 8
    // members are permanently nullptr for the object's entire lifetime and
    // this guard is currently a no-op. Kept anyway (cheap, provably
    // correct, exactly the shape needed if that dead branch is ever
    // revived) rather than removed -- see
    // refactor/T124_remaining_texture_holders.md Review for the full
    // investigation, including a SEPARATE, PRE-EXISTING bug this uncovered:
    // setParticleManager() (renderer_entities.cpp) is called during
    // createEntityRenderer(), which itself runs BEFORE particleManager_ is
    // constructed (both inside initLoadingScreen()), so m_particle_manager
    // is ALSO always null -- meaning the GLES2 particle-delegation path
    // this class was built to prefer never engages either. Not fixed here
    // (a functional/feature bug, not a UAF); flagged in refactor/deferred.md.
    void swapEvictedTexture(irr::video::ITexture* from, irr::video::ITexture* to);

    // T124: exposed (was private) so the eviction-guard regression test can
    // observe swapEvictedTexture()'s effect through the same lookup
    // createXNode() call sites actually use, rather than needing a friend
    // declaration or a test-only accessor. Behavior unchanged.
    irr::video::ITexture* getTextureForSpell(uint32_t spell_id) const;

    // spell_id -> noted spell_affect_index -> spells.eff record ->
    // resolved per-stage visual. Also logs, at DEBUG, which of the two
    // fallback populations (no index vs. index resolves to empty padding)
    // a spell landed in, or that it resolved to a real record with N/3
    // stages populated -- see the T72 Review for why this distinction
    // exists in logs.
    //
    // T381: public (was private). It is a pure query over noted vocabulary
    // plus the loaded spells.eff, and it is the only observation point for
    // the three T72 stage-suppression gates in a headless test -- the gates
    // themselves live inside the ParticleManager branch of createCastGlow /
    // createProjectile / createImpact.
    SpellVisualResolution resolveVisual(uint32_t spell_id) const;

private:
    // Get spell color based on the noted resist type/school for this id.
    // T381: the `const SpellData*` overload is gone with m_spell_db -- the
    // renderer no longer names SpellData at all.
    irr::video::SColor getSpellColor(uint32_t spell_id) const;
    static irr::video::SColor colorForVocabulary(const SpellVisualVocabulary& vocab);

    // Remove and clean up an effect
    void removeEffect(size_t index);
    void cleanupEffectNode(SpellFXInstance& effect);

    // Create scene nodes for different effect types
    void createGlowNode(SpellFXInstance& effect);
    void createProjectileNode(SpellFXInstance& effect);
    void createImpactNode(SpellFXInstance& effect);

    // Update individual effect types
    void updateProjectile(SpellFXInstance& effect, float delta_time,
                          std::vector<std::pair<uint16_t, uint32_t>>& pendingImpacts);
    void updateGlow(SpellFXInstance& effect, float delta_time);

    irr::scene::ISceneManager* m_smgr;
    irr::video::IVideoDriver* m_driver;
    EntityPositionCallback m_entity_pos_callback;

    // T381 (B160): spell id -> render vocabulary, fed by the bridge from the
    // SpellCastVisual* event payloads. Replaces the never-set SpellDatabase*.
    std::unordered_map<uint32_t, SpellVisualVocabulary> m_spell_vocab;

    std::vector<SpellFXInstance> m_effects;
    uint32_t m_next_effect_id = 1;

    // Particle density multiplier (0.0 = off, 1.0 = normal, up to 3.0)
    float m_particle_multiplier = 1.0f;

    // Particle textures by resist type
    irr::video::ITexture* m_particle_texture = nullptr;  // Default particle
    irr::video::ITexture* m_fire_texture = nullptr;      // Fire spells
    irr::video::ITexture* m_frost_texture = nullptr;     // Cold spells
    irr::video::ITexture* m_magic_texture = nullptr;     // Magic spells
    irr::video::ITexture* m_poison_texture = nullptr;    // Poison spells
    irr::video::ITexture* m_disease_texture = nullptr;   // Disease spells
    irr::video::ITexture* m_chromatic_texture = nullptr; // Chromatic/Prismatic spells
    irr::video::ITexture* m_corruption_texture = nullptr;// Corruption/darkness spells
    std::string m_eq_client_path;
    EQT::Graphics::ConstrainedTextureCache* m_constrained_cache = nullptr;

    void loadParticleTextures();
    void loadFallbackTexture();
    irr::video::ITexture* loadDDSTexture(const std::string& path, const std::string& name);

    // ========================================================================
    // T72: spells.eff-driven visual selection
    // ========================================================================

    // Loads spells.eff from <eqClientPath>/spells.eff. Failure is non-fatal
    // (m_spells_eff_loaded stays false, resolveVisual() then always returns
    // the NoAffectIndex fallback) -- spells.eff being absent must degrade
    // exactly like a spell having no spell_affect_index, not crash the
    // effects system.
    void loadSpellsEff();


    SpellsEffReader m_spells_eff;
    bool m_spells_eff_loaded = false;
    // B1127: spellsnew.eff + spellsnew.edd from the same directory.
    SpellsNewEffReader m_spells_new_eff;
    bool m_spells_new_eff_loaded = false;
    // B1146 (#466): the colour words of spellsnew.edd's emitters.
    SpellsNewEddColourTable m_spells_new_edd_colour;
#ifdef EQT_HAS_GLES2
    // Installs stage `stage`'s (0 Source, 1 Middle, 2 Target) colour words on
    // `def` when that stage came from spellsnew.eff.
    void applyStageEddColours(EQT::Graphics::Environment::SpellEffectDef& def,
                              const SpellVisualResolution& visual, int stage) const;
#endif

#ifdef EQT_HAS_GLES2
    EQT::Graphics::Environment::ParticleManager* m_particle_manager = nullptr;
#endif

    // Worker-driven state
    std::vector<EQT::Graphics::SpellVFXCommandData> m_pending_commands;
    bool m_worker_driven = false;

    // Projectile speed in units per second
    static constexpr float PROJECTILE_SPEED = 500.0f;

    // Default effect durations
    static constexpr float DEFAULT_IMPACT_DURATION = 1.5f;  // Impact particles last 1.5 seconds
    static constexpr float DEFAULT_PROJECTILE_DURATION = 2.0f;
};

} // namespace EQ

#endif // EQT_GRAPHICS_SPELL_VISUAL_FX_H
