# T223 — three small command-layer defects: headless outcome, /newskills registry, phantom gequip7

Slot: slot3. x86. NO eqemu needed. Class A backlog items. Do NOT touch
include/client/bridge/*, src/client/bridge/*, eq_bridge.cpp (T220 territory)
or command_processor.cpp's /reply (T222).

1. T139/T142: headless (`--no-graphics`) build reports `outcome=queued` for
   36 renderer-only command names instead of a "requires graphics" failure —
   `Application::dispatchToActionOrRenderer` lacks the `EQT_HAS_GRAPHICS`
   guard at the fallback site. Add the guard; test in the headless test
   binary that a renderer-only command yields a failure outcome with a clear
   message. Also restore the lost console echo for the renderer `chat()`
   path if it is the same site (T142) — otherwise note it.
2. T143: `/newskills` (the real skills-window toggle) is registered in NO
   registry and registry 3 (renderer `processSlashCommand` if-chain) is
   invisible to `/help`. Register `/newskills` properly so `/help newskills`
   finds it; add a catalog hook so `/help` lists renderer-only commands with
   a "(graphics)" tag — derive from the if-chain, don't hand-duplicate the
   list if a table can be extracted.
3. T198: `config/eqe1_archives.json:8` lists `gequip7`, for which no
   `gequip7.s3d` exists anywhere under /mnt/projects/EverQuestP1999. Remove
   the entry; confirm no test or tool pins it.

## Verification
Red/green per item; full ctest; visual gate not needed unless graphics code
changed; Review in this file; evidence review/T223/. Commit:
`T223: headless refuses renderer commands; /newskills registered; phantom gequip7 dropped`.

## Review (2026-08-25, slot3, task/T223-command-hygiene from fable d7925e08)

All three items delivered, each with a measured red/green. Evidence in
`/mnt/projects/fable/review/T223/` (README.md there indexes the logs).

### Item 1 — headless refuses renderer commands (T139/T142)

Fix site is `Application::dispatchToActionOrRenderer()`
(`src/client/application.cpp`), registry-3 stage. The guard is
`!m_graphicsInitialized`, not a bare `#ifdef EQT_HAS_GRAPHICS`: the flag is
false in BOTH an `EQT_GRAPHICS=OFF` build and a `--no-graphics` run of a
graphics build, and the latter is the case Will actually hits (the deleted
`rendererCmd()` helper only ever covered the compile-time case). Behaviour
now:

| input | before | after |
|---|---|---|
| `/newskills`, no renderer | `outcome=queued registry=renderer` (pushed to ConsoleBridge, nobody listening) | `outcome=failed registry=renderer message="/newskills requires graphics mode (not available in this headless build)."` + the same line as a chat system message |
| `/wq high`, no renderer | queued | failed, `/wq requires graphics mode ...` |
| `/definitelynotacommand`, any mode | queued (render thread logs "Unknown renderer command") | `outcome=unrecognized registry=none`, nothing pushed |
| `/newskills`, graphics | queued | queued (unchanged) |

The name classification comes from item 2's catalog, so the refusal names
only real renderer commands -- the T139 note that "any unrecognized name"
was being reported queued is closed by the same change. `CommandDispatchResult`
gained `bool queued` so `printCommandResult()` can print `failed` for a
registry-3 refusal and `queued` only for an actual bridge push (previously
`registry == "renderer"` alone selected `queued`).

Test: `tests/test_headless_command_refusal.cpp` (new, links `willeq_core`)
drives `DispatchSlashCommand()` on a bare, never-`initialize()`d
`Application` -- `m_graphicsInitialized` is false, the same state as
`--no-graphics`. A process-level test could not reach this stage: pre-
connection everything except `/quit`/`/help` is HELD (T30) and there is no
server under ctest. Red (`targeted_red.log`, production fix stashed): 2 of 3
fail, message empty, registry=renderer queued. Green: 3/3.

T142's lost console echo: NOT the same site. `IrrlichtRenderer::
processSlashCommand()`'s `chat()` lambda pushes a `SystemMessage` only; the
echo lived in `EverQuest::AddChatSystemMessage()`'s `LOG_INFO`. Fixing it
means touching every `chat()` call in the renderer if-chain (graphics
territory, dozens of sites) -- not done, still open as recorded in
`deferred.md`'s T142 entry. The headless REFUSAL message does go through
`AddChatSystemMessage()`, so that one line is echoed.

### Item 2 — `/newskills` registered; registry 3 has a catalog (T143)

New TU `include/client/help/renderer_command_catalog.h` +
`src/client/help/renderer_command_catalog.cpp` (104 lines, graphics-
independent, compiled into `willeq_core`): a static table of the 42
commands / 70 identifiers in `IrrlichtRenderer::processSlashCommand()`'s
if/else chain, with aliases and one-line descriptions, plus
`FindRendererCommand()` (case-insensitive, slash-tolerant) and
`AllRendererCommands()`. `merged_help.cpp` lists it as a `Graphics
(graphics): ...` line in the overview, answers `/help graphics` with every
line tagged `(graphics)`, and answers `/help <name|alias>` with
`Registry: renderer (graphics)`. `HelpCoverage` gained `rendererCount`.

"Derive from the if-chain, don't hand-duplicate": the chain was NOT
restructured into table dispatch (that is the >100-line option the dispatch
said to scope out; it also runs on the render thread). Instead the catalog is
a hand-written table with a DRIFT GUARD:
`RendererCommandCatalogTest.MatchesRendererIfChainExactly` parses
`irrlicht_renderer.cpp` (path baked at configure time, same idiom as
`test_headless_shutdown`'s `WILLEQ_BIN_PATH`), extracts every `cmd == "/x"`
literal inside `processSlashCommand()`, and asserts set equality in both
directions. Red run: with `/placeholder` deleted from the catalog the test
failed with `in if-chain but NOT in catalog: /placeholder`. `/newskills`
red: `/help newskills` -> `Unknown command: newskills`; green: `=== newskills
===`, `Toggle the skills window`, `Registry: renderer (graphics)`.

Why the catalog and not registering `/newskills` in registry 1 or 2:
registry 1 would recreate exactly the duplicate T139 deleted; registry 2
cannot push a bridge event (T136). The catalog is registry 3's registration.

Catalog size honesty: the two catalog files are 145 lines total (41-line
header is mostly comment). The dispatch's "~100 lines" ceiling was for the
extraction; the code portion is under it and the whole unit's non-test
production diff is ~190 lines. Reported, not hidden.

### Item 3 — phantom `gequip7` (T198)

Removed from `config/eqe1_archives.json` AND from the built-in fallback copy
of that list in `IrrlichtRenderer::loadEQE1ArchiveConfig()`
(`renderer_effects.cpp`, used only when the JSON is absent -- the plan named
the JSON, the fallback carried the same phantom). No test or tool pinned
it: `grep -rn gequip7` hits only the two lists and historical refactor
notes; `tools/entity_composite_builder.cpp` uses its own
`GLOBAL_CHR_ARCHIVES` table which never listed it.

Test: `tests/test_eqe1_archive_config.cpp` (new). First cut checked
`<client>/<name>.s3d` and FAILED on `textures` -- which exists ONLY as
`textures.eqe1`; the loader opens `.eqe1` packs, so the S3D dir is the
wrong oracle. Corrected to `<EQE1 dir>/<name>.eqe1` (asset-gated skip).
Red on the original config: `configured global archive has no EQE1 pack:
/mnt/projects/EverQuest_ETC1/gequip7.eqe1`; green after. Second case
greps `renderer_effects.cpp` for the `"gequip7"` literal.

### Verification bar

1. `cmake --build build -j24`: OK (`build_green.log`).
2. `ctest`: **2776/2777**, sole failure `AudioManagerTest.PreloadCommonSounds`
   (documented ALSA quirk). +12 new tests (7 help/catalog, 3 headless, 2
   archive config). `ctest_full.log`.
3. `build-gles2-x86.sh`: OK (`build_gles2.log`).
4. Visual gate: PASS, goldens untouched (`visual_gate.log`); the only
   graphics-side edit is the fallback archive list string.
5. `build-arm-noble.sh`: OK, exit 0 (`build_arm.log`).
6. `check_file_sizes.sh fable`: 5 ok, 1 WARN (`application.cpp` 2345 lines,
   pre-existing warn, +~35 lines here), 0 FAIL.

### Deviations

- Guard is runtime (`m_graphicsInitialized`) rather than the `#ifdef` the
  plan literally names -- it is a superset (covers `--no-graphics` too).
- Unknown names are now `unrecognized` instead of `queued` in graphics mode
  as well. Consequence of classifying by catalog before pushing; scoped in
  because the drift guard makes the catalog authoritative.
- T142 console echo NOT fixed (different site, graphics territory) -- noted.
- Territory: untouched `include/client/bridge/*`, `src/client/bridge/*`,
  `eq_bridge.cpp`, `command_processor.cpp`.
