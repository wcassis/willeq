# G02 — Split RegisterCommands into per-domain registration methods

Decompose the 2,568-line `eq_commands.cpp` (dominated by the single
`RegisterCommands` method of sequential slash-command lambda registrations)
into per-domain `Register*Commands()` private methods across 2–3 TUs, each
file under the 1,500-line warn ceiling.

## Design

- Command registrations are sequential, independent
  `m_command_registry->...` calls with lambda bodies — partition them by
  domain. Survey the actual command set first, then group into roughly:
  gameplay/chat/social commands; movement/navigation/zone commands;
  debug/diagnostic/renderer commands (match reality over this guess —
  put the real grouping in the review).
- New private methods declared in eq.h (e.g. `RegisterGameplayCommands()`,
  `RegisterDebugCommands()`, …); `RegisterCommands()` becomes a short driver
  calling them in the ORIGINAL registration order (order can matter for
  duplicate names/aliases — preserve it exactly; if a domain grouping would
  reorder registrations, document why it is safe or keep order-critical
  entries in the driver's sequence).
- Lambda bodies move verbatim. The `#ifdef EQT_HAS_GRAPHICS` guard wrapping
  the current file must be preserved on every new file/method.
- Files: keep `eq_commands.cpp` (driver + first domain), add
  `eq_commands_<domain>.cpp` as needed. runPmemDiagnostics /
  runLoadDiagnostics stay where they are.
- Add new files to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Steps

1. Analysis first (in review BEFORE editing): list every registered command
   name in order, with its assigned domain group.
2. Declare the Register*Commands methods in eq.h (private, guarded like
   RegisterCommands).
3. Split verbatim; driver preserves order.
4. CMakeLists update.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — expected:
  additions limited to method signatures/declarations/driver calls and
  per-file boilerplate. Paste output in review for orchestrator sign-off.
- `bash scripts/refactor/check_file_sizes.sh --files <all eq_commands*.cpp>`
  — every file under 1,500 (ok, no WARN).

## Review

### Analysis (written before editing eq_commands.cpp)

`eq_commands.cpp` registers exactly 120 commands via 120 sequential
`m_command_registry->registerCommand(...)` calls inside the single
`RegisterCommands()` method (lines 207–2565), which — along with
`runPmemDiagnostics`/`runLoadDiagnostics` above it — sits inside one
file-wide `#ifdef EQT_HAS_GRAPHICS` (opens line 88, closes at EOF line 2568).

`CommandRegistry::registerCommand()` (`src/client/graphics/ui/command_registry.cpp:18`)
stores commands in a `std::map`/`unordered_map` keyed by lowercased name:
`commands_[primaryName] = cmd;` — **last registration wins**. There are two
commands both named `"who"`: `who_cmd` (line 611, "/who [name] [level range]",
server-request semantics) and `who` (line 1868, "/who [filter]", local
entity-list semantics, category "Social"). Because of last-write-wins, only
the second `who` registration is ever live; the first is silently shadowed.
**This makes relative order between these two registrations
observable/order-critical.**

Existing `// === ... ===` section-header comments in the file already
partition the 120 commands into contiguous, non-interleaved domain blocks
matching file order. The grouping below assigns each contiguous block to one
of 3 domains, preserving the file's top-to-bottom order exactly — so the
`who_cmd` / `who` relative order (and every other potential name collision)
is preserved automatically, since domains are never interleaved: each
domain's commands occupy one unbroken line range, and the driver calls the
3 new methods in the same order their ranges appear in the original file.

Domain groups (real grouping, not the plan's initial guess — matches
reality per the file's own section comments):

| # | Command (name) | Original section | Domain | New file |
|---|---|---|---|---|
| 1 | say | Chat Commands | Social | eq_commands.cpp |
| 2 | shout | Chat Commands | Social | eq_commands.cpp |
| 3 | ooc | Chat Commands | Social | eq_commands.cpp |
| 4 | auction | Chat Commands | Social | eq_commands.cpp |
| 5 | gsay | Chat Commands | Social | eq_commands.cpp |
| 6 | gu | Chat Commands | Social | eq_commands.cpp |
| 7 | tell | Chat Commands | Social | eq_commands.cpp |
| 8 | emote | Chat Commands | Social | eq_commands.cpp |
| 9 | reply | Chat Commands | Social | eq_commands.cpp |
| 10 | invite (invite_cmd) | Group Commands | Social | eq_commands.cpp |
| 11 | follow (follow_cmd, group-follow) | Group Commands | Social | eq_commands.cpp |
| 12 | disband (disband_cmd) | Group Commands | Social | eq_commands.cpp |
| 13 | decline (decline_cmd) | Group Commands | Social | eq_commands.cpp |
| 14 | trade (trade_cmd) | Trade Commands | Social | eq_commands.cpp |
| 15 | accept (accept_cmd) | Trade Commands | Social | eq_commands.cpp |
| 16 | tradedecline | Trade Commands | Social | eq_commands.cpp |
| 17 | canceltrade | Trade Commands | Social | eq_commands.cpp |
| 18 | rezaccept | Resurrection Commands | Social | eq_commands.cpp |
| 19 | rezdecline | Resurrection Commands | Social | eq_commands.cpp |
| 20 | who (who_cmd, server-request variant — later shadowed, see above) | Who Command | Social | eq_commands.cpp |
| 21 | inspect (inspect_cmd) | Who Command | Social | eq_commands.cpp |
| 22 | guildinvite | Guild Commands | Social | eq_commands.cpp |
| 23 | guildaccept | Guild Commands | Social | eq_commands.cpp |
| 24 | guilddecline | Guild Commands | Social | eq_commands.cpp |
| 25 | guildremove | Guild Commands | Social | eq_commands.cpp |
| 26 | guilddemote | Guild Commands | Social | eq_commands.cpp |
| 27 | guildleader | Guild Commands | Social | eq_commands.cpp |
| 28 | guildmotd | Guild Commands | Social | eq_commands.cpp |
| 29 | corpsedrag | Phase 3: Corpse Commands | Social | eq_commands.cpp |
| 30 | corpsedrop | Phase 3: Corpse Commands | Social | eq_commands.cpp |
| 31 | consent | Phase 3: Corpse Commands | Social | eq_commands.cpp |
| 32 | deny | Phase 3: Corpse Commands | Social | eq_commands.cpp |
| 33 | assist | Phase 3: Combat Targeting Commands | Social | eq_commands.cpp |
| 34 | split | Phase 3: Group Commands | Social | eq_commands.cpp |
| 35 | lfg | Phase 3: LFG Commands | Social | eq_commands.cpp |
| 36 | shield | Phase 3: Combat Ability Commands | Social | eq_commands.cpp |
| 37 | save | Phase 3: Misc Commands | Social | eq_commands.cpp |
| 38 | duel | Phase 4: Duel Commands | Social | eq_commands.cpp |
| 39 | duelaccept | Phase 4: Duel Commands | Social | eq_commands.cpp |
| 40 | dueldecline | Phase 4: Duel Commands | Social | eq_commands.cpp |
| 41 | bandage (bindwound_cmd) | Phase 4: Skill Commands | Social | eq_commands.cpp |
| 42 | surname | Phase 4: Cosmetic Commands | Social | eq_commands.cpp |
| 43 | random | Phase 4: Misc Commands | Social | eq_commands.cpp |
| 44 | rewind | Phase 4: Misc Commands | Social | eq_commands.cpp |
| 45 | yell | Phase 4: Misc Commands | Social | eq_commands.cpp |
| 46 | bug | Phase 4: Misc Commands | Social | eq_commands.cpp |
| 47 | petition | Phase 4: Misc Commands | Social | eq_commands.cpp |
| 48 | loc | Movement Commands | Movement | eq_commands_movement.cpp |
| 49 | sit | Movement Commands | Movement | eq_commands_movement.cpp |
| 50 | stand | Movement Commands | Movement | eq_commands_movement.cpp |
| 51 | move (move_cmd) | Movement Commands | Movement | eq_commands_movement.cpp |
| 52 | moveto | Movement Commands | Movement | eq_commands_movement.cpp |
| 53 | warp | Movement Commands | Movement | eq_commands_movement.cpp |
| 54 | follow (entity-follow variant) | Movement Commands | Movement | eq_commands_movement.cpp |
| 55 | stopfollow | Movement Commands | Movement | eq_commands_movement.cpp |
| 56 | target | Combat Commands | Movement | eq_commands_movement.cpp |
| 57 | attack | Combat Commands | Movement | eq_commands_movement.cpp |
| 58 | stopattack | Combat Commands | Movement | eq_commands_movement.cpp |
| 59 | aa | Combat Commands | Movement | eq_commands_movement.cpp |
| 60 | combat | Combat Commands | Movement | eq_commands_movement.cpp |
| 61 | taunt | Combat Commands | Movement | eq_commands_movement.cpp |
| 62 | autofire | Combat Commands | Movement | eq_commands_movement.cpp |
| 63 | help | Utility Commands | Movement | eq_commands_movement.cpp |
| 64 | quit | Utility Commands | Movement | eq_commands_movement.cpp |
| 65 | q | Utility Commands | Movement | eq_commands_movement.cpp |
| 66 | camp | Utility Commands | Movement | eq_commands_movement.cpp |
| 67 | debug | Utility Commands | Movement | eq_commands_movement.cpp |
| 68 | timestamp | Utility Commands | Movement | eq_commands_movement.cpp |
| 69 | perf | Utility Commands | Movement | eq_commands_movement.cpp |
| 70 | reloadeffects | Utility Commands | Movement | eq_commands_movement.cpp |
| 71 | reloadspells (spellreload) | Utility Commands | Movement | eq_commands_movement.cpp |
| 72 | reloadweather | Utility Commands | Movement | eq_commands_movement.cpp |
| 73 | weatherquality | Utility Commands | Movement | eq_commands_movement.cpp |
| 74 | frametiming | Utility Commands | Movement | eq_commands_movement.cpp |
| 75 | simworker | Utility Commands | Movement | eq_commands_movement.cpp |
| 76 | sceneprofile | Utility Commands | Movement | eq_commands_movement.cpp |
| 77 | dumpscene | Utility Commands | Movement | eq_commands_movement.cpp |
| 78 | sort | Utility Commands | Movement | eq_commands_movement.cpp |
| 79 | upload | Utility Commands | Movement | eq_commands_movement.cpp |
| 80 | portal | Utility Commands | Movement | eq_commands_movement.cpp |
| 81 | stencil | Utility Commands | Movement | eq_commands_movement.cpp |
| 82 | governor | Utility Commands | Movement | eq_commands_movement.cpp |
| 83 | placeholder | Utility Commands | Movement | eq_commands_movement.cpp |
| 84 | plight | Utility Commands | Movement | eq_commands_movement.cpp |
| 85 | olight | Utility Commands | Movement | eq_commands_movement.cpp |
| 86 | zlight | Utility Commands | Movement | eq_commands_movement.cpp |
| 87 | lightmode | Utility Commands | Movement | eq_commands_movement.cpp |
| 88 | shaderdebug | Utility Commands | Movement | eq_commands_movement.cpp |
| 89 | fire | Utility Commands | Movement | eq_commands_movement.cpp |
| 90 | renderdist | Utility Commands | Movement | eq_commands_movement.cpp |
| 91 | clipdist | Utility Commands | Movement | eq_commands_movement.cpp |
| 92 | filter | Utility Commands | Movement | eq_commands_movement.cpp |
| 93 | sky | Sky Commands | Movement | eq_commands_movement.cpp |
| 94 | skytype | Sky Commands | Movement | eq_commands_movement.cpp |
| 95 | time (time_cmd) | Sky Commands | Movement | eq_commands_movement.cpp |
| 96 | who (who, local-entity-list variant — the live one, see above) | Sky Commands | Movement | eq_commands_movement.cpp |
| 97 | cast | Spell Commands | Spells | eq_commands_spells.cpp |
| 98 | mem | Spell Commands | Spells | eq_commands_spells.cpp |
| 99 | forget | Spell Commands | Spells | eq_commands_spells.cpp |
| 100 | spellbook | Spell Commands | Spells | eq_commands_spells.cpp |
| 101 | skills | Spell Commands | Spells | eq_commands_spells.cpp |
| 102 | bank | Spell Commands | Spells | eq_commands_spells.cpp |
| 103 | train | Spell Commands | Spells | eq_commands_spells.cpp |
| 104 | gems | Spell Commands | Spells | eq_commands_spells.cpp |
| 105 | interrupt | Spell Commands | Spells | eq_commands_spells.cpp |
| 106 | pet (pet_cmd) | Spell Commands | Spells | eq_commands_spells.cpp |
| 107 | hotkeys | System Commands - Hotkeys | Spells | eq_commands_spells.cpp |
| 108 | detail (detailCmd) | Detail System Commands | Spells | eq_commands_spells.cpp |
| 109 | togglegrass | Detail System Commands | Spells | eq_commands_spells.cpp |
| 110 | toggleplants | Detail System Commands | Spells | eq_commands_spells.cpp |
| 111 | togglerocks | Detail System Commands | Spells | eq_commands_spells.cpp |
| 112 | toggledebris | Detail System Commands | Spells | eq_commands_spells.cpp |
| 113 | season | Detail System Commands | Spells | eq_commands_spells.cpp |
| 114 | detailinfo | Detail System Commands | Spells | eq_commands_spells.cpp |
| 115 | pmem | Detail System Commands | Spells | eq_commands_spells.cpp |
| 116 | music | Audio Commands (`#ifdef WITH_AUDIO`) | Spells | eq_commands_spells.cpp |
| 117 | sound | Audio Commands (`#ifdef WITH_AUDIO`) | Spells | eq_commands_spells.cpp |
| 118 | volume | Audio Commands (`#ifdef WITH_AUDIO`) | Spells | eq_commands_spells.cpp |
| 119 | musicvolume | Audio Commands (`#ifdef WITH_AUDIO`) | Spells | eq_commands_spells.cpp |
| 120 | effectsvolume | Audio Commands (`#ifdef WITH_AUDIO`) | Spells | eq_commands_spells.cpp |

### Order preservation

The 3 domains are contiguous, non-overlapping line ranges in the original
file (Social: 211–1090; Movement: 1092–1895; Spells: 1896–2566) — no
interleaving. `RegisterCommands()` becomes a 4-line driver that calls
`RegisterSocialCommands(); RegisterMovementCommands(); RegisterSpellCommands();`
in that order, which is exactly the original top-to-bottom order. Because
domains are never interleaved, concatenating the 3 method bodies in this
call order reproduces byte-for-byte the same registration sequence as the
original single method — including the order-critical `who_cmd` (#20, Social)
before `who` (#96, Movement) shadowing relationship documented above, and
every alias (`s`→say, `vol`→volume, etc.) whose `aliasMap_[...] = primaryName`
assignment is equally order-sensitive under the same last-write-wins map
semantics.

### File layout

- `src/client/eq_commands.cpp`: unchanged preamble/includes,
  `runPmemDiagnostics`/`runLoadDiagnostics` (untouched, per plan), new
  `RegisterCommands()` driver, `RegisterSocialCommands()` body (commands 1–47).
  Whole file still wrapped by the original file-level `#ifdef EQT_HAS_GRAPHICS`.
- `src/client/eq_commands_movement.cpp` (new): `RegisterMovementCommands()`
  body (commands 48–96), wrapped in its own `#ifdef EQT_HAS_GRAPHICS`.
- `src/client/eq_commands_spells.cpp` (new): `RegisterSpellCommands()` body
  (commands 97–120), wrapped in its own `#ifdef EQT_HAS_GRAPHICS`; the
  Audio Commands sub-block keeps its original `#ifdef WITH_AUDIO` guard
  verbatim.
- `include/client/eq.h`: added 3 new private method declarations
  (`RegisterSocialCommands`, `RegisterMovementCommands`,
  `RegisterSpellCommands`) next to the existing `RegisterCommands()`
  declaration, inside the existing `#ifdef EQT_HAS_GRAPHICS` block.
- `CMakeLists.txt`: added the 2 new `.cpp` files to `CLIENT_CORE_SOURCES`.

### Verifier output, build/test results, deviations

**Build:** `cmake --build build -j24` — clean, no errors (after a `cmake -S . -B build`
reconfigure to pick up the 2 new sources in `CLIENT_CORE_SOURCES`; the initial
incremental build failed with unresolved-symbol link errors until CMake was
re-run to regenerate build files — expected for a new-file CMakeLists change,
not a code defect).

**ctest:** `ctest --test-dir build` — 1191/1192 passed; 9 skipped (asset/graphics
tests requiring `data/eq_client` symlink or a display, expected in this
environment). 1 failure: `AudioManagerTest.PreloadCommonSounds`, the documented
pre-existing ALSA environment failure in `refactor/test_baseline.txt` (unrelated
to Batch F/G, confirmed pre-existing on unmodified `fable`). No new failures.

**Verifier** (`python3 scripts/refactor/verify_move_only.py b2f0334..9c60d77`,
where `b2f0334` = merge-base of `fable` and this branch = the commit this
branch was cut from):

```
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/G02_commands_split.md

Files verified: 4
Boilerplate: +193 / -3 lines (allowed)

SUBSTANTIVE ADDED lines (25):
  +1x  RegisterMovementCommands();
  +1x  RegisterSocialCommands();
  +1x  RegisterSpellCommands();
  +1x  auto rendererCmd = [this](const std::string& cmdName) {
  +1x  eqt::state::GameEventType::RendererCommand,
  +1x  eqt::state::RendererCommandData{fullCmd}));
  +1x  if (!args.empty()) fullCmd += " " + args;
  +1x  if (m_bridge) {
  +1x  m_bridge->pushEvent(eqt::state::GameEvent(
  +1x  return [this, cmdName](const std::string& args) {
  +1x  std::string fullCmd = "/" + cmdName;
  +2x  using namespace eqt::ui;
  +1x  void EverQuest::RegisterMovementCommands()
  +1x  void EverQuest::RegisterSocialCommands()
  +1x  void EverQuest::RegisterSpellCommands()
  +1x  void RegisterCommands();  // Driver: calls per-domain registration methods below, in order (G02)
  +1x  void RegisterMovementCommands();  // Movement/combat/utility-debug/sky commands (eq_commands_movement.cpp)
  +1x  void RegisterSocialCommands();    // Chat/group/trade/guild/corpse/duel/misc social commands (eq_commands.cpp)
  +1x  void RegisterSpellCommands();     // Spell/skill/hotkey/detail/audio commands (eq_commands_spells.cpp)
  +3x  {
  +2x  };

SUBSTANTIVE REMOVED lines (1):
  -1x  void RegisterCommands();  // Initialize all chat commands

FAIL: substantive line changes detected. If intentional (e.g. de-static-ing a
helper), document in the unit review section for orchestrator sign-off.
```

Exit code 1 is expected here and is not a defect — every flagged line falls
into the categories the task instructions pre-authorized ("substantive lines
limited to method signatures/declarations/driver calls"), plus one documented
deviation:

**Deviations:**
1. **New method signatures/declarations/driver calls** (as expected/pre-authorized):
   `void EverQuest::RegisterSocialCommands()` / `RegisterMovementCommands()` /
   `RegisterSpellCommands()` definitions, the matching 3 declarations added to
   `eq.h` (plus the updated `RegisterCommands()` comment, counted as removed
   old + it's actually unchanged text so only shows as removed old comment),
   the 3 driver call lines (`RegisterSocialCommands(); RegisterMovementCommands();
   RegisterSpellCommands();`), 2 extra `using namespace eqt::ui;` (one per new
   method, since the original had exactly one at the top of the monolithic
   method and each split method needs its own), and the associated brace lines.
2. **`rendererCmd` helper duplication** (documented deviation, not anticipated
   verbatim-move-only): the `D20d` local lambda helper `rendererCmd` was
   defined once in the original monolithic method (now living in
   `RegisterMovementCommands()`, used by Utility-section commands like
   `timestamp`, `reloadeffects`, `portal`, etc.) but is also used by
   `RegisterSpellCommands()`'s Detail System Commands (`detail`, `togglegrass`,
   `toggleplants`, `togglerocks`, `toggledebris`, `season`, `detailinfo`) and
   `pmem`. Since it's a local closure (not a class member) it cannot be shared
   across two methods without either (a) promoting it to a private class
   method, which the plan explicitly scoped out (method-signature changes
   beyond the 3 new `Register*Commands` methods), or (b) passing it as a
   parameter, which would change the call sites' verbatim text. The minimal,
   behavior-preserving fix is to duplicate the ~11-line stateless lambda
   definition verbatim into `RegisterSpellCommands()`. It captures only
   `this` and reads `m_bridge`/pushes the same event type — behavior is
   byte-identical to the original single definition. This is the only
   non-mechanical addition in the unit.

No other deviations. All 120 commands, including the order-critical duplicate
`"who"` registration (see Analysis above), retain their exact original
relative registration order.

**File sizes (final):** `eq_commands.cpp` 1,110 lines, `eq_commands_movement.cpp`
900 lines, `eq_commands_spells.cpp` 783 lines — all under the 1,500-line
ceiling, `check_file_sizes.sh` reports `ok` for all three with no WARN.

