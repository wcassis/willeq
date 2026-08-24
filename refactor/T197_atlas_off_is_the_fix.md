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
