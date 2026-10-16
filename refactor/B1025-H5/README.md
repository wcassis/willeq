# B1025-H5 — the hotbar's live CI feature scenario (#345)

The feature-gap rule (docs/issues.md:66-77) requires a REPEATABLE TEST in CI
before a B-class feature is done. H-1..H-4 built the hotbar (model + rendering
+ the assignment gesture + the official page keys); this unit is the test: a
headless live scenario that drives the hotbar's WHOLE user path against an
eqemu lane and asserts on what the SERVER and the CHAT SAY, never on the
client's own model state.

## What the scenario proves (lane 1, Testcaster + peer Testbuddy)

`scripts/scenario.sh hotbar_lifecycle` (peer `hotbar_isolation_assign`, the
second character on a DIFFERENT account, so isolation is real):

1. **Assignment through the user path.** All four slot types are assigned via
   `HotbarAssignIntent` (the hold-to-pickup gesture H-3 wired,
   `BridgeComponent::onHotbarAssignIntent` -> `HotbarModel::assignSlot`) —
   never a model call, never a model write:
   - spell — `#memspell 288 0` (GM, T401's recipe: persists the spell AND
     sends OP_MemorizeSpell so `findSpellGem(288)` resolves);
   - item — the lane's seed.sql puts 8443 (Bag of Bone Dust, 10 charges,
     Click.Effect = spell 86) in general 7;
   - skill — Fishing 55 (pole + bait also seeded: primary 13 / waist 20);
   - social — a free-text `/emote` macro (the fable form; see the gap below).
2. **Each activated by BOTH routes** — the click (`HotbarActivateIntent`) and
   the keyboard (H-4's hotbar key, injected through AutomatedMode's
   NullInputHandler, drained by `InputActionBridge::processHotbarRequests`) —
   and each asserts a SERVER- or chat-visible landing:
   - spell: the server-delivered buff (OP_Action 231 -> BuffManager, the
     cast_lands model) + the client's "Casting from gem 1" (key route only;
     the click route prints "Casting <name>");
   - item: the client's "Using Bag of Bone Dust" x2 (the HC_OP_CastSpell
     sender prints it only when it sent) + the DB charge decrement (post-
     check: 10 -> 8 or 9 — the key route re-casts an active buff, which
     eqemu may or may not charge; the deterministic half is "dropped from 10");
   - skill: the deterministic `#fish` (GoFish(true) guarantee, forage.cpp:302)
     lands a catch ("caught" line + a NEW cursor item). The client-route
     skill press is best-effort exactly as forage_lands drives it: the
     client's own gate reads the pole's PARSED wire skillType (field 56),
     which a direct DB seed does not guarantee the client parses as 36.
     Zone 96 (dreadspire) has fishing rows and NO watermap, so CanFish()
     passes (forage.cpp:207 requires a watermap to probe) and the catch lands.
   - social: the client's own local echo of /emote (chat_component.cpp:331 —
     eqemu broadcasts emotes ignore_sender, so the actor's line is
     client-generated) x2, once per route.
3. **Page switch.** Page 2, then an empty page-2 slot through both routes:
   nothing fires (no new emote/catch line).
4. **Relog persistence.** /camp -> character select -> Enter World -> re-
   zone. The three TYPED slots restore from the per-character INI (the post-
   check reads the BYTES: Page1Button1=H<gem>, Page1Button2=F8443,
   Page1Button3=J55 — exactly 3 lines). The free-text emote slot does NOT
   persist: the official client's social is an emote INDEX (E<number>);
   fable's free-text form encodes to nothing and the save skips it
   (hotbar_persistence.cpp, `encoded()`) — the documented gap, asserted
   ABSENT here.
5. **Isolation (acceptance C).** The peer (Testbuddy) assigns ONE skill slot
   through the same user path; the post-check reads BOTH per-character INIs:
   Testbuddy's holds ONLY its own J55, none of Testcaster's.

## Lane wiring

- `scripts/scenario_configs/hotbar_lifecycle.json` (Testcaster, lane 1) +
  `peer.json` (Testbuddy).
- `scripts/scenario.sh`: `INVENTORY_CHARS=Testcaster` (snapshot/restore pair),
  `SEED_SQL=hotbar_lifecycle_seed.sql`, `PEER_SCENARIO=hotbar_isolation_
  assign`, and `export WILLEQ_CACHE_DIR=$OUT/cache` (the per-character INIs
  land in a known place for the post-check; ResolveCachePathForWrite).
- `hotbar_lifecycle_seed.sql`: pole primary 13, bait waist 20, consumable
  general 7 (DB numbering, T494) — Testcaster is fully equipped, so the kit
  is INSERTed (the ranged_single_shot model), not #summoned.
- `hotbar_lifecycle.post.sh`: the server half (charge decrement, both INIs,
  the two "Using" lines).
- No CI job: the lane's live scenarios are operator-rig, not pipeline (they
  take the lane lock and hold it minutes; docs/ci.md "Live scenarios").
  This card's acceptance A is the live run below on lane 1; the rig is
  re-runnable any time via the command line.

## Live run (lane 1, 2026-10-16)

`EQEMU_LANE=1 bash scripts/scenario.sh hotbar_lifecycle --unit B1025-H5
--timeout 420` — exit code, client/peer/post logs in
`/mnt/projects/fable/review/B1025-H5/scenario/` (and attached to the task).

## Not verified / left open

- The free-text emote slot's persistence is the DOCUMENTED fable-vs-official
  gap (official = E<number> index; H-O's oracle measured that form), not an
  oversight: the relog leg asserts the slot is absent, and H-5's README is
  the named home for it. Filling it (persist the index form) is a follow-up,
  not this unit.
- The exact post-relog charge count (8 vs 9) depends on eqemu's decision to
  charge a completed cast whose buff it will not re-apply; the post-check
  asserts the deterministic half (dropped from 10) and prints the measured
  value.
- The H0 partial official-client capture and its "Not captured / not
  verified" cases remain open (B1025-H0); this item's BOARD row is struck for
  the CI-test half H-5 owes.
