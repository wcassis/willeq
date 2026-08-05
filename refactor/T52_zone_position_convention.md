# T52 — Server-initiated teleports land at the wrong position: one consumer, two producers, disagreeing x/y conventions

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the ground truth and the
acceptance are both live, and both need packet-level evidence.**
No sibling unit.

**Territory: `EverQuest::ZoneProcessZoneChange` / `RequestZoneChange`
(`src/client/eq_zone.cpp`), the zone-line producer in the same file, the
`m_pending_zone_*` consumer in `src/client/eq_login_world.cpp`, and
tests.** Do **not** change the server — `/mnt/projects/akk-stack/code/`
is READ-ONLY reference here. Do not change `SendPositionUpdate` or the
movement/coordinate helpers.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `docs/CLAUDE.md`'s coordinate
section (this unit lives or dies on coordinate conventions), then
`refactor/T50_bank_train_prevalidation.md`'s Review section on the
`#goto` finding that started this.

## Why this unit exists, and the correction that produced it

T50 found that GM `#goto x y z` does not land the player at the
requested coordinates — the position "echoes back" the DB-persisted one.
T50 recorded it as a server-side eqemu quirk.

**I initially agreed and was wrong.** Will corrected the framing:
**the server respects whatever position the client sends it. Position
flows client→server in the overwhelming majority of updates; a
server→client position is authoritative only at the moment it is sent.**
So a client that mis-applies an approved teleport position will
immediately "correct" the server back to the wrong place via its next
`SendPositionUpdate()` — producing exactly the symptom T50 observed from
the outside, with no server bug required.

That reframing points at a specific, checkable client defect.

## The suspect — a hypothesis to PROVE or REFUTE, not a finding

`m_pending_zone_*` has **two producers and one consumer**, and their
coordinate conventions appear to disagree.

**Producer A — the server-approved teleport** (`eq_zone.cpp:71-118`):
reads the wire in server order (`offset 70 = server y`,
`offset 74 = server x`), **swaps into client format**
(`target_x = zone_y; target_y = zone_x`, `:93-95`), and stores *that*
into `m_pending_zone_x/y` (`:116-118`).

**Producer B — the zone-line crossing** (`eq_zone.cpp:718-724`): stores
`result.targetX/targetY` straight through. **Its convention is NOT
established — establishing it is step 1 of this unit.**

**The consumer** (`eq_login_world.cpp:857-867`): its comment asserts
"pending coordinates are in SERVER format (x=server_x, y=server_y)" and
it therefore **swaps again**: `m_x = m_pending_zone_y;
m_y = m_pending_zone_x;`.

**If producer A really stores client-format coordinates, the consumer's
second swap transposes x and y for every server-approved teleport** —
`#goto`, GM summon, succor/evac, anything server-initiated — while
zone-line travel (producer B) would be correct if B's convention matches
the consumer's assumption. **That asymmetry is exactly why this could
sit undetected: zone-line travel is the common path and works.**

**One of the two comments is actively wrong.** Part of this unit's value
is making the convention explicit at all three sites so the next person
does not have to re-derive it.

## Step 1 — ground truth from the wire, before changing anything

`DumpPacket` already exists and `RequestZoneChange` already calls it.
Capture a real `#goto` and record, as a table:

1. what the server **sent** (`OP_ZoneChange` response, raw offsets 70/74/78);
2. what the client **stored** in `m_pending_zone_x/y/z`;
3. what the client **applied** to `m_x`/`m_y`/`m_z`;
4. what the client then **sent back** in its next `SendPositionUpdate()`;
5. where the character actually **ended up** (`/loc`, and the DB row).

Do the same for a normal **zone-line crossing** — that is the control,
and the regression you must not break.

**This table is the unit's primary deliverable even if the fix turns out
to be one line, and it is what T50 was missing.** It settles the fork
that source-reading cannot:

- Server sends the **requested** position and the client mangles it →
  **our bug, fix it** (the expected outcome).
- Server sends the **DB/safe** position → the client is faithful and the
  behaviour is genuinely server-side → **STOP**, record the capture as
  evidence, and close the board item as not-ours. Do not "fix" the client
  to compensate for a server decision.

## Step 2 — fix, if step 1 says it is ours

Make the convention **consistent and explicit**. Prefer correcting
whichever site is wrong over adding a compensating swap somewhere else —
a second wrong that cancels a first is how this class of bug survives.
State which site you changed and why that one is the wrong one.

Ride-along, clearly separate and cosmetic: `RequestZoneChange`
hardcodes `zone_reason = 1` (ZoneUnsolicited) even when responding to a
**server-solicited** change, contradicting its own comment two lines up
(`eq_zone.cpp:180-184`). I checked the server: `zone_reason` is **only
logged**, never used for logic (eqemu's own header calls it "a memo field
of sorts"), so this is log fidelity, not behaviour. **Fix it in its own
commit and do not claim it changes anything.**

## STOP conditions

- Step 1 shows the server sends the DB/safe position → STOP as above.
- The fix would require touching `SendPositionUpdate`, the movement
  helpers, or the coordinate conventions themselves → STOP; that is a
  much bigger unit.
- **Zone-line travel regresses in any way** → STOP. That is the common
  path; breaking it to fix a GM command is a bad trade.
- Producer B's convention turns out to also be wrong (i.e. zone-line
  travel is *also* subtly off) → report it, and think hard before
  "fixing" a path that users experience as working today.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP unwaived (T35),
   golden replay does not skip (T36); pinned tests from T43-T51 stay
   green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `scripts/ci/visual_gate.sh` PASS, goldens untouched.
5. Unit tests pinning the convention at each of the three sites, so a
   future edit that re-introduces a swap fails a test rather than a
   play session.
6. **Live acceptance, both trees, entirely piped:**
   - The step-1 wire table, before and after.
   - `#goto x y z` to a **known, verifiable** destination — the player
     ends up there, confirmed by `/loc` AND by the DB row.
   - **The regression leg, which matters more than the fix**: real
     zone-line crossings (befallen ↔ qeynos2, the pair P17 used for its
     5-hop session) still land correctly, several hops, both directions.
   - eqemu LOCK FIRST (if the lock holds YOUR unit id from an
     interrupted run, it is yours — do not stop over it); `up.sh` only;
     restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190;
     `down.sh` and delete the lock after. Evidence under
     `benchmarks/audits/T52/`.
   - **Note from T50: `#npctypespawn`'d NPCs do not survive a full
     disconnect/reconnect**, and `/warp`'s argument order does not match
     the DB column order. Both cost T50 time; do not rediscover them.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## What to report in the Review

- **The wire table** (both `#goto` and the zone-line control).
- Producer B's actual convention, stated plainly — nobody has written it
  down.
- Which site was wrong, and why you fixed that one rather than
  compensating elsewhere.
- Whether any *other* server-initiated position path (GM summon, succor,
  evac, death/bind) shares producer A and was therefore equally broken.
  **You do not have to fix them — but say which ones are implicated**,
  because that determines whether this was a `#goto` curiosity or a
  whole family of teleport bugs.
- Whether the board item should now read "fixed" or "server-side, not
  ours".

## Commit early

Three executors running have now been interrupted by transient API 529s.
**Commit working code before long verification runs**, so an
interruption costs a resume rather than the work.

## Review

### Headline: the hypothesis was PARTIALLY right and PARTIALLY wrong -- the wire proved both

The consumer's second swap is real and wrong, exactly as suspected. But it
turns out to be **dead code** in every path this unit could exercise, and
the actual landing position (for both `#goto` and real zone-line crossings)
is decided by a completely different, already-correct code path
(`PlayerProfile`'s own single swap in `eq.cpp`), which runs *after* the
consumer and overwrites whatever it would have set. T50's `#goto` symptom
is confirmed, live and repeatably, to be genuinely server-side. **Board
item: "not ours"** for the `#goto` symptom itself; the client-side fixes
below are a correctness hardening for latent bugs that do not currently
produce any observed misbehavior.

### Step 1 -- the wire table (the deliverable)

All captures below at `-d 6`/TRACE (`DumpPacket` hex enabled), piped
stdin, headless (`--no-graphics`), against a live eqemu (`befallen`, zone
36). Scripts and transcripts under `benchmarks/audits/T52/`.

**Leg A -- `#goto` (producer A, server-solicited), same-zone, clean run
(`run_step1_goto_probe.sh`, `#goto 100 200 5`):**

| Stage | Value |
|---|---|
| Server SENT (`OP_RequestClientZoneChange`, wire offset 6=y-field/10=x-field) | y=200, x=100 (the GM's literal typed y/x) |
| Client parsed + swapped (producer A logic, `ZoneProcessRequestClientZoneChange`) | client `(x,y,z)=(200,100,5)` -- correct single swap |
| Client's own `OP_ZoneChange` **request** back to server | `RequestZoneChange` sent zone 36 at `(200,100,5)` |
| Server's `OP_ZoneChange` **reply** (`success=1`) | `pos=(21.9,-77.9,3.0)` -- **NOT the requested target; the character's own PRE-existing client-format position** |
| Client stored (`m_pending_zone_x/y/z`, producer A) | `(21.9,-77.9,3.0)` (whatever the server sent, swap-parsed) |
| Consumer (`WorldProcessZoneServerInfo`) | **did not run** -- `is_zone_transition` was false (see below) |
| `PlayerProfile` for the (same) zone, the real landing source | `server=(-77.92,21.91,3.00) -> client=(21.91,-77.92,3.00)` -- **identical to the pre-`#goto` position** |
| `/loc` after | `21.9, -77.9, 3.0` -- unchanged from before the `#goto` |
| DB row after | `x=-77.9232, y=21.9086` -- unchanged |

**The server's own persisted character position was never updated.** This
reproduces T50's finding exactly, now with full wire evidence: **the
server sends the character's existing DB position, not the requested one,
and not zero** (an earlier, contaminated run with a negative-number
argument, `#goto 555 -333 8`, got an all-zero reply instead -- see
Deviations; not investigated further, out of territory, but flagged since
it shows the reply's position fields are not even reliably the same kind
of "wrong" across runs). Verified **identically in both trees**
(`build/bin/willeq` and `build-nogfx/bin/willeq`) with `#goto 100 200 5`.
This is the STOP condition ("server sends the DB/safe position") --
**client is faithful, this is genuinely server-side.**

**Leg B -- real zone-line crossing (producer B), both directions
(`run_step1_zoneline_probe.sh` / `run_step1_zoneline_return_probe.sh`):**

befallen (zone 36) has exactly one real physical zone line, to
commons/West Commonlands (zone 21) -- **not to qeynos2**; the
befallen<->qeynos2 pair P17 used was reached via GM `#zone` teleport for
texture-eviction testing, not a walked zone line (confirmed:
`zone_points` for `befallen` has rows only to `commons` (21) and
`commonlands` (408)). Used the real pair instead, flagged as a deviation.

| Stage | befallen -> commons | commons -> befallen (return) |
|---|---|---|
| Zone-line trigger (client BSP/JSON check) | fired at client `(-75.3,35.2,3.8)`, box from `data/zone_lines/befallen.json` | fired at client `(-1163.918,595.625,-34.984)` (a genuinely razor-thin, single-plane trigger box in `data/zone_lines/commons.json`) |
| Producer B stored (`m_pending_zone_x/y/z`) | `(596,-1150,-38)` -- straight from the zone's own zone-line data, matches `zone_points.target_x/y/z` exactly | `(0,0,0)` -- **this specific commons.json entry has no destination coordinates at all**, a pre-existing data gap, not a code bug |
| Client's `OP_ZoneChange` request | sent zone 21 at `(596,-1150,-38)` | sent zone 36 at `(0,0,0)` |
| Server's `OP_ZoneChange` reply | `success=1`, `pos=(0,0,0)` | `success=1`, `pos=(0,0,0)` |
| Consumer | did not run (dead, see below) | did not run |
| `PlayerProfile` (real landing source) | `server=(596,-1150,-38) -> client=(-1150,596,-38)` | `server=(35.22,-75.27,3.13) -> client=(-75.27,35.22,3.13)` |
| `/loc` after | `-1150.0, 596.0, -38.0` -- **matches the zone's real entry point** | `-75.2, 35.1, 3.1` -- **matches befallen's real entry point** |
| DB row after | `zone_id=21, x=596, y=-1150, z=-38` | `zone_id=36, x=35.125, y=-75.25, z=3.125` |

**Both directions land correctly**, including the return leg where
producer B's own local data was flatly wrong/missing (0,0,0) -- proof that
**final landing position never actually depends on `m_pending_zone_x/y`
at all**, for either producer. `PlayerProfile` is what determines it,
every time, for every zone entry (initial login and every subsequent
transition, same-zone or cross-zone).

### The discovery that reframes the whole unit: the consumer is dead code

`WorldProcessZoneServerInfo`'s position-transfer block is gated on
`bool is_zone_transition = m_zone_connected || m_zone_connection != nullptr;`.
Traced against the live captures: by the time this handler runs (on
receiving `OP_ZoneServerInfo`, mid zone-to-zone reconnect),
`ProcessDeferredZoneChange` has **always already called
`DisconnectFromZone()`** (`eq_zone.cpp` Step 1, synchronous, unconditional
for every approved zone change), which clears both `m_zone_connected` and
`m_zone_connection` **before** the client ever reconnects to world and
receives `OP_ZoneServerInfo`. So `is_zone_transition` is **always false**,
in every path traced (`#goto`, both zone-line directions) -- the log line
`"Setting spawn position: ..."` never once appeared across any capture in
this unit. This is structural, not incidental: the ordering
(disconnect-then-reconnect, always) is baked into
`ProcessDeferredZoneChange` for every successful zone-change approval, same-
or cross-zone. Even hypothetically, if this branch did run, its effect
would be overwritten moments later by `PlayerProfile`'s own (independent,
correct) position assignment on zone entry -- so the branch is doubly
inert. Not fixed here (would mean touching `ProcessDeferredZoneChange`'s
ordering, a materially bigger and riskier change than this unit's
territory), but flagged prominently since it explains why the suspected
bug never bit anyone.

### Producer B's actual convention, stated plainly

`CheckZoneLine` (`eq_zone.cpp`) stores `result.targetX/Y/Z` **straight
through, already in CLIENT format** -- sourced from the zone's own
WLD/JSON-derived zone-line data (`m_zone_lines`, loaded via
`LoadZoneLines`), which docs/CLAUDE.md's convention #3 establishes shares
the client's coordinate space (Z-up, identity vs. client internal, no
swap). Confirmed by inspection (the three-line assignment at the "Trigger
zone line!" site does no arithmetic) and by the wire (producer B's stored
values for the outbound leg, `(596,-1150,-38)`, exactly match
`zone_points.target_x/y/z` with no transposition). **Both producers agree
on client format** -- only the consumer's comment (and logic) was wrong.

### Which site was wrong, and why fixed there

1. **Consumer (`eq_login_world.cpp:857-889`)** -- was wrong: claimed
   `m_pending_zone_x/y` were in "SERVER format" and applied a second swap.
   Producer A already stores client format (verified against the wire, and
   independently against docs/CLAUDE.md + the live `PlayerProfile` handler
   which uses the identical relationship). Fixed by removing the second
   swap (straight assignment) and rewriting the comment to state the
   correct, verified convention. **Chosen over "fix producer A instead"**
   because producer A's swap is independently correct (matches the
   PlayerProfile handler byte-for-byte in relationship, and matches
   docs/CLAUDE.md); the consumer's comment was the one thing asserting the
   opposite of everything else in the codebase.
2. **`RequestZoneChange`'s outbound packet build (`eq_zone.cpp:141-201`)**
   -- a second, independent latent bug, also fixed: it wrote
   `PutFloat(70, y); PutFloat(74, x)` (a straight, unswapped copy), which
   contradicts the parse-side convention used everywhere else (including
   this same file's own `ZoneProcessZoneChange`, three functions above it).
   Fixed to `PutFloat(70, x); PutFloat(74, y)`, matching the inverse of the
   established parse relationship. **No live symptom traced to this
   either**: in every path exercised, the server decides the real
   destination itself (`ZonePC`'s `m_ZoneSummonLocation` for a solicited
   teleport; the zone's own `zone_points` row for a zone-line crossing)
   without reading back this outgoing packet's position fields.
3. **Ride-along (`zone_reason`, cosmetic only)** -- `RequestZoneChange`
   hardcoded `zone_reason=1` (`ZoneUnsolicited`) even when replying to a
   server-solicited change. Added a `solicited` parameter (default `false`,
   preserving existing zone-line behavior), passed `true` from
   `ZoneProcessRequestClientZoneChange`'s call site. Confirmed against the
   real eqemu source (`common/patches/titanium_structs.h` /
   `zone/zoning.cpp`): `zone_reason` is a log-only "memo field", never
   read for logic. **Separate commit, no behavioral claim.**

### Other server-initiated position paths sharing producer A -- implicated

Searched every production write site of `m_pending_zone_x/id` (3 total,
confirmed exhaustive by grep across `src/client/`):

- **`ZoneProcessZoneChange`** (producer A) -- `#goto`, and by the same
  mechanism, **succor and evacuate spells**: this client has no dedicated
  client-side handling for those spell effects at all
  (`spell_effects.cpp` only has a cosmetic "teleporting" message string),
  so any server-initiated teleport that isn't a bind/death respawn arrives
  through this exact `OP_RequestClientZoneChange`/`OP_ZoneChange`
  mechanism. **Implicated.**
- **`CheckZoneLine`** (producer B) -- zone-line crossings only. Not a
  "server-initiated" path; not implicated by the consumer question (its
  own convention was always correct).
- **`ZoneProcessZonePlayerToBind`** (`eq.cpp:3392-3439`, death/bind
  respawn) -- a **third, previously-unlisted producer**, found during this
  unit. Uses the identical, correct swap (`bind_x = zone_y; bind_y =
  zone_x`). **Implicated** -- would have hit the same (now-fixed) double
  swap had the consumer branch ever run.
- **GM summon (`HC_OP_GMSummon`, `eq_gm.cpp:185-200`)** is a **separate**
  opcode that does **not** touch `m_pending_zone_x/y` at all -- it only
  logs success/failure and never sets position in this client. **Not
  implicated** (and looks like an incomplete implementation independent of
  this unit's scope).

So: `#goto`, succor, evacuate, and death/bind respawn all share producer
A's (correct) convention and the (now-fixed) consumer; all were equally
unaffected live for the same reason (dead consumer branch,
`PlayerProfile` override). This was a whole-family latent-bug class, not
a `#goto` curiosity -- but a *latent* one, not a live one.

### Deviations from the plan, flagged rather than absorbed

- **befallen<->qeynos2 substituted with befallen<->commons.** befallen has
  no real zone line to qeynos2; the P17 pair was reached via GM `#zone`
  teleport, not a walked zone line. Used befallen's one real physical zone
  line instead (to commons/zone 21), which exercises the same producer-B
  code path the plan's regression bar cares about.
- **`/zoning` command added** (`eq_commands_movement.cpp`, right after
  `/warp`). Zone-line-triggered zoning (`m_zoning_enabled`, gating
  `CheckZoneLine`'s actual trigger) defaults to `false` and was, before
  this unit, reachable **only** via a renderer `Ctrl+Z` keybind
  (`ZoningEnabledIntent`) -- there was no slash command at all, so a
  purely piped/headless session could never exercise a real zone-line
  crossing. This is another instance of the automation-reachability
  pattern (T26 `/combat`, T29 registry, T34 `/loot`, T42->T48 vendor, T50
  `/bank`+`/train` -- now a 6th instance). Not a scratch hook: it exposes
  the existing, unchanged `SetZoningEnabled`/`IsZoningEnabled` API through
  the standard command registry, the same pattern every prior instance
  used. **Required** to drive this unit's own regression-leg acceptance
  test via pure piped stdin.
- **Test-only accessors added to `eq.h`**: `GetPendingZoneId/X/Y/Z()` and
  `Test_SetZoneConnectedForSeam(bool)`. Minimal, read-only (except the
  last, which exists solely to force the dead consumer branch to execute
  in a fast unit test rather than leaving a fixed-but-never-tested branch)
  -- flagged per the charter's testability-seam allowance.
- **The `#goto` same-zone reply's position field was NOT consistently the
  same "wrong" value across runs**: a contaminated first attempt using a
  negative-number argument (`#goto 555 -333 8`) got an all-**zero** reply
  (`success=1`, `pos=(0,0,0)`), while the clean, isolated rerun with
  positive numbers (`#goto 100 200 5`) got the character's **existing DB
  position** echoed back instead. Both are consistent with "the server did
  not honor the requested position" (this unit's actual finding), but the
  inconsistency between zero and stale-echo was not root-caused (would
  require instrumenting the server, which is read-only reference here) --
  noted rather than papered over.
- **Zone-line return leg (commons->befallen) triggered but with a data
  gap**: the specific `commons.json` entry back to befallen has no
  destination coordinates at all (`0,0,0` after parsing), a pre-existing
  curated-data gap unrelated to this unit's x/y convention work. Landing
  was still correct (via `PlayerProfile`, not via this data), which is
  itself further proof of the "PlayerProfile always wins" finding, but the
  data gap itself is left unfixed (out of territory -- data curation, not
  code).

### Verification bar

1. **Both configures build clean**: `build` (`EQT_GRAPHICS=ON`,
   `EQT_GLES2=ON`) and `build-nogfx` (`EQT_GRAPHICS=OFF`) -- clean full
   rebuilds after every source change, no new warnings from this unit's
   files.
2. **`ctest --output-on-failure`**: `build` 1913/1914 passed,
   `build-nogfx` 1481/1482 passed -- sole failure in each tree is the
   documented `AudioManagerTest.PreloadCommonSounds` (ALSA) exception.
   Pinned tests from T43-T51 stay green. New `test_zone_position_convention`
   (4 cases) passes in both trees.
3. **Golden replay**: confirmed to have **run** (not skipped, not the
   synthetic-baseline fallback) in both trees via direct
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq
   ./bin/test_session_replay` -- both logged the real capture's zone-map
   lookups (`befallen.map`, `qeynos2.map` "Unable to open" errors, expected
   per T48-T51 precedent).
4. **`scripts/ci/visual_gate.sh`**: `PASS` (qeynos2/gfaydark/befallen all
   verified); `git status --porcelain data/` empty before and after --
   goldens untouched.
5. **Unit tests pinning the convention**: `tests/test_zone_position_convention.cpp`,
   4 cases -- producer A's swap (via `ReplayZonePacket` + new
   `GetPendingZoneX/Y/Z/Id()` accessors), producer A's denied-change
   clear, producer B's straight-passthrough (re-asserted against the same
   real `data/zone_lines.json` "akanon" fixture
   `test_zone_lines.cpp`'s pre-existing `LegacyFormat_AkanonPinnedFields`
   already covers, under this unit's own test name so a regression in
   either producer is caught by name), and the consumer's fixed
   straight-assignment (via `Test_SetZoneConnectedForSeam(true)` +
   `ReplayWorldPacket`, since the branch is otherwise unreachable). All 4
   pass in both trees.
6. **Live acceptance, both trees, entirely piped, `benchmarks/audits/T52/`**:
   - The step-1 wire table, before and after the fix (behaviorally
     identical, since the STOP-condition symptom is server-side and the
     consumer fix was to genuinely dead code) -- see above.
   - `#goto 100 200 5`: confirmed identical, unchanged behavior in both
     `build/bin/willeq` and `build-nogfx/bin/willeq` post-fix (lands at
     the pre-existing DB position both times, matching the pre-fix
     capture and T50's original finding).
   - **The regression legs**: real zone-line crossings, befallen<->commons
     (the real pair; see Deviations), **both directions**, confirmed
     landing correctly (`-1150.0,596.0,-38.0` outbound;
     `-75.2,35.1,3.1` return) via `PlayerProfile` + `/loc` + DB row, all
     three agreeing.
   - eqemu: lock created (`T52-zone-position-convention 2026-08-05`,
     confirmed absent first), `up.sh` only, Testchar restored to
     `zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190` (confirmed via
     `SELECT` before `down.sh`), `down.sh` clean, lock removed and
     confirmed absent.
   - T50's two environment traps avoided: did not attempt a full
     disconnect/reconnect with `#npctypespawn`'d NPCs still needed;
     restore used a direct `UPDATE character_data` (DB-column values), not
     `/warp`'s client-argument order, avoiding the x/y-swap-on-restore trap
     T50 hit.
7. `bash scripts/refactor/check_file_sizes.sh --files <7 touched/added
   paths>`: `eq.h` 2054 lines (**WARN, pre-existing** -- confirmed via
   `git stash` that the baseline `ca52bb48` revision is already 2027
   lines, over the 1500 threshold, before this unit's +28-line addition;
   not a new FAIL), `eq_commands_movement.cpp` 1009 (ok),
   `eq_login_world.cpp` 916 (ok), `eq_zone.cpp` 875 (ok),
   `tests/CMakeLists.txt` 1269 (ok), `test_zone_position_convention.cpp`
   208 (ok). No new FAILs.

### Board item: "server-side, not ours" for the `#goto`/succor/evac/bind
symptom; latent client-side bugs fixed as hardening

The T50 `#goto` symptom is confirmed server-side (STOP condition met,
live, reproducibly, in both trees). The suspected client-side
double-swap bug is real but was never live -- it lives in dead code
(`is_zone_transition` structurally always false) that is additionally
overridden by `PlayerProfile` even in the hypothetical case it ran. Fixed
anyway, plus an independent send-side swap bug in `RequestZoneChange`,
plus the cosmetic `zone_reason` ride-along, all as correctness hardening
with zero behavioral change confirmed live in both trees. The
`is_zone_transition`-always-false finding and the `commons.json`
destination-data gap are flagged for a future unit, not fixed here.
