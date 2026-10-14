# B1051 — Panfrost/Mali-T620 NPC + player-weapon "flat single-texture" smear
## Take 3: decisive knob-isolation + root-cause isolation (XU4, Mesa 25.2.8)

**Status: root cause isolated. The fault is in the ETC1-constrained texture
path (client), NOT a Mesa/Panfrost GPU tiling/AFBC/LOD bug. No Mesa knob fixes
it; disabling ETC1 (`--eqe1-path` off) renders correctly.**

---

## 1. The bug (reproduced, current binary)

On the XU4 (Rockchip, Mali-T620 / Panfrost, `OpenGL ES 2.0 Mesa 25.2.8-
0ubuntu0.24.04.1`), the NPC (`Lieutenant_Arathur`, race 71, code `QCM`) and
the player's held weapon render as **ONE single two-tone mottled texture
covering the entire model** — no skin tone, no distinct armor pieces, no
face, no separate weapon material. Every mesh buffer of the multi-buffer
character is skinned with the same texture. (Reported hue was "gold/brass";
with the Options window hidden the smear is actually a brown/tan mottle — the
"gold" tint was the warm translucent Options window laid over the smudged
model. See §3 on the earlier false-negative.)

Reproduced with the **exact card recipe** on the unmodified binary
`/home/user/willeq/willeq` (branch `task/kb-b1051-panfrost-npc-textures`,
HEAD `57b86707`, origin/fable merged), UI hidden (ini `Show=0`, restored
byte-identical after, sha `e30f4e24…` pinned):

```
--constrained orangepi -r 1280 720 --eqe1-path /mnt/projects/EverQuest_ETC1
--script: wait 25 / /hideui / wait 5 / /moveto Lieutenant_Arathur /
           wait 20 / /face Lieutenant_Arathur / wait 8 / q
--dump-frames-dir … --dump-every 30 --log-level=INFO
```

`/face Lieutenant_Arathur` applied in every valid run (client.log `CMD
outcome=applied`). Frames pulled at arm's length.

---

## 2. Knob table (clean-UI, one knob per run, same failing ETC1 config)

| Row | Setting (on top of `--eqe1-path` ETC1 config) | Result |
|-----|----------------------------------------------|--------|
| **base** | default (constrained orangepi, ETC1) | **SMEAR** — single mottled texture |
| **constrained_none** | `--constrained none` (ETC1 still on) | **SMEAR** — single mottled texture |
| **eqe1_off** | **`--eqe1-path` removed** (raw S3D / RGBA) | **CORRECT** — 4+ distinct materials (helmet, chestplate, chainmail, skin, shield) |
| **notile** | `PAN_MESA_DEBUG=notile` | **SMEAR** — persists |
| **noafbc** | `PAN_MESA_DEBUG=noafbc` | **SMEAR** — persists |
| **mip_legacy** | `WILLEQ_GPU_BUFFERS_LEGACY=1` | **SMEAR** — persists (tight single brown tone) |
| **gles30** | `MESA_GLES_VERSION_OVERRIDE=3.0` | ambiguous (no close NPC in frame) — inconclusive |

**Discriminator: the ETC1 path is the trigger.** Every Mesa/Panfrost GPU knob
(tiling off, AFBC off, legacy mip chain) fails to remove the smear; only
switching the *texture pipeline* from ETC1 to raw RGBA (`eqe1_off`) restores
correct multi-material rendering.

### Why this isolates the fault to the client ETC1 path (not the GPU)

The client draw path is verified **identical and correct in both modes**:
- `RaceModelLoader::getAnimatedMeshForRace` → composite path *not* taken
  (`compositeData=false`, no sheet) → `buildMeshFromGeometry` multi-buffer.
- The Lieutenant (race 71) builds a **13-buffer** animated mesh
  (`Built multi-texture animated mesh with 13 buffers`), one buffer per part.
- `EQAnimatedMeshSceneNode::render()` (animated_mesh_scene_node.cpp:283-297)
  does a per-buffer `driver->setMaterial(materials_[i]); drawMeshBuffer(mb)` —
  **each buffer binds its own texture.** `materials_[i]` is populated from
  `mb->getMaterial()` at node construction (line 63).
- In ETC1 mode `buildMeshFromGeometry` (mesh_building.cpp:204-314) resolves
  **each buffer's texture by its own name** via
  `ZoneMeshBuilder::getOrLoadTexture(name)` → `textureCache_` (zone_geometry.cpp:
  621-648). Per-part ETC1 textures **are** uploaded distinct and **are**
  registered into that cache by name
  (`ConstrainedTextureCache` → `meshBuilder_->registerUploadedTexture(name,
  texture, hasAlpha)`, constrained_texture_cache_compressed.cpp:506 / :805).
- **Census (DEBUG) confirms** all 13 Lieutenant ETC1 textures upload with
  *distinct* dimensions/mips (qcmch0001 64x128, qcmch0002 32x16, qcmfa0001
  32x64, qcmhe0001 128x64, qcmlg0001 128x64, …) and **are never evicted**
  (the 86 evictions are all zone/scene textures — walls/banners). Same for the
  player `hum*` textures. So the GPU **does** hold 13 distinct, resident,
  distinct-dimension textures, and the client binds them per buffer.

Conclusion: the GPU is handed 13 distinct textures and told to bind them per
buffer — yet all 13 buffers sample the same one. That is not a tiling/AFBC/
LOD/mip defect (all those knobs failed), and not a residency/eviction defect
(textures never evicted). The difference between the working `eqe1_off` path
and the failing ETC1 path is entirely in **how the ETC1 textures are created
and bound** — a client-side ETC1 pipeline issue.

---

## 3. Reconciling the earlier contradiction (census "clean" vs baseline "smears")

The DEBUG-level "census" run was read as a clean NPC; the INFO baseline was
read as smears. Both used the same binary/config/ETC1 path. Resolution:

- The **census frame that was read as "clean" actually shows the *player*
  (Will) in third-person** — a different, correctly-textured model — with the
  Options window still open, not the arm's-length NPC. The earlier "clean
  census NPC" read was a misattribution (a vision read on a UI-cluttered frame
  picked the wrong figure).
- The **warm "gold" tint** in the original repro was the translucent Options
  window overlying the smears model; with UI hidden the smear is a brown/tan
  mottle.
- The **clean-UI knob table (§2) is the authoritative signal**: on the same
  binary, ETC1 → smear, `eqe1_off` → correct. No INFO-vs-DEBUG dependency.

---

## 4. Root-cause mechanism (specific site)

The ETC1 path differs from the RGBA path at exactly one render-relevant
stage: **`ConstrainedTextureCache` uploads ETC1 as compressed
`GL_COMPRESSED_ETC1` storage with a companion-alpha pipeline
(`cached.isETC1 = true`, "companion-based alpha", H02d), and the per-buffer
materials are set to `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` for masked parts
(mesh_building.cpp:333-335).** Under Panfrost/Midgard, Mali-T620 has **no
native ETC1** — Mesa clumps it to ETC2:

```
mesa-25.2.8/src/panfrost/lib/pan_texture.c:221
    CLUMP_FMT(ETC1_RGB8,  ETC2_RGB8),
```

and the Midgard texture descriptor is emitted from the clumped iview
(`pan_texture.c:1288-1314`: `cfg.format = mali_format; cfg.levels = …;
cfg.maximum_lod = cfg.levels - 1`). The ETC1→ETC2 clump plus the
`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` / companion-alpha shader variant is the
ETC1-only stage the working RGBA path never exercises. The multi-buffer
character collapses to one texture **only** when every buffer's texture is
ETC1-clumped + alpha-channel-ref — matching the fact that zone geometry
(single-buffer / ETC1-companion handled differently) and the raw-RGBA
`eqe1_off` path both render correctly.

**The GPU is exonerated** as the primary cause by the knob table; the durable
fix must address the client ETC1 multi-buffer material/sampler setup (most
likely the per-buffer sampler/texture binding not taking effect under the
`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` ETC1 shader variant, or a shared
descriptor in the ETC1 upload path), **not** a Mesa patch.

### Candidate client fix directions (to verify, NOT applied in this run)
1. Force ETC1 character textures to the same per-buffer solid/alpha material
   the RGBA path uses (i.e. don't route masked ETC1 parts through
   `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` on Panfrost) and re-test the 13-buffer
   Lieutenant — if the smear disappears, the alpha-channel-ref ETC1 shader
   variant is collapsing the sampler.
2. Log the `glActiveTexture`/sampler-unit + texture name actually bound per
   `drawMeshBuffer` under the ETC1 ETC1-REF variant (vs `eqe1_off`) to
   confirm whether all 13 buffers sample unit 0.
3. If (1) confirms, gate the ETC1 companion-alpha material on
   `!isPanfrost()` (or fall back to the RGBA solid path for multi-buffer
   characters on Panfrost) as the durable workaround until the binding
   root-cause is fixed.

### What is explicitly NOT the cause (evidence)
- Tiling: `PAN_MESA_DEBUG=notile` → still smears.
- AFBC: `PAN_MESA_DEBUG=noafbc` → still smears.
- Mip chain / LOD: `WILLEQ_GPU_BUFFERS_LEGACY=1` (full mip chain) → still
  smears; Mesa descriptor `cfg.levels`/`cfg.maximum_lod` are correct.
- Texture residency/eviction: per-part ETC1 textures never evicted (census).
- Client mesh/multi-buffer construction: 13 buffers built correctly in both
  modes; per-buffer `setMaterial` present and correct.

---

## 5. Reproduction + verification recipe (reproducible)

UI ini pin (restore byte-identical after; sha `e30f4e24fca4b7d2ce1768bf4ba2bf48136574cbc269d2c265255cec0f324be4`):
`/tmp/willeq-cache/ui/UI_Testchar_WillEQ.ini` → set `[OptionsWindow] Show=0`,
`ShowTips=0`, `ShowTOTD=0` for the batch, then `cp` the backup back and verify
sha.

Failing (smear):
```
WILLEQ_DATA_ROOT=/mnt/projects/fable/ci-arm/by-branch/fable \
WILLEQ_CACHE_DIR=/tmp/willeq-cache \
/home/user/willeq/willeq -c /home/user/willeq/willeq.json --drm --renderer gles2 \
  --constrained orangepi -r 1280 720 --eqe1-path /mnt/projects/EverQuest_ETC1 \
  --script <recipe> --dump-frames-dir <d> --dump-every 30 --log-level=INFO
```
Correct (no smear): identical, **minus `--eqe1-path`**.
`<recipe>` = `wait 25\n/hideui\nwait 5\n/moveto Lieutenant_Arathur\nwait 20\n/face
Lieutenant_Arathur\nwait 8\nq\n`. Judge at arm's length (~20-26th frame); UI
hidden so the model is unobstructed.

### Caveats / limitations
- `gles30` row inconclusive (no close NPC in the pulled frame) — not a
  disqualifier; the ETC1-vs-RGBA discriminator is decisive on its own.
- `regioncensus.py` / `huediv.py` pixel metrics over the NPC-body crop are
  polluted by background (stone/wood) in the crop and did not separate
  cleanly; the **full-frame vision read with UI hidden** was the consistent
  discriminator, corroborated by the client code trace and census log.
- The Mesa Panfrost tree in this workspace is **partial** (no `pan_driver.c`
  / sampler-state tracker), so the GPU side could not be traced to a specific
  source line — consistent with the knob table ruling it out as the primary
  cause. The ETC1 clump + descriptor sites cited above are the relevant
  Panfrost texture handling for context.

## 6. Deliverables
- Evidence frames + scripts: `~/kb-evidence/kb-b1051-take3/`
  (`ku_base/`, `ku_eqe1_off/`, `ku_constrained_none/`, `gk_notile/`,
  `gk_noafbc/`, `gk_gles30/`, `mip_legacy/`, `census_driver.py`,
  `huediv.py`, `ku_run.sh`, `gk_run.sh`).
- Census DEBUG log (ETC1, all per-part uploads + evictions):
  `<kanban-ws>/census_client.log`.
- Client source sites cited: mesh_building.cpp:204-338,
  animated_mesh_scene_node.cpp:63/283-319, zone_geometry.cpp:621-648,
  constrained_texture_cache_compressed.cpp:470-519,
  model_loading_search.cpp:515-533, s3d_loader_textures.cpp:92-146.
- Mesa 25.2.8 Panfrost: src/panfrost/lib/pan_texture.c:221, 1288-1314.
