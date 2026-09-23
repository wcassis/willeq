#include "client/graphics/eqe1_texture_index.h"

#include "common/logging.h"

#include <cctype>
#include <iterator>

#include <algorithm>
#include <fstream>
#include <string_view>

namespace EQT { namespace Graphics {

// T504c Leg 2e: a streaming parse of the index's own schema
//   { "archives": [ "name", ... ], "entries": { "name": [ int, ... ], ... } }
// straight into the flat arrays. It used to go through a jsoncpp DOM:
// MEASURED (run105, gfaydark, `archives` ledger trace) 26.4 MiB peak in
// 235,907 allocations for the 1.9 MB file, plus 3.9 MiB for
// getMemberNames() -- the whole `archives` category peak of every zone
// load (35 MiB), freed the moment load() returned. Now the peak is the
// file text plus the arrays (~3.5 MiB). The builder (jsonEscape) emits
// only the escapes handled here; anything else is a parse error, not a
// guess.
namespace {

class IndexJsonReader {
public:
    IndexJsonReader(const std::string& text, std::string& err) : s_(text), err_(err) {}

    bool expect(char c) {
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != c) return fail(std::string("expected '") + c + "'");
        ++pos_;
        return true;
    }
    bool peekIs(char c) { skipWs(); return pos_ < s_.size() && s_[pos_] == c; }
    bool atEnd() { skipWs(); return pos_ >= s_.size(); }

    bool string(std::string& out) {
        out.clear();
        if (!expect('"')) return false;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
                    unsigned v = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[pos_++];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= h - '0';
                        else if (h >= 'a' && h <= 'f') v |= 10 + h - 'a';
                        else if (h >= 'A' && h <= 'F') v |= 10 + h - 'A';
                        else return fail("bad \\u escape");
                    }
                    if (v > 0x7F) return fail("non-ASCII \\u escape in a texture name");
                    out += static_cast<char>(v);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool uint(unsigned long& out) {
        skipWs();
        if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("expected a number");
        out = 0;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            out = out * 10 + static_cast<unsigned long>(s_[pos_++] - '0');
            if (out > 0xFFFFFFFFul) return fail("number out of range");
        }
        return true;
    }

    size_t offset() const { return pos_; }
    bool fail(const std::string& what) {
        if (err_.empty()) err_ = what + " at byte " + std::to_string(pos_);
        return false;
    }

private:
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
    }
    const std::string& s_;
    size_t pos_ = 0;
    std::string& err_;
};

}  // namespace

bool EQE1TextureIndex::load(const std::string& path) {
    archives_.clear();
    nameBlob_.clear();
    nameOffsets_.clear();
    ownerOffsets_.clear();
    ownerIds_.clear();
    loaded_ = false;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: cannot open {}", path);
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string err;
    IndexJsonReader r(text, err);
    auto bad = [&](const char* what) {
        LOG_ERROR(MOD_GRAPHICS, "EQE1 index: {} in {}: {}", what, path, err.empty() ? "-" : err);
        archives_.clear(); nameBlob_.clear(); nameOffsets_.clear(); ownerOffsets_.clear(); ownerIds_.clear();
        return false;
    };

    // { "archives": [ ... ], "entries": { ... } } -- the two members in either
    // order; any other member is a schema error.
    if (!r.expect('{')) return bad("parse error");
    bool sawArchives = false, sawEntries = false;
    // Entries are parsed into the flat arrays in FILE order; the builder
    // writes them sorted (a std::map walk), which the binary search needs,
    // and that is verified below, not assumed.
    std::string key, name;
    while (true) {
        if (!r.string(key)) return bad("parse error");
        if (!r.expect(':')) return bad("parse error");
        if (key == "archives") {
            if (sawArchives) return bad("duplicate 'archives'");
            sawArchives = true;
            if (!r.expect('[')) return bad("parse error");
            if (!r.peekIs(']')) {
                while (true) {
                    if (!r.string(name)) return bad("parse error");
                    archives_.push_back(name);
                    if (r.peekIs(',')) { r.expect(','); continue; }
                    break;
                }
            }
            if (!r.expect(']')) return bad("parse error");
            // uint16_t indices cap the tree at 65535 archives; the real tree is ~1506 (T281).
            if (archives_.size() > UINT16_MAX) {
                LOG_ERROR(MOD_GRAPHICS, "EQE1 index: {} lists {} archives, over the {} cap",
                          path, archives_.size(), UINT16_MAX);
                return bad("too many archives");
            }
        } else if (key == "entries") {
            if (sawEntries) return bad("duplicate 'entries'");
            if (!sawArchives) return bad("'entries' before 'archives'");   // owners are validated against it
            sawEntries = true;
            if (!r.expect('{')) return bad("parse error");
            if (!r.peekIs('}')) {
                while (true) {
                    if (!r.string(name)) return bad("parse error");
                    if (!r.expect(':')) return bad("parse error");
                    if (!r.expect('[')) return bad("parse error");
                    const size_t before = ownerIds_.size();
                    if (!r.peekIs(']')) {
                        while (true) {
                            unsigned long idx = 0;
                            if (!r.uint(idx)) return bad("parse error");
                            if (idx < archives_.size()) ownerIds_.push_back(static_cast<uint16_t>(idx));
                            if (r.peekIs(',')) { r.expect(','); continue; }
                            break;
                        }
                    }
                    if (!r.expect(']')) return bad("parse error");
                    if (ownerIds_.size() != before) {   // no valid owner: not an entry
                        nameOffsets_.push_back(static_cast<uint32_t>(nameBlob_.size()));
                        ownerOffsets_.push_back(static_cast<uint32_t>(before));
                        nameBlob_ += name;
                    }
                    if (r.peekIs(',')) { r.expect(','); continue; }
                    break;
                }
            }
            if (!r.expect('}')) return bad("parse error");
        } else {
            return bad("unknown member");
        }
        if (r.peekIs(',')) { r.expect(','); continue; }
        break;
    }
    if (!r.expect('}')) return bad("parse error");
    if (!r.atEnd()) return bad("trailing text");
    if (!sawArchives || archives_.empty()) return bad("no 'archives' array");
    if (!sawEntries) return bad("no 'entries' object");
    nameOffsets_.push_back(static_cast<uint32_t>(nameBlob_.size()));
    ownerOffsets_.push_back(static_cast<uint32_t>(ownerIds_.size()));
    nameBlob_.shrink_to_fit();
    nameOffsets_.shrink_to_fit();
    ownerOffsets_.shrink_to_fit();
    ownerIds_.shrink_to_fit();

    // The binary search needs sorted names. Verify; rebuild through a
    // permutation only when a hand-edited file is out of order.
    const size_t n = nameCount();
    auto nameAt = [&](size_t i) {
        return std::string_view(nameBlob_.data() + nameOffsets_[i], nameOffsets_[i + 1] - nameOffsets_[i]);
    };
    bool sorted = true;
    for (size_t i = 1; i < n && sorted; ++i) sorted = nameAt(i - 1) < nameAt(i);
    if (!sorted) {
        std::vector<uint32_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = static_cast<uint32_t>(i);
        std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return nameAt(x) < nameAt(y); });
        std::string blob; blob.reserve(nameBlob_.size());
        std::vector<uint32_t> nOff; nOff.reserve(n + 1);
        std::vector<uint32_t> oOff; oOff.reserve(n + 1);
        std::vector<uint16_t> ids; ids.reserve(ownerIds_.size());
        for (uint32_t i : order) {
            nOff.push_back(static_cast<uint32_t>(blob.size()));
            oOff.push_back(static_cast<uint32_t>(ids.size()));
            blob.append(nameAt(i));
            ids.insert(ids.end(), ownerIds_.begin() + ownerOffsets_[i], ownerIds_.begin() + ownerOffsets_[i + 1]);
        }
        nOff.push_back(static_cast<uint32_t>(blob.size()));
        oOff.push_back(static_cast<uint32_t>(ids.size()));
        nameBlob_.swap(blob); nameOffsets_.swap(nOff); ownerOffsets_.swap(oOff); ownerIds_.swap(ids);
        LOG_WARN(MOD_GRAPHICS, "EQE1 index: {} was not sorted by name; sorted at load", path);
    }

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EQE1 index: {} archives, {} names from {} ({} bytes resident)",
             archives_.size(), nameCount(), path, residentBytes());
    return true;
}

size_t EQE1TextureIndex::residentBytes() const {
    size_t bytes = nameBlob_.capacity() + nameOffsets_.capacity() * sizeof(uint32_t) +
                   ownerOffsets_.capacity() * sizeof(uint32_t) + ownerIds_.capacity() * sizeof(uint16_t);
    for (const auto& a : archives_) bytes += sizeof(std::string) + a.capacity();
    return bytes;
}

EQE1TextureIndex::OwnerList EQE1TextureIndex::owners(const std::string& name) const {
    const size_t n = nameCount();
    const std::string_view key(name);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const std::string_view cur(nameBlob_.data() + nameOffsets_[mid],
                                   nameOffsets_[mid + 1] - nameOffsets_[mid]);
        const int c = cur.compare(key);
        if (c == 0) {
            return OwnerList{ownerIds_.data() + ownerOffsets_[mid],
                             static_cast<size_t>(ownerOffsets_[mid + 1] - ownerOffsets_[mid])};
        }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return OwnerList{};
}

bool EQE1TextureIndex::resolve(const std::string& name,
                               const std::vector<std::string>& candidates,
                               Resolution& out) const {
    out = Resolution{};
    if (!loaded_) return false;
    const OwnerList owners = this->owners(name);
    if (owners.empty()) return false;
    out.ownerCount = owners.size();

    for (const std::string& candidate : candidates) {
        for (uint16_t idx : owners) {
            if (archives_[idx] == candidate) {
                out.archive = candidate;
                out.lateOpen = false;
                return true;
            }
        }
    }

    out.archive = archives_[owners.front()];
    out.lateOpen = true;
    return true;
}

}} // namespace EQT::Graphics
