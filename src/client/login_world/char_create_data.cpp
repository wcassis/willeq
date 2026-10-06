// B888 (GitLab #204) -- data/char_create.tsv, parsed. The file header is the
// documentation of record; see include/client/login_world/char_create_data.h.

#include "client/login_world/char_create_data.h"

#include "client/data_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace eqt {
namespace charcreate {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, '\t')) out.push_back(cur);
    return out;
}

std::vector<std::string> splitCommas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, ',')) {
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

// Case-insensitive, spaces and apostrophes ignored.
std::string squash(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\'' || c == '`' || c == '_') continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::vector<EraEntry> visibleOf(const std::vector<EraEntry>& all) {
    std::vector<EraEntry> out;
    for (const auto& e : all) {
        if (isInEra(e.era)) out.push_back(e);
    }
    std::sort(out.begin(), out.end(),
              [](const EraEntry& a, const EraEntry& b) { return a.label < b.label; });
    return out;
}

const EraEntry* byId(const std::vector<EraEntry>& all, uint32_t id) {
    for (const auto& e : all) {
        if (e.id == id && isInEra(e.era)) return &e;
    }
    return nullptr;
}

const EraEntry* byName(const std::vector<EraEntry>& all, const std::string& name) {
    const std::string want = squash(name);
    for (const auto& e : all) {
        if (!isInEra(e.era)) continue;
        const std::string label = squash(e.label);
        if (label == want) return &e;
        if (label.rfind("the", 0) == 0 && label.substr(3) == want) return &e;
    }
    return nullptr;
}

}  // namespace

bool isInEra(const std::string& era) {
    return era == "Classic" || era == "Kunark" || era == "Velious";
}

bool CharCreateData::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        error_ = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadText(ss.str());
}

bool CharCreateData::loadShipped() {
    return loadFile(eqt::ResolveDataPath("data/char_create.tsv"));
}

bool CharCreateData::loadText(const std::string& text) {
    races_.clear(); classes_.clear(); deities_.clear(); cities_.clear();
    deityRows_.clear(); cityRows_.clear();
    error_.clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line.rfind("kind\t", 0) == 0) continue;
        const auto f = splitTabs(line);
        const std::string& kind = f[0];
        auto bad = [&](const char* why) {
            error_ = "char_create.tsv line " + std::to_string(lineNo) + ": " + why;
            return false;
        };
        if (kind == "race" || kind == "class" || kind == "deity") {
            if (f.size() < 6) return bad("expected 6 columns");
            EraEntry e;
            e.xmlIndex = std::atoi(f[1].c_str());
            e.id = static_cast<uint32_t>(std::strtoul(f[2].c_str(), nullptr, 10));
            e.label = f[3];
            e.era = f[4];
            e.eqstrId = static_cast<uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10));
            (kind == "race" ? races_ : kind == "class" ? classes_ : deities_).push_back(e);
        } else if (kind == "city") {
            if (f.size() < 5) return bad("expected 5 columns");
            EraEntry e;
            e.xmlIndex = std::atoi(f[1].c_str());
            e.id = static_cast<uint32_t>(e.xmlIndex);
            e.label = f[2];
            e.era = f[3];
            e.homeZones = splitCommas(f[4]);
            cities_.push_back(e);
        } else if (kind == "deities") {
            if (f.size() < 5) return bad("expected 5 columns");
            deityRows_.push_back({f[1], f[2], "*", splitCommas(f[3]), f[4]});
        } else if (kind == "cities") {
            if (f.size() < 6) return bad("expected 6 columns");
            cityRows_.push_back({f[1], f[2], f[3], splitCommas(f[4]), f[5]});
        } else {
            return bad("unknown kind");
        }
    }
    if (races_.empty() || classes_.empty() || deities_.empty() || cities_.empty()) {
        error_ = "char_create.tsv: a race, class, deity or city section is empty";
        return false;
    }
    return true;
}

std::vector<EraEntry> CharCreateData::visibleRaces() const {
    // BUTTON ORDER is the label order: EQUI_CharacterCreate.xml places
    // CC0_Race0..13 alphabetically and slots CC0_Race14 Froglok between
    // Erudite and Gnome. Sorting by label reproduces the on-screen order with
    // the hidden entries simply absent (Will: laid out without them).
    return visibleOf(races_);
}
std::vector<EraEntry> CharCreateData::visibleClasses() const { return visibleOf(classes_); }

std::vector<EraEntry> CharCreateData::visibleDeities() const {
    // Deities and cities are NOT alphabetical on the screen: keep XML order.
    std::vector<EraEntry> out;
    for (const auto& e : deities_) if (isInEra(e.era)) out.push_back(e);
    return out;
}
std::vector<EraEntry> CharCreateData::visibleCities() const {
    std::vector<EraEntry> out;
    for (const auto& e : cities_) if (isInEra(e.era)) out.push_back(e);
    return out;
}

const EraEntry* CharCreateData::raceById(uint32_t id) const { return byId(races_, id); }
const EraEntry* CharCreateData::classById(uint32_t id) const { return byId(classes_, id); }
const EraEntry* CharCreateData::deityById(uint32_t id) const { return byId(deities_, id); }
const EraEntry* CharCreateData::cityByIndex(uint32_t z) const { return byId(cities_, z); }
const EraEntry* CharCreateData::raceByName(const std::string& n) const { return byName(races_, n); }
const EraEntry* CharCreateData::classByName(const std::string& n) const { return byName(classes_, n); }
const EraEntry* CharCreateData::deityByName(const std::string& n) const { return byName(deities_, n); }
const EraEntry* CharCreateData::cityByName(const std::string& n) const { return byName(cities_, n); }

const CharCreateData::FilterRow* CharCreateData::bestRow(const std::vector<FilterRow>& rows,
                                                         uint32_t race, uint32_t cls,
                                                         uint32_t deity, bool useDeity) const {
    const EraEntry* r = raceById(race);
    const EraEntry* c = classById(cls);
    const EraEntry* d = deityById(deity);
    const std::string rl = r ? r->label : std::string();
    const std::string cl = c ? c->label : std::string();
    const std::string dl = d ? d->label : std::string();
    const FilterRow* best = nullptr;
    int bestScore = -1;
    for (const auto& row : rows) {
        int score = 0;
        if (row.race != "*") { if (row.race != rl) continue; score += 4; }
        if (row.cls != "*") { if (row.cls != cl) continue; score += 2; }
        if (useDeity && row.deity != "*") { if (row.deity != dl) continue; score += 1; }
        if (score > bestScore) { best = &row; bestScore = score; }
    }
    return best;
}

std::vector<uint32_t> CharCreateData::allowedDeities(uint32_t race, uint32_t cls) const {
    std::vector<uint32_t> out;
    const FilterRow* row = bestRow(deityRows_, race, cls, 0, false);
    if (!row) return out;
    for (const auto& d : visibleDeities()) {
        const bool all = row->values.size() == 1 && row->values[0] == "ALL";
        if (all || std::find(row->values.begin(), row->values.end(), d.label) != row->values.end())
            out.push_back(d.id);
    }
    return out;
}

std::vector<uint32_t> CharCreateData::allowedCities(uint32_t race, uint32_t cls,
                                                    uint32_t deity) const {
    std::vector<uint32_t> out;
    const FilterRow* row = bestRow(cityRows_, race, cls, deity, true);
    if (!row) return out;
    for (const auto& c : visibleCities()) {
        if (std::find(row->values.begin(), row->values.end(), c.label) != row->values.end())
            out.push_back(c.id);
    }
    return out;
}

std::string CharCreateData::citySource(uint32_t race, uint32_t cls, uint32_t deity) const {
    const FilterRow* row = bestRow(cityRows_, race, cls, deity, true);
    return row ? row->source : std::string();
}

}  // namespace charcreate
}  // namespace eqt
