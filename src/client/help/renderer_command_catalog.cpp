#include "client/help/renderer_command_catalog.h"

#include <algorithm>
#include <cctype>

namespace eqt {
namespace help {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Order and grouping mirror IrrlichtRenderer::processSlashCommand()'s
// if/else chain (src/client/graphics/irrlicht_renderer.cpp). Keep them in
// sync -- the drift test in tests/test_merged_help.cpp enforces name-set
// equality, not order.
const std::vector<RendererCommandInfo> kCatalog = {
    // --- Visual rendering ---
    {"sort", {}, "Toggle front-to-back zone draw sorting"},
    {"mergedraws", {}, "Toggle merged zone draw calls"},
    {"screenshot", {}, "Save a screenshot"},
    {"camera", {}, "Show or set the camera mode"},
    {"upload", {}, "Toggle the GPU upload thread"},
    {"portal", {}, "Toggle portal occlusion / debug overlay"},
    {"stencil", {}, "Toggle the stencil debug overlay"},
    {"plight", {}, "Toggle per-pixel player light"},
    {"olight", {}, "Toggle object lights"},
    {"zlight", {}, "Toggle the directional zone light"},
    {"lightmode", {}, "Toggle unified/additive lighting"},
    {"shaderdebug", {"sd"}, "Set the shader debug visualisation mode"},
    {"fire", {}, "Toggle fire particles"},
    {"sky", {}, "Toggle sky rendering"},
    {"skytype", {}, "Force a sky type (0-255)"},
    // --- Distance ---
    {"renderdist", {"clipplane", "viewdist"}, "Show or set the render distance"},
    {"clipdist", {"clip", "farplane"}, "Show or set the far clip distance"},
    // --- Profiling/debug ---
    {"frametiming", {"frametime", "ft"}, "Toggle the frame timing profiler"},
    {"sceneprofile", {"sp", "scenebreak"}, "Print a scene profile breakdown to the console"},
    {"dumpscene", {"ds"}, "Dump the scene graph to the console log"},
    {"simworker", {"sw"}, "Show simulation worker status"},
    // --- UI ---
    // T301 Leg D: hides EVERY panel (chat, hotbar, gems, buffs, popups, XP bar,
    // casting bars, FPS counter) -- the scriptable half of the F11 hotkey.
    {"hideui", {"toggleui", "ui"}, "Show/hide the whole UI (same as F11)"},
    {"newui", {}, "Toggle the static UI"},
    {"newbook", {}, "Toggle the spellbook window"},
    {"newbookprev", {}, "Spellbook: previous page"},
    {"newbooknext", {}, "Spellbook: next page"},
    {"newskills", {}, "Toggle the skills window"},
    {"newinv", {}, "Toggle the inventory window"},
    {"timestamp", {"timestamps", "ts"}, "Chat timestamps (not available: WindowManager removed)"},
    {"filter", {}, "Chat filter (not available: WindowManager removed)"},
    {"reloadeffects", {"particlereload", "reloadfx", "reloadparticles"}, "Reload environment effect settings"},
    {"reloadweather", {"weatherreload", "weather_reload"}, "Reload weather effect settings"},
    {"spellreload", {"reloadspells", "reloadspellfx"}, "Reload spell effect settings"},
    {"weatherquality", {"wq", "weatherpreset"}, "Show or set the weather quality preset"},
    // --- Detail ---
    {"detail", {"detaildensity", "grass"}, "Show or set detail (grass) density"},
    {"togglegrass", {}, "Toggle grass detail"},
    {"toggleplants", {}, "Toggle plant detail"},
    {"togglerocks", {}, "Toggle rock detail"},
    {"toggledebris", {}, "Toggle debris detail"},
    {"season", {}, "Override the detail season"},
    {"detailinfo", {}, "Print detail manager debug info"},
    // --- Governor ---
    {"governor", {"gov", "budget"}, "Frame-budget governor status / fps / force / auto"},
    {"placeholder", {}, "Zone placeholder status"},
};

} // namespace

const std::string& RendererCommandCategory() {
    static const std::string kCategory = "Graphics";
    return kCategory;
}

const std::vector<RendererCommandInfo>& AllRendererCommands() {
    return kCatalog;
}

const RendererCommandInfo* FindRendererCommand(const std::string& name) {
    std::string key = Lower(name);
    if (!key.empty() && key[0] == '/') {
        key.erase(0, 1);
    }
    if (key.empty()) {
        return nullptr;
    }
    for (const auto& info : kCatalog) {
        if (info.name == key) {
            return &info;
        }
        for (const auto& alias : info.aliases) {
            if (alias == key) {
                return &info;
            }
        }
    }
    return nullptr;
}

} // namespace help
} // namespace eqt
