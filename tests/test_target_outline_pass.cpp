// B1064 (#384) -- the target outline needs no stencil buffer.
//
// What is pinned:
//   * TargetOutlinePass::plan() never asks for the stencil, whatever the
//     surface has (0 planes is the orangepi preset since B971), and its pass
//     is the inverted hull: a depth-only pre-pass of the unscaled mesh (the
//     target's own depth, also when the scene drew it as an impostor), then
//     the scaled mesh with front faces culled, depth LESSEQUAL, no depth
//     write, the 1.10 scale;
//   * outlineTransform() composes the uniform scale about the entity origin
//     onto the draw's own world transform -- checked element by element
//     against hand-computed matrices;
//   * the overlay itself: OverlaysComponent::drawTargetOutline() takes its
//     state from plan() and contains no stencil call (on fable it wrote ref
//     128 and drew the inflated mesh where stencil != 128 -- the whole
//     silhouette on a stencil-less surface).

#include <gtest/gtest.h>

#include "client/graphics/overlays/target_outline_pass.h"

#include <irrlicht.h>
#include <fstream>
#include <sstream>
#include <string>

namespace TOP = EQT::Graphics::TargetOutlinePass;

namespace {

void expectMatrixNear(const irr::core::matrix4& got, const float want[16]) {
	for (int i = 0; i < 16; ++i)
		EXPECT_NEAR(got[i], want[i], 1e-4f) << "element " << i;
}

std::string drawTargetOutlineBody() {
	std::ifstream in(TARGET_OUTLINE_OVERLAY_SOURCE);
	if (!in) return {};
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string src = ss.str();
	const auto start = src.find("void OverlaysComponent::drawTargetOutline()");
	if (start == std::string::npos) return {};
	// The body ends at the next top-level definition or preprocessor arm.
	auto end = src.find("\n}\n", start);
	if (end == std::string::npos) return {};
	return src.substr(start, end - start);
}

} // namespace

TEST(TargetOutlinePass, NeverUsesTheStencilOnAnySurface) {
	for (int bits : {0, 1, 4, 8, 16, -1}) {
		const TOP::Plan p = TOP::plan(bits);
		EXPECT_FALSE(p.usesStencil) << "stencilBits=" << bits;
		EXPECT_TRUE(p.depthPrepass) << "stencilBits=" << bits;
		EXPECT_EQ(p.cull, TOP::Cull::Front) << "stencilBits=" << bits;
		EXPECT_FALSE(p.depthWrite) << "stencilBits=" << bits;
		EXPECT_TRUE(p.depthLessEqual) << "stencilBits=" << bits;
		EXPECT_FLOAT_EQ(p.scale, TOP::kScale) << "stencilBits=" << bits;
	}
}

TEST(TargetOutlinePass, TheSameSinglePathWithAndWithoutAStencil) {
	const TOP::Plan none = TOP::plan(0);
	const TOP::Plan s8 = TOP::plan(8);
	EXPECT_EQ(none.usesStencil, s8.usesStencil);
	EXPECT_EQ(none.depthPrepass, s8.depthPrepass);
	EXPECT_EQ(none.cull, s8.cull);
	EXPECT_EQ(none.depthWrite, s8.depthWrite);
	EXPECT_EQ(none.depthLessEqual, s8.depthLessEqual);
	EXPECT_FLOAT_EQ(none.scale, s8.scale);
}

TEST(TargetOutlinePass, ScaleIsAboutTheEntityOriginForATranslation) {
	// normalTransform = translate(10, 20, 30): p -> 1.1 p + t.
	irr::core::matrix4 normal;
	normal.setTranslation(irr::core::vector3df(10.f, 20.f, 30.f));
	const irr::core::matrix4 m = TOP::outlineTransform(normal, 1.10f);
	// Irrlicht is column-vector-in-rows: translation at [12..14].
	const float want[16] = {
		1.1f, 0, 0, 0,
		0, 1.1f, 0, 0,
		0, 0, 1.1f, 0,
		10.f, 20.f, 30.f, 1.f};
	expectMatrixNear(m, want);

	irr::core::vector3df p(1.f, 2.f, 3.f);
	m.transformVect(p);
	EXPECT_NEAR(p.X, 11.1f, 1e-4f);
	EXPECT_NEAR(p.Y, 22.2f, 1e-4f);
	EXPECT_NEAR(p.Z, 33.3f, 1e-4f);
}

TEST(TargetOutlinePass, ScaleComposesAfterTheNodesRotation) {
	// normalTransform = rotate 90 deg about Y, then translate (5, 0, -7).
	// Irrlicht's setRotationDegrees(0,90,0) maps +X -> -Z and +Z -> +X.
	// Hand-computed: p -> 1.1 * R p + t.
	irr::core::matrix4 normal;
	normal.setRotationDegrees(irr::core::vector3df(0.f, 90.f, 0.f));
	normal.setTranslation(irr::core::vector3df(5.f, 0.f, -7.f));
	const irr::core::matrix4 m = TOP::outlineTransform(normal, 1.10f);
	const float want[16] = {
		0, 0, -1.1f, 0,
		0, 1.1f, 0, 0,
		1.1f, 0, 0, 0,
		5.f, 0, -7.f, 1.f};
	expectMatrixNear(m, want);

	irr::core::vector3df x(1.f, 0.f, 0.f);
	m.transformVect(x);
	EXPECT_NEAR(x.X, 5.f, 1e-4f);
	EXPECT_NEAR(x.Y, 0.f, 1e-4f);
	EXPECT_NEAR(x.Z, -8.1f, 1e-4f);
}

TEST(TargetOutlinePass, ScaleOneIsTheDrawsOwnTransform) {
	irr::core::matrix4 normal;
	normal.setRotationDegrees(irr::core::vector3df(0.f, 37.f, 0.f));
	normal.setTranslation(irr::core::vector3df(-120.f, 4.f, 88.f));
	const irr::core::matrix4 m = TOP::outlineTransform(normal, 1.0f);
	float want[16];
	for (int i = 0; i < 16; ++i) want[i] = normal[i];
	expectMatrixNear(m, want);
}

TEST(TargetOutlinePass, TheOverlayDrawsThePlanAndTouchesNoStencil) {
	const std::string body = drawTargetOutlineBody();
	ASSERT_FALSE(body.empty()) << "drawTargetOutline() not found in " << TARGET_OUTLINE_OVERLAY_SOURCE;
	EXPECT_NE(body.find("TargetOutlinePass::plan("), std::string::npos);
	EXPECT_NE(body.find("TargetOutlinePass::outlineTransform("), std::string::npos);
	EXPECT_NE(body.find("plan.depthPrepass"), std::string::npos);
	EXPECT_EQ(body.find("GL_STENCIL_TEST"), std::string::npos);
	EXPECT_EQ(body.find("glStencilFunc"), std::string::npos);
	EXPECT_EQ(body.find("glStencilOp"), std::string::npos);
	EXPECT_EQ(body.find("STENCIL_REF"), std::string::npos);
}
