// B909: the zone's collision triangles per BSP region, held once
// (include/client/graphics/region_world_triangles.h).
#include "client/graphics/region_world_triangles.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

void RegionWorldTriangles::setSource(const RegionTriangleSource* source) {
    source_ = source;
    sourceRegions_ = source ? source->regionsWithTriangles() : std::vector<size_t>{};
    sourceRegions_.shrink_to_fit();
    // The source covers these regions: an owned copy of one (the player's
    // region, built as a node before the pools are consulted) is dropped, as
    // the pre-B909 copy overwrote it with the pools' triangles.
    for (size_t r : sourceRegions_) owned_.erase(r);
}

void RegionWorldTriangles::setOwned(size_t regionIdx, std::vector<irr::core::triangle3df> triangles) {
    owned_[regionIdx] = std::move(triangles);
}

RegionTrianglesView RegionWorldTriangles::find(size_t regionIdx) const {
    auto it = owned_.find(regionIdx);
    if (it != owned_.end()) return RegionTrianglesView(&it->second);
    if (source_ && std::binary_search(sourceRegions_.begin(), sourceRegions_.end(), regionIdx)) {
        return RegionTrianglesView(source_, regionIdx);
    }
    return RegionTrianglesView();
}

size_t RegionWorldTriangles::size() const {
    size_t n = owned_.size();
    for (size_t r : sourceRegions_) {
        if (owned_.find(r) == owned_.end()) ++n;
    }
    return n;
}

void RegionWorldTriangles::clear() {
    owned_.clear();
    source_ = nullptr;
    sourceRegions_.clear();
    sourceRegions_.shrink_to_fit();
}

size_t RegionWorldTriangles::ownedTriangleBytes() const {
    size_t bytes = 0;
    for (const auto& [idx, tris] : owned_) bytes += tris.capacity() * sizeof(irr::core::triangle3df);
    return bytes;
}

}  // namespace Graphics
}  // namespace EQT
