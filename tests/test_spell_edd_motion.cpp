// B1151 (#471): the record's motion / spin / frame / flag words from
// spellsnew.edd (review/B1141/edd_fields_from_code.tsv rows; refactor/B1151/
// TRACE.md). Every rule is cited to its decompile site in
// refactor/B1151/README.md; the PARTIAL rows are marked there and are NOT
// asserted here beyond what the TSV proves.
//
// Three layers:
//   1. The reader decodes every motion word (a synthetic 416-byte entry).
//   2. The pure rules in the headers (frame banding, the +0x198 draw rule,
//      the spin angle, the +0x178 alignment axis dispatch) -- deterministic,
//      no GL.
//   3. The sim: each motion term (axis-2/3 velocity + accel, radial with the
//      shape normal, swirl, gravity, drift, spin, +0x74 follow) driven through
//      the real SimulationWorker, read back off the renderBuffer particle.
//
// The census (every distribution summing to the record count) is a script --
// refactor/B1151/edd_census.py, output pasted into the README (the B1150
// edd_shapes.py pattern) -- AND a C++ net: CensusOfTheMotionWords walks all
// 2,118 file entries through the zero-based accessor motionAt(), exactly
// B1150's CensusOfTheShapeWord walking shapeAt() (test_spell_spawn_shapes).
// The visual gate (tour-7 + the 40-record sweep + the motion signatures) is
// the end-to-end net; see the README.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "client/spell/spells_new_edd_reader.h"
#include "client/spell/spell_fx_timing.h"
#include "client/graphics/environment/spell_spawn_shape.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/simulation_worker.h"
#include "particle_test_seed.h"

namespace {

using EQ::SpellsNewEddMotion;
namespace Env = EQT::Graphics::Environment;

// ---------------------------------------------------------------------------
// 1. Reader decode
// ---------------------------------------------------------------------------

std::vector<uint8_t> blankEntry() { return std::vector<uint8_t>(416, 0); }

void putFloat(std::vector<uint8_t>& e, size_t off, float v) {
    std::memcpy(&e[off], &v, 4);
}

void putInt(std::vector<uint8_t>& e, size_t off, int v) {
    uint32_t x = static_cast<uint32_t>(v);
    std::memcpy(&e[off], &x, 4);
}

TEST(SpellEddMotionReader, DecodesEveryMotionWord) {
    auto e = blankEntry();
    putInt(e, 0x60, 1);    // orient
    putInt(e, 0x64, 1);    // noDepthWrite
    putInt(e, 0x70, 2);    // spriteMode
    putInt(e, 0x74, 1);    // follow
    putFloat(e, 0xEC, 1.5f); putFloat(e, 0xF0, 2.5f); putFloat(e, 0xF4, 0.5f);
    putFloat(e, 0xF8, 3.5f); putFloat(e, 0xFC, 4.5f); putFloat(e, 0x100, 0.25f);
    putFloat(e, 0x104, 5.5f); putFloat(e, 0x108, 6.5f); putFloat(e, 0x10C, -0.25f);
    putFloat(e, 0x110, 7.5f); putFloat(e, 0x114, 8.5f); putFloat(e, 0x118, 0.125f);
    putFloat(e, 0x11C, 10.5f); putFloat(e, 0x120, 11.5f); putFloat(e, 0x124, 2.0f);
    putFloat(e, 0x128, 9.5f);   // gravity
    putFloat(e, 0x12C, 12.5f);  // driftX
    putInt(e, 0x174, 1);    // randomStartSpin
    putFloat(e, 0x138, 20.0f);  // spinMin
    putFloat(e, 0x188, 40.0f);  // spinMax
    putInt(e, 0x178, 5);    // spinAlign
    putInt(e, 0x130, 6);    // frameCount
    putFloat(e, 0x134, 12.0f);  // frameRate
    putInt(e, 0x198, 1);    // drawNegativeRadius

    const SpellsNewEddMotion m = EQ::decodeSpellsNewEddMotion(e.data());
    EXPECT_TRUE(m.orientToAttach);
    EXPECT_TRUE(m.noDepthWrite);
    EXPECT_EQ(m.spriteMode, 2);
    EXPECT_TRUE(m.follow);
    EXPECT_FLOAT_EQ(m.upMin, 1.5f);
    EXPECT_FLOAT_EQ(m.upMax, 2.5f);
    EXPECT_FLOAT_EQ(m.upAccel, 0.5f);
    EXPECT_FLOAT_EQ(m.axis2Min, 3.5f);
    EXPECT_FLOAT_EQ(m.axis2Max, 4.5f);
    EXPECT_FLOAT_EQ(m.axis2Accel, 0.25f);
    EXPECT_FLOAT_EQ(m.axis3Min, 5.5f);
    EXPECT_FLOAT_EQ(m.axis3Max, 6.5f);
    EXPECT_FLOAT_EQ(m.axis3Accel, -0.25f);
    EXPECT_FLOAT_EQ(m.radialMin, 7.5f);
    EXPECT_FLOAT_EQ(m.radialMax, 8.5f);
    EXPECT_FLOAT_EQ(m.radialAccel, 0.125f);
    EXPECT_FLOAT_EQ(m.swirlMin, 10.5f);
    EXPECT_FLOAT_EQ(m.swirlMax, 11.5f);
    EXPECT_FLOAT_EQ(m.swirlAccel, 2.0f);
    EXPECT_FLOAT_EQ(m.gravity, 9.5f);
    EXPECT_FLOAT_EQ(m.driftX, 12.5f);
    EXPECT_TRUE(m.randomStartSpin);
    EXPECT_FLOAT_EQ(m.spinMin, 20.0f);
    EXPECT_FLOAT_EQ(m.spinMax, 40.0f);
    EXPECT_EQ(m.spinAlign, 5);
    EXPECT_EQ(m.frameCount, 6);
    EXPECT_FLOAT_EQ(m.frameRate, 12.0f);
    EXPECT_TRUE(m.drawNegativeRadius);
}

TEST(SpellEddMotionReader, ABlankEntryIsAllZero) {
    const auto e = blankEntry();
    const SpellsNewEddMotion m = EQ::decodeSpellsNewEddMotion(e.data());
    EXPECT_FALSE(m.orientToAttach);
    EXPECT_FALSE(m.noDepthWrite);
    EXPECT_EQ(m.spriteMode, 0);
    EXPECT_FALSE(m.follow);
    EXPECT_FALSE(m.randomStartSpin);
    EXPECT_EQ(m.spinAlign, 0);
    EXPECT_EQ(m.frameCount, 0);
    EXPECT_FALSE(m.drawNegativeRadius);
    EXPECT_FLOAT_EQ(m.upMin, 0.0f);
    EXPECT_FLOAT_EQ(m.gravity, 0.0f);
    EXPECT_FLOAT_EQ(m.driftX, 0.0f);
}

// ---------------------------------------------------------------------------
// The shipped census -- the zero-based accessor (motionAt) is the census's
// window onto every file entry (entry 0 included; the 1-based slot
// accessors reject 0 by design, since spellsnew.eff's slot numbers are
// 1-based with 0 = empty slot). B1150's pattern: the same test shape as
// test_spell_spawn_shapes's CensusOfTheShapeWord (shapeAt), the int-word
// distributions from the README "The census" (edd_census.py over the same
// file).
// ---------------------------------------------------------------------------

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

TEST(SpellEddMotionShipped, CensusOfTheMotionWords) {
    EQ::SpellsNewEddReader reader;
    if (!reader.loadFile(clientDir() + "/spellsnew.edd"))
        GTEST_SKIP() << "spellsnew.edd not under " << clientDir();

    int orient[2] = {0, 0}, noDepth[2] = {0, 0}, mode[3] = {0, 0, 0},
        follow[2] = {0, 0}, frameCount[3] = {0, 0, 0}, startSpin[2] = {0, 0},
        spinAlign[7] = {0, 0, 0, 0, 0, 0, 0}, drawNeg[2] = {0, 0};
    for (size_t k = 0; k < reader.emitterCount(); ++k) {
        const EQ::SpellsNewEddMotion& m = reader.motionAt(k);
        ++orient[m.orientToAttach ? 1 : 0];
        ++noDepth[m.noDepthWrite ? 1 : 0];
        if (m.spriteMode < 0 || m.spriteMode > 2)
            ADD_FAILURE() << "entry " << k << " spriteMode " << m.spriteMode;
        else
            ++mode[m.spriteMode];
        ++follow[m.follow ? 1 : 0];
        if (m.frameCount == 1) ++frameCount[0];
        else if (m.frameCount == 4) ++frameCount[1];
        else if (m.frameCount == 16) ++frameCount[2];
        else
            ADD_FAILURE() << "entry " << k << " frameCount " << m.frameCount;
        ++startSpin[m.randomStartSpin ? 1 : 0];
        if (m.spinAlign < 0 || m.spinAlign > 6)
            ADD_FAILURE() << "entry " << k << " spinAlign " << m.spinAlign;
        else
            ++spinAlign[m.spinAlign];
        ++drawNeg[m.drawNegativeRadius ? 1 : 0];
    }
    // README "The census" (edd_census.py); every distribution sums to 2118.
    EXPECT_EQ(orient[0], 1728);   EXPECT_EQ(orient[1], 390);
    EXPECT_EQ(noDepth[0], 206);   EXPECT_EQ(noDepth[1], 1912);
    EXPECT_EQ(mode[0], 1911);     EXPECT_EQ(mode[1], 205);   EXPECT_EQ(mode[2], 2);
    EXPECT_EQ(follow[0], 1900);   EXPECT_EQ(follow[1], 218);
    EXPECT_EQ(frameCount[0], 855); EXPECT_EQ(frameCount[1], 199); EXPECT_EQ(frameCount[2], 1064);
    EXPECT_EQ(startSpin[0], 2098); EXPECT_EQ(startSpin[1], 20);
    EXPECT_EQ(spinAlign[0], 2116); EXPECT_EQ(spinAlign[3], 1);   EXPECT_EQ(spinAlign[6], 1);
    EXPECT_EQ(drawNeg[0], 2117);   EXPECT_EQ(drawNeg[1], 1);
    int sum = orient[0] + orient[1];
    EXPECT_EQ(sum, 2118);
}

// ---------------------------------------------------------------------------
// 2. Pure rules
// ---------------------------------------------------------------------------

// +0x130 N / +0x134 fps (TRACE.md (c)): frame = ftol(age * fps) % N; N == 1
// and N > 16 are the whole texture (frame 0); 2..4 a 2x2, 5..8 4x2, 9..16 4x4
// -- all row-major, so the frame is the cell offset.
TEST(SpellEddFrame, BandsFollowTheNTwelvefpsRule) {
    const float fps = 10.0f;
    // N == 1: the whole texture, held all life.
    Env::UnifiedParticle p1;
    p1.age = 0.9f;
    p1.eddFrameCount = 1;
    p1.eddFrameRate = fps;
    EXPECT_EQ(p1.eddFrame(), 0);

    // N == 2 (2x2): at age 0.05s, frame = floor(0.05 * 10) % 2 = 0.
    Env::UnifiedParticle p2;
    p2.age = 0.05f;
    p2.eddFrameCount = 2;
    p2.eddFrameRate = fps;
    EXPECT_EQ(p2.eddFrame(), 0);
    p2.age = 0.15f;  // floor(1.5) % 2 = 1
    EXPECT_EQ(p2.eddFrame(), 1);
    p2.age = 0.35f;  // floor(3.5) % 2 = 1
    EXPECT_EQ(p2.eddFrame(), 1);

    // N == 5 (4x2): at age 0.2s, frame = floor(2.0) % 5 = 2.
    Env::UnifiedParticle p5;
    p5.age = 0.2f;
    p5.eddFrameCount = 5;
    p5.eddFrameRate = fps;
    EXPECT_EQ(p5.eddFrame(), 2);

    // N == 16 (4x4): at age 1.0s, frame = floor(10.0) % 16 = 10.
    Env::UnifiedParticle p16;
    p16.age = 1.0f;
    p16.eddFrameCount = 16;
    p16.eddFrameRate = fps;
    EXPECT_EQ(p16.eddFrame(), 10);

    // N > 16: the whole texture again.
    Env::UnifiedParticle p99;
    p99.age = 0.5f;
    p99.eddFrameCount = 99;
    p99.eddFrameRate = fps;
    EXPECT_EQ(p99.eddFrame(), 0);
}

// +0x198 (draw site 0x10062e5c): the particle is not drawn when the word is
// not 1 and r < 0 (inward radial motion past the axis).
TEST(SpellEddDraw, InwardRadialMotionPastTheAxisIsNotDrawn) {
    // No motion: always drawn.
    Env::UnifiedParticle plain;
    plain.eddR = -5.0f;
    EXPECT_TRUE(plain.eddDraws());

    // Motion, r >= 0: drawn.
    Env::UnifiedParticle out;
    out.setEddMotion(true);
    out.eddR = 0.0f;
    EXPECT_TRUE(out.eddDraws());

    // Motion, r < 0, word != 1: NOT drawn.
    Env::UnifiedParticle in0;
    in0.setEddMotion(true);
    in0.eddR = -0.25f;
    in0.eddDrawNegativeRadius = 0;
    EXPECT_FALSE(in0.eddDraws());

    // Motion, r < 0, word == 1: still drawn.
    Env::UnifiedParticle in1;
    in1.setEddMotion(true);
    in1.eddR = -0.25f;
    in1.eddDrawNegativeRadius = 1;
    EXPECT_TRUE(in1.eddDraws());
}

// spin = age * spin + start (draws 0x100634c1/0x100638e8), in 512ths of a
// turn (512 = full turn).
TEST(SpellEddSpin, AngleIsAgeTimesSpinPlusStart) {
    Env::UnifiedParticle p;
    p.age = 2.5f;
    p.eddSpin = 100.0f;   // 512ths of a turn per second
    p.eddStart512 = 64.0f;
    EXPECT_FLOAT_EQ(p.eddSpinAngle512(), 2.5f * 100.0f + 64.0f);
}

// +0x178 axis dispatch (the TSV row: 1,2, default=3; 4..6 = negated 1..3).
// PARTIAL: which attach-matrix column is X/Y/Z is untraced; fable uses the
// actor-frame convention A = the facing, B = up, C = the perpendicular.
TEST(SpellEddAlign, DispatchSelectsAndNegatesTheAxis) {
    const glm::vec3 A(1.0f, 0.0f, 0.0f);
    const glm::vec3 B(0.0f, 1.0f, 0.0f);
    const glm::vec3 C(0.0f, 0.0f, 1.0f);
    EXPECT_EQ(Env::eddAlignAxis(1, A, B, C), A);
    EXPECT_EQ(Env::eddAlignAxis(2, A, B, C), B);
    EXPECT_EQ(Env::eddAlignAxis(3, A, B, C), C);  // the default
    EXPECT_EQ(Env::eddAlignAxis(4, A, B, C), -A);
    EXPECT_EQ(Env::eddAlignAxis(5, A, B, C), -B);
    EXPECT_EQ(Env::eddAlignAxis(6, A, B, C), -C);
    EXPECT_EQ(Env::eddAlignAxis(7, A, B, C), C);  // > 6 falls to the default
}

// The +0x178 angle is the 2D screen-space angle between the projected attach
// axis and the particle's screen position, in 512ths (0x1006235f/0x1006246f).
// A zero angle is 0; a quarter turn on screen is 128.
TEST(SpellEddAlign, ScreenAngleIsMeasuredIn512ths) {
    const glm::vec3 viewRight(1.0f, 0.0f, 0.0f);
    const glm::vec3 viewUp(0.0f, 1.0f, 0.0f);
    // axis along the particle's screen direction -> 0.
    EXPECT_FLOAT_EQ(Env::eddAlignAngle512(glm::vec3(1.0f, 0.0f, 0.0f),
                                         glm::vec3(1.0f, 0.0f, 0.0f),
                                         viewRight, viewUp), 0.0f);
    // axis 90 deg from the particle's screen direction -> 128.
    const float quarter = Env::eddAlignAngle512(glm::vec3(0.0f, 1.0f, 0.0f),
                                                glm::vec3(1.0f, 0.0f, 0.0f),
                                                viewRight, viewUp);
    EXPECT_NEAR(quarter, 128.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// 3. Sim: each motion term through the real worker
// ---------------------------------------------------------------------------

// One eddMotion particle (burst 1, a point shape, the record's basis) run for
// `frames` frames at 60 Hz, returning the single live particle.
Env::UnifiedParticle runMotionTerm(const Env::EmitterConfig::EddMotion& motion,
                                   const Env::EmitterConfig::EddShape& shape,
                                   int frames) {
    Env::EmitterConfig c;
    c.spawnRate = 0.0f;
    c.burstCount = 1;
    c.ticksPerSecond = 0.0f;
    c.emitterLifetime = 30.0f;
    c.lifetimeMin = c.lifetimeMax = 30.0f;
    c.velocityBase = glm::vec3(0.0f);
    c.velocitySpread = glm::vec3(0.0f);
    c.eddShape = shape;
    c.eddMotion = motion;
    c.eddMotion.active = true;
    c.eddShape.active = true;

    Env::SpellEffectDef def;
    def.name = "edd_motion_term";
    Env::SpellEmitterDef ed;
    ed.config = c;
    ed.trigger = Env::SpellTrigger::IMMEDIATE;
    def.emitters.push_back(std::move(ed));

    EQT::Graphics::SimulationWorker worker;
    EQT::Graphics::SimulationOutput output;
    std::vector<Env::UnifiedParticle> alive;
    for (int f = 0; f < frames; ++f) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 256;
        if (f == 0) {
            EQT::Graphics::ParticleCommandData cmd;
            cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
            cmd.spellDef = def;
            cmd.preAssignedEffectID = 1;
            pi.commands.push_back(cmd);
        }
        worker.computeOnce(input, output);
        if (output.particleOutput.valid) alive = output.particleOutput.renderBuffer;
    }
    EXPECT_EQ(alive.size(), 1u);
    return alive.empty() ? Env::UnifiedParticle{} : alive.front();
}

Env::EmitterConfig::EddShape pointShape() {
    Env::EmitterConfig::EddShape s;
    s.active = true;
    s.shape = 0;  // point: the shape point is the origin, so the motion is clean
    return s;
}

Env::EmitterConfig::EddMotion blankMotion() {
    Env::EmitterConfig::EddMotion m;
    m.active = true;
    m.scale = 1.0f;
    return m;
}

const int kFrames = 60;  // 1 s at 60 Hz

// axis-2 velocity (+0xF8/+0xFC): the particle moves along basis 2 (+X) at the
// drawn speed; the offset is v * T (the accel word is 0).
TEST(SpellEddMotionSim, Axis2VelocityMovesAlongBasis2) {
    auto m = blankMotion();
    m.axis2Min = m.axis2Max = 3.0f;
    m.axis2Accel = 0.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    EXPECT_TRUE(p.hasEddMotion());
    // basis 2 is +X; the offset accumulates v * dt each update, and so does
    // age, so eddO2 = v * age exactly (independent of the exact frame count).
    EXPECT_NEAR(p.eddO2, 3.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.position.x, 3.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.position.y, 0.0f, 1e-3f);
    EXPECT_NEAR(p.position.z, 0.0f, 1e-3f);
}

// axis-2 accel (+0x100): v starts at 0 and grows by a each frame; the offset
// is the discrete sum a * dt^2 * N(N+1)/2 over the particle's own updates.
TEST(SpellEddMotionSim, Axis2AccelIntegratesQuadratically) {
    auto m = blankMotion();
    m.axis2Min = m.axis2Max = 0.0f;
    m.axis2Accel = 2.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    const float dt = 1.0f / 60.0f;
    const int N = static_cast<int>(std::lround(p.age / dt));
    const float expect = 2.0f * dt * dt * (static_cast<float>(N) * (N + 1)) / 2.0f;
    EXPECT_NEAR(p.eddO2, expect, 1e-3f);
    EXPECT_NEAR(p.position.x, expect, 1e-3f);
}

// axis-3 velocity (+0x104/+0x108): the particle moves along basis 3 (+Z).
TEST(SpellEddMotionSim, Axis3VelocityMovesAlongBasis3) {
    auto m = blankMotion();
    m.axis3Min = m.axis3Max = 2.0f;
    m.axis3Accel = 0.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    EXPECT_NEAR(p.eddO3, 2.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.position.z, 2.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.position.x, 0.0f, 1e-3f);
}

// radial velocity (+0x110/+0x114) with the DEFAULT normal (no shape normal):
// the normal is (1, 0) in (r, h), so the motion is pure radial and, from the
// spawn theta of 0, lands along basis 2 (+X).
TEST(SpellEddMotionSim, RadialVelocityGrowsTheRadius) {
    auto m = blankMotion();
    m.radialMin = m.radialMax = 4.0f;
    m.radialAccel = 0.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    EXPECT_NEAR(p.eddR, 4.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.eddH, 0.0f, 1e-3f);  // the default normal has no height
    // theta is 0 at spawn, so the radius sits along basis 2 (+X).
    EXPECT_NEAR(p.position.x, 4.0f * p.age, 1e-3f);
}

// radial velocity with the SHAPE normal (the +0x110 rule): a sphere-surface
// spawn carries the outward normal, split into (r, h). The stored normal is a
// unit vector split, and the radial velocity moves the particle.
TEST(SpellEddMotionSim, RadialVelocityUsesTheShapeNormal) {
    auto m = blankMotion();
    m.radialMin = m.radialMax = 5.0f;
    m.radialAccel = 0.0f;
    Env::EmitterConfig::EddShape s;
    s.active = true;
    s.shape = 5;  // sphere surface: carries the outward normal
    s.dimX = 1.0f;
    const auto p = runMotionTerm(m, s, kFrames);
    // The normal split is a unit vector: eddNH^2 + eddNR^2 == 1.
    EXPECT_NEAR(p.eddNH * p.eddNH + p.eddNR * p.eddNR, 1.0f, 1e-3f);
    // The radial velocity moved the particle (r grew from the spawn radius).
    EXPECT_GT(p.eddR, 0.0f);
}

// swirl (+0x11C/+0x120): theta (512ths) accumulates the swirl speed each
// frame; 512 = a full turn.
TEST(SpellEddMotionSim, SwirlAccumulatesTheta) {
    auto m = blankMotion();
    m.swirlMin = m.swirlMax = 512.0f;  // one full turn per second
    m.swirlAccel = 0.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    // theta = 0 at spawn (the point shape), then += w * dt each update:
    // theta = w * age (512ths; 512 = one full turn).
    EXPECT_NEAR(p.eddTheta512, 512.0f * p.age, 3.0f);
    EXPECT_GT(p.eddTheta512, 400.0f);  // ~one full turn over the life
}

// world gravity (+0x128): g accumulates the accel each frame and the particle
// drops (EQ z-up -> Irrlicht -y). The drop is the discrete free-fall sum.
TEST(SpellEddMotionSim, GravityDropsTheParticle) {
    auto m = blankMotion();
    m.gravity = 4.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    const float dt = 1.0f / 60.0f;
    const int N = static_cast<int>(std::lround(p.age / dt));
    const float expect = 4.0f * dt * dt * (static_cast<float>(N) * (N + 1)) / 2.0f;
    EXPECT_NEAR(p.eddGy, expect, 1e-3f);
    EXPECT_NEAR(p.position.y, -expect, 1e-3f);  // EQ z-up -> Irrlicht -y
    EXPECT_LT(p.position.y, 0.0f);
}

// +X drift (+0x12C): the particle's x grows linearly at the drift speed.
TEST(SpellEddMotionSim, DriftMovesTheParticleInPositiveX) {
    auto m = blankMotion();
    m.driftX = 6.0f;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    EXPECT_NEAR(p.eddDx, 6.0f * p.age, 1e-3f);
    EXPECT_NEAR(p.position.x, 6.0f * p.age, 1e-3f);
}

// spin (+0x138/+0x188): the per-particle spin (512ths/s) is stored and the
// angle grows with age.
TEST(SpellEddMotionSim, SpinIsStoredAndGrowsWithAge) {
    auto m = blankMotion();
    m.spinMin = m.spinMax = 100.0f;
    m.randomStartSpin = false;
    const auto p = runMotionTerm(m, pointShape(), kFrames);
    EXPECT_FLOAT_EQ(p.eddSpin, 100.0f);
    EXPECT_FLOAT_EQ(p.eddStart512, 0.0f);
    EXPECT_NEAR(p.eddSpinAngle512(), p.age * 100.0f, 1e-2f);
}

// +0x74 follow: the particle re-copies its origin from the emitter each frame,
// so it tracks the emitter's translation while holding its local offset. The
// emitter (attached to caster 1) travels +X at 0.5 units/frame; the local
// offset is in +Z (axis-3 velocity), so the two axes separate: a following
// particle's X is the emitter's X, a non-following one's X stays ~0.
TEST(SpellEddMotionSim, FollowTracksTheMovingEmitter) {
    Env::EmitterConfig c;
    c.spawnRate = 0.0f;
    c.burstCount = 1;
    c.ticksPerSecond = 0.0f;
    c.emitterLifetime = 30.0f;
    c.lifetimeMin = c.lifetimeMax = 30.0f;
    c.velocityBase = glm::vec3(0.0f);
    c.velocitySpread = glm::vec3(0.0f);
    auto m = blankMotion();
    m.follow = true;
    m.axis3Min = m.axis3Max = 2.0f;  // the local offset, in +Z
    m.axis3Accel = 0.0f;
    auto s = pointShape();
    c.eddShape = s;
    c.eddMotion = m;

    Env::SpellEffectDef def;
    def.name = "edd_follow";
    Env::SpellEmitterDef ed;
    ed.config = c;
    ed.trigger = Env::SpellTrigger::IMMEDIATE;
    def.emitters.push_back(std::move(ed));

    EQT::Graphics::SimulationWorker worker;
    EQT::Graphics::SimulationOutput output;
    Env::UnifiedParticle p;
    const int frames = 30;
    for (int f = 0; f < frames; ++f) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 256;
        pi.entityPositions[1] = glm::vec3(0.5f * f, 0.0f, 0.0f);  // moving caster
        if (f == 0) {
            EQT::Graphics::ParticleCommandData cmd;
            cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
            cmd.spellDef = def;
            cmd.casterID = 1;  // attach the emitter to caster 1
            cmd.preAssignedEffectID = 1;
            pi.commands.push_back(cmd);
        }
        worker.computeOnce(input, output);
        if (output.particleOutput.valid && !output.particleOutput.renderBuffer.empty())
            p = output.particleOutput.renderBuffer.front();
    }
    EXPECT_EQ(1u, output.particleOutput.renderBuffer.size());
    // The particle tracked the emitter's +X travel (within a frame of lag).
    EXPECT_GT(p.position.x, 0.5f * (frames - 1) - 1.0f);
    EXPECT_LT(p.position.x, 0.5f * frames + 1.0f);
    // and held its local +Z offset (axis-3 velocity * its own age).
    EXPECT_NEAR(p.position.z, 2.0f * p.age, 1e-2f);
}

// B1145 removal: the config-motion time-scaling (oldLife / L on velocityBase)
// is kept for non-record spells and skipped for record ones (the record's
// motion words drive the extent, so scaling the config motion under them would
// double-drive it). Same config through applySlotEmitter, only shape/motion
// differ: non-record scales, record does not.
TEST(SpellEddMotionB1145, RecordSpellsSkipTheConfigMotionScaling) {
    auto makeDef = []() {
        Env::SpellEffectDef d;
        d.name = "b1145";
        Env::SpellEmitterDef e;
        e.config.velocityBase = glm::vec3(2.0f, 2.0f, 2.0f);
        e.config.lifetimeMin = e.config.lifetimeMax = 1.0f;  // oldLife = 1.0
        e.config.burstCount = 1;
        d.emitters.push_back(std::move(e));
        return d;
    };
    EQ::SpellFxSlotTiming t;
    t.start = 0.0f;
    t.run = 1.0f;    // window = 1.0
    t.lifetime = 0.5f;  // L
    t.burst = 1;

    // Non-record: k = oldLife / L = 1.0 / 0.5 = 2.0 -> velocityBase * 2.
    auto dNon = makeDef();
    Env::SpellPresets::applySlotEmitter({&dNon}, t, nullptr, 0.0f, 1.0f, nullptr, nullptr);
    EXPECT_NEAR(dNon.emitters[0].config.velocityBase.x, 4.0f, 1e-4f);

    // Record: the scaling is skipped -> velocityBase unchanged.
    auto dRec = makeDef();
    EQ::SpellsNewEddShape shape;
    EQ::SpellsNewEddMotion motion;
    Env::SpellPresets::applySlotEmitter({&dRec}, t, nullptr, 0.0f, 1.0f, &shape, &motion);
    EXPECT_NEAR(dRec.emitters[0].config.velocityBase.x, 2.0f, 1e-4f);
    // and the record's motion words landed on the emitter config.
    EXPECT_TRUE(dRec.emitters[0].config.eddMotion.active);
    EXPECT_TRUE(dRec.emitters[0].config.eddShape.active);
}

}  // namespace
