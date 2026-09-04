# G03 — `registerBuiltinCommands`: one list becomes per-family registrations, ORDER PRESERVED

Batch: `refactor/G_oversize_methods.md` (G01 corrections 1–7). Two-wide
with G02. Measured at `32e2adab`: `src/client/action/command_processor_registration.cpp`
(792 lines); `CommandProcessor::registerBuiltinCommands` is a sequence of
~142 registration calls. **Registration order is load-bearing** (J04: later
registrations shadow earlier names; T097/T098 audited shadowing). Re-measure
mechanically.

## Shape

1. Cut the sequence at its existing banners into `registerXCommands()`
   private methods (movement, social, world, bank, loot, train, spells/GM,
   …— use the banners that are there; do not invent families), each a
   verbatim slice; `registerBuiltinCommands` becomes the ordered list of
   those calls. **No reordering of any call across a cut.**
2. Proof: (a) the line multiset (every registration line moves once);
   (b) **the order test**: `tests/test_command_registration_order.cpp`
   snapshots the registry's listing (names in registration order — read
   how `merged_help.cpp`/`test_merged_help.cpp` enumerate the registry)
   against a golden `tests/goldens/command_registration_order_g03.txt`
   extracted at `fable` BEFORE the change (from a run of the old code in
   the test's own fixture, or from `git show fable:…` parsed for the
   registration names in order); assert the golden is non-empty and the
   right size in its own case (G01 5); plant the fault in the code by
   swapping two adjacent registrations across a cut and confirm the test
   fails (G01 6).
3. `test_merged_help` and `test_command_registry_*` (grep) must pass with
   assertions untouched; `test_reachability_sweep` may scan this TU for
   registration calls — read it, widen if needed.

Predicted: `registerBuiltinCommands` 768 → the number of families (≈ 8–12
lines); `command_processor.h` gains ≈ 10 declarations.

STOP: a registration depends on a local defined earlier in the sequence
that a cut would separate (then the cut moves, not the call); the order
golden differs after the change for any reason.

Territory: `src/client/action/command_processor_registration.cpp`,
`include/client/action/command_processor.h`, the new test + golden +
cmake, `tests/test_reachability_sweep.cpp` (widening only),
`docs/module_map.md`. No graphics files.

## Review

Executed 2026-09-04 in `wt/slot1`, branch `task/G03-register-builtin-commands`
off `fable` at `e06237c7`. Structural, move-only, zero behaviour change. No
STOP condition hit.

### Re-measurement at the tip (correction 1: mechanical, not the plan's numbers)

`src/client/action/command_processor_registration.cpp` was **792 lines**;
`registerBuiltinCommands()` spanned lines 21–789 (**769 lines**) and contained
**142** `registerCommand({…})` calls and *nothing else*. Verified mechanically:
every line of the body matches one of `^\s*//`, blank,
`^    registerCommand\(\{`, the argument continuation lines, or the closing
`}, [this](const std::string& args) { return cmdX(args); });` — residual **0**.

**The STOP condition "a registration depends on a local defined earlier in the
sequence" cannot arise here**: the function declares no locals at all. Every
registration captures only `this`. So every cut is legal by construction.

### The cut: 21 families at the banners that were already there

Cut points are the leading comment blocks already in the file; no family was
invented, none reordered. `<start>`/`<end>` are the new file's line numbers.

| family | start | end | registrations | the banner it was cut at |
|---|---|---|---|---|
| `registerBuiltinCommands` | 35 | 57 | 0 | the ordered call list |
| `registerChatCommands` | 60 | 110 | 9 | T98/T100 chat |
| `registerInteractionCommands` | 113 | 162 | 6 | T98 face / T51 / T153 / T171 |
| `registerGroupCommands` | 165 | 191 | 5 | T99 + T179–T184 raid |
| `registerCombatCommands` | 194 | 262 | 13 | T102 Combat (+ T156 `/useskill`) |
| `registerSocialCommands` | 265 | 311 | 10 | T102 Social |
| `registerGuildCommands` | 314 | 348 | 7 | T132 Guild |
| `registerCorpseCommands` | 351 | 377 | 6 | T134 Corpse + Resurrection |
| `registerTradeskillCommands` | 380 | 425 | 7 | T144 + T149 + T278 |
| `registerUtilityCommands` | 428 | 455 | 6 | T135 Utility |
| `registerCommerceCommands` | 458 | 479 | 4 | T136 Merchant |
| `registerInventoryCommands` | 482 | 512 | 3 | T141 + T273 + T276 |
| `registerMovementAndUtilityCommands` | 515 | 608 | 20 | T140 final sixteen (+ T280) |
| `registerSpellAndPetCommands` | 611 | 661 | 8 | T101 Spells + T277 + T101 Pet |
| `registerExtendedMovementCommands` | 664 | 695 | 7 | `// Extended movement commands` |
| `registerEntityQueryCommands` | 698 | 707 | 2 | `// Entity query commands` |
| `registerExtendedCombatCommands` | 710 | 737 | 6 | `// Extended combat commands` (+ T268) |
| `registerPathfindingCommands` | 740 | 749 | 2 | `// Pathfinding and roleplay` |
| `registerEmoteShortcutCommands` | 752 | 769 | 4 | `// Emote shortcut commands` |
| `registerLoggingFilterCommands` | 772 | 789 | 4 | `// Logging filter commands` |
| `registerFindCommands` | 792 | 833 | 4 | `===== T150: Social / Find family =====` |
| `registerGMCommands` | 836 | 888 | 9 | T148 GM |

Sum of the registrations column: **142**, the whole vocabulary.

- **`registerBuiltinCommands`: 769 lines → 23** (signature + 21 calls + `}`).
- `command_processor_registration.cpp`: 792 → **891** lines (+99, all
  attributed below).
- `include/client/action/command_processor.h`: 602 → **636** (+34: 21
  declarations, a 13-line comment block explaining why the order is
  load-bearing and where it is pinned, and blank separators). Both files far
  under the ceiling (`check_file_sizes.sh`: ok).

**Deviation from the prediction (≈ 8–12 families, ≈ 10 declarations).** The
banners that are actually in the file number 21, and the contract says to use
the banners that are there rather than invent families, so the split is 21-wide.
Three cuts merged a one-registration banner into its neighbour rather than
emit a one-line function, and both directions of that choice preserve order:
T144 `/clickobject` heads `registerTradeskillCommands` (it is the world-object
click the tradeskill container arrives through), T280 `/trainclose` and the
T101 Pet block stay inside the families whose banner precedes them
(`registerMovementAndUtilityCommands`, `registerSpellAndPetCommands`), and the
T98 "filter registration removed" residue comment heads
`registerExtendedMovementCommands`. No registration crossed a cut.

### Proof (a): the line multiset

`git show fable:…` vs the new file, every line `strip()`ed, `Counter`
difference taken both ways:

```
REMOVED: 0 lines            <- nothing was dropped or reworded
ADDED:   99 lines
    21  }                       (one per new family)
    21  void CommandProcessor::registerXCommands() {
    21  // <family banner>       (one-line name for each new function)
    21  registerXCommands();     (the call list in registerBuiltinCommands)
    14  the G03 header comment on the file's existing banner
     1  blank separator
```

Residual outside those buckets: **0 lines, both directions**. Every one of the
768 original body lines is present, verbatim, exactly once.

### Proof (b): the order golden and the planted faults

**Why a text parse and not a runtime enumeration** — the finding that shaped
the test. `CommandProcessor::m_commands` is a
`std::map<std::string, RegisteredCommand>`, so `getAllCommands()` returns names
in **alphabetical** order and *cannot witness registration order at all*. What
registration order actually decides is shadowing: `registerCommand()` does
`m_commands[lowerName] = …` and `m_aliases[alias] = lowerName`, so the **last**
registration of a name or alias wins (J04; T097/T098 audited the live bugs this
caused). A runtime order test is therefore impossible against this registry
without instrumenting it — so `tests/test_command_registration_order.cpp` parses
the TU and composes the two things that now jointly define the order: the call
sequence inside `registerBuiltinCommands()`, and the registration sequence
inside each family it names.

`tests/goldens/command_registration_order_g03.txt`: **142 entries**, `<ordinal>
<name>` per line, extracted mechanically from
`git show fable:src/client/action/command_processor_registration.cpp` at
`e06237c7` — i.e. from the pre-change code, before a line of it was touched.

Four cases (registered in `tests/cmake/commands.cmake`; no link dependencies,
`WILLEQ_REPO_ROOT` is global from `tests/CMakeLists.txt`):

1. `GoldenIsPresentAndTheRightSize` — non-empty and **== 142**, in its own case
   (correction 5 / K11 ee: everything below is vacuous against a missing file).
2. `EveryFamilyIsCalledExactlyOnceAndNothingElse` — each call resolves to a
   family defined in the TU, none is called twice, no `register*Commands()`
   defined here is orphaned, and `registerBuiltinCommands()` registers nothing
   directly.
3. `RegistrationOrderMatchesThePreG03Golden` — the composed order equals the
   golden, position by position; the failure message names the four commands
   around the divergence in both lists.
4. `TheFamiliesPartitionTheGolden` — the per-family counts sum to 142, so a
   registration cannot be outside every family or inside two.

**Planted faults, all in the CODE, both directions (correction 6). Four
plants, four catches, source byte-identical afterwards (`diff` clean):**

| plant | outcome |
|---|---|
| **the contract's plant** — swap the two adjacent registrations either side of the `registerLoggingFilterCommands` / `registerFindCommands` cut (`logclear` ⇄ `groupassist`) | **FAILS**: `registration order diverges at position 129: golden: logclear, groupassist, track, find / actual: groupassist, logclear, track, find`. 1 failed, 3 passed |
| delete a real registration (`guilddemote`) | **FAILS** ×2: `registration count changed: 142 -> 141` and the partition case |
| add an invented one (`"invented"` in the Guild family) | **FAILS** ×2: `142 -> 143` and the partition case |
| reorder two calls in `registerBuiltinCommands` (`registerCommerceCommands` ⇄ `registerInventoryCommands`) — no registration edited at all | **FAILS**: `diverges at position 70: golden: vendor, vendorlist, buy, sell / actual: equip, read, scribe, vendor` |
| (the empty-file trap, for completeness) truncate the golden to 0 bytes | **FAILS** ×3 including case 1 — nothing passes vacuously |

### Text-reader check (correction 7)

`grep` for the TU filename **and** for `registerBuiltinCommands` across
`tests/`, `scripts/`, `tools/`, `docs/`: three hits, **none a text parse** —
`tests/cmake/commands.cmake` (four targets *compile* the TU; unaffected by a
split within it), `tests/test_merged_help.cpp` (two prose comments saying the
processor "registers ~112 real built-ins"; no assertion on the number or the
order), and `docs/module_map.md` (updated). **`test_reachability_sweep.cpp`
needed no widening**: it never names this TU or this method, and its two
`collect({"src","include"})` directory sweeps see the moved bodies in the same
TU as before. Confirmed by the sweep passing unchanged.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0**. First attempt hit the 600 s Bash ceiling (exit 143) and its kill corrupted `build/lib/libwilleq_core.a` — `ar t` listed **0** members and ~30 targets then failed to link with undefined references to core symbols. Deleted the archive and one zero-byte `ar` temp (`build/lib/stuJnCW0`), rebuilt: exit 0, `ar t` lists **321** members, **252** files in `build/bin`. Not a G03 defect — see the correction below |
| `ctest -j6 --output-on-failure` | **3741/3742 passed**; sole failure is the known host quirk `AudioManagerTest.PreloadCommonSounds`. 3742 tests (was 3738 — the 4 new order cases) |
| registry tests, assertions untouched | `-R 'MergedHelp|CommandProcessor|CommandRegistr|Appearance'` → **440/440 passed**. `test_merged_help`, `test_command_processor` (all five files), `test_appearance_command`, `test_command_output_routing` all green with not one assertion edited |
| `check_file_sizes.sh --all` | **checked 861 files: 861 ok, 0 warn, 0 fail** |
| hygiene re-run after the `docs/module_map.md` edit | `FileSizeCeiling` + `DocCitations` **2/2 passed** |
| line multiset | 0 removed, 99 added, every line attributed; residual **0** |
| planted faults | 4 plants in the code + the empty-golden trap, **5/5 caught**; source restored byte-identical |
| visual gate / ARM | **skipped with evidence**: `git diff --name-only` lists only `docs/module_map.md`, `include/client/action/command_processor.h`, `refactor/G03_register_builtin_commands.md`, `src/client/action/command_processor_registration.cpp`, `tests/cmake/commands.cmake`, `tests/goldens/command_registration_order_g03.txt`, `tests/test_command_registration_order.cpp`. **Zero files under `src/client/graphics`**, no shader, no asset |

### Corrections to the Batch G template

- **(8) An exit-143 on the build can leave `build/lib/libwilleq_core.a`
  TRUNCATED TO ZERO MEMBERS, and the next build then fails with a wall of
  `undefined reference` at link time that looks like a source defect and is
  not.** The protocol's post-143 rule ("assert the expected artifacts exist,
  delete zero-byte files, validate `build/bin` and `build/lib`) passed here on
  a *filename* basis — `libwilleq_core.a` existed and was non-empty — and still
  missed it. **Validate the archive's CONTENTS: `ar t build/lib/libwilleq_core.a
  | wc -l` must be in the hundreds, not 0.** The recovery is `rm` the archive
  and rebuild (cheap: it re-archives existing `.o` files); also sweep
  `build/lib` for a stray `ar` temp file with a random name.
- **(9) Before designing an "order" test, check what the container actually
  preserves.** G03's registry is a `std::map`, so no runtime enumeration can
  see registration order — the order is only observable as *shadowing*, after
  the fact. The order test therefore has to read the source. A unit that
  assumed `getAllCommands()` would witness the order would have shipped a test
  that passes against any permutation.
- **(10) When a method contains no locals, every cut is legal by
  construction — establish that first, mechanically, and the batch's main STOP
  condition is discharged in one command.** G03's body is 142 registrations and
  comments and nothing else; one `grep -v` of the five legal line shapes
  returned empty, and after that no cut needed individual justification.
- **(11) "Use the banners that are there" can outrun the predicted family
  count; follow the banners, not the prediction.** G03 predicted 8–12 and
  delivered 21. The rule that kept it honest: merge a one-registration banner
  into the neighbour whose banner precedes it rather than emit a one-line
  function, and never merge across a banner in the other direction.
- **(12) Plant the call-order fault too, not just the registration-order
  fault.** Splitting one list into families creates a *second* place order can
  regress — the call list — and swapping two calls edits no registration at
  all, so a test that only compares a per-family slice would miss it. G03's
  fourth plant covers it.
