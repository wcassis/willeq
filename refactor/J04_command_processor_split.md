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
