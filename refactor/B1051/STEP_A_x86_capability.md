# B1051 Step (a) — x86 ETC1 capability determination (client-fault)

**Verdict: the x86/llvmpipe capability check CLEARS the driver.** llvmpipe
uploads and decodes ETC1; the multi-buffer character smear therefore comes from
the **client's multi-buffer ETC1 path**, not from a missing/broken Mesa ETC1
capability. The ruling's step (b) — "only if (a) clears the client, confirm the
Panfrost ETC2-clump fault on the XU4" — is **MOOT**: the client is the fault, on
two drivers (llvmpipe x86 AND Midgard XU4 both smear; both render correctly
with raw RGBA).

This **overturns the take-3 root cause**
(`refactor/B1051/ROOT_CAUSE_t3.md` §4), which had attributed the smear to a
Panfrost/Midgard ETC1→ETC2 clump + companion-alpha shader variant. Take-3 ran
only on the XU4; it never tested whether llvmpipe had ETC1 at all, so it could
not distinguish "Mesa clump bug" from "client ETC1 multi-buffer bug". The x86
leg (this step) is the discriminator the ruling asked for, and it rules out the
driver on the x86 side — which is the side where a driver bug would NOT be
invoked (llvmpipe does not use the Panfrost clump code path at all). If the
smear is identical on a driver that has no Panfrost ETC1 clump, the common
cause must be the client.

---

## 1. The three independent x86 measurements (all at branch head 6e178c3b)

### (1) llvmpipe HAS ETC1 — extension present, uploads accepted

Probe `etalib.c` (committed under `refactor/B1051/mesa-probe/`) replicates the
client's exact EGL sequence — `eglGetPlatformDisplayEXT(
EGL_PLATFORM_SURFACELESS_MESA, ...)` + `eglBindAPI(EGL_OPENGL_ES_API)` before
`eglChooseConfig` (the missing `eglBindAPI` is why every earlier probe reported
"no cfg") — against the same Mesa 25.2.8-0ubuntu0.24.04.4 the client loads
(`[gldriver]` line, client.log). It enumerates extensions and uploads real
compressed textures on a surfaceless ES2 and ES3 context:

```
ver=OpenGL ES 3.2 Mesa 25.2.8-0ubuntu0.24.04.4
render=llvmpipe (LLVM 20.1.2, 256 bits) vendor=Mesa
[surfaceless ES2] ETC1_OES=1 ETC2=0 ASTC=1 S3TC=1
  RGBA8 16x16           0x0000 ACCEPTED
  ETC1 16x16 (128B)    0x0000 ACCEPTED
  ETC1 64x64 (2048B)   0x0000 ACCEPTED
  S3TC_DXT1 16x16 (128B) 0x0000 ACCEPTED
[surfaceless ES3] ETC1_OES=1 ETC2=0 ASTC=1 S3TC=1
  ETC1 16x16 (128B)    0x0000 ACCEPTED
  ETC1 64x64 (2048B)   0x0000 ACCEPTED
  S3TC_DXT1 16x16 (128B) 0x0000 ACCEPTED
```

`GL_OES_compressed_ETC1_RGB8_texture` is in `glGetString(GL_EXTENSIONS)`
(ETC1_OES=1). ETC1 uploads return `glGetError()==0x0000` for 16×16 (128 B) and
64×64 (2048 B) — the exact sizes the character QCMs use. **x86 has working ETC1
storage.**

> Earlier probes that reported `0x0501 INVALID_OPERATION` for the 16×16 ETC1
> were a **probe bug**: they passed 256 bytes for a 16×16 ETC1 that is 128
> bytes. With the correct `levelSize` the upload is accepted. Do NOT cite the
> earlier rejection as a driver finding.

### (2) The client's multi-buffer character smears on x86 with ETC1, renders
correctly with raw RGBA — same guard, same binary, same head

Two back-to-back live runs against the play server, current binary
(`build/bin/willeq` at 6e178c3b, 2026-10-15 05:42), `--renderer gles2
--constrained orangepi -r 1280 720`, `Guard_Treitan` (qeynos2, race 71,
multi-buffer, `placeholder=false`) framed arm's length via `/moveto` +
`/face` (both `outcome=applied`):

| Run | Texture path | `--eqe1-path` | Guard_Treitan render | Zone (single-buffer ETC1) |
|-----|--------------|---------------|----------------------|---------------------------|
| ETC1 | compressed ETC1 + companion alpha | set | **SMEAR** — one flat two-tone mottle over the whole body | CORRECT |
| RGBA | raw S3D/RGBA (ETC1 off) | removed | **CORRECT** — distinct helmet, face, chestplate, arms, legs, sword, shield | CORRECT |

Three independent reads agree:
- **Vision** (full-frame): ETC1 frame = single flat mottled texture stretched
  over the body, no distinct parts; RGBA frame = clearly distinct per-part
  materials. The zone geometry is correctly textured in both.
- **WILLEQ_B1051_TRACE** (driver sampler read-back at draw time): in both runs
  the guard's mat=256 buffers DO bind *distinct* per-part color textures on
  unit 0 (the real QCM sizes w=64/32/128, distinct GL ids per part) — so the
  per-buffer color **binding is not** the fault; the GPU is handed distinct
  textures and still shows one.
- **Pixel color-diversity** of the guard-body crop (frame_000050): ETC1 region
  top-5 color-bucket share 0.76–0.87 (mottled/low diversity) vs RGBA 0.43–0.50
  (distinct/high diversity).

The zone (single-buffer ETC1) renders correctly in the **ETC1** run, which is
the key point: llvmpipe **does** sample ETC1 textures correctly for the zone.
So ETC1 storage itself is fine on x86; only the **multi-buffer character**
ETC1 path mis-renders.

### (3) The client ETC1 path vs the RGBA path differ only in storage + the
multi-buffer material/alpha routing

The working RGBA path and the failing ETC1 path are identical through mesh
construction and per-buffer `setMaterial`; they diverge at texture storage:
- ETC1: `ConstrainedTextureCache::createETC1Storage`
  (`constrained_texture_cache_compressed.cpp:556`) uploads
  `GL_ETC1_RGB8_OES` (0x8D64) level 0 only, `GL_LINEAR` filters, `GL_REPEAT`
  wrap, and records `cached.isETC1 = true` ("companion-based alpha", H02d)
  with an `_alpha` companion texture on unit 1.
- RGBA: raw `glTexImage2D` path (`constrained_texture_cache.cpp`), no
  `isETC1`, no companion.

Both runs bind distinct per-part color textures per buffer (measurement (2));
the only ETC1-only render-relevant difference is the **compressed ETC1 storage
+ `isETC1` companion-alpha material routing for the multi-buffer character**.
That is a **client** property (it holds on llvmpipe, which has no Panfrost
clump path). The take-3 hypothesis that a Panfrost ETC1→ETC2 clump is the
mechanism cannot explain an identical failure on llvmpipe.

---

## 2. Outcome table (ruling step (a))

| Check | Expected if llvmpipe LACKS ETC1 | Expected if llvmpipe HAS ETC1 (observed) |
|-------|----------------------------------|------------------------------------------|
| `GL_OES_compressed_ETC1_RGB8_texture` in ext list | absent | **present** (ETC1_OES=1) |
| ETC1 16×16/64×64 `glCompressedTexImage2D` | rejected | **ACCEPTED 0x0000** |
| Zone single-buffer ETC1 on x86 | magenta/black | **correct** |
| Multi-buffer character on x86 (ETC1) | — | **smear** |
| Multi-buffer character on x86 (RGBA) | — | **correct** |

Ruling branch: **"if x86 HAS ETC1 and still paints the multi-buffer figure
magenta, the fault is in the CLIENT's multi-buffer ETC1 upload (same bug, two
drivers) and the Mesa deep-dive is moot — fix the client."** That branch fired.

## 3. Consequences for the remaining steps

- **(b) Panfrost ETC2-clump confirmation: MOOT.** The client is the fault on a
  driver (llvmpipe) that does not use the Panfrost clump path; there is no Mesa
  commit/line to name for the x86 half, and the XU4 half is the same client bug
  under a different driver. (The take-3 clump site `pan_texture.c:221` remains
  context, not a confirmed cause.)
- **(c) Midgard-only RGBA8 fallback: superseded by the real client fix.** The
  fallback (gated to Midgard) would mask the symptom on the XU4 but leave x86
  broken and not fix the underlying client bug. The correct durable fix is to
  correct the client's multi-buffer ETC1 path so it renders correctly on
  **both** drivers (then no fallback is needed at all; if a fallback is kept,
  it is a guard, not the fix).

## 4. Next (client fix — acceptance (B))

Find why 13+ distinct ETC1 character textures, correctly uploaded and correctly
bound per buffer, sample as one on both llvmpipe and Midgard. Leading candidates
to verify against the tree (do not assume):
1. The `isETC1` companion-alpha routing for the multi-buffer material — whether
   the per-buffer sampler/companion binding actually takes effect under the
   ETC1 solid material (the take-3 §4.2 candidate, now the top suspect since the
   driver is cleared).
2. Whether the level-0-only ETC1 upload + `GL_REPEAT` wrap produces a texture
   whose sampled result is uniform at the UVs the character buffers use, on both
   drivers (the zone's single-buffer geometry uses different UVs, which could
   explain why it survives).
3. A minimal 13-ETC1-texture-13-buffer GLES2 reproduction under
   `refactor/B1051/mesa-probe/` that runs on x86 (llvmpipe) to confirm the
   client-side mechanism without the XU4.

The `WILLEQ_B1051_TRACE` net is RETAINED in the branch (this analysis uses it).

## 5. Deliverables / evidence
- `refactor/B1051/mesa-probe/etalib.c` + `build_and_run.sh` — the corrected
  surfaceless ETC1 capability probe (ACCEPTED result above).
- Fresh x86 stills (current head 6e178c3b):
  - ETC1 smear: `~/.hermes/profiles/dev/cache/scratch/b1051_x86_stepA/frame_000050.png`
  - RGBA correct: `~/.hermes/profiles/dev/cache/scratch/b1051_x86_stepA_rgba/frame_000050.png`
  - traces: `<dir>/stderr.log` (WILLEQ_B1051_TRACE), logs `<dir>/client.log`
- Pixel-diversity + trace analysis scripts: `b1051_stepA/{diver,cmp2,per,freshmat,ord}.py`

## 6. Negative finding (2026-10-15, follow-up run): the offline `gles2_zone_benchmark --entity-viewer` is NOT a valid multi-buffer-ETC1 repro

I tried to get a server-free x86 repro through the P47 magenta entity
viewer (`gles2_zone_benchmark --entity-viewer --entity-viewer-race 71`
`--eqe1-path ...`, which the tooling bills as the "etc1" leg). It renders
the guard (race 71) with DISTINCT per-part textures — i.e. it does NOT
reproduce the smear. The reason, verified in the tree and the binary, is
that **the offline tool never uploads the character as compressed ETC1**:

- A `--log-level DEBUG` entity-viewer run (race 71, `--eqe1-path` set)
  produces **0** `[CONSTRAINED] ETC1 texture ... uploaded` lines and **0**
  `[CONSTRAINED] texture ... EQE1 hit` lines — the character's per-part
  textures are decoded to RGBA and uploaded as `glTexImage2D`, NOT through
  `uploadCompressedETC1()`/`createETC1Storage()`.
- Root cause in `tools/hwcaps/zone_bench_loader.cpp`: the entity path
  routes through `RaceModelLoader::getMeshForRace()` (static, unmasked S3D
  decode) plus the `--animate`-gated `preloadModelData()` (P46d comment at
  line ~893). The benchmark's `ConstrainedTextureCache` is wired with only
  the zone/`_obj.s3d` archives as its `ArchiveTextureSource`, so the
  character's per-part names do not resolve to EQE1 compressed entries —
  `findCompressedEntry()` misses and the decode path is taken. Only the
  zone meshes (single-buffer, separate loader) actually go ETC1, which is
  why the zone renders correct in this tool and always has.
- The deeper reason: the benchmark's EQE1 name→pack resolver is wired ONLY
  under `config.compositeEntities` (`zone_bench_loader.cpp:818-838`, the
  T284 comment: it is "the only x86 harness that drives the COMPOSITE path
  without a server"). The plain `--entity-viewer` (multi-buffer, non-
  composite) run opens the zone zone/obj packs at load but sets no
  `eqe1ArchiveResolver_`, so a character per-part name (e.g.
  `qcfch0001.bmp`) that lives in a `*_chr.eqe1` pack has no archive to
  resolve from — `findCompressedEntry()` returns null and the S3D RGBA
  decode is taken. This mirrors the tool's stated scope: it is a
  COMPOSITE harness, not a multi-buffer-character-ETC1 harness.
- So the "etc1" vs "rgba" legs in `--entity-viewer` differ only in the
  zone texture source, not in the character path. The character is RGBA in
  BOTH. This is consistent with P46d's long comment: the benchmark does not
  reproduce the live client's production character-ETC1 wiring.

**Consequence:** the live-client path (`EntityPrepWorker` →
`preloadModelData()` → `compressedEntries_` → `uploadCompressedETC1()`)
cannot be exercised offline. The offline character S3D (the same
`/mnt/projects/EverQuestP1999` the live run reads) carries RAW texture
bytes, so `getOrLoadTexture()`/`buildMeshFromGeometry()` decode RGBA from
S3D and never ask the cache for the name — the compressed lookup never
fires even with the EQE1 resolver wired (verified: wiring the resolver for
the non-composite leg too, `zone_bench_loader.cpp` step-11 block, still
yields 0 `EQE1 hit` / 0 `ETC1 texture ... uploaded` lines for a race-71
entity-viewer run, because the per-part names resolve from S3D raw bytes
first). The LIVE client is the opposite: its character S3D parse reports
"472 textures with 0 raw bytes (0 released at once: EQE1 authoritative)"
(client.log 05:54:58.408), so every per-part name IS a compressed lookup.
That divergence — offline S3D raw vs live S3D empty — is the structural
reason the offline tool can never reproduce this bug.

So the only valid x86 repro is the live `willeq` client against a live
server — which is what step (a)'s stills already were. **A fix must be
verified live; it cannot be verified offline.** (This is a negative
result, not a fix: nothing is claimed to be correct or incorrect about
the client's ETC1 path beyond step (a)'s "multi-buffer ETC1 client path
smears on llvmpipe.")

## 7. Server state (2026-10-15 ~07:00Z, follow-up run)

Every eqemu lane is down (T497 probe: container running, zero server
processes, `eqemu_stack_ready: NO`):

| lane | host | loginserver | world | ready |
|------|------|-------------|-------|-------|
| 1 | 172.16.200.99 | DOWN | DOWN | NO |
| 2 | 172.16.200.98 | DOWN | DOWN | NO |
| 3 | 172.16.200.97 | DOWN | DOWN | NO |
| 4 | 172.16.200.96 | DOWN | DOWN | NO |
| 5 | 172.16.200.205 | DOWN | DOWN | NO |
| card | 10.0.30.23 | — | — | ssh `Permission denied (publickey,password)`, no E |

`10.0.30.23` (the card's named play server, the NAS akk-stack) is not even
ssh-reachable from hermes-ext, so I cannot inspect or start its stack.
Per the card ("if down, block with the probe evidence — do not re-point at
another server without a ruling"), I am blocking rather than re-pointing.

**Remaining (all require a live server; none can be done offline):**
- (B) Pin the client multi-buffer ETC1 mechanism with a live
  `WILLEQ_B1051_TRACE` + a per-`entry.etc1Data` content-hash at upload
  (refute/confirm the data-sharing hypothesis) and implement the fix.
- (B) Live x86 verification: the guard (Guard_Treitan, race 71, qeynos2)
  rendering ETC1 == RGBA with distinct per-part textures.
- (C) XU4 after-stills (server + XU4; ARM binary from CI `ci-arm/<sha>/`;
  never compile on the box).
- (D) x86 gate goldens; (E) MR !13 pipeline green at the new head.
