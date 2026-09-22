# T504c — the entity-load transient: parse only the WLD fragments a wanted character needs

**Status:** Legs 1, 1b, 2, 2b, 2c, 2d, 2e, 2f, 2g DONE (2026-09-22).

**Leg 2f (equipment index):** Step 5 rebuilt the equipment index on EVERY
zone load by parsing 17 `*equip*.s3d` WLDs whole (17,884 skeleton and
18,140 track fragments per load under the `archives` scope), 11 of them
archives the original client never loads (`Resources/GlobalLoad.txt`
names GEquip, GEquip2-5, GEquip8; loy/ldon/gates/frog/omens/don/dod are
later expansions; vequip/lgequip*/gequip6/obequip_lexit are not listed at
all). `EquipmentModelLoader::indexedArchiveNames()` is now the era six
(same relative priority), the index is built once per process (Step 5
skips it when the loader has one), and the open archive is reused for the
parse. 622 of the 1,081 indexed models came from the pruned archives; the
census tests iterate the loader's list (floors re-pinned to 459 / 34).
FLAG for Will: vequip.s3d (20 models, "Velious equipment" by name) is not
in GlobalLoad.txt either; it is pruned with the rest under the rule.

**Leg 2g (zone_parse transients, iceclad trace `run108`):** (1) every 0x22
region filled a private PVS store that `addRegion` then copied into the
tree's growing arena -- 13 MiB of transient for a 4.6 MiB arena; the
region now joins the tree first and appends in place, the arena is
reserved from the 0x22 fragment sizes, and `shrinkPvsArena` copies only
when the slack exceeds 1/8. (2) The combined zone geometry (12 + 7.5 MiB
of vertex+triangle copy) was built on every load but read on the PVS path
only for its texture names/animations and bounds; Step 1 no longer builds
it, Step 2 gives the PVS path a materials+bounds summary
(`getCombinedMaterialSummary`) and the no-PVS fallback the full one.

**Legs 2f/2g measured (`run109` vs `run104`; archives peak / zone_parse
peak / RssAnon at entities):** gfaydark 35.4 → 5.4 / 19.6 → 13.0 / 209.9 →
202.3; dreadlands 35.5 → 5.5 / 32.2 → 23.3 / 219.9 → 208.1; iceclad 35.4 →
5.4 / 58.5 → 58.0 / 245.5 → 231.7; qeynos2 35.4 → 5.5 / 14.3 → 11.7 / 165.4
→ 160.3. Equipment index built once per run; render parses 0; errors 0.
Iceclad's zone_parse peak did not move (its LIVE at entities did, 47.4 →
34.4): re-traced in `run110`.

**Visual gate (memory-coverage block) needs Will's bless:**
`characterModelsBytes` for the fixed 5-race sample is now 1,445,267 in all
three zones (was 23,935,538 / 23,567,212 / 23,695,017) -- the T504b source
pruning Will approved; two gate passes bit-identical. The manifest values
are applied BY HAND after a bless in chat (T303 rule), so the gate stays
red until then.

**Leg 2g follow-up:** the first reserve (sum of the 0x22 BODIES) over-reserved
iceclad's 4.6 MiB PVS arena to 8.1 MiB and the shrink then copied it
(`run110`); the reserve is now an exact pre-pass over the 0x22 fragments
(`fragment22PvsBytes`), allocated once at its final size.

**Suite triage (local run of all 4,987 tests, CI never ran them on this
branch):** 7 failures. (a) X11NumpadE2E fails at the merge base too
(environment). (b) HeldItemTest/EquipmentItemCensus floors re-pinned to the
era set. (c) Race330LoaderTest expected the playable Froglok from
globalpcfroglok_chr.s3d -- a post-Velious archive the resolver prunes; the
test now pins that race 330 resolves to nothing in the era. (d)
EntityModelSharingTest (the split-vs-ledger identity) was off by 47 %: two
double counts, both ours -- the adopted global_chr.s3d other-chr entry shares
the global store's TextureInfo/CharacterModel objects (counted in both
maps), and RaceModelData::textures became ONE shared snapshot per merged
map (T504) while the stats still charged a map copy per model; each object
is now counted once and the split closes at +0.25 % (baseline was -0.88 %).
(e) The boat rescale test spawned race 72 with no zone; SHIP lives in
oot_chr.s3d, so the test now builds it in Ocean of Tears.

**game_state, the largest RETAINED client term (15.8 MiB, gfaydark, trace
`run112`):** navmesh tiles 5.2 MiB (`PathfinderNavmesh::Load`), and the
hc_map raycast BVH 9.4 MiB (`createRaycastMesh` in hc_map.cpp: the
MyRaycastMesh triangle copy 3.0 + 0.5, NodeAABB split index vectors 2.4 +
1.5, raycast scratch 2.0) -- the zone's collision/LOS map, queried by
FindBestZ/LOS. Not a defect; a representation-compactness lever (the BVH
keeps a per-node std::vector of triangle indices plus a full triangle
copy the zone geometry already holds) for the plan Will asked for on
skel_containers. Everything else in game_state is under 0.2 MiB.

**Leg 4 (parse each archive once per load): DONE as a race-set prefetch.**
Not a WldLoader cache: the zone load knows every (race, gender) it will
prepare before it prepares the first one, so `setPrefetchRaces` announces
the set, the first parse of each archive is filtered to the UNION of their
wanted codes, and `pruneSourceCharacters` keeps the announced characters
until `zone_load_complete` (without that last rule the prune between two
races dropped what the next race needed and the archive was parsed again
-- `run113` showed the filter alone changed nothing). Measured (`run114`
vs `run111`; archive parses / reloads / base-model preload ms /
entity_models peak during the load): gfaydark 17 → 14 / 11 → 8 / 406 →
114 / 15.9 → 18.6; dreadlands 10 → 8 / 2 → 0 / 237 → 189 / 19.6 → 27.5;
iceclad 9 → 7 / 2 → 0 / 225 → 135 / 14.0 → 15.7; qeynos2 17 → 8 / 11 → 2
/ 350 → 114 / 17.9 → 14.0. Steady state unchanged; identity 4/4; render
parses 0. The load-time peak rises where the zone has many races
(dreadlands +8 MiB) because their characters are held until the load
completes; `WILLEQ_PREFETCH_RACES=0` restores the per-race chain for A/B.
On the Pi a reload costs 1.7-2.8 s per race (Leg 5 table), which is why
the trade is taken by default. The prep worker's pool threads are now
named `eq-bgpool` (they read as "willeq" in the parse census).

**Also found (CI):** the branch's CI `build` job has failed since 108ba056
(`-Werror=format-truncation` on `FormatTimestamp`'s 32-byte buffer,
inlined into the T504 navmesh log line) and `hygiene` before that, so the
test job NEVER ran on this branch. Fixed (bounded copy). Running the suite
locally found `test_held_item_attachment` crashing: since T504b's
render-thread deferral, a main-thread model lookup with NO prep worker
(tests, tools) was deferred to nobody and never loaded. The worker request
now reports acceptance; without a worker the load is synchronous, as
before T504b.

**Leg 2e (the `archives` peak was the EQE1 index's jsoncpp DOM):** the
`archives` ledger trace (`run105`) put 26.4 MiB / 235,907 allocations in
libjsoncpp plus 3.9 MiB in `getMemberNames()`: `EQE1TextureIndex::load`
built a jsoncpp DOM of the 1.9 MB `data/eqe1_texture_index.json` (1,506
archives, 50,972 names) on every zone load and freed it on return -- the
whole 35 MiB `archives` peak. The index now parses its own two-member
schema with a small streaming reader straight into the flat arrays
(escapes = the builder's; anything else is refused; names verified sorted,
sorted through a permutation if a hand-edited file is not). Measured
(`run106` vs `run104`, gfaydark): archives peak 35.4 → 21.6 MiB, RssAnon at
game_zone_sources 116.9 → 106.3, at entities 209.9 → 206.7; index counts
identical (1506 / 50972, python-checked); errors 0; render parses 0.

**Leg 2d (PfsArchive maps the file):** `PfsArchive::open` read the whole
archive into a heap buffer AND copied every member's deflate blocks out of
it (global_chr.s3d 7.36 MB: a 14 MiB open peak, 7 MiB of anonymous heap
retained while the loader lived, 3,234 allocations) on every archive open
-- character archives, the zone archive, `_obj`, and the audio `snd*.pfs`.
It now mmaps the file (PROT_READ, MAP_PRIVATE; heap read as the fallback)
and records each member's block range; `get()` inflates straight from the
mapping. Mapped pages are file-backed (RssFile), shared through the page
cache and reclaimable. Measured (`run104` vs `run103`; entity_models peak
/ zone_parse peak / RssAnon at entities / RssFile peak): gfaydark 23.1 →
15.9 / 25.2 → 19.6 / 219 → 210 / 105 → 113; dreadlands 26.8 → 19.6 / 38.6
→ 32.2 / 235 → 220 / 101 → 112; iceclad 21.2 → 14.0 / 65.8 → 58.5 / 258 →
246 / 101 → 111; qeynos2 25.1 → 17.9 / 20.7 → 14.3 / 178 → 165 / 108 →
118. Identity 4/4; archive/graphics/audio/WLD unit tests green; render
parses 0; errors 0. REFUTED: the `archives` category peak (39 → 35.4 MiB)
was NOT the block store; it is something else under that scope (next
trace).

**Leg 2c (names-only textures under EQE1):** every character-archive load
inflated every BMP/DDS of the archive (global_chr: 3,233 files, 6.7 MiB)
and `releaseBytesIfEqe1Authoritative` freed them unread moments later
(EQE1 packs are authoritative on every audit run: "released at once" in
the log). `S3DLoadOptions::textureNamesOnly` records the same end state
(present(), bytesReleased) without inflating; `RaceModelLoader::
characterLoadOptions()` sets it when the constrained texture cache has
EQE1 loaders and is now the options of every character-archive load (17
sites). Measured (`run103` vs `run101`, entity_models peak): gfaydark 25.9
→ 23.1 (28.8 after the late reload); dreadlands 32.5 → 26.8; iceclad 24.1
→ 21.2; qeynos2 28.9 → 26.0; identity 4/4; render parses 0; errors 0.
global_chr retained at its `[srcchar] parsed` line 20.3 → 13.3 MiB; what is
left of one filtered global_chr load is the archive's block store (7 MiB,
`PfsArchive::open`), the inflated WLD (4 MiB) and the unfiltered 0x36/0x10
fragments (~2 MiB, Leg 3). Leg 3 (0x36/0x10/0x14) and
Leg 4 (reuse across reloads) open.

**Leg 2b (traced, not planned):** the peak trace after Leg 2 (`run99`) named
three costs of every CHARACTER-archive load that no consumer reads: the
combined "zone" geometry of all character meshes (3.9 MiB), `loadTextures`
inflating every texture that `loadCharacterTextures` inflated again (2 × 6.7
MiB, both retained until the loader died) and a second `PfsArchive::open`
of the same file in `loadCharacters` (2 × 7 MiB of blocks resident at once).
`S3DLoader::loadZone` now recognises a character archive
(`deriveChrArchivePath(path) == path`) and skips the first two; the third
reuses the open archive. Measured (`run101` vs `run98`, entity_models peak
/ steady RssAnon): gfaydark 43.6 → 25.9 (31.5 after a late reload) / 241.6
→ 231.5; dreadlands 49.6 → 32.5 / 261.5 → 242.1; iceclad 41.6 → 24.1 / 279.1
→ 264.4; qeynos2 46.5 → 28.9 / 214.8 → 204.6. Identity 4/4 (and the tool
runs 224 → 154 s); render parses 0; errors 0.

**Leg 2 measured (x86, baseline, `run98_leg2_*` vs `run95_resolver_*`):**

| zone | entity_models peak before → after | steady RssAnon before → after |
|---|---|---|
| gfaydark | 63.7 → 43.6 MiB | 265.3 → 241.6 |
| dreadlands | 73.9 → 49.6 | 278.7 → 261.5 |
| iceclad | 69.8 → 41.6 | 302.7 → 279.1 |
| qeynos2 | 66.6 → 46.5 | 235.4 → 214.8 |

Fingerprint goldens identical 4/4; render-thread parses 0; `dangling13=0` in
every `[wldparse]` line; 0 errors. The Leg 3b STOP 1 (race 120 resolving from
another archive) was a filtered cache entry served as complete on a later
hit; every hit now checks the archive's name index and reloads (filtered)
for a wanted code the store lacks (`storeLacksIndexedCodeLocked`,
`reloadArchiveForCodesLocked`). The remaining peak is the unfiltered
whole parse of the zone's own `<zone>_chr.s3d` (loadZoneModels) plus the
0x36 meshes of every filtered parse — Leg 3.

## Why

After T504b Legs 1–2b the retained `entity_models` row is 5–12 MiB in the
four audit zones, but the row's PEAK during the entities step is still
62–68 MiB (x86, baseline; `run88`, `run91`), and glibc keeps RSS at the peak.
Leg 3b (build a `CharacterModel` only for wanted codes) moved the peak by
**0.0 MiB**: gfaydark entities-step peak 63.3 → 63.3 while the built
characters fell from 31 to 3 in `global_chr.s3d` and from 12 to 1 in
`crushbone_chr.s3d`. The trace instrument (`WILLEQ_LEDGER_TRACE=entity_models
... _SORT=peak`, `run90`) names the peak: `WldLoader::parseFragment12` (track
definitions: every keyframe of every bone of every animation of every
character in the archive), `parseFragment13` (track references) and
`parseFragment36` (meshes) — the WHOLE-WLD parse that precedes any character
build, kept alive in `S3DZone::wldLoader` until the `S3DLoader` goes out of
scope. The plan for T504b said this term gets its own unit if 3a+3b leave
the peak over budget. They do.

A second cost with the same root: every reload-on-miss re-parses the whole
WLD (gfaydark: `gfaydark_chr.s3d` parsed 6× in one zone load, once per newly
seen race), ~0.3–0.7 s each on the loading thread.

## What the format allows (measured in the code, not inferred)

- `WldLoader::parseWldBuffer` reads every fragment's `nameRef` BEFORE its
  body (`wld_loader.cpp` ~366–376): the name is known at the `switch`, so a
  fragment can be skipped by name without touching its body.
- Track names encode the animation and the model: `C01HUF_ROOT_TRACK`
  = anim `c01`, model `HUF`, bone `ROOT` (`wld_fragments_skeleton.cpp:298`);
  pose tracks are `HUF_ROOT_TRACK`. Mesh (0x36), skeleton (0x10), actor
  (0x14) and reference (0x2D/0x11) names start with the model code
  (`HUM_DMSPRITEDEF`, `HUMHE00_DMSPRITEDEF`, `HUM_HS_DEF`, ...).
- Fragments reference each other by INDEX (`fragIndex = i + 1`); indices do
  not change when a body is skipped, so references from kept fragments stay
  valid; a dangling reference to a skipped fragment reads as "absent" in
  the existing maps (`trackDefs_`, `trackRefs_`, `geometries`), which the
  skeleton builder already tolerates (it logs and skips).
- Donor animations: a character borrows tracks whose model code is its
  animation source (`getAnimationSourceCode`, e.g. PUM ← LIM). The wanted-code
  set T504b already computes (`wantedCodesFor`) includes the donor codes, so
  the same set drives this filter.

## The rule

> `S3DLoadOptions::characterCodeFilter` (T504b Leg 3) also reaches
> `WldLoader::parseFromOpenArchive`: with a non-empty filter, a fragment of
> type 0x10/0x12/0x13/0x14/0x2D/0x11/0x36 is parsed only if its decoded name's
> model code is in the set. Everything else (materials 0x30/0x31, textures
> 0x03/0x04/0x05, BSP/region/light fragments) is parsed as today. The archive
> name index (`S3DZone::allCharacterNames`) is filled from the 0x10 names,
> which are read whether or not the body is parsed.

Unfiltered parses (`loadGlobalModels`, `loadZoneModels`, `--scenario`
tools, the fingerprint tool's own zone context) are byte-for-byte what they
are today; the filter is opt-in per call, exactly like Leg 3b.

## Expected effect (to be measured, not promised)

Per archive parse, the transient becomes ~(wanted characters / all
characters) of today's: `global_chr.s3d` (31 characters, 7 MiB parse) for 3
wanted codes → ~0.7 MiB; the entities-step peak in gfaydark should fall from
63 MiB toward the retained ~12 plus one archive's filtered parse. The leg
is held to the number the trace instrument reports for
`parseFragment12/13/36` at the entities point, ±5 %.

## Legs

1. **Instrument (0 change):** per parse, log fragment counts parsed vs
   skipped per type, and the ledger bytes the parse added
   (`[srcchar] parsed ... entity_models_live=` already exists; add the
   per-type counts).
2. **Filter 0x12/0x13 by name** (tracks are the largest term). Fingerprint
   identical in all four zones (`test_entity_source_pruning`), with pruning
   on (the tool's exhaustive 380-request scenario reloads every archive with
   a filter, so it exercises this path 1,400 times).
3. **Filter 0x36/0x10/0x14/0x2D/0x11 by name.** Same net.
4. **Reuse across reloads:** keep the last parsed `WldLoader` of an archive
   for the duration of one zone load (the equipment loader already does
   this: `EquipmentModelLoader::parsedWldLocked`, b0d6cff7), so N newly seen
   races in one archive cost one parse, not N. Held to the parse count in
   the `[srcchar] parse` lines (gfaydark today: `gfaydark_chr.s3d` 6×).
5. **Pi:** the same three zones on the Pi; peak RSS and the wall clock of a
   forced late-spawn reload.

## STOP conditions

1. Any fingerprint line differs from the golden.
2. A skipped fragment turns out to be referenced by a kept one in a way the
   builder does not tolerate (a crash or a WARN that the baseline does not
   log) — then the reference graph must be followed (parse 0x10/0x14 first,
   collect referenced indices, then parse bodies), which is the larger
   change this plan avoids.
3. Any parse on the render thread (the census `thread=` field).

## Open points for Will

1. Pose tracks (`HUF_ROOT_TRACK`, no animation prefix) and the shared
   `_TRACK` names some archives use without a model prefix: I will list
   every 0x12/0x13 name in the three zones' archives that does NOT start
   with a known code before Leg 2, and treat "no code" as "keep" until the
   list is understood.
2. Leg 4 (reuse across reloads) changes the peak in the other direction
   (one archive's parse stays resident for the whole zone load); if the
   measured peak of Leg 3 alone is already acceptable, Leg 4 becomes a
   load-time-only leg and can wait.
