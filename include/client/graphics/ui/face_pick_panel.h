#pragma once

// B888-CC1 (#204): the FacePick modal's draft state, draw and input. The
// committed values live on CharCreateScreenState; Cancel never changes them.

#include "client/login_world/char_create_faces.h"

#include <array>
#include <cstdint>

namespace EQT {
namespace Graphics {

using eqt::charcreate::FacePickData;
using eqt::charcreate::FacePickFeature;
using eqt::charcreate::FacePickRanges;
using eqt::charcreate::kFacePickFeatures;

class UIRenderer;
struct CharCreateScreenState;

struct FacePickState {
    bool isOpen = false;
    FacePickRanges ranges;
    std::array<uint32_t, kFacePickFeatures> values{};
    uint32_t randomSeed = 0;
};

enum class FacePickHit : uint8_t { None, Down, Up, Random, Accept, Cancel, Blocked };
struct FacePickClickTarget {
    FacePickHit hit = FacePickHit::None;
    int row = -1;
};

bool openFacePick(CharCreateScreenState& s);
void stepFacePick(FacePickState& s, int row, int direction);
void randomFacePick(FacePickState& s);
void acceptFacePick(CharCreateScreenState& s);
void cancelFacePick(CharCreateScreenState& s);

void renderFacePick(UIRenderer& ui, int sw, int sh, const FacePickState& s);
FacePickClickTarget hitTestFacePick(int sw, int sh, const FacePickState& s, int x, int y);
void applyFacePickClick(CharCreateScreenState& s, const FacePickClickTarget& target);

}  // namespace Graphics
}  // namespace EQT
