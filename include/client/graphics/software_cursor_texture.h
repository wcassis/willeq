// T460 (board B520): the DRM/KMS software cursor's texture, acquired from the
// ConstrainedTextureCache in ONE place so the lifetime rule it needs cannot be
// dropped by a caller.
//
// THE RULE. IrrlichtRenderer::softwareCursorTexture_ is a raw ITexture* drawn
// every frame for the life of the process (FrameComponent::renderFrameTail),
// on the only cursor the Orange Pi has. It is not a zone asset. But it lives in
// the constrained cache, and that cache destroys every texture nobody
// protected on zone unload (ConstrainedTextureCache::clear(), from
// unloadZone()) and on LRU pressure (evictUntilAvailable()). The P10b
// SingleTexturePointerGuard then nulls the renderer's pointer -- which stops
// the use-after-free and, with nothing ever reloading it, also stops the
// cursor from EVER being drawn again: the cursor vanished at the first zone
// unload (Will, on the Pi).
//
// So the cursor is protected exactly like the UI atlas and the item icon
// sheets (UIAtlas::load(), ItemIconSheets): the same class of asset, with the
// same raw-pointer owner. The guard stays registered as a second net.
//
// Lives in its own TU (not IrrlichtRenderer) so a test can drive the real
// acquisition against a real cache without a renderer.

#pragma once

#include <vector>

namespace irr { namespace video { class ITexture; } }

namespace EQT {
namespace Graphics {

class ConstrainedTextureCache;

// The cache name. It is the PACK's name (T413, board B285): the flat-pack
// entry built from data/textures/software_cursor.bmp is "software_cursor".
inline constexpr const char* kSoftwareCursorCacheName = "software_cursor";

// Load (EQE1 hit or raw BMP decode), upload, and protect the cursor texture.
// Returns nullptr only if both getOrLoad() and uploadTexture() fail; the
// caller decides what that means (createSoftwareCursor(): fatal).
irr::video::ITexture* AcquireSoftwareCursorTexture(ConstrainedTextureCache& cache,
                                                   const std::vector<char>& bmpBytes);

}  // namespace Graphics
}  // namespace EQT
