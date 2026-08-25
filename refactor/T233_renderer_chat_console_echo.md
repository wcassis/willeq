# T233 — renderer-registry commands lose their console echo (T142 leftover)

Slot: slot2. x86. NO eqemu. Class A. Territory: the renderer's `chat()`
lambda / slash-command reply path in src/client/graphics/ (irrlicht_renderer*
processSlashCommand reply plumbing) and the console/log echo it should reach;
tests. Do NOT touch combat.cpp, action/*, eq_action_handler (T230 unmerged),
state/tradeskill*, eq_merchant.cpp (T232).

## Defect (T142, confirmed still open by T223's Review)
When a registry-3 (renderer) command replies via the renderer's `chat()`
lambda, the text reaches the in-game chat panel but NOT the console log /
headless-style `CMD outcome=` echo, so scripted runs and the log show
nothing for e.g. `/newskills`, `/fps`, `/wireframe`. T223 fixed the
headless refusal but explicitly left this (different site).

## Scope
1. Find the site: how registry-3 replies are emitted vs how action-layer
   replies are (`AddChatSystemMessage` also logs). Route the renderer reply
   through the same echo so a `--script` run logs it. Keep the chat-panel
   behaviour identical.
2. Test: a renderer command in the graphics test harness (or a unit test on
   the reply plumbing) produces the log/echo line. Red/green measured.
3. Run a short `--script` with `/fps` (or similar) under the normal build
   and show the echo line in the log (no server needed if the command is
   pre-connection safe — check T223's note that pre-connection everything
   but /quit and /help is held; if held, use the graphics test harness).

## Verification
Full ctest; visual gate PASS (renderer touched); Review; evidence
review/T233/. Commit: `T233: renderer command replies echo to the console log`.

## Review (2026-08-25, slot2, task/T233-renderer-echo from fable 81033cf8)

### The site and the fix shape

T223's Review estimated "every chat() call in the renderer if-chain (dozens
of sites)". That was wrong: all ~68 call sites go through ONE lambda,
`auto chat = [this](const std::string& msg)` at the top of
`IrrlichtRenderer::processSlashCommand()` (`irrlicht_renderer.cpp`). It
pushed the SystemMessage and nothing else. The fix is a single
`LOG_INFO(MOD_MAIN, "{}", msg);` in that lambda, after the (bridge-guarded)
push -- same module and shape as `EverQuest::AddChatSystemMessage()`
(`eq_chat.cpp`), so `[MAIN] <reply>` greps now work identically for
registry-1/2 and registry-3 replies. The log echo does not depend on
`bridge_` being set (matching AddChatSystemMessage, which logs even with no
bridge). Chat-panel path untouched; `test_irrlicht_camera_command` still
12/12.

### Test (red/green measured)

`tests/test_renderer_command_echo.cpp` (new, 3 cases, registered in
`tests/CMakeLists.txt` next to `test_irrlicht_camera_command`). Bare,
never-Init()'d IrrlichtRenderer + fake bridge (T66 idiom), `/camera`
(device-free), fd-1 redirected to a temp file (T31 idiom from
`test_logging.cpp`), poll for the line while nudging `FlushThreadLog()`.

| case | red (fix stashed) | green |
|---|---|---|
| ReplyReachesChatPanelAndConsoleLog | FAIL (bridge got it, stdout empty) | OK |
| EchoDoesNotDependOnBridge | FAIL | OK |
| EchoIsGatedByLogLevel (LOG_NONE -> panel yes, log no) | OK (trivially) | OK |

Green run 3x consecutively, 3/3 each (`review/T233/test_green_run{1,2,3}.log`).
Echo line as captured: `[INFO ] [MAIN] Camera mode: Follow`.

Test-side lessons (first green attempt failed 3/3 on the TEST, kept as
`test_green.log`): level names are padded (`[INFO ]`); `ThreadLogBuffer`
only flips sides on append()/flush(), so a lone logged line whose flip was
refused waits for the next append -- the test calls `FlushThreadLog()` in
its poll loop; SetUp drains the previous case's teardown lines first.

### Scope item 3 (--script run)

`review/T233/script_run_no_server.log`: `--script` with `/fps`, `/camera`
against no server. The tour never advances past step 1 (`wait 2.0s`) --
`Application::update()` gates script advance on `m_fullyConnected` (T30),
and the plan forbids eqemu. So the unit test carries the echo evidence, as
the plan's fallback allows. Observation (not in scope): a held `--script`
command emits no `CMD outcome=held` line; T30's held-visibility intercept
is on the piped-stdin path only.

### Verification bar

1. `cmake --build build -j24`: OK (`build_green.log`; reconfigure needed for the new target, `configure.log`).
2. ctest: **2818/2819**, sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA quirk). +3 tests vs the 2815 base implied by fable. `ctest_full.log`.
3. `build-gles2-x86.sh`: OK.
4. Visual gate: **PASS**, goldens untouched (`git status` clean apart from the three unit files).
5. `build-arm-noble.sh`: OK (first invocation hit the 600s tool ceiling mid-compile; re-run completed incrementally, exit 0).
6. `check_file_sizes.sh fable`: 1 FAIL `irrlicht_renderer.cpp` 3059 lines -- PRE-EXISTING (3052 on fable, +7 here, the lambda is the only sane place for it). Not a new FAIL. Note `fable` had moved to `b417ffff` (T232 merge) by the time the check ran, so the script also listed T232's files; all ok.

### Deviations
- None from Scope. Territory respected: only `irrlicht_renderer.cpp` (the lambda), `tests/CMakeLists.txt`, the new test file, this plan.
