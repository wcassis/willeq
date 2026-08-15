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

(executor fills: whether a dead texture ever reaches `cacheTexParams()`, and
how that was demonstrated; the `clear()` vs `evictTexture()` finding
confirmed or refuted; the full destruction-path table; `clearTextureReferences()`
completeness with the holders it misses; the re-run ASAN result **with
quarantine enabled** and whether it changes T118's negatives; the fix and why
it is narrow, or the diagnosis; **N of 10** Pi runs; deviations)

## Board

If this closes, say what it means for all four T117 signatures — one
mechanism should explain the mid-tour `ranges_` crash **and** the teardown
ones, and if it only explains some, say which remain. If it does not close,
record what was eliminated: the destruction-path table and the
`clearTextureReferences()` completeness audit are durable regardless of
outcome.
