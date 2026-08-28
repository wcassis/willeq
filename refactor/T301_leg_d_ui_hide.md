# T301 Leg D — a UI-hide command

Executor unit, slot3, branch `task/T301-legCD` (with Leg C), cut from `fable`
@ `ffb7e4ca`.

## The premise was half wrong, and that is the finding

The contract and `deferred.md` item 17 both say "fable has no F10 equivalent".
fable HAS one: `IrrlichtRenderer::toggleAllUI()` (`renderer_debug.cpp:41`),
bound to **F11** in the global defaults (`hotkey_manager.cpp:447`), reachable
through `HA::ToggleAllUI -> RA::ToggleAllUI` (`irrlicht_renderer.cpp:301`) and
`processCommonInput()` (`renderer_frame.cpp:565`). It had two problems:

1. **It did not hide the UI.** `allUIHidden_` gated the three things that
   existed when it was written — the entity casting bars, the legacy
   `guienv_->drawAll()` and the FPS counter (`renderer_frame.cpp:1578-1586`) —
   but NOT the static-UI block immediately below them (`:1607`), which draws
   the chat panel, hotbar, spell gems, buff bar, casting bar, group, pet, every
   popup and the XP bar. With the static UI on, F11 hid nothing a capture cared
   about. A cousin of the reachability pattern: a feature with a caller whose
   effect no longer reaches the thing it is named after.
2. **A raw key event is not scriptable.** A `--script` run, or any capture
   harness without an X server, could not press it — the same gap `/camera`
   closed for camera modes in T66, and `/screenshot` for F12 in P10.

## The change

* `renderer_frame.cpp:1607` — the static-UI block is now
  `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)`.
* `irrlicht_renderer.cpp` — `/hideui` (aliases `/toggleui`, `/ui`) in registry
  3, echoing `UI: HIDDEN` / `UI: VISIBLE` through the same `chat()` helper
  every other renderer command uses (so a log grep for `[MAIN]` sees it).
* `irrlicht_renderer.h` — `isAllUIHidden()`, so the command can echo the
  resulting state and the toggle can be asserted headlessly.
* `renderer_command_catalog.cpp` — the catalog entry. Without it
  `RendererCommandCatalogTest.MatchesRendererIfChainExactly` fails, which is
  the existing guard doing its job: registry 3's help catalog is derived from
  the if-chain in both directions.
* `include/client/graphics/ui/CLAUDE.md` — F11 added to the global key table
  (it was undocumented) and `/hideui` added to the registry-3 list.

## How a user reaches it, and what the test proves

| user path | production chain | pinned by |
|---|---|---|
| types `/hideui` (or `/toggleui`, `/ui`) in chat, or a `--script` line | `processSlashCommand()` -> `toggleAllUI()` -> `allUIHidden_` | `SlashHideUiTogglesAndReportsBothWays`, `AliasesReachTheSameToggle` — real `IrrlichtRenderer`, real dispatch, asserts the state AND the chat reply both ways |
| presses **F11** | `HotkeyManager::getAction(KEY_F11)` -> `HA::ToggleAllUI` -> `RA::ToggleAllUI` -> `processCommonInput()` -> `toggleAllUI()` | `F11IsBoundToToggleAllUIInEveryMode` (real defaults, both modes) + `RendererActionFromThatKeyTogglesTheUI` (the real consumer, fed the exact queue entry the key pushes) |

`tests/test_ui_hide_command.cpp`. Neither test touches `toggleAllUI()`
directly — both enter through a path a user occupies, which is the bar the
contract set.

**Stated plainly, not dressed up:** two steps live inside code that needs a GL
device to execute — the key-event switch arm and the render-time gate itself.
Those are pinned by parsing their source (`UIHideSourceInvariants`, the idiom
this suite already uses for single-site invariants). A source pin proves the
line exists; it does not prove a pixel disappeared. The first visual gate that
runs `/hideui` will settle that, and it now can.

## Not done, deliberately

Hidden UI still eats mouse clicks: `staticUIInput_->processInput()`
(`renderer_frame.cpp:1594`) runs regardless of `allUIHidden_`, so a click over
where the hotbar would be still hit-tests it. Leg D changed the DRAW gate only;
the input gate is a behaviour change with its own risk and belongs to whoever
owns the static UI input. Out of scope here, and named so it is not lost.

The static UI's missing scroll input (`static_panels.h:67,340,386`) was named in
the dispatch as explicitly NOT this leg's, and was not touched.

## Verification

Shared with Leg C — see `refactor/T301_leg_c_weapon_visibility.md`'s table.
Suite 3234/3235 (ALSA quirk only), visual gate PASS with goldens untouched,
GLES2 and ARM builds exit 0.
