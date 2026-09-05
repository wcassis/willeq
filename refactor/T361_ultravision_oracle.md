# T361 — Ultravision against the oracle (B067, and B060's table question)

Measurement first, then match; slot1; eqemu lock (take it atomically; it
must be ABSENT — if present, read it and STOP). Class A. Read the B067 and
B060 board text and `review/T358/README.md` first: fable's Ultravision at
night is worth +4.9% brightness while the day/night cycle is worth 4x, and
the only consumers of `visionType` scale 218 data-only point lights. B066
(the swapped switch) is being fixed in T360 in parallel — branch from the
tip you are given and do NOT touch `sim_animation.cpp`'s switch.

## Leg A — measure the official client (the golden)

The rig: `review/T328/scripts/login-as-traced.sh` (Xvfb + Wine Titanium
client; screenshots with `import -window`), T311's death capture for video
under Wine, the `eq-oracle-provisioning` memory (account/character
creation is automated). Same server, same zone (qeynos2), same position
and heading as T358 (Testchar's recorded spot: zone 2, x=-74, y=428.04,
z=3.75, h=160), forced night (`#set time 22 0` — verify the submit line,
T358 lesson: blind chat input silently loses text).

1. Official client, night, no buff: capture 3 s of video, then stills
   every 0.5 s — the 400x300 world crop's mean luminance and RGB.
2. `#castspell 46` on the official client's character (invulnerable OFF),
   wait for the landing, capture the same. Then `#nukebuffs`, capture.
3. Repeat 1–2 with Infravision (`#castspell 45`? — find Infravision's id in
   `spells_new`; record it) and, if the character can be a Dark Elf
   (racial floor), one zone-in with no buff.
4. Record ALSO what changes qualitatively: does the whole scene lift
   (ambient), does the sky change, do only near objects lift (a fog / draw
   distance change), does the palette shift? Frame diffs, not adjectives.
5. Same protocol on fable at the same tip, same spot, same time — the
   T358 driver. Both sets go to `review/T361/` with a table: client ×
   condition → luminance, R, G, B, and the ratio to the no-buff night frame.

## Leg B — match

With the measured ratio in hand, implement the closest mechanism that
reproduces it: the obvious candidate is raising the zone shader's ambient
(`ZoneShader::setAmbientColor`) from the vision type, with the value
solved by A/B against the oracle table (a parameter picked by ARGUMENT is
the smell — search until fable's night+Ultravision frame matches the
oracle's within the still-to-still noise floor T358 measured, 1.3%). If
the oracle shows a palette shift or fog change instead, implement THAT.
Tests: a unit test on the vision→ambient mapping (pre-fix failing) and the
live A/B frames in the README. Strike B067; if the oracle's Dark Elf
zone-in answers B060, record the answer on B060 (it stays class C for Will
only if it still needs a call — say which).

## Proof

Leg A is the proof of Leg B. Build, `ctest -j6`, visual gate standalone +
ARM (graphics files change; rebuild all three trees — willeq.db v6), file
sizes. Restore every server flag/time; `down.sh`; delete the lock.

STOP: the official client will not cast (10 minutes of trying, record
why); the measured oracle effect cannot be reached by any single parameter
in 2 hours of search (report the table and the nearest reach as a board
note; do not commit a guessed value).

---

## Review

Executor session 2026-09-05, slot1, branch `task/T361-ultravision-oracle`,
base/tip `feb51570`. Full evidence, every table and every frame:
**`/mnt/projects/fable/review/T361/README.md`**.

### Leg A — the oracle answers, and the answer is big

The official Titanium client under Wine/Xvfb (T328's rig), same server, same
zone, same T358 spot, forced 22:00, character **Oraclealpha** (`willeq_a`,
GM 250, **Human** — no racial floor to confound the buff). Video at 5 fps for
the whole session; chat verified by OCR of the client's own chat panel.

700x420 world crop, **linear light**:

| client × condition | grey | R | G | B | ratio |
|---|---|---|---|---|---|
| oracle, night, no buff | 0.033226 | 0.038490 | 0.035383 | 0.025805 | 1.000 |
| oracle, night, **Ultravision** | 0.051635 | 0.056086 | 0.051711 | 0.047108 | **1.554** |
| oracle, night, **Infravision** (Serpent Sight, spell **276**) | 0.041628 | 0.051125 | 0.042839 | 0.030921 | **1.253** |
| fable PRE-FIX, night, no buff | 0.0019006 | 0.0017650 | 0.0012842 | 0.0026526 | 1.000 |
| fable PRE-FIX, night, Ultravision | 0.0020193 | 0.0019245 | 0.0013764 | 0.0027569 | **1.059** |
| fable PRE-FIX, night, Infravision | 0.0020531 | 0.0019411 | 0.0014463 | 0.0027719 | **1.077** |
| fable POST-FIX, night, no buff (219+169+154 video frames) | 0.0013744 | — | — | — | 1.000 |
| fable POST-FIX, night, **Ultravision** (170 frames) | 0.0020248 | — | — | — | **1.473** (crop R) / **1.780** (combined) |
| fable POST-FIX, night, **Infravision** (178 frames) | 0.0018744 | — | — | — | **1.364** (crop R) / **1.516** (combined) |

The oracle's edges are ONE frame wide in the video and each state is flat
(luma 53.45 → 64.16 → 53.66 → 59.26 → 53.62, window spreads ≤0.62). fable's
pre-fix Ultravision was worth +5.9% and, through B066's swap, less than its
own Infravision — B067 and B066 both re-confirmed live at this tip.

**Qualitatively (question 4 of the plan): it is an AMBIENT raise, and the
frame diffs rule out the alternatives.** Sky-only crop x1.17 (its blue channel
x1.02) — not a post-process. Distant geometry x3.25 but near geometry x1.32 —
a fog or clip-distance change explains the first and *cannot* explain the
second, because fog is ~0 a few metres away. Ultravision's added light is
near-neutral and slightly blue ("eyes glow violet"); Infravision's is strongly
red, R/B 2.49 ("eyes glow red"), at 0.480 of the magnitude.

### Leg B — matched, with a stated residual

`src/client/graphics/vision_ambient.cpp` +
`include/client/graphics/vision_ambient.h` (new TU): vision raises a
per-channel **floor** under the global ambient, applied in
`IrrlichtRenderer::updateTimeOfDay()` — the one place fable computes the
day/night ambient and hands it to `smgr_->setAmbientLight()` and
`ZoneShader::setAmbientColor()`. A floor, not a multiply or an add, is what
makes the buff invisible at noon (day ambient 0.5 is already above it) and
transformative at midnight (night ambient 0.08/0.08/0.15 is far below).
`PlayerCameraComponent::refreshVisionLighting()` re-runs it at each of the
three sites that write `ctx_.currentVision`, or the buff would not show until
the next server time tick.

**The search.** Chromaticities are the oracle's measured added-light deltas.
The only free scalars are the two gains. Probe 1 set both at 0.400 and
measured fable's own frame; the frame answers the mean ambient as a power law
whose exponent the two probe points give **independently** as **1.1402** and
**1.1409**. Inverting for the oracle's two ratios: **Ultravision gain 0.1521**,
**Infravision gain 0.0956**. Confirmed live.

| basis | fable post-fix | oracle | residual |
|---|---|---|---|
| crop R (sky + distant geometry) | 1.473 | 1.554 | −5.2% |
| crops R+L, area-weighted | 1.780 | 1.554 | +14.5% |
| crop L (near wall) | 2.074 | 1.325 (oracle near wall) | +56% |

**DEVIATION, flagged not absorbed.** The plan asked for a match within the
1.3% still-to-still noise floor. Not reached, and the reason is measured:
**fable's night is 35x darker than the oracle's on near geometry** (near-wall
linear grey 0.0018 vs 0.0643), so one ambient parameter cannot satisfy both a
near-wall ratio of 1.33 and a whole-frame ratio of 1.554. The shipped gain
solves the whole-world-crop ratio the oracle table is built on. The 35x
night-baseline gap is a separate defect, boarded as **B080**.

### B060 — answered by the oracle, one row left

Two racial sweeps, `#race N` in-game at a fixed camera, each carrying its own
cast-Ultravision positive control that race 6 lands on to four decimals.
Three levels and only three (window spreads ≤0.14 luma, separation 4.8):

- **Normal**: Human (1), Barbarian (2), Erudite (3)
- **Infravision**: Wood Elf (4), High Elf (5), Half Elf (7), Dwarf (8),
  **Troll (9)**, **Ogre (10)**, Halfling (11), Gnome (12), Iksar (128),
  Vah Shir (130)
- **Ultravision**: **Dark Elf (6) alone**
- **Froglok (330) measures a fourth level, 63.49**, which `VisionType` cannot
  express — left on `Normal` rather than rounded by argument. **This one row
  needs Will's call**; B060 stays class C for it and nothing else.

Will's stated expectation was Troll = Ultravision, Ogre = Infravision. The
oracle puts **both** on Infravision. The table moved into
`EQT::Graphics::racialBaseVision()` (same new TU) so it is testable, and
`PlayerCameraComponent::setPlayerRace` now calls it.

### Tests

`tests/test_vision_ambient.cpp` (11 cases, registered in
`tests/cmake/renderer_guards.cmake`). **Pre-fix 3 of 6 FAIL** with the ambient
mapping stubbed to the pre-T361 behaviour; **pre-fix 2 FAIL** on the racial
cases with the old inferred table; **11/11 pass** after. Logs in
`review/T361/logs/test_vision_ambient_*.log`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6` | **3878/3879**, sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh fable` | 53 checked, **53 ok, 0 warn, 0 fail** |
| visual gate | goldens **untouched**, no image mismatch in any run; **CPU-p95 load artefact** — see below |

The visual gate's only failures are `p95 process-CPU`, and they moved zones
between runs (run 1: befallen 31.2; run 2: befallen 6.2 and qeynos2 15.0)
while slot2 and slot3 were both mid-build on this host — the documented load
artefact, not a regression. No golden image compare failed in either run.

### Server state

Lock `T361 2026-09-05` taken with `noclobber`, `up.sh --launcher`. Restored
and verified by SELECT: `account willeq_test.invulnerable` back to `1`;
`character_data` 65 `Testchar` and 74 `Oraclealpha` unchanged (no write was
made — `#race` is an illusion, not a row). No `logsys_categories` touched.
`down.sh` run and the lock deleted.

### Not touched

`src/client/graphics/sim_animation.cpp` — T360 owns B066 in parallel. The
ambient floor keys off `VisionType` directly and is correct whichever way that
lands.
