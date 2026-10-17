/*
 * SpellIconSheets -- the runtime side of
 * data/textures/spell_icons{gems,spells,buff}.bmp + spell_icon_holder.bmp.
 *
 * B1077 (GitLab #397). Sibling of ItemIconSheets: four more ETC1 pack
 * entries, loaded from `willeq.eqe1` through the same
 * ConstrainedTextureCache, addressed by the shared layout header
 * `client/graphics/ui/spell_icon_atlas.h`.
 *
 * EAGER, exactly like UIAtlas and ItemIconSheets, for the same measured
 * reason (T430): a pack MISS is fatal by design (`ConstrainedTextureCache::
 * getOrLoad()` FATALs and exits on an absent entry), so lazy loading would
 * convert a startup failure into a mid-SESSION exit the moment a gem bar
 * asks for an icon. The sheets load beside ui_atlas, and a stale pack fails
 * where every other stale-pack failure already does.
 *
 * PROTECTED once uploaded (T202): the panels keep a raw ITexture* for the
 * life of a draw and the cache is a zone-scoped LRU that would otherwise
 * evict them mid-frame.
 *
 * lookup() returning null is still a normal state -- an unloaded instance
 * (headless tests build one with no cache) and every id a family does not
 * carry (negative, or at/above the family's count) land there -- so every
 * caller keeps its pre-B1077 text fallback.
 */

#pragma once

#include "client/graphics/ui/spell_icon_atlas.h"

#include <irrlicht.h>
#include <cstdint>

namespace EQT {
namespace Graphics {

class ConstrainedTextureCache;

class SpellIconSheets {
public:
    SpellIconSheets() = default;
    ~SpellIconSheets() = default;

    /**
     * Upload and protect all three family atlases plus the holder. Called
     * by UIAtlas::load(). Returns false only when there is no cache at all;
     * a pack MISSING an entry never returns here -- getOrLoad() exits the
     * process, which is the contract for an incomplete EQE1 pack.
     */
    bool load(ConstrainedTextureCache* cache);

    /**
     * The texture holding the icon, with `srcRect` set to the part of its
     * 40x40 cell that carries art (the native 36x28 gem cell for Gems, the
     * full 40x40 for Spells and Buff, both at the cell's (0,0) origin), or
     * nullptr if the sheets were never loaded or the id carries no art.
     * Pure lookup -- it touches no cache and cannot fail the process.
     */
    irr::video::ITexture* lookup(SpellIconAtlasLayout::SpellIconSheet sheet,
                                 uint32_t id,
                                 irr::core::rect<irr::s32>& srcRect) const;

    /**
     * The 40x40 CW_SpellGemHolder piece (its 36x28 art at the (0,0) origin),
     * or nullptr if the sheets were never loaded.
     */
    irr::video::ITexture* holder(irr::core::rect<irr::s32>& srcRect) const;

    /** True once every entry (three families + holder) is resident. */
    bool isLoaded() const { return loaded_; }

private:
    irr::video::ITexture* sheets_[static_cast<int>(
        SpellIconAtlasLayout::SpellIconSheet::Count)] = {};
    irr::video::ITexture* holder_ = nullptr;
    bool loaded_ = false;
};

}  // namespace Graphics
}  // namespace EQT
