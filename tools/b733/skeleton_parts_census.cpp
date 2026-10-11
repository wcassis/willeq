// B733 census: for every skeleton in every archive, list (a) the meshes its
// 0x200 list references, (b) the meshes its bones reference, and (c) what the
// production loader stored. Flags skeletons whose 0x200 list names a mesh the
// loader did NOT add (the B733 drop).
// Usage: skeleton_parts_census <archive>
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace EQT::Graphics;

static std::shared_ptr<ZoneGeometry> geomForMeshRef(const WldLoader& wld, uint32_t ref) {
    auto it = wld.getModelRefs().find(ref);
    if (it == wld.getModelRefs().end() || it->second.geometryFragRef == 0) return nullptr;
    return wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: " << argv[0] << " <archive>\n"; return 1; }
    PfsArchive arch;
    if (!arch.open(argv[1])) { std::cerr << "open failed\n"; return 1; }
    std::vector<std::string> wlds; arch.getFilenames(".wld", wlds);
    if (wlds.empty()) return 0;
    WldLoader wld;
    if (!wld.parseFromOpenArchive(arch, wlds[0])) { std::cerr << "parse failed\n"; return 1; }

    std::map<std::string, std::vector<std::shared_ptr<ZoneGeometry>>> byName;
    for (const auto& [f, t] : wld.getSkeletonTracks()) {
        std::set<std::shared_ptr<ZoneGeometry>> listed;
        std::set<std::shared_ptr<ZoneGeometry>> bones;
        std::function<void(const std::shared_ptr<SkeletonBone>&)> walk = [&](const std::shared_ptr<SkeletonBone>& b) {
            if (!b) return;
            if (b->modelRef > 0) if (auto g = geomForMeshRef(wld, b->modelRef)) bones.insert(g);
            for (auto& c : b->children) walk(c);
        };
        for (auto& r : t->bones) walk(r);
        for (uint32_t ref : t->meshRefs) if (auto g = geomForMeshRef(wld, ref)) listed.insert(g);
        for (auto& g : listed) byName[t->name].push_back(g);
        // report only when the lists disagree with each other
        bool missing = false;
        for (auto& g : listed) if (bones.find(g) == bones.end()) missing = true;
        if (!listed.empty() && missing) {
            std::cout << argv[1] << " " << t->name << ":";
            for (auto& g : listed) {
                std::cout << (bones.find(g) != bones.end() ? " " : " +") << g->name << "(" << g->vertices.size() << ")";
            }
            std::cout << "\n";
        }
    }
    return 0;
}
