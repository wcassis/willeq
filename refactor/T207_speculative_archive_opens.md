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

## Recommended fix: a generation-time manifest

The EQE1 assets are produced by our own tool, so the index can be built where
the truth already is. Emit one manifest alongside the archives mapping every
contained name -> owning archive. Then a lookup either:

  - resolves to exactly one archive and opens exactly that one, or
  - is absent from the manifest and fails immediately, with no opens at all.

Speculative opens go to zero by construction, races 415/457 fail in O(1) and can
be reported as the asset gaps they are rather than as a 33-open scan, and the
Pi's open count for befallen drops from 38 to roughly 5. `globalArchives` stops
being a probe order and becomes what it should have been: a set of candidates
the manifest arbitrates.

T199's spawn-derived selection stays as-is — it is the same principle applied to
models, and it already works (15 races -> 3 archives, correctly).

## Not yet answered
Which of entries 5-37 served at least one texture before the list was exhausted.
A per-loader hit counter logged at zone-complete would turn "cannot know it
needs" into a precise count of proven-unneeded opens. Cheap, and worth having as
the regression guard for the manifest fix.
