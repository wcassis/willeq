// T504 (Will, 2026-09-22): the ORIGINAL client's character-model sources.
//
// The original client loads the archives named in Resources/GlobalLoad.txt
// once at first load, then on every zone entry the zone's own <zone>_chr.s3d
// and the archives its <zone>_chr.txt lists (`count` then `code,archive`
// lines). This module reads those files and prunes them to the target era
// (classic + Kunark + Velious) with data/era_zones.tsv: a GlobalLoad entry
// is kept when it is Global_chr/Global2-4_chr or a <zone>_chr of an era zone
// (growthplane); a zone-list archive is dropped when it is the character
// archive of a zone outside the era (poair, ponightmare, ...). Entries that
// name no archive in the tree (tar, rtn, dke, btn in dozens of lists) are
// recorded as unresolved: their models come from the global set.
//
// Leg 0 (this header's first use) only REPORTS: the resolver that replaces
// the directory scan and the per-race archive guess is the next leg.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct EraModelSources {
    struct ListEntry {
        std::string code;      // upper-case model code from the list
        std::string archive;   // resolved file name (lower-case) or the raw name when unresolved
        bool resolved = false; // an archive with that name exists in the client tree
        bool pruned = false;   // resolved, but its zone is outside the era
    };
    std::vector<std::string> globalArchives;   // kept GlobalLoad entries, file order, lower-case file names
    std::vector<std::string> droppedGlobals;   // GlobalLoad character archives outside the era
    std::string zoneArchive;                   // "<zone>_chr.s3d" when present
    std::vector<ListEntry> zoneList;           // <zone>_chr.txt, file order
    bool zoneListPresent = false;

    // The search order of the original rule: globals, the zone archive, then
    // the list's resolved and unpruned archives -- deduplicated, first wins.
    std::vector<std::string> orderedArchives() const;
    std::string toLogLine(const std::string& zone) const;
};

// data/era_zones.tsv -> short names (empty set = file missing; callers log it).
std::set<std::string> LoadEraZones();

// clientPath ends with '/'. Never throws; missing files leave the fields empty.
EraModelSources LoadEraModelSources(const std::string& clientPath, const std::string& zone,
                                    const std::set<std::string>& eraZones);

}  // namespace Graphics
}  // namespace EQT
