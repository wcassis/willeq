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
    ~PfsArchive() { close(); }
    PfsArchive(const PfsArchive&) = delete;             // owns a mapping
    PfsArchive& operator=(const PfsArchive&) = delete;

    // T504c Leg 2d: open() MAPS the file read-only (mmap, MAP_PRIVATE) and
    // records each member's block range; nothing is copied. It used to read
    // the whole file into a heap buffer AND copy every member's compressed
    // blocks out of it (global_chr.s3d, 7.36 MB: a 14 MiB open peak and
    // 7 MiB of anonymous heap for as long as the archive stayed open, on
    // every character-archive load -- run99/run102 named it). Mapped pages
    // are file-backed (RssFile, reclaimable), not anonymous heap. When mmap
    // is unavailable the file is read into one heap buffer, still without
    // the per-member copies.
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

    // One member: where its deflate blocks sit in the mapped file.
    struct FileEntry {
        uint32_t offset = 0;            // first block header
        uint32_t blockBytes = 0;        // all block headers + deflate payloads
        uint32_t uncompressedSize = 0;
    };
    const std::map<std::string, FileEntry>& getFiles() const { return files_; }

    // Bytes this archive keeps addressable: the mapping (file-backed; or
    // the heap buffer when mmap was unavailable) plus the directory.
    size_t getMemoryUsage() const {
        size_t total = mappedSize_ + heapBuffer_.capacity();
        for (const auto& [name, e] : files_) total += name.capacity() + sizeof(FileEntry);
        return total;
    }

private:
    bool parseDirectory();
    bool inflateByOffset(uint32_t offset, uint32_t size, std::vector<char>& outBuffer) const;

    const char* data_ = nullptr;      // the file: mapping_ or heapBuffer_.data()
    size_t dataSize_ = 0;
    void* mapping_ = nullptr;
    size_t mappedSize_ = 0;
    std::vector<char> heapBuffer_;    // fallback when mmap fails
    std::map<std::string, FileEntry> files_;
};

// Compression utilities
uint32_t inflateData(const char* buffer, uint32_t len, char* outBuffer, uint32_t outLenMax);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PFS_H
