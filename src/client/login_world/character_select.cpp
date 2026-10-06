// B998 (GitLab #318) -- the character-select model: the thread seam, the
// show/hide decision and the composed text. Its own TU (M00). The rules and
// their oracle citations: include/client/login_world/character_select.h.

#include "client/login_world/character_select.h"

#include "client/data_paths.h"
#include "client/eq_strings.h"
#include "client/login_world/char_create_data.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace eqt {
namespace login_world {

namespace {

// eqstr_us.txt, verbatim. The select screen's own strings:
constexpr uint32_t kEqstrDeletePrompt = 9065;     // "Are you absolutely sure you want to delete %1?" (MEASURED, 318/10)
constexpr uint32_t kEqstrCreateNewCharacter = 1947;   // "Create a New Character"
// ...and the creation screen's (char_create_screen.cpp / char_create_panel.cpp):
constexpr uint32_t kCreationFixed[] = {
    3238,                                   // the creation intro
    3332, 3333, 3334, 3335, 3336, 3337, 3338,   // Strength..Charisma
    1576, 1577, 1579, 1580, 1581,           // the refusals and the submit line
    5874, 5875,                             // CharacterCreationHelp, page 0 / page 1
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

void CharacterSelectChannel::publish(const CharacterSelectView& view) {
    std::lock_guard<std::mutex> lock(mutex_);
    view_ = view;
    ++generation_;
}

bool CharacterSelectChannel::takeViewIfNewer(uint64_t& seen, CharacterSelectView& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ == seen) return false;
    seen = generation_;
    out = view_;
    return true;
}

void CharacterSelectChannel::post(const CharacterSelectRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back(req);
}

std::vector<CharacterSelectRequest> CharacterSelectChannel::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CharacterSelectRequest> out;
    out.swap(requests_);
    return out;
}

bool CharacterSelectScreenWanted(const std::string& configuredCharacter, bool explicitFlag,
                                 bool graphicsEnabled, bool worldScriptRequested, bool scenarioRun) {
    if (!graphicsEnabled || worldScriptRequested) return false;
    if (scenarioRun) return explicitFlag;
    return explicitFlag || configuredCharacter.empty();
}

std::string ComposeCharacterInfoLine(const std::string& name, int level, const std::string& className) {
    // MEASURED: "Oraclealpha [1 Warrior]" (eq-oracle/logs/shots/cc_13_delete_confirm.png),
    // "Newbie [1 Warrior]" (review/UIGAP-ORACLE/204/00_character_select.png).
    return name + " [" + std::to_string(level) + " " + className + "]";
}

std::map<uint32_t, std::string> LoadEraZoneLongNames() {
    std::map<uint32_t, std::string> names;
    std::ifstream in(eqt::ResolveDataPath("data/era_zones.tsv"));
    std::string line;
    while (in && std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("expansion\t", 0) == 0) continue;
        std::istringstream ss(line);
        std::string exp, id, shortName, longName;
        if (!std::getline(ss, exp, '\t') || !std::getline(ss, id, '\t') ||
            !std::getline(ss, shortName, '\t') || !std::getline(ss, longName, '\t')) {
            continue;
        }
        try {
            names[static_cast<uint32_t>(std::stoul(trim(id)))] = trim(longName);
        } catch (...) {
        }
    }
    return names;
}

std::vector<CharacterSelectRow> BuildCharacterSelectRows(
    const std::vector<CharacterSelectEntry>& list, const std::map<uint32_t, std::string>& zoneNames) {
    std::vector<CharacterSelectRow> rows;
    rows.reserve(list.size());
    for (const auto& e : list) {
        CharacterSelectRow r;
        r.slot = e.slot;
        r.name = e.name;
        r.level = e.level;
        r.cls = e.char_class;
        r.race = e.race;
        r.zone = e.zone;
        r.infoLine = ComposeCharacterInfoLine(e.name, e.level, eqt::strings::GetClassName(e.char_class));
        const auto it = zoneNames.find(e.zone);
        // A zone outside the era list (the tutorial, 189, among them) has no
        // long name here: fable prints its id (assumption -- the oracle prints
        // "The Mines of Gloomingdeep" from its own zone table).
        r.zoneLine = it != zoneNames.end() ? it->second : "Zone " + std::to_string(e.zone);
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(),
              [](const CharacterSelectRow& a, const CharacterSelectRow& b) { return a.slot < b.slot; });
    return rows;
}

std::vector<uint32_t> CharacterSelectEqstrIds(const charcreate::CharCreateData& data) {
    std::vector<uint32_t> ids = {kEqstrDeletePrompt, kEqstrCreateNewCharacter};
    ids.insert(ids.end(), std::begin(kCreationFixed), std::end(kCreationFixed));
    // In-era entries only: a hidden race/class (Will 2026-09-23) is never drawn,
    // so its description is never asked for.
    for (const auto& list : {data.visibleRaces(), data.visibleClasses(), data.visibleDeities(),
                             data.visibleCities()}) {
        for (const auto& e : list) {
            if (e.eqstrId) ids.push_back(e.eqstrId);
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

}  // namespace login_world
}  // namespace eqt
