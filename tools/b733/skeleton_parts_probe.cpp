// B733 probe: which meshes does a skeleton's 0x10 fragment actually reference,
// and what does the production character loader store for that character?
// Usage: skeleton_parts_probe <archive.s3d> <skeleton-name-prefix>
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <functional>

using namespace EQT::Graphics;

static std::shared_ptr<ZoneGeometry> geomForMeshRef(const WldLoader& wld, uint32_t ref) {
    auto it = wld.getModelRefs().find(ref);
    if (it == wld.getModelRefs().end() || it->second.geometryFragRef == 0) return nullptr;
    return wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: " << argv[0] << " <archive> <prefix>\n"; return 1; }
    const std::string archive = argv[1], prefix = argv[2];

    // 1. WLD level: the skeleton's own reference lists
    PfsArchive arch;
    if (!arch.open(archive)) { std::cerr << "open failed\n"; return 1; }
    std::vector<std::string> wlds; arch.getFilenames(".wld", wlds);
    size_t wldIdx = 0;
    if (wlds.size() > 1) {
        for (size_t i = 0; i < wlds.size(); ++i)
            if (wlds[i].find("chr") != std::string::npos) { wldIdx = i; break; }
    }
    WldLoader wld;
    if (!wld.parseFromOpenArchive(arch, wlds[wldIdx])) { std::cerr << "parse failed\n"; return 1; }

    for (const auto& [fragIdx, track] : wld.getSkeletonTracks()) {
        std::string upper = track->name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c){return std::toupper(c);});
        if (upper.find(prefix) == std::string::npos) continue;
        std::cout << "skeleton: " << track->name << " (frag " << fragIdx << ")\n";
        // bone-attached meshes
        std::cout << "  bone-attached meshes:" << std::endl;
        std::function<void(const std::shared_ptr<SkeletonBone>&)> walk = [&](const std::shared_ptr<SkeletonBone>& b) {
            if (!b) return;
            if (b->modelRef > 0) {
                auto g = geomForMeshRef(wld, b->modelRef);
                std::cout << "    bone '" << b->name << "' modelRef=" << b->modelRef
                          << (g ? (std::string(" -> ") + g->name + " verts=" + std::to_string(g->vertices.size())) : " -> UNRESOLVED")
                          << std::endl;
            }
            for (auto& c : b->children) walk(c);
        };
        for (auto& r : track->bones) walk(r);
        // 0x200 mesh list
        std::cout << "  0x200 meshRefs: " << track->meshRefs.size() << std::endl;
        for (uint32_t ref : track->meshRefs) {
            auto g = geomForMeshRef(wld, ref);
            std::cout << "    ref " << ref << (g ? (std::string(" -> ") + g->name + " verts=" + std::to_string(g->vertices.size()) + " pieces=" + std::to_string(g->vertexPieces.size())) : std::string(" -> UNRESOLVED")) << std::endl;
        }
    }

    // 2. Production loader level
    S3DLoader loader;
    if (!loader.loadZone(archive)) { std::cerr << "loadZone failed: " << loader.getError() << "\n"; return 1; }
    std::cout << "\nproduction loader characters:\n";
    for (const auto& ch : loader.getCharacters()) {
        if (!ch || ch->name.find(prefix) == std::string::npos) continue;
        std::cout << "  character '" << ch->name << "': parts=" << ch->parts.size()
                  << " partsWithTransforms=" << ch->partsWithTransforms.size()
                  << " rawParts=" << ch->rawParts.size() << std::endl;
        for (const auto& p : ch->partsWithTransforms)
            if (p.geometry) std::cout << "    part '" << p.geometry->name << "' verts=" << p.geometry->vertices.size() << std::endl;
        for (const auto& p : ch->rawParts)
            if (p.geometry) std::cout << "    raw  '" << p.geometry->name << "' verts=" << p.geometry->vertices.size() << std::endl;
    }
    return 0;
}
