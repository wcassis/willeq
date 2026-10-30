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
#include "../tools/model_viewer/fx_target_anchor.h"
#include "../tools/model_viewer/fx_capture_pose.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
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
        lastStatus_ = std::system(cmd.c_str());
        return lastStatus_ == 0 ? frames : fs::path();
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
    // B1171 (#490): timing.tsv's caster_level per packet row, "event:level"
    // in file order (the per-frame rows carry '-').
    static std::vector<std::string> timingLevels(const fs::path& frames) {
        std::vector<std::string> out;
        std::ifstream f(frames / "timing.tsv");
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "frame\tt\tevent\tspell\ttarget\tdistance\tcast_ms\tt_event\tcaster_level");
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string c[9];
            for (auto& x : c) std::getline(ss, x, '\t');
            if (c[2] != "-") out.push_back(c[2] + ":" + c[8]);
        }
        return out;
    }
    // B1171: the run log's "[fx-harness] cast N: ..." lines.
    static std::vector<std::string> castLevelLines(const fs::path& frames) {
        std::vector<std::string> out;
        std::ifstream f(frames.parent_path() / "run.log");
        std::string line;
        while (std::getline(f, line))
            if (line.rfind("[fx-harness] cast ", 0) == 0) out.push_back(line);
        return out;
    }
    void cleanDirs() {
        if (HasFailure()) return;
        for (const auto& d : dirs_) fs::remove_all(d);
    }
    irr::IrrlichtDevice* device_ = nullptr;
    std::string bin_;
    std::vector<fs::path> dirs_;
    int lastStatus_ = 0;  // runPlan's raw std::system status (B1177)
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

// fx_activity.tsv (B1174): per written frame, the harness's live spell
// effects and missiles and the caster's current track.
struct FxActivityRow {
    int frame = 0;
    int effects = 0;
    int missiles = 0;
    std::string anim;
};
std::vector<FxActivityRow> fxActivity(const fs::path& frames) {
    std::vector<FxActivityRow> out;
    std::ifstream f(frames / "fx_activity.tsv");
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        FxActivityRow r;
        double t = 0.0;
        if (ss >> r.frame >> t >> r.effects >> r.missiles >> r.anim) out.push_back(r);
    }
    return out;
}

// B1174 (#493): a /disc is OP_CastSpell slot 10 -> OP_Action with NO
// OP_BeginCast. Its record (field 145: 326 for 4499, 325 for 8604) has
// Source slots only, and the client plays a Source from its begin-cast
// handler only (0x49b39d), so no sprite is drawn (B1144); but the OP_Action
// handler (eqgame 0x47f4af, spell_action_anim.h) asks the standing source for
// the spell's CastingAnim. That swing is ALL the oracle shows: B1174's lane-5
// leg b1174_disc8604 (the client's /disc Leechcurse Discipline, the lane's
// row 8604 pointed at its own Leechcurse for the leg) lit the observer's
// caster box Act+0.422..+4.355 with the t06 swing and no sprite
// (refactor/B1174/README.md); B1162's 4499 leg the same with its own track.
// So the action-only harness run draws no slot, no effect and no missile,
// and the caster plays the spell's CastingAnim track at the packet, then
// returns to the idle. Replaces B1162's whole-frame "draws nothing" pins,
// which the swing (a model animation, measured by the oracle) makes wrong.
void expectDiscActionSwing(const std::vector<std::string>& slots, const std::vector<FxActivityRow>& act,
                           const char* track, int heldUntilFrame, int idleByFrame) {
    EXPECT_TRUE(slots.empty()) << "a /disc drew a spellsnew.eff slot";
    ASSERT_GT(act.size(), static_cast<size_t>(idleByFrame)) << "fx_activity.tsv is short";
    for (const auto& r : act) {
        EXPECT_EQ(r.effects, 0) << "frame " << r.frame << ": a /disc left a live spell effect";
        EXPECT_EQ(r.missiles, 0) << "frame " << r.frame;
    }
    EXPECT_EQ(act[14].anim, "p01") << "the caster idles before the packet";
    for (int f = 15; f <= heldUntilFrame; ++f)
        EXPECT_EQ(act[f].anim, track) << "frame " << f << ": the CastingAnim track plays from the packet";
    EXPECT_EQ(act[idleByFrame].anim, "p01") << "the track played out and the idle returned";
}

TEST_F(FxHarness, ADisciplineWithoutBeginCastSwingsTheCasterAndDrawsNoSprite) {
    // 4499 Defensive Discipline: CastingAnim 27 = s01 (eqgame's code table).
    const fs::path disc = runPlan("disc", "0.5\t4499\tself\t0\t0\t60\taction\n", 6.0, "", "b1130-self");
    ASSERT_FALSE(disc.empty()) << "the /disc harness run failed";
    ASSERT_STREQ(EQ::SpellCastAnimationCode(27), "s01");
    // s01 on the char-67 Dark Elf female plays 3.0 s (fx_activity.tsv on this
    // tip: s01 frames 15..104); held to Act+2.5, idle by Act+4.0.
    expectDiscActionSwing(slotRows(disc), fxActivity(disc), "s01", 15 + 75, 15 + 120);
    cleanDirs();
}

TEST_F(FxHarness, TheLeechcurseDisciplineSwingsT06AndDrawsNoSprite) {
    // 8604 Leechcurse Discipline: CastingAnim 44 = t06, the Dark Elf female's
    // 4100 ms track (B1148). The oracle's caster box: Act+0.422..+4.355.
    const fs::path disc = runPlan("disc8604", "0.5\t8604\tself\t0\t0\t60\taction\n", 6.0, "", "b1130-self");
    ASSERT_FALSE(disc.empty()) << "the /disc harness run failed";
    ASSERT_STREQ(EQ::SpellCastAnimationCode(44), "t06");
    expectDiscActionSwing(slotRows(disc), fxActivity(disc), "t06", 15 + 105, 15 + 135);
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
    // B1171 (#490): the level each run cast at is in its log and its timing.tsv.
    EXPECT_EQ(timingLevels(l1), (std::vector<std::string>{"BeginCast:1", "Action:1"}));
    EXPECT_EQ(timingLevels(l60), (std::vector<std::string>{"BeginCast:60", "Action:60"}));
    EXPECT_EQ(castLevelLines(l1), (std::vector<std::string>{"[fx-harness] cast 0: spell 93 caster_level 1 (plan)"}));
    EXPECT_GT(sum1, 0) << "the level-1 caster's hand slots drew nothing";
    EXPECT_LT(sum1, sum60) << "the level-1 caster drew as much Source as the level-60 caster";
    cleanDirs();
}

// B1171 (#490): a plan row without a level casts at --caster-level, and the
// run log says the level did not come from the plan -- a sweep that means to
// copy an oracle capture's `#level` writes it into every plan row.
TEST_F(FxHarness, ThePlanLevelAndItsSourceAreLogged) {
    const fs::path dflt = runPlan("lvl_dash", "0.1\t93\tother\t14\t200\t-\tbegincast\n", 0.5, "--caster-level 7");
    ASSERT_FALSE(dflt.empty());
    EXPECT_EQ(castLevelLines(dflt),
              (std::vector<std::string>{"[fx-harness] cast 0: spell 93 caster_level 7 (--caster-level)"}));
    EXPECT_EQ(timingLevels(dflt), (std::vector<std::string>{"BeginCast:7", "Action:7"}));
    cleanDirs();
}

// B1155 (#475): --camera b1130 / b1130-self solve B1130's fixed-geometry
// stills (review/B1130/README.md "Geometry"): the B1118 opening pose with the
// caster 209 px, feet row 598 at column 640. One written frame per preset (the
// cast lies at 100 s, past it); the solver's pixels are asserted from that
// frame, with the same off-clear-colour rule the solver corrects on.
//
// B1166 (#485): the b1130 target is the oracle's level-1 human male (race 1,
// gender 0, size 6 -- the r51 capture's own OP_NewSpawn for TrollF000,
// refactor/B1166/README.md), solved on its STANDING frame (no longer the rest
// pose, whose 50 px strip put the running figure's feet 130 px off) so its
// feet land on the oracle target's MEASURED feet (row 615, column 940;
// fx_target_anchor.h -- B1130's 652/325 began at the name label). Its height
// is the model's own at that spot (two feet pixels fix x, z): the harness's
// self-check asserts it equals the solved standing height and the MEASURED
// standing height 294 +-3 px (1.021x the oracle's 288 at the capture-heading pose, the
// merge with B1172's actor scale 1.2; fx_target_anchor.h kStandingPx, re-measured on the
// merged binary), and prints PASS in run.log.
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
            "[fx-gate] b1130 target rows %d..%d (%d px, feet row %d), column %.1f (oracle r51 target: feet 615 "
            "at 940, 288 px)\n",
            t.top, t.bottom, t.bottom - t.top + 1, t.bottom, t.col);
        const auto& a = fxharness::kB1130TargetAnchor;
        EXPECT_NEAR(t.bottom, a.feetY, fxharness::kFeetTolPx) << "target feet row (the oracle's measured feet)";
        EXPECT_NEAR(t.col, a.feetX, fxharness::kColumnTolPx) << "target column centroid";
        EXPECT_NEAR(t.bottom - t.top + 1, fxharness::kStandingPx, fxharness::kStandingTolPx)
            << "target pixel height: not the measured standing human (294 px back-turned at the client's actor scale 1.2, the B1167/B1172 merge; the B1155 rest-pose strip was 50 px)";
    }
    img->drop();
}

TEST_F(FxHarness, B1130PresetSolvesTheFixedNpcGeometry) {
    const fs::path frames = runPlan("b1130", "100.0\t93\tother\t0\t250\t60\n", 0.5, "", "b1130");
    ASSERT_FALSE(frames.empty()) << "the b1130 harness run failed (exit 5 = the target self-check)";
    checkB1130Frame(frames, true, device_->getVideoDriver());
    // B1166: the harness's own self-check ran on frame 0 and passed.
    std::ifstream log(frames.parent_path() / "run.log");
    std::string line, check;
    while (std::getline(log, line))
        if (line.find("b1130 target self-check") != std::string::npos) check = line;
    std::printf("%s\n", check.c_str());
    EXPECT_NE(check.find(": PASS"), std::string::npos) << "the b1130 target self-check did not PASS";
    cleanDirs();
}

// B1166 (MR !320 note 7834 finding 3): a WRONG body at the anchor must fail the
// harness's own self-check, and the run must exit non-zero (5) with a FAIL
// line. B1172: the wrong body is the human male at size 5 -- actor scale
// 5 x 0.2 = 1.0, i.e. the human exactly as fable drew it before B1172 --
// which rendered 238 px face-on (review/B1172/worker/wrong_model/r1_1_0_5.log),
// inside the earlier +-25 %-of-288 bound. B1167/B1172 merge: the preset now
// turns the target to its capture heading, where the same body renders 246 px
// (setme_b1130_wrong, /mnt/projects/fable/review/B1167/continuation/merge/
// measure/) -- still inside the old bound, still outside the 294 +-3 ratchet.
// (B1166 used the dark-elf female 6:1:6; at the
// client's actor scale her solve does not converge on every renderer -- it
// exits "did not solve" on gitlab-runner-1, r1_6_1_6.log -- so it no longer
// reaches the self-check everywhere.)
TEST_F(FxHarness, B1130SelfCheckRejectsAWrongTargetModel) {
    const fs::path frames = runPlan("b1130wrong", "100.0\t93\tother\t0\t250\t60\n", 0.5, "--target 1:0:5", "b1130");
    EXPECT_TRUE(frames.empty()) << "the b1130 run with the size-5 (scale 1.0) human target exited 0";
    std::ifstream log(dirs_.back() / "run.log");
    std::string line, check;
    while (std::getline(log, line))
        if (line.find("b1130 target self-check") != std::string::npos) check = line;
    std::printf("%s\n", check.c_str());
    EXPECT_NE(check.find(": FAIL"), std::string::npos) << "no FAILing self-check line for the wrong model";
    // Its height (246 px back-turned at scale 1.0; 238 px face-on before the
    // turn): outside the ratchet, inside the old bound.
    int px = 0;
    const auto at = check.find("px, feet row");
    ASSERT_NE(at, std::string::npos) << check;
    const auto open = check.rfind('(', at);
    ASSERT_EQ(std::sscanf(check.c_str() + open, "(%d px", &px), 1) << check;
    EXPECT_GT(std::fabs(px - fxharness::kStandingPx), fxharness::kStandingTolPx) << px << " px";
    EXPECT_GE(px, 216) << "the wrong body left the old +-25 % bound; the negative no longer shows finding 3";
    cleanDirs();
}

TEST_F(FxHarness, B1130SelfPresetSolvesTheCasterAlone) {
    const fs::path frames = runPlan("b1130self", "100.0\t93\tself\t0\t250\t60\n", 0.5, "", "b1130-self");
    ASSERT_FALSE(frames.empty()) << "the b1130-self harness run failed";
    checkB1130Frame(frames, false, device_->getVideoDriver());
    cleanDirs();
}

// B1177 (#496): a --camera preset that does not solve exits with its
// documented code 2 (fxharness::kExitPresetDidNotSolve; refactor/B1155/README.md
// "The solve") through the normal teardown -- before B1177 this path dropped
// the device and left the texture caches to the exit handlers, which died in
// ~ConstrainedTextureCache (SIGSEGV, rc 139 through the shell;
// review/B1177/worker/runs/tp_gdb_iter1.log). --target-fit-iterations 1 lets
// the solve measure only its start point, so it cannot solve on any renderer.
TEST_F(FxHarness, B1130PresetThatDoesNotSolveExitsTwoWithoutASignal) {
    const fs::path frames =
        runPlan("b1130nosolve", "100.0\t93\tother\t0\t250\t60\n", 0.5, "--target-fit-iterations 1", "b1130");
    EXPECT_TRUE(frames.empty()) << "a one-fit solve exited 0";
    ASSERT_FALSE(WIFSIGNALED(lastStatus_)) << "the shell itself died of signal " << WTERMSIG(lastStatus_);
    ASSERT_TRUE(WIFEXITED(lastStatus_));
    // The shell reports a child killed by signal n as 128 + n (139 = SIGSEGV).
    EXPECT_EQ(WEXITSTATUS(lastStatus_), fxharness::kExitPresetDidNotSolve)
        << "exit " << WEXITSTATUS(lastStatus_) << " (128 + n = killed by signal n); see " << (dirs_.back() / "run.log");
    std::ifstream log(dirs_.back() / "run.log");
    std::string line, closest;
    bool didNotSolve = false;
    while (std::getline(log, line)) {
        if (line.find("did not solve to its recorded geometry") != std::string::npos) didNotSolve = true;
        if (line.find("b1130 target solve: no convergence in 1 fits") != std::string::npos) closest = line;
    }
    EXPECT_TRUE(didNotSolve) << "no 'did not solve' line";
    std::printf("%s\n", closest.c_str());
    EXPECT_NE(closest.find("closest (x -2.000, z 2.000)"), std::string::npos)
        << "the diagnostic does not name the one measured candidate: " << closest;
    cleanDirs();
}

// B1177 (#496): the dark-elf female 6:1:6 target (B1166's first wrong body)
// sent the solve around a 616/616/618 feet-row cycle on llvmpipe until the cap
// (review/B1172/worker/wrong_model/r1_6_1_6.log). The solve must now TERMINATE
// on every renderer and reach the self-check, which rejects her height (exit 5
// with a FAIL line), never "did not solve" (2) or a signal.
TEST_F(FxHarness, B1130DarkElfFemaleTargetSolvesAndFailsTheSelfCheck) {
    const fs::path frames = runPlan("b1130de", "100.0\t93\tother\t0\t250\t60\n", 0.5, "--target 6:1:6", "b1130");
    EXPECT_TRUE(frames.empty()) << "the b1130 run with a dark-elf female target exited 0";
    ASSERT_TRUE(WIFEXITED(lastStatus_));
    EXPECT_EQ(WEXITSTATUS(lastStatus_), 5) << "see " << (dirs_.back() / "run.log");
    std::ifstream log(dirs_.back() / "run.log");
    std::string line, check, solved;
    while (std::getline(log, line)) {
        if (line.find("b1130 target self-check") != std::string::npos) check = line;
        if (line.find("camera b1130: target (") != std::string::npos) solved = line;
    }
    std::printf("%s\n%s\n", solved.c_str(), check.c_str());
    EXPECT_NE(solved.find("feet row 615 at column 940"), std::string::npos) << "the solve did not reach the anchor";
    EXPECT_NE(check.find(": FAIL"), std::string::npos) << "no FAILing self-check line for the wrong model";
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

// B1166 (#485): the target-anchor geometry, offline (no harness run): the
// silhouette rule, the search window and the self-check's verdicts on
// synthetic frames -- the standing body at the anchor passes, the B1155
// rest-pose strip (50 px at the right feet) and a body 2 px off the feet fail.
namespace {
struct SynthFrame {
    int w = 1280, h = 800;
    std::vector<int> rgb;  // packed 0xRRGGBB, clear colour 50,50,80
    SynthFrame() : rgb(1280 * 800, (50 << 16) | (50 << 8) | 80) {}
    void box(int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) rgb[y * w + x] = (200 << 16) | (120 << 8) | 60;
    }
    fxharness::Silhouette measure(const fxharness::TargetAnchor& a) const {
        const auto win = fxharness::targetWindow(a, h);
        return fxharness::measureSilhouette(
            [this](int x, int y, int& r, int& g, int& b) {
                const int c = rgb[y * w + x];
                r = (c >> 16) & 255;
                g = (c >> 8) & 255;
                b = c & 255;
            },
            w, h, win.x0, win.y0, win.x1, win.y1);
    }
};
}  // namespace

TEST(B1166TargetAnchor, TheAnchorIsTheOracleTargetsMeasuredBox) {
    const auto& a = fxharness::kB1130TargetAnchor;
    EXPECT_FLOAT_EQ(a.feetX, 940.0f);
    EXPECT_FLOAT_EQ(a.feetY, 615.0f);
    EXPECT_FLOAT_EQ(a.px, 288.0f);
    // The clear colour and the off-clear rule the solver and the frames share.
    EXPECT_FALSE(fxharness::offClear(50, 50, 80));
    EXPECT_FALSE(fxharness::offClear(54, 54, 84));  // |4|+|4|+|4| = 12, still clear
    EXPECT_TRUE(fxharness::offClear(55, 54, 84));
    // The window keeps the caster (column 640) out.
    const auto win = fxharness::targetWindow(a, 800);
    EXPECT_EQ(win.x0, 796);
    EXPECT_EQ(win.x1, 1084);
    EXPECT_GT(win.x0, 700);
}

TEST(B1166TargetAnchor, AStandingBodyOnTheAnchorPasses) {
    const auto& a = fxharness::kB1130TargetAnchor;
    SynthFrame f;
    f.box(620, 389, 660, 598);  // the caster, outside the window
    f.box(920, 616 - 294, 960, 615);  // 294 px, feet 615, centroid 940 (the back-turned target)
    const auto s = f.measure(a);
    EXPECT_EQ(s.top, 616 - 294);
    EXPECT_EQ(s.bottom, 615);
    EXPECT_EQ(s.height(), 294);
    EXPECT_DOUBLE_EQ(s.col, 940.0);
    const auto c = fxharness::checkTargetAnchor(s, a, 294.0f);
    EXPECT_TRUE(c.ok());
    EXPECT_NEAR(c.ratio, 294 / 288.0, 1e-6f);
}

// The standing-height ratchet (fx_target_anchor.h kStandingPx 294 +-3, the back-turned target at
// the client's actor scale 1.2, the B1167/B1172 merge): the heights other bodies rendered at the
// anchor with `--target` -- B1166's at scale 1.0 face-on
// (review/B1166/worker/wrong_model/wrong_model_heights.tsv) and B1172's at scale 1.2 face-on
// (review/B1172/worker/wrong_model/*.log) -- all fail it, though every one of them passed the
// earlier +-25 %-of-288 bound (216..360 px); 291..297 passes. (The barbarian 2:0:6,
// 284 px face-on, is the same-height body.) Each body is fitted to itself (fitPx = its own
// height), so only the ratchet can reject it.
TEST(B1166TargetAnchor, TheStandingHeightRatchetRejectsOtherBodies) {
    const auto& a = fxharness::kB1130TargetAnchor;
    EXPECT_FLOAT_EQ(fxharness::kStandingPx, 294.0f);
    EXPECT_NEAR(fxharness::kStandingPx / a.px, 1.021f, 0.001f);
    struct Body { const char* what; int px; };
    for (const Body b : {Body{"human female 1:1:6", 260}, Body{"dark-elf female 6:1:6 (the caster model)", 261},
                         Body{"human male at size 7", 332}, Body{"troll male 9:0:6", 280},
                         Body{"the human at fable's pre-B1172 scale 1.0", 238}, Body{"the old bound's low edge", 216},
                         Body{"the old bound's high edge", 360}}) {
        SynthFrame f;
        f.box(920, 616 - b.px, 960, 615);
        const auto s = f.measure(a);
        ASSERT_EQ(s.height(), b.px) << b.what;
        const auto c = fxharness::checkTargetAnchor(s, a, static_cast<float>(b.px));
        EXPECT_TRUE(c.feetOk && c.colOk && c.pxOk) << b.what;
        EXPECT_FALSE(c.standOk) << b.what << " passed the standing-height ratchet";
        EXPECT_FALSE(c.ok()) << b.what;
    }
    for (int px : {291, 294, 297}) {
        SynthFrame f;
        f.box(920, 616 - px, 960, 615);
        EXPECT_TRUE(fxharness::checkTargetAnchor(f.measure(a), a, static_cast<float>(px)).ok()) << px << " px";
    }
}

TEST(B1166TargetAnchor, TheRestPoseStripAndAnOffsetBodyFail) {
    const auto& a = fxharness::kB1130TargetAnchor;
    {   // B1155's defect: the fit saw a 50 px strip with its bottom on the feet row
        SynthFrame f;
        f.box(930, 566, 950, 615);
        const auto c = fxharness::checkTargetAnchor(f.measure(a), a, 50.0f);
        EXPECT_TRUE(c.feetOk && c.colOk && c.pxOk);
        EXPECT_FALSE(c.standOk) << "a 50 px body is not the measured 294 px standing human";
        EXPECT_FALSE(c.ok());
    }
    {   // ... and its running frames: the standing body 316 px with feet at 782
        SynthFrame f;
        f.box(896, 467, 1023, 782);
        const auto c = fxharness::checkTargetAnchor(f.measure(a), a, 50.0f);
        EXPECT_FALSE(c.feetOk);
        EXPECT_FALSE(c.pxOk) << "the frame's body is not the body the solve fitted";
        EXPECT_FALSE(c.ok());
    }
    {   // feet 2 px low
        SynthFrame f;
        f.box(920, 333, 960, 617);
        const auto c = fxharness::checkTargetAnchor(f.measure(a), a, 285.0f);
        EXPECT_FALSE(c.feetOk);
        EXPECT_FALSE(c.ok());
    }
    {   // nothing in the window
        SynthFrame f;
        EXPECT_FALSE(fxharness::checkTargetAnchor(f.measure(a), a, 285.0f).ok());
    }
}

// B1167 (#486): the presets' world yaw from the capture facts
// (fx_capture_pose.h; refactor/B1167/README.md sec. 2 and 7).
TEST(B1167CapturePose, TheRigYawPutsTheCameraAtTheCapturesDirHeading) {
    EXPECT_FLOAT_EQ(fxharness::rigYawDeg(fxharness::kB1130NpcPose), 45.0f);    // heading 0 + 135 - 90
    EXPECT_FLOAT_EQ(fxharness::rigYawDeg(fxharness::kB1130SelfPose), 157.5f);  // heading 160 (112.5) + 135 - 90
    EXPECT_FLOAT_EQ(fxharness::rigYawDeg(fxharness::kB1118Pose), 202.5f);      // heading 112.5 + 180 - 90
    // The fitted camera sits on +Z of the caster (angle 90); turned, it sits
    // at the capture's caster heading + its DirHeading (135 deg for B1130's
    // user camera 1, 180 deg for B1118's pinned chase camera).
    for (const auto* pose : {&fxharness::kB1130NpcPose, &fxharness::kB1130SelfPose, &fxharness::kB1118Pose}) {
        float x = 0.0f, z = 30.0f;
        fxharness::turnXZ(fxharness::rigYawDeg(*pose), x, z);
        const float at = std::atan2(z, x) * 180.0f / 3.14159265f;
        const float want = fxharness::headingDeg(pose->casterHeading512 + pose->cameraDirHeading512);
        EXPECT_NEAR(std::remainder(at - want, 360.0f), 0.0f, 1e-3f);
    }
    // The client's heading rule: rotation Y = -heading deg.
    EXPECT_FLOAT_EQ(fxharness::rotYForHeading(160.0f), -112.5f);
}

// B1167's continuation: the actors' fit-frame rotations -- after the rig turn
// subtracts psi, each actor sits at rotYForHeading(its capture heading).
TEST(B1167CapturePose, TheActorsFitOnTheirCaptureHeadingsAndEndAtThem) {
    for (const auto* pose : {&fxharness::kB1130NpcPose, &fxharness::kB1130SelfPose, &fxharness::kB1118Pose}) {
        const float psi = fxharness::rigYawDeg(*pose);
        for (const float h : {pose->casterHeading512, pose->targetHeading512}) {
            const float r0 = fxharness::presetRotYDeg(*pose, h);
            EXPECT_NEAR(std::remainder(r0 - psi - fxharness::rotYForHeading(h), 360.0f), 0.0f, 1e-4f)
                << "the turn does not leave the actor at its capture heading";
        }
    }
    // b1130: the caster three-quarters back (45 deg), the target 22.5 deg off
    // face-on (-67.5); b1118: both fully back to the chase camera (90).
    EXPECT_FLOAT_EQ(fxharness::presetRotYDeg(fxharness::kB1130NpcPose, 0.0f), 45.0f);
    EXPECT_FLOAT_EQ(fxharness::presetRotYDeg(fxharness::kB1130NpcPose, 160.0f), -67.5f);
    EXPECT_FLOAT_EQ(fxharness::presetRotYDeg(fxharness::kB1118Pose, 160.0f), 90.0f);
}

TEST(B1167CapturePose, TheTurnedTargetStandsOnTheCapturesBearing) {
    // B1166's solved target offset (review/B1166/worker/probe/after.log:125:
    // x -8.888, z 0.924, the face-on target), turned by the npc yaw, against
    // the capture's offset from the caster: #spawn at (-74, 428), caster
    // #goto (-67.6, 434.4) in server x/y, i.e. (zone x, zone y) = (server y,
    // server x) = (-6.4, -6.4) -> Irrlicht (x, z) = (-6.4, -6.4). The continuation
    // re-measured the solve on the turned target (heading 160,
    // 22.5 deg off face-on): x -8.685, z 0.272 (review/B1167/worker/settle5/
    // settle5_b1130/run.log, tgt fit 2; post-turn target -6.334/-5.949 in the
    // same log's rig line).
    struct Probe { const char* what; float x, z; float tolAfterDeg; float tolUnits; };
    // The face-on solve lands 5.9 deg / 0.94 units from the capture; the
    // turned solve 1.6 deg / 0.49 units. The face-on probe is the case that
    // proves the turn is doing the work: 50.9 deg off the bearing pre-turn.
    // The turned probe sits 47.0 deg off pre-turn (its heading is already
    // 22.5 deg off face-on), so it does not claim the >50 deg bound.
    for (const Probe& p : {Probe{"face-on (B1166)", -8.888f, 0.924f, 6.0f, 1.0f},
                           Probe{"turned (B1167 cont.)", -8.685f, 0.272f, 3.0f, 0.6f}}) {
        const float cx = -6.4f, cz = -6.4f;
        float x = p.x, z = p.z;
        const float before = std::atan2(z, x) - std::atan2(cz, cx);
        fxharness::turnXZ(fxharness::rigYawDeg(fxharness::kB1130NpcPose), x, z);
        const float after = std::atan2(z, x) - std::atan2(cz, cx);
        const float d = std::fabs(std::remainder(after * 180.0f / 3.14159265f, 360.0f));
        EXPECT_LT(d, p.tolAfterDeg) << p.what << ": turned target bearing off the capture's by " << d << " deg";
        EXPECT_LT(std::hypot(x - cx, z - cz), p.tolUnits) << p.what << ": turned target " << x << "/" << z;
        if (p.what[0] == 'f')
            EXPECT_GT(std::fabs(std::remainder(before * 180.0f / 3.14159265f, 360.0f)), 50.0f)
                << "the un-turned rig already matched the capture";
    }
}

// B1177 (#496): the solve's cycle detector and its diagnostic, offline.
TEST(B1177TargetSolve, ThePreB1177CycleIsDetected) {
    // review/B1172/worker/wrong_model/r1_6_1_6.log, tgt fit 0..14 (the
    // surface is the requested 1280x800 there, so these are requested px).
    const float rows[] = {631, 621, 624, 626, 633, 620, 616, 616, 618, 616, 616, 618, 616, 616, 618};
    const float cols[] = {709.3f, 912.0f, 906.9f, 901.7f, 940.6f, 939.5f, 940.3f, 940.2f,
                          948.8f, 940.2f, 940.1f, 948.7f, 940.1f, 940.0f, 948.6f};
    std::vector<fxharness::FitSample> s;
    int firstCycle = -1;
    for (int i = 0; i < 15; ++i) {
        fxharness::FitSample f;
        f.row = rows[i];
        f.col = cols[i];
        s.push_back(f);
        const int p = fxharness::fitCyclePeriod(s);
        if (p && firstCycle < 0) {
            firstCycle = i;
            EXPECT_EQ(p, 3);
        }
    }
    EXPECT_EQ(firstCycle, 14) << "two full repeats of fits 6..8 end at fit 14";
    // None of the three candidates is within |d| < 1 px of (940, 615).
    const auto& a = fxharness::kB1130TargetAnchor;
    for (int i = 12; i < 15; ++i) EXPECT_FALSE(fxharness::fitSampleSolves(s[i], a)) << i;
    EXPECT_NE(fxharness::fitSampleWhy(s[12], a).find("feet row 616 (d +1: misses |d| < 1)"), std::string::npos)
        << fxharness::fitSampleWhy(s[12], a);
    EXPECT_NE(fxharness::fitSampleWhy(s[14], a).find("column 948.6 (d +8.6: misses |d| < 1)"), std::string::npos)
        << fxharness::fitSampleWhy(s[14], a);
}

TEST(B1177TargetSolve, AStallOrAnApproachIsNotACycle) {
    std::vector<fxharness::FitSample> s;
    auto add = [&](float row, float col) {
        fxharness::FitSample f;
        f.row = row;
        f.col = col;
        s.push_back(f);
    };
    // The same pixels many times over (a sub-pixel stall): period 1 is not a cycle.
    for (int i = 0; i < 9; ++i) add(616, 940.2f);
    EXPECT_EQ(fxharness::fitCyclePeriod(s), 0);
    // A damped approach that swings across the anchor twice (one repeat of a
    // period-2 swing) and then lands: never a cycle.
    s.clear();
    for (float r : {631.f, 621.f, 614.f, 616.f, 614.f, 616.f, 615.f}) {
        add(r, 940.0f);
        EXPECT_EQ(fxharness::fitCyclePeriod(s), 0) << s.size();
    }
    // Two full repeats of the swing are.
    s.clear();
    for (float r : {614.f, 616.f, 614.f, 616.f, 614.f}) add(r, 940.0f);
    EXPECT_EQ(fxharness::fitCyclePeriod(s), 0);
    add(616, 940.0f);
    EXPECT_EQ(fxharness::fitCyclePeriod(s), 2);
    // The anchor itself solves; the documented exit code is 2.
    fxharness::FitSample at;
    at.row = 615;
    at.col = 940.4f;
    EXPECT_TRUE(fxharness::fitSampleSolves(at, fxharness::kB1130TargetAnchor));
    EXPECT_EQ(fxharness::kExitPresetDidNotSolve, 2);
}

// ---------------------------------------------------------------------------
// B1193 (#512): the writer-thread frame output is the sync path's pixels.
// ---------------------------------------------------------------------------

// frame_writer.cpp's --frames-raw dump (P6 PPM, RGB24): the pixel array.
std::vector<unsigned char> readPpmP6(const fs::path& p, int& w, int& h) {
    std::ifstream f(p, std::ios::binary);
    std::string magic;
    w = h = 0;
    if (!(f >> magic) || magic != "P6" || !(f >> w >> h)) return {};
    int maxval = 0;
    if (!(f >> maxval) || maxval != 255) return {};
    f.get();  // the one whitespace after the maxval
    std::vector<unsigned char> px((size_t)w * h * 3);
    f.read((char*)px.data(), (std::streamsize)px.size());
    return f.gcount() == (std::streamsize)px.size() ? px : std::vector<unsigned char>();
}

// The frame_*.png in `dir`, sorted by name.
std::vector<fs::path> pngFrames(const fs::path& dir) {
    std::vector<fs::path> out;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".png" && e.path().filename().string().rfind("frame_", 0) == 0)
            out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

TEST_F(FxHarness, FrameWriterOutputIsPixelIdenticalToTheSyncPath) {
    // 0.2 s at 30 fps = 6 frames, the same plan/seed/camera three ways:
    // --frames-sync (the pre-B1193 render-thread path: scaleImageTo + the
    // driver's libpng writer), the default (the B1193 writer thread: the same
    // bilinear, the PNG at zlib level 0 / filter 0) and --frames-raw (the
    // writer thread's P6 PPM dump). The PNG CONTAINER bytes differ (a
    // different zlib level and filter), so the identity is on the decoded
    // pixels -- which is what every measure reads (cv2's imread), and keeps
    // the gate like with like. The runs are separate processes, so this also
    // rides on B1169's fixed-seed frame identity.
    const auto rows = std::to_string(kBegin) + "\t201\tother\t14\n";
    const fs::path sync = runPlan("b1193sync", rows, 0.2, "--frames-sync");
    ASSERT_FALSE(sync.empty()) << "the sync run failed (see its run.log)";
    const fs::path write = runPlan("b1193write", rows, 0.2, "");
    ASSERT_FALSE(write.empty()) << "the writer-thread run failed (see its run.log)";
    const fs::path raw = runPlan("b1193raw", rows, 0.2, "--frames-raw");
    ASSERT_FALSE(raw.empty()) << "the raw run failed (see its run.log)";
    const auto aFrames = pngFrames(sync);
    const auto bFrames = pngFrames(write);
    ASSERT_EQ(aFrames.size(), bFrames.size());
    ASSERT_GE(aFrames.size(), 6u) << "0.2 s at 30 fps is 6 frames";
    for (size_t i = 0; i < aFrames.size(); ++i) {
        irr::video::IImage* a = load(aFrames[i]);
        irr::video::IImage* b = load(bFrames[i]);
        ASSERT_NE(a, nullptr) << aFrames[i];
        ASSERT_NE(b, nullptr) << bFrames[i];
        ASSERT_EQ(a->getDimension(), b->getDimension()) << aFrames[i];
        const auto d = a->getDimension();
        int firstDiff = -1;
        for (int y = 0; y < d.Height && firstDiff < 0; ++y)
            for (int x = 0; x < d.Width; ++x) {
                const auto ca = a->getPixel(x, y), cb = b->getPixel(x, y);
                if (ca.getRed() != cb.getRed() || ca.getGreen() != cb.getGreen() ||
                    ca.getBlue() != cb.getBlue() || ca.getAlpha() != cb.getAlpha()) {
                    firstDiff = y * d.Width + x;
                    break;
                }
            }
        EXPECT_EQ(firstDiff, -1) << aFrames[i].filename() << ": pixel " << firstDiff
                                 << " differs from the sync path";
        // The raw dump: the same pixels with the alpha dropped.
        std::string ppmName = bFrames[i].filename().string();
        ppmName.replace(ppmName.size() - 4, 4, ".ppm");
        int pw = 0, ph = 0;
        const auto px = readPpmP6(raw / ppmName, pw, ph);
        ASSERT_EQ(px.size(), (size_t)pw * ph * 3) << raw / ppmName;
        ASSERT_EQ(pw, (int)d.Width) << ppmName;
        ASSERT_EQ(ph, (int)d.Height) << ppmName;
        int ppmDiff = -1;
        for (int y = 0; y < d.Height && ppmDiff < 0; ++y)
            for (int x = 0; x < d.Width; ++x) {
                const auto c = a->getPixel(x, y);
                const size_t o = ((size_t)y * d.Width + x) * 3;
                if (px[o] != c.getRed() || px[o + 1] != c.getGreen() || px[o + 2] != c.getBlue()) {
                    ppmDiff = y * d.Width + x;
                    break;
                }
            }
        EXPECT_EQ(ppmDiff, -1) << ppmName << ": pixel " << ppmDiff << " differs from the PNG";
        a->drop();
        b->drop();
    }
}
