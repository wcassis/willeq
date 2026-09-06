/*
 * display_settings_loader.h — config/display_settings.json → DisplaySettings.
 *
 * T414 Leg A (board B297).  The parse used to be a `static` helper inside
 * src/client/graphics/renderer/renderer_init.cpp that read exactly SEVEN of
 * the shipped file's FIFTEEN keys.  The other eight — `renderDistance`,
 * `environmentEffects.quality` / `.density` / `.reactiveFoliage` /
 * `.enableFireGlowLighting` / `.enableFireGlowIcospheres` /
 * `.maxFireGlowLights` and `detailObjects.density`, `.grass`, `.plants`,
 * `.rocks`, `.debris` — were shipped, documented and read by NOBODY, while
 * every one of the DisplaySettings fields they name has a live consumer in
 * OverlaysComponent::applyEnvironmentalDisplaySettings() and
 * IrrlichtRenderer::setRenderDistance().  Editing them changed nothing.
 *
 * It lives in its own TU so `tests/test_config_key_reachability.cpp` can call
 * the parse directly with no renderer, no device and no GL context.
 */

#pragma once

#include "client/graphics/display_settings.h"

#include <json/json.h>

#include <string>

namespace eqt {
namespace ui {

/**
 * Apply every key present in `root` onto `out`.  Absent keys leave the
 * corresponding field at whatever value it already carries, so passing a
 * default-constructed DisplaySettings yields "defaults, overridden by file".
 */
void ParseDisplaySettings(const Json::Value& root, DisplaySettings& out);

/** Parse an EffectQuality name ("off"/"low"/"medium"/"high", case-insensitive). */
bool ParseEffectQuality(const std::string& name, EffectQuality& out);

/**
 * Resolve and read config/display_settings.json.  A missing or unparsable
 * file is an ERROR (the file IS shipped) and yields built-in defaults.
 */
DisplaySettings LoadShippedDisplaySettings();

}  // namespace ui
}  // namespace eqt
