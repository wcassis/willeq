/*
 * ItemIconSheets -- the runtime side of data/textures/item_icons{0,1}.bmp.
 *
 * T430 (board B376). Sibling of UIAtlas: two more ETC1 pack entries, loaded
 * from `willeq.eqe1` through the same ConstrainedTextureCache, addressed by
 * the shared layout header `client/graphics/ui/item_icon_atlas.h`.
 *
 * EAGER, exactly like UIAtlas, and the reason is not taste. A pack MISS is
 * fatal by design: `ConstrainedTextureCache::getOrLoad()` logs FATAL and
 * `std::exit(1)`s when EQE1 is configured and an entry is absent (Will's
 * all-or-nothing rule for EQE1 assets). T430 first wrote this lazily -- load
 * on the first icon drawn, to keep 2 x 1024x1024 off a 512 MB Orange Pi that
 * never opens an inventory -- and MEASURED the consequence: against a
 * pre-T430 pack the process died the moment a panel asked for an icon. Lazy
 * loading converts a startup failure into a mid-SESSION exit, which is
 * strictly worse. So the sheets load beside ui_atlas, and a stale pack fails
 * where every other stale-pack failure already does.
 *
 * They are PROTECTED once uploaded, for the same reason ui_atlas is (T202):
 * the panels keep a raw ITexture* for the life of a draw and the cache is a
 * zone-scoped LRU that would otherwise evict them mid-frame.
 *
 * lookup() returning null is still a normal state -- an unloaded instance
 * (headless tests build one with no cache) and every icon id outside
 * 500..1723 land there -- so every caller keeps its pre-T430 text fallback.
 */

#pragma once

#include "client/graphics/ui/item_icon_atlas.h"

#include <irrlicht.h>
#include <cstdint>

namespace EQT {
namespace Graphics {

class ConstrainedTextureCache;

class ItemIconSheets {
public:
    ItemIconSheets() = default;
    ~ItemIconSheets() = default;

    /**
     * Upload and protect both sheets. Called by UIAtlas::load(). Returns
     * false only when there is no cache at all; a pack that is MISSING an
     * entry never returns here -- getOrLoad() exits the process, which is
     * the contract for an incomplete EQE1 pack.
     */
    bool load(ConstrainedTextureCache* cache);

    /**
     * The texture holding `iconId`, with `srcRect` set to its 40x40 cell, or
     * nullptr if the sheets were never loaded or the id carries no art.
     * Pure lookup -- it touches no cache and cannot fail the process.
     */
    irr::video::ITexture* lookup(uint32_t iconId,
                                 irr::core::rect<irr::s32>& srcRect) const;

    /** True once the sheets are resident. */
    bool isLoaded() const { return loaded_; }

private:
    irr::video::ITexture* sheets_[ItemIconAtlasLayout::kAtlasCount] = {};
    bool loaded_ = false;
};

}  // namespace Graphics
}  // namespace EQT
