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
