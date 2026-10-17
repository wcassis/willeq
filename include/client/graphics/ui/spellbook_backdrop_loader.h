/*
 * SpellBookBackdropLoader -- runtime loader for
 * data/textures/spellbook_backdrop.bmp (pack entry `spellbook_backdrop`).
 * B1076 (GitLab #396).
 *
 * Sibling of SpellIconSheets: one more ETC1 pack entry, loaded from
 * `willeq.eqe1` through the same ConstrainedTextureCache, composed from the
 * official client's own book art by tools/spellbook_backdrop_builder and
 * addressed by the shared layout header client/graphics/ui/spellbook_backdrop.h
 * (the composed 512x363 canvas) and drawn per tile by
 * client/graphics/ui/spellbook_layout.h (the window's own four backdrop
 * rects).
 *
 * EAGER, exactly like UIAtlas and the icon sheets, for the same measured
 * reason (T430): a pack MISS is fatal by design (getOrLoad() FATALs and
 * exits), so lazy loading would convert a startup failure into a mid-SESSION
 * exit the moment a player opened the book. The texture loads beside
 * ui_atlas, and a stale pack fails where every other stale-pack failure
 * already does.
 *
 * PROTECTED once uploaded (T202): renderSpellbookPopup() keeps a raw ITexture*
 * for the life of a draw and the cache is a zone-scoped LRU that would
 * otherwise evict it mid-frame.
 *
 * texture() returning null is still a normal state -- an unloaded instance
 * (headless tests build one with no cache) -- so the render site keeps its
 * flat-panel fallback.
 */

#pragma once

#include <irrlicht.h>
#include <cstddef>

namespace EQT {
namespace Graphics {

class ConstrainedTextureCache;

class SpellBookBackdropLoader {
public:
    SpellBookBackdropLoader() = default;
    ~SpellBookBackdropLoader() = default;

    /**
     * Upload and protect the composed backdrop. Called by UIAtlas::load().
     * Returns false only when there is no cache at all; a pack MISSING the
     * entry never returns here -- getOrLoad() exits the process, which is
     * the contract for an incomplete EQE1 pack.
     */
    bool load(ConstrainedTextureCache* cache);

    /**
     * The 512x363 composed book art (the four backdrop tiles + the Done X),
     * or nullptr if the backdrop was never loaded (no cache, headless).
     * Pure lookup -- it touches no cache and cannot fail the process.
     */
    irr::video::ITexture* texture() const { return texture_; }

private:
    bool loaded_ = false;
    irr::video::ITexture* texture_ = nullptr;
};

}  // namespace Graphics
}  // namespace EQT
