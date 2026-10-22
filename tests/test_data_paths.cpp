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
#include <sstream>
#include <string>

#include "client/data_paths.h"

// T399 (board B235): the loaders whose CWD-relative literals T399 routed.
#include "client/graphics/constrained_renderer_config.h"
// B1038: the hotkey manager is graphics-only (hotkey_manager.h includes
// irrlicht.h); its case below is compiled out of an EQT_GRAPHICS=OFF build.
#ifdef EQT_HAS_GRAPHICS
#include "client/input/hotkey_manager.h"
#endif
#include "client/skill/skill_cap_table.h"
#include "client/zone_lines.h"

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/detail/detail_config_loader.h"
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

// The discriminator is the impact style's SPAWN RATE, and it has to be a
// three-way one: shipped, built-in-fallback, and never-configured must all be
// distinguishable, or a test that means "the file was found" can pass on a
// file that was not.
//
// T422 moved it off `burstCount`. That used to read 36 shipped / 12 built-in /
// 0 never-configured, but T422 matched the impact style to the official
// client, whose population rises over ~2 s rather than appearing in one frame,
// so the shipped style is now a `spawnRate` emitter and its burstCount is 0 --
// which used to be the "no config was applied at all" value. `spawnRate` reads
// 1214.6 shipped (B1123; 1100 at B1120, 480 at T422) / 0 built-in (setDefaults() still describes the pre-T422
// one-shot) / 10 never-configured (EmitterConfig's own default, board B221's
// "permanent 10/s emitter").
//
// T441 (board B397) made the SPELL ID matter: `spellOverrides` is no longer
// empty (93 Burst of Flame carried its own spawnRate from T441 to B1122).
// The discriminator here is about which FILE was read, not about any one
// spell, so it asks for the STYLE -- spell id 0, which
// SpellEffectsConfig::getStyleSettings() documents as "no per-spell lookup".
// Asking for 93 would have made this test track a per-spell tuning number and
// go red every time a future unit measured that spell again.
//
// B1120 (#440) raised the impact STYLE's population with the landing volume
// (480 -> 1100/s), and B1123 (#443) to the pool bound (1214.6/s); the
// three-way discriminator (shipped / 0 / 10) is intact.
constexpr float kShippedImpactSpawnRate = 1214.6f;
constexpr float kBuiltInImpactSpawnRate = 0.0f;

// PRE-FIX RED. From a CWD with no config/ tree, the production string opens
// nothing, load() returns true anyway, and the impact preset is the built-in
// 12-particle burst rather than the shipped 36.
TEST_F(CwdElsewhere, TheProductionStringLoadsTheShippedConfigFromAnyDirectory) {
    ASSERT_TRUE(SpellEffectsConfig::instance().load("config/spell_effects.json"))
        << "load() reported failure for the string ParticleManager::init() uses";

    const auto impact = SpellPresets::Impact(glm::vec4(1.0f), 0);
    ASSERT_EQ(impact.emitters.size(), 1u);
    EXPECT_FLOAT_EQ(impact.emitters[0].config.spawnRate, kShippedImpactSpawnRate)
        << "the preset is not the shipped impact; " << kBuiltInImpactSpawnRate
        << " means the built-in fallback table, 10 means no config was applied at all";
}

// PRE-FIX RED. A genuinely missing shipped data file must not report success.
// Pre-fix load() returned true for every miss, so no caller could tell the
// difference between "loaded" and "silently a different effect".
TEST_F(CwdElsewhere, AMissingConfigFileIsReportedAsAFailure) {
    EXPECT_FALSE(SpellEffectsConfig::instance().load("config/t397_no_such_file_anywhere.json"))
        << "a missing shipped data file was reported as a successful load";

    // ...and the fallback is still SAFE: the built-in table, not empty state.
    // Style, not a spell id, for the same reason as above -- and setDefaults()
    // clears spellOverrides_ anyway, so a failed load cannot leave the previous
    // file's per-spell tuning standing.
    const auto impact = SpellPresets::Impact(glm::vec4(1.0f), 0);
    ASSERT_EQ(impact.emitters.size(), 1u);
    EXPECT_FLOAT_EQ(impact.emitters[0].config.spawnRate, kBuiltInImpactSpawnRate)
        << "the built-in fallback table did not apply";
    EXPECT_EQ(impact.emitters[0].config.burstCount, 12)
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

// ===========================================================================
// T399 (board B235): the rest of the census, routed.
//
// T397 routed ONE literal (config/spell_effects.json) and boarded the other
// ~40 as B235. These are the tests for the rest. Every one of them runs from
// a working directory with no config/ or data/ tree -- the only place the
// defect was ever visible -- and every one of them is RED before the routing
// commit for its family.
// ===========================================================================

namespace {

// One row per shipped file a routed literal names. Bytes, not existence:
// resolving to *a* file called race_models.json somewhere else would pass an
// existence check and still be the wrong client.
const char* const kShippedLiterals[] = {
    // core / app
    "config/constrained_presets.json",
    "config/debug.json",
    "config/hotkeys.json",
    "config/skill_caps.json",
    "data/willeq.db",
    "data/zone_lines.json",
    "data/zone_lines/qeynos2.json",
    // config/*.json loaders
    "config/display_settings.json",
    "config/race_models.json",
    "config/eqe1_archives.json",
    "config/detail_objects.json",
    "config/tree_wind.json",
    "config/weather_effects.json",
    "config/environment_effects.json",
    "config/spell_effects.json",
    // audio (the two *_index_cache.json entries left this list in T402: they
    // are generated caches, no longer shipped and no longer in the checkout --
    // board B248, and see the CachePaths tests below)
    "config/music_events.json",
    // textures
    "data/textures/particle_atlas.bmp",
    "data/textures/detail_atlas.bmp",
    "data/textures/creature_atlas.bmp",
    "data/textures/tumbleweed.bmp",
    "data/textures/software_cursor.bmp",
    "data/textures/spell_particle_fallback.bmp",
    "data/textures/spell_particle_fallback.bmp",
    "data/textures/storm_cloud_blend_0.bmp",
    "data/textures/rain_streaks.bmp",
    "data/textures/snow_flakes.bmp",
    // the rest
    "data/item_models.json",
    "data/eqe1_texture_index.json",
};

std::string SlurpOrEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// Every shipped literal in the census resolves, from a working directory that
// has neither config/ nor data/, to the SAME BYTES the repo ships.
TEST_F(CwdElsewhere, EveryShippedLiteralResolvesToTheRepoBytes) {
    for (const char* lit : kShippedLiterals) {
        const std::string resolved = eqt::ResolveDataPath(lit);
        const std::string expected = std::string(WILLEQ_REPO_ROOT) + "/" + lit;
        const std::string got = SlurpOrEmpty(resolved);
        ASSERT_FALSE(got.empty())
            << lit << " resolved to '" << resolved << "', which does not open";
        EXPECT_EQ(got, SlurpOrEmpty(expected))
            << lit << " resolved to '" << resolved << "', whose bytes are not the repo's";
    }
}

// --- family: core / app ----------------------------------------------------
//
#ifdef EQT_HAS_GRAPHICS
// B1038: constrained_renderer_config.cpp is a renderer TU (GRAPHICS_SOURCES).
// PRE-FIX RED. loadJsonOverrides() opened the CWD-relative literal and
// returned false SILENTLY, so the renderer ran on the built-in preset numbers
// with nothing said. loadDebugOverrides() was the same shape.
TEST_F(CwdElsewhere, ConstrainedPresetOverridesLoadFromAnyDirectory) {
    EQT::Graphics::ConstrainedRendererConfig cfg =
        EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::OrangePi);
    EXPECT_TRUE(cfg.loadJsonOverrides("orangepi", "config/constrained_presets.json"))
        << "the shipped preset overrides were not applied";
    EXPECT_TRUE(cfg.loadDebugOverrides("config/debug.json"))
        << "the shipped debug skip-flags were not applied";
}
#endif  // EQT_HAS_GRAPHICS

#ifdef EQT_HAS_GRAPHICS
// PRE-FIX RED. The shipped key bindings were silently replaced by the built-in
// defaults (and the miss logged at DEBUG, i.e. nowhere).
TEST_F(CwdElsewhere, HotkeyBindingsLoadFromAnyDirectory) {
    auto& mgr = eqt::input::HotkeyManager::instance();
    mgr.resetToDefaults();
    EXPECT_TRUE(mgr.loadFromFile("config/hotkeys.json"))
        << "the shipped key bindings were not loaded";
}
#endif  // EQT_HAS_GRAPHICS

// PRE-FIX RED. A missing skill_caps.json disables the skill possession gate
// entirely -- the loudest silent default in the census, and it fired for every
// client not started from the repo root.
TEST_F(CwdElsewhere, SkillCapTableLoadsFromAnyDirectory) {
    auto& table = EQ::SkillCapTable::instance();
    EXPECT_TRUE(table.load(EQ::SkillCapTable::kDefaultPath))
        << "the skill possession gate is DISABLED from this working directory";
    EXPECT_GT(table.entryCount(), 0u);
}

// PRE-FIX RED. Zone lines are what make a zone boundary work at all.
TEST_F(CwdElsewhere, ZoneLinesLoadFromAnyDirectory) {
    EQT::ZoneLines lines;
    EXPECT_TRUE(lines.loadFromZone("qeynos2", ""))
        << "no zone line data resolved from this working directory";
}

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

// --- family: config/*.json loaders -----------------------------------------
//
// PRE-FIX RED. Both of these loaders returned TRUE on a miss and quietly
// substituted their built-in table, so the only observable is the path they
// ended up reading. getConfigPath() must name a file that opens.
TEST_F(CwdElsewhere, WeatherAndEnvironmentConfigsLoadFromAnyDirectory) {
    auto& weather = EQT::Graphics::WeatherConfigLoader::instance();
    weather.load("config/weather_effects.json");
    EXPECT_TRUE(std::ifstream(weather.getConfigPath()).is_open())
        << "WeatherConfigLoader settled on '" << weather.getConfigPath()
        << "', which does not open -- the built-in defaults are in effect";

    auto& env = EQT::Graphics::Environment::EnvironmentEffectsConfig::instance();
    env.load("config/environment_effects.json");
    EXPECT_TRUE(std::ifstream(env.getConfigPath()).is_open())
        << "EnvironmentEffectsConfig settled on '" << env.getConfigPath()
        << "', which does not open -- the built-in defaults are in effect";
}

// PRE-FIX RED. loadDefaultConfig() returns nullptr when the file misses, and
// the caller falls back to a hard-coded per-zone table.
TEST_F(CwdElsewhere, DetailObjectConfigLoadsFromAnyDirectory) {
    EQT::Graphics::Detail::DetailConfigLoader loader;
    EXPECT_NE(loader.loadDefaultConfig(""), nullptr)
        << "config/detail_objects.json did not resolve; the hard-coded default is in effect";
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

// ===========================================================================
// T402 (board B248): generated caches resolve OUTSIDE the checkout.
//
// config/snd_index_cache.json and config/gfx_index_cache.json were tracked
// files the client rewrote on every run, so a plain run dirtied the working
// tree. They are caches -- a miss costs a rescan -- so the write side moved to
// a run-local directory. These tests pin the two properties that matter: the
// resolved write path is never inside the repo's config/, and what the write
// side produces is what the read side finds.
// ===========================================================================

TEST(CachePaths, WriteTargetIsNeverInsideTheCheckout) {
    ::unsetenv("WILLEQ_CACHE_DIR");
    const std::string path = eqt::ResolveCachePathForWrite("snd_index_cache.json");
    ASSERT_FALSE(path.empty()) << "no writable cache directory at all";
    EXPECT_EQ(path.find(std::string(WILLEQ_REPO_ROOT) + "/config/"), std::string::npos)
        << "the cache would be written into the checkout: " << path;
    EXPECT_NE(path.find("cache/"), std::string::npos)
        << "expected a cache/ directory, got " << path;
}

TEST(CachePaths, ExplicitCacheDirIsHonouredAndRoundTrips) {
    char tmpl[] = "/tmp/willeq_t402_cacheXXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    const std::string sub = std::string(dir) + "/nested";  // must be created for us

    // A name no other candidate directory can hold. Using a REAL cache name
    // here is a race: the read side searches every candidate, and a
    // test_pfs_sound_load running in parallel writes <exe dir>/cache/
    // snd_index_cache.json, which is a legitimate hit (T402 lost a ctest -j6
    // run to exactly that).
    const char* kName = "t402_roundtrip_cache.json";
    ::setenv("WILLEQ_CACHE_DIR", sub.c_str(), 1);
    const std::string write = eqt::ResolveCachePathForWrite(kName);
    EXPECT_EQ(write, sub + "/" + kName);

    // Nothing written yet: the read side must report a MISS, not a path.
    EXPECT_EQ(eqt::ResolveCachePathForRead(kName), "");

    { std::ofstream(write) << "{}"; }
    EXPECT_EQ(eqt::ResolveCachePathForRead(kName), write);

    ::unsetenv("WILLEQ_CACHE_DIR");
    ::unlink(write.c_str());
    ::rmdir(sub.c_str());
    ::rmdir(dir);
}

TEST(CachePaths, DataRootCacheDirIsUsedWhenSet) {
    char tmpl[] = "/tmp/willeq_t402_rootXXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    ::unsetenv("WILLEQ_CACHE_DIR");
    ::setenv("WILLEQ_DATA_ROOT", dir, 1);

    const std::string write = eqt::ResolveCachePathForWrite("gfx_index_cache.json");
    EXPECT_EQ(write, std::string(dir) + "/cache/gfx_index_cache.json");

    ::unsetenv("WILLEQ_DATA_ROOT");
    ::rmdir((std::string(dir) + "/cache").c_str());
    ::rmdir(dir);
}
