# T283 — one practice-point store; retire the loot-window UI-gesture leg (T280 + T268 hand-backs)

Slot: slot1. x86. NO eqemu (unit tests + a static-panel check). Class A.
Territory: `EverQuest::m_practice_points` / `PlayerState::m_practicePoints`
and the PlayerProfile parse that feeds both, the loot popup question below
(static_panels / static_ui_input — T272's click plumbing is the model),
tests. Do NOT touch the EQE1 index (T281 slot2), world containers (T282
slot3).

## 1. Two practice-point stores (T280)
Both are written from the same PlayerProfile so they cannot drift in
production, but a test seeding one silently gets the wrong answer from the
other. Per the 2026-08-25 decision (one GameState, state written at the
source), keep the `PlayerState` one and delete the duplicate — or, if the
duplicate is what the wire path needs, make the other a read-through. Grep
every reader first and say which way you went.

## 2. Loot window: the last UI-gesture leg (T268)
T268 closed F1 by command and left the three loot INTENTS producer-less
because there is no loot popup. Decide by looking: does the static UI have
a panel that lists corpse contents (T264/T268 said no)? If a panel exists
or is a small addition on the T272 pattern, wire the clicks to the same
handler bodies `/loot`, `/lootall`, `/lootclose` already use (one body).
If building a panel is genuinely a UI feature rather than a wiring job,
STOP and report it as a feature request — do not build a new window
speculatively.

## Verification
Full ctest; visual gate if a panel changed; Review; evidence review/T283/.
Commit: `T283: one practice-point store; loot UI leg <wired|reported>`.

---

## Review (executor, 2026-08-26)

Branch `task/T283-practice-loot-ui` off fable `1b67f864`. No eqemu.
Evidence: `/mnt/projects/fable/review/T283/` (README.md there indexes it).

### 1. Two practice-point stores — `PlayerState` survives

Grepped every reader before touching anything. The result was one-sided:

| site | store used |
|---|---|
| `eq_player_info.cpp:253` `GetPracticePoints()` | `m_game_state.player().practicePoints()` |
| `eq_commands_train.cpp:192,243` (`/train` refusal + the session banner) | `GetPracticePoints()` |
| `eq_merchant.cpp:1662` (`trainerData.practicePoints`, what the trainer event carries) | `GetPracticePoints()` |
| `test_state_modules.cpp:628` | `PlayerState` |
| `eq.cpp:1205` (the OP_SkillUpdate spend gate) | **`m_practice_points`** |
| `eq.cpp:2323` (PlayerProfile parse) | wrote **both** |

So `EverQuest::m_practice_points` had exactly ONE reader — the gate on the
spend — and every user-visible reader went through `PlayerState`. That
makes the choice the plan's default one: keep `PlayerState` (the
2026-08-25 "one GameState, state written at the source" decision), delete
the duplicate. Nothing needed a read-through: the wire path writes the
PlayerProfile value straight into `PlayerState` two lines later.

The gate collapses cleanly because `PlayerState::decrementPracticePoints()`
already floors at zero (pinned by `PlayerStateTest.DecrementPracticePointsFloorsAtZero`),
so `if (m_trainer_npc_id != 0 && m_practice_points > 0) { --m_practice_points;
player.decrementPracticePoints(); }` becomes
`if (m_trainer_npc_id != 0) { player.decrementPracticePoints(); }` —
same behaviour, one store, and the guard can no longer read a different
object than the mutation.

**Measured red/green.** The bug the plan predicted was real and the test
harness was already carrying a comment apologising for it ("Practice points
live in TWO places… seed both, the way a real zone-in does"). Deleting that
double-seeding so the harness seeds only the canonical store turned it red:
`TrainSkill.SkillUpdateDuringASessionSpendsATrainingPoint` saw 5 where it
wanted 4, and the new `TrainSkill.PracticePointsHaveASingleStore` saw the
accessor and the store disagree (3 vs 2). Both green after the delete.
Full logs in `review/T283/before_red_*.txt` / `after_green_*.txt`.

`TrainRefusal.NoTrainingPointsRefuses` also lost its double-write.

### 2. Loot window — REPORTED as a feature, nothing built

The plan said to decide by looking. Looking says there is no panel and no
half of one:

- `PopupType::Loot` (`static_panels.h:77`) is the ONLY "Loot" string in the
  entire static UI. `static_panels.cpp` has thirteen `render*` functions and
  none of them is a loot popup.
- `static_ui_input.cpp` knows `PopupType` only for `Inventory` plus a generic
  Escape-closes-any-popup arm — no loot hit-test, because there are no loot
  pixels to hit.
- There is no renderer-side corpse-contents state at all. The old window
  went with U07c3's `getWindowManager()` removal and the static UI never
  grew a replacement.
- The four game→renderer loot events (`LootWindowOpened`, `LootWindowClosed`,
  `LootItemAdded`, `LootItemRemoved`) reach `IrrlichtBridge` only to
  `LOG_TRACE` (`irrlicht_bridge.cpp:455-467`). Their payloads are discarded.
  The renderer does not know what is on a corpse.

This is why it is NOT the T272/T277 job. Both of those wired clicks onto a
panel that was **already drawn** — the missing piece was hit-testing over
existing pixels. Here the pixels, the state behind them, and the four bridge
arms that would feed that state are all absent. A loot popup needs a
`LootPanelState`, four real bridge arms replacing the log lines, a
`renderLootPopup`, layout constants, hit-testing, popup mutual exclusion,
the three intents pushed from those hits, tests, and a golden re-bless.
That is a window feature, so the plan's STOP applies and no window was
built.

`LootItemIntent` / `LootAllIntent` / `CloseLootIntent` stay on
`kBlockedIntents`; the reason is now written out in
`test_reachability_sweep.cpp` and in `include/client/graphics/ui/CLAUDE.md`
instead of being implied, so the next reader does not have to redo this
grep.

**Feature request for Will:** a static-UI loot popup, sized as its own unit
(panel + four bridge arms + hit-test + re-bless). Until then `/loot`,
`/lootall`, `/lootclose` are the only producers, which T268 already
delivers.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --test-dir build` | **3146/3147**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | PASS |
| visual gate | **PASS**, goldens untouched (no panel changed, run anyway) |
| `scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | no NEW fails — `src/client/eq.cpp` was already over and this unit is net **0** lines there; `include/client/eq.h` WARN pre-existing and this unit is net **-3**. (The run also listed T281's `eqe1_texture_index*` files: `fable` moved under the slot mid-unit when T281 merged. Not this unit's files.) |

### Deviations

None. Item 2 hit the plan's STOP by design, which the plan names as a valid
outcome; it is reported above rather than absorbed.
