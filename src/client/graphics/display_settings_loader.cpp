/*
 * display_settings_loader.cpp — see display_settings_loader.h.
 *
 * T414 Leg A (board B297).
 */

#include "client/graphics/display_settings_loader.h"

#include "client/data_paths.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace eqt {
namespace ui {

bool ParseEffectQuality(const std::string& name, EffectQuality& out) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "off")    { out = EffectQuality::Off;    return true; }
    if (lower == "low")    { out = EffectQuality::Low;    return true; }
    if (lower == "medium") { out = EffectQuality::Medium; return true; }
    if (lower == "high")   { out = EffectQuality::High;   return true; }
    return false;
}

void ParseDisplaySettings(const Json::Value& root, DisplaySettings& out) {
    if (root.isMember("renderDistance")) {
        out.renderDistance = root["renderDistance"].asFloat();
    }

    if (root.isMember("environmentEffects")) {
        const Json::Value& env = root["environmentEffects"];
        out.atmosphericParticles = env.get("atmosphericParticles", out.atmosphericParticles).asBool();
        out.ambientCreatures     = env.get("ambientCreatures",     out.ambientCreatures).asBool();
        out.reactiveFoliage      = env.get("reactiveFoliage",      out.reactiveFoliage).asBool();
        out.rollingObjects       = env.get("rollingObjects",       out.rollingObjects).asBool();
        out.skyEnabled           = env.get("skyEnabled",           out.skyEnabled).asBool();
        out.animatedTrees        = env.get("animatedTrees",        out.animatedTrees).asBool();
        out.fireEffects          = env.get("fireEffects",          out.fireEffects).asBool();
        out.enableFireGlowLighting =
            env.get("enableFireGlowLighting", out.enableFireGlowLighting).asBool();
        out.enableFireGlowIcospheres =
            env.get("enableFireGlowIcospheres", out.enableFireGlowIcospheres).asBool();
        out.maxFireGlowLights    = env.get("maxFireGlowLights",    out.maxFireGlowLights).asInt();
        out.environmentDensity   = env.get("density",              out.environmentDensity).asFloat();
        if (env.isMember("quality")) {
            EffectQuality q = out.environmentQuality;
            if (ParseEffectQuality(env["quality"].asString(), q)) {
                out.environmentQuality = q;
            } else {
                LOG_WARN(MOD_GRAPHICS,
                         "display_settings.json: unknown environmentEffects.quality '{}' "
                         "(expected off/low/medium/high); keeping current value",
                         env["quality"].asString());
            }
        }
    }

    if (root.isMember("detailObjects")) {
        const Json::Value& detail = root["detailObjects"];
        out.detailObjectsEnabled = detail.get("enabled", out.detailObjectsEnabled).asBool();
        out.detailDensity        = detail.get("density", out.detailDensity).asFloat();
        out.detailGrass          = detail.get("grass",   out.detailGrass).asBool();
        out.detailPlants         = detail.get("plants",  out.detailPlants).asBool();
        out.detailRocks          = detail.get("rocks",   out.detailRocks).asBool();
        out.detailDebris         = detail.get("debris",  out.detailDebris).asBool();
    }
}

DisplaySettings LoadShippedDisplaySettings() {
    DisplaySettings settings;  // defaults: everything enabled

    // T399 (board B235): one path rule for shipped data.  A miss is an ERROR --
    // the file IS shipped, so "using defaults" means the shipped display
    // settings are not the ones on screen.
    const std::string path = eqt::ResolveDataPath("config/display_settings.json");
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS,
                  "cannot open '{}' -- the SHIPPED display settings are not applied; "
                  "built-in defaults (everything enabled) are in effect", path);
        return settings;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse display_settings.json: {}", errors);
        return settings;
    }

    ParseDisplaySettings(root, settings);
    return settings;
}

}  // namespace ui
}  // namespace eqt
