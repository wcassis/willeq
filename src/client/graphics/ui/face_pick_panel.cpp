// B888-CC1 (#204): EQUI_FacePick.xml modal, with picker ranges loaded from
// data/char_create_faces.tsv. The model preview is B888's later
// character-model leg; numbers expose each choice until that preview exists.

#include "client/graphics/ui/face_pick_panel.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/char_create_screen.h"
#include "client/graphics/ui/face_pick_layout.h"
#include "client/graphics/ui/ui_renderer.h"

#include <algorithm>
#include <random>
#include <string>

namespace EQT {
namespace Graphics {

namespace {

using Rect = FacePickLayout::Rect;
const irr::video::SColor kText(255, 230, 230, 230);
const irr::video::SColor kFrame(255, 120, 112, 90);

bool inside(const Rect& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

void button(UIRenderer& ui, TextBatch* tb, const Rect& rect, const std::string& label) {
    ui.drawRect(rect, irr::video::SColor(210, 48, 48, 60));
    ui.drawFrame(rect, kFrame);
    if (tb) tb->addTextCentered(label, rect, kText);
}

}  // namespace

bool openFacePick(CharCreateScreenState& s) {
    if (!s.isOpen || s.page != 0 || s.submitting) return false;
    const FacePickRanges* ranges = s.faceData.find(s.race, s.gender);
    if (!ranges) return false;
    // The model-era gate (finding 1): classic models show the visible_classic
    // rows, Luclin models the Luclin rows. The modal's state copies the
    // era-SELECTED column into its plain `visible` field, so drawing,
    // hit-testing, stepping and Random all consume one era-consistent set.
    s.facePick.ranges = *ranges;
    s.facePick.values = s.appearance;
    for (size_t i = 0; i < kFacePickFeatures; ++i) {
        const auto& range = (*ranges)[i];
        const bool shown = range.visibleFor(s.useOldModels);
        s.facePick.ranges[i].visible = shown;
        s.facePick.values[i] = shown ? std::clamp(s.facePick.values[i], range.min, range.max) : 0;
    }
    s.facePick.isOpen = true;
    return true;
}

void stepFacePick(FacePickState& s, int row, int direction) {
    if (!s.isOpen || row < 0 || row >= static_cast<int>(kFacePickFeatures)) return;
    const auto& r = s.ranges[static_cast<size_t>(row)];
    if (!r.visible) return;
    auto& value = s.values[static_cast<size_t>(row)];
    value = direction >= 0 ? (value >= r.max ? r.min : value + 1)
                           : (value <= r.min ? r.max : value - 1);
}

void randomFacePick(FacePickState& s) {
    if (!s.isOpen) return;
    std::mt19937 rng(s.randomSeed);
    for (size_t i = 0; i < kFacePickFeatures; ++i) {
        const auto& r = s.ranges[i];
        if (r.visible) s.values[i] = std::uniform_int_distribution<uint32_t>(r.min, r.max)(rng);
    }
    s.randomSeed = rng();
}

void acceptFacePick(CharCreateScreenState& s) {
    if (!s.facePick.isOpen) return;
    s.appearance = s.facePick.values;
    s.facePick.isOpen = false;
}

void cancelFacePick(CharCreateScreenState& s) { s.facePick.isOpen = false; }

void renderFacePick(UIRenderer& ui, int sw, int sh, const FacePickState& s) {
    if (!s.isOpen) return;
    using F = FacePickLayout;
    TextBatch* tb = ui.getTextBatch();
    ui.beginLayer();
    const Rect window = F::windowRect(sw, sh);
    ui.drawRect(window, irr::video::SColor(255, 15, 18, 29));
    ui.drawPanel(window);
    ui.drawFrame(window, kFrame);
    if (tb) tb->addTextCentered("Choose Your Appearance",
                                F::make(window.UpperLeftCorner.X, window.UpperLeftCorner.Y,
                                        F::kWindowW, F::kClientY), kText);
    for (int row = 0; row < static_cast<int>(kFacePickFeatures); ++row) {
        if (!s.ranges[static_cast<size_t>(row)].visible) continue;
        button(ui, tb, F::downRect(sw, sh, row), "<");
        button(ui, tb, F::upRect(sw, sh, row), ">");
        const std::string text = std::string(F::rowLabel(row)) + " " +
                                 std::to_string(s.values[static_cast<size_t>(row)]);
        if (tb) tb->addTextCentered(text, F::textRect(sw, sh, row), kText);
    }
    button(ui, tb, F::randomRect(sw, sh), "Random");
    button(ui, tb, F::acceptRect(sw, sh), "Accept");
    button(ui, tb, F::cancelRect(sw, sh), "Cancel");
}

FacePickClickTarget hitTestFacePick(int sw, int sh, const FacePickState& s, int x, int y) {
    FacePickClickTarget result;
    if (!s.isOpen) return result;
    result.hit = FacePickHit::Blocked;
    using F = FacePickLayout;
    if (inside(F::randomRect(sw, sh), x, y)) { result.hit = FacePickHit::Random; return result; }
    if (inside(F::acceptRect(sw, sh), x, y)) { result.hit = FacePickHit::Accept; return result; }
    if (inside(F::cancelRect(sw, sh), x, y)) { result.hit = FacePickHit::Cancel; return result; }
    for (int row = 0; row < static_cast<int>(kFacePickFeatures); ++row) {
        if (!s.ranges[static_cast<size_t>(row)].visible) continue;
        if (inside(F::downRect(sw, sh, row), x, y)) {
            result.hit = FacePickHit::Down; result.row = row; return result;
        }
        if (inside(F::upRect(sw, sh, row), x, y)) {
            result.hit = FacePickHit::Up; result.row = row; return result;
        }
    }
    return result;
}

void applyFacePickClick(CharCreateScreenState& s, const FacePickClickTarget& target) {
    switch (target.hit) {
    case FacePickHit::Down: stepFacePick(s.facePick, target.row, -1); break;
    case FacePickHit::Up: stepFacePick(s.facePick, target.row, 1); break;
    case FacePickHit::Random: randomFacePick(s.facePick); break;
    case FacePickHit::Accept: acceptFacePick(s); break;
    case FacePickHit::Cancel: cancelFacePick(s); break;
    case FacePickHit::Blocked:
    case FacePickHit::None: break;
    }
}

}  // namespace Graphics
}  // namespace EQT
