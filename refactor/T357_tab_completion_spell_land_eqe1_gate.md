# T357 — Tab completion reached (B057), dead spell-land hook deleted (B061), EQE1 preload gate (B014)

Class B, slot1. Three disjoint items; one branch, three commits.

## Leg A — B057 tab completion

`CommandProcessor::getCompletions` and `CommandRegistry::getCompletions` are
both implemented and tested and have ZERO production callers. Will's rule is
functionality first, and both CLAUDE.md files advertise tab completion, so
WIRE it on the path a user reaches:

1. Find the in-game chat input box's key handling (the Irrlicht UI edit box
   the player types `/commands` into) and record which registry its SUBMIT
   path dispatches through — that is the registry to complete from. If the
   two `getCompletions` are duplicated-then-drifted, say so on the board as a
   new item; do NOT merge them here.
2. On Tab while the box has focus and the text begins with `/`: one match →
   replace the partial command with it (keep the trailing space so the user
   can type arguments); several → print the candidates on one chat line (the
   same output channel `/help` uses); none → nothing. Tab must not move
   Irrlicht focus off the box (consume the event).
3. Test through the same seam the static-panel UI tests use (T54–T56 shape):
   an edit box with text `/gu`, a synthetic Tab key event, assert the text
   afterwards. Pre-fix failing.
4. Update both CLAUDE.md sentences to say where it lives; strike B057.

## Leg B — B061 `SpellManager::setOnSpellLand`

Zero setters tree-wide; the invocation at `spell_manager.cpp:1027` fires on
the animation packet, outside the `is_success_packet` gate — a wrong hook
that T348 did not use (landing is OP_Action effect_flag 4). DELETE the
typedef, setter, member and invocation, and any test that only exercises
the setter. K12 ff check first (no `pushEvent` in the removed lines). Strike
B061.

## Leg C — B014

`RaceModelLoader::preloadModelData` calls
`ConstrainedTextureCache::getOrLoad()` with an empty buffer whenever a JSON
manifest exists but no EQE1 archive is configured — one guaranteed-to-fail
call per composite name. Gate the call on EQE1 being configured (find the
existing "is EQE1 configured" predicate; do not invent a second one). Test:
manifest present, no archive → `getOrLoad` never called (count via the
cache's existing stats/seam, or a source-reader pin if none). Strike B014.
NOTE: `mesh_building.cpp` and `race_model_loader.cpp` were just touched by
T352 (a lock in `getMergedTextures`); you branch from a tip that includes it.

## Proof

Build, `ctest -j6`, the new tests named with pre-fix/post-fix runs, file
size check; Leg A and Leg C touch `src/client/graphics` → visual gate
standalone + ARM build.

STOP: the chat box has no reachable key hook without restructuring the
input bridge (then report the shape and do Legs B and C).

---

## Review

Branch `task/T357-tab-completion-spell-land-eqe1`, base `aa2e55fd`. Three legs,
three commits: Leg A `d7f8de70`, Leg B `16529e54`, Leg C `200180ba`. No merge, no push. No eqemu lock taken (nothing here needed a
server).

### Leg A — B057, tab completion reached

**Step 1, the registry question (the plan's first ask).** The chat box is
**not an Irrlicht `IGUIEditBox`** — `grep -rn "EditBox"` over `src/ include/
tests/` returns nothing tree-wide. It is a hand-rolled `std::string` in
`ChatPanelState` (`include/client/graphics/ui/static_panels.h`), drawn as raw
rects by `renderChatPanel()` and fed by a key queue
`RendererEventReceiver::OnEvent` fills, drained once per frame by
`FrameComponent::processChatInput()` on the **render thread**.

Its submit path: `KEY_RETURN` → `ChatSubmitIntent` →
`BridgeComponent::onChatSubmitIntent` → `ChatComponent::ProcessChatInput`
(game thread), which tries **registry 1**, `eqt::ui::CommandRegistry`
(`ctx_.command_registry->executeCommand`), and on a miss falls through
`SetUnrecognizedCommandFallback` → `Application::dispatchToActionOrRenderer` →
**registry 2**, `action::CommandProcessor`, then **registry 3**, the renderer's
own `SlashCommandsComponent`.

So the box reaches registries 1 **and** 2, and `/help` already merges exactly
those two (`eqt::help::BuildHelpResponse` takes both). Completing from registry 1
alone would have offered a **different** set than `/help` prints — a defect on
its own — so the provider merges both. **Flagged as a deliberate widening of the
plan's step 1**, which named one registry. Registry 3 has no `getCompletions()`
to ask.

> **CORRECTED by T365 (B091), 2026-09-05.** The last sentence was true and the
> conclusion drawn from it was not. `/help` does NOT merge "exactly those two":
> `BuildCategoryOverview` also appends registry 3's whole Graphics category from
> `AllRendererCommands()`. So `/help` listed THREE registries while Tab offered
> two, and the ~70 renderer commands were not tab-completable at all — the exact
> divergence this paragraph says was designed out. T365 gave the static catalog
> a completion source (`eqt::help::RendererCommandCompletions`, the same shared
> helper) and `Application::commandCompletions()` unions all three. The claim
> holds now; it did not when it was written.

Both dead functions are now called: `Application::commandCompletions()` unions
`CommandRegistry::getCompletions()` with `CommandProcessor::getCompletions()`,
sorts and de-duplicates.

**Step 1's other ask** — duplicated-then-drifted? Yes, and boarded as **B069**
rather than merged (as instructed). Two independent implementations over two
different maps, neither delegating, sharing the same
`name.find(lowerPartial) == 0` idiom. The drift is one line: `CommandProcessor`
de-duplicates after sorting, `CommandRegistry` does not, so a `CommandRegistry`
command also registered as its own alias comes back twice. The union's own
de-duplication hides it from this caller; another caller would still see it.

**The seam.** `RendererContext` has no registry and must not learn one, so
`Application` installs one closure:
`IrrlichtRenderer::setCommandCompletionProvider()` →
`FrameComponent::setCommandCompletionProvider()`, wired in
`initRendererAndGraphics()` one line after `setBridge()`. Lock-free by
construction: both command maps are filled during `EverQuest` construction /
`initActionLayer()`, before the render loop starts, and nothing registers or
unregisters a command afterwards. Commented at all three sites; if a command is
ever registered live it becomes a race and needs a lock.

**Behaviour** (`FrameComponent::applyCommandCompletion`, the new `KEY_TAB` arm
of the drain): one match replaces the partial and appends a space; several print
on ONE System-channel chat line (the render-thread counterpart of
`AddChatSystemMessage`, the same push registry-3 commands use) and leave the
typed text alone; none does nothing. Only a line starting `/` completes, and
only the command word — once the line contains a space the vocabulary is
per-command and we have none, so Tab is a no-op rather than a corruption of the
message being typed.

**Tab does not move focus and does not cycle targets.** No change was needed:
`RendererEventReceiver::OnEvent` already consumes every key except F1–F12 while
`chatInputFocused_`, so Tab never reached `HotkeyAction::CycleTargets` from the
chat box, and there is no widget for Irrlicht focus to move to. Pinned by
`TabStaysInsideTheChatBox`.

### Leg B — B061, dead spell-land hook deleted

Confirmed on the tree before touching it: `SpellLandCallback`,
`setOnSpellLand()` and `m_on_spell_land` existed; the only setter calls
tree-wide were two in `tests/test_spell_manager_timing.cpp`; the invocation at
`spell_manager.cpp:1027` sat at function scope, **below and outside** the
`if (spell && is_success_packet)` gate at 1017, so it fired on the animation
packet too. All four removed.

**K12 `pushEvent` check: clean.** None of the removed lines carried one. The
nearest, `spell_manager.cpp:1012` (the `SpellCastVisualComplete` push), is
twelve lines above inside the `m_bridge` block and is untouched.

Tests: `SpellManagerCallbacks.OnSpellLandReceivesSpellCasterTarget` (setter-only)
deleted; the mixed `HandleActionSuccessPacketCompletesOwnCastAndFiresCallback`
kept, with its `landed` local, setter call and assertion stripped and its
`setOnCastComplete` half intact; the section comment updated.

`setOnSpellFade` deliberately LEFT — its own test documents that it has no
invoker either, and that is a separate question, not this unit's.

### Leg C — B014, EQE1 preload gate

The predicate used is the **existing** one, not a new one:
`constrainedTextureCache_->getConfig().eqe1Path.empty()` — the same field
`renderer_zone_load.cpp` ("Step 0: EQE1 … (if configured)"), `renderer_init.cpp`
and `eqe1_archive_component.cpp` all read, reached through the cache's own
config copy, so no new plumbing.

Deliberately **not** gated on the loader set being non-empty
(`snapshotEQE1Loaders()`, the other candidate — and private): T284/T207 open
packs lazily and a composite sheet's pack is *meant* to still be closed at this
point; the miss is what drives `eqe1ArchiveResolver_` →
`openArchiveContaining()`. "Configured" is the question B014 asks; "already
open" is a different one.

The test is a source-reader pin, as the plan permitted. Reaching the composite
branch of `preloadModelData()` needs a real S3D model, a real manifest and the
shipped EQ tree (`tests/test_palette_mask_plumbing.cpp` does exactly that and
`GTEST_SKIP`s without them), so a counted assertion would not have run here. It
lives next to T92's `EmptyInputReportedDistinctlyFromDecodeFailure`, which
diagnosed this same caller — diagnosis and fix now sit together.

### Tests: pre-fix and post-fix

| Test | Pre-fix | Post-fix |
|---|---|---|
| `ChatTabCompletionTest.TabCompletesAUniquePartialAndLeavesRoomForArguments` | **FAIL** | PASS |
| `ChatTabCompletionTest.TabOnSeveralMatchesListsThemOnOneChatLineAndKeepsTheText` | **FAIL** | PASS |
| `ChatTabCompletionTest.TabWithNoMatchDoesNothingAtAll` | pass (vacuous) | PASS |
| `ChatTabCompletionTest.TabOnPlainChatTextIsANoOp` | pass (vacuous) | PASS |
| `ChatTabCompletionTest.TabAfterTheCommandWordDoesNotTouchTheArguments` | pass (vacuous) | PASS |
| `ChatTabCompletionTest.TabStaysInsideTheChatBox` | pass (vacuous) | PASS |
| `ChatTabCompletionTest.ABareRendererWithNoProviderTreatsTabAsANoOp` | pass (vacuous) | PASS |
| `ApplicationInitOrderTest.ComposedStartupOrderMatchesThePreG04Golden` | **FAIL** | PASS |
| `SpellLandHookIsGone.NoTypedefSetterMemberOrInvocationRemains` | **FAIL** | PASS |
| `Eqe1PreloadGate.CompositeSheetLookupIsGatedOnEqe1BeingConfigured` | **FAIL** | PASS |

Method (no `git stash` — patches, per protocol): each leg's production change
was exported with `git diff > build/T357/leg*_prod.patch`, reverted with
`git apply -R`, the affected test target rebuilt, run, then restored with
`git apply`.

Two reds were captured for Leg A, because the first is stronger but not
behavioural:

1. **Whole-leg revert:** the test file does not COMPILE —
   `'class EQT::Graphics::IrrlichtRenderer' has no member named
   'setCommandCompletionProvider'` and `... FrameComponent ... has no member
   named 'hasCommandCompletionProvider'`. The seam the test drives had no
   production existence at all, which is the literal content of B057.
2. **Surgical revert** (the `KEY_TAB` arm and the one `application.cpp` wiring
   statement removed, the seam kept) so the tests build and fail on behaviour:
   the two rows above, plus the init-order golden.

The four "vacuous" rows are honest and deliberate: they assert Tab does
*nothing* in situations where it must do nothing, so they cannot go red on a
tree that never handled Tab. They are the regression net against a future
over-eager completion (eating a plain sentence, rewriting an argument), not
evidence of this fix.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | **PASS** (EXIT=0) |
| 2. `ctest -j6 --output-on-failure` | **3860 / 3861**; sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA host quirk) |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (EXIT=0) |
| 4. Visual gate | **PASS** (see deviation below) |
| 5. `scripts/build-arm-noble.sh` | **PASS** (EXIT=0) |
| 6. `check_file_sizes.sh fable` | **35 checked, 35 ok, 0 warn, 0 fail** |

Suite count 3861 (up from the board header's 3760 — the board is stale on this,
as its own header says it will be).

### Goldens

One golden changed, and it is a **statement-text order golden learning a
reviewed edit IN PLACE** (G-correction 32), not a re-bless:
`tests/goldens/application_init_order_g04.txt` gains entry **256**
`m_renderer->setCommandCompletionProvider(...)` after entry 255
`m_renderer->setBridge(...)`; entries 256–314 renumber to 257–315; the count
constant in `tests/test_application_init_order.cpp` goes 314 → 315. Both
carry a LEARNED EDITS note naming T357. **No image golden was touched and none
was re-blessed.** That entry is also the reachability net for Leg A: if the
wiring statement is ever dropped, this golden fails.

### Deviations and surprises

1. **The chat box has no Irrlicht edit box at all.** The plan's step 1 assumed
   one ("the Irrlicht UI edit box the player types `/commands` into"). There is
   none in the tree. The STOP condition ("no reachable key hook without
   restructuring the input bridge") did **not** fire — the opposite is true:
   `processChatInput()`'s existing key drain is a clean, already-reachable,
   already-tested hook, and Tab was already being queued and already being
   consumed while chat is focused. The arm is eight lines.
2. **Completing from two registries, not one** — argued above. Widens the
   plan's step 1 deliberately, to keep Tab and `/help` in agreement.
   *(T365/B091: two was still one short — `/help` lists three. All three are
   unioned now; see the correction above.)*
3. **`build/CMakeCache.txt` pointed `EQT_GLES2_IRRLICHT_DIR` at
   `/mnt/projects/fable/main/third_party/irrlicht-gles2-x86`, not slot1's own.**
   Mid-unit, a build failed with `ld: cannot find
   /mnt/projects/fable/main/.../libIrrlicht.a: No such file or directory` — main
   deleted and rebuilt it (mtime 22:50 vs slot1's 21:36), which a
   `--irrlicht-only` bootstrap does. **This is the same hazard class as the
   shared git stash and the shared scratchpad: a slot's build silently depending
   on another checkout's artifact, with a window in which it does not exist.**
   Fixed by reconfiguring against
   `/mnt/projects/fable/wt/slot1/third_party/irrlicht-gles2-x86`, at the cost of
   a full rebuild. Worth checking in the other slots.
4. **A killed `ar` deleted `build/lib/libwilleq_core.a` outright.** One
   `cmake --build --target ...` hit the 600 s tool ceiling (exit 143); the
   post-143 sweep found no zero-byte file, no zero-member archive and no
   non-ELF binary — because the archive was *absent*, which a glob cannot match.
   The protocol already warns about exactly this ("assert the artifacts you
   EXPECT are present FIRST"); this is a second sighting. Rebuilt.
5. **The visual gate failed its first run on a CPU-time budget**, not on pixels:
   `gfaydark: p95 process-CPU ms 16.971 exceeds ceiling 10.672 (baseline 5.336
   x2.0)`. All three legs only ever *remove* per-frame work (Leg C removes a
   guaranteed-to-fail decode per composite name; Leg B removes a null-function
   check; Leg A's arm runs only on a Tab keypress with chat focused), so a 3x
   CPU regression is not attributable to this change. An immediate clean re-run
   on an otherwise idle host gave **VISUAL GATE: PASS** with every zone inside
   budget. Recorded rather than absorbed: `build/T357/gate_run1.log` is the
   failing run, `build/T357/gate.log` the passing one.
6. **B014's call was cheaper than the board implied on this host** — with no
   composite entity path set, the branch is not reached at all. The cost is
   real only for a `--composite-entities` run with a manifest and no EQE1 tree.
   The gate is still correct; the board wording ("one guaranteed-to-fail call
   per composite name") is accurate for that configuration.

### Board

`B057`, `B061` and `B014` struck with their commit hashes. One item added:
**B069** (B), the `getCompletions` duplication Leg A recorded rather than merged.
