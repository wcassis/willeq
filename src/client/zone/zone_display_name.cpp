// B1088 (GitLab #408): the zone display name's single lookup body.
// Header: include/client/zone/zone_display_name.h.

#include "client/zone/zone_display_name.h"

#include "client/login_world/character_select.h"
#include "common/logging.h"

namespace eqt {

ZoneDisplayNames& ZoneDisplayNames::instance() {
    // Function-local: no init-order concerns, and the era table (small,
    // read-only after parse) is parsed per lookup (a tiny file).
    static ZoneDisplayNames names;
    return names;
}

std::map<uint32_t, std::string> ZoneDisplayNames::eraNames() const {
    // The one parser: the character-select zone line's own loader
    // (ResolveDataPath over data/era_zones.tsv, col 4). Not a second copy.
    return eqt::login_world::LoadEraZoneLongNames();
}

void ZoneDisplayNames::setOverride(uint32_t zoneId, std::string longName) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (longName.empty()) {
        overrides_.erase(zoneId);
    } else {
        overrides_[zoneId] = std::move(longName);
    }
}

std::string ZoneDisplayNames::lookup(uint32_t zoneId) const {
    // The override is authoritative for that zone: it is what the server
    // itself sent, and it is the only source that knows the ids the era
    // table omits.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = overrides_.find(zoneId);
        if (it != overrides_.end() && !it->second.empty()) {
            return it->second;
        }
    }
    const auto names = eraNames();
    const auto it = names.find(zoneId);
    return it != names.end() ? it->second : std::string();
}

std::string ZoneDisplayNames::entryLine(const std::string& name) {
    if (name.empty()) return std::string();
    return "Entering " + name + "...";
}

std::string ZoneDisplayNames::entryText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entry_;
}

// entry_ recomputed for shownZoneId_ ("" when that zone has no name).
// lookup() owns its own locking; the era-table read is a tiny file parse.
void ZoneDisplayNames::recomputeShown() {
    const uint32_t zoneId = shownZoneId_.load(std::memory_order_relaxed);
    const std::string shown = zoneId == 0 ? std::string() : entryLine(lookup(zoneId));
    std::lock_guard<std::mutex> lock(mutex_);
    entry_ = shown;
}

std::string ZoneDisplayNames::refresh(uint32_t zoneId) {
    // The one refresh point (game thread), at the moment a target zone id
    // is known (the character's enter-world zone, an approved zone change):
    // make this zone the shown one, recompute the line (era table now; an
    // OP_NewZone override -- when it arrives -- recomputes through
    // noteNewZone), and log the line the loading screen will draw. The INFO
    // line is the scenario arm's assertion: it names the string the screen
    // draws, so a lane can grep it out of a plain log without pixels.
    //
    // Logged only when the SHOWN ZONE changes: a world re-login re-sends the
    // character list, and the same id landing twice (the same zone targeted
    // again) is not a new line the screen started showing.
    std::string shown;
    if (shownZoneId_.load(std::memory_order_relaxed) != zoneId) {
        shownZoneId_.store(zoneId, std::memory_order_relaxed);
        recomputeShown();
        shown = entryText();
        if (shown.empty()) {
            // The screen shows no line for this zone; say so in the same voice.
            LOG_INFO(MOD_GRAPHICS_LOAD,
                     "[LOADING] zone {} has no display name -- loading screen shows no zone line",
                     zoneId);
        } else {
            LOG_INFO(MOD_GRAPHICS_LOAD, "[LOADING] {}", shown);
        }
    }
    return entryText();
}

void ZoneDisplayNames::noteNewZone(uint32_t zoneId, const std::string& longName) {
    // The OP_NewZone refresh (game thread): the server's own long name is
    // authoritative for this zone and is the ONLY source that knows the ids
    // the era table omits (tutorialb). Store it (an empty field clears a
    // stale override), and when this IS the zone being entered re-derive the
    // shown line -- logging it only when it CHANGED (the era table already
    // said "North Qeynos" and the server confirms: no second line; the era
    // table had no row for tutorialb and the server now names it: the line
    // appears, in the same voice as the era-table line).
    setOverride(zoneId, longName);
    const uint32_t shownZone = shownZoneId_.load(std::memory_order_relaxed);
    if (zoneId == 0 || shownZone != zoneId) return;
    recomputeShown();
    const std::string shown = entryText();
    // The recompute already happened; the log gate is against the PREVIOUS
    // line, so take it before the recompute in production terms. Simplest
    // correct form: log when this recompute changed what the screen shows.
    static thread_local bool havePrev = false;
    static thread_local std::string prev;
    const bool changed = !havePrev || prev != shown;
    havePrev = true;
    prev = shown;
    if (!changed) return;
    if (shown.empty()) {
        LOG_INFO(MOD_GRAPHICS_LOAD,
                 "[LOADING] zone {} has no display name -- loading screen shows no zone line",
                 zoneId);
    } else {
        LOG_INFO(MOD_GRAPHICS_LOAD, "[LOADING] {}", shown);
    }
}

void ZoneDisplayNames::setEntryForTest(std::string entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entry_ = std::move(entry);
}

void ZoneDisplayNames::clearEntry() {
    shownZoneId_.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    entry_.clear();
}

}  // namespace eqt
