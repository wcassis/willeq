# T09 — Read-only login/world accessors + the assertions they unlock

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

Authorized by Will (items from T06 finding 4). Base-layer source, so
the same narrow-territory rules as T05/T07 apply.

## Territory

**ALLOWED:**
- `include/client/eq.h` — **read-only accessors only** (see the list
  below). Nothing else in the header.
- `tests/**`.

**NOT allowed:** any `.cpp` under `src/`, any new member variable, any
change to parsing or control flow, `src/client/graphics/**`,
opportunistic cleanups. Every header hunk must be one of the accessors
enumerated below.

If an accessor you want isn't on the list, **STOP and report** — do not
add it, and above all do not add storage to make one possible.

## Why this unit

T06 covered login/world sequencing and robustness but could not assert
*state*, because none of it is publicly readable. T06's Review listed
what was blocked; this unit unblocks the part that is genuinely just a
visibility problem.

## What is actually accessible — I checked each one

T06's wish-list does not survive contact with the code intact. Split:

### IN — the member exists, so this is a pure visibility change

| accessor | member | notes |
|----------|--------|-------|
| `GetAccountId()` | `m_dbid` (eq.h:1211) | set by `LoginProcessLoginResponse` |
| `GetSessionKey()` | `m_key` (:1210) | see the note below |
| `IsWorldReady()` | `m_world_ready` (:1220) | |
| `HasSentEnterWorld()` | `m_enter_world_sent` (:1221) | |
| `GetZoneServerHost()` | `m_zone_server_host` (:1222) | from `ZoneServerInfo` |
| `GetZoneServerPort()` | `m_zone_server_port` (:1223) | |
| `GetWorldServers()` | `m_world_servers` (:1167) | `const std::map<uint32_t, WorldServer>&` — this is the "server-list contents" item |
| `GetCharacterSelectIndex()` | `m_character_select_index` (:1422) | `int`, `-1` until our character is matched |
| `GetChatServerHost()` | `m_ucs_host` (:1706) | |
| `GetChatServerPort()` | `m_ucs_port` (:1707) | |
| `GetMailKey()` | `m_mail_key` (:1708) | |

All `const`, all trivial inline returns, matching the style of the
existing accessors around `eq.h:817` (`GetMyName`). Return `const&` for
the strings and the map; by value for the scalars.

### OUT — cannot be done as an accessor, and must not be faked

- **Character-list contents** (names, level, class, race, zone).
  `WorldProcessCharacterSelect` (`eq_login_world.cpp:554`) parses all
  ten slots, logs them, and **keeps only** `m_character_select_index`.
  Everything else is discarded. Exposing it would mean *adding a member
  and populating it* — a functional change to the client made solely
  for tests. That is a different decision and it is not in this unit.
  Report it; Will can rule on it separately.
- **The `WorldSendClientAuth` zoning flag.** It is a local computed at
  send time (`eq_login_world.cpp:511`) from `m_zone_connected` and
  `m_zone_change_requested`. Nothing stores it. Assert the inputs
  instead if you need to; do not add a member.

## What to do

### 1. Add the accessors (`include/client/eq.h`)

Exactly the eleven above, in the public section, grouped with a short
comment saying they are read-only observers of login/world state. No
logic in them.

**Note on `GetSessionKey()`:** `m_key` is the login session key. It is
the client's own credential and the object already holds it, so an
inline const getter leaks nothing further — but it is worth being
deliberate about, so keep it a plain read-only getter, do not log it
from tests, and flag in your Review that it was added.

### 2. Use them (`tests/`)

Extend `tests/test_login_world_flow.cpp` (or a new file if it would
push past the file-size check — say which you chose and why) with the
assertions T06 could not make. The point of this unit is the tests, not
the getters:

- **Login response**: replay a synthetic `HC_OP_LoginAccepted` with a
  known dbid and key and assert `GetAccountId()` / `GetSessionKey()`
  parse correctly. This is the first test of that parsing path.
- **Server list**: replay `HC_OP_ServerListResponse` and assert
  `GetWorldServers()` has the expected count and per-entry fields
  (`long_name`, `address`, `lang`, `region`). Note the parse walks
  `GetCString` offsets — good news for T08, and a place a bad offset
  would previously have gone unnoticed.
- **Character select**: replay the recorded `HC_OP_SendCharInfo` and
  assert `GetCharacterSelectIndex()` finds the configured character.
  Also assert the not-found path leaves it at `-1`.
- **Zone server info**: replay `HC_OP_ZoneServerInfo` and assert host
  and port.
- **Chat server**: replay `HC_OP_SetChatServer` and assert the parsed
  host/port/mail key.
- **World ready / enter-world**: assert the flags flip at the right
  points in the handshake.

Prefer driving these from the **real recorded records** where the
fixture has them (it has `SendCharInfo`, `ZoneServerInfo`,
`SetChatServer`, `LoginAccepted`, `ServerListResponse`), falling back
to synthetic payloads where you need specific values. Real records
prove the parsers work on real server output, which is the higher-value
assertion.

## STOP conditions

- You want an accessor not on the IN list → STOP and report.
- You want to add a member, or change any `.cpp` → STOP. This includes
  the character-list storage question.
- A parser turns out to be wrong (a field lands in the wrong member, an
  offset is off) → that is a **finding**: pin it with a test that
  documents the real behavior and report it prominently. Do not fix it.

## Acceptance criteria

1. Exactly the eleven accessors in `eq.h`, nothing else changed there.
2. Tests exercising each one, driven from real recorded records where
   available.
3. Build clean; test binaries under 5 seconds each — report times.
4. `test_session_replay` golden matched, `session.golden` md5 unchanged.
   A mismatch is a STOP, never a re-bless.
5. Full suite ×3, documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` / `RDPServerLifecycleTest.*` are the known
   T03b use-after-free — name them, do not call the run clean).
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
7. **`bash <slot>/scripts/build-arm-noble.sh` passes** — `eq.h` is
   shared source, so the ARM build applies as it did for T05/T07/T08.

Visual gate: not applicable — say so.

## Environment notes

- GLES2 config is the only tree; relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `.../lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.
- The capture's file header is **112 bytes**
  (`SessionLogFileHeader`, `static_assert` in `session_log.h`). Use the
  header structs; do not hand-roll an offset. A previous hand-rolled
  parse assumed 16 and invented six phantom records.

## Review

Per the protocol, plus:
- Confirm `GetSessionKey()` was added and that no test logs its value.
- Any parser finding, prominently.
- Whether the character-list storage question is worth raising to Will,
  with your read on what it would cost.
