// B888-CC1 (#204): parser for data/char_create_faces.tsv. Numeric provenance
// belongs to the shipped table; the parser does not infer appearance limits.

#include "client/login_world/char_create_faces.h"

#include "client/data_paths.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace eqt {
namespace charcreate {

namespace {

const char* const kNames[kFacePickFeatures] = {
    "face", "hair", "haircolor", "eyes", "beard", "beardcolor", "wode"
};

std::vector<std::string> tabs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string field;
    while (std::getline(in, field, '\t')) out.push_back(field);
    return out;
}

bool number(const std::string& text, uint32_t& value) {
    if (text.empty()) return false;
    uint32_t n = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
        if (n > 255) return false;
    }
    value = n;
    return true;
}

}  // namespace

bool FacePickData::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) { rows_.clear(); error_ = "cannot open " + path; return false; }
    std::ostringstream text;
    text << in.rdbuf();
    return loadText(text.str());
}

bool FacePickData::loadShipped() {
    return loadFile(eqt::ResolveDataPath("data/char_create_faces.tsv"));
}

bool FacePickData::loadText(const std::string& text) {
    rows_.clear();
    error_.clear();
    std::map<std::pair<uint32_t, uint32_t>, FacePickRanges> parsed;
    std::map<std::pair<uint32_t, uint32_t>, std::set<size_t>> seen;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line.rfind("race\t", 0) == 0) continue;
        const auto f = tabs(line);
        auto bad = [&](const std::string& why) {
            error_ = "char_create_faces.tsv line " + std::to_string(lineNo) + ": " + why;
            return false;
        };
        if (f.size() != 7) return bad("expected 7 columns");
        uint32_t race = 0, gender = 0, lo = 0, hi = 0;
        if (!number(f[0], race) || !number(f[1], gender) || gender > 1 ||
            !number(f[3], lo) || !number(f[4], hi) || lo > hi)
            return bad("invalid race, gender or range");
        const auto it = std::find(std::begin(kNames), std::end(kNames), f[2]);
        if (it == std::end(kNames)) return bad("unknown picker " + f[2]);
        const size_t picker = static_cast<size_t>(it - std::begin(kNames));
        if (f[5] != "0" && f[5] != "1") return bad("visible must be 0 or 1");
        if (f[6].empty()) return bad("missing oracle source");
        if (!seen[{race, gender}].insert(picker).second) return bad("duplicate picker");
        parsed[{race, gender}][picker] = {lo, hi, f[5] == "1", f[6]};
    }
    for (const auto& entry : seen) {
        if (entry.second.size() != kFacePickFeatures) {
            error_ = "char_create_faces.tsv: incomplete race/gender pair";
            return false;
        }
    }
    if (parsed.empty()) { error_ = "char_create_faces.tsv: no ranges"; return false; }
    rows_ = std::move(parsed);
    return true;
}

const FacePickRanges* FacePickData::find(uint32_t race, uint32_t gender) const {
    const auto it = rows_.find({race, gender});
    return it == rows_.end() ? nullptr : &it->second;
}

}  // namespace charcreate
}  // namespace eqt
