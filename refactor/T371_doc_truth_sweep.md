# T371 — documentation truth sweep after T352–T370

Class P (Will: stale docs are a first-class defect). slot2, no server.
`DocCitations` proves paths resolve; it does not prove CLAIMS. Nineteen
units merged since the last orientation rewrite (T352–T370) changed:
command names (`/skills`, `/inventory`), the completion path (three
registries), the vision pipeline (`vision_type.h`, `vision_ambient.cpp`,
the light-culling rule), the send path (all senders via `PacketSender`,
the send-guard bypass on it), the component pattern (the `EverQuest&`
handle is at ZERO, no friend lines), the chr-cache accounting, `/warp`,
the UI toggle keys, willeq.db v6 (`endurance_cost`).

1. For each of `CLAUDE.md`, every `*/CLAUDE.md` under `include/` and
   `src/`, `README.md`, `docs/module_map.md`, `docs/decisions.md` (only
   the orientation parts), `docs/*.md` that name any of the areas above:
   read it against the tree and list every sentence that is now FALSE or
   names a thing that no longer exists (`/newskills`, `/newinv`,
   `setOnSpellLand`, `FindEntityByName`, `StartCombatMovement(name)`,
   `Test_SetBypassSendGuardForSeam` on EverQuest, `friend class` for the
   components, "data-only" zone lights, "Tab covers registries 1+2",
   `kWilleqDbExpectedVersion 5`, `drawZoneLineOverlay`, the collision
   registration API, …). Grep is the start; reading is the proof.
2. Fix each in place with the measured truth (cite the unit). Do not add
   prose; correct it.
3. Add a `DocClaims` hygiene test, next to `DocCitations`: a small list of
   (doc, forbidden phrase) pairs for the retired names above, so a retired
   name reappearing in orientation docs fails ctest. Register under
   `tests/cmake/hygiene.cmake`.
4. Board header suite count (it says 3,760; the tip runs ~3,940) — make
   the header say how to get the number rather than a number, or update
   it and say when.
5. Review: the table of false sentences found, per file.

## Proof

`ctest -j6` (the two hygiene tests + the new one), file sizes. No
graphics change → no gate.

STOP: none expected.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, branch
`task/T371-doc-truth-sweep`, base `fable` tip `a7e760dc` (verified before
any work). No server, no graphics change, no gate. `fable` advanced to
`3964d100` (T369 merge + the T372 plan) DURING this unit; the sweep is
against `a7e760dc` and T369/T370 had no `## Review` at that base, so they
contributed no measured truths — that is a deviation from the plan's
"T352 through T370" and is why the table below cites T352-T368 only.

**Headline: 21 false sentences across 8 files. The docs were NOT uniformly
stale — T354, T363, T365, T366 and T368 each corrected the docs they
touched, and `/newskills`, `/newinv`, `setOnSpellLand`, `FindEntityByName`,
`Test_SetBypassSendGuardForSeam`-on-EverQuest, `drawZoneLineOverlay` and the
collision-registration API were ALREADY absent from every orientation doc.
What survived is a different class entirely: (a) COUNTS, every one of which
had drifted, and (b) sentences citing board items that were closed under
them.** Both are invisible to a name-based scan, and (a) is boarded as
**B130**.

### Per-file table of false sentences

| # | File | The false sentence (abridged) | The measured truth | Proof |
|---|---|---|---|---|
| 1 | `include/client/graphics/ui/CLAUDE.md` | "`/help …` lists registries **1+2** deduplicated" | `/help` lists all three; `BuildCategoryOverview` appends registry 3's whole catalog as one `Graphics (graphics):` line | `src/client/help/merged_help.cpp` `BuildCategoryOverview`, the `AllRendererCommands()` block (T223) |
| 2 | same | registry 3 is "Not queryable by `/help` (T143)" | It has been queryable since T223 and tab-completable since T365 | `src/client/help/renderer_command_catalog.cpp` (45 rows), `RendererCommandCompletions()`; board **B013** closed as STALE by T347 |
| 3 | same | registry 2 has "**142** `registerCommand({...})` calls at `9684644f`" | 143 | `grep -c 'registerCommand({' command_processor_registration.cpp` = 143; `/cleartarget` at `:217`, T347 (B012) |
| 4 | same | "` / NumLock / Numpad+ … **autorun is a no-op** — `EqActionHandler::toggleAutorun()` is a stub … (B002)" | Autorun works; the route is renderer-side (`RA::ToggleAutorun` → `PlayerCameraComponent::toggleAutorun()`) | Board **B002** struck (T343, live-proven T345, `review/T345/`); `tests/test_autorun_arm.cpp` |
| 5 | same | "ESC … Clear target (keybinding only — no slash command reaches it, B012)" | `/cleartarget` reaches the same `ActionDispatcher::clearTarget()` | `command_processor_registration.cpp:217`; board **B012** struck (T347) |
| 6 | `src/client/input/CLAUDE.md` | "`InputAction` enum (**40** discrete one-shot actions …, re-counted J21)" | 33 | parsed count of the enum in `include/client/input/input_handler.h`; T365 deleted `ToggleInventory`/`ToggleSkills`/`ToggleGroup`, T368 deleted `ToggleVendor`/`TogglePetWindow`/`ToggleTrainer`/`ToggleSpellbook` (B090/B100) |
| 7 | same | "`HotkeyAction` enum (**80** actions …, counted 2026-09-03)" | 94 | parsed count of the enum in `include/client/input/hotkey_manager.h` |
| 8 | same | "its **142** `registerCommand({…})` calls" | 143 | as #3 |
| 9 | `include/client/bridge/CLAUDE.md` | "## Intent Types (**35**)" | 36 | parsed `std::variant` in `include/client/events/renderer_intents.h` |
| 10 | same | the **Audio** bullet lists two intents | three — `PlaySoundIntent` is missing | `renderer_intents.h`; added by T328 Leg B `1685492d` |
| 11 | `src/client/graphics/CLAUDE.md` | "`loadZoneSequential()` — **13-step** …" | twenty `loadZoneStepN_*()` bodies (0..13 with 8a-8f and 12b separate) — and the component table in the SAME FILE already said twenty | `include/client/graphics/zone_load/zone_load_component.h:196-215` |
| 12 | same | "L13 closed that handle down to **151** uses" (stated as current) | 165 comment-filtered at this tip; T366's census read 163 | comment-stripped `\bowner_\b` count over `src/client/graphics` + `include/client/graphics`, excluding `zone_shader_callbacks.h` |
| 13 | `CLAUDE.md` | "L13 took that last handle from 1,065 uses to **151**" (stated as current) | as #12 | as #12 |
| 14 | `docs/module_map.md` | "Ceilings: warn >1,500, fail >3,000 … **As of Batch J no file is over the 3,000-line fail line**" | 1,500 is the ENFORCED ceiling tree-wide; the allowlist is empty | `scripts/refactor/check_file_sizes.sh:160-163` (`--all` mode), M00 / `docs/decisions.md` 2026-09-04 |
| 15 | `README.md` | "The default suite has **2746** registered tests across **123** test executables" | 3,949 across 232 | `ctest -j6` summary line; `ls build/bin \| grep -c '^test_'` |
| 16 | same | "`-L live` # **19 tests across 3 binaries**" | 3 binaries confirmed; the test count is not verifiable without an `EQT_LIVE_TESTS` configure, so the number is removed and the binaries named | `GTEST_DISCOVER_TESTS(... LABELS "live")` in `tests/cmake/{ui_rendering,renderer_guards,zone_integration}.cmake` |
| 17 | same | "Ctrl+D / **PageDown** \| Strafe right" | Ctrl+D / **Home**; PageDown scrolls the UI | `src/client/input/hotkey_manager.cpp:466` (`KEY_HOME`, "T313: was Page Down") |
| 18 | same | "Ctrl+L \| Lock/unlock UI" | No such binding or action exists | no `KEY_KEY_L` + Ctrl binding in `hotkey_manager.cpp` (L alone is `CycleObjectLights`, `:556`); the lockable UI was `WindowManager`, deleted in U07c |
| 19 | same | "Ctrl+S \| Save UI layout" | No such binding or action exists | as #18 (`KEY_KEY_S` is `MoveBackward`, `:461`) |
| 20 | same | "Ctrl+Shift+R \| Reset UI defaults" | No such binding or action exists | as #18 |
| 21 | `refactor/BOARD.md` | "**Suite:** 3,759 of 3,760 passing at `661cb078`" | 3,948 of 3,949 at this tip | `ctest -j6` summary line |

Also corrected in passing, not counted as false: `README.md` gained the
PageUp/PageDown and mouse-wheel scroll rows (T313) that the removed rows had
crowded out, and `include/client/graphics/ui/CLAUDE.md`'s `/help` paragraph
learned T365's command-before-category resolution order.

Two lines are DELIBERATE tombstones and were marked `RETIRED-OK` rather than
rewritten: `docs/decisions.md`'s B058 ruling ("rename `/newskills` to
`/skills`") and `include/client/graphics/ui/CLAUDE.md`'s "renamed from
`/newskills` with NO alias".

### The three that matter most

1. **#1 + #2 (the `/help` pair).** T365 corrected the TAB half of this
   paragraph and left the `/help` half standing three lines below it, so the
   file contradicted itself: "all three registries, the same three `/help`
   lists" in one sentence and "lists registries 1+2 deduplicated" in another.
   A reader auditing `/help` would have concluded registry 3 was invisible
   and reopened B013 — which T347 had already closed as stale for exactly
   that reason.
2. **#18-20 (three phantom hotkeys in `README.md`).** Ctrl+L / Ctrl+S /
   Ctrl+Shift+R were bindings of the draggable-window UI deleted in U07c
   (~35K lines). They survived in the README's Debug table for the whole
   static-panel era. This is the only entry in the sweep that would make a
   USER press a key and get nothing.
3. **#4 + #5 (two closed board items cited as open defects).** Both
   sentences told a reader that a working feature was broken and pointed at
   a struck board entry as evidence. This is the reverse of the usual doc
   defect and the most expensive: it invites a unit to "fix" what is already
   fixed. Neither T343/T345 nor T347 struck the doc line when they struck
   the board entry — the "strike EVERY mention" rule
   (`feedback-pin-every-fix`) was applied to `BOARD.md` only.

### `DocClaims`

`scripts/refactor/check_doc_claims.py`, registered as `DocClaims` in
`tests/cmake/hygiene.cmake` under label `hygiene`, beside `DocCitations` and
`FileSizeCeiling`.

- **What it checks:** a table of **23** (retired phrase, what is true now,
  the unit that measured it) rows — `/newskills`, `/newinv`,
  `setOnSpellLand`, `SpellLandCallback`,
  `EntitiesComponent::FindEntityByName`, `drawZoneLineOverlay`,
  `addRegionToCollision`, `addDoorToCollision`, `addObjectToCollision`,
  `findBspRegionForPoint`, `clearCollisionDebugLines`,
  `checkResourceRequirement`, `SkillData::endurance_cost`,
  `SkillData::mana_cost`, `EverQuest::m_test_bypass_send_guard`,
  `kWilleqDbExpectedVersion 5`, and the seven deleted `InputAction::Toggle*`
  enumerators. Each phrase is matched with identifier boundaries (so
  `/skills` does not trip the `/newskills` row and `findEntityByName` does
  not trip the qualified one). A hit prints file, line, the truth and the
  unit.
- **Scope:** the ORIENTATION docs — root `CLAUDE.md`, every `*/CLAUDE.md`,
  `README.md`, `docs/*.md`. **12 files at this tip.** Deliberately narrower
  than `DocCitations`' live-doc set: `refactor/BOARD.md` and
  `refactor/K_*.md` are ledgers of struck work and name deleted symbols on
  purpose — running the table over them produced **39** hits, every one
  legitimate. Boarded as **B131**.
- **Escape hatch:** a line carrying the marker `RETIRED-OK` is exempt, for
  deliberate tombstones. Two such lines exist (above); the pass line reports
  the exemption count so the hatch cannot be used silently.
- **Planted-fault proof:** `/newskills` planted into the `K` row of
  `include/client/graphics/ui/CLAUDE.md`'s Player Mode key table (line 84),
  then `ctest -R '^DocClaims$'`:

  ```
  1/1 Test #3949: DocClaims ........................***Failed    0.57 sec
  DocClaims: 12 orientation docs scanned, 23 retired names in the table, 2 exempted line hits (RETIRED-OK)
  FAIL  include/client/graphics/ui/CLAUDE.md:84 names the retired '/newskills' -- renamed to /skills, no alias (registry 3) (T363 (B058)). Correct the sentence, or mark the line RETIRED-OK if it is a deliberate tombstone.
  DocClaims: 1 failure(s).
  ```

  The line was reverted from a byte-for-byte backup and the test passes on
  the corrected tree (0.39 s).

### Board header (plan item 4)

Rewritten to say HOW rather than WHAT: the header now opens "DO NOT QUOTE A
NUMBER FROM THIS LINE", gives `ctest --test-dir <slot>/build -N | tail -1`
for the count and `ctest -j6` for pass/fail, restates the ALSA invariant and
the exit-8 trap, and records the last measurement with its tip and date
(3,948 of 3,949 at `a7e760dc` + this sweep, 2026-09-05, slot2). `README.md`
got the same treatment for its own suite number.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | **exit 143** on the first invocation (10 min ceiling), then **EXIT=0**. Post-143 sweep run in full: zero non-ELF binaries over 273 files in `build/bin`, `libwilleq_core.a` present with 322 members and `libEtcLib.a` with 15 (`ar t`), no `build*/lib/st??????` temp files. Two targets had been **DELETED outright** by the kill — `test_static_ui_panels` and `test_t124_texture_holder_guards`, both named `Terminated` in the log tail — and were absent from `build/bin`; the rebuild restored both (16,011,704 B and 11,181,784 B). This is the absence shape the protocol warns about, and the ELF scan alone would have missed it |
| `ctest --test-dir build -j6` | first run died at gtest DISCOVERY on `test_zone_in_z_conversion` ("Process terminated due to timeout"); the binary was intact (`--gtest_list_tests` exit 0, 3 cases listed) — cold-cache contention, not truncation. Re-run: **exit 8, 3,948 of 3,949 passing, sole failure `AudioManagerTest.PreloadCommonSounds`** (the waived ALSA host quirk) |
| hygiene trio | `FileSizeCeiling` PASS (6.54 s), `DocCitations` PASS (0.33 s), `DocClaims` PASS (0.39 s) |
| `check_file_sizes.sh fable` | 3 files checked, **3 ok, 0 warn, 0 fail** — the three are T369's (`s3d_loader.{h,cpp}`, `model_loading_search.cpp`), which entered `fable` mid-unit; none of THIS unit's files is a source file. `--files` on the two new/changed non-doc files: `check_doc_claims.py` 190 lines ok, `hygiene.cmake` 26 lines ok |
| visual gate / ARM | not run — no graphics change (plan: "No graphics change → no gate") |

### Deviations

1. **T369 and T370 had no `## Review` at the base tip**, so the plan's
   "T352 through T370" was executed as T352-T368. T369 merged into `fable`
   during the unit; its subject (`S3DLoader::loadCharacters()`'s `_chr` path
   derivation, B105) is not described in any orientation doc, and
   `src/client/graphics/CLAUDE.md`'s one-line `s3d_loader.cpp/h` entry
   ("High-level zone/model/character loader") remains true against it.
2. **`DocClaims`' scope is narrower than the plan's wording.** The plan said
   "orientation docs"; the implementation had to say so explicitly rather
   than reuse `DocCitations`' live-doc set, because that set includes the
   board. Recorded as B131 rather than absorbed.
3. **No `docs/*.md` outside the three named files needed a correction** —
   `docs/CLAUDE.md` (coordinate systems, the load-bearing heading offset) was
   read line-by-line against `zone_entry.cpp` and
   `entities_component.cpp` and is accurate.

### New board items

**B130** (P) — the orientation docs' hand-maintained COUNTS have no
regression net and every one of the six had drifted. **B131** (P) —
`refactor/BOARD.md` and the batch close-outs are outside `DocClaims`' scan by
design, so a future board entry describing live code with a retired name is
uncaught.
