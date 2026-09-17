#ifndef EQT_GRAPHICS_PFS_H
#define EQT_GRAPHICS_PFS_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace EQT {
namespace Graphics {

// CRC class for PFS filename hashing
class PfsCrc {
public:
    static PfsCrc& instance();
    int32_t get(const std::string& s);

private:
    PfsCrc();
    void generateTable();
    int32_t update(int32_t crc, const uint8_t* data, int32_t length);

    int32_t crc_table[256];
};

// PFS Archive reader for S3D files
class PfsArchive {
public:
    PfsArchive() = default;
    ~PfsArchive() = default;

    bool open(const std::string& filename);
    void close();

    bool get(const std::string& filename, std::vector<char>& buffer);
    bool exists(const std::string& filename) const;
    bool getFilenames(const std::string& extension, std::vector<std::string>& filenames) const;

    // T496 Leg D (board B830): read ONLY an archive's filename table, without
    // materialising the archive.
    //
    // `open()` reads the whole file into one buffer AND copies every member's
    // compressed blocks into `files_`, so it costs roughly twice the archive on
    // disk and keeps it. An INDEXING pass wants neither: it wants the names.
    // AudioManager::scanPfsArchives() indexes seventeen `snd*.pfs`
    // (124,397,766 bytes on disk) at startup and used to pay `open()` for each
    // -- measured at 155,560,752 bytes of peak `audio` ledger bytes, against
    // 25.7 MB of steady state.
    //
    // This walks the directory, finds the filename-table entry (CRC
    // 0x61580ac9) and inflates that entry alone, streaming its blocks straight
    // off the ifstream. Peak is the uncompressed filename table, tens of kB.
    // Static because it owns no archive state; `extension` matches
    // getFilenames() ("*" for all), and the names come back lowercased, as
    // they are stored.
    static bool listFilenames(const std::string& path, const std::string& extension,
                              std::vector<std::string>& filenames);

    const std::map<std::string, std::vector<char>>& getFiles() const { return files_; }

    // Memory usage of all cached file buffers
    size_t getMemoryUsage() const {
        size_t total = 0;
        for (const auto& [name, data] : files_)
            total += name.capacity() + data.capacity();
        return total;
    }

private:
    bool storeBlocksByOffset(uint32_t offset, uint32_t size,
                             const std::vector<char>& buffer,
                             const std::string& filename);
    bool inflateByOffset(uint32_t offset, uint32_t size,
                         const std::vector<char>& inBuffer,
                         std::vector<char>& outBuffer);

    std::map<std::string, std::vector<char>> files_;
    std::map<std::string, uint32_t> uncompressedSizes_;
};

// Compression utilities
uint32_t inflateData(const char* buffer, uint32_t len, char* outBuffer, uint32_t outLenMax);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PFS_H
