# Handback to the perf session

From: testing session (charter `refactor/TESTING_CHARTER.md`).

## Round 1 (T01, 2026-07-28) — both items CLOSED, thank you

Items 1 and 2 below (`EQT_GRAPHICS=OFF` broken; no capture fixtures)
were fixed by **P28** and **P29** respectively. Item 1's fix unblocked
the login/world/zone connection flow, which was the highest-risk gap in
the T01 survey. Item 2's fixtures are now feeding real protocol tests.
The original writeups are kept below for the record — **do not act on
them again.**

Your P29 routed the hardcoded-port-7000 issue back to the testing
session; we own it, it is in flight as **T03a**. Measured detail if
useful: the fixture has no port-7000 traffic at all — zone sessions are
on 7012 (befallen, 920 pkts) and 7026 (qeynos2, 3172 pkts), and the
`server_ports` set in `tests/pcap_test_utils.h` misclassifies 4,092 of
5,603 packets as a result.

## Round 2 (T02 merge verification, 2026-07-28) — one NEW item

**The canonical configure command in the root `CLAUDE.md` "Build
Commands" does not work as written.**

`CLAUDE.md` documents:

```
-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
```

but `CMakeLists.txt:94` builds the library path from it and tests it
with `EXISTS`, which CMake defines only for absolute paths. With the
documented relative form, configure fails the patched-Irrlicht check
and dies with the `FATAL_ERROR` that tells you to build the very
library you already built:

```
EQT_GRAPHICS=ON requires a locally-built patched GLES2 Irrlicht —
desktop-GL Irrlicht (system libirrlicht-dev / X11-GLX) support was
removed (P30, ...)
```

Reproduce: bootstrap with
`bash scripts/build-gles2-x86.sh --irrlicht-only`, confirm
`third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` exists, then
configure with the relative path exactly as documented. Passing an
absolute path works.

Both the T02 executor and I hit this independently and worked around
it. Since P30 made this the only tree, this is now the documented
entry point for every new checkout and every executor slot. Suggested
fix: make the path absolute inside `CMakeLists.txt`
(`get_filename_component(... ABSOLUTE)` against `CMAKE_SOURCE_DIR`)
so the documented relative form works, rather than only correcting the
doc. Root `CMakeLists.txt` and `CLAUDE.md` are outside testing
territory, hence handed over.

Also worth folding into the same fix: the bootstrap leaves the archive
at both `lib/libIrrlicht.a` and `lib/Linux/libIrrlicht.a`, and only the
former is what `CMakeLists.txt` looks for. Not a bug today, but a trap
if the script's copy step ever changes.

**Withdrawn from an earlier round:** the T02 executor reported X11 link
failures in the GLES2 tree (`test_graphics`, `test_asset_loading`,
`test_door_loading` + five tools, undefined Xlib references). On
re-verification against the current tip, a full unscoped
`cmake --build build -j24` completes with **0 errors**. P30 removed the
offending paths. Ignore that report.

---

## ROUND 1 ITEMS BELOW — CLOSED, RETAINED FOR RECORD ONLY

Original context: these were handed back rather than fixed because both
sat inside the perf session's territory (the graphics boundary and the
live-eqemu / capture workflow), and because item 1 was far larger than
the testing charter's "minimal, behavior-preserving seam" allowance.

---

## Item 1 — `EQT_GRAPHICS=OFF` does not compile

### Reproduce

```
cmake -S . -B build-nogfx -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON
cmake --build build-nogfx -j24 -- -k
```

351 errors across 12 TUs. Distribution:

| errors | file |
|-------:|------|
| 33 | `src/client/eq_zone.cpp` |
| 31 | `src/client/eq.cpp` |
| 29 | `src/client/eq_chat.cpp` |
| 14 | `src/client/eq_entities.cpp` |
| 12 | `src/client/eq_movement.cpp` |
| 7 | `src/client/application.cpp` |
| 3 | `src/client/eq_bridge.cpp` |
| 2 | `src/client/eq_action_handler.cpp` |
| 1 each | `eq_strings.cpp`, `eq_merchant.cpp`, `eq_combat_actions.cpp`, `combat.cpp` |

### Root cause — it is a misclassification, not a missing guard

My first read was "headers `#ifdef` members out, `.cpp` files use them
unguarded, so add matching guards." **That diagnosis is wrong, and
acting on it would make things worse.**

The real problem is that `include/client/eq.h` has five
`#ifdef EQT_HAS_GRAPHICS` blocks (note: the macro is `EQT_HAS_GRAPHICS`,
while the CMake option is `EQT_GRAPHICS` — worth confirming that
indirection is intentional), and they enclose a large amount of code
that has nothing to do with graphics:

| eq.h lines | size | contents |
|-----------|-----:|----------|
| 73–110 | 36 | fwd decls: `IrrlichtRenderer`, but also `InventoryManager`, `CommandRegistry`, `HotbarButtonType`, `PendingHotbarButton` |
| 765–769 | 3 | pending-hotbar-button accessors |
| 1032–1081 | 48 | `InitGraphics`/`ShutdownGraphics` — but also **`GameTick`**, `PreRenderTick`, `PostRenderTick`, `SetEQClientPath`, `GetEQClientPath`, `SetConfigPath`, `SaveHotbarConfig` |
| 1563–1565 | 1 | `m_pending_hotbar_buttons` |
| 1689–1853 | **163** | `m_eq_client_path`, `m_config_path`, **`m_quit_requested`**, `m_zone_load_requested`, `m_hotbar_model`, `m_inventory_manager`, `m_command_registry`, `RegisterCommands` + all per-domain command registration, and ~82 more decls |

`GameTick(float)` — the game-thread tick, the core of the threading
model — is inside a graphics guard. So is `m_quit_requested`, the
inventory manager, the hotbar model, the command registry, and the EQ
client path. None of these are graphics.

That is why the errors look the way they do:

- `'m_quit_requested' was not declared in this scope` ×52
- `'class EverQuest' has no member named 'GameTick'`
- `'m_inventory_manager' was not declared in this scope`
- `'CloseVendorWindow' / 'IsBankWindowOpen' was not declared in this scope`

A second, smaller category is genuinely missing includes/decls when
graphics is off — `'eqt::ui' has not been declared` ×17 (chat channel
enums), `'eqt::state::EntityPoseState' has not been declared` ×5,
`'Graphics' is not a member of 'EQT'` ×75 (uses of
`EQT::Graphics::…` types in non-graphics code paths).

A third, in `application.cpp` only: `GameStateBridge` is an incomplete
type at the headless-bridge attach site (`application.cpp:540`, the
D25 "attach console bridge in headless mode" block), so the
`ConsoleBridge*` → `GameStateBridge*` assignment fails even though
`ConsoleBridge` does publicly derive from `GameStateBridge`
(`console_bridge.h:16`). This looks like a missing include rather than
a design problem.

### What the fix is

Move the genuinely non-graphics members **out** of the
`EQT_HAS_GRAPHICS` blocks, leaving only renderer-facing API inside
(`InitGraphics`, `ShutdownGraphics`, `IrrlichtRenderer`,
`ConstrainedRendererConfig`, the `EQT::Graphics::*` types). The
`.cpp` files then need no guards at all — which is the correct end
state, since `eq.cpp` is supposed to have only two graphics includes
per the architecture notes in `CLAUDE.md`.

The 163-line block at 1689–1853 is the bulk of the work and the one
that needs judgment: it mixes real graphics state
(`m_graphics_initialized`, `m_constrained_config`) with plain game
state. Splitting it is the unit.

### Why the testing session cares

This is the single biggest obstacle to base-layer coverage. Because
these TUs cannot compile without the renderer, **~33k lines of
base-layer source are in no test binary at all** — all 18 `eq*.cpp`
(21,351 lines), `combat.cpp`, `inventory_manager.cpp`,
`spell_manager.cpp`, `skill_manager.cpp`, `application.cpp`,
`hotkey_manager.cpp`. Their 0% coverage is not even in the
denominator of the 24.0% headline number.

Concretely blocked: the login/world/zone connection flow, which is
backlog item 4 in `testing_progress.md` and the highest-risk untested
area in the client (zone-load hangs are a known recurring failure).

Three test binaries also fail to build as a direct consequence:
`test_titanium_opcodes`, `test_eq_client`, `test_session_replay`.

### Note on scope

I am **not** asking for a full headless build as a supported product
configuration. What base-layer testing needs is that these TUs can be
compiled and linked without the renderer. If a cheaper shape achieves
that (e.g. a link-time null renderer rather than compile-time
exclusion), it serves the same purpose — your call, you own the
boundary.

---

## Item 2 — no capture fixtures exist, so ~50 protocol tests are inert

`data/captures/` does not exist in the fable checkout. Every pcap and
session-replay test therefore skips. Paths the tests look for:

| fixture | consumers | override |
|---------|-----------|----------|
| `data/captures/session.pcap` | `test_daybreak_connection_pcap`, `test_daybreak_harness`, `test_pcap_extractor`, `test_pcap_step_through` (via `tests/pcap_test_utils.h:37`) | `WILLEQ_PCAP` |
| `data/captures/session.sleq` | `test_session_replay` (`test_session_replay.cpp:240`) | `WILLEQ_SESSION_LOG` |
| `data/captures/session.golden` | `test_session_replay` — self-creates on first run | — |

### Impact

**`src/common/net/daybreak_connection.cpp` — 1195 lines, the reliable
UDP protocol core of the entire client — measures 0.0% line coverage.**
Sequencing, fragmentation, CRC, compression, retransmit: all untested
in practice. The tests that would exercise it are written and
building; they have no data to run on.

This is the highest-blast-radius gap in the codebase.

### What is needed

One recorded play session against live eqemu:

```
./build/bin/willeq -c <config> --record-session data/captures/session.sleq
```

plus a pcap of the same session for the protocol-level tests. A short
session that covers login → world → zone-in → some movement/chat would
activate the whole family. `test_session_replay` writes its own golden
on first run, so no blessing step is needed.

This is handed to the perf session because it needs live eqemu, which
the testing session treats as unavailable by default (Will, 2026-07-28:
plan for replay/synthetic fixtures only). Note the eqemu server was up
and in use by another session as of 2026-07-28 with **no**
`/mnt/projects/fable/.eqemu-lock` present — the lock protocol is not
currently being honored, so coordinate before recording.

The testing session is not blocked on this: T04 as proposed builds
synthetic Daybreak fixtures precisely so protocol coverage does not
depend on a capture that may never be recorded. A real capture would
be strictly better, and would additionally light up ~50 existing tests
for free.

---

## Coordination gap worth fixing

`TESTING_CHARTER.md:89-90` says the perf session reads
`refactor/testing_progress.md`, but nothing in the perf session's own
docs points at it — `refactor/deferred.md` and `refactor/progress.md`
contain no reference to the testing tracker or charter. The reverse
direction is well-specified (the testing session must read
`deferred.md` before every batch).

So the handoff channel is write-only on the testing side. Will relayed
these two items directly this time. A standing fix would be a pointer
line in `deferred.md`, which is the doc the perf session actually
reads before every batch — that file is outside the testing session's
territory, so it needs to come from your side or from Will.
