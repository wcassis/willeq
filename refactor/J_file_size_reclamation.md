# Batch J — File-size reclamation: get every source file under the 1,500-line soft limit

**Opened 2026-09-02. Goal: no production source file over the 1,500-line warn
ceiling, so that any single file fits comfortably in a coding agent's context.**

Batch letter J because E, F, G, H, P, Q, R, S2 and T are all taken — **H is the
harness batch** (`H_harness.md`, H01-H05), not this.

## Where the tree stands

Measured at `9ae1460c`: **31 files over 1,500 lines; 4 over the 3,000 hard cap** —
`src/client/eq.cpp` 3,913 (**3,923 at `2eb34f3d`, 2026-09-02 — it is still
growing; re-measure at your own tip**), `src/client/graphics/entity_renderer.cpp` 3,255,
`src/client/graphics/irrlicht_renderer.cpp` 3,237,
`src/client/action/command_processor.cpp` 3,024. Batch F left `eq.cpp` at 3,629
and `irrlicht_renderer.cpp` at 2,736 in 2026-07; both grew back, and
`command_processor.cpp` crossed the hard cap after F/G shipped. T166 already
re-split one file (`renderer_zone_load.cpp`) for exactly this reason.

## The headline finding — this is CHEAPER than it looks

**No file in this tree is decomposition-first for the purpose of the ceiling.**
The largest method anywhere in the oversize set is
`EverQuest::DispatchZonePacket` (`src/client/eq.cpp:800-1679`, 880 lines,
verified), and the largest preamble is 97 lines
(`irrlicht_renderer.cpp`). Since a TU's floor is `preamble + largest method`,
**every oversize .cpp clears 1,500 by pure Batch-F move-only relocation.**
Batch G's shape — restructuring method bodies into helpers — is **not required
anywhere in this batch**, and units must not do it opportunistically.

The eight methods that dominate their TUs are listed in §Optional below as a
SEPARATE, OPTIONAL goal. Do not fold them in.

## The one genuinely hard problem: headers, and it is a god-class problem

Four headers are over the soft limit, in two different shapes. ⚠ **All four
GREW between the survey and `136a319b` — `eq.h` by 107 lines and
`irrlicht_renderer.h` by 125 — so every "mechanical to ~N" figure below is
optimistic by that much. Re-measure at the unit's own tip.**

| header | lines | shape | verdict |
|---|---|---|---|
| `include/common/packet_structs.h` | 2,207 → **2,215** | 125 independent packed structs, already banner-sectioned | **mechanical**, clears fully |
| `include/client/action/action_dispatcher.h` | 1,556 → **1,581** | 3 independent entities (types / `IActionHandler` / `ActionDispatcher`) | **mechanical**, clears fully |
| `include/client/graphics/irrlicht_renderer.h` | 2,395 → **2,520** | 20 free types + one god class | mechanical to ~1,820, then **design change** |
| `include/client/eq.h` | 2,538 → **2,645** | 504 lines of enums + 3 structs + `class EverQuest` (897 declarations) | mechanical to ~1,930, then **design change** |

Extracting every free-standing type from `eq.h` still leaves `class EverQuest`
itself at ~1,811 lines. Nothing mechanical closes that gap — only pimpl'ing the
private data block or extracting owned components does, and that turns the risk
from "a compile error" into "a subtle lifetime bug".

### PROPOSED POLICY AMENDMENT — needs Will's sign-off before J14-J17 land

`docs/decisions.md` is the record of Will's decisions, so this batch RECOMMENDS
and does not self-apply:

1. **Multi-type headers** (more than one top-level class/struct/enum defined —
   mechanically decidable) keep the existing 1,500 warn / 3,000 fail unchanged.
2. **Single-class headers** gate on GROWTH, not absolute size: add a `--no-grow`
   mode to `scripts/refactor/check_file_sizes.sh` that fails a changed header
   only if it grew while already over 1,500.
   *Why:* the checker gates files a unit CHANGES. A flat FAIL would fire on the
   next unit that adds one method to `EverQuest`, forcing it to either perform
   an unrelated class decomposition or compress code to fit — which is exactly
   the harm T115 recorded (a crash fix squeezed onto a shared line) and T166
   exists to prevent. An unsatisfiable rule gets waived, and a waived rule
   gates nothing.
3. **Take the free lines anyway** (J16/J17), then grandfather `eq.h` (~1,930)
   and `irrlicht_renderer.h` (~1,820) with a `deferred.md` entry naming the
   class decomposition as the remaining debt.
4. **Do NOT attempt the `EverQuest` / `IrrlichtRenderer` decomposition in this
   batch.** It is a design question for Will, not a mechanical sweep.

Rationale for gating headers at all rather than exempting them: they are what an
agent reads MOST. `eq.h` has 65 includers, `packet_structs.h` 41,
`irrlicht_renderer.h` 39 — `eq.h` alone is ~30k tokens pulled into nearly every
client-side task, a larger recurring context cost than any .cpp in the tree.

## Process per unit — Batch F, plus T166's bar, plus J01's corrections

**Everything in this section marked ⚠ was corrected after the J01 pilot ran
(2026-09-02, `refactor/J01_inventory_manager_split.md` §Review). Units written
before that date carry the old, wrong text — this section wins.**

1. Orchestrator writes `refactor/J##_<name>.md`: the cluster table with line
   ranges, target TU names, and the named hazards.
2. Executor in an assigned slot, branch `task/J##-<name>` cut from `fable`,
   per `refactor/EXECUTOR_PROTOCOL.md`. **Execute the plan exactly. No scope
   creep, no drive-by fixes, no method decomposition.**
3. Acceptance, every unit:
   - build clean (by EXIT CODE, never by grepping the log — `grep ' error '`
     never matches GCC, which prints `error:`)
   - full `ctest -j6` (**NOT -j24** — the visual gate's CPU ceiling flakes)
   - ⚠ **`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable
     HEAD)..HEAD"`** — see the ⚠ box below. **NOT `fable..HEAD`.**
   - ⚠ the **line-multiset check** (below) as a named second proof
   - visual gate PASS with **goldens untouched** (a re-bless here means the
     split changed behaviour — STOP and report)
   - `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh`
   - `check_file_sizes.sh --files` on every changed AND created file: 0 warn,
     0 fail. **A split that leaves a 2,400-line file is not done.**
4. Any file-local `static` or anonymous-namespace helper that must become
   reachable from a second TU goes to a private internal header next to the
   sources and is declared in the unit's Review as the sanctioned Batch-F
   exception (T166's `wld_internal.h` precedent). J04 and J05 both need one.

### ⚠ The verifier's base revision — read this before you run it

**`fable..HEAD` is WRONG for this batch and will produce a loud, spurious
FAIL that looks like your own breakage.** Waves 1-4 run three units at once
and merge as they land, so `fable` moves under at least two of every three
units. `fable..HEAD` diffs against the *current* tip of `fable`, not your
fork point — so it reports every OTHER unit's merged work, **in reverse**, as
substantive removals. J01 saw exactly this: 533 removed lines and 18 added,
none of them its own, because slot3's work merged twenty minutes into the run.

Use the fork point:

```bash
BASE=$(git merge-base fable HEAD)     # or the literal hash named at dispatch
python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"
```

Record `$BASE` in the Review. If the verifier flags files your unit never
touched, **you have the wrong base — do not start "fixing" anything.**

### ⚠ The line-multiset check — the cheap second proof

`verify_move_only.py` compares *distinct* normalized lines, so it cannot see a
range that was duplicated into two TUs or dropped from both. Thirty seconds of
`sort`/`diff` can:

```bash
# bodies of the split files (each stripped of its preamble and namespace
# close) vs the pre-split original's body, as multisets
cat <new TU bodies> | sort > /tmp/after.txt
git show "$BASE:<original path>" | sed -n '<first body line>,<last body line>p' | sort > /tmp/before.txt
diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
```

Stronger still, and also cheap: re-concatenate the extracted ranges in their
original order and confirm the result is **byte-identical** to the original
body. Paste both results in the Review. J01 did both.

### ⚠ CMake: grep, never trust a line number

**Do not promise or trust a line number, and do not assume two add-sites.**
The plan's old text said "explicit source lists at `:266` and `:456`"; at J01's
tip `inventory_manager.cpp` appeared **exactly once**, at `CMakeLists.txt:319`
in `CORE_SOURCES`. Line numbers in a 1,899-line file rot within **hours**, not
days: J01's own merge moved `entity_renderer.cpp` from `CMakeLists.txt:458` to
`:463` between this batch plan's survey and J03's, the same afternoon. Find the
sites yourself:

```bash
grep -n '<original file basename>' CMakeLists.txt tests/CMakeLists.txt
```

`CMakeLists.txt` uses explicit source lists, no globs, in several variables
(`CORE_SOURCES`, `GRAPHICS_SOURCES`, …) — add each new TU beside the original
in **every** list that names it. **And check `tests/CMakeLists.txt` too:** J01
needed nothing there (its tests link `willeq_core`), but J04's file is listed
at four separate per-target source sites. Merge units ONE AT A TIME and rebase.

### ⚠ Three more standing rules from the pilot

- **Section banners travel with their cluster.** Give every range as the
  **first line of the cluster's leading `// ====` comment**, not the first
  statement, so no TU is left with an orphaned header comment and every
  executor makes the same call.
- **Each new TU carries the original's full include block, verbatim.** Not
  "the includes it actually needs". This is what T166 did; trimming per-TU
  include sets is a second, riskier change wearing a move-only commit's
  clothes, and it removes a judgement call from seventeen executors.
- **`chmod 644` every created file before `git add`.** J01's `sed >` pass
  produced mode 755 and the first commit recorded a spurious
  `100644 => 100755`; it was caught and amended, but it will recur.

### Scheduling: what a unit actually costs (measured, J01)

**~30 min wall clock end to end**, single agent, no retries: ~8 min reading the
contract docs and surveying, **under 2 min for the cut itself and CMake**, and
**~18 min for the verification bar** — `ctest -j6` 3.0 min, `build-arm-noble.sh`
~5 min, `build-gles2-x86.sh` ~4 min, `cmake --build -j24` ~4 min.

**The mechanical work is nearly free; a unit costs what the bar costs, and
that cost is fixed per unit regardless of the file's size.** Do not schedule
three concurrent units as three independent 30-minute jobs: `ctest -j6` on
three slots at once is 18 test processes plus three visual gates, all
contending, and the gate is precisely what the `-j6` cap exists to protect.
**Expect the verification tail to stretch.**

### Known pre-existing WARN — not a unit's fault

`CMakeLists.txt` is **1,899 lines** and trips `check_file_sizes.sh`'s 1,500
WARN. It has been over the line since long before Batch J, and every unit adds
a few lines to it. **No J unit is to "fix" this, and no unit's acceptance is
blocked by it.** Boarding the build-system split is the orchestrator's call.

## ⚠ TEMPLATE CORRECTIONS FROM J02 (2026-09-02) — apply to every remaining unit

**1. `verify_move_only.py` MUST be run with `--repo <slot>`. Highest priority.**
It defaults `--repo` to `.`, which is NOT the executor's slot — this project
forbids `cd` and an agent's cwd resets between calls. Run as the acceptance line
used to specify it, J02 verified `main/` instead of its own worktree and printed
a loud FAIL full of an unrelated merged unit's lines. ⚠ **That failure has the
SAME SIGNATURE as the wrong-base-rev failure** (another unit's work appearing as
removals) and a different cause — an executor will misdiagnose it. The
acceptance line is now:
`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo <your slot>`

**2. Survey ranges are NOT compile-checked, and one of J02's did not compile.**
The plan's UCS range cut **inside a block comment** — `eq.cpp:2933` opened `/*`
and `:3170` closed it, so all eleven `UCS*` methods are commented out. Cutting at
the stated boundary would have left an unterminated `/*` in one file and an
unopened `*/` in the other. **Every unit must check its boundaries against
comment and `#if` nesting before cutting**, and report any range it had to move.

**3. Take ranges from the leading banner comment** — J02 found three of six of
its own plan's rows still started one line after the banner, contradicting the
process rule added after J01.

**4. The CMake variable is `CLIENT_CORE_SOURCES`, not `CORE_SOURCES`** — one
add-site, found by grep, as the process now instructs.

### Scheduling, measured (J01 solo vs J02 in a three-wide wave)

**~32 min end to end, of which the cut plus CMake plus both proofs was 4
minutes** — the bar is everything else. **`ctest -j6` went 3.0 min solo → 6.8 min
under a three-slot wave, a 2.3x stretch**, and is the single slowest step.
Budget **~35 min per unit in a three-wide wave**. The `-j6` cap did protect the
visual gate (22.9 s, no flake); it did **not** protect a subprocess-timeout test
— see the flake note below.

⚠ **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
LOAD-DEPENDENT FLAKE**, not a regression: a 5 s subprocess-timing test that
captured empty output under three-slot contention and passes 5/5 in isolation on
the same binary. It is NOT in `test_baseline.txt`. **Do not re-diagnose it per
unit** — if it fails in a wave run, re-run it alone before reporting anything.

### J02's own leftover, for a later decision

`src/client/eq_ucs.cpp` is **320 lines containing ZERO code** — 241 lines of
commented-out `UCS*` methods plus 79 of includes. It builds and it is exactly
what the contract specified, but giving dead commented code its own TU is
probably not what anyone wants. **T297 has since PROVEN UCS is not needed by a
Titanium client** (7 of 8 chat channels work cross-zone without it), so the
honest options are: delete the block (git keeps it), or keep it and say why.
Recommend deletion in a later unit; not done here because it is not move-only.

## ⚠ FURTHER CORRECTIONS FROM J04 (2026-09-02)

**5. The "verifier clean" bar and the mandated internal header are MUTUALLY
EXCLUSIVE.** J04 needed `command_processor_internal.h` for two shared
anonymous-namespace helpers (§Process step 4 requires exactly this), and
`verify_move_only.py`'s `BOILERPLATE` allowlist covers `#include`, `#pragma`,
`namespace`, `}` and comments — but **not `inline` or `using`**. So any unit that
promotes a file-local helper to a private header exits 1 by construction.
**This is expected, not a failure.** The verifier's own text names the case
("document in the unit review section for orchestrator sign-off"). Units must
paste the full output and enumerate every flagged line; the orchestrator signs
off in the merge commit. **Do not contort code to force a green exit** — J04
correctly refused to. (Precedent: J04's 8 lines = 2 `inline` + 4 `using` +
2 comments, helper bodies byte-identical, all 12 call sites textually untouched;
signed off at merge.)

**6. `build-arm-noble.sh` blows the 600 s tool ceiling under wave concurrency**
— SIGTERM, **exit 143**, mid-`gles2_zone_benchmark`. J01 measured ~5 min solo;
three-wide roughly doubles it. **Exit 143 means re-invoke, not breakage.** Every
Wave-1+ unit should expect to call it twice.

**7. The multiset check needs a stated convention when a unit creates a
cross-TU header:** a naive "concatenate the .cpp bodies" **silently under-counts
by the header's moved lines and still looks clean.** State the header's line
contribution explicitly, or re-insert it at its original offset before
comparing (what J04 did — byte-identical re-concatenation, 3,004 lines).

**8. `tests/CMakeLists.txt` is now 2,778 lines** — over the soft limit, within
222 of the hard cap, and **every J unit adds to it**. §Process grandfathers
`CMakeLists.txt` by name but not this one. **Board a split before a later J unit
trips a genuine FAIL through no fault of its own.**

## ⚠ CORRECTIONS FROM THE WAVE-2 PLANNING PASS (2026-09-02)

Re-measured every Wave-2 file at `ed97d701` while writing J05/J06/J07. Four
things the survey got wrong or under-stated:

**9. `application.cpp` is 2,368, not 2,356** — T319 grew it. `wld_loader.cpp`
(2,908) and `eq_player_info.cpp` (2,301) are unchanged. **Re-measure every
remaining unit's file at its own tip; the survey's numbers are from before three
splits and eight bug-fix units merged.**

**10. J05's internal-header need is NARROWER than the survey claims — take only
`read_val<T>`.** The survey says `wld_internal.h` is needed for `read_val<T>`
*and* `HASH_KEY`/`decodeStringHash`. Only the first is true:
- `read_val<T>` — genuinely broad. **114 call sites**, spread continuously
  through every fragment parser (measured by 200-line bucket:
  3/9/9/11/16/2/13/12/13/21/5). No 7-way partition keeps it file-local. It must
  be promoted, and doing so costs **exactly one changed signature line**.
- `decodeStringHash` — **already declared in the public header**
  (`include/client/graphics/eq/wld_loader.h:720`). External linkage today. Needs
  nothing.
- `HASH_KEY` — **exactly one reader in the whole tree**, `wld_loader.cpp:28`,
  inside `decodeStringHash` itself. Travels with it for free.

Corollary for every future unit: **an internal header's contents are an
enumerated, individually signed-off exception list.** Do not add a symbol to one
"for symmetry" — measure each.

Related: J05's header can also avoid J04's `namespace detail` + `using` pattern
entirely by keeping `read_val` in its existing `EQT::Graphics` namespace, which
leaves all 114 call sites textually untouched and **halves the exception list to
one added + one removed line**. Prefer that shape when the symbol's current
namespace is already shared by every consumer.

**11. ⚠ CMake add-site counts vary by two orders of magnitude, and nobody has
been counting.** J01 found 1, J02 found 1, J04 found 5. **J05 has NINETEEN** —
`CLIENT_CORE_SOURCES` plus **fourteen tool/benchmark targets** (`model_viewer`,
`s3d_dump`, `entity_composite_builder`, `wld_dump`, `bsp_region_finder`,
`zone_region_classifier`, `region_poly_audit`, `generate_surface_map`,
`zone_line_editor`, `equipment_index_builder`, `atlas_uv_audit`,
**`gles2_zone_benchmark`**, `gles2_shader_diagnostic`, `gles2_composite_audit`)
plus **four test targets**. J06 and J07 have one each.
⚠ **`gles2_zone_benchmark` is the binary `scripts/ci/visual_gate.sh` runs**, so
a missed add-site there surfaces as a *visual gate failure*, which an executor
will misdiagnose as a behaviour change. **Every unit must count its own sites
before starting and report the count**; the shape of the file gives no hint —
`wld_loader.cpp` is a leaf parser and has the most exposure in the tree.

**12. ⚠ The CMake commands in this project are UPPERCASE.** `ADD_EXECUTABLE`,
`SET`, `IF`. `grep -n 'add_executable'` and `grep -n '^set('` both return
**nothing**, silently — which reads exactly like "no sites here". §Process
already says to grep for the **filename**; this is why that matters.

### A correction to Hazard 1's premise (J04)

**There are no duplicate registrations in `registerBuiltinCommands`** — J04
parsed all 142 calls: no duplicate primary names, no alias claimed twice, no
alias colliding with a primary. **G02's shadowed `who` lives in the OTHER
registry** — `graphics/ui/command_registry.cpp`'s `CommandRegistry`, fed from
`eq_commands_*.cpp`. The last-registration-wins mechanism is real and the
constraint (do not split that method) still stands, but it currently has **no
live victim in this file**.

### Doc debt created by J02/J04 (not yet done)

`docs/module_map.md` and `src/client/input/CLAUDE.md` both describe
`command_processor.cpp` as one file and need entries for its four new TUs;
`module_map.md` likewise for `eq.cpp`'s five. Outside every unit's territory —
**fold into a later J unit or a hygiene pass.**

## ⚠ CORRECTIONS FROM J11 (2026-09-02) — and THE HARD-CAP LIST IS NOW EMPTY

**All four files over the 3,000-line hard ceiling are cleared:** `eq.cpp`
3,923 → 1,022 (J02), `entity_renderer.cpp` 3,244 → 540 (J03),
`command_processor.cpp` 3,024 → 284 (J04), `irrlicht_renderer.cpp` 3,296 → 908
(J11). `find src include | awk '$1>3000'` returns nothing.

**9. Correction 5 is TOO NARROW — the verifier exception is a PREAMBLE-CONTENT
problem, not just a shared-helper one, and it will recur in every unit.** J11
needed no internal header (both anonymous namespaces verified single-consumer)
and still exited 1 with **70 lines**: the mandated verbatim preamble contains 12
`extern` GLES2 declarations and a one-line `WindowManager` stub, none of which
`BOILERPLATE` names, × 5 new TUs = 65 lines, plus 5 test repoints. Proven
byte-identical; signed off at merge. **Expect this wherever the original's
preamble carries anything beyond includes/pragmas/namespaces.**

**10. TERRITORY MUST COVER TESTS THAT SCAN SOURCE TEXT.** Four tests read
`irrlicht_renderer.cpp` **as text** and had to be repointed — outside J11's
stated territory and **not optional: three would have failed and one would have
silently asserted nothing.** **Every unit must run
`grep -rn '<basename>' tests/ scripts/` BEFORE cutting** and add what it finds
to its territory. J05, J06, J09 and J13 likely have such guards.
(J11's repoint made one guard strictly tighter, not weaker — worth aiming for.)

**11. J03's cutting rule REPLACES the banner rule outright.** `irrlicht_renderer.cpp`
had 2 banners in 3,296 lines; the rule — *cut at a blank line whose predecessor
closes a method, and verify comment depth 0 at the cut* — decided all 14 cuts
mechanically. **Worth scripting once for the remaining units.**

**12. Line estimates are unreliable in BOTH directions** — J03 ran 1-6 lines
high (counting the namespace close per cluster), J11 ran 20-130 lines LOW (the
survey assumed a 97-line preamble against a real 109, × 6 TUs). Cluster
membership has been right every time; the arithmetic has not. Treat projected
counts as indicative only.

## ⚠ CORRECTIONS FROM J06 (2026-09-02)

**13. THE VERIFIER SILENTLY NO-OPS ON AN UNCOMMITTED TREE — it prints
`No C++ files changed` and EXITS 0.** J06 hit this. **This is the THIRD trap
with the same green-or-alarming-but-wrong signature**, after the missing
`--repo` (verifies `main/`) and the wrong base rev (shows another unit's work as
removals). All three look like a definitive answer and are not one.
**The acceptance line must read: COMMIT FIRST, THEN VERIFY.**

**14. Corrections 6 and the J02 timing note are WORST CASES OBSERVED, not
forecasts, and the word "expect" makes a clean run read as an anomaly.** J06's
`ctest -j6` ran in **120.7 s — faster than J01's 3.0 min SOLO, on a larger
suite** — and `build-arm-noble.sh` completed on its FIRST invocation. Wave
contention varies with what the other slots are doing (and one earlier
measurement was inflated by an orphaned client process burning 113% CPU for five
hours, killed 2026-09-02). Treat the stretch figures as "may happen", not "will".

### J06's own result — the cleanest unit of the batch

`application.cpp` 2,368 → **948**, plus `application_commands.cpp` 552,
`application_zone_loading.cpp` 458, `application_config.cpp` 434,
`application_frame.cpp` 224. **Verifier exit 0, PASS, +224/-0 boilerplate — no
exception, no sign-off needed.** Both proofs `cmp`/`diff` exit 0. Zero range
drift; the plan's estimates were uniformly exactly 1 low (counting
`61 + body` instead of `61 + body + 1`).

**Hazard 1 resolved as Option A** (`mainLoop` stays with `initialize`, nothing
promoted), argued rather than picked: Option B's entire benefit was 99 lines of
cosmetics on a TU the size checker already calls `ok`, against the cost of
converting a live async-signal-safe object's internal linkage to external inside
a commit whose contract is verbatim relocation.

**Doc rot found, not fixed (comment-only, outside move-only scope):** 13 `tests/`
and `scripts/` mentions of `application.cpp`, **all comments, zero functional
readers** — so unlike `irrlicht_renderer.cpp` no repoint was needed — but **6
cite line numbers this split invalidated**, and
`test_headless_command_refusal.cpp:103` tells a reader to "grep application.cpp",
which will now miss. **Fold into the `docs/module_map.md` hygiene pass** along
with J02's and J04's entries.

**Also recorded:** `Application::initialize` is **587 lines** (not the 569 on
record) — 62% of the surviving TU and the floor for any further split; and the
22-line T104/T49 tombstone comment block rode into the zone-loading TU verbatim,
the same shape as J02's `eq_ucs.cpp` leftover. **That is now THREE dead-content
findings in the batch** (`eq_ucs.cpp` 320 lines of commented-out methods,
`wld_loader.cpp` 286 lines of `#if 0`, this) — the batch owes one decision
covering all of them.

## ⚠ CORRECTIONS FROM J05 (2026-09-02)

**15. A VISUAL-GATE FAIL IS NOT NECESSARILY A PIXEL FAIL — check WHICH metric
failed before believing you changed rendering.** J05's first gate run failed on
`befallen` **p95 CPU 11.684 vs a 10.948 ceiling** with **zero image mismatches**
and a clean `git status`; the re-run measured **4.676**. That is a 2.5x swing on
a 2x ceiling under three-wide load. ⚠ **This also disambiguates the J05 warning
about `gles2_zone_benchmark`: a missed CMake site for that binary fails at the
RENDER step, not the CPU step.** Different failure, different cause.

**16. `ctest` exit 8 during `GoogleTestAddTests.cmake` discovery is NOT the T293
truncated-binary trap.** J05 hit a discovery timeout listing one test; the binary
was intact (`--gtest_list_tests` exit 0 on demand) and the re-run was clean.
Check the binary before concluding a mid-link truncation.

**17. Declare count variables `local` in your proof helpers.** A non-local `n`
in J05's `body()` helper truncated one range and produced a 247-line shortfall
**that read exactly like a dropped range**. When a proof fails, suspect the
proof first.

**18. Correction 10's namespace advice is now a RULE, not an option: keep a
promoted helper in its EXISTING namespace.** J05 = 2 exception lines for 114
call sites; J04 = 8 lines for 12 call sites, because it introduced a `detail`
namespace and needed `using` declarations. Same problem, four times the paperwork.

### J05's result

2,908 → 7 TUs + an 18-line header, largest 749. Verifier exit 1 with **exactly
the two predicted lines** (`static inline read_val` → `inline read_val`), signed
off at merge. Both proofs clean (`cmp` exit 0 over 2,894 lines; multiset with the
header's 8 lines counted in). **All 19 CMake sites confirmed** — 15 in the root
list, 4 in tests. ~62 min wall clock, of which the cut plus CMake plus both
proofs was under 6; wave concurrency cost ~14 min in pure re-runs.

**Corrections to the record J05 made:** the dead `#if 0` block is **278 lines,
not 286** (both this plan and J05's said 286, which is the whole 2623-2908 tail
including two comments and the namespace closes) — still 37% of the surviving
file. And **`resolveTextureName` no longer exists anywhere in the tree**, which
corrects `T10_wld_kill_rate.md:281`.

⚠ **`tests/CMakeLists.txt` is now 2,850 — within 150 lines of the hard cap, and
`CMakeLists.txt` has crossed 2,000 (1,914 → 2,004).** J05 alone added 24 lines
to the tests file. **J20 (splitting it) is now the most urgent unit in the
batch**; the next two or three units will otherwise trip a genuine FAIL through
no fault of their own.

## ⚠ CORRECTIONS FROM THE WAVE-3 PLANNING PASS (2026-09-02)

Re-measured all three Wave-3 files at `f215651a` while writing J08/J09/J10. All
three plans carry every correction inline; nothing below needs chasing.

**19. `eq_entities.cpp` is 2,786, not 2,736 — and the relocation is 209 lines,
not 161.** T318 (`2fe75e35`, the size-update publish) grew the file by 50 lines
after the survey. The "161 lines into `eq_group.cpp`" figure counted only
`ZoneProcessRaidUpdate`'s body (1348-1508) and **excluded both its own 24-line
T169/T179/T180 comment block and `SendRaidCommand` entirely**. The real move is
**1324-1509 (186) + 2764-2786 (23) = 209 lines**. `eq_group.cpp` is **760
lines** today and becomes ~971. **Correction 9 keeps holding: re-measure every
remaining unit's file at its own tip.**

**20. ⚠ Moving code INTO an existing file breaks the verbatim-preamble rule, and
the rule does not say so.** §Process step "each new TU carries the original's
full include block, verbatim" is written for *newly created* TUs. J08 relocates
into `eq_group.cpp`, which must **not** receive `eq_entities.cpp`'s 31-line
include block — it needs exactly one added line (`#include
"client/raid_update.h"`). **A destination file gets a narrow, enumerated,
individually-justified include list, the same discipline correction 10 imposed
on internal headers.** And **both content proofs must include the destination
file's body**, or they under-count by the relocated range and still look clean
(correction 7, generalised).

**21. `zone_shader.cpp`'s survey row ("3 TUs + 2 hdrs") does not survive
measurement — the real shape is 1 TU + 3 private headers.** Two independent
reasons, both measured:
- The **1,362** GLSL lines (survey said 1,345) are two `#ifdef EQT_HAS_GLES2` /
  `#else` arms declaring the **same eight identifiers**. They are `static` and
  must stay `static`; de-`static`-ing them would put eight identically-named
  symbols in the link. **They can only move to headers** — one per arm, `#else`
  becoming `#ifndef`, each included by exactly one TU.
- The three shader-callback classes are **100% inline, in-class**, and the
  `ZoneShaderManager` constructor instantiates them **18 times**. A separate
  `.cpp` for them would need a declaring header that, being all-inline, would
  hold the entire implementation — leaving the `.cpp` empty. So they too become
  a header.
Result: `zone_shader.cpp` **2,561 → ~525**, plus `zone_shader_sources_gles2.h`
~1,010, `zone_shader_sources_gl.h` ~365, `zone_shader_callbacks.h` ~700.

**22. ✅ J09's "no `friend` needed" claim HOLDS — now measured, not read.**
`include/client/graphics/zone_shader.h` has **no `friend` declaration at all**,
and the three callbacks touch `owner_` through exactly **27 distinct members,
every one a public accessor** declared between `public:` (`:16`) and `private:`
(`:219`); the two constants they qualify are public `static constexpr`. **There
is not one raw-member access in 681 lines of callback code.** The unit does not
change shape on that axis.

**23. ⚠ `verify_move_only.py`'s BOILERPLATE allowlist is WIDER than any
correction has said, and that is load-bearing for J09.** Read
`scripts/refactor/verify_move_only.py:25-28`: it matches
`#include`, `#pragma`, `#ifndef`, `#ifdef`, `#if`, `#define`, `#endif`,
`#else`, `#elif`, any comment, `namespace … {`, and a bare `}`. So **rewriting
an `#else` as an `#ifndef` across a file boundary costs zero exception lines**,
and J09 is predicted to exit **0** despite splitting a live conditional. The
corollary bites too: **a bare `}` is allowlisted**, so a wholly dropped block's
braces are invisible to the verifier — which is exactly why the multiset and
re-concatenation proofs are not optional.

**24. `constrained_texture_cache.cpp`'s preamble is correction 9's case, and the
cost is computable in advance: 21 lines.** Lines 29-32 (two function-pointer
`typedef`s, 4 physical lines) and 35-37 (`extern void* gles2WrapTexture`, 3
physical lines) sit inside the preamble's
`#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)` region and are **not**
boilerplate. × 3 new TUs = **21 substantive added lines, 0 removed.** J10 is
therefore predicted to exit **1 by construction**, with an exception list that
can be stated before the unit runs. **When a preamble's non-boilerplate content
is enumerable, the plan should predict the exact exception count** — it turns
"the verifier failed" into "the verifier agreed with the plan".

**25. Neither `zone_shader.cpp` nor `constrained_texture_cache.cpp` has a single
`// ====` banner** (zone_shader has twelve; ctc has **none**, only `// ------`
section rules; `eq_entities.cpp` has **two** in 2,786 lines). **Correction 11
stands: J03's rule — cut at a blank line whose predecessor closes a method,
verify comment depth 0 at the cut — is the batch's cutting rule, and the banner
rule is now only a tie-breaker for where a leading comment belongs.**

**26. CMake exposure, counted before dispatch: J08 = 2, J09 = 4, J10 = 12.**
⚠ **J09 and J10 BOTH touch `gles2_zone_benchmark`** (`CMakeLists.txt:1645`
/ `:1656`), the visual-gate binary. J09 creates no `.cpp` so it has nothing to
add at any of its four; **J10 must add three TUs at all twelve sites** — nine in
the root file (`GRAPHICS_SOURCES`, `model_viewer`, `s3d_dump`,
`entity_composite_builder`, `generate_surface_map`, `zone_line_editor`,
`gles2_zone_benchmark`, `gles2_shader_diagnostic`, `gles2_composite_audit`) and
three in `tests/CMakeLists.txt` (`test_graphics`, `test_asset_loading`,
`test_door_loading`).

**27. Correction 10's tests/scripts pre-check came back CLEAN for all three
Wave-3 files — but only because it was run.** `grep -rn '<basename>' tests/
scripts/` finds **12 comment hits for `eq_entities`, 1 for `eq_group`, 0 for
`zone_shader`, and 19 header-`#include`s plus 3 prose mentions for
`constrained_texture_cache` — ZERO functional readers of any of the three as
text.** No repoints needed. ⚠ **Several `eq_entities.cpp` line-number citations
in `tests/test_zone_connection_flow.cpp` were ALREADY STALE before this split**
(`:347` cites 1598, `:349` cites 1035, `:355` cites 1323 — none matches the
current handler offsets). Same pattern J06 and J07 recorded: **line-number
citations in test comments rot on their own, long before a split touches them.**

## The dead-content decision — ONE recommendation for Will, covering SIX sites

The batch has now found six blocks of content that exist only to be read, and
has deliberately relocated every one of them verbatim rather than judge them
mid-split. They are **not one kind of thing**, and J07's Review drew the
distinction that makes this decidable:

> commented-out **code** is debt; a paragraph explaining a **deliberate absence**
> is documentation.

| # | site | size | kind |
|---|---|---|---|
| 1 | `src/client/eq_ucs.cpp` — 241 lines of commented-out `UCS*` methods + 79 of includes | **320 lines, ZERO live code** | **code** |
| 2 | `wld_loader.cpp` — the trailing `#if 0` block (J05 corrected 286 → **278**) | 278 lines, **37% of the surviving file** | **code** |
| 3 | `constrained_texture_cache.cpp:818-1120` — the T04 `#if 0` upload queue, containing a **second definition of `isPending`/`hasPendingWork`** and four nested `#ifdef` pairs | 299 lines, **13.5% of the file** | **code** |
| 4 | `eq_entities.cpp:185-226` — a commented-out packet hex-dump debug block inside `ZoneProcessZoneSpawns` | 42 lines | **code** |
| 5 | `application_zone_loading.cpp` — the T104/T49 tombstone block (J06) | 22 lines | **documentation** |
| 6 | `eq_player_misc.cpp:74-96` (T168 boats) and `:199+` (T145 `OP_ENVDAMAGE`), `eq_player_info.cpp:366` (T286) — J07's three | ~35 lines | **documentation** |

**Recommendation (one decision, not six):**

- **Delete the four "code" sites** — #1, #2, #3, #4 — in a single dedicated
  hygiene unit, **not inside any J split** (deletion is not move-only, and a
  J unit's whole contract is that nothing changed). Git keeps every line;
  `refactor/completed/` keeps the archaeology. For #1 the case is already made:
  **T297 PROVED UCS is not needed by a Titanium client** (7 of 8 chat channels
  work cross-zone without it), so `eq_ucs.cpp` is a TU that compiles nothing and
  documents a feature we have decided not to build. For #3, delete the `#if 0`
  region **and** leave the three-line `// T04: Upload queue removed. Use
  getOrLoad() + uploadTexture() instead.` rule above it — that rule is the
  documentation; the 299 lines are the debt.
- **Keep every "documentation" site** — #5 and #6 — unchanged, and stop treating
  them as findings. A paragraph that tells the next reader *why the obvious
  thing is absent* is the cheapest defect prevention in the tree; `eq_entities.cpp`'s
  four `NOTE:` paragraphs about what stayed in `eq.cpp` are the same shape and
  have already stopped at least one unit from "restoring" a deliberate split.
- **Standing rule going forward:** a J unit **records** dead content and
  **moves it verbatim**. It never deletes and never tidies. The recommendation
  above is Will's to accept or reject; until he does, nothing is deleted.

Total recoverable if accepted: **~939 lines**, one whole TU removed from the
tree, and `wld_loader.cpp` drops by 37%.

## ⚠ CORRECTIONS FROM J09 (2026-09-02)

**19. A PREDICTED DIFF SIZE MUST BE DERIVED FROM THE PLAN'S OWN PRESCRIBED
OUTPUT.** J09's plan predicted "exactly 2 differing lines" in one section while
another section prescribed a comment retitle that makes it **3** — the plan
contradicted itself, and a unit trusting the count over the pasted diff would
have reported a phantom anomaly. **When a plan predicts a number, derive it from
what the plan itself tells the executor to write.** (J10's 21-line prediction was
derived correctly; this one was not.)

**20. Correction 17 was stated too narrowly.** J09's one proof failure was range
arithmetic against the produced files' line numbering — no variable scoping
involved. **The rule is "suspect the proof first"; `local` count variables are
one instance of it, not the rule.**

### J09's result — the cleanest of Wave 3

2,561 → `zone_shader.cpp` **520** + three private headers (1,017 / 370 / 702),
no fourth header needed. **Zero range drift** — all 37 preprocessor line
numbers, 12 banner triples, 7 cut boundaries and 4 CMake sites reproduced
exactly, which is unusual this late in the batch. Build, gate, gles2 and ARM all
passed **first invocation**; no exit 143, so no ELF scan was needed.

**Both planning predictions held, and both had been measured rather than
assumed:** no `friend` is needed (27 distinct `owner_->` members, all public,
**zero raw member accesses** in 681 lines of callback code), and the verifier
**exits 0** despite rewriting `#else` to `#ifndef` across a file boundary —
**the first unit in the batch to split a live conditional across a file boundary
and still pass clean.** Both proofs came back with the same three preprocessor
lines and nothing else, so no GLSL line was duplicated or dropped.

⚠ **DEAD-CONTENT SITE #7, and the largest of the reachability kind:**
`zone_shader_sources_gl.h`'s **356 lines compile only under
`EQT_GRAPHICS=ON EQT_DRM=ON EQT_GLES2=OFF`, a configuration P32 RETIRED**
(`CMakeLists.txt:701-706`; `build-arm-noble.sh` dropped `--no-gles2`). **None of
the three configurations in the acceptance bar compiles it.** Since nothing in
the bar would parse it, J09 added a standalone
`g++ -fsyntax-only -UEQT_HAS_GLES2` probe and confirmed both arms parse clean —
but the arm is unreachable in every shipped build. **This belongs in the
dead-content decision as a distinct kind: not commented out, not `#if 0`, but
gated on a retired build configuration.**

## ⚠ CORRECTIONS FROM J08 (2026-09-02) — including the batch's first plan defect

**21. THE SINGLE-CONSUMER GREP PROVES A HELPER *CAN* STAY FILE-LOCAL. IT DOES
NOT PROVE THE PLAN'S RANGES *KEEP* IT THAT WAY.** J08's plan assigned the exp
helpers (`EXP_FOR_LEVEL` / `MAX_EXP_LEVEL` / `CalculateExpProgress`) to the
**stays** range and their sole consumer `ZoneProcessExpUpdate` to a **different
TU** — so the function would have been undeclared and the three statics
readerless. It contradicted the plan's own sentence about keeping them with
their callers. **This is the batch's first plan defect that only surfaces at the
BUILD step**, and every remaining plan should be checked for it: for each
file-local helper, confirm the plan puts it in the SAME range as its consumer,
not merely that it has one consumer. J08 fixed it by moving the helper range to
travel with the consumer; zero verifier exception lines resulted.

**22. On a RELOCATING unit, a mis-sliced destination fails at the most alarming
possible place.** J08's first re-concatenation attempt failed **exactly at the
relocation seam** — from its own offset arithmetic on the destination file, not
a dropped range. Reinforces correction 20: suspect the proof first.

**23. A long build can end as exit 143 OR be backgrounded by the tool with its
own exit code.** J08's `build-arm-noble.sh` did the latter (~11 min, exit 0).
**Only the former invalidates binaries.** The protocol's exit-143 section does
not distinguish them — do not run the ELF scan reflexively on every slow build,
and do not skip it after a real 143.

**24. STOP ATTRIBUTING STALE `tests/` LINE-NUMBER CITATIONS TO THE SPLITS.**
J08 checked all seven of its file's citations line by line **at its base commit**
and every one was **already wrong before the split** (one cites a line that is
now inside a different function, one a blank line, one a struct field). **Third
unit in a row to find this.** They are pre-existing doc rot, now owned by J21.

### J08's result

`eq_entities.cpp` 2,786 → **869**, plus 767 / 653 / 391, and `eq_group.cpp`
760 → **980** (the +209 relocation). **Verifier exit 0, no exception.** Both
proofs run WITH the destination included, per correction 20 — `cmp` exit 0 over
2,713 lines, multiset `diff` exit 0 over 3,448. The naive recipe would have
under-counted by exactly the 209 relocated lines and still looked clean.

Two smaller plan errors it caught: HAZARD 1's prose says "eleven `#ifdef` pairs"
where its own table correctly lists **eight** (the table is right), and the
tests/scripts pre-check found **19** hits, not the 12 recorded (all comments,
zero functional readers).

## ⚠ CORRECTIONS FROM THE WAVE-4 PLANNING PASS (2026-09-02, at `136a319b`)

Re-measured `eq_action_handler.cpp` and `eq_bridge.cpp` while writing J12/J13,
**audited every already-written-but-unexecuted plan against correction 21**, and
re-measured all fourteen §Residual files. Four things.

**25. ✅ THE CORRECTION-21 AUDIT CAME BACK CLEAN — and J10 was the only plan it
could have hit.** J01–J09, J11 and J20 have all executed; **J10 is the only
written-but-unexecuted plan in the batch**, and it does not have the defect:
- `constrained_texture_cache.cpp` has **exactly one file-scope symbol**,
  `static std::string eqe1ArchiveStem` at **1807**, and **exactly one consumer**,
  `recordEQE1Request` at **1824** — verified by
  `grep -rn 'eqe1ArchiveStem' src/ include/ tests/ scripts/`, which returns those
  two lines and nothing else. **Both fall inside J10's single range 1537-2209**,
  so the helper travels with its consumer. J10's plan already says so and already
  tells the executor to re-confirm it with their own grep.
- Everything else the file declares `static` is **function-local** (`kSuffix` 68,
  the four `pfn*` lookups at 1620/1622/1891/1948, `GL_ETC1_RGB8` 1873,
  `thread_local result` 1772, `kPayloadNames` 2172) and moves inside its method.
  **Zero anonymous namespaces**, confirmed.
- J12's and J13's files have **zero file-scope statics and zero anonymous
  namespaces at all**, so correction 21 has no exposure in either. Both plans
  say so and require the executor to paste the grep rather than cite the claim.

**Standing form of the check, for every remaining plan:** for each file-local
`static` / anonymous-namespace function / file-scope constant, do not stop at
"it has a single consumer" — **name the range the plan assigns the helper and
the range it assigns each consumer, and confirm they are the same range.**

**26. J12 and J13 were BOTH stale, in the same direction, and one is 46 lines
off.** `eq_action_handler.cpp` is **2,248**, not 2,202 (and has **174** methods,
not 173). `eq_bridge.cpp` is **2,127**, not 2,098 — and `docs/module_map.md:27`
still records it as **2,001**. **`ProcessBridgeIntents` is 580 lines (892-1471),
not the 558 in §Optional** — correct that row when J13 lands. Correction 9 keeps
holding: **re-measure every remaining unit's file at its own tip.**

**27. ⚠ CORRECTION 10 SCORED ITS FIRST HIT SINCE J11, AND IT IS J13's ONLY REAL
HAZARD.** `tests/test_reachability_sweep.cpp` opens `src/client/eq_bridge.cpp`
**as text at three sites** (`:221`, `:238`, `:258`) and asserts that every
`RendererIntent` member has an `is_same_v<T, eqt::events::…>` arm in it. All
three break when `ProcessBridgeIntents` moves out: two would **fail for every
intent member**, and the third — a *string-compare exclusion*, not a `readFile`
— would silently let the consumer arms be mistaken for producers. J13 carries
the exact three-line repoint and puts the file in its territory.
⚠ **The two greps are not interchangeable.** `grep -rn '<basename>' tests/
scripts/` is what finds the string-compare exclusion;
`grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u` is what
enumerates every source path any test opens as text. **Run both.** J12's file,
run through the same pair, has **zero** text readers.

Recorded for Wave 6 from the same sweep: **`renderer_frame.cpp` is read as text
at three sites** and **`action/action_dispatcher.cpp` at one**
(`test_reachability_sweep.cpp:356`, plus `test_ui_hide_command.cpp` and
`test_first_person_camera_single_formula.cpp` for `renderer_frame`). Those units
inherit J11's problem; the rest of the residual list does not.

**28. ⚠ THE RESIDUAL LIST DID NOT SHRINK — FOUR OF ITS FILES GREW.** The batch
plan's §Residual row says "re-measure first; several shrink as callers move."
**Measured at `136a319b`: not one file dropped below 1,500.** `eq_movement.cpp`
went 2,033 → **2,139**, `action_dispatcher.cpp` 2,039 → **2,081**,
`renderer_frame.cpp` 1,691 → **1,752**, `eq_bridge.cpp` 2,098 → **2,127**.
`combat.cpp` and the ten others are unchanged. **Splitting a file has not been
moving lines into the residual set — unrelated T-unit feature work has.**
§Residual below is now a measured list, not a survey.

**Total over the 1,500 warn ceiling at `136a319b`: 21 files** — 4 headers
(J14-J17), 3 with plans written (J10 in flight, J12, J13), and **14 for Wave 6**.
Zero over the 3,000 hard cap.

## ⚠ CORRECTIONS FROM J10 (2026-09-02)

**26. A PLAN THAT NAMES `tests/CMakeLists.txt` LINE NUMBERS IS STALE BY
CONSTRUCTION SINCE J20 — and it fails SILENTLY.** J20 split that file (2,850 →
135 + fragments) between J10's planning and its dispatch, so J10's prescribed
`grep -n '... CMakeLists.txt tests/CMakeLists.txt'` **would have found nine of
twelve add-sites and looked complete.** The three test sites had moved to
`tests/cmake/graphics.cmake` and `tests/cmake/zone_integration.cmake`.
**Remaining units must `grep -rn` across `tests/`, never name that file.**
Losing 25% of the add-sites this way produces a link error at best and a
silently-unbuilt test target at worst.

**27. THE TRUNCATED-BINARY TRAP FIRED AGAIN, AND A SIZE CHECK WOULD HAVE PASSED
IT.** J10's first build exited 143; the re-invocation **exited 0 with three
binaries truncated mid-link** — 50-71 MB files with **all-zero ELF headers**.
Second independent confirmation after J07. **Check the ELF magic, not the size**
— these were far too large for any size heuristic to flag. (The orchestrator hit
a third instance on its own merge build: a 377 MB file with zero magic bytes.)

### J10's result

2,211 → 815 / 722 / 454 / 373. **The 21-line verifier prediction held EXACTLY**
— 7 preamble lines × 3 TUs, no twenty-second line — which is what a prediction
derived from the plan's own prescribed output looks like (contrast correction
19). Signed off at merge after md5-verifying all three blocks byte-identical.
Three proofs, all exit 0: re-concatenation (2,167 lines), line-multiset, and a
dedicated `#if 0`-region diff (303 lines).

⚠ **NEW EVIDENCE FOR THE DEAD-CONTENT DECISION, and it points the other way from
"just delete the duplicate": the two `isPending` bodies are NOT identical.** The
disabled one reads `pendingDecodes_`; the live one reads `decodedEntries_`. The
`#if 0` block is an **older, DIFFERENT implementation**, not a stale copy — so
"delete the duplicate" would be substituting different behaviour for redundancy
removal. It is still dead (299 lines, now 80% of its new TU) and still a
deletion candidate, but the reason is "obsolete implementation", not "duplicate".

Two smaller plan errors it caught: the cut-boundary table misattributes line
1130 to the live `isPending` when it closes live `hasPendingWork` (the prose
below is right, the cut is unaffected), and the plan's whole
`tests/CMakeLists.txt`-is-2,850-lines urgency paragraph is obsolete.

## ⚠ CORRECTIONS FROM J12 (2026-09-02)

**28. THE BANNER SANITY CHECK IS BROKEN AS WRITTEN.**
`grep -c '^// ={4,}' <file>` returns **0**, not the real count — `{4,}` is not a
POSIX BRE interval, so the pattern never matches. The `awk` form in the plans is
correct, but **anyone cross-checking with plain `grep -c` will conclude the
banners vanished.** Use `grep -cE`. (Same family as the uppercase-CMake trap: a
grep that silently returns nothing reads exactly like a real answer.)

**29. `readelf -h` is a stronger and equally cheap truncation check** than the
`head -c 4 | od` recipe. Either works; prefer `readelf`.

**30. The tail convention double-spaces some TUs.** "blank + namespace close,
replicated in each TU" produces a double blank line in any TU whose body already
ends on the original's blank line. Cosmetic, invisible to both proofs and the
verifier — declare it rather than hand-fixing it.

**31. ORCHESTRATOR-SIDE: a doc-hygiene unit running CONCURRENTLY with a split
guarantees an alternatives-shaped conflict in the split file.** J21 edited two
comments in `eq_action_handler.cpp` while J12 was splitting it, so git offered
the entire pre-split file as one side. Taking either side wholesale silently
reverts a completed unit. **Resolve by taking the split and re-applying the
comment edits into the TU that now holds each line.** Better: **do not schedule
a tree-wide doc pass alongside splits** — or scope it to exclude every file with
a unit in flight. (Same shape as T322's conflict earlier today.)

### J12's result — and the truncation trap fired a FOURTH time

`eq_action_handler.cpp` 2,248 → **783 / 570 / 957**, method split 68/21/85
**exactly** as planned, **zero range drift** (all 26 banner line numbers, both
cut boundaries and every comment-depth check reproduced). **Verifier exit 0 with
the predicted `+57/-0`** — and the number was derivable from the plan alone:
(19 includes + 2 namespace + 5 header) x 2 new TUs + 5. Both proofs run against
the produced files rather than scratch extracts, so they cover assembly too.

⚠ **The x86 build hit exit 143 and its re-invocation exited 0 with a completely
clean log while two binaries were still truncated** — 21 MB of zeroes each.
**Fourth independent instance today** (J07's twelve, J10's three, the
orchestrator's one, now these two); the ARM build hit its own 143 separately.
This is not an edge case; it is the normal consequence of a three-wide wave.

The two cutting rules disagreed at exactly the one predicted place (a banner with
no preceding blank line) and nowhere else.

## ⚠ CORRECTIONS FROM J13 (2026-09-02)

**32. A KILLED *LINK* DELETES ITS OUTPUT; A KILLED *COMPILE* CAN LEAVE A
TRUNCATED FILE. The ELF scan's outcome is NOT predictable from the exit code —
keep it mandatory either way.** J13's `build-gles2-x86.sh` cost **two
consecutive exit-143s** (~25 of ~70 min wall clock) and `gmake` **deleted its own
partial output on SIGTERM both times**, so the target was *absent* rather than
truncated and the ELF scan (run twice) correctly found nothing. That does not
contradict J07/J10/J12 — their traps needed a partial file to survive. **Absent
and truncated are different failures with the same trigger; the scan
distinguishes them and costs seconds.**

**33. "Zero block comments" claims must mean "zero MULTI-LINE comment
regions".** J13's plan said zero; `grep -n '/\*'` returns **ten** — all
single-line `/*bank=*/` argument comments, none near a cut. Comment *depth* at
every cut was 0, which is what the rule actually needs. Reword future plans.

### J13's result

`eq_bridge.cpp` 2,127 → **892** + 705 + 746, methods 21/2/22, zero range drift.
`ProcessBridgeIntents` is **580 lines** (not 558) and moved whole — Batch-G
candidate, flagged not solved. Three proofs, all exit 0, including a standalone
diff of that method.

**How it handled the silent text-scanning guard is the model for the rest of the
batch.** The third site is a string-compare exclusion that `readFile` greps
cannot see; J13 kept the old exclusion, added the new TU, and then **measured
whether the guard was load-bearing at all** rather than asserting it —
`grep -c 'pushIntent('` is 0 in all three TUs, so at this tip neither exclusion
changes the computed set. Both are defensive, which is a reason to keep both,
not to drop either. `ctest -R ReachabilitySweep` 5/5.

Two plan errors: per-TU method counts were transposed (its own cluster
enumerations were right), and it predicted `packets_skills.cmake:58` would stay
correct — the method it names **moves**, so all five `tests/cmake/` comment lines
are now stale, not two of four. Owned by J21's successor pass.

## ⚠ CORRECTIONS FROM THE WAVE-6 PLANNING PASS (2026-09-02, at `824adb28`)

**All thirteen remaining Wave-6 files re-measured at `824adb28`, all thirteen
plans written (J23-J35), and every plan audited against correction 21 before
being committed.** Six things.

**34. ⚠ THE §RESIDUAL CALL ON `variant_loading.cpp` DOES NOT SURVIVE ARITHMETIC —
there is no Batch-G decision owed, and the same is true of
`renderer_player_physics.cpp`.** §Residual calls `variant_loading.cpp` "the
batch's worst floor" and "the strongest candidate in the tree for a Batch-G
decision taken *before* its Batch-F split". **It measured a TWO-way split.** A
three-way split at the two legal cut points 382 and 1198 puts the 815-line and
396-line methods in **different** TUs:

| file | TUs | largest | headroom |
|---|---|---|---|
| `variant_loading.cpp` 1,607 | 383 / **833** / 425 | 833 | **667** |
| `renderer_player_physics.cpp` 1,546 | **827** / 736 | 827 | **673** |

The test §Headline finding actually sets is **`preamble + largest method` vs the
ceiling**: `15 + 815 = 830` and `15 + 806 = 821`, both comfortably under 1,500.
**A method dominating a small TU is a readability observation, not a ceiling
problem.** Both are plain Batch-F relocations; both are flagged in their plans and
in their new TUs' file headers so the question is not re-opened. **Update
§Residual and the scheduling note.**

**35. ⚠ THE RESIDUAL LIST'S TEXT-SCAN RECORD WAS WRONG IN BOTH DIRECTIONS, AND
THERE IS A THIRD KIND OF GUARD NEITHER STANDARD GREP FINDS.**
- `action_dispatcher.cpp` is recorded as text-scanned **×1** at
  `test_reachability_sweep.cpp:356`. It is **×2** — `:357` (a `readFile` that
  parses every `ActionDispatcher::` definition) **and `:394`, a string-compare
  exclusion** of exactly J13's invisible shape. Missing `:394` would silently
  invert the external-caller sweep and let an unreachable method be declared
  reached.
- `renderer_frame.cpp` is recorded as text-scanned ×3 at
  `test_reachability_sweep.cpp:156` and `test_ui_hide_command.cpp:698,706`.
  **Every one of those citations is wrong.** `test_reachability_sweep.cpp` does
  not read this file at all. The real sites are
  **`test_static_ui_scroll.cpp:698` and `:706`** (plus a diagnostic string at
  `:702`) **and `test_ui_hide_command.cpp:156`**. The `:698` assertion counts
  `getMouseWheelDelta()` and requires **exactly 2** — after a split it reads 0 and
  fails saying the wheel is consumed by nothing.
- ⚠ **NEW: `tests/test_first_person_camera_single_formula.cpp` sweeps every
  `.cpp`/`.h` under `src/client/graphics` RECURSIVELY and keys two assertions on
  FILENAME** (`kHelperTU = "renderer_camera.cpp"`,
  `kFloorSecondReader = "renderer_player_physics.cpp"`). **It names no path that
  `grep -hn 'kRoot / "src…'` can find**, so every graphics-tree unit in this batch
  from now on inherits it automatically. Concretely it **forces
  `updatePlayerMovement` to stay in a file called `renderer_player_physics.cpp`**,
  because line 135 is the tree's second permitted reader of
  `firstPersonMinLookPitchDegrees`. **J33's single cut is chosen around that
  constraint.** Add a third pre-check to §Process:
  ```bash
  grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' <file>
  ```

**36. ⚠ A PREAMBLE CAN CONTAIN A DEFINITION, AND COPYING IT VERBATIM IS A
DUPLICATE-SYMBOL LINK ERROR. NEW HAZARD CLASS.**
`src/common/net/daybreak_connection.cpp:17` is
`int EQ::Net::DaybreakConnection::s_debug_level = 0;` — the out-of-class
definition of a static data member, **external linkage**. Every prior unit's
non-boilerplate preamble content was a *declaration* (`extern`, `typedef`) or an
internal-linkage constant, both safe to duplicate. **This one is not.** J26
prescribes an enumerated deviation: lines 16-17 stay in the original TU and
appear in no other. **Every remaining unit must grep its preamble for
out-of-class static member definitions before copying it.**

**37. ✅ THE CORRECTION-21 AUDIT OF ALL THIRTEEN WAVE-6 PLANS CAME BACK CLEAN.**
Every file-scope symbol in every one of the thirteen files was enumerated
mechanically, its readers located, and each reader's line mapped onto the range
its plan assigns:
- **Six files are CLEAN** — zero file-scope statics, zero anonymous namespaces,
  zero file-scope constants: `action_dispatcher.cpp`, `detail_texture_atlas.cpp`,
  `model_loading.cpp`, `renderer_player_physics.cpp`, `spell_visual_fx.cpp`,
  `variant_loading.cpp`.
- **Five are satisfied by construction** — definition and every reader inside one
  range: `combat.cpp` (`ClientTarget_Struct`, readers 220/221/261/262, all in the
  stays range), `eq_merchant.cpp` (both `kTradeskillFavorites*`, all in
  1346-1932), `zone_geometry.cpp` (**all six** tessellation entities and their
  readers inside the single `EQT_HAS_TEXTURE_ATLAS` region 923-1560 — ⚠ **a
  three-way split there WOULD have been J08's defect**), `sky_renderer.cpp` and
  `renderer_frame.cpp` (their `extern`s are declarations of symbols defined in
  `COpenGLES2Driver.cpp`).
- **Two need an enumerated preamble deviation**, both prescribed in their plans:
  `daybreak_connection.cpp` (correction 36 above, plus moving the three constants
  to the one TU that reads them) and **`eq_movement.cpp`, the only file in Wave 6
  that genuinely needs an internal header** — its eight `const float` constants
  have readers in **two** of its three ranges, so no assignment of the definitions
  to a single range works. `include/client/eq_movement_internal.h`, 19 lines,
  §Process step 4, precedent `wld_internal.h` / `command_processor_internal.h`.

**38. ⚠ T312 AND T313 ARE BOTH MERGED — the two blocked Wave-6 files are
unblocked, verified rather than assumed.** `git merge-base --is-ancestor
c9b02f7e HEAD` succeeds (T312, `Merge branch 'task/T312' into fable`), and T313
landed at `f0046f7c`. **The in-flight collision matrix's T312 row is now
historical.** `renderer_frame.cpp` (J31) and `renderer_player_physics.cpp` (J33)
carry no hold.

**39. Nothing shrank, and the residual numbers were stable for once.** Twelve of
the thirteen files measured **exactly** what §Residual recorded; the thirteenth,
`eq_merchant.cpp`, is **1,932, not 1,931** (+1). **None is under 1,500.**
Add-site counts, which the shape of a file never predicts: J23 1, J24 1, **J25 6**
(five in `tests/cmake/commands.cmake`), **J26 4** (three in
`tests/cmake/network.cmake`), J27 1, J28 1, **J29 7**, J30 1, J31 1, **J32 12**,
J33 1, J34 2 (⚠ `model_viewer`, not just `GRAPHICS_SOURCES`), **J35 7**. **J29,
J32 and J35 all touch `gles2_zone_benchmark`** and share five to seven CMake lines
with each other — merge one at a time.

## ⚠ CORRECTION 40 FROM J31 (2026-09-02) — THE PRESCRIBED FILE HEADER CAN DISARM THE GUARD IT DOCUMENTS

**§File-header comments tells each new TU to warn about any test that scans it
as text. Following that literally — naming the scanned tokens verbatim — BREAKS
ONE GUARD LOUDLY AND SATISFIES THREE SILENTLY.**

J31 caught this in draft, before building. Its header named the tokens
`tests/test_static_ui_scroll.cpp` looks for:
- the **counting** guard requires exactly two reads of the wheel accumulator;
  mentioning it once in a comment made the count **3**. Loud, harmless.
- **the dangerous half:** three guards are `find(...) != npos` checks. A header
  comment containing the literal gate expression **satisfies them on its own** —
  so the test would stay green after someone deleted the real gate from the
  code. **The prescribed defence would have disarmed the guard it defends.**

**Rule: a file-header warning must DESCRIBE the scanned tokens in words and say
why they are not spelled out. Never quote a token a text-scanning test greps
for.** J31's two headers are the reference implementation.

⚠ **This generalises past headers: any comment anywhere can satisfy a
`find != npos` guard.** The `tests/` text-scanning guards are structurally
weak in this way, and the batch has now met three flavours of them (readFile
counting, `find != npos` presence, string-compare exclusion, plus J33's
filename-keyed sweep). Worth boarding as its own hardening item once Wave 6
lands.

### J31's result

`renderer_frame.cpp` 1,752 → **513 / 747 / 597**, method split 1/7/1 as
predicted, **body estimates exact**, zero drift. **Verifier prediction held to
the line** (+6/−4 including the Counter arithmetic on two identical lines).
Three proofs, all exit 0.

**Two record errors it corrected.** The orchestrator's dispatch brief said
**12 CMake add-sites; the real number is ONE** (`CMakeLists.txt:489`) — 12 is
**J32's** figure, mis-transcribed into J31's brief. J31 searched all 23 CMake
files with `-i` before concluding, so neither correction 26 nor the uppercase
trap could have hidden anything. And the file's text-scan record named the wrong
test file for all three sites, as the dispatch had already flagged.

⚠ **Unlike J06/J07/J08's stale citations, J31's doc rot is NEWLY CREATED** — four
comment-only citations were accurate at its base and now name the wrong TU.
Attribute those to J31 in the doc-hygiene queue rather than to pre-existing rot.

**Correction 30 did not fire** — appending the closes to a body range that
already ends on the original's blank line produces no double-space. And
§File-header comments asks for "five to eight lines" *and* a warning paragraph
that cannot fit in eight; content wins, relax the wording.

## ⚠⚠ CORRECTION 41 FROM J25 (2026-09-02) — THE IN-SUITE VISUAL GATE MAY PROVE NOTHING, AND THE BAR'S ORDER CAUSES IT

**`scripts/ci/visual_gate.sh` runs against `build-gles2/` — a SEPARATE build
tree — and the acceptance bar lists `build-gles2-x86.sh` AFTER `ctest -j6`.** So
a unit that follows the bar in order runs the in-suite `visual_gate` test
against **whatever binary was in `build-gles2/` beforehand**, which predates its
own edits. J25 measured this: its gate run used a `gles2_zone_benchmark`
**stamped 11:01 against edits made at 11:55**, and it **passed while proving
nothing**. It then re-ran the gate standalone after the gles2 build.

**FIX THE ORDER: run `scripts/build-gles2-x86.sh` BEFORE `ctest -j6`**, or re-run
the gate standalone afterwards and say which you did. Every remaining unit must
do one or the other.

⚠ **Retroactive scope, stated honestly:** any earlier Batch J unit that followed
the bar's written order may have a vacuous gate result. What is NOT in doubt for
those units is that they compiled under gles2 (the build ran and exited 0) and
that **goldens were untouched** (`git status` empty is independent of the gate).
What is in doubt is whether the gate exercised the split. The orchestrator is
re-running the gate on the accumulated tree rather than assuming per-unit.
**A stale-binary pass is the same failure family as the other three the batch has
found — a green result that answers a question nobody asked.**

### J25's result

`action_dispatcher.cpp` 2,081 → **705 / 743 / 695**, zero range drift (both cuts
and all 29 banner line numbers exact). **The +5/−1 verifier prediction held
EXACTLY.** Both proofs exit 0, extracted from the produced files. Six CMake
add-sites, all six line numbers exact.

**Its guard handling is the best in the batch and should be the pattern.** For
`:357` it built the split, ran the sweep **before** repointing, and recorded the
failure: 102 handler methods named — precisely the "the split deleted the
handlers" misdiagnosis. For the silent `:394` it measured that the
external-caller regex matches **zero times in all three TUs** (the dispatcher's
internal calls are unqualified), so all three exclusions are defensive, and it
**added** rather than replaced so the guard stays exactly as strict.

Three smaller record errors: §Measured says six `#include`s (five) and describes
a file-header comment block at 1-5 that **does not exist** — the file had no
header at all, so all three TUs got newly written ones; and the method count is
184 or 182 or 183 depending on the counting pattern, so plans should state
theirs. Correction 30's artefact appeared **inverted** (no double-spaced tail;
instead no blank after the namespace open) — cosmetic, declared not fixed since
fixing breaks the byte proof.

## ⚠ CORRECTIONS FROM J33 (2026-09-02)

**42. Correction 40's blanket "never quote a scanned token" is the RIGHT RULE,
but plans should say WHICH GUARD SHAPES ARE ACTUALLY VULNERABLE.** J33 quoted a
token, then reworded — and observed that in *that* guard quoting was harmless:
`test_first_person_camera_single_formula.cpp` calls `stripLineComment` first,
and its assertion is an **at-most-N-readers count**, so a comment can only add a
false positive, never disarm. The dangerous shapes are the ones J31 and J13 hit:
`find(...) != npos` **presence** checks (a comment satisfies them) and
**string-compare exclusions** (a comment cannot, but a missed repoint silently
inverts them). Keep the blanket rule — it is cheap and always safe — but say why,
so an executor can tell a loud failure from a silent one.

**43. `find -newermt '-15 minutes'` is rejected by this host's `bfs`-backed
`find`.** Use `-mmin`.

**44. Correction 41 bit J33 too, and the fix worked.** Its in-suite
`visual_gate` (#3504) ran against a `build-gles2/` tree predating its edits; it
re-ran the gate standalone after `build-gles2-x86.sh` (binary stamped 12:44
against ~11:55 edits) and **the standalone run is the one that counts**. Every
remaining unit: build gles2 BEFORE ctest, or re-run the gate standalone and say
which.

⚠ **The truncation trap fired a FIFTH time** — five zero-byte `build/bin/test_*`
after an exit 143, all caught by `readelf -h`. Separately, both
`build-gles2-x86.sh` 143s were killed *links* that gmake deleted cleanly
(J13's absent-not-truncated shape). **Same trigger, two outcomes, and the scan
is what tells them apart.**

### J33's result

`renderer_player_physics.cpp` 1,546 → **839** (one method) + 742 (17 methods).
**Verifier exit 0 as predicted, +34/−0**, every line accounted for. Body
predictions **exact**; zero range drift. Three proofs, all exit 0, including a
byte-for-byte diff of the 806-line `updatePlayerMovement` across the split.

**It verified the filename-keyed constraint independently before cutting rather
than trusting the plan:** `kFloorSecondReader = "renderer_player_physics.cpp"`
is compared against `path.filename()` for one tunable read at line 135, inside
`updatePlayerMovement` — so the method had to stay in a file of that name,
which forced the single cut and sent the collision family out. **No test file
was edited**, and the new TU matches none of the sweep's tokens, so it is
invisible to both parts despite entering the recursive sweep.

**Item 12 is settled and should not be re-opened:** 673 lines of headroom means
no ceiling problem, and decomposing an 806-line method on the hottest
player-facing path *inside a verbatim-relocation commit* would destroy the only
cheap proof available. Recorded as Batch-G with the guard note attached.

§Measured's first-person token list is incomplete — it omits
`kFirstPersonMaxUpPitchDegrees`, which the guard does not cover.

## ⚠ CORRECTIONS FROM J26 (2026-09-02)

**45. GENERALISE CORRECTION 40: a comment can satisfy ANY text check, not just a
test guard.** J26 found the mechanism firing on a **plan-mandated grep**. Its
Review item 11 verifies uniqueness with `grep -rn 's_debug_level = 0' src/`,
while §File-header tells the stays TU to "record that the definition lives
here" — **a header quoting the definition makes that grep return two hits from
one definition.** Neither correction 40 (test guards) nor 42 (which guard shapes)
anticipated a plan's own verification step. **Rule: never quote a token that any
check greps for — test, script, or the plan's own acceptance step.** J26
described it in words instead.

**46. State the counting pattern whenever a plan quotes a method count.**
§Measured's "50 methods" reproduces under **no** pattern (45 by
qualified-name-at-column-0, 49 by `^{`). And the per-TU prediction was wrong for
one TU — 5 rather than 30, because two methods inside the range were omitted from
the split table. **Third unit running where per-TU method arithmetic slipped
while the RANGES held exactly** (J13, J25, J26). Ranges are reliable; derived
counts are not.

**47. A plan's prose sum must be checked against its own table.** J26's
§The split sums 479+731+775 against a row labelled 793 — a reader verifying the
arithmetic finds a phantom 18-line gap that does not exist.

**48. Correction 30's mechanism is broader than stated** — it fired twice in a
file with **no namespace close at all**, because the prescribed preamble slices
begin and end adjacent to a blank. Cosmetic; declare rather than hand-fix
(fixing breaks the byte proof).

### J26's result — a brand-new hazard class that cost nothing

1,985 → **483 / 748 / 799**. **Verifier exit 0, `+38/-0`, zero substantive** —
the plan's `+0/-0` prediction held, and the 38 is derivable (24 header-comment
lines + 7 includes × 2 new TUs). Both proofs exit 0, plus the correction-7
whole-file check showing exactly 18 added and 0 removed. Zero range drift on
every measured quantity.

**The out-of-class static member definition** (`s_debug_level`) stayed in one TU
only; the declaration is in the header and the value is reached solely through
inline accessors, so the other two TUs need no definition. The three tuning
constants **moved** rather than duplicating, because all their readers are in one
range. **No internal header was needed.** The hazard was identified in planning,
and the unit hit nothing it had not been told about.

**The CRC capture suite was reported explicitly, as asked: 7/7 PASSED** —
standalone and in ctest, with all three `test_daybreak_*` binaries confirmed
RELINKED at the binary level rather than trusted from a green aggregate, because
skip-gated targets can go green without running.

**Correction 41 followed correctly:** `build-gles2-x86.sh` ran BEFORE `ctest`,
benchmark stamped 13:40 against 13:07 edits, so no standalone re-run was needed.

## ⚠ CORRECTIONS FROM J32 (2026-09-02)

**49. ORCHESTRATOR-SIDE, TWICE IN A ROW: STOP TRANSCRIBING CMAKE ADD-SITE COUNTS
INTO DISPATCH BRIEFS.** J31 was handed J32's twelve; J32 was handed J31's/J29's
seven. Both executors derived the truth themselves and were unharmed, but a less
careful one would hunt for sites that do not exist or stop early believing it was
done. **The brief must say "derive the count yourself and report it", never quote
a number.** The plans and correction 39 were right both times; the transcription
into the brief was the defect.

**50. NEW GREP TRAP:
`grep -rn --include='CMakeLists.txt' <dir>` DOES NOT MATCH `<dir>/CMakeLists.txt`
on this host** (ugrep-backed). J32's first sweep returned only the three
`tests/cmake/` hits and **looked complete — 3 of 12.** Same shape as correction 26
(a silent partial answer), different cause. **Grep the root file BY NAME as well
as recursing.** That is now three distinct ways to under-count CMake sites:
naming `tests/CMakeLists.txt` (post-J20), lowercase commands, and this.

**51. The correction-21 audit pattern MISSES NAMESPACE-SCOPE FREE FUNCTIONS.**
`zone_geometry.cpp` has three; `heightToColor` is called at 1531 — **across the
cut** — and is safe only because the public header declares it. **A `static` in
that position would have been J08's defect and the audit would not have seen
it.** Widen the pattern: file-scope symbols are not only `static` and anonymous
namespaces.

**52. `grep -c '<GUARD>'` is NOT a matched-pair check for a preprocessor fence.**
J32's plan asked for one; it reports 3 vs 1 here because the preamble's `#endif`
carries no trailing comment, which reads as an unbalanced fence. **Use a
directive dump** (J32's `awk` form) instead.

**53. Two more derived-count slips** (now four units running): the predicted
12/2 method split is 13/1, and a prose sum `34 + 643 + 2 = 679` omits the two
namespace opens against a correct table row of 681 — **exactly the off-by-two
that correction 20 warns gets blamed on a dropped range.**

### J32's result

1,637 → **996 + 696**, zero drift on every line number in the plan. **Verifier
exit 0, +0/−0 substantive, prediction exact.** Three proofs, all exit 0,
including a byte-for-byte diff of the whole 638-line `EQT_HAS_TEXTURE_ATLAS`
region and an `awk` fence-balance check on both files.

**Two-not-three confirmed independently:** all six file-local entities terminate
their consumer chain inside the single `#ifdef`, so there is no legal third cut —
one would orphan four statics *and* break the fence, **and both halves would
still compile in the default config**, which is what makes it dangerous rather
than merely wrong.

**Correction 41 followed correctly:** gles2 built before ctest, so the in-suite
gate exercised a benchmark stamped by this unit's own build.

## ⚠ CORRECTIONS FROM J23 (2026-09-02)

**54. "PROMOTING A HELPER TRIPS THE VERIFIER BY CONSTRUCTION" IS FALSE — and the
sharper rule explains every earlier exception.** J23 promoted eleven constants
into a new internal header and got **exit 0, +0/−0 substantive.** The verifier
compares distinct normalized lines as a multiset across the whole changeset: the
eleven lines leave the `.cpp` and reappear **unchanged** in the header, count 1
before and 1 after, so they cancel.
**A promoted symbol costs verifier lines only if its TEXT changes or its COUNT
changes.** J04 paid 8 because it rewrote signatures and added `using`
declarations; J05 paid 2 because it dropped `static`; J11 paid 65 because its
preamble lines were **duplicated ×5**. **Correction 18 — keep the symbol in its
existing namespace — is what buys the zero.** (The orchestrator's dispatch brief
asserted the opposite; the plan was right.)

**55. ⚠ `grep -rn --include=<pat>` SILENTLY RETURNS NOTHING on this host**
(ugrep 7.8.4) even where the file demonstrably exists. **A unit counting CMake
add-sites that way reports ZERO and looks thorough.** Use
`find … -exec grep -Hn … {} +`. This is the **fifth** member of the
silently-empty-grep family, after: lowercase CMake commands, `{4,}` in a BRE,
`--include='CMakeLists.txt' <dir>` missing `<dir>/CMakeLists.txt`, and naming
`tests/CMakeLists.txt` post-J20. **On this host, an empty grep result is not
evidence of absence until the pattern is proven to match something.**

**56. A killed `ar` is a THIRD exit-143 site, and the prescribed ELF scan cannot
see it.** The record had killed compiles (truncate) and killed links (delete).
J23's x86 build died **archiving `libwilleq_core.a`**; gmake deleted it, so it
behaved like a link — but **scanning `build/bin/*` would not catch a truncated
static library at all.** Widen the scan to `lib*.a` as well as binaries.

**57. Predicted TU sizes must state the preamble convention they assume.** J23's
assumed the 38-line old header rides along while §File-header comments tells the
executor to write a new one. Better shape: `(your header) + <constant> + body`.
Bodies were exact to the line; zero range drift on anything measured.

Two smaller record errors: the file-header block is 1-38, not 1-30 (replacing
only 1-30 orphans eight NOTE lines), and the "two blank lines" claim is false —
141 single-blank runs and exactly one double, neither at a cut.

### J23's result

2,139 → **486 / 854 / 889** plus a 19-line internal header, exactly as
prescribed. **Verifier exit 0, no sign-off needed.** Both proofs run over lines
70-2139 with the header's eleven lines **re-inserted at their original offset**
(J04's method), and the script asserts the header's content matches the original
range **programmatically** rather than by eye. `body(TU)` was defined
structurally rather than by an offset constant, so no arithmetic could silently
shorten a range — the failure mode that produced J08's alarming false alarm.

**Gate ordering: the fix, not the workaround** — gles2 built before ctest, gate
run against a benchmark stamped by this unit's own build.

⚠ Five `tests/` comments **newly** name the wrong TU (attribute to J23, not to
pre-existing rot).

## ⚠ CORRECTIONS FROM J35 (2026-09-02) — AND THE BATCH-G QUESTION IS CLOSED

**THE `variant_loading.cpp` DESIGN DECISION IS WITHDRAWN. It was an artefact of
assuming a two-way split.** J35 re-derived the arithmetic independently and
agreed with the plan: the two dominant methods are **815** and **396** lines
(1,211 of 1,607, 75.4%), and the three-way cut at 382/1198 puts them in
**different** TUs. Result **391 / 849 / 440**, largest TU **849 with 651 lines of
headroom**; the floor is `preamble + largest method` = 830 against a 1,500
ceiling. §Residual's "batch's worst floor / strongest Batch-G candidate" was
measuring the only shape where the worry is real. **Nothing was decomposed, and
no decision is owed.** Both methods are flagged in their TUs' headers so it is
not re-opened. (Same outcome as J33's 806-line method with 673 spare.)

**58. TELL THE EXECUTOR TO COMPUTE HEADROOM, NEVER QUOTE IT.** J35's plan
prescribed 667/1,075 and its own prescribed headers falsify them (real:
651/1,060). Correction 57's `(your header) + <constant> + body` shape removes
this class of reconciliation entirely — here the constant is 15.

**59. Root CMake line numbers are stale in every unit now — THIRD RUNNING —
while the COUNTS have been right.** J35 found all seven targets named correctly
and **all five root line numbers stale** (+7 to +19). Plans should name targets
and never cite CMake line numbers.

**60. The `awk` preprocessor cross-check needs a POSITIVE CONTROL.** J35's file
has zero directives and so does `model_loading.cpp` — an empty result is
indistinguishable from a broken pattern. **Prove the pattern matches somewhere
before believing a zero** (the same discipline correction 55 forces on greps).

### J35's result

**Verifier exit 0, +0/−0 substantive, prediction exact.** FOUR proofs, all exit
0: byte-identical re-concatenation over 1,590 lines, line-multiset, **and
byte-for-byte diffs of both dominant methods across the split** (815 and 396
lines) — the strongest evidence any unit has produced that a large method
survived relocation intact. `body(TU)` defined structurally, not by offset
constants (J23's method).

**Its correction-21/51 audit is the reference:** it pasted the *widened* grep —
every column-0 non-comment, non-directive, non-`}` line — proving the file
defines zero file-scope symbols of any kind, including the namespace-scope free
functions the standard pattern misses. And it **proved every empty grep against
a file where the pattern must match** before believing the zero.

**Correction 41's fix used** (gles2 before ctest). **Correction 56's `ar` site
fired again** — main build killed while archiving `libwilleq_core.a`, gmake
deleted it, scan widened to `lib*.a` with `ar t`: 221 binaries + 2 archives,
0 BAD.

J35 creates **zero** `tests/` doc rot — the plan's "comment-only hits" claim was
wrong; there are no comment hits at all.

## ⚠ CORRECTIONS FROM J24 (2026-09-02)

**61. THE WIDENED CORRECTION-51 GREP HAS NOW SCORED TWICE RUNNING — MAKE IT THE
ONLY PATTERN.** `grep -n '^static \|^namespace'` returns nothing for
`combat.cpp` (exit 1), exactly as its §Measured claimed. The widened pattern
finds `IsCorpseEntity` at 1658 — **a namespace-scope free function, J32's exact
shape.** Safe here twice over (declared in the public header; definition and its
sole reader both inside one range), but the narrow pattern **cannot see this
class of symbol at all**, and it is the class that produces a non-compiling plan.
Use only:
`grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' <file> | grep -v '<Class>::'`

**62. CORRECTION 45 NEARLY FIRED ON A PLAN'S OWN ACCEPTANCE STEP — THIRD TIME
THIS CLASS.** J24's item 11 prescribes `grep -n 'pragma pack'` across all three
TUs while §File-header comments tells each TU to explain why it has no pack
directive — **a header spelling the token out makes the new TUs return comment
hits that read exactly like a half-copied pack pair.** (J26 hit the identical
shape on `s_debug_level`; J31 on a test guard.) All three headers describe it in
words; item 11 comes back clean. **Any plan that prescribes a grep AND a header
mentioning the same token is self-contradicting by construction.**

**63. Method counts slipped for the FIFTH consecutive unit while ranges held
exactly.** Predicted 30/20/27, real 25/27/26 — and note the plan-style pattern
`^[A-Za-z_].*<Class>::` **silently misses the constructor.** Plans should stop
predicting per-TU method counts, or state the pattern and accept they are
indicative.

**64. Another prose-vs-table contradiction (correction 19/47 class):** §The
split's line column used `49 + range` where §Includes prescribes `15 + range`,
making both new TUs 34 lines too high. The plan's own arithmetic sentence was
right and its table was wrong.

### J24's result

2,119 → **797 / 596 / 789**, zero range drift on every line number including all
six preprocessor directives. **Verifier exit 0, +0/−0 substantive** (61
boilerplate = 33 header + 14 preamble × 2). Both proofs exit 0 over 2,070 lines,
extracted from the produced files.

**The combat suite was reported explicitly, as asked: `test_combat_manager`
66/66**, standalone against the relinked binary and in ctest; the whole
combat selection 163/163; **no test file touched.** Correction 41's fix used
(gles2 before ctest, benchmark stamped after the edits).

**CMake: ONE site, derived** — and the recorded `SET(` opener was 2 lines stale,
while the plan's named false positive had moved 10 lines. Third consecutive unit
where counts were right and line numbers were not.

**Dead-content site #8 confirmed and moved verbatim:** `Consider_Struct` +
`Action_Struct`, 26 lines, zero readers — the 15 tree-wide grep hits are all
different symbols. Now at `combat.cpp:30-39` and `:41-55`.

**Doc rot split honestly per J31's distinction:** five citations in
`test_combat_manager.cpp:700` were **already stale at base** (checked line by
line — none is a loot call), while four other mentions were accurate at base and
now point at the wrong TU. Queue the latter against J24, not against pre-existing
rot.

## ⚠ CORRECTIONS FROM J29 (2026-09-02)

**65. A FOURTH EXIT-143 OUTCOME: "COMPLETED BUT UNRECORDED". AN ABSENT EXIT FILE
IS NO RESULT, NOT A BAD ONE.** J29's ARM build log ends with the script's own
`=== Build complete ===` — it **finished**, and the tool killed the wrapper
before the trailing `echo EXIT=$?` ran, so **no exit file existed at all**.
Classifying that from the log would have been the exact trap the dispatch warns
about. The taxonomy is now: killed compile (**truncates**), killed link
(**deletes**), killed `ar` (**deletes an archive the binary scan cannot see**),
and **finished-but-unrecorded (no exit file)**. J29 re-ran and took exit 0 from
the re-run — correct. ⚠ **The orchestrator hit this same shape three times today
from the other side**, reading a wrapper's exit code instead of the command's.
**Rule: read the exit code from inside the log; if it is absent, you have no
result and must re-run.**

**66. DROP PER-TU METHOD COUNTS FROM PLANS ENTIRELY — correction 46 is now FOUR
FOR FOUR** (J13, J25, J26, J29). J29's plan predicted 7/3/7 against an actual
8/4/5 (total 17 correct) because its split table simply omitted four methods.
**Ranges have been exact in every single unit; derived counts have slipped in
five.** The count adds nothing the ranges do not already pin, and a wrong one
costs an executor a reconciliation.

**67. Include counts are a third derived quantity that keeps slipping** — the
preamble has **eleven** `#include` lines, not ten. **Third Wave-6 unit to correct
an include count**, and it only surfaced because J29 derived its verifier total
(45) from first principles rather than accepting the prediction.

### J29's result

1,793 → **568 / 688 / 586**, all three exactly as predicted, **zero range
drift**. **Verifier exit 0, +45/−0, zero substantive.** Three proofs, all exit 0,
including a byte-for-byte diff of the 326-line `loadModelFromCachedChr` across
the split.

**Its correction-21 evidence is the form to copy:** it pasted the grep *and its
exit status* (no output, exit 1), then stated the inference explicitly — the set
of file-local symbols is empty, therefore the set of disagreeing
(definition, consumer) pairs is empty — and confirmed against the build. It also
ran correction 36's pre-check for an out-of-class static member definition in the
preamble.

**Correction 41's fixed order used**, benchmark stamped 14:34:56 against 13:59:03
edits. Seven CMake sites found and edited; root line numbers drifted +4/+8 while
the target names were right — **fourth consecutive unit with that exact shape.**

## Unit table

| Unit | File | Lines | → TUs | Largest after | Wave | Notes |
|---|---|---|---|---|---|---|
| ~~J01~~ (merged `7e0c7823`) | `inventory/inventory_manager.cpp` | 2,502 | 5 | **800** | 0 (pilot) | ✅ **DONE** `task/J01-inventory-split` `0166fae0`. 581 / 800 / 503 / 414 / 276. Zero hazards, verifier clean, gate PASS, goldens untouched. Six template corrections folded into §Process above |
| J02 | `eq.cpp` | 3,913 (**3,923**) | 6 | ~1,065 | 1 | `DispatchZonePacket` becomes its own ~965-line TU **with no room to grow** — flag it, do not decompose it |
| J03 | `graphics/entity_renderer.cpp` | 3,255 | 7 | ~500 | 1 | ⚠ **blocked on T318.** `updateFirstPersonBodyDrawRule()` is ALREADY its own TU (`entity_first_person_draw.cpp`, 90 lines); keep the rest of the first-person family with it |
| J04 | `action/command_processor.cpp` | 3,024 | 5 | ~800 | 1 | ⚠ must NOT split `registerBuiltinCommands` (registration ORDER is load-bearing). ⚠ 3 anonymous namespaces, one used across two clusters → needs `command_internal.h`. ⚠ 4 sites in `tests/CMakeLists.txt` |
| J05 | `graphics/eq/wld_loader.cpp` | **2,908** | 7 +1 hdr | **~748** | 2 | ⭐ **planned: `refactor/J05_wld_loader_split.md`.** needs `wld_internal.h` (`read_val<T>` used 114×) — so it **exits the verifier 1 by construction** (correction 5). ⚠ **NINETEEN CMake add-sites**, one of them `gles2_zone_benchmark` (the visual-gate binary). ⚠ worst mutation score in the repo |
| J06 | `application.cpp` | **2,368** (survey said 2,356; T319 grew it) | 5 | **~947** | 2 | ⭐ **planned: `refactor/J06_application_split.md`.** `s_terminateRequested` + 4 signal handlers: read by `mainLoop`, address-taken by `initialize`. Two options in the plan; the executor argues its choice |
| J07 | `eq_player_info.cpp` | **2,301** | 4 | **~753** | 2 | ⭐ **planned: `refactor/J07_eq_player_info_split.md`.** 118 small methods, no statics, no namespace, no hazards — **lowest risk in the batch, identical bar.** The batch's control sample for what the bar alone costs |
| J08 | `eq_entities.cpp` | **2,786** (survey said 2,736; T318 grew it) | 4 | **~991** | 3 | ⭐ **planned: `refactor/J08_eq_entities_split.md`.** also relocates **209** lines (not 161) into `eq_group.cpp`, which is **760** today → ~971. ⚠ the only unit that moves code INTO an existing file: narrow enumerated includes there, and both proofs must count it. 2 CMake sites |
| J09 | `graphics/zone_shader.cpp` | **2,561** | **1 TU + 3 hdrs** (not 3+2) | **~1,010** (a header) | 3 | ⭐ **planned: `refactor/J09_zone_shader_split.md`.** **1,362** GLSL lines in two identical-identifier `#ifdef`/`#else` arms → two private headers, `#else` becomes `#ifndef`; the 3 callback classes are all-inline and instantiated 18× by the ctor → a third header, no new `.cpp`. ✅ **no `friend` needed — measured.** Predicted verifier exit **0**. 4 CMake sites, one `gles2_zone_benchmark`, none needing an edit |
| J10 | `graphics/constrained_texture_cache.cpp` | **2,211** | 4 | **~812** | 3 | ⭐ **planned: `refactor/J10_constrained_texture_cache_split.md`.** ⚠ the 299-line `#if 0` T04 region (with a SECOND `isPending`/`hasPendingWork` and 4 nested `#ifdef` pairs) travels intact in one TU. Predicted verifier exit **1 with exactly 21 lines** (2 `typedef`s + 1 `extern`, × 3 TUs). ⚠ **TWELVE CMake sites**, one `gles2_zone_benchmark`, three in `tests/` |
| J11 | `graphics/irrlicht_renderer.cpp` | 3,237 | 6 | 980 | 4 | **hard dependency: T312 must merge first** |
| J12 | `eq_action_handler.cpp` | **2,248** (survey said 2,202) | 3 | **~952** | 4 | ⭐ **planned: `refactor/J12_eq_action_handler_split.md`.** **174** forwarding methods (not 173), largest **55**. **Zero statics, zero anonymous namespaces, zero preprocessor directives, zero block comments, zero text readers in `tests/`** — the lowest-risk unit left. Predicted verifier exit **0**. **ONE** CMake site. ⚠ one banner (`:1691`) has no blank line before it and is NOT a legal cut point |
| J13 | `eq_bridge.cpp` | **2,127** (survey said 2,098; `module_map.md` says 2,001) | 3 | **~891** | 4 | ⭐ **planned: `refactor/J13_eq_bridge_split.md`.** ⚠ **`tests/test_reachability_sweep.cpp` scans this file AS TEXT at three sites and all three break** — the repoint is in territory, not optional (correction 27). `ProcessBridgeIntents` is **580** lines (not 558) and gets its own ~700-line TU — **flagged Batch-G, not decomposed**. Three preprocessor regions span whole methods. Predicted verifier exit **1 with exactly +3/-2**, all in the test repoint. **ONE** CMake site |
| J14 | `include/common/packet_structs.h` | **2,215** | umbrella + `packets/*.h` | — | 5 | ⚠ `#pragma pack(push,1)` per header; T145/T147 bit this twice |
| J15 | `include/client/action/action_dispatcher.h` | **1,581** | 3 | 1,060 | 5 | after J04 and J12 |
| J16 | `include/client/eq.h` type extraction | **2,645** | +3 hdrs | ~1,930 | 5 | after J02, J08; leave forwarding includes |
| J17 | `include/client/graphics/irrlicht_renderer.h` | **2,520** | +2 hdrs | ~1,820 | 5 | after J11 |
| J22 | `graphics/detail/detail_manager.cpp` | 1,754 | 2 | ~1,206 | 6 | **in flight** — lifting the 548-line `createDefaultConfig` table alone clears it |
| J23 | `eq_movement.cpp` | **2,139** | 3 | **~908** | 6 | ⭐ **planned: `refactor/J23_eq_movement_split.md`.** ⚠ **the only Wave-6 unit needing an internal header** — 8 `const float` with readers in two ranges → `include/client/eq_movement_internal.h`, 19 lines. Predicted verifier **0**. 1 CMake site |
| J24 | `combat.cpp` | **2,119** | 3 | **~809** | 6 | ⭐ **planned: `refactor/J24_combat_split.md`.** ⚠ `#pragma pack(1)`/`pack()` pair + 3 structs stay with their only reader; `Consider_Struct`/`Action_Struct` are **dead — site #8**. Predicted **0**. 1 CMake site |
| J25 | `action/action_dispatcher.cpp` | **2,081** | 3 | **~729** | 6 | ⭐ **planned: `refactor/J25_action_dispatcher_split.md`.** ⚠ **TWO text-scan sites, not one** — `test_reachability_sweep.cpp:357` and **`:394`, a string-compare exclusion**. **SIX** CMake sites (five in `tests/cmake/commands.cmake`). Predicted **1, +5/-1**. ⚠ **serialise against J15** |
| J26 | `common/net/daybreak_connection.cpp` | **1,985** | 3 | **~793** | 6 | ⭐ **planned: `refactor/J26_daybreak_connection_split.md`.** ⚠ **preamble line 17 is a static-member DEFINITION — copying it is a duplicate-symbol link error** (new hazard class, correction 36). Only `src/common/` file in the batch. **FOUR** CMake sites. Predicted **0** |
| J27 | `eq_merchant.cpp` | **1,932** (was 1,931) | 3 | **~702** | 6 | ⭐ **planned: `refactor/J27_eq_merchant_split.md`.** Zero hazards; six single-line `/*…*/` and no multi-line region. Predicted **0**. 1 CMake site |
| J28 | `graphics/detail/detail_texture_atlas.cpp` | **1,826** | 3 | **~738** | 6 | ⭐ **planned: `refactor/J28_detail_texture_atlas_split.md`.** **Wave 6's control sample** — zero preprocessor, zero statics, zero text readers, 1 CMake site. Both cuts on banner starts. Predicted **0** |
| J29 | `graphics/eq/model_loading.cpp` | **1,793** | 3 | **~683** | 6 | ⭐ **planned: `refactor/J29_model_loading_split.md`.** Three big siblings (326/302/268), one per TU. **SEVEN** CMake sites, ⚠ one `gles2_zone_benchmark`. Predicted **0**. ⚠ shares seven CMake lines with J35 |
| J30 | `graphics/sky_renderer.cpp` | **1,770** | 3 | **~690** | 6 | ⭐ **planned: `refactor/J30_sky_renderer_split.md`.** ⚠ a 124-line trailing `EQT_HAS_GLES2` region must not be cut. Predicted **1 with exactly 6 lines** (3-line `extern` × 2 TUs). 1 CMake site |
| J31 | `graphics/renderer_frame.cpp` | **1,752** | 3 | **~730** | 6 | ⭐ **planned: `refactor/J31_renderer_frame_split.md`.** ✅ **T312 unblocked.** ⚠ **three text-scan sites and §Residual named the WRONG FILES for all three** (`test_static_ui_scroll.cpp:698,706` + `test_ui_hide_command.cpp:156`). Predicted **1, +6/-4**. 1 CMake site |
| J32 | `graphics/eq/zone_geometry.cpp` | **1,637** | 2 | **~994** | 6 | ⭐ **planned: `refactor/J32_zone_geometry_split.md`.** ⚠ **discontiguous stays** (1-918 + 1562-1637); the whole 638-line `EQT_HAS_TEXTURE_ATLAS` region 923-1560 lifts out with all six file-local entities. **TWELVE** CMake sites, ⚠ one `gles2_zone_benchmark`. Predicted **0** |
| J33 | `graphics/renderer_player_physics.cpp` | **1,546** | 2 | **~827** | 6 | ⭐ **planned: `refactor/J33_renderer_player_physics_split.md`.** ✅ **T312 unblocked.** ⚠ **`updatePlayerMovement` MUST STAY in a file with this NAME** — `test_first_person_camera_single_formula.cpp` names it as a string literal. **No Batch-G decision owed** (673 lines headroom). Predicted **0**. 1 CMake site |
| J34 | `graphics/spell_visual_fx.cpp` | **1,517** | 2 | **~841** | 6 | ⭐ **planned: `refactor/J34_spell_visual_fx_split.md`.** Cheapest unit in the batch (17 over), one cut on a banner. ⚠ `namespace EQ`, not `EQT::Graphics`. **TWO** CMake sites (⚠ `model_viewer`). Predicted **0** |
| J35 | `graphics/eq/variant_loading.cpp` | **1,607** | 3 | **~833** | 6 | ⭐ **planned: `refactor/J35_variant_loading_split.md`.** ⚠ **§Residual's "worst floor / Batch-G first" call is REFUTED** — a 3-way split at 382/1198 puts the 815 and the 396 in different TUs, largest 833, **667 lines headroom**. **SEVEN** CMake sites, ⚠ one `gles2_zone_benchmark`. Predicted **0** |
| ~~**J20**~~ ✅ **DONE** | **`tests/CMakeLists.txt`** | **2,850 → 135** | per-domain `.cmake` fragments | — | **none — see below** | ⚠ **HIGH-CONFLICT with every in-flight J unit.** Within 222 lines of the hard cap and every J unit appends to it. Wants a quiet moment, not a wave slot |

Full clearance is roughly **26-30 units**.

## Wave plan — three slots, collisions resolved

- **Wave 0 (serial, pilot):** ✅ **COMPLETE.** J01 landed as five TUs (the plan
  said four — `inventory_manager.cpp` itself stays and counts). Zero hazards
  found, confirming the survey's hazard analysis; six template corrections
  came back and are folded into §Process.
- **Wave 1 (3-way):** J02 / J03 / J04. `irrlicht_renderer.cpp` is deliberately
  NOT here: T312 owns it. ⚠ **J03 is BLOCKED until T318 merges** — slot3 has
  `entity_renderer.cpp` open right now (size-update path). J02 and J04 are
  clear and can go immediately.
- **Wave 2 (3-way):** J05 / J06 / J07. **All three planned** —
  `refactor/J05_wld_loader_split.md`, `refactor/J06_application_split.md`,
  `refactor/J07_eq_player_info_split.md`, each carrying all eight corrections
  inline so no executor rediscovers them. Re-measured at `ed97d701`; the only
  stale survey number was `application.cpp` (2,356 → **2,368**, T319).
- **Wave 3 (3-way):** J08 / J09 / J10. **All three planned** —
  `refactor/J08_eq_entities_split.md`, `refactor/J09_zone_shader_split.md`,
  `refactor/J10_constrained_texture_cache_split.md`, each carrying all
  eighteen corrections inline so no executor rediscovers them. Re-measured at
  `f215651a`; the stale survey numbers were `eq_entities.cpp` (2,736 →
  **2,786**, T318) and its relocation size (161 → **209**), and
  `zone_shader.cpp`'s GLSL block (1,345 → **1,362**). ⚠ **J09 and J10 both
  touch `gles2_zone_benchmark`.**
- **Wave 4 (after T312 AND T313 merge):** J11 has already landed, so this is a
  **2-way wave: J12 / J13.** **Both planned** —
  `refactor/J12_eq_action_handler_split.md`,
  `refactor/J13_eq_bridge_split.md`, each carrying all twenty-eight corrections
  inline so no executor rediscovers them. Re-measured at `136a319b`; **both
  survey numbers were stale** (2,202 → **2,248**, 2,098 → **2,127**) and so was
  `ProcessBridgeIntents` (558 → **580**). ⚠ **Neither touches
  `gles2_zone_benchmark`; each has exactly ONE CMake site
  (`CLIENT_CORE_SOURCES`). J13 must repoint
  `tests/test_reachability_sweep.cpp`; J12 touches no test.** The two file
  territories are disjoint, so they can run concurrently.
- **Wave 5 (headers, after the policy amendment):** J14 anytime a slot is free;
  J15 after J04+J12; J16 after J02+J08; J17 after J11.
- **Wave 6 (thirteen units, ALL PLANNED, 2026-09-02 at `824adb28`):** J23-J35,
  plus J22 (`detail_manager.cpp`) already in flight. Each plan carries all
  thirty-nine corrections inline so no executor rediscovers them. **T312 and T313
  are merged, verified by `git merge-base --is-ancestor` — J31 and J33 are
  unblocked.** ⚠ **§Residual's Batch-G call on `variant_loading.cpp` is refuted
  (correction 34); its text-scan record was wrong for two files and missed a third
  kind of guard entirely (correction 35).** Recommended dispatch order and the
  serialisation constraints are in §Wave 6 dispatch order below.

### In-flight collision matrix

| in flight | files | blocks |
|---|---|---|
| T311 (oracle capture) | no `src/` changes | nothing |
| T312 (thread safety) | `irrlicht_renderer.h`/`.cpp`, `renderer_frame.cpp`, `graphics_input_handler.cpp`, `renderer_player_physics.cpp` | **J11, J17**, and the Wave-6 `renderer_frame` / `renderer_player_physics` units |
| T318 (size update / guild rank) | `graphics/entity_renderer.*`, `renderer_entities.cpp`, `irrlicht_bridge.cpp`, `eq_entities.cpp`, `eq_guild.cpp`, `spell_effects.cpp`, `event_bus.h`, `tests/CMakeLists.txt` | **J03** (hard). Not J02 (`eq.cpp` untouched), not J04 |
| T313 (UI scroll) | `graphics/ui/`, one hunk in `irrlicht_renderer.h` | **J17** (soft: J11). No `graphics/ui/` file is over 1,500 (largest `static_panels.cpp` 915), so no J unit wants that territory |

## J20 — split `tests/CMakeLists.txt`. ✅ **DONE** — `task/J20-tests-cmake-split`

**Executed 2026-09-02 from `53d819bd`. Review: `refactor/J20_tests_cmake_split.md`.**
2,850 → **135** lines + **18 fragments** under `tests/cmake/`, largest 532
(`commands.cmake`). `ctest -N` 3,499 before and after, proven equal (sorted AND
in ctest order); target list `cmp`-identical at 1,235 lines; `ctest -j6`
3,498/3,499 with only the documented `AudioManagerTest.PreloadCommonSounds`;
from-scratch configure+build reproduces the same list; gate PASS, goldens
untouched; 19/19 files ok on `check_file_sizes.sh`.

⚠ **Every J unit from here adds its test target to
`tests/cmake/<domain>.cmake`, not to `tests/CMakeLists.txt`.**

⚠ **Two traps the review documents — read them before diffing `ctest -N`:**
`test_irrlicht_camera_command` has 8 parameterized test NAMES that embed an
ASLR-randomised pointer and therefore change on every relink (redact
`72-byte object <...>` before comparing); and two consecutive `ctest -N` runs
agree even when the names are unstable, because `PRE_TEST` discovery reuses its
generated file — that check is vacuous. Also: **the session scratchpad is shared
across slots and across earlier sessions**, so a `--exit-file` wait can return
success against someone else's hours-old file.

### Original scoping (kept for the record)

**Why it exists:** `tests/CMakeLists.txt` is **2,778 lines** — over the 1,500
WARN and **within 222 lines of the 3,000 hard cap** — and it holds **175
`ADD_EXECUTABLE` blocks with 177 `GTEST_DISCOVER_TESTS` calls**. §Process
grandfathers the root `CMakeLists.txt` by name; it does **not** grandfather this
one. **Every J unit that touches a test target appends to it** (J04 added 16
lines; J05 will add ~24). Left alone, a later J unit trips a genuine FAIL
through no fault of its own and has to either fix an unrelated build-system
problem inline or get the rule waived — which is exactly the harm T115 recorded
and T166 exists to prevent.

**Scope — mechanical, and nothing more:**
1. Carve the file into **per-domain `.cmake` fragments** under
   `tests/cmake/` (e.g. `network.cmake`, `graphics.cmake`, `wld.cmake`,
   `audio.cmake`, `commands.cmake`, `state.cmake`, …), each holding whole
   `IF(…) / ADD_EXECUTABLE / TARGET_LINK_LIBRARIES / GTEST_DISCOVER_TESTS /
   ENDIF()` blocks moved **verbatim**.
2. `tests/CMakeLists.txt` keeps its preamble — the `INCLUDE(GoogleTest)` and the
   **T163 `PRE_TEST` discovery-mode block, which is load-bearing and heavily
   commented; do not summarise it, do not move it into a fragment** — and
   becomes a list of `INCLUDE(cmake/<domain>.cmake)` lines.
3. **No target renames. No source-list edits. No link-library changes. No
   behaviour change.** Not even a drive-by dedup of a repeated source path.

**Acceptance — the one that actually matters is a target-set diff:**
- `ctest -N` before and after, sorted, **byte-identical**: same test names, same
  count, same order. Paste the diff.
- the same for the target list (`cmake --build build --target help`, sorted).
- full `ctest -j6` green, zero new failures vs `refactor/test_baseline.txt`.
- `check_file_sizes.sh --files` on the new top-level file and every fragment →
  0 warn, 0 fail. The root `CMakeLists.txt` WARN is not this unit's.
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` clean
  (`build-arm-noble.sh` exit 143 = re-invoke).
- a **from-scratch** configure + build, not just incremental: the T163 discovery
  path is exactly what a stale cache would hide.
- `verify_move_only.py` does not apply — CMake is a non-C++ file it declines to
  verify. **The `ctest -N` diff is this unit's central proof in its place.**

⚠ **Scheduling: this unit is HIGH-CONFLICT with every in-flight J unit and with
most T units.** Any unit that adds a test target rebases into a file that no
longer exists in the same shape, and the conflict is not a clean append. **Do
not give it a wave slot alongside other J units.** It wants a quiet moment —
no J unit in flight, no T unit holding `tests/CMakeLists.txt` — and it should
merge alone. Between Wave 2 and Wave 3 is the natural window if one opens;
otherwise before Wave 5 (the header wave), which touches test targets least.

## Optional, NOT part of this batch — the Batch-G follow-up

Eight methods dominate their TUs. Clearing the ceiling does not require touching
any of them, and a unit that does so is out of scope. Recorded so the decision
can be taken separately: `DispatchZonePacket` 880 (`eq.cpp:800`),
`loadModelFromGlobalChrWithVariantsForAnimation` 815 (`variant_loading.cpp:382`),
`updatePlayerMovement` 806 (`renderer_player_physics.cpp:19`),
`registerBuiltinCommands` 768 (`command_processor.cpp:285`),
`Application::initialize` 569 (`application.cpp:105`),
`ProcessBridgeIntents` **580** (`eq_bridge.cpp:892` — was recorded as 558 at `:885`; re-measured at `136a319b`),
`createDefaultConfig` 548 (`detail_manager.cpp:1203` — a data table, leave it),
`processFrameRender` 546 (`renderer_frame.cpp:1143`).

## Residual → **WAVE 6**, re-measured at `136a319b` (2026-09-02)

**This is a measured list, not a survey. Wave 6 can be dispatched from it
without another survey pass** — but each unit still re-measures its own file at
its own tip (correction 9), because these numbers went stale inside a day the
last two times.

`eq_bridge.cpp` has left this list (it is **J13**). Fourteen files remain. The
**largest method** column is what sets a TU's floor (`preamble + largest
method`), so it decides whether a file is a pure Batch-F relocation or needs a
Batch-G decision first.

| file | lines | largest method | floor-setter | note |
|---|---|---|---|---|
| `src/client/eq_movement.cpp` | **2,139** (was 2,033) | **499** `EverQuest::UpdateMovement` (509-1007) | 2nd: 229 `SendPositionUpdate` | grew 106 lines since the survey |
| `src/client/combat.cpp` | **2,119** | 192 `CombatManager::FindNextHuntTarget` (1360-1551) | 2nd: 108 `ListHuntTargets` | ⚠ `CombatManager` is a `friend` of `EverQuest` |
| `src/client/action/action_dispatcher.cpp` | **2,081** (was 2,039) | **68** `ActionDispatcher::interactNearest` (539-606) | 2nd: 24 | ⚠ **TEXT-SCANNED** by `test_reachability_sweep.cpp:356`. Lowest floor in the batch; pairs with **J15** |
| `src/common/net/daybreak_connection.cpp` | **1,985** | **453** `DaybreakConnection::ProcessDecodedPacket` (757-1209) | 2nd: 130 `ProcessPacket` | the only `src/common/` file in the batch |
| `src/client/eq_merchant.cpp` | **1,931** | 219 `EverQuest::SetupTradeManagerCallbacks` (678-896) | 2nd: 105 | |
| `src/client/graphics/detail/detail_texture_atlas.cpp` | **1,826** | 152 `DetailTextureAtlas::createAtlas` (18-169) | 2nd: 84 | |
| `src/client/graphics/eq/model_loading.cpp` | **1,793** | **326** `RaceModelLoader::loadModelFromCachedChr` (867-1192) | 2nd: 302, 3rd: 268 | three big siblings; splits three ways naturally |
| `src/client/graphics/sky_renderer.cpp` | **1,770** | 144 `SkyRenderer::calculateSkyColors` (1168-1311) | 2nd: 124 | |
| `src/client/graphics/detail/detail_manager.cpp` | **1,754** | **548** `DetailManager::createDefaultConfig` (1203-1750) | 2nd: 205 `classifyTexture` | ⚠ **lifting the 548-line config table ALONE clears it** (1,754 → 1,206). A §Optional method, but here relocating it verbatim IS the split |
| `src/client/graphics/renderer_frame.cpp` | **1,752** (was 1,691) | **546** `IrrlichtRenderer::processFrameRender` (1204-1749) | 2nd: 472 `processFrame`, 3rd: 211 | ⚠ **T312 territory.** ⚠ **TEXT-SCANNED at THREE sites** (`test_reachability_sweep.cpp:156`, `test_ui_hide_command.cpp:698,706`) |
| `src/client/graphics/eq/zone_geometry.cpp` | **1,637** | **499** `ZoneMeshBuilder::buildAtlasedMesh` (1061-1559) | 2nd: 259, 3rd: 198 | |
| `src/client/graphics/eq/variant_loading.cpp` | **1,607** | ⚠ **815** `RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation` (382-1196) | 2nd: **396** `preloadVariantModel` | ⚠ **the batch's worst floor: 2 methods = 1,211 of 1,607 lines.** A 2-way split works (~850 / ~780) but leaves no headroom. §Optional already names the 815 |
| `src/client/graphics/renderer_player_physics.cpp` | **1,546** | ⚠ **806** `IrrlichtRenderer::updatePlayerMovement` (19-824) | 2nd: 142 | ⚠ **T312 territory.** Only **46 lines over** the ceiling; one cut after `updatePlayerMovement` clears it |
| `src/client/graphics/spell_visual_fx.cpp` | **1,517** | 125 `SpellVisualFX::createCastGlow` (353-477) | 2nd: 90 | only **17 lines over**; the cheapest unit in the batch |

## Wave 6 dispatch order (recommended, 2026-09-02)

**Thirteen units, three slots. All planned. Constraints, then the order.**

**Hard serialisation constraints:**
1. **J25 must not run concurrently with J15** (`include/client/action/action_dispatcher.h`).
   Same entity; J15 restructures the header J25's every method is declared in.
   Correction 31 is the precedent for what a concurrent edit costs.
2. **J29, J32 and J35 share five to seven CMake add-site lines** (`GRAPHICS_SOURCES`,
   `model_viewer`, `gles2_zone_benchmark`, `gles2_shader_diagnostic`,
   `gles2_composite_audit`, `test_graphics`, `test_asset_loading`). Their source
   territories are disjoint so they may run concurrently, but **merge one at a
   time and rebase**, and never resolve a CMake conflict by taking one side
   wholesale.
3. **J28 must not touch `detail_manager.cpp`** while **J22** is in flight in the
   same directory.
4. **J31 and J33 both insert into `SET(GRAPHICS_SOURCES`** — same rule as 2.
5. **No tree-wide doc pass (J21's successor) alongside any of these** — correction
   31. Scope it to exclude every file with a unit in flight.

**Order, highest information per unit first, riskiest guards early while there is
slack to re-run them:**

| wave | slot 1 | slot 2 | slot 3 | why here |
|---|---|---|---|---|
| **6a** | **J25** action_dispatcher | **J31** renderer_frame | **J33** renderer_player_physics | ⚠ **All three carry a text-scanning guard, and all three of this batch's guard records were wrong.** Run them first, together, so the three corrections land before nine quieter units are dispatched against a stale record. J33 also settles the Batch-G question in practice. |
| **6b** | **J32** zone_geometry | **J26** daybreak_connection | **J23** eq_movement | The three structural one-offs: a discontiguous stays + twelve CMake sites; the batch's first preamble **definition** hazard; the batch's only Wave-6 internal header. Each produces a correction worth having before the bulk units. |
| **6c** | **J29** model_loading | **J35** variant_loading | **J24** combat | J29 and J35 share seven CMake lines — same wave is fine, **merge serially**. J35 tests correction 34's refutation. J24 is independent. |
| **6d** | **J27** eq_merchant | **J30** sky_renderer | **J28** detail_texture_atlas | The three low-risk units. **J28 is Wave 6's control sample** for what the bar alone costs — the role J07 played in Wave 2. |
| **6e** | **J34** spell_visual_fx | — | — | 17 lines over, one cut. ⚠ **Pair it in a slot with any 6d unit that finishes early** — the bar costs the same regardless of file size, and this is the cheapest cut in the batch. |

**J15 goes after 6a** (it is already gated on J04+J12, both merged; adding J25
makes three).

**Predicted verifier outcomes, so a wave's results can be triaged at a glance:**
exit **0** for J23, J24, J26, J27, J28, J29, J32, J33, J34, J35; exit **1** with a
predicted, enumerated list for **J25** (`+5/-1`, all in
`test_reachability_sweep.cpp`), **J30** (`+6/-0`, a 3-line `extern` × 2 TUs) and
**J31** (`+6/-4`, a 1-line `extern` × 2 plus four test-file lines). **Any other
result is a real finding.**

**Scheduling notes for Wave 6, from the measurement:**
- ~~**`renderer_frame.cpp` and `renderer_player_physics.cpp` are both T312
  territory**~~ ✅ **T312 merged (`c9b02f7e`) and T313 merged (`f0046f7c`);
  both verified as ancestors of `824adb28` by `git merge-base --is-ancestor`, not
  read off a note. J31 and J33 are UNBLOCKED.**
- **`action_dispatcher.cpp` should be boarded WITH or AFTER J15**
  (`include/client/action/action_dispatcher.h`, 1,581) — same entity, and J15 is
  already gated on J04+J12.
- **`spell_visual_fx.cpp` (17 over) and `renderer_player_physics.cpp` (46 over)
  are one-cut units.** They still cost the full bar (~30-60 min); that cost is
  fixed per unit regardless of file size. Consider pairing them in one slot.
- ~~**`variant_loading.cpp` … strongest candidate for a Batch-G decision taken
  before its Batch-F split**~~ ⚠ **REFUTED — see correction 34.** That reasoning
  assumed a two-way split. A three-way split at 382 and 1198 puts the 815-line
  and 396-line methods in **different** TUs: 383 / **833** / 425, **667 lines of
  headroom** in the largest. The floor is `15 + 815 = 830`, well under the
  ceiling. **No Batch-G decision is owed.** `refactor/J35_variant_loading_split.md`
  carries the arithmetic. The same question was asked of
  `renderer_player_physics.cpp` (806 of 1,546) and answered the same way — J33.

## Final state after Waves 0-5

All four hard-cap files gone from the FAIL list; the largest production TU in
the tree drops from **3,913 to ~1,035**; both mechanically-splittable headers
fully compliant; `eq.h` and `irrlicht_renderer.h` grandfathered with their
class-decomposition debt named on the board.
