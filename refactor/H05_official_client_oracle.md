# H05 — Official-client oracle: Wine/Xvfb observer + player-driven capture

**HANDOFF DOCUMENT. This is scoped for a SEPARATE Claude session with its
own working root — not for a `wt/` executor slot, and not for the fable
orchestration session that wrote it.**

**That session captures and documents. It does NOT change WillEQ.** See
"Boundaries" — this is the most important section in the document.

## Why this exists

WillEQ's verification has been circular at every level, and each fix has
exposed the next layer:

- **One client** — we asked the client whether the client thought a command
  worked. T82 concluded `/face` succeeded from an `outcome=applied` ack while
  every entity it could have faced sat at distance zero.
- **Two clients** (T84, in flight) — fixes that, but only for the *wire*: did
  the server accept what we sent, and did it reach an observer. The server is
  the oracle.
- **Still circular:** two willeq instances share a decoder and a rendering
  convention. If our *interpretation* is wrong in a self-consistent way, both
  instances agree with each other and both are wrong.

That last gap is not hypothetical. Will has hit it: willeq's idea of the
player's position+heading did not match what the server sent to other
clients, and willeq has rendered a player at a different orientation than the
official client showed. Two willeq instances would both apply the same
convention, agree perfectly, and both be wrong.

**Only an independent implementation closes it.** That is the official
Titanium client.

Note also that `data/captures/session.pcap` and `session.sleq` already exist
and the replay oracle already runs against them — but they are **willeq's own
recordings**. That makes the existing oracle a *regression* oracle ("did we
change?"), not a *correctness* oracle ("are we right?"). An official-client
capture upgrades machinery that is already built and passing.

## The actual prize: the actor/observer matrix

Will's plan enables something better than a one-sided capture. With an
official client on a Windows 11 laptop AND an official client under
Wine/Xvfb on this VM, all four cells become reachable against the same
server:

| actor | observer | what it establishes |
|---|---|---|
| official | official | **ground-truth baseline — what correct looks like** |
| official | willeq | our **decode/render** is right (inbound) |
| willeq | official | our **encode/send** is right (outbound) |
| willeq | willeq | current state of the art (T84) — cheap, but circular |

The first row is what makes the other rows interpretable: without it, a
disagreement tells you something is wrong but not which side. **Capturing the
official→official baseline is the single highest-value output of this work.**

Rows 2 and 3 are the ones that answer real open questions. Row 3 in
particular is how you find out whether willeq is putting a malformed
position/heading on the wire — the server silently drops or transforms bad
input before rebroadcasting, so a real client failing to see us is proof.

## Environments

**A. Windows 11 laptop (Will, player-driven).** Real official client, real
human play. This is the actor for the baseline row. ~20 minutes.

**B. Wine + Xvfb on this VM (agent-driven observer).** Official client
running headless under Xvfb so a session can capture from it — packets
certainly, screen if it proves useful. This is the observer, and it is the
piece that makes the setup repeatable rather than one-shot.

**C. eqemu, already running here.** `scripts/eqemu/env.sh`: `LOGIN_PORT=5998`,
`WORLD_PORT=9000`, `ZONE_PORT=7000`. VM address on the LAN is `10.0.30.23`
(the `172.17/172.18` addresses are docker bridges — not reachable from the
laptop).

### Setup risks worth front-loading

These are where this work will actually stall, so establish them before
planning any capture detail:

1. **LAN reachability laptop → VM** on the login/world/zone ports. Zone ports
   in particular are often a *range*, and eqemu hands the client a port the
   client then connects to directly — verify what the akk-stack compose
   actually publishes, do not assume 7000 alone is enough.
2. **`eqhost.txt`** in each client directory must point at `10.0.30.23:5998`,
   not the default live-server host. Both the laptop client and the Wine
   client need this.
3. **Wine + Titanium** is well-trodden in the emu community but not
   frictionless. Establish that it launches, logs in, and zones **before**
   building any capture tooling on top of it.
4. **Two clients, two accounts.** `scripts/eqemu/env.sh` already seeds a
   second account for two-client work (`Testcaster` on `willeq_test`,
   `Testbuddy` on `willeq_buddy`). Confirm what exists in the DB rather than
   trusting the comment.
5. **Packet capture point.** Capturing on the VM sees the Wine client's
   traffic natively; the laptop's traffic is visible at the VM's interface
   too, since the server is here. Confirm both are actually captured before
   spending Will's 20 minutes.

**If Wine cannot run the client, say so and stop.** The player-driven capture
(environment A) still has standalone value and does not depend on B.

## The shot list — what the 20 minutes MUST contain

**Re-capturing means re-playing, so this list is the reason the scoping
document exists.** These are chosen to answer open WillEQ questions, not to
be a general play session.

0. **CHARACTER CREATION — first, and it cannot be added later.** Creation
   happens at character-select, *before* entering the world; once in-game that
   traffic is gone until a fresh login. **Capture LOGIN and WORLD traffic
   (`WORLD_PORT=9000`), not just zone** — a zone-filtered capture misses this
   entirely.

   This unblocks three things that are stuck today. `scripts/eqemu/seed.sh`
   records that **no sanctioned command path creates a character from a
   script** — a real client login is required. Consequently: concurrent
   clients are capped at two accounts (T84 hit exactly this ceiling, since
   `Testchar`/`Testmelee`/`Testcaster` share one account); T84's orientation
   table had to leave the **model-default-rotation column blank** because it
   needs characters across several races that we cannot create; and every live
   unit pays restore-and-`SELECT` teardown because characters are scarce.

   Create **three characters varying ONE attribute at a time** (baseline;
   race-only change; gender-only change) so packet field diffs are readable,
   plus a class-only change if time allows. **Then delete one**, so the
   deletion exchange is captured too. Also capture the character-select list
   exchange and an ordinary existing-character login for contrast.

   **Record every choice made** — name, race, gender, class, deity, starting
   city, and all face/hair/beard/colour slider values. Packet fields are
   meaningless without known inputs to correlate against; an unrecorded
   capture is a puzzle rather than a reference.

   Note the three routes to automation afterward, which differ a lot in cost:
   replaying captured packets (fragile — session state and sequencing),
   implementing creation in willeq (real work, but willeq has no coverage of
   this path at all today), or driving the official client via Wine+`xdotool`
   (Tier 1, and the most robust). **The capture is the reference spec for all
   three, and is also how a direct-DB-insert shortcut could be validated as
   producing something equivalent.**

1. **Face each cardinal direction and hold still** — N, E, S, W, then a few
   intermediate angles. Announce each in `/say` before turning, so the chat
   packet timestamps segment the capture. **This is the single most important
   item**: it is what settles the heading-convention question.
2. **Both characters visible to each other**, at a known separation, each
   facing the other, then each facing away. The rebroadcast path must be in
   the capture.
3. **Movement** — walk, run, strafe, jump, stop. Smooth motion vs teleporting
   is a known open question.
4. **Equip a visible weapon**, primary and secondary, and sheath/unsheath.
   Held-item appearance has cost this project several units.
5. **Sit, stand, and a melee swing.** Animations propagate as their own
   messages.
6. **Zone once**, if convenient.
7. **An indoor zone and an outdoor zone** if time allows — Befallen's
   region/PVS visibility defect and the indoor sky-dome finding are both open.

Keep a written running order with wall-clock times. A capture nobody can
segment is much less useful than one with a timeline.

## Deliverables

1. Working, documented Wine+Xvfb official client on this VM — or a clear
   statement of where it fails.
2. **The capture corpus**: pcaps, plus the running order and timeline.
3. **A README** describing what each capture contains and how to replay or
   inspect it.
4. A short note on where captures should live and what belongs in git —
   `data/captures/` holds the existing willeq recordings, but these are
   larger and are reference data, so **propose rather than assume**.

## Boundaries

- **Do not modify WillEQ source.** If the capture shows our heading
  convention disagrees with the official client, that is a **finding to bring
  back**, not a fix to apply. The `90 - server_heading_deg` offset in
  `src/client/eq.cpp` is deliberate and hard won — players and NPCs do not
  share default rotations and the model files carry differing ones. There is
  a load-bearing comment at the site and a warning in `docs/CLAUDE.md`'s
  Heading/Rotation Conventions. Read both before forming any opinion.
- **Honor the eqemu lock** (`/mnt/projects/fable/.eqemu-lock`). It is
  exclusive and shared with the fable orchestration session's live units.
  Coordinate; do not bypass it.
- **Do not automate Will's 20 minutes.** An agent cannot sensibly drive the
  official client until the Wine environment is proven and Tier 1 tooling
  exists. The human-in-the-loop step is the point, not a limitation.
- Treat the official client as **read-only ground truth**. No patching, no
  memory writes, no modification of client files.

## Tiering

- **Tier 0 — capture.** Environments A and B stood up; baseline corpus
  recorded. Highest value per unit of effort; do this first and stop to
  evaluate.
- **Tier 1 — scripted Wine session** with injected input (XTEST/`xdotool`)
  so the corpus is regenerable and extensible. Still packets, not pixels.
- **Tier 2 — read the official client's screen or memory** for visual ground
  truth. Only if 0 and 1 leave a real question unanswered. Expect brittleness.

**Keep this an investigation tool, not a CI gate.** GUI automation against a
closed 2005 binary with no synchronization primitives will be timing-flaky,
and a flaky gate trains everyone to ignore it. Generate the corpus
occasionally; assert against the *corpus* in CI, where it is deterministic.

## Workspace

Use a **separate root**, e.g. `/mnt/projects/eq-oracle` — not a checkout of
the WillEQ tree.

- This work produces an environment and a binary corpus, not source changes.
  It has no reason to touch `/mnt/projects/fable/main`, and keeping it out
  avoids any collision with in-flight task branches and executor slots.
- It needs the eqemu harness (`scripts/eqemu/`) and the lock convention —
  read those from `/mnt/projects/fable/main` read-only, or copy the handful
  of scripts. One server, one lock, no source coupling.
- **`/mnt/projects/willeq-ro` is stale, and refreshing it is not required for
  this work** — Tier 0 and Tier 1 need Wine, the client files, and capture
  tooling, not the WillEQ source. Do not refresh it just to enable this.
- Handing the corpus back into the WillEQ repo is a **deliberate handoff**,
  proposed in deliverable 4 — not an automatic sync.
