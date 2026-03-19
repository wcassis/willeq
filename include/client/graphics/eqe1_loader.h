#pragma once

#include "client/graphics/eqe1_format.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace EQT { namespace Graphics {

class EQE1Loader {
public:
    EQE1Loader();
    ~EQE1Loader();

    EQE1Loader(const EQE1Loader&) = delete;
    EQE1Loader& operator=(const EQE1Loader&) = delete;
    EQE1Loader(EQE1Loader&&) noexcept;
    EQE1Loader& operator=(EQE1Loader&&) noexcept;

    bool open(const std::string& filepath);
    void close();

    struct TextureEntry {
        const uint8_t* data;
        uint32_t dataSize;
        uint16_t width;
        uint16_t height;
        uint8_t  mipLevels;
        bool     hasAlpha;
        const uint8_t* alphaData;
        uint32_t alphaDataSize;
    };

    const TextureEntry* lookup(const std::string& name) const;
    bool getRawData(const std::string& name, const uint8_t*& data, uint32_t& size) const;

    std::vector<std::string> getTextureNames() const;
    std::vector<std::string> getRawDataNames() const;

    uint16_t getEntryCount() const { return entryCount_; }
    const std::string& getFilePath() const { return filepath_; }
    bool isOpen() const { return isOpen_; }

private:
    std::string filepath_;
    std::vector<uint8_t> fileData_;
    std::unordered_map<std::string, TextureEntry> textureLookup_;
    std::unordered_map<std::string, std::pair<const uint8_t*, uint32_t>> rawDataLookup_;
    uint16_t entryCount_ = 0;
    bool isOpen_ = false;
};

}} // namespace EQT::Graphics
