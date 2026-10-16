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
   - item: the client's "Using Bag of Bone Dust" (the HC_OP_CastSpell
     sender prints it only when it sent; every out.System() echo is logged
     twice, plain + [SYS]-tagged, so the post-check counts the plain lines)
     + the DB charge decrement (post-check: the seeded 10 drops — measured
     8, one charge per landed route). The item click-cast DEFAULTS ITS
     TARGET TO SELF (bridge_component_ui_audio.cpp, UseInventoryItem): the
     server's DoCastingChecksOnTarget resolves a null target to the caster
     only for ST_Self spells, and 8443's Enduring Breath is ST_Target — a 0
     target was the silent no-charge failure that a rebuild could not fix.
   - skill: the deterministic `#fish` (GoFish(true) guarantee, forage.cpp:302)
     lands a catch ("caught" line + a NEW cursor item). The client-route
     skill press is best-effort exactly as forage_lands drives it: the
     client's own gate reads the pole's PARSED wire skillType (field 56),
     which a direct DB seed does not guarantee the client parses as 36.
     The seed places Testcaster in freporte (lane 1: the only zone that
     both loads headless AND has a real water body in its watermap —
     befallen's watermap has none), and the prep #gotos a computed
     water's edge (lane-1 freporte.wtr).
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
--timeout 420` — exit code 0 (client VERDICT pass in 137.3 s + post-check
PASS): the 8443 charge stack dropped 10 -> 8 in the DB (both routes
consumed a charge — the server's own record), Testcaster's INI holds exactly
its three typed slots (Page1Button1=H0, Page1Button2=F8443,
Page1Button3=J55; the free-text emote slot absent, the documented gap),
Testbuddy's INI holds only its own J55 (isolation), and 2 plain "Using Bag
of Bone Dust" activation lines. Client/peer/post logs in
`/mnt/projects/fable/review/B1025-H5/scenario/` (attached to the task) and
`/home/user/kb-evidence/kb-h5-hotbar-scenario/scenario/`.

## Not verified / left open

- **CI green (acceptance D) requires the MR pipeline at the pushed head.**
  The lane-seed fix (MR !214, the roster->befallen zone seed in
  scenario.sh) is not in `origin/fable` yet, so a rebase on it happens after
  it merges (one 4-line hunk before `snapshot "$BEFORE"` in scenario.sh).
  This branch's own hygiene (file sizes, doc counts, citations) is green.
  Do not call this MR's CI red an H-5 code defect on sight — read the trace
  (`glab api .../jobs/<id>/trace`) and confirm the failing ctest is not a
  hotbar scenario test.

- The free-text emote slot's persistence is the DOCUMENTED fable-vs-official
  gap (official = E<number> index; H-O's oracle measured that form), not an
  oversight: the relog leg asserts the slot is absent, and H-5's README is
  the named home for it. Filling it (persist the index form) is a follow-up,
  not this unit.
- The post-relog charge count was measured 8 (one charge per landed route —
  the key route's cast also consumed, so both routes' casts complete now
  that the item click-cast self-targets); the post-check asserts the
  deterministic half (8 or 9, i.e. dropped from 10) and prints the value.
- The H0 partial official-client capture and its "Not captured / not
  verified" cases remain open (B1025-H0); this item's BOARD row is struck for
  the CI-test half H-5 owes.
