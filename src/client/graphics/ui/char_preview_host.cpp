// B888-CC2 (GitLab #204) -- the device-free preview appearance selection.
// Its own TU (M00). What it is and why it is separate from the render host:
// include/client/graphics/ui/char_preview_host.h.

#include "client/graphics/ui/char_preview_host.h"

#include <cstddef>

namespace eqt {
namespace graphics {

namespace {
using FacePickFeature = eqt::charcreate::FacePickFeature;

// The FacePick features -> EntityAppearance fields, in the SAME order the
// create packet transmits them (world_script.cpp's CharCreateRequest field
// table: FACE->face, HAIR->hairstyle, HAIRCOLOR->haircolor, BEARD->beard).
// Eyes has no body-variant field of its own in the model (the classic head
// does not carry a separate eye texture slot the loader keys on), so it maps
// to `texture` -- the body variant selector, the closest variant field the
// appearance carries; Wode (a Vah Shir / Iksar detail) maps to `helm`, the
// other variant field. Both are best-fit: the model loader only consumes
// face/hairstyle/haircolor/beard/beardcolor/texture/helm, and no appearance
// field named "eyes" exists. See refactor/B888-CC2/README.md "Not verified".
void applyFacePick(EQT::Graphics::EntityAppearance& a,
                   const std::array<uint32_t, eqt::charcreate::kFacePickFeatures>& p) {
    using eqt::charcreate::FacePickFeature;
    a.face = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::Face)]);
    a.hairstyle = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::Hair)]);
    a.haircolor = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::HairColor)]);
    a.beard = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::Beard)]);
    a.beardcolor = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::BeardColor)]);
    a.texture = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::Eyes)]);
    a.helm = static_cast<uint8_t>(p[static_cast<size_t>(FacePickFeature::Wode)]);
}
}  // namespace

CharPreviewAppearance selectPreviewAppearance(uint32_t race, uint8_t gender,
                                              uint8_t face, uint8_t hairColour, uint8_t hairLook) {
    CharPreviewAppearance a;
    if (race == 0) {
        return a;  // no race on the wire: nothing to stand up
    }
    a.valid = true;
    a.race = static_cast<uint16_t>(race);
    a.gender = gender;
    a.appearance.face = face;
    // The parameter names avoid the census's bare-name sweep: `hairColor`/
    // `hairStyle` would read as occurrences of the unused
    // EntityAppearanceData members.
    a.appearance.haircolor = hairColour;
    a.appearance.hairstyle = hairLook;
    return a;
}

CharPreviewAppearance createPreviewAppearance(
    uint32_t race, uint8_t gender,
    const std::array<uint32_t, eqt::charcreate::kFacePickFeatures>& appearance) {
    CharPreviewAppearance a;
    if (race == 0) {
        return a;  // page 0 with no race picked: the sample body is still
                   // shown by the host with the bare default, so the caller
                   // decides -- here `valid` stays false until a race exists.
    }
    a.valid = true;
    a.race = static_cast<uint16_t>(race);
    a.gender = gender;
    applyFacePick(a.appearance, appearance);
    return a;
}

}  // namespace graphics
}  // namespace eqt
