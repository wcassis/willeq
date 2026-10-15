#pragma once

// B888-CC1 (#204): candidate FacePick endpoints with independently checked
// counts only where the retained matrix stills prove recurrence. Other counts
// remain UNMEASURED; no appearance limit lives in C++.

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
    bool visible = false;         // the Luclin-mode row (UseLuclin=TRUE stills)
    bool visibleClassic = false;  // the classic-mode row (stock UseLuclin=FALSE stills)
    std::string source;

    // The model-era gate (MR !168 finding 1): classic models (the client's
    // default, RaceModelLoader::isUsingOldModels()) consult the classic
    // column; Luclin models consult the Luclin column.
    bool visibleFor(bool oldModels) const { return oldModels ? visibleClassic : visible; }
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
