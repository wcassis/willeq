# T267 — race 330 (playable Froglok) cannot load: loader hardcodes the monster archive and stoi()s the s3d_file name (T265)

Slot: slot1. x86. OWNS eqemu briefly (noclobber; T266 slot2 may hold it —
bounded wait) for the pixel check. Class A. Territory:
src/client/graphics/eq/race_codes.cpp `getRaceModelFilename()`,
src/client/graphics/eq/race_model_loader.cpp (s3d_file branch + directory
scan regex), tests. Do NOT touch renderer_effects.cpp / the EQE1 resolver
(T266 slot2), action/bridge/eq_*.cpp (T264 slot3), race_models.json / db
(T265 merged — the data is now correct: 330 → globalpcfroglok_chr).

## Defect (T265 Review)
1. `getRaceModelFilename()` hardcodes 330 → `globalfroglok_chr.s3d` (holds
   FRG, the monster froglok), ignoring the row's `s3d_file`.
2. `race_model_loader.cpp`'s s3d_file branch does `stoi("pcfroglok")` for
   any non-numeric `global*` name → exception/zero → loads nothing.
3. The directory scan's regex skips `global[a-z]+_chr` archives.
So 330 renders a placeholder even with correct data.

## Scope
1. Make the row's `s3d_file` authoritative (drop the 330 hardcode; keep the
   generic `global{N}_chr` numeric path working); parse non-numeric global
   names as plain archive names; let the scan include `global[a-z]+_chr`.
   Check no other hardcoded race→archive mapping in `getRaceModelFilename`
   contradicts a row (T265's table) — fix any that do.
2. Test: race 330 resolves to `globalpcfroglok_chr` and the loader finds
   FRM/FRF (offline, S3D data present); a numeric global name still works.
   Red/green.
3. Live: `#npctypespawn` a race-330 NPC (or `#race 330` on Testchar if the
   GM command works) with `--eqe1-path` → placeholder=false, non-zero
   vertices, 0 magenta, screenshot. Delete the spawn row after (T263's
   `zone_state_spawns` mechanism); restore Testchar.

## Verification
Full ctest; visual gate; Review; evidence review/T267/. Commit:
`T267: race 330 loads the playable froglok archive`.
