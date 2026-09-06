# T412 — three inert knobs from the field census (B291, B292, B294)

Class A ×3, slot3, no server. Read the three board entries, T407's
§Review (the census script and the exemptions), and the consumers.

## Leg A — B291 `skipTextureUpload_`

The constrained-mode knob is stored and never read; entity texture
upload happens regardless. Read what the knob was meant to skip (the
config comment, the orangepi preset), consume it at the upload site, and
test (pre-fix failing): with the flag set, no entity texture upload
happens (count via the texture cache's stats seam); with it clear, it
does. The orangepi preset's value decides what the Pi does — say what it
is and whether the memory gate's coverage run exercises it.

## Leg B — B292 `cacheMaxBytes_`

The SFX cache has no ceiling: `setCacheMaxBytes()` stores a budget nothing
compares against. Implement eviction (LRU — the chr-cache store from
T364/T366 is the shape) when `cacheSizeBytes_` would exceed it; test
(pre-fix failing): load N sounds past the ceiling → the total stays under
it and the least-recently-used is gone; a sound in use is not evicted
(read how "in use" is known — a playing channel).

## Leg C — B294 `forcePvsUpdate_`

Set in four places to force a PVS recalculation, read nowhere: a
render-distance change (and the other three writers) never forces the
recompute. Find the PVS update site and consume the flag (or, if the PVS
now recomputes on a different trigger that already covers the four
writers, delete the flag and the four writes with the proof). Test
(pre-fix failing): after a render-distance change, the next frame
recomputes the PVS (the frame order golden G05 pins statements — learn in
place if one is added).

## Proof

Build, `ctest -j6` (expected reds: only the shared-pack consumers while
T411 is mid-replacement), pre-fix/post-fix, visual gate standalone + ARM
(Leg A/C touch the render path; a moved image golden is a STOP with the
contact sheet), file sizes. Strike B291, B292, B294.

STOP: Leg A's upload site cannot be skipped without breaking entity
rendering (then the knob's intent is wrong — report and board).

---

## Review

Branch `task/T412-census-class-a`, base `641a51a6`. Three commits, one per leg:

| leg | board | commit |
|---|---|---|
| A | B291 | `3596e841` |
| B | B292 | `37d35918` |
| C | B294 | `927994b8` |

### Leg A — B291 `skipTextureUpload_`

**What the knob was meant to skip, and what it now does.** The value comes
from `config/debug.json`'s `"skipEntityTextureUpload"` (shipped `false`),
parsed by `ConstrainedRendererConfig::loadDebugOverrides()`, which
`Application` calls at graphics init; `IrrlichtRenderer::initDeferredGraphics`
then hands it to `EntityRenderer::setSkipTextureUpload()`. The header comment
says "skip texture uploads during entity build (isolate glTexImage2D)" — it is
a profiling knob, one of a family of seven `skip*` flags whose siblings
(`skipEntityBuild`, `skipConstrainedTextureUpload`, `skipSkyTextureUpload`)
are all consumed at a single site each.

The knob is now read at the entity build path's one upload seam, new
`EntityRenderer::uploadPendingEntityTextures()` (`entity_build_step.cpp`),
which `processOneEntityBuildStep()`'s `SceneNodeCreation` phase calls in place
of its bare `constrainedTextureCache_->uploadAllPending()`. Skipping returns 0
and leaves the decoded entries **pending, not dropped**, so clearing the flag
later uploads them normally; entities still build and draw untextured, which is
what "skip glTexImage2D to isolate upload cost" means.

**Deviation from the plan (flagged, not absorbed):** the plan said "consume it
at the upload site", singular. There are **three** entity texture upload sites,
not one. Besides the gameplay-time site above, `ZoneLoadComponent`'s Step 8
(`renderer_zone_load_entities.cpp`) calls `uploadAllPending()` twice — once per
prepared entity and once as an end-of-step sweep. Gating only the gameplay site
would have left zone-in uploading every entity texture anyway and the knob
would still not have done what it says. Both are gated on
`ctx_.config.constrainedConfig.skipEntityTextureUpload` directly.

**The orangepi preset's value is `false`** — `fromPreset()`'s `OrangePi` case
never touches `skipEntityTextureUpload`, so it keeps the
`ConstrainedRendererConfig` struct default, and `config/constrained_presets.json`
has no `skip*` key at all (checked: `grep skip` over that file returns
nothing). The only way the flag is ever `true` on the Pi is by editing
`config/debug.json`. `DefaultIsUpload` pins that: an orangepi preset that
silently disabled entity textures would fail it.

**The memory gate's coverage run does NOT exercise it, either way.**
`loadDebugOverrides()` has exactly one caller, `application.cpp:608`, i.e. the
`willeq` binary; `scripts/ci/visual_gate.sh` drives
`gles2_zone_benchmark`, which never reads `config/debug.json` and constructs
its config itself. So the gate proves nothing about this knob in either
position — which is why the coverage is a unit test rather than a gate
baseline.

STOP condition not hit: the upload site skips cleanly, entity rendering is not
broken by it (the entries survive and upload on the next call with the flag
clear), so the knob's intent stands.

| test (`test_entity_texture_upload_skip`) | pre-fix | post-fix |
|---|---|---|
| `SkipTrueUploadsNothing` | **FAIL** (2 uploaded with the flag set) | PASS |
| `SkipFalseUploadsEverythingPending` | PASS (control) | PASS |
| `SkippedTexturesStayPendingAndUploadLater` | **FAIL** (0 on the second call — the first already uploaded them) | PASS |
| `DefaultIsUpload` | PASS (control) | PASS |

Pre-fix = the `if (skipTextureUpload_)` arm removed from
`uploadPendingEntityTextures()`, restored and re-proved green after.

### Leg B — B292 `cacheMaxBytes_`

`AudioManager::setMemoryConstraints()` passes the configured sound-cache budget
to `SfxManager::setCacheMaxBytes()`, which stored it against a member nothing
compared `cacheSizeBytes_` to. `src/client/audio/CLAUDE.md` has described
`SfxManager` as "WAV loading (dr_wav) and spatial playback **with LRU cache**"
the whole time; as of `37d35918` that sentence is true. (A stale doc claim that
became true rather than one that needed striking — no edit was needed, which is
worth saying out loud.)

**Eviction policy.** LRU, mirroring `AudioManager::loadSound()`'s own
sound-buffer LRU one layer up — the closest sibling in the tree, closer than
the chr-cache store the plan named:

* `lruOrder_` is a `std::list<std::string>`, most-recently-used at the front.
* `preload()` inserts at the front and enforces; `getWavData()` touches to the
  front, because it is the read `play()` itself goes through, so playing a
  sound makes it recent.
* `setCacheMaxBytes()` enforces **immediately**, so lowering the ceiling at
  runtime bites at once rather than at the next preload —
  `setMemoryConstraints()` is called precisely because memory is already tight.
* `cacheMaxBytes_ == 0` means unlimited (the documented default, unchanged).
* Never evict down to an empty cache (`lruOrder_.size() > 1`), the sibling's
  rule: a caller that just asked for a sound is better served by keeping it
  than by honouring a ceiling smaller than one WAV.

**"In use" is a live mixer channel, and this is the rule the sibling cache does
not need.** `AudioMixer` channels do not own their samples: `play()` stores
`wav->samples.data()` into `MixChannel::samples` and the **audio thread** reads
through that pointer until the channel goes inactive. Evicting a playing entry
is a use-after-free on the audio callback. `isInUseLocked()` scans all
`AudioMixer::TOTAL_CHANNELS` for an `active` channel whose `samples` pointer
equals the entry's `samples.data()` and refuses it; if everything cached is
playing, the cache stays over budget with a `LOG_WARN` rather than freeing
memory out from under the callback. The check and the erase happen under
`cacheMutex_` while no new channel can appear, because `preload()`,
`setCacheMaxBytes()` and `play()` are all game-thread calls and the audio
thread only ever reads an already-published channel.

| test (`test_sfx_cache_eviction`) | pre-fix (no enforcement) | pre-fix 2 (no in-use check) | post-fix |
|---|---|---|---|
| `UnlimitedByDefault` | PASS | PASS | PASS |
| `ZeroMeansUnlimited` | PASS | PASS | PASS |
| `TotalStaysUnderTheCeiling` | **FAIL** | PASS | PASS |
| `LeastRecentlyUsedGoesFirst` | **FAIL** | PASS | PASS |
| `LoweringTheCeilingEvictsAtOnce` | **FAIL** | PASS | PASS |
| `APlayingSoundIsNotEvicted` | **FAIL** | **FAIL** | PASS |

Two independent plants, because one red proves one thing: disabling
`enforceCacheBudgetLocked()` reds 4 of 6; removing only the `isInUseLocked()`
line reds `APlayingSoundIsNotEvicted` alone, which is what makes that clause a
guard on the use-after-free rather than on eviction in general.

### Leg C — B294 `forcePvsUpdate_`

**Deleted, with the proof — the plan's second option.** The flag is a leftover,
not a missing consumer:

* Its one reader lived in `IrrlichtRenderer::updatePvsVisibility()`, which
  cached the camera's BSP region behind a **5-unit movement threshold** in
  function-local statics. The flag reset those statics and `portalCacheDirty_`.
  (`git show 6914d99b` carries the deleted body verbatim.)
* Commit `6914d99b` ("Move unified particle physics to SimulationWorker
  background thread") deleted that whole function when PVS moved to the worker.
  `portalCacheDirty_` does not exist anywhere in the tree any more
  (`grep` over `src/ include/` → zero hits), so the flag has been inert since.

**The trigger that already covers all four writers**: every frame,
`FrameComponent::processFrame()` calls
`SimBridgeComponent::postSimulationInput()`, which sets
`input.renderDistance = ctx_.renderDistance` and the camera position
unconditionally; `SimulationWorker::computeAll()` then runs
`calculateRegions()` + `computeVisibility()` over them on every non-loading
pass. There is no movement threshold and no cached camera position left to
invalidate. The one cache that survives inside the worker,
`computeRegionDepthMap()`'s `cachedDepthMap_`, is keyed on the camera's region
and is pure portal adjacency — it does not read `renderDistance` at all, so a
render-distance change cannot leave it stale.

Each of the four writers is therefore covered: `setRenderDistance()` (the
per-frame republish), `hideLoadingScreen()` (the worker's first non-loading
`computeAll()` recomputes everything from scratch), the zone-in camera
reposition (the next frame reads the position it just set), and the occlusion
debug key (same).

Giving the flag a consumer would have meant inventing a skip that does not
exist, so the member and all four writes are gone; each site keeps a comment
naming what replaced it.

| test (`test_pvs_render_distance_trigger`) | at base `641a51a6` | post-fix |
|---|---|---|
| `PvsRenderDistanceTest.RenderDistanceChangeIsHonouredOnTheNextCompute` | PASS | PASS |
| `PvsRenderDistanceTest.EveryPassRecomputesFromScratch` | PASS | PASS |
| `ForcePvsUpdateFlagIsGone.NoMemberAndNoWritesRemain` | **FAIL** (all four writers reported) | PASS |

The two behavioural cases passing on **both** sides is the point, not a gap:
the trigger already worked, which is exactly why the flag could go. The red/green
pair is the source guard, the `SpellLandHookIsGone` (T357) shape — a member
nothing reads has no runtime observable. Pre-fix was produced by restoring the
four files from `fable`'s base blob (`git show <base>:<path>`, never
`git checkout --`), then restoring the fixed copies.

**No golden touched.** No file under `tests/goldens/` mentions `forcePvsUpdate`;
`frame_render_order_g05` does not pin any of the four statements, so nothing had
to learn in place.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, RelWithDebInfo) | **exit 0** |
| `ctest -j6` | **4,228 of 4,234** — 6 reds, all the shared pack (below) |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` (standalone) | **FAIL — only the three `eqe1MappedBytes` lines**; every image comparison passed, no golden moved |
| `bash scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh 641a51a6` | **12 checked, 12 ok, 0 warn, 0 fail** |

The six ctest reds are exactly the shared-pack consumers the dispatch named as
expected while T411 replaces the pack: `PackReproducible`,
`ParticleAtlasAlpha.PackEntryCarriesAlphaInBand`,
`TextureAlphaCensus.WeatherPackEntriesCarryAlphaInBand`,
`TextureAlphaCensus.SoftwareCursorPackEntryCarriesAlphaInBand`,
`TextureAlphaCensus.BillboardPackEntriesCarryAlphaInBand`, and `visual_gate`.
This branch touches nothing under `data/`, `tools/` or `scripts/`. The gate's
deficit is **−14,462,656 bytes per zone in all three zones**, which is the exact
figure T411 recorded in `CLAUDE.md` for taking the RGBA8888 payloads back out of
the pack — the branch's baselines are one merge stale, not wrong.

Running `check_file_sizes.sh fable` (rather than against the branch base) lists
24 files and one WARN on `docker/irrlicht-drm/COpenGLES2Driver.cpp`; that is
`fable` having advanced to `45d30d57` under the branch, not this unit's doing.
The base-rev run above is the one that describes this branch.

### Surprises

1. **The plan's "the upload site" was three sites.** Two of them are in
   `ZoneLoadComponent`, not `EntityRenderer`, so a reader of B291 who greps only
   `entity_*.cpp` finds one third of the knob. Gating one would have produced a
   test that passes and a flag that still does not work at zone-in — the
   worst outcome available.
2. **`hideLoadingScreen()`'s comment is a truncated sentence** — "Without
   resetting here, the movement" and then nothing. It was left dangling when
   `6914d99b` deleted the function it referred to; the flag write below it
   survived, the explanation did not. A half-sentence in the tree is a
   deletion's tombstone that nobody finished, and it is what made the flag look
   deliberate.
3. **`libwilleq_core.a` is 3.2 GB and a full `cmake --build` of this tree does
   not fit in the Bash tool's 600 s ceiling.** Two exit-143 kills in this unit,
   each of which DELETED the archive outright — the absence case the protocol
   warns about, invisible to any scan over what exists (`build/lib/` held only
   `libEtcLib.a` and a multi-GB `st??????` ar temp). Every build after the second
   kill was run backgrounded with an exit file and `wait_for.sh`. A targeted
   `--target test_x` is not safe either: it rebuilds `willeq_core` first.
4. **A comment can red its own source guard.** The first
   `ForcePvsUpdateFlagIsGone` run failed on `renderer_init.cpp` — because the
   comment explaining the deletion quoted the deleted statement verbatim. The
   guard was right; the tombstone had to be reworded.
5. **`src/client/audio/CLAUDE.md` already claimed the LRU cache B292 says does
   not exist.** The doc was aspirational, not stale, and Leg B made it true. The
   usual failure mode is a doc that outlived its code; this is the mirror image,
   and it means a doc grep would have "confirmed" the ceiling worked.

### Deviations

* Leg A gates three sites, not one (see above). Flagged, not absorbed.
* Leg C deletes rather than consumes, which the plan explicitly permits given
  the proof; the pre/post pair is a source guard, and the two behavioural cases
  deliberately pass on both sides.
* Board strikes and this Review are a fourth commit, since the leg commits
  cannot cite their own hashes.
