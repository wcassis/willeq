#include "client/help/renderer_command_catalog.h"

#include "client/command_completion.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace eqt {
namespace help {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Order and grouping mirror IrrlichtRenderer::processSlashCommand()'s
// if/else chain (src/client/graphics/renderer_slash_commands.cpp). Keep them in
// sync -- the drift test in tests/test_merged_help.cpp enforces name-set
// equality, not order.
const std::vector<RendererCommandInfo> kCatalog = {
    // --- Visual rendering ---
    {"sort", {}, "Toggle front-to-back zone draw sorting"},
    {"mergedraws", {}, "Toggle merged zone draw calls"},
    {"screenshot", {}, "Save a screenshot"},
    // T461: the official camera set (F9 cycles it) and exact orbit placement.
    {"camera", {}, "Camera mode (F9 cycles): report, cycle, <mode>; set / nudge / get az pitch dist height"},
    // T310 Leg A: the first-person camera's numbers, settable at runtime so
    // the pose can be SOLVED against the official client without a rebuild
    // per candidate value. No aliases -- every alias here must also exist as
    // a `cmd == "/alias"` literal in the if-chain (test_merged_help.cpp).
    {"fpcam", {}, "First-person camera tunables: list / set / reset / dump"},
    {"upload", {}, "Toggle the GPU upload thread"},
    {"portal", {}, "Toggle portal occlusion / debug overlay"},
    {"stencil", {}, "Toggle the stencil debug overlay"},
    {"plight", {}, "Toggle per-pixel player light"},
    {"olight", {}, "Toggle object lights"},
    {"zlight", {}, "Toggle the directional zone light"},
    {"lightmode", {}, "Toggle unified/additive lighting"},
    {"bakedlight", {}, "Zone baked vertex light: mode [gain] [skyWeight]"},
    {"nightambient", {}, "Night ambient base: r g b / reset (T428 probe)"},
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
    // casting bars, FPS counter) -- the scriptable half of the F10 hotkey.
    {"hideui", {"toggleui", "ui"}, "Show/hide the whole UI (same as F10)"},
    // T313: the scriptable half of the static-UI scroll (PageUp/PageDown and
    // the mouse wheel are the interactive halves).
    {"scroll", {}, "Scroll a UI panel: /scroll [chat|skills|loot] [up|down|top|bottom] [lines]"},
    {"newui", {}, "Toggle the static UI"},
    {"newbook", {}, "Toggle the spellbook window"},
    {"newbookprev", {}, "Spellbook: previous page"},
    {"newbooknext", {}, "Spellbook: next page"},
    {"skills", {}, "Toggle the skills window"},  // T363: was newskills (B058)
    {"inventory", {}, "Toggle the inventory window"},  // T365: was newinv (B090)
    {"friendswindow", {}, "Toggle the Friends window (Alt+F)"},  // B897
    {"timestamp", {"timestamps", "ts"}, "Chat timestamps (not available: WindowManager removed)"},
    // B433: the era /filter vocabulary (eqstr_us.txt 1354-1360), against the
    // Options window's Filters page and OP_SetServerFilter.
    {"filter", {}, "Toggle a chat filter: /filter <name> (no name lists them)"},
    // B430: every wired Options-window setting, by name.
    {"option", {}, "Show or set an Options-window setting: /option [list | <name> [value]]"},
    // B1165 (#484): eqgame's /showspelleffects (eqstr 13168-13171).
    {"showspelleffects", {}, "Turn all spell particle effects on/off: /showspelleffects <ON | OFF>"},
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

std::vector<std::string> RendererCommandCompletions(const std::string& partial) {
    // The catalog is a vector, but the shared helper wants the two
    // lowercase-keyed maps registries 1 and 2 already hold. Built once: the
    // catalog is a compile-time constant, so this cannot go stale.
    static const auto kMaps = [] {
        std::map<std::string, const RendererCommandInfo*> byName;
        std::map<std::string, std::string> byAlias;
        for (const auto& info : kCatalog) {
            byName[Lower(info.name)] = &info;
            for (const auto& alias : info.aliases) {
                byAlias[Lower(alias)] = Lower(info.name);
            }
        }
        return std::make_pair(byName, byAlias);
    }();

    return completion::PrefixCompletions(
        partial, kMaps.first,
        [](const RendererCommandInfo* info) { return info->name; },
        kMaps.second);
}

} // namespace help
} // namespace eqt
