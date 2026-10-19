# B1029: stable model fingerprints

Issue #349; Will approved regeneration of the four `model_fingerprint` goldens on 2026-10-19.

## Mechanism at HEAD

`tools/hwcaps/model_fingerprint.cpp:149-158` formats the base row immediately after `ensureModelDataLoaded`, before `preloadVariantModel`. In `src/client/graphics/eq/variant_loading_preload.cpp:269-320`, variant construction points `modelData->skeleton` at `character->animatedSkeleton` and inserts ELM source animations and bone tracks into that shared object. Thus a base row can show `anims=-` before the variant is built and the merged animation list afterward. `src/client/graphics/eq/animated_mesh_creation.cpp:64-150` separately merges animation-source data for client mesh creation; this is a fingerprint snapshot timing defect, not a client-runtime correction.

The chosen fix keeps tool-owned `RaceModelData` references and fingerprints them after all requested base and variant builds. It makes the golden describe the merged skeleton that the client ends up with, including any later preload of another race sharing that skeleton. It touches no renderer/runtime path. A test runs the same race/variant pair in both orders and checks the base row is the same merged count.

The new self-test failed against the pre-fix print timing: base-first race 127 had `anims=-`, while variant-first had 43 animations and the merged transform hash. The exact failing ctest output is `negative-order.log` in the evidence directory. After the fix, `positive-order.log` records the same test passing; `entity-source-pruning.log` records all five related ctests passing, and `hygiene.log` records the source hygiene run.

## Golden changes

Regenerated with the tool through `rbuild test -R EntitySourcePruning.RegenerateB1029Goldens` using a temporary test harness, then removed that harness. The audit script and its output are under `/home/user/kb-evidence/kb-b1029-fingerprint-order/`. Exactly the following 22 base rows change in **each** of `dreadlands`, `gfaydark`, `iceclad`, and `qeynos2` (88 changed lines total). For every row below, the mechanism is the same: the variant preload merged source animations and bone tracks into the base's shared skeleton after its old fingerprint had printed; the new snapshot sees that merge. Each new `anims` list and `xf` hash equals its matching `variant=1,1` row. The audit verifies that `code`, geometry, bones, skeleton hash, and all other rows are unchanged.

| race | gender | `anims` count before → after | `xf` before → after |
|---:|---:|---:|---|
| 1 | 0 | 3 → 43 | `f21c8fd0aefdf810` → `bbdac5cf1b1529b2` |
| 1 | 1 | 0 → 42 | `14650fb0739d0383` → `47fb86376dbedbea` |
| 10 | 0 | 0 → 36 | `14650fb0739d0383` → `30127bb209600ecd` |
| 10 | 1 | 36 → 42 | `30127bb209600ecd` → `8214fdcb4afff587` |
| 11 | 0 | 1 → 43 | `14650fb0739d0383` → `73677af1e55027d2` |
| 11 | 1 | 0 → 42 | `14650fb0739d0383` → `d157b2ed7507ea05` |
| 12 | 0 | 1 → 43 | `14650fb0739d0383` → `73677af1e55027d2` |
| 12 | 1 | 0 → 42 | `14650fb0739d0383` → `d157b2ed7507ea05` |
| 127 | 0 | 0 → 43 | `14650fb0739d0383` → `3b73e3e8c7b5ff48` |
| 3 | 0 | 0 → 43 | `14650fb0739d0383` → `3b73e3e8c7b5ff48` |
| 3 | 1 | 0 → 42 | `14650fb0739d0383` → `47fb86376dbedbea` |
| 5 | 0 | 0 → 43 | `14650fb0739d0383` → `3b73e3e8c7b5ff48` |
| 5 | 1 | 0 → 42 | `14650fb0739d0383` → `47fb86376dbedbea` |
| 6 | 0 | 0 → 43 | `14650fb0739d0383` → `3b73e3e8c7b5ff48` |
| 6 | 1 | 0 → 42 | `14650fb0739d0383` → `47fb86376dbedbea` |
| 60 | 0 | 7 → 43 | `8a3eeed512d82b04` → `de628cb42f09bd44` |
| 7 | 0 | 0 → 43 | `14650fb0739d0383` → `3b73e3e8c7b5ff48` |
| 7 | 1 | 0 → 42 | `14650fb0739d0383` → `47fb86376dbedbea` |
| 8 | 0 | 38 → 43 | `73677af1e55027d2` → `73677af1e55027d2` |
| 8 | 1 | 36 → 42 | `d05b52046a9ccf3b` → `d157b2ed7507ea05` |
| 9 | 0 | 2 → 36 | `a2965e58310de20b` → `ce2a92f78da3fa8f` |
| 9 | 1 | 2 → 36 | `a8d41d89f0405bdb` → `3e590f7932044cbf` |

For race 8 gender 0, the five added animation codes do not affect the four animation tracks sampled by the `xf` hash, so that hash does not change. No changed line lacks a print-order explanation.

## Not verified

No device or live server is involved or required for this offline card.
