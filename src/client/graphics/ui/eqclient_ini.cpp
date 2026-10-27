/*
 * B1175 (GitLab #494): fable's global eqclient.ini -- see
 * include/client/graphics/ui/eqclient_ini.h for what is in it, where it lives
 * and the read / write rules, each with its decompile cite.
 */

#include "client/graphics/ui/eqclient_ini.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "client/data_paths.h"
#include "client/graphics/ui/options_panel.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

namespace OL = OptionsLayout;

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The Windows profile API compares section and key names case-insensitively.
bool SameName(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return i == a.size() && b[i] == '\0';
}

bool IsEqClientRow(int i) {
    return i > 0 && i < OL::kSettingCount &&
           OL::isEqClientIniSetting(static_cast<OL::Setting>(i));
}

// One value, the same rules ui_preferences.cpp applies to its own keys
// (B995 / B1165): a float keeps its clamped value as the APPLIED one, a bool
// takes TRUE/FALSE too, anything the table refuses keeps the default.
void ParseValue(int i, const std::string& value, EqClientIni& p) {
    const auto& info = OL::allSettings()[i];
    const std::string t = Trim(value);
    int v = 0;
    if (OL::isIniFloatSetting(info.setting)) {
        std::optional<float> applied;
        if (!OL::parseSettingIniFloat(info.setting, t, v, applied)) return;
        p.optionValues[i] = v;
        p.optionIniFloats[i] = applied;
        return;
    }
    if (info.kind == OL::ValueKind::Bool &&
        (t == "TRUE" || t == "true" || t == "FALSE" || t == "false")) {
        v = (t == "TRUE" || t == "true") ? 1 : 0;
    } else if (!OL::parseSettingIniText(info.setting, t, v)) {
        return;
    }
    if (OL::isValidSettingValue(info.setting, v)) p.optionValues[i] = v;
}

// A line split that remembers the file's own line ending.
struct Lines {
    std::vector<std::string> lines;
    // A NEW file is written with fable's own "\n" (as UI_<Char>_<server>.ini
    // is); an existing one keeps its own (the client's files are CRLF).
    std::string eol = "\n";
    bool endsWithEol = true;
};

Lines SplitLines(const std::string& text) {
    Lines out;
    if (text.empty()) return out;
    if (text.find("\r\n") != std::string::npos) out.eol = "\r\n";
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            out.lines.push_back(text.substr(start));
            out.endsWithEol = false;
            break;
        }
        size_t end = nl;
        if (end > start && text[end - 1] == '\r') --end;
        out.lines.push_back(text.substr(start, end - start));
        start = nl + 1;
    }
    return out;
}

// "[Name]" -> Name; nullopt for any other line.
std::optional<std::string> SectionOf(const std::string& line) {
    const std::string t = Trim(line);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') return t.substr(1, t.size() - 2);
    return std::nullopt;
}

// "Key=Value" -> Key (trimmed); nullopt for a comment or a line with no '='.
std::optional<std::string> KeyOf(const std::string& line) {
    const std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#') return std::nullopt;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) return std::nullopt;
    return Trim(t.substr(0, eq));
}

}  // namespace

bool EqClientIni::operator==(const EqClientIni& o) const {
    for (int i = 0; i < OL::kSettingCount; ++i) {
        if (!IsEqClientRow(i)) continue;
        if (optionValues[i] != o.optionValues[i] || optionIniFloats[i] != o.optionIniFloats[i])
            return false;
    }
    return true;
}

EqClientIni CollectEqClientIni(const OptionsPanelState& s) {
    EqClientIni p;
    for (int i = 0; i < OL::kSettingCount; ++i) {
        if (!IsEqClientRow(i)) continue;
        p.optionValues[i] = s.values[i];
        p.optionIniFloats[i] = s.iniFloats[i];
    }
    return p;
}

void ApplyEqClientIni(const EqClientIni& p, OptionsPanelState& s) {
    for (int i = 0; i < OL::kSettingCount; ++i) {
        if (!IsEqClientRow(i)) continue;
        const auto setting = static_cast<OL::Setting>(i);
        if (!OL::isValidSettingValue(setting, p.optionValues[i])) continue;
        s.values[i] = p.optionValues[i];
        const auto& raw = p.optionIniFloats[i];
        s.iniFloats[i] = (raw && OL::isIniFloatSetting(setting))
                             ? std::optional<float>(OL::clampSettingIniFloat(setting, *raw))
                             : std::nullopt;
    }
}

EqClientIni ParseEqClientIni(const std::string& text, const EqClientIni& defaults) {
    EqClientIni p = defaults;
    const Lines lines = SplitLines(text);
    std::vector<bool> seenKey(OL::kSettingCount, false);
    std::string section;
    bool inSection = false;
    // GetPrivateProfileString reads the FIRST section of that name: a row's
    // key is only taken while its section's first occurrence is open.
    std::vector<std::string> closedSections;
    for (const std::string& line : lines.lines) {
        if (auto name = SectionOf(line)) {
            if (inSection) closedSections.push_back(section);
            section = *name;
            inSection = true;
            for (const auto& c : closedSections) {
                if (SameName(section, c.c_str())) { inSection = false; break; }
            }
            continue;
        }
        if (!inSection) continue;
        const auto key = KeyOf(line);
        if (!key) continue;
        for (int i = 0; i < OL::kSettingCount; ++i) {
            if (!IsEqClientRow(i) || seenKey[i]) continue;
            const auto& info = OL::allSettings()[i];
            if (!SameName(section, info.eqclientSection) || !SameName(*key, info.iniKey)) continue;
            seenKey[i] = true;
            const std::string t = Trim(line);
            ParseValue(i, t.substr(t.find('=') + 1), p);
            break;
        }
    }
    return p;
}

std::string UpdateEqClientIniText(const std::string& existing, const EqClientIni& p) {
    Lines lines = SplitLines(existing);
    for (int i = 0; i < OL::kSettingCount; ++i) {
        if (!IsEqClientRow(i)) continue;
        const auto& info = OL::allSettings()[i];
        const auto setting = static_cast<OL::Setting>(i);
        const std::string value =
            OL::settingIniText(setting, p.optionValues[i], p.optionIniFloats[i]);
        // The first section of that name, and the line after its last entry.
        int sectionAt = -1;
        int insertAt = -1;
        int keyAt = -1;
        for (int n = 0; n < static_cast<int>(lines.lines.size()); ++n) {
            if (auto name = SectionOf(lines.lines[n])) {
                if (sectionAt >= 0) break;  // left the first matching section
                if (SameName(*name, info.eqclientSection)) {
                    sectionAt = n;
                    insertAt = n + 1;
                }
                continue;
            }
            if (sectionAt < 0) continue;
            if (!Trim(lines.lines[n]).empty()) insertAt = n + 1;
            const auto key = KeyOf(lines.lines[n]);
            if (key && SameName(*key, info.iniKey)) {
                keyAt = n;
                break;
            }
        }
        if (keyAt >= 0) {
            // Keep the key's own spelling, replace the value.
            const std::string& old = lines.lines[keyAt];
            const size_t eq = old.find('=');
            lines.lines[keyAt] = Trim(old.substr(0, eq)) + "=" + value;
        } else if (sectionAt >= 0) {
            lines.lines.insert(lines.lines.begin() + insertAt, std::string(info.iniKey) + "=" + value);
        } else {
            lines.lines.push_back(std::string("[") + info.eqclientSection + "]");
            lines.lines.push_back(std::string(info.iniKey) + "=" + value);
        }
    }
    std::string out;
    for (size_t n = 0; n < lines.lines.size(); ++n) {
        out += lines.lines[n];
        if (n + 1 < lines.lines.size() || lines.endsWithEol) out += lines.eol;
    }
    return out;
}

bool LoadEqClientIni(EqClientIni& out) {
    const std::string path = eqt::ResolveCachePathForRead(kEqClientIniCacheName);
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "eqclient.ini: none in the cache directory -- defaults");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = ParseEqClientIni(buf.str(), out);
    LOG_INFO(MOD_UI, "eqclient.ini loaded from {}", path);
    return true;
}

bool SaveEqClientIni(const EqClientIni& p) {
    const std::string path = eqt::ResolveCachePathForWrite(kEqClientIniCacheName);
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "eqclient.ini: no writable cache directory");
        return false;
    }
    std::string existing;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream buf;
            buf << in.rdbuf();
            existing = buf.str();
        }
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream outFile(tmp, std::ios::binary | std::ios::trunc);
        if (!outFile) {
            LOG_WARN(MOD_UI, "eqclient.ini: could not write {}", tmp);
            return false;
        }
        outFile << UpdateEqClientIniText(existing, p);
        if (!outFile) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        LOG_WARN(MOD_UI, "eqclient.ini: could not rename {} -> {}", tmp, path);
        std::remove(tmp.c_str());
        return false;
    }
    LOG_DEBUG(MOD_UI, "eqclient.ini saved to {}", path);
    return true;
}

}  // namespace Graphics
}  // namespace EQT
