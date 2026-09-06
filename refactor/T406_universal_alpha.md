# T406 — the other 63 textures keep their alpha (B266)

Class A, slot1, no server. Read B266, T400's §Review (the 32-bit BMP
writer and the pack's `--rgba8888-texture` override), T403's §Review
(`generate_textures --report`, `scripts/build_textures.sh`,
`data/textures.sha256`, the one-commit cascade rule in CLAUDE.md), and
the consumers per file (rain/snow/storm-cloud renderers, the cursor, the
spell fallback, tumbleweed).

1. Census from `--report`: per file, the alpha levels the generator
   draws and the consumer's material/blend (alpha-blend, alpha-test,
   additive, or opaque). A file whose consumer never samples alpha is
   NOT changed (say which).
2. For every file whose consumer samples alpha: write 32-bit (T400's
   writer), regenerate, and give the pack entry the rgba8888 override
   where the ETC1 companion path cannot be sampled (T400/B261 — read the
   consumer's shader: single sampler → override needed; alpha-test dual
   sampler → the companion already works, say so). One commit per
   consumer family (weather, cursor, spell fallback, detail/tumbleweed).
3. The one-commit cascade: regenerate → `--write-pins` → rebuild the pack
   via the script (atomic; copy the old pack under `review/T406/`; no
   ctest/gate/willeq running) → `data/willeq.eqe1.sha256` — in the SAME
   commit per family so `TexturesReproducible` and `PackReproducible`
   never go red on a tip.
4. Tests: per changed file, the shipped BMP carries a non-trivial alpha
   histogram (pre-fix failing); per consumer, the material is pinned.
5. Visual: storm clouds / rain / snow / cursor may appear in the gate's
   zones — run the gate standalone after EACH family; if a pixel golden
   moves, that is the alpha becoming visible: STOP with the contact sheet
   for Will, never bless, and leave that family's commit in place with
   the STOP recorded. Report the `eqe1MappedBytes` delta per family for
   B240's pending bless (do not bless).
6. Strike B266 (or annotate per family).

## Proof

Build (all trees), `ctest -j6` (expected reds: B240's gate delta only —
the atlas determinism test is green after T403), pre-fix/post-fix, gate
standalone per family, ARM, file sizes.

STOP: a pixel golden moves (per family, as above).
