// T399 (refactor/T399_data_paths.md, board B235): THE CENSUS AS A TEST.
//
// B235 measured ~40 shipped data files named by CWD-relative string literals
// under src/client. Nothing enforced anything: the client worked because every
// script, every developer and the visual gate all start it from the repo root.
// T397 built the one resolution (eqt::ResolveDataPath, include/client/data_paths.h)
// and routed exactly one literal through it. T399 routed the rest.
//
// This file is the ratchet. It re-takes the census on every run: it reads the
// client sources, strips comments, and requires that every string literal
// beginning "config/" or "data/" sit on a line that also names ResolveDataPath
// -- the ONE resolution -- or appear in the allowlist below with a written
// reason. A new bare literal is a FAIL at ctest time, not a discovery three
// units later.
//
// It is deliberately a source reader with no link dependencies: most of the
// routed sites (every data/textures/*.bmp load) end in LOG_FATAL + std::exit(1)
// on a miss and cannot be exercised in-process at all. The behavioural half of
// the proof -- loaders that really do find their file from a foreign working
// directory -- is tests/test_data_paths.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cstdio>

namespace {

const std::string kRoot = std::string(WILLEQ_REPO_ROOT) + "/";

// ---------------------------------------------------------------------------
// The allowlist: literals that are NOT on a ResolveDataPath line and are right
// anyway, each with the reason. Keyed by "<repo-relative file>|<literal>".
// ---------------------------------------------------------------------------
struct Waiver { const char* file; const char* literal; const char* why; };

const Waiver kWaivers[] = {
    // Directory knobs. The complete path is resolved where the file is
    // actually opened (DetailManager::loadSurfaceMap,
    // DiagnosticsComponent::loadIndoorRegionMap), which is what lets an
    // absolute override still work.
    // T467: one site now -- the two copies (overlays_hud_env.cpp,
    // renderer_init.cpp) became OverlaysComponent::ensureDetailManager().
    {"src/client/graphics/overlays/overlays_env_managers.cpp", "data/detail/zones",
     "directory knob; resolved at DetailManager's open"},
    {"include/client/application.h", "data/region_maps",
     "directory default; resolved at DiagnosticsComponent's open"},
    {"include/client/bridge/bridge_component.h", "data/region_maps",
     "directory default; resolved at DiagnosticsComponent's open"},
    {"include/client/graphics/renderer_types.h", "data/region_maps",
     "directory default; resolved at DiagnosticsComponent's open"},
    {"include/client/mode/game_mode.h", "data/region_maps",
     "directory default; resolved at DiagnosticsComponent's open"},
    {"src/client/graphics/diagnostics/diagnostics_debug.cpp", "data/region_maps",
     "directory fallback when config.regionMapsPath is empty; the complete "
     "path two lines below is resolved"},
    // Header constants and default arguments. Every use site resolves.
    {"include/client/graphics/environment/particle_manager.h", "data/textures/particle_atlas.bmp",
     "constant; both use sites in particle_manager.cpp resolve it"},
    {"include/client/graphics/environment/particle_manager.h",
     "data/textures/spell_sprite_atlas.bmp",
     "T417 constant; both use sites in particle_manager.cpp (init and "
     "onZoneEnter re-acquisition) resolve it, exactly like its sibling above"},
    {"include/client/input/hotkey_manager.h", "config/hotkeys.json",
     "default argument / member default; HotkeyManager::loadFromFile resolves"},
    {"include/client/skill/skill_cap_table.h", "config/skill_caps.json",
     "kDefaultPath; SkillCapTable::load resolves"},
};

bool IsWaived(const std::string& file, const std::string& literal) {
    for (const auto& w : kWaivers) {
        if (file == w.file && literal == w.literal) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Source scanning
// ---------------------------------------------------------------------------

std::string ReadFile(const std::string& relative) {
    std::ifstream f(kRoot + relative, std::ios::binary);
    if (!f.is_open()) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Replace the body of every // and /* */ comment with spaces, leaving line
// structure and string literals intact. A literal quoted inside a comment --
// this file's own header is full of them -- must not be mistaken for code.
std::string StripComments(const std::string& src) {
    std::string out = src;
    enum { kCode, kLine, kBlock, kString, kChar } state = kCode;
    for (size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        const char n = (i + 1 < out.size()) ? out[i + 1] : '\0';
        switch (state) {
            case kCode:
                if (c == '/' && n == '/') { state = kLine;  out[i] = out[i + 1] = ' '; ++i; }
                else if (c == '/' && n == '*') { state = kBlock; out[i] = out[i + 1] = ' '; ++i; }
                else if (c == '"') state = kString;
                else if (c == '\'') state = kChar;
                break;
            case kLine:
                if (c == '\n') state = kCode; else out[i] = ' ';
                break;
            case kBlock:
                if (c == '*' && n == '/') { out[i] = out[i + 1] = ' '; ++i; state = kCode; }
                else if (c != '\n') out[i] = ' ';
                break;
            case kString:
                if (c == '\\') ++i;
                else if (c == '"') state = kCode;
                break;
            case kChar:
                if (c == '\\') ++i;
                else if (c == '\'') state = kCode;
                break;
        }
    }
    return out;
}

struct Offender {
    std::string file;
    int line = 0;
    std::string literal;
};

// Every "config/..." / "data/..." literal in `relative` that is neither on a
// ResolveDataPath line nor waived.
std::vector<Offender> ScanFile(const std::string& relative) {
    std::vector<Offender> out;
    const std::string src = ReadFile(relative);
    if (src.empty()) {
        // A file named in a family list that does not exist is itself a
        // failure -- the census has drifted. Report it as an offender with an
        // obvious literal so the message names the file.
        out.push_back({relative, 0, "<FILE NOT FOUND>"});
        return out;
    }
    const std::string code = StripComments(src);

    std::istringstream ls(code);
    std::string line;
    int lineNo = 0;
    while (std::getline(ls, line)) {
        ++lineNo;
        const bool routed = line.find("ResolveDataPath") != std::string::npos;
        size_t pos = 0;
        while ((pos = line.find('"', pos)) != std::string::npos) {
            const size_t end = line.find('"', pos + 1);
            if (end == std::string::npos) break;
            const std::string lit = line.substr(pos + 1, end - pos - 1);
            pos = end + 1;
            if (lit.rfind("config/", 0) != 0 && lit.rfind("data/", 0) != 0) continue;
            if (routed) continue;
            if (IsWaived(relative, lit)) continue;
            out.push_back({relative, lineNo, lit});
        }
    }
    return out;
}

std::string Describe(const std::vector<Offender>& offenders) {
    std::ostringstream ss;
    ss << offenders.size() << " unrouted shipped-data literal(s):\n";
    for (const auto& o : offenders) {
        ss << "  " << o.file << ":" << o.line << "  \"" << o.literal << "\"\n";
    }
    ss << "Route each through eqt::ResolveDataPath() (include/client/data_paths.h),\n"
          "or add a waiver with a reason to kWaivers in this file.";
    return ss.str();
}

void ExpectRouted(const std::vector<std::string>& files) {
    std::vector<Offender> offenders;
    for (const auto& f : files) {
        auto found = ScanFile(f);
        offenders.insert(offenders.end(), found.begin(), found.end());
    }
    EXPECT_TRUE(offenders.empty()) << Describe(offenders);
}

// ---------------------------------------------------------------------------
// The five families, exactly as T399's commits are split.
// ---------------------------------------------------------------------------

TEST(DataPathRouting, CoreAndAppLoadsAreRouted) {
    ExpectRouted({
        "src/client/application.cpp",
        "src/client/application_config.cpp",
        "src/client/zone_lines.cpp",
        "src/client/skill/skill_cap_table.cpp",
        "src/client/input/hotkey_manager.cpp",
        "src/client/graphics/constrained_renderer_config.cpp",
    });
}

TEST(DataPathRouting, ConfigJsonLoadersAreRouted) {
    ExpectRouted({
        "src/client/graphics/renderer/renderer_init.cpp",
        "src/client/graphics/eqe1/eqe1_archive_component.cpp",
        "src/client/graphics/detail/detail_config_loader.cpp",
        "src/client/graphics/tree_wind_config.cpp",
        "src/client/graphics/tree_identifier.cpp",
        "src/client/graphics/weather_system.cpp",
        "src/client/graphics/weather_effects_controller.cpp",
        "src/client/graphics/weather_config_loader.cpp",
        "src/client/graphics/environment/environment_config.cpp",
        "src/client/graphics/environment/particle_manager.cpp",
    });
}

TEST(DataPathRouting, TextureLoadsAreRouted) {
    ExpectRouted({
        "src/client/graphics/spell_visual_fx.cpp",
        "src/client/graphics/detail/detail_manager.cpp",
        "src/client/graphics/environment/rain_overlay.cpp",
        "src/client/graphics/environment/snow_overlay.cpp",
        "src/client/graphics/environment/tumbleweed_manager.cpp",
        "src/client/graphics/environment/boids_manager.cpp",
        "src/client/graphics/environment/storm_cloud_layer.cpp",
        "src/client/graphics/renderer/irrlicht_renderer.cpp",
    });
}

TEST(DataPathRouting, AudioAndIndexCacheLoadsAreRouted) {
    ExpectRouted({
        "src/client/audio/audio_manager.cpp",
        "src/client/graphics/graphics_archive_index.cpp",
    });
}

// T402 (board B248): the two GENERATED caches are not shipped data, and their
// old home -- a git-tracked config/*.json that every client run rewrote -- is
// what made a plain `willeq` run dirty the working tree. They now live in a
// run-local cache directory (eqt::ResolveCachePathForRead/ForWrite), so no
// "config/…_index_cache.json" literal may come back anywhere in the client.
// T402 (board B245/B255): every shipped config/*.json must be NAMED by code.
//
// config/chat_settings.json and config/ui_settings.json shipped for months
// after the WindowManager that wrote them was deleted (0f25cb57). They were
// invisible to every census here, because a file nothing opens has no literal
// to find -- the only way to see it is to start from the DIRECTORY and ask
// which files nothing names. That is this test.
TEST(DataPathRouting, EveryShippedConfigFileIsNamedByCode) {
    // The haystack: every client/tool source, comments included on purpose --
    // a name mentioned only in a comment is at least a live reference someone
    // can follow, and the failure this catches is a total absence.
    std::string haystack;
    const char* roots[] = {"src", "include", "tools"};
    for (const char* r : roots) {
        std::string cmd = "find '" + kRoot + r +
                          "' -type f \\( -name '*.cpp' -o -name '*.h' \\)";
        FILE* p = ::popen(cmd.c_str(), "r");
        ASSERT_NE(p, nullptr) << "could not enumerate " << r;
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
    ASSERT_GT(haystack.size(), 1000000u) << "the source walk came back empty";

    std::vector<std::string> orphans;
    FILE* p = ::popen(("ls '" + kRoot + "config'").c_str(), "r");
    ASSERT_NE(p, nullptr);
    char buf[4096];
    size_t seen = 0;
    while (::fgets(buf, sizeof(buf), p)) {
        std::string name(buf);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        if (name.size() < 6 || name.substr(name.size() - 5) != ".json") continue;
        ++seen;
        if (haystack.find(name) == std::string::npos) orphans.push_back(name);
    }
    ::pclose(p);
    EXPECT_GE(seen, 10u) << "config/ looks empty -- did the walk work?";

    std::ostringstream ss;
    for (const auto& o : orphans) ss << "\n  config/" << o;
    EXPECT_TRUE(orphans.empty())
        << orphans.size() << " shipped config file(s) that NO source names:" << ss.str()
        << "\nEither wire the loader (and board the unreached feature) or delete "
           "the file -- a shipped file nothing reads is data with no owner (B245).";
}

TEST(DataPathRouting, IndexCachesAreNotWrittenUnderConfig) {
    std::vector<Offender> offenders;
    const char* roots[] = {"src/client", "include/client"};
    for (const char* r : roots) {
        std::string cmd = "find '" + kRoot + r +
                          "' -type f \\( -name '*.cpp' -o -name '*.h' \\)";
        FILE* p = ::popen(cmd.c_str(), "r");
        ASSERT_NE(p, nullptr) << "could not enumerate " << r;
        char buf[4096];
        while (::fgets(buf, sizeof(buf), p)) {
            std::string path(buf);
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
            if (path.empty()) continue;
            if (path.rfind(kRoot, 0) == 0) path = path.substr(kRoot.size());
            // Comments are stripped first: the doc comment in data_paths.h
            // NAMES the old tracked path on purpose, and so does the note in
            // graphics_archive_index.h. Only code counts.
            const std::string code = StripComments(ReadFile(path));
            std::istringstream ls(code);
            std::string line;
            int lineNo = 0;
            while (std::getline(ls, line)) {
                ++lineNo;
                if (line.find("_index_cache.json") == std::string::npos) continue;
                if (line.find("config/") == std::string::npos) continue;
                offenders.push_back({path, lineNo, "config/*_index_cache.json"});
            }
        }
        ::pclose(p);
    }
    EXPECT_TRUE(offenders.empty())
        << Describe(offenders)
        << "\nThese are generated CACHES: use eqt::ResolveCachePathForRead/"
           "ForWrite (include/client/data_paths.h), which keeps them out of the "
           "checkout entirely (board B248).";
}

TEST(DataPathRouting, RemainingDataLoadsAreRouted) {
    ExpectRouted({
        "src/client/graphics/entity_equipment.cpp",
        "src/client/graphics/zone_load/renderer_zone_load.cpp",
        "src/client/graphics/diagnostics/diagnostics_debug.cpp",
    });
}

// The ratchet proper: the whole client tree, not just the files T399 touched.
// A new bare literal in a file nobody has thought about yet fails HERE.
TEST(DataPathRouting, TheWholeClientTreeIsRouted) {
    // A tiny recursive walk rather than <filesystem>, so this test has no
    // dependency beyond the standard library it already uses.
    std::vector<std::string> files;
    const char* roots[] = {"src/client", "include/client"};
    for (const char* r : roots) {
        std::string cmd = "find '" + kRoot + r + "' -type f \\( -name '*.cpp' -o -name '*.h' \\)";
        FILE* p = ::popen(cmd.c_str(), "r");
        ASSERT_NE(p, nullptr) << "could not enumerate " << r;
        char buf[4096];
        while (::fgets(buf, sizeof(buf), p)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (line.rfind(kRoot, 0) == 0) line = line.substr(kRoot.size());
            if (!line.empty()) files.push_back(line);
        }
        ::pclose(p);
    }
    ASSERT_GT(files.size(), 200u) << "the source walk found suspiciously few files";
    std::sort(files.begin(), files.end());
    ExpectRouted(files);
}

}  // namespace
