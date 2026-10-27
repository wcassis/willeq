/*
 * test_config_key_reachability.cpp — T414 Leg A (board B297).
 *
 * `DataPathRouting.EveryShippedConfigFileIsNamedByCode` (T402, board B245)
 * proves a shipped config FILE has a reader.  It says nothing about the KEYS
 * inside it, and T414 found twenty-nine of those that no parser looked at:
 * whole sections (`environment_effects.json`'s `detailObjects` and `boids`,
 * `detail_objects.json`'s `seasons`), individual keys whose consumer had been
 * deleted (`constrained_presets.json`'s `enableTextureAtlas`, removed by E10),
 * and — worst — eight keys in `display_settings.json` whose DisplaySettings
 * field had a live consumer all along and simply never got a parse line.
 * Editing any of them changed nothing, silently.
 *
 * Two nets:
 *   EveryShippedConfigKeyIsNamedByCode — the census, over every config/*.json.
 *   The DisplaySettings cases — the wiring, key by key.
 */

#include <gtest/gtest.h>

#include "client/graphics/display_settings_loader.h"

#include <json/json.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kRoot = std::string(WILLEQ_REPO_ROOT) + "/";

std::string ReadFile(const std::string& rel) {
    std::ifstream f(kRoot + rel, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Json::Value ParseFile(const std::string& rel) {
    Json::Value root;
    std::ifstream f(kRoot + rel);
    if (!f) return root;
    Json::CharReaderBuilder b;
    std::string errs;
    Json::parseFromStream(b, f, &root, &errs);
    return root;
}

// ---------------------------------------------------------------------------
// The census
// ---------------------------------------------------------------------------

// A key path prefix whose CHILD keys are data, not code-named settings: a map
// keyed by an id or by a user-chosen name that the loader enumerates with
// getMemberNames() rather than asking for by literal.  Anything listed here
// stops the walk at that node's children (the node's own name is still
// checked).  Each entry names the enumerating call site.
struct MapNode {
    const char* file;
    const char* path;   // "" = the document root
    const char* why;
};

const MapNode kDataKeyedMaps[] = {
    {"constrained_presets.json", "presets",
     "preset names, chosen by the user and passed to --constrained; "
     "ConstrainedRendererConfig::loadPresetsFromJson enumerates "
     "presets.getMemberNames() rather than asking for any by literal"},
    {"spell_effects.json", "spellOverrides",
     "B1153 (#473) + B1161 (#480): SPELL IDS (the five measured clouds that "
     "stay -- 38/41/54/458/656, the file is not the whole story, "
     "refactor/B1153/README.md; the 253 one was retired by B1161: its words "
     "live on the record path now); the style settings under each are "
     "settings again; SpellEffectsConfig::loadSpellOverrides enumerates "
     "root.getMemberNames() and std::stoul's each one, so no source names "
     "38, 41 or 458 as a literal"},
    // B1153 (#473): spell_effects.json's `spellOverrides` block STAYS --
    // the five B1120/B1122 measured clouds (38/41/54/458/656) whose
    // spellsnew file is not the whole story (refactor/B1153/README.md
    // documents each with before/after numbers); B1161 (#480) retired the
    // 253 one of them. spell IDs, and the style settings under each are
    // settings again; SpellEffectsConfig::loadSpellOverrides enumerates
    // root.getMemberNames() and std::stoul's each one, so no source names
    // 38, 41 or 458 as a literal. The ROW blocks it replaced
    // (`sourceRecords.records` / `overrideRecords`, `targetRecords.records`,
    // `animRecords.rows`) are RETIRED: the config carries the two default
    // rows only, and the measured words are the bands in
    // tests/spell_fx_row_oracle.h / tests/spell_fx_shape_oracle.h /
    // tests/spell_fx_population_oracle.h / tests/spell_fx_timing_oracle.h
    // (the field-145 -> row mapping is tests/spell_anim_rows.h).
};

// Whole files that are DATA TABLES, not settings: every key below the root is
// an id, and the file is consumed by enumeration (typically by
// tools/willeq_db_builder, into data/willeq.db).  Named with the consumer.
struct DataFile {
    const char* file;
    const char* why;
};

const DataFile kDataFiles[] = {
    {"race_models.json",
     "race id -> model code table; tools/willeq_db_builder folds it into "
     "data/willeq.db, and the runtime reads the db"},
    {"skill_caps.json",
     "class/skill/level cap table exported from eqemu peq.skill_caps (T159); "
     "tools/willeq_db_builder folds it into data/willeq.db"},
};

bool IsDataFile(const std::string& file) {
    for (const auto& d : kDataFiles) if (file == d.file) return true;
    return false;
}

// Keys that are documentation, not settings.  A loader is not expected to read
// them and their absence from the source is correct.
const char* kDocKeys[] = {"description", "version", "comment",
                          "_comment", "_version", "_notes"};

bool IsDocKey(const std::string& k) {
    for (const char* d : kDocKeys) if (k == d) return true;
    return false;
}

bool IsDataKeyedMap(const std::string& file, const std::string& path) {
    for (const auto& m : kDataKeyedMaps) {
        if (file == m.file && path == m.path) return true;
    }
    return false;
}

void CollectKeys(const std::string& file, const Json::Value& v,
                 const std::string& path, std::set<std::string>& out) {
    if (v.isObject()) {
        const bool childrenAreData = IsDataKeyedMap(file, path);
        for (const auto& k : v.getMemberNames()) {
            if (!childrenAreData) out.insert(k);
            const std::string child = path.empty() ? k : path + "." + k;
            // Below a data-keyed map the ids are data but their CONTENTS are
            // settings again, so keep walking with the id folded out of `path`.
            CollectKeys(file, v[k], childrenAreData ? path : child, out);
        }
    } else if (v.isArray()) {
        for (const auto& e : v) CollectKeys(file, e, path, out);
    }
}

std::vector<std::string> ShippedConfigFiles() {
    std::vector<std::string> files;
    FILE* p = ::popen(("ls '" + kRoot + "config'").c_str(), "r");
    if (!p) return files;
    char buf[4096];
    while (::fgets(buf, sizeof(buf), p)) {
        std::string name(buf);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        if (name.size() < 6 || name.substr(name.size() - 5) != ".json") continue;
        files.push_back(name);
    }
    ::pclose(p);
    return files;
}

std::string SourceHaystack() {
    std::string haystack;
    const char* roots[] = {"src", "include", "tools"};
    for (const char* r : roots) {
        std::string cmd = "find '" + kRoot + r +
                          "' -type f \\( -name '*.cpp' -o -name '*.h' \\)";
        FILE* p = ::popen(cmd.c_str(), "r");
        if (!p) continue;
        char buf[4096];
        while (::fgets(buf, sizeof(buf), p)) {
            std::string path(buf);
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
            if (path.empty()) continue;
            if (path.rfind(kRoot, 0) == 0) path = path.substr(kRoot.size());
            haystack += ReadFile(path);
            haystack += '\n';
        }
        ::pclose(p);
    }
    return haystack;
}

}  // namespace

TEST(ConfigKeyReachability, EveryShippedConfigKeyIsNamedByCode) {
    const std::string haystack = SourceHaystack();
    ASSERT_GT(haystack.size(), 1000000u) << "the source walk came back empty";

    const auto files = ShippedConfigFiles();
    ASSERT_GE(files.size(), 10u) << "config/ looks empty -- did the walk work?";

    std::ostringstream orphanText;
    size_t orphans = 0, checked = 0;
    for (const auto& f : files) {
        const Json::Value root = ParseFile("config/" + f);
        ASSERT_FALSE(root.isNull()) << "config/" << f << " did not parse";
        if (IsDataFile(f)) continue;
        std::set<std::string> keys;
        CollectKeys(f, root, "", keys);
        for (const auto& k : keys) {
            if (IsDocKey(k)) continue;
            ++checked;
            if (haystack.find("\"" + k + "\"") == std::string::npos) {
                ++orphans;
                orphanText << "\n  config/" << f << " : \"" << k << "\"";
            }
        }
    }
    EXPECT_GT(checked, 200u) << "the key walk found suspiciously few keys";

    EXPECT_EQ(orphans, 0u)
        << orphans << " shipped config KEY(S) that no source names:" << orphanText.str()
        << "\nA key nothing parses is a knob a user can turn with no effect (B297)."
           "\nEither add the parse line (and prove it with a test) or delete the key"
           "\nfrom the shipped JSON together with its field. If the key is data --"
           "\nan id or a user-chosen name a loader enumerates -- add it to"
           "\nkDataKeyedMaps in this file with the enumerating call site named.";
}

#ifdef EQT_HAS_GRAPHICS
// B1038: display_settings_loader.cpp is a renderer TU (GRAPHICS_SOURCES); these
// cases are compiled out of an EQT_GRAPHICS=OFF build.
// ---------------------------------------------------------------------------
// display_settings.json -- the wiring, key by key
// ---------------------------------------------------------------------------

TEST(ConfigKeyReachability, DisplaySettingsParsesEveryKeyItShips) {
    // Every value here differs from the struct default, so a missing parse line
    // shows up as the default coming back.
    const char* kJson = R"({
        "renderDistance": 137.5,
        "environmentEffects": {
            "quality": "high",
            "atmosphericParticles": false,
            "ambientCreatures": false,
            "reactiveFoliage": false,
            "rollingObjects": false,
            "skyEnabled": false,
            "animatedTrees": false,
            "fireEffects": false,
            "enableFireGlowLighting": false,
            "enableFireGlowIcospheres": true,
            "maxFireGlowLights": 7,
            "density": 0.125
        },
        "detailObjects": {
            "enabled": false,
            "density": 0.25,
            "grass": false,
            "plants": false,
            "rocks": false,
            "debris": false
        }
    })";

    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream in(kJson);
    ASSERT_TRUE(Json::parseFromStream(b, in, &root, &errs)) << errs;

    eqt::ui::DisplaySettings s;  // defaults
    eqt::ui::ParseDisplaySettings(root, s);

    EXPECT_FLOAT_EQ(s.renderDistance, 137.5f);

    EXPECT_EQ(s.environmentQuality, eqt::ui::EffectQuality::High);
    EXPECT_FALSE(s.atmosphericParticles);
    EXPECT_FALSE(s.ambientCreatures);
    EXPECT_FALSE(s.reactiveFoliage);
    EXPECT_FALSE(s.rollingObjects);
    EXPECT_FALSE(s.skyEnabled);
    EXPECT_FALSE(s.animatedTrees);
    EXPECT_FALSE(s.fireEffects);
    EXPECT_FALSE(s.enableFireGlowLighting);
    EXPECT_TRUE(s.enableFireGlowIcospheres);
    EXPECT_EQ(s.maxFireGlowLights, 7);
    EXPECT_FLOAT_EQ(s.environmentDensity, 0.125f);

    EXPECT_FALSE(s.detailObjectsEnabled);
    EXPECT_FLOAT_EQ(s.detailDensity, 0.25f);
    EXPECT_FALSE(s.detailGrass);
    EXPECT_FALSE(s.detailPlants);
    EXPECT_FALSE(s.detailRocks);
    EXPECT_FALSE(s.detailDebris);
}

TEST(ConfigKeyReachability, DisplaySettingsAbsentKeysKeepDefaults) {
    Json::Value empty(Json::objectValue);
    eqt::ui::DisplaySettings s;
    const eqt::ui::DisplaySettings before = s;
    eqt::ui::ParseDisplaySettings(empty, s);
    EXPECT_FLOAT_EQ(s.renderDistance, before.renderDistance);
    EXPECT_EQ(s.environmentQuality, before.environmentQuality);
    EXPECT_EQ(s.maxFireGlowLights, before.maxFireGlowLights);
    EXPECT_EQ(s.detailGrass, before.detailGrass);
}

TEST(ConfigKeyReachability, EffectQualityNamesRoundTrip) {
    eqt::ui::EffectQuality q = eqt::ui::EffectQuality::Off;
    EXPECT_TRUE(eqt::ui::ParseEffectQuality("Off", q));
    EXPECT_EQ(q, eqt::ui::EffectQuality::Off);
    EXPECT_TRUE(eqt::ui::ParseEffectQuality("low", q));
    EXPECT_EQ(q, eqt::ui::EffectQuality::Low);
    EXPECT_TRUE(eqt::ui::ParseEffectQuality("MEDIUM", q));
    EXPECT_EQ(q, eqt::ui::EffectQuality::Medium);
    EXPECT_TRUE(eqt::ui::ParseEffectQuality("High", q));
    EXPECT_EQ(q, eqt::ui::EffectQuality::High);
    // An unknown name is refused, not silently mapped.
    EXPECT_FALSE(eqt::ui::ParseEffectQuality("ultra", q));
    EXPECT_EQ(q, eqt::ui::EffectQuality::High);
}

TEST(ConfigKeyReachability, ShippedDisplaySettingsIsBehaviourNeutral) {
    // T414 rewrote config/display_settings.json to the values that were ALREADY
    // in effect when eight of its keys were unparsed, so wiring the parse
    // changed no pixel.  The one key that was parsed before and is not at its
    // struct default is atmosphericParticles.  If this fails, someone changed a
    // shipped value: that is allowed, but it moves the visual gate -- say so.
    const Json::Value root = ParseFile("config/display_settings.json");
    ASSERT_TRUE(root.isObject()) << "config/display_settings.json did not parse";

    eqt::ui::DisplaySettings shipped;
    eqt::ui::ParseDisplaySettings(root, shipped);
    const eqt::ui::DisplaySettings defaults;

    EXPECT_FLOAT_EQ(shipped.renderDistance, defaults.renderDistance);
    EXPECT_EQ(shipped.environmentQuality, defaults.environmentQuality);
    EXPECT_FALSE(shipped.atmosphericParticles);  // the one deliberate override
    EXPECT_EQ(shipped.ambientCreatures, defaults.ambientCreatures);
    EXPECT_EQ(shipped.reactiveFoliage, defaults.reactiveFoliage);
    EXPECT_EQ(shipped.rollingObjects, defaults.rollingObjects);
    EXPECT_EQ(shipped.skyEnabled, defaults.skyEnabled);
    EXPECT_EQ(shipped.animatedTrees, defaults.animatedTrees);
    EXPECT_EQ(shipped.fireEffects, defaults.fireEffects);
    EXPECT_EQ(shipped.enableFireGlowLighting, defaults.enableFireGlowLighting);
    EXPECT_EQ(shipped.enableFireGlowIcospheres, defaults.enableFireGlowIcospheres);
    EXPECT_EQ(shipped.maxFireGlowLights, defaults.maxFireGlowLights);
    EXPECT_FLOAT_EQ(shipped.environmentDensity, defaults.environmentDensity);
    EXPECT_EQ(shipped.detailObjectsEnabled, defaults.detailObjectsEnabled);
    EXPECT_FLOAT_EQ(shipped.detailDensity, defaults.detailDensity);
    EXPECT_EQ(shipped.detailGrass, defaults.detailGrass);
    EXPECT_EQ(shipped.detailPlants, defaults.detailPlants);
    EXPECT_EQ(shipped.detailRocks, defaults.detailRocks);
    EXPECT_EQ(shipped.detailDebris, defaults.detailDebris);
}
#endif  // EQT_HAS_GRAPHICS
