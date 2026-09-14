# T480 — hygiene: renderer headers that include unused upload/priority headers (B710), unused `AssetType` values (B711), the unused `PlayerState::setFollowDistance` (B684)

Class B, **no lock, no Pi**, slot3. Read T477's Review
(`refactor/T477_upload_leftovers_and_shared_tmp_names.md`) and T474's
(`refactor/T474_chase_camera_geometry.md`, B684).

## Items

- **B710:** three renderer headers include `gpu_upload_thread.h` and
  `work_priority.h` but use neither, and `irrlicht_renderer.h` includes
  `work_priority.h` without using it. Two zone TUs reach the priority key only
  transitively through those headers. The fix: move the include to the TUs
  that actually use each symbol, then remove it from the headers. Prove with
  the full build, tools included, that nothing else depended on the transitive
  include.
- **B711:** `AssetType::Door`, `EntityTexture` and `LightEffect` have no
  production user. T477 showed the `AssetType` priority key is compared
  in-process only: never persisted, serialised, used as a table index or
  switch-exhaustive. Re-prove that for these three, then delete them the way
  T477 deleted `ZoneTexture`, leaving explicit values with commented gaps.
- **B684:** `PlayerState::setFollowDistance` has no production caller. It was
  hidden by a name collision with the `CameraController` method T474 deleted.
  Prove zero callers per the protocol grep rule, delete it, and cascade the
  delete (e.g. its backing field, if that becomes write-only).

## Proof

Standard verification bar, builds strictly in sequence; `ZeroCallers` and
`WriteOnlyFields` rows; G-batch goldens for touched TUs; visual gate; ARM; file
sizes. Board B740–B749; strike B710, B711, B684.

File territory: the renderer headers B710 names, the two zone TUs, `work_priority.h`
`AssetType`, `tests/test_work_priority.cpp`, `PlayerState`, the zero-caller
script rows. **Not** `wld_loader*`, character, animated-mesh or object loaders
(T479, slot2). **Not** `camera_rig.h` or camera constants (T478, slot1).

STOP: an `AssetType` value turns out to be positionally significant (report).
Also STOP if an image golden moves.

## Review

(executor fills)
