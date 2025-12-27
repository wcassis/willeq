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

    const std::map<std::string, std::vector<char>>& getFiles() const { return files_; }

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
