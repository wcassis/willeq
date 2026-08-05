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
