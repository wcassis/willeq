# T370 — the zone tour under TSAN (B064)

Class B, slot1, eqemu lock (atomic; must be ABSENT — if present read it
and STOP). Measurement first; fix only what TSAN names. Read the B064
board text and `review/T352/README.md` (the TSAN build flags, the
two-thread driver, the positive control, and why the T121 tour was NOT
run there — it needs the server, which you now have).

1. Build the TSAN tree from T352's recipe (`review/T352/build_and_run.sh`
   shows the flags; T121's tour script is named in T352's Review). Bring
   the server up.
2. Run the T121 zone tour (login, six `#zone` commands) under TSAN with
   the EntityPrepWorker live — the run T352 said would confirm or clear
   B064's four containers (`globalTextures_`, `numberedGlobalTextures_`,
   `armorTextureCache_`, `zoneTextures_`) read in `getMergedTextures`
   while other loader paths mutate them. Three runs. Save every TSAN
   report verbatim in `review/T370/`.
3. For each distinct report: both stacks, the container, the two threads,
   and whether the T352 positive control still fires in this build (so a
   clean run is a finding and not a silent harness).
4. Fix: only what a report names, with the narrowest change that is
   deadlock-free by the same argument T352 made (no new lock ordering
   edge; the 12 call sites; nothing inside acquires a loader mutex). If a
   report needs the restructure B064 anticipates (the texture containers
   own by a different thread than the chr cache), STOP with the reports
   and the shape — do not add a lock that widens `otherChrCacheMutex_`
   over unrelated containers by argument.
5. Post-fix: three clean tours; the positive control still fires.
6. Strike B064 (or annotate it with the measured reports and the STOP).

## Proof

The TSAN runs are the proof; plus build, `ctest -j6`, visual gate
standalone + ARM if `src/client/graphics` changes, file sizes. Restore
server values; `down.sh`; delete the lock.

STOP: the TSAN tree no longer builds (report the first error — do not
fix the flags by guesswork); a report needs the restructure.
