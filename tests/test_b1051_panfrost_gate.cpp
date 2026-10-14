// B1051 (GitLab #371): the Panfrost ETC1 companion-alpha gate.
//
// Two pure decision functions live in include/client/graphics/panfrost_gate.h
// (isPanfrostRenderer over the GL_RENDERER string, and selectAlphaVariant, the
// per-material table applyAlphaTestMaterialType now runs). This suite pins
// both so the x86 gate goldens stay byte-identical (the non-Panfrost arm of the
// table MUST reproduce the pre-B1051 if/else exactly) and so the Panfrost arm
// cannot be "simplified" back into a unit-0 collapse.
//
// The GL_RENDERER strings are measured, not invented:
//   - "Mali-T620 (Panfrost)" is the XU4's Mesa 25.2.8 Panfrost, verbatim from
//     the [gldriver] line in the B1051 trace (client.log).
//   - "Mali-T6xx"/"Mali-T7xx" family prefixes fire for Mesa releases that
//     report the board name without the driver name.
//   - Lima (Orange Pi / Rock64) and llvmpipe (x86 dev VM) are the two
//     non-Panfrost families this client actually runs on; both must classify
//     as NOT Panfrost or the gate would change their behaviour (the byte-
//     identical goldens already prove it, the unit tests pin it to a string).

#include <gtest/gtest.h>

#include "client/graphics/panfrost_gate.h"

using EQT::Graphics::AlphaVariant;
using EQT::Graphics::isPanfrostRenderer;
using EQT::Graphics::selectAlphaVariant;

// ---------------------------------------------------------------------------
// isPanfrostRenderer — the GL_RENDERER classifier.
// ---------------------------------------------------------------------------

TEST(B1051Panfrost, Xu4ShippedMesaFires) {
    // Measured: the [gldriver] line in the B1051 trace (client.log, XU4,
    // Mesa 25.2.8 Panfrost) reports exactly this.
    EXPECT_TRUE(isPanfrostRenderer("Mali-T620 (Panfrost)"));
}

TEST(B1051Panfrost, MidgardFamilyPrefixFires) {
    // Mesa releases that name the board without the driver still land on the
    // Midgard generation.
    EXPECT_TRUE(isPanfrostRenderer("Mali-T628"));
    EXPECT_TRUE(isPanfrostRenderer("Mali-T720"));
}

TEST(B1051Panfrost, CaseInsensitiveSubstringFires) {
    // Mesa has reported both casings; the classifier lowercases before the
    // substring search.
    EXPECT_TRUE(isPanfrostRenderer("Mali-T628 (Panfrost)"));
    EXPECT_TRUE(isPanfrostRenderer("MALI-T620 (PANFROST)"));
}

TEST(B1051Panfrost, BifrostDoesNotFire) {
    // The Orange Pi's Mali-400 is Bifrost — Lima reports it, and the family
    // prefix is "Mali-4", not "Mali-T6"/"Mali-T7". Must NOT gate.
    EXPECT_FALSE(isPanfrostRenderer("Mali-400 MP"));
    EXPECT_FALSE(isPanfrostRenderer("Mali-450"));
}

TEST(B1051Panfrost, LimaDoesNotFire) {
    EXPECT_FALSE(isPanfrostRenderer("lima"));
    EXPECT_FALSE(isPanfrostRenderer("Mesa/lima"));
}

TEST(B1051Panfrost, LlvmPipeDoesNotFire) {
    // The x86 dev VM runs llvmpipe — its GL_RENDERER carries no "panfrost"
    // substring and no Mali prefix, so the x86 path is byte-identical to
    // pre-B1051 (the goldens prove it; this pins the string).
    EXPECT_FALSE(isPanfrostRenderer("llvmpipe (Mesa 25.2.8)"));
}

TEST(B1051Panfrost, UnknownIsEmpty) {
    // No GL_RENDERER captured (the empty string) classifies as NOT Panfrost,
    // which is the conservative default: the gate only fires on a positively
    // identified Midgard driver.
    EXPECT_FALSE(isPanfrostRenderer(""));
}

// ---------------------------------------------------------------------------
// selectAlphaVariant — the per-material decision table.
// ---------------------------------------------------------------------------

namespace {
// The families this client actually runs on, as GL_RENDERER strings.
const std::string kPanfrost = "Mali-T620 (Panfrost)";
const std::string kLima = "lima";
const std::string kUnknown = "";
const std::string kLlvmPipe = "llvmpipe (Mesa 25.2.8)";
}  // namespace

TEST(B1051Variant, PanfrostEtc1RoutesToMainAlpha) {
    // THE B1051 FIX: on Panfrost, an ETC1-masked part (isETC1=true) with the
    // RGBA variant available (hasMainAlpha=true) is routed to the
    // single-sampler MainAlpha variant instead of the dual-sampler Companion
    // one. On ETC1 storage uTexture.a is 1.0, so the discard never fires and
    // the part draws opaque with its own texture — the working behaviour.
    EXPECT_EQ(selectAlphaVariant(kPanfrost, /*isETC1=*/true, /*hasMainAlpha=*/true),
              AlphaVariant::MainAlpha);
}

TEST(B1051Variant, PanfrostEtc1FallsBackToCompanionWithoutMainAlpha) {
    // If the RGBA variant is not wired (shader compile failed), the Panfrost
    // ETC1 part keeps the companion variant — identical to pre-B1051, so the
    // gate can never make things worse.
    EXPECT_EQ(selectAlphaVariant(kPanfrost, /*isETC1=*/true, /*hasMainAlpha=*/false),
              AlphaVariant::Companion);
}

TEST(B1051Variant, PanfrostNonEtc1UsesMainAlpha) {
    // Non-ETC1 on Panfrost is unchanged from pre-B1051: the single-sampler
    // variant reads the texture's own alpha (the H02d path).
    EXPECT_EQ(selectAlphaVariant(kPanfrost, /*isETC1=*/false, /*hasMainAlpha=*/true),
              AlphaVariant::MainAlpha);
    // ...and falls through to the caller's companion-type fallback when the
    // RGBA variant is unavailable (matches the old else-if chain).
    EXPECT_EQ(selectAlphaVariant(kPanfrost, /*isETC1=*/false, /*hasMainAlpha=*/false),
              AlphaVariant::MainAlpha);
}

TEST(B1051Variant, NonPanfrostEtc1KeepsCompanion) {
    // The byte-identical arm: on Lima / llvmpipe / unknown, an ETC1-masked
    // part keeps the dual-sampler companion variant exactly as pre-B1051.
    // This is what keeps the x86 gate goldens identical.
    EXPECT_EQ(selectAlphaVariant(kLima, /*isETC1=*/true, /*hasMainAlpha=*/true),
              AlphaVariant::Companion);
    EXPECT_EQ(selectAlphaVariant(kUnknown, /*isETC1=*/true, /*hasMainAlpha=*/false),
              AlphaVariant::Companion);
    EXPECT_EQ(selectAlphaVariant(kLlvmPipe, /*isETC1=*/true, /*hasMainAlpha=*/true),
              AlphaVariant::Companion);
}

TEST(B1051Variant, NonPanfrostNonEtc1UsesMainAlpha) {
    // Non-ETC1 off-Panfrost is unchanged: MainAlpha when available.
    EXPECT_EQ(selectAlphaVariant(kLima, /*isETC1=*/false, /*hasMainAlpha=*/true),
              AlphaVariant::MainAlpha);
    // ...and the caller's companion-type fallback when it is not (old else-if).
    EXPECT_EQ(selectAlphaVariant(kUnknown, /*isETC1=*/false, /*hasMainAlpha=*/false),
              AlphaVariant::MainAlpha);
}
