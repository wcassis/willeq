# J07 — Batch J Wave 2: split eq_player_info.cpp (2,301 → 4 TUs, largest ~753)

**Slot: assigned at dispatch. Branch `task/J07-eq-player-info-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 2 of `refactor/J_file_size_reclamation.md`, running alongside **J05** and
**J06**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND its two correction sections — "⚠ TEMPLATE CORRECTIONS
FROM J02" and "⚠ FURTHER CORRECTIONS FROM J04"** (eight hard-won fixes; they win
over anything older), then `refactor/J04_command_processor_split.md`
**including its Review** (the proven template and its failure modes), plus
`refactor/EXECUTOR_PROTOCOL.md` and `refactor/F07_player_info.md` (the unit that
created this file out of `eq.cpp`, and whose cluster banners you are cutting
along).

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## This is the lowest-risk unit in Batch J. The bar is identical anyway.

Verified at `ed97d701`:

- **Zero file-local `static`s** (`grep -n '^static'` → empty).
- **Zero anonymous namespaces** (`grep -n '^namespace {'` → empty).
- **Zero namespaces at all** — `EverQuest` is in the global namespace and this
  file has no `namespace … {` open and no close to replicate. `grep -n
  'namespace'` returns nothing.
- **Zero block comments** (`grep -n '/\*'` → empty).
- **118 methods**, all `EverQuest::…`, the largest well under 200 lines. No
  method dominates its TU, so no §Optional Batch-G tension.
- **Twelve clusters already banner-delimited** by F07, with `// ====…` rules —
  the cut lines are pre-drawn.
- **One CMake add-site.**

So there is no Hazard 1 here in the sense J04, J05 and J06 have one: **no symbol
needs promoting, no internal header is needed, and `verify_move_only.py` must
exit 0.** If it does not, something is wrong — do not reach for the §Process
step-4 exception, find the mistake.

**None of that lowers the bar.** Every acceptance item below is the same as
J05's and J06's, for the same reason T166 exists: a split that is "obviously
safe" and unproven is the one that quietly duplicates a range. The two content
proofs are cheap; run both.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to `include/client/eq.h`. **No
method decomposition.** No drive-by fixes — and this file is *full* of tempting
ones (tombstone comments for deleted methods, T145/T168 notes about dead
opcodes). Leave every one of them exactly where it is.

## The split

**Measured at `ed97d701` (2026-09-02, `fable` tip after J01/J02/J04 merged):
2,301 lines** — unchanged from the survey. Every range below was read out of the
tree at that commit. **Re-verify against your own branch tip before you cut** and
report drift in your Review rather than silently adjusting.

**Preamble is lines 1-44**: a 9-line file-header comment describing the split
(1-9), a blank, the include block (11-43, containing `#ifdef WITH_AUDIO` at
26-29 and `#ifdef EQT_HAS_GRAPHICS` at 35-37), a blank at 44. **There is no
namespace open and no namespace close.** Line 2301 is the last line of the last
method.

⚠ **The 1-9 file-header comment is the one thing you may adapt.** It currently
enumerates all twelve clusters. Each new TU gets its own 4-6 line header naming
*its* clusters and citing `refactor/J07_eq_player_info_split.md` and
`refactor/F07_player_info.md`; the stays-file's header is trimmed to its own
clusters. **Comment lines are allowlisted boilerplate by `verify_move_only.py`,
so this is permitted** — but it **is** visible in the line-multiset check, so
**enumerate every header line you wrote in your Review** exactly as J04
enumerated its one corrected comment. Keep it to the headers; change no other
comment. **Lines 11-43 (the include block) are copied verbatim, unaltered.**

§Process: **every range starts at the first line of its cluster's leading
`// ============` banner**, not at the first statement or at the banner's text
line. The ranges below already do — verified: `45`, `374`, `948`, `1593` are
each the top rule of a three-line banner.

| Range | Clusters (F07's own numbering, banners verbatim) | Target TU | ≈ lines |
|---|---|---|---|
| 45-373 | **stays.** C1 Tribute/AA stubs (`@45`); C2 Player stat getters (`@119`); C3 GameState sync + group accessors (`@268`), incl. the "Group Accessors and Sync Helper" sub-banner (`@336`) and the T286 `SyncGroupMemberToGameState` deletion note (`@370`) | **stays** `eq_player_info.cpp` | ~373 |
| 374-947 | C4 Tasks/DZ/BeginCast/ManaChange/Buff/PetBuff (`@374`), incl. the four T277 buff helpers (`@621`, `@648`, `@672`, `@752`); C5 ApplyPoison / Track (`@841`) | `eq_player_buffs.cpp` | ~618 |
| 948-1592 | C6 Emote/AFK/Anon/RP/Camp/Logout (`@948`); C7 Rezz / WhoAll / Inspect (`@1113`); C8 Corpse / Consent (`@1450`), incl. the "Phase 3: Consent System" sub-banner (`@1528`) | `eq_player_social.cpp` | ~689 |
| 1593-2301 | C9 Assist/Boat/Split/LFG/Shielding/EnvDamage/Discipline/Banker (`@1593`) with its five Phase-3 sub-banners (`@1621`, `@1651`, `@1689`, `@1731`, `@1782`, `@1826`); C10 Save/Popup/ClearObject/Duel/BindWound/Track (`@1849`) with "Phase 4: Dueling" (`@1912`) and "Phase 4: Skills" (`@2001`); C11 Surname/FaceChange/Random/FindPerson + social misc (`@2035`) with "Phase 4: Misc" (`@2092`); C12 CameraEffect / PlayMP3 / Sound (`@2225`) | `eq_player_misc.cpp` | ~753 |

**Result: 4 TUs.** Every one under 1,500; largest is `eq_player_misc.cpp` at
~753.

Arithmetic that must hold: `44 + 329 + 574 + 645 + 709 = 2301`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

## ⚠ HAZARD — the only one: three `#ifdef WITH_AUDIO` regions in the body

The J02 lesson (a survey range cut inside a `/* */` and would not have compiled)
applies here as the preprocessor. **Check every cut boundary against `#if`/`#endif`
nesting before cutting, and paste the check:**

```bash
awk 'NR>=44 && /^\s*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/eq_player_info.cpp
```

At `ed97d701` that yields exactly three pairs below the preamble:
**`436/445`**, **`2254/2258`**, **`2290/2299`** — all `#ifdef WITH_AUDIO`, all
method-internal, and all comfortably inside a single cluster (`436/445` inside
374-947; the other two inside 1593-2301). **None of the three interior cut
points (374, 948, 1593) lands inside a conditional.** Verify that is still true
at your tip.

Note the file is **tab-indented**. Whatever you cut with must not re-indent —
`sed -n 'A,Bp'` is safe; anything that reformats is not.

## Includes

**Each new TU carries `eq_player_info.cpp`'s full include block, lines 11-43,
verbatim** — including the `#ifdef WITH_AUDIO` and `#ifdef EQT_HAS_GRAPHICS`
groups and the interleaved blank lines. **Not "the includes it actually
needs"**: §Process is explicit, trimming per-TU include sets is a second and
riskier change wearing a move-only commit's clothes, and it removes a judgement
call from the executor. **There is no namespace to open or close.**

## CMake — grep, never trust a line number

```bash
grep -n 'src/client/eq_player_info\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `ed97d701` that is **exactly one site**: `CMakeLists.txt:283`, inside
`SET(CLIENT_CORE_SOURCES` (which opens at `:265`), in the run of `eq_*.cpp`
domain TUs. ⚠ **The variable is `CLIENT_CORE_SOURCES`, not `CORE_SOURCES`** —
the batch plan's older text was wrong and J02 corrected it.
`tests/CMakeLists.txt` names `eq_player_info.cpp` **nowhere** and should need no
change. **Grep anyway** — line numbers in this 1,909-line file rot within hours.

⚠ **The CMake commands in this project are UPPERCASE** (`SET`, `ADD_EXECUTABLE`,
`IF`). `grep -n 'set('` returns nothing. Grep for the **filename**.

Add all three new TUs beside the original at the site you find, keeping the
`eq_*.cpp` run alphabetically coherent with its neighbours.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known LOAD-DEPENDENT FLAKE**, not a regression: a 5 s subprocess-timing test
    that captures empty output under multi-slot contention and passes 5/5 in
    isolation on the same binary. It is **not** in `test_baseline.txt`. **Do not
    re-diagnose it. If it fails in a wave run, re-run it alone before reporting
    anything.**
- ⚠ **the verifier, with `--repo`:**
  ```bash
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **Both parts of that line are load-bearing and each has its own failure mode
  that looks identical to the other's:**
  - **`--repo` is mandatory.** It defaults to `.`, and an executor agent's shell
    cwd is **not** its slot (this project forbids `cd` and agent threads reset
    cwd between calls). Omitting it silently verifies `/mnt/projects/fable/main`
    and prints a loud FAIL full of some unrelated merged unit's lines. J02 hit
    exactly this.
  - **`fable..HEAD` is WRONG.** Three units merge during a wave, so `fable`
    moves under you; `fable..HEAD` reports the *other* units' merged work, in
    reverse, as substantive removals. J01 saw 533 spurious removals this way.
  - ⚠ **These two failures have the SAME SIGNATURE — another unit's work
    appearing as removals — and different causes.** An executor who "fixes" the
    base and re-runs without `--repo` is still verifying the wrong tree. If the
    verifier flags files your unit never touched, check **both** before changing
    a single line of code.
  - Record `$BASE` in the Review and paste the output. ⚠ **For J07 this MUST
    exit 0** — this unit promotes nothing and needs no internal header, so
    there is no legitimate §Process step-4 exception available to it. A
    substantive line here is a mistake to find, not an exception to request.
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences.
  ```bash
  # `after`: the four TU bodies, each stripped of its 4-6 line new header AND of
  # the 33-line include block (lines 11-43). There is no namespace close.
  cat <the four stripped bodies> | sort > /tmp/after.txt
  git show "$BASE:src/client/eq_player_info.cpp" | sed -n '45,2301p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  This unit creates no cross-TU header, so J04's correction 7 (count the
  header's moved lines into `after`) does not apply — **but the four rewritten
  file-header comments do show up here.** Because they are stripped as preamble
  in the recipe above, the diff should be **empty with no exceptions at all.**
  If you instead include them, enumerate every line.
- **byte-identical re-concatenation** — stronger and also cheap: concatenate the
  extracted ranges **in original order** (`45-373, 374-947, 948-1592,
  1593-2301`) and confirm the result is byte-identical to
  `git show "$BASE:src/client/eq_player_info.cpp" | sed -n '45,2301p'`. Paste
  the result. **2,257 lines.** For this unit that should be a clean `diff` exit
  0 with **no reverted lines at all** — nothing outside the four headers changed.
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ **`build-arm-noble.sh` routinely blows the 600 s Bash-tool ceiling under
  wave concurrency** — SIGTERM, **exit 143**, typically mid-`gles2_zone_benchmark`.
  **Exit 143 means RE-INVOKE, not breakage.** The incremental re-run finishes.
  Expect to call it twice; J04 did.
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt` (1,909) and
  `tests/CMakeLists.txt` (2,778) both WARN; **both are pre-existing and neither
  is yours** (§Process, and J20).
- ⚠ **`chmod 644` every created file before `git add`.** A scripted `sed >` pass
  produces mode 755 and records a spurious `100644 => 100755`; verify with
  `git ls-files -s` that all new files are `100644`.

## Review must answer

1. **Confirmation that the four zero-hazard properties still held at your tip**,
   with the greps pasted: no file-local statics, no anonymous namespaces, no
   namespace at all, no block comments. If any has changed, that is a finding
   the batch needs.
2. **The preprocessor boundary check** on all three interior cut points, pasted
   — the J02 lesson. Any range you had to move, and why.
3. **Every rewritten file-header comment line**, enumerated, as the unit's only
   non-move change. Nothing else may appear in that list.
4. **How many CMake add-sites you actually found**, per file, against the one
   this plan names.
5. **Both content proofs**, pasted: multiset and byte-identical
   re-concatenation. Confirm the verifier exited **0**.
6. Line-range drift vs this plan, final line counts of all four TUs, the
   verifier base rev, wall clock and slowest step, and whether concurrency with
   J05/J06 stretched the verification tail. ⚠ **This unit is the batch's control
   sample** — it is the only Wave-2 unit with no hazard and no exception, so its
   wall clock is the cleanest measurement available of what the *bar alone*
   costs under three-slot contention. Report the per-step timings carefully.
7. **Findings only, not fixes:** the tombstone comment blocks for methods
   deleted by T104/T49/T168/T286 (`@370`, `@1625-1650`, and the T145 OP_ENVDAMAGE
   note at `@1750`); anything you notice that looks unreachable. **Record. Do
   not act.**

## Territory

`src/client/eq_player_info.cpp`, the three new `src/client/eq_player_*.cpp` TUs
beside it, `CMakeLists.txt`, this file. **Nothing else** — not
`include/client/eq.h`, not `eq_player_state.cpp` (a different file, created by
J02; do not confuse them), not any of the other 18 `eq_*.cpp` domain TUs, not
`who_format.cpp` / `who_response.cpp` / `inspect_answer.cpp` /
`bind_wound_outcome.cpp`, not any `tests/*.cpp`, not `docs/module_map.md` (it
will need entries for the three new TUs — a flagged follow-up, not this unit).

⚠ **Naming collision to avoid:** `src/client/eq_player_state.cpp` already exists
(J02 created it from `eq.cpp:3172-3700`). The three TUs here are
`eq_player_buffs.cpp`, `eq_player_social.cpp` and `eq_player_misc.cpp` — do not
name anything `eq_player_state*`.

Concurrent at dispatch: **J05** (`src/client/graphics/eq/wld_loader.cpp` + new
`wld_fragments_*.cpp` + `wld_internal.h`, `CMakeLists.txt`,
`tests/CMakeLists.txt`) and **J06** (`src/client/application.cpp` + new
`application_*.cpp`, `CMakeLists.txt`). No source overlap with either.
⚠ **`CMakeLists.txt` is shared with both** — which is why **merges are
serialized one at a time with a rebase.**
