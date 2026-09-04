# T354 — Two dead renderer paths, deleted by measurement (B042, B047)

Class B, slot3. Both were kept by L03/L05 under correction 17 ("a
zero-caller setter whose member is read live is kept and boarded") so the
decision could be taken with evidence. The evidence now:

- **B042** `OverlaysComponent::drawZoneLineOverlay` — runs every frame,
  gates on `inZoneLine_`, whose only writer (`setZoneLineDebug`) was
  deleted as dead. A zone-line DEBUG border is a developer aid, not an
  official-client feature (no oracle), and nothing in the tree has ever
  turned it on. DELETE the draw body and its three members
  (`inZoneLine_`, `zoneLineTargetZoneId_`, `zoneLineDebugText_`) and the
  per-frame call in the frame pass — ⚠ that call is a statement in the
  G05 order golden: it learns the deletion IN PLACE (T335b shape), both
  faults re-planted. `test_ui_hide_command`/`test_static_ui_scroll` read
  the frame TU — run them.
- **B047** the incremental collision-registration API
  (`addRegionToCollision`, `addDoorToCollision`, `addObjectToCollision`,
  `findBspRegionForPoint`, `clearCollisionDebugLines`) — zero callers;
  `renderer_zone_draw.cpp`'s comment claims it runs progressively. Measure
  once more (comment-filtered grep + the compiler by deleting the
  declarations); if still zero: DELETE the five and rewrite the comment to
  say what actually happens (`setupMinimalZoneCollision()` once). K12 ff
  publisher check first. `clearCollisionDebugLines` — T335 added a
  collision-debug pixel test with a seam; keep whatever that test uses.

Proof: build; the K/L multiset for deletions is not needed (deletions
are the change) — the compiler and the suite are the proof; the G05
golden re-planted; visual gate + ARM standalone (graphics files change).
Strike B042 and B047.

STOP: a caller appears (then it is not dead — leave it and say so).

## Review

Branch `task/T354-dead-renderer-paths`, from `fable` at `1ec98a37`. Both items
measured dead a second time and deleted. **No caller appeared for either.**
Net: 74 insertions, 208 deletions across 11 files; 145 lines of production code
gone.

### The re-measurement, both items

Two independent measurements, as the contract required.

**1. Comment-filtered grep** over `src/ include/ tests/ tools/`, dropping every
hit whose line begins with `//`, `*` or `/*`. Nine symbols, and the ONLY
non-comment hits anywhere in the tree were the declaration, the definition, and
(for `drawZoneLineOverlay` alone) the single per-frame call plus its line in the
G05 golden:

| symbol | non-comment hits | what they were |
|---|---|---|
| `drawZoneLineOverlay` | 4 | decl, def, the per-frame call, the golden line |
| `inZoneLine_` | 2 | member decl, the gate read inside the deleted body |
| `zoneLineTargetZoneId_` | 2 | member decl, one read inside the body |
| `zoneLineDebugText_` | 3 | member decl, two reads inside the body |
| `addRegionToCollision` | 2 | decl, def |
| `addDoorToCollision` | 2 | decl, def |
| `addObjectToCollision` | 2 | decl, def |
| `findBspRegionForPoint` | 2 | decl, def |
| `clearCollisionDebugLines` | 2 | decl, def |

So each of B047's five is reachable from nothing but itself, and B042's three
members are read by nothing but the body that was deleted with them.

**2. The compiler** — declarations AND definitions deleted, then a full
`cmake --build -j24`: **exit 0, zero `error:` lines**. A surviving caller would
have been a compile error on the declaration or a link error on the definition;
there was neither.

**K12 (ff) publisher check, before deleting:** every deleted body grepped for
`pushEvent` and `GameEventType::` — **zero matches in all six**. Nothing deleted
here was the sole publisher of an event, so
`ReachabilitySweep.EveryEventIsPublishedAndConsumed` was never at risk (and it
passes).

### B042 — the zone-line debug border

Deleted:

* `OverlaysComponent::drawZoneLineOverlay()` — the 44-line body (four
  `draw2DRectangle()` border strips in semi-transparent pink plus two built-in
  font strings), `src/client/graphics/overlays/overlays_component.cpp`.
* its declaration, `include/client/graphics/overlays/overlays_component.h`.
* the three members it read: `inZoneLine_`, `zoneLineTargetZoneId_`,
  `zoneLineDebugText_`, same header. Their comment block is replaced by one
  saying they are GONE and why, and pointing at the zone-line BOUNDING BOX
  visualization below them, which is live and unrelated.
* the per-frame call `ctx_.Overlays().drawZoneLineOverlay();` in
  `FrameComponent::renderZoneLineOverlays()`,
  `src/client/graphics/frame/renderer_frame_render.cpp`.

`renderZoneLineOverlays()` is NOT removed — `drawZoneLineBoxLabels()` and its
`measureSection()` bracket stay, so the pass count is still sixteen and
`frameTimings_.zoneLineOverlay` still measures a real section. Three stale
comments naming the deleted method were corrected rather than left to rot: the
P52 GPU-phase bracket comment in the frame TU (the phase now ends after
`drawZoneLineBoxLabels()`), the `frame_component.h` call-surface banner, and the
`irrlicht_renderer.h` forwarder note. `guienv_` survives — three other users.

The evidence stands as B042 stated it: the sole writer `setZoneLineDebug()` was
deleted by L03 as a zero-caller with no `pushEvent`, so `inZoneLine_` was `false`
for the life of the process and the border was unreachable. It is a developer
aid with no oracle in the official client.

### The G05 golden learns the deletion IN PLACE

`tests/goldens/frame_render_order_g05.txt`: entry **279**,
`ctx_.Overlays().drawZoneLineOverlay();`, removed at its position. Entries 0-278
are byte-identical; entries 280-304 became 279-303 with their TEXT
byte-identical (only the renumbering prefix moved — the test splits at the first
space and discards the number, so the renumber is cosmetic). Nothing was
reordered, and the file was never regenerated from the tip.

`tests/test_frame_render_order.cpp`: `kGoldenSize` **305u -> 304u**, and the
comment above it gained its second named entry, in the T335b/correction-32
shape — the unit, the position, what the entry was, why it went, and the
explicit note that the count DROPS by one because a statement was removed and
nothing replaced it. The guard text, the parser, the normaliser and all four
cases are otherwise untouched.

### Re-plant — both faults, still caught

The TU was copied aside first; after both restores `diff` against the copy
reports **IDENTICAL** and `git status --porcelain` lists the TU only for this
unit's intended edit. No `git checkout --`, no `git stash`.

| plant | outcome |
|---|---|
| **drop a statement** — delete `ctx_.Overlays().drawZoneLineBoxLabels();`, the remaining draw in the pass this unit edited | **FAILS**, exit 1: `draw statement count changed: 304 -> 303`. 3 passed, 1 failed |
| **swap two pass calls** — `renderWeatherEffects();` and `renderDebugOverlays(deltaTime);` exchanged in `processFrameRender()`, no pass body edited | **FAILS**, exit 1: `draw order diverges at statement 116 -- want: LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 7: weather"); got: if (ctx_.PlayerCamera().playerConfig().collisionDebug) ctx_.PlayerCamera().drawCollisionDebugLines(deltaTime);`. 3 passed, 1 failed |
| restore both | **4/4 PASSED** |

Teaching the golden a DELETION did not blunt it: the count clause still catches a
dropped statement and the position clause still catches a reordered pass.

### B047 — the never-run collision-registration API

Deleted from `src/client/graphics/player/renderer_player_collision.cpp` and
`include/client/graphics/player/player_camera_component.h`, 97 lines of body:

* `findBspRegionForPoint()` — a 5-line forward to `zoneBspTree->findRegionIndexForPoint()`.
* `addRegionToCollision()` — extracted a region node's world triangles into `ctx_.regionWorldTriangles`.
* `addDoorToCollision()` — the largest, 50 lines: opentype classification (spinning/lift/hazardous/invisible get no collision) and open/closed triangle extraction by rotating the pivot node to each heading.
* `addObjectToCollision()` — appended an object node's triangles to `ctx_.objectWorldTriangles`.
* `clearCollisionDebugLines()`.

**The T335 seam is untouched.** `test_collision_debug_draw.cpp` uses
`seedCollisionDebugLineForTest()` and `collisionDebugLineCount()`, never
`clearCollisionDebugLines()` — checked before deleting, and
`CollisionDebugDrawTest` passes. `drawCollisionDebugLines()`,
`addCollisionDebugLine()` and the `collisionDebugLines_` vector all stay: they
are the live movement-collision debug path.

**`extractWorldTriangles()` stays too.** Deleting its three `add*ToCollision`
callers does not orphan it — it is still called by `setupMinimalZoneCollision()`
(same TU) and by `ZoneLoadComponent::loadZoneStep9_Collision()`.

**The false comment, rewritten to say what happens.** The old text at
`renderer_zone_draw.cpp:198-199` claimed collision "is set up progressively via
setupMinimalZoneCollision() and addRegionToCollision() during the background
zone loading phases". Measured, it is neither progressive nor set up there: at
load step 9 `setupMinimalZoneCollision()` walks `ctx_.regionMeshNodes` in a
SINGLE pass, and that same step then adds the eagerly-built objects inline. The
comment now says exactly that, names the step-9 site, and records that anything
streamed in after step 9 still gets no collision — the real gap stays visible in
the code rather than being erased with the dead API. A SECOND stale comment in
the same file (line 1056, on why `updateAbsolutePosition()` is needed) also named
`addRegionToCollision()`; it now names `extractWorldTriangles()`, which is the
function that actually reads `getAbsoluteTransformation()` there.

### Steps

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, 0 `error:` lines. (First attempt hit the 10-minute tool ceiling and was killed — exit 143. Protocol cleanup ran: 17 zero-byte files under `build/bin` deleted, `ar t` gave `libwilleq_core.a` 321 members and `libEtcLib.a` 15, both intact. A first-4-bytes ELF-magic scan of `build/bin` then found **five FURTHER truncated binaries that were NOT zero-byte** — `test_bridge_teardown`, `test_eq_commands_bank_train`, `test_scribe_scroll`, `test_spell_manager`, `test_zoning_integration`, all all-zero-filled — which the zero-byte rule alone misses and which broke ctest's discovery step. Deleted and rebuilt; the scan then came back clean.) |
| `ctest -j6 --output-on-failure` | **3853 / 3854 passed**, 1 failed: `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA failure). 17 skipped. `test_frame_render_order` 4/4, `test_ui_hide_command`, `test_static_ui_scroll`, `test_collision_debug_draw` and `test_overlays_component` all pass — 16 cases across the five named targets, all Passed. (One earlier attempt aborted in ctest's DISCOVERY phase with `test_zone_in_z_conversion` timing out under load from the sibling slots; run by hand it lists its three cases in well under a second, and the re-run was clean.) |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` (standalone) | **`=== VISUAL GATE: PASS ===`**, exit 0. `git status --porcelain` lists **no pixel golden** — the eleven modified files are this unit's own edits and nothing else |
| `bash scripts/build-arm-noble.sh` | **exit 0** |
| `bash scripts/refactor/check_file_sizes.sh --all` | **880 checked, 880 ok, 0 warn, 0 FAIL** |

`docs/module_map.md`: two row descriptions changed — `overlays_component.cpp`
no longer claims a "zone-line overlay" (bounding boxes only), and
`renderer_player_collision.cpp` no longer claims "the incremental
collision-registration API", naming the one-shot `setupMinimalZoneCollision()`
instead. Both note the deleting unit.
