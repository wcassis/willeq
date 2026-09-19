#include "client/mode/game_mode.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/core/memory_ledger.h"

#include <algorithm>
#include <cctype>

namespace eqt {
namespace mode {

std::unique_ptr<IGameMode> createMode(OperatingMode mode, GraphicalRendererType rendererType) {
    // B839 (issue #94): the mode object (input handler, renderer adapter)
    // and the per-mode initialize() that runs under it are the "mode"
    // sub-owner. The mode's own bytes are small; the scope mostly exists so
    // a future growth there has a name instead of landing in misc.
    eqt::memory::StartupScope modeScope("mode");
    switch (mode) {
        case OperatingMode::Automated:
            return std::make_unique<AutomatedMode>();

        case OperatingMode::HeadlessInteractive:
            return std::make_unique<HeadlessMode>();

        case OperatingMode::GraphicalInteractive:
            return std::make_unique<GraphicalMode>(rendererType);

        default:
            // Default to graphical mode
            return std::make_unique<GraphicalMode>(rendererType);
    }
}

OperatingMode parseModeString(const std::string& str) {
    // Convert to lowercase for comparison
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "automated" || lower == "auto" || lower == "bot" || lower == "script") {
        return OperatingMode::Automated;
    }

    if (lower == "headless" || lower == "console" || lower == "text" || lower == "cli") {
        return OperatingMode::HeadlessInteractive;
    }

    if (lower == "graphical" || lower == "graphics" || lower == "gui" || lower == "visual") {
        return OperatingMode::GraphicalInteractive;
    }

    // Default to graphical mode
    return OperatingMode::GraphicalInteractive;
}

} // namespace mode
} // namespace eqt
