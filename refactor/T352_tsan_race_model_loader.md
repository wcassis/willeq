# T352 — TSAN run over `RaceModelLoader` (B016): confirm or close

Class B measurement, slot1, no eqemu. B016 says ~20 unlocked
`loadedModels_` reads across four TUs; a blanket lock self-deadlocks.
Unconfirmed since T114.

1. Configure a TSAN tree the way T121 did (`refactor/T121_tsan_zone_crossing.md`
   records the flags — reuse them, in `/mnt/projects/fable/wt/slot1/build-tsan`,
   GLES2 config, tests on). Build `willeq` and the race-model tests.
2. Run: the T121 zone-crossing script (headless, three zones) and the
   race-model/EQE1 test binaries under TSAN with `--eqe1-path`. Collect
   every ThreadSanitizer report naming `loadedModels_`, `RaceModelLoader`,
   or the four TUs (`race_model_loader.cpp`, `model_loading*.cpp`,
   `variant_loading*.cpp`, `mesh_building.cpp`).
3. Verdict: (a) reports present → list each race (both stacks, the
   member, the two threads), and propose the minimal fix shape (a
   recursive mutex on the loader, or moving the read under the existing
   lock — measure which reads sit on the render thread vs the loading
   thread); do NOT fix in this unit unless it is one lock at one site;
   (b) no report → B016 closes as "not reproducible under TSAN at this
   tip" with the run log.
4. `review/T352/README.md` with the report excerpts; B016 updated.

STOP: the TSAN tree does not build (report the error; the T121 flags may
have rotted).
