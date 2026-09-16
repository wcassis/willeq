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

### Leg E — Will's ruling on the sheets: the camera and held-item steps re-recorded

**The `/camera` command was never broken.** `[measured]`, from the Leg C
corpus's own logs: `/camera firstperson` was queued at **15:21:56.500**, the
renderer applied it at **15:21:56.593** (`Camera mode: First Person Camera`,
`renderer_camera.cpp:534`), and `/camera get` two minutes later read back
`mode="First Person Camera" view=1` with `node camPos=(428.001,-84.049,5.565)`
against `player=(428.000,-84.000,2.895)` — the camera is at the player's eye.
`Camera mode:` appears **15 times** in each willeq run.log; the report that it
appears none was a false negative. `camera_command.cpp`'s `Kind::Error` path
was never taken: there is no error line in either log.

What was wrong is the **settle**, and it explains the frames completely: the
`/screenshot` was queued 17 ms after the command and the renderer executed it at
**15:21:56.517 — 76 ms BEFORE the camera moved**. Both are registry-3 commands
on the same async bridge, so the screenshot grabbed the framebuffer of the frame
rendered under the previous camera. `[assumed]` that the same 17 ms race
explains every other camera and held-item frame in Leg C; the timestamps have
the same shape throughout, but only the First Person step was measured to the
millisecond.

The fix, and only this:

- `[measured]` `compile_timeline.py --settle N` puts `wait N` between a step's
  command and its `/screenshot`, and a `/camera get` in the gap on camera steps
  so the client's own `[CAMERA-GET] mode="…"` reply lands in its log between the
  two. `record.sh --settle N` gives the official client the same wait before its
  grab. The settle is subtracted from the gap to the next step, so the schedule
  does not slip one settle per step (76 x 3 s would have been nearly four
  minutes by the end of the segment); verified offline: max |script time −
  t_offset − lead| = **0.0 s** over all 76 rows.
- `[measured]` **All 33 camera/held frames per willeq player are preceded by a
  client camera-confirmation line** (p2 median gap 2.999 s, closest 0.001 s;
  p3 median 0.010 s, closest 0.000 s). The First Person frame follows its
  confirmation by +11.07 s (p2) and +19.15 s (p3).
- `[measured]` The change is visible in the pixels, by T310's gradient metric
  (`scripts/camera_match_score.py`, 0 = identical edges, no frame judged):
  First Person vs Chase scores **0.864** within the Leg E frames and **0.635**
  within the Leg C frames of the same two steps. Each comparison is internally
  consistent (Leg E has the UI off on both sides, Leg C has it on).
- `[measured]` `/hideui` (registry 3, T301 Leg D) is the only lever that removes
  the willeq debug overlay, and it is all-or-nothing — it takes the chat window
  with it. Used on p2/p3 for these steps; p1's UI is left ON, so in these 69
  steps the willeq frames have no UI and the official frames do. Boarded as
  **B791**; said in both READMEs.

**The run.** 2026-09-16 16:23:58 → 16:45:30, lock held 21 min 32 s, **76 of 76
steps**, no abort. Drift zero at the two points checked exactly (step 25 at
t+221 and step 50 at t+445 both fired at T0+their offset). Snapshot and
inventory **restored byte-identically**, 0 spawn rows and 0 corpse rows left,
stack down, lock released.

**The merge.** `scripts/llmbench/merge_segment.py` replaced 69 of the corpus's
125 steps: 276 frames moved to `review/T492/llmbench-v1-camera-steps-bad/`
(with the pre-merge `truth`/`marks`/`chat` files), 276 copied in, truth rows
swapped keeping the CORPUS step id and gaining `rerecorded=`, marks and chat
merged so the timestamp join still holds. 56 Leg C truth lines kept + 69 swapped
= 125 per player. `check_corpus.py` after the merge: **12 pass, 0 fail**.
Sheets regenerated: 16 sheets, 80 labelled step pairs.

**Will's two product rulings, boarded and NOT investigated here.** `[measured]`
colours, from single crops of named frames, which is what separates them from
the existing entries:

- **B789** (A) the sky renders red: mean **(80, 29, 29)** of 255 over a sky-only
  band (`600x60+200+10`) of
  `players/p2/frames/15:38:13.227_F_sky_p2_overhead_window.png` — red is 2.75x
  both other channels. **Not B781**, whose measured zenith is (80.2, 117.3,
  163.6), a blue that is merely the wrong blue, and whose channel-order half
  (B726) is already fixed in this binary.
- **B790** (A) untextured pink/magenta trees and distant blocks: mean
  **(88, 35, 38)** over the horizon band (`800x120+120+200`) of
  `players/p2/frames/15:46:54.538_H_cam_p2_qeytoqrg_chase_window.png` against
  the paired official frame's **(16, 30, 23)**. **Not B782**, which is "the
  distant trees are PALE" — fable (138, 153, 160) vs official (47, 63, 54), a
  desaturated grey-blue in a different zone.

`[assumed]` throughout Leg E: that these two are single defects rather than
several, and that a crop mean describes the region — Will ruled them bugs from
the frames, and this executor did not look at one.

**Still owed:** Will has not seen the re-generated sheets. The corpus is
recorded, not blessed.

### Leg F — the official F9 cycle, measured; the re-record attempt, failed

**Step 1 succeeded and is the deliverable. Step 2 failed and nothing from it was
merged; the corpus is exactly as Will's relabelling left it, 12 pass / 0 fail.**

#### Step 1 — the F9 cycle `[measured]`

Oraclealpha, qeynos2, pinned at T466/T478's spot (−74, 428, 3.751, h160) with
`camera_match_pose.sh` (zone bounced, pose read back), `#set time 11 0`,
`#set weather 0`, UI off (F10), 1024x768 ROOT grabs so no geometry mapping is
needed. Nine F9 presses from the untouched login camera, each followed by the
Leg E 3 s settle, scored against T466's six direct-mode stills with T310's
gradient metric, crop `1000x480+12+40`. **Nobody looked at a frame.**

| press | camera | score | next best |
|---|---|---|---|
| 0 | **First Person** | 0.062 | 0.784 |
| 1 | **Overhead** | 0.133 | 0.948 |
| 2 | **Chase** | 0.144 | 0.954 |
| 3 | **User 1** | 0.138 | 0.929 |
| 4 | **User 2** | 0.140 | 0.930 |
| 5 | **Tether** | 0.191 | 0.935 |
| 6 | **First Person** | 0.058 | 0.792 |
| 7 | **Overhead** | 0.133 | 0.948 |
| 8 | **Chase** | 0.109 | 0.796 |
| 9 | **User 1** | 0.079 | 0.918 |

**Period 6, and exactly T466's successor matrix:** First Person → Overhead →
Chase → User 1 → User 2 → Tether → First Person. Every grab identified, margin
≥ 0.59 in every row. **No seventh preset**: nothing scored above 0.30 against
all six, so nothing was named `unnamed_N`. The hermes-ext "period ~7" note is
**refuted at this spot** — it was an unmeasured impression at that VM's
different geometry with no reference stills to score against.

Method validated offline BEFORE the lock was taken: the six references score
0.000 against themselves and **0.769–1.078** against each other, so the 0.30
threshold sits far below the nearest wrong answer.

`[measured]` **So the mapping was never the bug.** The recorder drove the
official camera **open loop** — pressed F9 and wrote down what the count
implied. One dropped keypress shifts the rest of the cycle by one, which is
precisely the symptom Will found: the official client one step behind, in
Overhead where the truth said Chase. Output: `review/T492/f9_cycle/`
(`table.md`, `scores.tsv`, ten frames, contact sheet).

#### Step 2 — attempted and DISCARDED `[measured]`

`o.sh camto <camera>` was added to close the loop (press F9 until a grab scores
as the named camera, reject otherwise), plus `o.sh target` to read the official
target window — whose real location was MEASURED at window crop
`140x24+885+470` on `players/p1/frames/15:27:43.497_C_target_p1_p2_window.png`,
where it OCRs as `Testcaster`. (Will was right that the box he was shown is the
player's own window; the target window is a different one, further right.)

The run completed 32 of 32 steps and restored cleanly, but **every `camto`
failed** — 0.907, 1.020, 0.998, 1.025, 1.000, 1.021 against the named camera —
and `o.sh target` returned the fragment `es` on all six targeting steps. Its
frames were therefore **not merged**.

The cause, measured after the fact: `record.sh` does not set the conditions
T466's reference stills were taken in. `f9_cycle.sh` pins the pose with
`camera_match_pose.sh` and issues `#set time 11 0` / `#set weather 0`, and
identified all ten grabs at 0.058–0.191; the Leg F run did neither, and
re-scoring its frames offline gives **0.448–0.558** with the WRONG camera best
(User 1 where First Person was wanted) under two different UI-free crops — so
this is the scene conditions, not the UI and not the crop. Boarded as **B792**;
`record.sh --oracle-conditions` now issues both commands.

`[assumed]` that setting those two conditions is sufficient to bring `camto`
back to f9_cycle's 0.058–0.191 in a full three-client run; that has been
measured only in `f9_cycle.sh`'s own single-client run, not in `record.sh`.
`[assumed]` why `o.sh target` read `es` — the crop is measured on a Leg C frame
and unvalidated under Leg F's conditions; it was not re-measured.

**Owed:** the B721/B722/B725 steps are still parked at `expect=unlabelled` with
`pending=B7xx`, and the target steps at `expect=unlabelled;graded_from=log`.
Restoring them needs one more ~20 min lock session with `--oracle-conditions`
and a re-measured target crop. `restore_pending.py` is written and ready.

### Leg G — the gate held, and the labels were checked by machine instead of by Will

**Nothing was recorded.** The gate refused, which is what it is for. The corpus
is unchanged except that every labelled pair now carries the measured verdict.

#### The gate `[measured]`

`record.sh --gate "Chase,Testcaster"` runs once, with all three clients in
world — the condition that matters, because the other two players stand 10
units either side of Oraclealpha and are IN the official client's frame, which
T466's reference stills are not. It failed and stopped the run before a single
step was recorded (`review/T492/llmbench-legG/shared/gate.tsv`):

| crop | First Person | Overhead | **Chase** | User 1 | User 2 | Tether |
|---|---|---|---|---|---|---|
| `1000x480+12+40` | 0.874 | 1.013 | **0.495** | 0.967 | 0.994 | 0.998 |
| `1000x380+12+40` | 0.852 | 1.033 | **0.469** | 0.963 | 0.992 | 0.993 |
| `900x300+60+60`  | 0.862 | 1.006 | **0.415** | 0.971 | 0.978 | 0.998 |

Read it carefully: **`camto` put the client in Chase and the scoring says so** —
Chase is best under all three crops by a factor of 1.7–2.3 over every
alternative. What failed is the **absolute 0.30 threshold**, which was
validated in Leg F step 1 under single-client conditions (0.058–0.191) and does
not transfer to a frame with two other players in it. `[assumed]` that the two
extra players are the whole of the 0.415→0.062 difference; the pose is also
placed by SQL here rather than pinned with `camera_match_pose.sh`, and the two
were not separated.

The fix is **not** to raise the threshold until it passes — that is choosing the
answer. It is a calibration: put the official client in each of the six cameras
with all three clients in world and derive the threshold (or a best/next-best
margin criterion) from that. That is a new measurement and the instruction was
to stop before making one. **Owed, and the blocker for the parked labels.**

#### `verify_labels.py` — every labelled pair, checked mechanically `[measured]`

Will: *"there was no reason to have me evaluate those images. You could have
found all of the same issues trivially. Worst case, OCR of ui windows."* He is
right. `scripts/llmbench/verify_labels.py` checks each claim against something
measurable — `camera=` by scoring the official frame against T466's still for
that camera plus the willeq client's own `Camera mode:` line inside the settle
window; `target=` by OCR of the official target window plus the willeq log;
`held=` against the inventory snapshot the run started from plus any refusal the
client logged. Three-way verdict, because **contradicted** and **could not be
checked** are different answers.

**20 labelled pairs: 0 PASS, 10 FAIL, 10 INCONCLUSIVE.** Full table:
`review/T492/llmbench/verify_labels.tsv`.

The 10 FAILs are all pairs already parked at `pending=B7xx`, and they are
contradicted three separate ways:

1. `[measured]` **The official client is in Overhead on every one of them** —
   Overhead 0.766–0.782 against the claimed Chase/Tether at 0.971–1.000. This
   reproduces Will's finding by measurement, with nobody looking at a frame.
2. `[measured]` **The `held=` values were wrong from the start.** The Leg C
   timeline hardcoded them from a misread of the inventory dump:
   `E_heldA_p2_*` claims `slot13=9998, slot14=9979` while Testcaster's DB rows
   are **14337** and **20656**. (Testbuddy's `13=9997,14=9979` does match, and
   the check says so — it discriminates.)
3. `[measured]` **`/equip 23 13` was REFUSED**: the client answered
   *"Bread Cakes* cannot go in Primary (13) — wrong slot type for this item"*,
   so every `held=13=9990` claim on the `E_heldB_*` steps is false. Slot 23 is
   food, not the weapon the timeline assumed.

The 10 INCONCLUSIVE are every currently-graded pair (the 4 `match` labels and
the B781/B782 ones): the claimed camera IS the best match in each, at
0.545–0.959, above the 0.30 threshold — the same B792 calibration gap the gate
hit. They are annotated `unverified=` and their labels are left alone: demoting
them would take the corpus below the handoff's ≥10 match / ≥5 discrepancy and
that is a decision for the orchestrator, not a side effect of a threshold that
is known to be mis-calibrated.

`apply_verdicts.py` wrote `why=` onto the 10 contradicted rows and
`unverified=` onto the 10 unchecked ones — 24 truth rows annotated. No label was
promoted: `restore_pending.py` and the target-step restore run only on pairs
that PASS, and none did. `check_corpus.py` is unchanged at **12 pass, 0 fail**.
Sheets were not regenerated: no frame changed.

**Owed, in order:** (1) calibrate the camera threshold under three-client
conditions (B792); (2) fix the `held=` values in `make_timeline.py` against the
actual inventory and pick a real weapon for the `/equip` step; (3) re-record the
B721/B722/B725 and target steps behind the gate; (4) then `restore_pending.py`.

### Leg H — the margin criterion, validated and applied; held= read from the server

#### The criterion, validated before it touched the corpus `[measured]`

Camera identity is now: the claimed camera is the **best of the six** AND
**next-best / best ≥ 1.5**; a different camera best is FAIL, best-but-unseparated
is INCONCLUSIVE. Validated on data already in hand, both validations required to
pass before the criterion was used anywhere:

- `review/T492/f9_cycle/scores.tsv` (one client, empty scene): **all ten rows
  PASS with their measured camera**, ratios 4.90–13.58, **minimum 4.90**
  (press 5, Tether).
- `review/T492/llmbench-legG/shared/gate.tsv` (three clients in world): **Chase
  PASSes on all three crops**, ratios **1.77 / 1.82 / 2.08**.

So 1.5 sits below every real case, with the tightest at 1.77, and the old
absolute 0.30 sat above a correct identification. The 1.5 was not tuned: both
validations passed first time.

#### `verify_labels.py` under the new criterion `[measured]`

**20 pairs: 5 PASS, 5 INCONCLUSIVE, 10 FAIL** (was 0 / 10 / 10).

| verdict | pairs |
|---|---|
| PASS | `A_cam_p2_firstperson`, `A_camget_p2`, `A_cam_p3_firstperson`, `A_camget_p3` (FirstPerson), `D_cam_p3_chase` (Chase) |
| INCONCLUSIVE | `D_cam_p2_chase` ratio **1.48**, `F_sky_p2_overhead` 1.24, `F_sky_p3_overhead` 1.25, `H_cam_p2_qeytoqrg_chase` 1.03, `H_cam_p3_qeytoqrg_chase` 1.03 |
| FAIL | the eight `E_held*` pairs and `G_pre_p2_chase` / `G_pre_p3_chase` — official client in **Overhead** (0.766–0.782) against the claimed camera at 0.971–1.000 |

`D_cam_p2_chase` at **1.48** misses by 0.02 while its p3 twin passes. It stays
parked: moving the threshold to catch it would be choosing the answer, which is
the thing this criterion exists to stop.

`restore_pending.py` now refuses to promote anything the verifier did not pass,
and promoted exactly one label (`D_cam_p3_chase` → `discrepancy:B721`, three
rows). `apply_verdicts.py` was made idempotent — a verdict can change, so a
stale annotation is stripped before the current one lands — and rewrote 72 rows.
**`check_corpus.py`: 12 pass, 0 fail, now 12 match and 15 discrepancy step-rows.**

#### B793 — `held=` now comes from the server `[measured]`

`make_timeline_legH.py` ASKS the `inventory` table joined to `items` at compile
time instead of hardcoding. What is actually there:

- Testcaster (67): `13=14337 Blue Crystal Staff (IT122)`, `14=20656 Eye of Innoruuk (IT72)`
- Testbuddy (68): `13=9997 Dagger* (IT5)`, `14=9979 Gloomingdeep Lantern (IT48)`

— against Leg C's claim of `13=9998, 14=9979` for Testcaster, which was simply
false. And **no general slot of either character holds a weapon**: the spares
are food, drink, spells, bandages and (Testcaster) a Shuriken, which is ammo.
That is why `/equip 23 13` was refused. `record.sh --seed-weapon` now seeds
item 9997 (Dagger*, a one-hand weapon) into slot 21 **after** the inventory
snapshot, so the restore removes it again — verified: inventory restored
BYTE-IDENTICALLY with the seed gone. `harvest.py` now marks a client refusal
line (`cannot go in`, `wrong slot type`, `not recognized`) as `REFUSED` in
marks.tsv rather than letting the step read as done.

#### The gates `[measured]`

Camera gate **PASSED** — and this is the criterion working live, in the
condition that defeated the old one: `camto Chase OK presses=2 score=0.4433
next=0.8836 ratio=1.993`.

Target gate **FAILED**, so the run stopped before recording anything. The four
candidate crops read `es`, `a`, `a` and **`ow Another Tip`** — the official
client's **Tip of the Day dialog** is sitting over the region the target window
uses. The same crop reads `Testcaster` cleanly on a Leg C frame, so it is the
dialog and not the crop. Boarded **B795**; it also means any official frame
taken soon after entering the world may have the dialog over it, and earlier
units' post-login grabs deserve a re-check. `camto` is unaffected — it passed at
ratio 1.993 with the dialog in frame.

`[assumed]` that dismissing the dialog in `oracle_start.sh` is sufficient to
make the target gate pass; that has not been run.

**Owed:** dismiss the Tip of the Day dialog, then one more gated session to
re-record the B721/B722/B725 and target steps. Everything else for that session
is now in place and validated.

### Leg I — both gates passed, the steps were re-recorded, and 18 of 24 pairs now verify

#### B795 — the Tip of the Day dialog, fixed both ways `[measured]`

The dialog's own `[TipWindow]` geometry in the character UI ini is
**(664, 234) 360x300**, which is exactly where the target window sits — that is
why Leg H's target OCR read `ow Another Tip`.

- **Persistent:** `[Tips] ShowTips=0` in
  `~/eq-oracle/client/UI_Oraclealpha_WillEQ Private Server.ini`. Oraclealpha's
  ini had **no `[Tips]` section at all**, so tips defaulted on; other characters
  shipped in the same client copy already carry `ShowTips=0`, which is where the
  key came from. The pre-change file is kept beside it as `.t492-pristine`.
- **Runtime:** `oracle_start.sh` now OCRs that exact region after entering the
  world and refuses to proceed until it is clear, dismissing with Escape then a
  click on the panel's close box, up to four attempts, and failing loudly
  otherwise. `[measured]` this run: *"tip dialog PRESENT on probe 1 (OCR: PAY
  Check back with your guild trainers periadically…) -- dismissing"* then *"tip
  dialog: region clear on probe 2"*. So the ini key alone did not stop it on the
  launch that wrote it, and the runtime check is what actually cleared the way.

#### The gates `[measured]`

| gate | result |
|---|---|
| camera | **PASS** — `camto Chase OK presses=2 score=0.3316 next=0.8439 ratio=2.545` |
| target | **PASS** — `crop=140x24+885+470 read=Testcaster` |

Then 30 of 30 steps recorded, with both in-run `camto` calls verified
(Chase 2.545, First Person `presses=4 score=0.3968 next=0.8572 ratio=2.160`) and
the official target window reading real names throughout.

#### `verify_labels.py` `[measured]`

**24 pairs: 18 PASS, 4 INCONCLUSIVE, 2 FAIL** (Leg G was 0 / 10 / 10).

- **INCONCLUSIVE (4):** `F_sky_p2/p3_overhead` (ratio 1.24 / 1.25) and
  `H_cam_p2/p3_qeytoqrg_chase` (1.03). Not re-recorded this leg; they stay
  parked with `unverified=`.
- **FAIL (2):** `E_heldB_p2_chase` / `E_heldB_p2_tether`, and the reason is a
  new defect — see below. Parked with `why=`.

Three verifier defects were found and fixed on the way, all of which had been
producing wrong verdicts:

1. `merge_segment.py` copied every segment's log to the fixed name
   `run_legE.log`, so Leg I's merge **overwrote Leg E's log** — the file holding
   the `Camera mode:` evidence for the steps Leg E recorded. Restored from the
   segment directories and the name is now per-segment.
2. The target crops are WINDOW coordinates and the verifier was OCR'ing the
   ROOT grab, which is the same scene offset by (−1, +30): it read `pO`.
3. The official client's target window shows the OFFICIAL client's target, so on
   a step where a *willeq* player targeted someone it says nothing about the
   claim — it had failed `C_target_p2_p3` because the official window read
   `Testcaster` while p2 had targeted Testbuddy. The official OCR now decides
   only when the actor is the official client; otherwise the willeq log decides
   and the OCR is recorded as context.

`restore_pending.py` promoted only the 18 passing labels (14 rows per player).
**`check_corpus.py`: 12 pass, 0 fail — 24 match and 42 discrepancy step-rows,
every one of them machine-verified.** Sheets regenerated.

#### B796 — `/equip`'s slot numbers are off by one `[measured]`

The two `E_heldB_p2_*` failures are a real client defect, isolated by a
controlled experiment rather than argued: item 9997 was seeded into DB slot
**21** for BOTH willeq characters and each was told `/equip 21 13`. Testbuddy,
with nothing at DB slot 22, equipped the dagger and the pair verifies.
Testcaster, who has a Shuriken at DB slot **22**, got *"] Shuriken cannot go in
Primary (13)"*. The characters differ only in whether slot 22 is occupied, so
the command resolved `21` to the row at DB slot 22. Boarded; not fixed here.

`[assumed]` that the off-by-one is uniform across general slots rather than a
boundary case at 21/22 — only the one pair of slots was exercised.

### What is owed

- `[assumed]` Will has not seen the sheets. The corpus is recorded, not blessed.
- **B784 is a live defect in `scripts/scenario.sh`, not just in this unit's
  rig** — every live unit that restores through it is exposed. T492 fixed its
  own copy and left `scenario.sh` alone, being outside this unit's scope.
