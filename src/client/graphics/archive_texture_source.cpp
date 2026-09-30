// archive_texture_source.cpp — B944: see include/client/graphics/archive_texture_source.h.
#include "client/graphics/archive_texture_source.h"
#include "client/graphics/eq/pfs.h"
#include "client/core/memory_ledger.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace EQT {
namespace Graphics {

namespace {
std::string lowerCased(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}  // namespace

ArchiveTextureSource::ArchiveTextureSource() = default;
ArchiveTextureSource::~ArchiveTextureSource() = default;

void ArchiveTextureSource::addArchive(const std::string& path) {
    if (path.empty() || std::find(paths_.begin(), paths_.end(), path) != paths_.end()) return;
    paths_.push_back(path);
    archives_.emplace_back();
    openFailed_.push_back(false);
}

PfsArchive* ArchiveTextureSource::archiveAt(size_t index) {
    if (index >= paths_.size() || openFailed_[index]) return nullptr;
    if (!archives_[index]) {
        // The directory table is the only anonymous memory an open archive
        // keeps (the file itself is a read-only mapping).
        eqt::memory::MemoryScope scope(eqt::memory::Category::Archives);  // T491
        auto archive = std::make_unique<PfsArchive>();
        if (!archive->open(paths_[index])) {
            openFailed_[index] = true;
            LOG_WARN(MOD_GRAPHICS, "ArchiveTextureSource: cannot open '{}' -- textures evicted from it "
                     "cannot be restored", paths_[index]);
            return nullptr;
        }
        archives_[index] = std::move(archive);
    }
    return archives_[index].get();
}

bool ArchiveTextureSource::hasSource(const std::string& name) {
    const std::string lower = lowerCased(name);
    for (size_t i = 0; i < paths_.size(); ++i) {
        if (PfsArchive* archive = archiveAt(i)) {
            if (archive->exists(lower)) return true;
        }
    }
    return false;
}

bool ArchiveTextureSource::readSource(const std::string& name, std::vector<char>& out) {
    const std::string lower = lowerCased(name);
    for (size_t i = 0; i < paths_.size(); ++i) {
        PfsArchive* archive = archiveAt(i);
        if (archive && archive->exists(lower)) {
            return archive->get(lower, out);
        }
    }
    return false;
}

bool ArchiveTextureSource::readMember(const std::string& archivePath, const std::string& member,
                                      std::vector<char>& out) {
    auto it = std::find(paths_.begin(), paths_.end(), archivePath);
    if (it == paths_.end()) {
        addArchive(archivePath);
        it = std::find(paths_.begin(), paths_.end(), archivePath);
        if (it == paths_.end()) return false;
    }
    PfsArchive* archive = archiveAt(static_cast<size_t>(it - paths_.begin()));
    if (!archive) return false;
    const std::string lower = lowerCased(member);
    return archive->exists(lower) && archive->get(lower, out);
}

bool ArchiveTextureSource::readFile(const std::string& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(out.data(), size));
}

}  // namespace Graphics
}  // namespace EQT
