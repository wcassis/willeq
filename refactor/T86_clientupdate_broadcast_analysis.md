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

(executor fills: the decisive comparison and its evidence; per-direction
ClientUpdate counts over time; the session diff if one was needed; confidence
in the attribution; whether GM status was ruled out; what this means for
WillEQ and what remains unknown)
