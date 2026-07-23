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

(executor fills in: seeding path chosen and why, tour mechanism, recording
stats, oracle results, every DB/config change made, deviations)
