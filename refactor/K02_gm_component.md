# K02 — `GmComponent` (family: gm)

Batch: `refactor/K_component_extraction.md` — §Mechanism (REVISED) is the
method; apply K01's Review corrections. Runs alongside K03/K04 (Wave 1).

## Measured at `7ffc4522` (2026-09-03)

`src/client/eq_gm.cpp`, 307 lines, 12 `EverQuest::` bodies, one root
`CMakeLists.txt` site (grep `eq_gm.cpp`).

| method | callers outside the family (src/include) | tests |
|---|---|---|
| `SendGMEmoteZone`, `SendGMFind`, `SendGMGoto`, `SendGMKick`, `SendGMKill`, `SendGMLastName`, `SendGMZoneRequest` | `eq_action_handler_extended.cpp` | 0 |
| `SendGMHideMe` | `eq_action_handler_extended.cpp`, `action/command_processor_spells_gm.cpp` | 2 files |
| `SendGMSummon` | `eq_action_handler_extended.cpp`, `include/client/gm.h` (read it — a comment or a declaration?) | 2 files |
| `ZoneProcessGMFind` | `eq_zone_dispatch.cpp` | 2 files |
| `ZoneProcessGMSummon`, `ZoneProcessGMZoneRequest` | `eq_zone_dispatch.cpp` | 0 |

Every caller is retargeted (`eq.Gm().SendGMKick(…)`); every declaration is
deleted from `eq.h`. Re-measure with `grep -rw` at your tip.

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_test_last_gm_hideme`, `m_test_last_gm_summon` | 2 each | only via the inline `Test_GetLastGMHideMe()` / `Test_GetLastGMSummon()` accessors in `eq.h` | **move**; the two `Test_` accessors move to the component; retarget their test callers |
| `m_zone_connection` (19), `m_game_state` (9), `m_character` (6) | core | | `ctx_.` |

Predicted `eq.h` delta: −12 declarations −2 members −2 `Test_` accessors
−(the `TestSentGMHideMe`/`TestSentGMSummon` comment blocks IF the structs move
with the accessors; measure) +2 accessor/member +1 friend +1 include ≈ **−12
or better**. Report the measured number against this.

## Steps

Per §Mechanism 2–8. New files `include/client/gm/gm_component.h`,
`src/client/gm/gm_component.cpp` (`git mv` of `eq_gm.cpp`),
`tests/test_gm_component.cpp` (register next to the existing GM test in
`tests/cmake/` — grep for `SendGMHideMe` in `tests/cmake/*.cmake` to find
it). Port two existing GM-test cases to the component directly.
`include/client/gm.h` exists already — read it; if it is the natural home for
the component's types, say so in the Review, but do not merge the two files
in this unit.

## Territory

`include/client/gm/`, `src/client/gm/` (new), `src/client/eq_gm.cpp` (moves),
`include/client/eq.h`, `src/client/eq.cpp` (ctor), root `CMakeLists.txt` (one
line), `tests/test_gm_component.cpp` (new) + its `tests/cmake/*.cmake`,
`scripts/refactor/file_size_allowlist.txt` (one number), `docs/module_map.md`
(gm row), and CALL-SITE-ONLY edits in `src/client/eq_action_handler_extended.cpp`,
`src/client/eq_zone_dispatch.cpp`, `src/client/action/command_processor_spells_gm.cpp`,
and the test files that call the retargeted methods.

⚠ K03 and K04 run at the same time and edit `eq.h`, `eq.cpp`, `CMakeLists.txt`,
`eq_action_handler*.cpp`, `eq_zone_dispatch.cpp` and the allowlist at OTHER
lines. Touch only your family's lines. The allowlist number WILL conflict at
merge; write your measured number and say so — the orchestrator re-measures.

## Verification

Full protocol bar; `check_file_sizes.sh --all` 0 FAIL with the ratchet
lowered; `test_duplicate_state_stores` and `test_reachability_sweep` pass
unchanged; multiset proof by name mapping.

## Review

Executed in `wt/slot1` on `task/K02-gm-component`, branched from `fable` at
`feb6b10b`.

### Measured vs the plan's leads

**Methods.** The lead table is confirmed exactly: `src/client/eq_gm.cpp` had
**12** `EverQuest::` bodies (9 senders + 3 `ZoneProcess*` handlers), one root
`CMakeLists.txt` site. Re-measured every caller with `grep -rw` at this unit's
tip (`feb6b10b`), not the plan's `7ffc4522`:

| method | callers outside the family, re-measured | tests, re-measured |
|---|---|---|
| `SendGMEmoteZone`, `SendGMFind`, `SendGMGoto`, `SendGMKick`, `SendGMKill`, `SendGMLastName`, `SendGMZoneRequest` | `eq_action_handler_extended.cpp` (1 site each) | 0 |
| `SendGMHideMe` | `eq_action_handler_extended.cpp` | 2 files: `test_group_accept_gm_hideme.cpp` (4 real calls) + `test_titanium_opcodes.cpp` (comment only, confirmed by reading it -- no edit) |
| `SendGMSummon` | `eq_action_handler_extended.cpp`; `include/common/packets/gm.h` was a FALSE LEAD -- see corrections below | 2 files: `test_gm_summon_find.cpp` (3 real calls) + `tests/cmake/action_handlers.cmake` (build registration, not a call) |
| `ZoneProcessGMFind` | `eq_zone_dispatch.cpp` | 1 file, not 2: `test_gm_summon_find.cpp` reaches it only through `ReplayZone`/`DispatchZonePacket`, never a direct call -- no test call site to retarget |
| `ZoneProcessGMSummon`, `ZoneProcessGMZoneRequest` | `eq_zone_dispatch.cpp` | 0, confirmed |

Every caller retargeted to `eq.Gm().Method(...)` (senders) or
`m_gm->Method(p)` (packet handlers, from inside `DispatchZonePacket`). All 12
declarations deleted from `eq.h`, per §Mechanism 5 -- no forwarders.

**`include/client/gm.h` does not exist.** The plan's "read it" instruction
named a file that is not in the tree. The real GM-adjacent header is
`include/common/packets/gm.h` -- a wire-struct file (`GMSummon_Struct`,
`GMFind_Struct`, `GMKick_Struct`, etc., extracted by J14) with no class
declarations at all, so there was no "natural home for the component's
types" question to answer: it is pure POD wire layout and stays exactly
where it is. Flagged as a plan defect, not absorbed.

**Members.** Confirmed exactly: `m_test_last_gm_hideme` and
`m_test_last_gm_summon`, 2 refs each (a `sent=true` assignment and a
`bytes.assign`), reached only through their `Test_Get*` accessors. Both
**moved**, together with their `TestSentGMHideMe`/`TestSentGMSummon` struct
definitions and the two accessor methods -- the whole 21-line test-seam block
(structs + comments + accessors) came out of `eq.h` as one piece, since
nothing else in the class referenced those types.

**Core members**, re-measured with `grep -ow` over `src/client/eq_gm.cpp`:
`m_zone_connection` **19**, `m_game_state` **9**, `m_character` **6** -- all
three exactly as the plan predicted.

**Two dependencies the plan's tables did not name**, found only by reading
the bodies (same shape as K01's `AddChatSystemMessage`/`s_debug_level`
surprises): `PosX()`/`PosY()`/`PosZ()`/`MySpawnId()` (4 EverQuest accessor
methods, 5 call sites total in `SendGMSummon`/`SendGMHideMe`) and
`AddChatSystemMessage()` (4 call sites in the three `ZoneProcess*` handlers).
Both are §Mechanism 4's second substitution (`Foo()` -> `owner_.Foo()`);
`s_debug_level` does **not** appear anywhere in `eq_gm.cpp` -- the third
substitution form was not needed for this family.

### The three `eq.h` numbers

| | lines | vs dispatch |
|---|---|---|
| at dispatch (`fable`/task branch base, `feb6b10b`) | **2,024** | -- |
| after this unit | **1,993** | **-31** |

Predicted: "-12 declarations -2 members -2 `Test_` accessors -(comment
blocks IF the structs move with the accessors; measure) +2 accessor/member
+1 friend +1 include ~ -12 or better." Measured exactly from `git diff --stat`
(`fable`..HEAD, single file): **41 deletions, 10 insertions = net -31.**
Broken out by hunk:

| hunk | deletions | insertions | contents |
|---|---|---|---|
| test-seam block | 22 | 0 | 2 structs (4 lines each) + 2 accessors + 4 comment lines + 2 blank separators, deleted whole (moved to the new header) |
| `Guild()`/`Gm()` accessor area | 0 | 3 | new `Gm()` accessor + its 2-line pointer comment |
| sender declarations | 10 | 2 | "Phase 4: GM Commands" banner + 9 `void SendGM*` decls deleted; replaced with a 2-line K02 pointer comment |
| friend list | 0 | 1 | `friend class eqt::gm::GmComponent;` |
| `ZoneProcessGM*` declarations | 3 | 2 | 3 decls deleted; replaced with a 2-line K02 pointer comment |
| member declarations | 6 | 0 | 2 `m_test_last_gm_*` members + their 2 comments + 2 blank separators |
| include | 0 | 1 | `#include "client/gm/gm_component.h"` |
| `m_gm` member | 0 | 1 | `std::unique_ptr<eqt::gm::GmComponent> m_gm;` |
| **total** | **41** | **10** | **net -31** |

**Predicted "-12 or better", measured -31: much better than predicted.**
K01's Review flagged that guild was "the worst possible case on both axes"
(12/12 callers, only 3 members). Gm has the same 12/12 external-caller rate
but **2 members plus a 21-line test-seam block that travels with them** --
the guild family had no equivalent scaffolding to shed, because its 3 moved
members had no test seam attached. The corollary for K03+: a family's
`eq.h` shrinkage is driven not just by declaration count and member count,
but by how much test-only scaffolding (structs, accessors, comments) rides
along with the moved members. Worth checking per-family before dispatch
rather than assuming K01's "roughly declaration count."

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**2,024 -> 1,993**. `FileSizeCeiling` PASSES (re-run standalone, confirmed
above the residual section).

### Multiset residual (§Mechanism 8, name-mapped normalization)

Normalisation applied to the new file only, mapping core members back by
name before stripping prefixes: `s/ctx_\.character/m_character/`,
`s/ctx_\.zone_connection/m_zone_connection/`,
`s/ctx_\.game_state/m_game_state/`, then `s/owner_\.//` (recovers
`PosX()`/`PosY()`/`PosZ()`/`MySpawnId()`/`AddChatSystemMessage()` verbatim,
since those are method-call substitutions with no member-name change), then
`s/eqt::gm::GmComponent::/EverQuest::/`. `m_test_last_gm_hideme` and
`m_test_last_gm_summon` needed NO mapping -- they moved into the component as
its own members, so the moved body's reference to them is byte-identical
before and after (the K01 guild precedent for a moved member, not a stayed
one).

Residual: **10 lines, all ADDED, zero deleted, zero code lines modified** --
the new K02 file-header note (spread across the top-of-file banner where the
diff tool's sort interleaved it with old header lines) and
`#include "client/gm/gm_component.h"`. `diff` on the sorted files: 0 lines
starting `<` (nothing lost), 10 lines starting `>` (all header commentary +
the one new include). Same shape as K01's guild residual.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **first attempt: exit 143 at the 600s ceiling**, mid-link (many `.o` targets showed `Terminated`, several `bin/*` deleted mid-write per gmake's own "Deleting file" lines -- expected after a kill, not a defect). Scanned per protocol: all 222 produced binaries in `build/bin/*` valid ELF (`\x7fELF` magic) and non-trivial size, both `build/lib/libwilleq_core.a` and `build/lib/libEtcLib.a` intact (`ar t` exit 0 on both, once checked against a writable scratch path -- the first check's "BAD ARCHIVE" verdict was a false alarm from `ar`'s own error output hitting a permission-denied `/tmp_ar_check.log`, not archive corruption). **Second `cmake --build build -j24`: exit 0**, no further edits needed |
| `ctest --test-dir build -j6 --output-on-failure` | **3,510 / 3,511 passed** (99%), 16 skipped (pre-existing DB/graphics/spell-gate skips, unrelated to this unit), **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep.*` (5/5), `DuplicateStateStores.EverQuestKeepsNoCopyOfAStateModuleMember` all **PASS**. `visual_gate` (Test #3506) **PASS**, 20.94s, inside this same run |
| `test_gm_component` (new) | 6/6 pass: `ComponentExistsAndIsStable`, `ContextPointsAtTheOwningEverQuestsOwnMembers`, `HideMeSeamStartsEmpty`, `HideOnSendsOpGMHideMeWithVisibilityZero`, `SummonSeamStartsEmpty`, `SummonBodyCarriesGMPositionAsServerFloats` |
| `test_group_accept_gm_hideme` (retargeted) | all 8 GM-related cases pass (`GMHideMeTest.*` x4, plus the group-accept cases sharing the file) |
| `test_gm_summon_find` (retargeted) | all 8 cases pass (`GMSummonTest.*` x3, `GMFindTest.*` x3, 2 wire-layout static-assert tests) |
| `test_duplicate_state_stores` / `test_reachability_sweep` | **pass unchanged**, no file-list edit needed -- as the plan predicted, `m_test_last_gm_hideme`/`m_test_last_gm_summon` are not state-module names |
| `scripts/refactor/check_file_sizes.sh --all` | **801 ok, 0 warn, 0 FAIL** |
| `scripts/build-gles2-x86.sh` / visual gate (standalone) / `scripts/build-arm-noble.sh` | **not run standalone, correctly so**: `git diff --name-only fable...HEAD -- src/client/graphics include/client/graphics` is **empty** -- no graphics file changed in this unit's 11 touched files (`CMakeLists.txt`, `docs/module_map.md`, `include/client/eq.h`, `scripts/refactor/file_size_allowlist.txt`, `src/client/eq.cpp`, `src/client/eq_action_handler_extended.cpp`, `src/client/eq_zone_dispatch.cpp`, `src/client/eq_gm.cpp` -> `src/client/gm/gm_component.cpp`, `tests/cmake/action_handlers.cmake`, `tests/test_gm_summon_find.cpp`, `tests/test_group_accept_gm_hideme.cpp`, plus the two new files `include/client/gm/gm_component.h` and `tests/test_gm_component.cpp`). `visual_gate` still ran and passed as part of the full ctest suite above, which is strictly more evidence than the standalone script would add |

### Corrections to §Mechanism / the plan

1. **The plan's `include/client/gm.h` pointer is wrong.** No such file
   exists in the tree; the actual GM-adjacent header is
   `include/common/packets/gm.h` (wire structs only, J14 extraction), which
   has no bearing on where `GmComponent`'s types live. Future plans should
   verify a named "read it" file exists before dispatch, or the executor
   burns a round-trip discovering it doesn't.
2. **A family's `eq.h` shrinkage is not just "declaration count" -- test-seam
   scaffolding riding on moved members can dominate.** K01's guild moved 3
   members with no attached test structs and shrank `eq.h` by only 1 line
   net of the one-time `ClientContext` scaffolding cost; K02's gm moved 2
   members but they came with a 21-line struct+accessor+comment block, and
   the unit shrank `eq.h` by 31 net of a now-already-paid scaffolding cost.
   Add this to §Mechanism 6's guidance: predict test-seam line count
   alongside declaration and member count.
3. **§Mechanism 4's `Foo() -> owner_.Foo()` substitution needed FOUR distinct
   methods for this family** (`PosX`, `PosY`, `PosZ`, `MySpawnId`), none
   named in the plan's lead table. Confirms K01's finding that this class of
   dependency (a trivial EverQuest accessor a moved body happens to call) is
   only found by reading the bodies, not by grepping for `m_*` -- add "grep
   for bare `Foo()` calls with no `m_`/`ctx_`/ prefix inside the moved
   bodies" as an explicit measurement step in §Mechanism, not just an
   after-the-fact note in each unit's Review.
4. **The build's first attempt hit the 600s exit-143 ceiling** (this is a
   large tree; a change to `eq.h` forces a near-full rebuild). The
   EXECUTOR_PROTOCOL's post-timeout truncation scan was run in full (ELF
   magic + size on every `build/bin/*`, `ar t` on every `build/lib/*.a`) and
   found nothing wrong; the second `cmake --build` completed at exit 0 with
   no missing or truncated artifacts. No correction to the protocol itself --
   it worked exactly as documented, including the one wrinkle that a
   scratch-path permission error in the executor's OWN verification command
   can masquerade as a bad archive if not double-checked.
5. **No stop condition was hit.** Every moved body needed only the
   substitutions §Mechanism 4 already allows (two of the three forms; no
   `s_debug_level` site existed in this family). No test asserted on a
   member by name that could not be repointed. The predicted delta was
   negative before dispatch and the measured delta is more negative still.
