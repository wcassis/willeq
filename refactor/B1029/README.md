# B1029: stable model fingerprints

Issue #349; Will approved regeneration of the four `model_fingerprint` goldens on 2026-10-19.

## Mechanism at HEAD

`tools/hwcaps/model_fingerprint.cpp:149-158` formats the base row immediately after `ensureModelDataLoaded`, before `preloadVariantModel`. In `src/client/graphics/eq/variant_loading_preload.cpp:269-320`, variant construction points `modelData->skeleton` at `character->animatedSkeleton` and inserts ELM source animations and bone tracks into that shared object. Thus a base row can show `anims=-` before the variant is built and the merged animation list afterward. `src/client/graphics/eq/animated_mesh_creation.cpp:64-150` separately merges animation-source data for client mesh creation; this is a fingerprint snapshot timing defect, not a client-runtime correction.

The chosen fix keeps tool-owned `RaceModelData` references and fingerprints them after all requested base and variant builds. It makes the golden describe the merged skeleton that the client ends up with, including any later preload of another race sharing that skeleton. It touches no renderer/runtime path. A test runs the same race/variant pair in both orders and checks the base row is the same merged count.

## Golden changes

Pending regeneration and line-by-line audit.

## Not verified

Pending.
