#pragma once

// B888-CC2 (GitLab #204): the pre-zone 3D PREVIEW -- device-free appearance
// selection. The select screen shows the selected character's body (race,
// gender, face from the T14 wire); the creation screen shows the sample model
// of the chosen race + gender (+ FacePick picks). This header is the seam the
// render host (char_preview_host.h) and the headless frame tests share: it
// decides WHICH race/gender/appearance to stand up, with no Irrlicht, no
// device, no loader -- so a test can prove the right body is chosen per state
// without building a model. The actual model stands up in
// src/client/graphics/ui/char_preview_host.cpp on a CharacterModelView.
//
// WHY A SEPARATE TU (M00): the select/creation state machines
// (char_select_screen.h / char_create_screen.h) are device-free; the 3D scene
// is graphics-only. This header is device-free so the "right model in the
// view" test can link against it with no Irrlicht at all.

#include "client/graphics/entity_renderer.h"
#include "client/login_world/char_create_faces.h"
#include "client/login_world/char_create_packets.h"

#include <cstdint>

namespace eqt {
namespace graphics {

/** The body to stand up, as the preview host feeds CharacterModelView. */
struct CharPreviewAppearance {
    bool valid = false;                 // false: nothing to show (no selection, no race)
    uint16_t race = 0;                  // the model race id
    uint8_t gender = 0;                 // 0 = Male, 1 = Female (charcreate::Gender)
    EQT::Graphics::EntityAppearance appearance;  // face/hair/beard (+ no equipment)
};

/**
 * The SELECT preview body: the selected row's race/gender/face from the T14
 * wire. `row` is the row the screen has selected (its race/gender/face are the
 * wire fields B888-CC2 added to CharacterSelectRow). No row -> invalid.
 */
CharPreviewAppearance selectPreviewAppearance(uint32_t race, uint8_t gender,
                                              uint8_t face, uint8_t hairColor, uint8_t hairStyle);

/**
 * The CREATION preview body: the sample model of the page-0 race + gender,
 * plus the committed FacePick picks (face, hair, hair-color, beard, beard-
 * color). `race` is the charcreate race id (the same value the create packet
 * carries, and the one RaceModelLoader keys on); a zero race or the not-yet-
 * picked default -> still valid, just the bare sample body. FacePick feature
 * values index the race's variant tables, exactly as the create packet
 * transmits them (the server resolves them the same way).
 */
CharPreviewAppearance createPreviewAppearance(
    uint32_t race, uint8_t gender,
    const std::array<uint32_t, eqt::charcreate::kFacePickFeatures>& appearance);

}  // namespace graphics
}  // namespace eqt
