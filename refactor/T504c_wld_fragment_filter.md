# T504c — the entity-load transient: parse only the WLD fragments a wanted character needs

**Status:** Legs 1, 1b, 2, 2b DONE (2026-09-22). Leg 3 (0x36/0x10/0x14) and
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
