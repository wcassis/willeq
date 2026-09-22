// T504b: character-model source RESIDENCY -- the working set in both
// directions. Split out of race_model_loader.cpp (file-size ceiling).
//   snapshots of the three stores (sourceStoresMutex_), pruning to the used
//   set, reload-on-miss, the once-per-(race,gender,site) memo, the
//   render-thread readiness check, the original client's resolver order
//   (GlobalLoad.txt + <zone>_chr + <zone>_chr.txt) and its census.
#include "client/graphics/eq/race_model_loader.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <pthread.h>

#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/era_model_sources.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

RaceModelLoader::CharacterList RaceModelLoader::snapshotZoneCharacters() const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    return zoneCharacters_;
}
RaceModelLoader::CharacterList RaceModelLoader::snapshotGlobalCharacters() const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    return globalCharacters_;
}
RaceModelLoader::CharacterList RaceModelLoader::snapshotNumberedGlobalCharacters(int num) const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    auto it = numberedGlobalCharacters_.find(num);
    return it == numberedGlobalCharacters_.end() ? CharacterList{} : it->second;
}

// ---------------------------------------------------------------------------
// T504b Leg 2: source pruning + reload-on-miss
// ---------------------------------------------------------------------------
bool RaceModelLoader::pruningEnabled() {
    // ON by default (Will, 2026-09-22) now that the render thread never
    // parses (the STOP 3 finding: with pruning the sync fallback parsed
    // globalhum/global2-7 on eq-render; it now defers to the worker).
    // WILLEQ_PRUNE_SOURCES=0 disables pruning for A/B measurement.
    static const bool on = [] {
        const char* v = std::getenv("WILLEQ_PRUNE_SOURCES");
        return !(v && v[0] == '0');
    }();
    return on;
}


void RaceModelLoader::forgetReloadMemo(uint32_t key) {
    std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
    const std::string prefix = std::to_string(key) + "@";
    for (auto it = reloadedRaceGenders_.begin(); it != reloadedRaceGenders_.end();) {
        if (it->rfind(prefix, 0) == 0) it = reloadedRaceGenders_.erase(it); else ++it;
    }
}

void RaceModelLoader::forgetAllReloadMemos() {
    std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
    reloadedRaceGenders_.clear();
}

bool RaceModelLoader::isModelReady(uint16_t raceId, uint8_t gender) {
    promotePreparedModels();
    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
    return loadedModels_.count(makeCacheKey(raceId, gender)) > 0;
}

namespace {
std::string upperOf(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return std::toupper(c); });
    return u;
}
std::string threadNameNow() {
    char buf[32] = "?";
    pthread_getname_np(pthread_self(), buf, sizeof(buf));
    return buf;
}
}  // namespace

void RaceModelLoader::recordArchiveNamesLocked(const std::string& key, const std::string& path,
                                               const CharacterList& chars) {
    ArchiveIndexEntry& e = archiveIndex_[key];
    if (!e.names.empty()) return;   // recorded at the first (full) parse
    e.path = path;
    for (const auto& cm : chars) if (cm) e.names.push_back(upperOf(cm->name));
}

void RaceModelLoader::recordArchiveNamesLocked(const std::string& key, const std::string& path,
                                               const std::vector<std::string>& names) {
    ArchiveIndexEntry& e = archiveIndex_[key];
    if (!e.names.empty()) return;
    e.path = path;
    for (const auto& n : names) e.names.push_back(upperOf(n));
}

std::vector<std::string> RaceModelLoader::wantedCodesFor(uint16_t raceId, uint8_t gender) const {
    std::vector<std::string> wanted;
    const std::string base = getRaceCode(raceId);
    if (base.empty()) return wanted;
    const std::string raceCode = upperOf(getGenderedRaceCode(base, gender));
    wanted.push_back(raceCode);
    const std::string fallback = upperOf(getFallbackRaceCode(raceId, gender));
    if (!fallback.empty() && fallback != raceCode) wanted.push_back(fallback);
    for (const std::string& c : std::vector<std::string>{raceCode, fallback}) {
        if (c.empty()) continue;
        const std::string donor = upperOf(getAnimationSourceCode(c));
        if (!donor.empty() && donor != c) wanted.push_back(donor);
    }
    return wanted;
}

const std::vector<std::string>& RaceModelLoader::resolverOrder() {
    if (resolverOrderZone_ == currentZoneName_) return resolverOrder_;
    if (!eraZonesLoaded_) { eraZones_ = LoadEraZones(); eraZonesLoaded_ = true; }
    if (eraSources_.globalArchives.empty() && eraSources_.zoneArchive.empty() && !clientPath_.empty())
        eraSources_ = LoadEraModelSources(clientPath_, currentZoneName_, eraZones_);
    resolverOrder_.clear();
    auto add = [&](const std::string& a) {
        if (!a.empty() && std::find(resolverOrder_.begin(), resolverOrder_.end(), a) == resolverOrder_.end())
            resolverOrder_.push_back(a);
    };
    add(eraSources_.zoneArchive);
    for (const auto& e : eraSources_.zoneList) if (e.resolved && !e.pruned) add(e.archive);
    for (const auto& g : eraSources_.globalArchives) add(g);
    resolverOrderZone_ = currentZoneName_;
    return resolverOrder_;
}

std::string RaceModelLoader::originalRuleArchiveFor(uint16_t raceId, uint8_t gender) {
    static const bool census = [] { const char* v = std::getenv("WILLEQ_SRCRES_CENSUS"); return v && v[0] == '1'; }();
    if (!census) return "-";
    if (!eraNameIndexBuilt_) {
        // Names only: the filter matches nothing, so no CharacterModel is
        // built; S3DZone::allCharacterNames still lists every skeleton.
        for (const auto& archive : resolverOrder()) {
            S3DLoader loader;
            S3DLoadOptions opts;
            opts.characterCodeFilter = {"\x01"};
            opts.loadObjects = false;
            std::vector<std::string> names;
            if (loader.loadZone(clientPath_ + archive, opts) && loader.getZone())
                for (const auto& n : loader.getZone()->allCharacterNames) names.push_back(upperOf(n));
            eraNameIndex_.emplace_back(archive, std::move(names));
        }
        eraNameIndexBuilt_ = true;
        // Conflicts: a code held by more than one ordered archive is where
        // lookup PRECEDENCE decides the model (zone before list before
        // globals here). Logged once per zone so the choice is visible.
        std::map<std::string, std::vector<std::string>> holders;
        for (const auto& [archive, names] : eraNameIndex_)
            for (const auto& n : names) {
                const size_t us = n.find('_');
                if (us != std::string::npos && us >= 3) holders[n.substr(0, us)].push_back(archive);
            }
        std::string conflicts;
        int shown = 0;
        for (const auto& [code, archives] : holders) {
            std::vector<std::string> uniq(archives);
            std::sort(uniq.begin(), uniq.end());
            uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
            if (uniq.size() < 2) continue;
            if (shown++ < 40) {
                conflicts += (conflicts.empty() ? "" : " ") + code + "=";
                for (size_t i = 0; i < uniq.size(); ++i) conflicts += (i ? "|" : "") + uniq[i];
            }
        }
        LOG_INFO(MOD_GRAPHICS, "[srcres] zone={} conflicts={} {}", currentZoneName_, shown, conflicts);
        // Every code the ordered archives hold, with its holders: which
        // archive an animation DONOR (GIA for GOL, ...) would come from, or
        // that it is absent from this zone's sources.
        std::string all;
        for (const auto& [code, archives] : holders) {
            std::vector<std::string> uniq(archives);
            std::sort(uniq.begin(), uniq.end());
            uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
            all += (all.empty() ? "" : " ") + code + "=";
            for (size_t i = 0; i < uniq.size(); ++i) all += (i ? "|" : "") + uniq[i];
        }
        LOG_INFO(MOD_GRAPHICS, "[srcres] zone={} holders={} {}", currentZoneName_, holders.size(), all);
    }
    // The original client keys the zone list by CODE; globals and the zone
    // archive by whatever they hold. First archive holding the race code or
    // its fallback wins.
    const std::vector<std::string> wanted = wantedCodesFor(raceId, gender);
    for (size_t k = 0; k < wanted.size() && k < 2; ++k) {
        for (const auto& [archive, names] : eraNameIndex_)
            for (const auto& n : names)
                if (n.find(wanted[k]) != std::string::npos) return archive;
    }
    return "none";
}

void RaceModelLoader::computeUsedSet(std::set<const CharacterSkeleton*>& usedSkeletons,
                                     std::set<const Animation*>& usedAnimations) const {
    auto take = [&](const std::shared_ptr<RaceModelData>& m) {
        if (!m || !m->skeleton) return;
        usedSkeletons.insert(m->skeleton.get());
        for (const auto& [code, anim] : m->skeleton->animations)
            if (anim) usedAnimations.insert(anim.get());
    };
    { std::lock_guard<std::mutex> lock(loadedModelsMutex_); for (const auto& [k, m] : loadedModels_) take(m); }
    { std::lock_guard<std::mutex> lock(variantModelsMutex_); for (const auto& [k, m] : variantModels_) take(m); }
    { std::lock_guard<std::mutex> lock(preparedDataMutex_); for (const auto& [k, m] : preparedModelData_) take(m); }
}

void RaceModelLoader::pruneSourceCharacters(const char* why) {
    if (!pruningEnabled()) return;
    std::set<const CharacterSkeleton*> usedSkeletons;
    std::set<const Animation*> usedAnimations;
    computeUsedSet(usedSkeletons, usedAnimations);
    auto keep = [&](const std::shared_ptr<CharacterModel>& cm) {
        if (!cm) return false;
        if (!cm->animatedSkeleton) return true;   // static (no skeleton): tiny, keep
        if (usedSkeletons.count(cm->animatedSkeleton.get())) return true;
        for (const auto& [code, anim] : cm->animatedSkeleton->animations)
            if (anim && usedAnimations.count(anim.get())) return true;   // donor
        return false;
    };
    size_t dropped = 0, kept = 0;
    auto filter = [&](CharacterList& list) {
        CharacterList out;
        out.reserve(list.size());
        for (auto& cm : list) { if (keep(cm)) out.push_back(cm); else ++dropped; }
        kept += out.size();
        list.swap(out);
    };
    {
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        if (!globalCharacters_.empty()) {
            recordArchiveNamesLocked("global", clientPath_ + "global_chr.s3d", globalCharacters_);
            filter(globalCharacters_);
            archiveIndex_["global"].pruned = true;
        }
        for (auto& [num, chars] : numberedGlobalCharacters_) {
            const std::string key = "global" + std::to_string(num);
            recordArchiveNamesLocked(key, clientPath_ + key + "_chr.s3d", chars);
            filter(chars);
            archiveIndex_[key].pruned = true;
        }
        if (!zoneCharacters_.empty()) {
            recordArchiveNamesLocked("zone", clientPath_ + currentZoneName_ + "_chr.s3d", zoneCharacters_);
            filter(zoneCharacters_);
            archiveIndex_["zone"].pruned = true;
        }
        for (auto& [name, cache] : otherChrCaches_) {
            if (cache.characters.empty()) continue;
            const std::string key = "other:" + name;
            recordArchiveNamesLocked(key, clientPath_ + name, cache.characters);
            if (name == "global_chr.s3d" && !globalCharacters_.empty()
                && !cache.characters.empty() && cache.characters.front() == globalCharacters_.front()) {
                cache.characters = globalCharacters_;   // the shared entry: same objects, same set
            } else {
                filter(cache.characters);
            }
            archiveIndex_[key].pruned = true;
        }
    }
    if (dropped > 0) {
        LOG_INFO(MOD_GRAPHICS, "[srcchar] prune why={} dropped={} kept={} used_skeletons={} thread={}",
                 why, dropped, kept, usedSkeletons.size(), threadNameNow());
    }
}

void RaceModelLoader::ensureSourceForRace(uint16_t raceId, uint8_t gender, const char* site) {
    if (!pruningEnabled()) return;
    // Every new race request first drops what the previous builds left
    // unused (so the render-thread sync path prunes too), then makes sure
    // this race's sources are present.
    pruneSourceCharacters("request");
    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        const std::string memoKey = std::to_string(makeCacheKey(raceId, gender)) + "@" + site;
        if (!reloadedRaceGenders_.insert(memoKey).second) return;   // once per race/gender/site
    }
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        if (knownMissingRaceGenders_.count(makeCacheKey(raceId, gender))) return;   // no archive has it
    }
    // The codes the search chain and the animation merge will look for.
    const std::vector<std::string> wanted = wantedCodesFor(raceId, gender);
    if (wanted.empty()) return;

    // Which pruned archives hold a wanted name the store no longer has?
    std::vector<std::pair<std::string, std::string>> toReload;   // key, path
    {
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        auto storeHas = [&](const std::string& key, const std::string& code) {
            const CharacterList* list = nullptr;
            if (key == "zone") list = &zoneCharacters_;
            else if (key == "global") list = &globalCharacters_;
            else if (key.rfind("global", 0) == 0) {
                auto it = numberedGlobalCharacters_.find(std::atoi(key.c_str() + 6));
                if (it != numberedGlobalCharacters_.end()) list = &it->second;
            } else if (key.rfind("other:", 0) == 0) {
                auto it = otherChrCaches_.find(key.substr(6));
                if (it != otherChrCaches_.end()) list = &it->second.characters;
            }
            if (!list) return false;
            for (const auto& cm : *list) if (cm && upperOf(cm->name).find(code) != std::string::npos) return true;
            return false;
        };
        for (auto& [key, e] : archiveIndex_) {
            if (!e.pruned) continue;
            bool need = false;
            for (const auto& code : wanted) {
                bool archiveHas = false;
                for (const auto& n : e.names) if (n.find(code) != std::string::npos) { archiveHas = true; break; }
                if (archiveHas && !storeHas(key, code)) { need = true; break; }
            }
            if (need) toReload.emplace_back(key, e.path);
        }
    }
    for (const auto& [key, path] : toReload) {
        // T504c (STOP 1, 2026-09-22): a FILTERED parse changes which archive a
        // race resolves from (race 120 WOE: global6_chr vs global_chr), so the
        // reload parses whole and keeps the wanted characters afterwards.
        S3DLoader loader;
        if (!loader.loadZone(path)) continue;
        auto zone = loader.getZone();
        if (!zone) continue;
        CharacterList add;
        for (const auto& cm : zone->characters) {
            if (!cm) continue;
            const std::string n = upperOf(cm->name);
            for (const auto& code : wanted) if (n.find(code) != std::string::npos) { add.push_back(cm); break; }
        }
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        auto appendMissing = [&](CharacterList& list) {
            for (const auto& cm : add) {
                bool present = false;
                for (const auto& have : list) if (have && have->name == cm->name) { present = true; break; }
                if (!present) list.push_back(cm);
            }
        };
        if (key == "zone") appendMissing(zoneCharacters_);
        else if (key == "global") {
            appendMissing(globalCharacters_);
            auto it = otherChrCaches_.find("global_chr.s3d");
            if (it != otherChrCaches_.end()) it->second.characters = globalCharacters_;
        } else if (key.rfind("global", 0) == 0) {
            appendMissing(numberedGlobalCharacters_[std::atoi(key.c_str() + 6)]);
        } else if (key.rfind("other:", 0) == 0) {
            auto it = otherChrCaches_.find(key.substr(6));
            if (it != otherChrCaches_.end()) {
                if (key == "other:global_chr.s3d") { appendMissing(globalCharacters_); it->second.characters = globalCharacters_; }
                else appendMissing(it->second.characters);
            }
        }
        archiveIndex_[key].reloads++;
        LOG_INFO(MOD_GRAPHICS, "[srcchar] reload key={} race={} gender={} added={} thread={} site={}",
                 key, raceId, gender, add.size(), threadNameNow(), site);
    }
}

bool RaceModelLoader::adoptGlobalChrIntoOtherCacheLocked() {
    if (otherChrCaches_.find("global_chr.s3d") != otherChrCaches_.end()) return true;
    if (!globalModelsLoaded_.load(std::memory_order_acquire)) return false;
    const CharacterList globals = snapshotGlobalCharacters();   // T504b Leg 2: under sourceStoresMutex_
    if (globals.empty()) return false;
    storeOtherChrCacheLocked("global_chr.s3d", globals, globalTextures_);
    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: other-chr cache adopted {} global_chr.s3d characters from the global store (no second parse)",
             globals.size());
    return otherChrCaches_.find("global_chr.s3d") != otherChrCaches_.end();
}

} // namespace Graphics
} // namespace EQT
