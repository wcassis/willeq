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

(measured vs lead; predicted vs measured `eq.h` delta; residual; ctest
counts; corrections to §Mechanism.)
