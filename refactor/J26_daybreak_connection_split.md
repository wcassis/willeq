# J26 — Batch J Wave 6: split `src/common/net/daybreak_connection.cpp` (1,985 → 3 TUs, largest ~793)

**Slot: assigned at dispatch. Branch `task/J26-daybreak-connection-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 6 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND all eleven
correction sections (thirty-three numbered lessons from seventeen executed
units; newer text wins over older)**, then
**`refactor/J13_eq_bridge_split.md` including its Review** — the batch's
best-instrumented unit and the model for how a text-scanning test guard is
handled — and `refactor/J12_eq_action_handler_split.md` §Review. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behaviour changes, no interface changes.

Method definitions move **verbatim**. **No header changes.** **No method
decomposition** — §Optional of the batch plan is explicit that a unit which
decomposes a dominant method is out of scope. No drive-by fixes. No reordering.
No deletion of dead content: a J unit **records** it and **moves it verbatim**.

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/common/net/daybreak_connection.cpp` — **1,985 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,985** |
| preamble | **1-18** — 7 includes, **two `constexpr` window limits (10-11)**, **one `static constexpr` buffer size (14)**, and ⚠ **the static member DEFINITION `int EQ::Net::DaybreakConnection::s_debug_level = 0;` at 17** |
| namespace | **none** — this file uses fully-qualified `EQ::Net::…::` definitions throughout and opens no namespace. Each TU ends with its last method's closing brace. |
| method definitions | **50** |
| largest method | **453** `DaybreakConnection::ProcessDecodedPacket` (757-1209); 2nd **130** `ProcessPacket` (480-609); 3rd **121** `ProcessQueue` (611-731) |
| `// ====` banners | **zero** |
| conditional preprocessor directives | **zero.** No conditional compilation anywhere in the file. |
| file-scope `static` / anonymous namespaces | ⚠ **four non-boilerplate preamble lines (10, 11, 14, 17) plus two `static` free functions `Inflate` (1343) and `Deflate` (1381).** Zero anonymous namespaces. See §Correction 21 — **line 17 is a DEFINITION and duplicating it is a link error.** |
| multi-line comment regions | **zero** — `grep -n "/\*"` returns nothing at all. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/common/net/daybreak_connection.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {480,1211}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-8, 15-16, 18 | preamble: the 7 includes, the `// Static debug level definition` comment and the blank lines | copied to each new TU **minus lines 16-17** — see §Correction 21 | 9 |
| **9-14** | ⚠ `// observed client receive window …` + `MAX_CLIENT_RECV_PACKETS_PER_WINDOW` + `MAX_CLIENT_RECV_BYTES_PER_WINDOW` + `// Send buffer for InternalSend …` + `SEND_BUFFER_SIZE` | **move to `daybreak_connection_send.cpp` ONLY** — every reader is there | 6 |
| **16-17** | ⚠ `// Static debug level definition` + `int EQ::Net::DaybreakConnection::s_debug_level = 0;` | **stays in `daybreak_connection.cpp` ONLY.** ⚠ **Copying it to a second TU is a duplicate-symbol link error** | 2 |
| **19-479** | `DaybreakConnectionManager` in full: ctor, `Tick` (37-71), `Process` (96-185), `ProcessPacket` (235-301), `Connect`, `NewConnection`, the option accessors — the manager half of the file | **stays** `daybreak_connection.cpp` | **479** |
| **480-1210** | `DaybreakConnection::ProcessPacket` (480-609) / `ProcessQueue` (611-731) / `ProcessDecodedPacket` (**757-1209, 453 lines, moved WHOLE and VERBATIM**) — the inbound decode path | `daybreak_connection_decode.cpp` | **749** |
| **1211-1985** | `ValidateCRC` (1211-1243) / `Decompress` (1411-1456) / `Compress` / **the two `static` free functions `Inflate` (1343) and `Deflate` (1381)** / `ProcessResend` (1487-1561) / `InternalSend` (1714-1797) / `InternalQueuePacket` (1799-1905) / `FlushBuffer` (1907-1962) — validation, compression and the outbound path | `daybreak_connection_send.cpp` | **793** |

**Result: 3 TUs** (`daybreak_connection.cpp` stays and counts). Largest **~793**.

Arithmetic that must hold: **`479 + 731 + 775 = 1985` over the whole file (the preamble is inside the first range)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: **1-479** is `DaybreakConnectionManager`
— a different class, ending exactly where the last of its methods does.
**480-1210** is the inbound path: raw packet in, decoded packet dispatched.
**1211-1985** is everything outbound plus the compression and CRC primitives the
outbound path owns.

⚠ **`ProcessDecodedPacket` is 453 lines and is NOT to be decomposed.** §Optional
of the batch plan is explicit. It lands in a 749-line TU with 751 lines of
headroom, so no Batch-G decision is needed. Precedent: J02 (`DispatchZonePacket`
880), J13 (`ProcessBridgeIntents` 580) — both flagged, neither touched.

⚠ **This is the only `src/common/` file in the batch.** Its TUs go in
`SET(COMMON_SOURCES`, not `CLIENT_CORE_SOURCES`, and three test targets compile
it directly.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 480 | `478: }` (end of `DaybreakConnectionManager`'s last method), `479` blank | `480: void EQ::Net::DaybreakConnection::ProcessPacket(Packet &p)` |
| 1211 | `1209: }` (end of `ProcessDecodedPacket`), `1210` blank | `1211: bool EQ::Net::DaybreakConnection::ValidateCRC(Packet &p)` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/common/net/daybreak_connection.cpp
```

## ⚠ HAZARD 1 — ⚠ `s_debug_level`'s DEFINITION must appear in exactly ONE TU

`int EQ::Net::DaybreakConnection::s_debug_level = 0;` at **line 17** is the
out-of-class **definition** of a static data member. It has **external linkage**.
**The verbatim-preamble rule would copy it into all three TUs and the link would
fail with a duplicate symbol** — the loudest possible failure, but one an
executor may misread as "the split broke the build".

**This plan therefore prescribes an enumerated preamble deviation** (the
correction-20 discipline): lines **16-17 stay in `daybreak_connection.cpp` and
appear in no other TU.**

⚠ **This is a NEW hazard class for the batch.** Every prior unit's preamble held
only declarations (`extern`, `typedef`) or internal-linkage constants, which are
safe to duplicate. **A definition is not.** Before copying any preamble, grep it
for out-of-class static member definitions and non-`inline` non-`static`
file-scope definitions:

```bash
sed -n '1,18p' src/common/net/daybreak_connection.cpp
```
and confirm that **line 17 is the only such line.**

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `daybreak_connection.cpp`:**

`src/common/net/daybreak_connection.cpp` has **zero anonymous namespaces** and
**six file-scope symbols**. All six were mapped onto this plan's ranges:

| symbol | definition | readers | range holding the readers | verdict |
|---|---|---|---|---|
| `MAX_CLIENT_RECV_PACKETS_PER_WINDOW` | 10 | **1524** | **1211-1985** | move def to that TU |
| `MAX_CLIENT_RECV_BYTES_PER_WINDOW` | 11 | **1525** | **1211-1985** | move def to that TU |
| `SEND_BUFFER_SIZE` | 14 | **1730, 1762, 1764, 1772, 1774** | **1211-1985** | move def to that TU |
| `s_debug_level` (definition) | 17 | class-static, read via `DaybreakConnection::s_debug_level` in every TU | — | ⚠ **definition stays in ONE TU only** (HAZARD 1); the *declaration* is in the header, so all TUs can read it |
| `static uint32_t Inflate` | 1343-1379 | **1431** | **1211-1985** | already in the same range ✅ |
| `static uint32_t Deflate` | 1381-1409 | **1466** | **1211-1985** | already in the same range ✅ |

**Every internal-linkage symbol ends up in the same TU as every one of its
readers, and no internal header is needed.** `Inflate`/`Deflate` and their
callers are already inside range 1211-1985 by construction; the three constants
are moved out of the shared preamble into that TU's preamble, which costs zero
verifier lines because they move rather than duplicate.

⚠ **The alternative — copying 9-14 into all three preambles — is legal
(internal linkage) but creates three copies of three tuning constants**, and this
project's dominant defect class is *duplicated-then-drifted*. **Rejected.**

Re-derive it yourself; do not cite this table:

```bash
grep -n '^static \|^constexpr \|^const \|^namespace\s*{\|^int EQ::' src/common/net/daybreak_connection.cpp
for s in MAX_CLIENT_RECV_PACKETS_PER_WINDOW MAX_CLIENT_RECV_BYTES_PER_WINDOW \
         SEND_BUFFER_SIZE Inflate Deflate; do
  echo "-- $s"; grep -n "\b$s\b" src/common/net/daybreak_connection.cpp
done
grep -rn 'Inflate\|Deflate' src/ include/ | grep -v daybreak_connection.cpp
```

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `daybreak_connection.cpp`'s preamble, lines 1-18, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

⚠ **Deviation, enumerated and justified (correction 20's discipline). Three
different preambles, and they are not interchangeable:**

| TU | preamble lines it carries |
|---|---|
| `daybreak_connection.cpp` (stays) | **1-8, 15-18** — the includes plus the `s_debug_level` definition and its comment. **Lines 9-14 are removed from it** (they move). |
| `daybreak_connection_decode.cpp` | **1-8, 18** — the includes only. **No constants, and NO `s_debug_level`.** |
| `daybreak_connection_send.cpp` | **1-8, 9-14, 18** — the includes **plus** the three constants and their two comments, moved verbatim from the original. **NO `s_debug_level`.** |

**Lines 1-8 are copied verbatim in all three cases** — the whole include block,
not "the includes it actually needs".

**There is no namespace wrapper.** Each new TU ends with its last method's
closing brace and nothing else.

## File-header comments

`daybreak_connection.cpp` has **no file-header comment block.** **Each of the
three TUs gets a short one** (four to six lines) naming its clusters and citing
`refactor/J26_daybreak_connection_split.md`. That is the unit's only non-move
content; enumerate every line of it.

`daybreak_connection.cpp`'s header must record that **the `s_debug_level`
definition lives here and must not be copied elsewhere.**
`daybreak_connection_decode.cpp`'s must record that **`ProcessDecodedPacket` is
453 lines and is a recorded Batch-G candidate J26 deliberately did not touch.**

Comment lines are allowlisted boilerplate for `verify_move_only.py`, so header
edits are permitted — but they **are** visible in the line-multiset check, so
**enumerate every header line you wrote or changed in your Review**, exactly as
J07 did. **Change no other comment.**

⚠ **Correction 30: the tail convention double-spaces some TUs.** Replicating
"blank + namespace close" produces a double blank line in any TU whose body
already ends on a blank. It is cosmetic and invisible to both proofs and the
verifier — **declare it, do not hand-fix it.**

## CMake — grep, never trust a line number

```bash
grep -rn 'daybreak_connection\.cpp' CMakeLists.txt tests/ scripts/
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26).** J20 split it
into 18 per-domain fragments under `tests/cmake/`; the top-level file is now 135
lines and holds no test targets. A plan that greps `CMakeLists.txt
tests/CMakeLists.txt` **finds a subset of the add-sites and looks complete** —
J10 would have found nine of twelve that way. **`grep -rn` across `tests/`.**

⚠ **CMake commands in this project are UPPERCASE** (correction 12) —
`grep -n 'set('` and `grep -n 'add_executable('` both return nothing, silently,
which reads exactly like "no sites here". **Grep for the filename.**

⚠ **Add-site counts have ranged from 1 to 19 across this batch** (J01 1, J02 1,
J04 5, J05 19, J08 2, J09 4, J10 12, J12 1, J13 1). **Report the count you
actually found**, and if it differs from the FOUR below, say so loudly.

At `824adb28` this file has **FOUR real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:257` | `SET(COMMON_SOURCES` (opens at `:248`) — ⚠ **not `CLIENT_CORE_SOURCES`** | ⚠ **YES** |
| `tests/cmake/network.cmake:50` | `ADD_EXECUTABLE(test_daybreak_connection_pcap` (`:48`) | ⚠ **YES** |
| `tests/cmake/network.cmake:66` | `ADD_EXECUTABLE(test_daybreak_crc_capture` (`:64`) | ⚠ **YES** |
| `tests/cmake/network.cmake:81` | `ADD_EXECUTABLE(test_daybreak_state_machine` (`:79`) | ⚠ **YES** |

⚠ **Three of the four add-sites are TEST targets in `tests/cmake/network.cmake`
— a fragment that did not exist before J20** (correction 26). A plan that greps
`CMakeLists.txt tests/CMakeLists.txt` finds **one of four** and looks complete.
Add **both** new TUs at **all four** sites.

**This unit does NOT touch `gles2_zone_benchmark`.** ⚠ But the three
`test_daybreak_*` targets **skip unless a capture exists** at
`data/captures/session.pcap` — a missed add-site there can surface as a *skipped*
test rather than a failure. **Check the target actually links, not just that
`ctest` is green.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'daybreak_connection' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No repoint is needed.** (J11 was the
cautionary case: four tests read `irrlicht_renderer.cpp` as text; three would
have failed and one would have **silently asserted nothing**. J25 and J31 in this
same wave both have that problem. You do not.)

⚠ **Re-run both at your tip and paste them.** If a text reader has appeared, add
it to territory and repoint it — that is correction 10, not scope creep.

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end two ways and they are not the same** (correction 23):
  killed at the 600 s tool ceiling with **exit 143 (SIGTERM)**, or
  **backgrounded by the tool** and later reporting its own exit code (J08's ARM
  build did the latter — ~11 min, exit 0, binaries fine). **Only a real 143
  invalidates binaries.**
- ⚠ **After a real exit 143, scan every produced binary for valid ELF MAGIC, not
  size** (corrections 27 and 32). J10's truncated files were 50-71 MB with
  all-zero ELF headers; the orchestrator's was 377 MB. **No size heuristic
  catches these.** And the scan's outcome is **not predictable from the exit
  code**: a killed *link* makes `gmake` delete its own partial output (J13 — the
  target was *absent*, and the scan correctly found nothing), while a killed
  *compile* can leave a truncated file behind (J07 twelve, J10 three, J12 two).
  **Absent and truncated are different failures with the same trigger. Run the
  scan either way; it costs seconds.**
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || echo "BAD $f"
  done
  ```
  (correction 29: `readelf -h` is stronger than the `head -c 4 | od` recipe and
  equally cheap.) `rm` every hit, then rebuild once.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it** — re-run it alone before
    reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (correction 16): check the binary
    with `--gtest_list_tests` before concluding a mid-link truncation.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink. If you diff
    `ctest -N`, redact `72-byte object <...>` first.

- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer, and
  two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd`; agent threads reset cwd
     between calls). Omitting it silently verifies `/mnt/projects/fable/main`
     and prints a loud FAIL full of some unrelated merged unit's lines. J02 hit
     exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG** — units merge during a
     wave, so `fable` moves under you and `fable..HEAD` reports the *other*
     units' merged work, in reverse, as substantive removals. J01 saw 533
     spurious removals this way. **Use the fork point.**
     ⚠ **Failures 1 and 2 look IDENTICAL** and have different causes. An
     executor who "fixes" the base and re-runs without `--repo` is still
     verifying the wrong tree. **Check both before changing a line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J26: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied include block (1-8) is **100% boilerplate** (`#include`).
  - Lines 9-14 **move**, they do not duplicate: three constants and two comments
    leave `daybreak_connection.cpp` and appear once in
    `daybreak_connection_send.cpp`. Each distinct line's count is 1 before and 1
    after. **Net zero.**
  - Lines 16-17 **do not move at all** — they stay put, untouched.
  - Header-comment lines are `//` — boilerplate.
  - No namespace open/close to replicate; no internal header; no test repoint.
  - **Total: ADDED 0, REMOVED 0 → exit 0.**

  ⚠ If you instead copy 9-14 into every preamble (rejected above), the prediction
  becomes **exit 1 with 3 added substantive lines** (three distinct constant
  definitions, each `+2x`… which is 6, not 3 — **which is exactly why you derive
  the number from what you wrote rather than guessing it**). Do not take that
  option.

  Paste the full output and enumerate every flagged line. **Any line beyond the
  enumerated set is a real finding — chase it.** **NEVER contort code to force a
  green exit** (J04 correctly refused to). If the verifier exits 1 with exactly
  the predicted set, that is the §Process step-4 / correction-5 case: **expected,
  not a failure** — request orchestrator sign-off in the Review.
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment (correction 23), so a range duplicated into two TUs, or dropped from
  both, is invisible to it. **This check is not optional.**
  ```bash
  cat <the 3 TU bodies, each stripped of its preamble and namespace close> \
      | sort > /tmp/after.txt
  git show "$BASE:src/common/net/daybreak_connection.cpp" | sed -n '19,1985p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **The moved preamble lines 9-14 must be counted** (correction 7): a proof run
  only over lines 19-1985 **silently ignores the six lines this unit relocates and
  still looks clean.** Run both sides over **lines 1-1985** — the whole file —
  with the three TUs' preambles concatenated in the order this plan prescribes, or
  run the body proof over 19-1985 **and a separate three-way preamble diff**, and
  paste both.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`19-479, 480-1210, 1211-1985`) and confirm the result is
  byte-identical to `git show "$BASE:src/common/net/daybreak_connection.cpp" | sed -n '19,1985p'`.
  **1,967 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**

- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` **p95 CPU
    11.684 vs a 10.948 ceiling with ZERO image mismatches**; the re-run measured
    4.676. That is a 2.5x swing on a 2x ceiling under wave load: **contention,
    not your change. Re-run.**
  - ⚠ **A failure at the RENDER step is a different animal**: it means a
    `gles2_zone_benchmark` CMake add-site was missed. **This unit has no `gles2_zone_benchmark` site**, so a render-step failure here would be a genuine surprise — report it, do not re-run past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/common/net/daybreak_connection.cpp`, `src/common/net/daybreak_connection_decode.cpp`, `src/common/net/daybreak_connection_send.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,985** this plan measured,
   and any range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `824adb28`, **STOP and report**.
3. ⚠ **Correction 21, explicitly.** Paste the grep, name the range this plan
   assigns each file-local symbol and the range it assigns each consumer, and
   confirm they are the same range. **Do not cite §Correction 21 — re-derive it.**
4. **Comment depth at every cut**, computed with the `//`-stripping counter
   above, not asserted.
5. **The preprocessor dump**, pasted, confirming no cut lands inside a
   conditional region.
6. **Every file-header comment line you wrote**, enumerated.
7. **How many CMake add-sites you actually found**, against the FOUR
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts roughly 17 / 3 / 30 of the 50), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. ⚠ **`grep -rn 's_debug_level = 0' src/` on the finished tree, pasted**, showing
    **exactly one** definition. This is the unit's single most important check and
    the build will tell you loudly if you got it wrong.
12. **The four CMake add-sites, each confirmed** — one in `COMMON_SOURCES`, three
    in `tests/cmake/network.cmake` — and confirmation that the three
    `test_daybreak_*` targets **link**, not merely that `ctest` skipped them.

## Territory

`src/common/net/daybreak_connection.cpp`, the two new TUs beside it
(`src/common/net/daybreak_connection_decode.cpp`,
`src/common/net/daybreak_connection_send.cpp`), `CMakeLists.txt` (the one
`COMMON_SOURCES` site), `tests/cmake/network.cmake` (the three test add-sites),
and this plan file. **Nothing else** — not
`include/common/net/daybreak_connection.h` (this unit changes no interface and
promotes nothing), not any `tests/*.cpp`, not `docs/`.

## STOP conditions

- A file-scope symbol, an anonymous namespace, or a multi-line comment region
  has appeared in a place §Measured says there is none.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- The verifier reports any substantive line beyond the predicted set.
- Either content proof fails after you have satisfied yourself the proof itself
  is correct.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to decompose a method, delete dead content, trim an
  include block, or add a TU this plan does not name.
- A second out-of-class static member definition has appeared in the preamble.
