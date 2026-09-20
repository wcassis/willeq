#include "client/graphics/eqe1_texture_index.h"

#include "common/logging.h"

#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <string_view>

namespace EQT { namespace Graphics {

bool EQE1TextureIndex::load(const std::string& path) {
    archives_.clear();
    nameBlob_.clear();
    nameOffsets_.clear();
    ownerOffsets_.clear();
    ownerIds_.clear();
    loaded_ = false;

    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: cannot open {}", path);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: parse error in {}: {}", path, errs);
        return false;
    }

    const Json::Value& archives = root["archives"];
    if (!archives.isArray() || archives.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: {} has no 'archives' array", path);
        return false;
    }
    // uint16_t indices cap the tree at 65535 archives; the real tree is ~1506 (T281).
    if (archives.size() > UINT16_MAX) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: {} lists {} archives, over the {} cap",
                  path, archives.size(), UINT16_MAX);
        return false;
    }
    archives_.reserve(archives.size());
    for (const auto& a : archives) archives_.push_back(a.asString());

    const Json::Value& entries = root["entries"];
    if (!entries.isObject()) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: {} has no 'entries' object", path);
        return false;
    }
    // Sorted names (binary search needs it; jsoncpp's member order is already
    // lexicographic, the sort is what makes that a guarantee, not a habit).
    std::vector<std::string> names = entries.getMemberNames();
    std::sort(names.begin(), names.end());

    // Size everything once: no growth reallocation, no slack left behind.
    size_t blobBytes = 0, ownerTotal = 0, kept = 0;
    for (const auto& name : names) {
        const Json::Value& owners = entries[name];
        if (!owners.isArray()) continue;
        size_t valid = 0;
        for (const auto& o : owners) {
            if (o.asUInt() < archives_.size()) ++valid;
        }
        if (valid == 0) continue;
        blobBytes += name.size();
        ownerTotal += valid;
        ++kept;
    }
    nameBlob_.reserve(blobBytes);
    nameOffsets_.reserve(kept + 1);
    ownerOffsets_.reserve(kept + 1);
    ownerIds_.reserve(ownerTotal);

    for (const auto& name : names) {
        const Json::Value& owners = entries[name];
        if (!owners.isArray()) continue;
        const size_t before = ownerIds_.size();
        for (const auto& o : owners) {
            const auto idx = o.asUInt();
            if (idx < archives_.size()) ownerIds_.push_back(static_cast<uint16_t>(idx));
        }
        if (ownerIds_.size() == before) continue;   // no valid owner: not an entry
        nameOffsets_.push_back(static_cast<uint32_t>(nameBlob_.size()));
        ownerOffsets_.push_back(static_cast<uint32_t>(before));
        nameBlob_ += name;
    }
    nameOffsets_.push_back(static_cast<uint32_t>(nameBlob_.size()));
    ownerOffsets_.push_back(static_cast<uint32_t>(ownerIds_.size()));

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EQE1 index: {} archives, {} names from {} ({} bytes resident)",
             archives_.size(), nameCount(), path, residentBytes());
    return true;
}

size_t EQE1TextureIndex::residentBytes() const {
    size_t bytes = nameBlob_.capacity() + nameOffsets_.capacity() * sizeof(uint32_t) +
                   ownerOffsets_.capacity() * sizeof(uint32_t) + ownerIds_.capacity() * sizeof(uint16_t);
    for (const auto& a : archives_) bytes += sizeof(std::string) + a.capacity();
    return bytes;
}

EQE1TextureIndex::OwnerList EQE1TextureIndex::owners(const std::string& name) const {
    const size_t n = nameCount();
    const std::string_view key(name);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const std::string_view cur(nameBlob_.data() + nameOffsets_[mid],
                                   nameOffsets_[mid + 1] - nameOffsets_[mid]);
        const int c = cur.compare(key);
        if (c == 0) {
            return OwnerList{ownerIds_.data() + ownerOffsets_[mid],
                             static_cast<size_t>(ownerOffsets_[mid + 1] - ownerOffsets_[mid])};
        }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return OwnerList{};
}

bool EQE1TextureIndex::resolve(const std::string& name,
                               const std::vector<std::string>& candidates,
                               Resolution& out) const {
    out = Resolution{};
    if (!loaded_) return false;
    const OwnerList owners = this->owners(name);
    if (owners.empty()) return false;
    out.ownerCount = owners.size();

    for (const std::string& candidate : candidates) {
        for (uint16_t idx : owners) {
            if (archives_[idx] == candidate) {
                out.archive = candidate;
                out.lateOpen = false;
                return true;
            }
        }
    }

    out.archive = archives_[owners.front()];
    out.lateOpen = true;
    return true;
}

}} // namespace EQT::Graphics
