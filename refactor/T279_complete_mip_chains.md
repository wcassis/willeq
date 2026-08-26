# T279 — complete ETC1 mip chains + fix the alpha companion; re-bless goldens (Will's decision, 2026-08-26)

Slot: first free. x86. NO eqemu (offline assets + gate). Class A, LARGE
(regenerates every EQE1 pack). Decision: docs/decisions.md 2026-08-26 EQE1
mip entry. Territory: tools/eqe1_builder.cpp, include/client/graphics/
eqe1_format.h (if the count/size helpers need it), tools/verify_eqe1.cpp,
tools/convert_all_to_eqe1.sh, src/client/graphics/constrained_texture_cache.cpp
(the ALPHA companion filter at ~1962-1969 ONLY — T275 already fixed the RGB
paths), data/eqe1_texture_index.json, the golden bless, tests. Coordinate:
whatever units are live in the other two slots — do not touch their files.

## Facts established before this unit (verify, do not assume)
- T275 `ad51cc3f`: an incomplete chain + a mipmap MIN_FILTER samples
  opaque black; RGB paths now use plain `GL_LINEAR`.
- `computeMipLevels(256,256)` in `eqe1_format.h:96` ALREADY returns 9, and
  `generateMipChain`'s loop (`eqe1_builder.cpp:260`) runs `level < outMipLevels`.
  **So the CURRENT builder may already emit complete chains and the shipped
  packs may simply predate it (`eqe1_format.h` last touched 2026-07-24).**
  STEP 1 IS TO FIND OUT — build one pack from one S3D with the current
  builder and run `verify_eqe1` on it. If it reports 9, the builder needs
  no change and this unit is a regeneration + alpha-filter unit.
- The alpha companion sets `GL_NEAREST_MIPMAP_NEAREST`
  (`constrained_texture_cache.cpp:1969`) and the driver never overwrites
  unit 1's filters (T275).

## Scope
1. **Determine the truth** (above). If `encodeETC1_RGB/_Alpha` cannot encode
   a level below 4x4, fix it: ETC1 stores ceil(w/4)*ceil(h/4) blocks, so a
   2x2 or 1x1 level is one 8-byte block with the source padded. Confirm the
   READER (`eqe1_format.h` size helpers, the loader's per-level offset walk)
   agrees byte-for-byte with what the builder writes — a size mismatch here
   corrupts every subsequent level.
2. **Alpha companion:** with complete chains its mipmap filter becomes
   legal. Decide between keeping `GL_NEAREST_MIPMAP_NEAREST` (now valid) and
   plain `GL_NEAREST` — the comment at :1962 explains why NEAREST was chosen
   for alpha; keep that intent, only the completeness changes. State the
   choice and why. Verify unit 1 is actually bound with a complete texture.
3. **Regenerate all packs** with `tools/convert_all_to_eqe1.sh` into the
   real asset tree (`/mnt/projects/EverQuest_ETC1` — this is SHARED with the
   Pi via `PI_EQE1_PATH`; build to a STAGING dir first, verify, then swap,
   and keep the old tree until the gate passes). Run `verify_eqe1` over
   every pack: expect 0 `mipLevels` complaints. Record before/after entry
   counts and total bytes (complete chains add ~1 block per extra level —
   quantify it; the orangepi budget is the constraint, decision #2).
4. Rebuild `data/eqe1_texture_index.json` if any offset moved (T207's
   `eqe1_texture_index_builder`), with the T262-style diff proof.
5. **Re-bless the goldens** (Will approved) — only after 1-4 are green.
   Capture before/after crops of anything that changes, especially entity
   alpha edges (foliage, hair, capes). If NOTHING changes visually, say so
   and do not re-bless.
6. Pixel proof: extend `tests/test_ui_atlas_pixels.cpp` (T275) or add a
   sibling asserting an alpha-companion texture samples correctly at a
   reduced mip level. Red/green measured.

## STOP
If regenerating changes the on-disk format version or the loader's offset
math in a way that would break the Pi's existing packs, STOP and report —
that is a compatibility decision, not an executor call.

## Verification
Full ctest; visual gate (re-blessed, with the before/after evidence);
build-gles2-x86; build-arm-noble; `verify_eqe1` clean over every pack;
Review with the size delta; evidence review/T279/. Commit:
`T279: EQE1 packs ship complete mip chains; alpha companion fixed; goldens re-blessed`.
