# T199 — Derive the archive set from the spawn list

Will's framing, which supersedes the "add `_chr` archives to the global list"
option T198 offered:

> When a player zones in, the server sends the client a list of spawns and from
> that the client can figure out which asset files need to be opened. On the Pi
> and any I/O constrained device, file open operations are expensive so we must
> minimize them. **It is unacceptable to load assets that are not in use by any
> spawned entity or zone geometry.** The only "unused" assets may be for
> entities that are in zone but out of render distance / entity cap.

So: correctness first — load exactly what the zone's actual contents require —
and minimise opens within that constraint. A static list cannot do this, because
what spawns is server-side and dynamic.

## This is already possible with data the client has

**The race → archive mapping exists.** `config/race_models.json` carries 185
races with `s3d_file` (179 of them) and `also_in` (153):

```json
"24": { "code": "FIS", "name": "Fish", "scale": 0.3,
        "s3d_file": "akanon_chr.s3d",
        "also_in": ["akanon.s3d", "cabeast.s3d", "cabeast_chr.s3d",
                    "cazicthule.s3d", "cazicthule_chr.s3d", ...] }
```

**And it resolves the exact failure T198 hit.** Befallen spawns race 24; its
texture `fishe0301.bmp` was found by `verify_eqe1` in the regenerated
`akanon_chr.eqe1`, `cabeast_chr.eqe1` and `cazicthule_chr.eqe1` — precisely the
archives `race_models.json` names, and precisely the ones
`config/eqe1_archives.json`'s static list never opens.

**The spawn list is available at the right moment.** The graphics load already
receives it: `loadZoneGraphicsOnThread: starting (zone='…', entities=N,
doors=N, player='…')`. The `ZoneLoadSnapshot` is built from game state after
the server's spawn packets, so Step 0 can compute the archive set before
opening anything.

## The design

At zone load, build the archive set as the union of:

1. **Zone geometry** — `{zone}`, `{zone}_obj` (and `{zone}_chr` when present).
2. **Spawned races** — for each DISTINCT (race, gender) in the spawn list, the
   `s3d_file` from `race_models.json`, falling back through `also_in` if the
   primary is absent.
3. **Equipment/held items actually referenced** by those spawns (the `gequip*`
   family is currently opened wholesale and should be narrowed the same way).
4. **The client's own packs** — `willeq.eqe1` (UI atlas) and `textures.eqe1`.

Then open exactly that set. Nothing else.

This replaces `eqe1_archives.json`'s fixed `globalArchives` list, which today is
both **too wide** (every `global*_chr` and every `gequip*` regardless of what
spawned) and **too narrow** (no other-zone `_chr` pack, which is the bug).

## Why this beats the alternatives

- **Static list + more entries** — violates the rule directly: it loads assets
  no spawned entity uses, and on a 32-bit Pi each mapped archive costs address
  space (T195: that is what killed it at 3.13GB).
- **Open-on-miss / lazy** — cannot work under the all-or-nothing policy: a miss
  is a fatal error, so the set must be right *before* the first lookup, not
  discovered by failing (T196/T197).

## Sequencing note

Race 24's entry lists `s3d_file` **and** eleven `also_in` archives. Only one is
needed. Prefer `s3d_file`; use `also_in` only when the primary is missing from
the EQE1 tree. Opening all listed archives would reintroduce exactly the
over-loading this unit exists to stop.

## Known cleanups this subsumes

- `gequip7` is in `globalArchives` but `gequip7.s3d` does not exist in the
  client — the resulting `cannot open` error disappears once the set is derived
  rather than hardcoded.
- The out-of-render-distance case Will carved out is already handled elsewhere:
  T109's entity load-distance gate defers those entities, and their archives
  simply are not required until they come into range.
