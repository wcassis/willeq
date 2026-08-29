// T310 Leg A -- the first-person camera's numbers, settable at runtime.
//
// WHAT THIS TEST IS FOR. The unit's whole point is that solving the camera
// against the official client must not cost a rebuild per candidate value, and
// its whole RISK is that making the numbers settable quietly changes one of
// them. So this file pins, in order:
//
//   1. EVERY DEFAULT IS BYTE-IDENTICAL to the constant it replaced. A run with
//      no override renders what it rendered before, or this fails.
//   2. The store round-trips: set, read back, reset, overridden flags.
//   3. The parameters actually REACH the formulas -- a tunable nothing reads
//      is exactly the trap this unit exists to remove (and two of the nine
//      are honestly declared advisory rather than pretended live).
//   4. /fpcam is reachable from BOTH command paths: the renderer if-chain
//      (in-game chat) and the catalog gate that a --script / console run
//      passes through (application.cpp:1231 -- without the catalog row the
//      command is reported registry=none and never queued).
//   5. --fpcam parses at startup, so a sweep can launch a fresh process per
//      candidate instead of typing into a running one.
//
// The bare-IrrlichtRenderer idiom (a default-constructed, never-Init()'d
// renderer plus a minimal fake bridge) is copied from
// tests/test_irrlicht_camera_command.cpp -- see that file's header for why it
// needs no Irrlicht device.

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "client/application.h"
#include "client/bridge/game_state_bridge.h"
#include "client/graphics/first_person_camera.h"
#include "client/graphics/first_person_eye.h"
#include "client/graphics/first_person_tunables.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/help/renderer_command_catalog.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& l : lines) {
        if (l.find(needle) != std::string::npos) return true;
    }
    return false;
}

// The store is a process global. Every test in this binary must start and end
// at the compiled defaults or the "defaults are unchanged" assertions become
// order-dependent -- which is precisely the bug class this file guards.
class FpTunablesTest : public ::testing::Test {
protected:
    void SetUp() override { fpTunableResetAll(); }
    void TearDown() override { fpTunableResetAll(); }
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. DEFAULTS. This is the constraint the plan states twice: "Do NOT change
//    any default."
// ---------------------------------------------------------------------------

TEST_F(FpTunablesTest, EveryDefaultEqualsTheConstantItReplaced) {
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), kEyeForwardOffsetUnits);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeHeightFractionOfSkull), kEyeHeightFractionOfSkull);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::CrownAboveEyeUnits), kCrownAboveEyeUnits);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeHeightFractionOfStandingHeight),
                    kEyeHeightFractionOfStandingHeight);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::MaxDownPitchDegrees), kFirstPersonMaxDownPitchDegrees);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::MaxUpPitchDegrees), kFirstPersonMaxUpPitchDegrees);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::PitchDownDegrees), kFirstPersonPitchDownDegrees);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), kFirstPersonHorizontalFovDegrees);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::NearPlaneUnits), kWorldNearPlaneUnits);
}

// Belt and braces: the LITERAL values, so a sweep that edits the constant AND
// this table in the same motion still trips something.
//
// T310 Leg C moved two of them, which is what this leg exists to do: the eye
// height was SOLVED against the official client's own frames (skull fraction
// 0.574 -> 0.65, and the crown-to-eye distance that follows, 0.60 -> 0.49).
// The other seven are unchanged and were CONFIRMED by the same oracle sweeps
// -- eyeForwardOffset's curve minimises at 0.0 and hfov's at 62.8.
TEST_F(FpTunablesTest, TheDefaultsAreTodaysMeasuredNumbers) {
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.0f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeHeightFractionOfSkull), 0.65f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::CrownAboveEyeUnits), 0.49f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeHeightFractionOfStandingHeight), 0.925f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::MaxDownPitchDegrees), 94.9f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::MaxUpPitchDegrees), 89.0f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::PitchDownDegrees), 3.65f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), 62.8f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::NearPlaneUnits), 1.0f);
}

TEST_F(FpTunablesTest, NothingIsOverriddenOnAFreshStore) {
    for (int i = 0; i < kFpTunableCount; ++i) {
        EXPECT_FALSE(fpTunableIsOverridden(i)) << fpTunableSpecs()[i].name;
        EXPECT_FLOAT_EQ(fpTunableValue(i), fpTunableSpecs()[i].defaultValue);
    }
}

TEST_F(FpTunablesTest, SpecTableIsWellFormedAndIndexedByTheEnum) {
    const FpTunableSpec* specs = fpTunableSpecs();
    std::set<std::string> names;
    for (int i = 0; i < kFpTunableCount; ++i) {
        ASSERT_NE(specs[i].name, nullptr);
        EXPECT_TRUE(names.insert(specs[i].name).second) << "duplicate name " << specs[i].name;
        EXPECT_STRNE(specs[i].units, "");
        EXPECT_STRNE(specs[i].frame, "") << specs[i].name << " must state its frame";
        EXPECT_STRNE(specs[i].help, "");
        // The enum is the index into this table; a reordering that broke that
        // would silently write the wrong parameter.
        EXPECT_EQ(fpTunableIndexByName(specs[i].name), i);
    }
    EXPECT_EQ(fpTunableIndexByName("eyeForwardOffset"),
              static_cast<int>(FpTunable::EyeForwardOffsetUnits));
    EXPECT_EQ(fpTunableIndexByName("nearPlane"),
              static_cast<int>(FpTunable::NearPlaneUnits));
}

// ---------------------------------------------------------------------------
// 2. ROUND TRIP
// ---------------------------------------------------------------------------

TEST_F(FpTunablesTest, SetThenResetRoundTripsEveryParameter) {
    for (int i = 0; i < kFpTunableCount; ++i) {
        const float def = fpTunableSpecs()[i].defaultValue;
        const float probe = def + 1.25f;
        ASSERT_TRUE(fpTunableSet(i, probe)) << fpTunableSpecs()[i].name;
        EXPECT_FLOAT_EQ(fpTunableValue(i), probe);
        EXPECT_TRUE(fpTunableIsOverridden(i));
        fpTunableReset(i);
        EXPECT_FLOAT_EQ(fpTunableValue(i), def);
        EXPECT_FALSE(fpTunableIsOverridden(i));
    }
}

TEST_F(FpTunablesTest, NameLookupTakesEitherSpellingAndIsCaseInsensitive) {
    EXPECT_EQ(fpTunableIndexByName("EYEFORWARDOFFSET"),
              static_cast<int>(FpTunable::EyeForwardOffsetUnits));
    // The constexpr's own name works too, so a reader who only knows the
    // header comment can drive it.
    EXPECT_EQ(fpTunableIndexByName("kEyeForwardOffsetUnits"),
              static_cast<int>(FpTunable::EyeForwardOffsetUnits));
    EXPECT_EQ(fpTunableIndexByName("kFirstPersonMaxDownPitchDegrees"),
              static_cast<int>(FpTunable::MaxDownPitchDegrees));
    EXPECT_EQ(fpTunableIndexByName("  hfov  "), static_cast<int>(FpTunable::HorizontalFovDegrees));
    EXPECT_EQ(fpTunableIndexByName("nosuchparameter"), -1);
    EXPECT_EQ(fpTunableIndexByName(""), -1);
}

TEST_F(FpTunablesTest, AssignmentParsingAcceptsBothFormsAndRejectsGarbage) {
    std::string err;
    ASSERT_TRUE(fpTunableApplyAssignment("eyeForwardOffset=0.75", err)) << err;
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.75f);
    ASSERT_TRUE(fpTunableApplyAssignment("eyeForwardOffset -0.5", err)) << err;
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), -0.5f);

    EXPECT_FALSE(fpTunableApplyAssignment("nosuch=1", err));
    EXPECT_NE(err.find("unknown parameter"), std::string::npos) << err;
    EXPECT_FALSE(fpTunableApplyAssignment("hfov=abc", err));
    EXPECT_NE(err.find("not a finite number"), std::string::npos) << err;
    EXPECT_FALSE(fpTunableApplyAssignment("hfov=1.0garbage", err));
    EXPECT_FALSE(fpTunableApplyAssignment("hfov=nan", err));
    EXPECT_FALSE(fpTunableApplyAssignment("hfov=inf", err));
    // A rejected assignment must leave the parameter alone.
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), kFirstPersonHorizontalFovDegrees);
}

TEST_F(FpTunablesTest, AssignmentListAppliesEveryItemAndReportsTheFirstFailure) {
    std::string err;
    ASSERT_TRUE(fpTunableApplyAssignmentList("eyeForwardOffset=0.9,hfov=70,pitchDown=0", err))
        << err;
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.9f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), 70.0f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::PitchDownDegrees), 0.0f);

    fpTunableResetAll();
    EXPECT_FALSE(fpTunableApplyAssignmentList("hfov=70,bogus=1", err));
    EXPECT_NE(err.find("bogus"), std::string::npos) << err;
    // The good half still applied -- a partially-valid sweep argument must not
    // silently render the default for the parameter that WAS understood.
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), 70.0f);
}

TEST_F(FpTunablesTest, ValuesAreDeliberatelyUnclamped) {
    // This is a measurement instrument. A "sensible range" clamp would
    // truncate the sweep silently, which is worse than an absurd frame.
    std::string err;
    ASSERT_TRUE(fpTunableApplyAssignment("eyeForwardOffset=-50", err)) << err;
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), -50.0f);
    ASSERT_TRUE(fpTunableApplyAssignment("hfov=179", err)) << err;
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), 179.0f);
}

// ---------------------------------------------------------------------------
// 3. THE PARAMETERS REACH THE FORMULAS
// ---------------------------------------------------------------------------

TEST_F(FpTunablesTest, EyeForwardOffsetMovesTheCameraAlongTheLookRay) {
    const FirstPersonCameraPlacement before =
        computeFirstPersonCamera(100.0f, 200.0f, 10.0f, 3.65f, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::EyeForwardOffsetUnits), 1.0f));
    const FirstPersonCameraPlacement after =
        computeFirstPersonCamera(100.0f, 200.0f, 10.0f, 3.65f, 0.0f, 0.0f, 0.0f);
    // Heading 0 => forward is +Z in Irrlicht world axes.
    EXPECT_NEAR(after.posZ - before.posZ, 1.0f, 1e-4f);
    EXPECT_NEAR(after.posX, before.posX, 1e-4f);
    EXPECT_NEAR(after.posY, before.posY, 1e-4f);
}

TEST_F(FpTunablesTest, PitchDownAndMaxDownPitchReachTheFormula) {
    EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(), kFirstPersonMaxDownPitchDegrees);
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::MaxDownPitchDegrees), 60.0f));
    EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(), 60.0f);

    // The floor for the STORED mouse-look value tracks both tunables.
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::PitchDownDegrees), 10.0f));
    EXPECT_FLOAT_EQ(firstPersonMinLookPitchDegrees(), -(60.0f - 10.0f));

    // And the clamp actually bites: ask for 80 degrees down, get 60.
    const FirstPersonCameraPlacement p =
        computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -80.0f, 100.0f);
    const float pitchDeg = std::asin(p.targetY / 100.0f) * 180.0f / kFirstPersonPi;
    EXPECT_NEAR(pitchDeg, -60.0f, 0.05f);
}

TEST_F(FpTunablesTest, MaxUpPitchReachesTheFormula) {
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::MaxUpPitchDegrees), 30.0f));
    const FirstPersonCameraPlacement p =
        computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 80.0f, 100.0f);
    const float pitchDeg = std::asin(p.targetY / 100.0f) * 180.0f / kFirstPersonPi;
    EXPECT_NEAR(pitchDeg, 30.0f, 0.05f);
}

TEST_F(FpTunablesTest, HorizontalFovReachesTheProjectionHelper) {
    const float before = firstPersonVerticalFovRadians(4.0f / 3.0f);
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::HorizontalFovDegrees), 90.0f));
    const float after = firstPersonVerticalFovRadians(4.0f / 3.0f);
    EXPECT_GT(after, before);
    // 90 horizontal at 4:3 => 2*atan(tan(45)/1.3333) = 73.74 vertical.
    EXPECT_NEAR(after * 180.0f / kFirstPersonPi, 73.74f, 0.05f);
}

TEST_F(FpTunablesTest, SkullFractionReachesTheEyeDerivation) {
    HeadVertexCluster cluster;
    cluster.count = 100;
    cluster.minZ = -1.0f;
    cluster.maxZ = 1.0f;
    const EyeLocalPoint before = computeEyeInHeadBoneSpace(cluster);
    EXPECT_NEAR(before.z, -1.0f + kEyeHeightFractionOfSkull * 2.0f, 1e-5f);

    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::EyeHeightFractionOfSkull), 1.0f));
    const EyeLocalPoint after = computeEyeInHeadBoneSpace(cluster);
    EXPECT_NEAR(after.z, 1.0f, 1e-5f);
}

// The two parameters with no runtime consumer are declared advisory rather
// than pretended live. A sweep driver that assumed otherwise would chase a
// flat difference curve for an afternoon.
TEST_F(FpTunablesTest, AdvisoryParametersAreLabelledAsSuch) {
    const FpTunableSpec* specs = fpTunableSpecs();
    EXPECT_FALSE(specs[static_cast<int>(FpTunable::CrownAboveEyeUnits)].live);
    EXPECT_FALSE(specs[static_cast<int>(FpTunable::EyeHeightFractionOfStandingHeight)].live);
    for (int i = 0; i < kFpTunableCount; ++i) {
        if (i == static_cast<int>(FpTunable::CrownAboveEyeUnits)) continue;
        if (i == static_cast<int>(FpTunable::EyeHeightFractionOfStandingHeight)) continue;
        EXPECT_TRUE(specs[i].live) << specs[i].name;
    }
    const auto lines = FirstPersonTunablesCommandLines("set crownAboveEye 0.9");
    EXPECT_TRUE(AnyContains(lines, "ADVISORY")) << lines.front();
}

// ---------------------------------------------------------------------------
// 4. THE COMMAND'S TEXT HALF
// ---------------------------------------------------------------------------

TEST_F(FpTunablesTest, ListNamesEveryParameterWithValueDefaultUnitsAndFrame) {
    const auto lines = FirstPersonTunablesCommandLines("");
    ASSERT_EQ(lines.size(), static_cast<size_t>(kFpTunableCount) + 1);
    for (int i = 0; i < kFpTunableCount; ++i) {
        const std::string& l = lines[i + 1];
        EXPECT_NE(l.find(fpTunableSpecs()[i].name), std::string::npos) << l;
        EXPECT_NE(l.find("default="), std::string::npos) << l;
        EXPECT_NE(l.find("units="), std::string::npos) << l;
        EXPECT_NE(l.find("frame="), std::string::npos) << l;
        EXPECT_NE(l.find("overridden=no"), std::string::npos) << l;
    }
}

TEST_F(FpTunablesTest, SetAndResetReportWhatTheyDid) {
    auto lines = FirstPersonTunablesCommandLines("set eyeForwardOffset 0.4");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("set eyeForwardOffset = 0.400000"), std::string::npos) << lines[0];
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.4f);

    // "name = value" and "name=value" are both accepted.
    lines = FirstPersonTunablesCommandLines("set eyeForwardOffset = 0.6");
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.6f);
    lines = FirstPersonTunablesCommandLines("set eyeForwardOffset=0.8");
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.8f);

    lines = FirstPersonTunablesCommandLines("reset eyeForwardOffset");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("reset eyeForwardOffset"), std::string::npos) << lines[0];
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), kEyeForwardOffsetUnits);
}

TEST_F(FpTunablesTest, ResetWithNoArgumentRestoresEverything) {
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::HorizontalFovDegrees), 80.0f));
    ASSERT_TRUE(fpTunableSet(static_cast<int>(FpTunable::PitchDownDegrees), 12.0f));
    const auto lines = FirstPersonTunablesCommandLines("reset");
    EXPECT_TRUE(AnyContains(lines, "reset all"));
    for (int i = 0; i < kFpTunableCount; ++i) {
        EXPECT_FLOAT_EQ(fpTunableValue(i), fpTunableSpecs()[i].defaultValue);
    }
}

TEST_F(FpTunablesTest, BadInputIsReportedNotSwallowed) {
    auto lines = FirstPersonTunablesCommandLines("set");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("error"), std::string::npos) << lines[0];

    lines = FirstPersonTunablesCommandLines("set nosuch 1");
    EXPECT_TRUE(AnyContains(lines, "unknown parameter"));

    lines = FirstPersonTunablesCommandLines("wobble");
    EXPECT_TRUE(AnyContains(lines, "unknown subcommand"));

    lines = FirstPersonTunablesCommandLines("reset nosuch");
    EXPECT_TRUE(AnyContains(lines, "unknown parameter"));
}

TEST_F(FpTunablesTest, HelpExplainsTheStartupFormToo) {
    const auto lines = FirstPersonTunablesCommandLines("help");
    EXPECT_TRUE(AnyContains(lines, "--fpcam"));
    EXPECT_TRUE(AnyContains(lines, "/fpcam dump"));
}

// The dump formatter, directly: the two frames must not be mixed up, which is
// the specific mistake this dump exists to make impossible for Leg C.
TEST_F(FpTunablesTest, DumpStatesTheSameEyeInBothFramesWithTheAxesSwapped) {
    FirstPersonPoseSample sample;
    sample.modeName = "First Person";
    sample.eyeIrrX = 11.0f;   // EQ x
    sample.eyeIrrY = 22.0f;   // EQ z (up)
    sample.eyeIrrZ = 33.0f;   // EQ y
    sample.nodeOriginEqZ = 15.0f;
    sample.collisionZOffset = 3.05f;
    const auto lines = FormatFirstPersonPoseDump(sample);

    EXPECT_TRUE(AnyContains(lines, "eyeIrrX=11.000000 eyeIrrY=22.000000 eyeIrrZ=33.000000"));
    EXPECT_TRUE(AnyContains(lines, "eyeEqX=11.000000 eyeEqY=33.000000 eyeEqZ=22.000000"));
    // eye above the node origin (22 - 15) and above the feet (22 - (15-3.05)).
    EXPECT_TRUE(AnyContains(lines, "eyeAboveNodeOrigin=7.000000"));
    EXPECT_TRUE(AnyContains(lines, "eyeAboveGround=10.050000"));
    EXPECT_TRUE(AnyContains(lines, "feetEqZ=11.950000"));
    for (const auto& l : lines) EXPECT_EQ(l.rfind("[FPCAM-DUMP] ", 0), 0u) << l;
}

// ---------------------------------------------------------------------------
// 5. REACHABILITY -- BOTH PATHS
// ---------------------------------------------------------------------------

// Path A: the renderer's own if-chain, which is what the in-game chat input
// reaches (EverQuest::ProcessChatInput -> the unrecognized-command fallback ->
// the RendererCommand bridge -> IrrlichtRenderer::processSlashCommand).
class FpCamRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        fpTunableResetAll();
        renderer.setBridge(&bridge);
    }
    void TearDown() override { fpTunableResetAll(); }

    IrrlichtRenderer renderer;
    TestBridge bridge;
};

TEST_F(FpCamRendererTest, NoArgListsThroughTheRealIfChain) {
    renderer.processSlashCommand("/fpcam");
    const auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), static_cast<size_t>(kFpTunableCount) + 1);
    EXPECT_TRUE(AnyContains(msgs, "eyeForwardOffset"));
    EXPECT_TRUE(AnyContains(msgs, "maxDownPitch"));
}

TEST_F(FpCamRendererTest, SetThroughTheRealIfChainChangesTheValue) {
    renderer.processSlashCommand("/fpcam set eyeForwardOffset 0.9");
    const auto msgs = DrainSystemMessages(bridge);
    ASSERT_FALSE(msgs.empty());
    EXPECT_NE(msgs[0].find("set eyeForwardOffset = 0.900000"), std::string::npos) << msgs[0];
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.9f);

    renderer.processSlashCommand("/fpcam reset");
    DrainSystemMessages(bridge);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), kEyeForwardOffsetUnits);
}

// The line Leg C's sweep driver reads back. It must NOT assert on the
// dispatcher's outcome= (registry 3 always says "queued"), so this asserts on
// the emitted line itself -- and on the units/frame being stated, because two
// frames are in play and they have been confused before.
TEST_F(FpCamRendererTest, DumpEmitsParseableLinesStatingUnitsAndFrame) {
    renderer.processSlashCommand("/fpcam dump");
    const auto msgs = DrainSystemMessages(bridge);
    ASSERT_FALSE(msgs.empty());
    for (const auto& m : msgs) {
        EXPECT_EQ(m.rfind("[FPCAM-DUMP] ", 0), 0u) << m;
    }
    EXPECT_TRUE(AnyContains(msgs, "eyeIrrX="));
    EXPECT_TRUE(AnyContains(msgs, "frame=irrlicht_world"));
    EXPECT_TRUE(AnyContains(msgs, "eyeEqX="));
    EXPECT_TRUE(AnyContains(msgs, "frame=eq_world"));
    EXPECT_TRUE(AnyContains(msgs, "nodeOriginEqZ="));
    EXPECT_TRUE(AnyContains(msgs, "eyeAboveGround="));
    EXPECT_TRUE(AnyContains(msgs, "hfovDeg="));
    EXPECT_TRUE(AnyContains(msgs, "nearPlane="));
    EXPECT_TRUE(AnyContains(msgs, "units=eq_units"));
    EXPECT_TRUE(AnyContains(msgs, "units=degrees"));
    // The inputs that produced the pose travel with it, so one dump is a
    // complete sweep sample.
    EXPECT_TRUE(AnyContains(msgs, "tunables eyeForwardOffset="));
}

TEST_F(FpCamRendererTest, DumpTracksASet) {
    renderer.processSlashCommand("/fpcam set eyeForwardOffset 2.0");
    DrainSystemMessages(bridge);
    renderer.processSlashCommand("/fpcam dump");
    const auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "eyeForwardOffset=2.000000"));
}

// Path B: the catalog gate every NON-chat path passes through --
// Application::dispatchToActionOrRenderer() (application.cpp:1231) refuses
// anything FindRendererCommand() does not know, reporting registry=none, so a
// --script run or piped console input would never queue /fpcam without this
// row. This is the exact half T308 had to go back and fix for another command.
TEST_F(FpTunablesTest, FpcamIsInTheRendererCommandCatalog) {
    const auto* info = eqt::help::FindRendererCommand("fpcam");
    ASSERT_NE(info, nullptr)
        << "/fpcam is missing from kCatalog: a --script run would report "
           "registry=none and never queue it";
    EXPECT_EQ(info->name, "fpcam");
    EXPECT_NE(eqt::help::FindRendererCommand("/FPCam"), nullptr);
    EXPECT_FALSE(info->description.empty());
}

// ...and the SAME dispatcher a --script step (application.cpp:1162) and piped
// stdin (application.cpp:395) run through, which is the half that has to be
// proven separately: the chat window reaches processSlashCommand() by a
// different route entirely (eq_chat.cpp:344 + the unrecognized-command
// fallback). A bare Application has no renderer, so the honest outcome here is
// the graphics-mode REFUSAL -- reaching which means the catalog gate at
// application.cpp:1231 admitted /fpcam. An unknown name never gets that far.
TEST_F(FpTunablesTest, FpcamIsClassifiedAsARendererCommandByTheScriptDispatcher) {
    eqt::Application app;
    eqt::CommandDispatchResult r = app.DispatchSlashCommand("/fpcam dump");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_NE(r.message.find("/fpcam requires graphics mode"), std::string::npos) << r.message;

    eqt::CommandDispatchResult bad = app.DispatchSlashCommand("/fpcamnotacommand");
    EXPECT_FALSE(bad.recognized);
    EXPECT_EQ(bad.registry, "none");
}

// ---------------------------------------------------------------------------
// 6. STARTUP
// ---------------------------------------------------------------------------

TEST_F(FpTunablesTest, FpcamCliFlagIsParsedAndIsRepeatable) {
    const char* argv[] = {"willeq", "--fpcam", "eyeForwardOffset=0.9,hfov=70",
                          "--fpcam", "pitchDown=0"};
    auto config = eqt::Application::parseArguments(5, const_cast<char**>(argv));
    ASSERT_EQ(config.fpCamOverrides.size(), 2u);
    EXPECT_EQ(config.fpCamOverrides[0], "eyeForwardOffset=0.9,hfov=70");
    EXPECT_EQ(config.fpCamOverrides[1], "pitchDown=0");
    // parseArguments is documented side-effect-free; the store is untouched
    // until initialize() applies the strings.
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), kEyeForwardOffsetUnits);

    std::string err;
    for (const auto& s : config.fpCamOverrides) {
        EXPECT_TRUE(fpTunableApplyAssignmentList(s, err)) << err;
    }
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::EyeForwardOffsetUnits), 0.9f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::HorizontalFovDegrees), 70.0f);
    EXPECT_FLOAT_EQ(fpTunable(FpTunable::PitchDownDegrees), 0.0f);
}

TEST_F(FpTunablesTest, NoFpcamFlagLeavesEveryDefaultInPlace) {
    const char* argv[] = {"willeq"};
    auto config = eqt::Application::parseArguments(1, const_cast<char**>(argv));
    EXPECT_TRUE(config.fpCamOverrides.empty());
    for (int i = 0; i < kFpTunableCount; ++i) {
        EXPECT_FLOAT_EQ(fpTunableValue(i), fpTunableSpecs()[i].defaultValue);
    }
}
