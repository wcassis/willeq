# J13 — Batch J Wave 4: split eq_bridge.cpp (2,127 → 3 TUs, largest ~891)

**Slot: assigned at dispatch. Branch `task/J13-eq-bridge-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 4 of `refactor/J_file_size_reclamation.md`, alongside **J12**. Read, before
editing anything: **`refactor/J_file_size_reclamation.md` §Process per unit AND
all eight correction sections (twenty-four numbered lessons from J01–J11;
newer text wins over older)**, then `refactor/J09_zone_shader_split.md`
**including its Review** (the cleanest plan in the batch) and
`refactor/J11_irrlicht_renderer_split.md` §Review — **J11 is this unit's
cautionary twin: four tests read its file AS TEXT and had to be repointed,
outside the plan's stated territory, and three of them would have failed.**
**This unit has that problem too, and it is the only real hazard here.** Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## ⚠ THE ONE THING THIS UNIT MUST NOT GET WRONG: `test_reachability_sweep.cpp`

**`tests/test_reachability_sweep.cpp` opens `src/client/eq_bridge.cpp` and scans
it as text, at THREE sites, and all three break when `ProcessBridgeIntents`
moves out of that file.** This is correction 10 (J11's), and it is **not
optional and not scope creep** — it is part of this unit's territory.

At `136a319b`:

| site | code | what happens after the split if you do nothing |
|---|---|---|
| `:221` | `std::string bridge = readFile(kRoot / "src/client/eq_bridge.cpp");` in `TEST(ReachabilitySweep, EveryIntentHasAConsumerArm)` — asserts every `RendererIntent` member has an `is_same_v<T, eqt::events::…>` arm | ⚠ **FAILS for EVERY intent member.** All 33 arms move to `eq_bridge_intents.cpp`. This is the loudest possible failure and the easiest to misdiagnose as "the split deleted the intent arms". |
| `:238` | `if (f.rel == "src/client/eq_bridge.cpp") continue;` inside `TEST(ReachabilitySweep, ProducerlessIntentsAreExactlyTheBlockedList)` — excludes the *consumer* file from the *producer* sweep over `src/` and `include/` | ⚠ **May silently invert the test's meaning.** After the split the consumer arms live in a file the sweep no longer excludes, so a consumer line can be mistaken for a producer, the `producerless` set shrinks, and `expectSetsEqual` fails against `kBlockedIntents` — or worse, a genuinely producer-less intent is quietly declared reachable. |
| `:258` | the same `readFile(… "src/client/eq_bridge.cpp")` line again, pinning that every **blocked** intent still has its consumer arm | ⚠ **FAILS**, same cause as `:221`. |

**The prescribed repoint — exactly three edits, no more:**

1. `:221` — `"src/client/eq_bridge.cpp"` → `"src/client/eq_bridge_intents.cpp"`.
2. `:258` — the same change. (These two lines are **textually identical**, so
   they are one distinct line appearing twice.)
3. `:238` — **ADD** a line excluding the new consumer TU, and **KEEP** the
   existing exclusion of `eq_bridge.cpp`:
   ```cpp
   if (f.rel == "src/client/eq_bridge.cpp") continue;
   if (f.rel == "src/client/eq_bridge_intents.cpp") continue;
   ```
   Keeping both makes the guard **exactly as strict as it is today** — the
   content previously excluded is still excluded, wherever it now lives.
   **Do not replace the existing line**; replacing it would newly expose the
   surviving `eq_bridge.cpp` to the producer sweep, which is a behaviour change
   wearing a repoint's clothes.

Also update the `:224` failure message string
(`"… has no arm in eq_bridge.cpp ProcessBridgeIntents"`) **only if you can do it
without changing what is asserted** — it is a diagnostic string, and a wrong
filename in a failure message is exactly the doc rot J21 exists to clean up.
**Prefer updating it; count it in your exception list if you do** (see
§Acceptance for how that changes the predicted verifier output — and if you
update it, the prediction below changes by +1/-1, so **say so**).

⚠ **This unit's verifier prediction is derived from these three edits.** See
§Acceptance.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. **No change to `include/client/eq.h`.**
**No method decomposition — in particular, `ProcessBridgeIntents` is NOT to be
decomposed** (see §The Batch-G flag). No drive-by fixes. No reordering.

## ⚠ The Batch-G flag: `ProcessBridgeIntents` dominates its TU, and that is FINE

`EverQuest::ProcessBridgeIntents` is **892-1471 — 580 lines**, not the 558 on
record in §Optional of the batch plan (it has grown; **correct that record in
your Review**). It is 83% of the TU this plan gives it and there is no room to
grow. **That is a Batch-G candidate, recorded here so the decision can be taken
separately. It is NOT this unit's problem and NOT this unit's scope.** §Optional
of the batch plan is explicit: a unit that decomposes one of the eight dominant
methods is out of scope. The ceiling is cleared without touching it — the TU
lands at ~700 lines.

Precedent: J02 gave `DispatchZonePacket` (880 lines) its own ~965-line TU and
flagged it rather than decomposing it. Do the same.

## Measured at `136a319b` (2026-09-02, `fable` tip after J08 merged)

`src/client/eq_bridge.cpp` — **2,127 lines**. ⚠ **The unit table's 2,098 and
`docs/module_map.md:27`'s 2,001 are both STALE** (correction 9). **Re-verify at
your own branch tip before you cut** and report the drift in your Review rather
than silently adjusting.

**45 method definitions**, all `EverQuest::` members. **There is no namespace
wrapper** — `grep -n 'namespace' src/client/eq_bridge.cpp` returns exactly one
hit, `using namespace eqt::inventory;` at **1501, inside a method body**. So
unlike J12 there is no namespace open to copy and no namespace close to
replicate: **each new TU ends with the last method's closing brace and nothing
else.**

**Zero file-scope `static`s** (`grep -n '^\s*static '` → nothing) and **zero
anonymous namespaces.** ⚠ **Therefore correction 21 — the batch's first
build-step plan defect, where a file-local helper was assigned to a different
range from its sole consumer — has NO exposure in this file.** Nothing needs
promoting; **no internal header is required.** **Verify with your own grep and
paste it** rather than citing this sentence.

**Preamble is lines 1-106, and it is 100% boilerplate to the verifier:**

| lines | content | boilerplate to `verify_move_only.py`? |
|---|---|---|
| 1-13 | the F10 file-header comment (`// eq_bridge.cpp`, what the TU contains, "this is a pure move … see refactor/F10_bridge.md") | yes (`//`) — **and this is the block each new TU adapts**, see §File-header comments |
| 14-36 | the unconditional include block, with an `#ifdef EQT_HAS_NAVMESH` pair at 18/20 | yes |
| 38-43 | the P28 comment explaining which headers are gated and which are not | yes |
| 44-51 | eight unconditional data-model includes | yes |
| 52-65 | `#ifdef EQT_HAS_GRAPHICS` … `#endif` — 12 renderer-only includes | yes |
| 67-72 | `<limits>`, `game_state_bridge.h`, `position_frames.h`, `irrlicht_bridge.h` | yes |
| 73-80 | `#ifdef WITH_AUDIO` … `#endif` — 6 audio includes | yes |
| 82-98 | `posix_udp_transport.h`, `animation_constants.h`, 15 standard headers | yes |
| 99-101 | `#ifdef __linux__` / `#include <unistd.h>` / `#endif` | yes |
| 103-105 | `#ifndef M_PI` / `#define M_PI 3.14159265358979323846` / `#endif` | yes (`#ifndef`, `#define`, `#endif`) |
| 106 | blank | dropped by the normalizer |

⚠ **Unlike J10 and J11, this preamble contains NO non-boilerplate line** — no
bare `extern`, no `typedef`, no stub. Every physical line matches
`BOILERPLATE`, and blank lines are dropped before comparison. **So the preamble
copies cost ZERO verifier exception lines.** (Correction 24's "when a preamble's
non-boilerplate content is enumerable, predict the exact count" — here the count
is enumerably zero.)

⚠ **`#define M_PI` at 104 is read by the body** and **`EQT_HAS_GRAPHICS` /
`WITH_AUDIO` gate whole method definitions.** The verbatim-preamble rule carries
all of this into every new TU, which is exactly why §Process forbids trimming
per-TU include sets. **Do not trim.**

Body is **107-2127**. **The file ends at 2127 with `}` — the close of
`ActivateHotbarCommandSlot`. There is no trailing namespace close.**

**Three `// ====` banners only**, at 821-823 (T269 bank-coin helpers),
1579-1581 (Player Mode Loot Window Methods) and 1878-1880 (Audio System). The
banner rule cannot decide the cuts; **use J03's rule** (correction 11): *cut at
a blank line whose predecessor closes a method, verify comment depth 0 at the
cut.*

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-106 | preamble (see table above) | copied to each new TU, with the 1-13 header adapted | 106 |
| **107-891** | ⚠ **`#ifdef EQT_HAS_GRAPHICS` (107) … `#endif // EQT_HAS_GRAPHICS` (221)** wrapping `InitGraphics` (110-195) and `ShutdownGraphics` (200-220) — **both wholly inside it**; then `wireBridgeToSubsystems` (235-255) / `setBridge` (257-259) / `GameTick` (271-297) / `PostRenderTick` (307-340) / `CreateZoneLoadSnapshot` (345-425) / `SetZoneBspTree` (427-438) — with the second `#ifdef EQT_HAS_GRAPHICS` pair at **342/443**, which **opens before `CreateZoneLoadSnapshot` and closes after `SetZoneBspTree`**; then the spawn/pet bridge family `OnSpawnAddedGraphics` (450-563) / `OnPetCreated` (565-582) / `OnPetRemoved` (584-598) / `OnPetButtonStateChanged` (600-615) / `SaveEntityDataToFile` (617-663) / `OnSpawnRemovedGraphics` (665-704) / `OnSpawnMovedGraphics` (706-739) / `OnGraphicsMovement` (741-819); then the **T269 bank-coin banner (821-823)** + `BankCoinGet` (825-834) / `BankCoinSet` (836-845) / `BankCoinName` (847-855) / `BankCoinCoppers` (858-865) / `PublishBankCoinProjection` (867-890) — **22 methods** | **stays** `eq_bridge.cpp` | **~891** |
| **892-1485** | ⚠ **`ProcessBridgeIntents` (892-1471, 580 lines — the Batch-G candidate, moved WHOLE and VERBATIM)** with its two nested `#ifdef WITH_AUDIO` pairs at **1333/1339** and **1342/1348**; then the 4-line T220 comment (1473-1476) and `ProcessBridgeEvents` (1477-1484) — **2 methods** | `eq_bridge_intents.cpp` | ~700 |
| **1486-2127** | `UpdateInventoryStats` (1486-1577, containing the file's ONLY `namespace` token in the body — `using namespace eqt::inventory;` at **1501**, inside the method); the **Player Mode Loot Window banner (1579-1581)** + `RequestLootCorpse` (1583-1612) / `LootItemFromCorpse` (1614-1671) / `FindLootSlotByName` (1673-1697) / `CloseLootWindow` (1699-1738); the client-config + hotbar + inspect persistence family `ResolveClientConfigForWrite` (1746-1779) / `ResolveClientConfigForRead` (1786-1797) / `SaveHotbarConfig` (1799-1811) / `LoadHotbarConfig` (1813-1826) / `SetInspectMessage` (1837-1847) / `SaveInspectMessage` (1849-1861) / `LoadInspectMessage` (1863-1876); the **Audio System banner (1878-1880)** + ⚠ **`#ifdef WITH_AUDIO` (1882) … `#endif // WITH_AUDIO` (2039)** wrapping `InitializeAudio` / `ShutdownAudio` / `UpdateDayNightState` / `PlaySound` / `PlaySoundAt` / `PlayCombatSound` / `PlaySpellSound` / `PlayUISound` (with a nested `#ifdef EQT_HAS_GRAPHICS` at **1926/1942**); finally `UseInventoryItem` (2049-2091) / `ActivateHotbarCommandSlot` (2102-2127) — **21 methods** | `eq_bridge_ui_audio.cpp` | ~748 |

**Result: 3 TUs** (`eq_bridge.cpp` stays and counts). Largest **~891**.

Arithmetic that must hold: `106 + 785 + 594 + 642 = 2127`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — **find it before you build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions.** The three
counts above are `106 + range` and nothing else. Your new file-header comment
lines land on top of that; **say so explicitly** (correction 19: a predicted
number must be derived from what the plan prescribes).

Why the boundaries fall where they do: the file has three genuinely separate
concerns and the cuts are the two places where one ends. **107-891** is the
renderer-facing lifecycle: graphics init/shutdown, the tick methods, the
zone-load snapshot, the spawn/pet/movement bridge callbacks, and the bank-coin
helpers the intent arms call into. **892-1485** is the bridge pump itself, both
directions. **1486-2127** is everything that rides the bridge rather than being
it: inventory stat sync, the loot-window UI flow, config persistence, audio, and
item use.

⚠ **Nothing crosses a cut.** `ProcessBridgeIntents` calls the bank-coin helpers
in the stays TU — but they are **public `EverQuest` members declared in
`include/client/eq.h`**, not file-local statics, so a cross-TU call is a normal
link. **There are no file-local helpers in this file at all** (correction 21).

## Every cut boundary, with its neighbours — verify each at your tip

The rule is J03's: **a blank line whose predecessor closes a method, comment
depth 0 at the cut.** Checked at `136a319b`:

| Cut | line-2 | line-1 (blank) | line (first of next range) |
|---|---|---|---|
| 107 | `105: #endif` (closes `#ifndef M_PI`) | `106` | `107: #ifdef EQT_HAS_GRAPHICS` |
| 892 | `890: }` (end of `PublishBankCoinProjection`) | `891` | `892: void EverQuest::ProcessBridgeIntents() {` |
| 1486 | `1484: }` (end of `ProcessBridgeEvents`) | `1485` | `1486: void EverQuest::UpdateInventoryStats() {` |

**Both interior cuts land on a plain method boundary with no banner and no
conditional.** Paste lines `888-894` and `1482-1488` in your Review.

Comment depth at both cuts is **0**. Verify it — do not cite this sentence:

```bash
grep -n '/\*' src/client/eq_bridge.cpp
```

## ⚠ HAZARD 1 — the preprocessor. Nineteen directives; THREE regions span whole methods.

**Run this and paste it:**

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/eq_bridge.cpp
```

At `136a319b`, the body's regions (the preamble's six pairs are listed in
§Measured):

| pair | guard | where | note |
|---|---|---|---|
| **107 / 221** | `EQT_HAS_GRAPHICS` | 107-891 | ⚠ **115 lines wrapping `InitGraphics` (110-195) and `ShutdownGraphics` (200-220) whole** |
| 182 / 184 | `WITH_AUDIO` | 107-891 | nested inside the above |
| 272 / 283 | `EQT_HAS_GRAPHICS` | 107-891 | inside `GameTick` |
| 308 / 310 | `EQT_HAS_GRAPHICS` | 107-891 | inside `PostRenderTick` |
| 312 / 337 / 339 | `WITH_AUDIO` / `#else` | 107-891 | inside `PostRenderTick` |
| **342 / 443** | `EQT_HAS_GRAPHICS` | 107-891 | ⚠ **102 lines wrapping `CreateZoneLoadSnapshot` (345-425) and `SetZoneBspTree` (427-438) whole** |
| 1333 / 1339 | `WITH_AUDIO` | 892-1485 | inside `ProcessBridgeIntents` |
| 1342 / 1348 | `WITH_AUDIO` | 892-1485 | inside `ProcessBridgeIntents` |
| **1882 / 2039** | `WITH_AUDIO` | 1486-2127 | ⚠ **158 lines spanning all eight audio methods** |
| 1926 / 1942 | `EQT_HAS_GRAPHICS` | 1486-2127 | nested inside the above |

**None of the two interior cut points (892, 1486) lands inside any of them** —
892 sits between the `342/443` region and the `1333` one; 1486 sits between the
`1342/1348` region and the `1882` one. **Verify that is still true at your tip.**

⚠ **This is J02's failure mode with the preprocessor rather than a block
comment:** J02's plan cut *inside* a `/* … */` and would have left an
unterminated comment in one file and an unopened close in the other. A cut
inside `1882/2039` would do the same with `#ifdef`/`#endif` — and **it would
still compile in the default configuration**, because `WITH_AUDIO`… is on. The
break would surface only in a config the acceptance bar exercises. **Do not cut
inside a conditional.**

## Includes

**Each new TU carries `eq_bridge.cpp`'s full preamble, lines 14-105, verbatim**
— the whole include block including the `EQT_HAS_NAVMESH`, `EQT_HAS_GRAPHICS`,
`WITH_AUDIO` and `__linux__` groups, the P28 comment, and the `#ifndef M_PI` /
`#define M_PI` pair. **Not "the includes it actually needs":** §Process is
explicit, and here trimming would additionally break `M_PI` and the three
config guards.

**Lines 1-13 are the F10 file-header comment and are the one thing you adapt** —
see the next section.

**Each new TU ends with the last method's closing brace. There is no namespace
close to replicate.**

This unit relocates nothing into an existing file, so **correction 20 does not
apply** — every destination is a file this unit creates. (The one existing file
it touches, `tests/test_reachability_sweep.cpp`, receives three enumerated
edits, not an include block.)

## File-header comments

`eq_bridge.cpp` **already has** a 13-line F10 header describing the whole file's
contents. **Each new TU gets its own adapted version** — same shape, naming that
TU's clusters, citing `refactor/J13_eq_bridge_split.md` alongside the existing
`refactor/F10_bridge.md` reference. **The stays file's header must be updated
too**, because its current text ("Contains graphics lifecycle …, the bridge
intent/event pumps (ProcessBridgeIntents / ProcessBridgeEvents), inventory stat
sync, the loot-window UI flow …, hotbar config persistence, and audio
init/playback helpers") will be **actively wrong** after the split — it would
describe three files' worth of content in the one file that no longer holds most
of it.

`eq_bridge_intents.cpp`'s header must say, in plain words, that
**`ProcessBridgeIntents` is 580 lines and is a recorded Batch-G decomposition
candidate that J13 deliberately did not touch**, and that
**`tests/test_reachability_sweep.cpp` scans THIS file for the
`is_same_v<T, eqt::events::…>` arms** — so the next person to move an arm knows
there is a text-scanning test pointed at it.

Comment lines are allowlisted boilerplate for `verify_move_only.py`, so header
edits are permitted — but they **are** visible in the line-multiset check, so
**enumerate every header line you wrote or changed in your Review**, exactly as
J07 did (its §3 is the model). **Change no other comment**, and in particular
do not touch the P28 block at 38-43.

## CMake — grep, never trust a line number

```bash
grep -rn 'eq_bridge\.cpp' CMakeLists.txt tests/CMakeLists.txt tests/cmake/
```

At `136a319b` that is **ONE add-site plus THREE comment-only hits**:

| file:line | target | edit? |
|---|---|---|
| `CMakeLists.txt:272` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) | ⚠ **YES — add both new TUs here** |
| `tests/cmake/packets_skills.cmake:58` | a comment naming `ActivateHotbarCommandSlot`'s home | no |
| `tests/cmake/commands.cmake:150`, `:399` | comments | no |
| `tests/cmake/ui_rendering.cmake:221` | a comment | no |

**Add both new TUs beside the original at the ONE real site.** The four comment
hits are prose about which file an arm lives in; two of them
(`packets_skills.cmake:58` naming `ActivateHotbarCommandSlot`, which **stays**
in `eq_bridge.cpp`, and `ui_rendering.cmake:221`) remain correct. **Record any
that go stale; do not fix them — that is J21's** (correction 24).

⚠ **`tests/CMakeLists.txt` no longer holds test targets** — J20 split it into 18
per-domain fragments under `tests/cmake/` (top-level file is now 135 lines).
**Any test-target edit goes in `tests/cmake/<domain>.cmake`.** This unit changes
a test *source* (`test_reachability_sweep.cpp`) but adds no target and no source
to any target, so **it needs no `tests/cmake/` edit.** Confirm that.

⚠ **CMake commands in this project are UPPERCASE** — `grep -n 'set('` and
`grep -n 'add_executable'` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.** ⚠ **Add-site counts have ranged
from 1 to 19 across this batch** (J01 1, J02 1, J04 5, J05 19, J08 2, J09 4,
J10 12). **Report the count you actually found**, and if it is not 1, say so
loudly.

`CMakeLists.txt` is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- ⚠ **A long build can end TWO different ways and only one invalidates
  binaries** (correction 23): killed at the 600 s tool ceiling with **exit 143
  (SIGTERM)**, or **backgrounded by the tool** and later reporting its own exit
  code (J08's `build-arm-noble.sh` did this — ~11 min, exit 0, binaries fine).
  **After a real exit 143, and only then, scan every produced binary for valid
  ELF magic before trusting the rebuild:**
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' ')" = "7f454c46" ] || echo "BAD $f"
  done
  ```
  then `rm` every hit and rebuild once. J07 hit this at scale: **twelve binaries
  truncated mid-link**, the re-invocation **exited 0**, and `ctest` died at
  *discovery*, reporting one bad binary per full suite run. **Do not run this
  scan reflexively after every slow build; do not skip it after a real 143.**
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ **`test_reachability_sweep` is the test this unit is most likely to break,
    and it is also the test that proves the repoint worked.** Run it alone,
    before the full suite, immediately after the cut:
    `ctest --test-dir build -R ReachabilitySweep --output-on-failure`.
    A failure naming *every* intent member means you repointed `:221`/`:258`
    wrongly or not at all; a failure from `ProducerlessIntentsAreExactlyTheBlockedList`
    with a short `producerless` set means the `:238` exclusion is missing.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it.** If it fails in a wave run,
    re-run it alone before reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (correction 16): check the binary
    with `--gtest_list_tests` before concluding a mid-link truncation.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink (J20's Review). If you
    diff `ctest -N`, redact `72-byte object <...>` first.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer out of
  this script, and two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd` and agent threads reset
     cwd between calls). Omitting it silently verifies
     `/mnt/projects/fable/main` and prints a loud FAIL full of some unrelated
     merged unit's lines. J02 hit exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG**: units merge during a
     wave, so `fable` moves under you and `fable..HEAD` reports the *other*
     units' merged work, in reverse, as substantive removals. J01 saw 533
     spurious removals this way. Use the fork point.
     ⚠ **Failures 1 and 2 look IDENTICAL** and have different causes. An
     executor who "fixes" the base and re-runs without `--repo` is still
     verifying the wrong tree. **Check both before changing a line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one. **Commit first, then verify.**
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J13: EXIT 1, with exactly 3 substantive
  ADDED lines and 2 substantive REMOVED lines — and every one of them in
  `tests/test_reachability_sweep.cpp`, none in the split itself.**
  **Derivation, from what this plan prescribes and nothing else** (correction
  19):
  - The preamble copies cost **zero** — every physical line of 1-105 matches
    `BOILERPLATE` (`#include`, `#ifdef`, `#ifndef`, `#define`, `#endif`, `//`),
    and blank lines are dropped by the normalizer. This is the difference from
    J10 (21 lines, from two `typedef`s and an `extern`) and J11 (70 lines).
  - The header-comment adaptations cost **zero** — `//` is boilerplate.
  - There is no namespace open/close to replicate, so nothing there either.
  - **`:221` and `:258` are textually identical**, so the old
    `readFile(kRoot / "src/client/eq_bridge.cpp")` line is **one distinct line
    with count 2** — the verifier's `Counter` arithmetic reports
    **`-2x`** for it and **`+2x`** for its replacement.
  - The **added** `:238` exclusion line is **`+1x`**.
  - Total: **ADDED 3, REMOVED 2.**
  ⚠ **If you also update the `:224` diagnostic string, add `+1x` / `-1x` and
  report FOUR added / THREE removed instead. Say which you did.**
  **This is the §Process step-4 / correction-5 case: expected, not a failure.**
  Paste the full output, enumerate every flagged line, confirm each is one of
  the edits this plan prescribes, and request orchestrator sign-off in the
  Review. **Any line beyond the enumerated set is a real finding — chase it.**
  **NEVER contort code to force a green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  allowlists a bare `}` and every comment, so a wholly dropped block's braces
  are invisible to it (correction 23). This check is not optional.
  ```bash
  # after: the three TU bodies, each stripped of its adapted 1-13 header and of
  # the copied 92-line preamble (14-105) and the blank at 106.
  cat <the three stripped bodies> | sort > /tmp/after.txt
  git show "$BASE:src/client/eq_bridge.cpp" | sed -n '107,2127p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Run this over the THREE .cpp bodies only. `test_reachability_sweep.cpp` is
  NOT part of the moved content** and must not be concatenated into either side
  — its three edits are accounted for by the verifier, above. This unit creates
  no cross-TU header and relocates nothing into an existing file, so **neither
  correction 7 nor correction 20 applies** and the diff should be **empty.**
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  three extracted ranges **in original order** (`107-891, 892-1485, 1486-2127`)
  and confirm the result is byte-identical to
  `git show "$BASE:src/client/eq_bridge.cpp" | sed -n '107,2127p'`.
  **2,021 lines.** Expect `cmp` exit 0.
  ⚠ **Additionally and separately, prove `ProcessBridgeIntents` survived byte
  for byte** — it is 27% of the file and the thing a reviewer looks at first:
  ```bash
  diff <(git show "$BASE:src/client/eq_bridge.cpp" | sed -n '892,1471p') \
       <(sed -n '<its new range>p' src/client/eq_bridge_intents.cpp)
  ```
  **580 lines, exit 0. Paste it.**
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
    `gles2_zone_benchmark` CMake add-site was missed. **This unit has no
    `gles2_zone_benchmark` site** (its only site is `CLIENT_CORE_SOURCES`), so a
    render-step failure here would be a genuine surprise — report it, do not
    re-run past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. Do not report a clean run as an anomaly.
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/client/eq_bridge.cpp`, `src/client/eq_bridge_intents.cpp`,
  `src/client/eq_bridge_ui_audio.cpp`, `tests/test_reachability_sweep.cpp` →
  **0 warn, 0 fail.** The root `CMakeLists.txt` WARN is pre-existing and
  grandfathered; it is not yours.
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **2,127** this plan measured, and any
   range drift. The unit table's 2,098 and `docs/module_map.md:27`'s 2,001 are
   both stale — say which number you found.
2. ⚠ **`ProcessBridgeIntents`'s exact size at your tip.** This plan measures
   **580** (892-1471); §Optional of the batch plan records **558**. **Correct
   the record.** Confirm you did not decompose it.
3. **The three zero-hazard properties, re-verified with your own greps and
   pasted:** zero file-scope statics, zero anonymous namespaces, zero block
   comments — and therefore **zero correction-21 exposure**. If any has changed
   since `136a319b`, **STOP and report.**
4. **The preprocessor boundary check**, pasted, confirming neither interior cut
   (892, 1486) lands inside any of the ten body regions.
5. ⚠ **The `test_reachability_sweep.cpp` repoint, in full:** the three (or four)
   lines before and after, and the output of
   `ctest --test-dir build -R ReachabilitySweep --output-on-failure` **run alone,
   pasted.** State explicitly that you **kept** the `eq_bridge.cpp` exclusion at
   `:238` and **added** the new one, rather than replacing it — and why that
   keeps the guard exactly as strict.
6. **Every file-header comment line you wrote or changed**, enumerated,
   including the stays file's rewritten header.
7. **How many CMake add-sites you actually found**, against the ONE this plan
   names, and confirmation that no `tests/cmake/` fragment needed an edit.
8. **All three content proofs**, pasted — the multiset, the re-concatenation,
   and the standalone `ProcessBridgeIntents` diff — plus the verifier's exit
   code and `$BASE`. State whether the verifier's flagged set is **exactly** the
   3-added / 2-removed this plan predicts (or 4/3 if you updated `:224`).
9. **Final line counts of all three TUs**, method counts per TU (this plan
   predicts 22 / 2 / 21 = 45), wall clock, slowest step, and whether concurrency
   with J12 stretched the verification tail.
10. **Findings only, not fixes.** `docs/module_map.md:27` records this file as
    2,001 lines and one TU; `docs/decisions.md:403` cites `eq_bridge.cpp:947`;
    `tests/test_combat_manager.cpp:704` cites `eq_bridge.cpp:1573` for
    `LootAllFromCorpse`, **a method that does not exist in this file at all** —
    so that citation was **already wrong before this split**. That is
    **comment/doc rot, outside a move-only commit's scope** — fold it into the
    `docs/module_map.md` hygiene pass J02, J04, J06, J07, J09 and J11 already
    owe (**J21**). ⚠ **Do NOT attribute any stale line-number citation to this
    split** (correction 24): J06, J07 and J08 each checked their file's
    citations **at their base commit** and found every one already wrong.
    **Record. Do not act.**

## Territory

`src/client/eq_bridge.cpp`, the two new TUs beside it
(`src/client/eq_bridge_intents.cpp`, `src/client/eq_bridge_ui_audio.cpp`),
**`tests/test_reachability_sweep.cpp` (the three prescribed repoint edits, and
nothing else in that file)**, `CMakeLists.txt` (the one `CLIENT_CORE_SOURCES`
site), and this file. **Nothing else** — **not `include/client/eq.h`** (this
unit changes no interface and promotes nothing), not `src/client/eq.cpp` or any
other `eq_*.cpp`, not `include/client/events/renderer_intents.h`, not
`include/client/state/event_bus.h`, not any other `tests/*.cpp`, not
`tests/cmake/*.cmake`, not `docs/`.

**Correction 10 pre-check, already run for you and to be re-run at your tip.**
The exhaustive question is *which source paths does any test open as text* —
answer it with the second grep, not the first:

```bash
grep -rn 'eq_bridge' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `136a319b` the first returns ~25 hits, **all comments except the three in
`test_reachability_sweep.cpp`**; the second returns the complete list of
text-scanned sources, in which **`src/client/eq_bridge.cpp` appears exactly
twice — lines 221 and 258 of `test_reachability_sweep.cpp`** (the `:238`
exclusion is a string compare, not a `readFile`, which is why the first grep is
the one that finds it and the second alone would miss it). **Run both.**

**If a new text reader has appeared since, add it to territory and repoint it.**
That is not scope creep — it is correction 10, and J11 proved it is not
optional: **three of its four repoints would otherwise have failed and one would
have silently asserted nothing.**

## STOP conditions

- A file-scope `static` or an anonymous namespace has appeared in the body since
  `136a319b`.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- Either interior cut would land inside one of the ten preprocessor regions.
- The verifier reports a substantive line **outside** the enumerated
  `test_reachability_sweep.cpp` set.
- Any content proof fails after you have satisfied yourself the proof itself is
  correct.
- `ReachabilitySweep` still fails after the prescribed repoint — that means the
  split moved an arm, not just the file it lives in. **Report; do not "fix" the
  test until it passes.**
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself decomposing `ProcessBridgeIntents`.
