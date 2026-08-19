# T148 — GM status gate, then the nine GM commands

**Slot: slot1. x86. eqemu lock REQUIRED (the gate needs a capture). No Pi.**

**Territory: `src/client/eq.cpp` (`ZoneProcessPlayerProfile` only),
`include/client/eq.h`, `src/client/eq_gm.cpp`, `src/client/action/`,
`include/client/action/`, `src/client/eq_action_handler.{h,cpp}`, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T148/`.**

**T149 (slot2) and T150 (slot3) run in parallel.** T149 owns
`src/client/eq_merchant.cpp`; T150 owns `src/client/eq_player_info.cpp` and
`src/client/eq_group.cpp`. All three of you edit `src/client/action/*` and
`eq_action_handler.*` — **append your registrations, do not reorder or reflow
existing ones**, and keep `tests/CMakeLists.txt` changes to a single appended
block. Those files are shared by design; the merge stays trivial only if nobody
tidies.

Read T132's Review (the net-new action-stack recipe and its ~50-minute measured
cost) and T133's (the `PlayerProfile` parse-site precedent this unit copies).

## Part A — the gate. Do this first; the commands are worthless without it.

**Will's decision, 2026-08-19: gate the GM commands behind a status check
performed ONCE AT LOGIN. A GM who changes state mid-session must log out and
back in to see it reflected. This staleness is accepted and is not a defect —
do not add a mid-session refresh path.** `AT_GM` live-update was explicitly
considered and declined; **do not implement it**, and do not add an `AT_GM`
case to `ZoneProcessSpawnAppearance`.

### What the field actually is — read this before writing the gate

Account status (the `200` on `willeq_test`) **never reaches the client**. There
is no account-status field in the Titanium protocol. What exists is
`PlayerProfile.gm` at **offset 13054** (eqemu
`common/patches/titanium_structs.h:894`), 18 bytes past the `guild_id` at 13036
that T133 already parses in `ZoneProcessPlayerProfile`.

**Its only writer in the entire server is `Client::SetGM(bool)`
(`zone/client.cpp:2661`)** — the `#gm on/off` toggle, which calls `Save()` so
the value persists in the character profile. So the flag means **"GM mode was ON
at login"**, NOT "this account has GM privileges". Name the client-side member
and any user-facing string accordingly — `m_gm_mode` reads truer than
`m_is_gm`. A GM who was `#gm off` at login will not see the commands until they
toggle and relog; that is the accepted consequence of the chosen design and
should be stated in a header comment so the next reader does not "fix" it.

### Capture before you trust it

eqemu's own header comments this field `// 0=no, 1=yes (guessing!)`. **That is
their uncertainty, and this arc has been burned three times by reasoning from
source instead of bytes.** Verify by capture, T144's method:

1. Log in with GM mode ON, dump the `PlayerProfile` bytes at the parse site,
   read offset 13054.
2. Log in with GM mode OFF, same dump, same offset.
3. **Cross-check the neighbours** `pvp@13052` and `anon@13053`, whose values you
   know independently — that confirms the whole region is aligned, not merely
   that one byte reads plausibly.

Annotated dump into `review/T148/`. **If offset 13054 does not track the toggle,
STOP** — the gate has no field to stand on and the rest of this unit is void.
Report; do not hunt for a substitute field on inference.

### The gate itself

Parse it in `ZoneProcessPlayerProfile` alongside T133's `guild_id`, store it,
expose a `GetGMMode()`-shaped accessor. Every GM command registration checks it
and returns a clear refusal when false. **The refusal must be distinguishable
from "no such command"** — a player typing `/kick` should learn the command
exists and is unavailable to them, not that it does not exist.

## Part B — the nine commands

All nine senders in `src/client/eq_gm.cpp` currently have **zero production
callers** and no GM command is registered in either registry (verified by sweep,
`refactor/deferred.md` T146 section). Signatures, `include/client/eq.h:1104-1112`:

| sender | signature |
|---|---|
| `SendGMZoneRequest` | `(const std::string& charname, uint16_t zone_id)` |
| `SendGMSummon` | `(const std::string& charname)` |
| `SendGMGoto` | `(const std::string& charname)` |
| `SendGMFind` | `(const std::string& charname)` |
| `SendGMKick` | `(const std::string& charname)` |
| `SendGMKill` | `(const std::string& charname)` |
| `SendGMHideMe` | `(bool hide)` |
| `SendGMEmoteZone` | `(const std::string& text)` |
| `SendGMLastName` | `(const std::string& charname, const std::string& lastname)` |

Full action stack per the T132 recipe: `CommandProcessor` registration →
`ActionDispatcher` method → `EqActionHandler` → existing sender. **Register in
`action::CommandProcessor` only** — do not add to the legacy `game` registry;
T97's shadowing bug is why. Atomicity is not a concern here since none of these
exist in either registry today, but say so in the Review rather than leaving it
unaddressed.

`SendGMHideMe` already calls `SendSpawnAppearance(AT_GM, ...)`
(`eq_gm.cpp:126`) — that is the *sending* side and is unrelated to the declined
receive-side live-update. Leave it as it is.

**T145 fixed `GMKick_Struct` 132 → 129, correct-and-unreachable until now.**
This unit is the first that can put it on the wire, so `/kick` and `/kill` are
also the first real wire test of that fix.

## Acceptance — AT THE WIRE

**`Handle_OP_GMKick` bare-returns on a size mismatch with NO log line at all**
(`zone/client_packet.cpp:6729`), so "no error in the server log" proves nothing.
Only observed effects count. Two-client, per T146's method — and T146's negative
control is the standard now: where a command was previously unreachable, show
the observed effect, not just an absence of complaint.

Verify as many of the nine as the server lets you. **A command you could not
exercise is a valid outcome — list it as unexercised rather than folding it into
"the GM family works."** T133's precedent: four guild commands were flagged as
"same fix applied, not separately exercised" rather than claimed.

`SendGMKill` on a live character is destructive; use your own test characters
and restore per the protocol (Testchar → zone_id=36, x=-77.9232, y=21.9086, z=3,
h=190).

## Verification

Full protocol bar. Suite baseline 2457 (tip `359f3647`), sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- Offset 13054 does not track the GM toggle (see Part A).
- The gate needs state that does not exist at login — report, do not invent a
  mid-session path Will declined.
- A command needs server-side privileges the test account lacks — report which.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T148-gm-status-gate`,
off `fable` tip `72169720`. eqemu lock held for the whole live phase and deleted
after clean shutdown. Full evidence: `/mnt/projects/fable/review/T148/`
(README + annotated capture dump + eight live-session client logs + the eqemu
zone server's own log).

**Note on the base rev:** `fable` moved during this unit — T149 (`c84d3edf`)
and T150 (`e03e45c0`) merged while this was running. This branch is off
`72169720` as instructed and was NOT rebased. `git diff fable` therefore shows
their work as deletions; `git diff 72169720` is the true diff for this unit
(13 files).

## PART A — CAPTURE FIRST. Result: CONFIRMED, no STOP.

**Offset 13054 tracks the `#gm` toggle in both directions.** Captured before a
single line of the gate was written, by logging bytes 13036..13059 of the
player's own `PlayerProfile` at the top of `ZoneProcessPlayerProfile`
(temporary instrumentation, reverted before commit — the committed tree
contains no capture hook, T144/T146 precedent).

Three sequential logins of `Testmelee`, with eqemu's own `#set gm on|off`
toggled between them, and `#set anon 1|0` moved independently as a
neighbour control:

| field | offset | run1 | run2 | run3 | authority |
|---|---|---|---|---|---|
| `guild_id` | 13036 | 1 | 1 | 1 | `guild_members`: guild 1 (TestGuild133, T133) |
| `birthday` | 13040 | 1784836619 | 1784836619 | 1784836619 | **`character_data.birthday` = 1784836619, exact match** |
| `lastlogin` | 13044 | 1787154286 | 1787161091 | 1787161167 | increases across the three logins |
| `timePlayedMin` | 13048 | 38587 | 38700 | 38701 | increases with session time |
| `pvp` | 13052 | 0 | 0 | 0 | never touched — **constant control** |
| `anon` | 13053 | 0 | **1** | 0 | `#set anon 1` then `0` — **tracks independently** |
| **`gm`** | **13054** | **0** | **1** | **0** | **`#set gm on` then `off` — TRACKS** |
| `guildrank` | 13055 | 2 | 2 | 2 | constant |
| `guildbanker` | 13056 | 0 | 0 | 0 | constant |

The cross-check the plan demanded is what makes this evidence rather than a
plausible read:

* `birthday`@13040 matches `character_data` **exactly**, in all three runs —
  an authority entirely outside the client (T144's method). A misaligned
  region cannot produce that.
* `anon`@13053, the byte **immediately before** `gm`, was moved independently
  in the same experiment and tracked its own toggle — so 13054 is not a lucky
  read of a neighbour, and the two bytes are distinct.
* `pvp`@13052 was deliberately not touched and stayed 0 throughout — the
  region is not simply "everything in it moved".

eqemu's `// 0=no, 1=yes (guessing!)` is resolved for Titanium: **the guess is
correct.** Annotated bytes + full method:
`review/T148/playerprofile_gm_offset_annotated.txt`.

**Independent corroboration from the live phase**, which the capture did not
need but got anyway: every eqemu GM handler this unit exercised begins
`if (!GetGM()) { Message(Chat::Red, "Your account has been reported for
hacking."); return; }`. In the GM-on session that string appears **zero
times** and every command took effect; in the GM-off session the client
refused everything before it could be sent. The server's own `m_pp.gm` and the
byte we parse at 13054 agree.

### What the flag is — named to match

`m_gm_mode` / `GetGMMode()`, **not** `m_is_gm` / `IsGM()`, and the refusal
string says *"you were not in GM mode when you logged in"*, not *"you are not
a GM"*. Proof that the distinction is real and not pedantry, from this
session: in the negative-control run `Testmelee`'s `gm` byte was 0 and the
client refused all nine — yet `#set time`, `#set gm` and `#set anon` all still
worked in that same session, because eqemu gates server-side GM commands on
`Client::Admin()` (account status 200), a value that **never reaches a
Titanium client**. Account status and GM mode are different things and the
client can only ever see the second one.

## The gate

| layer | file | what it does |
|---|---|---|
| parse | `src/client/eq.cpp`, `ZoneProcessPlayerProfile` | `uint8_t gm_mode = p.GetUInt8(base + 13054)` → `m_gm_mode`, grouped with T133's `m_guild_id = guild_id`. One assignment, once per login, no spawn-id match, no ordering hazard. |
| state | `include/client/eq.h` | `bool m_gm_mode = false;` + `GetGMMode()`, with a header comment stating the contract (login-time only, deliberately stale, NOT an account privilege) so the next reader does not "fix" it. |
| interface | `IActionHandler::isGMMode() const` | the only non-void method on the interface; `EqActionHandler::isGMMode()` returns `m_eq.GetGMMode()`. |
| gate | `ActionDispatcher::checkGMMode()` | the single point that consults it. Called by all nine `gm*()` dispatcher methods, **before** `checkZoneConnection()`. |

**Checked once at login; no mid-session refresh.** No `AT_GM` case was added to
`ZoneProcessSpawnAppearance`, and
`GMModeGate.GMModeIsLoginTimeOnly_NoMidSessionRefresh` **pins that decision**:
it feeds our own spawn an `AT_GM` `SpawnAppearance` — exactly the packet a
live-update path would consume, and exactly what T133 *did* wire for
`AT_GUILD_ID` — and asserts `GetGMMode()` does not change. If a future reader
"fixes" the staleness, that test fails and points them at this plan.
`GMModeGate.SecondPlayerProfileRefreshesGMMode` covers the flip side: the
sanctioned refresh (relog) actually works.

**The refusal is distinguishable from "no such command"** — the plan's explicit
requirement. Registration is unconditional; the gate is one layer down. Live:

```
CMD outcome=failed registry=action recognized=true \
  message="That command is a GM command and you were not in GM mode when you logged in. Use #gm on, then log out and back in." cmd="/kick Testbuddy"
```

`recognized=true` — the player learns the command exists.
`T148_GMCommand_NotGMMode_RefusalIsNotUnknownCommand` asserts the refusal text
differs from an unknown command's, and that `hasCommand("kick")` is true while
`hasCommand("definitelynotacommand")` is false.

Checking the gate **before** the zone-connection guard is deliberate: an
ordinary player who is not zoned in gets the honest reason (not a GM), not the
incidental one (not connected). `T148_GateIsCheckedBeforeZoneConnection`.

## Part B — the nine, and the action stack

| command | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | sender |
|---|---|---|---|---|
| `/gmzone <name> <zone_id>` | `cmdGMZone` | `gmZoneRequest(name, zoneId)` | `gmZoneRequest` | `SendGMZoneRequest` |
| `/summon <name>` | `cmdGMSummon` | `gmSummon(name)` | `gmSummon` | `SendGMSummon` |
| `/goto <name>` | `cmdGMGoto` | `gmGoto(name)` | `gmGoto` | `SendGMGoto` |
| `/gmfind <name>` | `cmdGMFind` | `gmFind(name)` | `gmFind` | `SendGMFind` |
| `/kick <name>` | `cmdGMKick` | `gmKick(name)` | `gmKick` | `SendGMKick` |
| `/kill <name>` | `cmdGMKill` | `gmKill(name)` | `gmKill` | `SendGMKill` |
| `/hideme <on\|off>` | `cmdGMHideMe` | `gmHideMe(bool)` | `gmHideMe` | `SendGMHideMe` |
| `/emotezone <message>` | `cmdGMEmoteZone` | `gmEmoteZone(text)` | `gmEmoteZone` | `SendGMEmoteZone` |
| `/gmlastname <name> <lastname>` | `cmdGMLastName` | `gmLastName(name, lastName)` | `gmLastName` | `SendGMLastName` |

Category `GM`. No aliases — checked against every name and alias in both
registries first; `/find` was deliberately avoided (T150's territory), hence
`gmfind`. Registered in `action::CommandProcessor` **only**, never the legacy
`game` registry (T97's shadowing bug).

**Atomicity — addressed, not left unaddressed as the plan asked.** It does not
arise. All nine senders had zero production callers and were registered in
*neither* registry, so there is no legacy registration to remove in the same
change and no window where two registries hold a command. There was never a
second copy. Verified by grep over `src/`, `include/`, `tools/` before starting
and confirmed live: every one dispatches `registry=action`, while `/loc`
(untouched) still resolves `registry=action` from T140's migration and `/say`
routes normally in the same session.

`/hideme` is explicit `on|off`, deliberately **not** a toggle: the client keeps
no hidden-state, so a toggle would have to guess and would desync the moment
the server disagreed. `SendGMHideMe` itself was left exactly as it was, per the
plan.

## ACCEPTANCE — AT THE WIRE

Two clients, per T146's method. A = `Testmelee` (`willeq_test`, logged in with
GM mode ON, verified by capture); B = `Testbuddy` (`willeq_buddy`, account
status **0** — no GM privileges at all), an independent observer in the same
zone. Daylight forced first (`/say #set time 11 0`, confirmed in the zone log).

**Nothing below is accepted on a local "sent OK" log.** `Handle_OP_GMKick`
bare-returns on a size mismatch with no log line, so absence of a complaint
proves nothing; every row names an effect observed by the server, by the DB,
or by the second client.

| # | command | on the wire? | observed effect | evidence |
|---|---|---|---|---|
| 1 | `/gmzone Testmelee 36` | **YES** | server answered `OP_GMZoneRequest` with `success=1`; A printed `GM zone request approved for zone 36` | `gm_on_A_testmelee.log` |
| 2 | `/summon Testbuddy` | **YES** | server relocated **the other client**: `ZonePC Client [Testbuddy] zone_id [77] x [0] y [0] z [0] ignorerestrictions [1] zone_mode [4]`; B's own log shows it leaving the zone at that instant | zone log + `gm_on_B_testbuddy.log` |
| 3 | `/goto Testbuddy` | **YES** | server relocated A to B's live position: `ZonePC Client [Testmelee] zone_id [77] x [156] y [-1009] z [51] heading [77]` — B's coordinates *and heading*, which A had no other source for | zone log |
| 4 | `/gmfind Testbuddy` | **YES** | server answered `OP_GMFind`; A printed `GM find result: Testbuddy in zone 77` | `gm_on_A_testmelee.log` |
| 5 | `/kick Testbuddy` | **YES** | **B received `HC_OP_GMKick`** at 17:58:44.362, 42 ms after A sent it. The server only forwards this packet after the size check **and** `GetGM()` pass — so this is the first real wire test of **T145's `GMKick_Struct` 132→129 fix**, and it passes | `gm_on_B_testbuddy.log` |
| 6 | `/kill Testbuddy` | **YES** | **B received `HC_OP_GMKill`** at 17:58:35.356, same 129-byte struct, same reasoning | `gm_on_B_testbuddy.log` |
| 7 | `/hideme on` | packet observed, **effect NOT achievable** | reaches the server as `OP_SpawnAppearance` type 20 and is **dropped**: `Unknown SpawnAppearance type: 0x0014 value: 0x00000001`. It never reaches `Handle_OP_GMHideMe`, which wants `OP_GMHideMe`. **Listed as unexercised**, see findings | zone log |
| 8 | `/emotezone <text>` | **YES, after an in-territory fix** | first run: `Wrong size: OP_GMEmoteZone, [size=32,] expected [512]`, nobody saw the text. After the fix: **both** clients printed it, B at 18:12:39.478 from A's send at 18:12:39.458 | zone log + `emote_fixed_{A,B}*.log` |
| 9 | `/gmlastname Testbuddy Gated` | **YES** | B received `HC_OP_GMLastName`; and `character_data.last_name` for `Testbuddy` **became `Gated` in the server's own database** — a persisted effect in an authority entirely outside the client | `gm_on_B_testbuddy.log` + `SELECT` |

**Eight of nine exercised on the wire with an observed effect. One —
`/hideme` — could not be exercised, and is reported as unexercised rather than
folded into "the GM family works."**

### The negative control — the part that makes this evidence

T146's standard. The **identical script** was re-run against the same server,
in the same zone, with the same binary, differing in exactly one thing: A was
logged in with GM mode **off** (`#set gm off` at the end of the previous
session, then relog — the accepted refresh path, exercised in reverse).

| | GM-on run | GM-off run |
|---|---|---|
| nine GM commands `recognized` | 9 | **9** (they still exist) |
| nine GM commands `applied` | 9 | **0** |
| GM opcodes seen by observer B | `GMKick`, `GMKill`, `GMLastName` | **none** |
| GM replies received by A | `OP_GMFind`, `OP_GMZoneRequest` | **none** |
| GM lines in the zone server log | `ZonePC` ×2, emote size error, unknown SpawnAppearance | **none** |
| `/say`, `/loc` in the same session | applied | **applied** |
| `#set time` (server-side GM cmd) | worked | **worked** — account status is unchanged |

The last two rows are what stop this being "the client crashed": the session
was demonstrably healthy and the account demonstrably still privileged. Only
the *client-side login-time flag* differed, and it silenced all nine.

## Findings this unit made and did NOT fold into a pass

1. **`SendGMEmoteZone` wrote a bare variable-length string where the server
   demands a fixed 512-byte body.** `Handle_OP_GMEmoteZone` checks
   `app->size != sizeof(GMEmoteZone_Struct)` and that struct is a single
   `char text[512]`. Measured, not inferred:
   `Wrong size: OP_GMEmoteZone, [size=32,] expected [512]` in the server's own
   log, and no client — including the sender — saw the emote. **Fixed in this
   unit** (see Deviations) because the plan's acceptance bar is observed
   effects and this command could otherwise produce none. Re-verified live:
   both clients printed the text.
2. **`SendGMHideMe` is misrouted.** It calls `SendSpawnAppearance(AT_GM, ...)`,
   which puts `OP_SpawnAppearance` type 20 on the wire. eqemu's
   `Handle_OP_SpawnAppearance` has no case for type 20 and prints
   `Unknown SpawnAppearance type: 0x0014`; the real handler,
   `Handle_OP_GMHideMe`, listens on `OP_GMHideMe` and never runs. **Left
   exactly as it is, per the plan's explicit instruction**, and reported.
   `/hideme` is therefore a registered, gated, correctly-dispatched command
   that cannot have its intended effect.
3. **`ZoneProcessGMFind` decodes the reply's coordinates with the wrong struct.**
   The client reuses `GMSummon_Struct` (`int32 y, x, z` at 136/140/144) for
   `OP_GMFind`, but eqemu's `GMFind_Struct` is `float x, y, z` at the same
   offsets — different type *and* different order. Visible in the evidence:
   `GM find result: Testbuddy in zone 77 at (-998490112, 1125908480,
   1112276992)` — floats read as ints. The `charname`/`zoneID` fields, which
   share offsets between the two structs, decode correctly, so the round trip
   itself is proven; only the coordinates are garbage. **Not fixed** — this is
   a receive-side struct bug belonging to the T145/T147 struct-audit arc, not
   to this unit's sender arc, and fixing it needs its own captured-bytes test.
4. **`SendGMSummon` sends `x=y=z=0`.** The struct has the fields; the sender
   memsets and never fills them, so the server dutifully teleported `Testbuddy`
   to the origin (`ZonePC ... x [0] y [0] z [0]`). The real client fills them
   with the GM's own position. **Not fixed** — a behaviour change to a sender
   beyond wiring it up, and it did not block acceptance (the relocation is the
   observed effect). Worth its own follow-up.

These four belong in `refactor/deferred.md`. **They were NOT appended there**
— `deferred.md` was modified on `fable` by both T149 and T150 while this unit
ran, and appending from the older base would manufacture a merge conflict for
no benefit. Flagged for the orchestrator to file at merge time.

## Tests — 30 new, all passing

`tests/test_zone_connection_flow.cpp`, new `GMModeGate` suite (5), sibling to
T133's `GuildIdPopulation`:

* `OwnPlayerProfile_GMFlagSet_PopulatesGMMode`
* `OwnPlayerProfile_GMFlagClear_LeavesGMModeFalse`
* `NeighbouringPvpAndAnonBytesDoNotBleedIntoGMMode` — sets `pvp`@13052,
  `anon`@13053 and `guildrank`@13055 while leaving `gm` clear; an off-by-one in
  either direction at the parse site fails this.
* `GMModeIsLoginTimeOnly_NoMidSessionRefresh` — pins Will's decision.
* `SecondPlayerProfileRefreshesGMMode` — the relog path works.

`tests/test_action_dispatcher.cpp` (5): null-handler safety, all nine refused
with `gmMode=false` and the handler untouched, all nine routed with
`gmMode=true`, the refusal message's content, empty-argument rejection.

`tests/test_command_processor.cpp` (20): registration + category, the refusal
vs unknown-command distinction, all nine refused, gate-before-zone-check, each
command's dispatch, `/gmzone`'s non-numeric / out-of-`uint16` / missing-token
rejections, `/hideme` on/off/no-args, `/gmlastname`'s missing second token, and
all nine under not-connected.

`tests/action_test_fakes.h` gains `RecordingActionHandler::gmMode` (default
**false**, so every pre-existing test keeps its meaning and the GM path must be
asked for explicitly) plus the ten overrides.

**`tests/CMakeLists.txt` was not touched at all** — every new case landed in an
existing test binary, so this unit contributes nothing to the shared-file merge
with T149/T150.

## Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **PASS** on the second attempt. The first `-j24` invocation failed in `gtest_discover_tests` with `Process terminated due to timeout` on `test_zone_connection_flow` and `test_combat_manager` — a discovery-time resource contention, not a compile error (T146 recorded the same class of transient at `-j24`). Re-ran at `-j8`: clean. Flagged rather than absorbed. |
| 2. `ctest --output-on-failure` | **2487/2488**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| 4. Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`; `git status tests/goldens` empty — goldens untouched |
| 5. `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| 6. `check_file_sizes.sh 72169720` | 13 files checked, **no new FAIL, no new WARN**. `src/client/eq.cpp` FAILs at 3920 lines — **pre-existing**, it was already 3902 (over the 3000 ceiling) at the base rev; this unit adds 18 lines, one assignment and its doc comment, which is not "substantial new code" warranting a new TU. The four WARNs (`eq.h` 2181→2215, `action_dispatcher.cpp` 1654→1802, `command_processor.cpp` 2204→2359, `test_command_processor.cpp` 2649→2844) were all already over 1500 at the base rev. |

**Suite count, stated honestly:** 2488 total against the plan's stated baseline
of 2457 = **+31**, but this unit adds **30** tests (20 + 5 + 5, each count
confirmed by running the filtered binaries). The extra +1 is not this unit's
and is not explained here — the plan's baseline was measured at tip `359f3647`
while this branch is off the later `72169720`, so the likeliest explanation is
a stale baseline figure in the plan. Recorded rather than quietly rounded.

## Method

Client `build/bin/willeq`, headless, `-d 4 --log-level=DEBUG --script <file>`,
two concurrent processes launched from a single foreground driver that `wait`s
on both (no background parking, no turn ended on a pending task). A exits 0 via
`/camp` → `OP_LogoutReply`; B idles and is ended by `timeout` (exit 124), the
documented T84 headless behaviour. Configs, scripts and drivers live under
`data/captures/` (gitignored; T84/T102/T135/T146 precedent).

## Server state — left as found

Verified by `SELECT` before and after:

| character | zone | x, y, z, heading | gm | anon | last_name |
|---|---|---|---|---|---|
| `Testchar` | 36 | -77.9232, 21.9086, 3, 190 | 0 | 0 | (empty) |
| `Testmelee` | 77 | 156, -1009, 51, 0 | 0 | 0 | (empty) |
| `Testbuddy` | 77 | 156, -1009, 51, 77 | 0 | 0 | (empty) |

Identical to the pre-session snapshot. eqemu shut down with `down.sh`;
`.eqemu-lock` deleted.

## Deviations

1. **`SendGMEmoteZone` was fixed** (`src/client/eq_gm.cpp`, in territory). Not
   anticipated by the plan, but required to meet its own acceptance bar: the
   command produced *no* effect and a server-side size rejection, so it could
   only have been reported as unexercisable. T133's precedent — an in-territory
   bug that blocks the plan's acceptance gets fixed in the same commit and
   flagged, not silently absorbed. The 512-byte body is built inline rather
   than by adding a `GMEmoteZone_Struct` to `include/common/packet_structs.h`,
   deliberately: it is one fixed-size `char` array with no layout to get wrong,
   and keeping it local avoids touching a header the parallel struct-audit
   units share. Re-verified live on a second two-client run.
2. **`/hideme` is reported as NOT exercised.** The plan expected all nine to be
   gated and registered, which they are, but `SendGMHideMe` cannot reach
   `Handle_OP_GMHideMe` (finding 2). The plan says to leave that sender alone,
   so it was left alone and the command is listed as unexercised rather than
   counted toward "the GM family works."
3. **Two further defects found and NOT fixed** — `ZoneProcessGMFind`'s
   int-vs-float coordinate decode and `SendGMSummon`'s zeroed coordinates
   (findings 3 and 4). Both are real; neither blocked acceptance; both are
   scope beyond gating and wiring. Reported instead of absorbed.
4. **`refactor/deferred.md` was not appended to**, contrary to the usual habit,
   because both parallel units modified it on `fable` mid-unit. The four
   findings are written up above for the orchestrator to file at merge.
5. **`Testbuddy.last_name` was written back to empty by `UPDATE`** after
   `/gmlastname` set it to `Gated`. That is a DB write outside the protocol's
   explicitly sanctioned "character position" carve-out. It is a restore of a
   field this unit itself changed, back to its exact pre-session value, and is
   recorded here rather than left behind (contrast T146, which documented a
   corpse row it chose not to delete). `Testbuddy` was also relocated to the
   origin by `/summon` and re-zoned itself back; its final position matches the
   pre-session value exactly.
6. **The first `-j24` build failed in test discovery** and was re-run at `-j8`.
   Environmental, not a code error; recorded rather than hidden.

## Board

**The gate stands on bytes, not on eqemu's guess.** `PlayerProfile.gm` @13054
is confirmed for Titanium, with the region proven aligned against
`character_data.birthday` and with an independently-moved neighbour byte. Any
future unit wanting a client-visible GM signal should use this and nothing
else — there is no account-status field in the protocol to find.

**The zero-caller sender arc's first real wire result.** T146 found the nine GM
senders unreachable and could prove nothing about them. Eight are now proven
on the wire with observed effects, including the first exercise of T145's
`GMKick_Struct` fix. But wiring up dead senders **surfaced three more defects
in them** (emote size, hideme misroute, find decode) plus one gap (summon
coordinates) — one defect for every two senders. **A zero-caller sender is not
"correct but unreached"; it is unverified.** The remaining 19 of T146's 28
should be planned with that ratio in mind: budget for finding bugs in each one
as it becomes reachable, not just for the plumbing.

**`/hideme` and `/gmfind` are the next two units' worth of work here**, and
they are receive/route bugs, not plumbing — the same asymmetry T148's parent
plan identified: senders need a trigger, but once triggered they need to be
*right*, and nothing had ever checked.
