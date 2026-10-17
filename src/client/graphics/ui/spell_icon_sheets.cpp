/*
 * SpellIconSheets -- runtime loader for the spell icon atlases (B1077,
 * GitLab #397). See include/client/graphics/ui/spell_icon_sheets.h for the
 * contract and include/client/graphics/ui/spell_icon_atlas.h for the layout.
 */

#include "client/graphics/ui/spell_icon_sheets.h"

#include "client/graphics/constrained_texture_cache.h"
#include "client/core/memory_ledger.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

namespace Layout = SpellIconAtlasLayout;
using Layout::SpellIconSheet;

bool SpellIconSheets::load(ConstrainedTextureCache* cache) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::UI);  // T491
    if (loaded_) return true;
    if (!cache) return false;

    // Same two-step every pack-backed texture takes (UIAtlas::load,
    // ItemIconSheets::load): getOrLoad first, then a bare uploadTexture for
    // the case where the entry is present but not cached. getOrLoad() itself
    // exits the process on a pack MISS, so the null arm below is only
    // reachable with no EQE1 loader configured at all.
    std::vector<char> emptyData;
    int resident = 0;
    for (int i = 0; i < static_cast<int>(SpellIconSheet::Count); ++i) {
        const char* name = Layout::atlasName(static_cast<SpellIconSheet>(i));
        irr::video::ITexture* tex = cache->getOrLoad(name, emptyData);
        if (!tex) tex = cache->uploadTexture(name);
        if (!tex) {
            LOG_WARN(MOD_GRAPHICS,
                     "SpellIconSheets: no texture for '{}'; spell icons "
                     "will fall back to their text placeholders",
                     name);
            continue;
        }
        // T202's rule: the panels hold a raw pointer across a frame and this
        // is a zone-scoped LRU.
        cache->protectTexture(name);
        sheets_[i] = tex;
        ++resident;
    }

    {
        const char* name = Layout::holderName();
        irr::video::ITexture* tex = cache->getOrLoad(name, emptyData);
        if (!tex) tex = cache->uploadTexture(name);
        if (!tex) {
            LOG_WARN(MOD_GRAPHICS,
                     "SpellIconSheets: no texture for '{}'; the gem bar "
                     "will fall back to its flat slot background",
                     name);
        } else {
            cache->protectTexture(name);
            holder_ = tex;
            ++resident;
        }
    }

    loaded_ = resident == static_cast<int>(SpellIconSheet::Count) + 1;
    if (loaded_) {
        LOG_INFO(MOD_GRAPHICS,
                 "SpellIconSheets: loaded {} spell icon sheets ({} gems, "
                 "{} spells, {} buff icons, holder)",
                 resident, Layout::kGemIconCount, Layout::kSpellIconCount,
                 Layout::kBuffIconCount);
    }
    return loaded_;
}

irr::video::ITexture* SpellIconSheets::lookup(
    SpellIconSheet sheet, uint32_t id,
    irr::core::rect<irr::s32>& srcRect) const {
    if (!loaded_) return nullptr;
    const int index = Layout::indexForId(sheet, id);
    if (index < 0) return nullptr;

    irr::video::ITexture* tex =
        sheets_[static_cast<int>(sheet)];
    if (!tex) return nullptr;

    int x = 0, y = 0;
    Layout::atlasCellOrigin(sheet, id, x, y);
    int w = Layout::kCellSize, h = Layout::kCellSize;
    if (sheet == SpellIconSheet::Gems) {
        w = Layout::kGemCellW;
        h = Layout::kGemCellH;
    }
    srcRect = irr::core::rect<irr::s32>(x, y, x + w, y + h);
    return tex;
}

irr::video::ITexture* SpellIconSheets::holder(
    irr::core::rect<irr::s32>& srcRect) const {
    if (!loaded_ || !holder_) return nullptr;
    srcRect = irr::core::rect<irr::s32>(0, 0, Layout::kHolderW, Layout::kHolderH);
    return holder_;
}

}  // namespace Graphics
}  // namespace EQT
