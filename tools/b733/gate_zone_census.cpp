// B733 gate-zone census: for one archive, per character the production loader
// stores, report the skeleton's 0x200 list vs bones vs what a default-name
// selection ({CODE}_DMSPRITEDEF / {CODE}HE00_DMSPRITEDEF) keeps, and whether
// "keep all listed" differs from that selection.
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>
#include <string>

using namespace EQT::Graphics;

static std::vector<std::string> boneMeshNames(const WldLoader& wld, const SkeletonTrack& t) {
    std::vector<std::string> out;
    std::function<void(const std::shared_ptr<SkeletonBone>&)> walk = [&](const std::shared_ptr<SkeletonBone>& b) {
        if (!b) return;
        if (b->modelRef > 0) {
            auto it = wld.getModelRefs().find(b->modelRef);
            if (it != wld.getModelRefs().end() && it->second.geometryFragRef)
                if (auto g = wld.getGeometryByFragmentIndex(it->second.geometryFragRef)) out.push_back(g->name);
        }
        for (auto& c : b->children) walk(c);
    };
    for (auto& r : t.bones) walk(r);
    return out;
}
static std::vector<std::string> listedMeshNames(const WldLoader& wld, const SkeletonTrack& t) {
    std::vector<std::string> out;
    for (uint32_t ref : t.meshRefs) {
        auto it = wld.getModelRefs().find(ref);
        if (it != wld.getModelRefs().end() && it->second.geometryFragRef)
            if (auto g = wld.getGeometryByFragmentIndex(it->second.geometryFragRef)) out.push_back(g->name);
    }
    return out;
}
static std::string upper(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return std::toupper(c);}); return s; }

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: " << argv[0] << " <archive>\n"; return 1; }
    PfsArchive arch;
    if (!arch.open(argv[1])) { std::cerr << "open failed\n"; return 1; }
    std::vector<std::string> wlds; arch.getFilenames(".wld", wlds);
    if (wlds.empty()) return 0;
    WldLoader wld;
    if (!wld.parseFromOpenArchive(arch, wlds[0])) return 1;

    S3DLoader loader;
    if (!loader.loadZone(argv[1])) { std::cerr << "loadZone failed\n"; return 1; }

    for (const auto& ch : loader.getCharacters()) {
        if (!ch || !ch->skeleton) continue;
        const auto& t = *ch->skeleton;
        auto listed = listedMeshNames(wld, t);
        auto bones = boneMeshNames(wld, t);
        if (listed.empty() && bones.empty()) continue;
        std::set<std::string> listedSet(listed.begin(), listed.end());
        std::set<std::string> boneSet(bones.begin(), bones.end());
        // missing-from-bones entries in the 0x200 list
        std::vector<std::string> missing;
        for (auto& n : listed) if (boneSet.count(n) == 0) missing.push_back(n);
        // default-name selection over the loaded parts
        std::string code = upper(t.name);
        size_t p = code.find("_HS_DEF");
        if (p != std::string::npos) code = code.substr(0, p);
        else code = code.size() >= 3 ? code.substr(0, 3) : code;
        std::string body = code + "_DMSPRITEDEF", head = code + "HE00_DMSPRITEDEF";
        std::set<std::string> kept;
        bool anyMatch = false;
        for (const auto& pt : ch->partsWithTransforms)
            if (pt.geometry) {
                std::string n = upper(pt.geometry->name);
                if (n == body || n == head) { kept.insert(n); anyMatch = true; }
            }
        if (!anyMatch) for (const auto& pt : ch->partsWithTransforms) if (pt.geometry) kept.insert(upper(pt.geometry->name));
        std::set<std::string> listedUpper;
        for (auto& n : listed) listedUpper.insert(upper(n));
        bool differs = (kept != listedUpper);
        if (missing.empty() && !differs) continue;
        std::cout << argv[1] << " " << ch->name;
        std::cout << " listed=["; for (size_t i=0;i<listed.size();++i) std::cout << (i?",":"") << listed[i];
        std::cout << "] missing_from_bones=["; for (size_t i=0;i<missing.size();++i) std::cout << (i?",":"") << missing[i];
        std::cout << "] kept=" << kept.size() << (differs ? " DIFFERS" : "") << "\n";
    }
    return 0;
}
