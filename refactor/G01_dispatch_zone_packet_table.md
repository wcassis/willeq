# G01 — `DispatchZonePacket`: the 145-arm switch becomes an opcode table (Batch G pilot)

Batch: `refactor/G_oversize_methods.md`. **Runs ALONE.** Measured at
`1c9afba7`: `src/client/zone/zone_dispatch.cpp` (958 lines),
`ZoneComponent::DispatchZonePacket` from line ~78 to the end; **145 `case`
arms**; **101 are a single call + `break`** (`ctx_.Family().ZoneProcessX(p);`
or `ZoneProcessX(p);`); **68 arm-lines carry local logic** (`if`/`for`/
`auto` — nested handlers, inline `OP_MemorizeSpell` scribe logic K05
noted, opcode-name logging, deliberate no-op arms with comments).

## Shape

1. **Every arm with local logic becomes a named private handler first**
   (`void ZoneComponent::OnMemorizeSpell(const EQ::Net::Packet&)` etc.),
   body verbatim (the K-batch multiset proves it), so that afterwards
   EVERY arm is a single call. This is the bulk of the unit and is
   move-only in the K sense. Deliberate no-op arms (a comment + `break`)
   become entries in a `kIgnoredOpcodes` set with the comment carried as a
   one-line reason string; the logging of unknown opcodes stays as the
   table miss path.
2. **The table:** `static const std::unordered_map<uint16_t, Handler>` where
   `Handler` is `void (ZoneComponent::*)(const EQ::Net::Packet&)` for
   in-family handlers and a small lambda-free adapter for sibling
   components (`[](ZoneComponent& z, const Packet& p){ z.ctx_.Guild().ZoneProcessGuildInvite(p); }`
   — or, simpler and preferred, a private one-line forwarder per sibling
   handler so the table holds only member pointers; count them). The
   body of `DispatchZonePacket` becomes: look up, call, else the miss
   path. Target: **under 40 lines.**
3. **Proof:** (a) the call-graph multiset — every handler body equals its
   former arm's lines; (b) **an opcode-coverage test**: for every `case`
   label that existed in the old switch (extract the list from `git show
   fable:src/client/zone/zone_dispatch.cpp` in the test's fixture or as a
   checked-in golden list `tests/goldens/zone_opcodes_g01.txt`), assert
   the table has an entry or the opcode is in `kIgnoredOpcodes`; and the
   reverse — no table entry without a former arm. This is the regression
   net for "an opcode silently stopped being dispatched".
4. Text readers: six tests name `DispatchZonePacket` (the ones that drive
   packets through it: `test_zone_component`, `test_entities_component`,
   `test_chat_component`, `test_guild_component`, `test_combat_manager`,
   `test_eq_commands_equip`) — they must pass unchanged (assertions
   untouched); `zonetest::ReplayZone`/`MakeEq` helpers in
   `zone_stream_test_utils.h` are the harness — read them first.
   `test_reachability_sweep.cpp` may count `case OP_` labels or
   `ZoneProcess*` arms as consumers — read it; if it does, it learns the
   table (correction 21 shape: widen, re-prove with a planted missing
   entry).

## Predicted

`zone_dispatch.cpp` stays about the same length (bodies move within the
TU); `DispatchZonePacket` 880 → < 40; ~68 new private handlers declared
in `include/client/zone/zone_component.h` (+~70 lines there — that
header is under the ceiling; report its count). `eq.h` untouched.

## STOP

- An arm's local logic needs state that no handler signature can carry
  → STOP with the arm named.
- Any of the six packet-driving tests would need an assertion change.
- The coverage test finds an opcode that was in the switch and is in
  neither the table nor the ignored set (that is a bug in the unit).

## Territory

`src/client/zone/zone_dispatch.cpp`, `include/client/zone/zone_component.h`,
`tests/test_zone_dispatch_table.cpp` (new) + cmake + its golden list,
`tests/test_reachability_sweep.cpp` (widening only, if forced),
`docs/module_map.md`. Nothing under `src/client/graphics`: visual gate +
ARM skipped with evidence.

## Review

Executed 2026-09-04 in `wt/slot3`, branch `task/G01-dispatch-zone-packet-table`
off `fable` at `58628347`. Structural, move-only. No STOP condition hit.

### Arm census (measured at the tip, not inherited from the plan)

| class | count | disposition |
|---|---|---|
| `case` labels in the switch | **145** | all accounted for |
| single call + `break;` (pure call) | **104** | table entry, calling the sibling/in-family `ZoneProcess*` directly |
| deliberate no-op (comment + `break;`, nothing else) | **3** | `kIgnoredOpcodes`, comment carried as the reason |
| local logic | **38** | named private `On*` handler, body verbatim |
| `default:` | 1 | the table-miss path |

**Deviation from the plan's prediction (101 / 68).** The plan counted
*arm-lines carrying `if`/`for`/`auto`*, which over-counts: 16 of the arms it
grouped as "local logic" are a comment plus a single `LOG_DEBUG` (the skill
responses `OnHide`/`OnSneak`/`OnForage`/… and the four zone-in no-data arms
`OnTaskDescription`/`OnTaskActivity`/`OnClearAA`/`OnClearLeadershipAbilities`/
`OnAAExpUpdate`), and leading comments on otherwise-pure arms (`OnAnimation`,
`OnEmote`, `OnTrack`, `OnBindWound`, …) inflated it further. The mechanical
split is 104/3/38. The 16 log-only arms became handlers rather than
`kIgnoredOpcodes` entries **on purpose**: they are not no-ops, they emit a
distinct message, and folding them into the ignored set would have changed
observable behaviour. `kIgnoredOpcodes` holds only the three arms that were
literally a comment and a `break`.

### Shape delivered

- 38 handlers created, all `void ZoneComponent::On<Name>(const EQ::Net::Packet & p)`,
  private, declared in `include/client/zone/zone_component.h`, reached only
  through the table. `OnTargetCommandResponse` is the one name not derived from
  an `HC_OP_` spelling (its label was the raw `0x61f9`).
- **Table size: 142 entries** + **3 ignored**. One handler type covers both
  in-family and sibling arms: a captureless lambda,
  `using ZoneArmHandler = void (*)(ZoneComponent&, const EQ::Net::Packet&)`.
  **Deviation, deliberate:** the plan's preferred shape was a private one-line
  forwarder per sibling handler so the table could hold member pointers. That
  would have added ~104 forwarder declarations to the header for zero
  behavioural gain. A captureless lambda decays to a plain function pointer, so
  the table is still one POD type and still a `static const unordered_map`; the
  lambdas live in the initialiser of a **static member function**, which is
  class scope, so `z.ctx_.Merchant()` and the private `On*` handlers are
  reachable with no friendship and no forwarders. Header cost: 4 lines instead
  of ~104. Count of sibling forwarders written: **0**.
- `ZoneOpcodeTable()` and `IgnoredZoneOpcodes()` are public static accessors
  returning function-local statics — public so the coverage test can read the
  two maps; function-local so there is no static-init-order question.
- **`DispatchZonePacket`: 880 lines → 15.** (Old span 78–957 inclusive; new
  880–894.) Under the 40-line target.
- `src/client/zone/zone_dispatch.cpp`: 958 → 894 lines.
  `include/client/zone/zone_component.h`: 403 → **461** lines (+58: 38 handler
  declarations + 3 comment lines, the 4-line public table block + 6 comment
  lines, `#include <unordered_map>`, and blank separators). The plan predicted
  ~+70 for ~70 declarations; the lambda shape is why it is 38, not ~70. The
  header is far under the ceiling (`check_file_sizes.sh`: ok).

### Substitutions inside moved bodies

Exactly **one**, and it is forced: `OnMemorizeSpell`'s arm used `break;` as an
early exit for the short-packet guard, which becomes `return;`. Everything else
is byte-identical modulo one tab of dedent. `OnMobRename`'s inner `break;` is a
`for`-loop break and was **left alone** — the two were distinguished by
indentation depth and re-checked by hand.

### Proof

**(a) Line multiset over the TU** (`git show fable:…` vs the new file, each line
`strip()`ed, `Counter` difference both ways). Full residual, every line
attributed:

```
ADDED: 298 lines
    142  table entry
     83  brace ({ } };  — 38 handler bodies + 2 accessors + the new dispatcher)
     38  handler signature
     15  new G01 banner comment
     14  new dispatcher / accessor scaffolding (incl. #include <unordered_map>)
      3  return;   (2 in the new dispatcher + OnMemorizeSpell's break->return)
      3  kIgnoredOpcodes entry

REMOVED: 401 lines
    147  break;        (145 arm breaks + default's + OnMemorizeSpell's early exit)
    146  case label    (145 + default:)
      1  switch (opcode) {
    107  absorbed into table entries:
           104  the pure-call arms' single statement, now the lambda body of
                its table entry (verified one-by-one, see (c))
             3  the three no-op arms' comment, now the reason string in
                kIgnoredOpcodes
```

Residual outside those buckets: **0 lines, both directions.**

**(b) Handler-body verbatim check.** Each of the 38 handler bodies was
reconstructed from the new file and diffed line-for-line against its arm as it
stands on `fable` (dedent by one tab, drop the trailing `break;`, apply the one
`break`→`return`): **38 checked, 0 mismatches**. Each of the 104 pure-call arms'
statements was matched against its table entry's lambda body: **0 mismatches**.

**(c) The opcode-coverage test** — the regression net.
`tests/goldens/zone_opcodes_g01.txt` is the golden list, generated mechanically
from `git show fable:src/client/zone/zone_dispatch.cpp` with the opcode values
resolved from `include/client/eq_opcodes.h`: **145 entries, 145 distinct opcode
values** (`<hex> <label>` per line, so a failure names the opcode the way the
switch spelled it). `tests/test_zone_dispatch_table.cpp` (6 cases, registered in
`tests/cmake/packets_skills.cmake`) asserts:

1. the golden itself has 145 entries — so nothing below can pass vacuously
   against an empty/unreadable file (K11 correction ee, the `readFile` trap);
2. every former label is a key of the table **or** of the ignored set;
3. every key of either map was a former label;
4. the two maps are disjoint **and** partition the golden exactly;
5. all three ignored entries carry a non-trivial reason;
6. no table entry is a null function pointer.

**Planted-fault re-proof (correction 21), done twice against the real code, not
against the golden:**

- deleted `{HC_OP_MobHealth, …}` from the table, rebuilt, ran:
  `EveryFormerCaseLabelIsHandledOrDeliberatelyIgnored` FAILED with
  `opcodes the switch handled that the table no longer dispatches: HC_OP_MobHealth (0x0695)`,
  and `HandledAndIgnoredAreDisjointAndPartitionTheGolden` FAILED 144 vs 145.
  Restored, green.
- added an invented `{HC_OP_Camp, …}` entry, rebuilt, ran:
  `EveryTableEntryWasAFormerCaseLabel` FAILED. Restored, rebuilt, 6/6 green.

`test_reachability_sweep.cpp` needed **no widening**: it never names
`DispatchZonePacket`, never reads `zone_dispatch.cpp` by path, and counts
neither `case OP_` labels nor `ZoneProcess*` arms. Its four file-path readers
are the intent visitor, the event bus, `action_handler.h` and the action
dispatchers; its two `collect({"src","include"})` sweeps are directory-wide and
see the moved bodies in the same TU as before. Verified by grep for
`zone_dispatch` across `tests/` (three hits, all prose comments naming the
pre-K09 filename `eq_zone_dispatch.cpp`) and by the suite passing unchanged.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0**. (Exceeded the 600 s Bash ceiling and finished in the background; not an exit-143. Artifacts asserted present then validated: `build/lib/libwilleq_core.a` present, `ar t` lists 321 members; 251 files in `build/bin`, all valid ELF, none truncated.) |
| `ctest -j6 --output-on-failure` | **3737/3738 passed**; the sole failure is the known host quirk `AudioManagerTest.PreloadCommonSounds`. 3738 tests (was 3732 — the 6 new coverage cases). |
| the six packet-driving suites, assertions untouched | `test_zone_component` 9/9, `test_entities_component` 7/7, `test_chat_component` 14/14, `test_guild_component` 5/5, `test_combat_manager` 66/66, `test_eq_commands_equip` 57/57 |
| `check_file_sizes.sh --all` | **checked 860 files: 860 ok, 0 warn, 0 fail**. `zone_component.h` 461 lines. |
| hygiene re-run after the `docs/module_map.md` edit | `FileSizeCeiling` + `DocCitations` 2/2 passed |
| call-graph multiset | residual fully attributed, 0 unexplained lines (above) |
| visual gate / ARM | **skipped with evidence**: `git status --short` shows the only touched paths are `include/client/zone/zone_component.h`, `src/client/zone/zone_dispatch.cpp`, `tests/cmake/packets_skills.cmake`, `tests/test_zone_dispatch_table.cpp`, `tests/goldens/zone_opcodes_g01.txt`, `docs/module_map.md`, this plan. Zero files under `src/client/graphics`. |

### Corrections to the Batch G template

- **(1) Re-measure the arm classes mechanically at the tip; do not carry the
  plan's numbers into the Review.** G01's plan said 101 pure / 68 logic from a
  grep for `if`/`for`/`auto` on arm lines; the parse says 104 / 3 / 38. A
  by-eye or by-grep census of a switch over-counts "has logic" by every leading
  comment and every one-line log.
- **(2) "Deliberate no-op" means a comment and a `break`, nothing else.** An arm
  that logs is not a no-op — folding a `LOG_DEBUG` arm into an ignored set
  deletes a message. G01 found 3 true no-ops where the plan implied many more.
- **(3) A captureless lambda in the initialiser of a STATIC MEMBER FUNCTION
  removes the forwarder question entirely.** Class scope gives the lambdas
  access to private members and to `ctx_`, so one function-pointer type covers
  in-family and sibling arms with zero per-sibling forwarders; putting the table
  behind that accessor (rather than as a static local inside the dispatcher) is
  also what gets the dispatcher under 40 lines, since the 145-line initialiser
  would otherwise be inside it.
- **(4) Distinguish an arm-terminating `break;` from a loop `break;` before
  converting anything.** G01 had exactly one of each and they were told apart by
  indentation depth; a blind `break;`→`return;` pass would have silently turned
  `OnMobRename`'s entity-search loop into an early return.
- **(5) The golden list must be asserted to be non-empty and the right size in
  its own test case.** Every other assertion in a coverage test passes vacuously
  against a missing file (K11 correction ee). Store `<value> <original label>`
  so the failure message names the opcode the way the source spelled it.
- **(6) Plant the fault in the CODE, not in the golden, and plant it in both
  directions.** Perturbing the golden only proves the file parses; deleting a
  real table entry and adding an invented one is what proves the net catches
  the two regressions it exists for.
- **(7) A batch-G text-reader check is cheap and was negative here** — record it
  either way: grep `tests/` for the TU's filename AND for the method name, and
  read the sweep's file-path list. G01 needed no widening; G02/G05/G06 are
  flagged in the batch doc as needing it, so the check is per-unit.
