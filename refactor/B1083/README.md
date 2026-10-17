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

The entry accessors (`getTexture` / `hasAlpha` / `getAlphaTexture` /
`isETC1Texture`) route through one `findEntryForKey()`: a HIT on the exact
`(archive, name, mask)` key is the answer; a SCOPED MISS stays a miss (that
is the fix — qeytoqrg tree70 must not answer the zone's request with the
_obj copy or vice versa); an UNSCOPED MISS falls back to the entry stored
for that `(name, mask)` under any archive. The fallback is what keeps the
pre-B1083 plain-name consumers working: T285's on-demand fallback
(`tests/test_palette_mask_plumbing.cpp`) queries an S3D-stemmed entry by
plain name and mask, and the unmasked S3D names it reads are exactly the
ones the key change would have hidden.

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

## Step 6 — dedupe by content (ruling 2026-10-16, Will; #403)

The scoping above splits EVERY name a sibling pack also holds into two
entries — byte-identical or not — and those extra bytes bust the texture
budget on zones the name-keyed run stayed under (the x86 gate: qeynos2 at
99.6% of the 20 MiB ceiling, evicted textures drawing black). Ruling:
**"dedupe by content (hash decoded payload; same bytes under two archives →
one entry)"** — `docs/decisions.md` 2026-10-16 records it.

The content identity is an FNV-1a 64 over what a GPU entry actually holds:
`decodedContentDigest` (pixels + dimensions + alpha) on the decode path,
`compressedContentDigest` (payload + alpha companion + shape fields) on the
EQE1 path. The SINGLE decision point is the promote-time gate
(`dedupeBeforePromote` in `uploadTexture()`): a scoped request about to
create an entry folds onto a content-identical (name, mask) family entry
(first promoted in time wins — the pre-B1083 first-upload-wins answer), its
request key becomes an alias (`deduped_`, `getDedupedEntryCount()`), and
every accessor answers the alias like its target via `findEntryForKey`'s
remap. Genuinely different same-name textures (the qeytoqrg tree70
DXT1-vs-DXT5 willow pair) digest differently and keep two entries — the
willow fix itself is untouched. Unscoped requests never alias.

Pinned by `ConstrainedTextureCacheTest.SameBytesUnderTwoArchivesIsOneEntry`
(one entry, one GPU texture, alias stable across re-arrival — RED on the
pre-dedupe branch), `DifferentBytesUnderTwoArchivesKeepTwoEntries` (the
willow pair must NOT fold), and `AliasRearrivalKeepsTheTwinEntry`; the
scoping tests in `test_eqe1_source_archive.cpp` (incl.
`ObjectKeepsItsOwnCopyWhenNamesCollide`, real EDT_OGLES2) keep the two
genuinely-different copies apart. All five pass, plus the full
ConstrainedTextureCacheTest, the eviction/residency suites, and the
hygiene net (WriteOnlyFields clean).

### Step 6b — dedupe measured on the gate (2026-10-16): fires 0 times, regression survives

The ruling predicted the five non-qeytoqrg gate zones would return to 0 px
once identical-byte duplicates collapsed. **The measurement contradicts that
for the gate path**: the dedupe fires **0 times** in all five x86 gate zones,
and qeynos2's cache is **byte-identical to the pre-dedupe branch** — so the
197 px gate diff (evicted textures drawing black at 99.6% of the 20 MiB
ceiling) is **untouched by the dedupe**.

Measured by running the exact x86 gate invocation (60 frames, 3 camera
poses, `--eq-path` only — the gate passes no `--eqe1-path`) through the
`gles2_zone_benchmark` with a new `deduped` counter in the summary line
(`getDedupedEntryCount()`, `tools/hwcaps/zone_bench_modes.cpp` +
`gles2_zone_benchmark.cpp`), ThinkPad / llvmpipe, 2026-10-16:

| zone | entries | used / limit | deduped |
|---|---|---|---|
| qeynos2 | 126 | 21,932,691 / 22,020,096 (99.6%) | **0** |
| gfaydark | 32 | 11,184,800 / 22,020,096 | **0** |
| befallen | 9 | 2,883,581 / 22,020,096 | **0** |
| iceclad | 15 | 3,866,619 / 22,020,096 | **0** |
| timorous | 35 | 3,183,945 / 22,020,096 | **0** |

qeynos2 `entries=126 used=21,932,691` is the **same byte count as the
pre-B1083 / pre-dedupe run** (GATE_FINDINGS `used=21932691`); the gate
diff is still 197 px (62/45/90), byte-identical to pre-dedupe.

**Why 0 deduped.** The gate benchmark loads **one** `.s3d` archive per zone —
qeynos2's own — and **no character models, no objects, no EQE1**
(`characterModelsBytes=0`, `placeableObjectsBytes=0`, `eqe1ArchiveCount=0`
in the run's memory JSON; log: `S3D loaded: 2074 regions, 148 textures`).
The only cross-archive duplicate family qeynos2 holds is **zone ∩ chr**
(~140 names — the `bat*`/`beach*`/`bet*`/`bgm*` character-model textures,
per `s3d_dump` over the three archives: zone∩obj = ∅, zone∩chr = 140,
obj∩chr = ∅). Because the gate never loads `_chr.s3d`, **none of those
shared names are in the working set** — there is no content-identical
cross-archive duplicate for the dedupe gate to fold. The dedupe is correct
and unit-proven; it simply has no in-working-set target in a
single-archive gate run.

**Consequence for the ruling:** the dedupe restores the **_obj / _chr**
working set (the live-client path, where the willow and other object /
character textures DO cross archives) — it does **not** restore the five
gate zones, because the gate's working set is a single archive. The
regression on those five zones is therefore a **separate** consequence of
the (archive,name) keying that the ruling's dedupe does not address, and
fixing it is a further design decision (re-bless vs. change the keying) —
left for Will (step 7; the gate goldens are bless-only). See the
`FABLE RULING` comment and the card's block note (2026-10-16).

## Step 5 — docs

`docs/decisions.md` records the ruling. `refactor/B1083/README.md` is this
file. The EQE1 lookup paragraph in `src/client/graphics/CLAUDE.md` is edited
via the `CLAUDE.md EDITS:` section below (the file is protected).

## CLAUDE.md EDITS:

In `src/client/graphics/CLAUDE.md`, the bullet

```
- T207 (`ed14c4de`): EQE1 lookups resolve through a name→archive index
  (`eqe1_texture_index_builder`, `openArchiveContaining()`) instead of
  linear-probing every archive; 37 opens/24 unused/149MB → 13/0/46MB on the
  Pi (T208 `604ef722`), zone-in time unchanged (load time is not a work item —
  see decisions).
```

is superseded on one sentence. **Replace** the phrase

```
EQE1 lookups resolve through a name→archive index
(`eqe1_texture_index_builder`, `openArchiveContaining()`) instead of
linear-probing every archive
```

**with**

```
EQE1 lookups resolve through a name→archive index
(`eqe1_texture_index_builder`, `openArchiveContaining()`) instead of
linear-probing every archive; since B1083 (`docs/decisions.md` 2026-10-16)
the cache key is `(sourceArchiveStem, name, applyPaletteMask)` and
`findCompressedEntry()` tries the requesting archive's own pack first before
falling back to the configured-order scan + index late-open (T207/T266) — a
name two zone-family packs hold with different content (361 names in
`data/eqe1_client`; the byte-complete census is
`scripts/refactor/eqe1_family_census.py`) now resolves to the REQUESTING
pack's copy rather than the first pack in configured order
```

Everything else in the paragraph (the ALL-OR-NOTHING rule, T205, T214, T266,
T284, the T195/T197-dead rationale) stays. The T195/T197-dead paragraph
"so a name two archives share cannot restore the wrong pixels" is about
S3D-path (`TextureOrigin`) decode entries and is unaffected by B1083, which
keys EQE1 (compressed) entries.

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
- **Pre-existing local-only failures, NOT from this branch** (2026-10-16,
  hermes-ext): the five `FirstPerson*` gtest binaries segfault at
  `__dynamic_cast` (backtrace: libstdc++ `__dynamic_cast` from
  `FirstPersonBodyDrawRuleTest_...::TestBody`'s `dynamic_cast<
  EQAnimatedMeshSceneNode*>`) on THIS box even at the pristine fable tip
  `4e987f0c` and at `6cc847d2` (pre-accessor-fix) — both reproduced from
  fresh evidence worktrees under `/home/user/kb-evidence/` — while CI's
  `build-test` ran them green at `6cc847d2` (pipeline 1381, test #3624-#3628
  Passed). `PackReproducible` likewise fails locally only because the host's
  INSTALLED `/mnt/projects/EverQuest_ETC1/willeq.eqe1` differs from a rebuild
  of the committed inputs. Neither is in scope here; CI is the arbiter.
- The census counts EQE1 *payload* bytes; the S3D-side `.s3d` members are
  not re-digested (the packs mirror the S3D faithfully per the issue's
  `detectAlpha()` measurement), so an S3D member that differs while its
  pack copy does not would be missed. Low risk: the fix keys on the pack
  stem, and the packs are what the cache resolves against.

## Step 7 — the 197px gate regression (finding, ruling request; then the fix)

The keying commit `08ce2282` broke the x86 visual gate: qeynos2 renders
197 px black vs the golden (self-consistent across runs, commit-isolated),
and the other four gate zones diff too (135/274/678/192 px). Card
`t_85bfde31` asked the dev leg to find the texture the (archive,name) keying
"loses" and fix it. The full measured finding is in
`refactor/B1083/gate_ledger_diff.md`; the two instrumented ledgers (base
`37fb0df6`, head `7fa8dff3`) are committed beside it.

**What Step 1 measured (the card's escape clause, verbatim trigger):** the
base and head ledgers are **byte-identical** — 126 entries / 81 evictions /
20 restores / 21,932,691 B, 3,662/3,662 getOrLoad requests, 0 nulls, 0
budget failures. **No texture is lost, split or re-keyed.** The card's
STOP clause was satisfied exactly; the residual was located at file:line
(the driver-facing GL texture NAME the S3D path handed `addTexture()`:
mangled `internalKey` `name\x01@<stem>\x01` where base handed the plain
name → `textureGroupId()` → `materialKey` → per-pose draw-order reshuffle,
not a lost texture) and the fix was routed to Will as a ruling request
(#403 note 5878, options A/B/C; candidate A specified in
`gate_ledger_diff.md` "Candidate fix").

**Ruling (Will, 2026-10-17, #403 note 5964): Option A** — family-first
driver name, plus "decide the driver name ONCE per entry and store it; an
evict->restore re-creates under the SAME name (unit test covers it)."
B and C not approved.

### Step 7a — the fix (Option A, committed)

- `8db85f7b` — `decideDriverName()` in
  `ConstrainedTextureCache::getOrLoad()`: decided ONCE at store time
  (under the cache mutex), stored on `DecodedEntry::driverName` /
  `CompressedEntry::driverName`, carried to
  `CachedTexture.driverName` at promote (`uploadTexture`,
  `uploadCompressedETC1`, `uploadCompressedV2`; the alpha companion is
  `driverName + "_alpha"`). First stored member of a (name, mask) family
  → the pre-B1083 P46d name (`name` or `name\x01masked`); a later
  cross-archive twin → the full stem-bearing `internalKey`, so the
  driver's `findTexture(name)` dedup can never fold two distinct images.
  The cache's own keying and the content dedupe are untouched.
- `63a86b1f` — the evict/restore test's filler gets a restore source
  (a B947 pin with none was blocking the evicted entry's restore).

Unit tests (`tests/test_texture_driver_name.cpp`, 5, registered in
`tests/cmake/renderer_guards.cmake`): family-first (unmasked + masked),
cross-archive twin keeps both images under distinct names (the willow),
evict→restore re-creates under the SAME name on a real `EDT_OGLES2`
surfaceless device, and the family-membership check. Red-before proven at
pre-fix head `7fa8dff3`: the same file, without the
`B1083_STEP7_DRIVER_NAME_STORED` define (private-field asserts compile
out; the public-API driver name alone is the red), fails **4/5**.

### Step 7b — the result (all acceptance met)

- **All five gate zones 0 px** vs the committed goldens, fix head
  `63a86b1f`: MR !205 **pipeline 1608** — hygiene, build-headless,
  build-test (6106 tests) and **`gate` PASS on attempt 1/3**
  (`=== VISUAL GATE: PASS ===`), zone-budget-census green.
- **Local re-run** (exact x86 gate invocation, `tests/data/b1024/
  x86_gate_plan.txt`, hermes-ext llvmpipe — the gate's own env): all
  15 frames 0 px; qeynos2 ledger identical to base and golden
  (126 entries / 81 evictions / 20 restores / 21,932,691 B).
  `step5_fix_5zone.sh` + `step5_fix_run.log` in the evidence dir.
- **Willow (acceptance E) unchanged:** qeytoqrg `--objects`, same
  invocation as the Step-3 willow still: 103 textures, 0 evictions,
  4,588,992 B, **0 deduped**, frame byte-identical to the Step-3
  "after" capture (`step1_diff2.py` → 0 px vs `willow_after_f0.png`).
  Both cross-archive twins live under distinct driver names — the fold
  the willow fix exists to prevent never fires.
- **F (no blessed files touched):** the branch's diff is 6 files —
  `constrained_texture_cache.h/.cpp`,
  `constrained_texture_cache_compressed.cpp`,
  `tests/test_texture_driver_name.cpp` (new),
  `tests/cmake/renderer_guards.cmake`. No budget, golden, manifest,
  ratchet or `config/constrained_presets.json` line changed.
- `docs/decisions.md` gained the 2026-10-17 entry (the store-time,
  family-first driver-name rule + the evict→restore same-name
  requirement).

**Incidental, not a branch defect:** pipeline 1604 (the pre-retrigger
run) failed `build-test` on `PackReproducible` — the installed shared
NFS pack `/mnt/projects/EverQuest_ETC1/willeq.eqe1` had been rewritten
by another actor at 16:37Z and no longer matched the committed pin
(`02a3d6e4…` vs `6549a368…`); the same test was red on `fable`
(pipeline 1593) independently of this branch. Re-installing the pinned
pack (the script's own recipe, `scripts/build_willeq_pack.sh`, which
reproduced the pin byte-for-byte: 9,726,596 B) restored it; pipeline
1608 then ran clean.
