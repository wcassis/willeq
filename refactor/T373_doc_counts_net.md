# T373 — a regression net for hand-maintained doc counts (B130); DocClaims over the board's LIVE entries (B131)

Class P, slot2, no server. Read the B130 and B131 board text and T371's
§Review (the six drifted counts and the 39 legitimate struck-ledger hits).

## Leg A — B130

Every count an orientation doc states must be checked against the tree or
generated from it. Add `scripts/refactor/check_doc_counts.py` (ctest
`DocCounts`, label `hygiene`, next to `DocClaims`): a table of (doc, regex
capturing the stated number, a measuring function over the tree). Measuring
functions: enumerator count of `eqt::input::InputAction` and of
`HotkeyAction` (parse the enum body), `registerCommand({` call count in the
registering TU, `RendererIntent` variant count (parse the `std::variant<…>`),
component count per family (`include/client/<family>/` headers), the
zone-load step count (`loadZoneStep` functions). Do NOT net the suite total
or the `owner_` census — T371 already replaced those with the measuring
command; assert instead that the doc names the command and not a number.
Planted fault: change one enum count in a doc → FAIL naming the doc line,
the stated and the measured number; revert. Strike B130.

## Leg B — B131

`DocClaims` skips `refactor/BOARD.md` and the batch close-outs because
struck entries legitimately name retired things. Extend the scanner to the
board with a rule instead of an exemption: scan only LIVE entries (a bullet
that does not start with `- ~~`) and only their own lines (until the next
`- ` bullet or heading); struck entries and the close-outs stay out. Show
the pass line reporting how many live entries were scanned; plant a
retired name into a live entry → FAIL; revert. Strike B131.

## Proof

`ctest -R 'Doc'` + `ctest -j6`, file sizes on the two scripts.

STOP: none expected.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, branch `task/T373-doc-counts-net`,
base `fable` tip **`94b6c08e`** (verified before any work). No server (the
eqemu lock is held by T370 and was not touched), no graphics change, no gate.

| leg | commit |
|---|---|
| A (B130, `DocCounts`) | `4bd4709a` |
| B (B131, `DocClaims` over the board) | this commit |

### Leg A — `DocCounts`

`scripts/refactor/check_doc_counts.py`, registered as `DocCounts` in
`tests/cmake/hygiene.cmake` under label `hygiene`, beside `DocClaims`,
`DocCitations` and `FileSizeCeiling`. Pure doc/source scan: no build
artefact, no server, no display.

Each row is a measuring function over the TREE plus the doc sites that state
its number. The regexes run over the whole file text, not line by line,
because several of these sentences wrap across a line break (the line number
is recovered from the match offset); the stated number may be digits
(`**143**`) or an English word (`eleven component directories`).

| count | measurer | doc sites (regex) | value at this tip |
|---|---|---|---|
| `InputAction` enumerators | parse `enum class InputAction` in `include/client/input/input_handler.h`, comments stripped, sentinel dropped | `src/client/input/CLAUDE.md` — ``` `InputAction`\s+enum\s+\(\*\*(\w+)\*\* ``` | **33** |
| `HotkeyAction` enumerators | same parser, `include/client/input/hotkey_manager.h` | `src/client/input/CLAUDE.md` — ``` `HotkeyAction`\s+enum\s+\(\*\*(\w+)\*\* ``` | **94** |
| `registerCommand({` calls (registry 2) | literal count in `src/client/action/command_processor_registration.cpp`, comments stripped | `src/client/input/CLAUDE.md` and `include/client/graphics/ui/CLAUDE.md` — ``` \*\*(\w+)\*\*\s+`registerCommand\(\{ ``` | **143** |
| `RendererIntent` alternatives | parse `using RendererIntent = std::variant<…>` in `include/client/events/renderer_intents.h`, depth-aware comma split | `include/client/bridge/CLAUDE.md` — `##\s+Intent\s+Types\s+\((\w+)\)` | **36** |
| `loadZoneStep*` functions | distinct `loadZoneStep…(` names declared in `include/client/graphics/zone_load/zone_load_component.h` | `src/client/graphics/CLAUDE.md` — `and its\s+(\w+)\s+steps` and ``` It runs \*\*(\w+)\*\*\s+`loadZoneStepN_\*\(\)`\s+bodies ``` | **20** |
| renderer components | `*_component.h` headers under `include/client/graphics/*/` | `CLAUDE.md` ×3, `docs/module_map.md` ×3 (six regexes, one per sentence shape) | **11** |
| EverQuest components | `*_component.h` headers under `include/client/*/` | `docs/module_map.md` — `Batch K extracted\s+(\w+)\s+components` | **12** |

Seven counts, **fourteen doc sites**, eight docs. Every one of the six T371
corrected is correct at this tip; the numbers above are the tree's, measured,
not quoted.

**Failure modes, all three of them:**

1. A number that disagrees with the tree — `FAIL <doc>:<line> states N …; the
   tree has M`, plus the reason that count drifts.
2. A doc site whose sentence has been REMOVED or reworded past the regex —
   `MISSING`, naming the doc and the pattern. A row that checks nothing is
   not a pass (the T37 rule), so deleting the sentence to silence the test
   fails instead.
3. A measuring function that can no longer read the tree (enum renamed,
   variant moved) — `MEASURE`, naming the parser, not the doc.

**The two counts deliberately NOT netted.** The registered-test total and the
`owner_` census drift with ordinary work; pinning them would make the suite
fail on unrelated units, which is why T371 replaced each with the command
that measures it. `DocCounts` asserts the opposite thing for those — the doc
must still CONTAIN its measuring command:

| doc | required phrase |
|---|---|
| `README.md` | `ctest --test-dir build -N \| tail -1` |
| `refactor/BOARD.md` | `ctest --test-dir <slot>/build -N \| tail -1` |
| `CLAUDE.md` | `re-measure, do not` (the `owner_` census) |
| `src/client/graphics/CLAUDE.md` | `re-measure rather than` (the `owner_` census) |

**Planted fault** — `HotkeyAction` **94** -> **80** in
`src/client/input/CLAUDE.md:24`:

```
1/1 Test #3955: DocCounts ........................***Failed    0.14 sec
DocCounts: 7 counts netted over 14 doc sites in 8 docs, 4 not-a-number assertions
  measured InputAction enumerators                                    33
  measured HotkeyAction enumerators                                   94
  ...
FAIL  src/client/input/CLAUDE.md:24 states 80 HotkeyAction enumerators; the tree has 94. (the key table grows with every UI panel.) Correct the doc -- the measurement is authoritative.
DocCounts: 1 failure(s).
```

Reverted byte-for-byte from a `cp -p` copy (never `git stash` — the stack is
shared across slots), `cmp` clean, `git status` clean for the file, test
green (0.16 s).

**Two doc corrections were needed to make the table honest**, both flagged
rather than absorbed:

- `docs/module_map.md` now STATES the Batch K component count (twelve) that
  its family table only implied, and says that the last two rows (`core`,
  `action_handler`) are not components and have no `_component.h`. Without a
  stated number there was nothing for the client-family measurer to net,
  and the plan named that measurer explicitly.
- `README.md`'s directory tree still said `tests/ # 123 test executables`.
  T371 corrected the prose paragraph (2746 -> "run `ctest -N`") and missed
  this second site — **a seventh drifted count, found by building the net
  rather than by reading**. It now names the measuring command, and is
  covered by the `README.md` not-a-number assertion.

### Leg B — `DocClaims` over the board's LIVE entries

The exemption became a rule. `check_doc_claims.py` now includes
`refactor/BOARD.md` in its scan set and walks it one TOP-LEVEL BULLET at a
time (`- ` at column 0). A bullet whose text opens with the strike marker is
a closed ledger entry and is skipped whole, together with every continuation
line under it, up to the next top-level bullet or a markdown heading. Prose
belonging to no bullet — the class/rules preamble, the section headings, the
horizontal rules — is not an entry and is not scanned. The batch close-outs
(`refactor/K_*.md`), the unit plans and `refactor/completed/` stay OUT
entirely: they are historical end to end and have no live half to separate,
which is the one part of T371's exemption that was right.

Pass line at this tip:

```
DocClaims: 13 orientation docs scanned (including refactor/BOARD.md's 19 LIVE
entries; struck entries skipped), 23 retired names in the table, 2 exempted
line hits (RETIRED-OK)
OK: no live doc names a retired symbol.
```

**19 live entries out of 88 top-level bullets** — 15 live board items plus the
4 header bullets (`Tree confirmed at`, `Date`, `Suite`, `Items are
confirmed`). 68 struck entries are skipped, and with them T371's 39
legitimate hits.

**Two hits the rule found immediately, before any plant.** With the board in
scope and B131 still live, its own text failed three times
(`/newskills`, `setOnSpellLand`, `EntitiesComponent::FindEntityByName` — the
examples it cited to describe the problem). Striking B131 as part of this leg
removed them, which is exactly the intended lifecycle: the ledger text moves
into the struck half at the moment the item closes.

**Planted fault** — `setOnSpellLand` written into the LIVE entry B064 at
`refactor/BOARD.md:417`, and, as a CONTROL, the same name written into the
continuation line of a STRUCK entry at line 32:

```
DocClaims: 13 orientation docs scanned (including refactor/BOARD.md's 19 LIVE entries; struck entries skipped), 23 retired names in the table, 2 exempted line hits (RETIRED-OK)
FAIL  refactor/BOARD.md:417 names the retired 'setOnSpellLand' -- the typedef, setter, member and invocation were all deleted (T357 (B061)). Correct the sentence, or mark the line RETIRED-OK if it is a deliberate tombstone.
DocClaims: 1 failure(s).
```

One failure, not two: the live entry is caught and the struck entry is
correctly silent. That pair is the whole point of the rule, so both halves
were planted at once. `ctest -R '^DocClaims$'` FAILED in the same state.
Reverted byte-for-byte from a `cp -p` copy, `cmp` clean, all three Doc tests
green.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** first invocation (cmake regen for the new test only; no exit 143, so no post-143 sweep was owed) |
| `ctest --test-dir build -j6` | **exit 8**, summary `99% tests passed, 1 tests failed out of 3955` — **3,954 of 3,955**, sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). Suite grew by exactly one: `DocCounts` |
| `ctest -R '^Doc'` | `DocCitations` PASS, `DocClaims` PASS, `DocCounts` PASS |
| `check_file_sizes.sh fable` | "no C++ files to check … nothing was checked, this is not a pass" — this unit touches no C++. Re-run with `--files`: `check_doc_counts.py` 372 lines ok, `check_doc_claims.py` 243 lines ok, `hygiene.cmake` 39 lines ok; 3 ok, 0 warn, 0 fail |
| visual gate / ARM | not run — no graphics change, no `willeq.db` change |

### Deviations

1. **The plan's "component count per family" needed a doc sentence to exist
   before it could be netted.** The graphics family had six stated sites; the
   client family (Batch K) had none, so `docs/module_map.md` gained one true
   sentence rather than the row being dropped. Flagged, not absorbed.
2. **`README.md`'s second stale count was fixed in this unit** though B130's
   text did not list it — T371's sweep had missed it. It is the seventh
   count, and it is now covered.
3. **The board's live-entry count is 19, not the 37 a stale roll-up claims.**
   88 top-level bullets, 68 struck, 15 live items + 4 header bullets. Anyone
   quoting an open-item count should read the pass line instead.

### New board items

None. Both B130 and B131 are struck in place with their closing evidence.
