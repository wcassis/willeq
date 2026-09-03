# L02 — `Eqe1EffectsComponent` (family: `renderer_effects.cpp` — EQE1 archive resolution + fire-glow icospheres)

Batch: `refactor/L_renderer_component_extraction.md` (§L01 corrections are
the template). Wave 1, three-wide with L03/L04. Measured at `cfbbde57`.

The family name "effects" is misleading: 11 of the 14 bodies are EQE1
archive plumbing (`getOrOpenEQE1`, `getOrOpenEQE1FromDir`,
`resolveEQE1Archives`, `openArchiveContaining`, `openCompositeSheetEQE1`,
`isPersistentEQE1Archive`, `loadEQE1ArchiveConfig`, `logEQE1ArchiveUsage`,
`getEQE1MappedBytesTotal`, `addSpawnedRaceEQE1Candidate`) and 3 are the
fire-glow icosphere (`buildIcosphereMesh`, `compileIcosphereShader`,
`renderFireGlowIcospheres`, `updateFireGlowLighting`). Name the component
for what it is: `Eqe1ArchiveComponent`, and put the icosphere trio in the
same TU under its own banner (they share the `icoLoc*` uniforms, all 9
single-family). 697 lines; single-family members: the 9 `icoLoc*` uniform
locations (measure; the batch survey says 9).

Callers (word grep; the compiler confirms): `renderer_zone_load.cpp` (6
methods), `renderer_zone_stream.cpp`, `renderer_zone_load_env.cpp` (2),
`renderer_init.cpp` (2), `renderer_zone_lifecycle.cpp`, `renderer_diagnostics.cpp`,
`renderer_frame_render.cpp` (2), `constrained_texture_cache.h`
(`getOrOpenEQE1` — read it: a callback type or a comment). All in-renderer;
retarget as `eqe1_->X()` inside `IrrlichtRenderer` bodies and
`ctx_.Eqe1().X()` inside other components (none yet besides rdp).
`getOrOpenEQE1FromDir` has no external caller — in-family or dead (§5 with
the in-family rule; check for `pushEvent` first, K12 ff).

Text readers: `tests/cmake/commands.cmake` defines
`RENDERER_EFFECTS_SOURCE=…/renderer_effects.cpp` — repoint to the new path;
`tests/test_eqe1_archive_config.cpp` names `renderer_effects.cpp` in a
message and a comment and reads the built-in fallback list — read it; if
it parses the TU as text, the repoint is territory (K03 c).

Predicted header delta: −14 declarations −9 members −banners +(4..6
scaffolding at L01 cost) ≈ **−15 or better**.

Territory: `include/client/graphics/eqe1/eqe1_archive_component.h` (new),
`src/client/graphics/eqe1/eqe1_archive_component.cpp` (git mv of
`renderer_effects.cpp`), `irrlicht_renderer.h`, `renderer_init.cpp` (ctor
construction line only), root `CMakeLists.txt`, `tests/cmake/commands.cmake`
(the one define), `tests/test_eqe1_archive_config.cpp` (repoint only),
`tests/test_eqe1_archive_component.cpp` (new) + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the renderer TUs named. L03
and L04 edit `irrlicht_renderer.h`, `renderer_init.cpp`, `renderer_frame_render.cpp`,
`renderer_frame_input.cpp` at their own lines. Files under
`src/client/graphics` change: visual gate + ARM are NOT skippable.

## Review

Executed in `wt/slot1` on `task/L02-eqe1-archive-component`, branched from
`fable` at `d13f8ddc` (the dispatched hash; verified before any edit). Every
verification step passed; nothing STOPped.

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,871 -> 1,809 (-62)**;
allowlist lowered to 1,809 in the same commit. **Zero forwarders** — no caller
of any of the 14 methods lives outside `IrrlichtRenderer`, so Mechanism 5
applied in full and every call site was retargeted to `eqe1_->`. The moved
TU's residual is **zero code lines removed and one added** (the component's own
header include) — the cleanest residual in either batch so far. New suite total
3,622, one failure (the ALSA quirk).

### Measured vs the plan's lead

| plan | measured at `d13f8ddc` |
|---|---|
| "11 of the 14 bodies are EQE1 plumbing ... and 3 are the fire-glow icosphere" | **10 and 4**. `updateFireGlowLighting` is icosphere-side, not EQE1-side, and `isPersistentEQE1Archive` is an inline header static with no `IrrlichtRenderer::` body — so a "count the bodies" measurement sees 13 definitions, not 14. The declaration count IS 14 (K04 correction f: the inline accessor/static is part of the move and invisible to body counts). |
| 697 lines | 697 confirmed |
| "single-family members: the 9 `icoLoc*` uniform locations (measure; the batch survey says 9)" | **9 confirmed**, plus **two the survey missed**: the nested `EQE1ArchiveEntry` struct and the `eqe1ArchiveEntries_` map it keys — see the deviation below |
| callers: `renderer_zone_load.cpp` (6), `renderer_zone_stream.cpp`, `renderer_zone_load_env.cpp` (2), `renderer_init.cpp` (2), `renderer_zone_lifecycle.cpp`, `renderer_diagnostics.cpp`, `renderer_frame_render.cpp` (2) | **exact, all 15, and the compiler named no others.** The one adjustment: `renderer_zone_load_env.cpp`'s two are `buildIcosphereMesh`/`compileIcosphereShader`, and `renderer_frame_render.cpp`'s two are `updateFireGlowLighting`/`renderFireGlowIcospheres` |
| `constrained_texture_cache.h` names `getOrOpenEQE1` | **a comment** (L525, "from prep-worker/equipment threads via getOrOpenEQE1"), not a callback type. Still true of the component; left alone |
| `getOrOpenEQE1FromDir` "has no external caller — in-family or dead" | **in-family, kept private.** Two in-family callers (`getOrOpenEQE1`, `openCompositeSheetEQE1`). K03 correction (a) applies; it was never a deletion candidate. No `pushEvent`/`GameEventType::` anywhere in this TU, so K12 (ff) had nothing to protect either |
| `tests/cmake/commands.cmake` `RENDERER_EFFECTS_SOURCE` | repointed to the new path |
| `tests/test_eqe1_archive_config.cpp` "if it parses the TU as text, the repoint is territory" | **it does** — `std::ifstream in(RENDERER_EFFECTS_SOURCE)` and a `find("\"gequip7\"")` over the whole file. It also `ASSERT_TRUE(in.is_open())`, so a bad repoint would fail loudly rather than pass over an empty buffer (contrast K11 correction ee). Repointed in the cmake define; the two prose mentions of the old filename in the test were updated too |

One text reader the plan did not name was found by a tree-wide grep after the
move: `tools/hwcaps/zone_bench_memory.cpp:168` cites `renderer_effects.cpp` in
a comment next to `getEQE1MappedBytesTotal()`. Repointed (K05 correction l — a
comment naming a file path is a live citation).

### Member census (the whole risk surface)

32 trailing-underscore identifiers appear in the moved TU. Every one was
classified before the first build; the counts are the substitution counts the
script reported.

| class | count | members |
|---|---|---|
| `RendererContext` core -> `ctx_.<name>` | 5 (28 sites) | `camera_` (4), `config_` (5), `constrainedTextureCache_` (4), `currentZoneName_` (5), `zoneShader_` (10) |
| shared with another family, stays on the renderer -> `owner_.<name>` | 17 (95 sites) | `captured3DProj_`, `captured3DView_`, `eqe1CandidateRaces_`, `eqe1CandidatesMutex_`, `eqe1Index_`, `eqe1Loaders_`, `eqe1LoadTimeCandidateCount_`, `eqe1Mutex_`, `fireGlowLights_`, `fireGlowTime_`, `icosphereIBO_`, `icosphereIndices_`, `icosphereProgram_`, `icosphereVBO_`, `icosphereVertices_`, `maxFireGlowLights_`, `pendingGlobalEQE1Archives_` |
| single-family, MOVED (bare name kept) | 9 | the `icoLoc*` uniform locations |
| `IrrlichtRenderer::` -> `Eqe1ArchiveComponent::` | 15 sites | the 14 definition headers (`resolveEQE1Archives` carries two on one line) plus the one qualified call to `isPersistentEQE1Archive` |

Substitutions were applied to **code lines only** (K01 correction 4). The file
carries exactly two comment lines that name a member (`eqe1Mutex_` at old L398,
`eqe1Loaders_` at old L433) and **no code line has a trailing `//` comment
containing one** — both facts were measured before the script ran, so a
`lstrip().startswith("//")` skip was provably sufficient here. K08 correction
(w)'s trailing-comment hazard did not arise; do not assume that for the next TU.

### Deviation: `EQE1ArchiveEntry` + `eqe1ArchiveEntries_` moved, against §3

Mechanism 3 says a member shared with a second family stays on the renderer.
`eqe1ArchiveEntries_` is read by one line outside the family —
`renderer_init.cpp:320`, `if (eqe1ArchiveEntries_.empty())`, guarding the
`loadEQE1ArchiveConfig()` call on the next line — so §3 read literally would
keep it, **and with it the nested `EQE1ArchiveEntry` struct that types it**.
That is not viable: `resolveEQE1Archives` RETURNS `EQE1ArchiveEntry`, so the
component's own header would have to name `IrrlichtRenderer::EQE1ArchiveEntry`
in a public signature, which forces `#include "irrlicht_renderer.h"` into
`eqe1_archive_component.h` — the ~60-transitive-include cycle L01 correction 4
exists to avoid, in the header of the thing being extracted.

Both moved instead. `renderer_init.cpp`'s guard became
`if (eqe1_->archiveEntries().empty())`, reading a new 3-line public accessor on
the component. Recorded as a deviation, not absorbed. **The general rule it
suggests: a nested TYPE that appears in a moving method's signature is not
subject to §3's shared-member test — it must land wherever the signature lands,
and any member typed by it follows it.** Note also that "init/core" is not a
family any L unit extracts, so a member shared only with `renderer_init.cpp`
would otherwise stay on the renderer forever; §3's "the unit extracting the
LAST family moves it" has no terminating unit for that case.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan's prediction | **-15 or better** |
| this unit's pre-execution prediction | **-58** (70 removed, +12 scaffolding at L01's +3..+6 per construct) |
| measured | **-62** (1,871 -> 1,809) |

The plan's -15 was pessimistic by a factor of four because it costed the
**declarations only**. The EQE1 declarations in `irrlicht_renderer.h` carry
**44 lines of comment banner** between them (T207/T214/T266/T284 design notes,
5 to 9 lines each), and a banner moves with the declaration it documents. 70 of
the 77 deleted lines were predicted exactly; the extra 7 are the
`EQE1ArchiveEntry` struct + its comment + the `eqe1ArchiveEntries_` line + a
blank, decided mid-unit (the deviation above).

| block | predicted | actual |
|---|---|---|
| 14 declarations (16 lines: 2 are multi-line, 1 is a 3-line inline static) | -16 | -16 |
| their comment banners | -44 | -44 |
| 9 `icoLoc*` members | -9 | -9 |
| separating blanks | -1 | -1 |
| `EQE1ArchiveEntry` + `eqe1ArchiveEntries_` + comment + blank | 0 | **-7** |
| include | +1 | +1 |
| `Eqe1()` accessor + 4-line comment + blank | +5 | +6 |
| `friend class Eqe1ArchiveComponent;` + 2-line comment + blank | +3 | +4 |
| `eqe1_` member + 2-line comment + blank | +3 | +4 |

**L01's +3..+6-per-construct scaffolding rule held** (three constructs, 14
lines). The lesson to add for L03+: **predict the BANNER, not just the
declaration.** In this header a declaration costs on average 4.3 lines, not 1.

### Forwarders

**Zero.** The plan's caller table was right that every caller is in-renderer, so
Mechanism 5 applied without exception and no `IrrlichtRenderer` declaration
survives for this family. `Eqe1()` (the accessor) and `archiveEntries()` (the
accessor the deviation required) are scaffolding, not forwarders.
`isPersistentEQE1Archive` is a static and its one external call site became
`Eqe1ArchiveComponent::isPersistentEQE1Archive(...)` — a qualified static call,
not a forwarder.

This is the first L unit with **no `IOutputRenderer` override in the family**
(L01 correction 5): the effects family is entirely internal, so the count is 0.

### Friendship

`Eqe1ArchiveComponent` **needs `friend class`**, unlike L01's `RdpComponent`.
Axis 1 of K13 correction (ii) is decisive on its own: 17 private
`IrrlichtRenderer` members are reached through `owner_.`, 95 sites. Axis 2
(qualified private names) is empty after the move — the one such name,
`IrrlichtRenderer::isPersistentEQE1Archive`, came with the family.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping (`ctx_.config` ->
`config_`, `owner_.eqe1Loaders_` -> `eqe1Loaders_`, ... for all 22, plus
`Eqe1ArchiveComponent::` -> `IrrlichtRenderer::`), longest-first; comment and
blank lines dropped; both sides sorted into multisets. 511 old code lines
against 512 new:

```
removed (0):
  -- nothing
added (1):
  #include "client/graphics/eqe1/eqe1_archive_component.h"
```

**Zero moved body lines removed, added or modified.** `irrlicht_renderer.h` is
still included by the moved TU — 17 `owner_` members are private renderer state
and the complete type is required — so L01 correction 4's "incomplete type"
iteration did not arise here at all, and the only new line is the component's
own header. K13 correction (kk)'s observation holds in the other direction too:
the residual is exact and trivial precisely because every substitution is a
pure rename.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, clean on the FIRST compile — the mechanical pre-classification of all 32 members (K08 correction x) meant zero build iterations on the move itself |
| `ctest --test-dir build -j6 --output-on-failure` | **3,621 / 3,622 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds`** (the known ALSA quirk) — the only permitted one. `Eqe1ArchiveConfigTest.BuiltInFallbackListHasNoPhantomGequip7` passes against the repointed path; `FileSizeCeiling` and `DocCitations` both pass |
| `test_eqe1_archive_component` (new) | **9 tests, all pass** (none skipped — the EQE1 tree is mounted on this host) |
| `scripts/build-gles2-x86.sh` | **exit 0** (after the fix below) |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), all three zones + memory coverage. `git status --porcelain` names **no golden path** — nothing re-blessed |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **836 checked, 836 ok, 0 warn, 0 FAIL**, with the `irrlicht_renderer.h` allowlist entry lowered 1,871 -> 1,809 |

Neither long build hit an exit-143 kill: the harness moved two of them to the
background at its 600 s ceiling, but the underlying process ran to completion
and reported exit 0, so the truncation/absence scan was not triggered.
`build/lib/libwilleq_core.a`, `build/bin/willeq` and the new test binary were
asserted present regardless.

### The one build failure, and why the x86 build could not see it

`build-gles2-x86.sh` failed with `'Eqe1ArchiveComponent' does not name a type`.
The cause: `irrlicht_renderer.h`'s component-include site is **inside
`#ifdef WITH_RDP`** — L01 put `rdp_component.h` there correctly, next to
`rdp_server.h`, and the natural place to append the second component's include
is the line after it. This slot's `build/` cache has `WITH_RDP=ON`, so the main
build and the whole ctest suite passed over the bug; `build-gles2/` configures
without FreeRDP and caught it immediately. Fixed by hoisting the include next to
`renderer_context.h`, outside the guard.

**Correction for L03+: `irrlicht_renderer.h`'s include block is not a flat list
— `renderer_context.h` is unguarded and the three RDP includes are not. Put an
unconditional component include with `renderer_context.h`, and confirm with
`build-gles2-x86.sh`, not with `build/`, because the two trees disagree on
`WITH_RDP`.**

### The ARM build is load-bearing for this family

The four icosphere bodies are guarded by `#ifdef EQT_HAS_DRM`, which is defined
only when `EQT_DRM=ON` — `scripts/build-gles2-x86.sh` and `docker/Dockerfile`
both pass `-DEQT_DRM=OFF`, `docker/Dockerfile.arm-noble` passes `-DEQT_DRM=ON`.
So **no x86 compiler in this repo ever sees `buildIcosphereMesh`,
`compileIcosphereShader`, `updateFireGlowLighting` or `renderFireGlowIcospheres`
— nor the 9 `icoLoc*` members, which are inside the same guard in both the old
and the new header.** Proved rather than assumed:
`strings build-arm-noble/bin/willeq | grep -c 'Compiled icosphere shader program'`
returns **1** and the same grep on `build/bin/willeq` returns **0**. The ARM
cross-compile is the only verification step that compiled 4 of this unit's 14
moved bodies and 9 of its 10 moved members; skipping it would have left roughly
a third of the move unchecked by any compiler.

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,871 | **1,809** (-62) |
| `src/client/graphics/renderer_effects.cpp` -> `src/client/graphics/eqe1/eqe1_archive_component.cpp` | 697 | 726 (+29: the 27-line move banner, the component header include, one blank) |
| `include/client/graphics/eqe1/eqe1_archive_component.h` | — | 159 (new) |
| `tests/test_eqe1_archive_component.cpp` | — | 140 (new) |

### Tests

`tests/test_eqe1_archive_component.cpp`, registered in
`tests/cmake/renderer_guards.cmake` next to `test_rdp_component` (L01
correction 10's placement, reused deliberately: it links `willeq_core` and
constructs a bare, never-`Init()`'d `IrrlichtRenderer`). Nine cases:

- `Eqe1()` returns one stable component (it is constructed in the ctor body).
- `isPersistentEQE1Archive` — the T214 rule, now a component static.
- `archiveEntries()` empty on a fresh renderer — this is exactly the condition
  `renderer_init.cpp` guards on, so the guard's premise is pinned.
- the missing-config path installs the built-in fallback, with the `{zone}`
  triple and **no `gequip7`** (a second, behavioural guard on what
  `test_eqe1_archive_config.cpp` checks by text).
- `resolveEQE1Archives` substitutes `{zone}`; and yields nothing on an empty map.
- nothing is open on a fresh renderer, and `logEQE1ArchiveUsage()` is a no-op
  rather than a crash (it locks `owner_.eqe1Mutex_` and reads
  `owner_.eqe1Loaders_`).
- with `eqe1Path` empty, `getOrOpenEQE1` opens nothing.
- **the binding proof, by mutation in one path (L01 correction 6/9):**
  `eqe1Path` is a private renderer member with no public setter, so the test
  writes `renderer.Context().config.constrainedConfig.eqe1Path` and the
  component then really opens `willeq.eqe1` — proving `ctx_.config` is a
  reference to `config_` — and `getEQE1MappedBytesTotal()` reads the open loader
  back out of `owner_.eqe1Loaders_`, proving the transition handle in the same
  assertion. Idempotence and the negative cache are checked too. Asset-gated
  with `GTEST_SKIP` on `/mnt/projects/EverQuest_ETC1`, the idiom
  `test_eqe1_archive_config.cpp` already uses.

### Corrections to the Batch L template

1. **Predict the comment BANNER, not the declaration.** L02's plan predicted
   -15 and the unit landed -62 because `irrlicht_renderer.h` documents its
   declarations at 4 to 9 lines each. Count the banner lines that travel with
   each moving declaration; in this header a declaration costs ~4.3 lines.
2. **A nested type in a moving signature is exempt from §3.** If a moving method
   returns or takes an `IrrlichtRenderer` nested type, the type must move (or
   the component header must include `irrlicht_renderer.h`, which defeats the
   extraction). Any member typed by it moves with it, and its out-of-family
   readers get a small public accessor.
3. **`renderer_init.cpp` is a terminal owner.** §3's "the LAST family to use it
   moves it" never fires for a member shared only with init/core, because init
   is not extracted. Decide such members in the unit that meets them.
4. **The component include goes with `renderer_context.h`, outside
   `#ifdef WITH_RDP`.** L01's placement is correct for RDP and wrong to copy.
   `build/` (WITH_RDP=ON) will not catch it; `build-gles2-x86.sh` will.
5. **The ARM build is the ONLY compiler for `EQT_HAS_DRM` code**, and a family
   containing any is not verified without it. Prove which side compiled what
   with `strings` on the two binaries rather than assuming.
6. **A family can need friendship on axis 1 alone.** L01's "no friend" result
   was a property of `RdpComponent`, not of the mechanism; 17 `owner_.` members
   is the normal case, and the friend line is then not optional.
7. **Zero forwarders is achievable and is the goal.** No `Application` or
   `IrrlichtBridge` caller exists for this family, so the K11 rule imposed no
   forwarders at all. Check the external-surface list per unit before assuming
   forwarders are needed.
