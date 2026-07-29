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

**Status: DONE.**

### Acceptance criteria

1. **Exactly the eleven accessors in `eq.h`, nothing else changed there.**
   PASS. Diffed against the actual checkout base (`f570b2b`, not the
   `fable` ref, which moved forward mid-unit — see "Deviations" below):
   `include/client/eq.h` gained exactly 14 lines (11 accessors + 3
   comment lines), one hunk, nothing else touched.
2. **Tests exercising each one, driven from real recorded records where
   available.** PASS. 9 new tests in a new `LoginWorldAccessors` suite
   in `tests/test_login_world_flow.cpp` (extended, not a new file —
   794 lines total, well under the 1500-line warn ceiling). 6 of the 9
   drive from the real capture (`GTEST_SKIP` if absent, matching
   section 1's existing idiom); the other 3 are synthetic where the
   plan called for specific/contrast values (character-not-found,
   5-field chat-server happy path, ExpansionInfo world-ready flags).
   Every one of the 11 accessors is exercised by at least one test.
3. **Build clean; test binaries under 5 seconds each.** PASS.
   `test_login_world_flow` (36 tests total including the pre-existing
   T05-T08 suite): ~0.07-0.08s wall time across 3 standalone runs.
4. **`test_session_replay` golden matched, `session.golden` md5
   unchanged.** PASS. `4d55b40a672cec0ae6022f60a21b7b25` before and
   after all 3 full-suite runs below — unchanged throughout.
5. **Full suite x3, documented exceptions only.** PASS, 1415/1416 all
   three runs, only `AudioManagerTest.PreloadCommonSounds` (host ALSA)
   failed each time. No `RDPServerFrameTest`/`RDPServerLifecycleTest`
   failures occurred in any of the 3 runs (that family is intermittent,
   not "always fails" — its absence here doesn't contradict T03b).
   Run times: 315.96s, 283.12s, 280.71s.
6. **`check_file_sizes.sh` — no new FAILs.** PASS, with a process note:
   running the script unmodified from this agent's own cwd (a sibling
   worktree, not this slot) makes its internal `git rev-parse
   --show-toplevel` resolve to the wrong repo root and diff the wrong
   tree. Verified correctly two ways instead: (a) `git -C <slot> diff
   --name-only fable -- '*.cpp' '*.h' '*.hpp'` lists exactly the two
   changed files; (b) `check_file_sizes.sh --files <slot>/include/client/eq.h
   <slot>/tests/test_login_world_flow.cpp` (absolute paths, bypasses the
   cwd issue) → `eq.h` WARN at 1949 lines (pre-existing: 1935 lines on
   the checkout base already, so T09 only added 14 — not a new
   crossing), `test_login_world_flow.cpp` ok at 794 lines. Exit 0.
7. **`build-arm-noble.sh` passes.** PASS, clean cross-compile,
   `build-arm-noble/bin/willeq` produced (ARM ELF, 338M unstripped,
   debug symbols retained per the script's default).

Visual gate: not applicable — no graphics/rendering code touched, no
new visual assertions added.

### `GetSessionKey()`

Added exactly as specified (`const std::string& GetSessionKey() const {
return m_key; }`, eq.h, grouped with the other 10 T09 accessors). No
test logs, prints, or otherwise surfaces its value beyond a single
`EXPECT_EQ(eq.GetSessionKey(), "BW8BZW8UW4")` comparison against the
real capture's known decoded value (gtest only prints comparison
operands on assertion *failure*, which is standard/unavoidable test
practice, not logging).

### Parser finding (prominent)

**`WorldProcessSetChatServer` never populates its accessors on this
server's real wire format.** The handler
(`eq_login_world.cpp:743`) splits the chat-server string on `,` and
requires `parts.size() >= 5` (host, port, `server.character`,
connection_type, mail_key) before writing `m_ucs_host`/`m_ucs_port`/
`m_mail_key`; otherwise it logs a warning and leaves them untouched.
The real capture's `SetChatServer` payload
(`data/captures/session.sleq`, both zone-ins) is:

    10.0.30.23,7778,WillEQ Private Server.Testchar,B48FB5F37

— exactly **4** comma-separated fields (host, port, "server.character"
containing an embedded space but no comma, mail_key), one short of the
5 the parser requires. Verified by direct split of the decoded payload
bytes (not guessed) before writing
`BUG_SetChatServer_RealCapture_FourFieldFormatNeverPopulatesAccessors`,
which pins the real observed behavior: `GetChatServerHost()`/
`GetChatServerPort()`/`GetMailKey()` all stay at their construction
defaults after replaying the real record. A second synthetic test
(`SetChatServer_FiveFieldSynthetic_ParsesHostPortMailKey`) confirms the
split logic itself is correct when actually given 5 fields — so this
is specifically a field-count mismatch between what the handler expects
and what this server (WillEQ Private Server, P29 recording) sends, not
a broken parser overall. Two readings, Will's call: either the expected
5-field format is wrong for Titanium (should be 4), or this server
build never sends a working `connection_type` field and UCS/mail never
actually connects on it. Not fixed here — territory is tests/ only.

### Character-list storage question — is it worth raising to Will?

Yes, worth a short ask, low cost either way. The plan is right that
`WorldProcessCharacterSelect` currently discards 9 of 10 slots' worth
of parsed data (name/level/class/race/zone for every non-matching
character) and keeps only `m_character_select_index` for the one that
matches `m_character`. Cost to expose it properly: one new member (e.g.
`std::vector<CharacterSelectEntry> m_character_list;`, 10 or fewer
entries, POD-ish struct) populated in the existing loop instead of
discarded, plus one accessor — a small, mechanical, behavior-preserving
change (nothing currently reads or depends on the discarded values, so
there's no risk of behavior change, only added storage). The real
payoff isn't more test coverage of parsing (this unit already got that
for free by reading the fields locally in the real-capture test's
decode step, without needing the accessor) — it's whether any *product*
code (a character-select UI, `/who`-style tooling) will ever want the
list. If not, it's pure unused storage and not worth it. Flagging for
Will rather than deciding either way.

### Deviations

- `git diff fable -- include/client/eq.h` (comparing to the *branch
  ref* `fable`, not this task's checkout point) shows a second,
  unrelated hunk reverting P35's `m_zone_map` shared_ptr fix. This is
  **not** a T09 change — `fable` moved forward mid-unit (P36 planning
  commit `8eb2fb7`, on top of P35's `a376db2` which touched
  `m_zone_map`) after this branch was cut from `f570b2b`. Confirmed via
  `git merge-base fable task/T09-login-world-accessors` == `f570b2b`,
  and `git diff f570b2b -- include/client/eq.h` shows only the 14-line
  T09 hunk. Not rebased onto the newer `fable` tip — out of scope for
  this unit, and the orchestrator's merge will resolve it normally.
- `check_file_sizes.sh fable` (no explicit repo_root override) can't be
  run correctly from this agent's own shell cwd (a different worktree);
  worked around per criterion 6 above rather than `cd`-ing (shell
  discipline forbids it). Flagging in case other T-unit executors hit
  the same thing.
- No other deviations. No accessor requested outside the IN list, no
  new member variables, no `.cpp` changes, no STOP conditions
  triggered.
