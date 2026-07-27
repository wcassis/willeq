#include "client/graphics/texture_upload_ledger.h"

#include <cstdio>

namespace EQT { namespace Graphics {

void TextureUploadLedger::reset() {
    uploads_.clear();
    evictions_.clear();
    nameToCategory_.clear();
    everEvicted_.clear();
    uploadOrder_ = 0;
    evictionOrder_ = 0;
    thrashCount_ = 0;
}

void TextureUploadLedger::recordUpload(const std::string& name, int width, int height,
                                        size_t bytes, const std::string& format,
                                        const std::string& category, const std::string& callsite) {
    TextureUploadRecord rec;
    rec.order = uploadOrder_++;
    rec.name = name;
    rec.width = width;
    rec.height = height;
    rec.bytes = bytes;
    rec.format = format;
    rec.category = category;
    rec.callsite = callsite;
    rec.isReupload = (everEvicted_.find(name) != everEvicted_.end());
    if (rec.isReupload) {
        ++thrashCount_;
    }
    nameToCategory_[name] = category;
    uploads_.push_back(std::move(rec));
}

void TextureUploadLedger::recordEviction(const std::string& name, size_t bytes,
                                          const std::string& category) {
    TextureEvictionRecord rec;
    rec.order = evictionOrder_++;
    rec.name = name;
    rec.bytes = bytes;
    // Prefer the category passed by the caller (looked up from CachedTexture
    // at eviction time); fall back to the upload-time record if the caller
    // didn't have one.
    auto it = nameToCategory_.find(name);
    rec.category = !category.empty() ? category : (it != nameToCategory_.end() ? it->second : "unknown");
    everEvicted_.insert(name);
    evictions_.push_back(std::move(rec));
}

bool TextureUploadLedger::dumpToFile(const std::string& path, const std::string& runLabel) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        return false;
    }

    std::fprintf(f, "# P13 texture upload ledger — run=%s\n", runLabel.c_str());
    std::fprintf(f, "# uploads=%zu evictions=%zu thrash_reuploads=%zu\n",
                 uploads_.size(), evictions_.size(), thrashCount_);
    std::fprintf(f, "#\n# UPLOADS: order,name,width,height,bytes,format,category,callsite,is_reupload\n");
    for (const auto& u : uploads_) {
        std::fprintf(f, "U,%u,%s,%d,%d,%zu,%s,%s,%s,%d\n",
                     u.order, u.name.c_str(), u.width, u.height, u.bytes,
                     u.format.c_str(), u.category.c_str(), u.callsite.c_str(),
                     u.isReupload ? 1 : 0);
    }
    std::fprintf(f, "#\n# EVICTIONS: order,name,bytes,category\n");
    for (const auto& e : evictions_) {
        std::fprintf(f, "E,%u,%s,%zu,%s\n",
                     e.order, e.name.c_str(), e.bytes, e.category.c_str());
    }
    std::fclose(f);
    return true;
}

}} // namespace EQT::Graphics
