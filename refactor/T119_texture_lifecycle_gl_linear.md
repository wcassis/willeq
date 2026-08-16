# T119 — Trace how `GL_LINEAR` reaches a dead object: the texture destruction paths

**Slot: whichever is free. x86 first. eqemu lock required. Pi for
verification.**

**Territory: `constrained_texture_cache.cpp/.h`, `docker/irrlicht-drm/`
(`COpenGLES2Driver.cpp`, `COGLES2Texture.h`), zone/sky/model texture holders,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T119/`.**

Read T118's Review first — especially the `GL_LINEAR` identification and the
candidate write site it named.

## What is established

`0x2601` is **`GL_LINEAR`**. The only code that writes that literal into
memory is `COpenGLES2Driver.cpp:1355-1375`, which computes
`GLint magFilter = ...BilinearFilter ? GL_LINEAR : GL_NEAREST` and then calls
`tex->cacheTexParams(...)` — which, through a `const COGLES2Texture*`, writes
into `mutable` cache fields (`COGLES2Texture.h:79`).

**If `tex` is stale, that write lands `GL_LINEAR` at a fixed offset inside
whatever now occupies the dead texture's storage.** That single mechanism
explains T117's otherwise-unrelated victims: the corrupted container is just
whatever got allocated there, which differs per run.

**It also explains the clean ASAN legs** — see "Why ASAN missed it" below,
which you should read before concluding anything from T118's negatives.

## Two scoping findings that redirect this unit — do not skip

### 1. LRU eviction is NOT the mechanism. Evictions are zero.

T118 named `ConstrainedTextureCache`'s **LRU eviction timing** as the
suspect. **The logs contradict that.** Every run — including T117's plain-Pi
runs that actually crashed — reports:

```
Textures: TMU 23.1MB/64.0MB | Hit: 96% | Evictions: 0
```

Usage sits at roughly a third of budget in Befallen, so `evictUntilAvailable()`
never fires and `evictTexture()` is never called. **Do not start from the LRU
hypothesis.** (Note in passing: `calculateTextureSize()` charges a ~33% mipmap
inflation for a chain nothing uploads — already recorded as phantom pressure —
but even so, nothing evicts.)

### 2. The real asymmetry: `clear()` does not clear references, `evictTexture()` does

`clearTextureReferences()` — the function whose own comment says it "removes
texture from mesh materials before deletion ... prevents crashes from dangling
texture pointers during rendering" — is called from **exactly one place**,
`evictTexture()` (`:1122`).

**`clear()` does not call it.** `clear()` (`:595`) notifies eviction listeners
and calls `driver_->removeTexture()` for every cached texture, then empties the
cache — **without removing those texture pointers from any mesh material.**

And `clear()` is the **zone-unload** hook, called from
`IrrlichtRenderer::unloadZone()`.

**Three of T117's four victims crash at teardown.** That is the lead.

## What to establish, in order

### Step 1 — Prove or kill the `clear()` hypothesis (x86, cheap)

Determine whether, after `ConstrainedTextureCache::clear()` runs, any mesh
material still holds a pointer to a destroyed texture, and whether any
`setMaterial`/render call can occur afterwards on that material.

Both halves matter. A stale pointer nobody touches is harmless; the crash
needs a **write** through it. Establish the actual call ordering at zone
unload and shutdown — what renders, if anything, between `clear()` and the
point the renderer stops drawing.

**Instrument rather than reason** where you can: a temporary assertion or log
in `cacheTexParams()` checking the texture against
`ConstrainedTextureCache::isLiveTexture()` (which already exists, `:585`)
would answer "is `tex` ever dead when we write to it?" directly. That is a
diagnostic, not a fix — keep it out of the final commit unless it earns its
place.

### Step 2 — Why ASAN missed it, and re-run the legs that were blinded

**T118's Legs 2 and 3 both ran with `quarantine_size_mb=0`** (confirmed in
`review/T118/run_asan_leg2_m32_lowmem.sh` and
`leg3_pi_asan/run_pi_asan.sh`). That option was added to fit memory — and it
is precisely the option that **cripples use-after-free detection**, because
freed chunks are immediately recycled instead of being held poisoned. **A
write into a reallocated chunk looks legitimate to ASAN.**

So the two legs that ran at the Pi's actual bitness and on the Pi itself had
UAF detection substantially disabled by the very trick used to make them fit.
**Their "clean" results do not rule out this hypothesis.**

Leg 1 (x86-64) *did* run with the default quarantine and was clean — but it
also never evicted, and if the mechanism is `clear()` at zone unload, a run
that ends without a second zone transition may never exercise it.

**So: re-run Pi ASAN with a non-zero quarantine** (T118 proved ASAN now fits
at `-g1`/36.7MB — that blocker is dead), and **make the tour cross a zone
boundary** so `clear()` runs with the renderer still live, rather than only
at process teardown.

### Step 3 — Audit the destruction paths as a set

Enumerate **every** path that destroys or replaces a texture the renderer may
still reference, and say for each whether references are cleared first:

- `clear()` (zone unload) — **confirmed not cleared**
- `evictTexture()` — cleared
- same-name replacement (`:511`, "Different texture under same name — evict
  the old one first")
- anything calling `driver_->removeTexture()` outside this class

Then audit `clearTextureReferences()` itself for **completeness**: which
holders of `ITexture*` does it actually walk? Zone mesh buffers, entity
models, doors, objects, sky, particles, UI? A holder it misses is a dangling
reference even on the eviction path. **`SkyData`'s texture map is one of
T117's victims — check whether sky materials are covered.**

## If you find it

Fix the narrowest thing that closes it. Most likely shapes: `clear()` should
clear references the way `evictTexture()` does, or `cacheTexParams()` should
not be reachable with a dead texture.

**STOP and report** if the fix requires changing texture ownership or
refcounting semantics, or reordering GL teardown — those are their own units,
and a rushed lifetime change can turn a crash into silent corruption.

**Do not paper over**: no null-guard in `cacheTexParams()` that hides the
stale pointer without explaining it, no disabling the P42 state-dedup cache to
make the symptom vanish. If the honest outcome is "here is the write, the fix
is larger than this unit," that is a success.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **A test is required if you fix anything** — this bug class has survived
  five units. Assert on the real destruction path, not a fake.
- **Visual gate, goldens must not move.** You are touching texture binding and
  the P42 filter-state dedup; a golden change here is a real signal. If one
  moves, STOP — do not rebless.
- **Any Pi rate claim must use T117's exact method**: same tour, same flags,
  **10 runs**, stated as N of 10 against the current **2 of 10**. Do not
  compare a 3-run sample — that is the mistake T117 exists to prevent.
- Pi rules: cores and logs to **NFS never `/tmp`**, `--log-level=INFO`, no
  live gdb, teardown by real binary PID (`pgrep -x`), `down.sh`, lock deleted
  and confirmed gone.

## Review

### Does a dead texture reach `cacheTexParams()`? — Not demonstrated (and the mechanism is now confirmed too narrow to explain it alone)

A temporary diagnostic (`ConstrainedTextureCache::isLiveTexture()` checked at
the `cacheTexParams()` call site in `COpenGLES2Driver::
applyMaterialRenderState()`, gated behind `-DWILLEQ_T119_DIAG_DEAD_TEXTURE`,
removed from the final commit per the plan's own instruction) was run across:

- Two full x86 zone-crossing tours (Befallen → Qeynos2 → Befallen → `/camp`),
  one of which **independently reproduced a real crash** (see below) on the
  first attempt.
- One x86 run under `gdb` with the same tour (did not crash — see the
  gdb/race-sensitivity note below).

**Zero hits, including on the run that crashed.** The diagnostic never once
observed `cacheTexParams()` being called with a texture `isLiveTexture()`
reported dead. This refutes — for every path this unit's instrumentation
covered — the T118 candidate mechanism as *the* (or at least as *the sole*)
route by which `GL_LINEAR` (`0x2601`) lands in freed memory. It does not
prove the mechanism is impossible (a call from a code path this session's
tour never exercised could still hit it), but it means T118's candidate is
no longer the leading hypothesis without further narrowing.

### `clear()` vs `evictTexture()` — confirmed, and fixed

Confirmed exactly as the plan's scoping section described:
`clearTextureReferences()` (`constrained_texture_cache.cpp`, "removes
texture from mesh materials before deletion ... prevents crashes from
dangling texture pointers during rendering") was called from exactly one
place, `evictTexture()` (`:1122`, old line numbers). `clear()` (`:595`, the
zone-unload hook, called from `IrrlichtRenderer::unloadZone()`) iterated
`cache_`, notified eviction listeners, and called `driver_->
removeTexture()` for every entry — **without ever calling
`clearTextureReferences()`.**

This is a real, demonstrable use-after-free hazard, not a theoretical one.
Verified mechanistically, not just by inspection:

- `SMaterial::setTexture()` (`third_party/irrlicht-gles2-x86/include/
  SMaterial.h:482`) is a **raw, non-owning pointer assignment** — `TextureLayer[i].Texture = tex;`, no `grab()`/refcount participation.
- `CNullDriver::removeTexture()` (`third_party/irrlicht-gles2-x86/source/
  Irrlicht/CNullDriver.cpp:362`) unconditionally calls `texture->drop()` on
  the texture's one owning reference (the `Textures` list's own `grab()`
  from `addTexture()`), which deletes the `COGLES2Texture` object the
  instant its refcount hits zero — there is **no reference-counting
  protection** for any `SMaterial` that still points at it.
- This is exactly the failure mode `clearTextureReferences()`, and the two
  purpose-built `TextureEvictionListener` guard classes
  (`RegionMeshTextureGuard` and its entity-mesh sibling,
  `renderer_zone_merge.h`) already exist to prevent — but all three are
  wired only to the `evictTexture()` path (`addEvictionListener()` /
  `clearTextureReferences()`'s own single call site), never to `clear()`.
  `SkyRenderer` independently registers as an eviction listener too
  (`sky_renderer.cpp:38-53`) and correctly nulls its own `textureCache_`
  entries on both `evictTexture()` *and* `clear()` (listeners are notified
  from both) — but that only protects its private lookup map, not the raw
  `ITexture*` already baked into its scene-node materials
  (`skyDomeMeshNode_`/`sunNode_`/`moonNode_`/`sunGlowNode_`), which
  `clearTextureReferences()` would have covered but nothing else does.

**The fix** (`constrained_texture_cache.cpp`, `clear()`): call
`clearTextureReferences(pair.second.texture)` for every entry, immediately
before `driver_->removeTexture(pair.second.texture)` — the exact ordering
`evictTexture()` already uses. This is narrow: it adds one call already
proven safe on the sibling path, changes no ownership/refcounting semantics,
reorders nothing in GL teardown, and touches no rendering behavior when no
stale reference exists (the common case, since `unloadZone()` already
removes almost every scene node — zone mesh, region meshes, entities, doors,
objects — before `clear()` runs). The gap it closes is specifically the
handful of things `unloadZone()` only **disables** rather than removes at
that point (notably `SkyRenderer`'s dome/sun/moon/glow nodes, see below), or
future code that adds a new such case.

### Destruction-path table

| Path | Calls `clearTextureReferences()`? | Notes |
|---|---|---|
| `evictTexture()` (LRU path, `:1099`) | **Yes**, always did | Also notifies `evictionListeners_` before either step |
| `clear()` (zone-unload hook, `:595`→now `:614`) | **No, until this fix — now yes** | Sole zone-unload hook per its own comment; also called harmlessly from the destructor |
| Same-name replacement (`registerTexture()`, old `:511`) | Indirectly — delegates to `evictTexture(name)` | Already safe, no change needed |
| Any other `driver_->removeTexture()` call outside this class | None found | `removeTexture()` calls in this file are all inside `ConstrainedTextureCache` (`clear()`, `evictTexture()`, and the compressed-upload failure paths, which remove a texture that was never registered/exposed to any material) |

### `clearTextureReferences()` completeness audit — what it covers and what it misses

**Covers:** any `ITexture*` reachable by walking `smgr_->getRootSceneNode()`
recursively — both `IMeshSceneNode`/`IAnimatedMeshSceneNode` mesh-buffer
materials *and* the generic per-node override material slot
(`node->getMaterial(i)`), which also catches `IBillboardSceneNode` (sun,
moon, sun glow) since that check is type-agnostic. Combined with the
existing `RegionMeshTextureGuard`/entity-mesh-guard eviction listeners for
merged-mesh buffers built outside the live scene graph
(`renderer_zone_merge.h`), and `SkyRenderer`'s own eviction-listener
nulling of its private texture lookup map, this is comprehensive for the
`evictTexture()` path.

**Misses (all real, none newly introduced by this unit):**

- **Anything a manager only disables rather than removes at the point
  `clear()`/`evictTexture()` runs.** `SkyRenderer::setEnabled(false)`
  (called from `unloadZone()`) sets `setVisible(false)` on its nodes but
  does not call `clearSkyNodes()` — the nodes, and their stale material
  texture pointers, survive in the scene graph until the *next* zone's
  `createSkyDome()` call. `TumbleweedManager::onZoneLeave()` does the same
  (`node->setVisible(false)`, pool entries kept). Because
  `clearTextureReferences()` walks the live scene graph, it *would* catch
  these if invoked — and now is, via this unit's `clear()` fix — but a
  future manager that adds a similar "hide, don't remove" pattern without
  also being covered by a `clear()`/`evictTexture()` pass would reintroduce
  this class of gap.
  - Note this is why disabled-not-removed sky/tumbleweed nodes were **not**
    a live crash vector for the *normal* render path even before this fix:
    `ISceneNode::setVisible(false)` means `OnRegisterSceneNode()` never
    queues the node for a solid/transparent draw pass, so
    `applyMaterialRenderState()` is never called on their stale materials
    through ordinary scene-graph rendering. The hazard was real (a manual
    draw path, a re-enable race, or any code that bypasses the visibility
    gate could still reach it) but this unit did not find a path that
    proves it as *the* render-time write.
- **2D UI textures and anything that caches a raw `ITexture*` outside a
  scene-node `SMaterial` or a listener-registered lookup map.** Not audited
  exhaustively this unit — flagged, not chased, since it is outside the
  `clear()`/`evictTexture()` asymmetry this unit was scoped to.

### Re-run ASAN with quarantine enabled — NOT completed this unit (deviation, flagged)

**Not run.** Session time was spent instead on (a) the diagnostic
instrumentation and its two x86 zone-crossing tours, one of which
independently reproduced a real, previously-undocumented crash signature
(see below) on the very first attempt without any sanitizer, and (b) five
live Pi runs re-measuring the crash rate after the fix (see next section).
Both were judged higher-value than the ASAN-quarantine re-run given what
they actually found. **This is an explicit deviation from the plan**,
recorded rather than silently dropped: Leg 2/3's `quarantine_size_mb=0`
weakness (T118) is unresolved, and T118's ASAN negatives remain exactly as
weak as T118 itself already documented. A future unit should still do this
if the corruption survives further narrowing.

### A new signature, found for free: "double free or corruption (out)"

Running the diagnostic build through the x86 zone-crossing tour (Befallen →
Qeynos2 → Befallen → `/camp`, no ASAN, no Pi) **crashed on the first
attempt**, immediately after `/camp` teardown, with glibc's `double free or
corruption (out)` — a **fifth distinct heap-corruption sub-signature**
across this arc (T117 catalogued four: the historical mid-tour `ranges_`
SIGSEGV, `SkyData::~SkyData()`'s "corrupted size vs. prev_size", the
unsymbolized Mesa/Lima SIGBUS, and `RaceModelLoader::~RaceModelLoader()`'s
"double free or corruption (!prev)"). `(out)` is glibc's `_int_free()`
rejecting a pointer whose chunk metadata places it outside the arena's
expected bounds — consistent with the same "corrupted container differs per
run" pattern T118/T117 already established, not a new independent bug
class. The diagnostic logged **zero** dead-texture hits during this same
run, meaning this specific crash was not observed going through
`cacheTexParams()`.

A repeat of the identical tour under `gdb` (`handle SIGABRT stop`) did
**not** crash within the same tour length — consistent with a
timing/allocator-layout-sensitive defect (gdb's overhead measurably changes
scheduling/allocator behavior), not a deterministic one. This matches the
established character of every prior signature in this arc.

**Post-fix, the same tour run twice cleanly** (`review/T119/
x86_postfix_run1.log`, `x86_postfix_run2.log` — both reached `Game thread
stopped`/teardown logging with no crash text). Two clean repeats after one
crashing pre-fix attempt is suggestive, not a rate — see the Pi numbers
below for the real measurement, which complicates this.

### Pi re-measurement: **0 of 5 clean** — the fix does NOT close T117's crash signatures

**Deviation, flagged clearly**: the plan calls for 10 runs; this unit
completed **5** before session time ran out, and reports the partial tally
rather than stopping on convenient early results or silently truncating.
Binary: normal (non-sanitizer) ARM cross-compile of this unit's fix,
stripped to 10,275,568 bytes (matches the documented ~10MB baseline),
staged to Pi `/tmp`. Same tour, config, and flags as T117
(`--constrained orangepi --drm --renderer gles2 -r 800 600 --log-level=INFO`,
audio enabled) for direct comparability. Logs: `review/T119/pi/run{1..5}.log`.

| Run | Outcome | Signature |
|---|---|---|
| 1 | Killed by kernel OOM mid-tour (before `/move -700`) | `oom-kill` in `dmesg`, anon-rss 270848kB on a 512MB device — a different failure mode than T117's four, not one of them |
| 2 | Reached `/camp`, crashed at final teardown | `corrupted size vs. prev_size` — **matches T117 run 2's `SkyData`-adjacent signature; persists after this unit's fix** |
| 3 | Crashed immediately after `/move -700 20 3`, mid-tour, no message | Silent death (no dmesg entry, no glibc text) — **matches the historical mid-tour `ranges_`/`0x2601` SIGSEGV signature exactly (same triggering movement); persists after this unit's fix** |
| 4 | Reached `/camp`, crashed during `unloadZone()`/`clear()`'s own teardown sequence | `corrupted size vs. prev_size` again, appearing right at/after the code this unit's fix touches — but the corruption clearly originates earlier in the same run (this arc's established "manifests far from its cause" pattern), not from the fix itself (verified the fix does not touch the placeholder texture: `placeholderTexture_` is never stored in `cache_`, so it is never iterated/destroyed by `clear()`'s loop, and remains valid for `clearTextureReferences()`'s replacement use throughout) |
| 5 | Crashed immediately after `/move -700 20 3`, mid-tour, no message | Same as run 3 |

**0 of 5 clean, versus T117's 2 of 10 baseline.** This sample is too small
to claim a worse rate with confidence, but it flatly does not show an
improvement, and two of five runs reproduce the *exact* pre-fix mid-tour
signature at the *exact* triggering movement. **Caveat honestly noted**:
this session's Pi runs showed unusually heavy frame-time degradation (RED
frame states 44–60ms against a 33.3ms budget throughout, versus T117's
reported 40-54 FPS baseline) — a plausible sign of thermal or scheduling
contention specific to this session that could itself perturb the
timing-sensitive corruption's trigger rate in either direction. This is
noted as a confound, not used to discount the result.

### Conclusion: fix is real and justified, but does not close this arc

The `clear()`/`clearTextureReferences()` asymmetry was real, is now fixed,
and is defensible on its own terms (mirrors an already-proven-safe sibling
path, changes no ownership semantics, x86 suite/visual-gate/ARM
cross-compile all clean). But the Pi evidence is unambiguous: **the T117
crash signatures survive this fix.** Per the plan's own success criterion,
this is reported as "here is a real, narrow, justified fix — and here is
the evidence that the arc's actual corruptor is something else, or at
least something larger, that this unit did not reach." The diagnostic's
zero hits on `cacheTexParams()` (including during an independently
reproduced crash) further argue against T118's candidate mechanism as the
(sole) explanation.

### Deviations (summary)

1. ASAN-with-quarantine Pi re-run (plan's Step 2) not completed — time spent
   on the diagnostic + Pi re-measurement instead; explicitly flagged above,
   not silently dropped.
2. Pi run count is 5, not the target 10, due to session time; reported as a
   partial tally per protocol ("if you must stop early... say exactly why
   and how many you got"), not rounded up or presented as a full baseline.
3. The diagnostic instrumentation (`WILLEQ_T119_DIAG_DEAD_TEXTURE`) was
   written, used to gather the "zero hits" evidence above, and then removed
   from the final commit per the plan's own instruction ("keep it out of
   the final commit unless it earns its place") — it did not confirm the
   hypothesis, so it did not earn a place in the shipped diff.

## Board

**Does not close.** One mechanism was hoped to explain both the mid-tour
`ranges_` crash and the teardown ones; this unit's fix targets only a
teardown-adjacent hazard and the Pi evidence shows the mid-tour signature
(`ranges_`/`0x2601`, runs 3 and 5) and one teardown signature (`SkyData`-
adjacent "corrupted size vs. prev_size", runs 2 and 4) **both still
reproduce after the fix.** What the fix does close: a real,
now-demonstrated (via the code-level ownership/refcounting analysis above)
use-after-free hazard in `clear()` that had no protection at all before —
worth keeping regardless of whether it was the arc's dominant cause. What
remains open, unchanged from before this unit: the actual source of the
mid-tour `ranges_` corruption and of the `SkyData`-adjacent teardown
corruption. Durable artifacts for the next unit regardless of outcome: the
destruction-path table and the `clearTextureReferences()` completeness
audit above, plus a fifth heap-corruption signature (`double free or
corruption (out)`) and a live x86 (non-Pi, non-ASAN) repro recipe for a
crash in this same family — the zone-crossing + `/camp` tour in
`review/T119/tour_zone_cross_x86.txt`, which crashed on its first-ever
attempt with no sanitizer and no hardware required.
