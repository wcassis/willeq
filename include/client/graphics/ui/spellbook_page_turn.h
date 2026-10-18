// spellbook_page_turn.h -- the spellbook popup's ONE spread-turn body.
// B1076 (GitLab #396) fix leg 2, review t_d00ada41 finding 1.
//
// currentPage is a SPREAD index (one spread = the oracle's 16-slot open book,
// spellbook_layout.h). Before this unit the edge arrows in static_ui_input.cpp
// stepped it by SPELLS_PER_PAGE (16) spreads per click -- a forward click on a
// 17-spread book jumped to spread 16 (all 16 slots blank, page numbers 33/34)
// and a back click clamped straight to 0, so no intermediate spread was ever
// reachable -- while the pre-existing /newbookprev / /newbooknext stepped by
// ONE spread. The two paging paths had drifted; this header is the one body
// both now call, so they cannot drift again.
//
// A spread-turn never steps past the last spread (the clamp the old arrow
// code's `currentPage > 0` guard and the slash commands' own comparisons
// already did) -- the arrows and the slash commands are now one code path,
// not two.

#pragma once
#include "client/graphics/ui/static_panels.h"

namespace EQT {
namespace Graphics {

// One spread forward. Returns true when the book moved.
inline bool turnSpellbookSpreadForward(SpellbookPopupState& book) {
    if (book.currentPage < book.pageCount() - 1) {
        ++book.currentPage;
        return true;
    }
    return false;
}

// One spread back. Returns true when the book moved.
inline bool turnSpellbookSpreadBack(SpellbookPopupState& book) {
    if (book.currentPage > 0) {
        --book.currentPage;
        return true;
    }
    return false;
}

}  // namespace Graphics
}  // namespace EQT
