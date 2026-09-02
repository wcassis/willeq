# J04 — Batch J Wave 1: split command_processor.cpp (3,024 → 5 TUs, largest ~800)

**Slot: assigned at dispatch. Branch `task/J04-command-processor-split` cut
from `fable`. x86 only. No Pi hardware. No eqemu.**

Wave 1 of `refactor/J_file_size_reclamation.md`, running alongside **J02** and
**J03**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit** (six corrections made after the J01 pilot; it **wins over
anything older**), `refactor/J01_inventory_manager_split.md` (the proven
template, and its Review), `refactor/F_file_split.md`,
`refactor/T166_split_renderer_zone_load.md`, **`refactor/G02_commands_split.md`
§the `who` shadowing** (see Hazard 1) and `src/client/input/CLAUDE.md`.

This file crossed the 3,000-line hard cap after Batch F and G shipped. Three
sibling TUs already exist — `command_processor_bank.cpp` (158),
`command_processor_train.cpp` (43), `command_processor_loot.cpp` (131) — so the
naming convention is settled: **`command_processor_<domain>.cpp`.** Follow it.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to
`include/client/action/command_processor.h`. **No method decomposition** —
see Hazard 1, which is the whole reason this unit is safe. No drive-by fixes.

## ⚠ HAZARD 1 — `registerBuiltinCommands` MUST NOT BE SPLIT. This is the unit.

`registerBuiltinCommands()` (`:285-1053`, **769 lines**) is the single largest
method here and the obvious thing to carve into per-domain helpers
(`registerChatCommands()`, `registerGuildCommands()`, …). **Do not. Under any
circumstances, in this unit or opportunistically.**

`CommandProcessor::registerCommand` (`:99-112`) ends in

```cpp
m_commands[lowerName] = std::move(regCmd);
```

— a **map assignment, not an insert.** Last registration wins, silently. So
**the textual order of the ~250 `registerCommand({...})` calls inside that one
method is semantically load-bearing**, and it is load-bearing in a way nothing
in the build or the type system will tell you about. `refactor/G02_commands_split.md`
records the live instance: **`who` is registered twice, and only the second
registration is ever reachable — the first is silently shadowed.** Carving the
body into helpers means choosing a call order for those helpers, and any
reordering flips which duplicate wins, changing behaviour with a clean compile,
a clean link, and possibly a clean test run.

**The split below is safe precisely because `registerBuiltinCommands` travels
whole, as one contiguous block, into one TU.** A J04 that decomposes it has
failed its acceptance, not exceeded it. If you believe the duplicate
registrations should be cleaned up, that is a separate unit and a separate
finding — put it in your Review, do not act on it.

## The split

Measured at `2eb34f3d`, re-confirmed byte-for-byte at `7e0c7823` after J01 merged (2026-09-02). **Re-verify every range against the tree
at your own branch tip before you cut** — J02 and J03 are landing in parallel
and this file attracts new commands constantly. Report drift in your Review
rather than silently adjusting.

Preamble is **lines 1-18**: 14 includes, a blank, `namespace eqt {`,
`namespace action {`, a blank. The close is `} // namespace action` /
`} // namespace eqt` at 3023-3024. Both replicate into every new TU. Ranges
start at the **first line of the cluster's leading `// ==========` banner**,
per §Process.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 19-282 | **stays.** ctor/dtor, `setOutputRenderer`, `setMessageSink`, `processCommand`, `processInput`, `registerCommand`, `unregisterCommand`, `hasCommand`, `getCommandInfo`, `getAllCommands`, `getCommandsByCategory`, `getCategories`, `getCompletions`, `getAllCommandNames`, and the `// ===== Helper Methods =====` block (`toLower`, `parseCommandLine`, `splitArgs`, `emitMessage`, `displayMessage`, `displayError`) | **stays** `command_processor.cpp` | ~300 |
| 283-1053 | `// ===== Register Built-in Commands =====` + `registerBuiltinCommands` — **ONE method, 769 lines, indivisible (Hazard 1)** | `command_processor_registration.cpp` | **~800** |
| 1054-1831 | `// ===== Command Implementations =====` `cmdShout`; chat (T100); group (T99); combat + social (T102) **including the `normalizeSkillName`/`findSkillIdByName` anonymous namespace at 1516-1545**; trade; `cmdWho`; guild (T132) | `command_processor_social.cpp` | ~800 |
| 1832-2333 | corpse / resurrection (T134); `cmdClickObject`; **the anonymous namespace at 1881-1929 — see Hazard 2**; tradeskill recipes; `cmdCombine`; utility (T135); merchant (T136) **including the `ParseMerchantSlot`/`ParseMerchantQuantity` anonymous namespace at 2125-2151**; movement / utility / skills (T140) | `command_processor_world.cpp` | ~525 |
| 2334-3022 | spells (T101); pet (T101); extended implementations (walk/run/sneak/crouch/feign/jump/turn, list/dump/hunt/autoloot/pathfinding/roleplay, the emotes, the `cmdLog*` family); social / find (T150); **GM commands (T148) through `cmdAppearance`** | `command_processor_spells_gm.cpp` | ~710 |

**Result: 5 TUs** — `command_processor_registration.cpp` ≈ 800,
`command_processor_social.cpp` ≈ 800, `command_processor_spells_gm.cpp` ≈ 710,
`command_processor_world.cpp` ≈ 525, `command_processor.cpp` ≈ 300. Every one
under 1,500.

## ⚠ HAZARD 2 — one anonymous namespace is used across a cluster boundary

There are **three** anonymous namespaces in this file. Two are self-contained
and travel with their cluster for free:

- `1516-1545` — `normalizeSkillName`, `findSkillIdByName`. Sole caller
  `cmdUseSkill` at `:1578`. Both inside the 1054-1831 cluster. **Fine.**
- `2125-2151` — `ParseMerchantSlot`, `ParseMerchantQuantity`. Callers at
  `:2163`, `:2167`, `:2204`, `:2208` (`cmdBuy`, `cmdSell`). All inside the
  1832-2333 cluster. **Fine.**

The third is not:

- `1881-1929` — `parseU32Strict`, `splitWhitespace`, and the two `constexpr`s
  `kDefaultMinTrivial` / `kDefaultMaxTrivial`. The constants are used only at
  `:1932-1933` (`cmdRecipeSearch`), but **`parseU32Strict` and
  `splitWhitespace` are also called at `:2981` and `:2990`, inside
  `cmdAppearance`** — which this split sends to
  `command_processor_spells_gm.cpp`. A naive range cut breaks the build.

**Resolution — the T166 sanctioned Batch-F exception** (§Process step 4). Split
that one block:

1. Move `parseU32Strict` and `splitWhitespace` **verbatim** into a new private
   internal header `src/client/action/command_processor_internal.h`, inside
   `namespace eqt { namespace action { namespace detail {`, marked `inline`
   (they lose internal linkage, so `inline` is what keeps them ODR-safe across
   the two TUs that include it). Header guard `#pragma once`.
2. Leave `kDefaultMinTrivial` / `kDefaultMaxTrivial` in an anonymous namespace
   in `command_processor_world.cpp` beside `cmdRecipeSearch`.
3. Include the new header from `command_processor_world.cpp` and
   `command_processor_spells_gm.cpp`, and add
   `using detail::parseU32Strict; using detail::splitWhitespace;` at namespace
   scope in each so the eight call sites are untouched. **Do not rewrite the
   call sites** — that would be an edit, not a move.
4. **Declare this in your Review as the sanctioned exception**, naming both
   functions and both consuming TUs, exactly as T166 did for `wld_internal.h`.

**Do not solve this by duplicating the two functions into both TUs** (an ODR
violation waiting to diverge) **and do not solve it by re-routing
`cmdAppearance`** into the world TU to keep ranges contiguous — that would put
a GM command in the merchant file and make the feature unfindable.

## ⚠ HAZARD 3 — one comment goes stale, and fixing it is allowed

`:1279-1281`, inside `cmdRaid`, reads:

> Parsed inline rather than via `splitWhitespace()` (an anonymous-namespace
> helper defined later in this TU, not visible here).

After the split `cmdRaid` is in `command_processor_social.cpp` and
`splitWhitespace` is in the internal header, so "later in this TU" is wrong.
`verify_move_only.py` allowlists comment lines as boilerplate, so **correcting
this comment is permitted and expected.** Note it explicitly in your Review as
the unit's only non-move change, and keep it to that one comment.

## Includes

**Each new TU carries `command_processor.cpp`'s full 14-include block,
verbatim**, plus the `namespace eqt { namespace action {` open and the matching
close. Not "the includes it needs" — see §Process. The two TUs from Hazard 2
additionally include `"client/action/command_processor_internal.h"`. Do not
create a shared "everything" header beyond that one narrowly-scoped internal
header.

## CMake — ⚠ FIVE add-sites, four of them in `tests/CMakeLists.txt`

**This is where J04 differs sharply from the J01 pilot, which needed no test
CMake change at all.** `command_processor.cpp` is compiled directly into
several test targets, not linked via `willeq_core`. Find the sites yourself:

```bash
grep -n 'action/command_processor.*\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `7e0c7823` that is `CMakeLists.txt:365` plus **four** per-target source
lists in `tests/CMakeLists.txt` (around `:402`, `:424`, `:440`, `:568`). In
every one of the five, `command_processor.cpp` is immediately followed by
`command_processor_bank.cpp` / `_train.cpp` / `_loot.cpp` — **add the four new
TUs to that same group at every site.** Missing one produces undefined-symbol
link errors in a single test target, which is easy to misread as a bad split.
Do not trust these line numbers; grep.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- build clean; `cmake --build build -j24` classified by **EXIT CODE**, never by
  grepping the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`. **The five
  `test_command_processor*` targets are this unit's real net** — confirm all
  five built and ran, not merely that the suite total held.
- ⚠ `BASE=$(git merge-base fable HEAD)` then
  `python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"` **clean** — the
  unit's central proof. **NOT `fable..HEAD`:** three units are merging into
  `fable` during this wave and `fable..HEAD` will report the other two units'
  work, in reverse, as a very loud spurious FAIL. Record `$BASE` and paste the
  output in the Review.
- ⚠ the **line-multiset check** (§Process): the five TUs' bodies plus
  `command_processor_internal.h`'s two moved functions form a multiset
  identical to the original's lines 19-3022 (allowing the one corrected comment
  of Hazard 3 and the `inline` / `using` boilerplate of Hazard 2 — enumerate
  every such line explicitly in the Review); and the extracted ranges
  re-concatenated in original order are byte-identical to
  `git show "$BASE:src/client/action/command_processor.cpp" | sed -n '19,3022p'`
  modulo those same enumerated lines. Paste both results.
- **an explicit registration-order proof.** Beyond the suite: dump the
  registered command table before and after and diff it, e.g. add nothing to
  the tree but run the existing `test_command_processor` target and confirm
  `getAllCommandNames()`-based assertions pass, and state in the Review that
  `registerBuiltinCommands` moved as **one contiguous block with zero internal
  reordering** — quote the `git diff -M` similarity for that hunk.
- visual gate PASS with **goldens untouched**. A re-bless would mean the split
  changed behaviour — **STOP and report** rather than re-blessing.
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt`'s pre-existing 1,899-line WARN
  is not yours (§Process).
- ⚠ `chmod 644` every created file before `git add`.

## Review must answer

1. Confirmation, in as many words, that **`registerBuiltinCommands` was moved
   whole with no internal reordering** and no per-domain helpers were created.
2. The Hazard-2 internal header, declared as the sanctioned Batch-F exception:
   which symbols, which consuming TUs, and why duplication was rejected.
3. Every non-move line you touched (Hazard 3's comment, the `inline`/`using`
   boilerplate) enumerated, so the multiset check's exceptions are auditable.
4. How many CMake add-sites you actually found, in each file.
5. Line-range drift vs this plan, final line counts of all five TUs, the
   verifier base rev, wall clock and slowest step, and whether concurrency with
   J02/J03 stretched the verification tail.
6. Any further duplicate `registerCommand` registrations you noticed while
   moving the block — **as a finding only**, not as a fix. G02 found `who`;
   nobody has re-checked the other ~250 since.

## Territory

`src/client/action/command_processor.cpp`, the four new
`src/client/action/command_processor_*.cpp` TUs and
`src/client/action/command_processor_internal.h` beside them,
`CMakeLists.txt`, `tests/CMakeLists.txt`, this file. **Nothing else** — not
`include/client/action/command_processor.h`, not `action_dispatcher.*`, not the
three existing `command_processor_{bank,train,loot}.cpp` siblings, not any
`tests/test_command_processor*.cpp`, not `docs/module_map.md`.

Concurrent at dispatch: **J02** (`eq.cpp` + new `eq_*.cpp`, `CMakeLists.txt`),
**J03** (`graphics/entity_renderer.cpp` + new `entity_*.cpp`,
`CMakeLists.txt`; **itself gated on T318**), **T318** (slot3:
`graphics/entity_renderer.*`, `eq_entities.cpp`, `eq_guild.cpp`,
`spell_effects.cpp`, `event_bus.h`, **and `tests/CMakeLists.txt`**). No source
overlap with any of them. ⚠ **`tests/CMakeLists.txt` is shared with T318** and
`CMakeLists.txt` with J02/J03 — which is why merges are serialized one at a
time with a rebase.

---

## Review (slot2, 2026-09-02, branch `task/J04-command-processor-split`)

**Done exactly as specified. Move-only. Zero line-range drift — every boundary
in the cluster table was byte-correct at my tip.**

Branch cut from `fable` at **`091a13fd`** ("Merge branch 'task/T319' into
fable"), not the `7e0c7823` the plan measured at. `git diff 7e0c7823 HEAD --
src/client/action/command_processor.cpp` is **empty**: the file did not move
between those tips, so the plan's ranges applied verbatim.

### Result — final line counts

| file | lines | original range |
|---|---|---|
| `command_processor.cpp` | **284** | 19-282 (ctor..`displayError`) |
| `command_processor_registration.cpp` | **791** | 283-1053 (`registerBuiltinCommands`) |
| `command_processor_social.cpp` | **798** | 1054-1831 |
| `command_processor_world.cpp` | **490** | 1832-1882 + 1919-2333 |
| `command_processor_spells_gm.cpp` | **713** | 2334-3022 |
| `command_processor_internal.h` | **60** | 1883-1918 (the two shared helpers) |

3,024 → 3,136 across five TUs + one header. The +112 is four extra copies of
the 18-line preamble, five namespace closes, the header's own 20 lines of
guard/banner/includes/namespaces, and the six Hazard-2 lines.
`CMakeLists.txt` 1,900→1,904, `tests/CMakeLists.txt` 2,762→2,778.

### 1. `registerBuiltinCommands` moved WHOLE — proven, not asserted

**No per-domain helpers were created. The method was not decomposed. There was
zero internal reordering.** Proven directly rather than by inspection:

```
$ git show 091a13fd:src/client/action/command_processor.cpp | sed -n '283,1053p' > reg_orig.txt
$ sed -n '19,789p' src/client/action/command_processor_registration.cpp    > reg_new.txt
$ diff reg_orig.txt reg_new.txt ; echo $?
0
```

The banner comment, the 771-line method, and all **142** `registerCommand({…})`
calls are byte-identical and in their original textual order. (`git diff -M`
does not report a rename here, as expected: the original file still exists with
its first 282 lines, so all four new TUs show as `create mode 100644`.)

### 2. Hazard 2 — the sanctioned Batch-F exception (T166 `wld_internal.h` precedent)

**Symbols:** `detail::parseU32Strict(const std::string&, uint32_t&)` and
`detail::splitWhitespace(const std::string&)`, moved verbatim (bodies
unchanged, only the `inline` keyword added to each signature) out of the
anonymous namespace at `:1881-1929` into the new private header
**`src/client/action/command_processor_internal.h`**, inside
`namespace eqt { namespace action { namespace detail {`, `#pragma once`.

**Consuming TUs (both, and only these two):**
- `command_processor_world.cpp` — **10** call sites (orig. `:1956, 1978, 1979,
  1987, 1988, 1995, 2003, 2018, 2023, 2035`, across the recipe family and
  `cmdCombine`)
- `command_processor_spells_gm.cpp` — **2** call sites, both inside
  `cmdAppearance` (orig. `:2981`, `:2990`)

Each includes the header and carries `using detail::parseU32Strict;` /
`using detail::splitWhitespace;` at `eqt::action` scope, so **all twelve call
sites are textually untouched** — no call site was rewritten. (The plan's Hazard-2 text says "the eight call
sites"; the actual count at this tip is **twelve** — 10 + 2. Counted, not
taken on trust.)

`kDefaultMinTrivial` / `kDefaultMaxTrivial` stayed in an anonymous namespace in
`command_processor_world.cpp` beside `cmdRecipeSearch`, their only user, as the
plan directs. The surviving anonymous block in that TU is the original
`namespace {` … `} // namespace` with the two functions lifted out.

**Why duplication was rejected:** two independent definitions of a parser with
overflow/trailing-junk semantics is an ODR violation that the compiler will not
diagnose (internal linkage in each TU makes it legal-but-divergent), and the
two copies would drift the first time someone fixes a parse bug in one command
family. **Why re-routing `cmdAppearance` was rejected:** it would file a GM
appearance command inside the merchant/tradeskill TU purely to keep a line
range contiguous, making the feature unfindable — the plan's own reasoning, and
correct.

The other two anonymous namespaces were confirmed self-contained and travelled
free: `:1516-1545` (`normalizeSkillName`/`findSkillIdByName`, sole caller
`cmdUseSkill` at `:1578`) inside the social cluster, and `:2125-2151`
(`ParseMerchantSlot`/`ParseMerchantQuantity`, callers at `:2163/2167/2204/2208`)
inside the world cluster.

### 3. Every non-move line, enumerated

Exactly **six added, two removed** substantive lines. Nothing else.

**Hazard 3 (the one permitted comment correction), in `command_processor_social.cpp`:**
```
-    // via splitWhitespace() (an anonymous-namespace helper defined later in this
-    // TU, not visible here).
+    // via splitWhitespace() (a helper in command_processor_internal.h, which
+    // this TU does not include).
```
(Comment lines; the verifier allowlists them as boilerplate, so they do not
appear in its substantive counts, but they do appear in the multiset check.)

**Hazard 2 boilerplate — `inline` (2 added / 2 removed), in `command_processor_internal.h`:**
```
- bool parseU32Strict(const std::string& text, uint32_t& out) {
+ inline bool parseU32Strict(const std::string& text, uint32_t& out) {
- std::vector<std::string> splitWhitespace(const std::string& text) {
+ inline std::vector<std::string> splitWhitespace(const std::string& text) {
```

**Hazard 2 boilerplate — `using` (4 added), 2× in each of `command_processor_world.cpp`
and `command_processor_spells_gm.cpp`:**
```
+ using detail::parseU32Strict;
+ using detail::splitWhitespace;
```

Plus pure-boilerplate, allowlisted: 4 extra copies of the 14-include block, one
`#include "client/action/command_processor_internal.h"` in each of the two
consuming TUs, the header's `#pragma once` / 5 std includes / 3 namespace
opens + closes, and 4 extra `namespace eqt {` / `namespace action {` /
`} // namespace …` pairs.

### PROOF 1 — byte-identical re-concatenation (the strong proof)

The five TU bodies, with the header's 36 moved lines re-inserted at their
original offset inside the world TU's anonymous namespace and the two `inline`
keywords and Hazard-3 comment reverted, concatenated in original cluster order:

```
PROOF 1 (byte-identical re-concatenation, original order):
  original body lines 19-3022 : 3004
  reconstructed               : 3004
  BYTE-IDENTICAL: YES
```

Every one of the 3,004 body lines is accounted for **exactly once**, in order,
with no gaps and no overlap.

### PROOF 2 — line multiset (the check the verifier cannot do)

All six files' bodies + the two `using` pairs, normalised (stripped, blanks
dropped), as multisets against the pre-split body:

```
PROOF 2 (line multiset, non-blank normalised):
  before distinct/total: 1751 2594
  after  distinct/total: 1753 2598
  ADDED  : 8
    +1x  // this TU does not include).
    +1x  // via splitWhitespace() (a helper in command_processor_internal.h, which
    +1x  inline bool parseU32Strict(const std::string& text, uint32_t& out) {
    +1x  inline std::vector<std::string> splitWhitespace(const std::string& text) {
    +2x  using detail::parseU32Strict;
    +2x  using detail::splitWhitespace;
  REMOVED: 4
    -1x  // TU, not visible here).
    -1x  // via splitWhitespace() (an anonymous-namespace helper defined later in this
    -1x  bool parseU32Strict(const std::string& text, uint32_t& out) {
    -1x  std::vector<std::string> splitWhitespace(const std::string& text) {
```

The delta is **exactly** the eight lines enumerated in §3 and nothing else.
Because this is a per-*occurrence* count (not per-distinct-line), a range
duplicated into two TUs or dropped from both would show up here; none did.

### `verify_move_only.py` — ⚠ NOT clean, and it CANNOT be under this plan

**Base rev: `BASE=091a13fdd27c6e153814fdd7c69e78863fb72272`** (`git merge-base
fable HEAD`). Used the merge-base as instructed; no spurious foreign-file noise
appeared, so the J01 template correction did its job.

```
$ BASE=$(git merge-base fable HEAD)
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  tests/CMakeLists.txt

Files verified: 6
Boilerplate: +95 / -2 lines (allowed)

SUBSTANTIVE ADDED lines (6):
  +1x  inline bool parseU32Strict(const std::string& text, uint32_t& out) {
  +1x  inline std::vector<std::string> splitWhitespace(const std::string& text) {
  +2x  using detail::parseU32Strict;
  +2x  using detail::splitWhitespace;

SUBSTANTIVE REMOVED lines (2):
  -1x  bool parseU32Strict(const std::string& text, uint32_t& out) {
  -1x  std::vector<std::string> splitWhitespace(const std::string& text) {

FAIL: substantive line changes detected. If intentional (e.g. de-static-ing a
helper), document in the unit review section for orchestrator sign-off.
```

**This is not a failure of the split — it is the plan's Hazard-2 resolution
being reported, and the two requirements are mutually exclusive.** The
acceptance bar says "verifier clean"; Hazard 2 mandates adding `inline` to two
signatures and four `using` declarations. `BOILERPLATE` in
`verify_move_only.py` allowlists `#include`, `#pragma`, `namespace X {`, `}`
and comments — it does **not** allowlist `inline` on a moved signature or a
`using`-declaration, so no faithful execution of Hazard 2 can exit 0. The
verifier's own failure text names exactly this case ("If intentional, e.g.
de-static-ing a helper, document in the unit review section for orchestrator
sign-off"), which is the T166 sanctioned-exception path. **Submitted for
orchestrator sign-off on those 6+2 lines; there is nothing else in the diff.**

I deliberately did not contort the code to force a green exit (e.g. putting
`inline` on its own line, or hoisting the helpers into `eqt::action` to drop
the `using`s) — that would trade an auditable, enumerated exception for a
verifier-shaped hack.

### Acceptance

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0**, clean first try |
| `ctest -j6` (full) | **3477/3478**; sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk in `test_baseline.txt`. **Zero new failures.** 169.4 s |
| `verify_move_only.py 091a13fd..HEAD` | **6 added / 2 removed**, all Hazard-2, enumerated above — sign-off requested |
| line-multiset check | **+8 / −4, exactly the enumerated lines** |
| byte-identical re-concatenation | **YES** (3,004 lines) |
| `registerBuiltinCommands` block `diff` | **exit 0, byte-identical** |
| visual gate | **PASS** (ctest #3475, 28.38 s). `git status --porcelain` **empty** — goldens untouched, not re-blessed |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/build-arm-noble.sh` | **EXIT=0** (see note below) |
| `check_file_sizes.sh --files` (6 sources) | **6 ok, 0 warn, 0 fail** |
| `chmod 644` on created files | verified via `git ls-files -s`: all `100644` |

`check_file_sizes.sh` also WARNs on `CMakeLists.txt` (1,904) and
`tests/CMakeLists.txt` (2,778). **Both pre-existing** — 1,900 and 2,762 before
this unit. `tests/CMakeLists.txt` is a *second* build-system file over the
soft limit and it is closing on the 3,000 hard cap; §Process only names
`CMakeLists.txt`. Not this unit's to fix (see finding 3 below).

### 4. CMake add-sites actually found

**Five, exactly as the plan predicted, and at the exact predicted line
numbers** (`CMakeLists.txt:365`; `tests/CMakeLists.txt:402, 424, 440, 568`) —
the one time this batch's "line numbers rot within hours" warning did not bite,
because `fable` had not moved this file since the survey. I grepped anyway.

- `CMakeLists.txt` — **1** site, `CORE_SOURCES`.
- `tests/CMakeLists.txt` — **4** sites, all per-target explicit source lists:
  `test_command_processor`, `test_appearance_command`, `test_merged_help`,
  `test_input_action_bridge`.

⚠ **Terminology correction for the batch:** the plan calls these "the five
`test_command_processor*` targets". There is **one** target named
`test_command_processor` (whose five *test .cpp files* T165 split); the other
three test targets have unrelated names. All four built and ran:
`CommandProcessorTest*` **383** cases, `AppearanceCommand*` **7**,
`MergedHelp*` **21**, `InputActionBridge*` **40**. The registration-order net is
`test_command_processor`'s 383 cases, which include `getAllCommandNames()`- and
category-consistency assertions and all pass.

### 5. Drift, wall clock, concurrency

**Line-range drift vs the plan: NONE.** All ten cluster boundaries and all
three anonymous-namespace ranges were exact. The only refinement was splitting
the plan's `1832-2333` world range into `1832-1882` + `1919-2333` so the
header's `1883-1918` comes out of the middle of it, leaving the surviving
`namespace { … }` wrapper and the two `constexpr`s in place.

**Wall clock ~36 min** (05:46 → 06:23 UTC), single agent, no retries on
anything except the ARM build.

| step | approx |
|---|---|
| reading contracts + surveying | 9 min |
| the cut, header, CMake, hazard-3 fix | **~3 min** (one scripted pass) |
| `cmake --build -j24` | 3 min |
| both proofs + registration diff + duplicate scan | 2 min |
| `ctest -j6` | 2.8 min |
| `build-gles2-x86.sh` | 3 min |
| **`build-arm-noble.sh`** | **>10 min — the slowest step by far** |

⚠ **`build-arm-noble.sh` exceeded the Bash tool's 600 s ceiling and was
SIGTERM'd at 10:00 (exit 143), mid-`gles2_zone_benchmark`.** Re-invoked per
protocol; the incremental re-run finished **EXIT=0**. J01 measured this step at
~5 min running alone. **Concurrency with J02/J03 roughly doubled it and pushed
it past a single tool invocation** — this is exactly the stretched verification
tail §Scheduling predicted, and it means **every Wave-1+ unit should expect to
call `build-arm-noble.sh` twice.** Worth saying in §Process so an executor does
not read exit 143 as breakage. Everything else was ~1.5-2× J01's solo timings.

### 6. Findings (NOT acted on)

1. **No duplicate `registerCommand` registrations exist in
   `CommandProcessor::registerBuiltinCommands`.** I checked all 142 — parsed
   the primary name and alias list out of every call:
   - duplicate primary names: **none**
   - aliases claimed by more than one command: **none**
   - an alias colliding with another command's primary name: **none**

   So Hazard 1's *mechanism* is real (`m_commands[lowerName] = …` is an
   assignment, and `m_aliases[…] = …` likewise) but it currently has **no live
   victim in this method**. **G02's shadowed `who` is in a different registry:**
   `graphics/ui/command_registry.cpp`'s `CommandRegistry`, fed from
   `eq_commands_*.cpp` — a separate system this unit never touched. The hazard
   was still worth honouring: order is one careless helper-extraction away from
   mattering, and nothing would have told us.
2. **`registerBuiltinCommands` is 771 lines and now owns a 791-line TU with
   ~700 lines of headroom before the 1,500 WARN.** It grows every time a
   command is added. It is on §Optional's Batch-G list; flagging that
   `command_processor_registration.cpp` is a single-method TU that can only be
   reduced by decomposing that method, i.e. by taking the ordering decision
   deliberately.
3. **`tests/CMakeLists.txt` is 2,778 lines** — over the soft limit and within
   222 lines of the 3,000 hard cap, and *every* J unit adds to it. §Process
   grandfathers `CMakeLists.txt` (1,904) by name but not this one. **The
   orchestrator should board a `tests/CMakeLists.txt` split before a later J
   unit trips a genuine FAIL there through no fault of its own.**

### 7. What the template still gets wrong

1. ⚠ **"verifier clean" is unsatisfiable for any unit that needs an internal
   header.** §Process step 4 mandates the internal-header exception *and* the
   acceptance bar demands a clean verifier; `inline` and `using` are neither.
   **J05 will hit this identically and harder** (`wld_internal.h`,
   `read_val<T>` used 114× — likely `template`/`inline` on several signatures
   plus a `using` per consuming TU). Either (a) re-word the bar as "verifier
   clean **except** lines enumerated in the Review under the §Process step-4
   exception", or (b) extend `BOILERPLATE` in `verify_move_only.py` with
   `^inline\b` and `^using\b`. **(a) is better** — the point of the check is
   that a human reads the exceptions, and there were exactly eight here.
2. **The ARM build no longer fits one tool invocation under wave concurrency.**
   Say so, and say exit 143 means re-invoke, not fail. (Protocol §Hard rules
   mentions mid-link truncation; it does not warn that this specific step is
   now routinely over the ceiling.)
3. **Say "test targets", not "`test_command_processor*` targets".** Three of
   the four have unrelated names; an executor looking for five `test_command_*`
   binaries will conclude the build is broken.
4. **The multiset check needs a stated convention for a cross-TU header.** Mine
   counted the header's 36 moved lines and the `using` lines explicitly; a
   naive "cat the .cpp bodies" would silently under-count by 36 and *still*
   diff clean-looking. Spell out that the internal header's moved lines and
   every `using` line join the `after` multiset.
5. Otherwise the template held up perfectly. The merge-base fix worked (no
   foreign-file noise despite two units merging alongside), the
   banners-travel-with-the-cluster rule made every boundary unambiguous, the
   full-include-block rule meant zero include archaeology, and `chmod 644` was
   needed — the scripted emit produced 644 only because I chmod'd explicitly.

### Not done (out of scope, deliberately)

No method decomposition. No cleanup of the (nonexistent) duplicate
registrations. `docs/module_map.md` and `src/client/input/CLAUDE.md` both
describe `command_processor.cpp` and now need entries for the four new TUs —
outside this unit's territory, flagged for the orchestrator.
