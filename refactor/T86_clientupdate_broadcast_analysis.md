# T86 — Why did Wizzy's position updates never reach WillEQ?

**Slot: slot1. NO EQEMU, NO LIVE SESSION. Pure analysis of data already
captured. Do not bring the server up for any reason.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T86/`, throwaway
analysis scripts. PRODUCTION CODE IS READ-ONLY.**

Read T85's Review first, then
`/mnt/projects/eq-oracle/captures/README.md` and
`/mnt/projects/eq-oracle/notes/baseline-session-2026-08-13.md` **in full** —
they contain measured facts about the capture that will mislead you badly if
you skip them.

## The question

T85 established, from WillEQ's own `.sleq` wire capture, that `Wizzy`
(spawn 462) reached WillEQ **exactly once** — the zone-sync echo at
00:13:51 — and then never again across ~28 minutes of a confirmed live
cardinal-heading routine. Other spawn ids produced hundreds of
`OP_ClientUpdate` records in the same window, so **this is not a decode
failure. The packets never arrived on `willeq_a`'s wire.**

Why not?

## The decisive comparison — do this first

The oracle capture holds **three** clients' traffic simultaneously. That makes
one comparison worth more than all other analysis:

**Did `Oracledelta` (the official observer) receive `Wizzy`'s ClientUpdate
broadcasts during the same window in which `willeq_a` did not?**

- **If yes** — the server broadcast them, and something about WillEQ's session
  meant it did not get them. That points at us: a subscription we never
  establish, an update-radius or interest request we never send, a client
  state the server reads as "not ready", or something we send that suppresses
  them. **This is the interesting outcome and it is a real defect.**
- **If no, neither client received them** — the server did not broadcast, and
  the cause is server-side or Wizzy-side (e.g. he genuinely did not move
  enough to trigger updates, or the server culls by distance). **Then this is
  not a WillEQ defect at all**, and saying so clearly is just as valuable.

Do not proceed to deeper analysis before answering that. It splits the problem
in half.

## The evidence

- `/mnt/projects/eq-oracle/captures/baseline_official_official_20260812-234154/`
  — `capture.pcap` (427,734 packets, 49M), `running-order.md` (32 wall-clock
  entries), `summary.md`.
- WillEQ's own six `.sleq` fragments in `/mnt/projects/fable/wt/t1/data/captures/`
  (gitignored, listed with exact time spans in T85's Review). **Copy what you
  need; that worktree will be reused.**
- T85's wire-evidence parser in `/mnt/projects/fable/review/T85/`.

## Capture facts you must account for — measured, not assumed

From the oracle session's README. Ignoring these produces confident wrong
answers:

1. **A LAN client's packets appear THREE times** on the `-i any` capture (once
   on `enp1s0`, once on the docker bridge, once on the container `veth`). Any
   per-packet counting must dedupe or it overstates by 3x.
2. **Each side appears under two identities** because the server sits behind
   docker NAT. Server replies show up as both `172.18.0.3` and `10.0.30.23`.
   Key on the **client** side and on direction.
3. **Both VM clients share source `10.0.30.23`** — `Oracledelta` and WillEQ.
   They are separated by `ip.port` session. **The WillEQ attribution in the
   oracle's note is BY ELIMINATION, not a protocol fact.** Verify it
   independently (WillEQ's own `.sleq` timings give you a cross-check) and say
   how confident you are.
4. **Session identity is per ZONE, not per client** — every client gets a new
   source port when it zones. Counting sessions counts zone visits.
5. Zone ports: **7007 = South Qeynos**, **7012 = North Qeynos**. `Wizzy` is
   the only remote address (`10.0.30.169`), so he is trivially separable.

## Use the existing protocol tooling

Do **not** hand-roll a Daybreak parser. The project already has one, plus
pcap-driven tests: `src/common/net/` (`DaybreakConnection` — sequencing,
fragmentation, CRC, compression), and `tests/test_daybreak_*` /
`tests/test_pcap_*`, which already read `data/captures/session.pcap`. Reuse
that path. If it cannot read the oracle pcap for some reason, **report why** —
that is itself a finding about our tooling, and one worth knowing before the
next capture.

Note the Daybreak layer compresses and CRCs; raw `tcpdump` payload inspection
will not show you opcodes directly. Plan for that rather than concluding
"no ClientUpdates present" from an unparsed byte search.

## What to produce

1. The answer to the decisive comparison above, with the packet evidence.
2. A **count over time** of ClientUpdate-bearing traffic for spawn 462 in each
   direction: server→`Oracledelta`, server→`willeq_a`, and `Wizzy`→server.
   The third tells you whether Wizzy was even sending; the first two tell you
   what the server did with it.
3. If the server did broadcast to `Oracledelta` but not to WillEQ: **what is
   different about the two sessions?** Compare their login/zone-entry
   exchanges packet by packet. Something we do or fail to do is the cause, and
   the diff of the two entry sequences is where it will be visible.
4. A written statement of confidence in the by-elimination attribution.

## Caveats to carry into any conclusion

- **All three characters were GM-flagged (status 250)** during the session.
  Every spawn struct has that field set. Do not diff against "a normal
  player" without accounting for it, and consider whether GM status itself
  affects broadcast behaviour — some servers suppress or alter updates for
  GM-flagged characters, which would be a mundane explanation worth ruling
  out early.
- **Zone transitions were `#zone` teleports, not walked zone lines**, except
  the two death respawns.
- WillEQ has one genuine ~14s blind gap at 00:19:01-00:19:15.

## What you may not do

- **NO production code changes.** This unit explains; it does not fix.
- **Do not start the eqemu server.** Everything needed is already captured.
- **Do not conclude from absence without checking your parser.** "I found no
  ClientUpdates" is only meaningful if you have demonstrated your method finds
  them where they are known to exist — validate against a spawn id T85 already
  proved produces hundreds of records.

## Verification

Build + full suite to prove nothing broke (expect 2066/2067; sole failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Analysis scripts and
findings to `/mnt/projects/fable/review/T86/` with a README.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T86-clientupdate-analysis`, HEAD `b4d11d5f`. **No production code was
touched** — read-only analysis plus one throwaway standalone C++ tool.
Analysis scripts, decoded evidence, and README:
`/mnt/projects/fable/review/T86/`.

### Tooling: reused the project's own pcap/Daybreak stack, not a hand-rolled parser

Wrote `review/T86/extract_clientupdate.cpp` (~230 lines, not part of the
CMake build — throwaway, compiled standalone with `g++ -std=c++17
-I<slot>/tests -O2 ... -lz`), which `#include`s `tests/pcap_test_utils.h`
(pcap reading, port/session discovery) and `tests/daybreak_test_harness.h`
(fragmentation, `OP_Combined`, decompression, CRC-strip) **verbatim** —
exactly the machinery `test_pcap_extractor`/`test_pcap_step_through`
already use for `data/captures/session.pcap`. It did read the oracle's
`capture.pcap` without modification, so there is no tooling-gap finding to
report here (unlike the plan's fallback instruction). Raw UDP packets
(427,734) were deduped by exact `(src_ip,src_port,dst_ip,dst_port,payload)`
match before any reconstruction (→ 181,310), collapsing the measured 3x
`-i any` duplication. Sessions were identified purely by **port pair**
(zone port + client ephemeral port, taken from the oracle's own
`README.md`/`baseline-session-2026-08-13.md` session tables) — this
sidesteps the docker-NAT dual-server-identity problem entirely, since a
unique ephemeral client port already disambiguates without needing to
resolve which of `172.18.0.3`/`10.0.30.23` is in play.

### Method validation (presence before absence)

Before trusting the zero-count for spawn 462, the same tool/pipeline was
run against spawn ids T85 already proved (from its own, differently-sourced
`.sleq` capture) produce "dozens to hundreds" of records: 460 (self, 430
here), 32 (178), 40 (167), 42 (129), 172 (107), 448 (37), 450 (33), 453
(32) — full table in `review/T86/README.md`. All matched T85's independent
finding. The extraction demonstrably finds ClientUpdate records where they
are known to exist, cross-validated against a completely independent data
source (the oracle's network-level pcap vs. WillEQ's own client-side
`.sleq`), before the zero-count for 462 is trusted.

### The decisive comparison — YES

| direction | session | spawn 462 records, ~00:13:51–00:41:55 |
|---|---|---|
| Wizzy → server (C→S) | `wizzy_58340_7007` | continuous, 18–262/min throughout — Wizzy was actively transmitting the whole window, not silent |
| server → Oracledelta (S→C) | `oracledelta_47823_7007` | **898**, 00:13:52–00:34:30, tracking his cardinal-heading routine (00:14:14–00:17:42 per `running-order.md`) and beyond |
| server → willeq_a (S→C) | `willeq_53793_7007` (Wizzy's actual live spawn session) | **0** |
| server → willeq_a (S→C) | `willeq_56481_7007`, `willeq_59987_7007`, `willeq_44587_7007` (later re-zones, Wizzy already present) | 1 each — the zone-sync burst echo only |

**Oracledelta received Wizzy's broadcasts continuously in the exact window
`willeq_a` received essentially none.** The server broadcast them; Wizzy
was sending them; something about `willeq_a`'s own session is the reason
they didn't arrive. This is the "interesting outcome" branch the plan
called out, and it is a real WillEQ-side defect, not a server- or
Wizzy-side non-issue.

### GM status — ruled out directly

All three characters (Wizzy, Oracledelta, WillEQ) were GM-flagged (status
250) per the capture's own caveats. Oracledelta is GM-flagged and received
898 records in the same window WillEQ received ~0. If GM status suppressed
or altered broadcasts, Oracledelta would show the same silence. It
doesn't. Ruled out cheaply, without any server-side access, exactly as the
plan suggested doing.

### The session diff: a strong root-cause candidate, found this unit

Per the plan's instruction ("compare their login/zone-entry exchanges
packet by packet... the diff of the two entry sequences is where it will
be visible"), diffed the two sessions' opcode sequences
(`review/T86/all_opcodes.csv`). Found two things:

1. **Handshake duplication (secondary, unexplained).** WillEQ's
   `willeq_53793_7007` sends several C→S zone-entry-phase reliable packets
   **twice** each (`0x367d`/`SendAATable`, `0x5966`/`UpdateAA`,
   `0x067a`/`SendTributes`, `0x5e3a`/`RequestGuildTributes`,
   `0x0322`/`ReqClientSpawn`, `0x0587`/`SendExpZonein`,
   `0x6563`/`SetServerFilter`, `0x5e20`/`ClientReady`,
   `0x1004`/`ChannelMessage`), each pair carrying distinct Daybreak
   sequence numbers tens of ms apart. Oracledelta sends each once. Real,
   measured, not pursued further — doesn't obviously connect to the
   mechanism below, and distinguishing a genuine app-level double-send
   from Daybreak-layer retransmission needs more careful sequence
   tracking than this pass did.

2. **A ~1300-unit position mismatch (primary, quantitatively conclusive).**
   Decoded WillEQ's own outbound self-position
   (`PlayerPositionUpdateClient_Struct`, C→S `OP_ClientUpdate`, spawn 460 =
   self) during the parked/observing window: `x_pos=306.462,
   y_pos=-646.949` — the settled tail of a genuine, smooth, continuous
   23-second walk (00:09:14.05→00:09:36.90) matching T85's own reported
   `/move`-to-park timeline entry to the millisecond, i.e. real data, not
   corruption. Decoded the same struct from Oracledelta's own outbound
   self-report for a sanity check: `x=-653, y=314` — an **exact** match to
   the join spec's stated parking coordinates, confirming the field decode
   itself is correct and the official client applies no swap. Decoded
   Wizzy's true position from Oracledelta's *received* S→C broadcast
   (`PlayerPositionUpdateServer_Struct`, 19-bit/12-bit bitfields,
   `EQ19toFloat`/`EQ12toFloat` scale factors from
   `/mnt/projects/claude/EQEmu/common/misc_functions.cpp`): `x=-618.25,
   y=301.625` (stationary throughout).

   Distance from WillEQ's as-sent wire position to Wizzy's true position:
   **≈1324.8 units.** eqemu's player-`OP_ClientUpdate` broadcast gate
   (`zone/client_packet.cpp Handle_OP_ClientUpdate` →
   `entity_list.QueueCloseClients(..., RuleI(Range,
   ClientPositionUpdates), ...)`, `zone/entity.cpp`'s straight
   `DistanceSquared` check) defaults to 300 units
   (`common/ruletypes.h`). 1324.8 is nearly 4.5x even a generously-doubled
   default — a client that far away receives nothing, full stop. Reading
   WillEQ's `x_pos`/`y_pos` with the axes swapped back — matching the
   deliberate, documented inbound/outbound swap convention already in
   `src/client/eq_movement.cpp:1064-1068` (`SendPositionUpdate()`'s own
   comments: "to send back: server's y_pos = m_x, server's x_pos = m_y")
   and `src/client/eq.cpp`'s `ZoneProcessPlayerProfile`
   (`SetPosition(server_y, server_x, z)`, documented as "load-bearing...
   hard won") — drops the distance to **≈29.1 units**, well inside 300 and
   closely matching the ~37 units separating Oracledelta from Wizzy (i.e.
   what "parked near Oracledelta" was supposed to produce).

   This also explains, without needing a second mechanism, why *other*
   spawns (NPCs 32/40/42/172/448/450/453) still delivered hundreds of
   records to WillEQ in the same window: the NPC movement-broadcast path
   (`zone/mob_movement_manager.cpp`) is *also* distance-gated against the
   same server-tracked client position (`RuleI(Range,
   MobCloseScanDistance)` default 600, `RuleR(Pathing,
   ShortMovementUpdateRange)` default 130) — a server-side position
   >1300 units from Wizzy landing within 600 units of *some* NPC
   scattered elsewhere in a large zone is unremarkable.

   **Not root-caused to one line, and not fixed** — the outbound code's
   comments state it is trying to undo the inbound swap bit-for-bit; the
   measured wire output does not do that, but this unit did not determine
   whether the defect is in the outbound un-swap, the inbound swap, or how
   `/move`'s arguments get assigned to `m_x`/`m_y`. Flagged in
   `refactor/deferred.md` as believed wrong, not touched, exactly as
   T84/T85 flagged their own findings. Full numeric derivation and
   supporting decoded-position CSVs: `review/T86/README.md`.

### Confidence in the by-elimination attribution: high

Three independent cross-checks, none of which the oracle's own notes had
access to:

1. **Self-spawn-id agreement across independent capture mechanisms.** This
   unit's *network-level* pcap decode of `willeq_53793_7007`'s C→S traffic
   finds self spawn_id 460 — matching T85's *client-side* `.sleq`-based
   finding of the same value, from an entirely different recording
   mechanism.
2. **Sub-3ms timestamp agreement.** `willeq_53793_7007`'s first S→C
   `OP_PlayerProfile` lands at `00:09:10.171479` (this unit); T85
   independently logged "00:09:10.173 South Qeynos zone-in confirmed" from
   WillEQ's own client — 1.5ms apart.
3. **Movement-duration agreement.** The decoded self-position trace shows
   a smooth 23-second walk, 00:09:14.05→00:09:36.90, matching T85's
   reported "00:09:14.019–00:09:36.891 `/move` to park near Oracledelta"
   to within tens of milliseconds at both ends.

Not a cryptographic proof (the docker-NAT identity ambiguity the oracle
flagged is real), but three independently-sourced signals agreeing this
tightly makes misattribution implausible.

### What this means for WillEQ, and what remains unknown

**For WillEQ**: this is a real defect, not a server quirk or a Wizzy-side
non-issue. WillEQ's outbound position report to the server, taken at face
value by the server's own distance-gated broadcast logic, places the
client roughly 1300 units from where it visually/internally believes
itself to be — which silently starves it of position updates for anyone
outside that mistaken radius, while updates for spawns that happen to fall
within range of the *wrong* location continue to arrive normally,
producing exactly the "everything else looks fine, one specific spawn is
silent" signature T85 first noticed. This is a live-multiplayer-only
defect class a single client (or two WillEQ clients replaying against each
other) structurally cannot detect.

**What remains unknown**: (1) whether the live eqemu server's
`Range:ClientPositionUpdates` rule was at its 300-unit default or
overridden — no server-side access this unit, though the conclusion holds
comfortably even at several times the default given the 1324.8-unit
measured error; (2) which of the two swap sites (or `/move`'s argument
handling) is the actual one-line defect — a live round-trip test (send a
known position, read back what the server/another client believes) would
settle it, forbidden by this unit's no-eqemu mandate; (3) the handshake
duplication observation, real but unexplained; (4) T85's own
recommendation of a server-side pcap from the oracle session remains
un-actioned — this unit reached its conclusion from the client side of the
wire only, and a server-side log would turn the position hypothesis from a
very strong inference into a certainty.

### Verification

- `cmake --build build -j24` — clean, no errors.
- `ctest --test-dir build --output-on-failure` — **2066/2067**, sole
  failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` —
  matches the expected baseline exactly. No other verification steps were
  in scope (plan narrows this to build+suite only; no live session, no Pi,
  no visual gate, no eqemu).
- `refactor/deferred.md` updated: T85's Wizzy-silence entry re-titled to
  point at this addendum; full addendum appended in place (never-delete
  rule honored).
