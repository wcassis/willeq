# T207 — Zone loading opens 33 archives it cannot know it needs

Status: FINDING (no code change yet). Evidence: `review/T206/t206_x86.log`.

## The bar
Will, 2026-08-23: "It is unacceptable to load assets that are not in use by any
spawned entity or zone geometry." Functionality before performance.

## Verdict: the bar is not met

A cold befallen zone-in opens **38 EQE1 archives**. Only a handful are
justified by the zone or by any spawned entity.

Opens, in the order they happened:

    1  willeq                    <- ours
    2  befallen                  <- zone geometry      JUSTIFIED
    3  befallen_obj              <- zone objects       JUSTIFIED
    4  befallen_chr              <- zone characters    JUSTIFIED
    5..37  global_chr, global2..7_chr, gequip, gequip2..6, gequip8,
           gatesequip, ldonequip, lgequip, lgequip2, lgequip_amr,
           lgequip_amr2, loyequip, vequip, frogequip, obequip_lexit,
           global17..23_amr, sky, textures
    38 akanon_chr                <- T199 spawn-derived JUSTIFIED (race 24)

**Entries 5-37 are opened in list order, not in need order.** That ordering is
the proof: it is exactly `config/eqe1_archives.json`'s `globalArchives` array,
drained front-to-back. Nothing about befallen's 15 spawned races selects that
sequence.

## Root cause: the lazy opener is a linear probe

`renderer_zone_load.cpp:139-143` installs the cache's lazy opener:

    nextGlobalEQE1Idx_ = 0;
    constrainedTextureCache_->setEQE1LazyOpener(
        [this]() { return tryOpenNextGlobalEQE1(); });

and `renderer_effects.cpp:404-415`:

    while (nextGlobalEQE1Idx_ < pendingGlobalEQE1Archives_.size()) {
        const std::string name = pendingGlobalEQE1Archives_[nextGlobalEQE1Idx_++];
        const EQE1Loader* loader = getOrOpenEQE1(name);
        if (loader) return loader;
    }

On a texture miss the cache opens *the next archive in the list* and retries.
There is no name -> archive index anywhere in the path. Consequences:

1. A texture living in list entry #34 forces opening entries #1..#34.
2. **A texture that exists in NO archive drains the entire list** — 37 opens to
   answer one lookup, and the answer is "not found".

Case 2 is what happened here, and the log names the culprits:

    Skipping entity 274 (a_rat000): base race model not found:
      race=415 gender=2 (not found in any S3D archive) — placeholder cube
    Skipping entity 267 (SummonClockworkBanker000): base race model not found:
      race=457 gender=2 (not found in any S3D archive) — placeholder cube

Those two unresolvable races are individually enough to exhaust the list. The
placeholder-cube defect and the over-opening defect are the same bug wearing two
hats: **we discover "not present" only by opening everything.**

## This also explains T206's Pi result

EQE1's stated purpose (Will, 2026-08-23) is "flatpak to reduce file i/o
overhead". T206 measured the Pi at ~39.5s under EQE1 vs ~37.7s under S3D — no
gain. But the Pi still performs **38 open+mmap operations per zone-in**, because
the probe defeats the very thing the format was adopted for. On x86 the page
cache hides that (12.1s -> 5.0s, -59%); on an i/o-constrained SD card it does
not. The perf result is a symptom of the correctness defect, not a separate
problem — which is the right order to fix them in anyway.

Note this means T206's Pi number should NOT be treated as EQE1's verdict. It is
the verdict on EQE1-plus-a-33-archive-probe.

## Correction: the index machinery already exists

An earlier draft of this doc proposed inventing a manifest format. That
overstates the work. Three points, checked:

1. **Every .eqe1 already has a TOC.** `EQE1Loader::open()` builds
   `textureLookup_` / `rawDataLookup_`, exposed via `getTextureNames()` and
   `getRawDataNames()`. But it is *inside* the archive, so it answers "what is
   in here" only after the open we are trying to avoid. It cannot answer "which
   archive holds this name" — the question the probe is guessing at.

2. **Nothing is emitted alongside the archives.** The regen tree is 1008 `.eqe1`
   and 0 `.json`. `eqe1_builder.cpp`'s sole "json" mention (line 644) treats
   `.json` as a passthrough input extension. `convert_all_to_eqe1.sh` emits none.

3. **But the manifest pattern exists and works, three times over:**

       data/equipment_models.json   414KB   tools/equipment_index_builder.cpp
       data/item_models.json         51KB
       config/race_models.json      107KB   <- T199 already uses this

   And `equipment_models.json` is already nearly the right shape:

       "1": { "archive": "gequip.s3d", "wld": "gequip.wld",
              "actor": "IT1", "textures": ["flat.bmp","sword.bmp",...] }

   Name -> archive, plus the per-model texture list. It is built by walking the
   `*equip.s3d` archives — the same walk `convert_all_to_eqe1.sh` performs. EQE1
   base names match the S3D ones, so `archive` needs an extension swap, not a
   rebuild.

## Recommended fix

The real gap is narrower than "build a manifest": **nothing in the texture
lookup path consults any index.** `race_models.json` is consulted for models
(that is how T199 picks archives correctly) and `equipment_models.json` for
equipment, but a texture miss falls straight into `tryOpenNextGlobalEQE1()` and
begins guessing.

So: extend the existing index-builder pattern to cover texture names across the
EQE1 tree, and make the cache's lazy opener resolve through it instead of
probing. A lookup then either resolves to exactly one archive and opens exactly
that one, or is absent from the index and fails immediately with no opens at
all.

Speculative opens go to zero by construction, races 415/457 fail in O(1) and can
be reported as the asset gaps they are rather than as a 33-open scan, and
befallen's open count drops from 38 to roughly 5. `globalArchives` stops being a
probe order and becomes a candidate set the index arbitrates.

T199's spawn-derived selection stays as-is — the same principle applied to
models, already working (15 races -> 3 archives, correctly).

## MEASURED: 64% of opens serve nothing

`EQE1Loader` now counts the lookups it actually serves (`getLookupHits()`,
incremented on successful `lookup()` / `getRawData()`), and
`IrrlichtRenderer::logEQE1ArchiveUsage()` reports every open archive at
zone-in-complete. Always on -- a handful of INFO lines once per zone-in, and the
whole point is that the waste was invisible until counted.

Cold befallen zone-in, x86, regen tree, orangepi preset (`review/T207/`):

    zone 'befallen': 37 archive(s) open, 24 served no lookup at all
    (24 of 37 = 64% wasted opens, 103115KB of 149385KB mapped for nothing),
    293 lookups served total

**24 of 37 archives served zero lookups. 103MB of the 149MB mapped was mapped
for nothing** -- 69% of the bytes. The single worst offender is `lgequip` at
24MB, never touched once.

The 13 that did serve something, and by how much:

    global_chr    138      befallen_obj   59      befallen_chr   56
    global6_chr     9      befallen        9      akanon_chr      5
    sky             4      gequip3         4      gequip          3
    textures        2      gequip5         2      loyequip        1
    gequip4         1

Note the shape: three archives (`global_chr`, `befallen_obj`, `befallen_chr`)
serve 253 of 293 lookups -- 86%. The long tail of equipment and armor archives
either serves a handful or, in 24 cases, nothing at all. `akanon_chr`, the one
T199 derived from spawns, correctly serves 5.

This is the before number. The index fix has to drive "served no lookup" to 0.

## FIXED: 37 archives -> 13, zero unused

`tools/eqe1_texture_index_builder` (new target) walks an EQE1 tree, reads each
archive's TOC once, and writes `data/eqe1_texture_index.json`. Kept separate from
`equipment_index_builder` on purpose: that one maps equipment *model IDs* for the
equipment path and has its own consumer; widening it would have made its name a
lie and entangled two things with different lifetimes.

The index records **every** archive holding a name, not one. 21356 of 50447 names
live in more than one archive and one lives in 691 -- a single-winner index would
have resolved a name to whichever archive sorts first alphabetically and opened
an unrelated zone's file, trading the old waste for a stranger kind.

Runtime: `EQE1TextureIndex` loads it once; `IrrlichtRenderer::
openArchiveContaining(name)` resolves a miss to the one archive that both holds
the name and is in this zone's candidate set, in configured order.
`ConstrainedTextureCache`'s argless lazy opener became a name-taking resolver, so
both lookup paths are now search-open-archives then resolve-at-most-one.
`tryOpenNextGlobalEQE1()` and its cursor are deleted.

Cold befallen zone-in, x86, regen tree, orangepi preset (`review/T207/`):

    |                | archives open | served nothing | mapped |
    |----------------|---------------|----------------|--------|
    | before         |      37       |       24       | 149MB  |
    | after          |      13       |        0       |  46MB  |

Every archive still open serves at least one lookup:

    global_chr 138  befallen_obj 59  befallen_chr 56  global6_chr 9
    befallen     9  gequip5       5  akanon_chr    5  sky         4
    gequip       3  textures      2  gequip3       2  loyequip    1
    gatesequip   1

Same 294 lookups served, same 6 skipped entities, no new errors. Suite 2745/2746
(sole failure the known ALSA host quirk).

One honest note on the shifted counts: `gequip4` dropped out and `gatesequip`
came in, because a name present in several archives now resolves by configured
candidate order rather than by whichever archive the probe happened to reach
first. The content behind a given name is the same either way.

`convert_all_to_eqe1.sh` now rebuilds the index at the end of a conversion. The
runtime resolves every lookup through this file, so a stale index means textures
that exist go unfound -- the two must never ship out of step.

## Still open
Races 415 and 457 resolve to no archive at all and still render as placeholder
cubes. That is now a clean O(1) answer instead of a 33-open scan, but the assets
are genuinely missing -- a real gap, tracked separately from this fix.
