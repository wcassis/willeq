// B1123 (#443): the spell particle pool the pool-accounting tests run at.
//
// Will (2026-10-22): the pool is ONE preset key per platform
// (config/constrained_presets.json, orangepi preset, `spellParticlePool`
// {"sbc", "desktop"}) and no test carries a copy of the number -- the pool is
// PROVISIONAL (B1130 re-sizes it), so every bound is derived from what the
// file says.  The tests run each pool-accounting case at BOTH platforms' pools:
// the desktop one (x86 / macOS) and the sbc one (the Orange Pi's memory-bound
// budget), so a re-size of either is re-checked without a test edit.
// (Production reads the same key through ConstrainedRendererConfig::
// loadJsonOverrides; test_constrained_renderer.cpp pins that path.)
#pragma once

#include <json/json.h>

#include <fstream>
#include <string>

#include "client/graphics/environment/spell_effects_config.h"

namespace spell_pool {

constexpr const char* kPlatforms[] = {"desktop", "sbc"};

inline const char* ThisBuildPlatform() {
#ifdef EQT_HAS_DRM
    return "sbc";
#else
    return "desktop";
#endif
}

// The shipped pool of one platform; 0 if the preset file does not say.
inline int Shipped(const std::string& platform) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/config/constrained_presets.json");
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    if (!f || !Json::parseFromStream(b, f, &root, &errs)) return 0;
    return root["presets"]["orangepi"]["spellParticlePool"][platform].asInt();
}

// Load the shipped spell config and derive its populations at `pool`.
inline bool LoadAt(int pool) {
    auto& cfg = EQT::Graphics::Environment::SpellEffectsConfig::instance();
    if (!cfg.load(std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json")) return false;
    cfg.applyPool(pool);
    return cfg.getGlobal().maxParticles == pool && pool > 0;
}

}  // namespace spell_pool
