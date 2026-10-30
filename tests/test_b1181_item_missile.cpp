// B1181 (#500): the +0x65 IT model the client draws on a type != 9 missile
// object (client/spell/spell_missile_object.h;
// src/client/graphics/spell_visual_fx_missile.cpp;
// refactor/B1181/README.md).
//
//  - the model name from the payload bytes: the server sends the item's
//    IDFile at +0x65 (eqemu Mob::ProjectileAnimation), "IT" + the model
//    number the equipment archive indexes (EquipmentModelLoader:
//    modelName "IT156" / modelId 156);
//  - the orientation per frame: the actor's bearing / aim fields stay
//    synced to the flight's +0x2c / +0x3c (512-unit angles, 512 = 360
//    degrees) and the node rotation is the flight's angles, one rule;
//  - removal at lifetime / hit: the model node goes with the object;
//  - the type-9 path untouched: a spell missile never draws its model.
//
// The first half is device-free; the second drives SpellVisualFX on an
// EDT_NULL Irrlicht device (the idiom every renderer-side test in this
// suite uses) with the real equipment model loader, and GTEST_SKIPs when
// willeq.db or the EQ client data are absent, never fails.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "client/spell/spell_missile_object.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>

#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/spell_visual_fx.h"

using namespace EQ;
#endif

namespace M = EQ::SpellMissile;

namespace {

void PutF(std::vector<uint8_t>& b, size_t off, float v) { std::memcpy(&b[off], &v, 4); }
void PutU(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }

// The payload eqemu's Mob::ProjectileAnimation sends for an archery shot
// (zone/special_attacks.cpp, Arrow_Struct in common/eq_packet_structs.h):
// type 1, the src position, the thrust word, launch_angle / tilt / arc, the
// ids, the item id, the skill / item_type and the IDFile at +0x65.
std::vector<uint8_t> ArrowPayload(const char* model, uint8_t type, uint32_t word54) {
    std::vector<uint8_t> b(M::kWireSize, 0);
    PutU(b, 0x00, 1);
    PutF(b, 0x10, -120.5f);   // src_y
    PutF(b, 0x14, 33.25f);    // src_x
    PutF(b, 0x18, 4.0f);      // src_z
    PutF(b, 0x28, 1.5f);      // velocity (the thrust word)
    PutF(b, 0x2c, 200.0f);    // launch_angle (heading, 512 units)
    PutF(b, 0x30, 125.0f);    // tilt
    PutF(b, 0x3c, 50.0f);     // arc (aim, 512 units)
    PutU(b, 0x4c, 258);       // source_id
    PutU(b, 0x50, 259);       // target_id
    PutU(b, 0x54, word54);    // item id
    PutU(b, 0x58, 125);
    PutU(b, 0x5c, 16);
    b[0x62] = 7;
    b[0x63] = type;
    std::memcpy(&b[0x65], model, std::strlen(model) + 1);
    return b;
}

}  // namespace

// --- The model name from the payload bytes ----------------------------------

TEST(B1181ItemMissile, ModelNameFromThePayloadBytes) {
    // The archery payload (item 8005, IDFile IT10, item_type 27 != 9).
    const auto b = ArrowPayload("IT10", 27, 8005);
    M::Wire w;
    ASSERT_TRUE(M::DecodeWire(b.data(), b.size(), w));
    EXPECT_EQ(w.type, 27);
    EXPECT_EQ(w.model, "IT10");
    EXPECT_EQ(M::ParseMissileModelId(w.model), 10);

    // The bolt-spell payload (FRProjectileItem_Titanium: item 11504, whose
    // IDFile the server copies at +0x65).
    const auto c = ArrowPayload("IT11504", 27, 11504);
    M::Wire v;
    ASSERT_TRUE(M::DecodeWire(c.data(), c.size(), v));
    EXPECT_EQ(v.model, "IT11504");
    EXPECT_EQ(M::ParseMissileModelId(v.model), 11504);
}

TEST(B1181ItemMissile, OnlyITNamesParse) {
    EXPECT_EQ(M::ParseMissileModelId("IT10"), 10);
    EXPECT_EQ(M::ParseMissileModelId("IT156"), 156);
    EXPECT_EQ(M::ParseMissileModelId("IT1000000"), 1000000);
    EXPECT_EQ(M::ParseMissileModelId(""), -1);
    EXPECT_EQ(M::ParseMissileModelId("IT"), -1);        // no number
    EXPECT_EQ(M::ParseMissileModelId("ITX"), -1);       // not a number
    EXPECT_EQ(M::ParseMissileModelId("IT12A"), -1);     // a letter inside
    EXPECT_EQ(M::ParseMissileModelId("RED_BLOB"), -1);  // the fallback name
    EXPECT_EQ(M::ParseMissileModelId("IT1000001"), -1); // above the cap
}

// --- The orientation per frame ----------------------------------------------

TEST(B1181ItemMissile, AnglesAreTheFlightAnglesInDegrees) {
    // 512 units = 360 degrees (EQGraphicsDX9's tables mask with 0x1ff).
    const M::MissileModelAngles a0 = M::MissileModelAnglesFromFlight(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(a0.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(a0.pitchDeg, 0.0f);
    EXPECT_FLOAT_EQ(a0.rollDeg, 0.0f);

    // The archery payload's own angles: heading 200, aim 50.
    const M::MissileModelAngles a = M::MissileModelAnglesFromFlight(200.0f, 50.0f);
    EXPECT_FLOAT_EQ(a.yawDeg, -200.0f * 360.0f / 512.0f);
    EXPECT_FLOAT_EQ(a.pitchDeg, -50.0f * 360.0f / 512.0f);
    EXPECT_FLOAT_EQ(a.rollDeg, 0.0f);

    // A straight-up aim is a right angle of pitch, a full turn is a full
    // turn of yaw (period 512).
    const M::MissileModelAngles up = M::MissileModelAnglesFromFlight(0.0f, 128.0f);
    EXPECT_FLOAT_EQ(up.pitchDeg, -90.0f);
    const M::MissileModelAngles turn = M::MissileModelAnglesFromFlight(512.0f, 0.0f);
    EXPECT_FLOAT_EQ(turn.yawDeg, -360.0f);
}

TEST(B1181ItemMissile, TheModelFollowsTheFlightModel) {
    // One type-27 flight as the client runs it: the target stands north
    // (EQ y) of the source, a hair east so the bearing is the table's
    // quadrant 0 (the B1170 test's layout). The angles the model node gets
    // at each pass are exactly the flight model's heading / aim at that
    // pass -- the homing turns the flight, and the model turns with it.
    M::Object o;
    M::Wire w;
    w.word = 1.5f;
    w.damped = 1;
    w.type = 27;
    w.model = "IT10";
    w.heading = 200.0f;  // +0x2c, the archery payload's launch_angle
    w.aim = 50.0f;       // +0x3c, its arc
    w.sourceId = 1;
    w.targetId = 2;
    w.pos[1] = 1e-4f;  // a hair east
    o.w = w;
    M::Entity source, target;
    source.exists = target.exists = true;
    target.pos[0] = 20.0f;
    target.pos[1] = 1e-4f;
    uint32_t now = 1;
    ASSERT_TRUE(M::Spawn(o, now, nullptr, source, target, 0));

    const float k = 360.0f / 512.0f;
    // At the spawn the angles are the wire's (bearing = +0x2c, aim = +0x3c).
    EXPECT_FLOAT_EQ(o.w.heading, 200.0f);
    EXPECT_FLOAT_EQ(o.w.aim, 50.0f);
    {
        const auto a = M::MissileModelAnglesFromFlight(o.w.heading, o.w.aim);
        EXPECT_FLOAT_EQ(a.yawDeg, -200.0f * k);
        EXPECT_FLOAT_EQ(a.pitchDeg, -50.0f * k);
    }
    float lastYaw = 0.0f;
    bool changed = false;
    for (int pass = 0; pass < 200; ++pass) {
        now += 33;
        if (M::Pass(o, now, target)) break;
        const auto a = M::MissileModelAnglesFromFlight(o.w.heading, o.w.aim);
        // The rule: the node's angles ARE the flight's angles, every pass.
        EXPECT_FLOAT_EQ(a.yawDeg, -o.w.heading * k) << "pass " << pass;
        EXPECT_FLOAT_EQ(a.pitchDeg, -o.w.aim * k) << "pass " << pass;
        if (a.yawDeg != lastYaw) changed = true;
        lastYaw = a.yawDeg;
    }
    EXPECT_TRUE(changed) << "the homing turns the flight, and the model follows";
}

#ifdef EQT_HAS_GRAPHICS

std::string eqClientPath() {
    // The real client tree (the test_spell_slot_rules pattern): the repo's
    // data/ has no gequip archives, the client's own files are the NFS tree.
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

class B1181ItemMissileFxTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<SpellVisualFX> fx_;
    std::unique_ptr<EQT::Graphics::EquipmentModelLoader> loader_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        fx_ = std::make_unique<SpellVisualFX>(device_->getSceneManager(),
                                              device_->getVideoDriver(),
                                              eqClientPath());
        // The real equipment model loader over the EQ client's gequip
        // archives -- the same loader the renderer draws equipment with.
        loader_ = std::make_unique<EQT::Graphics::EquipmentModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(),
            device_->getFileSystem());
        loader_->setClientPath(eqClientPath());
        if (!loader_->loadEquipmentArchives()) {
            GTEST_SKIP() << "gequip archives not loadable under " << eqClientPath();
        }
        fx_->setEquipmentModelLoader(loader_.get());
        // Source 1 at the origin, target 2 twenty units north (EQ y), a
        // hair east (the B1170 test's layout).
        fx_->setMissileEntityCallback([](uint16_t id, SpellVisualFX::MissileEntityState& out) -> bool {
            if (id != 1 && id != 2) return false;
            out = SpellVisualFX::MissileEntityState{};
            out.eqPos[0] = id == 2 ? 20.0f : 0.0f;
            out.eqPos[1] = id == 2 ? 0.0001f : 0.0f;
            return true;
        });
    }

    void TearDown() override {
        fx_.reset();
        loader_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// A type != 9 object (the archery payload: type 27, IT10) draws its model on
// the FX clock and loses it with the object at the removal.
TEST_F(B1181ItemMissileFxTest, AType27ObjectDrawsItsITModel) {
    if (!loader_->getEquipmentMeshByModelId(10)) {
        GTEST_SKIP() << "IT10 not in the gequip archives under " << eqClientPath();
    }
    M::Wire w;
    w.word = 1.5f;
    w.damped = 1;
    w.type = 27;
    w.model = "IT10";
    w.sourceId = 1;
    w.targetId = 2;
    fx_->spawnMissileObject(w, 60);
    ASSERT_EQ(fx_->missileObjectCount(), 1u);
    fx_->update(1.0f / 30.0f);  // the spawn happens on the first frame
    ASSERT_EQ(fx_->missileObjectCount(), 1u);
    EXPECT_TRUE(fx_->missileModelDrawn(0));

    float pos[3] = {0, 0, 0};
    int frames = 0;
    while (fx_->missileObjectCount() == 1 && frames < 300) {
        fx_->update(1.0f / 30.0f);
        ++frames;
        if (fx_->missileModelDrawn(0) && fx_->missileObjectPosition(0, pos)) {
            // The model flies with the object: it reaches the target.
            EXPECT_GE(pos[0], 0.0f) << "frame " << frames;
        }
    }
    EXPECT_EQ(fx_->missileObjectCount(), 0u);
    EXPECT_FALSE(fx_->missileModelDrawn(0));
}

// A name that is not an IT number draws nothing (the client's own fallback
// is a different actor, out of scope; the object still flies and is removed).
TEST_F(B1181ItemMissileFxTest, ANameThatIsNotITDrawsNothing) {
    M::Wire w;
    w.word = 1.5f;
    w.damped = 1;
    w.type = 27;
    w.model = "RED_BLOB";
    w.sourceId = 1;
    w.targetId = 2;
    fx_->spawnMissileObject(w, 60);
    ASSERT_EQ(fx_->missileObjectCount(), 1u);
    fx_->update(1.0f / 30.0f);
    ASSERT_EQ(fx_->missileObjectCount(), 1u);
    EXPECT_FALSE(fx_->missileModelDrawn(0));
    int frames = 0;
    while (fx_->missileObjectCount() == 1 && frames < 300) {
        fx_->update(1.0f / 30.0f);
        ++frames;
    }
    EXPECT_EQ(fx_->missileObjectCount(), 0u);
}

// The type-9 path is byte-unchanged: a spell missile (even one the server
// names with an IT model at +0x65, the bolt-spell case) never draws its
// model; only B1170's stages draw.
TEST_F(B1181ItemMissileFxTest, AType9ObjectNeverDrawsItsModel) {
    if (!std::filesystem::exists(eqClientPath() + "/spells_us.txt")) {
        GTEST_SKIP() << "spells_us.txt not present under " << eqClientPath();
    }
    M::Wire w;
    w.word = 1.5f;
    w.damped = 1;
    w.type = M::kTypeSpell;
    w.spellId = 38;
    w.model = "IT11504";  // what the bolt-spell path would name
    w.sourceId = 1;
    w.targetId = 2;
    fx_->spawnMissileObject(w, 60);
    ASSERT_EQ(fx_->missileObjectCount(), 1u);
    int frames = 0;
    while (fx_->missileObjectCount() == 1 && frames < 300) {
        fx_->update(1.0f / 30.0f);
        EXPECT_FALSE(fx_->missileModelDrawn(0)) << "frame " << frames;
        ++frames;
    }
    EXPECT_EQ(fx_->missileObjectCount(), 0u);
}

#endif  // EQT_HAS_GRAPHICS
