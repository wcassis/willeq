# B1083 Step 1 — the gate ledger diff (base `37fb0df6` vs head `7fa8dff3`)

Measured on the exact x86 gate invocation for qeynos2 (`--zone qeynos2
--eq-path <EQ client> --frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2
--occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode
count --player-light-level 9 --camera-pose <3 poses>`), surfaceless EGL /
llvmg on a non-GPU identity (byte-identical to the CI gate host — see
`GATE_FINDINGS.md`). The ledger was dumped by `WILLEQ_TEXTURE_LEDGER=<file>`
instrumentation (env-gated; zero new output at the default log level): every
`getOrLoad` request with the (name, stem, mask) it was asked for, every null
answer with its reason, every EVICT/RESTORE frame-stamped, every per-frame
residency (from `endFrame()`), and the final entry set.

## The texture the keying "loses": NONE. The cache is byte-identical.

The card's premise — "(archive,name) and name should be one-to-one in a
single-archive run, so the keying still changes what the cache holds; that
mismatch is the defect" — is **not** what the measurement shows.

| quantity | base `37fb0df6` | head `7fa8dff3` |
|---|---|---|
| `getOrLoad` requests | 3,662 | 3,662 — **same (name, mask) multiset** |
| `getTexture` lookups | 0 | 0 |
| null answers (lost / unbound / budget) | **0** | **0** |
| final cache entries | **126** | **126** — same names, same bytes, same residency |
| evictions | **81** | **81** — same (frame, name) multiset |
| restores | **20** | **20** — same (frame, name) multiset |
| per-name eviction count | identical | identical |
| total entry bytes | 21,932,691 | 21,932,691 |
| per-frame residency at the 3 golden frames (F16/17/18) | — | **byte-identical to base** |

The "127 vs 126" from the earlier comments is a **miscount, not a lost
texture**: the blessed golden manifest itself records `texture_count: 126`
and `texture_cache_bytes_used: 21,932,691` (`tests/goldens/manifest.json`),
which is exactly what base and head both produce. The zone `.s3d` loads 148
texture references but the cache holds 126 distinct (name, mask) entries at
the golden frames in **both** base and head. There is no missing/split/
re-keyed texture, and no eviction-order change that drops a texture.

## The remaining diff location: the GL texture NAME

Base and head differ in exactly one place that reaches the GPU: the **name
each S3D texture is created under in the driver**.

- The zone `.s3d` textures are the DECODED path. `uploadTexture()` promotes
  them via `createDecodedTexture(internalKey, …)` (`constrained_texture_cache.cpp:780`),
  and `createDecodedTexture()` hands `internalKey` straight to
  `driver_->addTexture(glName)` (`:896`). `internalKey` is
  `cacheKey(name, sourceArchiveStem, applyPaletteMask)` (`:694`).
- **Base** (pre-stem) `cacheKey` is the identity for the unmasked single-
  archive case, so the GL name is the **plain** `name` (e.g. `10dirt2b.bmp`).
- **Head** adds the source-archive stem (the S3D loader threads `qeynos2`), so
  the GL name is the **mangled** `name\x01@qeynos2\x01` (e.g.
  `10dirt2b.bmp\x01@qeynos2\x01`). Ledger confirms: base keys carry no
  control byte, head keys carry 0x01.
- The **compressed EQ** path (con.g. `uploadConcompressed…ETC1/V2`) already
  hands the driver the **plain** `name` (`constrained_texture_cache_compressed.cpp:128`) —
  it was not stem-mangled. The gate loads no EQE1, so only the decoded S3D
  path is live there; and that is precisely the one that now mangles the name.

That GL name is not just a label. H02b/Q02 made the material-sorted draw
order independent of pointer addresses by hashing the **texture name**:
`textureGroupId()` = FNV-1a(`texture->getName()`) & 0xFFFF, folded into
`materialKey = (materialType << 16) | textureGroupId(mat)`, and the zone/entity
draw lists are **sorted by `materialKey`** (`renderer_zone_draw.cpp:139/424`,
`renderer_zone_merge.cpp:22/409`, `scene_graph_batch.cpp:32/187`,
`zone_bench_draw.cpp:386/425`, `zone_bench_entities.cpp:143`,
`zone_bench_merge.cpp:29`). B1083's mangled GL name changes **126/126** of
those group IDs, moving **125/126** of the 126 names' sorted rank (max shift
119) — so the merged draw order reshuffles, and the coplanar / near-coplanar
surfaces that the gate's exact-px comparison is sensitive to flip depth.

## Why the pixels confirm this and rule out a "lost texture"

Pixel diff vs the committed goldens (base = golden = 0 px):

| frame | diff px | of which pure black (0,0,0) | head **brighter** than base |
|---|---|---|---|
| f0 | 62 | 21 | yes (e.g. (549,158): base (27,33,35) → head (30,43,44)) |
| f1 | 45 | 7 | yes |
| f2 | **90** | **0** | yes (several) |

A texture that is **missing or unbound** can only render its region BLACK or
darker than base — it can never make a pixel **brighter**. f2 has **90 diff
px and zero of them black**, and multiple px are head-brighter-than-base. That
signature is a **draw-order change** (a different surface winning the depth
test at the same screen pixel), not a lost texture. f0/f1's black pixels are
the same mechanism on dark-on-dark geometry where the losing surface is
nearer-black.

## Step 2 — mechanism at file:line + prediction

**Mechanism (single line):** `constrained_texture_cache.cpp:780` creates the
decoded (S3D) texture under the mangled `internalKey`
(`name\x01@<stem>\x01[...\x01masked]`). Pre-B1083, `internalKey == name` for
the unmasked single-archive case, so base created the plain name. name. The
name flows into `textureGroupId()` (FNV-1a over the whole name, H02b/Q02),
which sets the `materialKey` draw-order key; mangling it reorders the merge
sort.

The mangled `internalKey` is **required as the *cache-map* key** (the
`cache_`/`decodedEntries_`/`compressedEntries_`/`deduped_` maps all key on
it, and that is where the (archive,name,mask) willow fix lives). But the
**driver-facing** name handed to `addTexture()` is a separate question, and
it is where the defect is. Pre-B1083 that name was `name[\x01masked]`;
B1083's stem axis made it `name\x01@stem\x01[\x01masked]`, and that suffix is
what reshuffles the draw order. (The naive fix "always hand the driver
(name,mask), no stem" does NOT work: it would make the willow's two
same-name tree70 copies share one driver name, so the driver's `findTexture`
name-dedup would discard the _obj copy on the second `addTexture` — regressing
the willow. The correct candidate, stated before implementing, is the
family-first rule in the "Candidate fix" section below: the FIRST entry of a
(name,mask) family takes the pre-B1083 name, a later cross-archive twin takes
the full `internalKey`.)

**Prediction (stated before the fix), family-first candidate:** the first
entry of a (name, mask) family is created under the pre-B1083 name
(`name` or `name\x01masked`); a later, genuinely-different cross-archive twin
(the willow) is created under the full `internalKey`. Then, for any zone whose
family never spans two archives (all five gate zones: single .s3d, no
_chr/_obj, no EQE1), every GL texture is created under exactly base's name →
126/126 group IDs and the merge sort are **byte-identical to base**, so:
- **qeynos2: 0 px** vs the golden (draw order restored), still 126 entries /
  81 evictions / 20 restores / 21,932,691 B (cache untouched — the fix is in
  the driver name, not the cache);
- the **other four gate zones** → **0 px**;
- **qeytoqrg `--objects` willow**: unchanged — the zone's tree70 (family
  first) keeps base's plain name, the _obj tree70 (second, different bytes)
  gets the stem-suffixed name, so `findTexture` cannot fold them and the
  alpha-tested foliage survives; the dedupe still fires 0 there.

If this prediction does not hold (i.e. the 5 zones are not 0 px, or the willow
regresses), the residual is reported, not blessed.

## Step 5 (partial, at the CURRENT head `7fa8dff3` — no fix implemented yet) — all five gate zones

The exact gate invocation (manifest `productionConfig.args` + each zone's
`extraArgs` poses, `--frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2`)
run on the faithful gate host (hermes-ext, llvmpipe) at head, each frame diffed
vs the committed golden:

| zone | entries | evictions | restores | f0 | f1 | f2 | total |
|---|---|---|---|---|---|---|---|
| qeynos2 | 126 | 81 | 20 | 62 | 45 | 90 | **197** |
| gfaydark | 32 | **0** | **0** | 26 | 18 | 91 | **135** |
| befallen | 9 | **0** | **0** | 148 | 0 | 126 | **274** |
| iceclad | 15 | **0** | **0** | 37 | 594 | 47 | **678** |
| timorous | 35 | **0** | **0** | 43 | 129 | 20 | **192** |

Two facts generalize the qeynos2 finding to the whole gate:

1. **Four of five zones have ZERO eviction pressure** (gfaydark/befallen/
   iceclad/timorous: 0 evictions, 0 restores, 13–51 % of the ceiling). Any
   "over-budget → evicted → unbound → black" mechanism is dead for them —
   yet all four still diff. Only qeynos2 sits at 99.6 % of the ceiling.
2. **befallen f1 = 0 px** at the same head: the diff is per-pose, exactly
   what a per-pose draw-order reshuffle predicts (each pose re-sorts its own
   buffer list), not a global asset/scene break.

## Not verified / left open (block pending Will's ruling)

- The fix (below) is specified with its prediction but **NOT implemented**:
  it changes what the driver-facing name carries, which touches the 16:00Z
  (archive,name,mask) keying ruling's letter. The card's Step-1 stop rule
  (identical ledgers, still 197 px → the remaining diff location is a new
  question) applies, so the mechanism is posted on #403 for the ruling first.
- The qeytoqrg `--objects` willow re-run at the candidate head (expectation:
  unchanged, +87,408 B, 0 deduped) — runs only once the ruling lands.
- CI `gate` stage on the current head stays RED until the fix + push.

## Candidate fix (for the ruling — see #403 note)

`ConstrainedTextureCache::uploadTexture` (decoded/S3D path,
`constrained_texture_cache.cpp:780`) hands the driver the **mangled**
`internalKey` (`name\x01@stem\x01[...\x01masked]`). Pre-B1083 the same line
handed the driver `name[\x01masked]` (stem axis did not exist; for the
unmasked single-archive case `internalKey == name`). The mangled name flows
into `textureGroupId()` (FNV-1a of `texture->getName()`, H02b/Q02) →
`materialKey` → the sorted draw order. The stem suffix is a *pure suffix* of
the P46d name, so it re-shuffles every group ID (126/126 for qeynos2).

**Candidate (family-first, willow-safe):** hand the driver the pre-B1083
P46d name (`name`, or `name\x01masked` for the masked variant) when this
upload is the FIRST loaded entry of its (name, mask) family across ALL four
cache maps (`cache_`/`deduped_`/`decodedEntries_`/`compressedEntries_`);
hand it the full `internalKey` (stem included) for every later family
member. All five gate zones are single-archive, so every one of their
families is family-first → every GL texture is created under exactly base's
name → 126/126 group IDs back to base → merge sort byte-identical →
**0 px on all five zones**. The cache key stays `(archive, name, mask)`;
the content dedupe and the scoped-miss rule are untouched. qeytoqrg's
willow: the zone's tree70 (family-first) keeps base's plain name, the _obj
tree70 (second, different bytes) gets the stem-suffixed name, so the
driver's `findTexture` cannot fold them and the alpha-tested foliage
survives. P46d's distinct-name invariant holds: the mask suffix is always
present and the stem suffix only *adds* distinctness. (The naive
"always plain name" does NOT work: it would share one driver name with the
willow's second copy and `addTexture` would discard its image — the
family-first rule is what keeps the willow intact.)

**Prediction (stated before implementing):** qeynos2 197 → **0 px**
(126 entries / 81 evictions / 20 restores / 21,932,691 B — cache numbers
unchanged, only the driver name changes); gfaydark/befallen/iceclad/timorous
135/274/678/192 → **0 px**; qeytoqrg `--objects` willow unchanged
(+87,408 B, 0 deduped, canopy holes still 52.5 %). If any zone is not 0 px
or the willow regresses, the residual is reported, nothing is blessed.
