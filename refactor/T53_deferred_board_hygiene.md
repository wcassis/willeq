# T53 — Board hygiene: make `refactor/deferred.md` mean something again

**Slot: t1. x86 only. No Pi. NO EQEMU — nothing in this unit is live.**
No sibling unit.

**Territory: `refactor/deferred.md` ONLY.** No source changes, no test
changes, no other docs. `git diff` at the end must touch exactly one file.
If you find a real bug while verifying, **record it in the board — do not
fix it.** That is a different unit, and mixing a code change into a
hygiene pass makes both unreviewable.

Read `refactor/EXECUTOR_PROTOCOL.md`, then skim the whole of
`refactor/deferred.md` before touching anything.

## Why this unit exists

The board has 68 sections and ~33 that a heading scan reads as open. **That
number is wrong and the board is no longer a usable work queue.** I
spot-checked one entry and every claim in it was already fixed:

> "Dead parallel state copies in the state layer, WITH LIVE READERS"
> (filed 2026-08-02, three live bugs)
> 1. `CombatState`'s target half "never populated, four slash commands read
>    it" → **populated** by `Application::syncCombatState()` (T39,
>    `8d820392`); the `/assist` reader was repointed by T28.
> 2. `InventoryState` occupancy → **wired** by T42, extended by T49.
> 3. `WorldState::fireZoneChangedEvent()` reading pending-zone fields →
>    **fixed** by T28; the fields are now `m_zoneArrival*`.

Several other headings read as open but say COMPLETE/SWEPT/NOT-A-BUG in
their body (`P32`, `T43`, `T46`, `T52`). The cost is real: a stale board
makes every future scoping pass more expensive and hides the genuinely
open work.

## The governing rule — read this twice

**NEVER DELETE CONTENT.** This file is a research record, not a TODO list;
its history has repeatedly turned out to be load-bearing (T46 resolved a
conflict between two units by consulting T43's classification table).
Hygiene here means **annotate and re-title**, never remove.

**Mark an entry closed ONLY with concrete evidence** — a commit hash, a
file:line that shows the fix, or a named test that pins it. Write the
evidence into the entry. If you cannot produce evidence, **the entry stays
open**, and if you suspect it is closed but cannot prove it cheaply, say
exactly that ("suspected closed by T4x, not verified") rather than
guessing either way.

**Bias toward leaving things open.** A stale-open entry costs a future
reader some time. A wrongly-closed entry loses a real defect, possibly for
good. Those are not symmetric.

## What to do

1. **Walk every `## ` section.** For each, decide: genuinely OPEN,
   CLOSED-with-evidence, or PARTIALLY closed (some claims resolved, some
   not — very common; the worked example above is one).
2. **Re-title so a heading scan tells the truth.** Closed entries get an
   explicit marker in the heading itself (the file already uses forms like
   `— **SWEPT by T43**` and `— **SERVER-SIDE, NOT OURS**`; follow the
   existing style rather than inventing one). For partial ones, the
   heading should say partial and the body should say which parts.
3. **Check the reverse too**: entries currently marked closed that are
   actually still open, or that later work re-opened. Same evidence rule.
4. **Add an index at the top of the file** listing the genuinely-open
   items with their line numbers, grouped sensibly (e.g. client
   correctness / architecture / performance / tooling — your call, justify
   it). This is the deliverable a future scoping pass actually uses.
   Additive only.
5. **Add these genuinely-open items that are NOT currently recorded**,
   found while I was answering a status question:
   - **14 unexamined test skips**: 8 `DoorManagerGraphicsTest.*` and 6
     `InventoryModelViewTest.*`, skipped in the graphics tree. Nobody has
     established whether these are environment-gated by design or silent
     coverage loss. (The 2 `BridgeTeardownAsanRepro` skips are
     deliberately opt-in per T47 — do not file those.)
   - `PfsArchiveContentTest.DdsFileValid` and
     `SoundBufferTest.LoadSwingWav`, likewise unexamined.
   - The existing "2 SpellManagerGems skips" note should be reconciled
     with the fact that they still skip on the current tip.

## Cheap verification techniques that actually work here

- The claim in most entries is of the form "X is never called" /
  "Y is unreachable" / "Z is hardcoded". `grep -rn` against `src/` and
  `include/` settles most of them in one command — that is exactly how the
  worked example above fell apart.
- `git log --oneline -S "<symbol>" -- <path>` finds the commit that
  introduced or removed a call site, which gives you the evidence hash.
- Entries name their filing date and often a fable tip; anything merged
  after that date is a candidate resolver. T-series units T26–T52 have
  Review sections that frequently state what they closed.

## STOP conditions

- You find yourself deleting a section → STOP, that is not this unit.
- You want to fix code you discovered is broken → STOP, record it instead.
- An entry's status depends on a live run to settle → **leave it open**
  and note "needs a live check"; this unit owns no server.
- The pass balloons past the file → STOP; territory is one file.

## Verification bar

1. `git diff --stat` shows **exactly one file changed**:
   `refactor/deferred.md`.
2. **No content removed.** Prove it: `git diff` should be
   overwhelmingly additions and heading rewrites. Report the
   added/removed line counts and explain any removed line individually.
   (Re-titling a heading legitimately removes one line and adds one — that
   is fine and expected; bulk deletions are not.)
3. Both configures still build clean and `ctest` is unchanged in both
   trees — a docs-only change must not perturb anything. Known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). This is a sanity check
   that you did not stray outside territory, so it is cheap insurance,
   not busywork.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs path>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37).

## What to report in the Review

Put the Review in **this plan file**, not in `deferred.md`.

- **A table: every section → verdict (OPEN / CLOSED / PARTIAL) → the
  evidence** for each non-OPEN verdict. This is the unit's real output and
  the thing I will spot-check.
- The before/after count of genuinely-open items.
- Anything you found marked closed that is actually open — flag these
  loudly, they are the dangerous direction.
- Any entry you could not settle cheaply, and what it would take.
- Whether the board is now usable as a work queue, honestly. If it still
  is not, say what remains.
