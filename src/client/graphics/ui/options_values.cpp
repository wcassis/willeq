/*
 * The Options window's value rules (B430/B433): setting a value, mirroring a
 * game-owned one, and what a game-owned checkbox asks the server for.
 * Device-free; options_settings.cpp holds the table these rules read.
 */

#include "client/graphics/ui/options_panel.h"

namespace EQT {
namespace Graphics {

using OptionsLayout::Owner;
using OptionsLayout::Setting;

bool setOptionsValue(OptionsPanelState& state, Setting setting, int value) {
    const auto& info = OptionsLayout::settingInfo(setting);
    if (info.owner == Owner::Legacy) {
        const int before = optionsSettingStep(state, setting);
        setOptionsSettingStep(state, setting, value);
        return optionsSettingStep(state, setting) != before;
    }
    if (info.owner != Owner::Renderer && info.owner != Owner::Game) return false;
    if (!OptionsLayout::isValidSettingValue(setting, value)) return false;
    // B1165: a change stores the table value, as the oracle's handler does
    // (eqgame.exe.c:245163-245204), so an applied off-table ini float goes.
    auto& raw = state.iniFloats[static_cast<int>(setting)];
    const bool hadRaw = raw.has_value();
    raw.reset();
    int& slot = state.values[static_cast<int>(setting)];
    if (slot == value) return hadRaw;
    slot = value;
    // The two anon checkboxes are views of ONE server value (0/1/2): checking
    // one unchecks the other here, exactly as the server will.
    if (value == 1 && setting == Setting::Anonymous) {
        state.values[static_cast<int>(Setting::Roleplaying)] = 0;
    } else if (value == 1 && setting == Setting::Roleplaying) {
        state.values[static_cast<int>(Setting::Anonymous)] = 0;
    }
    return true;
}

bool applyGameOptionChanged(OptionsPanelState& state, eqt::GameOption option, int value) {
    auto set = [&state](Setting s, int v) {
        int& slot = state.values[static_cast<int>(s)];
        const bool moved = slot != v;
        slot = v;
        return moved;
    };
    switch (option) {
        case eqt::GameOption::AnonStatus: {
            const bool a = set(Setting::Anonymous, value == 1 ? 1 : 0);
            const bool r = set(Setting::Roleplaying, value == 2 ? 1 : 0);
            return a || r;
        }
        case eqt::GameOption::AutoSplit:        return set(Setting::AutoSplit, value ? 1 : 0);
        case eqt::GameOption::AutoConsentGroup: return set(Setting::AutoConsentGroup, value ? 1 : 0);
        case eqt::GameOption::AutoConsentRaid:  return set(Setting::AutoConsentRaid, value ? 1 : 0);
        case eqt::GameOption::AutoConsentGuild: return set(Setting::AutoConsentGuild, value ? 1 : 0);
        default:                                return false;
    }
}

int gameOptionValueFor(Setting setting, bool checked) {
    switch (setting) {
        case Setting::Anonymous:   return checked ? 1 : 0;
        case Setting::Roleplaying: return checked ? 2 : 0;
        default:                   return checked ? 1 : 0;
    }
}

Setting optionsSelectedFilter(const OptionsPanelState& state) {
    const Setting s = OptionsLayout::filterAt(state.filterSelectedRow);
    return s == Setting::None ? OptionsLayout::filterAt(0) : s;
}

}  // namespace Graphics
}  // namespace EQT
