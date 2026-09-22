# T504b — `entity_models`: stop retaining character archives the zone does not use

**Status: PLAN FOR WILL'S REVIEW. Nothing here is implemented.** Part of the
T504 load-step audit (branch `task/T504-load-step-audit`). Executed by the
orchestrator in-session, per Will's instruction for this audit (no executors).

## Why this row

`entity_models` is the largest row of the client heap in every audit zone, at
the original-parity baseline (`WILLEQ_BASELINE=1`, x86, `steady_60s`):

| zone | client heap | `entity_models` | share |
|---|---:|---:|---:|
| gfaydark | 93.2 MiB | 38.6 MiB | 41 % |
| dreadlands | 122.0 MiB | 38.5 MiB | 32 % |
| iceclad | 103.8 MiB | 28.1 MiB | 27 % |
| qeynos2 | 57.4 MiB | 31.8 MiB | 55 % |

The target for the whole client heap is 64 MiB. This one row is more than half
of it, and almost none of it is in use.

## What is measured (not inferred)

All from the `[modelmem]` line at `zone_load_complete` and the client log of
the T504 baseline runs (`run41_*`, `run42_*`).

1. **What the zone actually uses is ~1–2 MiB.** dreadlands: `model_geometry`
   0.30 + `model_skeleton` 0.65 + `anim_mesh` 0.18 MiB for the models built.
   The other ~35 MiB is *source*: every `CharacterModel` of every `_chr`
   archive that was opened, kept for the life of the zone (global ones for the
   life of the process).

   | term (dreadlands) | MiB | what it is |
   |---|---:|---|
   | `src_skel_*` | 10.0 | skeletons + every animation of every character in the archive |
   | `skel_containers` | 8.2 | map/vector/string nodes of the above |
   | `dup_geometry` | 7.1 | the same part geometry held three times (`parts`, `partsWithTransforms`, `rawParts`) |
   | `src_char_*` | 7.1 | part geometry |
   | block overhead | 4.8 | 167,284 live blocks × glibc header |

2. **Whole archives are parsed to take one thing from them.** dreadlands
   opens `global4_chr.s3d` for race 128 and then all of `global_chr.s3d` —
   every playable race with every animation — to borrow animations for that
   one race ("animation merge loading global_chr.s3d from disk for race=128").

3. **`global_chr.s3d` is held twice.** `src_char_global` and `src_char_other`
   are byte-identical (2,049,649) in iceclad; dreadlands' `other` is that plus
   global4 (3,098,599 − 2,049,649 = 1,048,950). `loadGlobalModels()` fills
   `globalCharacters_`, and the prep worker's `tryLoadFromArchive()` parses the
   same file again into `otherChrCaches_["global_chr.s3d"]`. Two full parses,
   two full copies. This is a plain defect, independent of pruning.

4. **The transient is worse than the steady state.** `ledger_peak` for the
   category is 96.5 MiB in dreadlands, 83.4 in gfaydark and iceclad, against
   ~40 live. Entities load *last*, on top of everything else, so this spike
   sets the process peak — and RSS follows the peak (glibc does not give it
   back). Shrinking only the retained set would not move max RSS.

5. **Pruning is pointer-safe by construction.** `RaceModelData` holds
   `shared_ptr<ZoneGeometry>` and `shared_ptr<CharacterSkeleton>`;
   `CharacterSkeleton::animations` is a map of `shared_ptr<Animation>`, and the
   animation merge copies those pointers (`ourSkel->animations[code] =
   sourceAnim`). A built model therefore keeps alive exactly what it uses,
   whoever else lets go. Dropping a source `CharacterModel` frees only what no
   built model references.

6. **The constraint T500 found still stands (its STOP 5).**
   `preloadVariantModel()` re-reads `partsWithTransforms`/`rawParts` of an
   *already built* character to pick body/head variant parts, and
   `OP_WearChange` reaches it. So the source of a character that is **in use**
   must stay reachable. This plan never drops a used character's source; it
   drops the characters nobody is using.

## The rule

> After a zone's entity load, and after every later model build, the loader
> retains the source `CharacterModel` of **the used set** only. Everything else
> parsed from the same archive is dropped. A later request for a dropped
> character re-parses its archive, takes that one character, and drops the rest
> again.

**The used set** is the closure of:
- every character a built `RaceModelData` / variant / animated mesh was made
  from (keyed by archive + model code), and
- every character one of those borrowed animations from
  (`getAnimationSourceCode()`), transitively.

The second bullet exists for the variant path, which re-runs the animation
merge when it builds a variant: if the donor were dropped, a WearChange on a
race with borrowed animations would re-parse an archive. Keeping the donor's
source costs one character; its *animations* are alive anyway (fact 5).

This applies uniformly to all four stores (`globalCharacters_`,
`numberedGlobalCharacters_`, `zoneCharacters_`, `otherChrCaches_`). A store
entry becomes "archive X: these N characters retained, the rest dropped", and a
lookup miss on a known archive means *reload*, not *absent*. The negative
lookup cache (T94) keeps its meaning: a code that is in no archive stays
negative and never triggers a re-parse.

### Late spawns (the on-demand reload)

A race that first appears after zone load already goes through the prep worker
(`preloadModelData`), off the render thread, and already handles "archive not
in cache → parse it". After this unit the same branch also serves "archive was
parsed, but that character was dropped". Cost per *new distinct race*:
one archive parse on the worker — measured 0.75 s for `global_chr.s3d` on x86
(log timestamps, run41); the Pi figure is taken in Leg 5. It is bounded by the
number of distinct races in the zone (dreadlands: 11 zone characters), never
per spawn. Will's standing ruling applies: load time is secondary to
correctness, no maximum.

Alternative considered and **not recommended**: eagerly build every character
of the zone's own archive at load, then drop all source. It removes the late
re-parse but pays for races the player may never see (~0.35 MiB of skeleton +
animations each), and it does nothing for global archives. If the Pi re-parse
turns out to be objectionable in play, this is the fallback, per archive.

## Legs, in order — each one measured before the next starts

Every leg follows the audit's procedure: state the expected change, run the
three audit zones + qeynos2, compare, stop on a discrepancy.

**Leg 0 — instrument (additive only).** A `[srcchar]` INFO census per archive
at `zone_load_complete`: characters parsed, characters referenced by a built
model, bytes each — and, on every archive parse, **which thread ran it**.
`preloadModelData()`/`preloadVariantModel()` have two callers today, the prep
worker and the zone-load thread, both off the render thread; but
`getAnimatedMeshForRace()` still has a synchronous fallback, and STOP 3 needs
to know whether it ever fires before pruning makes misses more common.
Expected memory change: 0. This produces the exact
"droppable" number per zone that Legs 2–3 are then held to.

**Leg 1 — `global_chr.s3d` parsed once.** Confirmed in code, not only by the
byte match: `tryLoadFromArchive()` (`race_model_loader.cpp`) looks in
`otherChrCaches_` and then parses from disk; it never consults
`globalCharacters_`. That is deliberate — `globalCharacters_` is filled lazily
on the render thread with no lock, so the prep worker must not read it (T114).
The fix therefore does not make the worker read it. Both stores hold
`shared_ptr<CharacterModel>`, so whichever side parses the file first
publishes the pointers under `otherChrCacheMutex_` and the other side *adopts
the same objects* instead of parsing again; the second store costs a vector of
pointers. Same for the numbered globals. Expected: −`src_char_other`
and −`src_skel_other` of the duplicate, plus its containers — about −7 MiB
steady in each audit zone, and one fewer full parse in the peak. No behaviour
change: the same objects are found, from the other store.

**Leg 2 — prune to the used set.** The rule above, with reload-on-miss.
Expected steady `entity_models`: used-set source + built products + textures
bookkeeping — single-digit MiB; the precise figure per zone comes from Leg 0's
census and the leg is held to it (±5 %).

**Leg 3 — the transient.** With pruning in place the archive is still parsed
whole before being cut down. Two steps, second only if the first is not enough:
(a) prune *per archive, immediately after its parse* rather than at the end of
entity load, so at most one archive's source is ever resident — expected peak
≈ largest single archive parse, not the sum; (b) a model-code filter passed
into `S3DLoader` so `CharacterModel`s (three geometry copies each) are only
*built* for wanted codes. The WLD fragment parse itself stays whole in this
unit — filtering 0x12/0x13 tracks by name is a larger change with its own
risk, and gets its own plan if (a)+(b) leave the peak over budget.

**Leg 4 — `dup_geometry`.** Out of scope here unless Leg 2 leaves it
material: after pruning it only exists for used characters and transiently.
Re-measure; board it with the number if it is still worth a unit. (`parts` has
readers in eight production TUs and five tools; it is not a free delete.)

**Leg 5 — Pi confirmation.** Same zones on the Pi through the local lane:
`[modelmem]`, `[srcchar]`, RssAnon **and** RssFile, and the wall-clock of one
forced late-spawn reload. T500 Leg C is the precedent: an x86 win that did not
reach the Pi is not reported as a win.

## The regression net (mechanical — no one judges a frame)

The risk is not a crash (fact 5); it is a model that silently comes out
*different* — a missing animation, a wrong donor skeleton, a variant head that
falls back. So the net compares **data**, before vs after, through the
production loader:

1. **`tools/hwcaps/model_fingerprint`** (new, headless, `EDT_NULL`): for every
   race × gender in `data/willeq.db` that resolves to a model, in each of the
   three audit zones' archive contexts, emit one line:
   model code · archive it came from · vertex/index counts + FNV of the raw
   geometry · bone count + FNV of bone names/parents · sorted animation-code
   list · FNV of every bone's transform at frames 0, mid and last of `l01`,
   `c01`, `o01`, `p01` (where present) · the same for the variant built with
   a non-default body/head (`texture=1, helm=1`), which is the WearChange path.
2. **Golden = the fingerprint file produced by the tip *before* Leg 1**,
   committed under `tests/goldens/`. It is generated by today's code, so it
   needs no blessing of new behaviour — the unit's claim is "byte-identical
   model data", and any diff is a failure, not a judgement call.
3. **`EntitySourcePruning` ctest** (skips without EQ files, like the other
   asset tests) runs the fingerprint with pruning ON and diffs the golden;
   plus three behavioural cases on ledger bytes, red first:
   - after loading race A from archive X, `src_*` bytes for X are ≤ the used
     set (red today);
   - requesting dropped race B from X afterwards succeeds and equals B's
     golden line (the reload path);
   - a WearChange-style variant request for A, and for a race with a borrowed
     animation set, performs **zero** archive parses (the closure rule).
4. **Live, once per leg:** the three audit zones at baseline, 60 s steady, exit
   0, zero `ERROR`/`FATAL`, no placeholder-cube fallbacks logged, and the
   entity-cropped frame A/B against the pre-leg build within the measured
   noise floor (0.02 % of pixels) with animation paused at a fixed frame.
5. **TSan, once at the end** (`T352`/`T370` recipe) over a zone tour: the
   stores are shared with the prep worker under `otherChrCacheMutex_`, and
   pruning adds a writer.

## STOP conditions (report, do not proceed)

1. Any fingerprint line differs from the golden.
2. A measured leg misses its expected change by more than 5 % or 0.5 MiB,
   whichever is larger — find out why before continuing.
3. A reload path is reachable from the **render thread** (it must only ever
   run on the prep worker). A synchronous re-parse on the render thread is a
   frame-rate defect under the 1999 contract, not an acceptable cost.
4. Pruning requires touching `preloadVariantModel()`'s part-selection logic
   (T500 STOP 5 territory) rather than only what it finds its source *in*.
5. Any change to `config/constrained_presets.json` budgets or to a golden
   other than the new fingerprint file — those are Will's to bless.

## File territory

`src/client/graphics/eq/`: `race_model_loader.cpp`, `model_loading.cpp`,
`model_loading_chr.cpp`, `model_loading_search.cpp`, `variant_loading*.cpp`,
`animated_mesh_creation.cpp`, `s3d_loader_characters.cpp` (Leg 3b only);
`include/client/graphics/eq/race_model_loader.h`, `s3d_loader.h` (Leg 3b
only); new `tools/hwcaps/model_fingerprint*`, new
`tests/test_entity_source_pruning.cpp`, new golden. Substantial new code goes
in a new TU (`src/client/graphics/eq/model_source_pruning.cpp`) — several of
the files above are near the 1,500-line ceiling.

## Open points for Will

1. **Reload-on-demand vs eager-build for the zone's own archive** — I
   recommend reload-on-demand (above); the Pi timing in Leg 5 is what would
   change that.
2. **Global player races.** Another player of a race not yet seen is a late
   spawn like any other, and costs one `global_chr` re-parse on the worker
   (entity appears a moment later, exactly as a first-seen NPC race does
   today). I recommend accepting that rather than pinning all playable races
   resident, which is most of what this unit removes.

## Revision 1 — Will, 2026-09-21: residency is a working set, in both directions

Will's direction: players are a variable we control (our server, our accounts);
NPC spawns and pathing are dynamic, so some run-to-run variation must be
allowed; and the original client cannot have held a whole zone's assets at
once, so assets are to be loaded **and unloaded** as needed.

What that changes here:

1. **Both open points are settled toward on-demand.** No playable race is
   pinned resident; a race is loaded when its first entity needs it.
2. **The rule gains the unload half.** The used set is reference-counted by
   live entities. When the last entity of a race (and nothing borrowing its
   animations) is gone, its built model *and* its retained source become
   evictable; they are dropped after a hysteresis window (so a roamer crossing
   the visibility edge does not thrash a 0.75 s parse) or immediately under
   budget pressure, least-recently-used first. New **Leg 2b**, after Leg 2,
   with its own red-first test: spawn race B, despawn it, assert the category
   returns to its pre-B bytes.
3. **Representation is the other half, and the disk sizes prove it.** qeynos2's
   three archives are 9.2 MB on disk and `global_chr.s3d` 7.4 MB — *compressed,
   textures included*. `entity_models` holds 31.8 MiB for that zone **with no
   texture pixels in it** (EQE1 serves those from a file mapping). The source
   data is being expanded several-fold into per-bone `shared_ptr`, `std::map`
   and `std::string` nodes (167,284 live blocks in dreadlands). Pruning removes
   what is not needed; it does not make what *is* needed compact. Leg 0's
   census therefore also reports bytes-per-character against that character's
   raw fragment bytes, so the blow-up factor is a number, and a compact
   skeleton/animation layout gets its own plan off that number.
4. **Measurement protocol.** Players: exactly one (Testchar), always — a
   controlled variable, asserted in the run (`visible players == 0 others`).
   NPCs: the entity rows (`entity_models`, `entity_instances`, `equipment`)
   get a tolerance band **measured**, not chosen: N=5 repeat runs per audit
   zone, band = observed spread; every other row is deterministic and keeps a
   tight bound. A budget for an entity row is stated as "per distinct resident
   race" plus a fixed part, so it scales with what the server happened to
   spawn instead of being blown by it.

The same working-set principle applies to the rows after this one (zone
geometry by region visibility, decoded textures) and is taken up in their own
plans; this unit stays on character models.

## Measurements

Baseline with the navmesh counted as core (Will, 2026-09-21), `run42_*`:

| zone | client heap | `entity_models` | next rows |
|---|---:|---:|---|
| gfaydark | 93.2 | 38.6 | game_state 15.2, zone_mesh 8.3, spatial 8.1, objects 5.3 |
| dreadlands | 122.0 | 38.5 | game_state 21.9, zone_mesh 17.3, irrlicht_scene 14.0, spatial 8.9 |
| iceclad | 103.8 | 28.1 | zone_mesh 16.3, game_state 15.9, zone_parse 13.3, irrlicht_scene 10.9 |
| qeynos2 | 57.4 | 31.8 | startup 4.8, game_state 4.5, archives 2.5, zone_mesh 2.4 |

Navmesh tile data inside `game_state`: gfaydark 5.43 MB (172 tiles), dreadlands 10.88 MB (2,406 tiles).
qeynos2 is the clearest case: `entity_models` is 55 % of the whole client heap there.
