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

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/spell_cast_animation.h"
#include "client/willeq_database.h"

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

using namespace EQT::Graphics;

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
    // B1155: `camera` selects the preset (b1118, b1130, b1130-self).
    fs::path runPlan(const std::string& name, const std::string& rows, double seconds,
                     const std::string& extra = "", const std::string& camera = "b1118") {
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
                                " --headless --camera " + camera + " --seed 1 " + extra + " > '" +
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
    // B1145 (#465): the caster's spellsnew.eff Source now runs its file
    // duration (cast + the cast-animation term, ~Act+4.35 s + life), so the
    // caster box holds the Source through the landing window. The reference
    // is the same cast whose OP_Action falls past the window (cast 9000 ms):
    // the same Source at the same age, no landing; the landing must add
    // nothing on the caster beyond that (a caster landing adds thousands).
    const fs::path ref = runPlan("srcref", std::to_string(kBegin) + "\t201\tother\t14\t9000\t60\tbegincast\n", 4.0);
    ASSERT_FALSE(ref.empty()) << "the reference harness run failed";
    irr::video::IImage* refBase = load(ref / frame(kBegin - 0.1).filename());
    ASSERT_NE(refBase, nullptr);
    int sourcePeak = 0;
    for (double t = kAction + 1.4; t <= kAction + 1.95; t += 1.0 / kFps) {
        irr::video::IImage* img = load(ref / frame(t).filename());
        ASSERT_NE(img, nullptr) << frame(t);
        sourcePeak = std::max(sourcePeak, lit(img, refBase, scene.at("caster")));
        img->drop();
    }
    refBase->drop();
    int targetPeak = 0, casterPeak = 0;
    for (double t = kAction + 1.4; t <= kAction + 1.95; t += 1.0 / kFps) {
        irr::video::IImage* img = load(frame(t));
        ASSERT_NE(img, nullptr) << frame(t);
        targetPeak = std::max(targetPeak, lit(img, base, scene.at("target")));
        casterPeak = std::max(casterPeak, lit(img, base, scene.at("caster")));
        img->drop();
    }
    base->drop();
    // B1145: record 112's slots now run their file envelopes (482's one-sprite
    // burst lives to Act+1.83, 483/23 stream to Act+2.33): 2,677 px in this
    // window at seed 1 (3,000+ on the B1121 schedule). A presence check.
    EXPECT_GE(targetPeak, 2000) << "the landing did not light the target";
    // 400 = 2.3x the measured idle-animation maximum (176); a caster landing
    // puts thousands there (7,816 in the mutation check).
    EXPECT_LE(casterPeak, sourcePeak + sourcePeak / 10 + 400)
        << "the landing (or a half of it) is on the caster of an other-target cast (Source alone: "
        << sourcePeak << ")";
    if (!HasFailure()) fs::remove_all(dir);
}

// B1144 (#464): a /disc is OP_CastSpell slot 10 -> OP_Action with NO
// OP_BeginCast (B1134 leg P4b's pcap), and the official client plays a
// record's Source from its begin-cast handler only (eqgame.exe 0x49b39d);
// 4499 Defensive Discipline's record 326 has no Target stage, so no landing.
// The observer drew no sprite (B1134 record 51; re-observed by B1162 leg
// b1162_disc4499_v3: OP_Action 4499 with no OP_BeginCast, the observer's
// caster ROI lit only by the caster's animation, Act+0.454..1.120, peak cov
// 0.067 -- refactor/B1162/README.md "The /disc landing leg"). The harness's action-only
// packets must therefore draw NOTHING: every frame equals the frame of a run
// whose only cast lies past the end (no lit px beyond the two processes'
// pre-cast noise).
// RED on fb6d6481: B1105's 2 s action-only glow and the spellComplete half.
TEST_F(FxHarness, ADisciplineWithoutBeginCastDrawsNothing) {
    const fs::path disc = runPlan("disc", "0.5\t4499\tself\t0\t0\t60\taction\n", 3.0);
    const fs::path none = runPlan("none", "100\t4499\tself\t0\t0\t60\taction\n", 3.0);
    ASSERT_FALSE(disc.empty()) << "the /disc harness run failed";
    ASSERT_FALSE(none.empty()) << "the reference harness run failed";
    EXPECT_TRUE(slotRows(disc).empty()) << "a /disc drew a spellsnew.eff slot";
    const Box whole{0, 0, 1279, 799};
    // Two harness processes differ by 0-4 px at the caster's held orb from
    // frame 0, before any packet (measured on 9d37d807: (662..667, 463..468),
    // max channel delta 152..203, the idle cycle's period). A drawn Source or
    // landing is thousands (93's Source above: 34,483 summed). The bound is
    // that pre-cast noise, measured in the same two runs, plus 16 px.
    int noise = 0;
    for (int f = 0; f < 15; ++f) {
        irr::video::IImage* a = load(frameAt(disc, f));
        irr::video::IImage* b = load(frameAt(none, f));
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        noise = std::max({noise, lit(a, b, whole), lit(b, a, whole)});
        a->drop();
        b->drop();
    }
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
    std::printf("[fx-gate] /disc 4499 (OP_Action only): peak lit px vs the no-cast run %d (pre-cast noise %d)\n",
                peak, noise);
    EXPECT_LE(peak, noise + 16) << "an OP_Action without OP_BeginCast drew something";
    cleanDirs();
}

// B1162 (#481): the same gate on record 325, with the discipline the official
// client really fired on lane 5: /disc Leechcurse Discipline -> C->S
// OP_CastSpell slot 10 spell 8604, no OP_BeginCast (leg b1162_disc325_v2,
// refactor/B1162/README.md "Correction: Leg A did not land"). 8604's
// spellsnew.eff record (spells_us.txt field 145) is 325, which has no Target
// stage. The lane server refused the cast (its 8604 row is another spell), so
// the oracle never drew a real record-325 /disc; this pins the fable side
// only. The harness's action-only 8604 must draw nothing.
TEST_F(FxHarness, TheLeechcurseDisciplineWithoutBeginCastDrawsNothing) {
    const fs::path disc = runPlan("disc8604", "0.5\t8604\tself\t0\t0\t60\taction\n", 3.0);
    const fs::path none = runPlan("none8604", "100\t8604\tself\t0\t0\t60\taction\n", 3.0);
    ASSERT_FALSE(disc.empty()) << "the /disc harness run failed";
    ASSERT_FALSE(none.empty()) << "the reference harness run failed";
    EXPECT_TRUE(slotRows(disc).empty()) << "a /disc drew a spellsnew.eff slot";
    const Box whole{0, 0, 1279, 799};
    int noise = 0;  // the two processes' pre-cast difference (see the 4499 test)
    for (int f = 0; f < 15; ++f) {
        irr::video::IImage* a = load(frameAt(disc, f));
        irr::video::IImage* b = load(frameAt(none, f));
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        noise = std::max({noise, lit(a, b, whole), lit(b, a, whole)});
        a->drop();
        b->drop();
    }
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
    std::printf("[fx-gate] /disc 8604 (OP_Action only): peak lit px vs the no-cast run %d (pre-cast noise %d)\n",
                peak, noise);
    EXPECT_LE(peak, noise + 16) << "an OP_Action without OP_BeginCast drew something";
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

// B1155 (#475): --camera b1130 / b1130-self solve B1130's fixed-geometry
// stills (review/B1130/README.md "Geometry"): the B1118 opening pose with the
// caster 209 px, feet row 598 at column 640, and for b1130 the target with feet
// (940,652). One written frame per preset (the cast lies at 100 s, past it); the
// solver's pixels are asserted from that frame, with the same off-clear-colour
// rule the solver corrects on.
//
// The target is B1130's stunned npc, `#spawn TrollF 1 1 0 30000` -- per
// eqemu's #spawn arg order that is race=1 (HUMAN), level=1, with the gender
// arg absent (Mob::GetDefaultGender(1) = MALE); "TrollF" is only a label
// (model_viewer_fx_harness.cpp). The harness renders that level-1 human male
// (race 1, gender 0, size 6) -- the oracle's OWN model. The 325 px in
// B1130's still is DEPTH, not a race scale: the npc stands ~1.55x nearer to
// the camera than the caster. The FEET (row 652, column 940) are the pinned
// FX reference and are asserted to the solved value. The rendered PIXEL
// HEIGHT is the model's own, and is a consistency check, not a solved
// input: in the FABLE client this harness renders the human mesh in a
// collapsed rest pose (~50 px, deterministic across seeds; the robed
// dark-elf caster stands at 209 px) -- a model-fidelity gap vs the official
// client's standing 325 px human, documented in refactor/B1155/README.md
// and asserted here to the measured value.
struct Silhouette {
    int top = -1, bottom = -1, n = 0;
    double col = 0;
};
Silhouette offClear(const irr::video::IImage& img, int x0, int y0, int x1, int y1) {
    Silhouette s;
    const auto d = img.getDimension();
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min<int>(x1, d.Width - 1);
    y1 = std::min<int>(y1, d.Height - 1);
    double sx = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const auto c = img.getPixel(x, y);
            if (std::abs(int(c.getRed()) - 50) + std::abs(int(c.getGreen()) - 50) +
                    std::abs(int(c.getBlue()) - 80) <= 12)
                continue;  // the clear colour (the ground plane paints it too)
            if (s.top < 0) s.top = y;
            s.bottom = y;
            sx += x;
            ++s.n;
        }
    if (s.n) s.col = sx / s.n;
    return s;
}
void checkB1130Frame(const fs::path& frames, bool expectTarget, irr::video::IVideoDriver* driver) {
    const auto scene = readScene(frames / "scene.tsv");
    ASSERT_EQ(scene.count("caster"), 1u);
    ASSERT_EQ(scene.count("target"), expectTarget ? 1u : 0u)
        << (expectTarget ? "the b1130 target box is missing" : "b1130-self draws the caster alone");
    irr::video::IImage* img = driver->createImageFromFile((frames / "frame_00000.png").string().c_str());
    ASSERT_NE(img, nullptr);
    // The pose box's bottom is above the rendered feet (the fit's comment), so
    // the search runs past it; the clear-colour ground keeps the region clean.
    const Box& cb = scene.at("caster");
    const auto c = offClear(*img, cb.x0 - 30, cb.y0 - 10, cb.x1 + 30, cb.y1 + 100);
    ASSERT_GT(c.n, 0) << "no off-clear pixels where the caster stands";
    std::printf(
        "[fx-gate] b1130 caster rows %d..%d (%d px, feet row %d), column %.1f (B1130: 209 px, feet 598 "
        "at 640)\n",
        c.top, c.bottom, c.bottom - c.top + 1, c.bottom, c.col);
    EXPECT_NEAR(c.bottom, 598.0, 1.0) << "caster feet row";
    EXPECT_NEAR(c.bottom - c.top + 1, 209.0, 1.0) << "caster pixel height";
    // B1130's 640 is the feet column; this is the silhouette CENTROID, which sits
    // ~1-2 px right of the feet column for the robed dark elf (the robe's skirt
    // leans one way). Tolerance 2 px.
    EXPECT_NEAR(c.col, 640.0, 2.0) << "caster column centroid";
    if (expectTarget) {
        const Box& tb = scene.at("target");
        ASSERT_LT(cb.x1, tb.x0) << "the caster and target boxes overlap; the framing moved";
        const auto t = offClear(*img, tb.x0 - 30, tb.y0 - 10, tb.x1 + 30, tb.y1 + 100);
        ASSERT_GT(t.n, 0) << "no off-clear pixels where the target stands";
        std::printf(
            "[fx-gate] b1130 target rows %d..%d (%d px, feet row %d), column %.1f (B1130 still: feet 652 "
            "at 940, 325 px -- depth, not a race scale; the FABLE human mesh renders in a collapsed "
            "rest pose at ~50 px, a model-fidelity gap)\n",
            t.top, t.bottom, t.bottom - t.top + 1, t.bottom, t.col);
        EXPECT_NEAR(t.bottom, 652.0, 1.0) << "target feet row (the pinned FX reference)";
        // The FABLE client's human mesh renders in a collapsed rest pose (~50 px),
        // not the official client's standing 325 px human (whose 325 px is the
        // npc's nearer depth, not a race scale). The FEET (row 652, col 940) are
        // the solved reference and are asserted tightly; the height is the
        // model's own collapsed value, asserted to the measured ~50 px.
        EXPECT_NEAR(t.bottom - t.top + 1, 50.0, 6.0)
            << "target pixel height (the FABLE human mesh's collapsed rest pose)";
        EXPECT_NEAR(t.col, 940.0, 2.0) << "target column centroid";
    }
    img->drop();
}

TEST_F(FxHarness, B1130PresetSolvesTheFixedNpcGeometry) {
    const fs::path frames = runPlan("b1130", "100.0\t93\tother\t0\t250\t60\n", 0.5, "", "b1130");
    ASSERT_FALSE(frames.empty()) << "the b1130 harness run failed";
    checkB1130Frame(frames, true, device_->getVideoDriver());
    cleanDirs();
}

TEST_F(FxHarness, B1130SelfPresetSolvesTheCasterAlone) {
    const fs::path frames = runPlan("b1130self", "100.0\t93\tself\t0\t250\t60\n", 0.5, "", "b1130-self");
    ASSERT_FALSE(frames.empty()) << "the b1130-self harness run failed";
    checkB1130Frame(frames, false, device_->getVideoDriver());
    cleanDirs();
}

// B1157 (#476): the harness caster plays the spell's CastingAnim through the
// CLIENT's own code (spell_cast_animation.h, the body
// EntityRenderer::startEntityCast runs on OP_BeginCast) -- started at BeginCast,
// held through the channel, ended at Action. The pre-B1157 rule (an o01 idle
// looping during the channel, a guessed t04/t05/t06 once at completion) is
// gone; every B1149-B1152 gate number was measured on that idle caster.
//
// For 93/38/54 the harness's chosen track + start time == the client path's
// for the same spell: both sides are CALLED here. The client path is an
// in-process EntityRenderer (EDT_NULL, the Dark Elf female -- the B1130
// oracle's caster) driven through startEntityCast and read off at the
// return; the harness path is the binary's own run, whose BeginCast event
// resolves the track with the same function and logs it, at timing.tsv's
// BeginCast event time (the plan's t_begin).
TEST_F(FxHarness, TheCasterPlaysTheSpellCastAnimThroughTheClientCode) {
    const std::string dbPath = WILLEQ_DB_REAL_PATH;
    if (!fs::exists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
    EQT::WilleqDatabase db;
    ASSERT_TRUE(db.open(dbPath)) << dbPath;
    EQT::WilleqDatabase::setGlobalInstance(&db);
    const fs::path client(kClient);
    if (!fs::exists(client / "global_chr.s3d")) GTEST_SKIP() << "no client tree with global_chr.s3d at " << client;
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
    auto tex = std::make_unique<ConstrainedTextureCache>(config);
    tex->setDriver(device_->getVideoDriver());
    auto ents = std::make_unique<EntityRenderer>(device_->getSceneManager(), device_->getVideoDriver(),
                                                 device_->getFileSystem());
    ents->setClientPath(std::string(kClient) + "/");
    ents->setConstrainedTextureCache(tex.get());
    if (!ents->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";

    uint16_t spawn = 6001;
    for (const uint32_t id : {93u, 38u, 54u}) {
        const auto info = db.getSpell(id);
        ASSERT_EQ(info.id, id) << "the row is in the shipped db";
        ASSERT_EQ(info.castAnim, 44) << "spell " << id << ": the db's field 120";
        ASSERT_GT(info.castTimeMs, 0u);

        // The client path: the bridge's body on the model, read off at the
        // return -- the track the spell's CastingAnim names, started at the
        // packet (the return), for the db's cast time.
        ASSERT_TRUE(ents->registerEntity(spawn, 6, "Caster", 0.0f, 0.0f, 0.0f, 0.0f, false, 1,
                                         EntityAppearance(), true, false, 6.0f, 10)) << "spell " << id;
        ASSERT_TRUE(ents->buildEntityMesh(spawn)) << "spell " << id;
        ents->startEntityCast(spawn, id, info.name, info.castTimeMs, static_cast<int>(info.castAnim));
        const auto e = ents->getEntities().find(spawn);
        ASSERT_NE(e, ents->getEntities().end());
        const std::string clientTrack = e->second.castAnimCode;
        ASSERT_EQ(clientTrack, "t06") << "spell " << id << ": the client's track";
        EXPECT_LT(e->second.animatedNode->getAnimator().getProgress(), 0.05f)
            << "the client starts the track AT the packet (spell " << id << ")";
        ents->completeEntityCast(spawn);
        ++spawn;

        // The harness path: the same spell through the binary, the db's cast
        // time, level 60, BeginCast at 0.5 s.
        const std::string rows =
            "0.5\t" + std::to_string(id) + "\tother\t14\t" + std::to_string(info.castTimeMs) + "\t60\tbegincast\n";
        const double seconds = 0.5 + info.castTimeMs / 1000.0 + 1.0;
        const fs::path frames = runPlan("b1157_" + std::to_string(id), rows, seconds);
        ASSERT_FALSE(frames.empty()) << "the harness run for spell " << id << " failed";
        const fs::path dir = frames.parent_path();

        std::string log;
        {
            std::ifstream f(dir / "run.log");
            std::string line;
            while (std::getline(f, line))
                if (line.rfind("[fx-harness] BeginCast " + std::to_string(id) + ":", 0) == 0) log = line;
        }
        ASSERT_FALSE(log.empty()) << "no cast-anim line for spell " << id << " in " << (dir / "run.log").string();
        EXPECT_NE(log.find(" caster cast-anim " + clientTrack + " (CastingAnim 44)"), std::string::npos)
            << "the harness's track differs from the client's: " << log;

        // The start time: the harness starts the track at the BeginCast EVENT
        // (timing.tsv's BeginCast row, t_event == the plan's 0.5 s) -- the
        // same instant the client starts it at the packet.
        double beginT = -1.0;
        {
            std::ifstream f(frames / "timing.tsv");
            std::string line;
            std::getline(f, line);  // header
            while (std::getline(f, line)) {
                std::istringstream ss(line);
                int fr;
                double t, ev;
                std::string event, spell, target, dist;
                uint32_t castMs;
                if (!(ss >> fr >> t >> event >> spell >> target >> dist >> castMs >> ev)) continue;
                if (event == "BeginCast" && spell == std::to_string(id)) beginT = ev;
            }
        }
        EXPECT_NEAR(beginT, 0.5, 1e-6) << "the harness's BeginCast fell off the plan time (spell " << id << ")";
    }
    EQT::WilleqDatabase::setGlobalInstance(nullptr);
    cleanDirs();
}

}  // namespace
