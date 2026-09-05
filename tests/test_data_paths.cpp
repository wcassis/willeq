// T397 Leg A (refactor/T397_spell_effects_config_path.md, board B221):
// config/spell_effects.json is resolved like a shipped data file, not like a
// path that happens to work when you start the client from the repo root.
//
// THE DEFECT. ParticleManager::init() calls
// SpellEffectsConfig::load("config/spell_effects.json"). That literal is
// resolved against the CURRENT WORKING DIRECTORY. Started from anywhere else
// -- a packaged install, a systemd unit, `cd build && ./bin/willeq` -- the
// open fails, load() logged at WARN and RETURNED TRUE, and every spell preset
// silently came from the built-in style table instead. That table is a
// DIFFERENT effect, not a degraded one: its `impact` burst is 12 particles
// where the shipped file's is 36. (With no load() call at all the presets fall
// all the way to EmitterConfig's struct defaults -- spawnRate 10/s,
// burstCount 0, emitterLifetime 0: a PERMANENT emitter where the shipped
// effect is a 0.1 s one-shot. That is the state T395's worker test found and
// the reason B221 was filed.)
//
// THE THREE NUMBERS this file pins, all measured from the tree:
//   shipped config/spell_effects.json  styles.impact.burstCount == 36
//   built-in setDefaults()             styles["impact"].burstCount == 12
//   no config loaded at all            EmitterConfig default burstCount == 0
//
// THE SEAM. eqt::ResolveDataPath() is deliberately CWD-FIRST, so a client
// started from the repo root reads exactly the file it read before this
// existed and no shipped behaviour (the visual gate included) moves. These
// tests therefore run with the CWD moved AWAY from the repo root, which is
// the only place the bug was ever visible.

#include <gtest/gtest.h>

#include <unistd.h>
#include <limits.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "client/data_paths.h"

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#endif

namespace {

// Move the process CWD somewhere with no config/ tree for the duration of a
// test, and put it back afterwards -- ctest runs the whole binary in one
// process, so a leaked chdir would poison every later test.
class CwdElsewhere : public ::testing::Test {
protected:
    void SetUp() override {
        char buf[PATH_MAX];
        ASSERT_NE(::getcwd(buf, sizeof(buf)), nullptr);
        original_ = buf;
        ASSERT_EQ(::chdir("/"), 0) << "could not leave the working directory";
        // Guard against the test itself being a lie: from "/" the literal
        // must NOT be openable, or the assertions below prove nothing.
        std::ifstream probe("config/spell_effects.json");
        ASSERT_FALSE(probe.is_open())
            << "there is a config/spell_effects.json at the filesystem root; "
               "this test cannot distinguish the CWD path from the resolved one";
    }
    void TearDown() override {
        if (!original_.empty()) {
            EXPECT_EQ(::chdir(original_.c_str()), 0);
        }
    }
    std::string original_;
};

// ---------------------------------------------------------------------------
// The resolver itself
// ---------------------------------------------------------------------------

// Pre-fix this whole header does not exist. Post-fix: from a CWD with no
// config/ tree, the shipped file is still found -- via the executable's own
// directory (<repo>/build/bin/test_data_paths -> <repo>/config/...).
TEST_F(CwdElsewhere, TheShippedConfigIsFoundFromAWorkingDirectoryThatHasNone) {
    const std::string resolved = eqt::ResolveDataPath("config/spell_effects.json");
    EXPECT_NE(resolved, "config/spell_effects.json")
        << "the resolver handed back the CWD-relative literal, which does not exist here";
    std::ifstream f(resolved);
    EXPECT_TRUE(f.is_open()) << "resolved to '" << resolved << "', which does not open";
}

// An absolute path is already resolved and must never be rewritten -- T395's
// impact test passes WILLEQ_REPO_ROOT "/config/spell_effects.json".
TEST_F(CwdElsewhere, AnAbsolutePathIsReturnedUnchanged) {
    const std::string abs = std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
    EXPECT_EQ(eqt::ResolveDataPath(abs), abs);
}

// A file that exists nowhere comes back as the caller wrote it, so the
// caller's own "could not open X" names the X it asked for rather than some
// invented candidate.
TEST_F(CwdElsewhere, AnUnresolvablePathIsReturnedUnchanged) {
    const std::string rel = "config/t397_no_such_file_anywhere.json";
    EXPECT_EQ(eqt::ResolveDataPath(rel), rel);
}

// The CWD-relative candidate is tried FIRST. This is the property that makes
// the change safe for the visual gate and every script that runs from the
// repo root: whatever those runs read before, they read now.
TEST(DataPaths, TheCwdRelativePathWinsWhenItExists) {
    char buf[PATH_MAX];
    ASSERT_NE(::getcwd(buf, sizeof(buf)), nullptr);
    const std::string cwd = buf;
    const std::string rel = "t397_cwd_first_probe.json";
    {
        std::ofstream out(rel);
        ASSERT_TRUE(out.is_open()) << "cannot write a probe file into " << cwd;
        out << "{}\n";
    }
    EXPECT_EQ(eqt::ResolveDataPath(rel), rel);
    ::unlink(rel.c_str());
}

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

// ---------------------------------------------------------------------------
// The consumer: the exact call ParticleManager::init() makes
// ---------------------------------------------------------------------------

using EQT::Graphics::Environment::SpellEffectsConfig;
namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;

// The shipped burst count, read out of config/spell_effects.json.
constexpr int kShippedImpactBurst = 36;
// What setDefaults() puts there instead when the file is missing.
constexpr int kBuiltInImpactBurst = 12;

// PRE-FIX RED. From a CWD with no config/ tree, the production string opens
// nothing, load() returns true anyway, and the impact preset is the built-in
// 12-particle burst rather than the shipped 36.
TEST_F(CwdElsewhere, TheProductionStringLoadsTheShippedConfigFromAnyDirectory) {
    ASSERT_TRUE(SpellEffectsConfig::instance().load("config/spell_effects.json"))
        << "load() reported failure for the string ParticleManager::init() uses";

    const auto impact = SpellPresets::Impact(glm::vec4(1.0f), 93);
    ASSERT_EQ(impact.emitters.size(), 1u);
    EXPECT_EQ(impact.emitters[0].config.burstCount, kShippedImpactBurst)
        << "the preset is not the shipped impact; " << kBuiltInImpactBurst
        << " means the built-in fallback table, 0 means no config was applied at all";
}

// PRE-FIX RED. A genuinely missing shipped data file must not report success.
// Pre-fix load() returned true for every miss, so no caller could tell the
// difference between "loaded" and "silently a different effect".
TEST_F(CwdElsewhere, AMissingConfigFileIsReportedAsAFailure) {
    EXPECT_FALSE(SpellEffectsConfig::instance().load("config/t397_no_such_file_anywhere.json"))
        << "a missing shipped data file was reported as a successful load";

    // ...and the fallback is still SAFE: the built-in table, not empty state.
    const auto impact = SpellPresets::Impact(glm::vec4(1.0f), 93);
    ASSERT_EQ(impact.emitters.size(), 1u);
    EXPECT_EQ(impact.emitters[0].config.burstCount, kBuiltInImpactBurst)
        << "the built-in fallback table did not apply";
}

// Leave the singleton holding the shipped config: it is process-global and
// other tests in other binaries assume nothing, but within this binary the
// ordering should not matter. Re-load explicitly so it never matters.
TEST(DataPaths, ZzzRestoreTheShippedConfigForTheSingleton) {
    const std::string abs = std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
    EXPECT_TRUE(SpellEffectsConfig::instance().load(abs));
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

}  // namespace
