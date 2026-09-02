# J02 — Batch J Wave 1: split eq.cpp (3,923 → 6 TUs, largest ~1,065)

**Slot: assigned at dispatch. Branch `task/J02-eq-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 1 of `refactor/J_file_size_reclamation.md`, running alongside **J03** and
**J04**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit** (it carries six corrections made after the J01 pilot and it
**wins over anything older**), `refactor/J01_inventory_manager_split.md`
(the proven template, and its Review), `refactor/F_file_split.md`,
`refactor/T166_split_renderer_zone_load.md` and `docs/module_map.md`.

`eq.cpp` is the largest file in the tree and Batch F already split it once
(3,629 → grew back to 3,923). This is a re-split, not a first cut.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No header changes: every `EverQuest`
declaration stays exactly where it is in `include/client/eq.h`. **No method
decomposition** — that is Batch G's shape and is out of scope, `DispatchZonePacket`
emphatically included (see §Hazards). No drive-by fixes.

## The split

Measured at `2eb34f3d`, re-confirmed byte-for-byte at `7e0c7823` after J01 merged (2026-09-02). **Re-verify every range against the tree
at your own branch tip before you cut** — `eq.cpp` grew 10 lines in the days
between the survey and this plan, and J03/J04 are landing in parallel. Report
drift in your Review rather than silently adjusting.

Preamble is **lines 1-79** (78 lines of includes plus the `M_PI` guard). There
is **no namespace** in this file — `EverQuest::` methods are defined at file
scope, so there is no namespace open/close to replicate.

Ranges below start at the **first line of the cluster's leading comment**, per
§Process. Where a cluster has no banner, they start at the first blank line
above the first statement.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 81-799 | **stays.** `s_debug_level` definition, ctor/dtor, `StartSessionRecording`, `OpenDatabase`, `InitializeSpellSystem`, `ConnectToLogin`, `TickNetwork`, loading phase + progress + status text, `OnGameStateComplete`, `OnGraphicsComplete`, `PublishFullStateSnapshot` (197), `ConnectToWorld`, `ConnectToZone`, the four `ZoneOn*` connection callbacks, `ReplayZonePacket` | **stays** `eq.cpp` | — |
| 800-1681 | `DispatchZonePacket` — **one method, 882 lines** | `eq_zone_dispatch.cpp` | **~965** |
| 1682-1953 | the `ZoneSend*` handshake family: `SessionReady`, `ZoneEntry`, `ReqClientSpawn`, `ClientReady`, `SetServerFilter`, `StreamIdentify`, `AckPacket`, `ReqNewZone`, `SendAATable`, `UpdateAA`, `SendTributes`, `RequestGuildTributes`, `SpawnAppearance`, `SendExpZonein` | `eq_zone_handshake.cpp` | ~355 |
| 1954-2933 | `ZoneProcessNewZone` (160), `ZoneProcessPlayerProfile` (467), `ZoneProcessAnimation` (295), `ZoneProcessGuildMOTD`, `CheckZoneRequestPhaseComplete` | `eq_zone_entry.cpp` | **~1,065** |
| 2934-3173 | the UCS family: `ConnectToUCS`, `UCSOnNewConnection`, `UCSOnConnectionLost`, `UCSOnPacketRecv`, `UCSSendMailLogin`, `UCSSendTell`, `UCSSendJoinChannel`, `UCSSendLeaveChannel`, `UCSProcessChatMessage`, `UCSProcessMailNew`, `UCSProcessBuddy` | `eq_ucs.cpp` | ~325 |
| 3174-3700 | the `// ====` accessor banner block (movement / stat / entity-sync / combat-sync / behavioural-flag accessors, 3174-3272) then `ZoneProcessPlayerStateAdd`, `ZoneProcessDeath` (168), `ZoneProcessPlayerStateRemove`, `ZoneProcessStamina`, `ZoneProcessZonePlayerToBind`, `ZoneProcessLevelUpdate`, `SendTradeSkillCombine`, `ZoneProcessTradeSkillCombine` | `eq_player_state.cpp` | ~610 |
| 3701-3923 | **stays.** `SafeQueueZonePacket`, `StartUpdateLoop`, `StopUpdateLoop`, the Phase 3/4 banner block, `SendPetition`, `SetSneak`, `QueuePacket`, `SetEQClientPath`, `SetConstrainedConfig` | **stays** `eq.cpp` | — |

**Result: 6 TUs** — `eq.cpp` ≈ 1,025 (79 preamble + 719 + 223),
`eq_zone_dispatch.cpp` ≈ 965, `eq_zone_entry.cpp` ≈ 1,065,
`eq_player_state.cpp` ≈ 610, `eq_zone_handshake.cpp` ≈ 355,
`eq_ucs.cpp` ≈ 325. Every one under 1,500.

## Hazards

Three, all verified directly against the tree — not taken from the survey.

1. **`int EverQuest::s_debug_level = 0;` (`eq.cpp:82-83`) is a static member
   DEFINITION.** It must appear in **exactly one** TU or the link fails
   (duplicate symbol) or does not resolve. It stays in `eq.cpp` with the
   preamble. Do not let the "full include block" rule tempt you into copying
   the two lines above it — copy the **includes**, not the definition.

2. **`#ifdef EQT_HAS_GRAPHICS` at `:3918` wraps `SetConstrainedConfig` and
   closes at `:3923`, the last line of the file.** The guard must travel intact
   with the method. Every other preprocessor conditional in the body
   (`:311`, `:355`, `:424`/`:458`/`:472`, `:2060`, `:3374`) is fully contained
   inside a single method and travels for free — confirm this at your tip with
   `awk 'NR>80 && /^#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}'`.

3. **`DispatchZonePacket` becomes a ~965-line TU with essentially no room to
   grow** (1,500 ceiling − 79-line preamble − 882-line method ≈ 540 lines of
   slack, all of which the next opcode-handling addition eats into). **Flag
   this in your Review and do NOTHING about it in this unit.** The remedy —
   domain sub-dispatchers, i.e. carving the opcode switch into
   `DispatchZoneCombatPacket` / `DispatchZoneEntityPacket` / … — is Batch G's
   shape: it changes method bodies, is a real design question, and belongs to
   the separate follow-up already recorded in
   `refactor/J_file_size_reclamation.md` §Optional. **A J02 that decomposes
   `DispatchZonePacket` has failed its acceptance, not exceeded it.**

No anonymous namespaces, no file-local `static` functions, no file-scope
objects with construction order to preserve — re-check this yourself and say
so in the Review. If you find one, **say so loudly**: it would mean the
survey's hazard analysis is unreliable for the remaining units too.

## Includes

**Each new TU carries `eq.cpp`'s full 79-line include block, verbatim.** Not
"the includes it needs" — see §Process. Do not create a shared "everything"
header.

## CMake

**Do not trust a line number.** Find the sites yourself:

```bash
grep -n 'src/client/eq\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `7e0c7823` there is exactly **one** site, `CMakeLists.txt:266`
(in `CORE_SOURCES`), and `tests/CMakeLists.txt` names `eq.cpp` nowhere — but
the file moves and J03/J04 are editing `CMakeLists.txt` in parallel. Add all
five new TUs beside the original in **every** list that names it.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- build clean; `cmake --build build -j24` classified by **EXIT CODE**, never by
  grepping the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`
- ⚠ `BASE=$(git merge-base fable HEAD)` then
  `python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"` **clean** — the
  unit's central proof. **NOT `fable..HEAD`:** three units are merging into
  `fable` during this wave and `fable..HEAD` will report the other two units'
  work, in reverse, as a very loud spurious FAIL. Record `$BASE` and paste the
  output in the Review.
- ⚠ the **line-multiset check** (§Process): the six files' bodies, each
  stripped of its preamble, form a multiset identical to the original's lines
  80-3923; and the seven extracted ranges re-concatenated in original order
  are byte-identical to `git show "$BASE:src/client/eq.cpp" | sed -n '80,3923p'`.
  Paste both results.
- visual gate PASS with **goldens untouched**. A re-bless would mean the split
  changed behaviour — **STOP and report** rather than re-blessing.
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt`'s pre-existing 1,899-line WARN
  is not yours (§Process) — note it and move on.
- ⚠ `chmod 644` every created file before `git add`.

## Review must answer

1. Line-range drift vs this plan at your tip, and the final line count of each
   of the six TUs.
2. The verifier's base rev you used, and whether it came back clean first try.
3. Wall clock and the slowest step, and whether running concurrently with
   J03/J04 stretched the verification tail (Wave 1 is the first parallel wave;
   the Wave 2-4 schedule depends on this number).
4. `DispatchZonePacket`'s TU: its exact final size and remaining slack, as the
   input to the sub-dispatcher decision.

## Territory

`src/client/eq.cpp` and the five new `src/client/eq_*.cpp` TUs beside it,
`CMakeLists.txt`, this file. **Nothing else** — not `include/client/eq.h`, not
the other 18 `eq_*.cpp` domain TUs, not `docs/module_map.md` (the new TUs will
need entries there; that is a follow-up, not this unit).

Concurrent at dispatch: **J03** (`graphics/entity_renderer.cpp` + new
`entity_*.cpp`, `CMakeLists.txt`), **J04** (`action/command_processor.cpp` +
new `command_*.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`), **T318**
(slot3: `graphics/entity_renderer.*`, `eq_entities.cpp`, `eq_guild.cpp`,
`spell_effects.cpp`, `event_bus.h`). **No source overlap with any of them —
`CMakeLists.txt` is the only shared file, which is why merges are serialized
one at a time with a rebase.**

---

## Review (slot3, 2026-09-02, branch `task/J02-eq-cpp-split`, commit `e7341ccb`)

**Done as specified. Move-only, no deviations from the cluster plan beyond one
required boundary correction (§1). Base tip verified `5d7d7b08` at checkout.**

### 1. Line-range drift, and one hazard the plan missed

`eq.cpp` was **3,923 lines at my tip — byte-for-byte the size the plan
measured**, and every cluster boundary named in the table sat exactly where the
plan said. Two adjustments, both forced by the plan's own §Process rules rather
than by drift:

1. ⚠ **HAZARD THE PLAN MISSED: the UCS family is a single `/* ... */` block
   comment.** `eq.cpp:2933` opens `/*` and `:3170` closes `*/`; **every one of
   the eleven `UCS*` methods is commented out**, under a `// TODO: Fix UCS
   implementation to use correct EQStream API` banner. The plan's range for that
   cluster, **2934-3173, cuts *inside* the comment** — it would have left an
   unterminated `/*` in `eq.cpp` and an unopened `*/` in `eq_ucs.cpp`. Taken as
   written it does not compile. I used **2931-3171** (banner + TODO + `/*` …
   `*/` + trailing blank), which keeps the block intact. The §Hazards claim that
   the ranges were "verified directly against the tree" does not hold for this
   one. `eq_ucs.cpp` is therefore **320 lines of which 241 are comment and 79
   are includes: a TU containing zero code.** It builds, and it is what the
   contract specifies, but the orchestrator may prefer to delete the block
   outright (a separate, non-move-only decision) rather than give dead commented
   code its own translation unit.
2. **Banners shifted one line, per §Process ("ranges start at the first line of
   the cluster's leading comment").** The plan's table still starts three ranges
   at the first *statement*: `// Zone packet senders` sits at `:1681` not
   `:1682`, and `// Zone packet processors` at `:1953` not `:1954`. I took each
   banner with its cluster. I also left `// Movement implementation` (`:3172`,
   inside the plan's UCS range) with the movement accessors it introduces.

Final partition, contiguous and exhaustive over body lines 80-3923:

| lines | → |
|---|---|
| 1-79 | preamble (copied to all six) |
| 80-799 | `eq.cpp` part A |
| 800-1680 | `eq_zone_dispatch.cpp` |
| 1681-1952 | `eq_zone_handshake.cpp` |
| 1953-2930 | `eq_zone_entry.cpp` |
| 2931-3171 | `eq_ucs.cpp` |
| 3172-3700 | `eq_player_state.cpp` |
| 3701-3923 | `eq.cpp` part B |

| file | lines | plan estimate |
|---|---|---|
| `src/client/eq.cpp` | **1,022** | ~1,025 |
| `src/client/eq_zone_dispatch.cpp` | **960** | ~965 |
| `src/client/eq_zone_handshake.cpp` | **351** | ~355 |
| `src/client/eq_zone_entry.cpp` | **1,057** | ~1,065 |
| `src/client/eq_ucs.cpp` | **320** | ~325 |
| `src/client/eq_player_state.cpp` | **608** | ~610 |
| `CMakeLists.txt` | 1,905 (+5) | — |

3,923 → 4,318 across six TUs; the +395 is five extra copies of the 79-line
preamble. **No header touched, no method decomposed, no drive-by fix.**

### Hazards 1-3, re-checked at my tip

1. `int EverQuest::s_debug_level = 0;` is at **`:81-82`** (the plan says 82-83;
   off by one). It stayed in `eq.cpp` with part A. The preamble I copied is
   lines **1-79**, which ends two blank lines short of the comment — the
   definition was never at risk of duplication. Link is clean.
2. `#ifdef EQT_HAS_GRAPHICS` at `:3918`…`:3923` is inside part B and stayed in
   `eq.cpp`. `awk 'NR>80 && /^#(if|ifdef|...)/'` reproduced the plan's list
   exactly (`311`, `355`, `424`/`458`/`472`, `2060`, `3374`, `3918`/`3923`);
   all are method-internal and travelled for free.
3. `DispatchZonePacket` — see §4.

**No anonymous namespaces, no file-local `static` functions, no file-scope
objects with construction order to preserve** — re-checked myself
(`grep -n '^namespace\|^static'` → empty; the only file-scope non-method line
in the whole file is `s_debug_level`). The survey's hazard analysis was right
about *that* for `eq.cpp`; it was wrong about the UCS comment block, so
**the remaining units should not treat the survey's ranges as compile-checked.**

### CMake

`grep -n 'src/client/eq\.cpp' CMakeLists.txt tests/CMakeLists.txt` → **exactly
one site**, `CMakeLists.txt:266` in `CLIENT_CORE_SOURCES` (the plan calls the
variable `CORE_SOURCES`; its real name is `CLIENT_CORE_SOURCES`). All five TUs
added beside the original. `tests/CMakeLists.txt` names `eq.cpp` nowhere and
needed no change.

### 2. The verifier — clean first try, but the flag that matters is `--repo`

```
$ BASE=$(git merge-base fable HEAD)   # 5d7d7b08c7c3847b1aa2f8bd48de34fa1103b694
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot3
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt

Files verified: 6
Boilerplate: +360 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
```

`fable` did move under me exactly as J01 predicted — `5d7d7b08` at checkout,
`091a13fd` (T319 merged) by the time I ran the verifier — so `merge-base` was
load-bearing. The +360 boilerplate is the five extra preambles (5 × 72
non-blank lines).

⚠ **NEW TEMPLATE BUG, for J03-J17: `verify_move_only.py` defaults `--repo` to
`.`, and an executor agent's shell cwd is NOT its slot** (this project forbids
`cd`, and agent threads reset cwd between calls). My first invocation, written
exactly as the acceptance line specifies, silently verified
`/mnt/projects/fable/main` instead of my slot and printed a **loud FAIL listing
T319's `Application::setupHotbarCallback` / `IrrlichtRenderer` lines** — the
identical failure signature the batch doc warns about for the wrong base rev,
from a completely different cause. An executor who "fixed" the base and re-ran
would still be verifying the wrong tree. **The acceptance line must read
`verify_move_only.py "$BASE..HEAD" --repo <slot>`.**

### 3. Independent content proof (the multiset check)

Both halves, exactly as §Process specifies:

```
$ cat <six TU bodies, each with its 79-line preamble stripped> | sort > after.txt
$ git show "$BASE:src/client/eq.cpp" | sed -n '80,3923p' | sort > before.txt
   3844 before.txt
   3844 after.txt
$ diff before.txt after.txt ; echo $?
0                       # MULTISET-OK
```

```
$ cat r_a r_dispatch r_handshake r_entry r_ucs r_player r_b > recat.txt
$ cmp recat.txt <(sed -n '80,3923p' eq_orig.cpp) ; echo $?
0                       # BYTE-IDENTICAL
```

Body lines 80-3923 are partitioned **exactly once each**, no gaps, no overlap,
byte-identical when re-concatenated in original order.

### 4. `DispatchZonePacket`'s TU — the input to the sub-dispatcher decision

`eq_zone_dispatch.cpp` is **960 lines**: 79 preamble + 881 for the single
method (`:800-1680` including its trailing blank). Against the 1,500 warn
ceiling that is **540 lines of slack, all of it inside one method** — the file
cannot be split again without decomposing `DispatchZonePacket`, so every future
opcode handler spends that slack directly. **Flagged, not acted on**, per
§Hazards 3. The remedy (domain sub-dispatchers —
`DispatchZoneCombatPacket` / `DispatchZoneEntityPacket` / …) is Batch G's shape
and belongs to `J_file_size_reclamation.md` §Optional. **This TU is the batch's
next candidate.**

### Acceptance

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0**, clean first try |
| `ctest -j6` (full, 3,470 tests) | **3468/3470**. `AudioManagerTest.PreloadCommonSounds` = documented ALSA host quirk. `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` = **load-dependent flake, not a regression** (see below). **Zero real new failures.** 409 s |
| `verify_move_only.py 5d7d7b08..HEAD --repo <slot>` | **PASS, first try** (output above) |
| multiset + byte-identical concat | **both exit 0** (above) |
| visual gate | **PASS** (ctest #3467, 22.87 s). `git status --porcelain` **empty** — goldens untouched, nothing re-blessed |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `check_file_sizes.sh --files` (6 sources + CMakeLists) | **6 ok, 0 fail**; the sole WARN is `CMakeLists.txt` at 1,905 |

**On the `HeadlessShutdownTest` failure.** It is a 5-second subprocess-timing
test: it launches the headless client, pipes a command, and greps the output
for `CMD outcome=held`. Under the parallel wave it captured an **empty**
output — the child produced nothing at all in its window, not a wrong outcome.
Re-run in isolation on this same branch and binary it passes **5/5**
(`--repeat until-fail:5`). It is not in `test_baseline.txt`; **it should be
added as a load-dependent flake**, alongside the RDP one, or given a longer
timeout — three concurrent slots is now the normal condition, not an
exception.

`CMakeLists.txt` 1,900 → 1,905 (my 5 source lines). It was already over the
1,500 WARN at my base and is explicitly not this unit's to fix.

### 5. Wall clock, and what the parallel wave cost

**~32 minutes end to end** (05:36 → 06:08 UTC), single agent, no retries.

| step | approx |
|---|---|
| reading the four contract docs + surveying `eq.cpp` | ~9 min |
| **the cut itself + CMake + both proofs** | **~4 min** |
| `cmake --build -j24` | ~4 min |
| **`ctest -j6`** | **6.8 min — the single slowest step** |
| `build-gles2-x86.sh` | ~3 min |
| `build-arm-noble.sh` | ~4 min |

**J01's warning was right and the number is now measured: `ctest -j6` went from
3.0 min solo (J01) to 6.8 min under Wave 1's three-slot contention — 2.3×.**
Every other step held roughly steady, so the whole tail stretched by about the
one step. A three-unit wave is ~35 min of wall clock per unit, not 30, and the
cost is again almost entirely the bar rather than the file: `eq.cpp` is 1.6×
`inventory_manager.cpp` and the mechanical work still took **four minutes**.
The visual gate passed at 22.87 s with no sign of the CPU-ceiling flake, so
`-j6` did its job; the flake that did fire was an unrelated subprocess-timeout
test, which suggests the cap protects the gate but not every timing-sensitive
test in the suite.

### What should change in the template before J05+

1. ⚠ **`--repo <slot>` on the `verify_move_only.py` line** (§2). Highest
   priority: without it the command verifies the wrong checkout and fails
   loudly with another unit's diff — indistinguishable from the wrong-base
   failure the doc already warns about.
2. ⚠ **Compile-check the survey's ranges, or say plainly that they are not.**
   The UCS `/*`…`*/` boundary (§1.1) would have broken the build for any
   executor who took the table literally. A one-line `awk` for unbalanced
   block-comment markers per proposed range would have caught it.
3. **Make the plan tables actually follow the banner rule.** J02's table still
   starts three of its six ranges at the first statement, one line after the
   banner the rule says to include. Either fix the tables or drop the rule to
   "executor extends backwards to the banner" — right now the plan contradicts
   the process doc it cites.
4. **Do not name the CMake *variable* either, not just the line.** The plan
   says `CORE_SOURCES`; it is `CLIENT_CORE_SOURCES`. `grep -n` found it
   instantly, which is the point.
5. **Board `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`
   in `test_baseline.txt`** as load-dependent, so the next three units in a wave
   do not each spend a re-run diagnosing it.
6. The rest of the bar cost nothing to meet and should stay verbatim —
   especially "goldens untouched / STOP rather than re-bless".

### Not done (out of scope, deliberately)

No method decomposition: `DispatchZonePacket` (881), `ZoneProcessPlayerProfile`
(467), `ZoneProcessAnimation` (295), `PublishFullStateSnapshot` (197) and
`ZoneProcessDeath` (168) all moved verbatim. `include/client/eq.h` untouched.
`docs/module_map.md` needs entries for the five new TUs — outside this unit's
territory, and a follow-up.
