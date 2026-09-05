// T360 Leg A (B066) -- the vision-type -> zone-light mapping.
//
// `SimulationWorker::applyVisionWeatherToZoneLights` and
// `SimulationWorker::computeLightAnimations` (src/client/graphics/sim_animation.cpp)
// each carried a hand-written switch reading `case 1: // Ultravision` and
// `case 2: // Infravision`. `EQT::Graphics::VisionType` and
// `EQ::BuffManager::computeVisionType` are both Normal = 0, Infravision = 1,
// Ultravision = 2, so the two arms were SWAPPED: an Ultravision landing got
// Infravision's 0.75 intensity plus a 0.3 red shift, and vice versa. T358
// measured it live (R +5.5%, B +0.4% across a `#castspell 46` landing -- a red
// shift, which only the Infravision arm produces).
//
// These tests drive the REAL production functions through the public
// `computeOnce()` seam -- not a reimplementation of the mapping. Both
// functions are observable from a SimulationOutput:
//   * applyVisionWeatherToZoneLights writes zoneData_.zoneLightNodes[i].
//     diffuseColor for every NON-animated light, and computeLightSelection
//     copies that into output.selectedLights[].diffuseColor;
//   * computeLightAnimations writes output.zoneLightAnimColors directly.
// So light index 0 (no anim) pins the first function and light index 1 (with
// an anim) pins the second, in one run.
//
// No Irrlicht device is needed: computeOnce() is synchronous, runs on the
// calling thread, and the fixture's zone data has no BSP tree, no portals and
// no occluders (loadingActive = true takes the culling-free branch).

#include <gtest/gtest.h>

#include "client/graphics/renderer_types.h"   // EQT::Graphics::VisionType
#include "client/graphics/simulation_worker.h"

namespace EQT {
namespace Graphics {
namespace {

// Base colour of both lights is pure white, so every expected number below is
// the modifier itself and the arithmetic in the two functions is visible:
//   r = base.r * intensity, then * (1 + redShift) when redShift > 0
//   g = base.g * intensity * (1 - redShift * 0.5)
//   b = base.b * intensity * (1 - redShift)
constexpr float kNormalIntensity = 0.25f;

class VisionZoneLightsTest : public ::testing::Test {
protected:
    void SetUp() override {
        SimulationZoneData data;

        // Two zone lights at the origin, both inside the default 300-unit
        // render distance so computeLightSelection picks them up.
        data.zoneLights.resize(2);
        data.zoneLights[0].position = irr::core::vector3df(0, 0, 0);
        data.zoneLights[0].bspRegion = SIZE_MAX;
        data.zoneLights[1].position = irr::core::vector3df(0, 0, 0);
        data.zoneLights[1].bspRegion = SIZE_MAX;

        data.zoneLightNodes.resize(2);
        for (auto& n : data.zoneLightNodes) {
            n.position = irr::core::vector3df(0, 0, 0);
            n.diffuseColor = irr::video::SColorf(1.0f, 1.0f, 1.0f, 1.0f);
            n.radius = 100.0f;
            n.attConstant = 1.0f;
            n.attLinear = 0.0f;
            n.attQuadratic = 0.0f;
        }

        // Light 1 only is animated; light 0 stays on the
        // applyVisionWeatherToZoneLights path.
        SimulationZoneData::ZoneLightAnimData anim;
        anim.lightIndex = 1;
        anim.frameCount = 2;
        anim.sleepMs = 1;
        anim.frameColors = {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
        data.zoneLightAnims.push_back(anim);

        worker_.setZoneData(data);
    }

    // Runs kBackgroundInterval frames so the background tier (which owns
    // computeLightAnimations) fires at least once, and returns the last output.
    SimulationOutput run(uint8_t visionType) {
        SimulationInput input;
        input.loadingActive = true;
        input.visionType = visionType;
        input.weatherAmbientModifier = 1.0f;
        input.renderDistance = 300.0f;
        input.deltaTime = 0.1f;  // 100 ms >= the 1 ms anim sleep
        SimulationOutput output;
        for (int i = 0; i < 3; ++i) {
            input.frameNumber = static_cast<uint32_t>(i);
            worker_.computeOnce(input, output);
        }
        return output;
    }

    // The non-animated zone light, as computeLightSelection published it.
    static irr::video::SColorf staticLightColor(const SimulationOutput& out) {
        for (const auto& sl : out.selectedLights) {
            if (sl.valid && sl.isZoneLight && sl.sourceIndex == 0) return sl.diffuseColor;
        }
        ADD_FAILURE() << "zone light 0 was not selected";
        return irr::video::SColorf(-1, -1, -1, -1);
    }

    SimulationWorker worker_;
};

// --- Ultravision (VisionType::Ultravision == 2): full intensity, NO red shift.
//     Pre-fix this arm ran Infravision's 0.75 / 0.3 and both assertions failed.

TEST_F(VisionZoneLightsTest, UltravisionIsFullIntensityWithNoRedShift) {
    SimulationOutput out = run(static_cast<uint8_t>(VisionType::Ultravision));

    const irr::video::SColorf c = staticLightColor(out);
    EXPECT_NEAR(c.r, 1.0f, 1e-4f);
    EXPECT_NEAR(c.g, 1.0f, 1e-4f);
    EXPECT_NEAR(c.b, 1.0f, 1e-4f);

    ASSERT_EQ(out.zoneLightAnimColors.size(), 1u);
    const auto& a = out.zoneLightAnimColors[0];
    EXPECT_EQ(a.lightIndex, 1u);
    EXPECT_NEAR(a.r, 1.0f, 1e-4f);
    EXPECT_NEAR(a.g, 1.0f, 1e-4f);
    EXPECT_NEAR(a.b, 1.0f, 1e-4f);
}

// --- Infravision (VisionType::Infravision == 1): 0.75 intensity, 0.3 red
//     shift. Pre-fix this arm ran Ultravision's 1.0 / 0.0.

TEST_F(VisionZoneLightsTest, InfravisionIsThreeQuarterIntensityRedShifted) {
    SimulationOutput out = run(static_cast<uint8_t>(VisionType::Infravision));

    // intensity 0.75, redShift 0.3 -> r 0.75 * 1.3, g 0.75 * 0.85, b 0.75 * 0.7
    const irr::video::SColorf c = staticLightColor(out);
    EXPECT_NEAR(c.r, 0.975f, 1e-4f);
    EXPECT_NEAR(c.g, 0.6375f, 1e-4f);
    EXPECT_NEAR(c.b, 0.525f, 1e-4f);

    ASSERT_EQ(out.zoneLightAnimColors.size(), 1u);
    const auto& a = out.zoneLightAnimColors[0];
    EXPECT_NEAR(a.r, 0.975f, 1e-4f);
    EXPECT_NEAR(a.g, 0.6375f, 1e-4f);
    EXPECT_NEAR(a.b, 0.525f, 1e-4f);
}

// --- Normal (VisionType::Normal == 0): the untouched 0.25 default. This arm
//     was never wrong; it is here so a future edit cannot "fix" the swap by
//     rotating all three.

TEST_F(VisionZoneLightsTest, NormalVisionIsTheQuarterIntensityDefault) {
    SimulationOutput out = run(static_cast<uint8_t>(VisionType::Normal));

    const irr::video::SColorf c = staticLightColor(out);
    EXPECT_NEAR(c.r, kNormalIntensity, 1e-4f);
    EXPECT_NEAR(c.g, kNormalIntensity, 1e-4f);
    EXPECT_NEAR(c.b, kNormalIntensity, 1e-4f);

    ASSERT_EQ(out.zoneLightAnimColors.size(), 1u);
    const auto& a = out.zoneLightAnimColors[0];
    EXPECT_NEAR(a.r, kNormalIntensity, 1e-4f);
    EXPECT_NEAR(a.g, kNormalIntensity, 1e-4f);
    EXPECT_NEAR(a.b, kNormalIntensity, 1e-4f);
}

// --- The enum the two switches must agree with. computeVisionType() in
//     src/client/spell/buff_manager.cpp literally `return 2;` for an
//     Ultravision effect, which is what made the swap a live defect rather
//     than a naming quibble.

TEST(VisionTypeEnumTest, UnderlyingValuesAreNormalZeroInfraOneUltraTwo) {
    EXPECT_EQ(static_cast<int>(VisionType::Normal), 0);
    EXPECT_EQ(static_cast<int>(VisionType::Infravision), 1);
    EXPECT_EQ(static_cast<int>(VisionType::Ultravision), 2);
}

}  // namespace
}  // namespace Graphics
}  // namespace EQT
