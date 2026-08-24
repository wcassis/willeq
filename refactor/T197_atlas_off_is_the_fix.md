# T197 — Turning the atlas off removes the Pi's failure entirely

Will's decisions, recorded as policy:

- **EQE1 is all-or-nothing. No S3D fallback.** If EQE1 is enabled the assets
  must exist, and a miss must fail loudly. The `exit(1)` in
  `constrained_texture_cache.cpp:221` is therefore **correct behaviour, not a
  defect** — which closes the "prerequisite policy decision" T196 raised.
- **EQE1 is doing two independent jobs**, and only one is wanted for certain:
  flat-pack (file-I/O reduction) plus ETC1 compression are the wins; the
  **composite texture atlas is not sold**, because most textures are UV-wrapped
  and cannot benefit from an atlas.

## They are separate knobs, and only the Pi turns the atlas on

`enableCompositeEntities` defaults **false**. The **`orangepi` preset is the
only one that sets it true**, with `compositeEntityPath` pointing at the same
directory as the flat packs. So the atlas is enabled on exactly the machine that
cannot afford it, and nowhere else.

## Disabling the atlas removes the address-space failure

Pi, `--eqe1-path` on, `enableCompositeEntities=false`:

| | atlas ON (T193/T195) | **atlas OFF** |
|---|---|---|
| peak VmSize | **3,128 MB** → `std::bad_alloc` | **754 MB** |
| peak RSS | 197 MB | 207 MB |
| composite archives mapped | 664 | **0** |

**The 32-bit address-space exhaustion is entirely the atlas.** No lazy loading,
no TOC index, no on-demand opening is required to fix it — T193, T195 and T196
all describe a problem that disappears when the atlas is off.

**T196 is therefore superseded, not blocked.** Its TOC index solved a problem
we no longer need solved, and its unresolved `draw2DImage` segfault stops
mattering. Branch kept for the record; do not spend more on it.

The measured case for dropping the atlas was already in T191: **2 composite
meshes built against 119 eligible entities.** Even working, almost nothing used
it — consistent with UV-wrapped textures not being atlas candidates.

## What it exposed: the EQE1 asset set is INCOMPLETE

With the atlas off the Pi now fails loudly, exactly as policy wants — and what
it reports is an asset gap the atlas had been masking:

```
[ERROR] EQE1Loader: cannot open '/mnt/projects/EverQuest_ETC1/gequip7.eqe1'
[FATAL] Texture 'fishe0301.bmp' not found in any EQE1 flat pack.
```

- `gequip7.eqe1` **does not exist**, though `gequip2/3/4/...` do (12 present).
- `fishe0301.bmp` is **not packed in any of the 1506 archives**.
- None of the 1506 archives is zero-length, so this is missing coverage rather
  than corruption.

The composite archives were serving these per-part textures, so the flat-pack
set's gaps never surfaced while the atlas was on.

## Next

**Regenerate the EQE1 assets with `eqe1_builder`** so the flat-pack set is
complete — the FATAL says as much itself. That is an asset-build task, not a
client-code one. Once the set is complete, the Pi should load with flat-pack +
ETC1 and no atlas, which is the configuration Will actually wants, and the first
valid Pi EQE1 zone-in number becomes measurable.

**Also worth deciding once the above is done:** whether `enableCompositeEntities`
should stay in the `orangepi` preset at all.

---

## T198 — atlas removed from the preset; assets regenerated; ONE GAP REMAINS

**Done:** `enableCompositeEntities` and `compositeEntityPath` removed from the
`orangepi` preset (`28a59bf4`). The struct default is already false, so this
restores the default rather than adding a state.

**Regenerated** into `/mnt/projects/EverQuest_ETC1_regen` (staging; live tree
untouched). `eqe1_builder` is single-threaded and archive-independent, so it was
driven 20-way with `xargs -P 20` — ~25 minutes for 1006 archives against an
estimated 12h sequential. **999 of 1006 produced output**; four did not
(`vexthal`, `westwastes_chr`, `yxtta_obj`, `yxtta`).

**What an S3D-only regen cannot produce.** The live tree's 1506 archives are
1006 S3D-derived plus 500 others: **489 `*_hs_def_*` and 9 `*_actordef_*`
composite sheets** (obsolete with the atlas off) and exactly **two still
needed — `willeq.eqe1` and `textures.eqe1`**, built from BMP directories via
the builder's `--bmp-dir` mode. Copied across from the live tree.

**This explains T196's segfault.** `willeq.eqe1` holds the **UI atlas**, so UI
textures are EQE1-backed — and `draw2DImage` faulting inside
`UIRenderer::endFrame` was the UI atlas failing to resolve under T196's lazy
opener. Closed; the fix stays unwanted, since T197 supersedes it.

**THE REMAINING GAP.** With the atlas off and the regenerated flat-packs in
place, the client still fails loudly:

```
[FATAL] Texture 'fishe0301.bmp' not found in any EQE1 flat pack.
```

`fishe0301.bmp` is a per-part character texture that **only the composite sheets
were providing**. Removing the atlas removes its only source.

**The question that decides the approach:** does `fishe0301.bmp` exist in the
source client's `.s3d` set at all?

- **If yes**, `eqe1_builder` is skipping it — a builder bug, and the fix is in
  the builder.
- **If no**, those per-part textures are *derived* by
  `entity_composite_builder`, and the atlas cannot simply be switched off: the
  flat-pack path would need whatever step produces them.

Either way this is an **asset-pipeline** question, not client code.

Until it is answered, **EQE1 is unusable with the atlas off**, and the Pi's
usable configuration remains S3D (no `--eqe1-path`) — which is exactly what
T186's ~37.7s baseline measured.

## T198 addendum — ANSWERED: the texture exists; the archive LIST is the gap

The question was whether `fishe0301.bmp` exists in the source. **It does, and
`eqe1_builder` produced it correctly.** `verify_eqe1 --verbose` finds it in the
regenerated `akanon_chr.eqe1`, `cabeast_chr.eqe1`, `cazicthule_chr.eqe1` and
other zones' `_chr` packs.

It is **not** in the archives the client actually opens for befallen:

| archive | has `fishe0301` |
|---|---|
| `befallen`, `befallen_obj`, `befallen_chr` | no |
| `global_chr` | no |
| `akanon_chr`, `cabeast_chr`, `cazicthule_chr`, … | **yes** |

`config/eqe1_archives.json` opens `{zone}`, `{zone}_obj`, `{zone}_chr` plus a
fixed global list (`global_chr`..`global7_chr`, `gequip*`, …). Other zones'
`_chr` packs are never in that set, so the texture is unreachable — while the
atlas was on, all 664 composite sheets were open and one of them carried it.

**So neither hypothesis was right.** It is not a builder bug and the texture is
not derived: it is an **archive-coverage gap in `eqe1_archives.json`**, exposed
the moment the atlas stopped hiding it.

Also confirmed benign: `gequip7` is listed in `globalArchives` but
`gequip7.s3d` does not exist in the client (gequip, 2-6, 8 do), which is why
`cannot open '.../gequip7.eqe1'` appears. Harmless, and worth removing from the
list to stop the noise.

**The real question this raises** is why befallen spawns an entity whose texture
is in no archive befallen loads. In S3D mode the same lookup misses, but S3D
mode does not FATAL — it falls back to a placeholder, which is very likely one
source of the **magenta/checkerboard textures** already on the open list. The
EQE1 all-or-nothing rule turned a silent placeholder into a loud failure, which
is exactly what that rule is for.

**Options, for a decision:** add the character `_chr` archives to
`globalArchives` (simple, costs address space on the Pi — measure it), or
establish which archives a zone's spawned races actually need and derive the
list per zone.
