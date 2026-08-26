# T275 — UIRenderer::drawRect() draws nothing visible in the gem panel (T272 finding)

Slot: slot2. x86. OWNS eqemu only if a live capture is needed (the static
UI panels may be reachable pre-connection — try that first; noclobber if
not). Class A/B. Territory: src/client/graphics/ui/ui_renderer.*,
ui_atlas.*, the gem-panel draw path in static_panels.*, tests. Do NOT
touch race data/getters (T274 slot1), read-book (T273 slot3).

## Finding (T272 Review, review/T272/t272_5_gemcolumn_zoom.png)
Neither T272's new gold destination marker nor the PRE-EXISTING
Ready-state green borders appear in a real capture, though both use the
same `UIRenderer::drawRect()` idiom that other panels apparently rely on.
So either drawRect is a no-op in this configuration, its geometry lands
off-panel, or it is drawn and then overdrawn.

## Scope
1. Determine which: instrument the call (log rect + clip + the texture/
   atlas state), capture a frame with `--dump-frames-dir` and
   `--eqe1-path`, and check the pixels at the logged rect. Establish
   whether ANY drawRect anywhere is visible (grep its call sites — if some
   are visible and some not, the difference is the finding).
2. Fix the cause (missing atlas binding, wrong blend/depth state for
   untextured quads, coordinate space, draw order vs the panel background,
   or an early-out). T205's UI-atlas history and T193/T204's texture
   lifetime notes are the nearest prior art.
3. Test: a static-panel pixel test (T54 added the first ones — see
   tests/ for the panel harness) asserting the border pixels are the
   expected colour after a draw. Red/green measured.
4. Capture before/after crops for Will.

## Verification
Full ctest; visual gate PASS with goldens untouched (UI is not in the
goldens — confirm); Review; evidence review/T275/. Commit:
`T275: UIRenderer rectangles are visible again -- <cause>`.

---

## Review (executor, 2026-08-26, slot2, branch `task/T275-drawrect`)

### Headline

`drawRect()` was never a no-op. **The entire UI atlas was sampling opaque
black**, so every atlas-backed quad -- panel backgrounds, slot backgrounds,
slot borders, HP/mana/stamina bars, the Ready-state gem borders and T272's
gold marker -- painted black on black. Two independent defects, both fixed:

1. **Root cause (invisible).** `ConstrainedTextureCache::uploadCompressedETC1()`
   (and the sibling `uploadCompressedV2()`) set
   `GL_TEXTURE_MIN_FILTER = GL_LINEAR_MIPMAP_LINEAR` whenever `mipLevels > 1`.
   An `eqe1_builder` ETC1 mip chain stops at 4x4 (ETC1's block size): for the
   256x256 `ui_atlas` that is **7 levels where GLES2 requires 9** (down to
   1x1) for mipmap completeness. Nothing ever called `glGenerateMipmap`. A
   texture requesting a mipmap filter over an incomplete chain is INCOMPLETE,
   and sampling an incomplete texture returns **opaque black (0,0,0,1)** --
   exactly the "black box with text on it" the panels rendered as.

   The 3D path never showed it because
   `COpenGLES2Driver::applyMaterialRenderState()` overwrites texture unit 0's
   `MIN_FILTER` with plain `GL_LINEAR`/`GL_NEAREST` on **every** material
   bind, stomping the bad filter before any 3D sampler sees it. The 2D path
   (`draw2DImage`, which the whole static UI goes through) binds the texture
   **without touching filters** -- so `ui_atlas` reached the sampler still
   incomplete. Fixed to plain `GL_LINEAR` in both compressed upload paths.

   P42 had already drawn precisely this conclusion for the RGBA upload path
   in the same file ("requested filtering against an incomplete mip chain --
   no code path here ever called glGenerateMipmap") and simplified it to the
   filter that actually took effect. **The two EQE1 compressed paths were
   missed by that sweep.** Same reasoning, same resolution, and -- as the
   visual gate confirms -- identically a no-op for 3D rendering.

2. **Second defect (wrong colour).** `UIRenderer::endFrame()` passed
   `nullptr` for `draw2DImage`'s `colors` argument, so the driver fell back
   to opaque white and **every per-quad tint accumulated during the frame was
   discarded at the last step**. With only defect 1 fixed, the gem's Ready
   border measured `255,255,255` instead of `100,200,100`
   (`red_mipmap_fix_only_tint_still_dropped.txt`). Now passes `&q.color`.

Neither needed a UI redesign; no STOP condition was hit.

### Why this was never caught

Every UI acceptance capture in the tree's history (T54/T57/T60/T62, via
`tools/hwcaps/ui_inventory_capture`) ran `ui.init(driver, /*atlas=*/nullptr,
...)` -- the **fallback** path, where `drawRect()` calls
`draw2DRectangle()` directly and no atlas texture is involved. The real
client always loads `ui_atlas`. **The atlas path had zero pixel coverage.**
`tests/test_ui_atlas_pixels.cpp` (new) closes that: a real headless GLES2
device + the real `ui_atlas` out of `willeq.eqe1`, framebuffer read back and
asserted on. `ui_inventory_capture` gained `--eqe1-path`, which drives the
same panels through a real `UIAtlas` -- the before/after evidence.

### Red / green (measured)

| file | state | result |
|---|---|---|
| `red_no_fixes.txt` | fable tip | **4 of 6 fail** -- atlas drawRect, bar_hp sprite, gem Ready border, gold marker all sample `0,0,0` |
| `red_mipmap_fix_only_tint_still_dropped.txt` | mipmap fix only | **3 of 6 fail** -- now visible, but the gem Ready border reads `255,255,255`: the dropped tint, isolated |
| `green_test_ui_atlas_pixels.txt` | both fixes | **6/6 pass** |

`SpellGemWithoutMarkerIsNotGold` passes in every state on purpose: it is the
control that keeps the two marker assertions from both passing on a renderer
that painted the whole panel one colour.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | pass |
| `ctest --test-dir build` | **3049/3050**; sole failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds` |
| `scripts/build-gles2-x86.sh` | exit 0 |
| Visual gate | **PASS**, `refactor/goldens` untouched (`git status` clean of it). The gate passing is itself the evidence that the min-filter change is a no-op for 3D -- as predicted from the driver's unconditional per-bind filter override. |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | **no new FAILs.** The one FAIL, `entity_renderer.cpp` (3340), is untouched and already 3340 on `fable`. `constrained_texture_cache.cpp` was already a WARN (2175 -> 2193, comments only). |

Evidence: `/mnt/projects/fable/review/T275/` (README, before/after captures
and 3x crops, all logs).

### Deviations

- **Territory.** The plan scoped `ui_renderer.*`, `ui_atlas.*`, the gem-panel
  draw path and tests. Defect 1's actual cause is in
  `src/client/graphics/constrained_texture_cache.cpp` -- two `glTexParameteri`
  lines. Fixed there rather than papered over in `UIAtlas`, since the bug
  affects every EQE1 texture reaching a 2D draw, not just the UI atlas. No
  sibling unit (T273 read-book, T274 race data) owns that file.
  `tools/hwcaps/ui_inventory_capture.cpp` was extended (additive
  `--eqe1-path` mode) to produce the before/after evidence on the atlas path.

### Observed, NOT fixed

- **The ETC1 alpha companion has the same latent defect.** Companion textures
  are uploaded with `GL_NEAREST_MIPMAP_NEAREST` over the same 4x4-terminated
  chain, and `applyMaterialRenderState()` binds texture unit **1** without
  ever setting its filters -- so nothing stomps it the way unit 0 is stomped.
  Left alone deliberately: whatever it currently samples is baked into the
  blessed goldens, so changing it is a re-bless decision for Will, not an
  executor's. Flagged for a follow-up unit.
- **`verify_eqe1` already reports the truncated chain** -- `ERROR: Entry 0
  'ui_atlas' mipLevels=7 (expected 9 for 256x256)`, and the same for every
  entry of every pack. Its expectation matches GL's completeness rule
  exactly. This unit is the first evidence that the truncation had a
  user-visible consequence; whether to fix the builder (generate 2x2/1x1
  levels) or keep the non-mipmap filter is a separate call.
- **Harness fact, recorded in the test's comments:** on the surfaceless-EGL /
  llvmpipe backend a frame whose content is *only* 2D colour quads never
  reaches the framebuffer, while the identical quads appear as soon as one
  font draw joins them. This is on top of the "first content frame is lost"
  quirk `ui_inventory_capture.cpp` already documents. Neither is what this
  unit fixed; both cost real diagnosis time and are now written down.
