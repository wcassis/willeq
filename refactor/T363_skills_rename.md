# T363 — `/newskills` becomes `/skills` (B058, Will's ruling 2026-09-05)

Class B, any slot. Read the B058 board text.

1. Rename the slash command `/newskills` (`renderer_slash_commands.cpp:422`)
   to `/skills`; REMOVE `/newskills` entirely (no alias — Will's ruling).
   The body that survives is the fuller one: toggle the skills popup, close
   the spellbook and inventory popups, echo to chat.
2. The bare `skills` keyword on piped stdin (`console_input_handler.cpp:432`
   → `InputAction::ToggleSkills` → `renderer_frame_input.cpp:222`) must end
   up in the SAME body — route the intent to the one implementation so the
   two paths cannot drift again (measure that the popup-closing behaviour
   is now reached from stdin too; if `ToggleSkills` has other callers, the
   compiler finds them).
3. `/skills` is one of the names `/help` and tab completion (T357) list —
   check both show it and neither shows `/newskills`. The command
   registration order is pinned by the G03 golden
   (`tests/goldens/command_registration_order_g03.txt`): it learns the
   rename IN PLACE (correction 32), both faults re-planted.
4. Tests, pre-fix failing: `/skills` toggles the popup and closes the other
   two; `/newskills` is unknown; the stdin keyword closes the other two.
5. Every text mention of `/newskills` tree-wide (docs, CLAUDE.md files,
   help strings, tests) — grep and fix; `DocCitations` and the help golden
   if any.
6. Strike B058.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, G03 golden,
file sizes; renderer files change → visual gate standalone + ARM (rebuild
all three trees first; willeq.db v6).

STOP: none expected.

## Review

Branch `task/T363-skills-rename`, base `e69410f4`. One commit. No merge, no
push, no eqemu lock (nothing here needed a server). The slot's build cache was
repointed at its OWN `third_party/irrlicht-gles2-x86` before the first build
(it named `main/`); reconfigure exit 0.

### What changed

| File | Change |
|---|---|
| `include/client/graphics/frame/frame_component.h` | new `bool FrameComponent::toggleSkillsWindow()` + its doc comment |
| `src/client/graphics/frame/renderer_frame_input.cpp` | the body; `case RA::ToggleSkills:` is now one call |
| `src/client/graphics/slash/renderer_slash_commands.cpp` | `/newskills` → `/skills`, body delegates, echo kept |
| `src/client/help/renderer_command_catalog.cpp` | catalog row `newskills` → `skills` |
| `src/client/input/console_input_handler.cpp` | the dead `skills` → ToggleSkills interception DELETED |
| `src/client/bridge/irrlicht_bridge.cpp` | comment text |
| `README.md`, `include/client/graphics/ui/CLAUDE.md` | command name |
| 4 test files + `tests/cmake/commands.cmake` | see below |

**The single body is `FrameComponent::toggleSkillsWindow()`**
(`src/client/graphics/frame/renderer_frame_input.cpp`, declared in
`include/client/graphics/frame/frame_component.h`). It toggles
`skillsPopupState_.isOpen` and, on the OPEN branch only, clears
`inventoryState_.activePopup` and `spellbookState_.isOpen`. It returns the new
state so the slash arm echoes without re-reading the member. Same shape as
T294's `IrrlichtRenderer::toggleInventory()`. Two callers, no third:
`case RA::ToggleSkills` (the K keybind, `HotkeyAction::ToggleSkills` →
`RendererEventReceiver` → `processPlayerInput`) and the `/skills` slash arm.

### The plan's step 2 was wrong about the stdin path, and the truth is worse

B058 recorded `console_input_handler.cpp:432 → InputAction::ToggleSkills →
renderer_frame_input.cpp:222`. **That arrow does not exist.**
`eqt::input::InputAction::ToggleSkills` (`include/client/input/input_handler.h`)
and `EQT::Graphics::RendererAction::ToggleSkills`
(`include/client/graphics/renderer_types.h`) are different enums, and the first
has **no consumer at all**. Measured mechanically:

```
InputAction literals produced by console_input_handler.cpp
  MINUS those consumed by input_action_bridge.cpp
  = ToggleInventory, ToggleSkills, ToggleGroup   (+ Count)
```

`InputActionBridge::processDiscreteActions()` has an arm for every other
action it produces and none for these three. So the bare `skills` word on
piped stdin **did nothing whatsoever** — it did not toggle the popup, which is
what B058 claimed it did. The fix is a deletion, not a re-route: with the
interception gone the word falls through to `m_rawCommands`, which
`Application`'s unified dispatcher (`application.cpp:463`) hands **verbatim,
with no leading slash** to `DispatchSlashCommand`, which accepts an optional
`/`, misses registries 1 and 2, and is classified by registry 3's static
catalog — which now holds `skills`. So the keyword reaches the one body, and
the popup-closing behaviour with it. `inventory`/`inv` and `group` are dead in
exactly the same way and were left alone (out of territory) → **B090**.

### The G03 golden was NOT touched, and could not be

The plan said the rename is pinned by
`tests/goldens/command_registration_order_g03.txt` and must learn it in place.
**It is not in that golden.** G03 pins
`CommandProcessor::registerBuiltinCommands()` — registry 2 — extracted from
`command_processor_registration.cpp`; `grep skills` over the file returns
nothing (`useskill` at entry 028 is a different command).
`/newskills` was a **registry 3** name, which lives in
`src/client/help/renderer_command_catalog.cpp` and has no order golden.
**Deviation, stated: no golden file was edited, and none needed to be.** All
four `CommandRegistrationOrderTest` cases pass unchanged, and
`git status tests/goldens/` is empty.

What DOES pin the registry-3 rename is
`RendererCommandCatalogTest.MatchesRendererIfChainExactly` (T223): it parses
`renderer_slash_commands.cpp` as text and asserts its `cmd == "/x"` literal set
equals the catalog. Renaming one without the other fails there; it passes.

### Step 3: /help lists it, tab completion does not — and never did

- **`/help`**: `/skills` is listed. `BuildCategoryOverview` appends registry 3's
  whole Graphics category, and `/help graphics` prints all ~70 with `/skills`
  among them and no `/newskills` (`T363_SkillsIsListedByHelpUnderGraphics`,
  `T223_OverviewListsGraphicsCategoryWithTag`,
  `T223_GraphicsCategoryDetailTagsEveryLine`).
- **BUT `/help skills` no longer reaches the command's own detail block.**
  `BuildHelpResponse` resolves a bare argument as a CATEGORY before a command
  name, and registry 2 has a `"Skills"` category (bandage/train/trainclose/
  track). So `/help skills` prints that category. The command works; only its
  help topic is shadowed. Not fixed here — inverting the order would shadow
  every category that shares a name with a command, wider than this unit.
  Boarded as **B092** and pinned as it stands by
  `T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand`.
- **Tab completion**: `/skills` is NOT completable, and `/newskills` was not
  either. `Application::commandCompletions()` unions
  `CommandRegistry::getCompletions()` and
  `CommandProcessor::getCompletions()` — registries 1 and 2 only; registry 3
  has no `getCompletions()`. **The rename changed nothing here**, but it
  refutes T357's Review claim that "Tab and `/help` can never list different
  commands" (repeated in two `CLAUDE.md` files): `/help` lists three
  registries, Tab two. Boarded as **B091**.

### Tests — 11, all failing before, all passing after

Proved by exporting the production-only diff (`git diff -- src include`),
`git apply -R`-ing it, rebuilding the three binaries, running, then
re-applying (never `git stash` — it is shared across slots).

| Test | Pre | Post |
|---|---|---|
| `SlashCommandsComponentTest.SkillsCommandTogglesThePopupAndClosesTheOtherTwo` | FAIL | PASS |
| `SlashCommandsComponentTest.NewSkillsIsNoLongerARendererCommand` | FAIL | PASS |
| `SlashCommandsComponentTest.ToggleSkillsActionClosesTheOtherTwoPopupsToo` | FAIL | PASS |
| `HeadlessCommandRefusalTest.KnownRendererCommandIsRefusedNotQueued` | FAIL | PASS |
| `HeadlessCommandRefusalTest.NewSkillsIsNoLongerRecognizedByAnyRegistry` | FAIL | PASS |
| `HeadlessCommandRefusalTest.BareSkillsKeywordIsClaimedByTheRendererRegistry` | FAIL | PASS |
| `HeadlessCommandRefusalTest.ConsoleHandlerNoLongerInterceptsTheSkillsKeyword` | FAIL | PASS |
| `MergedHelpTest.T363_SkillsIsListedByHelpUnderGraphics` | FAIL | PASS |
| `MergedHelpTest.T363_NewSkillsIsUnknownToHelp` | FAIL | PASS |
| `MergedHelpTest.T223_OverviewListsGraphicsCategoryWithTag` | FAIL | PASS |
| `RendererCommandCatalogTest.LookupIsCaseInsensitiveAndSlashTolerant` | FAIL | PASS |

Pre-fix binaries: `test_slash_commands_component` 3 FAILED,
`test_headless_command_refusal` 4 FAILED (5 passed),
`test_merged_help` 4 FAILED (22 passed).
Post-fix: 9/9, 9/9, 26/26 respectively; `test_frame_component` 16/16.

`T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand` is **not** in that
table on purpose: it documents pre-existing behaviour that the rename newly
EXPOSES, so it passes in both states. Called out rather than smuggled in.

The `ConsoleHandler…` case is a source-text pin, not a behavioural one:
`ConsoleInputHandler::processCommand` is private and its constructor spawns a
stdin-reading thread, which a ctest may not do. The behavioural half of the
same claim is `BareSkillsKeywordIsClaimedByTheRendererRegistry`, which drives
the real `DispatchSlashCommand("skills")` — the exact string the raw-command
path passes. `tests/cmake/commands.cmake` gained `CONSOLE_INPUT_SOURCE` for it.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | **exit 0** (no exit 143 at any point; no post-143 sweep needed) |
| `ctest --test-dir build -j6` | **3888 / 3889**, ctest exit 8 — sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). No re-runs needed; no `visual_gate`/`HeadlessShutdownTest` load artefacts. |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` | **PASS** (standalone; qeynos2 p95 4.911 ms / ceiling 10.108, gfaydark 5.405 / 10.672, befallen 5.320 / 10.948) |
| goldens | untouched — `git status tests/goldens/` empty |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 28 checked, 28 ok, 0 warn, **0 fail** (largest touched: `renderer_frame_input.cpp` 859) |

### Surprises

1. The board's own arrow for the stdin path was fiction; the keyword was inert,
   not merely drifted (above). Three such dead InputActions, not one.
2. G03 does not cover registry 3 at all, so the plan's "learns the rename in
   place" instruction had no object.
3. The new name collides with an existing help CATEGORY. Nothing in the plan or
   the board anticipated that `/skills` was only "free" as a *command* name.
4. `/help` and Tab already disagree by ~70 commands, contradicting T357's
   Review and two `CLAUDE.md` sentences.
