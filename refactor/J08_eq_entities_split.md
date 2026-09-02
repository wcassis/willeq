# J08 — Batch J Wave 3: split eq_entities.cpp (2,786 → 4 TUs, largest ~991) and relocate the raid family into eq_group.cpp

**Slot: assigned at dispatch. Branch `task/J08-eq-entities-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 3 of `refactor/J_file_size_reclamation.md`, running alongside **J09** and
**J10**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND all six correction sections** (eighteen numbered lessons
from J01–J07 and J11; newer text wins over older), then
`refactor/J07_eq_player_info_split.md` **including its Review** (the closest
template to this unit — same file family, same shape) and
`refactor/J05_wld_loader_split.md` §Review, plus `refactor/EXECUTOR_PROTOCOL.md`
and `refactor/F11_entities.md` / `refactor/F12_group.md` (the units that created
these two files out of `eq.cpp`).

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## What makes this unit different from every other unit in Batch J

It is the only one that **moves code INTO an existing, unrelated file.** Two
ranges — the raid handler and the raid sender — leave `eq_entities.cpp` and land
in `src/client/eq_group.cpp`, which is **760 lines** today and whose own header
comment already calls itself the "group/**raid**/pet cluster". `docs/module_map.md`
likewise already lists `eq_group.cpp` as holding "raid (T169–T184: OP_RaidUpdate
decode, raid senders)" — **the documentation is already describing the layout
this unit creates.** Today it is wrong; after this unit it is right.

Two consequences the other units do not have:

1. **`eq_group.cpp` needs includes it does not currently have.** The
   §Process "carry the original's full include block verbatim" rule is written
   for *newly created* TUs and does **not** apply to an existing destination
   file — you must not paste `eq_entities.cpp`'s 60-line preamble on top of
   `eq_group.cpp`. See §Includes below for the exact, minimal, enumerated set.
2. **Both content proofs must account for the relocation.** The
   re-concatenation is of *seven* ranges across *five* files, not four ranges
   across four. See §Acceptance.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to `include/client/eq.h`. **No
method decomposition.** No drive-by fixes. This file is full of tempting ones —
a 42-line commented-out debug block, several T-unit tombstones, `#ifdef`-guarded
audio calls. **Leave every one of them exactly where it is.** Record them in the
Review; do not act.

## Measured at `f215651a` (2026-09-02, `fable` tip after J07 merged)

⚠ **The batch survey says 2,736 and 161 relocated lines. BOTH ARE STALE.**
T318 (`2fe75e35`, "publish the size the server already sends") grew the file to
**2,786**, and the 161 figure counted only `ZoneProcessRaidUpdate`'s body —
excluding its own 24-line explanatory comment block and excluding
`SendRaidCommand` entirely. **The real relocation is 209 lines.** Every range
below was read out of the tree at `f215651a`. **Re-verify against your own
branch tip before you cut** and report drift in your Review rather than silently
adjusting.

- `src/client/eq_entities.cpp` — **2,786 lines**
- `src/client/eq_group.cpp` — **760 lines** (→ ~971 after the relocation)

**Preamble is lines 1-72**: a **40-line** file-header comment (1-40, F11's
cluster enumeration plus four `NOTE:` paragraphs about what deliberately stayed
in `eq.cpp`), a blank at **41**, then the include block **42-72**, which
contains a `#ifdef WITH_AUDIO`
group at **63-66** and a 3-line explanatory comment at 57-59. Line **73 is
blank**; the body starts at **74**. Then `namespace`: **there is none** —
`EverQuest` is in the global namespace, so there is no namespace open and no
namespace close to replicate. Line 2786 is the closing `}` of `SendRaidCommand`.

Verified properties at `f215651a`:

- **Four file-local `static`s**, all in two clusters that this plan keeps
  together with their only callers: `EXP_FOR_LEVEL` (76), `MAX_EXP_LEVEL` (143)
  and `CalculateExpProgress` (146) — used only by `ZoneProcessExpUpdate`
  (1296-1322); and `getDoorTypeFromName` (2425) — used only by
  `ZoneProcessMoveDoor` (2466-2528). ⚠ **Confirm both single-consumer claims
  with your own grep before cutting.** If either is wrong, **STOP and report** —
  do not reach for an internal header without saying so first.
- **Zero anonymous namespaces.**
- **Exactly one block comment**, `185`–`226`: a 42-line commented-out
  packet-hex-dump debug block inside `ZoneProcessZoneSpawns`. It is **interior
  to a single cluster** and no cut goes near it — but it is exactly the J02 trap
  (a survey range that cut inside `/* */` and would not have compiled), so
  **verify comment depth 0 at every cut** anyway.
- **Two `// ====` banners only**, at 2614-2616 ("World Object / Tradeskill
  Methods"). §Process's banner rule therefore cannot decide the cuts here:
  **use J03's rule instead** (correction 11) — *cut at a blank line whose
  predecessor closes a method, and verify comment depth 0 at the cut.* Every
  boundary below was chosen that way and each is stated with the line above and
  below.
- **One CMake add-site** for each of the two files.
- **Zero functional readers in `tests/` or `scripts/`** — see §Territory.

## The split

| Range | Content | Target | ≈ lines |
|---|---|---|---|
| 1-72 | preamble (40-line header comment, blank, include block 42-72) | copied to each new TU | 72 |
| **74-992** | `EXP_FOR_LEVEL` / `MAX_EXP_LEVEL` / `CalculateExpProgress` (74-165); `ZoneProcessZoneSpawns` (167-510, contains the 185-226 block comment); `ZoneProcessTimeOfDay` (512-543); `ZoneProcessSpawnDoor` (545-622); `ZoneProcessSendZonepoints` (624-696); `ZoneProcessSpawnAppearance` (698-991) | **stays** `eq_entities.cpp` | **~991** |
| **993-1323** | `ZoneProcessGroundSpawn` (993-1046); `ZoneProcessWeather` (1048-1078); `ZoneProcessNewSpawn` (1080-1294); `ZoneProcessExpUpdate` (1296-1322) | `eq_entities_updates.cpp` (part 1 of 2) | 331 |
| **1324-1509** | ⚠ **RELOCATES.** The T169/T179/T180 comment block (1324-1347) + `ZoneProcessRaidUpdate` (1348-1508) | → **`eq_group.cpp`** | 186 |
| **1510-1809** | `ZoneProcessClientUpdate` (1510-1808) | `eq_entities_updates.cpp` (part 2 of 2) | 300 |
| **1810-2422** | `ZoneProcessDeleteSpawn` (1810-1910); `ZoneProcessMobHealth` (1912-1997); `ZoneProcessHPUpdate` (1999-2073); `FindEntityByName` (2075-2094); `ListEntities` (2096-2169); `ZoneProcessWearChange` (2171-2227); T221 comment + `ApplyAppearanceToGameState` (2229-2255); `UpdatePlayerAppearanceFromInventory` (2257-2338); `ZoneProcessIllusion` (2340-2421) | `eq_entities_appearance.cpp` | ~686 |
| **2423-2763** | door-type comment + `#ifdef WITH_AUDIO` + `getDoorTypeFromName` (2423-2464); `ZoneProcessMoveDoor` (2466-2528); `SendSpawnAppearance` (2530-2551); `SendAnimation` (2553-2573); `SendClickDoor` (2575-2612); the **"World Object / Tradeskill Methods" banner (2614-2616)**; `SendClickObject` (2618-2645); `SendCloseContainer` (2647-2684); `GetWorldObject` (2686-2693); `ClearWorldObjects` (2695-2703); `ZoneProcessClickObjectAction` (2705-2762) | `eq_entities_world.cpp` | ~414 |
| **2764-2786** | ⚠ **RELOCATES.** The T179 comment block (2764-2767) + `SendRaidCommand` (2768-2786) | → **`eq_group.cpp`** | 23 |

**Result: 4 TUs** (`eq_entities.cpp` stays and counts, as J01's did) **plus 209
relocated lines in `eq_group.cpp`.**

Arithmetic that must hold:
`72 + 1 (blank@73) + 919 + 331 + 186 + 300 + 613 + 341 + 23 = 2786`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

⚠ **`eq_entities_updates.cpp` takes TWO non-adjacent ranges** (993-1323 and
1510-1809) because the raid handler sits between them and leaves the file. Paste
them **in original order**, separated by exactly the one blank line that the
convention already gives each range's tail. This is deliberate and is not a
sign that the cut is wrong.

### Projected sizes

| TU | Ranges | body | + preamble | ≈ total |
|---|---|---|---|---|
| `eq_entities.cpp` (stays) | 74-992 | 919 | 72 | **~991** |
| `eq_entities_updates.cpp` | 993-1323, 1510-1809 | 631 | 72 | **~703** |
| `eq_entities_appearance.cpp` | 1810-2422 | 613 | 72 | **~685** |
| `eq_entities_world.cpp` | 2423-2763 | 341 | 72 | **~413** |
| `eq_group.cpp` | +1324-1509, +2764-2786 | +209 | +includes | **~971** |

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low, J07 ran exactly 3 high). Cluster membership has
been right every time; the arithmetic has not. Treat these as indicative and
report the real counts.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is J03's: **a blank line whose predecessor closes a method, comment
depth 0 at the cut.** Checked at `f215651a`:

| Cut | line-1 | line (blank) | line (first of next range) |
|---|---|---|---|
| 993 | `991: }` | `992` | `993: void EverQuest::ZoneProcessGroundSpawn(...)` |
| 1324 | `1322: }` | `1323` | `1324: // T169: every raid membership event arrives here, ...` |
| 1510 | `1508: }` | `1509` | `1510: void EverQuest::ZoneProcessClientUpdate(...)` |
| 1810 | `1808: }` | `1809` | `1810: void EverQuest::ZoneProcessDeleteSpawn(...)` |
| 2423 | `2421: }` | `2422` | `2423: // Helper function to determine door type from door name` |
| 2764 | `2762: }` | `2763` | `2764: // T179: send an OP_RaidCommand (Titanium wire opcode OP_RaidInvite, ...` |

Two of the six start at a **comment block that belongs to the method below it**
(1324 and 2764 — both raid; 2423 — the door helper). Take the comment with the
code: §Process's banner rule generalises to "the leading comment travels with
its cluster", and both raid comments are the T169/T179/T180 archaeology that is
the only reason those handlers are readable.

## ⚠ HAZARD 1 — the preprocessor. Eleven `#ifdef` regions, none at a cut.

The J02 lesson applies here as the preprocessor. **Run this and paste it:**

```bash
awk 'NR>=73 && /^[ \t]*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/eq_entities.cpp
```

At `f215651a` that yields exactly eleven pairs below the preamble:

| pair | guard | inside range |
|---|---|---|
| 397 / 410 | `EQT_HAS_GRAPHICS` | 74-992 (`ZoneProcessZoneSpawns`) |
| 536 / 539 | `WITH_AUDIO` | 74-992 (`ZoneProcessTimeOfDay`) |
| 1261 / 1269 | `EQT_HAS_GRAPHICS` | 993-1323 (`ZoneProcessNewSpawn`) |
| 1773 / 1775 | `EQT_HAS_GRAPHICS` | 1510-1809 (`ZoneProcessClientUpdate`) |
| 1803 / 1805 | `EQT_HAS_GRAPHICS` | 1510-1809 (`ZoneProcessClientUpdate`) |
| 1900 / 1902 | `EQT_HAS_GRAPHICS` | 1810-2422 (`ZoneProcessDeleteSpawn`) |
| 2424 / 2464 | `WITH_AUDIO` | 2423-2763 (wraps `getDoorTypeFromName` **entirely**) |
| 2497 / 2512 | `WITH_AUDIO` | 2423-2763 (`ZoneProcessMoveDoor`) |

(The remaining `63 / 66` pair is in the preamble's include block.)
**None of the six interior cut points lands inside a conditional.** Verify that
is still true at your tip.

⚠ **Note `2424/2464`: `getDoorTypeFromName` exists ONLY when `WITH_AUDIO` is
defined.** It is a `static` with a single caller inside the same guard region in
`ZoneProcessMoveDoor` (2497-2512). Both are inside range 2423-2763, so nothing
needs promoting — **but if you were tempted to move the door handler and the
helper apart, don't: that would need an internal header AND the header would
have to replicate the guard.**

## ⚠ HAZARD 2 — the 42-line block comment at 185-226

`ZoneProcessZoneSpawns` contains a commented-out packet-hex-dump debug block,
opened by a `/*` on line **185** (with trailing whitespace) and closed by `*/`
on **226**. It is interior to the stays range and no cut approaches it. Confirm
with:

```bash
grep -n '/\*\|\*/' src/client/eq_entities.cpp
```

which must return exactly those two lines. **If it returns more, a range may
straddle a comment — stop and re-derive.** Record the block in the Review as a
dead-content finding (see §Review item 7); **do not delete it.**

## ⚠ HAZARD 3 — the file is TAB-INDENTED

Whatever you cut with must not re-indent. `sed -n 'A,Bp'` is safe; anything that
reformats is not. `eq_group.cpp` is also tab-indented, so the relocated ranges
paste in cleanly.

## Includes

**The three new `eq_entities_*.cpp` TUs each carry `eq_entities.cpp`'s full
include block, lines 42-72, verbatim** — including the 3-line P28 comment at
57-59, the `#ifdef WITH_AUDIO` group at 63-66 and the interleaved blank lines.
**Not "the includes it actually needs":** §Process is explicit, trimming per-TU
include sets is a second and riskier change wearing a move-only commit's
clothes, and it removes a judgement call from the executor. **There is no
namespace to open or close.**

**`eq_group.cpp` is the exception, and it is a NARROW, ENUMERATED one.**
Do not paste `eq_entities.cpp`'s preamble on top of it. `eq_group.cpp`'s current
include block is:

```
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <cstring>
```

The relocated code references, beyond what those already supply:
`EQT::DecodeRaidUpdate`, `EQT::RaidUpdate`, `EQT::RaidAction*`,
`EQT::RaidActionName`, `EQT::encodeRaidCommand` — **all declared in
`include/client/raid_update.h`** — plus `std::vector`. So:

- **Add exactly `#include "client/raid_update.h"`** to `eq_group.cpp`, beside
  the existing `client/` includes.
- Add `#include <vector>` **only if the build actually fails without it**
  (it is very likely already transitive through `eq.h`). If you add it, say so.

### Where in `eq_group.cpp` the two ranges go

`eq_group.cpp`'s structure at `f215651a`: header comment **1-13**, blank 14,
includes **15-18**, blank, `#include <cstring>` **20**, blank, the
`// ==== Group Methods ====` banner **22-24**, blank, body **26-760**
(last line 760 is the closing `}` of `ZoneProcessGroupCancelInvite`).

**Append both relocated ranges at the END of the file, in their original
relative order** — `1324-1509` first, then `2764-2786` — each preceded by one
blank line, verbatim and tab-indented as they came. You may add **one**
`// ==== Raid Methods ====` three-line banner ahead of them, matching the file's
existing banner style; if you do, enumerate those three lines with the header
changes in your Review. Do not interleave them among the group handlers and do
not reorder them.

⚠ **Enumerate every include line you added to `eq_group.cpp` in your Review.**
They are allowlisted boilerplate for `verify_move_only.py` but they **do** show
up in the line-multiset check. **Add nothing "for symmetry"** — correction 10's
corollary: an exception list is measured symbol by symbol.

## File-header comments — the one thing you may adapt

`eq_entities.cpp`'s 40-line header (1-40) enumerates F11's clusters and carries
four `NOTE:` paragraphs. Each new TU gets its own 5-7 line header naming *its*
clusters and citing `refactor/J08_eq_entities_split.md` and
`refactor/F11_entities.md`; the stays file's header is trimmed to its own
clusters, **keeping every `NOTE:` paragraph that is still true of the file it
sits in** (they explain what deliberately stayed in `eq.cpp` — that is
documentation, not dead weight; see the batch plan's dead-content section).

`eq_group.cpp`'s 13-line header (1-13) already says "group/raid/pet cluster" and
already mentions the "raid invite stub". **Amend it in place** to say that
`ZoneProcessRaidUpdate` and `SendRaidCommand` arrived here from
`eq_entities.cpp` via J08, and cite this plan. **Enumerate every header line you
wrote or changed, in all five files, in your Review** — exactly as J07 did.
Change no other comment.

## CMake — grep, never trust a line number

```bash
grep -n 'eq_entities\.cpp\|eq_group\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `f215651a` that is **exactly two sites, both in the root file, both inside
`SET(CLIENT_CORE_SOURCES` (which opens at `:265`)**:

```
CMakeLists.txt:273:    src/client/eq_entities.cpp
CMakeLists.txt:290:    src/client/eq_group.cpp
```

`tests/CMakeLists.txt` names `eq_entities.cpp` only inside a **comment**
(`:1782`, "T18: entity-domain zone-stream tests") and should need **no change**;
`eq_group.cpp` appears nowhere in it. **Grep anyway** — line numbers in this
2,000-line file rot within hours. ⚠ **The variable is `CLIENT_CORE_SOURCES`,
not `CORE_SOURCES`** (J02's correction 4). ⚠ **CMake commands in this project
are UPPERCASE** — `grep -n 'set('` and `grep -n 'add_executable'` both return
nothing, silently, which reads exactly like "no sites here". **Grep for the
filename.**

Add the three new TUs beside `eq_entities.cpp`, keeping the `eq_*.cpp` run
alphabetically coherent with its neighbours. `eq_group.cpp` gains no new source
line — it only gains content.

⚠ **CMake add-site counts have ranged from 1 to 19 across this batch** and the
shape of the file gives no hint. **Report the count you actually found.**

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- ⚠ **After ANY exit-143 (SIGTERM) build, scan every produced binary for valid
  ELF magic BEFORE trusting the rebuild.** J07 hit this at scale: the timed-out
  build left **twelve binaries truncated mid-link**, the re-invocation **exited
  0**, and `ctest` then died at *discovery* — reporting only ONE bad binary per
  full suite run. Find them all at once:
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' ')" = "7f454c46" ] || echo "BAD $f"
  done
  ```
  then `rm` every hit and rebuild once. This is now also in
  `refactor/EXECUTOR_PROTOCOL.md`.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it.** If it fails in a wave run,
    re-run it alone before reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (J05's correction 16): check the
    binary with `--gtest_list_tests` before concluding a mid-link truncation.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer from
  this script, and two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd` and agent threads reset
     cwd between calls). Omitting it silently verifies
     `/mnt/projects/fable/main` and prints a loud FAIL full of some unrelated
     merged unit's lines. J02 hit exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG**: three units merge
     during a wave, so `fable` moves under you and `fable..HEAD` reports the
     *other* units' merged work, in reverse, as substantive removals. J01 saw
     533 spurious removals this way. Use the fork point.
     ⚠ **Failures 1 and 2 look IDENTICAL** — another unit's work appearing as
     removals — and have different causes. An executor who "fixes" the base and
     re-runs without `--repo` is still verifying the wrong tree. **Check both
     before changing a single line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one. **Commit first, then verify.**
  Record `$BASE` in the Review and paste the full output.
- **Expected verifier result for J08: exit 0.** This unit promotes nothing, adds
  no internal header, and `eq_entities.cpp`'s preamble is includes and comments
  only — nothing in it is outside `verify_move_only.py`'s `BOILERPLATE`
  allowlist (`#include`, `#pragma`, `#if*`, `#else`, `#endif`, `#define`,
  comments, `namespace … {`, `}`). ⚠ **But correction 9 is general: the
  verifier exits 1 by construction wherever a copied preamble carries anything
  beyond includes/pragmas/namespaces/comments** — J11 got 70 such lines from 12
  `extern` declarations plus a one-line stub. If yours exits 1, **paste the full
  output, enumerate every flagged line, and request orchestrator sign-off in the
  Review. NEVER contort code to force a green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences.
  ⚠ **This unit's `after` set must include `eq_group.cpp`'s tail** — a naive
  "concatenate the new `eq_entities_*` bodies" **silently under-counts by 209
  lines and still looks clean** (correction 7, generalised).
  ```bash
  # before: the pre-split bodies of BOTH files
  { git show "$BASE:src/client/eq_entities.cpp" | sed -n '73,2786p'
    git show "$BASE:src/client/eq_group.cpp"    | sed -n '21,760p'; } \
    | sort > /tmp/before.txt
  # after: the four eq_entities* bodies (each stripped of its new header and of
  # the copied 31-line include block) + eq_group.cpp's body, MINUS the include
  # lines you added to eq_group.cpp — count those explicitly instead.
  cat <the five stripped bodies> | sort > /tmp/after.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  seven extracted ranges **in original order** (`74-992, 993-1323, 1324-1509,
  1510-1809, 1810-2422, 2423-2763, 2764-2786`) from wherever each now lives, and
  confirm the result is byte-identical to
  `git show "$BASE:src/client/eq_entities.cpp" | sed -n '74,2786p'`. **2,713
  lines.** Paste the `cmp` exit status.
  ⚠ **Declare count variables `local` in your proof helpers** (correction 17). A
  non-local `n` in J05's `body()` helper truncated one range and produced a
  247-line shortfall **that read exactly like a dropped range.** When a proof
  fails, suspect the proof first.
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed.** J05's first run failed on `befallen` **p95 CPU 11.684 vs a 10.948
    ceiling with ZERO image mismatches**; the re-run measured 4.676. That is
    wave contention, not your change: **re-run.** ⚠ **A missed CMake site for
    `gles2_zone_benchmark` fails at the RENDER step instead** — different
    failure, different cause. J08 has no `gles2_zone_benchmark` site, so a
    render-step failure here is a real finding.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ `build-arm-noble.sh` can blow the 600 s Bash-tool ceiling under wave
  concurrency — SIGTERM, **exit 143**, typically mid-`gles2_zone_benchmark`.
  **Exit 143 means RE-INVOKE, not breakage** — and then run the ELF scan above.
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt` (~2,004) and
  `tests/CMakeLists.txt` (~2,850) both WARN; **both are pre-existing and neither
  is yours** (§Process, and J20).
- ⚠ **`chmod 644` every created file before `git add`.** A scripted `sed >` pass
  produces mode 755 and records a spurious `100644 => 100755`; verify with
  `git ls-files -s` that all new files are `100644`.

⚠ **Timing figures anywhere in the batch plan are WORST CASES OBSERVED, NOT
FORECASTS** (correction 14). J06's `ctest -j6` ran in 120.7 s — faster than
J01's solo run on a smaller suite — and its `build-arm-noble.sh` finished first
time. Treat the stretch figures as "may happen".

## Review must answer

1. **Line count of `eq_entities.cpp` and `eq_group.cpp` at your tip**, against
   the 2,786 / 760 this plan measured, and any range drift.
2. **The two single-consumer claims, with your greps pasted**: that
   `EXP_FOR_LEVEL`/`MAX_EXP_LEVEL`/`CalculateExpProgress` are read only by
   `ZoneProcessExpUpdate`, and `getDoorTypeFromName` only by
   `ZoneProcessMoveDoor`. If either is wrong, that is a finding the batch needs
   and the unit's shape changes.
3. **The preprocessor boundary check** on all six interior cut points, pasted,
   plus the `grep -n '/\*\|\*/'` showing exactly 185 and 226. Any range you had
   to move, and why.
4. **Every include line added to `eq_group.cpp`**, enumerated — and confirmation
   that you did **not** paste `eq_entities.cpp`'s preamble onto it.
5. **Every rewritten or amended file-header comment line, across all five
   files**, enumerated as the unit's only non-move changes. Nothing else may
   appear in that list.
6. **How many CMake add-sites you actually found**, per file, against the two
   this plan names.
7. **Both content proofs**, pasted, with the `eq_group.cpp` contribution
   explicitly counted in both. State the verifier's exit code and `$BASE`.
8. Final line counts of all five files, wall clock and slowest step, and whether
   concurrency with J09/J10 stretched the verification tail.
9. **Findings only, not fixes:**
   - the **42-line commented-out debug block at 185-226** — this is the batch's
     sixth dead-content site and the first one that is commented-out *code*
     inside a live method. Record it against the batch's pending dead-content
     decision (see `refactor/J_file_size_reclamation.md` §The dead-content
     decision).
   - the four `NOTE:` paragraphs in the file header and the T169/T179/T180/T221
     archaeology comments — these are the *other* kind: paragraphs explaining a
     deliberate absence or a corrected misreading. **They are documentation and
     should survive.** Confirm they did.
   - the **twelve `tests/` comment citations of `eq_entities.cpp`**, five of
     which cite line numbers this split invalidates (`test_zone_connection_flow.cpp`
     `:101`, `:347`, `:349`, `:355`, `:410`, `:419`, `:981`; `test_spell_manager.cpp:95`).
     Note that **several were already stale before this split** — check and say
     which. Comment-only, outside a move-only commit's scope; they belong in the
     `docs/module_map.md` hygiene pass J02/J04/J06/J07 already owe.
   - **`docs/module_map.md:23` lists `eq_entities.cpp` at 2,641** (it is 2,786)
     and **`:33` lists `eq_group.cpp` at 595** (it is 760) **and already
     attributes raid to `eq_group.cpp`.** This unit makes the second claim true.
     Record; the row updates belong to the hygiene pass.
   - anything that looks unreachable. **Record. Do not act.**

## Territory

`src/client/eq_entities.cpp`, the three new `src/client/eq_entities_*.cpp` TUs
beside it, `src/client/eq_group.cpp`, `CMakeLists.txt`, and this file.
**Nothing else** — not `include/client/eq.h`, not `include/client/raid_update.h`,
not `src/client/raid_update.cpp`, not any of the other 20 `eq_*.cpp` domain TUs,
not any `tests/*.cpp`, not `tests/CMakeLists.txt` (verify it needs nothing), not
`docs/module_map.md`.

**Correction 10 pre-check, already run for you and to be re-run at your tip:**

```bash
grep -rn 'eq_entities' tests/ scripts/
grep -rn 'eq_group'    tests/ scripts/
```

At `f215651a` this returns **twelve hits for `eq_entities` and one for
`eq_group`, ALL of them comments — ZERO functional readers.** No test reads
either file as *text*, so unlike J11 (four text-scanning tests, three of which
would have failed and one of which would have silently asserted nothing) **no
repoint is needed.** ⚠ **Re-run it anyway before cutting** and report what you
find; this is now a standing pre-cut step, not an optional one.

⚠ **Naming collision to avoid:** `src/client/eq_entity_projection.cpp` and
`src/client/eq_state_entities.cpp`-style names may exist in the `eq_*` family —
`grep -l 'eq_ent' src/client/*.cpp` before naming anything. The three TUs here
are `eq_entities_updates.cpp`, `eq_entities_appearance.cpp` and
`eq_entities_world.cpp`.

Concurrent at dispatch: **J09** (`src/client/graphics/zone_shader.cpp` + new
private headers, `CMakeLists.txt`) and **J10**
(`src/client/graphics/constrained_texture_cache.cpp` + new TUs,
`CMakeLists.txt`, `tests/CMakeLists.txt`). No source overlap with either.
⚠ **`CMakeLists.txt` is shared with both** — which is why **merges are
serialized one at a time with a rebase.**
