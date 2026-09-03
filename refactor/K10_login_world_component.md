# K10 — `LoginWorldComponent` (family: login_world — login server, world server, character select)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K06/K07). Wave 3; runs alongside K08. **K09 waits for both.**

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

One TU, `eq_login_world.cpp`, 37 bodies. Almost every method is internal to
the family (`Login*`, `World*` handlers and senders). External callers found
by grep: `eq.cpp` (the connection-manager callback wiring for
`LoginOnNewConnection`, `LoginOnPacketRecv`, `LoginOnStatusChange*`,
`WorldOn*`), `eq_zone.cpp` (`WorldOn*` — the zone-transition path
re-enters world), `eq_player_state.cpp` (`WorldProcessZoneServerInfo`).
Compiler confirms.

Members:
- **move** (single-family): `m_character_select_index`,
  `m_character_select_list`, `m_login_sequence`, `m_mail_key`,
  `m_ucs_connection_type`, `m_ucs_host`, `m_ucs_port`, `m_world_servers`.
  ⚠ the three `m_ucs_*` feed the DEAD `UCSConnection` (BOARD B005); move
  them anyway — deleting is B005's job.
- **decision — the connection managers.** `m_login_connection`,
  `m_login_connection_manager`, `m_world_connection`,
  `m_world_connection_manager`, `m_world_ready`, `m_world_server_host`,
  `m_enter_world_sent`, `m_zone_server_host/port`, `m_user`, `m_pass`,
  `m_server`, `m_host`, `m_port`, `m_key`, `m_dbid`, `m_session_recorder`,
  `m_zone_to_zone_in_progress`. Readers outside the family: `eq.cpp`
  (ctor/teardown/tick), `eq_zone.cpp` (zone transition), `eq_commands.cpp`
  (`/who`-style status), `ucs.cpp` (dead). The batch doc says the
  connection managers "move with it". **Do it in this shape:** the login
  and world managers + their connections + credentials + `m_world_servers`
  move into the component; `EverQuest` keeps `m_zone_connection` /
  `m_zone_connection_manager` (core) and `m_session_recorder` (core-shaped,
  ctor-owned). `eq.cpp`'s tick/teardown reach the moved managers through
  `LoginWorld().LoginManager()` / `.WorldManager()` getters; `eq_zone.cpp`'s
  reads likewise. If the ctor wiring of the manager callbacks cannot move
  cleanly (the managers are constructed in `EverQuest`'s ctor with
  `std::bind`/lambdas capturing `this`), STOP and report the exact shape —
  do not half-move.
- `m_replay_suppressed_sends` (12 here, also zone handshake + player buffs)
  stays, `owner_.`; K09 decides.
- `m_x/m_y/m_z` hits are `PlayerState`'s, not `EverQuest`'s — ignore.

`DispatchLoginPacket` / `DispatchWorldPacket` / `ReplayLoginPacket` /
`ReplayWorldPacket`: the replay pair is the session-replay oracle's entry
(`test_session_replay`) — grep `tests/` for them and retarget.

Predicted `eq.h` delta: −37 declarations −8 members −(managers block if it
moves, ≈8) −banners +4 ≈ **−45 or better**.

## Territory

`include/client/login_world/` + `src/client/login_world/` (new),
`src/client/eq_login_world.cpp` (moves), `eq.h`, `eq.cpp` (ctor, tick,
teardown call sites), root `CMakeLists.txt`, `tests/test_login_world_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in
`eq_zone.cpp`, `eq_player_state.cpp`, `eq_commands.cpp`, and tests
(`test_login_world_seam.cpp`, `test_login_world_flow.cpp`,
`test_session_replay*.cpp` — call sites only, assertions untouched; if a
seam test reaches a private through `Test_*`, move the seam with it).

## Verification / Review

Executed in `wt/slot2` on `task/K10-login-world-component`, branched from
`fable` at `e6d42422`. Ran alongside K08 (slot1). §Mechanism REVISED followed
as written, including the K03–K07 correction blocks. **No STOP condition was
hit** — see the connection-manager section below for why.

### The connection-manager decision, as executed

The plan's STOP condition was "if the ctor wiring of the manager callbacks
cannot move cleanly (the managers are constructed in `EverQuest`'s ctor with
`std::bind`/lambdas capturing `this`), STOP and report the exact shape". The
premise turned out to be **wrong in a way that made the move easier, not
harder**: the managers are NOT constructed in `EverQuest`'s constructor. They
are built in three places, none of them the ctor:

| site | what it does |
|---|---|
| `EverQuest::ConnectToLogin()` (`src/client/eq.cpp`) | builds the login manager, binds `LoginOnNewConnection` / `LoginOnStatusChangeReconnectEnabled` / `LoginOnPacketRecv` |
| `EverQuest::ConnectToWorld()` (`src/client/eq.cpp`) | builds the world manager, binds the three `WorldOn*` |
| `EverQuest::ProcessDeferredZoneChange()` (`src/client/eq_zone.cpp`) | REBUILDS the world manager on a zone-to-zone transition and re-binds the same three `WorldOn*` |

`EverQuest`'s constructor only assigns the credentials
(`m_host`/`m_port`/`m_user`/`m_pass`/`m_server`/`m_dbid`). So the shape was:

- **`ConnectToLogin()` and `ConnectToWorld()` moved into the component** with
  the family. They are the code that constructs the managers and binds the
  callbacks; leaving them in `eq.cpp` would have meant writing
  `std::bind(&eqt::login_world::LoginWorldComponent::LoginOnPacketRecv, m_login_world.get(), …)`
  out in `eq.cpp` three times over. Inside the component the binds are plain
  `&LoginWorldComponent::X, this` — **identical in shape to the lines they
  replace**. These two are the only bodies this unit moved out of a TU other
  than `eq_login_world.cpp`; the eq.cpp-side residual proof below shows the
  move is line-for-line.
- **`ProcessDeferredZoneChange()`'s rebuild block stayed in `eq_zone.cpp`**
  (K09's territory; the plan grants CALL-SITE-ONLY edits there) and was
  retargeted through reference-returning transition getters. That block is the
  reason `WorldOnNewConnection` / `WorldOnStatusChangeReconnectEnabled` /
  `WorldOnPacketRecv` are **public** on the component and everything else in
  the family is private: friendship runs `EverQuest → component`, not the
  other way, so `EverQuest::ProcessDeferredZoneChange` cannot name a private
  component member.
- `EverQuest` keeps `m_zone_connection` / `m_zone_connection_manager` (core),
  `m_session_recorder`, `m_zone_to_zone_in_progress`,
  `m_replay_suppressed_sends`, and — a change from the plan — **`m_zone_server_host` /
  `m_zone_server_port`**, which have three real reads in
  `EverQuest::ConnectToZone` (`eq.cpp`, the zone family) against one write
  here. `owner_.m_zone_server_*`; their inline `GetZoneServerHost()` /
  `GetZoneServerPort()` accessors stayed with them.
- One new one-line method, `SetCredentials(host, port, user, pass, server)`,
  carries the ctor's six assignment lines verbatim (the K07 `ResetPath()`
  precedent). `EverQuest`'s ctor calls it once.

**Transition scaffolding on the component** (K-final removes it, as with
`owner_`): `LoginManager()`, `LoginConnection()`, `WorldManager()`,
`WorldConnection()`, `WorldServerHost()`, `WorldReady()`, `EnterWorldSent()` —
reference-returning getters for the five production sites outside the family
that still touch this state (`TickNetwork`, `~EverQuest`,
`ProcessDeferredZoneChange`, the `/connections` command).

### Compiler-derived caller list

All 37 declarations plus `ConnectToLogin` / `ConnectToWorld` and the eleven
inline `eq.h` accessors were deleted from `eq.h`, the lead sites retargeted in
the same pass, and the tree built (§Mechanism 5, K05 correction 3). The build
went clean, which is the compiler's confirmation that no caller was missed.

| file | sites | receiver expression | change |
|---|---|---|---|
| `src/client/eq.cpp` | 12 | bare / `m_login_connection*` / `m_world_connection_manager` | ctor credentials → `m_login_world->SetCredentials(…)`; `TickNetwork`'s two manager ticks → `m_login_world->LoginManager()` / `WorldManager()`; `~EverQuest`'s two resets → `m_login_world->LoginConnection()` / `LoginManager()`; `ConnectToLogin` / `ConnectToWorld` bodies removed entirely |
| `src/client/eq_zone.cpp` | 10 | bare, inside `EverQuest::ProcessDeferredZoneChange` | `m_world_ready` / `m_enter_world_sent` / `m_world_connection` / `m_world_connection_manager` / `m_world_server_host` → the component's getters; the three `std::bind(&EverQuest::WorldOn*, this, …)` → `std::bind(&eqt::login_world::LoginWorldComponent::WorldOn*, m_login_world.get(), …)` |
| `src/client/eq_commands.cpp` | 2 | bare | `addConn("World"/"Login", …)` → `m_login_world->WorldConnection()` / `LoginConnection()` |
| `src/client/application.cpp` | 1 | `m_eqClient->` | `ConnectToLogin()` → `m_eqClient->LoginWorld().ConnectToLogin()` |
| `tests/test_login_world_flow.cpp` | 38 | `eq.` → `eq.LoginWorld().` | the two replay seams and all eleven T09/T14 observers. **Assertions untouched** |
| `tests/test_login_world_seam.cpp` | 5 | `eq.` → `eq.LoginWorld().` | **Assertions untouched** |
| `tests/test_zone_position_convention.cpp` | 1 | `eq.` → `eq.LoginWorld().` | `ReplayWorldPacket` |
| `tests/test_beneficial_spell_casting.cpp`, `tests/test_zoning_graphics_integration.cpp`, `tests/test_zoning_integration.cpp` | 1 each | `eq_->` → `eq_->LoginWorld().` | `ConnectToLogin` |

**Where the plan's leads were wrong** (K04 correction g / K06's confirmation
of it — comment-filter every lead):

| plan lead | measured |
|---|---|
| `eq_player_state.cpp` calls `WorldProcessZoneServerInfo` | **comment only** (`eq_player_state.cpp:509`, a prose note). Zero call sites |
| `eq_zone.cpp` calls the `WorldOn*` | true, but as `std::bind` ARGUMENTS in the manager rebuild, not as calls |
| `eq_commands.cpp` reads connection state for "`/who`-style status" | it is the `/connections` command, 2 sites, and it reads the two `shared_ptr`s, not the managers |
| `ucs.cpp` (dead) reads the credentials | **no.** `UCSConnection` declares its OWN `m_host` / `m_port` / `m_key` / `m_dbid` (`include/client/ucs.h` 54–59). Every `ucs.cpp` hit is that class's member — the K06 correction-4 name-collision trap, four members deep |
| `m_x/m_y/m_z` hits are `PlayerState`'s | confirmed — they are inside a T55/T288 comment, no code reference |
| "`DispatchLoginPacket` / … / the replay pair … grep `tests/` for them" | done: `test_login_world_flow`, `test_login_world_seam`, `test_zone_position_convention`. The `Dispatch*` pair has **no external caller at all** — it is reached only through `*OnPacketRecv` and `Replay*`, both in-family, so both stayed private on the component |

**Methods: 37 measured, matching the plan's prose (the batch table's 42 is
wrong).** `grep -n '^[A-Za-z_].*EverQuest::'` over `eq_login_world.cpp` returns
37 bodies. 36 moved; one was deleted:

- **`WorldSendSessionReady()` — DELETED.** Zero callers tree-wide, in-family or
  out (K03 correction a applied: the only occurrences were its own declaration
  and its own definition). Its body was an unconditional `return;` under a
  comment saying the world server does not expect the packet. §Mechanism 5.
  It is invisible to `ReachabilitySweep.EverySenderHasACaller`, whose regex
  only captures names *beginning* `Send`.
- **`WorldSendChannelMessage()` and `WorldProcessChannelMessage()` — DELETED
  declarations.** Found by grepping the family's name prefixes rather than the
  moved TU: both were declared in `eq.h`'s UCS block and **had no definition
  anywhere in the tree** and no caller. Dead declarations, 2 lines + a banner.
  They are in this family's territory by name and were not in the plan's
  tables.

**Inline `eq.h` accessors on a moving member (K04 correction f):** eleven, all
found by grepping `eq.h` for each moving member — `GetAccountId`,
`GetSessionKey`, `IsWorldReady`, `HasSentEnterWorld`, `GetWorldServers`,
`GetCharacterSelectIndex`, `GetCharacterSelectList`, `GetChatServerHost`,
`GetChatServerPort`, `GetMailKey`, `GetChatConnectionType`. All eleven moved
verbatim onto the component and carry 26 test call sites the method table
never saw. `GetZoneServerHost` / `GetZoneServerPort` are the mirror case:
their members stayed, so they stayed.

### Members

| member | plan's decision | measured | outcome |
|---|---|---|---|
| `m_character_select_index`, `m_character_select_list`, `m_login_sequence`, `m_mail_key`, `m_ucs_connection_type`, `m_ucs_host`, `m_ucs_port`, `m_world_servers` | move (single-family) | confirmed: every hit outside the family is the member's own `eq.h` declaration or its inline accessor | **moved** (the three `m_ucs_*` + `m_mail_key` feed the dead `UCSConnection`, BOARD B005 — moved anyway, per the plan) |
| `m_login_connection_manager`, `m_login_connection`, `m_world_connection_manager`, `m_world_connection` | move | `eq.cpp` (build/tick/teardown), `eq_zone.cpp` (rebuild), `eq_commands.cpp` (status) | **moved**, reached through the four transition getters |
| `m_host`, `m_port`, `m_user`, `m_pass`, `m_server`, `m_key`, `m_dbid` | move (credentials) | only external site is the ctor's assignment block | **moved**, ctor calls `SetCredentials()` |
| `m_world_ready`, `m_enter_world_sent`, `m_world_server_host` | in the decision list, unassigned | writers in `eq_zone.cpp`'s rebuild block only | **moved**, reached through `WorldReady()` / `EnterWorldSent()` / `WorldServerHost()` |
| **`m_zone_server_host` / `m_zone_server_port`** | in the decision list | **3 real reads in `EverQuest::ConnectToZone` (`eq.cpp`) vs 1 write here** | **⚠ STAY — deviates from the plan's "move with the managers" framing.** §Mechanism 3's shared rule wins; the readers are the zone family. `owner_.m_zone_server_*` |
| `m_session_recorder` | stays (core-shaped, ctor-owned) | 9 sites in `eq.cpp` | **stays**, `owner_.` |
| `m_zone_to_zone_in_progress` | not named | `eq.cpp`, `eq_zone.cpp`, `eq_player_social.cpp` + two `Test_*` seams | **stays**, `owner_.` |
| `m_replay_suppressed_sends` | stays; K09 decides | 12 here + `eq_zone_handshake.cpp` (11) + `eq_player_buffs.cpp` | **stays**, `owner_.` |
| `m_x/m_y/m_z` | "PlayerState's, ignore" | confirmed — comment text only | — |
| core | | `ctx_.` | `m_game_state` (17), `m_character` (5), `m_zone_connection` (2) |

**22 members moved.**

### Predicted vs measured `eq.h` delta

| | lines |
|---|---|
| at dispatch (`fable` `e6d42422`) | **1,732** |
| plan's pre-dispatch estimate | −45 or better |
| **predicted** before any edit (line-by-line) | **1,637** (−95) |
| **measured** after | **1,630** (**−102**) |

`git diff --numstat` on the single file: **110 deletions, 8 insertions = net
−102**. Broken out:

| hunk | del | ins |
|---|---|---|
| `ConnectToLogin()` + its comment + blank | 3 | 0 |
| `ReplayLoginPacket`/`ReplayWorldPacket` + their 2-line T05 comment + blank | 5 | 0 |
| the eleven T09/T14 observers + the 2-line T14 note (banner and the two `GetZoneServer*` kept) | 13 | 0 |
| the private Login block: banner, 12 decls, `DispatchLoginPacket` + its comment, `m_login_connection_manager`, `m_login_connection`, `m_world_servers`, blanks | 21 | 0 |
| the private World block: banner, `ConnectToWorld`, 20 decls, `DispatchWorldPacket` + its comment, `m_world_connection_manager`, `m_world_connection`, blanks | 32 | 0 |
| the Variables block: `m_host`/`m_port`/`m_user`/`m_pass`/`m_server`/`m_key`/`m_dbid` + blank, the sequence-number banner + `m_login_sequence` + blank, `m_world_ready`/`m_enter_world_sent`, the reconnection banner + `m_world_server_host` + blank | 16 | 4 |
| `m_character_select_index` + `m_character_select_list` + blank | 3 | 0 |
| the UCS block: banner, 3 members, the 5-line T13 note, `m_ucs_connection_type`, blank | 11 | 0 |
| the dead `WorldSendChannelMessage`/`WorldProcessChannelMessage` decls + banner + blank | 4 | 0 |
| the 2 remaining `GetZoneServer*` re-sited under a new 2-line banner | 2 | 0 |
| include / `LoginWorld()` + 2-line note / friend / `m_login_world` | 0 | 4 |
| **total** | **110** | **8** |

The 7-line beat on the prediction is the two dead channel-message declarations
and their banner (not in any plan table) plus the `WorldSendSessionReady` decl.

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**1,732 → 1,630**. ⚠ **Shared file**: K08 lowers this same line. **Measured
value for K10 alone is 1630**; the orchestrator resolves at merge. The same
applies to `eq.h`, `eq.cpp`, root `CMakeLists.txt` and `docs/module_map.md`.

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides, longest-name-first (K04 correction i):
`ctx_.zone_connection` → `m_zone_connection`, then `ctx_.game_state`,
`ctx_.character`; `LoginWorldComponent::` → `EverQuest::`; then un-qualify the
three scope-only forms (`EverQuest::DumpPacket`, `EverQuest::GetOpcodeName`,
`EverQuest::s_debug_level` — K06 correction 6); `owner_.m_` → `m_`; `owner_.`
→ ``; comment, blank and `#include` lines filtered; both sides sorted.

Old side: `src/client/eq_login_world.cpp` at `e6d42422` (618 code lines). New
side: `src/client/login_world/login_world_component.cpp` (650 code lines).

**Residual on code lines: 32 additions, 2 deletions, ZERO modifications.**

- **+30**: the `ConnectToLogin` (10), `ConnectToWorld` (11) and
  `SetCredentials` (7) bodies moved in from `eq.cpp`, plus the two `namespace`
  opening lines; **+6/−0 more** are the four namespace braces and the two
  function-signature continuation lines.
- **−2**: `void EverQuest::WorldSendSessionReady()` and its `return;` — the
  deleted zero-caller sender (its two body comments filter out).

The 30 moved-in lines were checked **against the other side of the move**: the
same normalisation over `src/client/eq.cpp` before and after shows those exact
30 lines leaving `eq.cpp`, plus the retargeted receivers in `TickNetwork` /
`~EverQuest` / the ctor, and nothing else. The move is verbatim in both
directions.

### Files

| file | lines |
|---|---|
| `include/client/eq.h` | 1,732 → **1,630** (−102) |
| `src/client/eq_login_world.cpp` → `src/client/login_world/login_world_component.cpp` | 945 → 996 |
| `include/client/login_world/login_world_component.h` | 197 (new) |
| `tests/test_login_world_component.cpp` | 100 (new) |
| `src/client/eq.cpp` | 1,110 → 1,077 |

**Text readers of the moved TU (K03 correction c):** `grep -rn 'eq_login_world'
tests/ scripts/ docs/ refactor/BOARD.md` before moving anything returns 16
hits — **every one a prose comment naming the old filename with a line
number**, no reader of the file as data. `docs/module_map.md:31` was the one
row citing it and is repointed to the full new path (K05 correction 5: no
"(was …)" prose). `docs/decisions.md:759` names the bare filename in prose, not
a repo-relative path, so `DocCitations` does not resolve it; left as history.
`test_duplicate_state_stores.cpp` and `test_reachability_sweep.cpp` scan `eq.h`
as text and **both pass unchanged** — no file-list edit needed (K05's generic
widening to `include/client/*/*_component.h` already covers this unit).

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Two attempts: the first failed with **exactly 4 distinct errors**, all one cause (`EQ::Net::PosixUdpTransport` — the two moved-in `Connect*` bodies needed `#include "common/net/posix_udp_transport.h"`, which `eq.cpp` supplied and `eq_login_world.cpp` never had; K03 correction e, re-derive the include set for both sides of a move). No timeout, no exit 143 |
| `ctest --test-dir build -j6 --output-on-failure` | **3,555 / 3,556 passed**, 107 skipped, 194 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, all 5 `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (Test #3551, 25.97 s) all PASS |
| login/world flow tests (T05–T09 family) | **36 `LoginWorld*` cases, 0 failures** — `LoginWorldReplaySeam`, `LoginWorldFlow`, `LoginWorldRobustness`, `LoginWorldAccessors`, `LoginWorldCaptureReplay`. **Pass unchanged**: only receivers were retargeted, no assertion touched |
| `tests/test_login_world_component.cpp` (new) | **7/7 pass** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --cached --name-only -M -- src/client/graphics include/client/graphics` is **empty** — this unit changed no graphics file. Goldens untouched. `visual_gate` nonetheless ran and PASSED inside the full ctest run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **814 ok, 0 warn, 0 fail** with the ratchet lowered to 1,630 |

### Corrections to §Mechanism

1. **§Mechanism 5's "no forwarders / delete the declaration" has a visibility
   consequence the batch doc does not state: friendship is one-directional, so
   any moved method a NON-extracted `EverQuest` body binds or calls must be
   PUBLIC on the component.** `EverQuest::ProcessDeferredZoneChange` rebuilds
   the world connection manager and re-binds three `WorldOn*` handlers that
   were `private` on `EverQuest` and that `EverQuest` could therefore name.
   After the move `EverQuest` is *granting* friendship, not receiving it, so
   those three had to become public — the private/public split on a component
   is decided by **who still reaches it from outside**, not by what it was on
   `EverQuest`. Every remaining unit with a callback wired from another
   family's TU will hit this.

2. **A method's construction/wiring site may live in a DIFFERENT family's TU
   than its declaration, and it should move with the family, not stay.**
   `ConnectToLogin`/`ConnectToWorld` were `EverQuest::` bodies in `eq.cpp`
   (the "core" family, which §Families says "stays"), yet they are pure
   login/world wiring: they construct this family's two managers and bind six
   of its handlers. Moving them made the callback rebase a no-op
   (`&LoginWorldComponent::X, this`); leaving them would have spread
   `std::bind(&eqt::login_world::LoginWorldComponent::…, m_login_world.get(), …)`
   across `eq.cpp`. **§Mechanism 4 should say the unit moves the family's
   bodies wherever they live, and name any body it takes out of a TU it does
   not own.** Both are named above and both are proved verbatim by a residual
   run on `eq.cpp` as well as on the moved TU.

3. **A plan's STOP condition can encode a premise that is simply false, and
   the executor's first job is to check the premise, not to look for the
   shape.** This plan's STOP was written for managers "constructed in
   `EverQuest`'s ctor with `std::bind`/lambdas capturing `this`". The ctor
   constructs neither manager; it only assigns credentials. Had the shape been
   matched by name rather than measured, this unit would have STOPped on a
   condition that does not exist in the tree.

4. **`grep -rw <member>` over the tree is unreliable at four-members-deep, not
   just one.** `UCSConnection` (`include/client/ucs.h`) declares `m_host`,
   `m_port`, `m_key` AND `m_dbid` — four of this family's seven credentials,
   all with `EverQuest`'s exact names. The plan's "readers outside the family:
   … `ucs.cpp` (dead)" is entirely that collision. This is K06 correction 4
   (`m_loot_items` on two classes) at four times the width; the rule "confirm
   which class's member a hit belongs to by reading the body" is now
   three-for-three across K03/K06/K10 and should be a plan-template column,
   not a correction.

5. **Dead DECLARATIONS — a name declared in `eq.h` with no definition anywhere
   — are invisible to every measurement §Mechanism uses.** They are not
   `EverQuest::` bodies (so no method count sees them), they are not members
   (so no member grep sees them), and they compile forever because nothing
   calls them. `WorldSendChannelMessage` and `WorldProcessChannelMessage` were
   two such, in this family by name, found only by grepping the family's
   `World*` prefix across `eq.h` rather than across the TU. **A unit should
   grep `eq.h` for its family's NAME PREFIXES, not just for its TU's bodies.**

6. **No other STOP condition was hit.** Every moved body needed only the four
   substitutions §Mechanism 4 allows (this family needed no `this` → `&owner_`
   — the one `std::bind(&EverQuest::…, this, …)` inside the family is a
   member-function pointer rebase, not an identity key). No test asserted on a
   member by name that could not be repointed. The predicted delta was −95
   before any edit and the measured delta is −102.

### Left for the board

- **`EverQuest::WorldSendSessionReady()` deleted** — zero callers tree-wide;
  its body was an unconditional `return;` with a comment saying the world
  server does not expect the packet. §Mechanism 5 deletes; the reachability
  pattern's next instance.
- **`WorldSendChannelMessage` / `WorldProcessChannelMessage` declarations
  deleted** — declared in `eq.h`, **defined nowhere**, called nowhere. A
  different shape from the usual unreached-code finding: not dead code, but a
  dead API promise that has been compiling since the UCS block was written.
  Worth a sweep for others of the same shape (a `eq.h` declaration with no
  definition in the tree) — this unit found two in one family by accident.
- The four `m_ucs_*` / `m_mail_key` members moved into the component and
  remain the only consumers of `OP_SetChatServer`'s payload; `UCSConnection`
  is still dead (BOARD B005). Deleting them is still B005's job, and they are
  now in one place.
