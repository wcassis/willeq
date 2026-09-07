# T455 — the zero-caller census, re-taken over the whole tree

Class B (the dominant defect class on this project is implemented-but-
unreached and duplicated-then-drifted; T366/T414/T429/T442/T448/T452
each found more by hand), slot1, no server. Read `tests/test_reachability_sweep.cpp`,
`scripts/refactor/check_write_only_fields.py` and the other census
scripts, T366's and T414's §Reviews (the census method: identifiers
counted tree-wide over `src/ include/ tests/ tools/ benchmarks/
docker/`, comments stripped, then each candidate re-grepped by hand),
T448's §Review (its 1.62M-occurrence identifier census).

1. Build the census as a SCRIPT (`scripts/refactor/check_zero_callers.py`):
   every free function and every public/protected member function
   declared under `include/` and `src/`, with its occurrence count
   outside its own declaration/definition, comments stripped. Report
   the zero-caller set. Exclude by rule, not by hand: entry points,
   gtest bodies, event-dispatch table targets (read the bridge intent
   table and the command registries — a function reached only through
   a table IS reached), virtual overrides, and operators.
2. For each zero-caller hit: delete it (with its tests if they only
   pin the dead code, docs struck), or if it is a feature that was
   never wired (the T39/B024 pattern), board it with the evidence and
   leave it — do not wire features in this unit. Say the split.
3. The script joins the hygiene label as `ZeroCallers` with an
   EXEMPT table (reason per row, as `WriteOnlyFields` does) so the
   count ratchets to zero and stays there.
4. Docs: strike every mention of deleted symbols (DocClaims/
   module_map/CLAUDE.md census numbers re-measured).

## Proof

Build (background), `ctest -j6` (no expected reds), the new net red on
the pre-fix tree (the hit list) and green after, G05/G06 in place
(deletions only), visual gate standalone + ARM (nothing moves), file
sizes, doc nets. New items B476+ (T449/T454 allocate from B474+/B476+ —
say which you used).

STOP: an image golden moves; a deleted symbol turns out reachable via a
table you did not model (then fix the rule, not the hit).

## Review

Executed 2026-09-07 in `wt/slot1`, branch `task/T455-zero-caller-census` off
`fable` at `cf248caa`. RelWithDebInfo, `EQT_GLES2_IRRLICHT_DIR` pointed at the
slot's own Irrlicht (both checked at checkout).

### What shipped

1. **`scripts/refactor/check_zero_callers.py`** — the census, and the ratchet
   behind ctest `ZeroCallers` (label `hygiene`, `tests/cmake/hygiene.cmake`).
2. **32 census rows deleted** (34 function names, 38 overloads): the whole of
   `common/util/file`, 26 helpers out of `common/util/strings`, and three
   `position.h` overload sets. 993 lines removed, **zero inserted** in any
   source file.
3. **417 rows boarded** as **B490–B502**, one entry per subsystem, every row
   listed individually in the script's `EXEMPT` table so a NEW dead function
   cannot be added silently. **B503** is the process finding (the census is one
   pass and does not see cascading death).

### The census: method and numbers

Declarations are collected from `include/` and `src/`: every free function at
namespace scope and every **public or protected** member function, comment- and
string-stripped, with class scope and access tracked by a brace walk. Uses are
swept from **`src/ include/ tests/ tools/ benchmarks/ docker/`** — board B166's
rule, so `tools/model_viewer.cpp` can never again be the caller a census missed.

| | pre-fix (`cf248caa`) | post-fix |
|---|---|---|
| functions declared under `include/`+`src/` | 6,686 | 6,651 |
| public/protected, non-virtual candidates | 4,465 | 4,430 |
| distinct candidate names | 3,977 | 3,943 |
| files swept | 1,213 | 1,211 |
| **zero callers anywhere** | **447** | **417** |
| reached only from `tests/` | 322 | 322 |
| reached only from `tools/`+`benchmarks/` | 36 | 36 |

The two secondary buckets are printed but not gated. `tools`-only is **not** a
hit (B166/B161: six of the seven zero-caller `SpellVisualFX` methods are called
by `tools/model_viewer`); `tests`-only is a real signal — 322 public functions
whose only caller is their own test — but it is a different unit's argument and
gating it here would have swamped the ratchet.

### Exclusion rules implemented (by rule, never by hand)

| rule | why |
|---|---|
| constructors, destructors, `operator*` | reached by construction/destruction/expression syntax, never by name |
| `main` | the loader calls it |
| **standard-library customisation points** (`allocate`, `deallocate`, `construct`, `destroy`, `max_size`, `begin`, `end`, `cbegin`, `cend`, `rbegin`, `rend`, `swap`, `hash`) | the library calls them and no source line names them. **The census found this rule itself**: `PageAlignedAllocator::allocate()` / `::deallocate()` (`include/common/util/ksm.h`) were reported dead while every `std::vector<T, PageAlignedAllocator<T>>` reaches them |
| anything virtual — a declaration carrying `virtual`, `override`, `final` or `= 0`, **plus any name declared `virtual` anywhere in the tree** | an override reached through a base pointer never names the override. The second half covers an override written without the keyword |
| private members | the plan scopes the census to public/protected |
| out-of-line definitions (`Owner::name`) are not re-collected, and neither definitions nor re-declarations count as uses | otherwise a function's own body reads as its caller — the shape that hid `getSkyDebugInfo` and `removeWorldObject` until T448 |
| **an occurrence NOT followed by `(` is a USE** | this is what makes a function reached only through a dispatch table, a command registry, a `std::function` target or a member-function pointer correctly REACHED. No table had to be modelled; the bridge intent arms, `command_processor_registration.cpp` and the hotkey/renderer action registries all name their targets as identifiers, so they are covered by construction |

Two rules exist purely to keep the error asymmetric in the safe direction: an
ambiguous occurrence is always resolved to USE, and identifier matching ignores
receiver type, so a name collision can only ADD uses. The census therefore
**misses** dead functions and never invents one; 417 is a floor, and the script
says so.

### The trap that nearly shipped

The first classifier read the text immediately before a name and called
`Owner::name(` a definition. That is right for
`const std::string eqt::util::StringFormat(const char*, ...)` and **wrong for
`eqt::util::StringFormat("x");`**, a perfectly ordinary qualified call at
statement start — and hiding a call INVENTS a dead function, the one direction
this census must never get wrong. The fix walks back over the whole qualified-id
and judges what precedes it (`check_zero_callers.py`, `classify_occurrence`).
**It moved the hit list from 485 to 449: 36 of the first run's rows were
fabricated.** A definition whose return type sits on the previous line still
scores as a use, which hides a dead function rather than inventing one.

### Precision check

Eighteen rows sampled with `random.seed(455)` and hand re-grepped `-w` across
all six directories, independently of the script: **18/18 confirmed genuinely
zero-caller.** `computeState` was the instructive one — fourteen raw grep hits,
every one of them a comment or a T423 tombstone, which is exactly why the
census strips comments before counting (T442's rule).

### The delete / board split

**Deleted (residue — a utility layer that predates this client):**

| what | rows | lines |
|---|---|---|
| `include/common/util/strings.h` + `src/common/util/strings.cpp` — 26 helpers: `ContainsLower`, `ToBigInt`, `ToUnsignedBigInt`, `ToUnsignedInt`, `RemoveNumbers`, `UcFirst`, `GetBetween`, `Implode`, `MillisecondsToTime`, `SecondsToTime`, `Wrap`, `TimeToSeconds`, `ToBool`, `StringFormat`, `vStringFormat`, `join_pair`, and the ten old C-string functions | 26 | 566 |
| `include/common/util/file.h` + `src/common/util/file.cpp` — **the whole translation unit**, plus its CMake line | 4 | 97 |
| `include/client/position.h` + `src/client/position.cpp` — `DistanceSquaredNoZ`, `IsHeadingEqual`, `IsPositionEqualWithinCertainZ` (five overloads) | 3 | 27 |

**Boarded (never-wired features — the T39/B024 rule, no wiring in this unit):**
B490 environment/particles (35), B491 asset loaders (49), B492 detail/foliage
(24), B493 paperdoll + output renderer (15), B494 renderer components (127),
B495 inventory (41), B496 GameState modules (30), B497 top-level managers (26),
B498 input injection (15), B499 audio (12), B500 game modes (10), B501 `common/`
utilities (12), B502 gameplay-domain managers (21).

The split rule was: **delete only what is residue from a layer this client no
longer has; board anything that is a coherent feature with a plausible caller.**
`SkeletalAnimator`'s blend-and-trigger surface (16 rows) is the cleanest example
of the second — a complete feature with no caller at all, which is precisely
what L03 correction 5 says not to delete silently, because a silent delete turns
a findable defect into an invisible one.

`common/util/ksm.h`'s three heap helpers were **not** deleted although they are
dead: every one is paired across a `_WIN32` / Linux `#ifdef` and a partial
deletion there is not mechanical. Boarded under B501 with that reason.

### Cascading death — the finding that became B503

The census is **one pass**. After deleting the 24 `strings` helpers, a re-run
reported two NEW hits — `Strings::SecondsToTime` and `Strings::ToUnsignedInt`,
whose only callers had been `MillisecondsToTime` and `TimeToSeconds`, deleted
minutes earlier. They were deleted too; a third pass produced none, so the tree
is at a fixed point. The same shape hid the whole of `common/util/file`:
`File::Exists()` showed exactly one use, the free `Exists()` wrapper — which was
itself uncalled. **A one-hit function whose one hit is inside another dead
function is dead**, and no single pass can see that. Whoever works B490–B502
must re-run the census after each group until it stops producing rows.

### Proof

| step | result |
|---|---|
| baseline build (background, exit file) | **exit 0** |
| build after deletions | **exit 0** |
| build after the doc/cmake edits | **exit 0** |
| **`ZeroCallers` RED on the pre-fix tree** | **exit 1** — `30 unlisted, 0 stale`, naming `(free)::StringFormat`, `(free)::CleanMobName`, `(free)::DistanceSquaredNoZ`, `Strings::ToBool`, `Strings::Wrap`, … Staged via an exported patch and `git apply -R --index` (**never `git stash`** — T306) |
| **`ZeroCallers` GREEN after** | **exit 0** — `zero-caller census clean: 417 functions, all 417 classified in EXEMPT` |
| `ctest -j6` | **4,685 / 4,686**, one failure: `check_build_tree_orphans`, on a **stale `build/bin/s3d_index_builder`** this slot inherited (T452 deleted that target). Deleted the binary; the test re-runs green standalone. Host load 17–52 through the unit (three slots) |
| G05 / G06 goldens | untouched — this unit changes no renderer TU, so `renderer_frame_render` and `renderer_player_physics` were not edited. Zero insertions in every source file it did touch (`git diff --numstat`: `0 1`, `0 5`, `0 21`, `0 72`, `0 22`, `0 76`, `0 494`) |
| `DocClaims` / `DocCounts` / `CommentClaims` / `DocCitations` | all green. Ten names added to `check_doc_claims.py`'s `RETIRED` table so a doc cannot reintroduce the deleted utility layer; `docs/module_map.md`'s hygiene-test count corrected 9 → 10 |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS**, all six verifications; CPU p95 7.322 / 7.612 / 9.944 ms against ceilings 10.108 / 10.672 / 10.948. **No golden moved** (`git status -- data tests` clean apart from `tests/cmake/hygiene.cmake`) |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | 4 files checked, 0 warn, 0 fail |

### Deviations

1. **The `EXEMPT` table is two dicts, not one.** `check_write_only_fields.py`
   writes a `(type, field) -> (category, reason)` literal; at 417 rows with
   thirteen shared reasons that is 2,500 lines of duplicated prose. This script
   keeps `EXEMPT_GROUPS` (row → board id) beside `BOARD_REASONS` (board id →
   the reason, once) and composes the same `(category, reason)` table from
   them. The `--check` contract is identical in both directions: an unlisted
   row fails, and a listed row whose function now has a caller fails.
2. **Board numbers moved from B476+ to B490+ mid-unit.** The dispatch allocated
   B476+, but T454 merged onto `fable` while this unit ran and used **B476** for
   its `equipment_models` finding. The T455 entries were renumbered to
   **B490–B503** to clear T449's B474+ and T454's B476+ outright.
3. **The unit deletes 32 of 449 rows and boards the rest.** The plan allows
   either disposition per hit; the split is stated above and is deliberate.
   Deleting 417 functions across 106 headers in one branch is not a diff that
   can be reviewed, and several of the groups are features rather than residue.
