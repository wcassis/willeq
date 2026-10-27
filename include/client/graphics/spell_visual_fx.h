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
#include "client/spell/spell_missile_object.h"
#include "client/spell/spell_fx_options.h"
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
    //   B1152 (#472) DELETED createProjectile; B1170 (#489) DELETED the
    //   record-driven missile that replaced it (createCastGlow's Missile,
    //   launchMissile, fireMissileLanding, the 0.302 s calibration): the
    //   official client draws a Middle stage only on a missile OBJECT, which
    //   only a server 0x4033 message creates (refactor/B1170/README.md).
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
    // B1165 (#484): `casterFx` is what the On-For filter reads
    // (spell_fx_options.h); the default (no spawn) passes every filter value's
    // null/null arm. The Source (stage 0) is gated on (caster, caster) as
    // eqgame's BeginCast passes it (eqgame.exe.c:122270).
    uint32_t createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms,
                            int casterLevel = kMeasuredCasterLevel, int castAnim = 0,
                            const SpellFxActor& casterFx = SpellFxActor{});

    // B1148: the caster model's track length for an animation code ("t04"),
    // in ms; < 0 = no such entity / model / track. Set by the renderer (the
    // entity's skeleton) and by tools/model_viewer's FX harness.
    using CastAnimationLengthCallback = std::function<int32_t(uint16_t entity_id, const char* animCode)>;
    void setCastAnimationLengthCallback(CastAnimationLengthCallback cb) { m_cast_anim_length_cb = std::move(cb); }
    // The CastingAnim term for this caster in ms (castAnim's track length),
    // < 0 when it does not resolve.
    int32_t castAnimationMs(uint16_t caster_id, int castAnim) const;

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
    // B1165: gated on (casterFx, targetFx) as stage 2 (eqgame.exe.c:101960).
    uint16_t createLanding(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                           int casterLevel = kMeasuredCasterLevel,
                           const SpellFxActor& casterFx = SpellFxActor{},
                           const SpellFxActor& targetFx = SpellFxActor{});

    // B1165 (#484): the Options > Particles > Spell page + ShowSpellEffects,
    // as the client holds them (spell_fx_options.h). Set every frame by the
    // renderer's sim bridge from the Options record and once by the FX
    // harness (tools/model_viewer --particle-options). Density is applied
    // when a stage's emitters are CREATED (slotTiming ->
    // SpellFxSlotTimingAtDensity), as CreateEmitter does.
    void setSpellFxOptions(const SpellFxOptions& o) { m_fx_options = o; }
    const SpellFxOptions& spellFxOptions() const { return m_fx_options; }
    // FUN_0049b260's gate for this spell's stage (0 Source, 1 Middle, 2 Target).
    bool stageDraws(uint32_t spell_id, int stage, const SpellFxActor& caster,
                    const SpellFxActor& target) const;

    // ========================================================================
    // B1170 (#489): the server's 0x4033 missile OBJECT
    // ========================================================================
    // The official client draws a record's Middle stage ONLY on a missile
    // object, and the only object is the one a received 0x4033 builds
    // (eqgame 00460737; client/spell/spell_missile_object.h). This is that
    // object: spawnMissileObject() takes the decoded message, update() runs
    // the client's per-frame spawn and its >= 33-unit physics passes
    // (00445556 / 00445580), the Middle stage rides the object (anchor 2,
    // 00495d8e) and the removal fires the 54 / 859 Target stages (0047789d).
    // `gateLevel` is the source spawn's slot-gate level (SpellsNewEffGateLevel).
    // B1165 (#484): `casterFx` / `targetFx` are the source and target as the
    // On-For filter reads them; every stage the object fires goes through
    // stageDraws as eqgame's FUN_0049b260 entry gates it.
    void spawnMissileObject(const SpellMissile::Wire& wire, int gateLevel = kMeasuredCasterLevel,
                            const SpellFxActor& casterFx = SpellFxActor{},
                            const SpellFxActor& targetFx = SpellFxActor{});
    size_t missileObjectCount() const { return m_missile_objects.size(); }
    // The i-th object's position (EQ y, x, z); false when there is none.
    bool missileObjectPosition(size_t i, float eqPos[3]) const {
        if (i >= m_missile_objects.size()) return false;
        for (int k = 0; k < 3; ++k) eqPos[k] = m_missile_objects[i].o.w.pos[k];
        return true;
    }

    // An entity as the missile object reads it, in EQ coordinates (y, x, z)
    // plus its model height. Set by the renderer (lastX/lastY/lastZ) and the
    // FX harness; unset, the entity position callback is used (Irrlicht
    // x, y, z -> EQ x, z, y; height 0).
    struct MissileEntityState {
        float eqPos[3] = {0.0f, 0.0f, 0.0f};  // y, x, z
        float height = 0.0f;
    };
    using MissileEntityCallback = std::function<bool(uint16_t entity_id, MissileEntityState& out)>;
    void setMissileEntityCallback(MissileEntityCallback cb) { m_missile_entity_cb = std::move(cb); }

    // The LOCAL PLAYER's spawn id (0 = none): spell 859's removal fires its
    // Target stage at the player within 40.0 XY units (00477962 .. 0047f5ed,
    // spell_b1163_missile_rules.h). Set by the renderer.
    using LocalPlayerIdCallback = std::function<uint16_t()>;
    void setLocalPlayerIdCallback(LocalPlayerIdCallback cb) { m_local_player_cb = std::move(cb); }

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
    // createImpact (B1152 removed the travel gate with createProjectile; a
    // record's Middle stage is drawn only on a 0x4033 missile object, B1170).
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
    // B1170 (#489): with `args`, the slots are placed as eqgame's
    // FUN_00495cc3 places them for an explicit FUN_0049b260 call: anchor 0
    // on args->world, 1 on args->caster, 2 on args->missile, 3 on
    // args->target, a 0 argument skipping the slot (00495d4c .. 00495dc3),
    // and every slot run for args->callerMs.
    struct StageArgs {
        uint16_t caster = 0;
        uint16_t target = 0;
        uint16_t world = 0;
        uint16_t missile = 0;
        int32_t callerMs = 0;
    };
    uint16_t createLandingFromSlots(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                                    int record, const StageSlots& slots,
                                    const StageArgs* args = nullptr);
#endif

    // B1170 (#489): the missile objects (spell_visual_fx_missile.cpp).
    struct MissileFx {
        SpellMissile::Object o;
        int gateLevel = kMeasuredCasterLevel;
        uint16_t anchor = 0;  // the ParticleManager anchor the object's emitters follow
        // The target's height as read at the spawn: the client's actor
        // height (+0x238) is a per-actor value, not the animated pose's box.
        float targetHeight = 0.0f;
        SpellFxActor casterFx;  // B1165: the On-For inputs for the object's fires
        SpellFxActor targetFx;
    };
    std::vector<MissileFx> m_missile_objects;
    double m_missile_clock_ms = 0.0;  // the object clock (time() read as ms)
    MissileEntityCallback m_missile_entity_cb;
    LocalPlayerIdCallback m_local_player_cb;
    // spells_us.txt fields 18 / 98 / 122 per spell id, read on first use.
    std::unordered_map<uint32_t, SpellMissile::SpellFields> m_missile_spell_fields;
    // Anchors kept after their object (spell 859's emitters stay where it
    // was removed, 00476ad4) or for a fixed-position fire, with the clock
    // time they are released at.
    std::vector<std::pair<uint16_t, double>> m_missile_anchor_expiry;
    uint16_t m_next_missile_anchor = 0;
    void updateMissileObjects(float delta_time);
    bool missileEntity(uint32_t id, SpellMissile::Entity& out) const;
    const SpellMissile::SpellFields* missileSpellFields(uint32_t spell_id);
#ifdef EQT_HAS_GLES2
    uint16_t allocMissileAnchor();
    void placeMissileAnchor(uint16_t anchor, const float eqPos[3]);
    void fireMissileStage(MissileFx& m, const SpellMissile::SpellFields& spell);
    void fireMissileRemoval(MissileFx& m);
#endif

    // Update individual effect types
    void updateGlow(SpellFXInstance& effect, float delta_time);


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

    SpellFxOptions m_fx_options;  // B1165
    // Default effect durations
    static constexpr float DEFAULT_IMPACT_DURATION = 1.5f;  // Impact particles last 1.5 seconds
};

} // namespace EQ

#endif // EQT_GRAPHICS_SPELL_VISUAL_FX_H
