# T483 — fable tracks 8 spell gems; Titanium has 9 (B762)

Class A (unconfirmed live), **no lock (Leg A)**, slot3. Found by T482 (read its
Review, `refactor/T482_cast_ended_by_stopcasting_or_resist.md`, B762). Context:
Will's Pi report on 2026-09-13 was "no spell gems visible". T459 and T462 later
showed the gem column IS in the rendered buffer, with 8 slots. Still, the gem
count is part of what Will looks at.

## The lead

`SpellManager` (and/or the gem state, the bridge payloads, the static UI gem
panel, `/cast`) sizes the gem bar at 8. Titanium has 9 memorised gems:
eqemu's `MAX_PP_MEMSPELL` for the Titanium client
(`/mnt/projects/akk-stack/code/common/patches/titanium_*`, `common/eq_limits.h`)
and the oracle UI's `EQUI_CastSpellWnd.xml` `CSPW_Spell0..CSPW_Spell8` (T461
and board ~5121 quote it). So slot 8 is dropped everywhere: the profile's
memorised spell 9, `OP_MemorizeSpell` for slot 8, the 9th gem button, and
`/cast 9`.

## Tasks

1. **Establish the count from the sources.** Cite the eqemu Titanium limit and
   the struct sizes (PlayerProfile `mem_spells`, `MemorizeSpell_Struct`), the
   oracle XML (`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CastSpellWnd.xml`),
   and whether the 9th gem is gated by level or AA in Titanium (and if so, what
   the client shows before it unlocks). Do NOT invent a gate.
2. **Census every 8** tied to gems: constants, arrays, loops, bridge payloads,
   UI layout (`UILayout` spellGems rect, `renderSpellGemPanel`, the atlas
   `SpellGem*` sprites), hotkeys (`/cast N`, gem key bindings), persistence, and
   tests. Name each one in the Review.
3. **Pre-fix failing tests:**
   - a profile with 9 memorised spells populates 9 gems;
   - `OP_MemorizeSpell` for slot 8 lands;
   - the gem panel emits 9 slots at 800x600 and at 1280x720, all on-screen,
     using the rect arithmetic T459/T462 used;
   - `/cast 9` reaches `beginCastFromGem(8)`.

   Then fix the count at one named constant.
4. **Layout.** Place the 9th gem the way the oracle XML does, scaled to fable's
   layout like T393's purse: proportionally, with the source numbers quoted.
   Capture offline before/after stills at 800x600 and 1280x720 into
   `review/T483/` with a README. Never self-assess.

## Proof

- Standard verification bar, builds strictly in sequence.
- G-batch goldens for every TU touched, updated in place per G-correction 32.
  Say so; `zone_dispatch`, `bridge_component_intents` and `renderer_frame_render`
  are pinned.
- Visual gate. If an image golden moves because the gem column grew, STOP and
  report; Will blesses.
- `UIAtlasPixelTest`, ARM, file sizes.
- Board B770–B779; strike or narrow B762.

File territory: `src/client/spell/`, the gem UI panel and layout, gem
payloads/events, `/cast` and gem hotkeys, spell and UI tests, `review/T483/`.
**Not** `camera_rig.h` (T478, slot1). **Not** any loader or the T479 branch
(slot2).

STOP: an image golden moves; the 9th gem is gated in Titanium by something fable
does not track (report the gate).

## Review

(executor fills)
