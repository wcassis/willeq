// T463 (boards B522-B524) -- the census that closes the class.
//
// B520 (T460), B522, B523 and B524 were one defect four times: an owner loads a
// texture through ConstrainedTextureCache::getOrLoad(), keeps the raw
// ITexture* past the zone, and the cache's clear()/LRU eviction destroys it
// under the owner -- leaving either a missing texture for the rest of the
// process or a dangling pointer. A generic "does this member survive" proof
// needs every owner's lifecycle, which a source scan cannot see. What IS cheap
// is to make every getOrLoad() call site in the renderer DECLARE its lifetime
// policy and to check the one marker that policy stands on:
//
//   Protected       -- protectTexture() on the name: clear() and the LRU skip it.
//   ListenerReload  -- an eviction listener nulls the pointer AND a reload
//                      path re-acquires it (ParticleManager/DetailManager).
//   ZoneScoped      -- loaded per zone into holders that are rebuilt or
//                      scrubbed per zone (the zone, entity and sky loads).
//   NotOnGles2      -- the owner is not constructed / never loads on GLES2.
//
// A NEW call site (a file not in the table, or a changed count in a file that
// is) fails here and has to pick a policy. This does not prove the marker is
// wired correctly -- tests/test_environment_texture_eviction.cpp and
// tests/test_software_cursor_eviction.cpp do that for the owners T460/T463
// fixed -- it prevents the next owner from being written without the question
// being asked. Scope: src/client/graphics and include/client/graphics (the
// renderer); tools/hwcaps/p50_mask_plumbing_probe.cpp is a standalone probe
// and is not scanned.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A line is a call site when getOrLoad( appears in code: not in a // or block
// comment line, not after a // on the same line, and not the definition.
int countCallSites(const std::string& text) {
    int n = 0;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t pos = line.find("getOrLoad(");
        if (pos == std::string::npos) continue;
        const size_t first = line.find_first_not_of(" \t");
        if (line.compare(first, 2, "//") == 0 || line.compare(first, 1, "*") == 0 ||
            line.compare(first, 2, "/*") == 0)
            continue;
        const size_t comment = line.find("//");
        if (comment != std::string::npos && comment < pos) continue;
        if (line.find("::getOrLoad(") != std::string::npos) continue;
        ++n;
    }
    return n;
}

enum class Policy { Protected, ListenerReload, ZoneScoped, NotOnGles2 };

struct Marker {
    const char* file;   // relative to the repo root
    const char* text;   // must appear verbatim
};

struct Site {
    int callSites;
    Policy policy;
    const char* why;
    std::vector<Marker> markers;
};

const std::map<std::string, Site>& census() {
    static const std::map<std::string, Site> table = {
        {"src/client/graphics/renderer/software_cursor_texture.cpp",
         {1, Policy::Protected, "DRM cursor, 8x10 px, every frame of the process (T460)",
          {{"src/client/graphics/renderer/software_cursor_texture.cpp",
            "protectTexture(kSoftwareCursorCacheName)"}}}},
        {"src/client/graphics/ui/ui_atlas.cpp",
         {1, Policy::Protected, "UI chrome atlas, every frame of the process (T205)",
          {{"src/client/graphics/ui/ui_atlas.cpp", "protectTexture(\"ui_atlas\")"}}}},
        {"src/client/graphics/ui/item_icon_sheets.cpp",
         {1, Policy::Protected, "item icon sheets, eager, process lifetime (T430)",
          {{"src/client/graphics/ui/item_icon_sheets.cpp", "protectTexture(name)"}}}},
        {"src/client/graphics/ui/spellbook_backdrop_loader.cpp",
         {1, Policy::Protected, "spellbook backdrop, the book's own art, held across frames and protected (B1076, #396)",
          {{"src/client/graphics/ui/spellbook_backdrop_loader.cpp", "protectTexture(name)"}}}},
        {"src/client/graphics/ui/spell_icon_sheets.cpp",
         {2, Policy::Protected, "spell icon sheets + gem holder, eager, process lifetime (B1077)",
          {{"src/client/graphics/ui/spell_icon_sheets.cpp", "protectTexture(name)"}}}},
        {"src/client/graphics/environment/boids_manager.cpp",
         {1, Policy::Protected, "64x64 creature atlas, loaded once, drawn in every zone (T463, B524)",
          {{"src/client/graphics/environment/boids_manager.cpp", "protectTexture(\"creature_atlas\")"}}}},
        {"src/client/graphics/environment/tumbleweed_manager.cpp",
         {1, Policy::Protected, "64x64 texture baked once into a process-lifetime mesh (T463, B523)",
          {{"src/client/graphics/environment/tumbleweed_manager.cpp", "protectTexture(\"tumbleweed\")"}}}},
        {"src/client/graphics/environment/storm_cloud_layer.cpp",
         {1, Policy::ListenerReload, "32 frames, ~11 MB of budget: evictable, re-acquired on use (T463, B522)",
          {{"src/client/graphics/environment/storm_cloud_layer.cpp", "onTextureEvicted"},
           {"src/client/graphics/environment/storm_cloud_layer.cpp", "reacquireFrame("}}}},
        {"src/client/graphics/environment/particle_manager.cpp",
         {2, Policy::ListenerReload, "particle + spell sprite atlases, reloaded in onZoneEnter (P34, T417)",
          {{"src/client/graphics/environment/particle_manager.cpp", "onTextureEvicted"},
           {"src/client/graphics/environment/particle_manager.cpp",
            "loadParticleAtlas(eqt::ResolveDataPath(kAtlasTexturePath))"},
           {"src/client/graphics/environment/particle_manager.cpp",
            "loadSpellSpriteAtlas(eqt::ResolveDataPath(kSpellSpriteAtlasPath))"}}}},
        {"src/client/graphics/detail/detail_manager.cpp",
         {1, Policy::ListenerReload, "detail atlas, loaded inside onZoneEnter when null",
          {{"src/client/graphics/detail/detail_manager.cpp", "onTextureEvicted"},
           {"src/client/graphics/detail/detail_manager.cpp", "if (!atlasTexture_ && constrainedCache_)"}}}},
        {"src/client/graphics/eq/equipment_model_loader.cpp",
         {1, Policy::ListenerReload, "cached equipment meshes scrubbed by their own listener (T126)",
          {{"src/client/graphics/eq/equipment_model_loader.cpp", "addEvictionListener(this)"}}}},
        {"src/client/graphics/animated_texture_manager.cpp",
         {1, Policy::ZoneScoped, "the manager itself is reset at every zone unload",
          {{"src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp",
            "ctx_.animatedTextureManager.reset()"},
           {"src/client/graphics/animated_texture_manager.cpp", "addEvictionListener(this)"}}}},
        {"src/client/graphics/eq/zone_geometry.cpp",
         {1, Policy::ZoneScoped,
          "zone region meshes and door meshes, rebuilt per zone; regions guarded (P10b), doors guarded "
          "by the one DoorManager construction body (T124, registered there since T468/B590)",
          {{"src/client/graphics/renderer/renderer_init.cpp",
            "addEvictionListener(regionMeshTextureGuard_.get())"},
           {"src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp",
            "ctx_.ZoneStream().ensureDoorTextureGuard()"}}}},
        {"src/client/graphics/eq/race_model_loader.cpp",
         {2, Policy::ZoneScoped, "entity textures; mesh caches cleared at unload, entity meshes guarded",
          {{"src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp", "rml->clearMeshCaches()"},
           {"src/client/graphics/renderer/renderer_init.cpp",
            "addEvictionListener(entityMeshTextureGuard_.get())"}}}},
        {"src/client/graphics/zone_load/renderer_zone_load_entities.cpp",
         {2, Policy::ZoneScoped, "zone-load entity texture decode, same holders as above",
          {{"src/client/graphics/renderer/renderer_init.cpp",
            "addEvictionListener(entityMeshTextureGuard_.get())"}}}},
        {"src/client/graphics/zone_load/renderer_zone_load_env.cpp",
         {1, Policy::ZoneScoped, "sky textures, decoded again by Step10 every zone",
          {{"src/client/graphics/sky_renderer.cpp", "addEvictionListener(this)"}}}},
        {"src/client/graphics/environment/rain_overlay.cpp",
         {1, Policy::NotOnGles2, "screen-space rain overlay is not constructed on GLES2",
          {{"src/client/graphics/weather_effects_controller.cpp",
            "skipped on GLES2 where unified particles replace them"}}}},
        {"src/client/graphics/environment/snow_overlay.cpp",
         {1, Policy::NotOnGles2, "screen-space snow overlay is not constructed on GLES2",
          {{"src/client/graphics/weather_effects_controller.cpp",
            "skipped on GLES2 where unified particles replace them"}}}},
        {"src/client/graphics/spell_visual_fx.cpp",
         {2, Policy::NotOnGles2, "never loaded on GLES2 (T124); the T124 swap guard covers it anyway",
          {{"src/client/graphics/renderer/renderer_entities.cpp",
            "addEvictionListener(ZoneStream().spellVisualFXTextureGuard().get())"}}}},
    };
    return table;
}

bool isCache(const std::string& rel) {
    return rel.find("constrained_texture_cache") != std::string::npos;
}

TEST(TextureCacheHolderCensus, EveryGetOrLoadSiteDeclaresALifetimePolicy) {
    const fs::path root(WILLEQ_SOURCE_DIR);
    std::map<std::string, int> found;
    for (const char* dir : {"src/client/graphics", "include/client/graphics"}) {
        ASSERT_TRUE(fs::is_directory(root / dir)) << dir;
        for (const auto& entry : fs::recursive_directory_iterator(root / dir)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h") continue;
            const std::string rel = fs::relative(entry.path(), root).generic_string();
            if (isCache(rel)) continue;
            const int n = countCallSites(readText(entry.path()));
            if (n > 0) found[rel] = n;
        }
    }

    for (const auto& [rel, n] : found) {
        auto it = census().find(rel);
        if (it == census().end()) {
            ADD_FAILURE() << rel << " calls ConstrainedTextureCache::getOrLoad() " << n
                          << " time(s) and is not in the census. clear() and the LRU destroy "
                             "every unprotected texture; declare how this holder survives that "
                             "(Protected / ListenerReload / ZoneScoped / NotOnGles2) -- see "
                             "boards B520-B524.";
            continue;
        }
        EXPECT_EQ(n, it->second.callSites)
            << rel << ": the getOrLoad() call count changed; re-check its lifetime policy ("
            << it->second.why << ")";
    }
    for (const auto& [rel, site] : census()) {
        EXPECT_TRUE(found.count(rel)) << rel << " is in the census but no longer calls getOrLoad()";
    }
}

TEST(TextureCacheHolderCensus, EveryDeclaredPolicyHasItsMarker) {
    const fs::path root(WILLEQ_SOURCE_DIR);
    for (const auto& [rel, site] : census()) {
        for (const auto& m : site.markers) {
            const std::string text = readText(root / m.file);
            ASSERT_FALSE(text.empty()) << m.file << " unreadable";
            EXPECT_NE(text.find(m.text), std::string::npos)
                << rel << " (" << site.why << ") stands on \"" << m.text << "\" in " << m.file
                << ", which is no longer there";
        }
    }
}

}  // namespace
