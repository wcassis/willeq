#include "client/graphics/eqe1_texture_index.h"

#include "common/logging.h"

#include <json/json.h>

#include <fstream>

namespace EQT { namespace Graphics {

bool EQE1TextureIndex::load(const std::string& path) {
    archives_.clear();
    entries_.clear();
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
    // uint16_t indices cap the tree at 65535 archives; the real tree is ~1008.
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
    for (const auto& name : entries.getMemberNames()) {
        const Json::Value& owners = entries[name];
        if (!owners.isArray()) continue;
        std::vector<uint16_t> list;
        list.reserve(owners.size());
        for (const auto& o : owners) {
            const auto idx = o.asUInt();
            if (idx < archives_.size()) list.push_back(static_cast<uint16_t>(idx));
        }
        if (!list.empty()) entries_.emplace(name, std::move(list));
    }

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EQE1 index: {} archives, {} names from {}",
             archives_.size(), entries_.size(), path);
    return true;
}

const std::vector<uint16_t>* EQE1TextureIndex::owners(const std::string& name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second;
}

bool EQE1TextureIndex::resolve(const std::string& name,
                               const std::vector<std::string>& candidates,
                               Resolution& out) const {
    out = Resolution{};
    if (!loaded_) return false;
    const std::vector<uint16_t>* owners = this->owners(name);
    if (!owners || owners->empty()) return false;
    out.ownerCount = owners->size();

    for (const std::string& candidate : candidates) {
        for (uint16_t idx : *owners) {
            if (archives_[idx] == candidate) {
                out.archive = candidate;
                out.lateOpen = false;
                return true;
            }
        }
    }

    out.archive = archives_[owners->front()];
    out.lateOpen = true;
    return true;
}

}} // namespace EQT::Graphics
