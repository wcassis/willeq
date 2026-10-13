#include "client/hotbar/hotbar_persistence.h"

#include "client/data_paths.h"
#include "client/hotbar/hotbar_model.h"
#include "client/ui_identity_cache_name.h"

#include <cstdio>
#include <fstream>

namespace eqt {

std::string HotbarCacheName(const std::string& character,
                            const std::string& server) {
    return "ui/" + UiIdentityFileStem(character, server) + ".hotbar.json";
}

bool LoadCharacterHotbar(const std::string& character, const std::string& server,
                         HotbarModel& model) {
    const auto path = ResolveCachePathForRead(HotbarCacheName(character, server));
    if (path.empty()) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors) || !root.isObject() ||
        (!root["pages"].isArray() && !root["buttons"].isArray())) return false;
    model.loadFromJson(root);
    return true;
}

bool SaveCharacterHotbar(const std::string& character, const std::string& server,
                         const HotbarModel& model) {
    const auto path = ResolveCachePathForWrite(HotbarCacheName(character, server));
    if (path.empty()) return false;
    const auto tmp = path + ".tmp";
    {
        std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        output << Json::writeString(builder, model.saveToJson());
        if (!output) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace eqt
