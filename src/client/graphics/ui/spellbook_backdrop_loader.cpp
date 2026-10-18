/*
 * SpellBookBackdropLoader -- runtime loader for the composed spellbook
 * backdrop (B1076, GitLab #396). See the header for the contract.
 */

#include "client/graphics/ui/spellbook_backdrop_loader.h"

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/ui/spellbook_backdrop.h"
#include "client/core/memory_ledger.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

namespace Backdrop = SpellBookBackdrop;

bool SpellBookBackdropLoader::load(ConstrainedTextureCache* cache) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::UI);  // T491
    if (loaded_) return true;
    if (!cache) return false;

    // Same two-step every pack-backed texture takes (UIAtlas::load,
    // ItemIconSheets::load): getOrLoad first, then a bare uploadTexture for
    // the case where the entry is present but not cached. getOrLoad() itself
    // exits the process on a pack MISS, so the null arm below is only
    // reachable with no EQE1 loader configured at all.
    const char* name = Backdrop::kPackName;
    std::vector<char> emptyData;
    irr::video::ITexture* tex = cache->getOrLoad(name, emptyData);
    if (!tex) tex = cache->uploadTexture(name);
    if (!tex) {
        LOG_WARN(MOD_GRAPHICS,
                 "SpellBookBackdrop: no texture for '{}'; the book will "
                 "fall back to a flat panel",
                 name);
        return false;
    }
    // T202's rule: renderSpellbookPopup() holds a raw pointer across a
    // frame and this is a zone-scoped LRU.
    cache->protectTexture(name);
    texture_ = tex;
    loaded_ = true;
    return true;
}

}  // namespace Graphics
}  // namespace EQT
