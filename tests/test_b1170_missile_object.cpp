// B1170 (#489): the official client's 0x4033 missile object, device-free
// (client/spell/spell_missile_object.h; refactor/B1170/README.md). Addresses
// are instruction starts in review/B1141/ghidra/out/eqgame.dis.
//
//  - the decoder: the constructor's received branch copies the 0x88-byte
//    message field by field (004770eb / 004770ee for the thrust word);
//  - the motion model: FUN_004766aa + FUN_004bfe0f per pass reproduce
//    refactor/B1163/scripts/missile_model.py's 24 rows;
//  - the hit / removal rule, the no-flight target types and the four
//    early-outs of the 859 removal fire.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/spell_b1163_missile_rules.h"
#include "client/spell/spell_missile_object.h"

namespace M = EQ::SpellMissile;
namespace R = EQ::SpellMissileRules;

namespace {

void PutF(std::vector<uint8_t>& b, size_t off, float v) { std::memcpy(&b[off], &v, 4); }
void PutU(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }

// The payload eqemu's Mob::ProjectileAnimation sends (zone/special_attacks.cpp,
// Arrow_Struct in common/eq_packet_structs.h): type 1, src_y / src_x / src_z,
// velocity (the thrust word), launch_angle, tilt 125, arc 50, the ids, the
// item id, 125 / 16 in the two unknowns, skill and item_type, the IDFile.
std::vector<uint8_t> ServerArrowPayload() {
    std::vector<uint8_t> b(M::kWireSize, 0);
    PutU(b, 0x00, 1);
    PutF(b, 0x10, -120.5f);   // src_y
    PutF(b, 0x14, 33.25f);    // src_x
    PutF(b, 0x18, 4.0f);      // src_z
    PutF(b, 0x28, 1.5f);      // velocity (TrySpellProjectile's default speed)
    PutF(b, 0x2c, 200.0f);    // launch_angle
    PutF(b, 0x30, 125.0f);    // tilt
    PutF(b, 0x3c, 50.0f);     // arc
    PutU(b, 0x4c, 258);       // source_id
    PutU(b, 0x50, 259);       // target_id
    PutU(b, 0x54, 8005);      // item_id
    PutU(b, 0x58, 125);       // unknown088
    PutU(b, 0x5c, 16);        // unknown092
    b[0x62] = 7;              // skill (Archery)
    b[0x63] = 27;             // item_type (ItemTypeArrow)
    std::memcpy(&b[0x65], "IT10", 5);
    return b;
}

// A type-9 (spell) object the way the client's own local branch would send
// it (00477514: word 1.5), for a spell id.
std::vector<uint8_t> SpellPayload(uint32_t spell, float word) {
    std::vector<uint8_t> b(M::kWireSize, 0);
    PutF(b, 0x28, word);
    PutU(b, 0x4c, 1);
    PutU(b, 0x50, 2);
    PutU(b, 0x5c, spell);
    b[0x62] = 1;
    b[0x63] = M::kTypeSpell;
    return b;
}

// One whole flight as the client runs it: spawn at t = 1 ms, then one pass
// every `passUnits` (FUN_00445243 runs one per frame once 33 units passed).
// The target stands `gap` units north of the source (y), a hair east so the
// bearing is the table's index 0 (FUN_0046e42f's quadrant for x2 > x1).
// Returns the removal pass (0: none in maxPasses).
int PassesToHit(float gap, float word, uint8_t damped, uint32_t passUnits, int maxPasses = 1000) {
    M::Object o;
    o.w.word = word;
    o.w.damped = damped;
    o.w.type = M::kTypeSpell;
    o.w.spellId = 38;
    o.w.sourceId = 1;
    o.w.targetId = 2;
    M::SpellFields spell;
    spell.exists = true;
    spell.aeDuration = 0;
    spell.targetType = 1;
    spell.travelType = 3;  // 30000-unit life; no catch-up
    M::Entity source, target;
    source.exists = target.exists = true;
    target.pos[0] = gap;
    target.pos[1] = 1e-4f;
    uint32_t now = 1;
    if (!M::Spawn(o, now, &spell, source, target, 0)) return -1;
    for (int k = 1; k < maxPasses; ++k) {
        now += passUnits;
        if (M::Pass(o, now, target)) return k;
    }
    return 0;
}

}  // namespace

TEST(B1170MissileObject, DecodesTheServersPayload) {
    const auto b = ServerArrowPayload();
    M::Wire w;
    ASSERT_TRUE(M::DecodeWire(b.data(), b.size(), w));
    EXPECT_FLOAT_EQ(w.pos[0], -120.5f);
    EXPECT_FLOAT_EQ(w.pos[1], 33.25f);
    EXPECT_FLOAT_EQ(w.pos[2], 4.0f);
    EXPECT_FLOAT_EQ(w.word, 1.5f);      // +0x28, the thrust word
    EXPECT_FLOAT_EQ(w.heading, 200.0f); // +0x2c
    EXPECT_FLOAT_EQ(w.pitch, 125.0f);   // +0x30
    EXPECT_FLOAT_EQ(w.aim, 50.0f);      // +0x3c
    EXPECT_EQ(w.sourceId, 258u);
    EXPECT_EQ(w.targetId, 259u);
    EXPECT_EQ(w.word54, 8005u);
    EXPECT_EQ(w.word58, 125u);
    EXPECT_EQ(w.spellId, 16u);          // +0x5c: the server's unknown092
    EXPECT_EQ(w.damped, 7);             // +0x62: the server's skill
    EXPECT_EQ(w.type, 27);              // +0x63: never 9 from this server
    EXPECT_EQ(w.model, "IT10");
    EXPECT_FLOAT_EQ(w.vel[0], 0.0f);
}

TEST(B1170MissileObject, ShortMessageAndTinyVelocity) {
    auto b = ServerArrowPayload();
    M::Wire w;
    EXPECT_FALSE(M::DecodeWire(b.data(), M::kWireSize - 1, w));
    EXPECT_FALSE(M::DecodeWire(nullptr, M::kWireSize, w));
    // Components under 0.001 in magnitude are zeroed ([0x641088]).
    PutF(b, 0x1c, 0.0005f);
    PutF(b, 0x20, -0.0009f);
    PutF(b, 0x24, 0.002f);
    ASSERT_TRUE(M::DecodeWire(b.data(), b.size(), w));
    EXPECT_EQ(w.vel[0], 0.0f);
    EXPECT_EQ(w.vel[1], 0.0f);
    EXPECT_FLOAT_EQ(w.vel[2], 0.002f);
}

TEST(B1170MissileObject, TrigTablesAndAngles) {
    EXPECT_EQ(M::TableCos(0.0f), 1.0f);
    EXPECT_EQ(M::TableSin(128.0f), 1.0f);
    EXPECT_EQ(M::TableSin(256.9f), 0.0f);     // truncation, not rounding
    EXPECT_EQ(M::TableCos(-0.5f), 1.0f);      // _ftol2(-0.5) = 0
    EXPECT_EQ(M::TableCos(-1.5f), M::TableCos(511.0f));  // -1 & 0x1ff = 511
    EXPECT_EQ(M::TableCos(512.0f), 1.0f);     // & 0x1ff
    EXPECT_EQ(M::TableAtanIndex(0.0f), 0);
    EXPECT_EQ(M::TableAtanIndex(1.0f), 64);   // tan[64] = sin45 / cos45 = 1
    EXPECT_EQ(M::TableAtanIndex(1e30f), 127);
    EXPECT_FLOAT_EQ(M::WrapAngle(513.0f), 1.0f);
    EXPECT_FLOAT_EQ(M::WrapAngle(-1.0f), 511.0f);
    float s = 0.0f;
    EXPECT_FLOAT_EQ(M::AngleDiff(10.0f, 500.0f, &s), 22.0f);
    EXPECT_FLOAT_EQ(s, -22.0f);
    EXPECT_FLOAT_EQ(M::AngleDiff(10.0f, 100.0f, &s), 90.0f);
    EXPECT_FLOAT_EQ(s, 90.0f);
    // FUN_0046e42f: east = 90 degrees x 511.5 / 360; due north lands on 511.5.
    EXPECT_NEAR(M::Bearing(0.0f, 0.0f, 0.0f, 10.0f), 127.875f, 1e-3);
    EXPECT_NEAR(M::Bearing(0.0f, 0.0f, 10.0f, 0.0f), 511.5f, 1e-3);
    EXPECT_NEAR(M::Bearing(0.0f, 0.0f, -10.0f, 0.0f), 255.75f, 1e-3);
    EXPECT_FLOAT_EQ(M::DistanceXY(0.0f, 0.0f, 3.0f, 4.0f), 5.0f);
}

// refactor/B1163/scripts/missile_model.py's 24 rows (gap, word / damping,
// pass units -> passes): the client's homing + physics with the thrust word
// and +0x62 (non-zero: 0.625; zero: 0.85). 23 rows are the script's value.
// The 24th (9.05, 1.5 / 0.625, 100 units) is 7, not the script's 5: at
// dt = 2.0 the object overshoots the target on pass 3, and the script's 1-D
// loop stores the SIGNED gap (-2.09 < 2.0, "hit" on pass 5) where the client
// stores the XY distance, a magnitude (004768b1 call 0x439c41 /
// 004768b9 fstp dword ptr [eax + 0xff4]), so it turns back (the heading snaps
// to the bearing under 32 units, 00476939 / 0047694c) and hits on pass 7
// (refactor/B1170/README.md (B)).
TEST(B1170MissileObject, ReproducesTheModelsTwentyFourRows) {
    struct Row { float gap; float word; uint8_t damped; uint32_t pu; int passes; };
    const Row rows[] = {
        {9.05f, 1.5f, 1, 33, 7}, {9.05f, 1.5f, 1, 34, 7}, {9.05f, 1.5f, 1, 40, 7},
        {9.05f, 1.5f, 1, 50, 6}, {9.05f, 1.5f, 1, 67, 5}, {9.05f, 1.5f, 1, 100, 7},
        {9.05f, 1.0f, 0, 33, 8}, {9.05f, 1.0f, 0, 34, 8}, {9.05f, 1.0f, 0, 40, 7},
        {9.05f, 1.0f, 0, 50, 7}, {9.05f, 1.0f, 0, 67, 6}, {9.05f, 1.0f, 0, 100, 5},
        {7.55f, 1.5f, 1, 33, 7}, {7.55f, 1.5f, 1, 34, 6}, {7.55f, 1.5f, 1, 40, 6},
        {7.55f, 1.5f, 1, 50, 5}, {7.55f, 1.5f, 1, 67, 5}, {7.55f, 1.5f, 1, 100, 4},
        {7.55f, 1.0f, 0, 33, 7}, {7.55f, 1.0f, 0, 34, 7}, {7.55f, 1.0f, 0, 40, 7},
        {7.55f, 1.0f, 0, 50, 6}, {7.55f, 1.0f, 0, 67, 5}, {7.55f, 1.0f, 0, 100, 5},
    };
    for (const auto& r : rows)
        EXPECT_EQ(PassesToHit(r.gap, r.word, r.damped, r.pu), r.passes)
            << "gap " << r.gap << " word " << r.word << " +0x62 " << int(r.damped) << " pass " << r.pu;
}

TEST(B1170MissileObject, NoPassBefore33UnitsAndTheHitRule) {
    M::Object o;
    o.spawned = true;
    o.lastPass = 100;
    EXPECT_FALSE(M::PassDue(o, 132));
    EXPECT_TRUE(M::PassDue(o, 133));
    // The hit test reads the distance stored at the PREVIOUS pass, and needs
    // the target's actor (0047674d / 00476752).
    M::Entity target;
    target.exists = true;
    target.pos[0] = 1.0f;
    o.w.word = 1.5f;
    o.w.sourceId = 1;
    o.w.targetId = 2;
    o.stored = 1.9f;
    target.hasActor = false;
    M::Home(o, 200, target);
    EXPECT_FALSE(o.hit);
    target.hasActor = true;
    M::Home(o, 200, target);
    EXPECT_TRUE(o.hit);
    EXPECT_TRUE(M::RemovedAfterPass(o, 200));
    // The lifetime ([actor+0x68]): removed once the frame time passes it.
    M::Object l;
    l.lifeEnd = 500;
    EXPECT_FALSE(M::RemovedAfterPass(l, 500));
    EXPECT_TRUE(M::RemovedAfterPass(l, 501));
    // A zero word never homes (004766de .. 004766e8).
    M::Object z;
    z.w.word = 0.0f;
    z.stored = 1.0f;
    M::Home(z, 10, target);
    EXPECT_FALSE(z.hit);
}

TEST(B1170MissileObject, SpawnRules) {
    M::Entity source, target;
    source.exists = target.exists = true;
    target.pos[0] = 30.0f;
    target.pos[1] = 5.0f;
    target.pos[2] = 2.0f;
    M::SpellFields s859;
    s859.exists = true;
    s859.aeDuration = 5;
    s859.targetType = 8;
    // No-flight target type: on the target, word and velocity zero, stage 2,
    // life time() + field 18.
    M::Object o;
    o.w.type = M::kTypeSpell;
    o.w.spellId = R::kSpell859;
    o.w.word = 0.8f;
    o.w.vel[0] = 2.0f;
    ASSERT_TRUE(M::Spawn(o, 1000, &s859, source, target, 0));
    EXPECT_EQ(o.stage, 2);
    EXPECT_EQ(o.w.word, 0.0f);
    EXPECT_EQ(o.w.vel[0], 0.0f);
    EXPECT_FLOAT_EQ(o.w.pos[0], 30.0f);
    EXPECT_EQ(o.lifeEnd, 1005u);
    EXPECT_EQ(o.refreshAt, 3000u);
    // A flight type: stage 1; travel 0 with no field 18 lives 50 units.
    M::SpellFields s93;
    s93.exists = true;
    s93.targetType = 5;
    M::Object f;
    f.w.type = M::kTypeSpell;
    f.w.spellId = 93;
    ASSERT_TRUE(M::Spawn(f, 1000, &s93, source, target, 0));
    EXPECT_EQ(f.stage, 1);
    EXPECT_EQ(f.lifeEnd, 1050u);
    // Travel type 3 with no field 18 lives 30000.
    s93.travelType = 3;
    M::Object g;
    g.w.type = M::kTypeSpell;
    g.w.spellId = 38;
    ASSERT_TRUE(M::Spawn(g, 1000, &s93, source, target, 0));
    EXPECT_EQ(g.lifeEnd, 31000u);
    // Not a spell (the server's arrow, type 27): no stage, 120000 life,
    // one velocity step, homing refresh +500.
    M::Object a;
    a.w.type = 27;
    a.w.vel[1] = 0.5f;
    ASSERT_TRUE(M::Spawn(a, 1000, nullptr, source, target, 0));
    EXPECT_EQ(a.stage, 0);
    EXPECT_EQ(a.lifeEnd, 121000u);
    EXPECT_FLOAT_EQ(a.w.pos[1], 0.5f);
    EXPECT_EQ(a.refreshAt, 1500u);
    // Failures: no source entity; a spell id out of range or with no record.
    M::Object n;
    n.w.type = M::kTypeSpell;
    n.w.spellId = 38;
    M::Entity none;
    EXPECT_FALSE(M::Spawn(n, 1000, &s93, none, target, 0));
    EXPECT_TRUE(n.hit);
    M::Object r;
    r.w.type = M::kTypeSpell;
    r.w.spellId = 10000;
    EXPECT_FALSE(M::Spawn(r, 1000, &s93, source, target, 0));
    M::Object q;
    q.w.type = M::kTypeSpell;
    q.w.spellId = 38;
    EXPECT_FALSE(M::Spawn(q, 1000, nullptr, source, target, 0));
}

TEST(B1170MissileObject, NoFlightTargetTypes) {
    for (int t = 0; t < 0x30; ++t) {
        const bool expect = t == 8 || t == 0x15 || t == 0x16 || t == 0x17 || t == 0x1c;
        EXPECT_EQ(M::TargetTypeHasNoFlight(static_cast<uint8_t>(t)), expect) << t;
    }
}

// FUN_0047789d: only a type-9 object with an actor fires a Target stage at
// removal, and only for spells 54 and 859; 859's goes through the four
// early-outs and the 40-unit gate (spell_b1163_missile_rules.h).
TEST(B1170MissileObject, RemovalFiresAndEarlyOuts) {
    M::Object o;
    o.w.type = M::kTypeSpell;
    o.spawned = true;
    o.w.spellId = 0x36;
    EXPECT_EQ(M::RemovalFireFor(o), M::RemovalFire::AtPositionBack);
    o.w.pos[0] = 10.0f;
    o.w.vel[0] = 2.5f;
    float p[3];
    M::RemovalPosition(o, p);
    EXPECT_FLOAT_EQ(p[0], 7.5f);
    o.w.spellId = R::kSpell859;
    EXPECT_EQ(M::RemovalFireFor(o), M::RemovalFire::AtLocalPlayer);
    o.w.spellId = 38;
    EXPECT_EQ(M::RemovalFireFor(o), M::RemovalFire::None);
    o.w.spellId = 0x36;
    o.spawned = false;  // 00477947: no actor, no fire
    EXPECT_EQ(M::RemovalFireFor(o), M::RemovalFire::None);
    o.spawned = true;
    o.w.type = 27;      // 0047793d: not a spell object
    EXPECT_EQ(M::RemovalFireFor(o), M::RemovalFire::None);

    R::Spell859RemovalInputs in;
    in.actorObject = true;
    in.spellId = R::kSpell859;
    in.recordExists = true;
    in.targetType = 8;
    in.distXY = 40.0f;
    EXPECT_TRUE(R::Spell859RemovalFires(in));
    auto off = in; off.actorObject = false;  EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.recordExists = false;      EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.targetType = 0;            EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.spellId = 10000;           EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.distXY = 40.01f;           EXPECT_FALSE(R::Spell859RemovalFires(off));
}

// The flight of a real type-9 payload through Spawn + Pass: it ends in a hit
// at the target, never before its 33-unit passes.
TEST(B1170MissileObject, ASpellPayloadFliesToItsTarget) {
    const auto b = SpellPayload(38, 1.5f);
    M::Object o;
    ASSERT_TRUE(M::DecodeWire(b.data(), b.size(), o.w));
    M::SpellFields s;
    s.exists = true;
    s.targetType = 1;
    s.travelType = 3;
    M::Entity source, target;
    source.exists = target.exists = true;
    target.pos[0] = 20.0f;
    target.pos[1] = 15.0f;
    ASSERT_TRUE(M::Spawn(o, 1, &s, source, target, 0));
    EXPECT_EQ(o.stage, 1);
    uint32_t now = 1;
    int k = 0;
    while (k < 200) {
        now += 33;
        ++k;
        if (M::Pass(o, now, target)) break;
    }
    EXPECT_TRUE(o.hit);
    EXPECT_LT(M::DistanceXY(o.w.pos[0], o.w.pos[1], 20.0f, 15.0f), 6.0f);
    EXPECT_GT(k, 3);
}

TEST(B1170MissileObject, SpellsUsFields) {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    const std::string path = std::string(env ? env : EQ_CLIENT_DATA_PATH) + "/spells_us.txt";
    std::ifstream in(path);
    if (!in) GTEST_SKIP() << path << " not present";
    std::string line;
    int seen = 0;
    while (std::getline(in, line) && seen < 3) {
        uint32_t id = 0;
        M::SpellFields f;
        if (!M::ParseSpellsUsLine(line, id, f)) continue;
        // refactor/B1163/README.md (A): fields 18 / 98 / 122.
        if (id == 38) { EXPECT_EQ(f.aeDuration, 0); EXPECT_EQ(f.targetType, 1); EXPECT_EQ(f.travelType, 3); ++seen; }
        if (id == 93) { EXPECT_EQ(f.aeDuration, 0); EXPECT_EQ(f.targetType, 5); EXPECT_EQ(f.travelType, 0); ++seen; }
        if (id == 859) { EXPECT_EQ(f.aeDuration, 5); EXPECT_EQ(f.targetType, 8); EXPECT_EQ(f.travelType, 0); ++seen; }
    }
    EXPECT_EQ(seen, 3);
}
