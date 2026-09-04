// T334 (refactor/T334_camera_audio_and_vision.md, board B046): the
// VisionChanged bridge arm.
//
// The board entry said the buff-fade hook did not exist. It does:
// EverQuest::InitializeSpellSystem() installs a BuffManager fade callback
// (everquest.cpp) that, on a UltraVision/InfraVision buff expiring,
// RECOMPUTES the vision from the buffs still up and publishes the result as a
// VisionChanged event. The gap was at the far end -- the arm called
// IrrlichtRenderer::setVisionType(), which only ever UPGRADED, so the
// recomputed value could raise vision and never lower it. The fade publish
// was a no-op and an Ultravision buff lasted the whole session.
//
// VisionChanged carries an ABSOLUTE recomputed vision, not a delta, so the
// arm must apply it as one. Racial vision is the floor, and it comes from
// baseVision_ (setPlayerRace), never from this event -- a fade to "no vision
// buffs left" publishes 0 and must land on the race's base, not on Normal.
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_navmesh_overlay_wire.cpp / test_player_camera_component.cpp: nothing
// on this path touches an Irrlicht device.

#include <gtest/gtest.h>

#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/renderer_types.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_database.h"
#include "client/state/event_bus.h"
#include "client/willeq_database.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif
#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

using namespace EQT::Graphics;

namespace {

// T339: opened once for the whole binary (test_buff_manager.cpp precedent).
// The T334 bridge-arm tests below need no DB and run regardless.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool HasSpellDb() { return g_db_available; }

// The publisher's encoding: 0 = normal, 1 = infravision, 2 = ultravision.
eqt::state::GameEvent visionChanged(uint8_t visionType) {
    eqt::state::VisionChangedData d;
    d.visionType = visionType;
    return eqt::state::GameEvent(eqt::state::GameEventType::VisionChanged, d);
}

TEST(VisionBuffFadeWireTest, AFadeToNoVisionBuffsReturnsAHumanToNormal) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(1);  // Human -- base Normal
    ASSERT_EQ(renderer.getVisionType(), VisionType::Normal);

    // A buff lands: the callback recomputes and publishes Ultravision.
    bridge.applyEvent(visionChanged(2));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // It fades with nothing else up: the callback publishes 0. Before T334
    // this stayed at Ultravision for the rest of the session.
    bridge.applyEvent(visionChanged(0));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal);
}

TEST(VisionBuffFadeWireTest, AFadeNeverDropsBelowTheRacialBase) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(8);  // Dwarf -- base Infravision
    ASSERT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Infravision);
    ASSERT_EQ(renderer.getVisionType(), VisionType::Infravision);

    bridge.applyEvent(visionChanged(2));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // No vision buffs left. The Dwarf keeps his own infravision -- the event's
    // 0 is "no BUFF vision", not "no vision".
    bridge.applyEvent(visionChanged(0));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

TEST(VisionBuffFadeWireTest, OneOfTwoStackedBuffsFadingLeavesTheOther) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    renderer.PlayerCamera().setPlayerRace(1);  // Human

    // Ultravision + Infravision up; the callback's loop reports the best.
    bridge.applyEvent(visionChanged(2));
    ASSERT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // The Ultravision one fades; the Infravision one is still up, so the
    // recomputed publish is 1 -- a genuine DOWNGRADE that must take effect.
    bridge.applyEvent(visionChanged(1));
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

}  // namespace

// ===========================================================================
// T339 (refactor/T339_vision_buff_landing.md, board B055): the LANDING half.
//
// The tests above drive the bridge arm with hand-built events. These drive the
// PUBLISHER: a real BuffManager over the real spell DB, with the two callbacks
// EverQuest::InitializeSpellSystem() installs, feeding a real IrrlichtBridge
// and the renderer's PlayerCameraComponent. Before T339 nothing was registered
// on the apply side at all, so applyBuff() published nothing and a cast
// Ultravision never reached the renderer.
//
// Precedence is BEST VISION WINS (Ultravision > Infravision > none); grepping
// docs/ in T339 found no record of the official client's rule, so that is the
// stated rule and it matches what the pre-existing fade loop already did.
// ===========================================================================

namespace {

// Real spells from data/willeq.db, both single-effect vision buffs:
//   46  Ultravision   -- effect_id_1 == 66 (SpellEffect::UltraVision)
//   276 Serpent Sight -- effect_id_1 == 65 (SpellEffect::InfraVision)
constexpr uint32_t kUltravisionSpell = 46;
constexpr uint32_t kInfravisionSpell = 276;

// A harness that wires exactly what InitializeSpellSystem() wires: one shared
// recompute-and-publish body behind both BuffManager callbacks.
struct VisionPublisherHarness {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    EQ::SpellDatabase db;
    std::unique_ptr<EQ::BuffManager> buffs;

    bool Init(uint16_t race) {
        if (!db.initFromDatabase()) return false;
        buffs = std::make_unique<EQ::BuffManager>(&db);
        bridge.setRenderer(&renderer);
        renderer.PlayerCamera().setPlayerRace(race);

        auto publish = [this](uint16_t entity_id, uint32_t spell_id,
                              uint32_t exclude_spell_id) {
            if (entity_id != 0) return;
            const EQ::SpellData* spell = db.getSpell(spell_id);
            if (!spell) return;
            if (!spell->hasEffect(EQ::SpellEffect::UltraVision) &&
                !spell->hasEffect(EQ::SpellEffect::InfraVision)) return;
            bridge.applyEvent(visionChanged(buffs->computeVisionType(exclude_spell_id)));
        };
        buffs->setBuffApplyCallback([publish](uint16_t e, uint32_t s) {
            publish(e, s, EQ::BuffManager::NO_EXCLUDED_SPELL);
        });
        buffs->setBuffFadeCallback([publish](uint16_t e, uint32_t s) {
            publish(e, s, s);
        });
        return true;
    }

    void Cast(uint32_t spell_id) {
        buffs->applyBuff(0, spell_id, /*caster*/ 0, /*level*/ 50, /*ticks*/ 100);
    }
    void Fade(uint32_t spell_id) { buffs->removeBuff(0, spell_id); }
};

}  // namespace

TEST(VisionBuffLandingWireTest, CastingUltravisionRaisesVisionOnLanding) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    VisionPublisherHarness h;
    ASSERT_TRUE(h.Init(1));  // Human -- base Normal
    ASSERT_EQ(h.renderer.getVisionType(), VisionType::Normal);

    // Before T339 the apply side published nothing and this stayed Normal.
    h.Cast(kUltravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);
}

TEST(VisionBuffLandingWireTest, ALesserVisionBuffLandingOnTopDoesNotDowngrade) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    VisionPublisherHarness h;
    ASSERT_TRUE(h.Init(1));  // Human

    h.Cast(kUltravisionSpell);
    ASSERT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);

    // Best vision wins: Infravision landing while Ultravision is up changes
    // nothing.
    h.Cast(kInfravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);
}

TEST(VisionBuffLandingWireTest, FadingTheBestBuffFallsBackToTheOtherThenToBase) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    VisionPublisherHarness h;
    ASSERT_TRUE(h.Init(1));  // Human -- base Normal

    h.Cast(kUltravisionSpell);
    h.Cast(kInfravisionSpell);
    ASSERT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);

    // removeBuff() notifies BEFORE erasing, so the shared recompute has to
    // exclude the departing spell; without that this would report Ultravision
    // for a buff that is on its way out.
    h.Fade(kUltravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Infravision);

    h.Fade(kInfravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Normal);
}

TEST(VisionBuffLandingWireTest, ADwarfNeverFallsBelowHisOwnInfravision) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    VisionPublisherHarness h;
    ASSERT_TRUE(h.Init(8));  // Dwarf -- base Infravision
    ASSERT_EQ(h.renderer.getVisionType(), VisionType::Infravision);

    h.Cast(kUltravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);

    h.Fade(kUltravisionSpell);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Infravision);
}

TEST(VisionBuffLandingWireTest, ANonVisionBuffPublishesNothing) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    VisionPublisherHarness h;
    ASSERT_TRUE(h.Init(1));  // Human

    h.Cast(kUltravisionSpell);
    ASSERT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);

    // Spell 3 (Summon Corpse) is not a vision buff; the hook must return before
    // recomputing, or every landing buff in the game would republish vision.
    const EQ::SpellData* plain = h.db.getSpell(3);
    ASSERT_NE(plain, nullptr);
    ASSERT_FALSE(plain->hasEffect(EQ::SpellEffect::UltraVision));
    ASSERT_FALSE(plain->hasEffect(EQ::SpellEffect::InfraVision));
    h.Cast(3);
    EXPECT_EQ(h.renderer.getVisionType(), VisionType::Ultravision);
}

// The recompute on its own, with no renderer in the way.
TEST(VisionBuffLandingWireTest, ComputeVisionTypeRanksBestFirstAndHonoursTheExclusion) {
    if (!HasSpellDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EQ::SpellDatabase db;
    ASSERT_TRUE(db.initFromDatabase());
    EQ::BuffManager mgr(&db);

    EXPECT_EQ(mgr.computeVisionType(), 0) << "no buffs up";

    mgr.applyBuff(0, kInfravisionSpell, 0, 50, 100);
    EXPECT_EQ(mgr.computeVisionType(), 1);

    mgr.applyBuff(0, kUltravisionSpell, 0, 50, 100);
    EXPECT_EQ(mgr.computeVisionType(), 2) << "best vision wins";

    EXPECT_EQ(mgr.computeVisionType(kUltravisionSpell), 1)
        << "excluding the better buff leaves the lesser one";
    EXPECT_EQ(mgr.computeVisionType(kInfravisionSpell), 2);
}

// A BuffManager with no spell DB must not guess.
TEST(VisionBuffLandingWireTest, ComputeVisionTypeWithoutASpellDbIsZero) {
    EQ::BuffManager mgr(nullptr);
    mgr.applyBuff(0, kUltravisionSpell, 0, 50, 100);
    EXPECT_EQ(mgr.computeVisionType(), 0);
}

// ---------------------------------------------------------------------------
// The registration itself has no runtime seam -- EverQuest is not constructible
// in a unit test -- so it is pinned by reading the source, the same idiom as
// test_command_registration_order.cpp. This is what actually regressed: the
// apply-side registration simply did not exist.
// ---------------------------------------------------------------------------
TEST(VisionBuffLandingWireTest, InitializeSpellSystemRegistersBothHooksOnOneRecompute) {
    const std::string path =
        std::string(WILLEQ_REPO_ROOT) + "/src/client/core/everquest.cpp";
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "cannot read " << path;
    const std::string src((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

    EXPECT_NE(src.find("setBuffApplyCallback"), std::string::npos)
        << "B055: a vision buff LANDING publishes nothing without this hook";
    EXPECT_NE(src.find("setBuffFadeCallback"), std::string::npos);

    // One shared body, one shared recompute: both hooks call the same lambda,
    // and the only vision recompute is BuffManager::computeVisionType().
    size_t first = src.find("publishVisionForBuffChange");
    ASSERT_NE(first, std::string::npos);
    int uses = 0;
    for (size_t p = first; p != std::string::npos;
         p = src.find("publishVisionForBuffChange", p + 1)) {
        ++uses;
    }
    EXPECT_GE(uses, 4) << "definition + two captures + two call sites";

    size_t recomputes = 0;
    for (size_t p = src.find("->computeVisionType("); p != std::string::npos;
         p = src.find("->computeVisionType(", p + 1)) {
        ++recomputes;
    }
    EXPECT_EQ(recomputes, 1u)
        << "the recompute must be called from exactly one place, shared by "
           "both hooks";
}

// ===========================================================================
// T348 (refactor/T348_buff_landing_producer.md, board B056): THE RACIAL FLOOR
// ON THE PATH THAT ACTUALLY RUNS.
//
// The tests above all set the racial base by calling
// PlayerCameraComponent::setPlayerRace() directly. T345 then found that
// "Player race N -> base vision: X" -- the INFO line setPlayerRace logs on
// every call -- appears in NONE of five live session logs, although the player
// entity is created in each one. Measured in T348: setPlayerRace()'s only call
// site was IrrlichtRenderer::createEntity(), and createEntity() has ZERO
// production callers tree-wide. The live zone load registers every entity
// through IrrlichtRenderer::registerEntity() (application_zone_loading.cpp,
// twice -- the threaded and the inline load) and post-load spawns arrive on
// the EntitySpawned bridge arm (bridge/irrlicht_bridge.cpp), which also calls
// registerEntity(). So baseVision_ stayed at its Normal default all session and
// a Dark Elf saw exactly as well in the dark as a Human.
//
// The runtime half below pins the floor itself. The reachability half has no
// runtime seam here -- IrrlichtRenderer::registerEntity() returns early without
// an entityRenderer_, which needs an Irrlicht device -- so it is pinned by
// reading the source, the same idiom as
// InitializeSpellSystemRegistersBothHooksOnOneRecompute above and
// test_command_registration_order.cpp. That is what actually regressed.
// ===========================================================================

TEST(RacialVisionFloorTest, TheFloorIsTheRaceAndBuffsOnlyRaiseIt) {
    IrrlichtRenderer darkElf;
    darkElf.PlayerCamera().setPlayerRace(6);
    EXPECT_EQ(darkElf.PlayerCamera().getBaseVision(), VisionType::Ultravision);
    EXPECT_EQ(darkElf.getVisionType(), VisionType::Ultravision)
        << "a Dark Elf sees in the dark before any buff";

    IrrlichtRenderer human;
    human.PlayerCamera().setPlayerRace(1);
    EXPECT_EQ(human.PlayerCamera().getBaseVision(), VisionType::Normal);
    EXPECT_EQ(human.getVisionType(), VisionType::Normal);

    // The defect's shape: without setPlayerRace the Dark Elf is a Human.
    IrrlichtRenderer neverTold;
    EXPECT_EQ(neverTold.PlayerCamera().getBaseVision(), VisionType::Normal)
        << "the default is Normal, which is why the miss was invisible";
}

TEST(RacialVisionFloorTest, RegisterEntitySetsTheRaceBecauseCreateEntityIsNeverCalled) {
    const std::string path = std::string(WILLEQ_REPO_ROOT) +
        "/src/client/graphics/renderer/renderer_entities.cpp";
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "cannot read " << path;
    const std::string src((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

    const size_t reg = src.find("IrrlichtRenderer::registerEntity(");
    ASSERT_NE(reg, std::string::npos);
    const size_t upd = src.find("IrrlichtRenderer::updateEntity(", reg);
    ASSERT_NE(upd, std::string::npos) << "registerEntity's body ends before updateEntity";

    EXPECT_NE(src.find("setPlayerRace", reg), std::string::npos);
    EXPECT_LT(src.find("setPlayerRace", reg), upd)
        << "B056: the racial vision floor must be set in registerEntity(), the "
           "only entity-creation entry point the live zone load reaches";
}

TEST(RacialVisionFloorTest, TheLiveZoneLoadStillGoesThroughRegisterEntity) {
    // If this ever flips to createEntity(), the call above moves with it.
    for (const char* rel : {"/src/client/application_zone_loading.cpp",
                            "/src/client/bridge/irrlicht_bridge.cpp"}) {
        const std::string path = std::string(WILLEQ_REPO_ROOT) + rel;
        std::ifstream in(path);
        ASSERT_TRUE(in.good()) << "cannot read " << path;
        const std::string src((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        EXPECT_NE(src.find("registerEntity("), std::string::npos) << rel;
        EXPECT_EQ(src.find("->createEntity("), std::string::npos)
            << rel << ": createEntity() has no production caller (T348)";
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    const char* env_path = std::getenv("WILLEQ_DB_PATH");
    const std::string db_path = env_path ? env_path : WILLEQ_DB_PATH;
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
