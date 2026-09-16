# T492 — three-player verification corpus for the Inspur LLM serving benchmark

Class C, **Will's ruling 2026-09-16: this unit runs BEFORE the T485/T486/T490/
T491 worktree recovery.** Live unit (eqemu lock for the whole recording), one
executor, worktree `wt/t3`, branch `task/T492-llmbench-corpus` from `fable`.

## What is being asked and by whom

The Inspur serving session wrote
`/mnt/projects/archive/docs/willeq-multiplayer-llm-benchmark-handoff.md`.
Read it first; §9 is this session's answers and is binding where it corrects
§1–§8. Short form: record ONCE, with three simultaneous interacting players
against the real server, a per-player corpus (frames at every mark, marks,
run log, ground truth) that the serving session will replay through an agent
loop against several inference layouts. The corpus must be replayable without
live clients, so it must be complete and self-describing.

Will also confirmed the corpus doubles as the sizing test for local-model
executors (q4x-a 4 seats, 27B+DFlash2 1 seat), so realism of the transcript
shape (log volume per step, frame at every step) matters more than polish.

## Verified state (2026-09-16, orchestrator)

- eqemu lock **FREE**; world **UP since 09-14** (zone processes alive on
  172.16.200.99). `scripts/eqemu/status.sh` works from this host.
- hermes VM 172.16.200.100: reaches eqemu 5998 (login) and 9000 (world);
  checkout `~/src/willeq` at **ed410e0e (stale)**, binary present; 16 cores,
  31 GB, assets at `/mnt/projects/EverQuestP1999`. Needs fetch + build at
  fable tip (Leg 0).
- Official client rig: `/mnt/projects/eq-oracle` (Wine/Xvfb 1024x768, window at
  root (−1, 30), window grab 1023x738, root grab 1024x768). Drivers to COPY, not
  edit: `review/T478/scripts/t478_oracle_start.sh` (login + enter world),
  `t478_o.sh` (`cmd`/`key`/`hold`/`click`/`shot`/`ocr`/`video`, writes a
  marks TSV in `HH:MM:SS.mmm<tab>text` form). Camera cycle F9: 0 FirstPerson,
  1 Overhead, 2 Chase, 3 User1, 4 User2, 5 Tether. F10 toggles UI.
- WillEQ scripted play: `--script <file>` (lines `wait N`, `/say ...`, `/loc`,
  ...; examples `review/AUDIT-0914/scripts/*.script`), uinput key injection with
  schedule + marks (`review/AUDIT-0914/scripts/uinput_inject_any.py`,
  `run_x86.sh`), per-frame dumps on x86 (`review/AUDIT-0914/x86/raw/R5_*/
  invocation.txt` has the exact flags). Headless never exits — kill it.
- `scripts/scenario.sh` (T410): lock take (atomic, exit 64 if held), world up,
  `character_data` snapshot, restore with the world DOWN, byte-identical check.
  REUSE its functions; the recorder is a new script beside it.
- Accounts (one world login each): official **Oraclealpha / willeq_a** (GM; T478
  used `#wc`, `#loc`); WillEQ **Testcaster / willeq_test**; WillEQ **Testbuddy /
  willeq_buddy** (`scripts/scenario_configs/peer.json`). **Do NOT use Testchar** —
  it sits at z=−495 deliberately as T489 evidence.
- Known-discrepancy items with B-numbers on `refactor/BOARD.md` for `expect=`
  labels: B721 (Chase framing), B722–B725 (held items A/B/C/IT10), B780 (R/B
  vertex colour), B781 (sky tint), B782 (distant trees), gem art absent
  (AUDIT-0914). Match steps only where a prior unit fitted the pair against the
  oracle (First Person login camera, T310/T466 camera fits).

## Legs (sequential; one executor; lock held only in Legs B–C)

### Leg 0 — hermes VM build (no lock)
Fetch `fable` on the hermes VM checkout, build `build/bin/willeq` at the same
commit as the dev host `build-gles2` binary, prove it logs in as Testbuddy to
the dev-host-visible world (a 60 s `zone_in` scenario) and exits cleanly. Record
the hermes↔dev-host clock offset (`date +%s.%N` both sides over ssh, three
samples) for `manifest.json`.

### Leg A — the recorder (no lock while writing; dry run under lock)
`scripts/llmbench/record.sh <unit>` plus per-player step files under
`scripts/llmbench/players/`. It: takes the lock (scenario.sh functions), world up
if down, snapshot; starts p1 official (Oraclealpha, dev host Xvfb), p2 WillEQ
x86 gles2 (Testcaster, dev host, frame dumps on), p3 WillEQ (Testbuddy, hermes
VM over ssh, frame dumps on); drives all three from ONE timeline file
(`scripts/llmbench/timeline.tsv`: `t_offset<tab>player<tab>action<tab>truth
k=v;...`) so every step yields, per visible player, a window frame (and root
frame for the official client), a marks line, a `truth.tsv` line written BY
THE DRIVER from what it commanded, and the chat OCR crop text; on finish or on
ANY failure: kill clients, restore snapshot with the world DOWN, release lock.
A dead-client watchdog (no new log line in 120 s → abort) and a hard stop at
recording length + 10 min (feedback-watch-long-live-runs).

Frame naming, layout and `manifest.json` exactly as handoff §4, corrected by
§9 (window 1023x738, root 1024x768). One clock: the dev host; hermes offset
stated in `manifest.json`.

### Leg B — dry run (lock, ≤ 10 min)
Three players, ~8 steps each: zone in, `/say` seen by the other two (assert on
the OBSERVERS' logs, never the actor's), one camera change, one frame pair.
Fix the recorder until the dry run's corpus passes the acceptance checker
(Leg D) end to end. Keep the dry-run corpus as `review/T492/llmbench-dry/`.

### Leg C — the recording (lock, 30–45 min per player, ≥ 40 steps each)
Content, in this order, all three players in qeynos2 then across a zone line:
1. zone in; each player `/say` and `/tell` the other two (cross-references);
2. target each other (F8 / `/target`), `/loc`, `/who`;
3. camera changes: official F9 cycle through all six; WillEQ the matching
   `/camera` modes; frames at each (FirstPerson = match steps, Chase = B721);
4. held-item change: sets A, B, C, IT10 by the T478 `#wc` / item recipes on
   Oraclealpha and the equivalent on the WillEQ characters — these are the
   B722–B725 discrepancy steps; idle frames at Chase + Tether;
5. movement: a 20 s run and turn, frames every 5 s; sky in frame (B781), a
   distant tree line (B782);
6. a fight: `#npctypespawn 77002` (training dummy) by the GM account, all
   three auto-attack, one player dies (`#damage` or `#kill` self) and respawns;
   inventory change (`#summonitem`, pick up, `/inventory` open on WillEQ —
   the T462 inventory popup);
7. zone line qeynos2 → qeytoqrg, all three; frames after zone-in;
8. camp out in order p3, p2, p1.
Every `#` command is issued by the GM account only, and every runtime spawn is
removed before restore (B720: dummies persist via `zone_state_spawns`).
Any temporal step (attack swing, zone-line crossing) also gets a short video
per the CLAUDE.md rule; stills at the mark remain the primary record.

### Leg D — ground truth, labels, acceptance, hand-off
- `truth.tsv` per player is the driver's; the executor adds `expect=` per step:
  `discrepancy:B###` only with a board entry, `match` only where a prior unit
  fitted the pair; everything else `expect=unlabelled` (the grader ignores
  those). **No model judges a frame.**
- `scripts/llmbench/check_corpus.py`: handoff §7 acceptance (3 players, ≥ 40
  steps each with mark + frame + truth, ≥ 5 discrepancy + ≥ 10 match steps with
  a paired official frame, `manifest.json` loads standalone, timestamps join).
  Run it on the dry run and the recording; its output goes in the README.
- `review/T492/llmbench/README.md`: hosts, accounts, characters, running order,
  clock offsets, checker output, the sheet list for Will's bless
  (`review/T492/sheets/`: contact sheets of every match/discrepancy step pair).
- Board: add B-entries for any rig defect found; strike nothing else.
- Commit ONLY `scripts/llmbench/*`, the plan's Review, and board/docs changes on
  the branch. The corpus lives under `review/T492/` (outside the checkout).

## Rules
- `wt/slot1`, `wt/slot3`, `wt/t1`, `wt/t2` hold dead executors' uncommitted
  work: **never touch them**. `wt/slot2` is T479 HELD.
- Lock: take with `eqemu_lock_take T492`; exit 64 means STOP, never wait or
  clobber. Release on every exit path. World DOWN for the restore.
- Do not sample a transient with stills (record video, step it). Crop against
  the window, not the root.
- Report `[measured]` / `[assumed]` on every claim in the Review.
- Time box: Legs 0–B 3 h, Leg C 1 h under lock, Leg D 1 h. Report and stop
  at the box; a partial corpus that passes the checker for 2 players is worth
  more than a third player that broke the run.

## Review

Every claim is tagged `[measured]` (it has a file, a frame, a mark or a truth
line behind it in this unit) or `[assumed]`.

### Leg 0 — hermes-ext build and login proof

- `[measured]` The hermes VM checkout `~/src/willeq` was at `ed410e0e`; the
  gitlab remote's `fable` is at `ed410e0e` too, so the tip could not be
  fetched from it. It was moved with an incremental `git bundle`
  (`git bundle create t492.bundle fable --not ed410e0e`, 1.0 MB), fetched into
  `refs/remotes/devhost/fable` and checked out DETACHED at `340158a2`. No
  branch anywhere was created, moved or pushed.
- `[measured]` `cmake --build build-gles2 --target willeq -j16` on hermes-ext:
  BUILD_EXIT=0, `build-gles2/bin/willeq` 90,945,344 bytes. `libIrrlicht.a` and
  `third_party/irrlicht-gles2-x86/` were already bootstrapped there, so only
  willeq was rebuilt.
- `[measured]` Testbuddy (`willeq_buddy`) logged in from hermes-ext to the
  dev-host-visible world and reached `Graphics loading complete. Game ready!`
  — one occurrence in `/tmp/llmbench-T492-p3/run.log`, in qeynos2, under
  surfaceless EGL/llvmpipe with NO display.
- `[measured]` hermes-ext clock offset vs the dev host:
  **+0.0066 s** (median of 7 samples over an ssh ControlMaster socket, rtt
  0.036–0.043 s, spread 0.0060–0.0086). The first, naive three-sample
  measurement over fresh ssh connections gave +0.24 to +0.46 s — the ssh
  handshake, not the clock. `scripts/llmbench/clock_offset.sh` is the one that
  is right.

### Leg A — the recorder

`scripts/llmbench/` (committed on the branch):

| file | what it is |
|---|---|
| `record.sh` | the recorder: lock, server, snapshot, place, compile, start p1/p2/p3, drive one timeline, harvest, restore, release |
| `teardown.sh` | the same teardown, standalone, for when the recorder dies without its trap |
| `o.sh` | the official-client driver — a copy of T478's `t478_o.sh` (local-time marks, `pair` also OCRs the chat crop) |
| `oracle_start.sh` | a copy of T478's launcher, plus a pristine `eqclient.ini` restored before every launch |
| `make_timeline.py` | generates `timeline.tsv`: the row SPACING is derived from the action, not hand-written |
| `compile_timeline.py` | one timeline → the two willeq `--script` files + `steps.tsv`/`steps.json` |
| `harvest.py` | one willeq `run.log` → `marks.tsv`, `truth.tsv`, `chat.tsv`, and the frames renamed to `<HH:MM:SS.mmm>_<label>_window.png` |
| `clock_offset.sh` | the other host's clock minus this host's, median over a ControlMaster socket |
| `check_corpus.py` | handoff §7 acceptance, by joining marks/frames/truth on the timestamp |
| `sheets.py` | contact sheets of every labelled step pair, for Will |
| `write_readme.py` | the corpus README, read back out of the corpus |

Design decisions worth stating:

- `[measured]` **The willeq clients are driven by `--script`, not live**, because
  a command step is HELD until `m_fullyConnected` while `wait` ticks during
  connection (`Application::updateScript`). A lead `wait` therefore puts step 0
  at a deterministic wall-clock time, and the driver's T0 is that time.
- `[measured]` **Frames come from the client's own `/screenshot <path>`**
  (`renderer_slash_commands.cpp`), which takes an explicit filename and logs
  `Screenshot saved: <path>`. So the compiler names every frame and the log
  timestamps it; no frame-dump-and-match-by-mtime is needed, and `harvest.py`
  can key marks, truth and frames on the same instant.
- `[measured]` **Every step frames every player** — the official client gets a
  window+root pair on every row, not only its own — so a match or discrepancy
  step on any player has a paired official frame, which is what handoff §7
  asks for.
- `[assumed]` The willeq `wait` clock and the driver's wall clock stay close
  enough over the run for the interleaving to be realistic. The corpus does not
  depend on it: every marks/truth timestamp is read out of the client's own log.

### Leg B — the dry runs (three of them; the first two failed, usefully)

**Dry run 1** `[measured]` — spent the whole five-minute official login before
discovering that `build-gles2/bin/willeq` does not exist inside a task
worktree. `record.sh` now PREFLIGHTS everything cheap (both binaries, the
client copy, the assets, the tools, the hermes hop) before it takes the lock or
starts anything. Killed by hand; because it was killed without its trap, and
because this executor had edited the script on disk while bash was still
reading it, the teardown had to be run from outside — which is why
`scripts/llmbench/teardown.sh` exists.

That teardown then found **B784** `[measured]`: `pkill`-ing world/zone inside
the container and restoring 90 rows one ssh round trip at a time takes two
minutes, the container's supervisor restarts the fleet inside that window, and
`Testbuddy`'s row came back written over (zone 2 / −64 / 428 / 3.75 / 160
instead of the snapshot's zone 36 / 0 / 0 / 3 / 0) while the three characters
who were NOT logged in restored cleanly. Stopping the eqemu-server container
(mariadb alone up) and sending the restore as one statement batch: **25 s,
byte-identical**.

**Dry run 2** `[measured]` — 24 of 24 steps, 49 official frames (24 pairs plus
the untouched login frame), 24 willeq frames each, 24 truth lines each, and a
byte-identical snapshot AND inventory restore. It passed all eleven handoff §7
lines of `check_corpus.py`. **And it was worthless**, because the three players
never overlapped: not one cross-player `/say` reached an observer. The cause is
`[measured]`: p3's willeq started on hermes-ext at 14:43:41 and was in world at
14:43:50 — ten seconds — but the `ssh` that launched it did not RETURN until
14:49:19, five minutes and forty seconds later, because the backgrounded client
still held the session channel's stdin. T0 (launch + lead) was three minutes in
the past when the driver reached its loop, so it ran the whole timeline flat
out while the willeq scripts kept their own clock.

Three things came out of that:

- `ssh -n` plus `< /dev/null` on the remote background command; p3's launch call
  now returns in under a second.
- a **desync guard**: if T0 is already within 15 s when the driver reaches its
  loop, it refuses to record rather than spend the lock on a corpus whose
  players do not overlap.
- `check_corpus.py` grew a **cross-reference check**: a `/say` or `/tell`
  counts only when its text appears in a DIFFERENT player's `chat.tsv`. It
  fails dry run 2's corpus, which every §7 line passed — the corpus is kept at
  `review/T492/llmbench-dry-run2-desynced/` as the evidence for that.
- `[measured]` What an OBSERVING willeq client logs is
  `[FormattedMessage] channel=<n>, sender='<name>', text='<...>'`; the first
  regex (says/tells/shouts) found two lines in a whole session and both were an
  NPC shout. `harvest.py` keys on `[FormattedMessage]`.
- `[measured]` **B788**: 5 min 12 s from character select to in world, on the
  fourth and last `enter-world.sh` attempt, twice in a row. With a 60 s settle
  first it is one attempt.

### Leg C — the recording

One run, 2026-09-16 15:14:05 → 15:49:23, the eqemu lock held for 35 minutes 18
seconds of it. `[measured]` throughout, from `review/T492/llmbench/` and
`shared/record.log`:

- **125 of 125 steps, no abort, no hard stop.** Own actions: p1 41, p2 42,
  p3 42. Timeline length 26 min 12 s (T0 15:21:43, last step 15:47:55).
- **Drift against the timeline: at most 20 s over 26 minutes**, and zero at the
  two points checked exactly (step 20 at t+274 fired at T0+274; step 80 at
  t+968 fired at T0+968). The willeq clients and the driver stayed on the same
  clock.
- **Per player: 125 frames, 125 truth lines, a marks line for every step.** p1
  additionally has its root grabs (251 frames = 125 window + 125 root + the
  untouched login frame) and a per-step OCR of its chat crop.
- **72 `expect=match` and 48 `expect=discrepancy:B###` step-rows, all 120 with a
  paired official frame** (handoff §7 asks for ≥10 and ≥5).
- **Cross-player chat reached the observers.** Seven `/say` texts appear in a
  DIFFERENT player's log, e.g. p2's log carries
  `[CHAT] Oraclealpha (say): T492 p1 Oraclealpha greets Testcaster and Testbuddy`.
  Independently, the official client's chat OCR carries both willeq players'
  lines in 55 of its 125 crops. The actor's own belief was never the evidence.
- **Server state:** snapshot (`character_data` + `character_currency`, 90 rows)
  restored BYTE-IDENTICALLY; inventory (46 rows, 4 characters) restored
  BYTE-IDENTICALLY; 1 corpse row this run created (the GM `#kill`) deleted by
  id; 0 runtime spawn rows left (B720). Stack down, lock released.
- `[measured]` Both willeq clients are the same 90,945,344-byte binary size,
  different md5 (build path and build id), and both postdate the last commit
  that touched `src`/`include` (`566ac88a`, 2026-09-14 10:36; the dev host's
  binary is from 19:20 the same day, hermes-ext's was built in Leg 0 at the
  tip). `[assumed]` that this means the same code: the sizes match and nothing
  has landed since, but no reproducible-build check was run.
- `[measured]` Log volume at `-d 4 --log-level=DEBUG --log-module=GRAPHICS:INFO`:
  15 MB (p2) and 11 MB (p3) for 28 minutes. Corpus total 333 MB.
- `[assumed]` The GM commands did what they say (`#wc`, `#npctypespawn 77002`,
  `#kill`, `#summonitem`, `#movechar`, `#zone`): each was typed and marked, and
  `truth.tsv` records what was COMMANDED. Whether each took effect is a
  question for the frames, and no model or executor looked at one.

### Leg D — acceptance, labels, hand-off

`scripts/llmbench/check_corpus.py` on `review/T492/llmbench/`: **12 pass,
0 fail** (the full output is in the corpus README). The twelfth line is not in
handoff §7 — see Leg B.

- Corpus: `/mnt/projects/fable/review/T492/llmbench/` (333 MB)
- Corpus README: `/mnt/projects/fable/review/T492/llmbench/README.md`
- Unit README: `/mnt/projects/fable/review/T492/README.md`
- Sheets: `/mnt/projects/fable/review/T492/sheets/` — 16 sheets, 80 labelled
  step pairs (8 match, 8 discrepancy across B721/B722/B725/B781/B782)
- Dry corpora: `llmbench-dry-run2-desynced/` (+ `sheets-dry/`) and
  `llmbench-dry-run3-desync-abort/`
- Board: **B784–B788 added**; nothing struck.

**`expect=` labels, and what they claim.** `match` is the First Person camera
and only that, because T466/T471 is the only unit that fitted a camera pair
against the oracle (the official client's untouched login camera). It means the
same fitted camera, zone and heading — **not** a pixel pair: the three players
stand 10 units apart, because a three-player corpus needs them to see each
other, and the corpus README says so where a grader will read it.
`discrepancy:B###` steps carry the board number and nothing else was consulted.
Everything else is `unlabelled`. **No model judged a frame at any point, and
neither did this executor.**

### What is owed

- `[assumed]` Will has not seen the sheets. The corpus is recorded, not blessed.
- **B784 is a live defect in `scripts/scenario.sh`, not just in this unit's
  rig** — every live unit that restores through it is exposed. T492 fixed its
  own copy and left `scenario.sh` alone, being outside this unit's scope.
