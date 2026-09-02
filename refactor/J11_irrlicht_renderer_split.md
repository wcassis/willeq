# J11 — Split irrlicht_renderer.cpp (3,296 → 6 TUs): the last file over the hard cap

**Slot: slot3. Branch `task/J11-irrlicht-renderer-split` cut from `fable`.
x86 only. No Pi. No eqemu.**

**This is the last of the four files over the 3,000-line hard ceiling.** J02
took `eq.cpp` 3,923 → 1,022, J03 `entity_renderer.cpp` 3,244 → 540, J04
`command_processor.cpp` 3,024 → 284. This one clears the list.

**Its blockers are gone:** T312 (the `bridgeQueue_` thread-safety unit) and T313
(the static-UI scroll unit) have both merged, as has T319, which added ~22 lines
to this file. **Re-measure at your tip — it was 3,237 in the survey, 3,274 at
T319's base and 3,296 after.**

## Read first

`refactor/J_file_size_reclamation.md` — **especially the two correction sections
"⚠ TEMPLATE CORRECTIONS FROM J02" and "⚠ FURTHER CORRECTIONS FROM J04"** — and
the Reviews of `refactor/J01_inventory_manager_split.md`,
`refactor/J02_eq_cpp_split.md`, `refactor/J03_entity_renderer_split.md` and
`refactor/J04_command_processor_split.md`. Four units have already paid for the
lessons below; do not re-pay them.

## MOVE-ONLY. No interface changes, no behaviour changes, no method decomposition.

## The proposed split (from the survey — RE-DERIVE every range at your tip)

| cluster | target TU | ≈ lines |
|---|---|---|
| all of `RendererEventReceiver` (its `OnEvent` alone is ~205 lines) | `renderer_event_receiver.cpp` | ~340 |
| ctor/dtor, `initLoadingScreen` (~430), `loadGlobalAssets`, loading-screen show/hide | `renderer_init.cpp` | ~625 |
| the anon-ns camera-mode token parsers + `processSlashCommand` (~438) | `renderer_slash_commands.cpp` | ~560 |
| `setupFog`, `drawLoadingScreen`, `applyEnvironmentalDisplaySettings`, HUD setup/update, paperdoll (+ its anon-ns `appearanceEquals`) | `renderer_hud_env.cpp` | ~625 |
| camera projection, first-person pitch/FOV/dump/reposition, spawn id/race, vision type, object light colours, `setPlayerPosition`, swimming, camera mode, `getCameraTransform` | `renderer_camera.cpp` | ~540 |
| display settings, shutdown, cursor, lighting, time of day, PVS, sky/door/world-object façades, `run`, `saveScreenshot`, frame-timing setters | **stays** `irrlicht_renderer.cpp` | ~980 |

⚠ **`renderer_event_receiver.cpp` is board item 32** — T312 recommended exactly
this TU and deliberately left it to Batch J. Note it in your Review as closed.

## Hazards

- **Two anonymous namespaces** (the camera-mode token parsers, and
  `appearanceEquals` in the paperdoll block). Each must travel with its sole
  consumer — **verify each really has only one, at your tip**. If one is shared
  across your cluster boundaries, it needs a private internal header, and then
  **the verifier will exit 1 by construction** (see below).
- **The preamble is ~97 lines, the largest in the tree.** Each new TU carries
  the original's full include block.
- This file is the subject of T312's mutex invariant: **`bridgeQueue_` has no
  raw access anywhere, only the three locked accessors**. A move must not
  disturb that; say in your Review that you checked.

## The five corrections that cost earlier units real time

1. **`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo /mnt/projects/fable/wt/slot3`** — omitting `--repo` silently verifies `main/` and produces a failure **whose signature is identical to the wrong-base-rev failure**. J02 lost time to exactly this.
2. **Check every cut boundary against block-comment and `#if` nesting before cutting.** J02's UCS range cut inside a `/* */` and would not have compiled. **J03's better rule, which also catches that case: cut at a blank line whose predecessor closes a method, and verify comment depth 0 at the cut.** The "take ranges from the leading banner" rule has no purchase in files without banners.
3. **The line-multiset check is a required second proof** — the verifier compares only *distinct* normalised lines, so a duplicated or dropped range is invisible to it. J03's form (re-concatenate the extracted ranges and `cmp` byte-identical against the base file) is the strongest and is what to copy.
4. **Budget TWO `cmake --build` invocations.** In a three-wide wave the first reliably blows the 600 s tool ceiling; **exit 143 means re-invoke, not breakage.** Same for `build-arm-noble.sh`. J03 measured the build, not `ctest`, as the slowest step.
5. **If a shared helper forces a private internal header, the verifier exits 1 by construction** (`BOILERPLATE` allows `#include`/`#pragma`/`namespace`/`}`/comments, not `inline` or `using`). Paste the full output, enumerate every flagged line, and ask for orchestrator sign-off. **Do not contort the code to force a green exit** — J04 correctly refused.

Also: `grep -n` for the CMake add-site (root variable is `CLIENT_CORE_SOURCES`);
`chmod 644` anything you create.

## Acceptance

Build clean by exit code · `ctest -j6` (**NOT -j24**) zero new failures vs
`refactor/test_baseline.txt` · verifier per correction 1 · the multiset/byte-identical
proof · **visual gate PASS with goldens untouched** — this file owns the frame
loop and the camera, so a moved golden means the split changed behaviour: STOP
and report · `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` ·
`check_file_sizes.sh --files` 0 warn 0 fail on everything changed and created.

⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a known
**load-dependent flake** under concurrent slots — re-run it alone before
reporting it.

## Territory

`src/client/graphics/irrlicht_renderer.cpp`, `CMakeLists.txt`, this file.
**The header `include/client/graphics/irrlicht_renderer.h` is NOT in scope** —
it is J17's, and J17 is gated on this unit.
⚠ Concurrent: slot1 T320 (live group/zone, `eq_chat.cpp`), slot2 T321
(`src/common/net/`, the CRC flag line in `eq.cpp`).
