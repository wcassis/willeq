#include "client/graphics/eqe1_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>

// Logging - use LOG macros when integrated into willeq, fprintf for standalone use
#ifdef EQT_HAS_LOGGING
#include "common/logging.h"
#define EQE1_LOG_INFO(fmt, ...)  LOG_INFO(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#define EQE1_LOG_ERROR(fmt, ...) LOG_ERROR(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#define EQE1_LOG_DEBUG(fmt, ...) LOG_DEBUG(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#else
#define EQE1_LOG_INFO(fmt, ...)  fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define EQE1_LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define EQE1_LOG_DEBUG(fmt, ...)
#endif

namespace EQT { namespace Graphics {

EQE1Loader::EQE1Loader() = default;
EQE1Loader::~EQE1Loader() { close(); }

EQE1Loader::EQE1Loader(EQE1Loader&& other) noexcept
    : filepath_(std::move(other.filepath_))
    , fileData_(std::move(other.fileData_))
    , textureLookup_(std::move(other.textureLookup_))
    , rawDataLookup_(std::move(other.rawDataLookup_))
    , entryCount_(other.entryCount_)
    , isOpen_(other.isOpen_)
{
    other.entryCount_ = 0;
    other.isOpen_ = false;
}

EQE1Loader& EQE1Loader::operator=(EQE1Loader&& other) noexcept {
    if (this != &other) {
        close();
        filepath_ = std::move(other.filepath_);
        fileData_ = std::move(other.fileData_);
        textureLookup_ = std::move(other.textureLookup_);
        rawDataLookup_ = std::move(other.rawDataLookup_);
        entryCount_ = other.entryCount_;
        isOpen_ = other.isOpen_;
        other.entryCount_ = 0;
        other.isOpen_ = false;
    }
    return *this;
}

bool EQE1Loader::open(const std::string& filepath) {
    close();

    // Read entire file
    std::ifstream in(filepath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        EQE1_LOG_ERROR("EQE1Loader: cannot open '%s'", filepath.c_str());
        return false;
    }

    size_t fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0);

    if (fileSize < sizeof(EQE1Header)) {
        EQE1_LOG_ERROR("EQE1Loader: file too small (%zu bytes): '%s'", fileSize, filepath.c_str());
        return false;
    }

    fileData_.resize(fileSize);
    in.read(reinterpret_cast<char*>(fileData_.data()), fileSize);
    in.close();

    // Validate header
    const auto* header = reinterpret_cast<const EQE1Header*>(fileData_.data());

    if (header->magic != EQE1_MAGIC) {
        EQE1_LOG_ERROR("EQE1Loader: bad magic 0x%08X in '%s'", header->magic, filepath.c_str());
        fileData_.clear();
        return false;
    }

    if (header->version != EQE1_VERSION) {
        EQE1_LOG_ERROR("EQE1Loader: unsupported version %u in '%s'", header->version, filepath.c_str());
        fileData_.clear();
        return false;
    }

    uint16_t numEntries = header->numEntries;
    size_t indexEnd = sizeof(EQE1Header) + numEntries * EQE1_INDEX_ENTRY_SIZE;

    if (fileSize < indexEnd) {
        EQE1_LOG_ERROR("EQE1Loader: truncated index (%zu < %zu) in '%s'",
                       fileSize, indexEnd, filepath.c_str());
        fileData_.clear();
        return false;
    }

    // Parse index entries — first pass: build texture and raw data maps
    const auto* entries = reinterpret_cast<const EQE1IndexEntry*>(
        fileData_.data() + sizeof(EQE1Header));

    int textureCount = 0;
    int alphaCount = 0;
    int rawCount = 0;

    for (uint16_t i = 0; i < numEntries; ++i) {
        const auto& e = entries[i];

        // Validate bounds
        if (static_cast<size_t>(e.dataOffset) + e.dataSize > fileSize) {
            EQE1_LOG_ERROR("EQE1Loader: entry %u data out of bounds in '%s'", i, filepath.c_str());
            fileData_.clear();
            return false;
        }

        if (e.dataOffset % EQE1_DATA_ALIGNMENT != 0) {
            EQE1_LOG_ERROR("EQE1Loader: entry %u misaligned (offset=%u) in '%s'",
                           i, e.dataOffset, filepath.c_str());
            fileData_.clear();
            return false;
        }

        // Extract name
        char nameBuf[EQE1_NAME_SIZE + 1] = {};
        std::memcpy(nameBuf, e.name, EQE1_NAME_SIZE);
        std::string name(nameBuf);

        if (e.entryType == EQE1_TYPE_RAW_DATA) {
            rawDataLookup_[name] = {fileData_.data() + e.dataOffset, e.dataSize};
            rawCount++;
        } else {
            TextureEntry tex;
            tex.data = fileData_.data() + e.dataOffset;
            tex.dataSize = e.dataSize;
            tex.width = e.width;
            tex.height = e.height;
            tex.mipLevels = e.mipLevels;
            tex.hasAlpha = (e.hasAlpha != 0);
            tex.alphaData = nullptr;
            tex.alphaDataSize = 0;
            textureLookup_[name] = tex;
            textureCount++;
            if (tex.hasAlpha) alphaCount++;
        }
    }

    // Second pass: link alpha companions
    for (auto& [name, tex] : textureLookup_) {
        if (!tex.hasAlpha) continue;

        std::string alphaName = name + "_alpha";
        auto alphaIt = textureLookup_.find(alphaName);
        if (alphaIt != textureLookup_.end()) {
            tex.alphaData = alphaIt->second.data;
            tex.alphaDataSize = alphaIt->second.dataSize;
        }
    }

    filepath_ = filepath;
    entryCount_ = numEntries;
    isOpen_ = true;

    EQE1_LOG_INFO("EQE1Loader: opened '%s' (%d textures, %d alpha, %d raw)",
                  filepath.c_str(), textureCount, alphaCount, rawCount);

    return true;
}

void EQE1Loader::close() {
    textureLookup_.clear();
    rawDataLookup_.clear();
    fileData_.clear();
    fileData_.shrink_to_fit();
    filepath_.clear();
    entryCount_ = 0;
    isOpen_ = false;
}

const EQE1Loader::TextureEntry* EQE1Loader::lookup(const std::string& name) const {
    auto it = textureLookup_.find(name);
    if (it != textureLookup_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool EQE1Loader::getRawData(const std::string& name,
                             const uint8_t*& data, uint32_t& size) const {
    auto it = rawDataLookup_.find(name);
    if (it != rawDataLookup_.end()) {
        data = it->second.first;
        size = it->second.second;
        return true;
    }
    return false;
}

std::vector<std::string> EQE1Loader::getTextureNames() const {
    std::vector<std::string> names;
    names.reserve(textureLookup_.size());
    for (const auto& [name, _] : textureLookup_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> EQE1Loader::getRawDataNames() const {
    std::vector<std::string> names;
    names.reserve(rawDataLookup_.size());
    for (const auto& [name, _] : rawDataLookup_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}} // namespace EQT::Graphics
