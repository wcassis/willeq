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

Four headers are over the soft limit, in two different shapes:

| header | lines | shape | verdict |
|---|---|---|---|
| `include/common/packet_structs.h` | 2,207 | 125 independent packed structs, already banner-sectioned | **mechanical**, clears fully |
| `include/client/action/action_dispatcher.h` | 1,556 | 3 independent entities (types / `IActionHandler` / `ActionDispatcher`) | **mechanical**, clears fully |
| `include/client/graphics/irrlicht_renderer.h` | 2,395 | 20 free types + one god class | mechanical to ~1,820, then **design change** |
| `include/client/eq.h` | 2,538 | 504 lines of enums + 3 structs + `class EverQuest` (897 declarations) | mechanical to ~1,930, then **design change** |

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

## Unit table

| Unit | File | Lines | → TUs | Largest after | Wave | Notes |
|---|---|---|---|---|---|---|
| ~~J01~~ (merged `7e0c7823`) | `inventory/inventory_manager.cpp` | 2,502 | 5 | **800** | 0 (pilot) | ✅ **DONE** `task/J01-inventory-split` `0166fae0`. 581 / 800 / 503 / 414 / 276. Zero hazards, verifier clean, gate PASS, goldens untouched. Six template corrections folded into §Process above |
| J02 | `eq.cpp` | 3,913 (**3,923**) | 6 | ~1,065 | 1 | `DispatchZonePacket` becomes its own ~965-line TU **with no room to grow** — flag it, do not decompose it |
| J03 | `graphics/entity_renderer.cpp` | 3,255 | 7 | ~500 | 1 | ⚠ **blocked on T318.** `updateFirstPersonBodyDrawRule()` is ALREADY its own TU (`entity_first_person_draw.cpp`, 90 lines); keep the rest of the first-person family with it |
| J04 | `action/command_processor.cpp` | 3,024 | 5 | ~800 | 1 | ⚠ must NOT split `registerBuiltinCommands` (registration ORDER is load-bearing). ⚠ 3 anonymous namespaces, one used across two clusters → needs `command_internal.h`. ⚠ 4 sites in `tests/CMakeLists.txt` |
| J05 | `graphics/eq/wld_loader.cpp` | 2,908 | 7 | 490 | 2 | needs `wld_internal.h` (`read_val<T>` used 114×) |
| J06 | `application.cpp` | 2,356 | 5 | 815 | 2 | signal-handler static promotion |
| J07 | `eq_player_info.cpp` | 2,301 | 4 | 745 | 2 | 118 small methods, no statics — lowest risk |
| J08 | `eq_entities.cpp` | 2,736 | 4 | 970 | 3 | also relocates 161 lines into `eq_group.cpp` |
| J09 | `graphics/zone_shader.cpp` | 2,561 | 3 +2 hdrs | 990 | 3 | 1,345 lines are GLSL string literals |
| J10 | `graphics/constrained_texture_cache.cpp` | 2,211 | 4 | 780 | 3 | carry the `#if 0` guard region intact |
| J11 | `graphics/irrlicht_renderer.cpp` | 3,237 | 6 | 980 | 4 | **hard dependency: T312 must merge first** |
| J12 | `eq_action_handler.cpp` | 2,202 | 3 | 750 | 4 | 173 forwarding methods, trivial |
| J13 | `eq_bridge.cpp` | 2,098 | 3 | — | 4 | |
| J14 | `include/common/packet_structs.h` | 2,207 | umbrella + `packets/*.h` | — | 5 | ⚠ `#pragma pack(push,1)` per header; T145/T147 bit this twice |
| J15 | `include/client/action/action_dispatcher.h` | 1,556 | 3 | 1,060 | 5 | after J04 and J12 |
| J16 | `include/client/eq.h` type extraction | 2,538 | +3 hdrs | ~1,930 | 5 | after J02, J08; leave forwarding includes |
| J17 | `include/client/graphics/irrlicht_renderer.h` | 2,395 | +2 hdrs | ~1,820 | 5 | after J11 |
| J18+ | the residual ~13 files (§Residual) | — | — | — | 6 | re-measure first; several shrink as callers move |

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
- **Wave 2 (3-way):** J05 / J06 / J07.
- **Wave 3 (3-way):** J08 / J09 / J10.
- **Wave 4 (3-way, after T312 AND T313 merge):** J11 / J12 / J13.
- **Wave 5 (headers, after the policy amendment):** J14 anytime a slot is free;
  J15 after J04+J12; J16 after J02+J08; J17 after J11.
- **Wave 6:** the residual list, re-measured.

### In-flight collision matrix

| in flight | files | blocks |
|---|---|---|
| T311 (oracle capture) | no `src/` changes | nothing |
| T312 (thread safety) | `irrlicht_renderer.h`/`.cpp`, `renderer_frame.cpp`, `graphics_input_handler.cpp`, `renderer_player_physics.cpp` | **J11, J17**, and the Wave-6 `renderer_frame` / `renderer_player_physics` units |
| T318 (size update / guild rank) | `graphics/entity_renderer.*`, `renderer_entities.cpp`, `irrlicht_bridge.cpp`, `eq_entities.cpp`, `eq_guild.cpp`, `spell_effects.cpp`, `event_bus.h`, `tests/CMakeLists.txt` | **J03** (hard). Not J02 (`eq.cpp` untouched), not J04 |
| T313 (UI scroll) | `graphics/ui/`, one hunk in `irrlicht_renderer.h` | **J17** (soft: J11). No `graphics/ui/` file is over 1,500 (largest `static_panels.cpp` 915), so no J unit wants that territory |

## Optional, NOT part of this batch — the Batch-G follow-up

Eight methods dominate their TUs. Clearing the ceiling does not require touching
any of them, and a unit that does so is out of scope. Recorded so the decision
can be taken separately: `DispatchZonePacket` 880 (`eq.cpp:800`),
`loadModelFromGlobalChrWithVariantsForAnimation` 815 (`variant_loading.cpp:382`),
`updatePlayerMovement` 806 (`renderer_player_physics.cpp:19`),
`registerBuiltinCommands` 768 (`command_processor.cpp:285`),
`Application::initialize` 569 (`application.cpp:105`),
`ProcessBridgeIntents` 558 (`eq_bridge.cpp:885`),
`createDefaultConfig` 548 (`detail_manager.cpp:1203` — a data table, leave it),
`processFrameRender` 546 (`renderer_frame.cpp:1143`).

## Residual (§15 of the survey) — surveyed, not yet mapped

`daybreak_connection.cpp` 1,985 · `eq_merchant.cpp` 1,931 ·
`detail_texture_atlas.cpp` 1,826 · `model_loading.cpp` 1,793 ·
`sky_renderer.cpp` 1,770 · `detail_manager.cpp` 1,754 (lifting the 548-line
config table alone clears it) · `renderer_frame.cpp` 1,691 (T312) ·
`zone_geometry.cpp` 1,637 · `variant_loading.cpp` 1,607 ·
`renderer_player_physics.cpp` 1,546 (T312) · `spell_visual_fx.cpp` 1,517 ·
`combat.cpp` 2,119 · `eq_bridge.cpp` 2,098 (= J13) ·
`action_dispatcher.cpp` 2,039 · `eq_movement.cpp` 2,033.

## Final state after Waves 0-5

All four hard-cap files gone from the FAIL list; the largest production TU in
the tree drops from **3,913 to ~1,035**; both mechanically-splittable headers
fully compliant; `eq.h` and `irrlicht_renderer.h` grandfathered with their
class-decomposition debt named on the board.
