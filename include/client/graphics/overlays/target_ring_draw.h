/*
 * The 3D target ring's draw call (B994) -- see target_ring.h. Separate from
 * the device-free geometry so a test of the geometry links no driver.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/overlays/target_ring.h"

namespace EQT {
namespace Graphics {
namespace TargetRing {

/** Draw `g` with the current view/projection (identity world). */
void drawGeometry(irr::video::IVideoDriver* driver, const Geometry& g);

}  // namespace TargetRing
}  // namespace Graphics
}  // namespace EQT
