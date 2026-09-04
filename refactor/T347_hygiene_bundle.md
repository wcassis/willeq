# T347 — Five small board items in one unit (B008, B012, B013, B029, B051)

Class B hygiene, each a few lines, disjoint files. Slot1. One commit per
item so a revert is surgical.

| item | file | change | proof |
|---|---|---|---|
| B008 | `src/common/net/daybreak_connection_send.cpp` `ValidateCRC` `default:` | a `LOG_WARN` naming the `crc_bytes` value before `return false` (or an assert if the caller can only pass 2/4 — measure the callers; if provably unreachable, `assert` + comment and say so) | a test feeding `crc_bytes = 3` sees the log (or the assert) |
| B012 | `src/client/action/command_processor_registration.cpp` | register a `/cleartarget` command reaching `ActionDispatcher::clearTarget` in the family whose banner fits (⚠ G03's order golden `command_registration_order_g03.txt` learns the new registration IN PLACE at its position, both faults re-planted) | the merged-help test lists it; a scripted `--script` run clears a target (headless test via the command path) |
| B013 | same file | register `/newskills` in the game registry (the renderer-side toggle stays; the registry entry routes to it the way other renderer-facing commands do — read how `/skills` routes) or rename+merge with `/skills` if they are the same feature — measure which is the working one (T143 said `/newskills` is the ONLY working toggle) | `/help` lists it; the merged-help test |
| B029 | `src/client/movement/movement_state.cpp` `m_water_surface_z` | zero readers → delete the member and its writes (§5; check `pushEvent` first) | build; the movement tests |
| B051 | `src/client/application.cpp` `initConstrainedRendererConfig` `customMemorySpec` | write-only → delete, or read it where the preset is applied if that was the intent (read the surrounding code; if the preset path ignores it by design, delete) — ⚠ G04's order golden learns the change in place | the init-order test |

Each: pre-fix evidence (the grep/test that shows the gap), the change,
the test, the board strike. Territory as listed plus the two goldens (in
place only), `refactor/BOARD.md`. No graphics files: gate + ARM skipped
with evidence.

STOP: an item needs more than a few lines or a design choice (B013's
merge question if `/skills` and `/newskills` differ in behaviour) —
leave it and say so.
