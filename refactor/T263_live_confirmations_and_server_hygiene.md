# T263 — owed live confirmation (T260) + test-server hygiene (T245/T255 notes)

Slot: slot2. x86. OWNS eqemu (noclobber; T259 slot3 may hold it — bounded
wait). Class B, no client code expected. Territory: none in src/ unless a
confirmation FAILS (then STOP and report, do not fix here); review/T263/;
documented server-DB writes only.

## 1. T260 live confirmation
Zone Testchar into befallen with the current fable binary (rebuild slot2
first) at DEBUG: expect `Snapshot: player BSP region=2220 pos=(-77.92,
21.91,3.00) zone=befallen` and NO `captured for ''` WARN. Then zone to
qeynos2 and back: the region line appears for each zone, tree tag matches
the zone. Record verbatim.

## 2. Server hygiene (document every write, keep a before/after dump)
- befallen: dozens of leftover GM-spawned `a_scaled_wolf_pup` (T245) and
  T247/T254 fire beetles at Testchar's home spot: `#depopzone` + `#repop`
  (or delete the rows from `spawn2`/`npc_types` ONLY if they were
  persisted by `#npctypespawn` with a permanent flag — check first;
  T227 spawned a banker non-persistently, so most should already be
  gone). Verify with `/who`-style entity count at the spot before/after.
- Testchar's stray inventory row 6011 (a meal delivered as an ItemPacket
  at slot 30 every zone-in, T255): delete the row, confirm the next
  zone-in has no slot-30 ItemPacket. Keep the row's contents in the
  evidence so it can be restored if anyone objects.
- Confirm the standing restore rows: Testchar zone 36 / -77.9232 /
  21.9086 / 3 / h190, gm=0, anon=0, level 2; Oraclerogue zone 2 /
  167/30/-29.25/h384; Testcaster zone 25 level 1; Testbuddy 0/0/3/h0.

## Verification
Review with the verbatim lines; evidence review/T263/. Commit (docs only):
`T263: T260 confirmed live; test server cleaned`.

## Review (2026-08-26, slot2, branch task/T263-live-confirm off fable 2dcabeb7)

Evidence: /mnt/projects/fable/review/T263/ (README.md indexes it). No client
code changed. Binary: slot2/build rebuilt at 12c4f6ef (T260) for sections
1-2, and again at 2dcabeb7 (T262 merged) for section 3.

### 1. T260 live confirmation -- CONFIRMED (mechanism), expected line corrected
Run1 (befallen -> qeynos2 -> befallen, exit 0), verbatim:

    Snapshot: player BSP region is SIZE_MAX (start position outside every BSP region) pos=(21.88,-77.88,3.00) zone=befallen -- continuing, nothing consumes it (T260)
    Snapshot: player BSP region=1161 pos=(428.00,-74.00,3.00) zone=qeynos2
    Snapshot: player BSP region=2220 pos=(-82.00,35.00,3.00) zone=befallen

`captured for ''` WARN: 0 occurrences in all five runs. The region lookup
runs on every zone-in against a tree tagged with the current zone; the tag
matches the zone each time (LoadZoneSources 'befallen' / 'qeynos2' /
'befallen' precede each line by <0.6 s).

Deviation, flagged: the plan expected `region=2220 pos=(-77.92,21.91,3.00)`
for the first zone-in. That row came from T260's probe, which fed the DB
coordinate in server order. The client's m_x/m_y are in geometry order
(docs/CLAUDE.md INVERSEXY: client x = server y), so the live position is
(21.88,-77.88,3) and that point is SIZE_MAX against befallen's tree -- the
renderer's own camera lookup at the same spot is also SIZE_MAX in the same
log (`currentPvsRegion_=18446744073709551615`). Probe rebuilt against
this slot's library (review/T263/bsp_point_probe_results.txt): geometry
order gives SIZE_MAX at the restore spot, 2220 at the befallen safe point,
1161 at the qeynos2 safe point -- exactly the three live lines. T260's
expected region for the restore coordinate was a coordinate-order slip in
its probe; nothing in the client is wrong, and the SIZE_MAX outcome is the
legitimate case T260's downgrade exists for. Not a STOP: the confirmation
the plan is after (lookup runs, tag matches, no stale WARN) holds 3/3.

### 2. Server hygiene -- DONE; every write logged with before/after
Mechanism: this eqemu persists GM-spawned NPCs across restarts in
`zone_state_spawns` (spawn2_id=0 rows) -- T245's "down.sh kills them" is
not true here. Findings and actions:

| item | before | action | after |
|---|---|---|---|
| befallen wolf pups (T245) | 0 (T247's depop already cleared them) | none | 0 |
| befallen fire beetles at Testchar's spot | 2 (npc 2024, ids 134/135, zone_state rows created 00:55 today) | run2 `#depopzone` + `#repop` in-game (the depop also emptied zone 36's zone_state rows) then server restart | run3 fresh zone-in: 0 beetles, 0 pups; zone 36 zone_state gm rows 0 |
| inventory row 6011 (Chef's Instant Meal x5, guid 278) | delivered as a slot-30 ItemPacket every zone-in (run1 lines 642-648, cursor queue 2) | DELETE (WRITE 1; full row + restore INSERT in db_writes.txt) | run3: 0 `Chef's Instant` lines, cursor queue 1 |
| Testchar standing row | left at safe point by runs | restored (WRITES 2,4,5,7) | zone 36 / -77.9232 / 21.9086 / 3 / h190, gm 0, anon 0, level 2 |
| Testbuddy standing row | -77.875/21.875/3/h190 (staged after T256, never restored) | restored (WRITE 3) | zone 36 / 0 / 0 / 3 / h0 |
| Oraclerogue, Testcaster | as documented | none | zone 2 /167/30/-29.25/h384; zone 25 level 1 |

`diff db_before.txt db_restored.txt` = Testbuddy row + row 6011 only;
`diff db_restored.txt db_final.txt` = empty. Left for a decision, not
touched (outside the plan's list): qeynos2 has 13 GM leftovers at its safe
point (7 training dummies, 4 rats, SummonResupplyAgent, T255's banker),
arena 19 rats -- ids in db_zone_state_spawns_gm_leftovers_before.txt.

Trap (cost WRITE 7): a client that exits without /camp is saved by the
zone at TERM time, overwriting a DB restore done while the server was up.
Restore after down.sh or ensure the tour reaches /camp.

### 3. Addendum: T262 race 195 -- STOP, does not render under --eqe1-path
Requested mid-unit. npc_types 114106 Lord_Yelinak (race 195, size 30),
`#invul on` then `#npctypespawn`.
- At Testchar's befallen spot (run4): never built -- stayed in the
  progressive queue 35 s, `/dumpscene`: `[NPC] id=170 'Lord_Yelinak000'
  bspRegion=-1 inGraph=true placeholder=true`; screenshot (0 magenta of
  480000) shows the blue placeholder. Spot-specific (region SIZE_MAX; T247's
  post-load beetle there never got build lines either), so repeated at a
  valid region.
- At the qeynos2 safe point (run5): model loads from bwd_chr.s3d
  (`race=195 code=BWD vertices=4631 vpieces=112`; willeq.db s3d_file is
  T262's 'bwd_chr.s3d'), then
  `EQE1: 'bwdhe0005' exists in 9 archive(s), none a candidate for zone
  'qeynos2'` -> `[FATAL] Texture 'bwdhe0005.dds' not found in any EQE1 flat
  pack` -> exit 1. bwd_chr.eqe1 holds the texture; the zone's EQE1
  candidate set is built from the spawn list at zone load only
  (`16 distinct spawned races -> 4 archive(s) queued`), so a post-load
  spawn of a race absent at load is refused by
  `IrrlichtRenderer::openArchiveContaining()` (renderer_effects.cpp:408,
  T207 rule 2) and the EQE1 all-or-nothing policy makes that fatal.
  T262's data is right; the archive selection does not react to post-load
  new races. Client code -- out of territory, reported, not fixed.
  Placeholder=false / non-zero vertices / 0-magenta screenshot: NOT
  obtained for race 195.
- Both dragons were persisted by the shutdown save and deleted (WRITE 6).

### Verification
| step | result |
|---|---|
| cmake --build slot2/build (x2: at 12c4f6ef and 2dcabeb7) | exit 0 |
| ctest / gles2 / arm / file sizes | not run: docs-only unit, no source change (plan narrows to Review + evidence) |
| eqemu | lock taken with noclobber three times (free each time), down.sh + lock deleted each time; final: containers 0, lock absent |
