# T211 — magenta/checkerboard: fragment 0x2C never resolved its material list

Evidence: `review/T211/`. Will's standing ruling: magenta/checkerboard textures
indicate a failure and should be fixed, not tolerated.

## Where the magenta comes from
`ConstrainedTextureCache::getPlaceholderTextureInternal()` builds an 8x8
magenta/dark-grey checkerboard. It is handed out when a mesh has no usable
texture. One upstream path produces exactly that state: `parseFragment2C()` in
`wld_loader.cpp` fails to resolve its material list, falls into

    if (geom->textureNames().empty()) { ...resize to maxIdx+1 empty entries... }

and the mesh is stored with empty texture names — which renders as the
placeholder.

## Root cause: `flags & 0xFFFF` is a flags word, not a reference

Fragment 0x36 (the modern mesh) carries a real material-list reference and the
code uses it (`header.frag1`). **Fragment 0x2C has no such field** —
`WldFragment2CHeader` is flags + counts + geometry bounds, nothing else — so the
code guessed:

    uint32_t texBrushSetRef = header.flags & 0xFFFF;

Adding the brush-set key range to the failure log settles what that guess yields:

    '2HNSWORD_DMSPRITEDEF'  ref=6147  keys 5..1763   flags=0x1803
    'IT1036_O_DMSPRITEDEF'  ref=6147  keys 5..4397   flags=0x1803
    'IVM_DMSPRITEDEF'       ref=6146  keys 177..556  flags=0x1802
    'IVMHE00_DMSPRITEDEF'   ref=6147  keys 177..556  flags=0x1803

Two things kill the guess. The ref is outside **every** file's key range, so it
is not an off-by-one. And `flags` is the identical `0x1803` for three unrelated
models in three different archives — a fragment reference would differ per model;
a flags word would not. It is a flags word.

## The fix: nearest preceding 0x31

WLD emits a material list immediately before the mesh that uses it. The failing
fragment indices confirm it, 4 for 4:

    | mesh      | 0x2C fragment | nearest preceding 0x31 |
    |-----------|---------------|------------------------|
    | 2HNSWORD  |          1764 |                   1763 |
    | IT1036_O  |          4398 |                   4397 |
    | IVMHE00   |           557 |                    556 |
    | IVM       |           558 |                    556 |

`WldLoader` now tracks `lastBrushSetFragIndex_` as it parses, and
`parseFragment2C` uses it **only when the existing lookup fails** — meshes that
resolve today are untouched, so this cannot regress them.

## Verified: the right list, not merely a list

Remaining `NOT FOUND in brushSets_` in a befallen zone-in: **0** (was 26). The
fallback reports what it produced, and the names match the meshes:

    '2HNSWORD_DMSPRITEDEF': fallback frag 1763 -> 4 textureNames (4 non-empty, first='sword2hndblade.bmp')
    'IVMHE00_DMSPRITEDEF':  fallback frag  556 -> 1 textureNames (1 non-empty, first='ivmhe0001.bmp')
    'IVM_DMSPRITEDEF':      fallback frag  556 -> 1 textureNames (1 non-empty, first='ivmhe0001.bmp')

`sword2hndblade.bmp` for a two-handed sword and `ivmhe0001.bmp` for a mesh named
`IVMHE00` are the correct textures, which is what distinguishes a real fix from a
silenced warning.

Suite 2745/2746 (sole failure the known ALSA host quirk).

## Honest limits
- **Not visually confirmed.** These meshes parse but nothing in the befallen run
  rendered them: EQE1 lookups served were identical before and after (369), so no
  on-screen pixel changed in this test. `2HNSWORD` needs an NPC wielding one and
  `IVM` is the invisible-man model. The fix is proven at the parse level — empty
  texture names became correct ones — not at the framebuffer.
- **`IVM` shares `IVMHE00`'s list.** Their refs differ (6146 vs 6147), so they
  presumably wanted different lists, but only one 0x31 precedes both. Body and
  head sharing a material list is plausible and beats empty names, but it is the
  one case here I cannot positively confirm is what the format intends.
- This closes the 0x2C path to the placeholder. It does not prove no other path
  reaches it — races 415/457, which have no model in any archive, still render as
  placeholder *cubes* and remain a separate asset gap.
