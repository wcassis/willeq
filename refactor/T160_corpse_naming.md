# T160 — Player corpses are not recognised as corpses

**Slot: slot2. x86. NO eqemu, NO lock, NO Pi.** T159 (skill gate) runs in
parallel in slot1 and owns `src/client/skill/` — **you touch neither it nor
`config/`.**

**Territory: `src/client/combat.cpp` (`IsCorpse` and its callers),
`src/client/eq.cpp` (the spawn-name corpse detection at `:1408`), `tests/`,
`refactor/`.**

T155 probed T153's corpse-consider route on a live server and it **FAILED** — but
the defect is not in the sender T153 fixed. Read T155's Review first.

## The defect

eqemu names corpses two different ways:

| corpse of | name form |
|---|---|
| an NPC | `Probemob's_corpse123` — **underscore** |
| a player | `Testmelee's corpse456` — **space** |

`CombatManager::IsCorpse` (`combat.cpp:1681`) is
`entity.is_corpse || entity.name.find("'s_corpse") != std::string::npos` — the
underscore form only. `eq.cpp:1408` does the same test when a spawn arrives.
And `is_corpse` is otherwise set **only by the death path** (`eq.cpp:3311`,
when we watch something die).

**Consequence: any player corpse, and any corpse that already existed when we
zoned in, is treated as a live entity.** `/consider` cons it as if alive;
T153's corpse route never fires. Confirm the two name forms against eqemu source
before you fix anything — T155 read them from live captures, and this unit
should be able to name the server-side site that builds each.

## The work

Fix the recognition, in **both** places, so it covers the space form as well as
the underscore form. Prefer one shared helper over two literals that can drift —
this is the board's duplicated-then-drifted class and there are already two
copies of the same test.

**Check every consumer of `is_corpse` before you widen the predicate.** There
are around a dozen (`combat.cpp:432`, `:2058`, `eq_bridge.cpp:650`,
`spell_type_processor.cpp` ×4, `spell_manager.cpp:483`, `eq_entities.cpp:1638`,
`eq_player_info.cpp:281`). Widening what counts as a corpse changes targeting,
spell validity and auto-attack. **Say in the Review which consumers you checked
and what each does differently now** — a silent behaviour change in combat
targeting is a worse defect than the one being fixed.

Watch for the false positive: a living NPC legitimately named something like
`a_corpse_beetle` must not become a corpse. State how your predicate avoids it.

## Acceptance

- A test with a **player-corpse name** (space form) recognised as a corpse.
- A test with the **NPC form** still recognised (regression).
- A test that a living entity with "corpse" in its name is **not** matched.
- A test covering the zone-in path, not just the death path — the death path
  already works and is not the defect.

## Verification

Full protocol bar. Baseline **2570/2571** at tip `4f69fb76`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

**Not claimed: any wire behaviour.** The live re-probe of `/consider` on a
corpse is owed and is what closes T153. T155 also recorded a second obstacle for
that probe — `NPC:EmptyNPCCorpseDecayTime = 0` makes a fresh NPC corpse
unresolvable server-side almost immediately — so name a **player** corpse as the
better subject for the follow-up.

## STOP conditions

- A consumer of `is_corpse` would change behaviour in a way you cannot justify
  from the plan (report it, do not guess).

## Review

(executor fills this in)
