# T428 — fable's night base matched to the oracle (B280)

Class A; eqemu lock (atomic; must be ABSENT); the slot that frees. Read
B280 (oracle night base R .01900 / G .03333 / B .02797, grey .02675;
fable R .00540 / G .00453 / B .00445, grey .00478 — 5.6x darker and a
different colour; every vision number is a ratio because of it), T408's
§Review (the night deficit is UNIFORM: ×2.27 lamp-free, ×2.09 by a lamp —
so it is the ambient, not a lamp term), T405's rig (heading pinned via
`#goto x y z h`, rain_chance zeroed, `#set time 22 0` re-pinned per
window, `#weather 3 1 0`), T423 (the ambient recomposes on any input;
`composeSceneAmbient(dayNight, weather, vision)`), and
`updateTimeOfDay()`'s night ambient (0.08, 0.08, 0.15) and its day/night
curve.

1. Live A/B on the night ambient's three channels (and, if the curve's
   shape matters, the dusk/dawn interpolation): probe builds, one
   parameter at a time, until fable's unbuffed night base matches the
   oracle's per channel within the noise floor at T405's spot AND at
   T367's lamp spot (both, so a lamp term is not being hidden). Every
   value carries its probe. Then re-run the vision ratios (T405's
   constants were solved against the OLD base — they may need re-solving:
   do it in the same session, same method).
2. Day must not move: the gate's zones are daytime — run the gate after
   each probe; if an image golden moves, STOP with the contact sheet (a
   night-base change that moves a daytime golden means the curve is
   coupled — report the coupling).
3. Tests pin the solved night base and the re-solved vision constants
   (pre-fix failing). Strike B280; annotate B067/B203.

Restore every server value (world DOWN); `down.sh`; delete the lock.

STOP: lock present; a daytime golden moves.

---

## Review

Executed in slot3, branch `task/T428-night-base`, base `6a826e8f`.
Full evidence, every table and all the media: **`review/T428/README.md`**.

### 1. The result

**The night ambient is `(0.26244, 0.31147, 0.24003)`**, solved live against the
official client's own **day/night ratio per channel** at the lamp-free spot, on
T408's geometry-only crop `560x110+200+350`:

| | R | G | B |
|---|---|---|---|
| oracle (three sessions, ≤0.15% apart) | 4.4540 | 3.6220 | 3.8090 |
| fable BEFORE `(0.08, 0.08, 0.15)` | 9.8411 (+121%) | 12.5683 (+247%) | 4.6454 (+22%) |
| **fable AFTER** | **4.4760 (+0.5%)** | **3.6025 (−0.5%)** | **3.7316 (−2.0%)** |

Every value carries its probe: six fable sessions and one oracle session, the
search log in `review/T428/README.md` §2. The knob is a new
`/nightambient <r> <g> <b>` so one session sweeps a candidate list, the same
lesson `/bakedlight` carries from T408.

The constants and the whole day/night curve now live in
`include/client/graphics/vision_ambient.h` (`kNightAmbient`, `kDayAmbient`,
`kDawnDuskDayAnchor`, the two sun intensities, and `dayNightAmbient()`), out of
the four-armed if-chain in `updateTimeOfDay()` that wrote the night triple three
times over.

### 2. The vision floors DID need re-solving, and the target moved too

Two independent reasons, both measurements:

* T405's Ultravision floor `(0.26545, 0.19653, 0.27623)` sits **below** the new
  night base on G, so leaving it would have made the buff a no-op there — the
  exact defect T405 itself found in the pre-T405 Infravision floor.
* T405's oracle ratios were taken on the `700x420+40+40` **world crop, which is
  half sky at that spot**, and the sky does not respond to a vision buff. That
  dilutes the ratio by a factor set by each client's own camera, so it is not a
  cross-client target. Re-measuring **T405's own two oracle videos** on the
  geometry band, plus a new session o3, gives Ultravision **x2.7995 / x1.9517 /
  x2.4946** (three sessions inside 1%) where T405 read x2.2531 / x1.4399 /
  x1.7687.

Re-solved live in the same session as the base, on the same measured curve:

| | R | G | B |
|---|---|---|---|
| fable Ultravision vs oracle | +0.9% | +1.2% | −4.0% |
| fable Infravision vs oracle | +3.0% | +1.6% | +3.0% |

**The floors do NOT hold from before — they moved by 2-3x** and are re-pinned in
`tests/test_vision_ambient.cpp`.

### 3. Froglok — B060 closed, one leg owed

Measured per channel on the oracle: **x2.6803 / x1.9478 / x2.1753**, its own
level (5.5% under Ultravision on B, 60% over Infravision there).
`VisionType::FroglokVision` carries it, appended as **3** because the enumerator
values are wire values — and `visionRank()` had to be introduced, or an
Ultravision buff would have been a silent no-op on a Froglok. **fable's own
confirmation is OWED**: `#race` on the player's own character never reaches
`setPlayerRace()` (new board **B448**), so the floor is solved from the same
measured curve and bracketed by the two floors that WERE confirmed live.

### 4. Day did not move, and that is structural

The visual gate PASSES and **no image golden moved**. The day arm of the curve
does not read the night triple at all, which
`NightBaseSolvedT428Test.TheDayArmDoesNotReadTheNightBase` asserts rather than
asserts about. The lamp spot's day reading reproduces T408's to 0.2%.

### 5. Deviations and new defects — flagged, not absorbed

1. **B445** — the ambient is uploaded to the shader TWICE, the second copy
   normalised to its own largest channel, so its chromaticity is applied
   squared. Raising one night channel measurably darkens the other two (R+0.16
   → G −9.4%, B −16%). **This is the root cause of T405's "the three channels
   are COUPLED"**, which that unit pinned in prose and could not explain. T428
   solved everything WITH it in place; removing it invalidates all seven
   constants, so it is a unit of its own.
2. **B446** — the plan asked for both spots "so a lamp term is not being
   hidden". One is. The committed base takes the lamp spot from +82/+256/+43%
   to **+34/+71/+32%** but does not match, because at that spot the non-ambient
   term is half the frame. No ambient triple satisfies both spots.
3. **B447** — the re-solved Ultravision floor is 2.6-2.8% above the day ambient
   on R and G, so Ultravision now lifts daylight ~2.5% (measured: x1.0239 /
   x1.0259 / x0.9414). Two `test_vision_ambient` assertions were REFUTED by the
   measurement and rewritten to say what is now true. The oracle side is
   unmeasured: its own day control drifted 30-48% in four minutes.
4. **B448** — `#race` on self never reaches the vision system.
5. **(fixed on fable: DocCitations)** — `DocCitations` is RED at the base tip and is not this branch:
   `BOARD.md:6877` (T433's B428) cites an eqemu path.
6. Rig fix outside the repo: `/mnt/projects/eq-oracle/scripts/enter-world.sh`
   clicked "Enter World" at y 624, fifteen pixels low (found by T433). Now
   y 609; session o3 entered the world on attempt 1.
7. `docker restart` of the eqemu container is NOT a substitute for `up.sh` — it
   brings `world` and the zones back but not `loginserver`, and cost one fable
   session.

### 6. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest -j6` | **4,470 / 4,471**; the one red is `DocCitations`, pre-existing at the base tip ((fixed on fable: DocCitations)), verified standalone |
| `build-gles2-x86.sh` | exit 0 |
| visual gate | **PASS**, no golden touched |
| `build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 37 checked, 37 ok, 0 fail |
| tests pre-fix | 3 of 7 red in `test_night_ambient`, 6 of 28 red in `test_vision_ambient` |
| tests post-fix | 7/7 and 28/28 |

Server state restored with the world DOWN and verified by a full `diff`
(`character_data`, `zone`, `eqtime`, `character_corpses` all IDENTICAL);
`down.sh` exit 0; the lock is deleted.
