# T424 — the spell fallback's falloff in RGB (B322); ambientMultiplier_ (B380); six unused UI sprites (B351)

Class B ×3, slot2, no server. Read the three board entries, T413's
§Review Leg C (the fallback is solid white 24-bit; keep additive, put
the cosine falloff in RGB — the shipped-data cascade rule), T423's §Review
(`ambientMultiplier_` one reader, zero writers), T416's §Review (the
UISprite census).

## Leg A — B322

`generateSpellParticleFallback()` writes the cosine into alpha over
constant white RGB; the 24-bit BMP drops it and the additive blend
(`GL_ONE, GL_ONE`) never samples alpha. Write the falloff into RGB
(premultiplied white × cosine) so the additive draw is a soft dome (T389's
generic dome is the oracle: soft, translucent). Regenerate → `--write-pins`
→ pack rebuild via the script (atomic; no ctest/gate/willeq running —
slot1 runs a LIVE client for T422: install only when no `willeq` process
exists; copy the old pack under `review/T424/`) → both sha pins, ONE
commit. Test pre-fix failing: the shipped BMP's centre-to-edge RGB
profile is a cosine, not flat; the pack entry matches. Report the
`eqe1MappedBytes` delta (never edit the manifest). Strike B322.

## Leg B — B380

`ambientMultiplier_` is read by `composeSceneAmbient()` and written by
nothing. Read what it was for (git log -S); if it was a zone/config
override with a lost writer, either wire the writer (the zone override
T423 named as an input) with a test, or delete the field and its read
(compiler proof). Say which. Strike B380.

## Leg C — B351

Six of 21 `UISprite` regions have no consumer. For each: is it a sprite
the oracle's window pieces draw somewhere fable does not yet (then
board its consumer under the panel item) or a leftover (delete the
region from the atlas builder and the enum; the atlas layout changes →
pack cascade in the SAME commit as Leg A's install if both change the
pack — sequence them so the pack is installed once). Strike B351.

## Proof

Build (background), `ctest -j6` (no expected failures), pre-fix/post-fix,
`TexturesReproducible`/`PackReproducible` green in every commit, visual
gate standalone + ARM (a moved image golden is a STOP), file sizes.

STOP: none expected.

## Review

Branch `task/T424-fallback-multiplier-sprites`, base `020403c2`, slot2
(RelWithDebInfo, `EQT_GLES2_IRRLICHT_DIR` = slot2's own tree). Commits:
**Legs A+C `0911184a`**, **Leg B `70e5de2a`**, **board + this Review — this
commit**. No server touched; no eqemu lock taken or needed.

### DEVIATION: Legs A and C share one commit

The plan asked for one commit per leg and also said Leg C's pack cascade goes
"in the SAME commit as Leg A's install if both change the pack — sequence them
so the pack is installed once". Both legs do change the pack, and the two
constraints cannot both hold across two commits: `PackReproducible --check`
compares the rebuild against the pinned hash **and** against the installed
pack, so whichever leg committed first would either pin a hash nothing had
installed, or install a pack the other leg's commit then invalidated. The
shipped-data cascade rule (all of `data/textures/`, `data/textures.sha256`,
`willeq.eqe1`, `data/willeq.eqe1.sha256` in one commit) is the harder
invariant, so Legs A and C are `0911184a` and the shared pack was installed
**once**. Leg B, which touches no asset, is its own commit as planned.

### Leg A — B322

**Before/after, measured on the shipped BMP** (32x32, 24-bit, unchanged
format):

| | before | after |
|---|---|---|
| centre-to-edge R, row y=16, x=0..16 | `255` x17 | `12, 37, 61, 85, 108, 130, 151, 170, 188, 204, 218, 230, 239, 247, 251, 254, 254` |
| corner (0,0) RGB | `255,255,255` | `0,0,0` |
| centre (16,16) RGB | `255,255,255` | `254,254,254` |
| distinct pixel byte values | 1 | 84 |
| ETC1 level 0 distinct blocks in the pack (of 64) | 1 | >8 (the test's bar) |

`generateSpellParticleFallback()` computed the cosine and wrote it into
**alpha** over constant white RGB; `tools/generate_textures.cpp` ships the file
as `BmpFormat::Bmp24`, which has no alpha channel, so the shape was destroyed
at generation time. `SpellVisualFX` draws it with `EMT_TRANSPARENT_ADD_COLOR`
→ `glBlendFunc(GL_ONE, GL_ONE)`, which samples colour and never alpha, so
premultiplied white × cosine in RGB is both the fix and the thing the blend
actually wants — T389's generic dome is soft and translucent for the same
reason, and the official client's Glowflare DDS carries its falloff the same
way. Alpha is left opaque and the file stays 24-bit; converting it would add
~8x the pack bytes for a channel no shader reads, and the `kNeverAlpha` reason
string in `tests/test_texture_alpha_census.cpp` now says exactly that.

**Cascade, one commit**: regenerate → `--write-pins` → `build_willeq_pack.sh`
(atomic install) → `data/willeq.eqe1.sha256`.

- Pack **6,930,494 → 6,930,116 bytes (−378)**, sha `14a6bd45…` → `19e78745…`.
- Install ran 20:26:19 → **20:27:44 (85 s)**, with **no `willeq`, ctest or
  gate process anywhere on the host** — `main`'s tip-verification `ctest -j6`
  had finished 32 s earlier (its exit file written 20:25:47) and slot1's only
  live process was an ARM docker build, which does not read the pack. The
  pre-T424 pack is preserved at `review/T424/willeq.eqe1.before_T424`.
- `eqe1MappedBytes` delta: **−378 bytes per zone**, identical in all three gate
  zones (124,064,062 → 124,063,684 qeynos2; 122,730,154 → 122,729,776
  gfaydark; 117,214,650 → 117,214,272 befallen). This is the whole of the gate
  failure below. **The manifest was NOT edited.**

**A surprise worth recording**: `scripts/build_textures.sh` (no flag)
regenerated `detail_atlas.bmp` as well, which is the one file on its
`NOT_REPRODUCIBLE` list (board B265) — the in-place mode regenerates
everything and only `--check` honours the exemption. Shipping it would have
moved pixels in a texture every outdoor gate zone draws. It was restored from
`HEAD` (`git show HEAD:… > …`, not `git checkout --`) before the pins were
written; `--check` then reports `64 byte-identical, 0 drifted, 1 exempt`.

### Leg B — B380: DELETE, and the history that decided it

T423 left the field in place because it could not tell whether a writer had
been lost. It had been: `git log -S ambientMultiplier --
include/client/graphics/irrlicht_renderer.h` returns exactly one commit,
`489a3826` ("first milestone"), and at that revision the writer is a
`processFrame()` arm reading `eventReceiver_->getAmbientLightDelta()` behind
the "Page Up/Down" the comment names. That arm was deleted by **`cac31166`**,
*"Remove Admin and Repair renderer modes, keeping only Player mode"* (Will,
Feb 2026), which stripped ~2,200 lines of admin/repair features including this
handler, its delta accumulator and its hotkey binding. `getAmbientLightDelta`
exists nowhere in the tree today.

So the term was residue of a **deliberately removed dev control**, not a
zone/config override with a lost writer, and wiring one would be inventing a
feature. Deleted: the field, the argument, and the per-channel
`std::min(1.0f, …)` clamp that existed only for it (the day/night curve is
already ≤ 1; the weather modifier is deliberately allowed to overshoot for a
lightning flash). `composeSceneAmbient()` is `(dayNight, weatherModifier,
vision)` and the pinned order is three steps. Compiler-by-deletion proof: the
whole tree builds with it gone — `build/` (tests, tools and benchmarks
included), `build-gles2/` and `build-arm-noble/`.

### Leg C — B351, per sprite

The plan's fork is "oracle art fable does not draw yet → board its consumer"
vs "leftover → delete". **All six are leftovers**, and the shared reason is
that none of them is oracle art: each is a procedural gradient or a 1px border
written by `ui_atlas_builder` for the WindowManager UI deleted in `0f25cb57`.

| sprite | atlas region | fate | why |
|---|---|---|---|
| `slot_border_selected` | 32x32 white border at (96,0) | **deleted** | the static UI has no "selected slot" state at all — every slot border is the `hovered ? Hover : Normal` ternary in `static_panels.cpp` (4 sites) |
| `button_normal` | 64x24 grey gradient at (0,32) | **deleted** | the only button-shaped caller is `loot_panel.cpp`'s `drawButton`, whose rects are not 64x24; stretching this gradient onto them invents a look → B385 |
| `button_pressed` | 64x24 darker gradient at (64,32) | **deleted** | same, and nothing tracks a pressed state for it to select |
| `button_disabled` | 64x24 flat at (128,32) | **deleted** | same; `drawButton` already expresses disabled with its own two colours |
| `panel_border` | 32x32 1px border at (64,160) | **deleted** | a 1px border stretched from 32x32 onto a 400px popup draws proportionally thick, uneven edges; panels use `UIRenderer::drawFrame` |
| `scroll_indicator` | 8x16 flat grey at (96,160) | **deleted** | a solid rect sprite is exactly a `drawRect`, which is what the chat thumb already is — zero benefit, one atlas region |

Removed from all three places together (builder, `UIAtlas::mapSprite`'s
nameMap, the `UISprite` enum), so the roster is fifteen and
`EveryShippedSpriteHasAConsumer` holds. The consumer half of B351 — that the
loot buttons and the chat thumb *should* be drawn from art — is boarded as
**B385 (C)**, with the oracle's actual source named
(`window_pieces01.tga`, 9-sliced) and filed behind B350's look decision.

### Tests

| test | pre-fix | post-fix |
|---|---|---|
| `TextureAlphaCensus.SpellParticleFallbackCarriesItsFalloffInRgb` (new) | **FAILED** — min channel 255, and (0,0) read 255 against the cosine's 0 | PASS |
| `TextureAlphaCensus.SpellParticleFallbackPackEntryCarriesTheSameShape` (new) | **FAILED** — 1 distinct ETC1 block in level 0 | PASS |
| `UISpriteRoster.TheEnumHoldsExactlyTheRoster` (new) | **FAILED** | PASS |
| `UISpriteRoster.TheJsonNameMapHoldsExactlyTheRoster` (new) | **FAILED** | PASS |
| `UISpriteRoster.TheBuilderRegistersExactlyTheRoster` (new) | **FAILED** | PASS |
| `UISpriteRoster.ThePackedAtlasJsonHoldsExactlyTheRoster` (new) | **FAILED** — all six names still in the shipped `ui_atlas.json` | PASS |
| `UISpriteRoster.EveryShippedSpriteHasAConsumer` (new) | PASS (the surviving fifteen already had consumers — that is the invariant, not the fix) | PASS |
| `VisionAmbientCompositionT396Test.NoUserAmbientMultiplierRemains` (new) | **FAILED** on two assertions, with Leg B reverted by exported patch (`git apply -R`, never `git stash`) | PASS |
| `VisionAmbientCompositionT396Test.DaylightIsAboveEveryFloorAndTheUserBoostClamps` | — | **renamed** `DaylightIsAboveEveryFloor`; the clamp half is gone with the term |

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background + exit file) | **exit 0** (four builds; no exit 143 anywhere in the unit) |
| `ctest --test-dir build -j6` (first run) | discovery timeout on `test_eq_action_handler_vendor` — cold-cache contention, not truncation: the binary lists its ten cases fine (`--gtest_list_tests`, exit 0). Re-run. |
| `ctest --test-dir build -j6` (final) | **4,363 / 4,364** — only `visual_gate`, red under `-j6` load (the documented in-suite artefact) and re-run standalone below |
| `TexturesReproducible` / `PackReproducible` | green in the final ctest, and by hand: `64 byte-identical, 0 drifted, 1 exempt` and `PASS: rebuild is byte-identical to the pinned hash and the installed pack` |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| `bash scripts/ci/visual_gate.sh` (standalone, 1st) | FAIL — 3x `eqe1MappedBytes` **plus** gfaydark CPU p95 13.344 vs ceiling 10.672, taken while `main` and slot3 were both building (load average 27) |
| `bash scripts/ci/visual_gate.sh` (standalone, 2nd, quiet host, load 1.85) | FAIL on `eqe1MappedBytes` ONLY. CPU is comfortable: qeynos2 4.964/10.108, gfaydark 5.981/10.672, befallen 6.520/10.948 |
| `git status tests/goldens/` | **empty — no image golden moved**, so no contact sheet and no STOP on that account |
| `bash scripts/build-arm-noble.sh` | exit 0, `build-arm-noble/bin/willeq` ELF 32-bit ARM EABI5 |
| `bash scripts/refactor/check_file_sizes.sh fable` | 33 checked: **33 ok, 0 warn, 0 fail** (largest touched: `event_bus.h` 1,218; `src/client/graphics/sim/renderer_sim_bridge.cpp` 1,133; new `test_ui_sprite_roster.cpp` 212) |

**The one open item: the gate's `eqe1MappedBytes` golden.** It is −378 bytes
in all three zones, which is exactly the pack's new size, and it is the only
gate assertion that moved. The dispatch says to report the delta and never
edit the manifest, so `tests/goldens/manifest.json` is untouched and the
re-bless belongs to the orchestrator. Note this is a numeric memory-accounting
golden, not an image golden; nothing under `tests/goldens/` changed.
