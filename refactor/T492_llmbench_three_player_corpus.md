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
(filled by the executor)
