# R02 — Replay harness and game-state oracle test

Replay a session log (format: `include/common/net/session_log.h`) into a
headless EverQuest instance and assert the resulting game state. Ships green
using a synthetic session built in-test; automatically switches to
golden-snapshot mode when a real recording exists.

## Design

- **Replay seam**: public `void EverQuest::ReplayZonePacket(uint16_t opcode,
  const uint8_t* data, size_t size)` — dispatches exactly as
  `ZoneOnPacketRecv` does for an app packet with that opcode (reuse the
  dispatcher; examine how it extracts the opcode and enter below that
  point). Document in eq.h as the replay/test seam.
- **Null-connection safety**: replay runs with no network. Audit the zone
  handler send paths (`QueuePacket`, `SafeQueueZonePacket`) and any other
  m_zone_connection uses reachable from handlers; guard so a null connection
  no-ops safely. Add a counter (`m_replay_suppressed_sends`) so tests can
  assert how many replies were suppressed. m_bridge is already null-checked
  throughout (verify with a grep sample; fix any unguarded use you hit).
- **Test** `tests/test_session_replay.cpp` (register in tests/CMakeLists.txt,
  link the same core sources as other eq-level tests):
  1. **Synthetic mode (always runs):** construct
     `EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar")`-style
     headless instance (no InitGraphics, no connections). Build a synthetic
     session in-memory or via a temp file using session_log.h structs:
     NewZone → PlayerProfile → a few ZoneSpawns/NewSpawn entries →
     TimeOfDay → HP updates → DeleteSpawn, using packet_structs.h layouts.
     Investigate what ordering/flags the handlers expect
     (CheckZoneRequestPhaseComplete's m_*_received flags) and satisfy the
     minimum needed. Replay, then assert via public accessors: zone name,
     entity count and per-entity name/level/position, player profile stats
     (GetLevel/GetMaxHP/...), time of day, entity removed after DeleteSpawn.
  2. **Golden mode (activates when a real capture exists):** if
     `data/captures/session.sleq` exists (path overridable via
     `WILLEQ_SESSION_LOG` env), replay all Zone-stream S→C records, produce
     a deterministic state summary (sorted text: zone, player stats, entity
     table), and compare to `data/captures/session.golden`. If the golden
     file is missing, WRITE it and GTEST_SKIP with a "golden created —
     re-run to verify" message. `data/captures/` is gitignored (verify;
     data/eq_client precedent).
- Handlers that crash on partial/synthetic data: fix ONLY by adding bounds
  checks that are correct for real traffic too; document each.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions; the new
  synthetic-mode test must PASS (not skip).
- Review documents: seam location, every null-guard added, the synthetic
  packet sequence, every assertion, and any handler robustness fix.

## Review

(executor fills in)
