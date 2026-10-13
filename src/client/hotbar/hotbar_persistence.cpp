#include "client/hotbar/hotbar_persistence.h"

#include "client/data_paths.h"
#include "client/hotbar/hotbar_model.h"
#include "client/spell/spell_constants.h"
#include "client/ui_identity_cache_name.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

namespace eqt {
namespace {

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

bool readFile(const std::string& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) return false;
    text = buffer.str();
    return true;
}

bool parseAddress(const std::string& key, HotbarAddress& address) {
    if (key.compare(0, 4, "Page") != 0) return false;
    const auto button = key.find("Button", 4);
    if (button == std::string::npos) return false;
    const auto page = key.substr(4, button - 4);
    const auto slot = key.substr(button + 6);
    if (page.empty() || slot.empty()) return false;
    for (char c : page) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    for (char c : slot) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    try {
        const auto p = std::stoul(page);
        const auto s = std::stoul(slot);
        if (p == 0 || p > HotbarModel::PAGE_COUNT || s == 0 || s > HotbarModel::SLOT_COUNT)
            return false;
        address = {static_cast<int>(p - 1), static_cast<int>(s - 1)};
        return true;
    } catch (...) {
        return false;
    }
}

bool decode(const std::string& value, const HotbarModel& model,
            HotbarSlotType& type, uint32_t& id) {
    if (value.size() < 2) return false;
    for (size_t i = 1; i < value.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    unsigned long number;
    try { number = std::stoul(value.substr(1)); }
    catch (...) { return false; }
    if (number > std::numeric_limits<uint32_t>::max()) return false;
    id = static_cast<uint32_t>(number);
    switch (value[0]) {
        case 'E': type = HotbarSlotType::Emote; return true;
        case 'J': type = id == 10 ? HotbarSlotType::Combat : HotbarSlotType::Skill; return true;
        case 'H':
            if (id >= EQT::VISIBLE_SPELL_GEM_COUNT) return false;
            type = HotbarSlotType::Spell;
            id = model.spellAtGem(static_cast<uint8_t>(id));
            return true;
        case 'F': type = HotbarSlotType::Item; return true;
        default: return false;
    }
}

bool parseButtons(const std::string& text, const HotbarModel& model,
                  Json::Value& pages) {
    pages = Json::Value(Json::arrayValue);
    for (int p = 0; p < HotbarModel::PAGE_COUNT; ++p) {
        Json::Value buttons(Json::arrayValue);
        for (int s = 0; s < HotbarModel::SLOT_COUNT; ++s)
            buttons.append(Json::Value(Json::objectValue));
        pages.append(buttons);
    }
    std::istringstream input(text);
    std::string line;
    bool inButtons = false;
    while (std::getline(input, line)) {
        const auto entry = trim(line);
        if (entry.empty() || entry[0] == ';' || entry[0] == '#') continue;
        if (entry.front() == '[' && entry.back() == ']') {
            inButtons = entry == "[HotButtons]";
            continue;
        }
        if (!inButtons) continue;
        const auto equals = entry.find('=');
        if (equals == std::string::npos) return false;
        HotbarAddress address;
        HotbarSlotType type;
        uint32_t id;
        const auto key = trim(entry.substr(0, equals));
        const auto value = trim(entry.substr(equals + 1));
        if (!parseAddress(key, address) || !decode(value, model, type, id)) return false;
        auto& slot = pages[address.page][address.slot];
        slot["type"] = static_cast<int>(type);
        slot["id"] = id;
        slot["sourceCode"] = value;
    }
    return true;
}

std::string encoded(const HotbarSlot& slot, const HotbarModel& model) {
    if (!slot.sourceCode.empty()) return slot.sourceCode;
    switch (slot.type) {
        case HotbarSlotType::Emote: return "E" + std::to_string(slot.id);
        case HotbarSlotType::Skill:
        case HotbarSlotType::Combat: return "J" + std::to_string(slot.id);
        case HotbarSlotType::Item: return "F" + std::to_string(slot.id);
        case HotbarSlotType::Spell: {
            const int gem = model.findSpellGem(slot.id);
            return gem >= 0 ? "H" + std::to_string(gem) : "";
        }
        default: return {};
    }
}

std::string withoutButtons(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    std::string kept;
    bool inButtons = false;
    while (std::getline(input, line)) {
        const auto entry = trim(line);
        if (entry.size() >= 2 && entry.front() == '[' && entry.back() == ']')
            inButtons = entry == "[HotButtons]";
        if (!inButtons) kept += line + "\n";
    }
    return kept;
}

} // namespace

std::string HotbarCacheName(const std::string& character,
                            const std::string& server) {
    // The stock client's assignment file is separate from UI_*.ini.
    return "ui/" + UiIdentityFileStem(character, server).substr(3) + ".ini";
}

HotbarLoadResult LoadCharacterHotbar(const std::string& character,
                                    const std::string& server, HotbarModel& model) {
    const auto path = ResolveCachePathForRead(HotbarCacheName(character, server));
    if (path.empty()) {
        // Migrate the short-lived H1 JSON twin once. A malformed twin still
        // means an existing character file, never permission to import the
        // unrelated global clients[0].hotbar binding.
        const auto oldName = "ui/" + UiIdentityFileStem(character, server) + ".hotbar.json";
        const auto oldPath = ResolveCachePathForRead(oldName);
        if (oldPath.empty()) return HotbarLoadResult::Missing;
        std::string oldText;
        if (!readFile(oldPath, oldText)) return HotbarLoadResult::Error;
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream input(oldText);
        if (!Json::parseFromStream(builder, input, &root, &errors) || !root.isObject() ||
            (!root["pages"].isArray() && !root["buttons"].isArray()))
            return HotbarLoadResult::Error;
        model.loadFromJson(root);
        if (model.currentPage() != 0) return HotbarLoadResult::Error;
        for (int p = 0; p < HotbarModel::PAGE_COUNT; ++p)
            for (int s = 0; s < HotbarModel::SLOT_COUNT; ++s) {
                const auto& slot = model.getSlot({p, s});
                if (!slot.emoteText.empty() || slot.iconId != 0)
                    return HotbarLoadResult::Error;
            }
        if (!SaveCharacterHotbar(character, server, model)) return HotbarLoadResult::Error;
        std::remove(oldPath.c_str());
        return HotbarLoadResult::Loaded;
    }
    std::string text;
    if (!readFile(path, text)) return HotbarLoadResult::Error;
    Json::Value pages;
    if (!parseButtons(text, model, pages)) return HotbarLoadResult::Error;
    Json::Value root(Json::objectValue);
    root["pages"] = pages;
    model.loadFromJson(root);
    return HotbarLoadResult::Loaded;
}

bool SaveCharacterHotbar(const std::string& character, const std::string& server,
                         const HotbarModel& model) {
    const auto name = HotbarCacheName(character, server);
    const auto existing = ResolveCachePathForRead(name);
    std::string previous;
    if (!existing.empty()) {
        if (!readFile(existing, previous)) return false;
        Json::Value unused;
        if (!parseButtons(previous, model, unused)) return false;
    }
    const auto path = ResolveCachePathForWrite(name);
    if (path.empty()) return false;
    std::string output = withoutButtons(previous);
    output += "[HotButtons]\n";
    for (int p = 0; p < HotbarModel::PAGE_COUNT; ++p) {
        for (int s = 0; s < HotbarModel::SLOT_COUNT; ++s) {
            const auto& slot = model.getSlot({p, s});
            const auto value = encoded(slot, model);
            if (slot.type != HotbarSlotType::Empty &&
                (value.empty() || !slot.emoteText.empty())) return false;
            if (value.empty()) continue;
            output += "Page" + std::to_string(p + 1) + "Button" +
                      std::to_string(s + 1) + "=" + value + "\n";
        }
    }
    const auto tmp = path + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << output;
        if (!file) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace eqt
