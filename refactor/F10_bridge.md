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

(executor fills in)
