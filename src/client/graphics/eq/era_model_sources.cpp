// T504: see era_model_sources.h.
#include "client/graphics/eq/era_model_sources.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "client/data_paths.h"
#include "common/logging.h"

namespace fs = std::filesystem;

namespace EQT {
namespace Graphics {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// lower-case file name -> actual file name, for every *.s3d in the client dir
// (the lists are not consistent about case: LAKEOFILLOMEN_CHR appears).
const std::map<std::string, std::string>& archiveIndex(const std::string& clientPath) {
    static std::map<std::string, std::map<std::string, std::string>> cache;
    auto it = cache.find(clientPath);
    if (it != cache.end()) return it->second;
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(clientPath, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = e.path().filename().string();
        if (n.size() > 4 && lower(n.substr(n.size() - 4)) == ".s3d") m[lower(n)] = n;
    }
    return cache.emplace(clientPath, std::move(m)).first->second;
}

// "<name>" -> "<name>.s3d" or "<name>_chr.s3d" (lower-case), or "" when absent.
std::string resolveArchive(const std::string& clientPath, const std::string& rawName) {
    const auto& idx = archiveIndex(clientPath);
    std::string base = lower(trim(rawName));
    if (base.size() > 4 && base.substr(base.size() - 4) == ".s3d") base = base.substr(0, base.size() - 4);
    for (const std::string& cand : {base + ".s3d", base + "_chr.s3d"}) {
        if (idx.count(cand)) return cand;
    }
    return std::string();
}

// "<zone>_chr.s3d" -> "<zone>" when "<zone>.s3d" exists (a zone's character
// archive), else "".
std::string zoneOfChrArchive(const std::string& clientPath, const std::string& archive) {
    if (archive.size() <= 8 || archive.substr(archive.size() - 8) != "_chr.s3d") return std::string();
    const std::string base = archive.substr(0, archive.size() - 8);
    return archiveIndex(clientPath).count(base + ".s3d") ? base : std::string();
}

}  // namespace

std::set<std::string> LoadEraZones() {
    std::set<std::string> zones;
    const std::string path = eqt::ResolveDataPath("data/era_zones.tsv");
    std::ifstream in(path);
    if (!in) {
        LOG_WARN(MOD_GRAPHICS, "[srcres] era zone list not found: {}", path);
        return zones;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("expansion\t", 0) == 0) continue;
        std::istringstream ss(line);
        std::string exp, id, shortName;
        if (std::getline(ss, exp, '\t') && std::getline(ss, id, '\t') && std::getline(ss, shortName, '\t'))
            zones.insert(lower(trim(shortName)));
    }
    return zones;
}

EraModelSources LoadEraModelSources(const std::string& clientPath, const std::string& zone,
                                    const std::set<std::string>& eraZones) {
    EraModelSources out;
    // 1. Resources/GlobalLoad.txt: lines "a,b,flags,archive,message"
    {
        std::ifstream in(clientPath + "Resources/GlobalLoad.txt");
        std::string line;
        while (in && std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::vector<std::string> f;
            std::istringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, ',')) f.push_back(trim(tok));
            if (f.size() < 4) continue;
            const std::string name = lower(f[3]);
            if (name.size() < 4 || name.find("_chr") == std::string::npos) continue;  // equipment/objects: not character sources
            const std::string archive = resolveArchive(clientPath, name);
            if (archive.empty()) { out.droppedGlobals.push_back(name + "(absent)"); continue; }
            bool keep = archive == "global_chr.s3d" || archive == "global2_chr.s3d"
                     || archive == "global3_chr.s3d" || archive == "global4_chr.s3d";
            if (!keep) {
                const std::string z = zoneOfChrArchive(clientPath, archive);
                keep = !z.empty() && eraZones.count(z) > 0;   // growthplane_chr: a Velious zone's archive
            }
            (keep ? out.globalArchives : out.droppedGlobals).push_back(archive);
        }
    }
    // 2. the zone's own character archive
    out.zoneArchive = resolveArchive(clientPath, zone + "_chr");
    // 3. <zone>_chr.txt
    {
        std::ifstream in(clientPath + zone + "_chr.txt");
        if (in) {
            out.zoneListPresent = true;
            std::string line;
            bool first = true;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty()) continue;
                if (first) { first = false; if (line.find(',') == std::string::npos) continue; }
                const size_t comma = line.find(',');
                if (comma == std::string::npos) continue;
                EraModelSources::ListEntry e;
                e.code = upper(trim(line.substr(0, comma)));
                const std::string raw = trim(line.substr(comma + 1));
                const std::string archive = resolveArchive(clientPath, raw);
                if (archive.empty()) { e.archive = lower(raw); }
                else {
                    e.archive = archive;
                    e.resolved = true;
                    const std::string z = zoneOfChrArchive(clientPath, archive);
                    e.pruned = !z.empty() && eraZones.count(z) == 0;
                }
                out.zoneList.push_back(e);
            }
        }
    }
    return out;
}

std::vector<std::string> EraModelSources::orderedArchives() const {
    std::vector<std::string> order;
    auto add = [&](const std::string& a) {
        if (!a.empty() && std::find(order.begin(), order.end(), a) == order.end()) order.push_back(a);
    };
    for (const auto& g : globalArchives) add(g);
    add(zoneArchive);
    for (const auto& e : zoneList) if (e.resolved && !e.pruned) add(e.archive);
    return order;
}

std::string EraModelSources::toLogLine(const std::string& zone) const {
    std::string s = "[srcres] zone=" + zone + " globals=";
    for (size_t i = 0; i < globalArchives.size(); ++i) s += (i ? "," : "") + globalArchives[i];
    s += " dropped_globals=";
    for (size_t i = 0; i < droppedGlobals.size(); ++i) s += (i ? "," : "") + droppedGlobals[i];
    s += " zone_archive=" + (zoneArchive.empty() ? std::string("-") : zoneArchive);
    s += " list=" + std::string(zoneListPresent ? "" : "(none)");
    for (size_t i = 0; i < zoneList.size(); ++i) {
        const auto& e = zoneList[i];
        s += (i ? "," : "") + e.code + ":" + e.archive + (e.pruned ? "(pruned)" : e.resolved ? "" : "(unresolved)");
    }
    return s;
}

}  // namespace Graphics
}  // namespace EQT
