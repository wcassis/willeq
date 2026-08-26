# T278 — manual tradeskill combine + close container (T264 F7)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live combine. Class A.
Territory: the combine/close-container intents + handler hops T264 KEPT
(delist in `tests/test_reachability_sweep.cpp` when wired), the
`SendTradeSkillCombine` / `SendCloseContainer` senders (T264 noted both are
reached only via kept blocked arms), CommandProcessor `/recipe*` family,
`EverQuest::m_active_tradeskill_object_id` (T232's note: this is where the
open container lives), tests. Do NOT touch buff bar (T277 slot1), scribe
(T276 slot3).

## Gap
`/clickobject` opens a forge/oven (T232 confirmed
`TradeskillContainerOpened/Closed` are published), `/recipesearch` and
`/recipecombine` exist — but T264 found the manual combine and the close
hop have no producer. Establish exactly which part is unreachable before
writing code (the recipe commands may already cover part of it).

## Scope
1. Trace the real path: `/recipecombine` → which sender → does it reach
   `SendTradeSkillCombine`? If it does, the F7 gap is only the container
   CLOSE and the mouse gesture — say so and scope down.
2. Wire what is actually missing, command-first (T156 rule). Verify the
   wire form against eqemu (`Handle_OP_TradeSkillCombine`,
   `Handle_OP_ClickObjectAction` close path) — T273's lesson: check field
   ids in `SerializeItem`/the struct, do not trust member names.
3. Tests: combine request bytes with a container + items; close request;
   the container model clears on the server's confirmation. Red/green.
4. Live: `#summonitem` a Tailoring/Baking kit or use a world container
   (`/clickobject` on a forge in befallen/qeynos2 — T268's item staging
   recipe), put components in, combine, DB shows the product; close the
   container. Restore rows.

## Verification
Full ctest; Review; evidence review/T278/. Commit:
`T278: manual tradeskill combine and container close reachable`.
