// B1138 (#458): the fable-side spell FX harness, end to end.
//
// Runs `model_viewer --spell-test --plan ... --headless` (the production
// SpellVisualFX -> ParticleManager -> SimulationWorker chain, driven by the
// client's own SpellCastVisual* sequence in src/client/bridge/spell_cast_fx.cpp)
// surfaceless for 4 s on one OTHER-target cast, then counts lit pixels in the
// caster's and the target's screen boxes (the harness's scene.tsv) against the
// pre-cast frame. B1132's rule: an other-target landing lights the TARGET and
// not the caster. Pre-B1132, half of it (spellComplete) was drawn on the
// caster; pre-B1138 the viewer drew nothing at all (it never fed the
// SimulationWorker that owns the particle pool, and exited FATAL on the
// missing particle-atlas cache).
//
// The worked case is 201 Flash of Light (spells.eff record 22): its Source is
// a head halo held only 0.31 s past OP_Action (B1131), so by Act+1 s the
// caster's box holds no FX at all and only the idle animation moves there.
// Measured on 2026-10-23 (review/B1138/README.md): target box 14,000-18,500
// lit px over Act+1..2 s, caster box 0-143 px (idle-animation diffs).
//
// Self-skips without the EQ client files or the willeq.eqe1 pack.

#include <gtest/gtest.h>

#include <irrlicht.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const char* kClient = "/mnt/projects/EverQuestP1999";
const char* kPack = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";
constexpr int kFps = 30;
constexpr double kBegin = 0.5;   // BeginCast
constexpr double kAction = 2.0;  // + Flash of Light's 1500 ms cast time
constexpr int kRiseThreshold = 80;  // the lane-5 rigs' "lit" rule (fx_timing.py)

struct Box { int x0, y0, x1, y1; };

std::map<std::string, Box> readScene(const fs::path& p) {
    std::map<std::string, Box> out;
    std::ifstream f(p);
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string name;
        int id;
        Box b{};
        if (ss >> name >> id >> b.x0 >> b.y0 >> b.x1 >> b.y1) out[name] = b;
    }
    return out;
}

class FxHarness : public ::testing::Test {
protected:
    void SetUp() override {
        const char* bin = std::getenv("WILLEQ_MODEL_VIEWER_BIN");
        if (!bin || !*bin) GTEST_SKIP() << "WILLEQ_MODEL_VIEWER_BIN unset (ctest sets it)";
        bin_ = bin;
        if (!fs::exists(fs::path(kClient) / "spells_us.txt") || !fs::exists(kPack))
            GTEST_SKIP() << "no EQ client files at " << kClient << " or no pack at " << kPack;
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr);
    }
    void TearDown() override {
        if (device_) device_->drop();
    }
    irr::video::IImage* load(const fs::path& p) {
        return device_->getVideoDriver()->createImageFromFile(p.string().c_str());
    }
    // Pixels in `b` whose max-channel rise over `base` exceeds the threshold.
    int lit(irr::video::IImage* img, irr::video::IImage* base, const Box& b) {
        const auto d = img->getDimension();
        int n = 0;
        for (int y = std::max(b.y0, 0); y <= std::min<int>(b.y1, d.Height - 1); ++y)
            for (int x = std::max(b.x0, 0); x <= std::min<int>(b.x1, d.Width - 1); ++x) {
                const auto c = img->getPixel(x, y), o = base->getPixel(x, y);
                const int rise = std::max({int(c.getRed()) - int(o.getRed()),
                                           int(c.getGreen()) - int(o.getGreen()),
                                           int(c.getBlue()) - int(o.getBlue())});
                if (rise > kRiseThreshold) ++n;
            }
        return n;
    }
    irr::IrrlichtDevice* device_ = nullptr;
    std::string bin_;
};

TEST_F(FxHarness, AnOtherTargetLandingLightsTheTargetAndNotTheCaster) {
    const fs::path dir = fs::temp_directory_path() / ("willeq_fx_harness_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path plan = dir / "plan.tsv";
    {
        std::ofstream f(plan);
        f << "t_begin_s\tspell_id\ttarget\tdistance_units\n" << kBegin << "\t201\tother\t14\n";
    }
    const fs::path frames = dir / "frames";
    const std::string cmd = std::string("cd '") + WILLEQ_REPO_ROOT + "' && '" + bin_ +
                            "' --spell-test --client " + kClient + " --eqe1 " + kPack +
                            " --plan '" + plan.string() + "' --frames-dir '" + frames.string() +
                            "' --fps 30 --seconds 4 --headless --camera b1118 --seed 1 > '" +
                            (dir / "run.log").string() + "' 2>&1";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "the harness failed; see " << (dir / "run.log");

    const auto scene = readScene(frames / "scene.tsv");
    ASSERT_EQ(scene.count("caster"), 1u);
    ASSERT_EQ(scene.count("target"), 1u);
    ASSERT_LT(scene.at("caster").x1, scene.at("target").x0) << "the boxes overlap; the framing moved";

    auto frame = [&](double t) {
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05d.png", static_cast<int>(t * kFps + 0.5));
        return frames / name;
    };
    irr::video::IImage* base = load(frame(kBegin - 0.1));  // pre-cast, idle pose running
    ASSERT_NE(base, nullptr);
    int targetPeak = 0, casterPeak = 0;
    for (double t = kAction + 1.0; t <= kAction + 1.95; t += 1.0 / kFps) {
        irr::video::IImage* img = load(frame(t));
        ASSERT_NE(img, nullptr) << frame(t);
        targetPeak = std::max(targetPeak, lit(img, base, scene.at("target")));
        casterPeak = std::max(casterPeak, lit(img, base, scene.at("caster")));
        img->drop();
    }
    base->drop();
    EXPECT_GE(targetPeak, 3000) << "the landing did not light the target";
    EXPECT_LE(casterPeak, 400) << "the landing (or a half of it) is on the caster of an other-target cast";
    if (!HasFailure()) fs::remove_all(dir);
}

}  // namespace
