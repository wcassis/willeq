// B888 (GitLab #204) -- the Character Creation screen's state machine. The
// flow and its oracle citations: include/client/graphics/ui/char_create_screen.h.

#include "client/graphics/ui/char_create_screen.h"

#include <algorithm>
#include <random>

namespace EQT {
namespace Graphics {

using namespace eqt::charcreate;

namespace {

// eqstr_us.txt, verbatim.
constexpr uint32_t kEqstrCreationIntro = 3238;   // "Right click or select a race, class, or statistic name ..."
constexpr uint32_t kEqstrStatFirst = 3332;       // 3332..3338 Strength..Charisma
constexpr uint32_t kEqstrNameNotValid = 1576;    // "Name is not valid."
constexpr uint32_t kEqstrSpendAllPoints = 1577;  // "You must spend all of your ability points before continuing."
constexpr uint32_t kEqstrChooseCity = 1579;      // "Choose a valid starting city."
constexpr uint32_t kEqstrSubmitting = 1580;      // "Submitting name to server for approval. Please Wait."

bool contains(const std::vector<uint32_t>& v, uint32_t x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

const EraEntry* find(const std::vector<EraEntry>& v, uint32_t id) {
    for (const auto& e : v) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

// After the race/class/deity changed: keep the deity and city when they are
// still offered, otherwise take the first offered one (button order).
void refreshDeityCity(CharCreateScreenState& s) {
    const auto deities = s.offeredDeities();
    if (!contains(deities, s.deity)) s.deity = deities.empty() ? 0 : deities.front();
    const auto cities = s.offeredCities();
    if (!contains(cities, s.city)) s.city = cities.empty() ? 0 : cities.front();
}

void loadLists(CharCreateScreenState& s, const CharCreateData& data) {
    s.data = &data;
    s.races = data.visibleRaces();
    s.classes = data.visibleClasses();
    s.deities = data.visibleDeities();
    s.cities = data.visibleCities();
}

void resetForOpen(CharCreateScreenState& s) {
    s.isOpen = true;
    s.page = 0;
    s.alloc.reset(s.race, s.cls);
    s.deity = 0;
    s.city = 0;
    refreshDeityCity(s);
    clearTextEntry(s.nameEntry);
    s.helpEqstr = kEqstrCreationIntro;
    s.helpTitle.clear();
    s.messageEqstr = 0;
    s.messageText.clear();
    s.submitting = false;
    s.faceData.loadShipped();
    s.facePick.isOpen = false;
    s.appearance.fill(0);
}

}  // namespace

bool CharCreateScreenState::raceClassValid(uint32_t r, uint32_t c) const {
    return find(races, r) && find(classes, c) && isValidRaceClass(r, c);
}

std::vector<uint32_t> CharCreateScreenState::offeredDeities() const {
    return data ? data->allowedDeities(race, cls) : std::vector<uint32_t>();
}

std::vector<uint32_t> CharCreateScreenState::offeredCities() const {
    return data ? data->allowedCities(race, cls, deity) : std::vector<uint32_t>();
}

bool CharCreateScreenState::deityOffered(uint32_t d) const { return contains(offeredDeities(), d); }
bool CharCreateScreenState::cityOffered(uint32_t c) const { return contains(offeredCities(), c); }

void openCharCreate(CharCreateScreenState& s, const CharCreateData& data, uint32_t seed,
                    bool useOldModels) {
    loadLists(s, data);
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (const auto& r : s.races) {
        for (const auto& c : s.classes) {
            if (isValidRaceClass(r.id, c.id)) pairs.emplace_back(r.id, c.id);
        }
    }
    std::mt19937 rng(seed);
    if (!pairs.empty()) {
        const auto& p = pairs[rng() % pairs.size()];
        s.race = p.first;
        s.cls = p.second;
    }
    s.gender = (rng() & 1u) ? Gender::Female : Gender::Male;
    s.facePick.randomSeed = rng();
    s.useOldModels = useOldModels;
    resetForOpen(s);
}

bool openCharCreateWith(CharCreateScreenState& s, const CharCreateData& data, uint32_t race,
                        uint32_t gender, uint32_t cls, bool useOldModels) {
    loadLists(s, data);
    if (!s.raceClassValid(race, cls)) return false;
    s.race = race;
    s.cls = cls;
    s.gender = gender == Gender::Female ? Gender::Female : Gender::Male;
    s.facePick.randomSeed = 1;
    s.useOldModels = useOldModels;
    resetForOpen(s);
    return true;
}

bool selectRace(CharCreateScreenState& s, uint32_t race) {
    const EraEntry* r = find(s.races, race);
    if (!r || s.submitting) return false;
    s.race = race;
    s.facePick.isOpen = false;
    s.appearance.fill(0);
    if (!isValidRaceClass(race, s.cls)) {
        for (const auto& c : s.classes) {
            if (isValidRaceClass(race, c.id)) { s.cls = c.id; break; }
        }
    }
    s.alloc.reset(s.race, s.cls);
    refreshDeityCity(s);
    s.helpEqstr = r->eqstrId;
    s.helpTitle.clear();
    return true;
}

bool selectClass(CharCreateScreenState& s, uint32_t cls) {
    const EraEntry* c = find(s.classes, cls);
    if (!c || s.submitting || !isValidRaceClass(s.race, cls)) return false;  // greyed: inert
    s.cls = cls;
    s.alloc.reset(s.race, s.cls);
    refreshDeityCity(s);
    s.helpEqstr = c->eqstrId;
    s.helpTitle.clear();
    return true;
}

void selectGender(CharCreateScreenState& s, uint32_t gender) {
    if (s.submitting) return;
    const uint32_t next = gender == Gender::Female ? Gender::Female : Gender::Male;
    if (s.gender != next) {
        s.gender = next;
        s.facePick.isOpen = false;
        s.appearance.fill(0);
    }
}

bool selectDeity(CharCreateScreenState& s, uint32_t deity) {
    const EraEntry* d = find(s.deities, deity);
    if (!d || s.submitting || !s.deityOffered(deity)) return false;
    s.deity = deity;
    refreshDeityCity(s);
    s.helpEqstr = d->eqstrId;
    s.helpTitle.clear();
    return true;
}

bool selectCity(CharCreateScreenState& s, uint32_t city) {
    const EraEntry* c = find(s.cities, city);
    if (!c || s.submitting || !s.cityOffered(city)) return false;
    s.city = city;
    // The city's own description is not identified in eqstr_us.txt yet
    // (1960-2040 are race/class/city blurbs, not indexed): the label only.
    s.helpEqstr = 0;
    s.helpTitle = c->label;
    return true;
}

bool statUp(CharCreateScreenState& s, int stat) { return !s.submitting && s.alloc.increment(stat); }
bool statDown(CharCreateScreenState& s, int stat) { return !s.submitting && s.alloc.decrement(stat); }

int statMax(CharCreateScreenState& s, int stat) {
    if (s.submitting || stat < 0 || stat >= kStatCount) return 0;
    showStatHelp(s, stat);
    return s.alloc.spendMax(stat);
}

void resetPoints(CharCreateScreenState& s) {
    if (!s.submitting) s.alloc.reset(s.race, s.cls);
}

void showStatHelp(CharCreateScreenState& s, int stat) {
    if (stat < 0 || stat >= kStatCount) return;
    s.helpEqstr = kEqstrStatFirst + static_cast<uint32_t>(stat);
    s.helpTitle.clear();
}

CharCreateRequest buildRequest(const CharCreateScreenState& s) {
    CharCreateRequest r;
    r.name = s.nameEntry.inputText;
    r.race = s.race;
    r.gender = s.gender;
    r.cls = s.cls;
    r.deity = s.deity;
    r.startZone = s.city;
    r.stats = s.alloc.stats();
    r.face = s.appearance[static_cast<size_t>(FacePickFeature::Face)];
    if (s.race == Race::Barbarian)
        r.face += 10 * s.appearance[static_cast<size_t>(FacePickFeature::Wode)];
    r.hairstyle = s.appearance[static_cast<size_t>(FacePickFeature::Hair)];
    r.haircolor = s.appearance[static_cast<size_t>(FacePickFeature::HairColor)];
    r.eyecolor1 = r.eyecolor2 = s.appearance[static_cast<size_t>(FacePickFeature::Eyes)];
    r.beard = s.appearance[static_cast<size_t>(FacePickFeature::Beard)];
    r.beardcolor = s.appearance[static_cast<size_t>(FacePickFeature::BeardColor)];
    return r;
}

CharCreateAction pressNext(CharCreateScreenState& s) {
    CharCreateAction a;
    if (s.submitting || s.facePick.isOpen) return a;
    if (s.page == 0) {
        if (!s.alloc.allSpent()) {
            s.messageEqstr = kEqstrSpendAllPoints;
            s.messageText.clear();
            return a;
        }
        s.page = 1;
        refreshDeityCity(s);
        if (const EraEntry* d = find(s.deities, s.deity)) {
            s.helpEqstr = d->eqstrId;
            s.helpTitle.clear();
        }
        return a;
    }
    if (validateCharacterName(s.nameEntry.inputText) != NameCheck::kOk) {
        s.messageEqstr = kEqstrNameNotValid;   // measured: create_Oracledwarf/04_deitypage.png
        s.messageText.clear();
        return a;
    }
    if (!s.cityOffered(s.city)) {
        s.messageEqstr = kEqstrChooseCity;
        s.messageText.clear();
        return a;
    }
    s.submitting = true;
    s.messageEqstr = kEqstrSubmitting;
    s.messageText.clear();
    a.kind = CharCreateAction::Kind::Submit;
    a.request = buildRequest(s);
    return a;
}

CharCreateAction pressBack(CharCreateScreenState& s) {
    CharCreateAction a;
    if (s.submitting) return a;
    if (s.facePick.isOpen) { cancelFacePick(s); return a; }
    if (s.page == 1) {
        s.page = 0;
        s.helpEqstr = kEqstrCreationIntro;
        s.helpTitle.clear();
        return a;
    }
    s.isOpen = false;
    a.kind = CharCreateAction::Kind::BackToCharacterSelect;
    return a;
}

CharCreateAction pressGetName(CharCreateScreenState& s) {
    CharCreateAction a;
    if (s.submitting || s.page != 1) return a;
    a.kind = CharCreateAction::Kind::GetName;
    a.race = s.race;
    a.gender = s.gender;
    return a;
}

void applyRandomName(CharCreateScreenState& s, const std::string& name) {
    if (!name.empty()) focusTextEntry(s.nameEntry, name);
}

void applySubmitRefused(CharCreateScreenState& s, const std::string& serverText) {
    s.submitting = false;
    s.messageEqstr = 0;
    s.messageText = serverText;
}

void dismissMessage(CharCreateScreenState& s) {
    if (s.submitting) return;
    s.messageEqstr = 0;
    s.messageText.clear();
}

}  // namespace Graphics
}  // namespace EQT
