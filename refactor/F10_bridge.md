# F10 — Split graphics-bridge processing into eq_bridge.cpp

Move the EverQuest↔renderer bridge marshaling cluster out of
`src/client/eq.cpp` (5,368 lines). This is the two-way boundary layer —
the most architecturally sensitive unit in Batch F, but still a pure TU
move. No behavior change, no interface change, no reformatting.

## Scope (orig cluster @ a0c02cd was 17924–19676; now near end of file.
Enumerate by NAME.)

Move to new `src/client/eq_bridge.cpp` the graphics-bridge cluster:
- `InitGraphics`, `ShutdownGraphics`
- `GameTick`, `PreRenderTick` / `PostRenderTick` (whichever still exist —
  verify by grep)
- `CreateZoneLoadSnapshot`
- The `OnSpawn*Graphics` / `OnPet*` bridge helpers
- `ProcessBridgeIntents` (~668 lines) and `ProcessBridgeEvents`
- `UpdateInventoryStats`
- The loot-window flow F13 documented as stranded here (`RequestLootCorpse`,
  `LootItemFromCorpse`, `LootAllFromCorpse`, `DestroyAllCorpseLoot`,
  `CloseLootWindow`) — if contiguous with this cluster, move them WITH it
  and document; they are UI-flow methods that ride the bridge.
- `SaveHotbarConfig` / `LoadHotbarConfig`
- Audio init helpers if inside the contiguous cluster (document).

What stays in eq.cpp: constructor/lifecycle, `PublishFullStateSnapshot`,
the `ZoneOnPacketRecv` dispatcher, `PlayerProfile`/`NewZone` processors,
connection management, and anything else not in this contiguous region.

## Steps

1. Locate the cluster; produce the moved/left mapping in the review BEFORE
   moving. When in doubt, leave + document.
2. Create `src/client/eq_bridge.cpp`: header comment + includes (this file
   legitimately needs the graphics/bridge includes eq.cpp carries — take
   what the moved code needs; eq.cpp's own include list may then contain
   entries only the moved code used, but do NOT prune eq.cpp's includes;
   that's a later cleanup, not this unit).
3. Move verbatim; preserve #ifdef guards exactly (EQT_HAS_GRAPHICS and
   WITH_AUDIO guards are heavy in this region).
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_bridge.cpp`
  — ~1,900 expected: WARN accepted, must not FAIL.
- eq.cpp shrinks by ~1,700+ lines.

## Review

**Cluster location (pre-move, on `fable` @ 8b29308):** the entire graphics/audio
bridge cluster turned out to be one contiguous tail region of eq.cpp, lines
3628–5368 (end of file, 5368 lines total). It sits inside a single
`#ifdef EQT_HAS_GRAPHICS ... #endif` block (opened at 3613, closed at 5212)
immediately followed by a `#ifdef WITH_AUDIO ... #endif` block (5218–5367,
with a small nested `#ifdef EQT_HAS_GRAPHICS` at 5233–5235). No file-local
statics or anonymous-namespace helpers exist anywhere in eq.cpp, so there was
no helper-placement decision to make.

**Moved/left mapping (decided before moving):**

Moved to `eq_bridge.cpp` (verbatim, lines 3628–5368 of pre-move eq.cpp):
- `InitGraphics`, `ShutdownGraphics`
- `GameTick`, `PreRenderTick`, `PostRenderTick`
- `CreateZoneLoadSnapshot`
- `OnSpawnAddedGraphics`, `OnPetCreated`, `OnPetRemoved`,
  `OnPetButtonStateChanged`, `OnSpawnRemovedGraphics`, `OnSpawnMovedGraphics`
- `SaveEntityDataToFile` — not named in the original scope list, but it sits
  physically between `OnPetButtonStateChanged` and `OnSpawnRemovedGraphics`
  inside the same unbroken `EQT_HAS_GRAPHICS` guard. Carving it out would
  require splitting that guard a second time for one 47-line debug-dump
  utility with no independent reason to stay. Moved and documented here per
  the "if contiguous, move with it and document" instruction given explicitly
  for the loot flow, applied by analogy.
- `OnGraphicsMovement` — also not named explicitly, but it is the renderer→game
  movement-intent handler (updates position/heading/animation from a
  `PlayerPositionUpdate` and calls `SendPositionUpdate`). This is squarely a
  bridge marshaling method, sitting immediately before `ProcessBridgeIntents`
  in the same guard. Moved.
- `ProcessBridgeIntents` (~668 lines), `ProcessBridgeEvents`
- `UpdateInventoryStats`
- Loot-window flow: `RequestLootCorpse`, `LootItemFromCorpse`,
  `LootAllFromCorpse`, `DestroyAllCorpseLoot`, `CloseLootWindow` — contiguous
  with the cluster (directly after `UpdateInventoryStats`), moved with it per
  plan instruction.
- `SaveHotbarConfig`, `LoadHotbarConfig` — closes out the `EQT_HAS_GRAPHICS`
  guard.
- Audio helpers, contiguous immediately after (separated only by a
  `// Audio System` banner comment, no other code between the two guard
  blocks): `InitializeAudio`, `ShutdownAudio`, `UpdateDayNightState`,
  `PlaySound`, `PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`,
  `PlayUISound`.

Left in eq.cpp (not moved):
- `SetEQClientPath`, `SetConstrainedConfig` — these two are the first two
  methods inside the same `EQT_HAS_GRAPHICS` guard (lines 3616–3626), directly
  preceding `InitGraphics`, but they are NOT named anywhere in the plan's
  scope list. Unlike `SaveEntityDataToFile`/`OnGraphicsMovement`, they sit at
  the very front of the guarded region (not interleaved between named
  functions), so leaving them behind is a clean boundary: no guard-splitting
  gymnastics needed beyond closing `EQT_HAS_GRAPHICS` right after
  `SetConstrainedConfig` in eq.cpp and reopening it at the top of
  `eq_bridge.cpp` before `InitGraphics`. These are simple config setters
  (client path, constrained-renderer preset) closer to lifecycle/config than
  to bridge marshaling — left per "when in doubt, leave + document."
- Everything else per the plan's "what stays" list: constructor/lifecycle,
  `PublishFullStateSnapshot`, `ZoneOnPacketRecv` dispatcher,
  `PlayerProfile`/`NewZone` processors, connection management, etc. — none of
  this is part of the contiguous 3628–5368 tail region, confirmed by the full
  boundary grep below.

**Loot-flow disposition:** moved with the cluster, as instructed — it was
contiguous (directly following `UpdateInventoryStats`, still inside the
`EQT_HAS_GRAPHICS` guard) and is genuinely UI-flow-that-rides-the-bridge
(`RequestLootCorpse` etc. call into `m_bridge`/inventory state).

**Guard handling:** the single original `#ifdef EQT_HAS_GRAPHICS` block
(3613–5212) is now split into two independently-balanced guards: a short one
in eq.cpp wrapping only `SetEQClientPath`/`SetConstrainedConfig`, and a full
one in `eq_bridge.cpp` wrapping `InitGraphics` through `LoadHotbarConfig`. The
adjacent `#ifdef WITH_AUDIO` block (audio init/playback helpers, with its
nested `#ifdef EQT_HAS_GRAPHICS` for the EQ-client-path lookup) moved intact,
unmodified internally. Guard balance was checked line-by-line in both files
post-move (see verification below) — no unmatched `#if`/`#endif` in either
file.

**Helper analysis:** no file-local `static` functions or anonymous namespaces
exist in eq.cpp (confirmed via `grep -n "^static \|^namespace {"` — zero
hits), so no helper-placement decisions beyond the two methods discussed
above (`SaveEntityDataToFile`, `OnGraphicsMovement`).

**Includes:** per plan instruction, the moved code's include needs were taken
verbatim from eq.cpp's existing include preamble (the full block, lines
1–79 of pre-move eq.cpp, covering combat/spell/skill/inventory/graphics/
bridge/audio headers) rather than hand-pruned — the graphics, bridge, and
audio headers are all genuinely used by the moved code (renderer types,
`GameStateBridge`, `AudioManager`/`SfxManager`/etc., `InventoryManager`,
`HotbarModel`). eq.cpp's own include list was left untouched (not pruned),
per explicit instruction — later cleanup, not this unit.

**Line counts:**
- eq.cpp: 5,368 → 3,629 lines (−1,739 lines, i.e. −1,741 removed + 2 added
  for the re-closed guard `#endif // EQT_HAS_GRAPHICS`).
- eq_bridge.cpp: new file, 1,837 lines (header comment + includes + moved
  body of 1,741 lines + one added `#ifdef EQT_HAS_GRAPHICS` / comment line to
  reopen the guard).

**Verifier summary:**
- `cmake --build build -j24` — clean (only pre-existing `-Wpointer-arith`
  warnings in unrelated `ZoneOnPacketRecv` code, not introduced by this
  move).
- `ctest --test-dir build` — 1190/1192 passed (2 skipped as pre-existing:
  `PfsArchiveContentTest.DdsFileValid`, `SoundBufferTest.LoadSwingWav`, plus
  6 `DoorManagerGraphicsTest.*` skips). 2 failures, both documented exactly
  in `refactor/test_baseline.txt`: `AudioManagerTest.PreloadCommonSounds`
  (ALSA card-0 resolution failure, pre-existing host issue) and
  `RDPServerFrameTest.UpdateFrameMultipleTimes` (known rare segfault flake,
  pre-existing, unrelated to Batch F). No new failures.
- `python3 scripts/refactor/verify_move_only.py 8b29308..HEAD` — **PASS**
  (move-only modulo boilerplate: +89/-0 lines, i.e. the CMakeLists.txt
  one-line addition plus the small header comment / include-guard reopening
  lines in eq_bridge.cpp; `CMakeLists.txt` itself reported separately as
  non-C++, reviewed manually above).
- `bash scripts/refactor/check_file_sizes.sh --files eq_bridge.cpp` — WARN
  (1,837 lines > 1,500 ceiling), accepted per plan (~1,900 expected).

**Deviations from the plan:** none requiring escalation. Two functions
(`SaveEntityDataToFile`, `OnGraphicsMovement`) were moved despite not being
named explicitly in the scope list, justified above by contiguity with the
guard and by analogy to the plan's own instruction for the loot-window flow.
`SetEQClientPath`/`SetConstrainedConfig` were left behind despite being
inside the same original guard, justified by the clean (non-interleaved)
boundary and by not appearing in the named scope.
