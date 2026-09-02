# J01 — Batch J pilot: split inventory_manager.cpp (2,502 → 4 TUs, largest 795)

**Slot: assigned at dispatch. Branch `task/J01-inventory-split` cut from
`fable`. x86 only. No Pi. No eqemu.**

**This is the PILOT for Batch J** (`refactor/J_file_size_reclamation.md`) — it
proves the template on this tree before 17 more units fan out across three
slots. Chosen for that role deliberately: zero file-local statics, two already
independent classes, and a subsystem with real unit-test coverage. **If the
template is wrong, it is cheaper to find out here.** Read
`refactor/F_file_split.md` (process), `refactor/T166_split_renderer_zone_load.md`
(the most recent precedent, and the closest template) and `docs/module_map.md`
before editing anything.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No header changes beyond what the split
mechanically requires (it should require none — `InventoryManager`'s and
`TitaniumItemParser`'s declarations stay exactly where they are). **No method
decomposition** — that is Batch G's shape and is explicitly out of scope for
every Batch J unit, including this one.

## The split

Preamble is 12 lines (`src/client/inventory/inventory_manager.cpp:1-20`);
duplicate the includes each new TU actually needs, as T166 did — do not
create a shared "everything" header.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 21-500 | **`TitaniumItemParser::`** — `toInt`, `toFloat`, `splitByPipe`, both `parseItem` overloads, `parseInstanceData`, `parseStaticData` (224), `parseSubItems` | `inventory_item_parser.cpp` | 490 |
| 509-793 | slot storage: get/set/remove/clear, `cacheItemById`, `findItemSlotByItemId`, trade/bank/world-container clears | **stays** `inventory_manager.cpp` | — |
| 795-1575 | cursor: `pickupItem`, `placeItem` (124), partial stacks, cursor queue/money, scribe slot, `swapItems`, `canPlaceItemInSlot`, `canEquipItem`, equip destination, `moveItem` (184) | `inventory_cursor.cpp` | 795 |
| 1577-1971 | `canPlaceInBag`, `canPlaceInWorldContainer`, restrictions, `hasLoreConflict`, the bag family, the bank-bag family | `inventory_bags.cpp` | 410 |
| 1973-2241 | bank / shared-bank / equipment / general / sellable queries, `calculateEquipmentStats`, `calculateTotalWeight`, `isValidSlot`, `destroyItem`, `destroyCursorItem` | **stays** | — |
| 2243-2499 | `processCharInventory`, `processItemPacket`, `ingestParsedItem`, `processMoveItemResponse`, `processDeleteItemResponse`, `dumpInventory` | `inventory_packets.cpp` | 270 |

**Result: 4 TUs** — `inventory_manager.cpp` ≈ 550, `inventory_cursor.cpp` ≈ 795,
`inventory_item_parser.cpp` ≈ 490, `inventory_bags.cpp` ≈ 410,
`inventory_packets.cpp` ≈ 270. Every one under 1,500 with room to grow.

**Verify the line ranges yourself against the tree at your branch tip before
you cut** — they were measured at `9ae1460c` and this file must not be trusted
over the code. Report any drift in your Review rather than silently adjusting.

## Hazards

**None found by the survey** — no file-local statics, no anonymous namespaces,
no order-dependent code. That is why it is the pilot. **If you find one, that
is the single most valuable thing this unit can report**: it means the survey's
hazard analysis is unreliable and the other 17 units need re-checking before
dispatch. Say so loudly rather than working around it quietly.

`CMakeLists.txt` has explicit source lists (no globs) at `:266` and `:456` —
add all four new TUs.

## Acceptance (the Batch J bar — every item, reported by exit code)

- build clean; `cmake --build` classified by **EXIT CODE**, never by grepping
  the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` **clean** — this is
  the unit's central proof; paste its output in the Review
- visual gate PASS with **goldens untouched**. A re-bless would mean the split
  changed behaviour — **STOP and report** rather than re-blessing
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**

## Review must answer, for the batch's sake

1. Did `verify_move_only.py` come back clean on the first try? If not, what did
   it flag and was it a real behaviour change?
2. How long did the unit take, wall-clock, and what was the slowest step? Waves
   1-3 are three units in parallel; the schedule depends on this number.
3. Anything about the template that should change before J02-J04 are written.

## Territory

`src/client/inventory/`, `CMakeLists.txt`, this file. Nothing else.
Concurrent units at dispatch time: T311 (no `src/` changes), T312
(`irrlicht_renderer.*`, `renderer_frame.cpp`, `graphics_input_handler.cpp`,
`renderer_player_physics.cpp`), T313 (`graphics/ui/`). No overlap with any.

---

## Review (slot2, 2026-09-02, branch `task/J01-inventory-split`, commit `0166fae0`)

**Done as specified. Move-only, no hazards found, no deviations from the
cluster plan.** Base tip verified `8bac4c6f` before cutting.

### Result

| file | lines | contents |
|---|---|---|
| `inventory_item_parser.cpp` | **503** | all of `TitaniumItemParser::` (orig. 16-500) |
| `inventory_manager.cpp` | **581** | slot storage (orig. 501-793) + query/stat/destroy (orig. 1972-2241) |
| `inventory_cursor.cpp` | **800** | cursor/pickup/place/swap/equip/`moveItem` (orig. 794-1575) |
| `inventory_bags.cpp` | **414** | bag / world-container / bank-bag family (orig. 1576-1971) |
| `inventory_packets.cpp` | **276** | packet ingest + `dumpInventory` (orig. 2242-2499) |

2,502 -> 2,574 across five TUs; the +72 is four extra copies of the 15-line
preamble (12 includes, blank, two `namespace` opens) and the three-line
namespace close. Original body lines 16-2499 are partitioned **exactly once
each** with no gaps and no overlap.

### Line-range drift vs the plan

Two discrepancies, both cosmetic; the cluster boundaries themselves were
**exactly right** at `8bac4c6f`.

1. **Section banners.** The plan's ranges start at the first *statement*
   (21, 795, 1577, 2243). Each cluster's `// ====` banner comment sits a few
   lines earlier. I extended each range backwards to take the banner with its
   code (16, 794, 1576, 2242) so no TU is left with an orphaned header
   comment. Same code, tidier files.
2. **`CMakeLists.txt` has ONE add-site, not two.** The plan says "explicit
   source lists at `:266` and `:456` — add all four new TUs". At this tip
   `inventory_manager.cpp` appears **exactly once**, at `CMakeLists.txt:319`
   in `CORE_SOURCES` (where P28 declassified it out of `GRAPHICS_SOURCES`).
   `:456` is inside `GRAPHICS_SOURCES`, which does not and should not list
   inventory. The four TUs were added at the single site. `tests/CMakeLists.txt`
   needs no change either — every inventory test links `willeq_core`, not a
   per-target source list. **J02-J04 must not blindly add to two sites.**

### Hazards: NONE — the survey was right

Re-checked directly, not taken on trust: zero anonymous namespaces, zero
file-local `static`s, zero file-scope objects with construction order to
preserve. The only `constexpr`s in the file (`kOpcodeBytes`, `kTypeBytes`,
orig. 2299-2300) are **function-local** to `processItemPacket` and travelled
with it into `inventory_packets.cpp`. Nothing had to be de-static-ed, no
shared private header was created, and no header was touched.

### Includes

Each new TU carries the **original's full 12-include block** verbatim, the
T166 precedent. Trimming per-TU include sets is a second, riskier change
wearing a move-only commit's clothes, and the compile cost of four more TUs
over this small header set does not justify the risk.

### Independent content proof (beyond the verifier)

The five files' bodies (each stripped of its 15-line preamble and 3-line
namespace close) form a **line-multiset identical** to the original's lines
16-2499 — 2,484 non-blank lines, `diff` of the sorted sets exits 0. And the
six extracted ranges re-concatenated in original order are **byte-identical**
to `sed -n '16,2499p'` of the pre-split file.

### Acceptance

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0**, clean first try |
| `ctest -j6` (full) | **3408/3409**; sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk in `test_baseline.txt`. **Zero new failures.** 180.4 s |
| `verify_move_only.py 8bac4c6f..HEAD` | **PASS, first try** (output below) |
| visual gate | **PASS** (ctest #3406, 28.16 s). `git status --porcelain` **empty** — goldens untouched, not re-blessed |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `check_file_sizes.sh --files` (5 sources) | **5 ok, 0 warn, 0 fail** (581 / 503 / 800 / 414 / 276) |

```
$ python3 scripts/refactor/verify_move_only.py 8bac4c6f..HEAD
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt

Files verified: 5
Boilerplate: +64 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
```

(`CMakeLists.txt` also trips `check_file_sizes.sh`'s WARN at 1,898 lines. That
is **pre-existing** — it was 1,892 before this unit and has been over the
1,500 line since long before Batch J. Not this unit's to fix; boarding it is
the orchestrator's call.)

### The three questions

**1. Did `verify_move_only.py` come back clean first try?**
Yes — against the correct base. But the first invocation, run exactly as the
plan writes it (`verify_move_only.py fable..HEAD`), **failed with 533
substantive removed lines and 18 added**, none of them inventory: they were
T317's and T318's tests and `renderer_rdp.cpp`/`skill` changes, seen in
reverse. **Cause: `fable` moved under me mid-unit** — it was `8bac4c6f` at
checkout and `68e7f621` twenty minutes later, when slot3's work merged.
`fable..HEAD` diffs against the *current* branch tip, not the fork point.

**This is a template bug and it will bite every parallel wave.** Batch J runs
three units at once and merges as they land, so the base will move under at
least two of every three. **J02-J04 must specify
`verify_move_only.py $(git merge-base fable HEAD)..HEAD`** — or simply the
literal base hash named in the dispatch. As written, the acceptance line will
produce a spurious, very loud FAIL that an executor could easily
misdiagnose as its own breakage.

**2. Wall clock and slowest step.**
**~30 minutes end to end** (04:03 -> 04:32 UTC), single agent, nothing
blocked, no retries. Roughly: reading the four contract docs + surveying the
file 8 min; the cut itself and CMake **under 2 min** (one scripted `sed`
pass); verification **~18 min**, which is essentially all of it:

| step | approx |
|---|---|
| `ctest -j6` | **3.0 min** (the single slowest command) |
| `build-arm-noble.sh` | ~5 min |
| `build-gles2-x86.sh` | ~4 min |
| `cmake --build -j24` | ~4 min |

The mechanical work is nearly free; **the unit costs what the verification bar
costs**, and that cost is fixed per unit regardless of file size. Three
parallel units per wave should land in well under an hour of wall clock, but
they will contend for CPU across four build/test steps — `ctest -j6` on three
slots at once is 18 test processes plus three visual gates, and the gate is
the thing the `-j6` cap exists to protect. **Do not assume three concurrent
units cost the same 30 minutes each; expect the verification tail to stretch.**

**3. What should change before J02-J04 are written.**
- **Fix the `fable..HEAD` base** (question 1). This is the important one.
- **Do not promise a CMake site count.** Say "add the new TUs wherever the
  split file is listed; find it with `grep -n '<file>' CMakeLists.txt
  tests/CMakeLists.txt`". The plan's `:266`/`:456` was wrong here and line
  numbers in a 1,898-line file rot within days.
- **Say the banners travel with their cluster** so every executor makes the
  same choice, and give ranges as *first line of the leading comment*, not
  first statement.
- **Standardise "each new TU carries the original's full include block"** as
  the rule rather than "the includes it actually needs" — it is what T166
  actually did, it is what keeps the commit move-only, and it removes a
  judgement call from seventeen executors.
- Add the **multiset check** as a named, cheap second proof (`sort` the
  bodies, `diff`) — it catches a duplicated or dropped range that a compile
  would not, took 30 seconds, and is stronger than `verify_move_only.py`
  because it is per-line-count, not per-distinct-line.
- **`chmod 644` the created files.** `sed >` in this tree produced mode 755
  and the first commit recorded a spurious `100644 => 100755` on
  `inventory_manager.cpp`; caught and amended, but it will recur in all 17.
- The bar as written is otherwise **exactly right** and cost nothing to meet.
  Keep the "goldens untouched / STOP rather than re-bless" clause verbatim —
  it is the clause that makes the visual gate mean something here.

### Not done (out of scope, deliberately)

No method decomposition (Batch G's shape). `moveItem` at 184 lines and
`parseStaticData` at 224 moved verbatim and are still the two longest bodies
in the subsystem. `docs/module_map.md` is outside this unit's territory and
was not updated; the new TUs will need entries.
