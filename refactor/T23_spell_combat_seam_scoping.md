# Spell / skill / combat coverage — seam scoping (for Will's decision)

**Status: SCOPING ONLY. Nothing implemented. Needs Will's ruling on
which option to take before any unit is written.**

Owner: testing session. Prepared 2026-07-31, after T22.

## The question this was expected to answer

T01's backlog item 7 reads: *"Spell/skill/combat non-graphics logic —
~5.4k lines, mostly uninstrumented. Rich behavior, but **needs seams to
separate from the renderer**."* Every later summary repeated that as
"needs a seam decision like T05's."

## Headline: that premise is stale. No seam is needed.

T01 was written 2026-07-28, before the replay seam had been used against
the domain TUs and before T18 built spawn-injection helpers. I re-checked
the actual coupling rather than trusting the survey, and **the layer is
reachable today with no source change at all**.

### What the coupling actually is

Neither `combat.cpp` nor the spell/skill TUs include any graphics header.
They include `client/eq.h` — the coupling is to `EverQuest`, not the
renderer. The back-reference surface is narrow:

| File | Lines | Distinct `m_eq->` methods used |
|---|---:|---:|
| `combat.cpp` | 1864 | 14 |
| `spell_manager.cpp` | 1317 | 10 |
| `spell_effects.cpp` + `spell_type_processor.cpp` + `buff_manager.cpp` | 2157 | 10 combined |
| `skill_manager.cpp` | 572 | (subset of the above) |

The calls are ordinary reads and sends — `GetEntities`, `GetEntityID`,
`GetPosition`, `GetLevel`, `GetCurrentMana`, `QueuePacket`,
`AddChatSystemMessage`. One friend-class member access
(`m_eq->m_loot_complete_corpse_id`, `CombatManager` is a friend).

### Verified empirically, not inferred

A scratch probe constructed a bare `EverQuest eq("127.0.0.1", 0, "test",
"test", "test", "Testchar")` — the exact idiom T04–T18 already use — and
linked it against `willeq_core`:

```
combat_manager: LIVE
spell_manager:  LIVE
skill_manager:  LIVE
buff_manager:   null
combat: HasTarget=0 TargetId=0
combat: after SetTarget(1234) -> HasTarget=0 TargetId=0
```

Three of the four managers are constructed unconditionally in the
`EverQuest` constructor (`eq.cpp:95/102/105`) and are exposed by existing
public accessors (`GetCombatManager`, `GetSpellManager`,
`GetSkillManager`, `GetBuffManager`, `eq.h:782-788`).

Two facts fell out of the probe that shape the real work:

1. **`BuffManager` is null until `InitializeSpellSystem(eqClientPath)`
   runs** (`eq.cpp:205`) — it depends on the SQLite spell DB. That method
   is public, and tests already have `data/willeq.db` plus the
   `EQ_CLIENT_DATA_PATH` compile definition, so it is one extra call, not
   a seam.
2. **`SetTarget(1234)` was a no-op** — and correctly so:
   `CombatManager::SetTarget` (`combat.cpp:221`) scans `GetEntities()`
   and returns false when the id is absent. The probe's entity table was
   empty. This is not a bug; it is the fixture requirement.

## So the real work is fixtures, not seams

The recipe already exists, assembled from three landed units:

- **T05/T04** — `ReplayZonePacket` drives the real dispatcher with no
  network.
- **T18** — `WriteSpawnStruct` / `NewSpawnPayload` populate the entity
  table (reusable from `tests/test_zone_entities.cpp`), and the
  `TestBridge` in `tests/zone_stream_test_utils.h` captures game→renderer
  events for assertions.
- **T05 again** — `GetReplaySuppressedSendCount()` (`eq.h:720`) counts
  outbound sends that were suppressed for lack of a connection, so
  "did this action try to send a packet" is assertable without a socket.

Per-behavior the loop is: construct `EverQuest` → replay spawn/state
packets to reach the precondition → call the manager's public API →
assert on public accessors, bridge events, and the send counter.

## Options for Will

**Option A — no seam; fixture-first coverage (RECOMMENDED).** Write the
units against the existing surface exactly as above. Zero source risk
(tests-only territory, the T12/T17/T19 bar), and it starts immediately.
Cost is fixture authoring: each behavior needs its precondition state
staged via packets. Realistically 3 units — combat targeting/auto-attack,
spell casting/gem state, skill activation + buffs.

**Option B — extract an interface (the T05-shaped move).** Define an
`ISpellHost`/`ICombatHost` over those ~14 methods, mirroring
`IActionHandler`, so managers can be driven by a fake with no `EverQuest`
at all. Genuinely cleaner unit isolation and faster tests. But it is a
source change across five TUs plus a header, for a layer that is already
reachable — and `IActionHandler` exists precisely because the action
layer had *no* other way in. This one does.

**Option C — defer.** The layer is uninstrumented but not obviously
buggy; nothing in T13–T22 pointed at it. Other backlog items (the three
T02 `KNOWN_GAP_` state stubs) are smaller.

**My recommendation: A.** Build the coverage first. If the fixtures turn
out to be genuinely painful — if we find ourselves replaying ten packets
to reach one branch — that pain is the evidence that justifies B, and it
will tell us exactly which methods the interface should carry. Extracting
an interface *now* would be designing the seam before we know its shape,
which is the mistake T05 avoided by extracting only what the dispatcher
actually needed.

## If Will picks A, the proposed unit split

- **T23** — combat: targeting (incl. the entity-must-exist rule above),
  auto-attack enable/disable/toggle, target cycling, consider, and the
  packet sends each one attempts.
- **T24** — spells: gem state, cast preconditions (mana/level/class),
  interrupt, and `BuffManager` after `InitializeSpellSystem`.
- **T25** — skills: activation, cooldowns, the `onSkillActivated`
  callback wiring.

Each is `tests/`-only, dispatchable, and independently mergeable.
