# T365 — dead stdin keywords (B090), /help name-vs-category (B092), Tab completes registry 3 (B091)

Class A + B + P, slot2. Three commits. Read the B090/B092/B091 board text
and T363's §Review (the measurement that found them).

## Leg A — B090

`inventory`/`inv` and `group` on piped stdin set `InputAction::ToggleInventory`
/ `ToggleGroup`, and `InputActionBridge::processDiscreteActions` has no arm
for either — they do nothing. Same defect as `skills` (T363), one keyword
over.
1. `inventory`/`inv`: fall through to `m_rawCommands` the way `skills` now
   does. Registry 3's catalog name is `newinv` — the same `new*` naming
   Will ruled against for skills. Apply the same shape: rename `/newinv` to
   `/inventory` in the catalog (remove `/newinv`, no alias), fix every text
   mention, and let `inventory` fall through as a catalog name; `inv` is an
   explicit rewrite to `inventory`. Flag the rename in the Review so Will
   can veto it (it mirrors his B058 ruling; it is not a new call).
2. `group`: find whether ANY consumer for a group-window toggle exists
   (a slash command, a hotkey). If one exists, route the word to it; if
   none, delete the keyword and the dead `InputAction` values (all three:
   ToggleInventory, ToggleSkills, ToggleGroup, once nothing produces them —
   the compiler finds any remaining reader).
3. Tests, pre-fix failing: `inventory`, `inv`, `group` on the console
   handler reach the renderer registry / the consumer (T363's
   `HeadlessCommandRefusalTest.BareSkillsKeywordIsClaimedByTheRendererRegistry`
   is the template).
4. Strike B090.

## Leg B — B092

`/help <arg>` resolves `<arg>` as a category before a command name, so
`/help skills` prints the Skills category and the `/skills` command's own
detail is unreachable. Make it try both: print the command detail first,
then the category (with a one-line "also a category" note), or the
reverse — pick the order that matches how the official client's `/help`
behaves if it has one (it does not have merged categories; so choose
command-first and say so). Update
`MergedHelpTest.T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand`
to the new truth (rename it), pre-fix failing. Strike B092.

## Leg C — B091

`Application::commandCompletions` unions registries 1+2; `/help` lists
three. Give registry 3 (`AllRendererCommands()` / the static catalog) a
completion source through T362's shared helper
(`include/client/command_completion.h`) and union it in, so `/ski<Tab>`
completes to `/skills`. Test, pre-fix failing: a renderer catalog name
completes. Then correct the three claims (T357 Review, the two CLAUDE.md
sentences) to the new truth: Tab and /help share all three registries.
Strike B091.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes;
renderer catalog/graphics files change → visual gate standalone + ARM
(slot2's trees are at willeq.db v6 from T363).

STOP: `/newinv` has semantics beyond toggling the inventory that make the
rename non-mechanical (then leave the name, route `inventory`→`newinv`,
board the naming as class C for Will).

## Review

Branch `task/T365-stdin-keywords-help-completion`, base **`8fd539ed`** (verified
at checkout). Three leg commits plus this one:

| commit | leg |
|---|---|
| `4cb806f5` | Leg A — B090, the dead `inventory`/`inv`/`group` stdin keywords |
| `d7ea0ef4` | Leg B — B092, `/help` resolves a command name before a category |
| `c70501c8` | Leg C — B091, Tab completion covers registry 3 |

No merge, no push, no eqemu lock (nothing here needed a server). The slot's
`build/CMakeCache.txt` was checked at checkout and already points
`EQT_GLES2_IRRLICHT_DIR` at slot2's own `third_party/irrlicht-gles2-x86`
(T363 fixed it). No golden file was touched; `git status tests/goldens/` is
empty.

### Leg A — what happened to `/newinv`, and to `group`

**`/newinv` is renamed `/inventory`, no alias.** Catalog row
(`src/client/help/renderer_command_catalog.cpp`) and the if/else chain branch
(`src/client/graphics/slash/renderer_slash_commands.cpp`) both, plus every
text mention (`README.md`, `include/client/graphics/ui/CLAUDE.md`,
`renderer_entities.cpp`, three test files, `tests/cmake/renderer_guards.cmake`).
Flagged for Will as the plan asked: it mirrors his B058 ruling on `/newskills`,
it is not a new call, and the STOP condition did not fire — `/newinv`'s body is
`owner_.toggleInventory()` and an echo, nothing more, so the rename is
mechanical.

**`inv` is NOT a fall-through, and the plan's shape was right for a reason the
board did not record.** `action::CommandProcessor` registers `inv` as an
**alias of `/invite`** (`command_processor_registration.cpp:171`), and registry
2 is tried *before* registry 3. Letting the word fall through would have
invited a group member instead of opening the bag. `ConsoleInputHandler` now
pushes the literal `"inventory"` into `m_rawCommands` for it — the only place
in the chain that knows which window the user meant. Adding `inv` as an alias
of `/inventory` was rejected for the same reason: registry 2 wins the dispatch,
so the catalog entry would be dead weight that also lies about what the word
does. Pinned from both sides:
`MergedHelpTest.T365_InvIsInvitesAliasNotInventorys` (registry 2 owns it) and
`HeadlessCommandRefusalTest.T365_RegistryThreeDoesNotClaimInv`.

**`group`: no consumer exists, so the keyword is DELETED.** The search was for
any group-window toggle — slash command, hotkey, anything. There is exactly one
candidate and it is empty: `case RA::ToggleGroup:` in
`src/client/graphics/frame/renderer_frame_input.cpp` is a bare `break;` with
the comment *"Group panel visibility is driven by inGroup state"*. That arm is
what the **G keybind** (and the Alt+P alternate, `hotkey_manager.cpp:486-487`)
reaches, so **G does nothing either** — the same empty shape as `RA::TogglePet`
and `RA::ToggleBuffWindow`. There was nothing to route the word to. It now
falls through and is reported unrecognized, which is the truth. Boarded as
**B101** (a key that advertises a toggle it does not have is a separate defect,
and it is not this unit's territory).

`InputAction::ToggleInventory`, `ToggleSkills` and `ToggleGroup` are deleted
from `include/client/input/input_handler.h`; the compiler found the one
remaining reader, `test_input_action_bridge.cpp`'s
`UIToggleActions_AreNotBridgeTerritory`, which now stands on `ToggleVendor`.
**Residue, boarded as B100:** `ToggleVendor`, `TogglePetWindow`, `ToggleTrainer`
and `ToggleSpellbook` are consumer-less in the identical way — but they have no
producer either, so they were never *reachable* dead code the way these three
were, and they are out of B090's territory.

### Leg B — the resolution order chosen, and why

**Command first, then the category.** `BuildHelpResponse` resolves the argument
as a command name; if a registry knows it, the command's detail block prints,
and if the same word is *also* a category the category detail follows a one-line
`"Skills" is also a category:` note. If no registry knows the name, the category
alone prints exactly as before; if neither, `Unknown command: <x>` as before.
`BuildCommandDetail` gained a `bool* found` out-param — it already distinguished
the two cases internally, it just never told the caller.

The plan asked for the order that matches the official client. It has no merged
categories, so there is no oracle for the collision *itself*; there is one for
the un-collided case — `/help <x>` in Titanium takes a command name — and
command-first is the reading that keeps every command's detail reachable by its
own name. Nothing that used to print is lost either way, which is what makes
"print both" better than "invert the order": the reverse collision the board
worried about (a category shadowed by a command) cannot happen.

`T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand` is renamed
`T365_HelpSkillsPrintsTheCommandFirstThenTheCategory` and rewritten to the new
truth, asserting the ORDER (the command block precedes the note), not just the
content.

### Leg C — how registry 3 feeds completion

`eqt::help::RendererCommandCompletions(partial)`, declared in
`include/client/help/renderer_command_catalog.h`, implemented in the catalog TU.
It builds the two lowercase-keyed maps T362's helper wants (once, in a function
static — the catalog is a compile-time constant, so it cannot go stale) and
calls `eqt::completion::PrefixCompletions`. So **all three registries run the
one prefix idiom**: same sort, same de-duplication, same case-insensitivity,
aliases offered the same way. No new TU, no new library edge — the catalog is
deliberately graphics-independent and `command_completion.h` is header-only, so
`test_command_completion` stays a two-TU target.

`Application::commandCompletions()` unions it last, matching the dispatch order
(the sort makes the position cosmetic). **`commandCompletions()` moved from
private to public** — a deviation worth naming: it is what lets the union be
asserted end-to-end through the function the Tab key actually reaches, instead
of being pinned as source text. It is a const query over maps filled during
construction and never mutated afterwards, and it is already exposed to the
renderer as a closure, so it holds no invariant a caller could break.

The three B091 claims are corrected in place, not deleted: the T357 Review
paragraph carries a dated `CORRECTED by T365` block that says what was true
(registry 3 had no `getCompletions()`) and what was false (the conclusion that
Tab and `/help` therefore agreed), its deviation note 2 gets a one-line pointer,
and both `CLAUDE.md` sentences now say all three registries and record that they
used to say "the same pair" and were wrong.

### Tests — pre-fix and post-fix

Every pre-fix run used an exported patch + `git apply -R` (never `git stash`).
The tree was verified byte-identical to the post-fix state after each restore
(`diff <(git diff) t365_full_postfix.patch` → 0).

**Leg A** — pre-fix = `console_input_handler.cpp` + `input_handler.h` reverted
and the two rename lines re-planted (catalog row, if-chain branch):

| test | pre | post |
|---|---|---|
| `HeadlessCommandRefusalTest.T365_BareInventoryKeywordIsClaimedByTheRendererRegistry` | FAIL | PASS |
| `HeadlessCommandRefusalTest.T365_NewInvIsNoLongerRecognizedByAnyRegistry` | FAIL | PASS |
| `HeadlessCommandRefusalTest.T365_ConsoleHandlerNoLongerInterceptsTheThreeUIToggles` | FAIL | PASS |
| `HeadlessCommandRefusalTest.T365_RegistryThreeDoesNotClaimInv` | pass | PASS |
| `HeadlessCommandRefusalTest.T365_GroupKeywordIsUnrecognizedNotSilentlySwallowed` | pass | PASS |
| `MergedHelpTest.T365_NewInvIsUnknownToHelp` | FAIL | PASS |
| `MergedHelpTest.T365_InvIsInvitesAliasNotInventorys` | pass | PASS |
| `MergedHelpTest.T365_HelpInventoryPrintsTheCommandFirstThenTheCategory` | FAIL | PASS |
| `SlashCommandsComponentTest.NewInvCommandReachesTheInventoryPopupState` | FAIL | PASS |
| `InventoryToggleTest.SlashNewinvAndTheKeybindShareOneState` | FAIL | PASS |

The last two are pre-existing cases the rename updates; they are in the table
because they did fail against the re-planted old name, not because they are new
coverage. The two `pass`/`PASS` rows are called out rather than smuggled in:
they pin facts the fix does not change (registry 2 owns `inv`; nothing owns
`group`) and a pre-fix failure there would have meant something else moved.
`RendererCommandCatalogTest.MatchesRendererIfChainExactly` — T223's
statement-text pin, the only golden-shaped thing covering registry 3 (T363
established there is no order golden for it) — was **green in both states**,
because the catalog row and the if-chain branch were renamed together and
re-planted together.

**Leg B** — pre-fix = `merged_help.cpp` reverted, tests untouched:

```
[ RUN      ] MergedHelpTest.T365_HelpSkillsPrintsTheCommandFirstThenTheCategory
  Value of: ContainsSubstring(r.lines, "Registry: renderer (graphics)")
    Actual: false / Expected: true
  Value of: ContainsSubstring(r.lines, "\"Skills\" is also a category:")
    Actual: false / Expected: true
  Expected: (cmdAt) < (noteAt), actual: 5 vs 5
[  FAILED  ] MergedHelpTest.T365_HelpSkillsPrintsTheCommandFirstThenTheCategory
[  FAILED  ] MergedHelpTest.T365_HelpInventoryPrintsTheCommandFirstThenTheCategory
 2 FAILED TESTS   (28 of 30 passed)
```

`T365_ACategoryThatIsNotACommandStillResolvesToTheCategory` passes in both
states by design — it pins preserved behaviour, and a pre-fix failure there
would have meant the change broke plain category lookup. Post-fix: 30/30.

**Leg C** — pre-fix = the `application_commands.cpp` union reverted:

```
[ RUN      ] HeadlessCommandRefusalTest.T365_TabCompletionReachesTheRendererCatalog
  Expected: (std::find(out.begin(), out.end(), "skills")) != (out.end())
  Tab still offers registries 1+2 only; /help lists three
[  FAILED  ] HeadlessCommandRefusalTest.T365_TabCompletionReachesTheRendererCatalog
[  FAILED  ] HeadlessCommandRefusalTest.T365_TabCompletionOffersTheRenamedInventory
 2 FAILED TESTS
```

Post-fix 16/16. **Stated honestly:** the six
`RendererCommandCompletionTest` cases cannot have a pre-fix failing run —
`RendererCommandCompletions` does not exist in the base tree, so reverting it is
a compile failure, not a test failure. The behavioural pre-fix proof for B091 is
the two `HeadlessCommandRefusalTest` cases above, which fail against a tree that
has the function but does not union it. (One of the six,
`TheRenamedInventoryCommandCompletes`, did fail against the Leg A pre-fix tree,
where the catalog still said `newinv`.)

Each leg was also committed against a build in which its own tests were green
and the later legs' code was absent, so no commit in the series is red.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2-config tree) | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,913 of 3,914**, ctest exit 8 — sole failure `AudioManagerTest.PreloadCommonSounds`, the waived ALSA host quirk. No load artefacts, no re-runs. 296 s |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` (standalone) | **`=== VISUAL GATE: PASS ===`** — qeynos2 p95 5.489 ms / ceiling 10.108, gfaydark 5.765 / 10.672, befallen 7.954 / 10.948 |
| goldens | **untouched** — `git status tests/goldens/` empty |
| `bash scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `bash scripts/refactor/check_file_sizes.sh fable` | **22 checked, 22 ok, 0 warn, 0 fail** (largest touched: `renderer_slash_commands.cpp` 600, `application_commands.cpp` 583, `console_input_handler.cpp` 539) |

**Golden tests (merge rule, T337).** The order/coverage goldens for the touched
TUs — `application_init_order_g04` (this unit touches `application.h` and
`application_commands.cpp`), `command_processor_registration`,
`renderer_frame_render`, `renderer_player_physics`, `bridge_component_intents`,
`zone_dispatch` — all ran inside the suite and are green. None learned a
statement: no `pushEvent` and no registration changed. `application_init_order_g04`
pins `setCommandCompletionProvider(...)` in `initRendererAndGraphics()`, which is
untouched — the change is inside `commandCompletions()`, not at the install site.

### Deviations and surprises

1. **`inv` is already `/invite`'s alias.** Neither the plan nor B090 anticipated
   this; the board's suggested "explicit rewrite to `newinv`" turns out to be
   load-bearing rather than cosmetic, because the alternative (fall-through, or
   an `inv` alias on `/inventory`) would have made a bare `inv` line send a
   group invite. Pinned from both sides.
2. **`group` has no consumer AND neither does the G key.** The search for a
   consumer found the empty `RA::ToggleGroup` arm, which means the keyword was
   dead *and* the keybind that shares its name is dead — three empty UI-toggle
   arms in total (`ToggleGroup`, `TogglePet`, `ToggleBuffWindow`). B090 only
   asked about the keyword. → **B101**.
3. **`commandCompletions()` moved to public.** Named above; it is the one API
   change in the unit and it exists to make the B091 fix testable end-to-end
   rather than by source text.
4. **The `/help` collision is not a one-off.** `/skills` vs "Skills" was
   T363's; `/inventory` vs "Inventory" is this unit's, created by Leg A. Any
   future rename toward the real client's vocabulary is likely to hit it again,
   which is the argument for fixing the resolution rather than dodging the name.
5. **Three exit-143 builds.** The full `build/` tree sits right at the 600 s
   ceiling on this host. Each was swept per protocol (zero-byte / non-ELF
   binaries, `ar t` member counts on both archives, `build*/lib/st??????` temp
   files): **bad=0 every time, `libwilleq_core.a` present with 321 members,
   no `ar` temp files** — clean timeout kills, not the T293/J07 truncation
   trap. Every subsequent long step ran detached with a unit-private exit file
   under `scratchpad/t365/` and was waited on with `wait_for.sh`; the one
   0-second MET was sanity-checked against the file's mtime (a build that had
   genuinely finished six seconds earlier) rather than trusted.
6. **`check_file_sizes.sh` reported 22 files, five of which this unit never
   touched** (`race_model_loader.*`, `model_loading_chr.cpp`,
   `renderer_camera.cpp`, `irrlicht_renderer.cpp`,
   `test_daybreak_connection_pcap.cpp`). `fable` moved under the branch while
   the unit ran; the extra files are that drift, and all are `ok`.
