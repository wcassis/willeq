/*
 * B1170 (#489): SpellVisualFX's half of the server's 0x4033 missile OBJECT.
 *
 * The object itself (decode, spawn, homing, physics, removal) is the
 * device-free client/spell/spell_missile_object.h; this TU runs it on the
 * FX clock, once per frame as eqgame's FUN_00445243 loop does (00445547 ..
 * 004455a4), and draws what the client draws on it: the record's Middle stage
 * on the object (stage 1, anchor 2 rides the object, anchor 1 the caster,
 * 00476ecc call 0x49b260 -> 00495d8e), the Target stage for the no-flight
 * target types (stage 2 with the object, 00476d52), and the removal's stage-2
 * fires (FUN_0047789d: spell 54 at the object one velocity step back,
 * spell 859 at the local player behind the 40-unit gate). Addresses are
 * instruction starts in review/B1141/ghidra/out/eqgame.dis;
 * refactor/B1170/README.md is the trace.
 */

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/spell_b1163_missile_rules.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "common/logging.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#endif
#include <algorithm>
#include <fstream>
#include <string>

namespace EQ {

namespace {
// The ParticleManager anchor ids the objects' emitters follow: a block at the
// top of the 14-bit spawn-id space (spawn ids stay far below it, see the
// hand-bone encoding in spell_visual_fx.h).
constexpr uint16_t kMissileAnchorBase = 0x3F00;
constexpr uint16_t kMissileAnchorCount = 0x100;
// How long an anchor outlives its object when its emitters stay behind
// (spell 859, 00476ad4) or for a fixed-position fire (spell 54): the longest
// Target-stage slot run in the shipped file is under this.
constexpr double kAnchorLingerMs = 15000.0;
}  // namespace

void SpellVisualFX::spawnMissileObject(const SpellMissile::Wire& wire, int gateLevel,
                                       const SpellFxActor& casterFx, const SpellFxActor& targetFx)
{
    MissileFx m;
    m.o.w = wire;
    m.gateLevel = gateLevel;
    m.casterFx = casterFx;
    m.targetFx = targetFx;
    m_missile_objects.push_back(m);
    LOG_INFO(MOD_SPELL, "B1170 missile object: source={} target={} spell={} type={} word={:.3f} "
             "heading={:.2f} aim={:.2f} pos=({:.2f},{:.2f},{:.2f}) vel=({:.3f},{:.3f},{:.3f}) +0x60={} +0x61={} +0x62={} "
             "+0x64={} +0x54={} +0x58={} model='{}'",
             wire.sourceId, wire.targetId, wire.spellId, static_cast<int>(wire.type), wire.word,
             wire.heading, wire.aim, wire.pos[0], wire.pos[1], wire.pos[2], wire.vel[0], wire.vel[1],
             wire.vel[2], static_cast<int>(wire.light), static_cast<int>(wire.byte61), static_cast<int>(wire.damped),
             static_cast<int>(wire.type2), wire.word54, wire.word58, wire.model);
}

bool SpellVisualFX::missileEntity(uint32_t id, SpellMissile::Entity& out) const
{
    out = SpellMissile::Entity{};
    if (id == 0 || id > 0xFFFF) return false;
    const auto eid = static_cast<uint16_t>(id);
    if (m_missile_entity_cb) {
        MissileEntityState st;
        if (!m_missile_entity_cb(eid, st)) return false;
        out.exists = true;
        for (int i = 0; i < 3; ++i) out.pos[i] = st.eqPos[i];
        out.height = st.height;
        return true;
    }
    if (!m_entity_pos_callback) return false;
    irr::core::vector3df p;
    if (!m_entity_pos_callback(eid, p)) return false;
    out.exists = true;
    out.pos[0] = p.Z;  // EQ y
    out.pos[1] = p.X;  // EQ x
    out.pos[2] = p.Y;  // EQ z
    return true;
}

const SpellMissile::SpellFields* SpellVisualFX::missileSpellFields(uint32_t spell_id)
{
    auto it = m_missile_spell_fields.find(spell_id);
    if (it != m_missile_spell_fields.end()) return it->second.exists ? &it->second : nullptr;
    // The client's spell record (FUN_0040cbd6): spells_us.txt, the line whose
    // field 0 is the id. Read once per id the server names.
    SpellMissile::SpellFields found;
    std::ifstream in(m_eq_client_path + "/spells_us.txt");
    const std::string prefix = std::to_string(spell_id) + "^";
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        uint32_t id = 0;
        SpellMissile::SpellFields f;
        if (SpellMissile::ParseSpellsUsLine(line, id, f) && id == spell_id) found = f;
        break;
    }
    auto& slot = m_missile_spell_fields[spell_id];
    slot = found;
    return slot.exists ? &slot : nullptr;
}

void SpellVisualFX::createMissileModel(MissileFx& m)
{
    // B1181 (#500), eqgame FUN_00476b12's tail (00476d8c .. 00476dd5):
    // uVar8 = FUN_0043972a(missile + 0x65, pos y, pos x, pos z, heading
    // (+0x2c), pitch (+0x30), mode, 1, 0) creates the object's actor from the
    // +0x65 name, and FUN_0043972a (0043972a .. 00439a44) looks the actor
    // definition up by the name (short names take "_ACTORDEF") -- for a
    // missile that is the item IDFile "IT" + the model number. The scope here
    // is type != 9 (the card's rule): a type-9 object keeps B1170's stages
    // byte-identical and never draws its model.
    if (m.o.w.type == SpellMissile::kTypeSpell) return;
    const int modelId = SpellMissile::ParseMissileModelId(m.o.w.model);
    if (modelId < 0) {
        LOG_INFO(MOD_SPELL, "B1181 missile model: type={} model='{}' is not an IT name -- nothing drawn",
                 static_cast<int>(m.o.w.type), m.o.w.model);
        return;
    }
    if (!m_equipment_model_loader) {
        LOG_INFO(MOD_SPELL, "B1181 missile model: IT{} named but no equipment model loader -- nothing drawn",
                 modelId);
        return;
    }
    irr::scene::IMesh* mesh = m_equipment_model_loader->getEquipmentMeshByModelId(modelId);
    if (!mesh || !m_smgr) {
        LOG_INFO(MOD_SPELL, "B1181 missile model: IT{} not in the equipment archives -- nothing drawn", modelId);
        return;
    }
    m.modelId = modelId;
    m.modelNode = m_smgr->addMeshSceneNode(mesh, nullptr);
    if (!m.modelNode) {
        m.modelId = -1;
        LOG_INFO(MOD_SPELL, "B1181 missile model: IT{} scene node failed -- nothing drawn", modelId);
        return;
    }
    m_equipment_model_loader->addMeshRef(modelId);
    placeMissileModel(m);
    LOG_INFO(MOD_SPELL, "B1181 missile model: IT{} drawn at ({:.2f},{:.2f},{:.2f}) heading={:.2f} aim={:.2f}",
             modelId, m.o.w.pos[0], m.o.w.pos[1], m.o.w.pos[2], m.o.w.heading, m.o.w.aim);
}

void SpellVisualFX::placeMissileModel(MissileFx& m)
{
    if (!m.modelNode) return;
    // EQ (y, x, z) -> renderer (x, z, y): the convention the entity
    // callbacks and the anchor placement use.
    m.modelNode->setPosition(irr::core::vector3df(m.o.w.pos[1], m.o.w.pos[2], m.o.w.pos[0]));
    // The actor's bearing / aim fields (00476e40 .. 00476e47) stay synced to
    // the object's +0x2c / +0x3c every pass, and the graphics rotation
    // follows them each frame (EQGraphicsDX9 FUN_10003ec0; the 512-unit
    // angles, the tables' period 512 = 360 degrees). The node's scale is 1:
    // FUN_0043972a's adjustment factor is 1.0 for every name the server can
    // send (the 0.8 / 1.3 / 1.5 rows are IT66 / IT69 / IT70 / IT73 / IT74
    // and BBBOARD -- character equipment, not missile names), so the scale
    // is the WLD actor definition's, already baked into the mesh the loader
    // builds.
    const SpellMissile::MissileModelAngles a =
        SpellMissile::MissileModelAnglesFromFlight(m.o.w.heading, m.o.w.aim);
    m.modelNode->setRotation(irr::core::vector3df(a.pitchDeg, a.yawDeg, a.rollDeg));
}

void SpellVisualFX::destroyMissileModel(MissileFx& m)
{
    // The removal's actor destroy (FUN_0047789d: the actor's delete,
    // -> FUN_00439a46 -> DAT_0099e368 + 0x48) drops the model with the
    // object: lifetime and hit alike (RemovedAfterPass).
    if (!m.modelNode) return;
    m.modelNode->remove();
    m.modelNode = nullptr;
    if (m.modelId >= 0) {
        if (m_equipment_model_loader) m_equipment_model_loader->removeMeshRef(m.modelId);
        m.modelId = -1;
    }
}

void SpellVisualFX::updateMissileObjects(float delta_time)
{
    // The anchors that outlive their object.
    m_missile_clock_ms += static_cast<double>(delta_time) * 1000.0;
#ifdef EQT_HAS_GLES2
    for (size_t i = 0; i < m_missile_anchor_expiry.size();) {
        if (m_missile_clock_ms >= m_missile_anchor_expiry[i].second) {
            if (m_particle_manager) m_particle_manager->clearFxAnchor(m_missile_anchor_expiry[i].first);
            m_missile_anchor_expiry.erase(m_missile_anchor_expiry.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
#endif
    if (m_missile_objects.empty()) return;
    // time() as ms, offset by 1 so the first object's [actor+0x50] is never
    // the "unset" 0 the physics tests for (004c0002).
    const uint32_t now = 1u + static_cast<uint32_t>(m_missile_clock_ms);
    for (size_t i = 0; i < m_missile_objects.size();) {
        auto& m = m_missile_objects[i];
        bool remove = false;
        if (!m.o.spawned) {
            // 00445550 .. 00445575: no actor yet -> FUN_00476b12; a failed
            // spawn sets [actor+0x1005] and the object is removed.
            SpellMissile::Entity source, target;
            missileEntity(m.o.w.sourceId, source);
            missileEntity(m.o.w.targetId, target);
            const SpellMissile::SpellFields* spell =
                m.o.w.type == SpellMissile::kTypeSpell ? missileSpellFields(m.o.w.spellId) : nullptr;
            m.targetHeight = target.height;
            // The caster's physics ticks every frame here: its tick age is 0.
            if (SpellMissile::Spawn(m.o, now, spell, source, target, 0)) {
                LOG_INFO(MOD_SPELL, "B1170 missile spawn: source={} target={} spell={} stage={} life={} ms "
                         "pos=({:.2f},{:.2f},{:.2f}) target_height={:.9g}",
                         m.o.w.sourceId, m.o.w.targetId, m.o.w.spellId, m.o.stage, m.o.lifeEnd - now,
                         m.o.w.pos[0], m.o.w.pos[1], m.o.w.pos[2], m.targetHeight);
#ifdef EQT_HAS_GLES2
                if (spell && m.o.stage != 0) fireMissileStage(m, *spell);
#endif
                createMissileModel(m);
            } else {
                LOG_INFO(MOD_SPELL, "B1170 missile spawn failed: source={} spell={} (no caster or no spell "
                         "record) -- removed, nothing drawn", m.o.w.sourceId, m.o.w.spellId);
                remove = true;
            }
        } else if (SpellMissile::PassDue(m.o, now)) {
            SpellMissile::Entity target;
            missileEntity(m.o.w.targetId, target);
            target.height = m.targetHeight;
            remove = SpellMissile::Pass(m.o, now, target);
            LOG_INFO(MOD_SPELL, "B1170 missile pass: spell={} t={} pos=({:.6f},{:.6f},{:.6f}) "
                     "vel=({:.6f},{:.6f},{:.6f}) heading={:.6f} aim={:.6f} aimTarget={:.6f} stored={:.6f} hit={}",
                     m.o.w.spellId, now, m.o.w.pos[0], m.o.w.pos[1], m.o.w.pos[2], m.o.w.vel[0], m.o.w.vel[1],
                     m.o.w.vel[2], m.o.w.heading, m.o.w.aim, m.o.aimTarget, m.o.stored, m.o.hit ? 1 : 0);
            if (remove) {
                LOG_INFO(MOD_SPELL, "B1170 missile removed: spell={} at t+{} ms ({}) pos=({:.2f},{:.2f},{:.2f})",
                         m.o.w.spellId, now, m.o.hit ? "hit" : "lifetime", m.o.w.pos[0], m.o.w.pos[1],
                         m.o.w.pos[2]);
#ifdef EQT_HAS_GLES2
                fireMissileRemoval(m);
#endif
            }
        }
#ifdef EQT_HAS_GLES2
        if (!remove && m.anchor != 0) placeMissileAnchor(m.anchor, m.o.w.pos);
        if (remove && m.anchor != 0 && m_particle_manager) {
            // 00476ad4: the removal frees the emitters linked to the object;
            // spell 859's are unlinked and left where they are.
            if (m.o.w.spellId == SpellMissileRules::kSpell859) {
                m_missile_anchor_expiry.push_back({m.anchor, m_missile_clock_ms + kAnchorLingerMs});
            } else {
                m_particle_manager->removeSpellEffectsForEntity(m.anchor);
                m_particle_manager->clearFxAnchor(m.anchor);
            }
        }
#endif
        if (!remove) placeMissileModel(m);
        if (remove) destroyMissileModel(m);
        if (remove) {
            m_missile_objects.erase(m_missile_objects.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
}

#ifdef EQT_HAS_GLES2
uint16_t SpellVisualFX::allocMissileAnchor()
{
    for (uint16_t k = 0; k < kMissileAnchorCount; ++k) {
        const uint16_t id = static_cast<uint16_t>(kMissileAnchorBase + (m_next_missile_anchor + k) % kMissileAnchorCount);
        bool used = false;
        for (const auto& m : m_missile_objects) used = used || m.anchor == id;
        for (const auto& a : m_missile_anchor_expiry) used = used || a.first == id;
        if (!used) {
            m_next_missile_anchor = static_cast<uint16_t>((id - kMissileAnchorBase + 1) % kMissileAnchorCount);
            return id;
        }
    }
    return 0;
}

void SpellVisualFX::placeMissileAnchor(uint16_t anchor, const float eqPos[3])
{
    // EQ (y, x, z) -> renderer (x, z, y): the convention the entity callbacks use.
    if (m_particle_manager)
        m_particle_manager->setFxAnchorPosition(anchor, glm::vec3(eqPos[1], eqPos[2], eqPos[0]));
}

void SpellVisualFX::fireMissileStage(MissileFx& m, const SpellMissile::SpellFields& spell)
{
    namespace Presets = EQT::Graphics::Environment::SpellPresets;
    if (!m_particle_manager) return;
    const uint32_t spellId = m.o.w.spellId;
    const auto caster = static_cast<uint16_t>(m.o.w.sourceId);
    // B1165 (#484): 00476ecc's FUN_0049b260 call is the stage gate's entry
    // (eqgame.exe.c:121989-122034): stage 1 or 2 on (caster, none) -- the
    // call passes 0 as the target (00476ebf .. 00476ecc).
    if (!stageDraws(spellId, m.o.stage, m.casterFx, SpellFxActor{})) {
        LOG_INFO(MOD_SPELL, "B1170 missile stage {}: spell={} not drawn (On For {}, density {}, opacity {}, "
                 "ShowSpellEffects {})", m.o.stage, spellId, m_fx_options.castFilter, m_fx_options.density,
                 m_fx_options.opacity, m_fx_options.showSpellEffects ? 1 : 0);
        return;
    }
    m.anchor = allocMissileAnchor();
    if (m.anchor == 0) return;
    placeMissileAnchor(m.anchor, m.o.w.pos);
    const int record = sourceRecordOf(spellId);
    // 00476ecc: FUN_0049b260(stage, record, caster, 0, 0, object, spell +0x28):
    // the slots on the caster (anchor 1) and the object (anchor 2) play for
    // the spell's field 18 (SpellFxCallerDurationMs's Middle rule).
    const int32_t callerMs = SpellFxCallerDurationMs(SpellFxStage::Middle, 0, spell.aeDuration);
    if (m.o.stage == 2) {
        const StageSlots slots = stageSlots(spellId, kSpellsNewEffTarget, m.gateLevel);
        if (!slots.fromFile) return;
        StageArgs args;
        args.caster = caster;
        args.missile = m.anchor;
        args.callerMs = callerMs;
        createLandingFromSlots(caster, 0, spellId, record, slots, &args);
        return;
    }
    const StageSlots slots = stageSlots(spellId, kSpellsNewEffMiddle, m.gateLevel);
    if (!slots.fromFile) {
        LOG_INFO(MOD_SPELL, "B1170 missile stage 1: spell={} has no spellsnew.eff record -- nothing drawn",
                 spellId);
        return;
    }
    auto scolor = getSpellColor(spellId);
    const glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f, scolor.getBlue() / 255.0f, 1.0f);
    std::string made;
    for (const auto& d : slots.drawn) {
        uint16_t follow = 0;
        switch (d.slot.anchorKind()) {
            case SpellsNewEffAnchor::Missile: follow = m.anchor; break;
            case SpellsNewEffAnchor::Caster:  follow = attachPointEntityId(caster, d.slot.attachPoint); break;
            default: break;  // anchor 0 / 3: the call's position / target are 0 (00495dc3 / 00495d65)
        }
        if (follow == 0) continue;
        glm::vec3 probe(0.0f);
        if (follow != m.anchor && follow != caster && !m_particle_manager->resolveEntityPosition(follow, probe))
            follow = caster;
        auto def = Presets::SourceForSpell(c, spellId, record, false, 0.0f, 1.0f);
        def.name = "MissileMiddle";
        if (!d.texture.empty()) {
            Presets::applyStageSpriteNames(def, {d.texture}, spellId);
            applySlotEddColours(def, {d.slot.emitter});  // B1146
        }
        float cap = 1.0f;
        SpellFxSlotTiming timing;
        if (slotTiming(d.slot.emitter, callerMs, timing)) {
            Presets::applySlotEmitter({&def}, timing, m_spells_new_eff.edd().sprite(d.slot.emitter),
                                      0.0f, kSpellFxAttachScale,
                                      m_spells_new_eff.edd().shape(d.slot.emitter),   // B1150
                                      m_spells_new_eff.edd().motion(d.slot.emitter));  // B1151
            cap = timing.end() + 0.1f;
        }
        m_particle_manager->createSpellEffect(def, follow, 0, cap);
        made += fmt::format(" {}:{}/{}@{}..{:.2f}", d.index, static_cast<int>(d.slot.anchorKind()),
                            d.slot.attachPoint, follow, timing.end());
    }
    LOG_INFO(MOD_SPELL, "B1170 missile stage 1: spell={} record={} slots{} (of {})", spellId, record, made,
             slots.measured);
}

void SpellVisualFX::fireMissileRemoval(MissileFx& m)
{
    namespace R = SpellMissileRules;
    const auto fire = SpellMissile::RemovalFireFor(m.o);
    if (fire == SpellMissile::RemovalFire::None || !m_particle_manager) return;
    const uint32_t spellId = m.o.w.spellId;
    const auto caster = static_cast<uint16_t>(m.o.w.sourceId);
    const int record = sourceRecordOf(spellId);
    const StageSlots slots = stageSlots(spellId, kSpellsNewEffTarget, m.gateLevel);
    if (fire == SpellMissile::RemovalFire::AtPositionBack) {
        // 00477a55 .. 00477aa3: FUN_0049b260(2, record, caster, 0, pos - vel, 0, 0).
        float pos[3];
        SpellMissile::RemovalPosition(m.o, pos);
        // B1165: 00477aa3's FUN_0049b260 is gated on (caster, none).
        if (!stageDraws(spellId, 2, m.casterFx, SpellFxActor{})) {
            LOG_INFO(MOD_SPELL, "B1170 missile removal: spell 54 Target stage not drawn (B1165 Spell page gate)");
            return;
        }
        const uint16_t world = allocMissileAnchor();
        if (world == 0 || !slots.fromFile) return;
        placeMissileAnchor(world, pos);
        m_missile_anchor_expiry.push_back({world, m_missile_clock_ms + kAnchorLingerMs});
        StageArgs args;
        args.caster = caster;
        args.world = world;
        createLandingFromSlots(caster, 0, spellId, record, slots, &args);
        LOG_INFO(MOD_SPELL, "B1170 missile removal: spell 54 Target stage at ({:.2f},{:.2f},{:.2f})",
                 pos[0], pos[1], pos[2]);
        return;
    }
    // 00477962 .. 00477a4e: spell 859 at the local player within 40.0 XY.
    if (!m_local_player_cb) return;
    const uint16_t player = m_local_player_cb();
    SpellMissile::Entity p;
    if (player == 0 || !missileEntity(player, p)) return;
    R::Spell859RemovalInputs in;
    in.actorObject = m.o.spawned;               // 00477947: [[missile+0x48]] != 0
    in.spellId = spellId;
    const SpellMissile::SpellFields* spell = missileSpellFields(spellId);
    in.recordExists = spell != nullptr;          // 0047f4de
    in.targetType = spell ? spell->targetType : 0;  // 0047f4e6: spell +0x180
    in.distXY = R::GateDistanceXY(m.o.w.pos[0], m.o.w.pos[1], p.pos[0], p.pos[1]);
    if (!R::Spell859RemovalFires(in) || !slots.fromFile) return;
    // B1165: 0x47f5ed's FUN_0049b260 is the stage gate on (caster, the
    // player); FUN_00491cff shows a spell whose target is me (0x491dd0 ..
    // 0x491e1d), so only density / opacity / ShowSpellEffects can hide it.
    SpellFxActor me;
    me.present = true;
    me.isSelf = true;
    if (!stageDraws(spellId, 2, m.casterFx, me)) {
        LOG_INFO(MOD_SPELL, "B1170 missile removal: spell 859 Target stage not drawn (B1165 Spell page gate)");
        return;
    }
    // 0047f5e1 .. 0047f5ed: FUN_0049b260(2, record, caster, player, 0, 0, 0).
    StageArgs args;
    args.caster = caster;
    args.target = player;
    createLandingFromSlots(caster, player, spellId, record, slots, &args);
    LOG_INFO(MOD_SPELL, "B1170 missile removal: spell 859 Target stage at the player {} (distXY {:.2f})",
             player, in.distXY);
}
#endif

}  // namespace EQ
