// B888 (GitLab #204) -- the --create-character spec parser. Grammar and rules:
// include/client/login_world/world_script.h.

#include "client/login_world/world_script.h"

#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_faces.h"

#include <algorithm>
#include <array>
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

struct AppearanceDescriptor {
    uint32_t CharCreateRequest::*member;
    size_t index;
};

AppearanceDescriptor appearanceByKey(const std::string& key) {
    const std::string k = upper(key);
    if (k == "FACE") return {&CharCreateRequest::face, static_cast<size_t>(FacePickFeature::Face)};
    if (k == "HAIR") return {&CharCreateRequest::hairstyle, static_cast<size_t>(FacePickFeature::Hair)};
    if (k == "HAIRCOLOR") return {&CharCreateRequest::haircolor, static_cast<size_t>(FacePickFeature::HairColor)};
    if (k == "EYES") return {&CharCreateRequest::eyecolor1, static_cast<size_t>(FacePickFeature::Eyes)};
    if (k == "BEARD") return {&CharCreateRequest::beard, static_cast<size_t>(FacePickFeature::Beard)};
    if (k == "BEARDCOLOR") return {&CharCreateRequest::beardcolor, static_cast<size_t>(FacePickFeature::BeardColor)};
    return {nullptr, kFacePickFeatures};
}

bool parseAppearanceValue(const std::string& text, uint32_t& value) {
    if (text.empty()) return false;
    uint32_t result = 0;
    for (unsigned char c : text) {
        if (!std::isdigit(c)) return false;
        result = result * 10 + (c - '0');
        if (result > 255) return false;
    }
    value = result;
    return true;
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

    StatAllocator a(r.race, r.cls);
    bool hasStat = false;
    FacePickData faces;
    const FacePickRanges* ranges = nullptr;
    std::array<bool, kFacePickFeatures> appearanceSeen{};
    for (size_t i = 6; i < f.size(); ++i) {
        const size_t eq = f[i].find('=');
        if (eq == std::string::npos) {
            error = "term '" + f[i] + "' requires KEY=value";
            return false;
        }
        const std::string key = trim(f[i].substr(0, eq));
        const auto appearance = appearanceByKey(key);
        if (appearance.member) {
            uint32_t value = 0;
            if (!parseAppearanceValue(trim(f[i].substr(eq + 1)), value)) {
                error = "appearance term '" + f[i] + "' requires an integer from 0 to 255";
                return false;
            }
            if (!ranges) {
                if (!faces.loadShipped()) { error = faces.error(); return false; }
                ranges = faces.find(r.race, r.gender);
                if (!ranges) { error = "no FacePick ranges for this race/gender"; return false; }
            }
            const size_t index = appearance.index;
            const auto& range = (*ranges)[index];
            if (appearanceSeen[index] || !range.visible || value < range.min || value > range.max) {
                error = "appearance term '" + f[i] + "' is repeated or not offered for this race/gender";
                return false;
            }
            appearanceSeen[index] = true;
            r.*appearance.member = value;
            if (appearance.member == &CharCreateRequest::eyecolor1) r.eyecolor2 = value;
            continue;
        }
        const int stat = statByKey(key);
        if (stat < 0) {
            error = "term '" + f[i] + "' is not a stat or FACE|HAIR|HAIRCOLOR|EYES|BEARD|BEARDCOLOR=n";
            return false;
        }
        hasStat = true;
        const int v = std::atoi(f[i].c_str() + eq + 1);
        if (!a.set(stat, v)) {
            error = std::string(statLabel(stat)) + "=" + std::to_string(v) + " is outside [" +
                    std::to_string(a.base(stat)) + ", base + points left]";
            return false;
        }
    }
    if (hasStat) {
        if (!a.allSpent()) {
            // eqstr 1577, verbatim.
            error = "You must spend all of your ability points before continuing. (" +
                    std::to_string(a.pointsLeft()) + " left)";
            return false;
        }
        r.stats = a.stats();
    } else {
        r.stats = defaultAllocation(r.race, r.cls);
    }
    out = r;
    return true;
}

}  // namespace charcreate
}  // namespace eqt
