#include "client/graphics/eq/pfs.h"
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <tuple>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace EQT {
namespace Graphics {

// Safe unaligned read helper - uses memcpy to avoid bus errors on ARM
template<typename T>
static inline T read_val(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}

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

    // T504c Leg 2d: map the file (see the header). MAP_PRIVATE + PROT_READ:
    // the pages are the page cache's, shared with every other mapping of the
    // same archive and reclaimable; nothing is written.
    int fd = ::open(filename.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { ::close(fd); return false; }
    const size_t size = static_cast<size_t>(st.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping != MAP_FAILED) {
        mapping_ = mapping;
        mappedSize_ = size;
        data_ = static_cast<const char*>(mapping);
        dataSize_ = size;
    } else {
        heapBuffer_.resize(size);
        if (::pread(fd, heapBuffer_.data(), size, 0) != static_cast<ssize_t>(size)) {
            ::close(fd);
            close();
            return false;
        }
        data_ = heapBuffer_.data();
        dataSize_ = size;
    }
    ::close(fd);   // the mapping outlives the descriptor

    if (!parseDirectory()) { close(); return false; }
    return true;
}

bool PfsArchive::parseDirectory() {
    size_t idx = 0;

    // Read directory offset
    if (idx + sizeof(uint32_t) > dataSize_) return false;
    uint32_t dir_offset = read_val<uint32_t>(&data_[idx]);
    idx += sizeof(uint32_t);

    // Check magic "PFS "
    if (idx + 4 > dataSize_) return false;
    char magic[4];
    std::memcpy(magic, &data_[idx], 4);
    if (magic[0] != 'P' || magic[1] != 'F' || magic[2] != 'S' || magic[3] != ' ') {
        return false;
    }

    // Read directory entries
    if (dir_offset + sizeof(uint32_t) > dataSize_) return false;
    uint32_t dir_count = read_val<uint32_t>(&data_[dir_offset]);

    std::vector<std::tuple<int32_t, uint32_t, uint32_t>> directoryEntries;
    std::vector<std::tuple<int32_t, std::string>> filenameEntries;

    for (uint32_t i = 0; i < dir_count; ++i) {
        size_t entryOffset = dir_offset + 4 + (i * 12);
        if (entryOffset + 12 > dataSize_) return false;

        int32_t crc = read_val<int32_t>(&data_[entryOffset]);
        uint32_t offset = read_val<uint32_t>(&data_[entryOffset + 4]);
        uint32_t entry_size = read_val<uint32_t>(&data_[entryOffset + 8]);

        // Special CRC for filename table
        if (crc == 0x61580ac9) {
            std::vector<char> filenameBuffer;
            if (!inflateByOffset(offset, entry_size, filenameBuffer)) {
                return false;
            }

            uint32_t filenamePos = 0;
            if (filenamePos + sizeof(uint32_t) > filenameBuffer.size()) return false;
            uint32_t filenameCount = read_val<uint32_t>(&filenameBuffer[filenamePos]);
            filenamePos += sizeof(uint32_t);

            for (uint32_t j = 0; j < filenameCount; ++j) {
                if (filenamePos + sizeof(uint32_t) > filenameBuffer.size()) return false;
                uint32_t filenameLength = read_val<uint32_t>(&filenameBuffer[filenamePos]);
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

    // Match CRCs to filenames and record each member's block range
    for (const auto& dirEntry : directoryEntries) {
        int32_t crc = std::get<0>(dirEntry);

        for (const auto& fnEntry : filenameEntries) {
            int32_t fileCrc = std::get<0>(fnEntry);
            if (crc == fileCrc) {
                uint32_t offset = std::get<1>(dirEntry);
                uint32_t entry_size = std::get<2>(dirEntry);
                const std::string& name = std::get<1>(fnEntry);

                // Walk the block headers once to size the member's range.
                uint32_t position = offset;
                uint32_t inflate_total = 0;
                while (inflate_total < entry_size) {
                    if (position + 8 > dataSize_) return false;
                    uint32_t deflateLength = read_val<uint32_t>(&data_[position]);
                    uint32_t inflateLength = read_val<uint32_t>(&data_[position + 4]);
                    inflate_total += inflateLength;
                    position += deflateLength + 8;
                }
                FileEntry e;
                e.offset = offset;
                e.blockBytes = position - offset;
                e.uncompressedSize = entry_size;
                files_[name] = e;
                break;
            }
        }
    }

    return true;
}

void PfsArchive::close() {
    files_.clear();
    if (mapping_) {
        munmap(mapping_, mappedSize_);
        mapping_ = nullptr;
    }
    mappedSize_ = 0;
    std::vector<char>().swap(heapBuffer_);
    data_ = nullptr;
    dataSize_ = 0;
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
    if (!inflateByOffset(it->second.offset, it->second.uncompressedSize, buffer)) {
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

bool PfsArchive::inflateByOffset(uint32_t offset, uint32_t size,
                                 std::vector<char>& outBuffer) const {
    outBuffer.resize(size);
    std::memset(outBuffer.data(), 0, size);

    uint32_t position = offset;
    uint32_t inflate_total = 0;

    while (inflate_total < size) {
        if (position + 8 > dataSize_) return false;
        uint32_t deflateLength = read_val<uint32_t>(&data_[position]);
        uint32_t inflateLength = read_val<uint32_t>(&data_[position + 4]);

        if (position + 8 + deflateLength > dataSize_) return false;

        inflateData(&data_[position + 8], deflateLength,
                   &outBuffer[inflate_total], inflateLength);

        inflate_total += inflateLength;
        position += deflateLength + 8;
    }

    return true;
}

// T496 Leg D (board B830). See the declaration in pfs.h for why this exists.
bool PfsArchive::listFilenames(const std::string& path, const std::string& extension,
                               std::vector<std::string>& filenames) {
    filenames.clear();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize fileSize = file.tellg();
    if (fileSize < 12) return false;

    auto readAt = [&](std::streamoff off, char* dst, std::streamsize n) {
        if (off < 0 || off + n > fileSize) return false;
        file.seekg(off, std::ios::beg);
        return static_cast<bool>(file.read(dst, n));
    };

    char head[8];
    if (!readAt(0, head, 8)) return false;
    const uint32_t dirOffset = read_val<uint32_t>(head);
    if (head[4] != 'P' || head[5] != 'F' || head[6] != 'S' || head[7] != ' ') return false;

    char countBuf[4];
    if (!readAt(dirOffset, countBuf, 4)) return false;
    const uint32_t dirCount = read_val<uint32_t>(countBuf);
    if (dirCount == 0 || dirCount > (1u << 22)) return false;

    std::vector<char> dir(static_cast<size_t>(dirCount) * 12);
    if (!readAt(static_cast<std::streamoff>(dirOffset) + 4, dir.data(),
                static_cast<std::streamsize>(dir.size()))) {
        return false;
    }

    // The filename table is the entry carrying this well-known CRC; every other
    // entry is a member file and is of no interest to an index pass.
    uint32_t fnOffset = 0, fnSize = 0;
    bool found = false;
    for (uint32_t i = 0; i < dirCount; ++i) {
        const char* e = dir.data() + static_cast<size_t>(i) * 12;
        if (read_val<int32_t>(e) != 0x61580ac9) continue;
        fnOffset = read_val<uint32_t>(e + 4);
        fnSize = read_val<uint32_t>(e + 8);
        found = true;
        break;
    }
    if (!found || fnSize == 0) return false;

    // Inflate just that entry, one compressed block at a time, straight off the
    // stream -- never a buffer the size of the archive.
    std::vector<char> out(fnSize, '\0');
    std::vector<char> block;
    std::streamoff pos = fnOffset;
    uint32_t done = 0;
    while (done < fnSize) {
        char lens[8];
        if (!readAt(pos, lens, 8)) return false;
        const uint32_t deflateLength = read_val<uint32_t>(lens);
        const uint32_t inflateLength = read_val<uint32_t>(lens + 4);
        if (inflateLength == 0 || done + inflateLength > fnSize) return false;
        block.resize(deflateLength);
        if (deflateLength && !readAt(pos + 8, block.data(),
                                     static_cast<std::streamsize>(deflateLength))) {
            return false;
        }
        inflateData(block.data(), deflateLength, out.data() + done, inflateLength);
        done += inflateLength;
        pos += static_cast<std::streamoff>(deflateLength) + 8;
    }

    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const bool allFiles = (ext == "*");
    const size_t extLen = ext.length();

    uint32_t p = 0;
    if (p + sizeof(uint32_t) > out.size()) return false;
    // Named `filenameTableCount` rather than `nameCount` or `entryCount`:
    // check_zero_callers.py matches call sites by bare IDENTIFIER, so a local
    // that happens to share a name with an exempt accessor silently
    // un-exempts it and turns ZeroCallers red pointing at an unrelated header.
    // Measured, T496 Leg D: `nameCount` collided with
    // EQE1TextureIndex::nameCount and `entryCount` with
    // SkillCapTable::entryCount. This name collides with nothing in the tree.
    const uint32_t filenameTableCount = read_val<uint32_t>(&out[p]);
    p += sizeof(uint32_t);
    for (uint32_t i = 0; i < filenameTableCount; ++i) {
        if (p + sizeof(uint32_t) > out.size()) return false;
        const uint32_t len = read_val<uint32_t>(&out[p]);
        p += sizeof(uint32_t);
        if (len == 0 || p + len > out.size()) return false;
        std::string name(&out[p], len - 1);   // trailing NUL, as open() does
        p += len;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (allFiles) {
            filenames.push_back(std::move(name));
        } else if (name.length() > extLen &&
                   name.compare(name.length() - extLen, extLen, ext) == 0) {
            filenames.push_back(std::move(name));
        }
    }
    return !filenames.empty();
}

} // namespace Graphics
} // namespace EQT
