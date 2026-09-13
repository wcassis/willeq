// T460 (board B520): see include/client/graphics/software_cursor_texture.h.

#include "client/graphics/software_cursor_texture.h"

#include "client/graphics/constrained_texture_cache.h"

namespace EQT {
namespace Graphics {

irr::video::ITexture* AcquireSoftwareCursorTexture(ConstrainedTextureCache& cache,
                                                   const std::vector<char>& bmpBytes) {
    // B520: protected BEFORE it enters the cache, like ui_atlas -- so neither
    // clear() (zone unload) nor evictUntilAvailable() (LRU) can destroy it.
    // Protection rather than reload-on-demand: the cursor is a process-lifetime
    // UI asset needed in every zone, it is 8x10 pixels, and a reload would put
    // a pack lookup (whose miss is exit(1)) on the frame path.
    cache.protectTexture(kSoftwareCursorCacheName);
    // T413 (board B285): the literal is the PACK's entry name -- see
    // tests/test_software_cursor_pack_name.cpp, which reads it out of this
    // call. getOrLoad() returns nullptr on an EQE1 hit or a fresh decode;
    // uploadTexture() promotes either.
    irr::video::ITexture* tex = cache.getOrLoad("software_cursor", bmpBytes);
    if (!tex) tex = cache.uploadTexture(kSoftwareCursorCacheName);
    return tex;
}

}  // namespace Graphics
}  // namespace EQT
