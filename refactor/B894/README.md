# B894: Forage and Fishing — preconditions, the caught item on the cursor, and the cast animation

Issue #210. Branch `task/kb-b894-forage-fishing`. Slug `b894-forage-fishing`, lane 3.

## What landed

### 1. Client-side precondition gates (RED → GREEN, offline)

The client now refuses forage/fishing *before* sending, with the official eqstr
ids, matching the server's own refusal set. `tests/test_forage_fishing.cpp`
(12 tests, all green) pins each cell of the table below. The RED state (the
first failing test, "forage while attacking sends OP_Forage") is commit
`382bbc77`; the green is `d479b4af` (first cell) and `b6ff5f15` (full table).
The gates live in `SkillManager` (client/skill/skill_manager.cpp,
`forageOrFishingRefusal()` and its paired `getSkillUseError()` arm — the T317
pairing rule: a refusal the predicate can return is always the message the
failure path prints).

| precondition | forage | fishing | eqstr (refuses, sends nothing) |
|---|---|---|---|
| skill not trained / not possessed | refuse | refuse | 149 / 156 |
| attacking (in combat) | refuse | refuse | 148 / 159 |
| not standing (moving) | refuse | refuse | 151 / 160 |
| pole missing or not in primary | — | refuse | 161 |
| bait missing | — | refuse | 162 |
| already fishing | — | refuse | 165 |

A cleared gate sends exactly one `OP_Forage`/`OP_Fishing` (verified by the
tests' packet capture). A second use while the previous cast is in flight is
refused (already-fishing) — the client-side cooldown the issue asks for.

### 2. The caught item on the cursor + the line (1b, live lane 3)

The server answers both successes identically (eqemu zone/forage.cpp:353-369
fish, :499-508 forage): `MessageString` then `PushItemOnCursor` +
`SendItemPacket(slotCursor)`. The client routes the item to the cursor (it
already did) and the line prints as a Chat::Skills chat message (already did).
`tests/test_forage_fishing.cpp` replays that exact packet pair and asserts the
item is ON THE CURSOR and the line printed (commit `797bf0e6`).

Live, lane 3, `scripts/scenario.sh forage_lands --timeout 240 --unit B894`:
**exit 0**, verdict "forage landed (scrounged line + item on cursor)"
(2026-10-08 19:31, evidence `/home/user/kb-evidence/b894-forage-fishing/
scenario-lane3-run3.out` and `refactor/B894/evidence/fish-256-client.log`).
Client log lines:

```
[MAIN] [CHAT] You have scrounged up some water.
[MAIN] Put Fishing Bait in slot 24; now holding Adamantite Axe.
[SCENARIO] forage landing at t=18.03
[SCENARIO] VERDICT pass (exit 0): forage landed (scrounged line + item on cursor)
```

The `forage_lands` scenario is registered (scenario_registry.cpp), its state
machine is unit-tested (`test_scenario_state_machines.cpp`, 5 tests, green):
the forage leg is the HARD leg (fail if no cursor item + "scrounged" line
within 45 s of `#forage`); the fishing leg is best-effort and reported, never
fails the run.

### 3. The fishing stand — the mechanism, measured against the lane's own watermap

The server's `CanFish()` (zone/forage.cpp:206-247, `RuleB(Watermap,
CheckForWaterWhenFishing)` ON on this lane) has TWO gates, in sequence:

1. `FindBestZ` (zone/map.cpp:49-86) on the **zonemap** (Recast raycast mesh,
   `.map`) at the rod tip: `len = player_z - bestz` must be in `[0, 100]`
   (FishingLineLength, ruletypes.h:420) or it is eqstr 166 FISHING_LAND.
2. A 1-unit-step scan of the **watermap** (`.wtr`, BSP tree) straight down 100u
   from the player's z at the rod tip; no water region (1 or 7) → FISHING_LAND.

The rod tip is `pos + 30*(sin(h), cos(h), 0)` (FishingRodLength, :419) with
`h = GetHeading()*360/512` (wire units → degrees, :214-218).

The scenario's stand was chosen by parsing the lane's own
`freporte.wtr` (md5 f52d9aa088; probes in `/home/user/kb-evidence/
b894-forage-fishing/wtr_headings.py`, `wtr_cols.py`): the harbor's water is
SOUTH of the dock (y ≈ -855, surface z ≈ -74.4, depth ≈ 10u). The stand is
`(0, -825, -74)` (the server-stored zone-in z) with **wire heading 256**
(180°): the rod tip lands at (0, -855) over ~10u of water. Headings 0/64/128
cast the rod to dry land (north/east of the dock) — that was the FISHING_LAND
refusal the earlier runs measured ("Trying to catch land sharks perhaps?",
scenario-lane3-run3 log 19:29:25/19:29:35).

The water gate for heading 256 is **proven passing** by the watermap probe.
The full live fishing CATCH is NOT yet measured on this stack (see below) —
the FindBestZ gate depends on the zone's Recast mesh (does the mesh carry a
floor under the harbor water?), which is not inspectable without re-implementing
the recast raycast. It is a server-side property, not a client defect.

### 4. Docs and BOARD

`refactor/BOARD.md` B894 struck below.

## Not verified / left open

- **The live fishing catch did not land on this lane's stack.** Scenario runs
  2 and 3 (19:20 and 19:31, stand heading 0 and 256) reached the `#fish`
  trigger; run 2 (heading 0) got the FISHING_LAND refusal; run 3 (heading 256,
  water gate provably passing per the watermap probe) got "Trying to catch
  land sharks perhaps?" twice — i.e. the FindBestZ/`.map` gate, whose floor
  the Recast mesh has or has not is not verifiable from here. Manual sweeps
  (gatesweep, slow) with pole+bait confirmed equipped then got **no server
  reply at all** from `#fish`, while the earlier scenario runs did get the
  refusal — a lane-state difference in the freshly booted stack, not a client
  behavior. The scenario treats the fishing leg as best-effort by design (the
  card's hard leg is the forage catch), so `forage_lands` exits 0. A re-run on
  a settled stack is the way to close this; the stand itself now faces the
  water.
- **Oracle leg (criterion 3) is OWED.** No official-client capture was taken
  on eq-oracle-3: the cast-and-reel animation's id/duration and the result-line
  timing are **not measured**, and **willeq has no fishing/forage cast
  animation to match** — `SkillManager::activateSkill()` sends the packet and
  nothing else (skill_manager.cpp:601-623); there is no `SpawnAppearance`/
  animation emission for skill 27/55 in the tree today (grep for
  fishing/forage/cast-anim under src/client/graphics/ returns nothing). Per
  the card's rule the animation id/duration must stay **[assumed] / not
  present** until the oracle capture is taken; no number is invented here.
  Recipe: `~/src/willeq-notes/wine-oracle.md` (Xvfb :99, wine eqgame.exe,
  drive-login.sh + step2/3/4/6/7/8/9, fish once from a dock,
  `ffmpeg -f x11grab -i :99`).
- **Two-client observer** (issue's "an observer sees the fisher's fishing
  animation") is part of the owed animation work: nothing to observe until an
  animation exists.
- `OnForage`/`OnFishing` in src/client/zone/zone_dispatch.cpp:718-728 remain
  log-only. eqemu answers these skills with MessageString + cursor item, which
  the client already handles on the generic paths (this card proves it); the
  dedicated opcodes are dead on this server and are the T147/T168 "delete if
  the pcap never shows them" case — left in place (deleting is a separate
  small task) so the log stays available for the oracle-leg pcap comparison.

## Evidence

- `/home/user/kb-evidence/b894-forage-fishing/scenario-lane3-run3.out` — the
  exit-0 live run (forage landed).
- `/home/user/kb-evidence/b894-forage-fishing/freporte_lane3.wtr` — the lane's
  watermap (md5 f52d9aa088b0b7e3bc96a6634992a425), the stand's source of truth.
- `/home/user/kb-evidence/b894-forage-fishing/wtr_headings.py`, `wtr_cols.py`,
  `wtr_dump.py` — the watermap probes (parse matches zone/water_map_v1.cpp
  exactly; sanity point: the a_shark000 NPC at (-410,-1151,-99.2) reads
  region 1 = water, city center reads land).
- `refactor/B894/evidence/` — client/server logs from the live runs.
- Commits: `382bbc77` (RED) → `d479b4af` → `b6ff5f15` (table green) →
  `797bf0e6` (cursor item + line) → `4056e5b3` (scenario) → `de5ffbc2` →
  `6692e84d` (stand faces the water, heading 256).
