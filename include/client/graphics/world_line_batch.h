#ifndef EQT_GRAPHICS_WORLD_LINE_BATCH_H
#define EQT_GRAPHICS_WORLD_LINE_BATCH_H

// T335 (refactor/T335_debug_overlay_siblings.md, B054) -- the one place the
// T333 fix (B048) is spelled, so the four debug overlays that draw world-space
// lines after smgr->drawAll() share it instead of each carrying a copy.
//
// THE DEFECT THIS GUARDS AGAINST, measured in T333 and reproduced headlessly:
//
// By the time the debug-overlay pass runs, 2D drawing has already happened this
// frame (billboards, GUI nodes and the target box, during and just after
// smgr->drawAll()). COpenGLES2Driver LATCHES that: setOrthoProjection() sets
// Is2DMode, overwrites Matrices[ETS_VIEW]/[ETS_PROJECTION] with an ortho screen
// matrix in PIXEL units, and then early-returns on every later call while the
// latch is set. Only beginScene(), setRenderTarget() and restore3DProjection()
// clear it -- setMaterial(), setTransform() and draw3DLine() do not. Two
// consequences, one symptom each:
//
//   * world-space lines drawn without re-setting ETS_VIEW/ETS_PROJECTION are
//     multiplied by that ortho matrix and land far outside the screen box --
//     "the overlay draws nothing"; and
//   * setMaterial() with a 3D material re-enables back-face culling (SMaterial's
//     default) and the depth test, and the 2D pass that FOLLOWS cannot undo it,
//     because setOrthoProjection() is the only caller of setCull(false) /
//     setDepthTest(false) and it early-returns while latched. Every UI quad is
//     then back-face culled away -- "the 2D UI vanishes for the rest of the
//     frame". Measured: flipping BackfaceCulling alone brings the UI back.
//
// So a batch borrows the driver's transforms and render state, and hands back
// exactly what it found. Construct one on the stack before the draw3DLine()
// loop; the destructor does the restore, so an early return cannot skip it.

#include "client/graphics/renderer_context.h"
#include "client/graphics/frame/frame_component.h"  // have3DTransforms()

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

class WorldLineBatch {
public:
	// Saves the three transforms, applies `lineMaterial`, and installs the
	// view/projection pair captured during the ESNRP_SOLID pass -- gated on
	// have3DTransforms(), which is drawTargetOutline()'s idiom.
	WorldLineBatch(RendererContext& ctx, const irr::video::SMaterial& lineMaterial)
		: driver_(ctx.driver) {
		if (!driver_) return;
		savedView_ = driver_->getTransform(irr::video::ETS_VIEW);
		savedProj_ = driver_->getTransform(irr::video::ETS_PROJECTION);
		savedWorld_ = driver_->getTransform(irr::video::ETS_WORLD);

		driver_->setMaterial(lineMaterial);

		if (ctx.frameComponent && ctx.Frame().have3DTransforms()) {
			driver_->setTransform(irr::video::ETS_VIEW, ctx.captured3DView);
			driver_->setTransform(irr::video::ETS_PROJECTION, ctx.captured3DProj);
		}
	}

	~WorldLineBatch() {
		if (!driver_) return;

		// The transforms restore literally. The render state has to be spelled
		// as a material, because setMaterial() is the only public door to
		// setCull()/setDepthTest()/setDepthWrite(). This one reproduces
		// setOrthoProjection()'s 2D state exactly: no culling, no depth test
		// (ECFN_NEVER), no depth write. The depth write comes from the material
		// TYPE -- COpenGLES2Driver::applyMaterialRenderState() switches on
		// MaterialType and IGNORES ZWriteEnable -- which is why the type here is
		// EMT_TRANSPARENT_ALPHA_CHANNEL (its branch is the depth-write-off one)
		// and not EMT_SOLID. Blending is set explicitly by every 2D draw call,
		// so it does not matter which way this leaves it.
		driver_->setTransform(irr::video::ETS_WORLD, savedWorld_);
		driver_->setTransform(irr::video::ETS_VIEW, savedView_);
		driver_->setTransform(irr::video::ETS_PROJECTION, savedProj_);

		irr::video::SMaterial restore2D;
		restore2D.Lighting = false;
		restore2D.BackfaceCulling = false;
		restore2D.ZBuffer = irr::video::ECFN_NEVER;
		restore2D.ZWriteEnable = false;
		restore2D.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
		driver_->setMaterial(restore2D);
	}

	WorldLineBatch(const WorldLineBatch&) = delete;
	WorldLineBatch& operator=(const WorldLineBatch&) = delete;

private:
	irr::video::IVideoDriver* driver_ = nullptr;
	irr::core::matrix4 savedView_;
	irr::core::matrix4 savedProj_;
	irr::core::matrix4 savedWorld_;
};

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_WORLD_LINE_BATCH_H
