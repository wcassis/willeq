# K01 — Pilot: `GuildComponent` + `ClientContext`

Batch: `refactor/K_component_extraction.md` (read §Mechanism first; this
plan only carries what is specific to the guild family). Runs ALONE — no
other K unit is dispatched until this Review is folded into the batch doc.
**Dispatch after K00 Leg B has merged** (the allowlist ratchet in step 6
needs `scripts/refactor/file_size_allowlist.txt` to exist).

## Measured at `3808ebe9` (2026-09-03)

`src/client/eq_guild.cpp` is 266 lines, 11 `EverQuest::` bodies, one CMake
site (root `CMakeLists.txt`, the `src/client/eq_guild.cpp` line in the core
source list — grep for it, never cite the line).

| method | callers outside `eq_guild.cpp` (src/include) | tests |
|---|---|---|
| `SendGuildInvite` | `eq_action_handler_extended.cpp` | `test_guild_wire_rank.cpp` |
| `SendGuildInviteAccept` | `eq_action_handler_extended.cpp`, `action/action_handler.h` (a comment or signature — read it) | — |
| `SendGuildRemove` | `eq_action_handler_extended.cpp` | `test_guild_wire_rank.cpp` |
| `SendGuildDemote` | `eq_action_handler_extended.cpp` | — |
| `SendGuildLeader` | `eq_action_handler_extended.cpp` | — |
| `SendGetGuildMOTD` | `eq_action_handler_extended.cpp` | `test_wire_layout_t145.cpp` |
| `SendSetGuildMOTD` | `eq_action_handler_extended.cpp` | — |
| `ZoneProcessGuildInvite` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMOTDReply` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMemberUpdate` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMemberAdd` | `eq_zone_dispatch.cpp` | — |

Every one has an external caller, so every one keeps a one-line forwarder in
`eq.h`. Re-measure with `grep -rw` at your tip; the table is a lead.

Members referenced by `eq_guild.cpp`:

| member | refs | other families | decision |
|---|---|---|---|
| `m_has_pending_guild_invite` | 3 (+ inline `HasPendingGuildInvite()` in `eq.h`) | none | **moves** into the component; `HasPendingGuildInvite()` becomes a forwarder |
| `m_guild_invite_from` | 3 | none | **moves** |
| `m_guild_invite_id` | 3 | none | **moves** |
| `m_guild_id` | 9 | written by `eq_zone_entry.cpp` (`ZoneProcessPlayerProfile`) and `ZoneProcessSpawnAppearance`; read by `GetGuildID()` | **stays** in `EverQuest`; reached as `owner_.m_guild_id` |
| `m_guild_rank` | 1 | written by profile parse | **stays**; `owner_.m_guild_rank` |
| `m_character` | 7 | 11 families (core) | `ctx_.character` |
| `m_guild_name` | 0 in this TU | measure who reads it | stays; note it in the Review |

`QueuePacket` is an `EverQuest` method with 40+ callers — stays; the moved
bodies call `owner_.QueuePacket(...)`.

Text readers of `eq.h` in `tests/`: ten files (grep `eq\.h` in `tests/*.cpp`);
the one that parses it is `test_duplicate_state_stores.cpp`. The three moved
members are not state-module names, so its `kKnownRemaining` should be
unaffected — run it and say so.

## Steps

1. `include/client/client_context.h` — `struct ClientContext` with one
   reference member per core service, named without the `m_` prefix:
   `game_state`, `bridge`, `zone_connection`, `entities`, `character`,
   `combat_manager`, `audio_manager`, `trade_manager`, `spell_manager`,
   `inventory_manager`, `skill_manager`, `zone_connection_manager`,
   `command_registry`, `buff_manager`, `active_tradeskill_object_id`.
   Match each member's type to the `EverQuest` member exactly (a
   `std::unique_ptr<X>&` stays a reference to the unique_ptr; do not
   dereference in the context). `EverQuest` gains `ClientContext m_ctx;`
   constructed in the ctor initialiser list from its own members — read the
   ctor in `eq.cpp` and put `m_ctx` AFTER every member it references in the
   declaration order, or it dangles. `ClientContext& Context()` accessor.
2. `include/client/guild/guild_component.h` — `class GuildComponent`, ctor
   `(ClientContext&, EverQuest&)`, the 11 methods as public members with the
   same signatures, the 3 moved data members private. Include only what the
   moved bodies need.
3. `git mv src/client/eq_guild.cpp src/client/guild/guild_component.cpp`;
   bodies verbatim, `EverQuest::` → `GuildComponent::`, the substitutions
   from §Mechanism 4 only. Update the one CMake line.
4. `eq.h`: `friend class GuildComponent;`, `std::unique_ptr<GuildComponent>
   m_guild;` (constructed in the ctor after `m_ctx`), delete the 3 moved
   members, replace the 11 declarations + `HasPendingGuildInvite()` with
   one-line inline forwarders. The `// Guild functions` and `// Guild state`
   comment banners go with what they described.
5. `tests/test_guild_component.cpp` + its `add_executable` in
   `tests/cmake/packets_skills.cmake` next to `test_guild_wire_rank`:
   construct via `zonetest::MakeEq()` (see `zone_stream_test_utils.h`),
   reach the component through the accessor you add (`GuildComponent&
   Guild()`), and port TWO of `test_guild_wire_rank.cpp`'s cases to call the
   component directly (invite grants member rank; senders refuse when not in a
   guild). Leave `test_guild_wire_rank.cpp` untouched — it proves the
   forwarders.
6. Ratchet: set the `include/client/eq.h` entry in
   `scripts/refactor/file_size_allowlist.txt` to the new `wc -l` of `eq.h`.
   It must be lower than before; record both numbers.
7. Proof: multiset check per §Mechanism 8 with `ctx_.` / `owner_.` /
   `GuildComponent::` / `EverQuest::` normalised. Paste the residual.

## Territory

`include/client/client_context.h` (new), `include/client/guild/` (new),
`src/client/guild/` (new), `src/client/eq_guild.cpp` (moves),
`include/client/eq.h`, `src/client/eq.cpp` (ctor only), root
`CMakeLists.txt` (one line), `tests/test_guild_component.cpp` (new),
`tests/cmake/packets_skills.cmake`, `scripts/refactor/file_size_allowlist.txt`
(one number), `docs/module_map.md` (the guild row). Nothing else. If a moved
body needs anything not covered by §Mechanism 4, STOP.

## Verification

Full protocol bar. Step 6 is `check_file_sizes.sh --all` (K00 Leg B).
`test_duplicate_state_stores` and `test_reachability_sweep` must pass
unchanged; if either needs a file-list edit, STOP and report — the
orchestrator decides.

## Review

Executed in `wt/slot1` on `task/K01-guild-component`, branched from `fable`
at `e888e89c`.

### ⚠ STOP CONDITION HIT: `eq.h` did not shrink

§Mechanism's third stop condition is "`eq.h` does not shrink -> STOP; the unit
did nothing." `eq.h` went **2,025 -> 2,033 lines (+8)**. The unit did NOT do
nothing -- 266 lines of implementation and three data members left the class --
but the header still grew, and §Mechanism 6's ratchet therefore cannot be
applied. `scripts/refactor/file_size_allowlist.txt` is **left at 2,025,
unchanged**: it may not be raised (2026-09-03 decision) and cannot be lowered.
`FileSizeCeiling` consequently FAILS on this branch. Reported, not absorbed.

**This is arithmetic, not sloppiness.** The floor for K01, with every comment
stripped, is +3:

| line delta | why |
|---|---|
| +1 | `#include "client/guild/guild_component.h"` |
| +2 | `Guild()` and `Context()` accessors (plan step 5 needs the first) |
| +1 | `friend class eqt::guild::GuildComponent;` |
| +1 | `private:` reopening the class after the `WITH_AUDIO` public block |
| +2 | `m_ctx`, `m_guild` |
| **-3** | the three moved data members |
| **= +4** | (+3 if `HasPendingGuildInvite()` is deleted per §Mechanism 5) |

The remaining +4 in the shipped diff is five comment lines documenting the
transition minus one recovered banner. Trimming further buys nothing: the
mechanism cannot make guild's `eq.h` footprint smaller.

**Root cause, and the correction the batch needs:** the ratchet is driven by
the **external-caller rate**, not by family size. A forwarder is line-neutral
(one declaration becomes one inline definition), so the ONLY sources of
shrinkage are (a) declarations deleted outright under §Mechanism 5 and (b)
moved data members. Guild is the worst possible case on both axes: **12 of 12
methods have an external caller** (measured, below), so zero declarations can
be deleted, and only 3 members move -- against +7 lines of fixed, one-time
scaffolding (`ClientContext`, the include, the friend, the two members, the
two accessors). Most of that +7 is paid ONCE for the whole batch, not per
unit, so K02 onward start from a +2 baseline and every deleted declaration is
pure profit. See "Corrections to §Mechanism" 6 for the proposed rewording.

### Measured vs the plan's leads

**Methods.** The lead table is confirmed exactly -- all 11, every one with an
external caller, so all 11 keep forwarders. Differences:

| difference | detail |
|---|---|
| `eq_guild.cpp` has **11** `EverQuest::` bodies, not 12 | the plan's prose says 11; `docs/module_map.md`'s family table said 12. module_map corrected to 11 in this commit. |
| `SendGuildInviteAccept` in `action/action_handler.h` | confirmed a real reference, not a comment: it is the `IActionHandler` virtual's doc. Still an external caller. |
| `HasPendingGuildInvite()` has **ZERO callers** tree-wide | `grep -rw` over `src/`, `include/`, `tests/` returns only its own declaration in `eq.h`. §Mechanism 5 says delete it; the plan's member table says make it a forwarder. **Kept as a forwarder** (the narrower, unit-specific contract wins, and deleting a public accessor is a surface change, not a structural one). Flagged as correction 5. |

**Members.** The lead table is confirmed, with two additions:

| member | lead | measured |
|---|---|---|
| `m_has_pending_guild_invite` | 3 refs + inline accessor | 3 refs + the accessor. **Moved.** |
| `m_guild_invite_from` | 3 | 3. **Moved.** |
| `m_guild_invite_id` | 3 | 3. **Moved.** |
| `m_guild_id` | 9, written by `eq_zone_entry.cpp` | 5 code refs in this TU (+4 in comments); writers are `eq_zone_entry.cpp:435` **and `eq_entities.cpp:799`** (the AT_GUILD_ID SpawnAppearance arm -- the plan named `ZoneProcessSpawnAppearance` but not its TU). **Stays**, reached as `owner_.m_guild_id`. |
| `m_guild_rank` | 1 | 1. **Stays**, `owner_.m_guild_rank`. |
| `m_character` | 7 | 7. `ctx_.character`. |
| `m_guild_name` | "measure who reads it" | **ZERO references anywhere in `src/`, `include/` or `tests/`** -- declaration-only dead state. Left in place (no drive-by fixes); needs a board entry. |

**Two dependencies the plan's tables did not list**, both found only by
compiling: `AddChatSystemMessage()` (13 call sites; `owner_.`) and
`EverQuest::s_debug_level` (1 site) -- see correction 2.

**Text readers of `eq.h`.** `test_duplicate_state_stores` and
`test_reachability_sweep` both **pass unchanged**; no file-list edit was
needed. As the plan predicted, the three moved members are not state-module
names, so `kKnownRemaining` is unaffected.

### Line counts

| file | before | after |
|---|---|---|
| `include/client/eq.h` | 2,025 | **2,033** (+8 -- see the STOP above) |
| `src/client/eq_guild.cpp` -> `src/client/guild/guild_component.cpp` | 266 | 276 (+10, all header comment) |
| `include/client/client_context.h` | -- | 93 (new) |
| `include/client/guild/guild_component.h` | -- | 68 (new) |
| `tests/test_guild_component.cpp` | -- | 159 (new) |

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides before `sort`:
`s/ctx_\.character/m_character/`, then `s/ctx_\.//`, `s/owner_\.//`,
`s/eqt::guild::GuildComponent:://`, `s/EverQuest:://`, trailing whitespace.

Residual: **10 lines, all ADDED, zero deleted, zero modified** --

```
> //                                                    (9-line K01 header note)
> // K01 (Batch K, refactor/K_component_extraction.md): moved again, from
> // src/client/eq_guild.cpp to here, and the bodies rebased from EverQuest onto
> // eqt::guild::GuildComponent. Still verbatim -- only the substitutions
> // §Mechanism 4 allows (m_character -> ..., m_guild_id/m_guild_rank
> // -> ..., they stay on EverQuest; QueuePacket/AddChatSystemMessage ->
> // ...), plus one qualification the batch method did not anticipate:
> // s_debug_level is EverQuest's private static, reached through the friendship
> // as s_debug_level. See the K01 Review.
> #include "client/guild/guild_component.h"          (1 new include)
```

No forwarder, class-scaffolding or friend line appears in the residual: they
are all in `eq.h`, not in the moved TU. The §Mechanism 8 prediction ("the
residual must be only the forwarders, the class scaffolding and the friend
line") describes the WRONG file -- see correction 7.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (clean on the first attempt, and again after each edit; no timeout, no truncation scan needed) |
| `ctest -j6 --output-on-failure` | **3,502 / 3,505 passed**, 17 skipped. 3 failures, all accounted for: `AudioManagerTest.PreloadCommonSounds` (known ALSA quirk), `DocCitations` (expected -- slot2 Leg C), `FileSizeCeiling` (**the STOP above**) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate | **PASS** (re-run after the GLES2 build; 21.4 s). Goldens untouched -- `git status --porcelain` shows no golden path |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **1 FAIL**: `include/client/eq.h: 2033 > allowlisted max 2025`. Allowlist deliberately NOT raised |
| `test_guild_component` (new) | 5 tests, all pass |

`DocCitations` detail: 21 failures, the same count as on `fable`. K01 FIXED
one (`K_component_extraction.md:70` cites `include/client/client_context.h`,
which now exists) and BROKE one: **`refactor/BOARD.md:136` cites
`src/client/eq_guild.cpp`, which this unit moved.** `BOARD.md` is outside K01's
territory and is being edited by slot2's citation leg, so it was left alone --
**the orchestrator must retarget that line to
`src/client/guild/guild_component.cpp`.**

### Corrections to §Mechanism (the pilot's deliverable)

1. **§Mechanism 1 / plan step 1: `ClientContext` cannot be an unconditional
   struct.** `EverQuest::m_audio_manager` is inside `#ifdef WITH_AUDIO`, so
   `audio_manager` must be `#ifdef`-guarded in `ClientContext` *and* in the
   ctor's initialiser list. One of the 15 core members is conditional; a plan
   that lists 15 flat members is wrong. K02+ plans should say "14 + 1
   conditional".

2. **§Mechanism 4 allows two substitutions; three are required.** A moved body
   that touches a `static` member of `EverQuest` needs a THIRD form:
   `s_debug_level` -> `EverQuest::s_debug_level`. Unqualified static lookup
   does not survive the change of enclosing class, and friendship grants
   access but not scope. `eq_guild.cpp` had one such site; larger families will
   have more (`s_debug_level` is the common one). Add it to §Mechanism 4 as an
   allowed substitution.

3. **The substitutions are NOT pure prefix insertions, so §Mechanism 8's
   normalisation as written does not cancel them.** Plan step 1 requires core
   members be named without `m_` (`character`, not `m_character`), so
   `m_character` -> `ctx_.character` changes the identifier as well as adding a
   prefix. `sed 's/ctx_\.\|owner_\.//g'` turns it into `character`, which
   does not match the old side, and every such line shows up as a false
   residual (it did here: 7 lines). The normalisation must map core-member
   names back: `s/ctx_\.<name>/m_<name>/` for each, THEN strip the prefixes.
   Give each K plan the exact `sed` for its own family.

4. **A mechanical substitution pass will rewrite COMMENTS.** The verbatim
   bodies here sit under a 30-line T133 comment that quotes the old code
   (`m_guild_id == 0`, `QueuePacket(opcode, packet)`). A regex pass rewrote all
   of them into `owner_.m_guild_id` etc., silently falsifying a historical
   note. Every K unit must exclude comment lines from the substitution pass and
   re-check them afterwards.

5. **NO FORWARDERS. Retarget callers; delete declarations.** (Rewritten after
   the orchestrator's ruling on the ratchet analysis below -- this supersedes
   §Mechanism 5 as written.) A forwarder keeps `eq.h` as the API surface, which
   is the thing Batch K exists to remove, and it is line-neutral, which is why
   the ratchet could not move. The rule for every K unit is:
   - every moved method's declaration is **deleted** from `eq.h`, forwarder or
     not;
   - every caller is retargeted through the component accessor
     (`m_eq.Guild().SendGuildInvite(...)` from outside, `m_guild->ZoneProcess*()`
     from inside an `EverQuest` member);
   - **test call sites are IN TERRITORY** for the unit -- call-site edits only,
     assertions untouched. K01's plan said "leave `test_guild_wire_rank.cpp`
     untouched", which is not compatible with deleting the declaration;
   - a method with zero callers anywhere is deleted outright, not moved.
     `HasPendingGuildInvite()` was that case, and §Mechanism 5 wins over a plan
     table that says "becomes a forwarder": **§Mechanism is the method, the
     per-unit plan carries only family-specific facts, so §Mechanism has
     precedence on mechanism questions.**
   - **the ratchet must go down every unit, and K01 has now paid the
     scaffolding cost** (`ClientContext`, the include, the friend, `Guild()`,
     `Context()`, the two members -- +7 lines, one-time for the whole batch).
     K02 onward pay only +1 member and +1 accessor and delete every moved
     declaration, so each should shrink `eq.h` by roughly its declaration count.

6. **§Mechanism 6's ratchet and the third stop condition are unsatisfiable for
   a family whose methods all have external callers.** See the STOP section
   above for the arithmetic. Proposed rewording:
   - the stop condition becomes "`eq.h` does not shrink AND no declaration was
     deleted AND no member moved -> STOP";
   - the ratchet becomes "lower the allowlist to the new count **when the count
     went down**; a unit that provably cannot shrink it records the arithmetic
     in its Review and the orchestrator raises the entry ONCE, by the measured
     one-time scaffolding cost, with Will's sign-off";
   - and each plan should carry a **predicted** `eq.h` delta computed from the
     measured external-caller rate BEFORE dispatch, so an unshrinkable unit is
     known at planning time instead of at the verification bar.

7. **§Mechanism 8's predicted residual names the wrong file.** It says the
   residual "must be only the forwarders, the class scaffolding and the friend
   line". Those all land in `eq.h`; the multiset check compares the moved TU
   against its old self, where the residual is only the new header comment and
   any new `#include`. Reword to: "residual = new file-header commentary and
   new includes ONLY; a residual containing a code line means the move was not
   verbatim."

8. **`EverQuest`'s ctor has no initialiser list, and reference members force
   one.** `src/client/eq.cpp`'s ctor assigns every member in the body. `m_ctx`
   holds references, so K01 had to introduce the first `:`-list on that ctor.
   K02+ inherits it, but the plan should say so rather than leaving the
   executor to discover it. (`m_ctx` is declared LAST in the class, per plan
   step 1; the component `unique_ptr` is constructed in the ctor BODY, first
   statement, so nothing below it can see a null `m_guild`.)

9. **Two things the plan asked for that needed a judgement call**, both minor,
   both recorded rather than absorbed:
   - `ClientContext` was put in `namespace eqt` (as `eqt::ClientContext`); the
     plan wrote it unqualified. Every other new type in this tree is
     namespaced.
   - `GuildComponent` was put in `namespace eqt::guild`, matching
     `eqt::inventory` / `eqt::ui`. The plan wrote `class <Family>Component`
     unqualified. K02+ should state the namespace.

10. **`git mv` + a new directory is invisible to the CMake source list until
    you grep for it** -- one line in the root `CMakeLists.txt` (line found by
    grep, per the plan). No `GLOB` anywhere, so a forgotten line is a link
    error, not a silent drop. Worked as the plan described.

### Left for the board

- `EverQuest::m_guild_name` -- declared, initialised, **zero references
  tree-wide**. The reachability pattern again (this makes it #10+).
- `refactor/BOARD.md:136` cites the moved `src/client/eq_guild.cpp`.

---

## Review addendum — forwarders dropped (second commit)

Orchestrator ruling after the ratchet analysis above: **forwarders are dropped
from Batch K entirely.** Merged `fable` (`44515c19`, K00 Leg C: doc citations
plus the checker's `review/` and absolute-path rules) into the branch first --
**clean merge, no conflicts, nothing resolved by hand.**

### The three `eq.h` numbers

| | lines | vs dispatch |
|---|---|---|
| at dispatch (`fable` `e888e89c`) | **2,025** | — |
| first commit, with forwarders | **2,033** | +8 (the STOP) |
| second commit, forwarders dropped | **2,024** | **-1** |

`scripts/refactor/file_size_allowlist.txt` lowered 2,025 -> **2,024**.
`FileSizeCeiling` PASSES. The margin is one line, which is the honest measure of
this family: guild contributes 12 declarations, and the one-time `ClientContext`
scaffolding (+7) very nearly eats all of it. No code was compressed to get
there -- the only edits after the retarget were to comment lines this unit had
itself added.

### Retarget list

| file | sites | change |
|---|---|---|
| `include/client/eq.h` | 12 declarations | **deleted**: 7 `SendGuild*`, `HasPendingGuildInvite()`, 4 `ZoneProcessGuild*`. `Guild()` and `Context()` are all that remain |
| `src/client/eq_action_handler_extended.cpp` | **8** (not 7 -- `SendGuildInviteAccept` appears twice, for accept and decline) | `m_eq.Send*` -> `m_eq.Guild().Send*` |
| `src/client/eq_zone_dispatch.cpp` | 4 | `ZoneProcessGuild*(p)` -> `m_guild->ZoneProcessGuild*(p)`. `ZoneProcessGuildMOTD` at the same file's line 181 is a DIFFERENT method (zone family, `eq_zone_entry.cpp`) and was correctly left alone |
| `tests/test_guild_wire_rank.cpp` | 4 | `eq.Send*` -> `eq.Guild().Send*`. **Assertions untouched** |
| `tests/test_wire_layout_t145.cpp` | **0** | its `SendGetGuildMOTD` mention is a COMMENT, not a call -- no edit needed |
| `include/client/action/action_handler.h` | **0** | line 355 is a COMMENT naming `SendGuildInviteAccept(bool)` in prose, not a reference. Left as-is (it still describes the behaviour accurately) |
| `tests/test_guild_component.cpp` | 2 | dropped the two assertions that read the deleted `eq.HasPendingGuildInvite()` forwarder; replaced one with "`Guild()` returns the same object every time" |

### Multiset residual (re-run, corrected normalisation)

Unchanged from the first commit: **10 lines, all ADDED, zero deleted, zero code
lines modified** -- the 9-line K01 file-header note plus
`#include "client/guild/guild_component.h"`. The moved bodies were not touched
by the forwarder drop, which is the point.

### Verification (second commit)

| step | result |
|---|---|
| `git merge fable` | clean, no conflicts |
| `cmake --build build -j24` | **exit 0** (one intermediate failure, caught and fixed: `test_guild_component.cpp` still called the deleted `eq.HasPendingGuildInvite()` forwarder) |
| `ctest -j6 --output-on-failure` | **3,504 / 3,505 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the ALSA quirk). `DocCitations` **PASSES** (216 citations, 215 ok, 0 fail, 1 skip). `FileSizeCeiling` **PASSES** |
| `check_file_sizes.sh --all` | **799 ok, 0 warn, 0 FAIL** |
| visual gate / `build-arm-noble.sh` | **not re-run, and correctly so**: `git diff --name-only` over `src/client/graphics` and `include/client/graphics` between the merge base and HEAD is **empty** -- no graphics file changed in either commit. Both passed on the first commit |

### BOARD.md

- **B017** citation retargeted from `src/client/eq_guild.cpp` to
  `src/client/guild/guild_component.cpp` (and the symbol from
  `EverQuest::SendGuildInviteAccept` to
  `eqt::guild::GuildComponent::SendGuildInviteAccept`).
- **B019** added (next free ID, B class): `EverQuest::m_guild_name` --
  declared, default-initialised, zero references tree-wide.
