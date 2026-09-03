# K00 — Truth reset: one board, enforced ceilings, docs that say what the tree says

**Why this unit exists (Will, 2026-09-03).** Stale documentation is corrupting
task scoping and dependency tracking. `refactor/deferred.md` is 10,409 lines
with an EMPTY `## STATUS` section that `CLAUDE.md` tells every reader to start
at; its index admits it is "two removes from current"; its item numbers run
35, 44, 45, 36. `refactor/` holds 477 markdown files and 160,000 lines.
`check_file_sizes.sh` checks only files changed vs `fable`, so nothing enforces
the ceiling once a unit merges. Executors (and the orchestrator's own memory)
have been scoping off all of this. This unit replaces it with something a
small model can read in one sitting and a ctest that keeps it honest.

Standing context: this project is private and single-user (Will). There is no
user base, no production, no deployment. Do not write any of those words into
any doc this unit touches.

Two legs, non-overlapping territory, run in parallel slots.

---

## Leg A — the board (slot1, branch `task/K00-legA-board`)

**Territory:** `refactor/BOARD.md` (new), `refactor/deferred.md` (moves),
`refactor/progress.md` (moves), `refactor/testing_progress.md` (moves),
`refactor/completed/` (additions), `CLAUDE.md`, `docs/CLAUDE.md`,
`docs/module_map.md`, `include/client/state/CLAUDE.md`,
`include/client/bridge/CLAUDE.md`, `src/client/input/CLAUDE.md`,
`src/client/graphics/CLAUDE.md`, `include/client/graphics/ui/CLAUDE.md`,
`README.md`. Nothing under `src/`, `include/`, `tests/`, `scripts/`.

### A1. Build `refactor/BOARD.md`

One entry per OPEN item, nothing else. Format, one item per bullet:

```
- **B001** (A) `bridge/renderer_event_receiver.cpp` `RendererEventReceiver` —
  one-sentence defect. Closes when: one sentence. Prov: deferred item 32.
```

- Stable IDs `B001..`, assigned in severity order A → B → C → process. The old
  deferred item number goes in `Prov:` and nowhere else.
- Anchor by **file and function/class name, never a line number** (J21 found
  line citations rot within a unit).
- Class letters (define them at the top of the board): A live defect, B
  implemented-but-unreached / hygiene with a code change, C needs Will's
  design decision, P process/doc).
- Source every candidate from: "⚠ THE ONE CLASS-A LIST" (items 1–45 and the
  addenda), "SERVER-SENT PLACEABLES AND DOORS ARE UNMEASURED", the "⚠ AUDIT
  2026-08-27" bullets, every non-struck entry further down, and the "Handoffs
  to the perf session" section of `refactor/testing_progress.md`.
- **An item is on the board only if you confirmed it against the tree at your
  branch tip.** For each candidate run a grep or read that shows the defect /
  gap still exists, and record that command and its one-line result in the
  Review table (NOT on the board). If the anchor is gone or the behaviour is
  already fixed, it is closed: list it under "Closed by this audit" in the
  Review with the commit that closed it if `git log -S` finds one.
- Items Will has explicitly ruled on are not "awaiting Will". Check
  `docs/decisions.md` and the J tracker's "WILL'S RULING" before writing
  class C.
- Add these items, which the 2026-09-03 survey established and no doc carries:
  - (P) Component extraction of `class EverQuest` and `class IrrlichtRenderer`
    is the open structural programme — `refactor/K_component_extraction.md`.
  - (B) `src/client/eq_ucs.cpp` is 320 lines of commented-out code; T297
    proved UCS is not needed by Titanium; delete it and the UCS connect path
    (deferred item 4 becomes a delete, not a fix).
  - (B) Eight vestigial `irrlicht_renderer.h` includes in game-logic TUs, the
    `InitGraphics(IrrlichtRenderer*, …)` parameter, and
    `door_manager.cpp`'s `EverQuest::GetDebugLevel()` — the S2 leftovers.
  - (P) The eight 546–880-line methods listed under "Optional, NOT part of
    this batch — the Batch-G follow-up" in `refactor/J_file_size_reclamation.md`.
- Top of the board: a 6-line header — tip hash, date, suite count from the
  last merged unit's Review (name it), and the sentence "Items are confirmed
  against the tree at the tip above; nothing here is quoted from an older
  board."
- Target: under 250 lines. If it is longer, you have copied prose instead of
  writing one sentence.

### A2. Archive, do not delete

- `git mv refactor/deferred.md refactor/completed/deferred_archive_20260903.md`
- `git mv refactor/progress.md refactor/completed/progress_archive_20260903.md`
- `git mv refactor/testing_progress.md refactor/completed/testing_progress_archive_20260903.md`
- Prepend each with a 3-line banner: archived 2026-09-03 by K00; superseded by
  `refactor/BOARD.md`; findings here are historical, do not scope off them.
- Grep the whole tree (`*.md`, `*.py`, `*.sh`, `*.cmake`, `*.cpp`, `*.h`) for
  `deferred.md`, `testing_progress.md`, `progress.md`. Repoint every
  reference in a LIVE doc (the files in your territory) to `BOARD.md`. If any
  script or test reads one of these files as text, STOP and report — that is
  outside your territory.

### A3. Rewrite the orientation

- `CLAUDE.md`: replace the "Orientation" and "Process requirement" sections.
  Order of reading for a new agent: `refactor/BOARD.md` →
  `docs/module_map.md` → `docs/decisions.md` → the unit plan. Remove every
  mention of a `## STATUS` block, of `F_file_split.md`, and the stale suite
  count sentence in "Build Commands" (replace with "run it; the sole expected
  failure is the ALSA quirk"). Add one paragraph: Batch J is complete (every
  `.cpp` under 1,500; `eq.h` and `irrlicht_renderer.h` carry a no-grow
  allowlist entry pending `refactor/K_component_extraction.md`). Keep every
  build/bootstrap command verbatim.
- `README.md`: its newest date is 2026-07-28. Do not rewrite it. Add a
  dated "Status" paragraph at the top pointing at `BOARD.md` and
  `module_map.md`, and delete any sentence you can show is false at the tip
  (list each deletion in the Review with the evidence).
- Each subdirectory `CLAUDE.md`: read it against the tree. Fix or delete
  sentences that are false (counts, file names, "TODO" claims). List each
  change in the Review with the evidence. Do not add prose.
- `docs/module_map.md`: add a row block for the flat `src/client/eq_*.cpp`
  family table from `refactor/K_component_extraction.md` §Families so the two
  agree.

### STOP conditions (Leg A)

- Any candidate item whose confirmation needs a build or a live run: board
  it as "unconfirmed, needs <what>" rather than guessing, and say so in the
  Review.
- Do not fix any item. Do not touch code.
- If the board exceeds 400 lines, stop and report the count; something is
  wrong with the method.

---

## Leg B — enforcement (slot2, branch `task/K00-legB-enforce`)

**Territory:** `scripts/refactor/check_file_sizes.sh`,
`scripts/refactor/file_size_allowlist.txt` (new),
`scripts/refactor/check_doc_citations.py` (new), `tests/cmake/` (one new
file), `tests/CMakeLists.txt` (one include line), `docs/decisions.md` (one
new dated decision entry). Nothing else.

### B1. `check_file_sizes.sh --all`

- New mode `--all`: check every `*.cpp`, `*.h`, `*.hpp` under `src/`,
  `include/`, `tests/` (exclude `third_party/`, `build*/`). Existing modes
  unchanged.
- Allowlist file `scripts/refactor/file_size_allowlist.txt`, format
  `path<TAB>max_lines<TAB>reason`. Seed with exactly two entries at their
  CURRENT line counts (measure, do not copy: `wc -l`):
  `include/client/eq.h` and `include/client/graphics/irrlicht_renderer.h`,
  reason "single class; shrinks under refactor/K_component_extraction.md; may
  not grow".
- Semantics under `--all`: FAIL if any file is over 3,000; FAIL if an
  allowlisted file exceeds its `max_lines` (that is the no-grow ratchet);
  WARN for any other file over 1,500; exit non-zero on any FAIL; print the
  count of files checked (never a silent pass, keep the T37 rule).
- Measure first: run `--all` on the tip before you change anything and record
  the WARN list in the Review. Expected: `eq.h`, `irrlicht_renderer.h`
  allowlisted; nothing else over 1,500 (J34 claims every `.cpp` is under; a
  test file may not be — if one is, WARN, do not FAIL, and list it).

### B2. `check_doc_citations.py`

- Walks ONLY the live docs: root `CLAUDE.md`, every `*/CLAUDE.md`,
  `docs/*.md` (not `docs/reference/`), `refactor/BOARD.md`,
  `refactor/EXECUTOR_PROTOCOL.md`, `refactor/K_*.md`. Not `refactor/completed/`,
  not the T/J/P plan files.
- Finds every `path/to/file.ext:NNN` citation. Reports: file does not exist
  (FAIL); line NNN beyond the file's length (FAIL); otherwise OK. Prints counts.
- Also FAIL on a citation of a path that does not exist even without a line
  number, when the path contains a `/` and ends in `.cpp|.h|.py|.sh|.md`.
- Run it on the tip first; record what it finds in the Review. Do not fix the
  docs it flags — that is Leg A's territory. If Leg A has not merged when you
  run it, `BOARD.md` will not exist; skip missing inputs with a printed note.

### B3. Register both in ctest

- One new `tests/cmake/hygiene.cmake` adding two tests: `FileSizeCeiling`
  runs `check_file_sizes.sh --all`; `DocCitations` runs
  `check_doc_citations.py`. Label both `hygiene`. Include it from
  `tests/CMakeLists.txt` the same way the other `tests/cmake/*.cmake` files
  are included (grep for the pattern; do not cite a line number).
- Confirm with `ctest --test-dir <slot>/build -R 'FileSizeCeiling|DocCitations'
  --output-on-failure` that both run and pass (DocCitations may FAIL before
  Leg A merges — if so, record the failing citations and mark the test with
  the `hygiene` label only; the orchestrator sequences the merge so Leg A
  lands first).

### B4. Decision entry

- Append to `docs/decisions.md`, dated 2026-09-03: the ceiling is enforced
  tree-wide by ctest; single-class headers are allowlisted with a no-grow
  maximum that each K unit ratchets down; live docs must have resolvable
  citations. Five lines.

### STOP conditions (Leg B)

- If `--all` finds a file over 3,000 lines, STOP and report — nothing should
  be, and it means the J close-out is wrong.
- Do not edit any file outside the territory to make a test pass.

---

## Verification bar

Protocol bar, narrowed: Leg A is docs-only — build + ctest need not be re-run
but `git diff --stat` must show no `src/`, `include/`, `tests/`, `scripts/`
change. Leg B: full bar steps 1, 2 and 6 (using the new `--all`); steps 3–5
not needed (no code under `src/` changes).

## Review

(filled by each executor: per-item confirmation table for Leg A; before/after
checker output for Leg B; deviations.)

---

## Review — Leg A

Executed in `wt/slot1` on `task/K00-legA-board`, base `fable` = `3808ebe9`
(verified at checkout). Docs-only: `git diff --stat $(git merge-base fable HEAD)..HEAD`
touches 12 files, all `*.md`; no `*.cpp`, `*.h`, `*.py`, `*.sh` or `*.cmake`
changed. Nothing was built.

⚠ **`fable` moved under this unit.** It now carries `e8d51033` (the K01 plan),
which post-dates the base named in the dispatch. A plain `git diff fable..HEAD`
therefore shows a spurious deletion of `refactor/K01_guild_component.md`; the
diff above is taken against the merge base, per the standing rule.

### Per-item confirmation

Every command was run at `3808ebe9` in `wt/slot1`. `W` = the worktree root.

**Boarded (confirmed present).**

| candidate | command | result | boarded as |
|---|---|---|---|
| item 15, non-sending position setter | `sed -n '287,345p' src/client/eq_movement_state.cpp` | `SetPositionState` has no non-sending overload; five arms end in `SendSpawnAppearance`, incl. `AT_DIE` | B001 (A) |
| item 10, autorun | `sed -n '144,148p' src/client/eq_action_handler.cpp`; `grep -rn autorun src include` | body is one `LOG_DEBUG`; `playerMovement_.autorun` is READ at `renderer_player_physics.cpp:75` and written nowhere | B002 (A) |
| item 27, no swing on auto-attack | — | no cheap tree check exists; the claim is about live behaviour | B003 (A), **unconfirmed, needs a live eqemu session** |
| archive "static-panel UI surface untested after T54" | `grep -rn 'popW\|popH' include/client/graphics/ui/ui_layout.h` | `popW = 400`, `popH = 350`, hardcoded at every resolution | B004 (A), **unconfirmed, needs a rendered popup or a hit-test** |
| item 4, UCS | `ls src/client/eq_ucs.cpp` → absent; `wc -l src/client/ucs.cpp` → 413; `grep -rn UCSConnection src include tests` → no hit outside its own pair; `grep -n ConnectToUCS src/client/eq_login_world.cpp` → `// ConnectToUCS(...)` | the TU named in the plan is gone (T326); a different 413-line zero-caller TU remains | B005 (B) |
| S2 leftovers | per-file `grep -c IrrlichtRenderer` over the 15 TUs that include `graphics/irrlicht_renderer.h` → exactly 8 score 0; `sed -n '875,877p' include/client/eq.h`; `grep -n GetDebugLevel src/client/graphics/door_manager.cpp` | all three present; the "eight" in the plan is exact | B006 (B) |
| item 35, skill costs | `grep -rn 'endurance_cost\|mana_cost' src include` | declared in `skill_data.h` with `= 0`, read in `skill_manager.cpp`, assigned nowhere | B007 (B) |
| item 41, ValidateCRC | `sed -n '25,57p' src/common/net/daybreak_connection_send.cpp` | `default: return false;` with no log line | B008 (B) |
| item 40, pcap test | `sed -n '135,150p' tests/test_daybreak_connection_pcap.cpp` | raw `*(uint32_t*)&pkt.data[2]` / `[6]` / `[13]` reads intact | B009 (B) |
| item 42, gtest names | `grep -n 'PrintTo\|CameraCommandRoundTripTest' tests/test_irrlicht_camera_command.cpp` | four hits, none a `PrintTo` | B010 (B) |
| item 38, setEntitySize | `grep -n 'collisionHeight\|deckZ' src/client/graphics/entity_size.cpp` | no hit; both members exist in `entity_renderer.h` | B011 (B) |
| archive T39, clearTarget | `grep -c clearTarget src/client/action/command_processor_registration.cpp` | 0 | B012 (B) |
| archive T143, /newskills | `grep -n newskills src/client/graphics/renderer_slash_commands.cpp` | one hit, in the registry-3 if/else chain | B013 (B) |
| archive T92, preloadModelData | `grep -n getOrLoad src/client/graphics/eq/race_model_loader.cpp` | `getOrLoad(compositeBaseName, emptyData, …)` still at the composite-sheet site | B014 (B) |
| archive T131, loadSoundFont | `grep -rn loadSoundFont src/client/audio` | raw swap, with the file's own comment at `midi_player.cpp:396` naming the single-call-site precondition | B015 (B) |
| archive T114, `loadedModels_` | `grep -rc loadedModels_ src/client/graphics/eq/*.cpp` | 67 references across four TUs; lock coverage not checked | B016 (B), **unconfirmed, needs a TSAN run** |
| item 37, decline wording | `grep -rn 'declined the guild' src include` | `eq_guild.cpp:96` intact | B017 (C) |
| item 16, first person | `ls src/client/graphics/entity_first_person_draw.cpp` | present (T309 draw rule landed); eye placement unchanged | B018 (C) |
| placeables/doors unmeasured | plan `T301`/section read; `tools/hwcaps/zone_bench_memory.h` annotated in place | no live memory harness exists | B040 (C), renumbered from B019 by K15 |
| archive T69, budgets | `grep -rn deferred include/client/graphics/constrained_renderer_config.h` | the per-category budgets are still absent | B020 (C) |
| archive T69, untracked RSS | archive read | not asserted in `visual_gate.py`; Leg B territory to change | B021 (C) |
| archive T70, spells.eff | `ls src/client/graphics/spell_visual_fx.cpp` | present, 1,517 lines, still the hardcoded vocabulary | B022 (C) |
| archive T146, SendReport | `grep -rn SendReport src/client` | producer now exists (`eq_action_handler_extended.cpp:278`); the wire-shape question is what remains | B023 (C) |
| Batch K programme | `refactor/K_component_extraction.md` read; `wc -l include/client/eq.h` = 2,025 | the plan exists and is unboarded anywhere else | B024 (P) |
| Batch-G follow-up, 8 methods | `grep -n 'Optional, NOT part of this batch' -A 12 refactor/J_file_size_reclamation.md` | the eight are listed with sizes | B025 (P) |
| item 22, unmerged branches | `git rev-list --count fable..task/eqcpp-zoneprocess-track-b-remaining` = 300; same for `task/eqemu-zone-line-transition-scenario` = 300 | both branches exist, both 300 ahead | B026 (P) |
| item 43, GM flag | — | server-side; no tree check possible | B027 (P), **needs eqemu** |
| archive Batch Z, Z5.3 | archive read | never taken | B028 (P) |

**Closed by this audit** — the anchor is gone or the behaviour is already fixed,
so these are NOT on the board.

| candidate | command | result | closed by |
|---|---|---|---|
| items 1, 2 — `bridgeQueue_` race + scalar RMWs | `grep -rn 'bridgeQueue_' include/client/graphics/irrlicht_renderer.h` | only two comments naming "the no-raw-access rule (T312)"; the raw vector is gone | T312 `c9b02f7e` |
| item 3 — CRC skipped on zone connect | `sed -n '25,30p' src/common/net/daybreak_connection_send.cpp` | `skip_crc_validation` is an option with no production setter | T323 `86ada50e` |
| item 6 — guild rank hardcoded | `grep -n 'officer\|leaderstatus' src/client/eq_guild.cpp` | both sites carry `// T317: this used to be …` | T317 |
| item 7 — skill combat/position gates | `grep -n checkPositionRequirement src/client/skill/skill_manager.cpp` | called at `:232` and `:311`, implemented | T317 (position) + T325 (combat gate reverted on Will's ruling) |
| item 18 — RDP caps lock | `grep -n capsLock src/client/graphics/renderer_rdp.cpp` | `rdpLockKeys_.capsLock()` | (tracked state now exists) |
| item 19 — `GraphicalMode` fallthrough | `sed -n '30,55p' src/client/mode/graphical_mode.cpp` | explicit, with Will's 2026-08-28 ruling written in | T304 Leg 3 + T308 Leg 2 — **ruled on, not "awaiting Will"** |
| item 26 — wire z | `grep -rn '0.625' src include` | `kWireZOffsetPerSize = 0.625f` in `position_frames.h`, used on the send path | T311 Leg B |
| item 28 — dead `headZ` | `sed -n '549,600p' src/client/eq_movement_state.cpp` | `headZ` IS passed to `findRegionForPoint` at `:580`; the archive records the original claim as a misreading | T311 Leg B |
| item 29 — wire heading scaling | `sed -n '612,632p' src/client/eq_movement_update.cpp` | fable packs `server_heading * 2048/360`, i.e. `4 ×` the 0-512 EQ unit — the same `heading * 4` the oracle carried (1920 at `#loc` 480) | confirmed by inspection here |
| item 32 — `RendererEventReceiver` wants its own TU | `ls src/client/graphics/renderer_event_receiver.cpp` | present | J11 |
| item 39 — "96 event types" doc drift | `awk` over `enum class GameEventType` = 97; `grep -rn '9[67] event'` over the live docs | every live doc already said 97 and 97 is correct | (already struck) |
| archive — race 330 placeholder cube | `grep -n 330 config/race_models.json`; `grep -n 330 src/client/graphics/eq/race_codes.cpp` | row points at `globalpcfroglok_chr.s3d`; the hardcode is gone and commented as removed | T265/T267/T274 |
| archive — hotbar Item/Emote slots skipped | `sed -n '320,345p' src/client/action/input_action_bridge.cpp` | Item → `useItemById`, Emote/Macro/Combat → `activateHotbarCommandSlot`; the skips are named as former | T230, T239 |
| archive — `protectedRegions_` all-false | `grep -rn protectedRegions_ src include` | six hits, every one a tombstone comment | T292 `8d07e768` |
| archive — `TargetHPUpdated` has no publisher | `grep -rn TargetHPUpdated src include` | zero hits; the event type no longer exists | T264 |
| archive — `SendGMHideMe` misrouted | `sed -n '163,180p' src/client/eq_gm.cpp` | sends `HC_OP_GMHideMe` | T217 `175c97ca` |
| archive — `SendFaceChange` has no caller | `grep -rn SendFaceChange src include` | called from `eq_action_handler_spells_trade.cpp:524` | T153 |
| archive — masked textures forced `EMT_SOLID` | `grep -n textureMasked src/client/graphics/eq/mesh_building.cpp` | `isMaskedMaterial` is computed from `geometry->textureMasked()` | (fixed in place) |
| archive — `SpellVisualFX::m_particle_manager` always null | `sed -n '276,278p' src/client/graphics/renderer_entities.cpp` | the call is guarded by `if (particleManager_)` and made after construction | (fixed in place) |
| archive — registry 3 has no queryable catalog | `grep -n 'registry 3' src/client/graphics/renderer_slash_commands.cpp` | catalog + drift-guard landed | T223 `e6d31424` |
| archive — `eq.cpp` is 3,898 lines, over the hard cap | `wc -l src/client/eq.cpp` | 1,073 | Batch J (J02) |
| perf-session handoffs | `grep -n 'Handoffs to the perf session' -A 55 refactor/testing_progress.md` | every bullet struck and closed; the only residue is "the handoff channel is write-only", which the single-session model retires | — |

Suite figure in the board header: 3497/3498 from
`refactor/J16_eq_header_type_extraction.md`'s Review — the last merged code unit
(`8e6b46f4`). Named on the board, as required.

### Batch J close-out, measured

`find src include tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | xargs wc -l`,
`third_party/` excluded: exactly two files exceed 1,500 lines —
`include/client/eq.h` (2,025) and
`include/client/graphics/irrlicht_renderer.h` (1,914). **Nothing exceeds 3,000.**
This is what the new CLAUDE.md paragraph asserts, and it is measured, not
copied from the J tracker.

### A2 — archive

`git mv` of `deferred.md`, `progress.md` and `testing_progress.md` into
`refactor/completed/` under their `_archive_20260903` names, each with a
three-line banner. **No STOP:** a tree-wide grep for the three filenames over
`*.md *.py *.sh *.cmake *.cpp *.h` found **no script or test that reads any of
them as text** — every hit outside `refactor/` is a prose or comment reference.
References in code comments were left alone (outside this leg's territory);
every reference in a LIVE doc in the territory list now points at `BOARD.md`,
verified by a final grep that returns nothing.

### False sentences fixed, with evidence

`CLAUDE.md`
- "read its `## STATUS` block (dated, top of file) first — it carries the current
  tip, suite count and the open items by class" — the section has an empty body;
  removed with the whole `deferred.md` bullet. Reading order now leads with
  `refactor/BOARD.md`.
- "`refactor/F_file_split.md` is COMPLETE (historical); the active unit plans are
  `refactor/T<nn>_*.md`" — removed; the active plans are `refactor/K<nn>_*.md`.
- "full ctest suite, 3506/3507 passing at `9684644f`" — replaced with "run it;
  the sole expected failure is the ALSA quirk", per the plan.
- "`src/client/eq.cpp` + 27 `eq_*.cpp` domain TUs" — `ls src/client/eq_*.cpp | wc -l`
  = **37**.
- "eq.cpp itself (1,026 lines at `9684644f`, after J02)" — `wc -l` = **1,073**.
- "33 intent structs in the `RendererIntent` variant" — counted in
  `renderer_intents.h` = **35**.
- Seam tracking pointed at `refactor/deferred.md`; now at `BOARD.md` item B006.

`README.md` (newest date 2026-07-28; not rewritten)
- Added a dated 2026-09-03 "Status" paragraph at the top of Project Status
  pointing at `BOARD.md` and `docs/module_map.md`, and saying the prose below it
  is orientation, not a defect list.
- **Deleted as false:** "First-person view is rebuilt on the measured mechanism
  (player model at the eyes, clamped down-pitch keeps the body out of frame) but
  the clamp angle is an inference…". Evidence: the clamp mechanism was refuted
  live (archive, "THE CLAMP DOES NOT HIDE THE BODY" — `maxDownPitch = 90 - vFOV/2`
  is by construction the angle that admits the body's own directions), and the
  rule that shipped is T309's attack-animation draw rule,
  `src/client/graphics/entity_first_person_draw.cpp`, which exists at this tip.
  Replaced with what the tree does; eye placement moved to board item B018.
- Two `deferred.md` references repointed; the plan-file naming was `T###_*.md`
  only and now names the J and K series too.
- Checked and left alone: "no character creation" (`grep -rn OP_CharacterCreate
  src include` → zero hits, true) and "Weather effects (rain, snow) with particle
  systems" (true — `WeatherEffectsController` drives
  `particleManager_->activateWeatherParticles()` under `EQT_HAS_GLES2`; only the
  screen-space overlays are non-GLES2).

`include/client/state/CLAUDE.md`
- "aggregates 11 domain-specific state modules" — `game_state.h` holds **10**
  (`EventBus` is not one of them); corrected, and the missing `TradeState` row
  added so the table matches the count.
- Dropped the "re-counted at fable `9684644f`" provenance from the 97-event line
  (the count is right; the stale hash is not a citation anyone can act on).

`include/client/bridge/CLAUDE.md`
- "`applyEvent()` implements an 839+ case switch" — `grep -c 'case '
  src/client/bridge/irrlicht_bridge.cpp` = **97**, one per event type.
- "## Event Types (140+)" — **97**.
- "## Intent Types (50+)" — **35**.
- Eleven named intents do not exist in `renderer_intents.h` (`grep -c` = 0 each):
  `TradeRequestIntent`, `InterruptSpellIntent`, `PetCommandIntent`,
  `GroupInviteIntent`, `GroupAcceptIntent`, `DisbandIntent`,
  `HotbarChangedIntent`, `ChatLinkClickIntent`, `RequestMemoryReport`,
  `RequestSceneDump`, `SlashCommandIntent`. The list was replaced with the real
  variant members.
- "All 343+ former direct `m_renderer->` calls" — an unverifiable count; reworded
  without a number.

`src/client/input/CLAUDE.md`
- "`HotkeyAction` enum (76 actions…, re-counted J21)" — counted in
  `hotkey_manager.h` = **80**.
- "split across nine TUs" — the `command_processor` family is **eight**
  (`command_processor{,_registration,_bank,_loot,_social,_spells_gm,_train,_world}.cpp`).
- "~180 action methods" — 182 by `grep -cE` over `action_dispatcher.h`; adjusted.
- "Item and Emote slots are DEBUG-logged and skipped … recorded open in
  `refactor/deferred.md`" — **false**: `input_action_bridge.cpp` routes Item via
  `useItemById` (T230) and Emote/Macro/Combat via `activateHotbarCommandSlot`
  (T239). Rewritten to describe the live routing.

`include/client/graphics/ui/CLAUDE.md`
- "Ctrl+D / PageDown | Strafe right" — `hotkey_manager.cpp` binds StrafeRight to
  `KEY_HOME` ("T313: was Page Down") and PageUp/PageDown to `ScrollUIUp/Down`.
  Corrected, and a PageUp/PageDown row added.
- "1-8 | Hotbar slots … Item and Emote slots are logged and skipped" — same false
  claim as above; corrected.
- The autorun row now says the binding exists and the action is a no-op, citing
  board item B002, rather than implying the key works.
- The ESC row notes it is keybinding-only (board item B012).

`src/client/graphics/CLAUDE.md`
- "24 `renderer_*.cpp` TUs" — `ls src/client/graphics/renderer_*.cpp | wc -l` =
  **27**. The "908 lines" figure was re-measured and is still correct; the stale
  `at 9684644f` hash was dropped.

`docs/CLAUDE.md` — read against the tree; every documentation-layout and
coordinate-system statement checked out (the `docs/reference/`,
`docs/standards/`, `docs/plans/` inventories are accurate and the four coordinate
systems match the code they cite). **No change made** — the plan says not to add
prose.

`docs/module_map.md`
- The `eq_*.cpp` table said 27 TUs and listed 27; eleven real TUs were absent
  (`eq_action_handler_extended`, `_spells_trade`, `eq_bridge_intents`,
  `_ui_audio`, `eq_entities_appearance`, `_updates`, `_world`,
  `eq_merchant_trade`, `_vendor_bank`, `eq_movement_state`, `_update`). Added.
- Every `Lines` value in that table was stale (e.g. `eq_entities.cpp` 2,786 →
  **830** after J08; `eq.cpp` 1,026 → **1,073**). The column was regenerated by
  `wc -l` at this tip and the header now cites `3808ebe9` instead of `9684644f`.
- Added the **Family grouping** block transcribed from
  `refactor/K_component_extraction.md` §Families, with a line saying the two must
  change together — the A3 deliverable.

### Deviations

1. **The plan's mandated UCS item names a file that no longer exists.**
   `src/client/eq_ucs.cpp` was deleted by **T326** (`CMakeLists.txt` carries the
   tombstone: "79 include lines and one 241-line comment block, no code"). The
   real residue is `src/client/ucs.cpp` (413 lines, **live code, not comments**)
   plus `include/client/ucs.h`, with zero callers. Boarded in that corrected
   form as B005 rather than transcribed as written.
2. **Two of the plan's territory files are under `src/` and `include/`**
   (`src/client/input/CLAUDE.md`, `src/client/graphics/CLAUDE.md`,
   `include/client/*/CLAUDE.md`), which the verification bar's "no file under
   `src/`, `include/`" wording would forbid. Read as "no code file": the diff is
   markdown-only, confirmed by
   `git diff --name-only $(git merge-base fable HEAD) | grep -v '\.md$'`
   returning nothing.
3. **The archive tail was surveyed, not exhaustively confirmed.** Lines 1–1300 of
   `deferred.md` (the ranked list, the placeables section, the 2026-08-27 audit
   bullets) were read directly and every candidate in them is in one of the two
   tables above. Lines 1300–10409 were surveyed by a dedicated scan that returned
   ~70 candidates with open residue; the ones with a cheap tree anchor were
   checked and are boarded or closed above. **Roughly thirty tail candidates were
   neither boarded nor closed** because confirming them needs a build, a live
   session, a Pi, or a rendered frame — chiefly: the blackburrow/crushbone blank
   benchmark render, the befallen first-frame SIGSEGV, the ARM shutdown SIGSEGV
   and the Mesa/Lima SIGBUS, the animated-composite pixel delta, the
   `CreateZoneLoadSnapshot` "BSP region is SIZE_MAX" race, the pale fog shapes in
   the goldens, the secondary-weapon paperdoll gap, and the `/shout`-`/ooc` relay
   question. They remain in
   `refactor/completed/deferred_archive_20260903.md`. **This is a real gap and is
   flagged rather than absorbed:** the board is true about what it contains, and
   it is not yet the complete set. Closing it wants one unit that takes those
   measurements; it could not be done inside a docs-only leg.
4. **`docs/CLAUDE.md` was left unchanged** — it is in the territory list, but
   reading it against the tree found nothing false, and the plan forbids adding
   prose.
5. **No STOP was hit.** The board is 211 lines, under the 250-line target.

### Board summary

211 lines, 28 items: **4 class A** (2 of them unconfirmed — B003 needs a live
session, B004 needs a rendered popup), **12 class B** (1 unconfirmed — B016
needs TSAN), **7 class C**, **5 class P** (1 needs eqemu — B027).
**22 candidates closed by this audit**, each with the command that showed it.

## Review — Leg B

Branch `task/K00-legB-enforce`, base tip `3808ebe9` (confirmed at checkout).

### B1 — `check_file_sizes.sh --all`

Since `--all` did not exist before this unit, "before" is the first run of
the new mode against the unmodified allowlist (i.e. against the tip with the
seeded no-grow ceilings, before any code change); this is also the pre-unit
tree-wide state the plan asked to record, since no file under `src/`,
`include/`, `tests/` was touched by this unit.

- `bash scripts/refactor/check_file_sizes.sh -C <slot> --all`
- **796 files checked: 796 ok, 0 warn, 0 fail. Exit 0.**
- WARN list: **empty.** No file besides the two allowlisted headers is over
  1,500 lines anywhere under `src/`, `include/`, `tests/` — confirms J34's
  claim for `.cpp` and extends it to `.h`/`.hpp` and the `tests/` tree.
- Allowlisted files, both `ok` (within their seeded ceiling, not WARNed):
  - `include/client/eq.h`: 2025 lines (allowlisted max 2025)
  - `include/client/graphics/irrlicht_renderer.h`: 1914 lines (allowlisted
    max 1914)
- No file over 3,000 — the STOP condition on a >3,000 finding did not fire.
- Ratchet correctness verified by temporarily lowering `eq.h`'s allowlisted
  max to 2000 (below its real 2025) and re-running `--all`: reproduced
  `FAIL include/client/eq.h: 2025 lines (> allowlisted max 2000 — no-grow
  ratchet exceeded; ...)`, exit 1, `795 ok, 0 warn, 1 fail`. Allowlist file
  restored immediately after (`git diff` on it shows the seeded values only).
- Existing modes reconfirmed unchanged: diff-mode against `fable` from the
  slot on its task branch (no `src/`/`include/`/`tests/` changes in this
  unit) prints `no C++ files to check ... nothing was checked, this is not a
  pass`, exit 0 (T37 behaviour intact); `--files <path>` still WARNs
  `include/client/eq.h` at 2025 lines exactly as before.

### B2 — `check_doc_citations.py`

Run against the tip before Leg A has merged (per the plan, `BOARD.md` is
expected absent):

- `python3 scripts/refactor/check_doc_citations.py --repo <slot>`
- **13 live docs checked, 176 citations found: 163 ok, 13 fail. Exit 1.**
- `NOTE refactor/BOARD.md not found — skipped (Leg A may not have merged
  yet)` — the documented skip-missing-input path fired correctly.
- The 13 FAILs (recorded here, not fixed — Leg A's territory):
  1. `CLAUDE.md:183` cites `review/T64/README.md` — does not exist in the
     repo tree. This is a **structural false positive**: `review/` is the
     sanctioned outside-checkout path for capture media
     (`EXECUTOR_PROTOCOL.md`), not part of the repo, so any citation into it
     will always FAIL here. Flagging for Leg A/orchestrator awareness, not a
     doc bug.
  2. `refactor/EXECUTOR_PROTOCOL.md:230` cites `review/T64/README.md` — same
     structural false positive as #1.
  3. `src/client/graphics/CLAUDE.md:129` cites
     `include/client/graphics/constrained_mesh_cache.h` — deleted T67, the
     doc's own prose says so two sentences earlier.
  4-5. Same line cites `src/client/graphics/constrained_mesh_cache.cpp`
     twice (once in prose, once inside a `git show` command) — same T67
     deletion, genuinely stale.
  6. `src/client/graphics/CLAUDE.md:249` cites `./scripts/gdb_zone_crash.sh`
     — does not exist.
  7. `docs/decisions.md:524` cites
     `include/client/graphics/ui/loot_window.h` — does not exist.
  8. `docs/decisions.md:525` cites `src/client/graphics/ui/loot_window.cpp`
     — does not exist.
  9. `docs/decisions.md:526` cites `refactor/D06b_publish_loot_vendor.md` —
     does not exist.
  10. `docs/decisions.md:758` cites `world/zoneserver.cpp:423` — this is a
      citation into `/mnt/projects/akk-stack/code` (a sibling project, named
      explicitly two lines above in the source text), not this repo. A
      second structural false-positive category: an intentional
      cross-project citation will always FAIL a repo-relative resolver.
  11. `docs/decisions.md:28` (this unit's own new B4 entry) cites
      `refactor/BOARD.md` — expected, Leg A has not merged; will resolve
      once it does.
  12. `refactor/K_component_extraction.md:70` cites
      `include/client/client_context.h` — does not exist (component not yet
      extracted).
  13. `refactor/K_component_extraction.md:147` cites
      `refactor/K01_guild_component.md` — a prospective future unit plan,
      not yet written.
- Deviation flagged for the orchestrator/Leg A: two FAIL categories
  (`review/...` paths, and the one intentional cross-project citation in
  `docs/decisions.md:758`) are **not** doc staleness — they are citations
  the checker cannot resolve by design (outside-repo by convention).
  `check_doc_citations.py` was left implementing the literal spec (repo-root
  resolution only) rather than special-casing `review/` or absolute
  cross-project paths, since the plan's territory for B2 did not ask for an
  exception list and inventing one would be guessing at Leg A's intent for
  how such citations should read. Recorded here as a finding for Will/the
  orchestrator to rule on (e.g. an explicit exemption comment convention, or
  leave the doc author to route around it).

### B3 — ctest registration

`tests/cmake/hygiene.cmake` (new) adds `FileSizeCeiling` (label `hygiene`)
running `check_file_sizes.sh -C ${CMAKE_SOURCE_DIR} --all`, and
`DocCitations` (label `hygiene`) running `check_doc_citations.py --repo
${CMAKE_SOURCE_DIR}`. Included from `tests/CMakeLists.txt` via
`INCLUDE(cmake/hygiene.cmake)`, appended after the existing
`INCLUDE(cmake/packets_skills.cmake)` line, following the file's own stated
rule ("ADD A NEW TEST TARGET TO THE FRAGMENT FOR ITS DOMAIN"). No `add_test`
of a script pre-existed under `tests/cmake/`; `tests/cmake/eqe1.cmake` (script
tests `check_sender_reachability`, `check_build_tree_orphans`) supplied the
closest precedent for `ADD_TEST(... COMMAND python3/bash <script>)`, and
`GTEST_DISCOVER_TESTS(... PROPERTIES LABELS "live")` (three sites, e.g.
`tests/cmake/zone_integration.cmake:118`) supplied the label-property
convention, applied here via `SET_TESTS_PROPERTIES(<name> PROPERTIES LABELS
"hygiene")` since these are plain `ADD_TEST` targets, not
`GTEST_DISCOVER_TESTS`.

`ctest --test-dir <slot>/build -R 'FileSizeCeiling|DocCitations'
--output-on-failure`:

```
1/2 Test #3499: FileSizeCeiling ..................   Passed    6.66 sec
2/2 Test #3500: DocCitations .....................***Failed    0.59 sec
50% tests passed, 1 tests failed out of 2
```

`DocCitations` failing here is the expected pre-Leg-A-merge state the plan
names explicitly (B3: "DocCitations may FAIL before Leg A merges ... the
orchestrator sequences the merge so Leg A lands first"); its output is the
same 13-FAIL list itemized under B2 above, plus the `BOARD.md` skip note.

### Full verification bar

1. `cmake --build <slot>/build -j24` — **exit 0.**
2. `ctest --test-dir <slot>/build -j6 --output-on-failure` — **3498/3500
   passed.** The two failures are exactly the two expected: `AudioManagerTest
   .PreloadCommonSounds` (documented ALSA host quirk) and `DocCitations`
   (expected pre-Leg-A-merge FAIL, see B2/B3 above). No other regression.
   Total test time 118.65s. Steps 3-5 (build-gles2-x86.sh,
   build-arm-noble.sh, visual gate) not run per the plan's narrowed
   verification bar for Leg B (no code under `src/` changed).

### Deviations

- None from the territory list: touched exactly
  `scripts/refactor/check_file_sizes.sh` (modified, new `--all` mode added,
  existing modes' behaviour reconfirmed unchanged),
  `scripts/refactor/file_size_allowlist.txt` (new, two seeded entries at
  measured current line counts), `scripts/refactor/check_doc_citations.py`
  (new), `tests/cmake/hygiene.cmake` (new, one file), `tests/CMakeLists.txt`
  (one include line), `docs/decisions.md` (one new dated entry). `git status
  --porcelain` at finish: exactly those six paths.
- `check_doc_citations.py`'s citation-with-line extension set was widened
  slightly beyond the plan's bare-path list (`.cpp|.h|.py|.sh|.md`) to also
  accept `.hpp` and `.cmake` for the *with-line-number* form only — the
  bare-path-no-line FAIL rule stayed exactly as specified. Called out in
  case Will wants the with-line set narrowed to match exactly; nothing in
  the live docs currently exercises the difference (no `.hpp:NNN` or
  `.cmake:NNN` citation was found in this scan).
- No STOP condition fired: no file over 3,000 lines under `--all`.

## Review — Leg C

Branch `task/K00-legC-citations`, base tip `e888e89c` (confirmed at
checkout, after both K00 Leg A and Leg B merged).

Baseline: `python3 scripts/refactor/check_doc_citations.py` at the merged
tip, before any change — **14 live docs, 213 citations: 192 ok, 21 fail**,
matching the dispatch exactly.

### (1) Checker rules

`scripts/refactor/check_doc_citations.py`:
- A citation whose path starts with `review/` now resolves against
  `/mnt/projects/fable/review/` (`REVIEW_DIR`) instead of the repo root —
  OK if found there, FAIL otherwise. Fixed 2 of the 21 (both
  `review/T64/README.md`, which exists on disk under `/mnt/projects/fable/review/T64/`).
- An absolute-path citation (starts with `/`) is now reported `SKIP`, not
  `FAIL`, and excluded from the exit-code decision (only FAILs make the
  script exit 1). A `skip` count was added to the summary line and printed
  per-citation as `SKIP ... — cross-project citation`.
- **Regression caught before it shipped:** the naive implementation (making
  the citation regex's leading path separator optional, `/?[A-Za-z0-9...]`)
  turned four previously-correct OK results into false SKIPs —
  `refactor/EXECUTOR_PROTOCOL.md`'s `` `<slot>/scripts/build-gles2-x86.sh` ``
  convention (a bracketed worktree placeholder followed by a real
  repo-relative path) let the regex start its match at the "/" right after
  `<slot>`, misreading the genuinely-relative `scripts/build-gles2-x86.sh`
  as the absolute path `/scripts/build-gles2-x86.sh`. Fixed with a negative
  lookbehind (`(?<![A-Za-z0-9_.\->])`) before the optional leading "/", so a
  "/" only opens an absolute-path match when it is not itself glued to a
  preceding path-token character (a placeholder's closing `>`, in this
  case). Verified before/after: the four sites
  (`refactor/EXECUTOR_PROTOCOL.md:148,168,171,172`) are `OK`, not `SKIP`, in
  the final run below.
- Final run at the tip after all doc fixes: **14 live docs, 213 citations:
  212 ok, 0 fail, 1 skip.** Exit 0. The one SKIP is the deliberate
  cross-project citation at `docs/decisions.md:765` (see below).

### (2) The 21 FAILs — what each was and what was done

1-2. `CLAUDE.md:191` and `refactor/EXECUTOR_PROTOCOL.md:230`, both citing
   `review/T64/README.md` — resolved by the new `review/` rule (file
   exists at `/mnt/projects/fable/review/T64/README.md`). No doc edit.
3. `src/client/graphics/CLAUDE.md:129` cites
   `include/client/graphics/constrained_mesh_cache.h` (deleted T67) —
   rewrote the sentence to name the file without a live-looking
   directory-prefixed path (`constrained_mesh_cache.h`/`.cpp`, "formerly
   under ... both deleted by T67"); the `git show 49844c72:...` retrieval
   command was replaced with `git log --all -- '*constrained_mesh_cache*'`
   (no path+extension pair left in the doc to cite as if live).
4-5. Same line, `src/client/graphics/constrained_mesh_cache.cpp` cited
   twice (prose + the old `git show` command) — same fix as #3.
6. `src/client/graphics/CLAUDE.md:249` cites `./scripts/gdb_zone_crash.sh`
   — `git log --all -- '*gdb_zone_crash*'` and a repo-wide `find` both
   confirm this script never existed in this repo's history (not merely
   deleted). Replaced the invented "sends keystrokes to trigger
   movement/zoning" example with a factual pointer to the crash-debugging
   tool that does exist, `scripts/gdb_core_bt.sh` (core-dump backtrace
   extraction), and a one-line note that no zone-specific helper exists.
7-9. `docs/decisions.md:524-526` cite `include/client/graphics/ui/loot_window.h`,
   `src/client/graphics/ui/loot_window.cpp` (deleted U07c2 `0f25cb57`) and
   `refactor/D06b_publish_loot_vendor.md` (moved to `refactor/completed/`
   at some point before this unit — not part of K00's own archival).
   `include/client/graphics/ui/CLAUDE.md` and `refactor/T290_loot_window.md`
   confirm the loot window was rebuilt on the static-panel architecture as
   `LootPanelState` / `src/client/graphics/ui/loot_panel.cpp` (T290).
   Rewrote the bullet to say so, dropped the directory-prefixed dead paths
   to bare filenames (no longer citation-shaped), and repointed the design
   doc to its actual archived path `refactor/completed/D06b_publish_loot_vendor.md`.
10. `docs/decisions.md:758` (now :765 after edits) cites
   `world/zoneserver.cpp:423` — this is eqemu source, explicitly named two
   lines above as `/mnt/projects/akk-stack/code`. Rewrote the citation as
   the full absolute path `/mnt/projects/akk-stack/code/world/zoneserver.cpp:423`
   so the SKIP rule catches it (confirmed: shows as SKIP in the final run,
   not OK or FAIL — the checker cannot and should not verify a sibling
   repo's line count, only flag that the citation is intentionally
   external).
11-15. `docs/decisions.md:209,239,264(x2),270,711` cite `refactor/deferred.md`
   / `refactor/progress.md` in historical (dated) decision entries.
   Repointed each to the K00 archive paths
   (`refactor/completed/deferred_archive_20260903.md`,
   `refactor/completed/progress_archive_20260903.md`) since every
   `docs/decisions.md` entry describes a past decision, not the current
   queue. The "Documentation truth tiers" entry (line ~260) additionally
   had its "load-bearing" file list corrected (`refactor/BOARD.md` in
   place of the two archived trackers) with a parenthetical explaining the
   K00 archival — the only entry needing more than a path swap, because
   the sentence describes what's *live*, not what a past commit changed.
   Care was taken that the parenthetical's own archive-path mentions don't
   reintroduce a citable `refactor/deferred.md`/`refactor/progress.md`
   path (written as bare `deferred.md`/`progress.md`, i.e. without a `/`,
   so they don't match the citation regex at all — they're prose, not
   citations).
16-17. `refactor/BOARD.md:64,68` cite `graphics/irrlicht_renderer.h` and
   `graphics/eq/pfs.h` — include-relative shorthand missing the
   `include/client/` prefix. Rewrote both to the full repo-relative path;
   also fixed the adjacent `wld_loader.h` mention (was bare, non-citing,
   left as-is where it already didn't match, but given the full path too
   since it was in the same clause being edited: `include/client/graphics/eq/wld_loader.h`).
18-19. `refactor/EXECUTOR_PROTOCOL.md:133,166` cite `refactor/deferred.md`
   — both describe the CURRENT queue ("Boarded in ...", "read the
   ## STATUS block ... or simply run it"), so repointed to
   `refactor/BOARD.md` per the dispatch's explicit rule for this file. The
   `## STATUS` block wording was also corrected — `BOARD.md` has no such
   heading, it has a header block — to "the header block of
   `refactor/BOARD.md`".
20. `refactor/K_component_extraction.md:70` cites
   `include/client/client_context.h` — a header K01 will create, not yet
   present at this unit's tip. Rephrased per the dispatch's suggestion: "a
   new header, `client_context.h`, under `include/client/`" — no longer a
   `/`-joined path+extension citation.
21. `refactor/K_component_extraction.md:147` cites
   `refactor/K01_guild_component.md` — this file now exists (created by the
   parallel K01 executor in slot1 between Leg B and Leg C); no edit needed,
   confirmed OK in the final run.

### Verification

- `python3 scripts/refactor/check_doc_citations.py` (tip, after fixes):
  exit 0, `checked 14 live doc(s), 213 citation(s): 212 ok, 0 fail, 1 skip`.
- `cmake --build <slot>/build -j24` — exit 0 (re-ran `cmake -S <slot> -B
  <slot>/build` first so the build tree picked up the merged
  `tests/cmake/hygiene.cmake`/`INCLUDE()` line from Leg B).
- `ctest --test-dir <slot>/build -R 'DocCitations|FileSizeCeiling'
  --output-on-failure`: **both Passed** (`FileSizeCeiling` 28.97s,
  `DocCitations` 1.56s), 100% of 2.
- `ctest --test-dir <slot>/build -j6 --output-on-failure`: **3499/3500
  passed.** The sole failure is the documented `AudioManagerTest
  .PreloadCommonSounds` ALSA host quirk. `DocCitations` is green tree-wide.

### Deviations

- Territory held exactly: `scripts/refactor/check_doc_citations.py`,
  `CLAUDE.md` (read, not edited — its one FAIL resolved via the checker
  rule change, no doc edit needed), `src/client/graphics/CLAUDE.md`,
  `docs/decisions.md`, `refactor/BOARD.md`, `refactor/EXECUTOR_PROTOCOL.md`,
  `refactor/K_component_extraction.md`. No code under `src/`/`include/`
  touched (confirmed by `git status --porcelain`: six files, all doc/script,
  matching the territory list minus `CLAUDE.md`).
- One regression was introduced and caught within this same unit (the
  false-SKIP bug from the naive optional-leading-slash regex, detailed
  under "(1) Checker rules" above) — fixed before commit, not left for a
  future unit.
- No STOP condition fired.
