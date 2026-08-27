# T294 — does the paperdoll survive the UI atlas in the REAL client? (+ the dead `I` keybind)

Slot: slot1. x86. OWNS eqemu (noclobber) — this needs a live client with a
built player entity. Class A/B. Territory: `src/client/graphics/ui/ui_renderer.*`
(draw ordering only), `renderer_entities.cpp` `toggleInventory()`,
`static_panels.cpp` if the fix lands there, `src/client/graphics/CLAUDE.md`
(the stale paragraph), tests. Do NOT touch character_model_view internals or
the atlas builder.

## The finding (audit, 2026-08-27) — verify it before fixing it
The paperdoll works: `CharacterModelView` renders the player's live
`EntityAppearance` into a 304x256 RTT, blitted by `UIRenderer::drawTexture()`.
**But every paperdoll capture in the tree was taken through the ATLAS-LESS
fallback** (`ui.init(driver, nullptr, ...)` in `ui_inventory_capture.cpp:315`
and `test_static_ui_panels.cpp:468,486`), where `drawSprite` also draws
immediately and the ordering is accidentally correct.

In the real client the atlas IS loaded (`irrlicht_renderer.cpp:655-672` when
EQE1 is configured), and then:
- `drawTexture()` draws IMMEDIATELY (`ui_renderer.cpp:103-119`, and the header
  says so at `ui_renderer.h:66-72`);
- `drawPanel()`/`drawSprite()` ACCUMULATE into `atlasQuads_` and flush in
  `endFrame()` (`ui_renderer.cpp:28-31, 120-147`).
So the popup background is painted AFTER the paperdoll, over the same rect —
and the atlas quad is opaque in practice, because `panel_background` is
authored at alpha 180 but the bound texture is the RGB ETC1 entry and its
`ui_atlas_alpha` companion is never bound (T279). Live captures show panels as
solid slabs (`review/T276/t276_2_spellbook_scribed_1024x768.png`).
**Predicted symptom: with the atlas loaded, the paperdoll is invisible —
covered by its own panel background.**

## Scope
1. **Observe first.** Live client, `--eqe1-path` (atlas loaded), zone in, wait
   for the player entity to build, `/newinv`, screenshot. Is the paperdoll
   visible? Record the answer either way — if it IS visible, the inference was
   wrong and this unit closes as not-a-defect with the evidence.
2. If it is covered: fix the ordering. The minimal shape is to make the
   paperdoll blit participate in the same ordered flush as the atlas quads
   (or flush before it), NOT to special-case the paperdoll. State why the
   chosen shape cannot silently break other immediate-mode draws.
3. **Pin it**: extend the atlas-backed capture path
   `runAtlasPanelCapture()` (`ui_inventory_capture.cpp:490-540`) — which
   currently never sets `inv.paperdollTexture` — so the atlas path is covered
   at all, then a pixel test in T275's style (`tests/test_ui_atlas_pixels.cpp`)
   asserting paperdoll pixels survive. Red/green measured.
4. **The dead keybind**: `HotkeyAction::ToggleInventory` reaches
   `IrrlichtRenderer::toggleInventory()`, an EMPTY STUB left by U07c2
   (`renderer_entities.cpp:351-353`), so `I` does nothing and the popup is
   reachable only via `/newinv`. Wire it to the same path `/newinv` uses (one
   body). Test it.
5. Fix the stale claim in `src/client/graphics/CLAUDE.md:203-206` that the
   paperdoll "was deleted in 0f25cb57 and never rebuilt" — false since T60.

## Verification
Full ctest (`-j6`); visual gate; Review; evidence review/T294/ with the live
before/after screenshots. Commit: `T294: the paperdoll survives the UI atlas; I opens the inventory`.

---

## Review (executor, 2026-08-27, slot1)

**The inference was RIGHT, and it was verified live before anything was
changed.** Evidence: `/mnt/projects/fable/review/T294/` (README.md +
`before/` and `after/` live captures, identical tour and flags).

### Scope 1 — observe first

Live client, 800x600, surfaceless EGL, `--eqe1-path /mnt/projects/EverQuest_ETC1`
(so the atlas IS loaded), Testchar zoned into befallen, `/say #set time 11 0`,
`/newinv`, screenshot. `before/01_inventory_open_atlas.png`: the paperdoll rect
`(248,145)-(552,401)` is a solid slab. Sampled RGB at (400,270) = **(10,10,18)**
— the popup's `panel_background`. The log for that same run shows
`CharacterModelView initialized: 304x256 RTT=yes`, `setCharacter race=1
gender=0`, `Created model for race=1 gender=0 scale=1`: the doll was built and
rendered into its RTT every frame the popup was open. Only the blit was
covered. Predicted symptom confirmed exactly.

### Scope 2 — the fix

`UIRenderer`'s `atlasQuads_` became an ordered command list: `SpriteQuad` →
`DrawCmd`, gaining `irr::video::ITexture* texture = nullptr` where `nullptr`
means "the atlas texture". `drawRect`/`drawSprite` push as before (three
initialisers, the fourth defaulted). `drawTexture()` now pushes a command
carrying its own texture and the T60 V-flipped `srcRect`, tinted opaque white
— the same colour `draw2DImage` falls back to when `colors` is null (T275), so
the blit itself is byte-identical. `endFrame()` replays the list in submission
order, picking `q.texture ? q.texture : atlasTex` and skipping a command whose
texture resolves to null (previously the whole flush was skipped when the atlas
had no texture; the behaviour for atlas quads is unchanged).

**Why this cannot silently break other immediate-mode draws.** `drawTexture()`
was the ONLY immediate draw left in `UIRenderer` on the atlas path, and it has
exactly one caller in the tree (`static_panels.cpp:319`, the paperdoll). The
fallback path (`atlas_ == nullptr`) is deliberately left immediate: there
`drawRect`/`drawSprite` are immediate too, so submission order is already draw
order and queueing would change nothing but add a copy. Nothing outside
`UIRenderer` draws between `beginFrame()` and `endFrame()` —
`renderer_frame.cpp` calls only `render*Panel()` helpers in that window, all of
which go through `UIRenderer`. The text batch still flushes last, unchanged.
Pinned in both directions by `PaperdollSurvivesOnTheFallbackPath` and
`PaperdollStaysInsideItsRect`.

`after/01_inventory_open_atlas.png`: a textured human paperdoll, same tour,
same flags. (400,270) = (21,5,5) on the model; (30,30,35) — the RTT's own
background — elsewhere in the rect.

### Scope 3 — pinned, red/green measured

`runAtlasPanelCapture()` now sets `inv.paperdollTexture` (a solid-magenta
stand-in: what the ATLAS capture has to demonstrate is that the blit survives
the flush; the doll's own appearance is already covered by
`runPaperdollCaptures()`). `tests/test_ui_atlas_pixels.cpp` gains
`UIAtlasPaperdollTest` with three cases.

| | measured |
|---|---|
| RED (ordering change reverted) | `PaperdollSurvivesTheAtlasFlush` fails: *"paperdoll blit did not survive the atlas flush (got 10,10,18)"* — the same pixel the live capture shows |
| GREEN | `test_ui_atlas_pixels` 9/9 |

### Scope 4 — the dead keybind

`toggleInventory()`, `openInventory()`, `closeInventory()` and
`isInventoryOpen()` (`return false;`) were ALL WindowManager-era stubs. The
plan named `toggleInventory()`; the other three were fixed in the same pass
because they are the same two lines of state and leaving `isInventoryOpen()`
returning a constant `false` next to a working toggle would be a fresh
landmine. **Deviation flagged:** that is three functions beyond the plan's
letter, in the named file, and `openInventory()` / `closeInventory()` still
have no production caller other than `toggleInventory()` itself.

`/newinv`'s open-coded copy of the toggle is deleted; the handler now calls
`toggleInventory()` and reports `isInventoryOpen()`. One body.

New `tests/test_inventory_toggle.cpp` (4 cases) drives the REAL
`RendererEventReceiver::OnEvent(KEY_KEY_I)` → `drainActions()` →
`processPlayerInput()` chain on a bare, never-`Init()`'d `IrrlichtRenderer`
(the T66/T233/T235 no-device idiom). It needs no `friend` declaration: the
receiver is a free-standing object and the hand-off under test is exactly
`drainActions()` → `processPlayerInput()`, both public.

| | measured |
|---|---|
| RED (stubs restored) | 3/4 fail. `IKeyQueuesToggleInventoryAction` still passes, localising the break to the body, not the binding |
| GREEN | 4/4 |

`I` is not verifiable in a live `--script` run — the client has no key
injection, `--script` drives slash commands only. That is why the keypress is
pinned by test rather than by capture.

### Scope 5 — stale doc

`src/client/graphics/CLAUDE.md`'s "deleted in 0f25cb57 and never rebuilt"
replaced with what is actually true since T60, with pointers to T60 and to
this unit.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | EXIT=0 |
| `ctest -j6` | **3215/3216**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | EXIT=0 |
| visual gate | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | EXIT=0 |
| `check_file_sizes.sh fable` | 5 ok, 1 FAIL — `irrlicht_renderer.cpp` 3051 > 3000, **pre-existing** (3059 on `fable`); this unit reduced it by 8 lines |

**Deviation:** the visual gate failed on the first full `ctest` run with
*"benchmark CSV has no `cpu_proc_ms` column — the build-gles2/ binary predates
T291"*. That is the stale `build-gles2/` tree in this slot, not a regression;
step 3 of the bar rebuilds it and the gate then passed. Re-ran the suite after,
hence the 3215/3216 above.

### Trap for the next executor

The red proof was made by editing `ui_renderer.cpp` and restoring it from a
backup with `mv`. **`mv` preserved the backup's older mtime, so CMake never
recompiled the TU** — the next hour of builds, tests and live runs silently
used the unfixed object. It presented as a GLES2 driver bug (an atlas quad
drawn immediately before an RTT blit made the blit vanish) and cost four probe
cycles to disprove. There is no driver bug. `touch` any hand-restored source.
