// T395 Leg A (refactor/T395_spell_impact_eqstr_blob_race_data.md, board B204):
// the spell impact visual, from the wire to a particle.
//
// B204, measured by T386 (review/T386/ SS3): an observing client 15 units away
// with the caster in frame logged both SpellAction packets and its pixels did
// not move -- no burst, no colour, nothing. T381's Fire/Cold vocabulary is
// real and has no consumer that reaches a frame.
//
// TWO independent breaks in that chain, both pinned here.
//
// (1) THE GATE. SpellManager::handleSpellAction publishes
//     SpellCastVisualComplete with isSuccess = (effect_flag == 4), and
//     irrlicht_bridge.cpp gated createSpellComplete()/createImpact() on
//     isSuccess. eqemu sends TWO OP_Action packets for one cast
//     (Mob::SpellOnTarget, zone/spells.cpp):
//       effect_flag == 0  -- "Actual cast action - this causes the caster
//                            animation and the particles around the target",
//                            queued to the target, the caster AND
//                            entity_list.QueueCloseClients(...) -- i.e. every
//                            client in range;
//       effect_flag == 4  -- "send the action packet again now that the spell
//                            is successful ... this is what causes the buff
//                            icon to appear", queued ONLY to the target client
//                            and the caster client.
//     So the packet the impact visual was gated on is the one a bystander
//     never receives. Confirmed in T386's own logs: the observer
//     (logs/fable_session_spells13_observer.log) records effect_flag=0 twice
//     and effect_flag=4 never, while the caster's log records the 4.
//     Fix: carry isCastAnimation = (effect_flag == 0) on the payload and gate
//     the FX on that; isSuccess keeps meaning "landed" for the chat lines,
//     the buff icon and completeCast().
//
// (2) THE ORIGIN BURST. SimulationWorker::computeParticles runs its SPAWN
//     phase before its spell-effect LIFECYCLE phase, and only the lifecycle
//     phase refreshed an entity-attached emitter's position from
//     pi.entityPositions. An impact emitter is therefore created at (0,0,0)
//     -- the attached entity's position is not in the map on the frame the
//     effect is created, because that map is built from the PREVIOUS frame's
//     positionRequestEntities -- and its first spawn frame still sees (0,0,0).
//     With frustum culling off it fires its whole one-shot burst at the world
//     origin and latches isBurstSpawned, so nothing ever appears on the
//     target; with culling on it is culled at the origin and, because the
//     preset's emitterLifetime is 0.1 s, is dead before the position arrives
//     at anything under ~20 fps. Fix: refresh an attached emitter's position
//     in the spawn phase, before the lifetime check and the frustum test.
//
// Seam for (1): zone_stream_test_utils.h's bare EverQuest + a recording
// bridge, driven through the real ReplayZonePacket dispatch into the real
// CommandsComponent::ZoneProcessAction -- the same seam
// tests/test_buff_landing_producer.cpp (T348) uses, with the same 33-byte
// wire Action layout.
// Seam for (2): SimulationWorker::computeOnce(), the worker's own public
// synchronous entry point, fed the exact ParticleCommandData that
// ParticleManager::createSpellEffect() enqueues for
// SpellVisualFX::createImpact() (SpellPresets::Impact, casterID 0, the target
// entity as targetID).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/spell/spell_manager.h"
#include "client/state/event_bus.h"
#include "zone_stream_test_utils.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/simulation_worker.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#endif
#endif

using namespace zonetest;

namespace {

constexpr uint8_t kSpellEffectAction = 231;  // 0xE7
constexpr uint8_t kAnimationPacket = 0;
constexpr uint8_t kLandedPacket = 4;

// T386's spells13 session: Testcaster is spawn 38 and casts on itself; the
// observer is a different client entirely, so 38 is a REMOTE entity there.
constexpr uint16_t kRemoteCaster = 38;
constexpr uint16_t kMySpawn = 287;
constexpr uint32_t kBurstOfFlame = 93;

// The 31-byte wire Action struct at the offsets ZoneProcessAction reads
// (opcode-inclusive), identical to test_buff_landing_producer.cpp's.
std::vector<uint8_t> ActionPayload(uint16_t target, uint16_t source,
                                   uint16_t level, uint16_t spell,
                                   uint8_t type, uint8_t effect_flag) {
    return BuildPayload(33, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, target);
        p.PutUInt16(4, source);
        p.PutUInt16(6, level);
        p.PutUInt32(8, 0);
        p.PutFloat(12, 0.0f);
        p.PutFloat(16, 0.0f);
        p.PutFloat(20, 0.0f);
        p.PutUInt8(24, type);
        p.PutUInt16(25, 0);
        p.PutUInt16(27, 0);
        p.PutUInt16(29, spell);
        p.PutUInt8(31, static_cast<uint8_t>(level));
        p.PutUInt8(32, effect_flag);
    });
}

struct ObserverHarness {
    EverQuest eq = MakeEq();
    TestBridge bridge;

    ObserverHarness() {
        eq.setBridge(&bridge);
        eq.InitializeSpellSystem("");
        eq.Test_SetMySpawnIdForSeam(kMySpawn);
    }

    // ZoneProcessAction forwards to ctx_.spell_manager; without one nothing
    // is published and the assertions below would be vacuous.
    bool Ready() { return eq.GetSpellManager() != nullptr; }

    // Everything published for SpellCastVisualComplete since the last look.
    std::vector<eqt::state::SpellCastVisualCompleteData> Complete() {
        std::vector<eqt::state::SpellCastVisualCompleteData> out;
        for (const auto& ev : bridge.drainEvents()) {
            if (ev.type == eqt::state::GameEventType::SpellCastVisualComplete) {
                out.push_back(std::get<eqt::state::SpellCastVisualCompleteData>(ev.data));
            }
        }
        return out;
    }

    void Deliver(uint8_t effectFlag) {
        ReplayZone(eq, HC_OP_Action,
                   ActionPayload(kRemoteCaster, kRemoteCaster, 50,
                                 static_cast<uint16_t>(kBurstOfFlame),
                                 kSpellEffectAction, effectFlag));
    }
};

// The packet EVERY client in range receives must be the one that arms the
// impact visual. Pre-fix isCastAnimation is always false, so the bridge arm
// that calls createSpellComplete()/createImpact() is unreachable for anyone
// but the caster and the target.
TEST(SpellImpactVisualProducer, AnimationPacketArmsTheImpactVisual) {
    ObserverHarness h;
    ASSERT_TRUE(h.Ready()) << "no SpellManager -- the seam is not wired";
    h.Deliver(kAnimationPacket);

    auto events = h.Complete();
    ASSERT_EQ(events.size(), 1u) << "the animation packet must still publish a completion event";
    EXPECT_TRUE(events[0].isCastAnimation)
        << "effect_flag == 0 is the packet eqemu broadcasts to bystanders and the one that "
           "draws the particles; nothing else reaches an observing client";
    EXPECT_FALSE(events[0].isSuccess)
        << "the animation packet is not a landing -- isSuccess must keep its meaning";
    EXPECT_EQ(events[0].casterId, kRemoteCaster);
    EXPECT_EQ(events[0].targetId, kRemoteCaster);
    EXPECT_EQ(events[0].spellId, kBurstOfFlame);
}

// ...and the flag-4 repeat must NOT arm it a second time, or the caster (the
// one client that gets both packets) draws every impact twice.
TEST(SpellImpactVisualProducer, LandedPacketDoesNotRearmTheImpactVisual) {
    ObserverHarness h;
    ASSERT_TRUE(h.Ready()) << "no SpellManager -- the seam is not wired";
    h.Deliver(kLandedPacket);

    auto events = h.Complete();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].isCastAnimation)
        << "the flag-4 repeat is the buff-icon packet; drawing on it double-fires on the caster";
    EXPECT_TRUE(events[0].isSuccess);
}

// The measured pair, in order: exactly one of the two arms the visual.
TEST(SpellImpactVisualProducer, ExactlyOneOfTheMeasuredPairArmsTheVisual) {
    ObserverHarness h;
    ASSERT_TRUE(h.Ready()) << "no SpellManager -- the seam is not wired";
    h.Deliver(kAnimationPacket);
    h.Deliver(kLandedPacket);

    auto events = h.Complete();
    ASSERT_EQ(events.size(), 2u);
    int armed = 0;
    for (const auto& e : events) {
        if (e.isCastAnimation) ++armed;
    }
    EXPECT_EQ(armed, 1) << "one cast must paint one impact";
}

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

// The exact command ParticleManager::createSpellEffect() enqueues for
// SpellVisualFX::createImpact(): the Impact preset, no caster, the target
// entity as targetID, duration 0.
EQT::Graphics::ParticleCommandData ImpactCommand(uint16_t targetEntity, uint32_t spellId) {
    EQT::Graphics::ParticleCommandData cmd;
    cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
    cmd.spellDef = EQT::Graphics::Environment::SpellPresets::Impact(
        glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), spellId);
    cmd.casterID = 0;
    cmd.targetID = targetEntity;
    cmd.duration = 0.0f;
    cmd.preAssignedEffectID = 1;
    return cmd;
}

class ImpactParticleTest : public ::testing::Test {
protected:
    // ParticleManager's constructor loads config/spell_effects.json (a
    // CWD-relative path in production); the presets read it through the
    // singleton. Without it SpellPresets::Impact() falls back to
    // EmitterConfig's own defaults -- spawnRate 10/s, burstCount 0 -- which is
    // not the shipped impact at all, so load it here the way the renderer does.
    void SetUp() override {
        const std::string cfg =
            std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
        if (!EQT::Graphics::Environment::SpellEffectsConfig::instance().load(cfg)) {
            GTEST_SKIP() << "no " << cfg;
        }
        auto impact = EQT::Graphics::Environment::SpellPresets::Impact(
            glm::vec4(1.0f), 93);
        ASSERT_EQ(impact.emitters.size(), 1u);
        // T422: the shipped impact style stopped being a one-shot burst. The
        // official client's population rises over ~2 s with no deaths rather
        // than appearing in a single frame (review/T422/README.md 2), so the
        // style is now a spawnRate emitter over a 0.6 s emitterLifetime and
        // its burstCount is 0. What these two tests prove is unchanged --
        // WHERE the first frame of particles is born -- because the emitter
        // does not exist until the lifecycle phase, so either shape spawns its
        // first particles on the SECOND worker frame, by which time the
        // attach position has been requested and resolved.
        ASSERT_GT(impact.emitters[0].config.spawnRate, 0.0f)
            << "the shipped impact style emits over a window; the config did not apply";
        ASSERT_GT(impact.emitters[0].config.emitterLifetime, 0.0f)
            << "the shipped impact style has a bounded emission window; the config did not apply";
    }

    static constexpr uint16_t kTarget = 38;
    // Irrlicht Y-up, the shape the renderer's entity-position callback
    // returns. T386's arena pose.
    const glm::vec3 kTargetPos{146.0f, 51.0f, -1009.0f};

    // One worker frame, run the way the renderer runs it.
    //
    // THE PROTOCOL MATTERS: pi.entityPositions is NOT "wherever everything
    // is". SimBridgeComponent::postSimulationInput() moves in
    // pendingParticleEntityPositions_, which applyWorkerResults() filled from
    // the PREVIOUS frame's output.particleOutput.positionRequestEntities. So
    // on the frame a spell effect is created its attached entity's position is
    // necessarily absent -- nothing had asked for it yet. That one-frame lag
    // is what puts the emitter at the world origin, and a test that feeds the
    // position unconditionally cannot see the bug.
    void Frame(bool withCommand, float deltaTime, bool frustum) {
        EQT::Graphics::SimulationInput input;
        if (frustum) {
            // A frustum that contains the target and excludes the world
            // origin: the half-space x >= 100 (EQ Z-up) on all six planes.
            input.frustumValid = true;
            for (int i = 0; i < 6; ++i) {
                input.frustumPlanes[i][0] = 1.0f;
                input.frustumPlanes[i][1] = 0.0f;
                input.frustumPlanes[i][2] = 0.0f;
                input.frustumPlanes[i][3] = -100.0f;
            }
        }
        auto& pi = input.particleInput;
        pi.deltaTime = deltaTime;
        pi.unifiedRendererInitialized = true;
        pi.poolSize = 1024;
        for (uint16_t id : requested_) {
            if (id == kTarget) pi.entityPositions[kTarget] = kTargetPos;
        }
        if (withCommand) {
            pi.commands.push_back(ImpactCommand(kTarget, 93));
        }
        worker_.computeOnce(input, output_);
        requested_ = output_.particleOutput.positionRequestEntities;
    }

    std::unordered_set<uint16_t> requested_;
    EQT::Graphics::SimulationWorker worker_;
    EQT::Graphics::SimulationOutput output_;
};

// The burst must land ON the target, not at the world origin. Pre-fix the
// emitter is created at (0,0,0) -- the target's position is not in the map
// yet -- and the spawn phase, which runs BEFORE the lifecycle phase that
// refreshes an attached emitter's position, fires the whole one-shot burst
// there and latches isBurstSpawned. Nothing ever appears on the target.
TEST_F(ImpactParticleTest, TheBurstSpawnsOnTheTargetNotAtTheWorldOrigin) {
    Frame(/*withCommand=*/true, 1.0f / 60.0f, /*frustum=*/false);
    Frame(/*withCommand=*/false, 1.0f / 60.0f, /*frustum=*/false);

    ASSERT_TRUE(output_.particleOutput.valid);
    const auto& buf = output_.particleOutput.renderBuffer;
    ASSERT_FALSE(buf.empty()) << "the impact preset's burst produced no particles at all";

    for (const auto& p : buf) {
        const float dx = p.position.x - kTargetPos.x;
        const float dy = p.position.y - kTargetPos.y;
        const float dz = p.position.z - kTargetPos.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        EXPECT_LT(dist, 4.0f)
            << "a particle spawned " << dist << " units from the target at ("
            << p.position.x << "," << p.position.y << "," << p.position.z
            << ") -- the emitter burst before its attach position was resolved";
    }
}

// The same break with frustum culling on, which is the shipped configuration:
// at the origin the emitter is culled out of the spawn phase entirely, so the
// burst does not happen on its first spawn frame at all. The impact preset's
// emitterLifetime is 0.1 s, so every frame lost that way is a large fraction
// of the effect -- and below ~20 fps the emitter is dead before its position
// is ever applied.
TEST_F(ImpactParticleTest, TheBurstIsNotCulledAtTheWorldOrigin) {
    Frame(/*withCommand=*/true, 1.0f / 60.0f, /*frustum=*/true);
    Frame(/*withCommand=*/false, 1.0f / 60.0f, /*frustum=*/true);

    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_FALSE(output_.particleOutput.renderBuffer.empty())
        << "the impact emitter was frustum-culled at the world origin on its first "
           "spawn frame, because its attach position is applied only afterwards";
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

}  // namespace
