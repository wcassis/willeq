# T79 — Four small defects that have each cost multiple units a workaround

**Slot: t3. x86 only. No Pi. NO EQEMU.**

**Territory: `scripts/refactor/check_file_sizes.sh`,
`tools/model_viewer_spell_bar.h`, `tests/CMakeLists.txt` (test-discovery
config only), `src/client/graphics/renderer_zone_merge.h` /
wherever `protectedRegions_` lives, and tests.**

Do not touch the first-person camera path (T76 owns it), `eq_commands.cpp`
(T77 just landed), or the spell reader. `refactor/deferred.md` is
append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the board entries named per
part below.

**Four independent parts. Do them in order, commit-ready at each
boundary. If one stalls, land the others and report the stall** — none of
these depends on another.

---

## Part 1 — `check_file_sizes.sh` has bitten four units

Board: `## check_file_sizes.sh has no handling for deleted files in the
diff (T67 finding, 2026-08-11)`.

Two separate defects, both confirmed by units working around them:

- **No working-directory anchor.** The script resolves paths against the
  ambient cwd, so running it as the protocol instructs
  (`bash <slot>/scripts/refactor/check_file_sizes.sh fable`) mis-resolves.
  T67, T69, T73 and T75 each independently worked around it — with
  `bash -c 'cd … && …'`, `env -C`, or `--files` with absolute paths.
  **Add a `-C`/`--repo <path>` option, or anchor to the script's own
  location**, so it works when invoked the way the protocol documents.
- **Deleted files in the diff.** A file removed in the branch appears in
  the change list and the script tries to measure it. T67 hit this
  removing `constrained_mesh_cache.{h,cpp}`. Skip deleted paths.

**Verify by running it the way the protocol tells executors to run it**,
from a slot on a task branch, and show it works without a wrapper. This
script is the file-size gate; a gate everyone works around is not a gate.

## Part 2 — `model_viewer_spell_bar.h`'s `spellsnew.eff` record size

Board: recorded by T70's entry.

`tools/model_viewer_spell_bar.h:274` has `const size_t ENTRY_SIZE = 256;`
for `spellsnew.eff`. The correct value is **268**: the file is 112,024
bytes and `112024 / 268 = 418` exactly, while `/256` gives 437.59 and
drifts.

That drift is why the surrounding code carries hacks like *"Skip entries
that look like fragments (common issue with this format)"* — the
"fragments" (`"ct 2"`, `"pell Effect"`, `"l Effect"`) are pure artifacts
of reading 12 bytes short per record.

Fix the constant. **Then remove the fragment-skipping workarounds it
motivated** and confirm the parser produces clean, whole entry names —
leaving them in place would hide a future regression the same way they hid
this one. Report the entry count before and after.

Note the companion EDD parser at `:129-135` is **already correct**
(416-byte entries, header handled) — do not "fix" it.

## Part 3 — `protectedRegions_` is permanently all-false

Board: `## protectedRegions_ bitvector is permanently all-false in
production (T67 side-finding, 2026-08-11)`.

Its only writer was gated on `ConstrainedMeshCache`, which T67 removed. It
is now a bitvector that is allocated, passed around, and always false.

**Establish what reads it before deciding.** If every consumer is now
dead, remove it with them. If something live still consults it, then it is
silently answering "nothing is protected" to a question that used to have
a real answer — say what that changes, and whether the consumer needs a
different signal or should also go.

Rendering must be byte-identical either way: the visual gate is the proof.

## Part 4 — `gtest_discover_tests` timeout under load

Not yet on the board; add it.

`cmake --build` failed three times today with
`GoogleTestAddTests.cmake: Error running test executable` while
discovering tests — `test_spell_manager` and `test_graphics` — each time
under parallel load, each time clean on retry. It is a **discovery-time
timeout, not a compile error**, and it makes builds spuriously red.

Raise the discovery timeout (CMake's `DISCOVERY_TIMEOUT` on
`gtest_discover_tests`), or move the affected targets to `PRE_TEST`
discovery mode so enumeration happens at test time rather than build time.
Pick one, say why, and note the trade-off.

A spuriously-failing build teaches people to re-run without reading, which
is how a real failure gets missed.

---

## STOP conditions

- Part 3's consumers turning out to need a real protection signal — report
  rather than inventing one.
- Any part requiring changes outside this unit's territory.
- Goldens or the visual gate moving. Parts 1, 2 and 4 cannot affect
  rendering at all; Part 3 must not.

## Verification

Full protocol bar. Additionally, per part: Part 1 run the documented way
without a wrapper; Part 2's before/after entry counts with the workarounds
removed; Part 3's consumer census plus visual gate; Part 4 demonstrated
under parallel load if you can provoke it, and stated honestly if you
cannot reproduce on demand.

## Review

All four parts landed. No STOP conditions hit; Part 3's finding (live
consumers) is reported per the plan's own instruction, not treated as a
blocker to the rest of the unit.

### Part 1 — `check_file_sizes.sh` — FIXED

Two independent defects, both fixed in one edit to
`scripts/refactor/check_file_sizes.sh`:
- **cwd anchor**: `repo_root` now defaults to the script's own on-disk
  location (`<repo>/scripts/refactor/check_file_sizes.sh`'s grandparent's
  parent), with an explicit `-C`/`--repo <path>` override. No longer
  depends on `git rev-parse --show-toplevel` against the ambient cwd.
- **deleted files**: the diff is now taken with `git diff --diff-filter=d`,
  which excludes deletions before the per-file loop ever sees them.

**No-wrapper invocation proof** (ambient shell cwd was `/mnt/projects/fable`,
OUTSIDE the slot, exactly the scenario every prior workaround was needed
for):
```
$ pwd
/mnt/projects/fable
$ bash /mnt/projects/fable/wt/slot3/scripts/refactor/check_file_sizes.sh fable
checking 2 file(s) (files changed vs 'fable' in /mnt/projects/fable/wt/slot3)
ok    src/client/graphics/renderer_zone_stream.cpp: 926 lines
WARN  tools/model_viewer_spell_bar.h: 1965 lines (> 1500)
checked 2 file(s): 1 ok, 1 warn, 0 fail
```
`repo_root` correctly resolved to the slot (not `/mnt/projects/fable` or
anything else the ambient cwd could have suggested), and it checked the
slot's real diff against `fable`. The WARN on `model_viewer_spell_bar.h` is
pre-existing (1963 lines at the `fable` tip, before this unit's edit) — not
a new condition, confirmed via `git show fable:tools/model_viewer_spell_bar.h
| wc -l`.

Deleted-file handling verified directly: temporarily `rm`'d a real tracked
header (`docker/irrlicht-drm-x86/CIrrDeviceFB.h`, uncommitted), ran the
script, confirmed it reported "no C++ files to check" (the deletion was
silently excluded, not reported as an ERROR/"file not found" the way it
would have been before this fix), then restored the file with
`git checkout --`.

### Part 2 — `spellsnew.eff` `ENTRY_SIZE` — FIXED

`tools/model_viewer_spell_bar.h:274` (now :279 after comment expansion)
changed from `256` to `268`. The fragment-skipping workaround at the old
line 294-297 (`if (name.find("Effect") != npos && name.length() < 10)
continue;`) was removed.

Entry counts, measured against the real `/mnt/projects/EverQuestP1999/
spellsnew.eff` (112,024 bytes) with a standalone probe replicating the
exact name-extraction logic (built and run outside the repo, in the
scratchpad, not committed):

| | ENTRY_SIZE | records | kept | notes |
|---|---|---|---|---|
| Before | 256 | 437 | 22 | overwhelmingly garbled fragments: `"P\xef\xbf\xbd"`, `"keLight85."`, `"106 V2"`, `"jjklhefga^^[XZ^``a`bi"` |
| After  | 268 | 418 | 416 | clean whole names throughout: `"Clinging Darkness"`, `"Frost Breath"`, `"Rain_Fall"`, `"Diseased Cell"`, `"Spitting Rock"` |

418 matches `112024 / 268` exactly. With `ENTRY_SIZE=268`, re-applying the
old fragment-skip predicate as a diagnostic (not shipped) skips **0 of 418**
records — confirming the hack was pure compensation for the 12-byte drift,
not a real data-quality filter, so removing it is safe. The 2 records
filtered by the pre-existing "too short / == None" check at 268 appear to
be legitimate empty/placeholder entries, not drift artifacts.

The companion EDD parser (`:129-135` before this edit) was not touched, per
the plan.

### Part 3 — `protectedRegions_` — CENSUS DONE, left in place, decision deferred

Full consumer census (declaration, writer, all 5 readers) done via
sub-agent search and manually spot-verified by reading
`renderer_zone_stream.cpp` and `irrlicht_renderer.h` directly. Summary:

- **Declaration**: `include/client/graphics/irrlicht_renderer.h:1502`,
  `std::vector<bool> protectedRegions_`.
- **Writers**: only `.resize(n, false)` (`renderer_zone_load.cpp:398`) and
  `.clear()` (`renderer_zone_lifecycle.cpp:177`) remain — no writer ever
  sets an element `true` anywhere in the tree (T67 removed the only one,
  which was itself dead — gated on a `ConstrainedMeshCache` pointer never
  constructed in any shipped build).
- **Readers — all 5 are live, reachable production code, not test-only**,
  all in `src/client/graphics/renderer_zone_stream.cpp`:
  - `:494` and `:755` — `doorManager_->getDoorsInRegions(protectedRegions_, ...)`
    (Priority 3 progressive door build; progressive-load status counter).
  - `:537`, `:547`, `:765` — direct `protectedRegions_[idx]` reads
    (Priority 4 progressive object build; same status counter for objects).
  - Because the vector is always empty/false, **every one of these is a
    permanent no-op**: Priority 3/4 loop bodies never execute, and the
    `unbuiltDoors`/`unbuiltObjects` status counters are permanently pinned
    to 0 regardless of real pending state.
- **A distinct, same-named sibling exists** and is NOT the same thing:
  `SimulationWorker::Results::protectedRegions` (`std::vector<size_t>`,
  `simulation_worker.h:430`) is still actively computed
  (`sim_visibility.cpp`) but only feeds a debug log line
  (`renderer_sim_bridge.cpp:634`) — it was never wired back into the
  bitvector (that copy was the writer T67 removed). Flagged for
  completeness; out of scope to touch.

**Decision: left in place, not removed.** The plan's own framing draws the
line exactly here — "if something live still consults it... say what that
changes... whether the consumer needs a different signal or should also
go" is a decision this unit reports on, not one it makes unilaterally, and
the STOP condition ("Part 3's consumers turning out to need a real
protection signal — report rather than inventing one") names this precise
case. Doors/objects are also built eagerly at zone load (SEQ Step6/7), so
whether the dead Priority 3/4 paths are a real functional gap or fully
redundant-but-harmless dead code is **not established either way** — that
was T67's open question and remains open after this census. What changed:
added an inline comment at each of the 5 read sites (previously only the
declaration carried a T67 comment) stating plainly that the site is a
permanent no-op and why, so the next person to touch
`processFrameProgressiveLoad()` doesn't have to re-derive this. No
behavior changed — visual gate confirms byte-identical rendering (expected,
since this was comment-only).

### Part 4 — `gtest_discover_tests` timeout — mechanism chosen, NOT reproduced this session

Chose **raising `DISCOVERY_TIMEOUT` to 60** (from the GoogleTest module's
5s default) on the two named targets (`test_spell_manager`,
`test_graphics` in `tests/CMakeLists.txt`), not `DISCOVERY_MODE PRE_TEST`.

**Why**: `DISCOVERY_MODE` is a CMake 3.20+ keyword; this project's
top-level `CMakeLists.txt` declares `CMAKE_MINIMUM_REQUIRED(VERSION 3.14)`,
so adopting `PRE_TEST` would be quietly incompatible with any CMake in the
3.14-3.19 range the project claims to support, where `DISCOVERY_TIMEOUT`
has no such floor (it predates 3.14). `DISCOVERY_TIMEOUT` is also the
narrower, more surgical change — it doesn't alter when/how test
enumeration integrates with `ctest`.

**Trade-off, stated plainly**: this does not remove the contention
mechanism. Discovery still runs as part of `cmake --build`'s parallel
graph, competing with `-j24` compilation for CPU. It only gives more slack
(60s vs 5s) before that contention manifests as a build failure. A host
under heavier load than today's could still exceed 60s. `PRE_TEST` would
have removed the mechanism structurally (discovery deferred to `ctest`
time, after the parallel build is done) at the cost of the CMake-version
floor above, and would also change where a genuine discovery crash
surfaces (from `cmake --build` to `ctest`) — arguably fine since `ctest`
is in the same verification bar, but a real behavior change worth naming.

**Could not provoke on demand.** This session's `cmake --build -j24` (from
a fresh `rm -rf build` + reconfigure, followed immediately by a full
parallel build) completed cleanly with no discovery failure of any kind.
The fix is applied on the strength of the documented mechanism — the exact
error text (`GoogleTestAddTests.cmake: Error running test executable`)
matches CMake's own `DISCOVERY_TIMEOUT` failure path, and both named
targets are legitimately heavy (link `willeq_core`, one also links
Irrlicht) — not on a captured repro from this session. Stated honestly per
the plan's instruction rather than claimed as proven.

### Verification bar

1. `cmake --build build -j24` — clean, from a fresh `rm -rf build` +
   reconfigure (an earlier `build/` in this slot had a stale/broken cache
   pointing `EQT_GLES2_IRRLICHT_DIR` at a nonexistent
   `/mnt/projects/fable/third_party/...` path outside the slot — unrelated
   pre-existing slot state, reconfigured correctly and noted here as a
   deviation, not touched further).
2. `ctest --test-dir build --output-on-failure` — 2057/2058 passed, 1
   failed: `AudioManagerTest.PreloadCommonSounds` — the documented ALSA
   host exception, no other failures. 13 tests skipped, all pre-existing
   documented skip categories (server/DISPLAY-dependent, gems memorize
   fixtures, etc.) — none newly introduced.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `bash scripts/ci/visual_gate.sh` — `=== VISUAL GATE: PASS ===`, goldens
   untouched.
5. `bash scripts/build-arm-noble.sh` — clean, all ARM binaries produced.
6. `bash scripts/refactor/check_file_sizes.sh fable` (from ambient cwd
   `/mnt/projects/fable`, no wrapper) — see Part 1 above; 1 ok, 1
   pre-existing WARN, 0 fail.

### Deviations

- Part 3: no code removed, per the STOP condition being invoked for the
  removal question specifically (see above) — this is a deviation from
  "decide and act" toward "decide and report," but it is the plan's own
  named STOP case, not an improvisation.
- Part 4: mechanism applied without a captured repro this session (stated
  honestly above), per the plan's explicit allowance for this outcome.
- `build/`'s stale cache (pointing outside the slot) required a
  `rm -rf build` + reconfigure before verification could run; this is
  pre-existing slot state unrelated to T79's edits, not a finding about
  this unit's changes.

Board updates: `check_file_sizes.sh` entry re-titled FIXED; `protectedRegions_`
entry re-titled with census-done/left-in-place/decision-deferred and a full
write-up appended; T70's entry got a **FIXED by T79** addendum with the
before/after counts; a new entry added for the `gtest_discover_tests`
timeout with the mechanism chosen and trade-off. All in
`refactor/deferred.md`.
