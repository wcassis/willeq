#ifndef EQT_GRAPHICS_TEXTURE_ORIGIN_H
#define EQT_GRAPHICS_TEXTURE_ORIGIN_H

// B947 (GitLab #265): where a decode-path texture's raw bytes came from, so
// ConstrainedTextureCache can re-read and re-decode EXACTLY those bytes when
// it restores the texture after an eviction.
//
// B944 made eviction keep the ITexture object and restore its storage on the
// next bind, but a decode-path (S3D/BMP) entry could only restore through a
// name-keyed TextureSourceProvider over the ZONE's archives. Character and
// equipment textures (`*_chr.s3d`, `global*_chr.s3d`, `gequip*.s3d`,
// `global*_amr.s3d`) and the procedural textures read from data/textures/ had
// no source, so their eviction still DESTROYED the object -- and
// RaceModelLoader's cached meshes kept the freed pointer.
//
// An origin is recorded where the bytes are READ (the S3D loaders stamp it on
// the TextureInfo; a file loader builds one from its path) and handed to
// ConstrainedTextureCache::getOrLoad(). The cache also records a digest of the
// bytes, and a restore that reads anything else is refused -- the same name in
// two archives cannot restore the wrong pixels.
//
// `file` is shared (PfsArchive::sharedPath()), so a TextureInfo pays one
// pointer for it, not a copy of the path.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct TextureOrigin {
    // The archive the member was read from, or -- when `member` is empty --
    // the texture file itself.
    std::shared_ptr<const std::string> file;
    // Archive member name as PfsArchive stores it (lower case). Empty for a
    // plain file.
    std::string member;

    bool known() const { return file && !file->empty(); }
    bool isArchiveMember() const { return known() && !member.empty(); }

    static TextureOrigin archiveMember(std::shared_ptr<const std::string> archive, std::string memberName) {
        TextureOrigin o;
        o.file = std::move(archive);
        o.member = std::move(memberName);
        return o;
    }
    // Absolute, so a restore does not depend on the working directory.
    static TextureOrigin plainFile(const std::string& path) {
        TextureOrigin o;
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(path, ec);
        o.file = std::make_shared<const std::string>(ec ? path : abs.string());
        return o;
    }
};

// FNV-1a 64 over a texture's raw (file) bytes: the identity a restore must
// reproduce before it is allowed to decode anything.
inline uint64_t rawBytesDigest(const std::vector<char>& bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : bytes) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_TEXTURE_ORIGIN_H
