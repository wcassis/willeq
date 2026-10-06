/*
 * HotkeyManager's user rebinding (B433, the Options window's Keys page).
 * See the declarations in include/client/input/hotkey_manager.h.
 *
 * DEVIATION, FLAGGED: the official client stores only the keys a player
 * changed (eqclient.ini `[KeyMaps] KEYMAPPING_<ACTION>_<n>=<scancode>`); fable
 * writes its whole binding set in its own JSON shape, because that is the
 * format HotkeyManager already loads and the shipped file is JSON.
 */

#include "client/input/hotkey_manager.h"

#include <cstdio>

#include "client/data_paths.h"
#include "common/logging.h"

namespace eqt {
namespace input {

namespace {
const char* const kUserBindingsCacheName = "hotkeys.json";
}

bool HotkeyManager::setBinding(HotkeyAction action, int slot, irr::EKEY_CODE key,
                               ModifierFlags mods) {
    if (action == HotkeyAction::Count || slot < 0 || key == irr::KEY_KEY_CODES_COUNT) {
        return false;
    }
    // The action's own bindings, in table order, and the mode it lives in.
    HotkeyMode mode = HotkeyMode::Player;
    std::vector<size_t> own;
    for (size_t i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].action == action) {
            if (own.empty()) mode = m_bindings[i].mode;
            own.push_back(i);
        }
    }
    HotkeyBinding b;
    b.action = action;
    b.mode = mode;
    b.keyCode = key;
    b.modifiers = mods;

    std::vector<HotkeyBinding> next;
    next.reserve(m_bindings.size() + 1);
    int seen = 0;
    bool placed = false;
    for (const auto& existing : m_bindings) {
        if (existing.action == action) {
            if (seen++ == slot) {
                next.push_back(b);
                placed = true;
                continue;
            }
            if (existing == b) continue;  // already this key in another slot
            next.push_back(existing);
            continue;
        }
        // A key means one thing: take it off every other action.
        if (existing == b) continue;
        next.push_back(existing);
    }
    if (!placed) next.push_back(b);
    m_bindings = std::move(next);
    rebuildIndex();
    LOG_INFO(MOD_INPUT, "HotkeyManager: {} slot {} = {}", actionEnumToName(action), slot,
             bindingToString(b));
    return true;
}

bool HotkeyManager::saveUserBindings() {
    const std::string path = eqt::ResolveCachePathForWrite(kUserBindingsCacheName);
    if (path.empty()) {
        LOG_WARN(MOD_INPUT, "HotkeyManager: no writable cache directory for user bindings");
        return false;
    }
    return saveToFile(path);
}

bool HotkeyManager::restoreShippedBindings() {
    const std::string user = eqt::ResolveCachePathForRead(kUserBindingsCacheName);
    if (!user.empty()) std::remove(user.c_str());
    setupDefaults();
    return loadFromFile(eqt::ResolveDataPath("config/hotkeys.json"));
}

std::string HotkeyManager::startupBindingsPath() {
    const std::string user = eqt::ResolveCachePathForRead(kUserBindingsCacheName);
    if (!user.empty()) return user;
    return eqt::ResolveDataPath("config/hotkeys.json");
}

}  // namespace input
}  // namespace eqt
