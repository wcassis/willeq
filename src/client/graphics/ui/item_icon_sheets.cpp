/*
 * ItemIconSheets -- runtime loader for the item icon atlases (T430, B376).
 * See include/client/graphics/ui/item_icon_sheets.h for the contract and
 * include/client/graphics/ui/item_icon_atlas.h for the layout.
 */

#include "client/graphics/ui/item_icon_sheets.h"

#include "client/graphics/constrained_texture_cache.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"

namespace EQT {
namespace Graphics {

namespace Layout = ItemIconAtlasLayout;

bool ItemIconSheets::load(ConstrainedTextureCache* cache) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::UI);  // T491
    if (loaded_) return true;
    if (!cache) return false;

    // Same two-step every pack-backed texture takes (UIAtlas::load,
    // ParticleManager::loadSpellSpriteAtlas): getOrLoad first, then a bare
    // uploadTexture for the case where the entry is present but not cached.
    // getOrLoad() itself exits the process on a pack MISS, so the null arm
    // below is only reachable with no EQE1 loader configured at all.
    std::vector<char> emptyData;
    int resident = 0;
    for (int i = 0; i < Layout::kAtlasCount; ++i) {
        const char* name = Layout::atlasName(i);
        irr::video::ITexture* tex = cache->getOrLoad(name, emptyData);
        if (!tex) tex = cache->uploadTexture(name);
        if (!tex) {
            LOG_WARN(MOD_GRAPHICS,
                     "ItemIconSheets: no texture for '{}'; item icons will "
                     "fall back to their text placeholders",
                     name);
            continue;
        }
        // T202's rule: the panels hold a raw pointer across a frame and this
        // is a zone-scoped LRU.
        cache->protectTexture(name);
        sheets_[i] = tex;
        ++resident;
    }

    loaded_ = resident == Layout::kAtlasCount;
    if (loaded_) {
        LOG_INFO(MOD_GRAPHICS,
                 "ItemIconSheets: loaded {} icon sheets ({} icons, ids {}..{})",
                 resident, Layout::kIconCount, Layout::kFirstIconId,
                 Layout::kFirstIconId + Layout::kIconCount - 1);
    }
    return loaded_;
}

irr::video::ITexture* ItemIconSheets::lookup(
    uint32_t iconId, irr::core::rect<irr::s32>& srcRect) const {
    if (!loaded_) return nullptr;
    const int index = Layout::indexForIconId(iconId);
    if (index < 0) return nullptr;

    const int atlas = Layout::atlasForIndex(index);
    if (atlas < 0 || atlas >= Layout::kAtlasCount) return nullptr;
    irr::video::ITexture* tex = sheets_[atlas];
    if (!tex) return nullptr;

    int x = 0, y = 0;
    Layout::atlasCellOrigin(index, x, y);
    srcRect = irr::core::rect<irr::s32>(x, y, x + Layout::kCellSize,
                                        y + Layout::kCellSize);
    return tex;
}

}  // namespace Graphics
}  // namespace EQT
