// B1051 (GitLab #371): Panfrost/Mali-Midgard ETC1 companion-alpha gate.
//
// On the XU4 (Mali-T620, Mesa 25.2.8 Panfrost), a multi-buffer character
// (Lieutenant_Arathur, race 71, 13 mesh buffers) + the player's held weapon
// rendered as ONE flat mottled texture over the whole model in the ETC1
// constrained config, while the raw-RGBA config rendered the 4+ distinct
// materials correctly. Take 3 isolated the fault to the client ETC1 path;
// this unit's driver-side trace (WILLEQ_B1051_TRACE, COpenGLES2Driver.cpp)
// showed the GL state is NOT the collapse take 3 hypothesized: under the
// ETC1 companion-alpha material the character's masked parts bind DISTINCT
// unit-0 objects and DISTINCT unit-1 companion objects per frame (the
// hardware-seen readback, glGetString), so the per-buffer binds are correct.
// The one render-path difference between the smearing ETC1 config and the
// correct RGBA config is the shader stage the masked parts run: the
// dual-sampler companion-alpha variant (samples a unit-1 ETC1 companion +
// constant-threshold discard) vs the single-sampler variant that reads
// uTexture.a directly. selectAlphaVariant() routes ETC1-masked entity
// materials to the single-sampler variant on Panfrost, where it draws
// opaque (ETC1 storage is RGB-only, so uTexture.a is 1.0 and the discard
// never fires) — the working behaviour — instead of the dual-sampler+
// discard variant.
//
// The classification is a pure string test over the GL_RENDERER string the
// driver already captures (gles2GetGlRenderer, B946's [gldriver] line), so
// it is unit-testable on x86 (tests/test_b1051_panfrost_gate.cpp).

#ifndef EQT_GRAPHICS_PANFROST_GATE_H
#define EQT_GRAPHICS_PANFROST_GATE_H

#include <string>

namespace EQT {
namespace Graphics {

// True when the GL_RENDERER string identifies the Mesa Panfrost driver
// (the open-source Mali-Midgard driver). The XU4's shipped Mesa 25.2.8
// Panfrost reports "Mali-T620 (Panfrost)" (measured: the B1051 [gldriver]
// line), and other Mesa releases have reported bare Midgard family names
// ("Mali-T628", the "Mali-T7xx" pre-board placeholder). The classifier
// fires on an explicit "panfrost" substring (case-insensitive — Mesa has
// reported both casings) OR a "Mali-T6"/"Mali-T7" prefix (Midgard
// generation; on these Linux boards that family is Panfrost-or-nothing).
// A Lima or llvmpipe GL_RENDERER carries neither, so it never fires.
inline bool isPanfrostRenderer(const std::string& glRenderer) {
    // Arm 1: an explicit "panfrost" substring, wherever it sits.
    {
        std::string lower;
        lower.reserve(glRenderer.size());
        for (unsigned char c : glRenderer)
            lower.push_back(static_cast<char>(
                (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
        if (lower.find("panfrost") != std::string::npos)
            return true;
    }
    // Arm 2: the Midgard family prefix, for Mesa releases that report the
    // board name without the driver name.
    if (glRenderer.rfind("Mali-T6", 0) == 0 || glRenderer.rfind("Mali-T7", 0) == 0)
        return true;
    return false;
}

// The alpha-test variant applyAlphaTestMaterialType() must pick for a
// masked mesh-buffer material.
enum class AlphaVariant {
    // Companion variant (mat=alphaTest): the dual-sampler shader that
    // samples a unit-1 ETC1 companion + constant-threshold discard, and
    // the caller binds the companion on unit 1.
    Companion,
    // Main-alpha variant (mat=alphaTestRGBA): the single-sampler shader
    // that reads uTexture.a directly and discards below 0.5. On ETC1
    // storage uTexture.a is 1.0, so the discard never fires and the part
    // draws opaque with its own texture — the working behaviour on
    // Panfrost. When the RGBA type is not wired, the caller falls back to
    // the companion TYPE without binding a companion (the pre-B1051
    // else-if behaviour).
    MainAlpha,
};

// The decision table. On Panfrost, ETC1-masked parts are routed to
// MainAlpha (the B1051 fix): the single-sampler variant is the one
// render-path difference from the correct raw-RGBA config, and the driver
// trace proved the per-buffer binds are correct, so the dual-sampler
// companion-alpha stage is the post-bind fault. When the RGBA variant is
// not available (hasMainAlpha false) ETC1 parts keep the companion variant
// — identical to the pre-B1051 code.
//
// Everywhere else (Lima, llvmpipe, unknown) the table is BYTE-IDENTICAL to
// the pre-B1051 if/else in applyAlphaTestMaterialType(): ETC1 -> Companion
// (the x86 gate goldens prove it), non-ETC1 -> MainAlpha when available,
// otherwise the companion TYPE (modelled as MainAlpha without the RGBA
// type — the caller's fallback applies it, exactly the old else-if chain).
inline AlphaVariant selectAlphaVariant(const std::string& glRenderer,
                                       bool isETC1, bool hasMainAlpha) {
    if (isPanfrostRenderer(glRenderer)) {
        return (isETC1 && !hasMainAlpha) ? AlphaVariant::Companion
                                         : AlphaVariant::MainAlpha;
    }
    // Not Panfrost: the pre-B1051 decision, unchanged.
    return isETC1 ? AlphaVariant::Companion : AlphaVariant::MainAlpha;
}

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_PANFROST_GATE_H
