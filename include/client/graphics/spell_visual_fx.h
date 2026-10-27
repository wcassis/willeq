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
#include <set>

#include "client/spell/spells_eff_reader.h"
#include "client/spell/spells_new_eff_reader.h"
#include "client/spell/spells_new_edd_colour.h"
#include "client/spell/spell_fx_timing.h"
#include "client/spell/spell_visual_vocabulary.h"
#include "client/graphics/spell_visual_selection.h"
// B1163 (#482): the missile speed word + calibrated timing and the oracle's
// spell-859 removal gate, as pure device-free rules (one source of truth).
#include "client/graphics/spell_b1163_missile_rules.h"

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
    // B1152 (#472): one in-flight missile (eqgame FUN_00476b12,
    // refactor/B1152/TRACE.md sec. 2-6). Spawns on the spell's first live tick
    // (createCastGlow) when its spellsnew.eff record has a Middle stage;
    // launches at the caster's cast-animation end; flies caster -> target
    // over flightTime; fires the record's Target stage on arrival.
    struct Missile {
        uint32_t spellId = 0;
        uint16_t casterId = 0;
        uint16_t targetId = 0;
        int gateLevel = kMeasuredCasterLevel;
        int castMs = 0;          // the cast duration (OP_Action = the hit time)
        double launchAt = 0.0;   // FX-time (s) the Middle emitter starts (flight)
        double reachAt = 0.0;    // FX-time (s) the missile reaches the target
        double landAt = 0.0;     // FX-time (s) the Target stage fires (the landing)
        float flightTime = 0.0f; // seconds, the B1152 calibration 9.05 / 30 (B1163)
        bool launched = false;   // Middle emitter created
        bool reached = false;    // the missile reached the target (removed)
        bool landed = false;     // the Target stage fired
    };

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

    // B1144 (#464): the point fable follows for a spellsnew.eff slot's attach
    // point (slot +12) on `entity`: LHAND / RHAND -> the hand bones; HEAD and
    // the face points LEYE / REYE / MOUTH -> the head bone (fable's head bone
    // sits at the face, B1132; it has no eye or mouth point); every other
    // point -> the entity itself: DEFAULT, whose height on a model is model
    // data (the measured rows place what follows it), CHEST / LFOOT / RFOOT
    // (the client falls back to DEFAULT on a model without the point, and
    // fable resolves none of them), none / origin / floor-under-chest.
    // The caller falls back to the entity when the bone does not resolve.
    static uint16_t attachPointEntityId(uint16_t entity, int32_t attachPoint);

    // B1144: the caster level every oracle capture behind the measured
    // Source/Target rows was taken at (`#level 60` in the B1118/B1127/B1130
    // plans): at it every slot of a record is drawn (minLevel is 0, 24 or 39).
    // A direct caller that passes no level draws what those captures drew;
    // the bridge always passes the payload's gate level.
    static constexpr int kMeasuredCasterLevel = 60;

    // B1144: the slots of `stage` (kSpellsNewEff{Source,Target}) a cast of
    // this spell draws at `casterLevel`. `fromFile` is false when the spell
    // has no spellsnew.eff record (field 145 = 0, an empty record, or the
    // files absent): the official client then plays its spells.eff record
    // (eqgame 0x49b260) and fable keeps the measured row's own anchor.
    // `measured` = the stage's emitter slots at any level, the set the
    // `#level 60` captures drew; each drawn slot carries 1/measured of the
    // row's population.
    struct StageSlots {
        bool fromFile = false;
        std::vector<SpellsNewEffDrawnSlot> drawn;
        size_t measured = 0;
    };
    StageSlots stageSlots(uint32_t spell_id, int stage, int casterLevel) const;
    // B1145 (#465): one slot's timing from its spellsnew.edd emitter, given
    // its stage's caller duration (EQ::SpellFxCallerDurationMs); false when
    // the emitter has no entry.
    bool slotTiming(int32_t emitter, int32_t callerMs, SpellFxSlotTiming& out) const;

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
    //   B1152 (#472) DELETED createProjectile: the missile actor (the Missile
    //   struct + update() -> updateMissiles) replaces it. createCastGlow
    //   spawns a missile for a record with a Middle stage; launchMissile
    //   flies the Middle emitter (PROJECTILE_PATH) and fireMissileLanding
    //   fires the record's Target stage on arrival. A record with no Middle
    //   stage (54/859) lands in createLanding at removal, as before.
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
    // B1144: `casterLevel` is the slot gate's level (SpellsNewEffGateLevel);
    // a spellsnew.eff record's Source draws one emitter per slot it passes.
    // B1148 (#468): `castAnim` = spell field 120; the Source's caller
    // duration is the cast time + the caster model's track for it
    // (castAnimationMs), the measured 4.35 s when that does not resolve.
    // B1152 (#472): `targetId` is the missile's destination (0 = none, the
    // non-missile path / a caster with no tracked target). A spell whose
    // spellsnew.eff record has a Middle stage spawns a missile here that flies
    // caster -> targetId and fires the record's Target stage on arrival.
    uint32_t createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms,
                            int casterLevel = kMeasuredCasterLevel, int castAnim = 0,
                            uint16_t targetId = 0);

    // B1148: the caster model's track length for an animation code ("t04"),
    // in ms; < 0 = no such entity / model / track. Set by the renderer (the
    // entity's skeleton) and by tools/model_viewer's FX harness.
    using CastAnimationLengthCallback = std::function<int32_t(uint16_t entity_id, const char* animCode)>;
    void setCastAnimationLengthCallback(CastAnimationLengthCallback cb) { m_cast_anim_length_cb = std::move(cb); }
    // The CastingAnim term for this caster in ms (castAnim's track length),
    // < 0 when it does not resolve.
    int32_t castAnimationMs(uint16_t caster_id, int castAnim) const;

    // B1163 (#482): the LOCAL PLAYER's spawn id (0 = none). When a spell-859
    // missile is removed within 40.0 XY units of the player, the oracle fires
    // 859's Target stage AT THE PLAYER (eqgame 00477962 .. 0047f5ed,
    // spell_b1163_missile_rules.h). Set by the renderer; the FX harness leaves
    // it unset (no local player).
    using LocalPlayerIdCallback = std::function<uint16_t()>;
    void setLocalPlayerIdCallback(LocalPlayerIdCallback cb) { m_local_player_cb = std::move(cb); }

    // B1164 (#483): the attach scale s of an entity's spellsnew.eff slots,
    // EQ::SpellFxAttachScale(race, server size) (spell_fx_attach_scale.h:
    // the client's actor scale, 0.2 x size). Set by the renderer (the
    // entity's race and Spawn_Struct size) and by the FX harness; unset or
    // an unknown entity = kSpellFxAttachScale (1.0).
    using EntityAttachScaleCallback = std::function<float(uint16_t entity_id)>;
    void setEntityAttachScaleCallback(EntityAttachScaleCallback cb) { m_attach_scale_cb = std::move(cb); }
    float entityAttachScale(uint16_t entity_id) const;

    // B1152 (#472): the missile actor. createProjectile() (the legacy
    // spell-visual projectile, tests-only since B1138) is REPLACED by this:
    // a spell whose spellsnew.eff record has a Middle stage spawns a missile
    // on its first live tick (createCastGlow) that flies caster -> target
    // (eqgame FUN_00476b12, refactor/B1152/TRACE.md sec. 2-5) and fires the
    // record's Target stage on arrival (the landing). The actor lives in
    // m_missiles and advances in update(); query it from the harness/tests:
    //   missileCount()          — missiles currently pending or in flight
    //   missileFor(spell, caster) — the one for a spell+caster (nullptr if none)
    // A spell with NO Middle stage (Frost Bolt / record 82) has no missile:
    // its Target stage fires at removal as it always did (54/859 rule, sec. 5).
    size_t missileCount() const { return m_missiles.size(); }
    const Missile* missileFor(uint32_t spell_id, uint16_t caster_id) const;

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
    // B1144 (#464): for a spell with a spellsnew.eff record the landing is
    // that record's Target slots the caster's level passes, one emitter pair
    // per slot on the slot's anchor actor (caster or target) at its attach
    // point; NO slot drawn = no landing at all (record 326, a /disc). The row
    // (B1132) still gives the shape class, sizes and timing; the impact half
    // draws the slot's own texture, the spellComplete half the textures of the
    // Source slots the same level draws (T441).
    uint16_t createLanding(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                           int casterLevel = kMeasuredCasterLevel);

    // ========================================================================
    // Effect Removal
    // ========================================================================

    // B1145 (#465): OP_DeleteSpawn of a spawn the FX may be attached to. The
    // official client (eqgame 0x4581b7 -> 0x4849b2 -> 0x48f4a4) deletes the
    // spawn's CActor, and the actor's destructor (EQGraphicsDX9 0x10040150 and
    // kin) calls the particle manager's remove-by-actor (vtbl +0x30 =
    // 0x1005e760 -> 0x1005e550): every emitter attached to that actor or one
    // of its bones is deleted WITH its live particles. For a CORPSE (spawn
    // +0x244 >= 2) whose race is not 216/472 (0x47bfd0) the actor is not
    // deleted then: 0x48f4a4 queues it on the corpse-fade list (0x401351,
    // started at the packet, 0x401100), which fades the model from 1.3 s
    // (0x401127) and deletes the actor when more than 3499 ms have passed
    // (0x4012bb: 0xdab < now - start) -- so a dying target's landing is cut
    // 3.5 s after its OP_DeleteSpawn. `corpse` = the spawn was a corpse.
    static constexpr float kCorpseActorDeleteSeconds = 3.5f;
    void onSpawnDeleted(uint16_t spawn_id, bool corpse, uint16_t race_id);

    // Remove casting glow (when cast ends)
    void removeCastGlow(uint16_t caster_id);
    // B1131: OP_Action's end of the Source: it keeps emitting its spells.eff
    // record's hold (holdOther when the target is another entity, holdSelf
    // when it is the caster or unknown), then its sprites finish their life.
    // B1145: a spellsnew.eff Source is not released here: each slot emits for
    // the duration eqgame gave it at OP_BeginCast (cast + the cast-animation
    // term, or its fixed +0x78) whatever OP_Action does.
    void releaseCastGlow(uint16_t caster_id, uint32_t spell_id, uint16_t target_id);

    // B1131/B1137: the spellsnew.eff record of this spell (spells_us.txt
    // field 145 via the vocabulary): the record whose spellsnew.edd emitters
    // draw it. B1153 (#473): the measured rows that number used to select
    // are retired -- every row-lookup consumer returns the default row.
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
    // createXNode() call (createGlowNode/createImpactNode/...) reads
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
    // the T72 stage-suppression gates in a headless test -- the gates
    // themselves live inside the ParticleManager branch of createCastGlow /
    // createImpact (B1152 removed the travel gate with createProjectile:
    // a record's Middle stage now spawns a missile, not a suppressed trail).
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
    void createImpactNode(SpellFXInstance& effect);

#ifdef EQT_HAS_GLES2
    // B1144: createLanding's spellsnew.eff path (one emitter pair per Target
    // slot; B1149: the pair is the slot's one emitter, the file's population).
    uint16_t createLandingFromSlots(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                                    int record, const StageSlots& slots);
#endif

    // Update individual effect types
    void updateGlow(SpellFXInstance& effect, float delta_time);

#ifdef EQT_HAS_GLES2
    // B1152 (#472): the missile actor (eqgame FUN_00476b12, refactor/B1152/
    // TRACE.md sec. 2-6). A spell whose spellsnew.eff record has a Middle stage
    // spawns one of these in createCastGlow(); it launches at the caster's
    // cast-animation end, flies caster -> target over flightTime, and fires
    // the record's Target stage on arrival (the landing). update() advances
    // the whole vector; launchMissile() creates the Middle emitter
    // (PROJECTILE_PATH) and fireMissileLanding() runs the Target-stage slots.
    void updateMissiles(float delta_time);
    void launchMissile(Missile& m);
    void fireMissileLanding(Missile& m);
    void fireSpell859RemovalAtPlayer(const Missile& m);  // B1163
    // A missile for this spell+caster still pending or in flight (its Target
    // stage has not fired yet). createLanding() defers to it on OP_Action.
    bool missileInFlight(uint32_t spell_id, uint16_t caster_id) const;
#endif

    irr::scene::ISceneManager* m_smgr;
    irr::video::IVideoDriver* m_driver;
    EntityPositionCallback m_entity_pos_callback;
    CastAnimationLengthCallback m_cast_anim_length_cb;  // B1148

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
    // B1144: the colour words of these emitters, one per texture region.
    void applySlotEddColours(EQT::Graphics::Environment::SpellEffectDef& def,
                             const std::vector<int32_t>& emitters) const;
    // Installs stage `stage`'s (0 Source, 1 Middle, 2 Target) colour words on
    // `def` when that stage came from spellsnew.eff.
    void applyStageEddColours(EQT::Graphics::Environment::SpellEffectDef& def,
                              const SpellVisualResolution& visual, int stage) const;
#endif

#ifdef EQT_HAS_GLES2
    EQT::Graphics::Environment::ParticleManager* m_particle_manager = nullptr;
#endif

    // B1145: corpse actors awaiting the client's 3.5 s delete (spawn, seconds left).
    std::vector<std::pair<uint16_t, float>> m_pending_actor_deletes;
    void removeAttachedEffects(uint16_t spawn_id);

    // Worker-driven state
    std::vector<EQT::Graphics::SpellVFXCommandData> m_pending_commands;
    bool m_worker_driven = false;

    // B1152 (#472): the missile actor's state. m_missiles holds pending and
    // in-flight missiles; m_fx_time is the FX time base (seconds) advanced by
    // update() that launchAt/reachAt/landAt are measured against. The timing
    // lives in client/graphics/spell_b1163_missile_rules.h (B1163, #482): the
    // flight is the record-80 oracle's calibrated 9.05-unit gap at the
    // calibrated 30 u/s (eqgame's [missile+0x28] is a thrust word in a damped
    // velocity, not a speed; refactor/B1163/README.md (A)); the flight ENDS
    // 0.184 s before the OP_Action and the Target stage fires 0.30 s after the
    // flight end (refactor/B1152/TRACE.md sec. 5/8). BLESS-ONLY.
    std::vector<Missile> m_missiles;
    double m_fx_time = 0.0;
    LocalPlayerIdCallback m_local_player_cb;  // B1163: spell 859's removal gate
    EntityAttachScaleCallback m_attach_scale_cb;  // B1164: s per entity
    // (spell, caster) whose Target stage a missile already fired: createLanding
    // skips it on OP_Action so the landing is drawn exactly once, at whichever
    // of arrival / OP_Action comes second is suppressed in favour of the first.
    std::set<std::pair<uint32_t, uint16_t>> m_missileLanded;

    // Default effect durations
    static constexpr float DEFAULT_IMPACT_DURATION = 1.5f;  // Impact particles last 1.5 seconds
};

} // namespace EQ

#endif // EQT_GRAPHICS_SPELL_VISUAL_FX_H
