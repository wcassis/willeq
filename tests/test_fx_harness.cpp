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
// held 0.31 s past OP_Action (B1131) and its sprites then finish their life,
// so the window starts at Act+1.4 s, after the last Source frame. Measured on
// 526c6db2 + pack 1cdd31e7 (review/B1138/tests/fxharness_1cdd31/): the
// caster box's near-white Source cloud (the same colour and rows as during the
// cast) decays 941 -> 578 -> 280 -> 48 -> 0 px over Act+0.9..1.3 s; from
// Act+1.4 to +2.0 the box holds 0-176 px, all in the head rows (371..426:
// the idle animation moving the pale hair), while the target box holds
// 23,200-27,700. A window starting at Act+1.0 caught the Source tail (578 px).
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
    // B1144: runs one plan (its rows after the header) for `seconds`; the
    // frames directory, or empty when the harness failed (see run.log).
    fs::path runPlan(const std::string& name, const std::string& rows, double seconds,
                     const std::string& extra = "") {
        const fs::path dir = fs::temp_directory_path() /
                             ("willeq_fx_harness_" + name + "_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        dirs_.push_back(dir);
        {
            std::ofstream f(dir / "plan.tsv");
            f << "t_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n" << rows;
        }
        const fs::path frames = dir / "frames";
        const std::string cmd = std::string("cd '") + WILLEQ_REPO_ROOT + "' && '" + bin_ +
                                "' --spell-test --client " + kClient + " --eqe1 " + kPack + " --plan '" +
                                (dir / "plan.tsv").string() + "' --frames-dir '" + frames.string() +
                                "' --fps 30 --seconds " + std::to_string(seconds) +
                                " --headless --camera b1118 --seed 1 " + extra + " > '" +
                                (dir / "run.log").string() + "' 2>&1";
        return std::system(cmd.c_str()) == 0 ? frames : fs::path();
    }
    static fs::path frameAt(const fs::path& frames, int f) {
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05d.png", f);
        return frames / name;
    }
    // slots.tsv as "stage:slot" in file order.
    static std::vector<std::string> slotRows(const fs::path& frames) {
        std::vector<std::string> out;
        std::ifstream f(frames / "slots.tsv");
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string c[7];
            for (auto& x : c) std::getline(ss, x, '\t');
            out.push_back(c[5] + ":" + c[6]);
        }
        return out;
    }
    void cleanDirs() {
        if (HasFailure()) return;
        for (const auto& d : dirs_) fs::remove_all(d);
    }
    irr::IrrlichtDevice* device_ = nullptr;
    std::string bin_;
    std::vector<fs::path> dirs_;
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
    for (double t = kAction + 1.4; t <= kAction + 1.95; t += 1.0 / kFps) {
        irr::video::IImage* img = load(frame(t));
        ASSERT_NE(img, nullptr) << frame(t);
        targetPeak = std::max(targetPeak, lit(img, base, scene.at("target")));
        casterPeak = std::max(casterPeak, lit(img, base, scene.at("caster")));
        img->drop();
    }
    base->drop();
    EXPECT_GE(targetPeak, 3000) << "the landing did not light the target";
    // 400 = 2.3x the measured idle-animation maximum (176); a caster landing
    // puts thousands there (7,816 in the mutation check).
    EXPECT_LE(casterPeak, 400) << "the landing (or a half of it) is on the caster of an other-target cast";
    if (!HasFailure()) fs::remove_all(dir);
}

// B1144 (#464): a /disc is OP_CastSpell slot 10 -> OP_Action with NO
// OP_BeginCast (B1134 leg P4b's pcap), and the official client plays a
// record's Source from its begin-cast handler only (eqgame.exe 0x49b39d);
// 4499 Defensive Discipline's record 326 has no Target stage, so no landing.
// The observer drew no sprite (B1134 record 51). The harness's action-only
// packets must therefore draw NOTHING: every frame equals the frame of a run
// whose only cast lies past the end (zero lit px, byte-identical frames).
// RED on fb6d6481: B1105's 2 s action-only glow and the spellComplete half.
TEST_F(FxHarness, ADisciplineWithoutBeginCastDrawsNothing) {
    const fs::path disc = runPlan("disc", "0.5\t4499\tself\t0\t0\t60\taction\n", 3.0);
    const fs::path none = runPlan("none", "100\t4499\tself\t0\t0\t60\taction\n", 3.0);
    ASSERT_FALSE(disc.empty()) << "the /disc harness run failed";
    ASSERT_FALSE(none.empty()) << "the reference harness run failed";
    EXPECT_TRUE(slotRows(disc).empty()) << "a /disc drew a spellsnew.eff slot";
    const Box whole{0, 0, 1279, 799};
    int peak = 0;
    for (int f = 15; f < 90; ++f) {  // Act-0.0 .. Act+2.5 s
        irr::video::IImage* a = load(frameAt(disc, f));
        irr::video::IImage* b = load(frameAt(none, f));
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        peak = std::max({peak, lit(a, b, whole), lit(b, a, whole)});
        a->drop();
        b->drop();
    }
    std::printf("[fx-gate] /disc 4499 (OP_Action only): peak lit px vs the no-cast run %d\n", peak);
    EXPECT_EQ(peak, 0) << "an OP_Action without OP_BeginCast drew something";
    cleanDirs();
}

// B1144: a slot is drawn only if its minLevel <= the caster's level
// (eqgame.exe 0x495cc3). 93 Burst of Flame draws record 80: Source LHAND +
// RHAND at level 0 and DEFAULT at 39 / 24, Target four DEFAULT slots, two at
// level 0. A level-1 caster draws only the level-0 slots; the lane-5 check of
// the same cast at level 1 and 60 is review/B1144/README.md.
TEST_F(FxHarness, ALevelOneCasterDrawsOnlyItsLevelZeroSlots) {
    const fs::path l1 = runPlan("l1", "0.5\t93\tother\t14\t1500\t1\tbegincast\n", 2.5);
    const fs::path l60 = runPlan("l60", "0.5\t93\tother\t14\t1500\t60\tbegincast\n", 2.5);
    const fs::path none = runPlan("ref", "100\t93\tother\t14\t1500\t60\tbegincast\n", 2.5);
    ASSERT_FALSE(l1.empty());
    ASSERT_FALSE(l60.empty());
    ASSERT_FALSE(none.empty());
    EXPECT_EQ(slotRows(l1), (std::vector<std::string>{"Source:0", "Source:1", "Target:0", "Target:1"}));
    EXPECT_EQ(slotRows(l60), (std::vector<std::string>{"Source:0", "Source:1", "Source:2", "Source:3",
                                                       "Target:0", "Target:1", "Target:2", "Target:3"}));
    // The Source during the cast (BC+0.6..Act): the level-1 caster's two hand
    // slots light fewer caster pixels than the level-60 caster's four slots.
    const auto scene = readScene(l1 / "scene.tsv");
    ASSERT_EQ(scene.count("caster"), 1u);
    long sum1 = 0, sum60 = 0;
    for (int f = 33; f < 60; ++f) {
        irr::video::IImage* r = load(frameAt(none, f));
        irr::video::IImage* a = load(frameAt(l1, f));
        irr::video::IImage* b = load(frameAt(l60, f));
        ASSERT_TRUE(r && a && b) << f;
        sum1 += lit(a, r, scene.at("caster"));
        sum60 += lit(b, r, scene.at("caster"));
        r->drop();
        a->drop();
        b->drop();
    }
    std::printf("[fx-gate] 93 Source, caster box lit px summed BC+0.6..Act: level 1 %ld, level 60 %ld\n", sum1,
                sum60);
    EXPECT_GT(sum1, 0) << "the level-1 caster's hand slots drew nothing";
    EXPECT_LT(sum1, sum60) << "the level-1 caster drew as much Source as the level-60 caster";
    cleanDirs();
}

}  // namespace
