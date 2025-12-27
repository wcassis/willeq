#include "client/graphics/eq/pfs.h"
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <tuple>

namespace EQT {
namespace Graphics {

#define POLYNOMIAL 0x04C11DB7

// --- PfsCrc Implementation ---

PfsCrc& PfsCrc::instance() {
    static PfsCrc inst;
    return inst;
}

PfsCrc::PfsCrc() {
    generateTable();
}

void PfsCrc::generateTable() {
    for (int i = 0; i < 256; ++i) {
        int32_t crc_accum = i << 24;
        for (int j = 0; j < 8; ++j) {
            if ((crc_accum & 0x80000000) != 0) {
                crc_accum = (crc_accum << 1) ^ POLYNOMIAL;
            } else {
                crc_accum = crc_accum << 1;
            }
        }
        crc_table[i] = crc_accum;
    }
}

int32_t PfsCrc::update(int32_t crc, const uint8_t* data, int32_t length) {
    while (length > 0) {
        int i = ((crc >> 24) ^ *data) & 0xFF;
        data += 1;
        crc = (crc << 8) ^ crc_table[i];
        length -= 1;
    }
    return crc;
}

int32_t PfsCrc::get(const std::string& s) {
    if (s.empty()) return 0;

    std::vector<uint8_t> buf(s.length() + 1, 0);
    std::memcpy(buf.data(), s.c_str(), s.length());

    return update(0, buf.data(), static_cast<int32_t>(s.length() + 1));
}

// --- Compression ---

uint32_t inflateData(const char* buffer, uint32_t len, char* outBuffer, uint32_t outLenMax) {
    z_stream zstream;
    std::memset(&zstream, 0, sizeof(zstream));

    zstream.next_in = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(buffer));
    zstream.avail_in = len;
    zstream.next_out = reinterpret_cast<unsigned char*>(outBuffer);
    zstream.avail_out = outLenMax;
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    int ret = inflateInit2(&zstream, 15);
    if (ret != Z_OK) {
        return 0;
    }

    ret = inflate(&zstream, Z_FINISH);
    if (ret == Z_STREAM_END) {
        inflateEnd(&zstream);
        return static_cast<uint32_t>(zstream.total_out);
    } else {
        inflateEnd(&zstream);
        return 0;
    }
}

// --- PfsArchive Implementation ---

bool PfsArchive::open(const std::string& filename) {
    close();

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) {
        return false;
    }

    size_t idx = 0;

    // Read directory offset
    if (idx + sizeof(uint32_t) > buffer.size()) return false;
    uint32_t dir_offset = *reinterpret_cast<uint32_t*>(&buffer[idx]);
    idx += sizeof(uint32_t);

    // Check magic "PFS "
    if (idx + 4 > buffer.size()) return false;
    char magic[4];
    std::memcpy(magic, &buffer[idx], 4);
    if (magic[0] != 'P' || magic[1] != 'F' || magic[2] != 'S' || magic[3] != ' ') {
        return false;
    }

    // Read directory entries
    if (dir_offset + sizeof(uint32_t) > buffer.size()) return false;
    uint32_t dir_count = *reinterpret_cast<uint32_t*>(&buffer[dir_offset]);

    std::vector<std::tuple<int32_t, uint32_t, uint32_t>> directoryEntries;
    std::vector<std::tuple<int32_t, std::string>> filenameEntries;

    for (uint32_t i = 0; i < dir_count; ++i) {
        size_t entryOffset = dir_offset + 4 + (i * 12);
        if (entryOffset + 12 > buffer.size()) return false;

        int32_t crc = *reinterpret_cast<int32_t*>(&buffer[entryOffset]);
        uint32_t offset = *reinterpret_cast<uint32_t*>(&buffer[entryOffset + 4]);
        uint32_t entry_size = *reinterpret_cast<uint32_t*>(&buffer[entryOffset + 8]);

        // Special CRC for filename table
        if (crc == 0x61580ac9) {
            std::vector<char> filenameBuffer;
            if (!inflateByOffset(offset, entry_size, buffer, filenameBuffer)) {
                return false;
            }

            uint32_t filenamePos = 0;
            if (filenamePos + sizeof(uint32_t) > filenameBuffer.size()) return false;
            uint32_t filenameCount = *reinterpret_cast<uint32_t*>(&filenameBuffer[filenamePos]);
            filenamePos += sizeof(uint32_t);

            for (uint32_t j = 0; j < filenameCount; ++j) {
                if (filenamePos + sizeof(uint32_t) > filenameBuffer.size()) return false;
                uint32_t filenameLength = *reinterpret_cast<uint32_t*>(&filenameBuffer[filenamePos]);
                filenamePos += sizeof(uint32_t);

                if (filenamePos + filenameLength > filenameBuffer.size()) return false;
                std::string name(filenameLength - 1, '\0');
                std::memcpy(&name[0], &filenameBuffer[filenamePos], filenameLength - 1);
                filenamePos += filenameLength;

                std::transform(name.begin(), name.end(), name.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                int32_t fileCrc = PfsCrc::instance().get(name);
                filenameEntries.push_back(std::make_tuple(fileCrc, name));
            }
        } else {
            directoryEntries.push_back(std::make_tuple(crc, offset, entry_size));
        }
    }

    // Match CRCs to filenames and store files
    for (const auto& dirEntry : directoryEntries) {
        int32_t crc = std::get<0>(dirEntry);

        for (const auto& fnEntry : filenameEntries) {
            int32_t fileCrc = std::get<0>(fnEntry);
            if (crc == fileCrc) {
                uint32_t offset = std::get<1>(dirEntry);
                uint32_t entry_size = std::get<2>(dirEntry);
                const std::string& name = std::get<1>(fnEntry);

                if (!storeBlocksByOffset(offset, entry_size, buffer, name)) {
                    return false;
                }
                break;
            }
        }
    }

    return true;
}

void PfsArchive::close() {
    files_.clear();
    uncompressedSizes_.clear();
}

bool PfsArchive::get(const std::string& filename, std::vector<char>& buffer) {
    std::string lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    auto it = files_.find(lowerName);
    if (it == files_.end()) {
        return false;
    }

    buffer.clear();
    uint32_t ucSize = uncompressedSizes_[lowerName];
    if (!inflateByOffset(0, ucSize, it->second, buffer)) {
        return false;
    }

    return true;
}

bool PfsArchive::exists(const std::string& filename) const {
    std::string lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return files_.find(lowerName) != files_.end();
}

bool PfsArchive::getFilenames(const std::string& extension, std::vector<std::string>& filenames) const {
    filenames.clear();
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    bool allFiles = (ext == "*");
    size_t extLen = ext.length();

    for (const auto& pair : files_) {
        if (allFiles) {
            filenames.push_back(pair.first);
            continue;
        }

        size_t fnLen = pair.first.length();
        if (fnLen <= extLen) continue;

        if (pair.first.compare(fnLen - extLen, extLen, ext) == 0) {
            filenames.push_back(pair.first);
        }
    }

    return !filenames.empty();
}

bool PfsArchive::storeBlocksByOffset(uint32_t offset, uint32_t size,
                                     const std::vector<char>& inBuffer,
                                     const std::string& filename) {
    uint32_t position = offset;
    uint32_t inflate_total = 0;

    while (inflate_total < size) {
        if (position + 8 > inBuffer.size()) return false;
        uint32_t deflateLength = *reinterpret_cast<const uint32_t*>(&inBuffer[position]);
        uint32_t inflateLength = *reinterpret_cast<const uint32_t*>(&inBuffer[position + 4]);
        inflate_total += inflateLength;
        position += deflateLength + 8;
    }

    uint32_t blockSize = position - offset;
    std::vector<char> tbuffer(blockSize);
    std::memcpy(tbuffer.data(), &inBuffer[offset], blockSize);

    files_[filename] = tbuffer;
    uncompressedSizes_[filename] = size;
    return true;
}

bool PfsArchive::inflateByOffset(uint32_t offset, uint32_t size,
                                 const std::vector<char>& inBuffer,
                                 std::vector<char>& outBuffer) {
    outBuffer.resize(size);
    std::memset(outBuffer.data(), 0, size);

    uint32_t position = offset;
    uint32_t inflate_total = 0;

    while (inflate_total < size) {
        if (position + 8 > inBuffer.size()) return false;
        uint32_t deflateLength = *reinterpret_cast<const uint32_t*>(&inBuffer[position]);
        uint32_t inflateLength = *reinterpret_cast<const uint32_t*>(&inBuffer[position + 4]);

        if (position + 8 + deflateLength > inBuffer.size()) return false;

        inflateData(&inBuffer[position + 8], deflateLength,
                   &outBuffer[inflate_total], inflateLength);

        inflate_total += inflateLength;
        position += deflateLength + 8;
    }

    return true;
}

} // namespace Graphics
} // namespace EQT
