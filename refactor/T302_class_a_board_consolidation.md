# T302 — One ranked class-A list; the audit's numbering demoted to provenance

**Trigger (Will, 2026-08-28).** The board's numbered class-A list is the
2026-08-27 audit's find list — its own heading says "NEWLY SURFACED CLASS-A
DEFECTS (**not previously on this board**)". An item earned a number by being NEW
THAT DAY, not by being severe. Consequence: the worst open defect in the tree
carries no number while twelve less severe ones do, and `bridgeQueue_` sat
mis-filed as a class-C decision for two weeks when it needed no decision at all.

**Deliverable: ONE consolidated, severity-ranked class-A section in
`refactor/deferred.md`**, with the audit's numbering demoted to a provenance note
("found by the 2026-08-27 audit") rather than structure.

## Rules

- **This is bookkeeping. Change no source.** If you find a new defect while
  reading, ADD it to the list with its file:line; do not fix it.
- **Never delete a section** (standing rule for this file). Entries move by being
  re-titled and cross-referenced, and the historical entries stay where they are
  with a pointer to the consolidated list. The file is a research record; its
  history has repeatedly been load-bearing.
- **Verify every entry against the tree at the current `fable` tip before listing
  it.** Do not inherit status from the board — two entries were found stale on
  2026-08-28 (`/target <substring>` closed by T244 `a6218d6f`;
  `connectRendererCallbacks()` deleted by T241). Mark anything you find already
  closed as closed in place, with the commit.
- Rank by **severity as a user or the process actually experiences it**: memory
  corruption and data loss first, then live user-reachable wrongness, then
  dead/unreachable code and bookkeeping. State the ranking criterion at the top of
  the section so the next reader can apply it rather than guess.
- For each entry: one line of description, `file:line`, one line of user impact,
  and whether it is **executor-ready** or **blocked on Will** (and on what).

## Known input (verify, do not trust)

The twelve numbered audit items, plus these six recorded elsewhere on the board —
their scattering is the whole reason for this unit:
- `bridgeQueue_` unsynchronised cross-thread vector — `deferred.md:116` and `:154`,
  `irrlicht_renderer.cpp:305+`. **Ranks first: it is the only open item that can
  corrupt the heap rather than merely behave wrongly.** Same shape for the
  two-word mouse-delta RMW and `keyWasPressed_[]`.
- Loot panel mouse-wheel plumbing, owed by T290 — `deferred.md:133`.
- Server-forced self position changes have no non-sending internal setter —
  `deferred.md:138`.
- `IOutputRenderer::updateCurrentTargetHP` family caller-less — `:112`, `:134`.
- Shrink/grow (SPA 89) decoded, dispatched, dropped — `:57`,
  `spell_effects.cpp:883-891`.
- `collisionZOffset` uses `bboxHeight/2` not origin-to-feet — `:59`,
  `entity_renderer.cpp:602`.

Also fold in the T301 items if they are not already listed, and check whether the
stale Index at `deferred.md:105+` should be struck rather than left to mislead —
the `## STATUS` block wins and the Index is a snapshot at `e70e3347`.

## Out of scope

The first-person arc (T296-T301) is tracked separately; list its open defects like
any other, but do not re-plan that work.

---

## Review (executor, slot1, 2026-08-28, base `4dd8b0bc`)

**Deliverable landed:** `refactor/deferred.md` gains one section,
`## ⚠ THE ONE CLASS-A LIST (T302, 2026-08-28) — RANKED BY SEVERITY. START HERE.`,
inserted inside the `## STATUS` block ahead of everything else. It states the
ranking criterion as a four-tier table, carries 24 ranked entries (each with a
one-line description, `file:line`, a user-impact line, and executor-ready /
blocked-on-Will), a struck "already closed" subsection, and an "adjacent, not
class A" tail so the class-B/C items are not re-discovered inside it.

**No source was changed.** No build, no ctest, no eqemu — bookkeeping unit.
`git diff --stat`: `refactor/deferred.md | 300 insertions(+), 10 deletions(-)`.
The 10 deletions are the 10 lines edited in place (each is re-emitted with an
appended cross-reference or strike-through). **No section was deleted.**

### Verification method

Every entry was checked against the working tree at this tip with `grep`/`sed`
before it was listed — the board's status was not inherited. All twelve audit
items were confirmed present at (or within a few lines of) their recorded
`file:line`; all six scattered items were confirmed open; two entries were found
already closed and are struck in place with their commits.

### Found already closed (struck in place, body kept)

| entry | closed by | evidence checked at `4dd8b0bc` |
|---|---|---|
| `/target <substring>` returns the lowest spawn id (T240 bullet) | **T244 `a6218d6f`** | `EntityManager::findEntityByNameRanked()` (`src/client/state/entity_manager.cpp:162-200`) tiers exact ahead of substring and breaks ties by distance |
| `Application::connectRendererCallbacks()` unreachable (T235 bullet) | **T241** | method and all 15 setters absent from the tree; only tombstone comments at `application.cpp:388-391` and `renderer_frame.cpp:829`. T241's own bullet, one line below, already said so — the board disagreed with itself |

Both had sat on the board after their fixes landed. No third stale entry was
found among the eighteen: the other sixteen are all genuinely open.

### New defects added (found while verifying, not fixed)

1. **`SpellEffects::handleIllusion()` (SPA 58) is the same defect as shrink/grow**
   — `src/client/spell/spell_effects.cpp:877-881`, dispatched at `:395`, one
   `LOG_DEBUG` and no write. The board carried only SPA 89. Folded into tier 2
   item 8 so the three are one unit.
2. **No scroll input exists anywhere in the static UI** — the board carried
   "loot panel mouse-wheel plumbing, owed by T290". The same gap covers chat
   scrollback (`include/client/graphics/ui/static_panels.h:67`) and the skills
   window (`:340`): all three `scrollOffset` fields are honoured by render and
   hit-test and written only by clamps/resets (`static_panels.cpp:169-170`,
   `loot_panel.cpp:192,233`). `StaticUIInput` has no wheel handler and no
   PageUp/PageDown binding exists (`hotkey_manager.cpp:457` binds `KEY_NEXT` to
   StrafeRight). Tier 2 item 12, one input path rather than three.

### Reclassifications made, with the reason

- **`bridgeQueue_` moved from class C to tier 1 item 1** — the top of the queue.
  The "thread-ownership decision" that made it class C was about *chat* key
  events, and T241 settled those by making them render-thread-only. What remains
  is plumbing and needs no ruling from Will. Pushed at
  `irrlicht_renderer.cpp:305-351` (render thread), swap-drained by
  `drainBridgeActions()` (`irrlicht_renderer.h:387`) from
  `graphics_input_handler.cpp:86` (game thread) — verified by grep, both sides.
- **The audit's 1-12 numbering is demoted to provenance** — the intro line under
  "NEWLY SURFACED CLASS-A DEFECTS" now says so out loud and points at the ranked
  list; each ranked entry cites its audit number.

### Could not be settled from the docs, and says so in the entry

- **Item 3 (CRC validation disabled, `eq.cpp:718`).** The TODO says "Remove after
  CRC bug is fixed" and **nothing on this board describes that bug**. Listed as
  executor-ready *to root-cause*; the flip itself is blocked on finding it.
- **Item 19 (`GraphicalMode` falls through to the console renderer).** Whether
  this is a defect or vestigial-by-design is genuinely ambiguous: the mode IS
  constructed in a real graphical run (`application.cpp:324`) and its
  `ConsoleRenderer` IS what the command processor is wired to
  (`application.cpp:381-385`), but the real Irrlicht renderer is owned by
  `Application` on a separate path, so this is not a black screen. The entry
  states both halves and frames it as a delete-or-wire decision rather than
  asserting a user impact it cannot demonstrate.
- **Item 15 (forced self position/pose).** The setter is small and
  executor-ready; what `AT_DIE` for self means for `POS_DEAD` is a product
  question T237 deliberately left open. Marked partly blocked on Will.

### Index at `deferred.md`: annotated, NOT struck

The plan asked whether the stale Index should be struck. **Recommendation: no,
and it was not.** It already carries a ⚠ banner, the 2026-08-27 audit proved it
drifts in one direction only (it understates progress; zero cases of it claiming
a fix the code lacks), and the standing rule for this file forbids deleting a
section. It gained one sentence saying it is now two removes from current and
that nothing should be scoped off it.

### T301 items

`refactor/T301_first_person_eye_placement.md` is **written and not dispatched**
(`d8e49f2a`), and the first-person arc is out of this unit's scope. Its open
defects are listed like any other: the body-visible/weapon-invisible failure as
tier 2 item 16 (blocked on Will), and the missing UI-hide command as tier 2 item
17 — the latter pulled out as independently executor-ready, because it is a gate
blocker for every visual unit, not a first-person question.
