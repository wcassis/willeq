# J16 — Batch J Wave 7: lift the free types out of `include/client/eq.h` (2,643 → ~2,030 + 3 headers)

**Slot: assigned at dispatch. Branch `task/J16-eq-header-types` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 7 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND every correction
section (eighty numbered lessons from thirty-odd executed units; newer text wins
over older)**, then **`refactor/J28_detail_texture_atlas_split.md` §Review** —
the batch's best correction-21/correction-60 evidence — and
**`refactor/J23_eq_movement_split.md` §Review**, whose structural `body(TU)`
definition is the proof shape this unit must copy. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## ⚠⚠ READ THIS FIRST: THIS UNIT DOES NOT REACH 1,500 AND IS NOT EXPECTED TO

**`eq.h` will land at roughly 2,030 lines and that is the correct, accepted
outcome of this unit.** It is not a shortfall, it is not a failure, and it is
not something to fix by cutting more.

A class definition **cannot span files**. `class EverQuest` alone is
**1,905 lines** (739-2643). Once every free-standing type is lifted out, what
remains is the class and the include block the class needs — nothing mechanical
can make that smaller. Reaching 1,500 would require pimpl'ing the private data
block or extracting owned components, which turns the risk from "a compile
error" into "a subtle lifetime bug".

**Will has ruled** (recorded in `refactor/J_file_size_reclamation.md`
§Header-rule): **component extraction is approved as a separate programme with
no deadline, and the move-only header wins come first.** This unit is one of
those wins. **Its purpose is to leave `eq.h` containing ONLY the class**, which
is the clean starting condition for that later programme.

**Do not treat the ~2,030-line residue as a failure. Do not decompose
`EverQuest`. Do not "compress code to fit" — that is exactly the harm T115
recorded and T166 exists to prevent.**
`check_file_sizes.sh` will emit a **WARN** on `eq.h`. **Report it; it does not
block acceptance.** Say so plainly in your Review so the next reader does not
re-open the question.

## MOVE-ONLY. No behaviour changes, no interface changes.

The enums, opcode tables, structs, constants and one function declaration move
**verbatim**. No enumerator value changes. No struct field changes. No drive-by
fixes. No deletion of dead content: a J unit **records** it and **moves it
verbatim**.

## Measured at `09c99b0b` (2026-09-02, `fable` tip after T325 merged)

`include/client/eq.h` — **2,643 lines**. ⚠ **The batch survey says 2,645; it is
now 2,643** — this header moves with ordinary bug-fix work in both directions.
**Re-verify at your own branch tip before you cut** (correction 9) and report
the drift in your Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **2,643** (survey: 2,645) |
| include guard | **`#pragma once` at line 1** — no `#ifndef` guard |
| namespace | ⚠ **NONE. Everything in this header is at GLOBAL scope** — the enums, the three structs, the two constants, the two `using` aliases and `class EverQuest` itself. There is no namespace to open, close or replicate. |
| preamble | **1-121** — `#pragma once`, ~31 `#include`s (3-33), then forward declarations in `eqt`, `EQ`, `EQT`, `eqt::bridge`, `EQT::Audio`, `EQT::Graphics`, `eqt::inventory`, `eqt::ui`, with **three conditional blocks** (`#ifdef WITH_AUDIO` 71-78, `#ifdef EQT_HAS_GRAPHICS` 80-95, `#endif` 95). **All of it stays.** |
| free-type region | **122-737** — **616 lines**, the whole subject of this unit |
| `class EverQuest` | **739-2643** — **1,905 lines**, closed by `};` at 2643 with no trailing namespace close |
| enums | ⚠ **11, not 13.** Four are opcode tables (`TitaniumLoginOpcodes`, `TitaniumWorldOpcodes`, `TitaniumZoneOpcodes`, `UCSClientOpcodes`); seven are ordinary (`Animations`, `HCAppearanceType`, `MovementMode`, `PositionState`, `enum class WaterState : uint8_t`, `ChatChannelType`, `enum class LoadingPhase`) |
| structs | **3** — `WorldServer` (639-648), `CharacterSelectEntry` (653-661), `Entity` (663-718) |
| ⚠ free function declaration | **1**, and the narrow grep cannot see it: **`std::string ChatChannelName(uint32_t channel);` at 609** (T320's one channel-name table). Defined in `src/client/eq_chat.cpp`. |
| file-scope constants | **2** — `static constexpr float NPC_INTERACTION_DISTANCE` (729) and `..._SQUARED` (730) |
| `using` aliases | **2** — `using eqt::state::MAX_GROUP_MEMBERS;` (725) and `using GroupMember = eqt::state::GroupMember;` (737) |
| conditional preprocessor directives in 122-737 | ⚠ **ZERO.** `awk` over the region returns nothing. All three conditional blocks are in the preamble (71-95) or inside the class (1484, 1517, 2587, 2598, 2600, 2642). |
| includers | **88** files `#include "client/eq.h"` |
| text-scanning readers | ⚠ **THREE** — two tests and one CI script. See §The guards. |

## The split — three new headers beside `eq.h`

| Range | Content | Target header | ≈ body lines |
|---|---|---|---|
| **1-121** | preamble | **stays** verbatim in `eq.h` | 121 |
| **122-500** | `// Titanium login opcodes` (122) + `TitaniumLoginOpcodes` (123-132), `TitaniumWorldOpcodes` (135-153), `TitaniumZoneOpcodes` (156-475), `UCSClientOpcodes` (478-500) | **new** `include/client/eq_opcodes.h` | **379** |
| **501-637** | `Animations` (503-533), `HCAppearanceType` (536-558), `MovementMode` (561-565), `PositionState` (568-574), `WaterState` (577-581), `ChatChannelType` (584-600), the T320 comment + **`ChatChannelName` declaration (609)**, `LoadingPhase` (613-637) | **new** `include/client/eq_enums.h` | **137** |
| **638-737** | `WorldServer` (639-648), `CharacterSelectEntry` (653-661), `Entity` (663-718), the S06 tombstone comment (720-721), `using …MAX_GROUP_MEMBERS` (725), the two `NPC_INTERACTION_DISTANCE` constants (729-730), `using GroupMember` (737) | **new** `include/client/eq_types.h` | **100** |
| **738-2643** | blank, `class EverQuest` and its close | **stays** in `eq.h` | 1,906 |

**Arithmetic that must hold:** `121 + 379 + 137 + 100 + 1906 = 2643`. **If your
own numbers do not sum to the file's line count, you have a gap or an overlap —
find it before you build.**

⚠ **Ranges 501-637 and 638-737 each BEGIN with the blank separator line** that
preceded them in the original. That is deliberate: it makes the three ranges
tile `122-737` exactly with no gap, so the byte-identity proof is trivial.
**Do not "tidy" the leading blank away.**

**Projected finished sizes** — ⚠ correction 71: the table's column is RANGE
LENGTH, not finished file size, and correction 58 says **compute, never quote**.
Each finished file is `(your prescribed header comment block) + (its preamble)
+ (its range)`:

| file | expression | ≈ |
|---|---|---|
| `eq_opcodes.h` | `H + 1 + 379` | **~380 + H** |
| `eq_enums.h` | `H + 3 + 137` | **~140 + H** |
| `eq_types.h` | `H + 4 + 100` | **~104 + H** |
| `eq.h` | `121 + 3 new #include lines + 1906` | **~2,030** — **over the soft limit, expected, see the box at the top** |

## Header shape

**Each new header gets a MINIMAL, ENUMERATED preamble — this unit deliberately
departs from §Process's "carry the original's full include block verbatim"
rule, and the reason is stated here so it is not a judgement call.** That rule
exists to remove per-executor judgement from `.cpp` splits, where a trimmed
include set is a second risky change in disguise. Here the original's preamble
is 121 lines including `<openssl/des.h>`, `<glm/glm.hpp>`, `<thread>` and the
whole graphics/audio forward-declaration block; copying it three times would
make a pure integer-enum table drag OpenSSL into every consumer, which is
exactly the context cost this batch exists to reduce. **The lists below are
prescribed, not chosen** — the dependency scan behind each is in §Correction 21.

```
// eq_opcodes.h
// Titanium login/world/zone and UCS opcode tables, extracted verbatim from
// eq.h by refactor/J16_eq_header_type_extraction.md. Move-only.

#pragma once

<lines 122-500 verbatim>
```
(no includes at all — the four opcode tables are unscoped enums of plain
integer literals and reference no type)

```
// eq_enums.h
#pragma once
#include <cstdint>
#include <string>
<lines 501-637 verbatim>
```
(`<cstdint>` for `enum class WaterState : uint8_t`; `<string>` for
`ChatChannelName`'s return type)

```
// eq_types.h
#pragma once
#include <cstdint>
#include <string>
#include "client/state/game_state.h"
<lines 638-737 verbatim>
```
(`game_state.h` for `eqt::state::MAX_GROUP_MEMBERS` and
`eqt::state::GroupMember`, which the two `using` lines name)

**Everything is at global scope — no namespace opens or closes in any new
header.**

## Where the three includes go in `eq.h` — this is load-bearing

**Put them at line 122, exactly where the region was**, in dependency order:

```
#include "client/eq_opcodes.h"
#include "client/eq_enums.h"
#include "client/eq_types.h"
```

**Not at the top of the include block.** Declaration ORDER is preserved by
putting them where the code was: everything in the preamble is declared before
them, and `class EverQuest` is declared after. Moving them earlier would place
`eq_types.h`'s `#include "client/state/game_state.h"` ahead of the preamble's
forward declarations, which is a reordering, not a relocation. **State exactly
where you put them.**

## The umbrella property — 88 includers must not change

`eq.h` keeps its name, its `#pragma once`, its entire 121-line preamble and
`class EverQuest`, and gains three `#include` lines. **Every one of the 88 files
that includes it sees exactly the declarations it saw before, in the same
order.** No includer is edited.

```bash
grep -rlE '#include[[:space:]]*[<"][^">]*client/eq\.h[">]' src include tests tools benchmarks scripts | wc -l
```
Expect **88** (46 in `src/client`, 36 in `tests`, 4 in `src/client/spell`, 1 in
`src/client/skill`, 1 in `src/client/graphics`). ⚠ **Re-count at your tip and
report the number** — do not quote it (correction 67: include counts are a
derived quantity that has slipped for four units). **The count must be the same
before and after**, and `git diff --stat` must not name any of those 88 files.

## ⚠⚠ THE GUARDS — three text readers, all of which must be checked

Correction 10 makes this part of **territory**, not scope creep, and it is the
one class of defect this unit could ship silently. **All three read `eq.h` as
text. This plan's analysis is that ALL THREE ARE SAFE because everything they
look for stays in `eq.h` — but you must MEASURE that, not accept it** (J13's
practice, which J25 called the batch's best).

**Guard 1 — `scripts/ci/check_sender_reachability.py:43`** opens
`include/client/eq.h` and regexes `^\s*(?:void|bool)\s+((?:Send|Request)\w+)\s*\(`.
It **exits 2 with `ERROR no Send*/Request* declarations found`** if the parse
collapses — a loud failure, not a silent one. `Send*`/`Request*` are
`EverQuest` members and **stay**. **Run the script before and after and paste
both `checked N Send*/Request* methods` lines; N must be identical.**

**Guard 2 — `tests/test_reachability_sweep.cpp:435`
(`ReachabilitySweep.EverySenderHasACaller`)** reads `eq.h`, parses `Send*`
declarations, and asserts `ASSERT_GT(senders.size(), 50u) << "eq.h Send* parse
failed"`. Same reasoning; the excluded-as-a-caller line at **`:450`**
(`if (f.rel == "include/client/eq.h") continue;`) also still refers to the file
that holds the declarations. ⚠ **But `collect({"src","include"}, {".cpp",".h"})`
sweeps `include/` RECURSIVELY, so your three new headers enter that sweep
automatically.** They contain no `Send*(` call, so they cannot make an uncalled
sender look called — **prove that with
`grep -cE 'Send[A-Za-z0-9_]+\(' include/client/eq_*.h` and paste the zeros.**
**No repoint is expected. If one is needed, it is in territory.**

**Guard 3 — `tests/test_duplicate_state_stores.cpp:144`** calls
`membersOf(kRoot / "include/client/eq.h")` to collect `EverQuest`'s data
members, and asserts `ASSERT_GT(eqMembers.size(), 100u) << "eq.h member parse
collapsed"`. `membersOf` skips comment lines and any line containing `(`, and
`normalize()` does `member.substr(2)` — i.e. it only handles `m_`-prefixed
names. **`EverQuest`'s members stay.** The three extracted structs' fields
(`long_name`, `spawn_id`, `hp_percent`, …) are **not** `m_`-prefixed, so
removing them should not change the set — **but run the test before and after
and paste both `eqMembers.size()` values** (add a temporary print if needed, or
simply confirm the test's pass/fail state is identical). ⚠ If the count changes
at all, **STOP and report**: it means the parse was picking up struct fields,
and this split makes the guard measure a different thing.

**Run both greps at your tip and paste both** (correction 10; they are not
interchangeable — the second finds string-compare source-path scans a `readFile`
grep cannot see):
```bash
grep -rn 'client/eq\.h\|eq_opcodes\|eq_enums\|eq_types' tests scripts
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
⚠ **`grep -rn --include=<pat>` SILENTLY RETURNS NOTHING on this host** (ugrep
7.8.4, correction 55). Never use `--include`.
⚠ **Expect nothing new** (corrections 73/77/80) — a hit is the surprise.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a symbol CAN stay file-local. It does NOT
prove the plan's ranges KEEP it with its consumers.** J08's plan assigned three
statics to one range and their only consumer to another; it surfaced only at
the BUILD step.

**Run BOTH greps (correction 68 — the widened pattern is call-shaped and CANNOT
match `static constexpr float kFoo = 15.0f;`, so it replaces the narrow one for
FREE FUNCTIONS and runs ALONGSIDE it; obeying correction 61 literally here would
have concluded "zero file-scope symbols" and missed both
`NPC_INTERACTION_DISTANCE` constants), and run each with a positive control
before believing any empty result (correction 60):**

```bash
grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/client/eq.h
grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/eq.h
```

⚠ **Positive control: `src/client/position.cpp`** — correction 78. Narrow
returns `11:constexpr float position_eps`; widened returns ~30 genuine free
functions. **The controls named in corrections 51 and 61
(`zone_geometry.cpp`'s `heightToColor`, `combat.cpp`'s `IsCorpseEntity`) are
STALE — J32 and J24 split those files and both symbols are gone.**

**The orchestrator audited this plan against correction 21. Result, symbol by
symbol:**

| symbol | defined at | tile | consumers | consumers' location | resolved? |
|---|---|---|---|---|---|
| `NPC_INTERACTION_DISTANCE`, `..._SQUARED` | 729, 730 | `eq_types.h` | `class EverQuest` and `src/client/*.cpp` | `eq.h` (which includes `eq_types.h`) and via `eq.h` | ✅ |
| `using eqt::state::MAX_GROUP_MEMBERS` | 725 | `eq_types.h` | `class EverQuest`, `src/` | same | ✅ |
| `using GroupMember = eqt::state::GroupMember` | 737 | `eq_types.h` | `class EverQuest`, `src/` | same | ✅ |
| ⚠ `std::string ChatChannelName(uint32_t)` | **609** | `eq_enums.h` | **`src/client/eq_chat.cpp`, which DEFINES it**, plus its console/bridge callers | all include `eq.h` | ✅ — **but this is the one symbol the narrow grep cannot see, and the reason correction 68 exists.** Its definition in `eq_chat.cpp` must still see the declaration; it does, via `eq.h`. **Confirm `eq_chat.cpp` includes `eq.h` (directly or transitively) and that the build links.** |
| `enum ChatChannelType` | 584-600 | `eq_enums.h` | `ChatChannelName`'s implementation and `class EverQuest` | same tile / `eq.h` | ✅ |
| the four opcode tables | 122-500 | `eq_opcodes.h` | `class EverQuest`, `src/client/eq_*.cpp`, `tests/` | via `eq.h` | ✅ |
| `WorldServer`, `CharacterSelectEntry`, `Entity` | 639-718 | `eq_types.h` | `class EverQuest` member containers | `eq.h` | ✅ |

**The rule that discharges every row: `eq.h` includes all three new headers at
line 122, ahead of the class, so every consumer that compiles today still
compiles.** No consumer includes a subset — nothing outside `include/client/`
includes the new headers directly.

**Re-run both greps at your tip with the control and paste them.** Do not cite
this section — if a file-scope symbol has appeared or moved since `09c99b0b`,
**STOP and report**; the unit changes shape.

## Every cut boundary — verify each at your tip

The rule is **J03's** (correction 11): **cut at a blank line whose predecessor
closes a definition, and verify comment depth 0 at the cut.** A leading comment
**travels with the entity it introduces**, which is why every range below starts
at a comment or at the blank preceding one.

| Cut | preceding lines | first line of next range |
|---|---|---|
| 122 | `119: }`, `120: }` (close of the `eqt::ui` / `eqt` forward-declaration blocks), `121` blank | `122: // Titanium login opcodes` |
| 501 | `500: };` (close of `UCSClientOpcodes`) | `501` blank, `502: // Character animations` |
| 638 | `637: };` (close of `LoadingPhase`) | `638` blank, `639: struct WorldServer` |
| 738 | `737: using GroupMember = eqt::state::GroupMember;` | `738` blank, `739: class EverQuest` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Note the 639 row has no leading comment** — `WorldServer` is introduced
directly after a blank. That is why range 3 starts at the blank (638) rather
than at a banner. **Say so if your tip differs.**

**Compute comment depth at every cut — do not assert it.** A `//` line
containing the token `*/` makes a naive counter go negative (the trap
`eq_movement.cpp:18` set for the orchestrator's own scanner):

```bash
python3 - <<'EOF'
L=open('include/client/eq.h').read().split('\n')
cuts={122,501,638,738}
d=0
for i,ln in enumerate(L,1):
    if i in cuts: print('depth before line',i,'=',d)
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
EOF
```
Expect **0** at all four.

## The preprocessor dump — this is the one place a mis-cut is invisible

⚠ **Correction 2: a range that cuts inside an `#ifdef` STILL COMPILES in the
default configuration and breaks only in a config the acceptance bar
exercises.** `eq.h` has three conditional blocks in its preamble
(`WITH_AUDIO` 71-78, `EQT_HAS_GRAPHICS` 80-95) and six more inside the class.
**None is in 122-737 — prove that.**

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/ {print NR": "$0}' \
    include/client/eq.h
```
At `09c99b0b` every hit is `< 122` or `> 737`. ⚠ **Correction 60: prove the
pattern is live** — the same `awk` against
`include/client/graphics/irrlicht_renderer.h` returns fourteen directives.
Paste both.

The acceptance bar exercises the conditional configs: `build-gles2-x86.sh`
(`EQT_HAS_GLES2`), `build-arm-noble.sh`, and the headless
`EQT_GRAPHICS=OFF` path exercised by the test suite. **All three must be
clean.**

## CMake — expect ZERO add-sites, and prove it

```bash
grep -rn 'client/eq\.h\|eq_opcodes\|eq_enums\|eq_types' CMakeLists.txt cmake tests scripts benchmarks
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26)** — J20 split it
into fragments under `tests/cmake/`.
⚠ **CMake commands here are UPPERCASE** (correction 12): `grep -n 'set('` and
`grep -n 'add_executable('` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.**
⚠ **Do not cite a CMake line number** — corrections 59 and 76 record five
consecutive units whose target names were right and whose line numbers were all
stale within a day. **Name the target; locate it yourself.**

**At `09c99b0b`, no CMake file names `eq.h`** — this project does not list
headers in its source lists. **The expected add-site count is ZERO, and the
three new headers need none either.** ⚠ Add-site counts in this batch have
ranged 1 to 19 — **count your own and say so loudly if you find any.**

## File-header comments

`eq.h` opens with `#pragma once` and has **no file-header comment block**. Each
of the three new headers gets the short block prescribed in §Header shape;
`eq.h` gains a matching one-line note above the three new includes. ⚠
**Correction 74: do not aim for a line count — carry the required content.**
That is the unit's only non-move content; **enumerate every line of it in your
Review**, exactly as J07 did. **Change no other comment**, and in particular do
not touch the T156 or S06 tombstone comments that ride along inside the moved
ranges.

⚠ **Correction 45/62: never quote, in a header comment, a token that any check
in this plan greps for.** The dangerous tokens here are `Send`, `Request`,
`class EverQuest`, `m_`, `client/eq.h`, `eq_opcodes`, `eq_enums` and `eq_types`
— a comment spelling `Send` inflates **Guard 1's own regex** and
**Guard 2's `senders` set**, and a comment spelling `client/eq.h` inflates this
plan's `tests/`/`scripts/` grep. **Describe them in words.** ⚠ **Audit your own
written headers against every grep and every guard in this plan before you run
one** — correction 62 records this nearly firing on four units, and here it
would corrupt a reachability guard rather than a bookkeeping grep.

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- ⚠ **`scripts/build-gles2-x86.sh` FIRST, BEFORE `ctest`** (corrections 41 and
  72). `scripts/ci/visual_gate.sh` runs against the **separate** `build-gles2/`
  tree; running `ctest` first tests whatever binary was there beforehand and
  **passes while proving nothing** — J25 measured a benchmark stamped 11:01
  against edits made at 11:55, and J30 confirmed it directly. **Either build
  gles2 first, or re-run the gate standalone afterwards, and say which you did
  and what the benchmark's timestamp was against your last edit's.**
- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end three ways** (corrections 23 and 65): **exit 143**
  (SIGTERM at the 600 s tool ceiling); **backgrounded** by the tool and later
  reporting its own code; or **completed with no exit file recorded at all**.
  ⚠ **An absent exit file is NO RESULT, not a bad one** (J28, J34). Relaunch
  detached with a **unit-private exit file** and read the code from inside it.
  ⚠ **`eq.h` has 88 includers — this unit triggers close to a full rebuild every
  time. Budget for it and expect at least one 143.**
- ⚠ **After a real exit 143, scan for valid ELF magic — not size**
  (corrections 27, 29, 32, 56). Truncated files have been 50-377 MB of zeroes;
  **no size heuristic catches them.** A killed *link* makes gmake delete its
  output; a killed *compile* can leave a truncated file; a killed **`ar`** is a
  third site (`libwilleq_core.a`, four instances) that `build/bin/*` alone is
  blind to. **Scan binaries AND `lib*.a`:**
  ```bash
  for f in build/bin/* build/*.a build/**/lib*.a; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || echo "BAD $f"
  done
  ```
  `rm` every hit, then rebuild once.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it** — re-run it alone first.
  - ⚠ `ctest` exit 8 during `GoogleTestAddTests.cmake` discovery is **not
    necessarily** the truncated-binary trap (correction 16).
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; redact `72-byte object <...>` before diffing
    `ctest -N`.
  - **`ctest -R ReachabilitySweep` and `ctest -R DuplicateStateStores` must be
    green, and their parsed-set sizes must be unchanged** — see §The guards.
- **`python3 scripts/ci/check_sender_reachability.py`** — run before and after,
  paste both `checked N …` lines, N identical.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **THREE distinct ways to get a confident-looking wrong answer, two of which
  share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd`; agent threads reset cwd
     between calls). Omitting it silently verifies `/mnt/projects/fable/main`
     and prints a loud FAIL full of some unrelated merged unit's lines (J02).
  2. **The wrong base rev.** `fable..HEAD` is **WRONG** — units merge during a
     wave, so `fable` moves under you and reports the *other* units' merged
     work, in reverse, as substantive removals (J01: 533 spurious removals).
     **Use the fork point.**
     ⚠ **Failures 1 and 2 look IDENTICAL.** **Check both before changing a line
     of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0** (J06). It looks like a definitive
     pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J16: EXIT 0, `+0 / -0` substantive.**

  **Derivation, from what this plan prescribes and nothing else** (correction
  19 — a predicted diff size must come from the plan's own prescribed output):
  - Every moved line — 11 enums, 3 structs, 2 constants, 2 `using` aliases, the
    `ChatChannelName` declaration — keeps its **exact text** and its **count of
    1**, so it cancels (correction 54: a promoted line costs verifier lines only
    if its TEXT or its COUNT changes; keeping a symbol in its existing scope is
    what buys the zero, and here the scope is global and unchanged).
  - The three new headers' preambles are `#pragma once` and `#include` —
    **BOILERPLATE**; their comment blocks are `//` — boilerplate.
  - `eq.h`'s three new `#include` lines — boilerplate.
  - No namespace to replicate (this header has none), no internal header, no
    signature change, no `using` added, **no test repoint expected**.
  - **Total: ADDED 0, REMOVED 0 → exit 0.**

  Paste the full output and enumerate every flagged line. **Any line beyond the
  enumerated set is a real finding — chase it.** **NEVER contort code to force a
  green exit** (J04 correctly refused to). If a guard did need a repoint, the
  prediction becomes `+n/-n` for exactly those lines: enumerate them and request
  orchestrator sign-off (§Process step 4 / correction 5) — **expected, not a
  failure.**
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment, so a range duplicated into two headers, or dropped from both, is
  invisible to it. **Not optional.**
  ```bash
  cat <the 3 bodies> | sort > /tmp/after.txt
  git show "$BASE:include/client/eq.h" | sed -n '122,737p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Define `body(header)` STRUCTURALLY** — everything after the file's own
  last `#include` / `#pragma once` line — **never by an offset constant.** J23,
  J27, J28, J30 and J35 all did this, and it is why none of them produced J08's
  alarming false alarm. ⚠ **Declare count variables `local` in your proof
  helpers** (correction 17): J05's non-`local` `n` produced a 247-line shortfall
  that read exactly like a dropped range. **When a proof fails, suspect the
  proof first** (correction 20) — three units, three proof bugs, zero real
  defects.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  three bodies **in original order** (`122-500`, `501-637`, `638-737`) and
  confirm the result is byte-identical to
  `git show "$BASE:include/client/eq.h" | sed -n '122,737p'`.
  **616 lines. Expect `cmp` exit 0.** Extract the bodies **from the produced
  files**, not from scratch `sed` on the original, so the proof covers assembly
  as well as content (J12's rule).
- **a fourth proof, cheap and worth it here:** `git show "$BASE:include/client/eq.h" | sed -n '739,2643p'`
  against `eq.h`'s `class EverQuest` region at your tip — **byte-identical, 1,905
  lines.** This is what proves the unit touched nothing inside the class.
  (J35's per-method byte diffs are the precedent — the strongest evidence any
  unit has produced that a large block survived relocation intact.)
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report.**
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` p95 CPU
    11.684 vs a 10.948 ceiling with **zero image mismatches**; the re-run
    measured 4.676. Contention, not your change. **Re-run.**
  - ⚠ A failure at the **RENDER** step means a `gles2_zone_benchmark` CMake
    add-site was missed. **This unit has no CMake site at all**, so a
    render-step failure would be a genuine surprise — report it, do not re-run
    past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file** — `include/client/eq.h`, `include/client/eq_opcodes.h`,
  `include/client/eq_enums.h`, `include/client/eq_types.h`.
  **Expected: 0 fail, and ONE WARN — `eq.h`, at ~2,030.** ⚠ **That WARN is the
  accepted outcome of this unit (see the box at the top). It does NOT block
  acceptance.** The three created files must be 0 warn, 0 fail. **Compute every
  total and paste them** rather than quoting §The split (correction 58).
- ⚠ **`chmod 644` every CREATED file before `git add`; PRESERVE the original
  mode on `eq.h`** (correction 69 and J01's inverse). A `sed >` pass produces
  755 and records a spurious `100644 => 100755`; chmod-ing the *stays* file
  records the opposite. **Check its mode with `git ls-files -s` BEFORE you touch
  it** and confirm `git diff --raw` shows no mode change.
- **`git diff --stat` must name exactly four files** — `eq.h` and the three new
  headers — unless a guard genuinely needed a repoint, in which case
  `tests/test_reachability_sweep.cpp` may be a fifth and must be justified.
  Nothing under `src/`, `scripts/`, `docs/`, `CMakeLists.txt` or `tests/cmake/`.

## Review must answer

1. **Line count at your tip** against the **2,643** this plan measured (and the
   survey's stale 2,645), and any range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `09c99b0b`, **STOP and report**.
3. ⚠ **A plain statement that `eq.h` did not reach 1,500, the number it did
   reach, and why that is the expected outcome** — so the next reader does not
   re-open a decision Will has already made.
4. ⚠ **Correction 21, explicitly.** Paste **both** greps **and both positive
   controls**, print the widened grep's **exit status** and a **count of its
   unfiltered matches** to prove it was live (J28's is the template), then name
   the tile this plan assigns each file-scope symbol and where its consumers
   land. ⚠ **Say explicitly what the narrow grep alone would have missed**
   (`ChatChannelName`) and what the widened grep alone would have missed (the
   two `NPC_INTERACTION_DISTANCE` constants) — this file is a live example of
   correction 68 in both directions. **Do not cite §Correction 21 — re-derive
   it.**
5. **Comment depth at all four cuts**, computed with the `//`-stripping counter,
   not asserted, plus the surrounding lines for each.
6. **The preprocessor dump**, pasted, with its positive control, confirming no
   directive lies in 122-737.
7. ⚠ **All three guards, measured:** `check_sender_reachability.py`'s `checked
   N` before and after; `ReachabilitySweep`'s pass state and `senders.size()`;
   `DuplicateStateStores`'s pass state and `eqMembers.size()`; and the
   `grep -cE 'Send[A-Za-z0-9_]+\('` zeros on the three new headers. Say whether
   any repoint was needed.
8. **Every file-header comment line you wrote**, enumerated, plus your
   correction-45 audit of them against every grep AND every guard regex in this
   plan.
9. **The includer count**, before and after — expect **88** unchanged, and
   expect `git diff --stat` to name none of those files.
10. **How many CMake add-sites you actually found**, against the **ZERO** this
    plan predicts, and the output of both `tests/`/`scripts/` greps.
11. **All four content proofs**, pasted, and the verifier's exit code plus
    `$BASE`. State whether the result matched the `+0/-0` prediction; if not,
    enumerate every flagged line before concluding anything.
12. **Final line counts of every file**, computed not quoted; wall clock;
    slowest step; and whether wave concurrency stretched the verification tail.
    ⚠ **Do not report per-header enumerator or method counts as a headline** —
    derived counts have slipped for seven consecutive units while ranges were
    exact (corrections 66, 75, 79).
13. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). `docs/module_map.md`
    needs rows for the three new headers — **record it, do not act.**

## Territory

`include/client/eq.h`, the three new headers beside it (`eq_opcodes.h`,
`eq_enums.h`, `eq_types.h`), and this plan file — plus
`tests/test_reachability_sweep.cpp` **only if a guard measurably needs a
repoint**, which this plan does not expect. **Nothing else** — not `src/`, not
`scripts/`, not any other `tests/` file, not `tests/cmake/*.cmake`, not
`CMakeLists.txt`, not `docs/`.

⚠ **`tests/test_reachability_sweep.cpp` is shared with J15**, which repoints
lines 333 and 399 in it. **If J15 is running in another slot at the same time,
the two units WILL conflict in that file** — correction 31's shape, where git
offers the entire pre-edit file as one side and taking either side wholesale
silently reverts a completed unit. **Serialise: do not run J15 and J16
concurrently.**

## STOP conditions

- Any of the three guards reports a different parsed-set size after the split
  than before.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- A conditional preprocessor directive is found inside 122-737.
- The verifier reports any substantive line beyond the predicted set.
- Any of the four content proofs fails after you have satisfied yourself the
  proof itself is correct — in particular, if the `class EverQuest` region is
  not byte-identical, **stop immediately**: this unit must not have touched it.
- The includer count changes, or `git diff --stat` names a file outside
  §Territory.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to decompose `EverQuest`, pimpl anything, "compress
  code to fit", delete dead content, trim the include block, or add a header
  this plan does not name.

---

## Review (to be written by the executor)

---

## Review (executed 2026-09-02, slot1, branch `task/J16-eq-header-types`)

Base / fork point: **`55a7be1e`** (`fable` tip, the J15 merge). Cut after J15, as
dispatched.

### 1. Line count at my tip, and range drift

**`include/client/eq.h` was 2,635 lines, not the 2,643 this plan measured at
`09c99b0b` (nor the survey's 2,645).** The header moved again between the plan
being written and this unit running. **Every range in §The split shifts by one
at the front, and the class region is seven lines shorter.** Re-derived tiling,
computed at my tip:

| region | plan (`09c99b0b`) | **measured at `55a7be1e`** | length |
|---|---|---|---|
| preamble | 1-121 | **1-120** | 120 |
| opcode tables | 122-500 | **121-499** | **379** (unchanged) |
| ordinary enums + name lookup | 501-637 | **500-636** | **137** (unchanged) |
| structs, constants, aliases | 638-737 | **637-736** | **100** (unchanged) |
| `class EverQuest` region | 738-2643 | **737-2635** | **1,899** (plan said 1,906) |

`120 + 379 + 137 + 100 + 1899 = 2635` ✅ — the arithmetic closes exactly, so
there is no gap and no overlap. **The three moved ranges are byte-for-byte the
same lengths the plan predicted**; all the drift is in the preamble (−1) and
inside the class (−7).

### 2. §Measured properties, re-verified

Every property held except the two counts below. Nothing changed shape, so no
STOP was warranted.

- `#pragma once` at line 1, no `#ifndef` guard — held.
- **No enclosing namespace** — held; every namespace the narrow grep found is a
  forward-declaration block in the preamble (lines 12-108), all of which stay.
- 11 enums, 3 structs, 2 file-scope constants, 2 `using` aliases, 1 free
  function declaration — held.
- **`ChatChannelName` is at 608, not 609** (the same −1 shift).
- **`NPC_INTERACTION_DISTANCE` / `_SQUARED` are at 728/729, not 729/730.**
- ⚠ **Includers: 87, not 88.** Re-counted with the plan's own grep at my tip:
  87 before and **87 after**, and `git diff --stat` names none of them. I did
  not chase which file the plan's 88th was; the count is a derived quantity and
  correction 67 says to measure it, which I did, before and after.
- ⚠ **The sweep sites are at `:437` and `:452`**, exactly as J15's Review
  predicted after its own edits at 333/399 — confirmed at my tip, not inherited.

### 3. `eq.h` did NOT reach 1,500, and that is the expected outcome

**`include/client/eq.h` finished at 2,025 lines.** `check_file_sizes.sh` emits
one WARN on it. **This is the accepted result of this unit, not a shortfall.**
A class definition cannot span files: `class EverQuest` alone is 1,899 lines and
the preamble it genuinely needs (OpenSSL, glm, `game_state.h`, the graphics and
audio forward-declaration blocks) is another 120. `120 + 6 + 1899 = 2025`.
Every free-standing type is now out. **The purpose of this unit was to leave the
header containing only the class, which is the starting condition for the
component-extraction programme Will approved with no deadline. Do not re-open
this.**

### 4. Correction 21 — both greps, both controls, re-derived

Narrow grep on `eq.h`, **exit 0**:

```
12:namespace Json { class Value; }
41:namespace eqt {
45:namespace EQ {
51:namespace EQT {
61:namespace eqt {
62:namespace bridge {
71:namespace EQT {
72:namespace Audio {
80:namespace EQT {
81:namespace Graphics {
93:namespace EQT { namespace Graphics { struct LoadingStatus; } }
101:namespace EQT { namespace Graphics { struct BspTree; class PfsArchive; class WldLoader; } }
102:namespace eqt { namespace events { struct PlayerPositionChanged; } }
103:namespace eqt {
104:namespace inventory {
108:namespace ui {
728:static constexpr float NPC_INTERACTION_DISTANCE = 15.0f;
729:static constexpr float NPC_INTERACTION_DISTANCE_SQUARED = NPC_INTERACTION_DISTANCE * NPC_INTERACTION_DISTANCE;
```

Widened grep on `eq.h`, **exit 0, 1 unfiltered match**:

```
608:std::string ChatChannelName(uint32_t channel);
```

Positive controls, both live (`src/client/position.cpp`, per correction 78 —
the corrections-51/61 controls are gone): narrow returns
`11:constexpr float position_eps = 0.0001f;`, widened returns **31** matches.
Neither grep returned an empty result I had to trust.

**This file is the batch's live proof of correction 68 in BOTH directions:**

- **The narrow grep alone would have missed `ChatChannelName(uint32_t)`** at
  608 entirely — it is not `static`/`const`/`constexpr`/`extern` and does not
  open a namespace, so the anchored pattern cannot see a free function
  declaration.
- **The widened grep alone would have missed BOTH
  `NPC_INTERACTION_DISTANCE` constants** at 728/729 — the widened pattern is
  call-shaped and requires a `(`, which `static constexpr float kFoo = 15.0f;`
  does not have. Obeying correction 61 literally here would have concluded
  "one file-scope symbol" and shipped both constants unexamined.

Tile assignment and consumer resolution, re-derived (not cited):

| symbol | line | tile | consumers | resolved |
|---|---|---|---|---|
| `NPC_INTERACTION_DISTANCE`, `_SQUARED` | 728, 729 | `eq_types.h` (637-736) | `class EverQuest`, `src/client/*.cpp` | ✅ via `eq.h`'s include at the original position |
| `using eqt::state::MAX_GROUP_MEMBERS` | 724 | `eq_types.h` | same | ✅ |
| `using GroupMember = eqt::state::GroupMember` | 736 | `eq_types.h` | same | ✅ |
| `ChatChannelName(uint32_t)` | 608 | `eq_enums.h` (500-636) | defined in `src/client/eq_chat.cpp`, called from console/bridge | ✅ — `eq_chat.cpp` still sees the declaration via `eq.h`; the build links, exit 0 |
| `ChatChannelType` | in 500-636 | `eq_enums.h` | its own lookup + the class | ✅ same tile / `eq.h` |
| four opcode tables | 121-499 | `eq_opcodes.h` | the class, `src/client/eq_*.cpp`, `tests/` | ✅ via `eq.h` |
| `WorldServer`, `CharacterSelectEntry`, `Entity` | 638-717 | `eq_types.h` | the class's member containers | ✅ |

No file-scope symbol has appeared or moved since `09c99b0b` beyond the uniform
−1 shift. No STOP.

### 5. Comment depth at all four cuts — computed, not asserted

The `//`-stripping counter over my tip's file, at my (shifted) cuts:

```
depth before line 121 = 0
depth before line 500 = 0
depth before line 637 = 0
depth before line 737 = 0
final depth 0
```

Surrounding lines at each cut:

```
118: }              <- close of the eqt::ui / eqt forward-declaration blocks
119: }
120: (blank)
121: // Titanium login opcodes
122: enum TitaniumLoginOpcodes {
---
498: 	HC_OP_UCS_Ignore = 0x41
499: };               <- close of UCSClientOpcodes
500: (blank)
501: // Character animations
---
635: 	COMPLETE = 15   // Hide loading screen, show game world
636: };               <- close of LoadingPhase
637: (blank)
638: struct WorldServer     <- no leading comment, as the plan flagged
---
735: // type, and GroupState is the one store.
736: using GroupMember = eqt::state::GroupMember;
737: (blank)
738: class EverQuest
```

Ranges 2 and 3 keep their leading blank separator, so the three ranges tile
`121-736` with no gap.

### 6. Preprocessor dump — no directive lies in the moved region

```
70: #ifdef WITH_AUDIO
77: #endif
79: #ifdef EQT_HAS_GRAPHICS
94: #endif
1482: #ifdef EQT_HAS_GRAPHICS
1515: #endif
2579: #ifdef EQT_HAS_GRAPHICS
2590: #endif
2592: #ifdef WITH_AUDIO
2634: #endif
```

**Ten directives, every one `< 121` or `> 736`.** Zero in the moved region.
Positive control (correction 60): the same `awk` against
`include/client/graphics/irrlicht_renderer.h` returns directives at 1, 2, 53,
56, 58, 60, 461, 463, 465, 720, 759, 887, 898, 1021, 1023, 1388, 1401, 1532,
1534, 1536 — the pattern is live.

All three conditional configurations are green: the `EQT_HAS_GRAPHICS`/GLES2
tree (`build-gles2-x86.sh`, exit 0), ARM (`build-arm-noble.sh`, exit 0) and the
headless path exercised by the suite.

### 7. All three guards, measured. **No repoint was needed.**

| guard | before | after |
|---|---|---|
| `scripts/ci/check_sender_reachability.py` | `checked 101 Send*/Request* methods in 329 production sources; 0 allowlisted` — PASS, exit 0 | **identical**, PASS, exit 0 |
| `ReachabilitySweep.EverySenderHasACaller` — re-implemented parse | `senders.size() = 78` | **78** |
| `DuplicateStateStores` — re-implemented `membersOf` | `eqMembers.size() = 150` | **150** |

Following J15's standard, **I did not settle for the counts matching**: I
serialised both parsed sets before and after and compared them element by
element — `senders SET IDENTICAL: True`, `members SET IDENTICAL: True`. The
guards count the same things, not merely the same number of things.

`grep -cE 'Send[A-Za-z0-9_]+\(' ` on the three created headers, all **0**:

```
include/client/eq_opcodes.h:0
include/client/eq_enums.h:0
include/client/eq_types.h:0
```

Both are in the recursive `collect({"src","include"}, {".cpp",".h"})` sweep now,
and neither can make an uncalled sender look called.

Guard 3's `membersOf` was also checked against the three new `#include` lines
added to `eq.h`: `kMemberDecl` requires a leading type, an `m_`-prefixed name
and a trailing `;`, and `#include "client/eq_opcodes.h"` matches none of those.
The extracted structs' fields are not `m_`-prefixed, which is why removing them
left the set unchanged — the parse was never picking them up.

Both live suite tests are green: `ReachabilitySweep.EverySenderHasACaller`
Passed (2.24 s) and `DuplicateStateStores.EverQuestKeepsNoCopyOfAStateModuleMember`
Passed (0.08 s).

### 8. Every file-header comment line written, and the correction-45 audit

`include/client/eq_opcodes.h` (3 lines):
```
// Titanium login, world and zone opcode tables, plus the UCS client table.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only; no value changed.
```

`include/client/eq_enums.h` (4 lines):
```
// Free-standing enumerations used across the client, plus the declaration of
// the chat-channel name lookup that is defined in src/client/eq_chat.cpp.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only.
```

`include/client/eq_types.h` (4 lines):
```
// Small free-standing structs, the two NPC interaction distance constants and
// the two group aliases used across the client.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only.
```

`include/client/eq.h` (3 lines, immediately above the three new includes):
```
// The free-standing enumerations, opcode tables and small structs that used to
// live here now sit in the three headers below, included at their original
// position so declaration order is unchanged.
```

**Correction-45 audit, token by token, against every check in this plan:**

| token | appears in any written comment? |
|---|---|
| `Send` | no — no comment says it; guard 1's regex and guard 2's `senders` set are unaffected, and the measured sets are identical before/after |
| `Request` | no |
| `class EverQuest` | no — described as "the umbrella client header" |
| `m_` | no |
| `client/eq.h` | no — every reference is "the umbrella client header"; the file-name form appears only inside the moved code's own tombstone comments, which I did not touch |
| `eq_opcodes` / `eq_enums` / `eq_types` | **not in any comment.** They appear only in the three `#include` directives in `eq.h`, which live under `include/`, outside the `tests`/`scripts` grep's scope. The `NPC interaction distance` phrase is deliberately spaced so it is not the constant's token, and the narrow grep is `^`-anchored regardless. |

The plan's own suggested header blocks spell `eq.h` and name the new headers;
**I deliberately did not copy them verbatim**, for exactly the reason correction
62 exists. That is a deviation from §Header shape's literal text and it is
flagged here, not absorbed.

### 9. Header shape as built, and where the includes went

Each new header has the minimal enumerated preamble §Header shape prescribes,
and **nothing else** — `eq_opcodes.h` has no includes at all,
`eq_enums.h` has `<cstdint>` and `<string>`, `eq_types.h` has those two plus
`"client/state/game_state.h"`. **No namespace is opened or closed in any of
them**; everything is at global scope, as it was.

The three includes went **at the original position of the region — immediately
after the preamble's last forward-declaration block (old line 120's blank) and
immediately before the blank that precedes `class EverQuest`** — in dependency
order opcodes, enums, types. **Not at the top of the include block.** Everything
in the preamble is still declared before them and the class is still declared
after, so declaration order for all 87 includers is bit-identical to what it
was.

### 10. CMake add-sites: **ZERO**, as predicted

```bash
grep -rn 'client/eq\.h\|eq_opcodes\|eq_enums\|eq_types' CMakeLists.txt cmake tests scripts benchmarks
```
returns hits only in `tests/*.cpp` (43 `#include "client/eq.h"` lines, 4 prose
mentions, and the two `test_reachability_sweep.cpp` / one
`test_duplicate_state_stores.cpp` path scans). **No `CMakeLists.txt`, no file
under `cmake/`, no file under `tests/cmake/`, no script names the header.** This
project lists no headers in its source lists, so the three created headers need
no add-site either. Falsified rather than assumed.

`grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp` returns nothing new
beyond the two known sites — no string-compare source-path scan was missed.

### 11. All four content proofs, and the verifier

Proofs computed with `body(header)` defined **structurally** — everything after
the file's own last `#pragma once`/`#include` line — and extracted **from the
produced files**, never from a scratch `sed` on the original:

```
eq_opcodes.h body lines = 379
eq_enums.h   body lines = 137
eq_types.h   body lines = 100
region lines = 616
concat lines = 616
BYTE-IDENTICAL-CONCAT: True      <- proof 1: the three bodies in original order
MULTISET-OK:           True      <- proof 2: line multiset vs BASE:eq.h 121-736
class region orig lines = 1899 new lines = 1899
CLASS-BYTE-IDENTICAL:  True      <- proof 3: BASE 737-2635 vs the class at my tip
PREAMBLE-IDENTICAL:    True      <- proof 4 (extra): BASE 1-120 vs my tip's 1-120
```

Proof 4 is not in the plan; I added it because with the preamble AND the class
region both proved byte-identical, and the moved region proved byte-identical by
re-concatenation, **every line of the original file is accounted for** and the
only new bytes in the tree are the four comment blocks and the three `#include`
lines.

**Verifier prediction and result.** I predicted **exit 0, +0/−0**, and — unlike
J15 and J17 — that prediction was sound here, because the failure mode that
broke theirs cannot fire: **this unit copies no preamble.** J15 and J17 each
paid `+(N−1)` per non-boilerplate declaration in a verbatim-duplicated preamble,
and forward declarations (`class Foo;`, `namespace X { struct Y; }`) are
declarations the verifier does not allowlist. `eq.h`'s preamble holds **sixteen**
such forward-declaration lines — had §Header shape's "carry the original include
block verbatim" rule been followed, this unit would have predicted roughly
`+32`. §Header shape's prescribed minimal preambles are the reason the number is
zero, and I counted them in advance rather than inheriting the prediction.

**Verifier, run after committing, with `--repo` and the fork point:**

```
$ BASE=$(git merge-base fable HEAD)   # 55a7be1e9d3f4124a85a00105e02d5b9160f24e0
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot1
Non-C++ files changed (not verified, review separately):
  refactor/J16_eq_header_type_extraction.md

Files verified: 4
Boilerplate: +25 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT=0
```

**Exit 0, `+0 / −0` substantive — the prediction held exactly.** Nothing was
flagged, so there is no flagged-line list to enumerate; nothing was contorted to
get there. The 25 allowed boilerplate lines are exactly the unit's non-move
content, enumerated: 11 comment lines across the three new headers + 3 comment
lines in `eq.h` (14 total, all `//`), 3 `#pragma once`, 5 `#include` lines in
the new headers (`<cstdint>` and `<string>` twice, `client/state/game_state.h`
once), and 3 `#include` lines in `eq.h`. `14 + 3 + 5 + 3 = 25` ✅.

I confirmed all three of the verifier's lying modes were avoided: `--repo`
points at my slot (not `main/`), `$BASE` is the **merge-base**, not `fable`
(which has moved), and the tree was **committed first** — the run reports
"Files verified: 4", not "No C++ files changed".

### 12. Acceptance bar — every step, by exit code

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (read from a unit-private exit file). Backgrounded past the 600 s tool ceiling and finished on its own — **not** an exit 143, so no ELF-truncation scan was warranted. |
| `scripts/build-gles2-x86.sh` — **run BEFORE ctest** (correction 41) | **exit 0** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **exit 8**, `3497/3498 passed`, **one failure: `AudioManagerTest.PreloadCommonSounds`** — the documented ALSA host quirk, the sole entry in `refactor/test_baseline.txt`. **Zero new failures.** 17 skips, all pre-existing. No `HeadlessShutdownTest` flake, no discovery failure. |
| `visual_gate` | **Passed, 22.80 s, inside the ctest run.** I built gles2 **first**, so the gate tested this unit's code: `build-gles2/bin/gles2_zone_benchmark` is stamped **22:09:17** against my last edit at **21:47:42** — the check J25 and J30 say to make. **`git status --porcelain` is empty; no golden was touched.** |
| `check_file_sizes.sh --files` on all four | `checked 4 file(s): 3 ok, 1 warn, 0 fail` — the one WARN is `eq.h` at 2,025, **expected and accepted** |
| `git diff --raw` modes | `100644 → 100644` on `eq.h` (its original mode, preserved — `cp` onto the existing file keeps it); the three created files `chmod 644` before staging, all recorded `100644`. **No spurious mode change.** |
| `git diff --stat` | names **exactly four** code files — `eq.h` and the three new headers — plus this plan file. Nothing under `src/`, `scripts/`, `docs/`, `CMakeLists.txt` or `tests/`. **No guard repoint was needed, so there is no fifth file.** |

**Final line counts, computed at the tip, not quoted:**

| file | lines |
|---|---|
| `include/client/eq.h` | **2,025** (was 2,635; −610) |
| `include/client/eq_opcodes.h` | **384** |
| `include/client/eq_enums.h` | **146** |
| `include/client/eq_types.h` | **111** |
| total | **2,666** (2,635 + the 25 boilerplate lines + 6 blank/structural) |

Wall clock ≈ 55 minutes. **Slowest step by a wide margin: the main `build -j24`
at ~12.5 minutes** — `eq.h` has 87 includers, so this unit is close to a full
rebuild, exactly as the plan warned. The ARM and gles2 builds were both well
inside one tool call. **No wave concurrency was observed** (the plan requires
J15 and J16 to be serialised, and J15 was already merged); nothing in the
verification tail stretched, and per correction 14 I am reporting that as
ordinary, not as an anomaly.

### 13. Findings only — recorded, not acted on

1. **`docs/module_map.md` needs rows for the three new headers.** Not touched —
   out of territory.
2. **The plan's includer count of 88 measures 87 at `55a7be1e`.** Recorded as a
   measurement, not investigated; the count is stable across this unit.
3. **`tests/test_wire_layout_t145.cpp:33` and `tests/test_eq_commands_equip.cpp:1152`
   carry prose citations of the opcode source by its old file name.** They are
   comments naming where the values were copied from; the values themselves are
   duplicated deliberately in those tests. **Correction 24: this split did not
   invalidate them and I am not attributing any staleness to it** — the opcode
   values are unchanged and still reachable through the same include. Recorded
   for whoever next touches those files.
4. **The plan's own §Header shape blocks violate correction 45/62** — they spell
   the new headers' base names and the old file name inside comment text that
   this plan's own `tests`/`scripts` grep and Guard 1's regex read. J14 and J15
   each found the same defect in their plans. **This is now four consecutive
   units finding a self-contradiction in the prescribed header block; the
   template should generate the comment text from the audit, not alongside it.**
5. **The plan's `//`-stripping comment-depth counter and its structural
   `body(header)` definition both worked unmodified** and are worth keeping
   verbatim in the template.

### What the template still gets wrong

- **§Measured line numbers go stale within hours.** Every single one in this
  plan was off by one (and the class region by seven) less than a day after it
  was written. The **range lengths** (379/137/100) were exact. **The template
  should state lengths and anchors — "the range from the blank before
  `struct WorldServer` to the `using GroupMember` line, 100 lines" — and stop
  publishing absolute line numbers**, which are the only part that rots.
- **The `+0/−0` prediction was correct here and wrong for J15 and J17 for a
  reason the template can compute mechanically.** The whole delta is
  `+(N−1)` per non-boilerplate declaration in a duplicated preamble. **The
  template should require the executor to run one grep — count the forward
  declarations in the preamble being copied, times the number of copies minus
  one — instead of restating "expect +0/−0" and then being surprised.** This
  unit predicted zero *because* it copies no preamble, and said so in advance.
- **The dispatch and the plan file disagreed on the plan's own filename**
  (`J16_eq_header_types_split.md` vs the actual
  `J16_eq_header_type_extraction.md`). Cheap to hit, cheap to fix.
