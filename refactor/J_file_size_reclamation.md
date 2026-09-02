# Batch J — File-size reclamation: get every source file under the 1,500-line soft limit

**Opened 2026-09-02. Goal: no production source file over the 1,500-line warn
ceiling, so that any single file fits comfortably in a coding agent's context.**

Batch letter J because E, F, G, H, P, Q, R, S2 and T are all taken — **H is the
harness batch** (`H_harness.md`, H01-H05), not this.

## Where the tree stands

Measured at `9ae1460c`: **31 files over 1,500 lines; 4 over the 3,000 hard cap** —
`src/client/eq.cpp` 3,913, `src/client/graphics/entity_renderer.cpp` 3,255,
`src/client/graphics/irrlicht_renderer.cpp` 3,237,
`src/client/action/command_processor.cpp` 3,024. Batch F left `eq.cpp` at 3,629
and `irrlicht_renderer.cpp` at 2,736 in 2026-07; both grew back, and
`command_processor.cpp` crossed the hard cap after F/G shipped. T166 already
re-split one file (`renderer_zone_load.cpp`) for exactly this reason.

## The headline finding — this is CHEAPER than it looks

**No file in this tree is decomposition-first for the purpose of the ceiling.**
The largest method anywhere in the oversize set is
`EverQuest::DispatchZonePacket` (`src/client/eq.cpp:800-1679`, 880 lines,
verified), and the largest preamble is 97 lines
(`irrlicht_renderer.cpp`). Since a TU's floor is `preamble + largest method`,
**every oversize .cpp clears 1,500 by pure Batch-F move-only relocation.**
Batch G's shape — restructuring method bodies into helpers — is **not required
anywhere in this batch**, and units must not do it opportunistically.

The eight methods that dominate their TUs are listed in §Optional below as a
SEPARATE, OPTIONAL goal. Do not fold them in.

## The one genuinely hard problem: headers, and it is a god-class problem

Four headers are over the soft limit, in two different shapes:

| header | lines | shape | verdict |
|---|---|---|---|
| `include/common/packet_structs.h` | 2,207 | 125 independent packed structs, already banner-sectioned | **mechanical**, clears fully |
| `include/client/action/action_dispatcher.h` | 1,556 | 3 independent entities (types / `IActionHandler` / `ActionDispatcher`) | **mechanical**, clears fully |
| `include/client/graphics/irrlicht_renderer.h` | 2,395 | 20 free types + one god class | mechanical to ~1,820, then **design change** |
| `include/client/eq.h` | 2,538 | 504 lines of enums + 3 structs + `class EverQuest` (897 declarations) | mechanical to ~1,930, then **design change** |

Extracting every free-standing type from `eq.h` still leaves `class EverQuest`
itself at ~1,811 lines. Nothing mechanical closes that gap — only pimpl'ing the
private data block or extracting owned components does, and that turns the risk
from "a compile error" into "a subtle lifetime bug".

### PROPOSED POLICY AMENDMENT — needs Will's sign-off before J14-J17 land

`docs/decisions.md` is the record of Will's decisions, so this batch RECOMMENDS
and does not self-apply:

1. **Multi-type headers** (more than one top-level class/struct/enum defined —
   mechanically decidable) keep the existing 1,500 warn / 3,000 fail unchanged.
2. **Single-class headers** gate on GROWTH, not absolute size: add a `--no-grow`
   mode to `scripts/refactor/check_file_sizes.sh` that fails a changed header
   only if it grew while already over 1,500.
   *Why:* the checker gates files a unit CHANGES. A flat FAIL would fire on the
   next unit that adds one method to `EverQuest`, forcing it to either perform
   an unrelated class decomposition or compress code to fit — which is exactly
   the harm T115 recorded (a crash fix squeezed onto a shared line) and T166
   exists to prevent. An unsatisfiable rule gets waived, and a waived rule
   gates nothing.
3. **Take the free lines anyway** (J16/J17), then grandfather `eq.h` (~1,930)
   and `irrlicht_renderer.h` (~1,820) with a `deferred.md` entry naming the
   class decomposition as the remaining debt.
4. **Do NOT attempt the `EverQuest` / `IrrlichtRenderer` decomposition in this
   batch.** It is a design question for Will, not a mechanical sweep.

Rationale for gating headers at all rather than exempting them: they are what an
agent reads MOST. `eq.h` has 65 includers, `packet_structs.h` 41,
`irrlicht_renderer.h` 39 — `eq.h` alone is ~30k tokens pulled into nearly every
client-side task, a larger recurring context cost than any .cpp in the tree.

## Process per unit — unchanged from Batch F, plus T166's bar

1. Orchestrator writes `refactor/J##_<name>.md`: the cluster table with line
   ranges, target TU names, the include list, and the named hazards.
2. Executor in an assigned slot, branch `task/J##-<name>` cut from `fable`,
   per `refactor/EXECUTOR_PROTOCOL.md`. **Execute the plan exactly. No scope
   creep, no drive-by fixes, no method decomposition.**
3. Acceptance, every unit:
   - build clean (by EXIT CODE, never by grepping the log)
   - full `ctest -j6` (**NOT -j24** — the visual gate's CPU ceiling flakes)
   - `python3 scripts/refactor/verify_move_only.py fable..HEAD` clean
   - visual gate PASS with **goldens untouched** (a re-bless here means the
     split changed behaviour — STOP and report)
   - `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh`
   - `check_file_sizes.sh --files` on every changed AND created file: 0 warn,
     0 fail. **A split that leaves a 2,400-line file is not done.**
4. Any file-local `static` that must become accessible goes to a private
   internal header and is declared in the unit's Review as the sanctioned
   Batch-F exception (T166 precedent).

**Standing constraint:** every unit edits `CMakeLists.txt` (explicit source
lists at `:266` and `:456` — no globs). Merge units ONE AT A TIME and rebase.

## Unit table

| Unit | File | Lines | → TUs | Largest after | Wave | Notes |
|---|---|---|---|---|---|---|
| J01 | `inventory/inventory_manager.cpp` | 2,502 | 4 | 795 | 0 (pilot) | zero file-local statics; two already-separate classes |
| J02 | `eq.cpp` | 3,913 | 6 | 1,035 | 1 | `DispatchZonePacket` becomes its own ~950-line TU |
| J03 | `graphics/entity_renderer.cpp` | 3,255 | 7 | 955 | 1 | keep the T307/T309 draw rule in one TU |
| J04 | `action/command_processor.cpp` | 3,024 | 5 | 790 | 1 | ⚠ must NOT split `registerBuiltinCommands` |
| J05 | `graphics/eq/wld_loader.cpp` | 2,908 | 7 | 490 | 2 | needs `wld_internal.h` (`read_val<T>` used 114×) |
| J06 | `application.cpp` | 2,356 | 5 | 815 | 2 | signal-handler static promotion |
| J07 | `eq_player_info.cpp` | 2,301 | 4 | 745 | 2 | 118 small methods, no statics — lowest risk |
| J08 | `eq_entities.cpp` | 2,736 | 4 | 970 | 3 | also relocates 161 lines into `eq_group.cpp` |
| J09 | `graphics/zone_shader.cpp` | 2,561 | 3 +2 hdrs | 990 | 3 | 1,345 lines are GLSL string literals |
| J10 | `graphics/constrained_texture_cache.cpp` | 2,211 | 4 | 780 | 3 | carry the `#if 0` guard region intact |
| J11 | `graphics/irrlicht_renderer.cpp` | 3,237 | 6 | 980 | 4 | **hard dependency: T312 must merge first** |
| J12 | `eq_action_handler.cpp` | 2,202 | 3 | 750 | 4 | 173 forwarding methods, trivial |
| J13 | `eq_bridge.cpp` | 2,098 | 3 | — | 4 | |
| J14 | `include/common/packet_structs.h` | 2,207 | umbrella + `packets/*.h` | — | 5 | ⚠ `#pragma pack(push,1)` per header; T145/T147 bit this twice |
| J15 | `include/client/action/action_dispatcher.h` | 1,556 | 3 | 1,060 | 5 | after J04 and J12 |
| J16 | `include/client/eq.h` type extraction | 2,538 | +3 hdrs | ~1,930 | 5 | after J02, J08; leave forwarding includes |
| J17 | `include/client/graphics/irrlicht_renderer.h` | 2,395 | +2 hdrs | ~1,820 | 5 | after J11 |
| J18+ | the residual ~13 files (§Residual) | — | — | — | 6 | re-measure first; several shrink as callers move |

Full clearance is roughly **26-30 units**.

## Wave plan — three slots, collisions resolved

- **Wave 0 (serial, pilot):** J01. Chosen over the hard-cap files deliberately —
  zero statics, two separate classes, real unit-test coverage. Proves the
  template on this tree before fan-out.
- **Wave 1 (3-way):** J02 / J03 / J04. `irrlicht_renderer.cpp` is deliberately
  NOT here: T312 owns it.
- **Wave 2 (3-way):** J05 / J06 / J07.
- **Wave 3 (3-way):** J08 / J09 / J10.
- **Wave 4 (3-way, after T312 AND T313 merge):** J11 / J12 / J13.
- **Wave 5 (headers, after the policy amendment):** J14 anytime a slot is free;
  J15 after J04+J12; J16 after J02+J08; J17 after J11.
- **Wave 6:** the residual list, re-measured.

### In-flight collision matrix

| in flight | files | blocks |
|---|---|---|
| T311 (oracle capture) | no `src/` changes | nothing |
| T312 (thread safety) | `irrlicht_renderer.h`/`.cpp`, `renderer_frame.cpp`, `graphics_input_handler.cpp`, `renderer_player_physics.cpp` | **J11, J17**, and the Wave-6 `renderer_frame` / `renderer_player_physics` units |
| T313 (UI scroll) | `graphics/ui/`, one hunk in `irrlicht_renderer.h` | **J17** (soft: J11). No `graphics/ui/` file is over 1,500 (largest `static_panels.cpp` 915), so no J unit wants that territory |

## Optional, NOT part of this batch — the Batch-G follow-up

Eight methods dominate their TUs. Clearing the ceiling does not require touching
any of them, and a unit that does so is out of scope. Recorded so the decision
can be taken separately: `DispatchZonePacket` 880 (`eq.cpp:800`),
`loadModelFromGlobalChrWithVariantsForAnimation` 815 (`variant_loading.cpp:382`),
`updatePlayerMovement` 806 (`renderer_player_physics.cpp:19`),
`registerBuiltinCommands` 768 (`command_processor.cpp:285`),
`Application::initialize` 569 (`application.cpp:105`),
`ProcessBridgeIntents` 558 (`eq_bridge.cpp:885`),
`createDefaultConfig` 548 (`detail_manager.cpp:1203` — a data table, leave it),
`processFrameRender` 546 (`renderer_frame.cpp:1143`).

## Residual (§15 of the survey) — surveyed, not yet mapped

`daybreak_connection.cpp` 1,985 · `eq_merchant.cpp` 1,931 ·
`detail_texture_atlas.cpp` 1,826 · `model_loading.cpp` 1,793 ·
`sky_renderer.cpp` 1,770 · `detail_manager.cpp` 1,754 (lifting the 548-line
config table alone clears it) · `renderer_frame.cpp` 1,691 (T312) ·
`zone_geometry.cpp` 1,637 · `variant_loading.cpp` 1,607 ·
`renderer_player_physics.cpp` 1,546 (T312) · `spell_visual_fx.cpp` 1,517 ·
`combat.cpp` 2,119 · `eq_bridge.cpp` 2,098 (= J13) ·
`action_dispatcher.cpp` 2,039 · `eq_movement.cpp` 2,033.

## Final state after Waves 0-5

All four hard-cap files gone from the FAIL list; the largest production TU in
the tree drops from **3,913 to ~1,035**; both mechanically-splittable headers
fully compliant; `eq.h` and `irrlicht_renderer.h` grandfathered with their
class-decomposition debt named on the board.
