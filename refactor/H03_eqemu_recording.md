# H03 — eqemu automation and the scripted recording tour

Automate the akk-stack eqemu server lifecycle and produce a recorded
session (data/captures/auto_session.sleq) via a scripted headless willeq
run — arming the Batch R replay oracle's golden mode with zero manual steps.

## Hard rules (from Will — also in orchestrator memory)

- MAY start/stop containers and modify the database.
- MAY flip server debug flags / enable/disable features.
- MUST NOT modify akk-stack server code (/mnt/projects/akk-stack/code or
  server binaries/scripts) — if something seems to require it, STOP and
  report; do not work around.
- Lifecycle operations (character creation etc.) have eqemu GM commands
  (`#`-prefixed in-game, some `/` forms) that respect game-state
  requirements — PREFER these over raw DB writes; use DB only where no
  command path exists, and document each DB write.

## Design

1. **Server control scripts** in `scripts/eqemu/` (new, in the WILLEQ repo —
   not in akk-stack):
   - `up.sh` — docker compose up -d (compose file at /mnt/projects/akk-stack),
     then wait-for-ready: poll until login (5998) / world (9000) accept
     connections, with timeout + clear failure output.
   - `down.sh`, `status.sh`.
   - `seed.sh` — ensure a test account + character exist. Investigate the
     sanctioned paths first: eqemu's world/zone console (telnet console if
     enabled), server rules/flags for auto account creation, GM commands.
     Explore /mnt/projects/akk-stack (Makefile, README, server config JSONs)
     READ-ONLY for how this stack is set up. Document exactly how seeding
     works and which path you chose.
2. **Scripted client tour**: drive headless willeq deterministically.
   Investigate how commands can be fed to a headless client (existing stdin
   handling? command processor entry points?). If no path exists, add a
   small CLIENT feature (willeq code is fair game): `--script <file>` — a
   timed command list (e.g. `wait 5`, `/moveto loc`, `/attack`, `/camp`)
   executed through the existing CommandRegistry/ProcessChatInput machinery
   on the game thread. Keep it minimal — no new scripting language, just
   `wait N` + slash commands, one per line.
3. **The tour itself** (`scripts/eqemu/record_tour.sh`): server up → run
   willeq headless with --record-session data/captures/auto_session.sleq and
   the tour script (connect to localhost login → world → zone in → move
   between a few waypoints → engage a target if feasible → /camp) → verify
   the .sleq is non-trivial (record count via a tiny reader or the R01 test
   utility pattern) → server down (leave DB state consistent).
4. **Oracle hookup**: run `ctest -R SessionReplayGolden` with
   WILLEQ_SESSION_LOG pointing at the new recording — first run creates the
   golden, second run must PASS. Both runs happen in this unit; commit the
   .golden? NO — data/captures/ is gitignored; instead document in the
   review where the artifacts live and note that CI regenerates the golden
   whenever the recording is regenerated (recording + golden are a pair).

## Verification

- `record_tour.sh` end-to-end on this VM: server comes up, tour runs,
  recording has >200 zone-stream records, server shuts down cleanly.
- `SessionReplayGolden` passes against the recording on its second run.
- Full ctest on the default build stays green modulo documented exceptions.
- Nothing under /mnt/projects/akk-stack/code or server scripts modified
  (state this explicitly in the review; DB/config changes listed).

## Review

### Environment discovered

This VM already had a persisted akk-stack mariadb data volume from prior
manual work: a world account `willeq_test` (id 70, status 200/GM,
`lsaccount_id=1`) with one character `Testchar` (id 65, level 1, zone_id 77
"arena"), and a `login_accounts` row for `willeq_test` with an unknown
password hash. This unit builds on that rather than re-seeding from empty —
see "Seeding path" below for what would be needed on a truly fresh install.

### Server control scripts (`scripts/eqemu/`)

- `env.sh` — shared config (container names, ports, `compose()`/`mysql_exec()`/
  `server_exec()` helpers), sourced by the others.
- `up.sh` — `docker compose up -d mariadb eqemu-server` (same two services as
  akk-stack's own `make up`), waits for mariadb, then runs
  `./bin/shared_memory` once and starts `./bin/loginserver`, `./bin/world`,
  `./bin/zone <shortname>` (default `arena`) in the background inside the
  container, polling the login (5998)/world (9000) TCP ports and the zone log
  for its "bootup" line.
- `down.sh` — `pkill` the three server processes, then
  `docker compose down --timeout 3` (mirrors akk-stack's `make down`).
- `status.sh` — container list, server process list, host port check.
- `record_tour.sh` — up.sh → seed.sh → write a client config + tour script →
  run `willeq --record-session --script` → verify record count → down.sh
  (via `trap ... EXIT`, so the server comes down even on failure).
- `count_zone_records.py` — tiny reader for `session_log.h`'s binary format
  (header + record-header + payload loop) that counts Zone-stream records;
  avoids needing a compiled C++ utility just to verify the recording.

**Why binaries are started directly instead of via akk-stack's own launcher**:
akk-stack ships `spire eqemu-server:launcher {start,stop}` (a Go tool,
config-driven from `eqemu_config.json`'s `web-admin.launcher` block:
`runLoginserver`/`runQueryServ`/`runUcs`/`minZoneProcesses`/`staticZones`),
which is the "intended" way to run the full multi-process stack. On this
host it was stuck reporting `"launcher process already running"` on every
`start` attempt, with no corresponding OS process, listening port, PID file,
or discoverable DB state (checked `ps`, `ss -tlnp`, files modified in the
last 10 minutes across the container, and every `peq` table with
`process`/`status`/`server`/`launcher` in its name) — `stop` eventually
reported success (after ~2.5 minutes) but `start` still refused afterward.
Rather than debug a closed-source Go binary's internal state further, this
unit drives the same underlying binaries (`bin/shared_memory`,
`bin/loginserver`, `bin/world`, `bin/zone`) directly with `docker exec`/
`docker exec -d`, in the same order the launcher would use them. This is
normal eqemu server *operation* (running the shipped binaries with their
standard arguments), not a modification of `/mnt/projects/akk-stack/code` or
any server script.

### Seeding path chosen (`seed.sh`)

- **Account creation**: not this script's job — `server/login.json` already
  ships `"auto_create_accounts": true` (akk-stack default, unmodified),
  so any login with an unknown account name is auto-created by the login
  server. Zero DB writes, zero config changes.
- **Password**: the existing `willeq_test` account's password hash was
  unknown (scrypt-hashed, created `2026-04-26` by some prior session).
  `seed.sh` resets it every run via the loginserver binary's own built-in
  admin subcommand: `./bin/loginserver login-user:update-credentials
  willeq_test WillEqH03Pass` (registered as
  `login-user:update-credentials` in
  `loginserver/loginserver_command_handler.cpp` — a shipped server *tool*,
  invoked, not a server *code* change). Idempotent; safe to run every time.
- **Character creation**: investigated and NOT implemented as an automated
  path — see "Character creation gap" below. `seed.sh` verifies the account
  and character exist (`SELECT id FROM account ...`, `SELECT id, zone_id
  FROM character_data ...`) and `STOP`s with a clear message (exit 1) if
  either is missing, rather than guessing at an untested raw-SQL fallback.
  Since this VM already has both, `seed.sh` succeeds today without ever
  reaching that stop path.

#### Character creation gap (investigated, not closed)

Willeq's client has no `OP_CharacterCreate` implementation at all — no
opcode in `TitaniumWorldOpcodes`, no `CharCreate_Struct` in
`packet_structs.h`, no send/process method anywhere in `include/client` or
`src/client` (confirmed by an Explore-agent grep sweep). The server side
(`akk-stack/code/world/client.cpp:1677`, `Client::OPCharCreate`) builds a
full `PlayerProfile_Struct` from a 140-byte `CharCreate_Struct` the *client*
is expected to send after `OP_CharacterCreateRequest`/`OP_ApproveName` —
implementing that client-side is out of scope for this unit (the plan's
design section only calls for the `--script` tour feature, not new wire
protocol). No console/telnet/GM command creates a character from nothing
either (eqemu's telnet world console — `code/world/console.cpp` — has no
account/character-creation verbs; GM commands like `#character` only edit
an *existing* character). Net effect: on a truly empty akk-stack database,
this unit's scripts cannot yet seed a first character unattended. `seed.sh`
documents this explicitly and stops cleanly rather than writing an untested
raw `character_data` INSERT — a real fresh-install fallback would need to
replicate `OPCharCreate`'s full `PlayerProfile_Struct` construction
(race/class/deity validation via `CheckCharCreateInfoTitanium`, starting
skills/languages via `SetRaceStartingSkills`/`SetClassStartingSkills`,
starting zone via `GetStartZone`, spellbook/buff array initialization,
etc. — `akk-stack/code/world/client.cpp:1677-1900+`), which felt too risky
to add un-reviewed and untested in this pass.

### Tour mechanism: `--script <file>` (willeq client change)

Investigated first (Explore agent): the existing headless input path
(`ConsoleInputHandler` → `InputActionBridge::update()` →
`CommandProcessor::processCommand()`, all on the game thread) reads from a
background stdin thread and is unsuitable for a scripted, timed tour (it
assumes an interactive TTY and is gated off whenever graphics are
initialized — `Application::processInput`, `application.cpp:745`, to avoid
double-processing keystrokes in DRM mode). Per the plan's fallback, added a
minimal `--script <file>` feature instead of extending that machinery:

- `include/client/application.h`: `ApplicationConfig::scriptPath` (new
  field); `ScriptStep` struct (`isWait`/`waitSeconds`/`commandText`);
  `Application::loadScriptFile()` / `Application::updateScript()` private
  methods; `m_scriptSteps`/`m_scriptIndex`/`m_scriptWaitRemaining`/
  `m_scriptWaitStarted` members (game thread only, no locking needed since
  nothing else touches them).
- `src/client/application.cpp`: `--script <file>` parsed alongside
  `--record-session` in `parseArguments()`; loaded via `loadScriptFile()`
  right after `CommandProcessor` construction in `initialize()` (needs
  `m_commandProcessor` to exist first — it doesn't need to be *usable* yet,
  just constructed); `updateScript(deltaTime)` called unconditionally at the
  end of `processInput()` (same game-thread tick as
  `m_inputBridge->update()`).
- Format: blank lines and `#`-comments ignored; `wait N` counts down `N`
  seconds of tick `deltaTime` before advancing; any other line is passed
  verbatim to `m_commandProcessor->processCommand()` — exactly the sink a
  live slash command reaches. A command step is *held* (not consumed, not
  dropped) until `m_fullyConnected` is true, so a script that starts with a
  command rather than a `wait` doesn't silently lose it during the
  login/world/zone-in handshake.
- No new scripting language, no threading changes, no interaction with
  `ConsoleInputHandler`/graphics input paths.

### Real bug found and fixed while arming the oracle

Running the tour's `/camp` step (`ActionDispatcher::startCamp()` →
`EverQuest::StartCampTimer()` → `SendCamp()` → `OP_Camp`) reliably segfaulted
the client (`DaybreakConnectionManager::Tick()`, null `m_transport` deref,
confirmed via `gdb catch throw`/backtrace/register dump, bisected by testing
`/list`-only, `/say`-only, and `/camp`-only scripts in isolation). Root
cause, traced end to end:

1. Our test account is GM-status (200). eqemu's `Client::Handle_OP_Camp`
   (`akk-stack/code/zone/client_packet.cpp:4269`) fast-paths GMs straight to
   `OnDisconnect(true)` (`EnableHackedFastCampForGM` rule is off by default,
   so GMs skip the normal 29-second camp timer entirely) — the server sends
   back an almost-immediate `OP_LogoutReply`.
2. `EverQuest::ZoneProcessLogoutReply` (`src/client/eq_player_info.cpp:898`,
   pre-existing code, not touched by R01/R02) called
   `DisconnectFromZone()` **directly**, which resets
   `m_zone_connection_manager` — destroying the very
   `DaybreakConnectionManager` object whose `Tick()` is still on the call
   stack (`Tick()` → `ProcessPacket()` → ... → `ZoneOnPacketRecv` →
   `DispatchZonePacket` → `ZoneProcessLogoutReply` → `DisconnectFromZone()`).
   The `for (;;)` receive loop in `Tick()` then dereferences a dangling
   `m_transport` on its next iteration — a use-after-free, 100% reproducible.
3. This is a **real, pre-existing client bug**, not specific to `--script` or
   headless mode — any real player camping while GM-flagged (or any server
   configured to fast-track camp for anyone) would hit it. `eq_zone.cpp`
   already documents the general hazard and the fix pattern for a sibling
   case (`m_zone_change_approved`, deferred to `UpdateMovement()`, "outside
   of any callback context").
4. **Fix** (`include/client/eq.h`, `src/client/eq_player_info.cpp`,
   `src/client/eq_movement.cpp`): added `m_pending_logout_disconnect`,
   mirroring `m_zone_change_approved` exactly.
   `ZoneProcessLogoutReply` now only sets the flag; `UpdateMovement()` (called
   unconditionally every game tick, in headless mode too) checks it first and
   calls `DisconnectFromZone()` there instead — outside the packet-callback
   stack. Verified fixed under `gdb` (clean exit, 302-313 records recorded,
   no crash) with the exact same script that crashed before the fix.

A second, independent bug surfaced arming the golden oracle itself:
`EverQuest::ZoneProcessStamina` (`src/client/eq.cpp:3269`) assumed a
`spawn_id(2) + current_stamina(4) + max_stamina(4)` layout requiring 12
bytes (2 opcode + 10) but only guarded for 10, and replaying a real captured
`OP_Stamina` record (8-byte payload) threw `std::out_of_range` inside
`EQ::Net::Packet::GetUInt32` (`ZoneProcessStamina` → `DispatchZonePacket` →
`ReplayZonePacket`, confirmed via `gdb catch throw`). Checked the real
protocol against every `OP_Stamina` sender server-side
(`akk-stack/code/zone/client.cpp`: `SetHunger`/`SetThirst`/`SetConsumption`)
— the real `Stamina_Struct` is just `{ uint32 food; uint32 water; }`, always
about the player's own character, no `spawn_id` field at all. Fixed to parse
the real 8-byte layout (offsets 2 and 6) with a corrected `>= 10` guard (2
opcode + 8 payload). This was a dead/no-op handler before (`// TODO: Store
stamina values...`) so the fix has no behavioral risk beyond "stop
crashing and log the right numbers."

Both fixes are additive, narrowly scoped, and match the "fix only with
bounds/logic checks correct for real traffic, document each" principle used
in Batch R.

### Recording stats

`record_tour.sh` end to end (server up → seed → tour → verify → server
down), tour script (`data/captures/tour.txt`, generated by
`record_tour.sh`):

```
wait 10
/say #npctypespawn 351043   # GM-spawn a weak "a_rat" (npc_types id 351043,
                             # level 1, 11 hp) — arena's own spawn2 table has
                             # 0 entries (SELECT COUNT(*) FROM spawn2 WHERE
                             # zone='arena' -> 0), so nothing wanders in on
                             # its own. #npctypespawn is a real eqemu zone GM
                             # command (code/zone/gm_commands/npctypespawn.cpp);
                             # reachable via /say because
                             # Client::ChannelMessageReceived() treats any
                             # chat message starting with '#' as a GM command
                             # on any channel, for GM-status accounts.
wait 2
/list
wait 1
/target                     # bare -> targetNearest()
wait 1
/attack
wait 4
/stopattack
wait 1
/move 200 -1009 51
wait 3
/move 200 -960 51
wait 3
/move 146 -1009 51
wait 2
/camp
```

Result: `data/captures/auto_session.sleq` — 347 total records, **334
zone-stream records** (well over the 200 required), clean process exit
(code 0), server brought down cleanly by `down.sh` afterward. "Engage a
target" turned out to be feasible after all (arena ships four other static
NPCs too — training dummies, DPS machines, a "Soulbinder_Gendal" NPC, and a
"zone_controller" — visible in `/list` output alongside our spawned rats,
though the rat was the only one close enough to target/attack without
walking).

### Oracle results

- First `ctest -R SessionReplayGolden` run against the recording (before the
  stamina fix): **failed** with `std::out_of_range` ("Packet read out of
  range") — the real bug above.
- After the stamina fix, first run: `Skipped` — "Golden created ... re-run
  to verify" (`data/captures/auto_session.golden` written, 664 bytes).
- Second run: **Passed**.
- Full `ctest --test-dir build` with `WILLEQ_SESSION_LOG` set: **1223/1224
  passed**. The sole failure, `AudioManagerTest.PreloadCommonSounds`, is the
  same pre-existing ALSA-environment exception documented in R01/R02's
  reviews and `refactor/test_baseline.txt` (no real audio device in this
  container) — unrelated to this unit. All other skips (pcap-capture-gated,
  door-graphics-gated, etc.) match the documented baseline.

### Every DB/config change made

- **Zero modifications** under `/mnt/projects/akk-stack/code` or any server
  script — confirmed (`git -C /mnt/projects/akk-stack/code` is not even a
  separate repo boundary here, but no file under that directory tree was
  ever opened for writing; only `Read`/`grep` for investigation).
- **`login_accounts.account_password`** for account `willeq_test`: reset via
  `./bin/loginserver login-user:update-credentials willeq_test
  WillEqH03Pass` (run every time `seed.sh` runs — idempotent, not a raw SQL
  write, uses the server's own admin CLI tool).
- **No other DB writes.** No raw SQL `INSERT`/`UPDATE` was executed anywhere
  in this unit — account, character, and all companion rows (skills,
  currency, inventory, etc.) were pre-existing and untouched.
- **No `eqemu_config.json`/`login.json` changes ship in this unit.** (A
  `runLoginserver`/`minZoneProcesses` edit was tried transiently while
  investigating the spire launcher, then reverted before settling on driving
  the binaries directly — `server/eqemu_config.json` is byte-identical to
  its state before this unit started.)
- Server left **down** at the end of this session (`down.sh` run, verified
  `docker ps -a` shows no akk-stack containers).

### Deviations from the plan

- Step 4 of the design ("Oracle hookup") is satisfied as specified: golden
  created on first run, PASS on second, both in this unit, artifacts left
  under gitignored `data/captures/` (not committed).
- The plan's `seed.sh` design anticipated needing to create an account and
  character from scratch; in practice this VM already had both from prior
  work, so `seed.sh`'s "create" responsibility narrowed to "verify + reset
  password." The character-creation gap this would otherwise require is
  fully investigated and documented above rather than worked around.
- Two pre-existing client bugs (`ZoneProcessLogoutReply`'s unsafe synchronous
  `DisconnectFromZone()`, `ZoneProcessStamina`'s wrong struct layout) were
  fixed as a direct precondition of getting real-traffic verification
  (step 1's `/camp` and the golden oracle) to complete without crashing —
  not originally anticipated by the plan, but within its "willeq client code
  changes are fair game" allowance and the project's established practice
  (Batch R) of fixing real-traffic-exposed bugs with narrowly-scoped,
  documented changes.
- `eqemu-server:launcher`'s stuck state (see above) meant not using
  akk-stack's own orchestration tool as originally implied by the plan's
  "wait-for-ready" language; documented in detail rather than silently
  worked around, per the hard rules' spirit (investigate honestly, don't
  thrash, don't touch server code).
