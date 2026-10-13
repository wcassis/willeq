#pragma once

// B888-CC1 (#204): measured stock-client FacePick ranges. The shipped TSV
// records each race/gender/picker separately; no appearance limit lives in C++.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace eqt {
namespace charcreate {

enum class FacePickFeature : size_t {
    Face, Hair, HairColor, Eyes, Beard, BeardColor, Wode, Count
};

constexpr size_t kFacePickFeatures = static_cast<size_t>(FacePickFeature::Count);

struct FacePickRange {
    uint32_t min = 0, max = 0;
    bool visible = false;
    std::string source;
};

using FacePickRanges = std::array<FacePickRange, kFacePickFeatures>;

class FacePickData {
public:
    bool loadText(const std::string& text);
    bool loadFile(const std::string& path);
    bool loadShipped();
    const FacePickRanges* find(uint32_t race, uint32_t gender) const;
    const std::string& error() const { return error_; }

private:
    std::map<std::pair<uint32_t, uint32_t>, FacePickRanges> rows_;
    std::string error_;
};

}  // namespace charcreate
}  // namespace eqt
