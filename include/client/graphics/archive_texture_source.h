#ifndef EQT_GRAPHICS_ARCHIVE_TEXTURE_SOURCE_H
#define EQT_GRAPHICS_ARCHIVE_TEXTURE_SOURCE_H

// B944 (GitLab #262): the restore source for decode-path (S3D/BMP) textures
// ConstrainedTextureCache evicts.
//
// On the S3D path the cache decodes a texture from raw bytes its caller owns,
// and the caller releases them after upload (S3DZone::releaseTexturePixelData)
// -- so an eviction has nothing to rebuild from unless the bytes can be read
// again. This reads them again from the archives they came from, by the
// member name: each archive is opened (PfsArchive::open maps it read-only,
// file-backed pages, T504c) on the first lookup and kept open until this
// object is destroyed. Nothing is decompressed until a restore asks for one
// member. Archives are searched in the order they were added; a name the
// first archive holds is always served from it.
//
// Lookups are lower-cased (archive member names are stored lower-case).

#include "client/graphics/constrained_texture_cache.h"

#include <memory>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

class PfsArchive;

class ArchiveTextureSource : public TextureSourceProvider {
public:
    ArchiveTextureSource();
    ~ArchiveTextureSource() override;
    ArchiveTextureSource(const ArchiveTextureSource&) = delete;
    ArchiveTextureSource& operator=(const ArchiveTextureSource&) = delete;

    // Add an archive path (not opened until a lookup needs it). A path that
    // is already listed is ignored.
    void addArchive(const std::string& path);
    const std::vector<std::string>& archivePaths() const { return paths_; }

    bool hasSource(const std::string& name) override;
    bool readSource(const std::string& name, std::vector<char>& out) override;

    // B947 (GitLab #265): read ONE named member of ONE archive -- the exact
    // origin a character/equipment texture was read from (TextureOrigin).
    // The archive is added (and opened on this first use) if it is not listed
    // yet; it then also serves hasSource()/readSource(). `member` is matched
    // lower-cased, as PfsArchive stores it.
    bool readMember(const std::string& archivePath, const std::string& member, std::vector<char>& out);
    // A plain file's whole contents (a data/textures/ BMP, a SpellEffects DDS).
    static bool readFile(const std::string& path, std::vector<char>& out);

private:
    PfsArchive* archiveAt(size_t index);  // opens on first use; nullptr if it cannot

    std::vector<std::string> paths_;
    std::vector<std::unique_ptr<PfsArchive>> archives_;  // parallel to paths_
    std::vector<bool> openFailed_;
};

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_ARCHIVE_TEXTURE_SOURCE_H
