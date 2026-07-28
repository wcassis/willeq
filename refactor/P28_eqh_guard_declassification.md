# P28 — eq.h graphics-guard misclassification fix

**Slot: slot2. x86 only — no Pi, no eqemu.** Sibling agents: P27
(slot1, headless-segfault test investigation — different files), P29
(slot3, capture recording — no source changes). Territories disjoint.

Executes TESTING_HANDBACK.md item 1 (read it FIRST — it carries the
full diagnosis; trust it, verify as you go). The five
`#ifdef EQT_HAS_GRAPHICS` blocks in `include/client/eq.h` enclose
large amounts of NON-graphics code (GameTick, m_quit_requested,
InventoryManager, CommandRegistry, hotbar model, EQ client path, the
~163-line member block at ~1689-1853). Consequence: `EQT_GRAPHICS=OFF`
fails with 351 errors across 12 TUs, and ~33k lines of base-layer
source can't be compiled into any test binary. The fix direction is
settled (handback + deferred.md): **move non-graphics members OUT of
the guards**, leaving only genuinely renderer-facing API inside
(InitGraphics/ShutdownGraphics, IrrlichtRenderer,
ConstrainedRendererConfig, EQT::Graphics::* types). Do NOT add guards
to the .cpp files — the correct end state is that they need none.

## Work

1. Confirm the macro indirection (CMake option `EQT_GRAPHICS` →
   compile macro `EQT_HAS_GRAPHICS`) and note where it's defined.
2. eq.h: reclassify the five guard blocks. The 163-line member block
   mixes real graphics state (m_graphics_initialized,
   m_constrained_config) with plain game state — split it with
   judgment; when a member is genuinely ambiguous, decide by "does the
   headless client need it to function?" (it began life headless).
   NOTE: this legitimately reverses the hotbar-accessor guards added
   by 55b101e — the types (HotbarButtonType/PendingHotbarButton/
   CommandRegistry/InventoryManager fwd decls, eq.h ~73-110) move OUT
   of the guard; expect knock-on un-guarding in eq_chat.cpp from that
   same commit.
3. Fix the two smaller error categories the handback identified:
   uses of `eqt::ui`/`eqt::state::EntityPoseState`/`EQT::Graphics`
   types in non-graphics code paths (move the decls out or relocate
   the code's types — judgment per case, favor the smallest change
   that keeps eq.cpp's two-graphics-includes architecture), and
   application.cpp's incomplete-type GameStateBridge at the
   headless-bridge attach site (~line 540 — likely a missing include).
4. GOAL STATE: `cmake -S . -B build-nogfx -DEQT_GRAPHICS=OFF
   -DEQT_BUILD_TESTS=ON` configures AND builds clean, and its ctest
   runs (expected: the previously NOT_BUILT test_titanium_opcodes /
   test_eq_client / test_session_replay now build; session-replay
   tests may SKIP for missing fixtures — P29 is recording them
   concurrently, skipping is fine).

## Rules

- Behavior-preserving refactor: the graphics-ON builds must be
  functionally identical. No logic changes, no member renames, no
  "while I'm here" cleanups. Preprocessor-block moves and includes
  only, plus the minimal type-relocations step 3 requires.
- Territory: include/client/eq.h, the 12 failing TUs' guard/include
  lines, application.cpp attach site, include/client headers that
  need decls relocated. NOT: graphics source, driver, tests' logic
  (test CMake fixes only if a link set needs the same backend idiom
  P26 established).
- STOP: a member's classification can't be settled without a design
  decision (e.g. something that LOOKS non-graphics but is entangled
  with renderer lifetime) — list it with analysis rather than
  guessing; partial progress with a clean build still failing on a
  named remainder is an acceptable outcome IF the remainder list is
  short and precise.

## Verification

- Full 6-step bar on the desktop tree (known exceptions per
  protocol) — graphics-ON must be at baseline.
- GLES2 tree full ctest at P26 baseline (1233/1235 + the documented
  P27-in-flight segfault, whatever its state on your branch).
- The NEW bar: build-nogfx builds clean + its ctest result reported
  (expected ~1235 registered now; failures = documented exceptions +
  fixture skips only).
- Visual gate PASS, goldens untouched.

## Review

(executor fills in: per-block reclassification table [stayed guarded /
moved out], the ambiguous calls and their rationale, the three trees'
numbers, deviations)
