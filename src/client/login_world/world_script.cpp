// B888 (GitLab #204) -- the --create-character spec parser. Grammar and rules:
// include/client/login_world/world_script.h.

#include "client/login_world/world_script.h"

#include "client/login_world/char_create_data.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace eqt {
namespace charcreate {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

int statByKey(const std::string& key) {
    static const char* const kKeys[kStatCount] = {"STR", "STA", "AGI", "DEX", "WIS", "INT", "CHA"};
    const std::string k = upper(key);
    for (int i = 0; i < kStatCount; ++i) {
        if (k == kKeys[i]) return i;
    }
    return -1;
}

}  // namespace

bool ParseCreateSpec(const std::string& spec, const CharCreateData& data, CharCreateRequest& out,
                     std::string& error) {
    std::vector<std::string> f;
    {
        std::istringstream ss(spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) f.push_back(trim(tok));
    }
    if (f.size() < 6) {
        error = "expected Name,Race,Gender,Class,Deity,City[,STR=n,...]";
        return false;
    }
    CharCreateRequest r;
    r.name = f[0];
    const NameCheck nc = validateCharacterName(r.name);
    if (nc != NameCheck::kOk) {
        error = "name '" + r.name + "': " + nameCheckText(nc);
        return false;
    }
    const EraEntry* race = data.raceByName(f[1]);
    if (!race) { error = "race '" + f[1] + "' is not an in-era race"; return false; }
    const std::string g = upper(f[2]);
    if (g == "MALE" || g == "M") r.gender = Gender::Male;
    else if (g == "FEMALE" || g == "F") r.gender = Gender::Female;
    else { error = "gender '" + f[2] + "' is not Male or Female"; return false; }
    const EraEntry* cls = data.classByName(f[3]);
    if (!cls) { error = "class '" + f[3] + "' is not an in-era class"; return false; }
    if (!isValidRaceClass(race->id, cls->id)) {
        error = race->label + " " + cls->label + " is not a combination the server allows";
        return false;
    }
    const EraEntry* deity = data.deityByName(f[4]);
    if (!deity) { error = "deity '" + f[4] + "' is not a deity on the screen"; return false; }
    const auto deities = data.allowedDeities(race->id, cls->id);
    if (std::find(deities.begin(), deities.end(), deity->id) == deities.end()) {
        error = deity->label + " is not offered to a " + race->label + " " + cls->label;
        return false;
    }
    const EraEntry* city = data.cityByName(f[5]);
    if (!city) { error = "city '" + f[5] + "' is not an in-era city"; return false; }
    const auto cities = data.allowedCities(race->id, cls->id, deity->id);
    if (std::find(cities.begin(), cities.end(), city->id) == cities.end()) {
        error = city->label + " is not offered to a " + race->label + " " + cls->label +
                " of " + deity->label + " (eqstr 1579: Choose a valid starting city.)";
        return false;
    }
    r.race = race->id;
    r.cls = cls->id;
    r.deity = deity->id;
    r.startZone = city->id;

    if (f.size() == 6) {
        r.stats = defaultAllocation(r.race, r.cls);
    } else {
        StatAllocator a(r.race, r.cls);
        for (size_t i = 6; i < f.size(); ++i) {
            const size_t eq = f[i].find('=');
            const int stat = eq == std::string::npos ? -1 : statByKey(trim(f[i].substr(0, eq)));
            if (stat < 0) {
                error = "stat term '" + f[i] + "' is not STR|STA|AGI|DEX|WIS|INT|CHA=n";
                return false;
            }
            const int v = std::atoi(f[i].c_str() + eq + 1);
            if (!a.set(stat, v)) {
                error = std::string(statLabel(stat)) + "=" + std::to_string(v) + " is outside [" +
                        std::to_string(a.base(stat)) + ", base + points left]";
                return false;
            }
        }
        if (!a.allSpent()) {
            // eqstr 1577, verbatim.
            error = "You must spend all of your ability points before continuing. (" +
                    std::to_string(a.pointsLeft()) + " left)";
            return false;
        }
        r.stats = a.stats();
    }
    out = r;
    return true;
}

}  // namespace charcreate
}  // namespace eqt
