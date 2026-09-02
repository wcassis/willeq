# J06 — Batch J Wave 2: split application.cpp (2,368 → 5 TUs, largest ~947)

**Slot: assigned at dispatch. Branch `task/J06-application-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 2 of `refactor/J_file_size_reclamation.md`, running alongside **J05** and
**J07**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND its two correction sections — "⚠ TEMPLATE CORRECTIONS
FROM J02" and "⚠ FURTHER CORRECTIONS FROM J04"** (eight hard-won fixes; they win
over anything older), then `refactor/J04_command_processor_split.md`
**including its Review** (the proven template and its failure modes), plus
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to `include/client/application.h`.
**No method decomposition** — in particular `Application::initialize` is 587
lines and is explicitly out of scope (`J_file_size_reclamation.md` §Optional
lists it at 569; it has grown). No drive-by fixes.

## ⚠ The file grew since the survey: it is 2,368 lines, not 2,356

**Measured at `ed97d701` (2026-09-02, `fable` tip after J01/J02/J04 and T319
merged): 2,368 lines.** T319 added the hotbar-callback wiring. Every range below
was read out of the tree at that commit. **Re-verify against your own branch tip
before you cut** and report drift in your Review rather than silently adjusting.

## ⚠ HAZARD 1 — `s_terminateRequested` and the four signal handlers. This is the unit.

`src/client/application.cpp:62-95` is a single banner-led block:

```
62   // ========== Signal Handlers ==========
64-74  (P36 rationale comment)
75   static std::atomic<bool> s_terminateRequested{false};
77   static void HandleSigTerm(int)   { s_terminateRequested.store(...); }
81   #ifndef _WIN32
82   static void HandleSigUsr1(int)   { LogLevelIncrease(); }
86   static void HandleSigUsr2(int)   { LogLevelDecrease(); }
90   #ifdef EQT_HAS_GRAPHICS
91   static void HandleSigHup(int)    { HotkeyManager::instance().reload(); }
94   #endif
95   #endif
```

Verified reachers at `ed97d701` (`grep -n 's_terminateRequested\|HandleSig'`):

| symbol | referenced at | inside |
|---|---|---|
| `s_terminateRequested` | `:78` (write), **`:922` (read)** | `HandleSigTerm`, **`mainLoop` (910-947)** |
| `HandleSigUsr1/2/Term/Hup` | **`:204, 205, 206, 208`** (address taken, `signal(…)`) | **`initialize` (105-691)** |

So the block has **two consumers in two different clusters**: `initialize`, and
`mainLoop`. Five `static`s with internal linkage cannot serve both if those two
methods land in different TUs. **This is the whole risk of J06** — everything
else in this file is plain relocation.

### Two options. Pick one, and ARGUE YOUR CHOICE IN THE REVIEW.

Both were surveyed and both are defensible. The plan does not decide for you;
it requires you to state which you took and why.

**Option A — keep `mainLoop` with `initialize` (no promotion, no exception).**
The signal block, `initialize`, `run`, `shutdown`, `requestQuit`,
`gameThreadLoop` and `mainLoop` all stay in `application.cpp` as one
"lifecycle + threads" cluster (lines 62-947). Nothing is promoted, nothing loses
internal linkage, `verify_move_only.py` **exits 0**, and the multiset check has
no exceptions to enumerate. Cost: the stays-TU is ~947 lines — the largest in
the split, though still 550 short of the 1,500 WARN. **This is the cluster table
below, and it is the recommended default** on the grounds that the batch's
lowest-cost outcome is one where nothing needs signing off.

**Option B — promote to `src/client/application_internal.h`.** Move the whole
62-95 block into a private internal header (`#pragma once`, `namespace eqt {`,
the C++17 `inline` variable form `inline std::atomic<bool>
s_terminateRequested{false};` and `inline void HandleSig…`), included by
whichever TUs need it. This buys the freedom to file `mainLoop` with the frame
methods (`updateGameState`, `render`) where it arguably belongs by domain, and
drops the stays-TU to ~849. Cost: **`verify_move_only.py` exits 1 by
construction** — its `BOILERPLATE` allowlist covers `#include`, `#pragma`,
`namespace`, `}` and comments but **not `inline`** — so five signature lines
become an enumerated exception needing orchestrator sign-off (J04's precedent,
its Review §"NOT clean, and it CANNOT be under this plan"). It also changes the
linkage of a signal-handler flag, which is a semantically live object, for a
cosmetic gain.

If you take Option B: **paste the verifier's full output, enumerate every
flagged line, and ask for sign-off explicitly. Do NOT contort the code to force
a green exit.** And do not take Option B *and* leave `mainLoop` where it is —
that would be an exception bought for nothing.

## The split (as written, this is Option A)

**There ARE banner comments in this file** (`// ========== Signal Handlers ==========`
at `:62`, `// ========== Application ==========` at `:97`, the
`// =====…` zone-loading banner at `:1600-1602`, `// ========== Static Helpers ==========`
at `:1996`). §Process: **every range starts at the first line of its cluster's
leading banner**, not at the first statement. The ranges below already do.

**Preamble is lines 1-61**: the include block (1-58, which contains three
conditional groups — `#ifndef _WIN32` at 32-34, `#ifdef EQT_HAS_GRAPHICS` at
46-54, `#ifdef WITH_AUDIO` at 56-58), a blank, `namespace eqt {` at `:60`, a
blank. The close is `} // namespace eqt` at **:2368**. Both replicate into every
new TU, **verbatim, conditionals and all**.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 62-947 | **stays.** Signal handlers (Hazard 1), ctor/dtor, `initialize` (105-691, 587 lines), `run`, `shutdown`, `requestQuit`, `gameThreadLoop`, `mainLoop` | **stays** `application.cpp` | **~947** |
| 948-1437 | `processNetworkEvents`, `processInput`, `processPreConnectionLifecycleCommands`, `loadScriptFile`, `updateScript`, `dispatchToActionOrRenderer`, `printCommandResult`, `wireUnifiedCommandDispatch`, `DispatchSlashCommand` | `application_commands.cpp` | ~553 |
| 1438-1599 | `updateGameState`, `render` | `application_frame.cpp` | ~225 |
| 1600-1995 | the `// ===== Zone Loading =====` banner, the whole `#ifdef EQT_HAS_GRAPHICS` region (`startLoadingThread`, `joinLoadingThread`, `checkLoadingComplete`, `loadZoneGraphicsOnThread`, `loadZoneGraphics`, `setupRDPAudio`), the T104/T49 deleted-method comment block, `updateLoadingProgress` | `application_zone_loading.cpp` | ~459 |
| 1996-2367 | `// ========== Static Helpers ==========`, `parseArguments` (2198 lines→201), `loadConfigFile` | `application_config.cpp` | ~435 |

**Result: 5 TUs.** Every one under 1,500; largest is the surviving
`application.cpp` at ~947.

Arithmetic that must hold: `61 + 886 + 490 + 162 + 396 + 372 + 1 = 2368`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

## ⚠ HAZARD 2 — this file is dense with `#ifdef`, and one region spans 364 lines

The J02 lesson (a survey range cut inside a `/* */` and would not have compiled)
applies here with the preprocessor instead of comments. **Check every cut
boundary against `#if`/`#endif` nesting before cutting, and paste the check:**

```bash
awk 'NR>=59 && /^\s*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/application.cpp
```

At `ed97d701` that yields, below the preamble: `134/145`, `175/197`,
`203/207/209/211`, `268/278`, `411`, `640/656/665`, `722/724/731/759`,
`851/888`, `1500/1512`, `1516/1595/1597`, **`1604 … 1939/1965 … 1967`**,
`2154/2167/2170/2171`, `2172/2178`, `2188/2191`, `2211/2218`,
`2229/2250/2263/2264`, `2266/2298`.

The one that matters:

- ⚠ **`#ifdef EQT_HAS_GRAPHICS` opens at `:1604` and closes at `:1967`** — a
  364-line region containing all six zone-loading methods, with a nested
  `#if defined(WITH_RDP) && defined(WITH_AUDIO)` at `1939/1965` inside
  `setupRDPAudio`. **The `1600-1995` range wraps it whole**, which is why that
  cluster is one TU and not two. **Do not cut anywhere inside 1604-1967.**
- `render` (1515-1599) carries its own `#ifdef EQT_HAS_GRAPHICS / #else /
  #endif` at `1516/1595/1597` — fully inside the `1438-1599` range. Fine.
- Everything else in the list is method-internal and travels for free.

There are **no block comments** in this file (`grep -n '/\*'` → the only hit is
inside a string/URL-free context; confirm at your tip) and **no anonymous
namespaces** (`grep -n '^namespace {'` → empty). The only file-scope statics are
Hazard 1's five.

## Includes

**Each new TU carries `application.cpp`'s full preamble, lines 1-58, verbatim** —
including the `#ifndef _WIN32`, `#ifdef EQT_HAS_GRAPHICS` and `#ifdef WITH_AUDIO`
conditional include groups and the P28/T310 explanatory comments — plus
`namespace eqt {` and the matching `} // namespace eqt` close. **Not "the
includes it actually needs"**: §Process is explicit, trimming per-TU include
sets is a second and riskier change wearing a move-only commit's clothes, and it
removes a judgement call from the executor. (Note the ordering comment on line
2: `<json/json.h>` must precede `eq.h`. Copying the block verbatim preserves
that for free; reordering would not.)

## CMake — grep, never trust a line number

```bash
grep -n 'src/client/application\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `ed97d701` that is **exactly one site**: `CMakeLists.txt:385`, inside
`SET(CLIENT_CORE_SOURCES` (which opens at `:265`). ⚠ **The variable is
`CLIENT_CORE_SOURCES`, not `CORE_SOURCES`** — the batch plan's older text was
wrong and J02 corrected it. `tests/CMakeLists.txt` names `application.cpp`
**nowhere** and should need no change. **Grep anyway** — line numbers in this
1,909-line file rot within hours, and a test target may have picked it up since.

⚠ **The CMake commands in this project are UPPERCASE** (`SET`, `ADD_EXECUTABLE`,
`IF`). `grep -n 'set('` returns nothing. Grep for the **filename**.

Add all four new TUs beside the original at the site you find.

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
    anything.** ⚠ **It is also the test closest to J06's territory** — it
    exercises the headless lifecycle path that `mainLoop`/`shutdown` own — so
    you are more likely than other units to see it and more likely to misread it
    as yours. Re-run it alone. If it then passes, it is the flake.
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
    exactly this — and the lines it printed were T319's
    `Application::setupHotbarCallback`, i.e. **this very file.**
  - **`fable..HEAD` is WRONG.** Three units merge during a wave, so `fable`
    moves under you; `fable..HEAD` reports the *other* units' merged work, in
    reverse, as substantive removals. J01 saw 533 spurious removals this way.
  - ⚠ **These two failures have the SAME SIGNATURE — another unit's work
    appearing as removals — and different causes.** An executor who "fixes" the
    base and re-runs without `--repo` is still verifying the wrong tree. If the
    verifier flags files your unit never touched, check **both** before changing
    a single line of code.
  - Record `$BASE` in the Review and paste the output. **Under Option A this
    must exit 0.** Under Option B see Hazard 1.
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences.
  ```bash
  # `after`: the five TU bodies, each stripped of the 61-line preamble and the
  # 1-line namespace close.
  cat <the five stripped bodies> | sort > /tmp/after.txt
  git show "$BASE:src/client/application.cpp" | sed -n '62,2367p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **If you took Option B, the header convention applies (J04 correction 7):**
  `application_internal.h`'s moved lines MUST be counted into the `after`
  multiset, or a naive "cat the .cpp bodies" **silently under-counts and still
  looks clean.** State the header's exact line contribution.
- **byte-identical re-concatenation** — stronger and also cheap: concatenate the
  extracted ranges **in original order** (`62-947, 948-1437, 1438-1599,
  1600-1995, 1996-2367`) and confirm the result is byte-identical to
  `git show "$BASE:src/client/application.cpp" | sed -n '62,2367p'`. Paste the
  result. **2,306 lines.**
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

1. ⚠ **Hazard 1: which option you took, and the argument for it.** Not "I took
   A because the plan recommended it" — state the tradeoff as you saw it at your
   tip, including whether any *new* reader of `s_terminateRequested` or of the
   four handlers had appeared since `ed97d701`. If Option B: the internal header
   declared as the §Process step-4 sanctioned exception, the verifier's full
   output pasted, every flagged line enumerated, and an explicit request for
   orchestrator sign-off. Confirm you did not contort the code to force green.
2. **The preprocessor boundary check** on all four interior cut points, pasted —
   the J02 lesson — with explicit confirmation that `1604-1967` was not cut.
   Any range you had to move, and why.
3. **How many CMake add-sites you actually found**, per file, against the one
   this plan names.
4. **Both content proofs**, pasted: multiset and byte-identical
   re-concatenation.
5. Line-range drift vs this plan, final line counts of all five TUs, the
   verifier base rev, wall clock and slowest step, and whether concurrency with
   J05/J07 stretched the verification tail.
6. **Findings only, not fixes:** `Application::initialize` at 587 lines (the
   §Optional Batch-G list records 569 — it has grown, and it now sets the floor
   for the stays-TU); the 22-line block of T104/T49 deleted-method tombstone
   comments at `1969-1990`. **Record. Do not act.**

## Territory

`src/client/application.cpp`, the four new `src/client/application_*.cpp` TUs
beside it (and `src/client/application_internal.h` only under Option B),
`CMakeLists.txt`, this file. **Nothing else** — not
`include/client/application.h`, not `main.cpp`, not `src/client/mode/*`, not
`eq_action_handler.cpp`, not any `tests/*.cpp`, not `docs/module_map.md`
(it will need entries for the four new TUs — a flagged follow-up, not this
unit).

Concurrent at dispatch: **J05** (`src/client/graphics/eq/wld_loader.cpp` + new
`wld_fragments_*.cpp` + `wld_internal.h`, `CMakeLists.txt`,
`tests/CMakeLists.txt`) and **J07** (`src/client/eq_player_info.cpp` + new
`eq_player_*.cpp`, `CMakeLists.txt`). No source overlap with either.
⚠ **`CMakeLists.txt` is shared with both** — which is why **merges are
serialized one at a time with a rebase.**

---

## Review (executor, slot3, 2026-09-02)

**Branch `task/J06-application-split`, cut from `fable` at
`524db263b11b4dfd19691065d35f2c73ebb0cdd0` ("Batch J: the hard-cap list is
EMPTY; fold J11's four corrections"). Verifier base rev = that same hash
(`git merge-base fable HEAD`).**

**Every acceptance item passed on the first attempt. No exception needs
sign-off: `verify_move_only.py` exited 0.**

### 1. Hazard 1 — Option A, and why

**Took Option A.** `mainLoop` stays with `initialize`; nothing promoted, no
internal header, no linkage change.

Re-measured at my tip. `grep -rn 's_terminateRequested\|HandleSig' src/ include/ tests/`
returns exactly the eleven hits the plan predicted at `ed97d701` and **no new
reader had appeared**: definitions at `:75/77/82/86/91`, the write at `:78`,
the four `signal()` address-takes at `:204-206, 208` inside `initialize`, and
the single read at `:922` inside `mainLoop`. Two consumers, both now in the
stays-TU.

The tradeoff as I saw it:

- Option B's *entire* benefit is cosmetic — a 948-line stays-TU instead of an
  849-line one. Both are far under the 1,500 WARN, and `check_file_sizes.sh`
  reports `ok` for 948. **There is no size problem to solve**, so the gain is
  the aesthetic claim that `mainLoop` belongs beside `render`.
- Its cost is real and permanent: five `static`s with internal linkage become
  `inline` externally-linked entities in a header. `s_terminateRequested` is a
  **live async-signal-safe object** — the one thing a SIGTERM handler is allowed
  to touch. Changing its linkage is a semantic change smuggled into a commit
  whose whole contract is "verbatim relocation". That it happens to be benign
  today is not the same as it being move-only.
- And it converts a clean exit-0 verifier run into a five-line enumerated
  exception requiring orchestrator sign-off — a cost paid by the reviewer, not
  by me, in exchange for 99 lines of file-length cosmetics.

The lifecycle grouping is also not a *worse* home for `mainLoop` than the frame
TU, merely a different one: `mainLoop` is the thread-and-shutdown loop that
drives `run`/`requestQuit`/`gameThreadLoop`, all of which stay. Filing it with
`updateGameState`/`render` would split the lifecycle cluster to unify a frame
cluster — a lateral move.

**I did not contort any code to force a green exit.** The verifier is green
because Option A genuinely introduces no non-boilerplate lines.

### 2. Preprocessor / comment boundary check

Directive map at my tip below line 58 (`awk 'NR>=59 && /^[ \t]*(#if|#ifdef|#ifndef|#else|#elif|#endif)/'`):

```
81 #ifndef _WIN32 | 90 #ifdef EQT_HAS_GRAPHICS | 94 #endif | 95 #endif
134/145 | 175/197 | 203/207/209/211 | 268/278 | 411 | 640/656/665
722/724/731/759 | 851/888 | 1500/1512 | 1516/1595/1597
1604 … 1939/1965 … 1967
2154/2167/2170/2171 | 2172/2178 | 2188/2191 | 2211/2218
2229/2250/2263/2264 | 2266/2298
```

Identical to the plan's `ed97d701` list — **zero drift**. Running-depth check at
every cut point:

```
line   61 depth_after=0      line 1599 depth_after=0
line  947 depth_after=0      line 1600 depth_after=0
line  948 depth_after=0      line 1995 depth_after=0
line 1437 depth_after=0      line 1996 depth_after=0
line 1438 depth_after=0      line 2367 depth_after=0   line 2368 depth_after=0
```

⚠ **Confirmed `1604-1967` was NOT cut.** The four interior cuts are at 948,
1438, 1600 and 1996; the nearest to that region is 1600, four lines above its
open, and 1996, twenty-nine below its close. The whole 364-line
`#ifdef EQT_HAS_GRAPHICS` region — six zone-loading methods plus the nested
`#if defined(WITH_RDP) && defined(WITH_AUDIO)` at `1939/1965` — travels intact
inside `application_zone_loading.cpp`.

Block comments: `grep -n '/\*\|\*/'` yields **four hits, all the same
single-line `/*sig*/` parameter-name comment** at `:77/82/86/91`. No multi-line
block comment exists in this file, so comment depth is 0 everywhere. No
anonymous namespace (`grep -n '^namespace {'` empty). The only file-scope
statics are Hazard 1's five.

Every cut satisfies J03's rule (blank line whose predecessor closes a method):
946 `}` / 947 blank; 1436 `}` / 1437 blank; 1598 `}` / 1599 blank; 1994 `}` /
1995 blank. **No range had to be moved.**

### 3. CMake add-sites

`grep -n 'application\.cpp' CMakeLists.txt tests/CMakeLists.txt` → **exactly one
site, `CMakeLists.txt:385`, inside `SET(CLIENT_CORE_SOURCES`** — matching the
plan's count of one. `tests/CMakeLists.txt`: **zero**. Four lines added beside
the original; `CMakeLists.txt` 1,919 → 1,923.

### 4. Both content proofs

Body = lines 62-2367 of the original, **2,306 lines**.

**Byte-identical re-concatenation** (bodies re-extracted *from the five files on
disk*, each stripped of its 61-line preamble and 1-line namespace close, in
original cluster order):

```
$ wc -l after2.txt                      -> 2306
$ cmp before_body.txt after2.txt ; echo -> RECONCAT-FROM-DISK-EXIT=0
```

**Line-multiset check** (same inputs, `sort`ed):

```
$ diff before_sorted.txt after2_sorted.txt ; echo -> MULTISET-FROM-DISK-EXIT=0
```

No header was created, so J04 correction 7's under-count trap does not apply —
the `.cpp` bodies are the complete `after` set.

Preamble identity, additionally checked per TU: `cmp <(head -61 $f) pre.txt`
exit 0 for all four new TUs, and each ends `} // namespace eqt`.

**Verifier:**

```
$ BASE=$(git merge-base fable HEAD)   # 524db263b11b4dfd19691065d35f2c73ebb0cdd0
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot3
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
Files verified: 5
Boilerplate: +224 / -0 lines (allowed)
PASS: move-only (modulo boilerplate).
VERIFY_EXIT=0
```

(+224 = the four replicated 56-line boilerplate-classified preamble slices. No
non-boilerplate additions, no removals. **No sign-off exception is needed for
this unit.**)

⚠ Note for the next executor: the verifier compares **committed** revisions. Run
before committing it prints `No C++ files changed` and **exits 0** — a green
result that proves nothing, and one that looks exactly like a pass. Commit
first, then verify. Worth adding to the template.

### 5. Drift, final counts, timings

**File was 2,368 lines at my tip — no drift from the plan's `ed97d701`
measurement.** Every range in the cluster table was correct as written; the
arithmetic `61 + 886 + 490 + 162 + 396 + 372 + 1 = 2368` held exactly.

| TU | plan ≈ | actual | body |
|---|---|---|---|
| `application.cpp` (stays) | ~947 | **948** | 62-947 (886) |
| `application_commands.cpp` | ~553 | **552** | 948-1437 (490) |
| `application_frame.cpp` | ~225 | **224** | 1438-1599 (162) |
| `application_zone_loading.cpp` | ~459 | **458** | 1600-1995 (396) |
| `application_config.cpp` | ~435 | **434** | 1996-2367 (372) |

All five estimates were **exactly one high** — the plan added a namespace close
*and* a preamble line per TU (61 + body + 1 = the actual). Batch correction 12
says estimates err in both directions; here the error was uniform and trivial.
Largest TU 948, 552 short of the WARN. `check_file_sizes.sh --files` on all six
changed/created files: **5 ok, 1 warn, 0 fail** — the warn is the grandfathered
`CMakeLists.txt`.

Acceptance, all by exit code:

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (~5.0 min) |
| `ctest --test-dir build -j6` | **1 failed / 3505**, `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk, the only entry in `test_baseline.txt`). **Zero new failures.** 120.7 s |
| `visual_gate` (inside ctest) | **Passed**, 20.18 s |
| goldens | `git status --porcelain` **empty** after the gate |
| `verify_move_only.py --repo <slot>` | **EXIT 0**, PASS |
| byte-identical re-concat | **EXIT 0**, 2,306 lines |
| line multiset | **EXIT 0** |
| `scripts/build-gles2-x86.sh` | **EXIT 0** |
| `scripts/build-arm-noble.sh` | **EXIT 0, first invocation** |
| `check_file_sizes.sh --files` | 5 ok, 1 pre-existing warn, **0 fail** |

**Wall clock ~28 min. Slowest step: `cmake --build -j24` at ~5.0 min** — it
exceeded the 600 s Bash ceiling and was backgrounded, then polled with
`scripts/wait_for.sh` (met at 300 s). The cut itself plus CMake plus both proofs
was **under 3 minutes**; everything else was the bar, as J01/J02 measured.

**Concurrency did NOT stretch the tail this time.** `ctest -j6` ran in **120.7 s
(2.0 min)** against J02's 6.8 min under a three-wide wave and J01's 3.0 min solo
— i.e. *faster than the solo baseline*, on a suite that has since grown to 3,505
tests. `build-arm-noble.sh` finished inside one 600 s call rather than the
expected two (J04's correction 6). Both suggest slot1/slot2 were between
build-heavy phases during my tail rather than that the contention effect is
gone; a later unit should not plan on this.

⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` — the
flake the plan flagged as closest to J06's territory — **passed in the wave
run**. No isolated re-run was needed.

### 6. Findings only (recorded, NOT acted on)

- **`Application::initialize` is 587 lines** (`:105-691`), confirmed at my tip
  against the §Optional Batch-G list's 569. It is 62% of the surviving
  `application.cpp` and sets that TU's floor: no further split of this file is
  worth doing until `initialize` is decomposed. Out of scope here (move-only).
- **The T104/T49 deleted-method tombstone comment block at `1969-1990`**
  (22 lines) rode into `application_zone_loading.cpp` verbatim, as the range
  required. Same shape as J02's `eq_ucs.cpp` leftover: commented-out history
  that git already keeps.
- **`tests/` carries 13 references to `application.cpp`, ALL of them comments —
  and 6 of them cite line numbers that this split has now invalidated.**
  `grep -rn 'application\.cpp' tests/ scripts/` (the J11 correction-10 check)
  found **zero functional readers**: no test opens this file as text, unlike
  `irrlicht_renderer.cpp`'s four guards. So **nothing joined my territory and no
  test needed repointing.** But the stale citations are real doc rot —
  `tests/test_first_person_tunables.cpp` alone names `application.cpp:1231`
  (twice), `:1162` and `:395`. Of those, `:395` still resolves inside the
  stays-TU; `:1162` and `:1231` are now in `application_commands.cpp` at
  `:275` and `:344`. Also `tests/test_headless_command_refusal.cpp:103` tells a
  future reader to "grep application.cpp", which will now miss.
  **A comment-only follow-up, outside move-only scope. Recommend folding into
  the same hygiene pass as the `docs/module_map.md` debt.**
- `docs/module_map.md` needs entries for the four new TUs, and `docs/future/
  ui_fixes.md` cites `application.cpp` ~lines 438/462 — both now stale. Flagged
  follow-up, explicitly outside this unit's territory.

### 7. What the template still gets wrong

1. **The verifier silently no-ops on an uncommitted tree** (see §4). `No C++
   files changed` + exit 0 is indistinguishable from a pass at a glance. This is
   a *third* failure mode with the same shape as the `--repo` and base-rev traps
   the corrections already document, and the acceptance section should say
   "commit first".
2. **Correction 12 says line estimates err in both directions; for J06 every
   one was off by exactly +1 in the same direction**, because the estimator
   counted `61 + body` and the real file is `61 + body + 1`. Trivial, but the
   plan's own arithmetic identity (`61 + … + 1 = 2368`) already encodes the
   fix — the per-row `≈` column just didn't use it.
3. **Correction 6's "expect to call `build-arm-noble.sh` twice" did not hold**
   (one call, exit 0), and neither did J02's 2.3x `ctest` stretch. The
   scheduling numbers in §Scheduling are a *worst case observed*, not a
   forecast; wording them as "expect" makes a green run look anomalous.
