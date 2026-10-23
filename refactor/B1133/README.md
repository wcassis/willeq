# B1133 (#453): a real-id OP_DeleteSpawn of a corpse is always honoured

Evidence: `/mnt/projects/fable/review/B1133/` (index: its `README.md`).

## 0. Result

`EntitiesComponent::ZoneProcessDeleteSpawn` refused a corpse's DeleteSpawn unless
`BridgeComponent::m_loot_complete_corpse_id` named it. That flag was written in two places:
the same handler, when the delete landed while the loot window was open on that corpse, and
`BridgeComponent::CloseLootWindow`, after an `OP_EndLootRequest`. Every other corpse delete
was dropped at TRACE level, so the corpse stayed in `ctx_.entities` and on screen. That
includes the server's decay delete of an empty corpse, ~1.03 s after the kill
(`review/B1125/pcap_deletespawn.tsv`, 17 of 17).

Rule (Will, 2026-10-23): the server owns corpse lifetime. The client deletes what the server
tells it to delete, nothing more and nothing less.

- **Fix.** The corpse branch now does one thing: if the loot window is open on this corpse, it
  closes it (LootWindowClosed event, loot items and pending slots cleared, looting state
  cleared, `CombatManager::OnLootWindowClosed()`). Then the ordinary delete runs.
- **Deleted.** `m_loot_complete_corpse_id` and `LootCompleteCorpseId()`. After the gate went,
  nothing read them; both writes went with it. `scripts/refactor/check_doc_claims.py` lists the
  two names as retired.
- **Test.** `tests/test_delete_spawn_zero.cpp`, fixture `CorpseDeleteSpawn` (7 cases, headless,
  no network). 5 were RED at 285a53f6; 13 of 13 in the file pass with the fix.
- **Live.** Lane 5, the B1125 A1 tour on 22425839. Each of the three troll kills (spells 38, 54,
  656) gets the server's DeleteSpawn(<corpse>) 1.034-1.039 s after OP_Death. fable logs
  `Entity <id> (<name>'s_corpse<id>) despawned` at that packet's timestamp, and the corpse is
  gone from the frame. The oracle (`review/B1118/oracle_leg`) drops the same three corpses at
  the same packet (+1.028 to +1.043 s). The pre-fix B1125 build leaves all three lying there.

## 1. Test cases (`CorpseDeleteSpawn`)

| case | at 285a53f6 |
|---|---|
| `NoLootWindow_ServerDeleteRemovesCorpse`: Death, DeleteSpawn(0), DeleteSpawn(corpse) | FAIL |
| `TargetedCorpse_ServerDeleteRemovesAndClearsTarget` | FAIL |
| `LootWindowOpen_ClosesWindowAndRemovesCorpse`: one LootWindowClosed event, items/slots/looting state cleared, combat manager out of LOOTING | pass (the one path the gate honoured) |
| `LootWindowOnOtherCorpse_LeftOpen`: a window on another corpse is untouched | FAIL |
| `CorpseNotNamed_Stays`: a corpse no DeleteSpawn names stays | FAIL (its rodent delete was refused) |
| `AfterUserClose_ThenAnotherCorpse_BothRemoved` | FAIL |
| `LiveNpcDelete_Unchanged`: a non-corpse delete, with a loot window open elsewhere | pass |

## 2. Changes

- `src/client/entities/entities_component_appearance.cpp`: gate removed; comments describe the
  server-owned rule.
- `include/client/bridge/bridge_component.h`, `src/client/bridge/bridge_component_ui_audio.cpp`:
  member, accessor and the `CloseLootWindow` write deleted.
- `tests/test_bridge_component.cpp`: the loot-cluster test no longer touches the deleted member.
- `tests/test_delete_spawn_zero.cpp`: the `CorpseDeleteSpawn` fixture.
- `scripts/refactor/check_doc_claims.py`: two retired-name rows.
- Annotated history: `refactor/T33_loot_death_event_wiring.md` (the "refuse deletion until
  looting completes" paragraph), `refactor/K06_entities_component.md`,
  `refactor/K11_bridge_component.md`, `refactor/T23_spell_combat_seam_scoping.md`.
  `docs/module_map.md`, `include/client/bridge/CLAUDE.md` and the T268/T286 notes never named
  the flag or the rule, so they are unchanged.

## 3. Verification

- Headless Release tree: `test_delete_spawn_zero`, `test_bridge_component`, `test_zone_entities`
  green; `ctest -L hygiene` 47/47.
- One whole-tree Release build with the CI flags (`-j 4 -- -k`): BUILD_EXIT=0. In that tree the
  DeleteSpawn/Corpse/Loot/BridgeComponent/ZoneEntities subset is 237/237, the order/coverage
  goldens 25/25 and `ctest -L hygiene` 50/50 (logs in `review/B1133/tests/`).

## 4. Not covered

- The live tour's AE 458 cast killed nothing this run, so there is no rodent-kill row (B1125's
  run had one). The three troll kills are the same packet shape (Death, DeleteSpawn(0) at
  +0.04 s, DeleteSpawn(corpse) at +1.03 s).
- No live leg looted a corpse with items. The mid-loot close path is covered by the device-free
  test only.
