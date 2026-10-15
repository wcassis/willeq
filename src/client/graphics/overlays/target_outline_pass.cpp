// B1064 (#384): the target outline's GL state decisions -- see the header.

#include "client/graphics/overlays/target_outline_pass.h"

namespace EQT {
namespace Graphics {
namespace TargetOutlinePass {

Plan plan(int /*stencilBits*/) {
	Plan p;
	p.usesStencil = false;
	p.depthPrepass = true;
	p.cull = Cull::Front;
	p.scale = kScale;
	p.depthWrite = false;
	p.depthLessEqual = true;
	return p;
}

irr::core::matrix4 outlineTransform(const irr::core::matrix4& normalTransform, float scale) {
	const irr::core::vector3df pos = normalTransform.getTranslation();
	irr::core::matrix4 toOrigin, fromOrigin, scaleMat;
	toOrigin.setTranslation(-pos);
	fromOrigin.setTranslation(pos);
	scaleMat.setScale(irr::core::vector3df(scale, scale, scale));
	return fromOrigin * scaleMat * toOrigin * normalTransform;
}

} // namespace TargetOutlinePass
} // namespace Graphics
} // namespace EQT
