/*
 * `/option` and `/filter` -- see include/client/graphics/ui/options_command.h.
 */

#include "client/graphics/ui/options_command.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "client/graphics/ui/options_panel.h"

namespace EQT {
namespace Graphics {

namespace {

using OptionsLayout::Owner;
using OptionsLayout::Setting;
using OptionsLayout::ValueKind;

// Lower case with spaces, '-' and '_' removed: "Show Mine Only" -> "showmineonly".
std::string squash(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool listed(const OptionsLayout::SettingInfo& info) {
    return info.setting != Setting::None && info.name[0] != '\0' &&
           info.owner != Owner::Marker && info.owner != Owner::None;
}

// The value `text` names for `s`, or false if it names none.
bool parseValue(Setting s, const std::string& text, int& out) {
    const auto& info = OptionsLayout::settingInfo(s);
    const std::string t = squash(text);
    if (info.kind == ValueKind::Bool) {
        if (t == "on" || t == "1" || t == "true" || t == "yes") { out = 1; return true; }
        if (t == "off" || t == "0" || t == "false" || t == "no") { out = 0; return true; }
        return false;
    }
    if (info.kind == ValueKind::Choice) {
        for (int i = 0; i < info.choiceCount; ++i) {
            if (squash(info.choices[i].text) == t) { out = info.choices[i].value; return true; }
        }
    }
    try {
        size_t used = 0;
        const int v = std::stoi(t, &used);
        if (used != t.size()) return false;
        out = v;
        return OptionsLayout::isValidSettingValue(s, v);
    } catch (...) {
        return false;
    }
}

std::string choicesHint(Setting s) {
    const auto& info = OptionsLayout::settingInfo(s);
    switch (info.kind) {
        case ValueKind::Bool: return "on|off";
        case ValueKind::Step: return "0-" + std::to_string(info.maxValue);
        case ValueKind::Choice: {
            std::string h;
            for (int i = 0; i < info.choiceCount; ++i) {
                if (i) h += "|";
                h += squash(info.choices[i].text);
            }
            return h;
        }
        case ValueKind::None: break;
    }
    return "";
}

std::string colorText(const OptionsPanelState& state, int n) {
    const auto& c = state.textColors[n - 1];
    return "textcolor " + std::to_string(n) + " (" + eqt::ui::userColorName(n) + ") = " +
           std::to_string(c.r) + " " + std::to_string(c.g) + " " + std::to_string(c.b);
}

// B993: `/option textcolor [reset | <N|name> [<r> <g> <b>]]`. A row is named
// by its User_N number or its Options-list text, squashed ("youhitother").
void runTextColor(OptionsPanelState& state, const std::string& rest, OptionsCommandResult& r) {
    static const char* kUsage =
        "Usage: /option textcolor [reset | <1-86|name> [<r> <g> <b>]] (0-255)";
    std::istringstream in(rest);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    if (words.empty()) {
        r.lines.push_back(kUsage);
        return;
    }
    if (squash(words[0]) == "reset") {
        if (resetOptionsTextColors(state)) r.changed.push_back(Setting::TextColorReset);
        r.lines.push_back("textcolor: every row reset to the defaults");
        return;
    }
    // The row: a number, else the longest run of leading words naming one.
    int n = 0;
    size_t used = 0;
    try {
        size_t pos = 0;
        const int v = std::stoi(words[0], &pos);
        if (pos == words[0].size()) { n = v; used = 1; }
    } catch (...) {
    }
    if (used == 0) {
        for (size_t k = words.size(); k >= 1 && n == 0; --k) {
            std::string joined;
            for (size_t j = 0; j < k; ++j) joined += words[j];
            for (int m = 1; m <= eqt::ui::kUserColorCount; ++m) {
                if (squash(eqt::ui::userColorName(m)) == squash(joined)) { n = m; used = k; break; }
            }
        }
    }
    if (n < 1 || n > eqt::ui::kUserColorCount) {
        r.lines.push_back(kUsage);
        return;
    }
    if (words.size() == used) {
        r.lines.push_back(colorText(state, n));
        return;
    }
    if (words.size() != used + 3) {
        r.lines.push_back(kUsage);
        return;
    }
    int rgb[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        try {
            size_t pos = 0;
            rgb[k] = std::stoi(words[used + k], &pos);
            if (pos != words[used + k].size() || rgb[k] < 0 || rgb[k] > 255) throw 0;
        } catch (...) {
            r.lines.push_back(kUsage);
            return;
        }
    }
    eqt::ui::UserColor c;
    c.r = static_cast<uint8_t>(rgb[0]);
    c.g = static_cast<uint8_t>(rgb[1]);
    c.b = static_cast<uint8_t>(rgb[2]);
    if (state.textColors[n - 1] != c) {
        state.textColors[n - 1] = c;
        r.changed.push_back(Setting::TextColors);
    }
    r.lines.push_back(colorText(state, n));
}

void runOption(OptionsPanelState& state, const std::string& args, OptionsCommandResult& r) {
    std::istringstream in(args);
    std::string name;
    in >> name;
    std::string rest;
    std::getline(in, rest);
    rest = trim(rest);

    if (squash(name) == "textcolor") {
        runTextColor(state, rest, r);
        return;
    }

    if (name.empty() || squash(name) == "list") {
        for (int i = 1; i < OptionsLayout::kSettingCount; ++i) {
            const auto& info = OptionsLayout::allSettings()[i];
            if (!listed(info)) continue;
            r.lines.push_back(std::string(info.name) + " = " + OptionsValueText(state, info.setting));
        }
        return;
    }
    const OptionsLayout::SettingInfo* info = OptionsLayout::findSettingByName(name);
    if (!info || !listed(*info)) {
        r.lines.push_back("Unknown option '" + name + "'. Usage: /option [list | <name> [value]]");
        return;
    }
    if (rest.empty()) {
        r.lines.push_back(std::string(info->name) + " = " + OptionsValueText(state, info->setting));
        return;
    }
    int value = 0;
    if (!parseValue(info->setting, rest, value)) {
        r.lines.push_back("Usage: /option " + std::string(info->name) + " <" +
                          choicesHint(info->setting) + ">");
        return;
    }
    if (setOptionsValue(state, info->setting, value)) r.changed.push_back(info->setting);
    r.lines.push_back(std::string(info->name) + " = " + OptionsValueText(state, info->setting));
}

void runFilter(OptionsPanelState& state, const std::string& args, OptionsCommandResult& r) {
    const std::string name = squash(args);
    const OptionsLayout::SettingInfo* info =
        name.empty() ? nullptr : OptionsLayout::findSettingByName(name);
    if (!info || OptionsLayout::filterRow(info->setting) < 0) {
        // eqstr_us.txt 1354-1360, verbatim.
        r.lines.push_back("Usage: /filter <name>");
        r.lines.push_back("Toggle text filters on or off. ");
        r.lines.push_back("<name> can be one of the following:");
        r.lines.push_back("badword, guildsay, social, gsay (for group say),");
        r.lines.push_back("shout, auction, ooc, mymisses, othersmisses,");
        r.lines.push_back("otherhits, atkmissesme, pcspells, npcspells,");
        r.lines.push_back("bardsongs, dmgshield, critspells, critmelee");
        return;
    }
    const Setting s = info->setting;
    const bool hides = OptionsLayout::filterHides(s, optionsSettingStep(state, s));
    const int next = hides ? OptionsLayout::filterShowValue(s) : OptionsLayout::filterHideValue(s);
    if (setOptionsValue(state, s, next)) r.changed.push_back(s);
    const std::string text = OptionsLayout::filterSlashText(s);
    // eqstr 1351 "%1 *ON*" (the filter now hides) / 1348 "%1 *OFF*".
    r.lines.push_back(text + (OptionsLayout::filterHides(s, optionsSettingStep(state, s)) ? " *ON*" : " *OFF*"));
}

// B1165 (#484): `/showspelleffects <on|off>`, eqgame FUN_004688b3's body
// (eqgame.exe.c:88027-88054): no argument prints the usage (eqstr 13168,
// 13169); "off" (eqstr 12946, case-insensitive) turns the switch off and any
// other argument turns it on; then the state line (13170 ON / 13171 OFF).
void runShowSpellEffects(OptionsPanelState& state, const std::string& args, OptionsCommandResult& r) {
    const std::string a = trim(args);
    if (a.empty()) {
        r.lines.push_back("FORMAT: /showspelleffects <ON | OFF> (default is ON)");
        r.lines.push_back("This command allows you to turn on/off all spell particle effects (NOT world or weapon particles).");
    } else {
        setOptionsValue(state, Setting::ShowSpellEffects, squash(a) == "off" ? 0 : 1);
        r.changed.push_back(Setting::ShowSpellEffects);
    }
    r.lines.push_back(optionsSettingStep(state, Setting::ShowSpellEffects) != 0
                          ? "Spell particle effects are currently ON."
                          : "Spell particle effects are currently OFF.");
}

}  // namespace

std::string OptionsValueText(const OptionsPanelState& state, Setting setting) {
    const auto& info = OptionsLayout::settingInfo(setting);
    const int v = optionsSettingStep(state, setting);
    switch (info.kind) {
        case ValueKind::Bool:   return v ? "on" : "off";
        case ValueKind::Choice: return OptionsLayout::choiceText(setting, v);
        case ValueKind::Step:
            if (info.owner == Owner::Legacy) return std::to_string(v) + " (" + optionsSettingText(state, setting) + ")";
            return std::to_string(v);
        case ValueKind::None:   break;
    }
    return "";
}

OptionsCommandResult RunOptionsCommand(OptionsPanelState& state, const std::string& cmd,
                                       const std::string& args) {
    OptionsCommandResult r;
    std::string c = squash(cmd);
    if (!c.empty() && c[0] == '/') c.erase(0, 1);
    if (c == "option") runOption(state, args, r);
    else if (c == "filter") runFilter(state, args, r);
    else if (c == "showspelleffects") runShowSpellEffects(state, args, r);
    return r;
}

}  // namespace Graphics
}  // namespace EQT
