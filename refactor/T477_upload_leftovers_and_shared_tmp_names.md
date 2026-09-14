# T477 — hygiene: T475's leftovers (B690) and fixed /tmp names shared across slots in tests (B571)

Class B/P, **no lock, no Pi**, slot3. Read T475's Review
(`refactor/T475_delete_dead_texture_arrival_path.md`) and T465's
(`refactor/T465_headless_shutdown_load_flake.md`, section on the per-process
`mkdtemp()` config).

## B690 — leftovers no net sees (T475)

`AssetType::ZoneTexture` has no production user; `gpu_upload_thread.h`,
`work_priority.h` and fmt includes that only the deleted texture path needed
remain in the TUs T475 edited. Census each (prove zero users per the protocol's
grep rule), delete, cascade to a fixed point. If an enum value is persisted,
serialised or indexed by position anywhere (a table, a switch that must stay
exhaustive, a golden), say so before deleting it.

## B571 — 17 fixed `/tmp` names in 9 test files (T465)

Every slot — and parallel `ctest -j6` cases — writes the same paths. None fails
today, but it is the same latent race T465 removed from
`test_headless_shutdown.cpp`. Replace each with a per-process unique directory
(`mkdtemp()` or a `getpid()` suffix, removed in `TearDown()`), the T465 idiom.
Add a source census test that fails on a new literal `"/tmp/` path in `tests/`
without a unique component (allowlist only with a written reason).

## Proof

Standard verification bar, builds strictly in sequence; `ctest -j6` run TWICE
(the /tmp change is concurrency-facing); G-batch goldens for touched TUs;
`ZeroCallers`/`WriteOnlyFields`; visual gate; ARM; file sizes. Board B710–B719;
strike B690, B571.

File territory: the TUs/headers T475 left includes in, `AssetType`'s header and
users, the nine test files T465 named, the new census test. **Not** the camera
component / `camera_rig.h` (T474, slot1), **not** the equipment loader /
`bone_attachment.h` / `character_model_view.cpp` / held-item tests (T476,
slot2).

STOP: `AssetType::ZoneTexture` turns out to be positionally significant
(report); an image golden moves.

## Review

(executor fills)
