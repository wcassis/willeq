# B1083 (#403): texture resolution keyed on the source archive

Object foliage in qeytoqrg drew opaque: `qeytoqrg.s3d` and `qeytoqrg_obj.s3d`
both ship `tree70`/`treea` under one name with DIFFERENT content (the zone
copy opaque, the object copy with its `_alpha` companion), and the name-keyed
texture cache collapsed the two into one entry. A name lookup opened the open
packs in configured order (`{zone}` first) and answered the OBJECT's "tree70"
request with the zone pack's opaque copy.

## Fix

The cache key gains a source-archive axis: `(sourceArchiveStem, name,
applyPaletteMask)`. An empty stem is the identity, so every unscoped call
site is byte-identical to before. `findCompressedEntry(name, stem)` tries the
request's OWN pack first, then falls through to the unchanged configured-order
scan + index late-open (T207/T266 rules untouched). The stem is threaded from
`TextureInfo::archiveStem()` (the S3D stem the loader stamped) through
`ZoneMeshBuilder::buildTexturedMesh` / `loadTextureFromBMP` / `getOrLoadTexture`
and `mesh_building.cpp`, so the zone region's material and the object's
material each ask for, store, and query their own `(archive, name)` entry —
including `hasAlpha` / `getAlphaTexture` / `isETC1Texture`, which are
properties of the entry, not the name.

Files: `constrained_texture_cache{,.compressed,.residency}.cpp` + header,
`s3d_loader.h` (`TextureInfo::archiveStem()`), `zone_geometry.{h,cpp}`,
`animated_mesh_creation.cpp`, `mesh_building.cpp`, `renderer_guards.cmake`
(test registration), `tests/test_eqe1_source_archive.cpp` (new).

## Step 1 — census (acceptance A)

`scripts/refactor/eqe1_family_census.py --eqe1-path data/eqe1_client` lists
every texture name held by >= 2 archives of the same zone family
(`{zone}`, `{zone}_obj`, `{zone}_chr`) with a DIFFERENT payload — the
SHA-256 of the payload bytes (size / hasAlpha / format are shown as hints;
two packs with identical size, hasAlpha and format can still hold different
pixels, and only the digest catches that half).

Result over `data/eqe1_client` (1506 packs, 485 zone families):
**361 names across 172 families**. (The first pass compared only
size/hasAlpha/format and found 59/21 — it missed the invisible half; the
digest made the census byte-complete.) The full table is attached as
`census.md` and regenerable with the command above.

qeytoqrg (the reported defect zone) has seven: `tree70`, `treea` (the
willow pair: zone 43704B a=0 vs obj 43704B a=1) and `cobble`, `floor`,
`fplace`, `roof`, `side1` (same size/alpha, different bytes). The willow
itself is the headliner: `tree70` zone `23b057091a7ac0b7` vs
`qeytoqrg_obj` `bbf0d54c5dcc72c4`.

## Step 3 — tests (acceptance B)

`tests/test_eqe1_source_archive.cpp`: two synthetic v1 packs mirroring the
measured tree70 collision under a real `EDT_OGLES2` surfaceless device —
the object's request is served by its own pack and keeps its companion, the
zone's by its own and stays opaque, the two are distinct GPU textures, an
unscoped request keeps today's configured-order answer, a source miss falls
through to the scan, and a full-path stem normalizes like the loader's.
4/4 PASS on this branch (llvmpipe, 2026-10-16). RED on origin/fable: the
source-archive parameters and the source-first resolution do not exist there
(the test does not compile, and `eqe1ServedByStem()` is absent).

T207 (`test_eqe1_texture_index`, 9/9), T266 (same file), T284
(`test_eqe1_composite_open`, 2/2) and `test_eqe1_loader` (19/19) all PASS
unchanged — acceptance D.

### Step 3 — the willow still (acceptance C)

Captured HEADLESS with the CLIENT's zone-object path, before and after, on
this lane (hermes-ext, llvmpipe). The CI `gles2_zone_benchmark`'s
`zones[]` golden runs load NO zone objects (that is why the gate never saw
it), but its `--objects` mode runs the production
`S3DLoader` + `ZoneMeshBuilder::buildTexturedMesh` +
`ConstrainedTextureCache` path with the real packs, so it reproduces the
defect without a server:

```
gles2_zone_benchmark --zone qeytoqrg \
  --eq-path /mnt/projects/EverQuestP1999 \
  --eqe1-path /mnt/projects/EverQuest_ETC1 \
  --objects --frames 5 --warmup 2 \
  --camera-pose 81.6,-220,4,0,-5 \
  --dump-frames 0 --dump-png-prefix <out>
```

- BEFORE: `origin/fable` tip `4e987f0c` (baseline worktree, same packs).
- AFTER: this branch, `112fe3bc`.
- Both: 383 objects placed, 803 buffers, identical frame count.

Pixel diff (`willow_diff.py`): **2,509 pixels (0.5%) differ**, isolated to
the two trees textured with the colliding names — the willow at the far
right of frame and a second tree at the left edge — while the pine (which
lives only in `_obj`, no collision) and every trunk are bit-identical.
`willow_diff_highlight.png` marks exactly those canopies red.

Inside the willow's canopy region (x 690..799, y 150..240):
background-matching ("see-through") pixels went from **30.4% → 52.5%**
(+2,175 holes) — the foliage is now alpha-tested instead of occluding as a
solid block (`willow_alpha.py`). The proposed golden is `willow_after_f0.png`
— **BLESS**, not committed (it is a benchmark-mode frame, not a `zones[]`
manifest golden; see the "Not verified / left open" note below on the
manifest row).

Stills + logs + scripts: `/home/user/kb-evidence/kb-b1083-texture-archive-key/`
(`willow_before_f0.png`, `willow_after_f0.png`, `willow_diff_highlight.png`,
`willow_bbox_{before,after}.png`, `willow_before.log`, `willow_after.log`,
`census.md`, `willow_diff.py`, `willow_alpha.py`).

## Step 4 — memory (acceptance E)

The ledger does not grow: the same texture from the same archive is still one
entry; the only new entries are the genuinely-different duplicates. Measured
on the qeytoqrg `--objects` run (orangepi preset, 22,020,096-byte ceiling):

| | cache bytes | entries |
|---|---|---|
| before (fable `4e987f0c`) | 4,501,584 | 103 |
| after (this branch) | 4,588,992 | 103 |
| delta | **+87,408** | 0 |

87,408 = exactly **2 x 43,704 B**, the `_obj` pack's `tree70` + `treea`
ETC1 payloads — the two duplicates that must now coexist. No other entry
moved (entry count 103 in both). `willow_before.log` / `willow_after.log`,
`[texture-cache]` + "Texture cache:" lines.

## Step 5 — docs

`docs/decisions.md` records the ruling. `refactor/B1083/README.md` is this
file. The EQE1 lookup paragraph in `src/client/graphics/CLAUDE.md` is edited
via the completion summary's `CLAUDE.md EDITS:` section (the file is
protected).

## Not verified / left open

- **The visual_gate `zones[]` manifest row for qeytoqrg is NOT added.** The
  gate's golden-image runs deliberately load no zone objects
  (`tests/goldens/manifest.json` `zones[]` note, T80/T303: "they never load
  objects, entities, or an EQE1 archive"), so a manifest qtoqrg row would
  show the empty ground, not the willow — it would bless nothing about this
  fix. The proof is the `--objects` before/above, which exercises the
  identical production path. Adding a gate row that loads objects would need
  a new manifest section (objects are the T80 `memoryCoverage` block's
  domain, blessed by Will) — that is a WILL decision, left open.
- The before/after were captured on llvmpipe (hermes-ext). The defect is
  platform-independent per the issue (same packs, same code on x86/ARM;
  nothing Panfrost-specific), and the Pi golden set is bless-only — no Pi
  re-bless was requested or done.
- The census counts EQE1 *payload* bytes; the S3D-side `.s3d` members are
  not re-digested (the packs mirror the S3D faithfully per the issue's
  `detectAlpha()` measurement), so an S3D member that differs while its
  pack copy does not would be missed. Low risk: the fix keys on the pack
  stem, and the packs are what the cache resolves against.
